/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2021 Xilinx, Inc.
 * Authors:
 *     Sonal Santan <sonal.santan@xilinx.com>
 */

#ifndef _DRM_SCHED_TEST_H_
#define _DRM_SCHED_TEST_H_

#ifndef __KERNEL__
#include <libdrm/drm.h>
#else
#include <uapi/drm/drm.h>
#endif /* !__KERNEL__ */

#if defined(__cplusplus)
extern "C" {
#endif

enum sched_test_queue {
	SCHED_TSTQ_A,
	SCHED_TSTQ_B,
	SCHED_TSTQ_MAX
};

#define DRM_SCHED_TEST_SUBMIT                     0x00
#define DRM_SCHED_TEST_WAIT                       0x01

struct drm_sched_test_submit {
	int in_fence;
	int out_fence;
	enum sched_test_queue qu;
};

struct drm_sched_test_wait_in {
	int fence;
	signed long timeout;
};

struct drm_sched_test_wait_out {
	signed long timeout;
};

union drm_sched_test_wait {
	struct drm_sched_test_wait_in in;
	struct drm_sched_test_wait_out out;
};

#define DRM_IOCTL_SCHED_TEST_SUBMIT           DRM_IOWR(DRM_COMMAND_BASE + DRM_SCHED_TEST_SUBMIT, struct drm_sched_test_submit)
#define DRM_IOCTL_SCHED_TEST_WAIT             DRM_IOWR(DRM_COMMAND_BASE + DRM_SCHED_TEST_WAIT, union drm_sched_test_wait)


#if defined(__cplusplus)
}
#endif

#endif
