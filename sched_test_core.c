// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021-2022 Xilinx, Inc.
 * Authors:
 *     Sonal Santan <sonal.santan@xilinx.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/gpu_scheduler.h>

#include "sched_test_common.h"

#define str(x) #x

static const char *sched_test_queue_name(const enum sched_test_queue qu)
{
	switch (qu) {
	case SCHED_TSTQ_A:
		return str(SCHED_TSTQ_A);
	case SCHED_TSTQ_B:
		return str(SCHED_TSTQ_B);
	default:
		return "SCHED_TSTQ_??";
	}
}

static const char *sched_test_hw_queue_name(const enum sched_test_queue qu)
{
	switch (qu) {
	case SCHED_TSTQ_A:
		return "HW_TSTQ_A";
	case SCHED_TSTQ_B:
		return "HW_TSTQ_B";
	default:
		return "HW_TSTQ_??";
	}
}

static const char *sched_test_fence_get_driver_name(struct dma_fence *fence)
{
	const struct sched_test_fence *f = to_sched_test_fence(fence);
	return f->sdev->drm.driver->name;
}

static const char *sched_test_fence_get_timeline_name(struct dma_fence *fence)
{
	const struct sched_test_fence *f = to_sched_test_fence(fence);
	return sched_test_queue_name(f->qu);
}

/*
 * The IRQ fence is released either by:
 * 1.  drm_sched_entity_fini() as part of entity tear down when an application
 *     attempts to close its device handle "after" finishing wait on all the
 *     submitted jobs. The last irq fence is released this way.
 *
 * 2.  sched_test_job_fini() which is called
 * 2.a when the application finishes wait on a submitted job
 * 2.b when the application attempts to close the device handle without calling
 *     wait on submitted jobs, sched_test_postclose does the cleanup.
 */
void sched_test_fence_release(struct dma_fence *fence)
{
	struct sched_test_fence *sfence = to_sched_test_fence(fence);
	DRM_DEBUG_DRIVER("Freeing fence object %p", sfence);
	//dump_stack();
	dma_fence_free(fence);
}


const struct dma_fence_ops sched_test_fence_ops = {
	.get_driver_name = sched_test_fence_get_driver_name,
	.get_timeline_name = sched_test_fence_get_timeline_name,
	.release = sched_test_fence_release,
};

/*
 * Custom routine for IRQ fence creation
 */
static struct dma_fence *sched_test_fence_create(struct sched_test_device *sdev, enum sched_test_queue qu)
{
	struct sched_test_fence *fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return ERR_PTR(-ENOMEM);

	fence->sdev = sdev;
	fence->qu = qu;
	fence->seqno = ++sdev->queue[qu].emit_seqno;
	dma_fence_init(&fence->base, &sched_test_fence_ops, &sdev->hwemu[qu]->job_lock,
		       sdev->queue[qu].fence_context, fence->seqno);

	return &fence->base;
}

/*
 * HW emulation model uses a queue of event objects
 */
struct event {
	struct list_head lh;
	/* Job object added by the scheduler */
	struct sched_test_job *job;
	/* Used to signal termination of HW emulation thread */
	bool stop;
	/* Currently unused */
	int seq;
};

/*
 * Called by the HW emulation thread to process the next job in the queue
 */
static struct event *dequeue_next_event(struct sched_test_hwemu *arg)
{
    struct event *e = NULL;

    spin_lock(&arg->events_lock);
    if (!list_empty(&arg->events_list)) {
	    e = list_first_entry(&arg->events_list, struct event, lh);
	    if (e)
		    list_del(&e->lh);
    }
    spin_unlock(&arg->events_lock);
    return e;
}

/*
 * Called by the scheduler thread to add the next job to the queue
 */
static void enqueue_next_event(struct event *e, struct sched_test_hwemu *arg)
{
	static int seq = 0;
	e->seq = seq++;
	spin_lock(&arg->events_lock);
	list_add_tail(&e->lh, &arg->events_list);
	spin_unlock(&arg->events_lock);
	wake_up(&arg->wq);
}

/*
 * Core loop of the HW emulation thread
 */
static int sched_test_thread(void *data)
{
	struct sched_test_hwemu *arg = data;

	while (!kthread_should_stop()) {
		int ret = 0;
		struct event *e = NULL;
		wait_event_interruptible(arg->wq, ((e = dequeue_next_event(arg)) ||
						   kthread_should_stop()));
		if (e->stop) {
			drm_info(&arg->dev->drm, "HW breaking out of kthread loop");
			break;
		}
		ret = dma_fence_signal(e->job->irq_fence);
		arg->count++;
	}
	return 0;
}


