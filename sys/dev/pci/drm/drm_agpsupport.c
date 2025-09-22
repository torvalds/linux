/* $OpenBSD: drm_agpsupport.c,v 1.30 2025/02/07 03:03:08 jsg Exp $ */
/*-
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Author:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 *
 */

/*
 * Support code for tying the kernel AGP support to DRM drivers.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <asm/agp.h>

#include <drm/drm_device.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_print.h>

#if IS_ENABLED(CONFIG_AGP)

int
drm_legacy_agp_info(struct drm_device * dev, struct drm_agp_info *info)
{
	struct agp_info	*kern;

	if (dev->agp == NULL || !dev->agp->acquired)
		return (EINVAL);

	kern = &dev->agp->info;
	agp_get_info(dev->agp->agpdev, kern);
	info->agp_version_major = 1;
	info->agp_version_minor = 0;
	info->mode = kern->ai_mode;
	info->aperture_base = kern->ai_aperture_base;
	info->aperture_size = kern->ai_aperture_size;
	info->memory_allowed = kern->ai_memory_allowed;
	info->memory_used = kern->ai_memory_used;
	info->id_vendor = kern->ai_devid & 0xffff;
	info->id_device = kern->ai_devid >> 16;

	return (0);
}

int
drm_legacy_agp_acquire(struct drm_device *dev)
{
	int	retcode;

	if (dev->agp == NULL || dev->agp->acquired)
		return (EINVAL);

	retcode = agp_acquire(dev->agp->agpdev);
	if (retcode)
		return (retcode);

	dev->agp->acquired = 1;

	return (0);
}

int
drm_legacy_agp_release(struct drm_device * dev)
{
	if (dev->agp == NULL || !dev->agp->acquired)
		return (EINVAL);
	agp_release(dev->agp->agpdev);
	dev->agp->acquired = 0;

	return (0);
}

int
drm_legacy_agp_enable(struct drm_device *dev, drm_agp_mode_t mode)
{
	int	retcode = 0;

	if (dev->agp == NULL || !dev->agp->acquired)
		return (EINVAL);

	dev->agp->mode = mode.mode;
	if ((retcode = agp_enable(dev->agp->agpdev, mode.mode)) == 0)
		dev->agp->enabled = 1;
	return (retcode);
}

void
drm_legacy_agp_takedown(struct drm_device *dev)
{
	if (dev->agp == NULL)
		return;

	drm_legacy_agp_release(dev);
	dev->agp->enabled  = 0;
}

struct drm_agp_head *
drm_legacy_agp_init(struct drm_device *dev)
{
	struct agp_softc	*agpdev;
	struct drm_agp_head	*head = NULL;
	int		 	 agp_available = 1;

	agpdev = agp_find_device(0);
	if (agpdev == NULL)
		agp_available = 0;

	DRM_DEBUG("agp_available = %d\n", agp_available);

	if (agp_available) {
		head = mallocarray(1, sizeof(*head), M_DRM, M_NOWAIT | M_ZERO);
		if (head == NULL)
			return (NULL);
		head->agpdev = agpdev;
		agp_get_info(agpdev, &head->info);
		head->base = head->info.ai_aperture_base;
		head->cant_use_aperture = (head->base == 0);
		TAILQ_INIT(&head->memory);
	}
	return (head);
}

#endif /* IS_ENABLED(CONFIG_AGP) */
