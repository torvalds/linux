/**
 * \file drm_stub.h
 * Stub support
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 */

/*
 * Created: Fri Jan 19 10:48:35 2001 by faith@acm.org
 *
 * Copyright 2001 VA Linux Systems, Inc., Sunnyvale, California.
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/drm2/drmP.h>
#include <dev/drm2/drm_core.h>

#ifdef DRM_DEBUG_DEFAULT_ON
unsigned int drm_debug = (DRM_DEBUGBITS_DEBUG | DRM_DEBUGBITS_KMS |
    DRM_DEBUGBITS_FAILED_IOCTL);
#else
unsigned int drm_debug = 0;	/* 1 to enable debug output */
#endif
EXPORT_SYMBOL(drm_debug);

unsigned int drm_notyet = 0;

unsigned int drm_vblank_offdelay = 5000;    /* Default to 5000 msecs. */
EXPORT_SYMBOL(drm_vblank_offdelay);

unsigned int drm_timestamp_precision = 20;  /* Default to 20 usecs. */
EXPORT_SYMBOL(drm_timestamp_precision);

/*
 * Default to use monotonic timestamps for wait-for-vblank and page-flip
 * complete events.
 */
unsigned int drm_timestamp_monotonic = 1;

MODULE_AUTHOR(CORE_AUTHOR);
MODULE_DESCRIPTION(CORE_DESC);
MODULE_LICENSE("GPL and additional rights");
MODULE_PARM_DESC(debug, "Enable debug output");
MODULE_PARM_DESC(vblankoffdelay, "Delay until vblank irq auto-disable [msecs]");
MODULE_PARM_DESC(timestamp_precision_usec, "Max. error on timestamps [usecs]");
MODULE_PARM_DESC(timestamp_monotonic, "Use monotonic timestamps");

module_param_named(debug, drm_debug, int, 0600);
module_param_named(vblankoffdelay, drm_vblank_offdelay, int, 0600);
module_param_named(timestamp_precision_usec, drm_timestamp_precision, int, 0600);
module_param_named(timestamp_monotonic, drm_timestamp_monotonic, int, 0600);

static struct cdevsw drm_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	drm_open,
	.d_read =	drm_read,
	.d_ioctl =	drm_ioctl,
	.d_poll =	drm_poll,
	.d_mmap_single = drm_mmap_single,
	.d_name =	"drm",
	.d_flags =	D_TRACKCLOSE
};

static int drm_minor_get_id(struct drm_device *dev, int type)
{
	int new_id;

	new_id = device_get_unit(dev->dev);

	if (new_id >= 64)
		return -EINVAL;

	if (type == DRM_MINOR_CONTROL) {
		new_id += 64;
	} else if (type == DRM_MINOR_RENDER) {
		new_id += 128;
	}

	return new_id;
}

struct drm_master *drm_master_create(struct drm_minor *minor)
{
	struct drm_master *master;

	master = malloc(sizeof(*master), DRM_MEM_KMS, M_NOWAIT | M_ZERO);
	if (!master)
		return NULL;

	refcount_init(&master->refcount, 1);
	mtx_init(&master->lock.spinlock, "drm_master__lock__spinlock",
	    NULL, MTX_DEF);
	DRM_INIT_WAITQUEUE(&master->lock.lock_queue);
	drm_ht_create(&master->magiclist, DRM_MAGIC_HASH_ORDER);
	INIT_LIST_HEAD(&master->magicfree);
	master->minor = minor;

	list_add_tail(&master->head, &minor->master_list);

	return master;
}

struct drm_master *drm_master_get(struct drm_master *master)
{
	refcount_acquire(&master->refcount);
	return master;
}
EXPORT_SYMBOL(drm_master_get);

static void drm_master_destroy(struct drm_master *master)
{
	struct drm_magic_entry *pt, *next;
	struct drm_device *dev = master->minor->dev;
	struct drm_map_list *r_list, *list_temp;

	list_del(&master->head);

	if (dev->driver->master_destroy)
		dev->driver->master_destroy(dev, master);

	list_for_each_entry_safe(r_list, list_temp, &dev->maplist, head) {
		if (r_list->master == master) {
			drm_rmmap_locked(dev, r_list->map);
			r_list = NULL;
		}
	}

	if (master->unique) {
		free(master->unique, DRM_MEM_DRIVER);
		master->unique = NULL;
		master->unique_len = 0;
	}

	list_for_each_entry_safe(pt, next, &master->magicfree, head) {
		list_del(&pt->head);
		drm_ht_remove_item(&master->magiclist, &pt->hash_item);
		free(pt, DRM_MEM_MAGIC);
	}

	drm_ht_remove(&master->magiclist);

	free(master, DRM_MEM_KMS);
}

void drm_master_put(struct drm_master **master)
{
	if (refcount_release(&(*master)->refcount))
		drm_master_destroy(*master);
	*master = NULL;
}
EXPORT_SYMBOL(drm_master_put);