static int sched_test_hwemu_thread_start(struct sched_test_device *sdev, enum sched_test_queue qu)
{
	struct sched_test_hwemu *arg = kzalloc(sizeof(struct sched_test_hwemu), GFP_KERNEL);
	int err = 0;

	if (!arg)
		return -ENOMEM;
	sdev->hwemu[qu] = arg;

	arg->dev = sdev;
	arg->qu = qu;

	init_waitqueue_head(&arg->wq);
	spin_lock_init(&arg->events_lock);
	spin_lock_init(&arg->job_lock);
	INIT_LIST_HEAD(&arg->events_list);
	arg->hwemu_thread = kthread_run(sched_test_thread, arg, sched_test_hw_queue_name(arg->qu));

	drm_info(&sdev->drm, "HW emulation thread start %s %p", sched_test_queue_name(qu),
		 sdev->hwemu[qu]->hwemu_thread);
	if(IS_ERR(arg->hwemu_thread)) {
		drm_err(&sdev->drm, "create %s", sched_test_hw_queue_name(arg->qu));
		err = PTR_ERR(arg->hwemu_thread);
		arg->hwemu_thread = NULL;
		goto out_free;
	}
	drm_info(&sdev->drm, "HW emulation queue %s", sched_test_queue_name(arg->qu));
	return 0;
out_free:
	kfree(arg);
	sdev->hwemu[qu] = NULL;
	return err;
}

static int sched_test_hwemu_thread_stop(struct sched_test_device *sdev, enum sched_test_queue qu)
{
	struct event *e;
	int ret;

	drm_info(&sdev->drm, "HW emulation thread stop request %s %p", sched_test_queue_name(qu),
		 sdev->hwemu[qu]->hwemu_thread);
	if (!sdev->hwemu[qu]->hwemu_thread)
		return 0;

	e = kzalloc(sizeof(struct event), GFP_KERNEL);
	e->stop = true;
	enqueue_next_event(e, sdev->hwemu[qu]);
	ret = kthread_stop(sdev->hwemu[qu]->hwemu_thread);
	sdev->hwemu[qu]->hwemu_thread = NULL;
	drm_info(&sdev->drm, "HW emulation thread %s stopped, processed %ld jobs", sched_test_hw_queue_name(qu),
		 sdev->hwemu[qu]->count);
	kfree(sdev->hwemu[qu]);
	sdev->hwemu[qu] = NULL;
	return ret;
}

int sched_test_hwemu_threads_start(struct sched_test_device *sdev)
{
	int result = sched_test_hwemu_thread_start(sdev, SCHED_TSTQ_A);
	if (result)
		return result;
	result = sched_test_hwemu_thread_start(sdev, SCHED_TSTQ_B);
	if (result)
		sched_test_hwemu_thread_stop(sdev, SCHED_TSTQ_A);
	return result;
}

int sched_test_hwemu_threads_stop(struct sched_test_device *sdev)
{
	enum sched_test_queue i;
	for (i = SCHED_TSTQ_MAX; i > 0;) {
		sched_test_hwemu_thread_stop(sdev, --i);
	}
	return 0;
}

int sched_test_job_init(struct sched_test_job *job, struct sched_test_file_priv *priv)
{
	int err = drm_sched_job_init(&job->base, &priv->entity[job->qu], NULL);

	if (err)
		return err;

	job->sdev = priv->sdev;
	drm_sched_job_arm(&job->base);
//	DRM_INFO("job %p done_fence %p refcount %d -- A", job, &job->base.s_fence->finished,
//		 kref_read(&job->base.s_fence->finished.refcount));
	/*
	 * Obtain our reference to scheduler's job done fence, we will wait on it later
	 * if/when the client process waits for the job completion
	 */
	job->done_fence = dma_fence_get(&job->base.s_fence->finished);
	drm_info(&priv->sdev->drm, "After done_fence...");
//	DRM_INFO("job %p done_fence %p refcount %d -- B", job, job->done_fence,
//		 kref_read(&job->done_fence->refcount));
//	drm_sched_entity_push_job(&job->base, &priv->entity[job->qu]);
	//drm_sched_entity_push_job(&job->base);
	drm_info(&priv->sdev->drm, "Done job init...");
	return err;
}

