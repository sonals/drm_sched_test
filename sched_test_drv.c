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

	priv->dev = to_sched_test_dev(dev);
	sched = &priv->dev->queue[SCHED_TEST_QUEUE_A].sched;
	ret = drm_sched_entity_init(&priv->entity, DRM_SCHED_PRIORITY_NORMAL, &sched,
				    1, NULL);
	if (ret)
		goto out;
	file->driver_priv = priv;
	return 0;

out:
	kfree(priv);
	return ret;
}

static void sched_test_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct sched_test_file_priv *priv = file->driver_priv;

	drm_sched_entity_destroy(&priv->entity);
	kfree(priv);
	file->driver_priv = NULL;
}

int sched_test_submit_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	struct sched_test_device *sdev = to_sched_test_dev(dev);
	struct sched_test_file_priv *priv = file_priv->driver_priv;
	struct drm_sched_test_submit *args = data;
	struct sched_test_job *job;
	int ret = 0;

	job = kcalloc(1, sizeof(*job), GFP_KERNEL);
	if (!job)
		return -ENOMEM;

#if 0
	ret = sched_test_job_init(job, priv);
	if (ret) {
		kfree(job);
		return ret;
	}


	if (args->flags & DRM_V3D_SUBMIT_CL_FLUSH_CACHE) {
		clean_job = kcalloc(1, sizeof(*clean_job), GFP_KERNEL);
		if (!clean_job) {
			ret = -ENOMEM;
			goto fail;
		}

		ret = v3d_job_init(v3d, file_priv, clean_job, v3d_job_free, 0);
		if (ret) {
			kfree(clean_job);
			clean_job = NULL;
			goto fail;
		}

		last_job = clean_job;
	} else {
		last_job = &render->base;
	}

	ret = v3d_lookup_bos(dev, file_priv, last_job,
			     args->bo_handles, args->bo_handle_count);
	if (ret)
		goto fail;

	ret = v3d_lock_bo_reservations(last_job, &acquire_ctx);
	if (ret)
		goto fail;

	mutex_lock(&v3d->sched_lock);
	if (bin) {
		ret = v3d_push_job(v3d_priv, &bin->base, V3D_BIN);
		if (ret)
			goto fail_unreserve;

		ret = drm_gem_fence_array_add(&render->base.deps,
					      dma_fence_get(bin->base.done_fence));
		if (ret)
			goto fail_unreserve;
	}

	ret = v3d_push_job(v3d_priv, &render->base, V3D_RENDER);
	if (ret)
		goto fail_unreserve;

	if (clean_job) {
		struct dma_fence *render_fence =
			dma_fence_get(render->base.done_fence);
		ret = drm_gem_fence_array_add(&clean_job->deps, render_fence);
		if (ret)
			goto fail_unreserve;
		ret = v3d_push_job(v3d_priv, clean_job, V3D_CACHE_CLEAN);
		if (ret)
			goto fail_unreserve;
	}

	mutex_unlock(&v3d->sched_lock);

	v3d_attach_fences_and_unlock_reservation(file_priv,
						 last_job,
						 &acquire_ctx,
						 args->out_sync,
						 last_job->done_fence);

	if (bin)
		v3d_job_put(&bin->base);
	v3d_job_put(&render->base);
	if (clean_job)
		v3d_job_put(clean_job);

	return 0;

fail_unreserve:
	mutex_unlock(&v3d->sched_lock);
	drm_gem_unlock_reservations(last_job->bo,
				    last_job->bo_count, &acquire_ctx);
fail:
	if (bin)
		v3d_job_put(&bin->base);
	v3d_job_put(&render->base);
	if (clean_job)
		v3d_job_put(clean_job);

#endif
	return ret;
}

static const struct drm_ioctl_desc sched_test_ioctls[] = {
	DRM_IOCTL_DEF_DRV(SCHED_TEST_SUBMIT, sched_test_submit_ioctl, DRM_RENDER_ALLOW | DRM_AUTH),
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

	return 0;

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

	sched_test_sched_fini(sched_test_device_obj);
	drm_dev_unregister(&sched_test_device_obj->drm);
	devres_release_group(&pdev->dev, NULL);
	platform_device_unregister(pdev);
}

module_init(sched_test_init);
module_exit(sched_test_exit);

MODULE_AUTHOR("Sonal Santan <sonal.santan@xilinx.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");
