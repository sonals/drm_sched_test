// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Xilinx, Inc.
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

void sched_test_fence_release(struct dma_fence *fence)
{
	struct sched_test_fence *sfence = to_sched_test_fence(fence);
	//drm_info(&sfence->sdev->drm, "Freeing fence object %p", sfence);
	dma_fence_free(fence);
}


const struct dma_fence_ops sched_test_fence_ops = {
	.get_driver_name = sched_test_fence_get_driver_name,
	.get_timeline_name = sched_test_fence_get_timeline_name,
	.release = sched_test_fence_release,
};

struct dma_fence *sched_test_fence_create(struct sched_test_device *sdev, enum sched_test_queue qu)
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

static void
sched_test_job_free_lambda(struct kref *ref)
{
	struct sched_test_job *job = container_of(ref, struct sched_test_job,
						  refcount);
	DRM_DEBUG_DRIVER("Freeing job object %p", job);
//	drm_info(&job->sdev->drm, "Freeing job object %p", job);
	kfree(job);
}

struct event {
	struct list_head lh;
	struct sched_test_job *job;
	bool stop;
	int seq;
};

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

static void enqueue_next_event(struct event *e, struct sched_test_hwemu *arg)
{
	static int seq = 0;
	e->seq = seq++;
	spin_lock(&arg->events_lock);
	list_add_tail(&e->lh, &arg->events_list);
	spin_unlock(&arg->events_lock);
	wake_up(&arg->wq);
}


static int sched_test_thread(void *data)
{
	int i = 0;
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
		i++;
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

	drm_info(&sdev->drm, "Emulated HW thread start %s %p", sched_test_queue_name(qu), sdev->hwemu[qu]->hwemu_thread);
	if(IS_ERR(arg->hwemu_thread)) {
		drm_err(&sdev->drm, "create %s", sched_test_hw_queue_name(arg->qu));
		err = PTR_ERR(arg->hwemu_thread);
		arg->hwemu_thread = NULL;
		goto out_free;
	}
	drm_info(&sdev->drm, "Emulated HW queue %s", sched_test_queue_name(arg->qu));
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

	drm_info(&sdev->drm, "Emulated HW thread stop %s %p", sched_test_queue_name(qu), sdev->hwemu[qu]->hwemu_thread);
	if (!sdev->hwemu[qu]->hwemu_thread)
		return 0;

	e = kzalloc(sizeof(struct event), GFP_KERNEL);
	e->stop = true;
	enqueue_next_event(e, sdev->hwemu[qu]);
	ret = kthread_stop(sdev->hwemu[qu]->hwemu_thread);
	sdev->hwemu[qu]->hwemu_thread = NULL;
	kfree(sdev->hwemu[qu]);
	sdev->hwemu[qu] = NULL;
	drm_info(&sdev->drm, "stop %s", sched_test_hw_queue_name(qu));
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
	int err = drm_sched_job_init(&job->base, &priv->entity, NULL);

	if (err)
		return err;

	/* kref_init will add a reference to this job */
	kref_init(&job->refcount);
	job->free = sched_test_job_free_lambda;
	job->sdev = priv->sdev;
//	int count = kref_read(&job->base.s_fence->finished.refcount);
//	DRM_DEBUG_DRIVER("job %p done_fence %p refcount %d", job, &job->base.s_fence->finished, count);
	job->done_fence = dma_fence_get(&job->base.s_fence->finished);
	int count = kref_read(&job->done_fence->refcount);
	DRM_DEBUG_DRIVER("job %p done_fence %p refcount %d", job, job->done_fence, count);
	drm_sched_entity_push_job(&job->base, &priv->entity);
	return err;
}

void sched_test_job_fini(struct sched_test_job *job)
{
//	int count = kref_read(&job->in_fence->refcount);
//	drm_info(&job->sdev->drm, "in_fence %p refcount %d", job, count);

	dma_fence_put(job->in_fence);
	DRM_DEBUG_DRIVER("job %p entity->last_scheduled %p", job, job->base.entity->last_scheduled);
	int count = kref_read(&job->done_fence->refcount);
	DRM_DEBUG_DRIVER("job %p done_fence %p refcount %d", job, job->done_fence, count);
	dma_fence_put(job->done_fence);
	kref_put(&job->refcount, job->free);

}


static struct dma_fence *sched_test_job_dependency(struct drm_sched_job *sched_job,
						   struct drm_sched_entity *sched_entity)
{
	struct sched_test_job *job = to_sched_test_job(sched_job);
//	int count = kref_read(&job->done_fence->refcount);
//	DRM_DEBUG_DRIVER("job %p done_fence %p refcount %d", job, job->done_fence, count);
	return job->in_fence;
}


static struct dma_fence *sched_test_job_run(struct drm_sched_job *sched_job)
{
	struct sched_test_job *job = to_sched_test_job(sched_job);
	struct dma_fence *fence = NULL;
	struct event *e = NULL;

	if (unlikely(job->base.s_fence->finished.error))
		return NULL;

	e = kzalloc(sizeof(struct event), GFP_KERNEL);
	if (!e)
		return NULL;
	/* Creates the fence and also adds a reference */
	fence = sched_test_fence_create(job->sdev, job->qu);
	if (IS_ERR(fence))
		goto out_free;

	job->irq_fence = dma_fence_get(fence);
	e->job = job;
	e->stop = false;
	kref_get(&job->refcount);
	enqueue_next_event(e, job->sdev->hwemu[job->qu]);
	int count = kref_read(&job->done_fence->refcount);
	DRM_DEBUG_DRIVER("job %p done_fence %p refcount %d", job, job->done_fence, count);
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
	DRM_DEBUG_DRIVER("ENTER JOB %p", job);
//	int count = kref_read(&job->irq_fence->refcount);
//	DRM_DEBUG_DRIVER("job %p irq_fence %p refcount %d", job, job->irq_fence, count);
	/* Done with the irq_fence, release it */
	dma_fence_put(job->irq_fence);
	job->irq_fence = NULL;
	DRM_DEBUG_DRIVER("job %p entity->last_scheduled %p", job, job->base.entity->last_scheduled);
	DRM_DEBUG_DRIVER("job %p done_fence %p", job, job->done_fence);
	int count = kref_read(&job->done_fence->refcount);
	DRM_DEBUG_DRIVER("job %p done_fence %p refcount %d", job, job->done_fence, count);
	drm_sched_job_cleanup(sched_job);
//	count = kref_read(&job->done_fence->refcount);
//	DRM_DEBUG_DRIVER("job %p done_fence %p refcount %d", job, job->done_fence, count);
	kref_put(&job->refcount, job->free);
	DRM_DEBUG_DRIVER("EXIT JOB %p", job);
}

static const struct drm_sched_backend_ops sched_test_regular_ops = {
	.dependency = sched_test_job_dependency,
	.run_job = sched_test_job_run,
	.timedout_job = sched_test_job_timedout,
	.free_job = sched_test_job_free,
};

static const struct drm_sched_backend_ops sched_test_fast_ops = {
	.dependency = sched_test_job_dependency,
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
			     NULL, sched_test_queue_name(SCHED_TSTQ_A));
	if (ret) {
		drm_err(&sdev->drm, "Failed to create %s scheduler: %d", sched_test_queue_name(SCHED_TSTQ_A), ret);
		return ret;
	}

	ret = drm_sched_init(&sdev->queue[SCHED_TSTQ_B].sched,
			     &sched_test_fast_ops,
			     hw_jobs_limit, job_hang_limit,
			     msecs_to_jiffies(hang_limit_ms),
			     NULL, sched_test_queue_name(SCHED_TSTQ_B));
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