void sched_test_job_fini(struct sched_test_job *job)
{
//	dma_fence_put(job->in_fence);
//	DRM_INFO("job %p done_fence %p refcount %d -- C", job, job->done_fence,
//		 kref_read(&job->done_fence->refcount));
	dma_fence_put(job->done_fence);
	drm_info(&job->sdev->drm, "Done job fini...");
}

/*
static struct dma_fence *sched_test_job_dependency(struct drm_sched_job *sched_job,
						   struct drm_sched_entity *sched_entity)
{
	struct sched_test_job *job = to_sched_test_job(sched_job);
	DRM_DEBUG_DRIVER("job %p done_fence %p refcount %d", job, job->done_fence,
			 kref_read(&job->done_fence->refcount));
	return job->in_fence;
}
*/

static struct dma_fence *sched_test_job_run(struct drm_sched_job *sched_job)
{
	struct sched_test_job *job = to_sched_test_job(sched_job);
	struct dma_fence *irq_fence = NULL;
	struct event *e = NULL;

	if (unlikely(job->base.s_fence->finished.error))
		return NULL;

	e = kzalloc(sizeof(struct event), GFP_KERNEL);
	if (!e)
		return NULL;
	/* Creates the fence and also adds a reference for our use */
	irq_fence = sched_test_fence_create(job->sdev, job->qu);
	if (IS_ERR(irq_fence))
		goto out_free;

	/* Get another reference for the scheduler thread */
	job->irq_fence = dma_fence_get(irq_fence);
	e->job = job;
	e->stop = false;
	enqueue_next_event(e, job->sdev->hwemu[job->qu]);
//	DRM_INFO("job %p done_fence %p refcount %d -- D", job, job->done_fence,
//		 kref_read(&job->done_fence->refcount));
	return job->irq_fence;

out_free:
	kfree(e);
	return NULL;
}

static enum drm_gpu_sched_stat sched_test_job_timedout(struct drm_sched_job *sched_job)
{
	return DRM_GPU_SCHED_STAT_NOMINAL;
}


static void sched_test_job_free(struct drm_sched_job *sched_job)
{
	struct sched_test_job *job = to_sched_test_job(sched_job);

	/* Done with the irq_fence, release it */
	dma_fence_put(job->irq_fence);
	job->irq_fence = NULL;
	drm_sched_job_cleanup(sched_job);
	sched_test_job_fini(job);
	kfree(job);
}

static const struct drm_sched_backend_ops sched_test_regular_ops = {
	.run_job = sched_test_job_run,
	.timedout_job = sched_test_job_timedout,
	.free_job = sched_test_job_free,
};

static const struct drm_sched_backend_ops sched_test_fast_ops = {
	.run_job = sched_test_job_run,
	.timedout_job = sched_test_job_timedout,
	.free_job = sched_test_job_free,
};

int sched_test_sched_init(struct sched_test_device *sdev)
{
	int hw_jobs_limit = 16;
	int job_hang_limit = 0;
	int hang_limit_ms = 500;
	int ret;

	ret = drm_sched_init(&sdev->queue[SCHED_TSTQ_A].sched,
			     &sched_test_regular_ops,
			     hw_jobs_limit, job_hang_limit,
			     msecs_to_jiffies(hang_limit_ms),
			     NULL, NULL, sched_test_queue_name(SCHED_TSTQ_A), sdev->drm.dev);
	if (ret) {
		drm_err(&sdev->drm, "Failed to create %s scheduler: %d", sched_test_queue_name(SCHED_TSTQ_A), ret);
		return ret;
	}

	ret = drm_sched_init(&sdev->queue[SCHED_TSTQ_B].sched,
			     &sched_test_fast_ops,
			     hw_jobs_limit, job_hang_limit,
			     msecs_to_jiffies(hang_limit_ms),
			     NULL, NULL, sched_test_queue_name(SCHED_TSTQ_B), sdev->drm.dev);
	if (ret) {
		drm_err(&sdev->drm, "Failed to create %s scheduler: %d", sched_test_queue_name(SCHED_TSTQ_B),
			ret);
		sched_test_sched_fini(sdev);
		return ret;
	}

	return 0;
}

void sched_test_sched_fini(struct sched_test_device *sdev)
{
	enum sched_test_queue i;
	for (i = SCHED_TSTQ_MAX; i > 0;) {
		if (sdev->queue[--i].sched.ready)
			drm_sched_fini(&sdev->queue[i].sched);
	}
}
