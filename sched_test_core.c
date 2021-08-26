// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Xilinx, Inc.
 * Authors:
 *     Sonal Santan <sonal.santan@xilinx.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/gpu_scheduler.h>

#include "sched_test_common.h"

#define str(x) #x

int sched_test_job_init(struct sched_test_job *job)
{
//	int err = drm_sched_job_init(&job->base, &context->base, vm);
	int err;
	return err;
}

void sched_test_job_fini(struct sched_test_job *job)
{
	drm_sched_job_cleanup(&job->base);

}

static const char *sched_test_fence_get_driver_name(struct dma_fence *fence)
{
	return "sched_test";
}

static const char *sched_test_fence_get_timeline_name(struct dma_fence *fence)
{
	const struct sched_test_fence *f = to_sched_test_fence(fence);

	switch (f->qu) {
	case SCHED_TEST_QUEUE_REGULAR:
		return str(SCHED_TEST_QUEUE_REGULAR);
	case SCHED_TEST_QUEUE_FAST:
		return str(SCHED_TEST_QUEUE_FAST);
	default:
		return NULL;
	}
}

const struct dma_fence_ops sched_test_fence_ops = {
	.get_driver_name = sched_test_fence_get_driver_name,
	.get_timeline_name = sched_test_fence_get_timeline_name,
};

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

static struct dma_fence *sched_test_job_dependency(struct drm_sched_job *sched_job,
						   struct drm_sched_entity *sched_entity)
{
	return NULL;
}


static struct dma_fence *sched_test_job_run(struct drm_sched_job *sched_job)
{
	struct sched_test_job *job = to_sched_test_job(sched_job);
	struct dma_fence *fence = NULL;


	return fence;
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

	ret = drm_sched_init(&sdev->queue[SCHED_TEST_QUEUE_REGULAR].sched,
			     &sched_test_regular_ops,
			     hw_jobs_limit, job_hang_limit,
			     msecs_to_jiffies(hang_limit_ms),
			     str(SCHED_TEST_QUEUE_REGULAR));
	if (ret) {
		drm_err(&sdev->drm, "Failed to create %s scheduler: %d", str(SCHED_TEST_QUEUE_REGULAR), ret);
		return ret;
	}

	ret = drm_sched_init(&sdev->queue[SCHED_TEST_QUEUE_FAST].sched,
			     &sched_test_fast_ops,
			     hw_jobs_limit, job_hang_limit,
			     msecs_to_jiffies(hang_limit_ms),
			     str(SCHED_TEST_QUEUE_REGULAR));
	if (ret) {
		drm_err(&sdev->drm, "Failed to create %s scheduler: %d", str(SCHED_TEST_QUEUE_REGULAR),
			ret);
		sched_test_sched_fini(sdev);
		return ret;
	}

	return 0;
}

void sched_test_sched_fini(struct sched_test_device *sdev)
{
	enum sched_test_queue i;
	for (i = SCHED_TEST_QUEUE_MAX; i > 0; i--) {
		if (sdev->queue[i].sched.ready)
			drm_sched_fini(&sdev->queue[i].sched);
	}
}
