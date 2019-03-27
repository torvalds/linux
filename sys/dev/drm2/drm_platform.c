/*
 * Derived from drm_pci.c
 *
 * Copyright 2003 José Fonseca.
 * Copyright 2003 Leif Delgass.
 * Copyright (c) 2009, Code Aurora Forum.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/drm2/drmP.h>

static void drm_platform_free_irq(struct drm_device *dev)
{
	if (dev->irqr == NULL)
		return;

	bus_release_resource(dev->dev, SYS_RES_IRQ,
	    dev->irqrid, dev->irqr);

	dev->irqr = NULL;
	dev->irq = 0;
}

static const char *drm_platform_get_name(struct drm_device *dev)
{
	return dev->driver->name;
}

static int drm_platform_set_busid(struct drm_device *dev, struct drm_master *master)
{
	int len, ret, id;

	master->unique_len = 13 + strlen(dev->driver->name);
	master->unique_size = master->unique_len;
	master->unique = malloc(master->unique_len + 1, DRM_MEM_DRIVER, M_NOWAIT);

	if (master->unique == NULL)
		return -ENOMEM;

	id = 0; // XXX dev->driver->id;

	/* if only a single instance of the platform device, id will be
	 * set to -1.. use 0 instead to avoid a funny looking bus-id:
	 */
	if (id == -1)
		id = 0;

	len = snprintf(master->unique, master->unique_len,
			"platform:%s:%02d", dev->driver->name, id);

	if (len > master->unique_len) {
		DRM_ERROR("Unique buffer overflowed\n");
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	return ret;
}

static int drm_platform_get_irq(struct drm_device *dev)
{
	if (dev->irqr)
		return (dev->irq);

	dev->irqr = bus_alloc_resource_any(dev->dev, SYS_RES_IRQ,
	    &dev->irqrid, RF_SHAREABLE);
	if (!dev->irqr) {
		dev_err(dev->dev, "Failed to allocate IRQ\n");
		return (0);
	}

	dev->irq = (int) rman_get_start(dev->irqr);

	return (dev->irq);
}

static struct drm_bus drm_platform_bus = {
	.bus_type = DRIVER_BUS_PLATFORM,
	.get_irq = drm_platform_get_irq,
	.free_irq = drm_platform_free_irq,
	.get_name = drm_platform_get_name,
	.set_busid = drm_platform_set_busid,
};

/**
 * Register.
 *
 * \param platdev - Platform device struture
 * \return zero on success or a negative number on failure.
 *
 * Attempt to gets inter module "drm" information. If we are first
 * then register the character device and inter module information.
 * Try and register, if we fail to register, backout previous work.
 */

int drm_get_platform_dev(device_t kdev, struct drm_device *dev,
			 struct drm_driver *driver)
{
	int ret;

	DRM_DEBUG("\n");

	driver->bus = &drm_platform_bus;

	dev->dev = kdev;

	sx_xlock(&drm_global_mutex);

	ret = drm_fill_in_dev(dev, driver);

	if (ret) {
		printf("DRM: Fill_in_dev failed.\n");
		goto err_g1;
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = drm_get_minor(dev, &dev->control, DRM_MINOR_CONTROL);
		if (ret)
			goto err_g1;
	}

	ret = drm_get_minor(dev, &dev->primary, DRM_MINOR_LEGACY);
	if (ret)
		goto err_g2;

	if (dev->driver->load) {
		ret = dev->driver->load(dev, 0);
		if (ret)
			goto err_g3;
	}

	/* setup the grouping for the legacy output */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = drm_mode_group_init_legacy_group(dev,
				&dev->primary->mode_group);
		if (ret)
			goto err_g3;
	}

#ifdef FREEBSD_NOTYET
	list_add_tail(&dev->driver_item, &driver->device_list);
#endif /* FREEBSD_NOTYET */

	sx_xunlock(&drm_global_mutex);

	DRM_INFO("Initialized %s %d.%d.%d %s on minor %d\n",
		 driver->name, driver->major, driver->minor, driver->patchlevel,
		 driver->date, dev->primary->index);

	return 0;

err_g3:
	drm_put_minor(&dev->primary);
err_g2:
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		drm_put_minor(&dev->control);
err_g1:
	sx_xunlock(&drm_global_mutex);
	return ret;
}
EXPORT_SYMBOL(drm_get_platform_dev);
