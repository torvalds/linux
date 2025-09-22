// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2016-2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König, Felix Kuehling
 */

#include "amdgpu.h"

/**
 * DOC: mem_info_preempt_used
 *
 * The amdgpu driver provides a sysfs API for reporting current total amount of
 * used preemptible memory.
 * The file mem_info_preempt_used is used for this, and returns the current
 * used size of the preemptible block, in bytes
 */
static ssize_t mem_info_preempt_used_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	struct ttm_resource_manager *man = &adev->mman.preempt_mgr;

	return sysfs_emit(buf, "%llu\n", ttm_resource_manager_usage(man));
}

static DEVICE_ATTR_RO(mem_info_preempt_used);

/**
 * amdgpu_preempt_mgr_new - allocate a new node
 *
 * @man: TTM memory type manager
 * @tbo: TTM BO we need this range for
 * @place: placement flags and restrictions
 * @res: TTM memory object
 *
 * Dummy, just count the space used without allocating resources or any limit.
 */
static int amdgpu_preempt_mgr_new(struct ttm_resource_manager *man,
				  struct ttm_buffer_object *tbo,
				  const struct ttm_place *place,
				  struct ttm_resource **res)
{
	*res = kzalloc(sizeof(**res), GFP_KERNEL);
	if (!*res)
		return -ENOMEM;

	ttm_resource_init(tbo, place, *res);
	(*res)->start = AMDGPU_BO_INVALID_OFFSET;
	return 0;
}

/**
 * amdgpu_preempt_mgr_del - free ranges
 *
 * @man: TTM memory type manager
 * @res: TTM memory object
 *
 * Free the allocated GTT again.
 */
static void amdgpu_preempt_mgr_del(struct ttm_resource_manager *man,
				   struct ttm_resource *res)
{
	ttm_resource_fini(man, res);
	kfree(res);
}

static const struct ttm_resource_manager_func amdgpu_preempt_mgr_func = {
	.alloc = amdgpu_preempt_mgr_new,
	.free = amdgpu_preempt_mgr_del,
};

/**
 * amdgpu_preempt_mgr_init - init PREEMPT manager and DRM MM
 *
 * @adev: amdgpu_device pointer
 *
 * Allocate and initialize the GTT manager.
 */
int amdgpu_preempt_mgr_init(struct amdgpu_device *adev)
{
	struct ttm_resource_manager *man = &adev->mman.preempt_mgr;
	int ret;

	man->use_tt = true;
	man->func = &amdgpu_preempt_mgr_func;

	ttm_resource_manager_init(man, &adev->mman.bdev, (1 << 30));

	ret = device_create_file(adev->dev, &dev_attr_mem_info_preempt_used);
	if (ret) {
		DRM_ERROR("Failed to create device file mem_info_preempt_used\n");
		return ret;
	}

	ttm_set_driver_manager(&adev->mman.bdev, AMDGPU_PL_PREEMPT, man);
	ttm_resource_manager_set_used(man, true);
	return 0;
}

/**
 * amdgpu_preempt_mgr_fini - free and destroy GTT manager
 *
 * @adev: amdgpu_device pointer
 *
 * Destroy and free the GTT manager, returns -EBUSY if ranges are still
 * allocated inside it.
 */
void amdgpu_preempt_mgr_fini(struct amdgpu_device *adev)
{
	struct ttm_resource_manager *man = &adev->mman.preempt_mgr;
	int ret;

	ttm_resource_manager_set_used(man, false);

	ret = ttm_resource_manager_evict_all(&adev->mman.bdev, man);
	if (ret)
		return;

	device_remove_file(adev->dev, &dev_attr_mem_info_preempt_used);

	ttm_resource_manager_cleanup(man);
	ttm_set_driver_manager(&adev->mman.bdev, AMDGPU_PL_PREEMPT, NULL);
}
