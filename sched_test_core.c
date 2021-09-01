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

DECLARE_WAIT_QUEUE_HEAD(wq);

// list events to be processed by kernel thread
struct list_head events_list;
spinlock_t events_lock;


// structure describing the event to be processed
struct event {
	struct list_head lh;
	struct dma_fence *fence;
	struct sched_test_device *sdev;
	bool stop;
	int seq;
};

static struct event *pop_next_event(struct sched_test_hwemu_thread *thread_arg)
{
    struct event *e = NULL;
    static int count = 0;

    spin_lock(&events_lock);
    if (!list_empty(&events_list)) {
	    e = list_first_entry(&events_list, struct event, lh);
	    if (e)
		    list_del(&e->lh);
    }
    spin_unlock(&events_lock);
    drm_info(&thread_arg->dev->drm, "Popped event %p, sdev %p seq %d", e, (e ? e->sdev : NULL), (e ? e->seq : 0xffffffff));
    return e;
}

static void push_next_event(struct event *e)
{
	static int seq = 0;
	e->seq = seq++;
	drm_info(&e->sdev->drm, "Pushing event %p, sdev %p, seq %d", e, e->sdev, e->seq);
	spin_lock(&events_lock);
	list_add_tail(&e->lh, &events_list);
	spin_unlock(&events_lock);
	wake_up(&wq);
}


static int sched_test_thread(void *data)
{
	int i = 0;
	struct sched_test_hwemu_thread *thread_arg = data;

	while (!kthread_should_stop()) {
		struct event *e = NULL;
		drm_info(&thread_arg->dev->drm, "Loop %d waiting for event", i);
//		msleep_interruptible(thread_arg->interval);
		wait_event_interruptible(wq, ((e = pop_next_event(thread_arg)) ||
					      kthread_should_stop()));
		if ((e == NULL) || e->stop) {
			drm_info(&thread_arg->dev->drm, "breaking out of kthread loop");
			break;
		}
		dma_fence_signal_locked(e->fence);
		i++;
	}
	drm_info(&thread_arg->dev->drm, "%s exit", sched_test_hw_queue_name(thread_arg->qu));
	return 0;
}

int sched_test_hwemu_thread_start(struct sched_test_device *sdev)
{
	int err = 0;
	struct sched_test_hwemu_thread *arg = kzalloc(sizeof(struct sched_test_hwemu_thread), GFP_KERNEL);
	if (!arg)
		return -ENOMEM;
	arg->dev = sdev;
	arg->interval = 1;
	arg->qu = SCHED_TSTQ_A;

	spin_lock_init(&events_lock);

	drm_info(&sdev->drm, "init %s", sched_test_queue_name(arg->qu));

	INIT_LIST_HEAD(&events_list);
	sdev->hwemu_thread = kthread_run(sched_test_thread, arg, sched_test_hw_queue_name(arg->qu));

	if(IS_ERR(sdev->hwemu_thread)) {
		drm_err(&sdev->drm, "create %s", sched_test_hw_queue_name(arg->qu));
		err = PTR_ERR(sdev->hwemu_thread);
		sdev->hwemu_thread = NULL;
		return err;
	}
	return 0;
}

int sched_test_hwemu_thread_stop(struct sched_test_device *sdev)
{
	struct event *e;
	int ret;

	if (!sdev->hwemu_thread)
		return 0;

	e = kzalloc(sizeof(struct event), GFP_KERNEL);
	e->stop = true;
	e->sdev = sdev;
	push_next_event(e);
	ret = kthread_stop(sdev->hwemu_thread);
	sdev->hwemu_thread = NULL;

	drm_info(&sdev->drm, "stop %s", sched_test_hw_queue_name(SCHED_TSTQ_A));
	return ret;
}


int sched_test_job_init(struct sched_test_job *job, struct sched_test_file_priv *priv)
{
	int err = drm_sched_job_init(&job->base, &priv->entity, NULL);

	if (err)
		return err;

	job->sdev = priv->sdev;
	job->fence = dma_fence_get(&job->base.s_fence->finished);
	drm_info(&job->sdev->drm, "Ready to push job %p on entity %p", job, &priv->entity);
	drm_sched_entity_push_job(&job->base, &priv->entity);

	return err;
}

void sched_test_job_fini(struct sched_test_job *job)
{
	dma_fence_put(job->fence);
	drm_sched_job_cleanup(&job->base);
}

static const char *sched_test_fence_get_driver_name(struct dma_fence *fence)
{
	return "sched_test";
}

static const char *sched_test_fence_get_timeline_name(struct dma_fence *fence)
{
	const struct sched_test_fence *f = to_sched_test_fence(fence);
	return sched_test_queue_name(f->qu);
}

const struct dma_fence_ops sched_test_fence_ops = {
	.get_driver_name = sched_test_fence_get_driver_name,
	.get_timeline_name = sched_test_fence_get_timeline_name,
};

/*
static struct dma_fence *sched_test_fence_create(struct sched_test_device *sdev, enum sched_test_queue qu)
{
	struct sched_test_fence *fence = kzalloc(sizeof(struct sched_test_fence), GFP_KERNEL);
	if (!fence)
		return ERR_PTR(-ENOMEM);

	fence->dev = &sdev->drm;
	fence->qu = qu;
	fence->seqno = ++sdev->queue[qu].emit_seqno;
	dma_fence_init(&fence->base, &sched_test_fence_ops, &sdev->job_lock,
		       sdev->queue[qu].fence_context, fence->seqno);

	return &fence->base;
}
*/

static struct dma_fence *sched_test_job_dependency(struct drm_sched_job *sched_job,
						   struct drm_sched_entity *sched_entity)
{
	struct sched_test_job *job = to_sched_test_job(sched_job);
	drm_info(&job->sdev->drm, "No dependency for job %p", job);
	return NULL;
}


static struct dma_fence *sched_test_job_run(struct drm_sched_job *sched_job)
{
	struct sched_test_job *job = to_sched_test_job(sched_job);
	struct event *e = kzalloc(sizeof(struct event), GFP_KERNEL);

	e->fence = job->fence;
	e->stop = false;
	e->sdev = job->sdev;
	drm_info(&e->sdev->drm, "Push next event %p job %p", e, job);
	push_next_event(e);
	return job->fence;
}

static enum drm_gpu_sched_stat sched_test_job_timedout(struct drm_sched_job *sched_job)
{
	struct sched_test_job *job = to_sched_test_job(sched_job);

//	drm_sched_stop(&job->sched, &job->base);
	return DRM_GPU_SCHED_STAT_NOMINAL;
}


static void sched_test_job_free(struct drm_sched_job *sched_job)
{
	struct sched_test_job *job = to_sched_test_job(sched_job);

	drm_sched_job_cleanup(sched_job);
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
	int hw_jobs_limit = 1;
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