int drm_setmaster_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	int ret;

	if (file_priv->is_master)
		return 0;

	if (file_priv->minor->master && file_priv->minor->master != file_priv->master)
		return -EINVAL;

	if (!file_priv->master)
		return -EINVAL;

	if (file_priv->minor->master)
		return -EINVAL;

	DRM_LOCK(dev);
	file_priv->minor->master = drm_master_get(file_priv->master);
	file_priv->is_master = 1;
	if (dev->driver->master_set) {
		ret = dev->driver->master_set(dev, file_priv, false);
		if (unlikely(ret != 0)) {
			file_priv->is_master = 0;
			drm_master_put(&file_priv->minor->master);
		}
	}
	DRM_UNLOCK(dev);

	return 0;
}

int drm_dropmaster_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	if (!file_priv->is_master)
		return -EINVAL;

	if (!file_priv->minor->master)
		return -EINVAL;

	DRM_LOCK(dev);
	if (dev->driver->master_drop)
		dev->driver->master_drop(dev, file_priv, false);
	drm_master_put(&file_priv->minor->master);
	file_priv->is_master = 0;
	DRM_UNLOCK(dev);
	return 0;
}

int drm_fill_in_dev(struct drm_device *dev,
			   struct drm_driver *driver)
{
	int retcode, i;

	INIT_LIST_HEAD(&dev->filelist);
	INIT_LIST_HEAD(&dev->ctxlist);
	INIT_LIST_HEAD(&dev->maplist);
	INIT_LIST_HEAD(&dev->vblank_event_list);

	mtx_init(&dev->irq_lock, "drmirq", NULL, MTX_DEF);
	mtx_init(&dev->count_lock, "drmcount", NULL, MTX_DEF);
	mtx_init(&dev->event_lock, "drmev", NULL, MTX_DEF);
	sx_init(&dev->dev_struct_lock, "drmslk");
	mtx_init(&dev->ctxlist_mutex, "drmctxlist", NULL, MTX_DEF);
	mtx_init(&dev->pcir_lock, "drmpcir", NULL, MTX_DEF);

	if (drm_ht_create(&dev->map_hash, 12)) {
		return -ENOMEM;
	}

	/* the DRM has 6 basic counters */
	dev->counters = 6;
	dev->types[0] = _DRM_STAT_LOCK;
	dev->types[1] = _DRM_STAT_OPENS;
	dev->types[2] = _DRM_STAT_CLOSES;
	dev->types[3] = _DRM_STAT_IOCTLS;
	dev->types[4] = _DRM_STAT_LOCKS;
	dev->types[5] = _DRM_STAT_UNLOCKS;

	/*
	 * FIXME Linux<->FreeBSD: this is done in drm_setup() on Linux.
	 */
	for (i = 0; i < ARRAY_SIZE(dev->counts); i++)
		atomic_set(&dev->counts[i], 0);

	dev->driver = driver;

	retcode = drm_pci_agp_init(dev);
	if (retcode)
		goto error_out_unreg;



	retcode = drm_ctxbitmap_init(dev);
	if (retcode) {
		DRM_ERROR("Cannot allocate memory for context bitmap.\n");
		goto error_out_unreg;
	}

	if (driver->driver_features & DRIVER_GEM) {
		retcode = drm_gem_init(dev);
		if (retcode) {
			DRM_ERROR("Cannot initialize graphics execution "
				  "manager (GEM)\n");
			goto error_out_unreg;
		}
	}

	retcode = drm_sysctl_init(dev);
	if (retcode != 0) {
		DRM_ERROR("Failed to create hw.dri sysctl entry: %d\n",
		    retcode);
	}

	return 0;

      error_out_unreg:
	drm_cancel_fill_in_dev(dev);
	return retcode;
}
EXPORT_SYMBOL(drm_fill_in_dev);

void drm_cancel_fill_in_dev(struct drm_device *dev)
{
	struct drm_driver *driver;

	driver = dev->driver;

	drm_sysctl_cleanup(dev);
	if (driver->driver_features & DRIVER_GEM)
		drm_gem_destroy(dev);
	drm_ctxbitmap_cleanup(dev);

	if (drm_core_has_MTRR(dev) && drm_core_has_AGP(dev) &&
	    dev->agp && dev->agp->agp_mtrr >= 0) {
		int retval;
		retval = drm_mtrr_del(dev->agp->agp_mtrr,
				  dev->agp->agp_info.ai_aperture_base,
				  dev->agp->agp_info.ai_aperture_size,
				  DRM_MTRR_WC);
		DRM_DEBUG("mtrr_del=%d\n", retval);
	}
	free(dev->agp, DRM_MEM_AGPLISTS);
	dev->agp = NULL;

	drm_ht_remove(&dev->map_hash);

	mtx_destroy(&dev->irq_lock);
	mtx_destroy(&dev->count_lock);
	mtx_destroy(&dev->event_lock);
	sx_destroy(&dev->dev_struct_lock);
	mtx_destroy(&dev->ctxlist_mutex);
	mtx_destroy(&dev->pcir_lock);
}

