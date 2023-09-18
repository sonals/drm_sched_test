// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021-2022 Xilinx, Inc.
 * Copyright (C) 2022-2023 Advanced Micro Devices, Inc.
 * Authors:
 *     Sonal Santan <sonal.santan@amd.com>
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
#include "uapi/sched_test.h"

#define DRIVER_NAME	"sched_test"
#define DRIVER_DESC	"DRM scheduler test driver"
#define DRIVER_DATE	"20210815"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static struct sched_test_device *sched_test_device_obj;

static int sched_test_open(struct drm_device *dev, struct drm_file *file)
{
	struct sched_test_file_priv *priv = NULL;
	struct drm_gpu_scheduler *sched;
	int ret = 0;

	/* Do not allow users to open PRIMARY node, /dev/dri/cardX node.
	 * Users should only open RENDER, /dev/dri/renderX node
	 */
	if (drm_is_primary_client(file))
		return -EPERM;

	priv = kzalloc(sizeof(struct sched_test_file_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->sdev = to_sched_test_dev(dev);
	sched = &priv->sdev->queue[SCHED_TSTQ_A].sched;
	ret = drm_sched_entity_init(&priv->entity[SCHED_TSTQ_A], DRM_SCHED_PRIORITY_NORMAL, &sched,
				    1, NULL);
	if (ret)
		goto out;
	sched = &priv->sdev->queue[SCHED_TSTQ_B].sched;
	ret = drm_sched_entity_init(&priv->entity[SCHED_TSTQ_B], DRM_SCHED_PRIORITY_NORMAL, &sched,
				    1, NULL);
	if (ret) {
		drm_sched_entity_destroy(&priv->entity[SCHED_TSTQ_A]);
		goto out;
	}

	idr_init_base(&priv->job_idr, 1);
	file->driver_priv = priv;
	drm_info(dev, "File opened...");
	return 0;

out:
	kfree(priv);
	return ret;
}

static int job_idr_fini(int id, void *p, void *data)
{
	struct sched_test_job *job = p;
	sched_test_job_fini(job);
	return 0;
}

static void sched_test_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct sched_test_file_priv *priv = file->driver_priv;
	const bool outstanding = idr_is_empty(&priv->job_idr);
	drm_info(dev, "File closing...");
	if (!outstanding)
		drm_info(dev, "Reap outstanding jobs...");
	else
		drm_info(dev, "No outstanding jobs...");
	drm_sched_entity_destroy(&priv->entity[SCHED_TSTQ_B]);
	drm_sched_entity_destroy(&priv->entity[SCHED_TSTQ_A]);
	idr_for_each(&priv->job_idr, job_idr_fini, priv);
	idr_destroy(&priv->job_idr);
	kfree(priv);
	drm_info(dev, "File closed!");
	file->driver_priv = NULL;
}

int sched_test_submit_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	struct sched_test_file_priv *priv = file_priv->driver_priv;
	union drm_sched_test_submit *args = data;
	struct sched_test_job *in_job = NULL;
	struct dma_fence *in_fence = NULL;
	struct sched_test_job *job;
	int ret = 0;

	if (args->in.fence) {
		in_job = idr_find(&priv->job_idr, args->in.fence);
		in_fence = in_job ? in_job->done_fence : NULL;
	}

	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job)
		return -ENOMEM;

	job->qu = args->in.qu;
	ret = idr_alloc(&priv->job_idr, job, 1, 0, GFP_KERNEL);
	if (ret > 0)
		args->out.fence = ret;
	else
		goto out_free;

	ret = sched_test_job_init(job, priv);
	if (ret)
		goto out_idr;
	drm_sched_job_add_dependency(&job->base, dma_fence_get(in_fence));
//	job->in_fence = dma_fence_get(in_fence);
	return 0;

out_idr:
	idr_remove(&priv->job_idr, args->out.fence);
out_free:
	kfree(job);
	args->out.fence = 0xffffffff;
	return ret;
}

int sched_test_wait_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct sched_test_file_priv *priv = file_priv->driver_priv;
	union drm_sched_test_wait *args = data;
	struct sched_test_job *job = idr_find(&priv->job_idr, args->in.fence);
	signed long int left = 0;

	if (!job)
		return -EINVAL;
	left = dma_fence_wait_timeout(job->done_fence, true, args->in.timeout);
	if (left > 0) {
		idr_remove(&priv->job_idr, args->in.fence);
		sched_test_job_fini(job);
		args->out.timeout = left;
		return 0;
	}
	if (left < 0)
		return left;
	return -ETIMEDOUT;
}

static const struct drm_ioctl_desc sched_test_ioctls[] = {
	DRM_IOCTL_DEF_DRV(SCHED_TEST_SUBMIT, sched_test_submit_ioctl, DRM_RENDER_ALLOW | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(SCHED_TEST_WAIT, sched_test_wait_ioctl, DRM_RENDER_ALLOW | DRM_AUTH),
};

DEFINE_DRM_GEM_FOPS(sched_test_driver_fops);

static struct drm_driver sched_test_driver = {
	.driver_features		= DRIVER_GEM | DRIVER_RENDER | DRIVER_SYNCOBJ | DRIVER_SYNCOBJ_TIMELINE,
	.open				= sched_test_open,
        .postclose                      = sched_test_postclose,
	.ioctls				= sched_test_ioctls,
	.num_ioctls 			= ARRAY_SIZE(sched_test_ioctls),
	.fops				= &sched_test_driver_fops,
	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.date	= DRIVER_DATE,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,
};

static int __init sched_test_init(void)
{
	int ret;
	struct platform_device *pdev = platform_device_register_simple("sched_test", -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	if (!devres_open_group(&pdev->dev, NULL, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto out_unregister;
	}

	sched_test_device_obj = devm_drm_dev_alloc(&pdev->dev, &sched_test_driver,
                                                   struct sched_test_device, drm);
	if (IS_ERR(sched_test_device_obj)) {
		ret = PTR_ERR(sched_test_device_obj);
		goto out_devres;
	}
	sched_test_device_obj->platform = pdev;

	ret = sched_test_sched_init(sched_test_device_obj);
	if (ret < 0)
		goto out_devres;

	ret = sched_test_hwemu_threads_start(sched_test_device_obj);
	if (ret)
		goto out_sched;

	ret = drm_dev_register(&sched_test_device_obj->drm, 0);
	if (ret)
		goto out_hwemu;

	return 0;

out_hwemu:
	sched_test_hwemu_threads_stop(sched_test_device_obj);
out_sched:
	sched_test_sched_fini(sched_test_device_obj);
out_devres:
	devres_release_group(&pdev->dev, NULL);
out_unregister:
	platform_device_unregister(pdev);
	return ret;
}

static void __exit sched_test_exit(void)
{
	struct platform_device *pdev = sched_test_device_obj->platform;

	sched_test_hwemu_threads_stop(sched_test_device_obj);
	drm_dev_unregister(&sched_test_device_obj->drm);
	sched_test_sched_fini(sched_test_device_obj);
	devres_release_group(&pdev->dev, NULL);
	platform_device_unregister(pdev);
}

module_init(sched_test_init);
module_exit(sched_test_exit);

MODULE_AUTHOR("Sonal Santan <sonal.santan@xilinx.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");
