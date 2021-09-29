// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Xilinx, Inc.
 * Authors:
 *     Sonal Santan <sonal.santan@xilinx.com>
 */

#ifndef _SCHED_TEST_COMMON_H_
#define _SCHED_TEST_COMMON_H_


#include <linux/platform_device.h>
#include <linux/spinlock_types.h>
#include <linux/idr.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/gpu_scheduler.h>


#define SCHED_TEST_MAX_QUEUE 2

enum sched_test_queue {
	SCHED_TSTQ_A,
	SCHED_TSTQ_B,
	SCHED_TSTQ_MAX
};

struct sched_test_queue_state {
	struct drm_gpu_scheduler sched;
	u64 fence_context;
	u64 emit_seqno;
};

/* Helper struct for the HW emulation thread */
struct sched_test_hwemu {
	struct sched_test_device *dev;
	/* Kernel thread emulating HW and processing jobs submitted by scheduler */
	struct task_struct *hwemu_thread;
	/* List of jobs to be processed by the kernel thread */
	struct list_head events_list;
	/* Used to protect the job (events_list) queue */
	spinlock_t events_lock;
	/* Used for fence locking between scheduler and emulated HW thread */
	spinlock_t job_lock;
	enum sched_test_queue qu;
};

struct sched_test_device {
	struct drm_device drm;
	struct platform_device *platform;
        struct sched_test_queue_state queue[SCHED_TSTQ_MAX];
	/* Kernel threads emulating HW queues*/
	struct sched_test_hwemu *hwemu[SCHED_TSTQ_MAX];
};

struct sched_test_file_priv {
	struct sched_test_device *sdev;
	struct drm_sched_entity entity;
	struct idr job_idr;
};

struct sched_test_job {
	struct drm_sched_job base;
	struct kref refcount;
	struct sched_test_device *sdev;
	/* Reference to the 'finished' fence owned by the drm_sched_job */
	struct dma_fence *done_fence;
	/* Fence created by the driver and used between scheduler and emulated HW thread */
	struct dma_fence *irq_fence;
	enum sched_test_queue qu;

	/* Callback for the freeing of the job on refcount going to 0. */
	void (*free)(struct kref *ref);
};

struct sched_test_fence {
	struct dma_fence base;
	struct sched_test_device *sdev;
	u64 seqno;
	enum sched_test_queue qu;
};

static inline struct sched_test_job *to_sched_test_job(struct drm_sched_job *job)
{
	return container_of(job, struct sched_test_job, base);
}

static inline struct sched_test_device *to_sched_test_dev(struct drm_device *dev)
{
	return container_of(dev, struct sched_test_device, drm);
}

static inline struct sched_test_fence *to_sched_test_fence(struct dma_fence *fence)
{
	return container_of(fence, struct sched_test_fence, base);
}

int sched_test_sched_init(struct sched_test_device *sdev);
void sched_test_sched_fini(struct sched_test_device *sdev);

int sched_test_job_init(struct sched_test_job *job, struct sched_test_file_priv *priv);
void sched_test_job_fini(struct sched_test_job *job);

int sched_test_hwemu_thread_start(struct sched_test_device *sdev, enum sched_test_queue qu);
int sched_test_hwemu_thread_stop(struct sched_test_device *sdev, enum sched_test_queue qu);

#endif
