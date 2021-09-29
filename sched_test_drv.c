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
#include "uapi/sched_test.h"

#define DRIVER_NAME	"sched_test"
#define DRIVER_DESC	"DRM scheduler test driver"
#define DRIVER_DATE	"20210815"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static struct sched_test_device *sched_test_device_obj;

static int sched_test_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_gpu_scheduler *sched;
	int ret = 0;
	struct sched_test_file_priv *priv = kzalloc(sizeof(struct sched_test_file_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->sdev = to_sched_test_dev(dev);
	sched = &priv->sdev->queue[SCHED_TSTQ_A].sched;
	ret = drm_sched_entity_init(&priv->entity, DRM_SCHED_PRIORITY_NORMAL, &sched,
				    1, NULL);
	if (ret)
		goto out;
	idr_init_base(&priv->job_idr, 1);
	file->driver_priv = priv;
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
	drm_sched_entity_destroy(&priv->entity);
	drm_info(dev, "Application exiting, harvesting all remaining jobs...");
	idr_for_each(&priv->job_idr, job_idr_fini, priv);
	idr_destroy(&priv->job_idr);
	kfree(priv);
	drm_info(dev, "Application exiting");
	file->driver_priv = NULL;
}

int sched_test_submit_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	struct sched_test_file_priv *priv = file_priv->driver_priv;
	struct drm_sched_test_submit *args = data;
	struct sched_test_job *job;
	int ret = 0;

	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job)
		return -ENOMEM;

	ret = idr_alloc(&priv->job_idr, job, 1, 0, GFP_KERNEL);
	if (ret > 0)
		args->fence = ret;
	else
		goto out_free;

	job->qu = SCHED_TSTQ_A;
	ret = sched_test_job_init(job, priv);
	if (ret)
		goto out_idr;
	return 0;

out_idr:
	idr_remove(&priv->job_idr, args->fence);
out_free:
	kfree(job);
	return ret;
}

int sched_test_wait_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv)
{
	struct sched_test_file_priv *priv = file_priv->driver_priv;
	struct drm_sched_test_wait *args = data;
	struct sched_test_job *job = idr_find(&priv->job_idr, args->fence);
	signed long int left = 0;

	if (!job)
		return -EINVAL;
	left = dma_fence_wait_timeout(job->done_fence, true, args->timeout);
	if (left > 0) {
		idr_remove(&priv->job_idr, args->fence);
		sched_test_job_fini(job);
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
	.driver_features		= DRIVER_GEM | DRIVER_RENDER,
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

	/* Final step: expose the device/driver to userspace */
	ret = drm_dev_register(&sched_test_device_obj->drm, 0);
	if (ret)
		goto out_sched;

	spin_lock_init(&sched_test_device_obj->job_lock);

	ret = sched_test_hwemu_thread_start(sched_test_device_obj);
	if (ret)
		goto out_drm_unregister;

	return 0;

out_drm_unregister:
	drm_dev_unregister(&sched_test_device_obj->drm);
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

	sched_test_hwemu_thread_stop(sched_test_device_obj);
	kfree(sched_test_device_obj->hwemu);
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
