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
#include <linux/version.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/gpu_scheduler.h>
#include <drm/drm_syncobj.h>
#include <drm/gpu_scheduler.h>


#include "sched_test_common.h"
#include "uapi/sched_test.h"

#define DRIVER_NAME	"sched_test"
#define DRIVER_DESC	"DRM scheduler test driver"
#define DRIVER_DATE	"20210815"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static struct sched_test_device *sched_test_device_obj;

static inline int sched_test_add_dependencies(struct sched_test_job *job, struct drm_file *file_priv,
					      int in_fence)
{
	struct sched_test_file_priv *priv = file_priv->driver_priv;
	struct drm_device *dev = &priv->sdev->drm;
	struct drm_syncobj *syncobj = NULL;
	struct dma_fence *fence = NULL;
	int ret = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
	syncobj = drm_syncobj_find(file_priv, in_fence);

	drm_info(dev, "<6.3.0Before add depedency fence = %d...", in_fence);
	if (!syncobj)
		return -ENOENT;

	fence = drm_syncobj_fence_get(syncobj);
	if (!fence) {
		drm_syncobj_put(syncobj);
		return -ENOENT;
	}

	ret = drm_sched_job_add_dependency(&job->base, fence);

	dma_fence_put(fence);
	drm_syncobj_put(syncobj);
	drm_info(dev, "After add depedency ret = %d...", ret);
	return ret;

#else
	drm_info(dev, ">=6.3.0Before add depedency fence = %d...", in_fence);
#if 0
	syncobj = drm_syncobj_find(file_priv, in_fence);
	drm_info(dev, "After find syncobj find, in_fence = %d, ret = %d, syncobj = 0x%p...", in_fence, ret, syncobj);
	fence = drm_syncobj_fence_get(syncobj);
	drm_info(dev, "After find syncobj fence get fence = 0x%p...", fence);
	ret = drm_syncobj_find_fence(file_priv, in_fence, 0, 0, &fence);
	drm_info(dev, "After find fence in_fence = %d, ret = %d, fence = 0x%p...", in_fence, ret, fence);
#endif
	ret = drm_sched_job_add_syncobj_dependency(&job->base, file_priv, in_fence, 0);
	drm_info(dev, "After add depedency ret = %d...", ret);
	return ret;
#endif
}

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

	file->driver_priv = priv;
	drm_info(dev, "File opened, sched entity A and B created...");
	return 0;

out:
	kfree(priv);
	return ret;
}


static void sched_test_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct sched_test_file_priv *priv = file->driver_priv;
	drm_info(dev, "File closing...");
	drm_sched_entity_destroy(&priv->entity[SCHED_TSTQ_B]);
	drm_sched_entity_destroy(&priv->entity[SCHED_TSTQ_A]);
	kfree(priv);
	drm_info(dev, "File closed!");
	file->driver_priv = NULL;
}

int sched_test_submit_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	struct sched_test_file_priv *priv = file_priv->driver_priv;
	const struct drm_sched_test_submit *args = data;
	struct drm_syncobj *out_sync = NULL;
	struct sched_test_job *job;
	int ret = 0;

	if (args->out_fence) {
		out_sync = drm_syncobj_find(file_priv, args->out_fence);
		if (!out_sync)
			return -ENOENT;
	}

	drm_info(dev, "After out fence...");
	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job) {
		ret = -ENOMEM;
		goto out_put;
	}

	job->qu = args->qu;

	ret = sched_test_job_init(job, priv);
	if (ret)
		goto out_free;

	drm_info(dev, "After job init...");
	if (args->in_fence) {
		ret = sched_test_add_dependencies(job, file_priv, args->in_fence);
		if (ret)
			/*
			 * Note if ret == -ENOENT, then it implies that user sent an empty sync
			 * object with no fence, which we are treating as error.
			 */
			goto out_dep;
	}

	drm_info(dev, "After in fence...");
	if (out_sync) {
		drm_syncobj_replace_fence(out_sync, job->done_fence);
		drm_syncobj_put(out_sync);
	}
	drm_sched_entity_push_job(&job->base);
	drm_info(dev, "After push job...");
	return 0;

out_dep:
	sched_test_job_fini(job);
out_free:
	kfree(job);
out_put:
	if (out_sync)
		drm_syncobj_put(out_sync);
	return ret;
}


static const struct drm_ioctl_desc sched_test_ioctls[] = {
	DRM_IOCTL_DEF_DRV(SCHED_TEST_SUBMIT, sched_test_submit_ioctl, DRM_RENDER_ALLOW | DRM_AUTH),
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

MODULE_AUTHOR("Sonal Santan <sonal.santan@amd.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");
