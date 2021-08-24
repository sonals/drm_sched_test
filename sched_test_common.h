// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Xilinx, Inc.
 * Authors:
 *     Sonal Santan <sonal.santan@xilinx.com>
 */

#include <linux/platform_device.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/gpu_scheduler.h>

#define DRIVER_NAME	"sched_test"
#define DRIVER_DESC	"DRM scheduler test driver"
#define DRIVER_DATE	"20210815"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0


struct sched_test_device {
	struct drm_device drm;
	struct platform_device *platform;
        struct drm_gpu_scheduler sched;
};

struct sched_test_file_priv {
	struct sched_test_device *obj;
	struct drm_sched_entity sched_entity;
};

int sched_test_sched_init(struct sched_test_device *sdev);
void sched_test_sched_fini(struct sched_test_device *sdev);