/**
 * Get a secondary minor number.
 *
 * \param dev device data structure
 * \param sec-minor structure to hold the assigned minor
 * \return negative number on failure.
 *
 * Search an empty entry and initialize it to the given parameters, and
 * create the proc init entry via proc_init(). This routines assigns
 * minor numbers to secondary heads of multi-headed cards
 */
int drm_get_minor(struct drm_device *dev, struct drm_minor **minor, int type)
{
	struct drm_minor *new_minor;
	int ret;
	int minor_id;
	const char *minor_devname;

	DRM_DEBUG("\n");

	minor_id = drm_minor_get_id(dev, type);
	if (minor_id < 0)
		return minor_id;

	new_minor = malloc(sizeof(struct drm_minor), DRM_MEM_MINOR,
	    M_NOWAIT | M_ZERO);
	if (!new_minor) {
		ret = -ENOMEM;
		goto err_idr;
	}

	new_minor->type = type;
	new_minor->dev = dev;
	new_minor->index = minor_id;
	INIT_LIST_HEAD(&new_minor->master_list);

	new_minor->buf_sigio = NULL;

	switch (type) {
	case DRM_MINOR_CONTROL:
		minor_devname = "dri/controlD%d";
		break;
	case DRM_MINOR_RENDER:
		minor_devname = "dri/renderD%d";
		break;
	default:
		minor_devname = "dri/card%d";
		break;
	}

	ret = make_dev_p(MAKEDEV_WAITOK | MAKEDEV_CHECKNAME, &new_minor->device,
	    &drm_cdevsw, 0, DRM_DEV_UID, DRM_DEV_GID,
	    DRM_DEV_MODE, minor_devname, minor_id);
	if (ret) {
		DRM_ERROR("Failed to create cdev: %d\n", ret);
		goto err_mem;
	}
	new_minor->device->si_drv1 = new_minor;
	*minor = new_minor;

	DRM_DEBUG("new minor assigned %d\n", minor_id);
	return 0;


err_mem:
	free(new_minor, DRM_MEM_MINOR);
err_idr:
	*minor = NULL;
	return ret;
}
EXPORT_SYMBOL(drm_get_minor);

/**
 * Put a secondary minor number.
 *
 * \param sec_minor - structure to be released
 * \return always zero
 *
 * Cleans up the proc resources. Not legal for this to be the
 * last minor released.
 *
 */
int drm_put_minor(struct drm_minor **minor_p)
{
	struct drm_minor *minor = *minor_p;

	DRM_DEBUG("release secondary minor %d\n", minor->index);

	funsetown(&minor->buf_sigio);

	destroy_dev(minor->device);

	free(minor, DRM_MEM_MINOR);
	*minor_p = NULL;
	return 0;
}
EXPORT_SYMBOL(drm_put_minor);

/**
 * Called via drm_exit() at module unload time or when pci device is
 * unplugged.
 *
 * Cleans up all DRM device, calling drm_lastclose().
 *
 */
void drm_put_dev(struct drm_device *dev)
{
	struct drm_driver *driver;
	struct drm_map_list *r_list, *list_temp;

	DRM_DEBUG("\n");

	if (!dev) {
		DRM_ERROR("cleanup called no dev\n");
		return;
	}
	driver = dev->driver;

	drm_lastclose(dev);

	if (drm_core_has_MTRR(dev) && drm_core_has_AGP(dev) &&
	    dev->agp && dev->agp->agp_mtrr >= 0) {
		int retval;
		retval = drm_mtrr_del(dev->agp->agp_mtrr,
				  dev->agp->agp_info.ai_aperture_base,
				  dev->agp->agp_info.ai_aperture_size,
				  DRM_MTRR_WC);
		DRM_DEBUG("mtrr_del=%d\n", retval);
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		drm_mode_group_free(&dev->primary->mode_group);

	if (dev->driver->unload)
		dev->driver->unload(dev);

	drm_sysctl_cleanup(dev);

	if (drm_core_has_AGP(dev) && dev->agp) {
		free(dev->agp, DRM_MEM_AGPLISTS);
		dev->agp = NULL;
	}

	drm_vblank_cleanup(dev);

	list_for_each_entry_safe(r_list, list_temp, &dev->maplist, head)
		drm_rmmap(dev, r_list->map);
	drm_ht_remove(&dev->map_hash);

	drm_ctxbitmap_cleanup(dev);

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		drm_put_minor(&dev->control);

	if (driver->driver_features & DRIVER_GEM)
		drm_gem_destroy(dev);

	drm_put_minor(&dev->primary);

	mtx_destroy(&dev->irq_lock);
	mtx_destroy(&dev->count_lock);
	mtx_destroy(&dev->event_lock);
	sx_destroy(&dev->dev_struct_lock);
	mtx_destroy(&dev->ctxlist_mutex);
	mtx_destroy(&dev->pcir_lock);

#ifdef FREEBSD_NOTYET
	list_del(&dev->driver_item);
#endif /* FREEBSD_NOTYET */
}
EXPORT_SYMBOL(drm_put_dev);
