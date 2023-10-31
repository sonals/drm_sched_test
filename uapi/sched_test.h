/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2021-2022 Xilinx, Inc.
 * Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
 * Authors:
 *     Sonal Santan <sonal.santan@amd.com>
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

struct drm_sched_test_submit {
	int in_fence;
	int out_fence;
	enum sched_test_queue qu;
};


#define DRM_IOCTL_SCHED_TEST_SUBMIT           DRM_IOWR(DRM_COMMAND_BASE + DRM_SCHED_TEST_SUBMIT, struct drm_sched_test_submit)

#if defined(__cplusplus)
}
#endif

#endif
