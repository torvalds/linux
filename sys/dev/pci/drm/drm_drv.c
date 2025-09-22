/*
 * Created: Fri Jan 19 10:48:35 2001 by faith@acm.org
 *
 * Copyright 2001 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Author Rickard E. (Rik) Faith <faith@valinux.com>
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

#include <sys/param.h>
#include <sys/fcntl.h>
#include <sys/specdev.h>
#include <sys/vnode.h>

#include <machine/bus.h>

#ifdef __HAVE_ACPI
#include <dev/acpi/acpidev.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/dsdt.h>
#endif

#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mount.h>
#include <linux/pseudo_fs.h>
#include <linux/slab.h>
#include <linux/srcu.h>
#include <linux/xarray.h>
#include <linux/suspend.h>

#include <drm/drm_accel.h>
#include <drm/drm_cache.h>
#include <drm/drm_client.h>
#include <drm/drm_color_mgmt.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_managed.h>
#include <drm/drm_mode_object.h>
#include <drm/drm_panic.h>
#include <drm/drm_print.h>
#include <drm/drm_privacy_screen_machine.h>

#include <drm/drm_gem.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

MODULE_AUTHOR("Gareth Hughes, Leif Delgass, José Fonseca, Jon Smirl");
MODULE_DESCRIPTION("DRM shared core routines");
MODULE_LICENSE("GPL and additional rights");

DEFINE_XARRAY_ALLOC(drm_minors_xa);

/*
 * If the drm core fails to init for whatever reason,
 * we should prevent any drivers from registering with it.
 * It's best to check this at drm_dev_init(), as some drivers
 * prefer to embed struct drm_device into their own device
 * structure and call drm_dev_init() themselves.
 */
static bool drm_core_init_complete;

static struct dentry *drm_debugfs_root;

#ifdef notyet
DEFINE_STATIC_SRCU(drm_unplug_srcu);
#endif

/*
 * Some functions are only called once on init regardless of how many times
 * drm attaches.  In linux this is handled via module_init()/module_exit()
 */
int drm_refcnt; 

struct drm_softc {
	struct device		sc_dev;
	struct drm_device 	*sc_drm;
	int			sc_allocated;
};

struct drm_attach_args {
	struct drm_device		*drm;
	const struct drm_driver		*driver;
	char				*busid;
	bus_dma_tag_t			 dmat;
	bus_space_tag_t			 bst;
	size_t				 busid_len;
	int				 is_agp;
	struct pci_attach_args		*pa;
	int				 primary;
};

void	drm_linux_init(void);
void	drm_linux_exit(void);
int	drm_linux_acpi_notify(struct aml_node *, int, void *);

int	drm_dequeue_event(struct drm_device *, struct drm_file *, size_t,
	    struct drm_pending_event **);

int	drmprint(void *, const char *);
int	drmsubmatch(struct device *, void *, void *);
const struct pci_device_id *
	drm_find_description(int, int, const struct pci_device_id *);

int	drm_file_cmp(struct drm_file *, struct drm_file *);
SPLAY_PROTOTYPE(drm_file_tree, drm_file, link, drm_file_cmp);

#define DRMDEVCF_PRIMARY	0
#define drmdevcf_primary	cf_loc[DRMDEVCF_PRIMARY]	/* spec'd as primary? */
#define DRMDEVCF_PRIMARY_UNK	-1

/*
 * DRM Minors
 * A DRM device can provide several char-dev interfaces on the DRM-Major. Each
 * of them is represented by a drm_minor object. Depending on the capabilities
 * of the device-driver, different interfaces are registered.
 *
 * Minors can be accessed via dev->$minor_name. This pointer is either
 * NULL or a valid drm_minor pointer and stays valid as long as the device is
 * valid. This means, DRM minors have the same life-time as the underlying
 * device. However, this doesn't mean that the minor is active. Minors are
 * registered and unregistered dynamically according to device-state.
 */

static struct xarray *drm_minor_get_xa(enum drm_minor_type type)
{
	if (type == DRM_MINOR_PRIMARY || type == DRM_MINOR_RENDER)
		return &drm_minors_xa;
#if IS_ENABLED(CONFIG_DRM_ACCEL)
	else if (type == DRM_MINOR_ACCEL)
		return &accel_minors_xa;
#endif
	else
		return ERR_PTR(-EOPNOTSUPP);
}

static struct drm_minor **drm_minor_get_slot(struct drm_device *dev,
					     enum drm_minor_type type)
{
	switch (type) {
	case DRM_MINOR_PRIMARY:
		return &dev->primary;
	case DRM_MINOR_RENDER:
		return &dev->render;
	case DRM_MINOR_ACCEL:
		return &dev->accel;
	default:
		BUG();
	}
}

static void drm_minor_alloc_release(struct drm_device *dev, void *data)
{
	struct drm_minor *minor = data;

	WARN_ON(dev != minor->dev);

#ifdef __linux__
	put_device(minor->kdev);
#endif

	xa_erase(drm_minor_get_xa(minor->type), minor->index);
}

/*
 * DRM used to support 64 devices, for backwards compatibility we need to maintain the
 * minor allocation scheme where minors 0-63 are primary nodes, 64-127 are control nodes,
 * and 128-191 are render nodes.
 * After reaching the limit, we're allocating minors dynamically - first-come, first-serve.
 * Accel nodes are using a distinct major, so the minors are allocated in continuous 0-MAX
 * range.
 */
#define DRM_MINOR_LIMIT(t) ({ \
	typeof(t) _t = (t); \
	_t == DRM_MINOR_ACCEL ? XA_LIMIT(0, ACCEL_MAX_MINORS) : XA_LIMIT(64 * _t, 64 * _t + 63); \
})
#define DRM_EXTENDED_MINOR_LIMIT XA_LIMIT(192, (1 << MINORBITS) - 1)

static int drm_minor_alloc(struct drm_device *dev, enum drm_minor_type type)
{
	struct drm_minor *minor;
	int r;

	minor = drmm_kzalloc(dev, sizeof(*minor), GFP_KERNEL);
	if (!minor)
		return -ENOMEM;

	minor->type = type;
	minor->dev = dev;

	r = xa_alloc(drm_minor_get_xa(type), &minor->index,
		     NULL, DRM_MINOR_LIMIT(type), GFP_KERNEL);
	if (r == -EBUSY && (type == DRM_MINOR_PRIMARY || type == DRM_MINOR_RENDER))
		r = xa_alloc(&drm_minors_xa, &minor->index,
			     NULL, DRM_EXTENDED_MINOR_LIMIT, GFP_KERNEL);
	if (r < 0)
		return r;

	r = drmm_add_action_or_reset(dev, drm_minor_alloc_release, minor);
	if (r)
		return r;

#ifdef __linux__
	minor->kdev = drm_sysfs_minor_alloc(minor);
	if (IS_ERR(minor->kdev))
		return PTR_ERR(minor->kdev);
#endif

	*drm_minor_get_slot(dev, type) = minor;
	return 0;
}

static int drm_minor_register(struct drm_device *dev, enum drm_minor_type type)
{
	struct drm_minor *minor;
	void *entry;
	int ret;

	DRM_DEBUG("\n");

	minor = *drm_minor_get_slot(dev, type);
	if (!minor)
		return 0;

#ifdef __linux__
	if (minor->type != DRM_MINOR_ACCEL) {
		ret = drm_debugfs_register(minor, minor->index,
					   drm_debugfs_root);
		if (ret) {
			DRM_ERROR("DRM: Failed to initialize /sys/kernel/debug/dri.\n");
			goto err_debugfs;
		}
	}

	ret = device_add(minor->kdev);
	if (ret)
		goto err_debugfs;
#else
	drm_debugfs_root = NULL;
#endif

	/* replace NULL with @minor so lookups will succeed from now on */
	entry = xa_store(drm_minor_get_xa(type), minor->index, minor, GFP_KERNEL);
	if (xa_is_err(entry)) {
		ret = xa_err(entry);
		goto err_debugfs;
	}
	WARN_ON(entry);

	DRM_DEBUG("new minor registered %d\n", minor->index);
	return 0;

err_debugfs:
	drm_debugfs_unregister(minor);
	return ret;
}

static void drm_minor_unregister(struct drm_device *dev, enum drm_minor_type type)
{
	struct drm_minor *minor;

	minor = *drm_minor_get_slot(dev, type);
#ifdef __linux__
	if (!minor || !device_is_registered(minor->kdev))
#else
	if (!minor)
#endif
		return;

	/* replace @minor with NULL so lookups will fail from now on */
	xa_store(drm_minor_get_xa(type), minor->index, NULL, GFP_KERNEL);

#ifdef __linux__
	device_del(minor->kdev);
#endif
	dev_set_drvdata(minor->kdev, NULL); /* safety belt */
	drm_debugfs_unregister(minor);
}

/*
 * Looks up the given minor-ID and returns the respective DRM-minor object. The
 * refence-count of the underlying device is increased so you must release this
 * object with drm_minor_release().
 *
 * As long as you hold this minor, it is guaranteed that the object and the
 * minor->dev pointer will stay valid! However, the device may get unplugged and
 * unregistered while you hold the minor.
 */
struct drm_minor *drm_minor_acquire(struct xarray *minor_xa, unsigned int minor_id)
{
	struct drm_minor *minor;

	xa_lock(minor_xa);
	minor = xa_load(minor_xa, minor_id);
	if (minor)
		drm_dev_get(minor->dev);
	xa_unlock(minor_xa);

	if (!minor) {
		return ERR_PTR(-ENODEV);
	} else if (drm_dev_is_unplugged(minor->dev)) {
		drm_dev_put(minor->dev);
		return ERR_PTR(-ENODEV);
	}

	return minor;
}

void drm_minor_release(struct drm_minor *minor)
{
	drm_dev_put(minor->dev);
}

/**
 * DOC: driver instance overview
 *
 * A device instance for a drm driver is represented by &struct drm_device. This
 * is allocated and initialized with devm_drm_dev_alloc(), usually from
 * bus-specific ->probe() callbacks implemented by the driver. The driver then
 * needs to initialize all the various subsystems for the drm device like memory
 * management, vblank handling, modesetting support and initial output
 * configuration plus obviously initialize all the corresponding hardware bits.
 * Finally when everything is up and running and ready for userspace the device
 * instance can be published using drm_dev_register().
 *
 * There is also deprecated support for initializing device instances using
 * bus-specific helpers and the &drm_driver.load callback. But due to
 * backwards-compatibility needs the device instance have to be published too
 * early, which requires unpretty global locking to make safe and is therefore
 * only support for existing drivers not yet converted to the new scheme.
 *
 * When cleaning up a device instance everything needs to be done in reverse:
 * First unpublish the device instance with drm_dev_unregister(). Then clean up
 * any other resources allocated at device initialization and drop the driver's
 * reference to &drm_device using drm_dev_put().
 *
 * Note that any allocation or resource which is visible to userspace must be
 * released only when the final drm_dev_put() is called, and not when the
 * driver is unbound from the underlying physical struct &device. Best to use
 * &drm_device managed resources with drmm_add_action(), drmm_kmalloc() and
 * related functions.
 *
 * devres managed resources like devm_kmalloc() can only be used for resources
 * directly related to the underlying hardware device, and only used in code
 * paths fully protected by drm_dev_enter() and drm_dev_exit().
 *
 * Display driver example
 * ~~~~~~~~~~~~~~~~~~~~~~
 *
 * The following example shows a typical structure of a DRM display driver.
 * The example focus on the probe() function and the other functions that is
 * almost always present and serves as a demonstration of devm_drm_dev_alloc().
 *
 * .. code-block:: c
 *
 *	struct driver_device {
 *		struct drm_device drm;
 *		void *userspace_facing;
 *		struct clk *pclk;
 *	};
 *
 *	static const struct drm_driver driver_drm_driver = {
 *		[...]
 *	};
 *
 *	static int driver_probe(struct platform_device *pdev)
 *	{
 *		struct driver_device *priv;
 *		struct drm_device *drm;
 *		int ret;
 *
 *		priv = devm_drm_dev_alloc(&pdev->dev, &driver_drm_driver,
 *					  struct driver_device, drm);
 *		if (IS_ERR(priv))
 *			return PTR_ERR(priv);
 *		drm = &priv->drm;
 *
 *		ret = drmm_mode_config_init(drm);
 *		if (ret)
 *			return ret;
 *
 *		priv->userspace_facing = drmm_kzalloc(..., GFP_KERNEL);
 *		if (!priv->userspace_facing)
 *			return -ENOMEM;
 *
 *		priv->pclk = devm_clk_get(dev, "PCLK");
 *		if (IS_ERR(priv->pclk))
 *			return PTR_ERR(priv->pclk);
 *
 *		// Further setup, display pipeline etc
 *
 *		platform_set_drvdata(pdev, drm);
 *
 *		drm_mode_config_reset(drm);
 *
 *		ret = drm_dev_register(drm);
 *		if (ret)
 *			return ret;
 *
 *		drm_fbdev_{...}_setup(drm, 32);
 *
 *		return 0;
 *	}
 *
 *	// This function is called before the devm_ resources are released
 *	static int driver_remove(struct platform_device *pdev)
 *	{
 *		struct drm_device *drm = platform_get_drvdata(pdev);
 *
 *		drm_dev_unregister(drm);
 *		drm_atomic_helper_shutdown(drm)
 *
 *		return 0;
 *	}
 *
 *	// This function is called on kernel restart and shutdown
 *	static void driver_shutdown(struct platform_device *pdev)
 *	{
 *		drm_atomic_helper_shutdown(platform_get_drvdata(pdev));
 *	}
 *
 *	static int __maybe_unused driver_pm_suspend(struct device *dev)
 *	{
 *		return drm_mode_config_helper_suspend(dev_get_drvdata(dev));
 *	}
 *
 *	static int __maybe_unused driver_pm_resume(struct device *dev)
 *	{
 *		drm_mode_config_helper_resume(dev_get_drvdata(dev));
 *
 *		return 0;
 *	}
 *
 *	static const struct dev_pm_ops driver_pm_ops = {
 *		SET_SYSTEM_SLEEP_PM_OPS(driver_pm_suspend, driver_pm_resume)
 *	};
 *
 *	static struct platform_driver driver_driver = {
 *		.driver = {
 *			[...]
 *			.pm = &driver_pm_ops,
 *		},
 *		.probe = driver_probe,
 *		.remove = driver_remove,
 *		.shutdown = driver_shutdown,
 *	};
 *	module_platform_driver(driver_driver);
 *
 * Drivers that want to support device unplugging (USB, DT overlay unload) should
 * use drm_dev_unplug() instead of drm_dev_unregister(). The driver must protect
 * regions that is accessing device resources to prevent use after they're
 * released. This is done using drm_dev_enter() and drm_dev_exit(). There is one
 * shortcoming however, drm_dev_unplug() marks the drm_device as unplugged before
 * drm_atomic_helper_shutdown() is called. This means that if the disable code
 * paths are protected, they will not run on regular driver module unload,
 * possibly leaving the hardware enabled.
 */

/**
 * drm_put_dev - Unregister and release a DRM device
 * @dev: DRM device
 *
 * Called at module unload time or when a PCI device is unplugged.
 *
 * Cleans up all DRM device, calling drm_lastclose().
 *
 * Note: Use of this function is deprecated. It will eventually go away
 * completely.  Please use drm_dev_unregister() and drm_dev_put() explicitly
 * instead to make sure that the device isn't userspace accessible any more
 * while teardown is in progress, ensuring that userspace can't access an
 * inconsistent state.
 */
void drm_put_dev(struct drm_device *dev)
{
	DRM_DEBUG("\n");

	if (!dev) {
		DRM_ERROR("cleanup called no dev\n");
		return;
	}

	drm_dev_unregister(dev);
	drm_dev_put(dev);
}
EXPORT_SYMBOL(drm_put_dev);

/**
 * drm_dev_enter - Enter device critical section
 * @dev: DRM device
 * @idx: Pointer to index that will be passed to the matching drm_dev_exit()
 *
 * This function marks and protects the beginning of a section that should not
 * be entered after the device has been unplugged. The section end is marked
 * with drm_dev_exit(). Calls to this function can be nested.
 *
 * Returns:
 * True if it is OK to enter the section, false otherwise.
 */
bool drm_dev_enter(struct drm_device *dev, int *idx)
{
#ifdef notyet
	*idx = srcu_read_lock(&drm_unplug_srcu);

	if (dev->unplugged) {
		srcu_read_unlock(&drm_unplug_srcu, *idx);
		return false;
	}
#endif

	return true;
}
EXPORT_SYMBOL(drm_dev_enter);

/**
 * drm_dev_exit - Exit device critical section
 * @idx: index returned from drm_dev_enter()
 *
 * This function marks the end of a section that should not be entered after
 * the device has been unplugged.
 */
void drm_dev_exit(int idx)
{
#ifdef notyet
	srcu_read_unlock(&drm_unplug_srcu, idx);
#endif
}
EXPORT_SYMBOL(drm_dev_exit);

/**
 * drm_dev_unplug - unplug a DRM device
 * @dev: DRM device
 *
 * This unplugs a hotpluggable DRM device, which makes it inaccessible to
 * userspace operations. Entry-points can use drm_dev_enter() and
 * drm_dev_exit() to protect device resources in a race free manner. This
 * essentially unregisters the device like drm_dev_unregister(), but can be
 * called while there are still open users of @dev.
 */
void drm_dev_unplug(struct drm_device *dev)
{
	STUB();
#ifdef notyet
	/*
	 * After synchronizing any critical read section is guaranteed to see
	 * the new value of ->unplugged, and any critical section which might
	 * still have seen the old value of ->unplugged is guaranteed to have
	 * finished.
	 */
	dev->unplugged = true;
	synchronize_srcu(&drm_unplug_srcu);

	drm_dev_unregister(dev);

	/* Clear all CPU mappings pointing to this device */
	unmap_mapping_range(dev->anon_inode->i_mapping, 0, 0, 1);
#endif
}
EXPORT_SYMBOL(drm_dev_unplug);

#ifdef __linux__
/*
 * DRM internal mount
 * We want to be able to allocate our own "struct address_space" to control
 * memory-mappings in VRAM (or stolen RAM, ...). However, core MM does not allow
 * stand-alone address_space objects, so we need an underlying inode. As there
 * is no way to allocate an independent inode easily, we need a fake internal
 * VFS mount-point.
 *
 * The drm_fs_inode_new() function allocates a new inode, drm_fs_inode_free()
 * frees it again. You are allowed to use iget() and iput() to get references to
 * the inode. But each drm_fs_inode_new() call must be paired with exactly one
 * drm_fs_inode_free() call (which does not have to be the last iput()).
 * We use drm_fs_inode_*() to manage our internal VFS mount-point and share it
 * between multiple inode-users. You could, technically, call
 * iget() + drm_fs_inode_free() directly after alloc and sometime later do an
 * iput(), but this way you'd end up with a new vfsmount for each inode.
 */

static int drm_fs_cnt;
static struct vfsmount *drm_fs_mnt;

static int drm_fs_init_fs_context(struct fs_context *fc)
{
	return init_pseudo(fc, 0x010203ff) ? 0 : -ENOMEM;
}

static struct file_system_type drm_fs_type = {
	.name		= "drm",
	.owner		= THIS_MODULE,
	.init_fs_context = drm_fs_init_fs_context,
	.kill_sb	= kill_anon_super,
};

static struct inode *drm_fs_inode_new(void)
{
	struct inode *inode;
	int r;

	r = simple_pin_fs(&drm_fs_type, &drm_fs_mnt, &drm_fs_cnt);
	if (r < 0) {
		DRM_ERROR("Cannot mount pseudo fs: %d\n", r);
		return ERR_PTR(r);
	}

	inode = alloc_anon_inode(drm_fs_mnt->mnt_sb);
	if (IS_ERR(inode))
		simple_release_fs(&drm_fs_mnt, &drm_fs_cnt);

	return inode;
}

static void drm_fs_inode_free(struct inode *inode)
{
	if (inode) {
		iput(inode);
		simple_release_fs(&drm_fs_mnt, &drm_fs_cnt);
	}
}

#endif /* __linux__ */

/**
 * DOC: component helper usage recommendations
 *
 * DRM drivers that drive hardware where a logical device consists of a pile of
 * independent hardware blocks are recommended to use the :ref:`component helper
 * library<component>`. For consistency and better options for code reuse the
 * following guidelines apply:
 *
 *  - The entire device initialization procedure should be run from the
 *    &component_master_ops.master_bind callback, starting with
 *    devm_drm_dev_alloc(), then binding all components with
 *    component_bind_all() and finishing with drm_dev_register().
 *
 *  - The opaque pointer passed to all components through component_bind_all()
 *    should point at &struct drm_device of the device instance, not some driver
 *    specific private structure.
 *
 *  - The component helper fills the niche where further standardization of
 *    interfaces is not practical. When there already is, or will be, a
 *    standardized interface like &drm_bridge or &drm_panel, providing its own
 *    functions to find such components at driver load time, like
 *    drm_of_find_panel_or_bridge(), then the component helper should not be
 *    used.
 */

static void drm_dev_init_release(struct drm_device *dev, void *res)
{
#ifdef __linux__
	drm_fs_inode_free(dev->anon_inode);

	put_device(dev->dev);
#endif
	/* Prevent use-after-free in drm_managed_release when debugging is
	 * enabled. Slightly awkward, but can't really be helped. */
	dev->dev = NULL;
	mutex_destroy(&dev->master_mutex);
	mutex_destroy(&dev->clientlist_mutex);
	mutex_destroy(&dev->filelist_mutex);
	mutex_destroy(&dev->struct_mutex);
}

#ifdef notyet

static int drm_dev_init(struct drm_device *dev,
			const struct drm_driver *driver,
			struct device *parent)
{
	struct inode *inode;
	int ret;

	if (!drm_core_init_complete) {
		DRM_ERROR("DRM core is not initialized\n");
		return -ENODEV;
	}

	if (WARN_ON(!parent))
		return -EINVAL;

	kref_init(&dev->ref);
	dev->dev = get_device(parent);
	dev->driver = driver;

	INIT_LIST_HEAD(&dev->managed.resources);
	spin_lock_init(&dev->managed.lock);

	/* no per-device feature limits by default */
	dev->driver_features = ~0u;

	if (drm_core_check_feature(dev, DRIVER_COMPUTE_ACCEL) &&
				(drm_core_check_feature(dev, DRIVER_RENDER) ||
				drm_core_check_feature(dev, DRIVER_MODESET))) {
		DRM_ERROR("DRM driver can't be both a compute acceleration and graphics driver\n");
		return -EINVAL;
	}

	INIT_LIST_HEAD(&dev->filelist);
	INIT_LIST_HEAD(&dev->filelist_internal);
	INIT_LIST_HEAD(&dev->clientlist);
	INIT_LIST_HEAD(&dev->vblank_event_list);

	spin_lock_init(&dev->event_lock);
	mutex_init(&dev->struct_mutex);
	mutex_init(&dev->filelist_mutex);
	mutex_init(&dev->clientlist_mutex);
	mutex_init(&dev->master_mutex);
	raw_spin_lock_init(&dev->mode_config.panic_lock);

	ret = drmm_add_action_or_reset(dev, drm_dev_init_release, NULL);
	if (ret)
		return ret;

	inode = drm_fs_inode_new();
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		DRM_ERROR("Cannot allocate anonymous inode: %d\n", ret);
		goto err;
	}

	dev->anon_inode = inode;

	if (drm_core_check_feature(dev, DRIVER_COMPUTE_ACCEL)) {
		ret = drm_minor_alloc(dev, DRM_MINOR_ACCEL);
		if (ret)
			goto err;
	} else {
		if (drm_core_check_feature(dev, DRIVER_RENDER)) {
			ret = drm_minor_alloc(dev, DRM_MINOR_RENDER);
			if (ret)
				goto err;
		}

		ret = drm_minor_alloc(dev, DRM_MINOR_PRIMARY);
		if (ret)
			goto err;
	}

	if (drm_core_check_feature(dev, DRIVER_GEM)) {
		ret = drm_gem_init(dev);
		if (ret) {
			DRM_ERROR("Cannot initialize graphics execution manager (GEM)\n");
			goto err;
		}
	}

	dev->unique = drmm_kstrdup(dev, dev_name(parent), GFP_KERNEL);
	if (!dev->unique) {
		ret = -ENOMEM;
		goto err;
	}

	if (drm_core_check_feature(dev, DRIVER_COMPUTE_ACCEL))
		accel_debugfs_init(dev);
	else
		drm_debugfs_dev_init(dev, drm_debugfs_root);

	return 0;

err:
	drm_managed_release(dev);

	return ret;
}

static void devm_drm_dev_init_release(void *data)
{
	drm_dev_put(data);
}

static int devm_drm_dev_init(struct device *parent,
			     struct drm_device *dev,
			     const struct drm_driver *driver)
{
	int ret;

	ret = drm_dev_init(dev, driver, parent);
	if (ret)
		return ret;

	return devm_add_action_or_reset(parent,
					devm_drm_dev_init_release, dev);
}

#endif

void *__devm_drm_dev_alloc(struct device *parent,
			   const struct drm_driver *driver,
			   size_t size, size_t offset)
{
	void *container;
	struct drm_device *drm;
#ifdef notyet
	int ret;
#endif

	container = kzalloc(size, GFP_KERNEL);
	if (!container)
		return ERR_PTR(-ENOMEM);

	drm = container + offset;
#ifdef notyet
	ret = devm_drm_dev_init(parent, drm, driver);
	if (ret) {
		kfree(container);
		return ERR_PTR(ret);
	}
	drmm_add_final_kfree(drm, container);
#endif

	return container;
}
EXPORT_SYMBOL(__devm_drm_dev_alloc);

#ifdef notyet

/**
 * drm_dev_alloc - Allocate new DRM device
 * @driver: DRM driver to allocate device for
 * @parent: Parent device object
 *
 * This is the deprecated version of devm_drm_dev_alloc(), which does not support
 * subclassing through embedding the struct &drm_device in a driver private
 * structure, and which does not support automatic cleanup through devres.
 *
 * RETURNS:
 * Pointer to new DRM device, or ERR_PTR on failure.
 */
struct drm_device *drm_dev_alloc(const struct drm_driver *driver,
				 struct device *parent)
{
	struct drm_device *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	ret = drm_dev_init(dev, driver, parent);
	if (ret) {
		kfree(dev);
		return ERR_PTR(ret);
	}

	drmm_add_final_kfree(dev, dev);

	return dev;
}
EXPORT_SYMBOL(drm_dev_alloc);

#endif

static void drm_dev_release(struct kref *ref)
{
	struct drm_device *dev = container_of(ref, struct drm_device, ref);

	/* Just in case register/unregister was never called */
	drm_debugfs_dev_fini(dev);

	if (dev->driver->release)
		dev->driver->release(dev);

	drm_managed_release(dev);

	kfree(dev->managed.final_kfree);
}

/**
 * drm_dev_get - Take reference of a DRM device
 * @dev: device to take reference of or NULL
 *
 * This increases the ref-count of @dev by one. You *must* already own a
 * reference when calling this. Use drm_dev_put() to drop this reference
 * again.
 *
 * This function never fails. However, this function does not provide *any*
 * guarantee whether the device is alive or running. It only provides a
 * reference to the object and the memory associated with it.
 */
void drm_dev_get(struct drm_device *dev)
{
	if (dev)
		kref_get(&dev->ref);
}
EXPORT_SYMBOL(drm_dev_get);

/**
 * drm_dev_put - Drop reference of a DRM device
 * @dev: device to drop reference of or NULL
 *
 * This decreases the ref-count of @dev by one. The device is destroyed if the
 * ref-count drops to zero.
 */
void drm_dev_put(struct drm_device *dev)
{
	if (dev)
		kref_put(&dev->ref, drm_dev_release);
}
EXPORT_SYMBOL(drm_dev_put);

static int create_compat_control_link(struct drm_device *dev)
{
	struct drm_minor *minor;
	char *name;
	int ret;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return 0;

	minor = *drm_minor_get_slot(dev, DRM_MINOR_PRIMARY);
	if (!minor)
		return 0;

	/*
	 * Some existing userspace out there uses the existing of the controlD*
	 * sysfs files to figure out whether it's a modeset driver. It only does
	 * readdir, hence a symlink is sufficient (and the least confusing
	 * option). Otherwise controlD* is entirely unused.
	 *
	 * Old controlD chardev have been allocated in the range
	 * 64-127.
	 */
	name = kasprintf(GFP_KERNEL, "controlD%d", minor->index + 64);
	if (!name)
		return -ENOMEM;

	ret = sysfs_create_link(minor->kdev->kobj.parent,
				&minor->kdev->kobj,
				name);

	kfree(name);

	return ret;
}

static void remove_compat_control_link(struct drm_device *dev)
{
	struct drm_minor *minor;
	char *name;

	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return;

	minor = *drm_minor_get_slot(dev, DRM_MINOR_PRIMARY);
	if (!minor)
		return;

	name = kasprintf(GFP_KERNEL, "controlD%d", minor->index + 64);
	if (!name)
		return;

	sysfs_remove_link(minor->kdev->kobj.parent, name);

	kfree(name);
}

/**
 * drm_dev_register - Register DRM device
 * @dev: Device to register
 * @flags: Flags passed to the driver's .load() function
 *
 * Register the DRM device @dev with the system, advertise device to user-space
 * and start normal device operation. @dev must be initialized via drm_dev_init()
 * previously.
 *
 * Never call this twice on any device!
 *
 * NOTE: To ensure backward compatibility with existing drivers method this
 * function calls the &drm_driver.load method after registering the device
 * nodes, creating race conditions. Usage of the &drm_driver.load methods is
 * therefore deprecated, drivers must perform all initialization before calling
 * drm_dev_register().
 *
 * RETURNS:
 * 0 on success, negative error code on failure.
 */
int drm_dev_register(struct drm_device *dev, unsigned long flags)
{
	const struct drm_driver *driver = dev->driver;
	int ret;

	if (!driver->load)
		drm_mode_config_validate(dev);

#ifdef notyet
	/* drm softc not allocated by devm_drm_dev_alloc() */
	WARN_ON(!dev->managed.final_kfree);
#endif

	if (drm_dev_needs_global_mutex(dev))
		mutex_lock(&drm_global_mutex);

	if (drm_core_check_feature(dev, DRIVER_COMPUTE_ACCEL))
		accel_debugfs_register(dev);
	else
		drm_debugfs_dev_register(dev);

	ret = drm_minor_register(dev, DRM_MINOR_RENDER);
	if (ret)
		goto err_minors;

	ret = drm_minor_register(dev, DRM_MINOR_PRIMARY);
	if (ret)
		goto err_minors;

	ret = drm_minor_register(dev, DRM_MINOR_ACCEL);
	if (ret)
		goto err_minors;

	ret = create_compat_control_link(dev);
	if (ret)
		goto err_minors;

	dev->registered = true;

	if (driver->load) {
		ret = driver->load(dev, flags);
		if (ret)
			goto err_minors;
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = drm_modeset_register_all(dev);
		if (ret)
			goto err_unload;
	}
	drm_panic_register(dev);

	DRM_INFO("Initialized %s %d.%d.%d for %s on minor %d\n",
		 driver->name, driver->major, driver->minor,
		 driver->patchlevel,
		 dev->dev ? dev_name(dev->dev) : "virtual device",
		 dev->primary ? dev->primary->index : dev->accel->index);

	goto out_unlock;

err_unload:
	if (dev->driver->unload)
		dev->driver->unload(dev);
err_minors:
	remove_compat_control_link(dev);
	drm_minor_unregister(dev, DRM_MINOR_ACCEL);
	drm_minor_unregister(dev, DRM_MINOR_PRIMARY);
	drm_minor_unregister(dev, DRM_MINOR_RENDER);
out_unlock:
	if (drm_dev_needs_global_mutex(dev))
		mutex_unlock(&drm_global_mutex);
	return ret;
}
EXPORT_SYMBOL(drm_dev_register);

/**
 * drm_dev_unregister - Unregister DRM device
 * @dev: Device to unregister
 *
 * Unregister the DRM device from the system. This does the reverse of
 * drm_dev_register() but does not deallocate the device. The caller must call
 * drm_dev_put() to drop their final reference, unless it is managed with devres
 * (as devices allocated with devm_drm_dev_alloc() are), in which case there is
 * already an unwind action registered.
 *
 * A special form of unregistering for hotpluggable devices is drm_dev_unplug(),
 * which can be called while there are still open users of @dev.
 *
 * This should be called first in the device teardown code to make sure
 * userspace can't access the device instance any more.
 */
void drm_dev_unregister(struct drm_device *dev)
{
	dev->registered = false;

	drm_panic_unregister(dev);

	drm_client_dev_unregister(dev);

	if (drm_core_check_feature(dev, DRIVER_MODESET))
		drm_modeset_unregister_all(dev);

	if (dev->driver->unload)
		dev->driver->unload(dev);

	remove_compat_control_link(dev);
	drm_minor_unregister(dev, DRM_MINOR_ACCEL);
	drm_minor_unregister(dev, DRM_MINOR_PRIMARY);
	drm_minor_unregister(dev, DRM_MINOR_RENDER);
	drm_debugfs_dev_fini(dev);
}
EXPORT_SYMBOL(drm_dev_unregister);

/*
 * DRM Core
 * The DRM core module initializes all global DRM objects and makes them
 * available to drivers. Once setup, drivers can probe their respective
 * devices.
 * Currently, core management includes:
 *  - The "DRM-Global" key/value database
 *  - Global ID management for connectors
 *  - DRM major number allocation
 *  - DRM minor management
 *  - DRM sysfs class
 *  - DRM debugfs root
 *
 * Furthermore, the DRM core provides dynamic char-dev lookups. For each
 * interface registered on a DRM device, you can request minor numbers from DRM
 * core. DRM core takes care of major-number management and char-dev
 * registration. A stub ->open() callback forwards any open() requests to the
 * registered minor.
 */

#ifdef __linux__
static int drm_stub_open(struct inode *inode, struct file *filp)
{
	const struct file_operations *new_fops;
	struct drm_minor *minor;
	int err;

	DRM_DEBUG("\n");

	minor = drm_minor_acquire(&drm_minors_xa, iminor(inode));
	if (IS_ERR(minor))
		return PTR_ERR(minor);

	new_fops = fops_get(minor->dev->driver->fops);
	if (!new_fops) {
		err = -ENODEV;
		goto out;
	}

	replace_fops(filp, new_fops);
	if (filp->f_op->open)
		err = filp->f_op->open(inode, filp);
	else
		err = 0;

out:
	drm_minor_release(minor);

	return err;
}

static const struct file_operations drm_stub_fops = {
	.owner = THIS_MODULE,
	.open = drm_stub_open,
	.llseek = noop_llseek,
};
#endif /* __linux__ */

static void drm_core_exit(void)
{
	drm_privacy_screen_lookup_exit();
	drm_panic_exit();
	accel_core_exit();
#ifdef __linux__
	unregister_chrdev(DRM_MAJOR, "drm");
	debugfs_remove(drm_debugfs_root);
	drm_sysfs_destroy();
#endif
	WARN_ON(!xa_empty(&drm_minors_xa));
	drm_connector_ida_destroy();
}

static int __init drm_core_init(void)
{
#ifdef __linux__
	int ret;
#endif

	drm_connector_ida_init();
	drm_memcpy_init_early();

#ifdef __linux__
	ret = drm_sysfs_init();
	if (ret < 0) {
		DRM_ERROR("Cannot create DRM class: %d\n", ret);
		goto error;
	}

	drm_debugfs_root = debugfs_create_dir("dri", NULL);

	ret = register_chrdev(DRM_MAJOR, "drm", &drm_stub_fops);
	if (ret < 0)
		goto error;

	ret = accel_core_init();
	if (ret < 0)
		goto error;
#endif

	drm_panic_init();

	drm_privacy_screen_lookup_init();

	drm_core_init_complete = true;

	DRM_DEBUG("Initialized\n");
	return 0;
#ifdef __linux__
error:
	drm_core_exit();
	return ret;
#endif
}

#ifdef __linux__
module_init(drm_core_init);
module_exit(drm_core_exit);
#endif

void drm_lastclose(struct drm_device *dev);

void
drm_attach_platform(struct drm_driver *driver, bus_space_tag_t iot,
    bus_dma_tag_t dmat, struct device *dev, struct drm_device *drm)
{
	struct drm_attach_args arg;

	memset(&arg, 0, sizeof(arg));
	arg.driver = driver;
	arg.bst = iot;
	arg.dmat = dmat;
	arg.drm = drm;

	arg.busid = dev->dv_xname;
	arg.busid_len = strlen(dev->dv_xname) + 1;
	config_found_sm(dev, &arg, drmprint, drmsubmatch);
}

struct drm_device *
drm_attach_pci(const struct drm_driver *driver, struct pci_attach_args *pa,
    int is_agp, int primary, struct device *dev, struct drm_device *drm)
{
	struct drm_attach_args arg;
	struct drm_softc *sc;

	arg.drm = drm;
	arg.driver = driver;
	arg.dmat = pa->pa_dmat;
	arg.bst = pa->pa_memt;
	arg.is_agp = is_agp;
	arg.primary = primary;
	arg.pa = pa;

	arg.busid_len = 20;
	arg.busid = malloc(arg.busid_len + 1, M_DRM, M_NOWAIT);
	if (arg.busid == NULL) {
		printf("%s: no memory for drm\n", dev->dv_xname);
		return (NULL);
	}
	snprintf(arg.busid, arg.busid_len, "pci:%04x:%02x:%02x.%1x",
	    pa->pa_domain, pa->pa_bus, pa->pa_device, pa->pa_function);

	sc = (struct drm_softc *)config_found_sm(dev, &arg, drmprint, drmsubmatch);
	if (sc == NULL)
		return NULL;
	
	return sc->sc_drm;
}

int
drmprint(void *aux, const char *pnp)
{
	if (pnp != NULL)
		printf("drm at %s", pnp);
	return (UNCONF);
}

int
drmsubmatch(struct device *parent, void *match, void *aux)
{
	extern struct cfdriver drm_cd;
	struct cfdata *cf = match;

	/* only allow drm to attach */
	if (cf->cf_driver == &drm_cd)
		return ((*cf->cf_attach->ca_match)(parent, match, aux));
	return (0);
}

int
drm_pciprobe(struct pci_attach_args *pa, const struct pci_device_id *idlist)
{
	const struct pci_device_id *id_entry;

	id_entry = drm_find_description(PCI_VENDOR(pa->pa_id),
	    PCI_PRODUCT(pa->pa_id), idlist);
	if (id_entry != NULL)
		return 1;

	return 0;
}

int
drm_probe(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct drm_attach_args *da = aux;

	if (cf->drmdevcf_primary != DRMDEVCF_PRIMARY_UNK) {
		/*
		 * If primary-ness of device specified, either match
		 * exactly (at high priority), or fail.
		 */
		if (cf->drmdevcf_primary != 0 && da->primary != 0)
			return (10);
		else
			return (0);
	}

	/* If primary-ness unspecified, it wins. */
	return (1);
}

int drm_buddy_module_init(void);
void drm_buddy_module_exit(void);

void
drm_attach(struct device *parent, struct device *self, void *aux)
{
	struct drm_softc *sc = (struct drm_softc *)self;
	struct drm_attach_args *da = aux;
	struct drm_device *dev = da->drm;
	int ret;

	if (drm_refcnt == 0) {
		drm_linux_init();
		drm_core_init();
		drm_buddy_module_init();
	}
	drm_refcnt++;

	if (dev == NULL) {
		dev = malloc(sizeof(struct drm_device), M_DRM,
		    M_WAITOK | M_ZERO);
		sc->sc_allocated = 1;
	}

	sc->sc_drm = dev;

	kref_init(&dev->ref);
	dev->dev = self;
	dev->dev_private = parent;
	dev->driver = da->driver;

	INIT_LIST_HEAD(&dev->managed.resources);
	mtx_init(&dev->managed.lock, IPL_TTY);

	/* no per-device feature limits by default */
	dev->driver_features = ~0u;

	dev->dmat = da->dmat;
	dev->bst = da->bst;
	dev->unique = da->busid;

	if (da->pa) {
		struct pci_attach_args *pa = da->pa;
		pcireg_t subsys;

		subsys = pci_conf_read(pa->pa_pc, pa->pa_tag,
		    PCI_SUBSYS_ID_REG);

		dev->pdev = &dev->_pdev;
		dev->pdev->vendor = PCI_VENDOR(pa->pa_id);
		dev->pdev->device = PCI_PRODUCT(pa->pa_id);
		dev->pdev->subsystem_vendor = PCI_VENDOR(subsys);
		dev->pdev->subsystem_device = PCI_PRODUCT(subsys);
		dev->pdev->revision = PCI_REVISION(pa->pa_class);
		dev->pdev->class = (PCI_CLASS(pa->pa_class) << 16) |
		    (PCI_SUBCLASS(pa->pa_class) << 8) |
		    PCI_INTERFACE(pa->pa_class);

		dev->pdev->devfn = PCI_DEVFN(pa->pa_device, pa->pa_function);
		dev->pdev->bus = &dev->pdev->_bus;
		dev->pdev->bus->pc = pa->pa_pc;
		dev->pdev->bus->number = pa->pa_bus;
		dev->pdev->bus->domain_nr = pa->pa_domain;
		dev->pdev->bus->bridgetag = pa->pa_bridgetag;

		if (pa->pa_bridgetag != NULL) {
			dev->pdev->bus->self = malloc(sizeof(struct pci_dev),
			    M_DRM, M_WAITOK | M_ZERO);
			dev->pdev->bus->self->pc = pa->pa_pc;
			dev->pdev->bus->self->tag = *pa->pa_bridgetag;
		}

		dev->pdev->pc = pa->pa_pc;
		dev->pdev->tag = pa->pa_tag;
		dev->pdev->pci = (struct pci_softc *)parent->dv_parent;
		dev->pdev->_dev = parent;

#ifdef CONFIG_ACPI
		dev->pdev->dev.node = acpi_find_pci(pa->pa_pc, pa->pa_tag);
		aml_register_notify(dev->pdev->dev.node, NULL,
		    drm_linux_acpi_notify, NULL, ACPIDEV_NOPOLL);
#endif
	}

	mtx_init(&dev->quiesce_mtx, IPL_NONE);
	mtx_init(&dev->event_lock, IPL_TTY);
	rw_init(&dev->struct_mutex, "drmdevlk");
	rw_init(&dev->filelist_mutex, "drmflist");
	rw_init(&dev->clientlist_mutex, "drmclist");
	rw_init(&dev->master_mutex, "drmmast");

	ret = drmm_add_action(dev, drm_dev_init_release, NULL);
	if (ret)
		goto error;

	SPLAY_INIT(&dev->files);
	INIT_LIST_HEAD(&dev->filelist_internal);
	INIT_LIST_HEAD(&dev->clientlist);
	INIT_LIST_HEAD(&dev->vblank_event_list);

	if (drm_core_check_feature(dev, DRIVER_RENDER)) {
		ret = drm_minor_alloc(dev, DRM_MINOR_RENDER);
		if (ret)
			goto error;
	}

	ret = drm_minor_alloc(dev, DRM_MINOR_PRIMARY);
	if (ret)
		goto error;

#ifdef CONFIG_DRM_LEGACY
	if (drm_core_check_feature(dev, DRIVER_USE_AGP)) {
#if IS_ENABLED(CONFIG_AGP)
		if (da->is_agp)
			dev->agp = drm_agp_init();
#endif
		if (dev->agp != NULL) {
			if (drm_mtrr_add(dev->agp->info.ai_aperture_base,
			    dev->agp->info.ai_aperture_size, DRM_MTRR_WC) == 0)
				dev->agp->mtrr = 1;
		}
	}
#endif

	if (dev->driver->gem_size > 0) {
		KASSERT(dev->driver->gem_size >= sizeof(struct drm_gem_object));
		/* XXX unique name */
		pool_init(&dev->objpl, dev->driver->gem_size, 0, IPL_NONE, 0,
		    "drmobjpl", NULL);
	}

	if (drm_core_check_feature(dev, DRIVER_GEM)) {
		ret = drm_gem_init(dev);
		if (ret) {
			DRM_ERROR("Cannot initialize graphics execution manager (GEM)\n");
			goto error;
		}
	}

	drmm_add_final_kfree(dev, NULL);

	printf("\n");
	return;

error:
	drm_managed_release(dev);
	dev->dev_private = NULL;
}

int
drm_detach(struct device *self, int flags)
{
	struct drm_softc *sc = (struct drm_softc *)self;
	struct drm_device *dev = sc->sc_drm;

	drm_dev_put(dev);

	drm_refcnt--;
	if (drm_refcnt == 0) {
		drm_buddy_module_exit();
		drm_core_exit();
		drm_linux_exit();
	}

	drm_lastclose(dev);

	if (drm_core_check_feature(dev, DRIVER_GEM)) {
		if (dev->driver->gem_size > 0)
			pool_destroy(&dev->objpl);
	}

#ifdef CONFIG_DRM_LEGACY
	if (dev->agp && dev->agp->mtrr) {
		int retcode;

		retcode = drm_mtrr_del(0, dev->agp->info.ai_aperture_base,
		    dev->agp->info.ai_aperture_size, DRM_MTRR_WC);
		DRM_DEBUG("mtrr_del = %d", retcode);
	}

	free(dev->agp, M_DRM, 0);
#endif
	if (dev->pdev && dev->pdev->bus)
		free(dev->pdev->bus->self, M_DRM, sizeof(struct pci_dev));

	if (sc->sc_allocated)
		free(dev, M_DRM, sizeof(struct drm_device));

	return 0;
}

void
drm_quiesce(struct drm_device *dev)
{
	mtx_enter(&dev->quiesce_mtx);
	dev->quiesce = 1;
	while (dev->quiesce_count > 0) {
		msleep_nsec(&dev->quiesce_count, &dev->quiesce_mtx,
		    PZERO, "drmqui", INFSLP);
	}
	mtx_leave(&dev->quiesce_mtx);
}

void
drm_wakeup(struct drm_device *dev)
{
	mtx_enter(&dev->quiesce_mtx);
	dev->quiesce = 0;
	wakeup(&dev->quiesce);
	mtx_leave(&dev->quiesce_mtx);
}

int
drm_activate(struct device *self, int act)
{
	struct drm_softc *sc = (struct drm_softc *)self;
	struct drm_device *dev = sc->sc_drm;

	switch (act) {
	case DVACT_QUIESCE:
#ifdef CONFIG_ACPI
		if (acpi_softc) {
			switch (acpi_softc->sc_state) {
			case ACPI_STATE_S0:
				pm_suspend_target_state = PM_SUSPEND_TO_IDLE;
				break;
			case ACPI_STATE_S3:
				pm_suspend_target_state = PM_SUSPEND_MEM;
				break;
			}
		}
#else
		pm_suspend_target_state = PM_SUSPEND_TO_IDLE;
#endif
		drm_quiesce(dev);
		break;
	case DVACT_WAKEUP:
		drm_wakeup(dev);
		pm_suspend_target_state = PM_SUSPEND_ON;
		break;
	}

	return (0);
}

const struct cfattach drm_ca = {
	sizeof(struct drm_softc), drm_probe, drm_attach,
	drm_detach, drm_activate
};

struct cfdriver drm_cd = {
	NULL, "drm", DV_DULL
};

const struct pci_device_id *
drm_find_description(int vendor, int device, const struct pci_device_id *idlist)
{
	int i = 0;
	
	for (i = 0; idlist[i].vendor != 0; i++) {
		if ((idlist[i].vendor == vendor) &&
		    (idlist[i].device == device ||
		     idlist[i].device == PCI_ANY_ID) &&
		    (idlist[i].subvendor == PCI_ANY_ID) &&
		    (idlist[i].subdevice == PCI_ANY_ID))
			return &idlist[i];
	}
	return NULL;
}

int
drm_file_cmp(struct drm_file *f1, struct drm_file *f2)
{
	return (f1->fminor < f2->fminor ? -1 : f1->fminor > f2->fminor);
}

SPLAY_GENERATE(drm_file_tree, drm_file, link, drm_file_cmp);

struct drm_file *
drm_find_file_by_minor(struct drm_device *dev, int minor)
{
	struct drm_file	key;
	
	key.fminor = minor;
	return (SPLAY_FIND(drm_file_tree, &dev->files, &key));
}

struct drm_device *
drm_get_device_from_kdev(dev_t kdev)
{
	int unit = minor(kdev) & ((1 << CLONE_SHIFT) - 1);
	/* render */
	if (unit >= 128)
		unit -= 128;
	struct drm_softc *sc;

	if (unit < drm_cd.cd_ndevs) {
		sc = (struct drm_softc *)drm_cd.cd_devs[unit];
		if (sc)
			return sc->sc_drm;
	}

	return NULL;
}

void
filt_drmdetach(struct knote *kn)
{
	struct drm_device *dev = kn->kn_hook;
	int s;

	s = spltty();
	klist_remove_locked(&dev->note, kn);
	splx(s);
}

int
filt_drmkms(struct knote *kn, long hint)
{
	if (kn->kn_sfflags & hint)
		kn->kn_fflags |= hint;
	return (kn->kn_fflags != 0);
}

void
filt_drmreaddetach(struct knote *kn)
{
	struct drm_file		*file_priv = kn->kn_hook;
	int s;

	s = spltty();
	klist_remove_locked(&file_priv->rsel.si_note, kn);
	splx(s);
}

int
filt_drmread(struct knote *kn, long hint)
{
	struct drm_file		*file_priv = kn->kn_hook;
	int			 val = 0;

	if ((hint & NOTE_SUBMIT) == 0)
		mtx_enter(&file_priv->minor->dev->event_lock);
	val = !list_empty(&file_priv->event_list);
	if ((hint & NOTE_SUBMIT) == 0)
		mtx_leave(&file_priv->minor->dev->event_lock);
	return (val);
}

const struct filterops drm_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_drmdetach,
	.f_event	= filt_drmkms,
};

const struct filterops drmread_filtops = {
	.f_flags	= FILTEROP_ISFD,
	.f_attach	= NULL,
	.f_detach	= filt_drmreaddetach,
	.f_event	= filt_drmread,
};

int
drmkqfilter(dev_t kdev, struct knote *kn)
{
	struct drm_device	*dev = NULL;
	struct drm_file		*file_priv = NULL;
	int			 s;

	dev = drm_get_device_from_kdev(kdev);
	if (dev == NULL || dev->dev_private == NULL)
		return (ENXIO);

	switch (kn->kn_filter) {
	case EVFILT_READ:
		mutex_lock(&dev->struct_mutex);
		file_priv = drm_find_file_by_minor(dev, minor(kdev));
		mutex_unlock(&dev->struct_mutex);
		if (file_priv == NULL)
			return (ENXIO);

		kn->kn_fop = &drmread_filtops;
		kn->kn_hook = file_priv;

		s = spltty();
		klist_insert_locked(&file_priv->rsel.si_note, kn);
		splx(s);
		break;
	case EVFILT_DEVICE:
		kn->kn_fop = &drm_filtops;
		kn->kn_hook = dev;

		s = spltty();
		klist_insert_locked(&dev->note, kn);
		splx(s);
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

int
drmopen(dev_t kdev, int flags, int fmt, struct proc *p)
{
	struct drm_device	*dev = NULL;
	struct drm_file		*file_priv;
	struct drm_minor	*dm;
	int			 ret = 0;
	int			 dminor, realminor, minor_type;

	dev = drm_get_device_from_kdev(kdev);
	if (dev == NULL || dev->dev_private == NULL)
		return (ENXIO);

	DRM_DEBUG("open_count = %d\n", atomic_read(&dev->open_count));

	if (flags & O_EXCL)
		return (EBUSY); /* No exclusive opens */

	if (drm_dev_needs_global_mutex(dev))
		mutex_lock(&drm_global_mutex);

	atomic_fetch_inc(&dev->open_count);

	dminor = minor(kdev);
	realminor =  dminor & ((1 << CLONE_SHIFT) - 1);
	if (realminor < 64)
		minor_type = DRM_MINOR_PRIMARY;
	else if (realminor >= 128 && realminor < 192)
		minor_type = DRM_MINOR_RENDER;
	else {
		ret = ENXIO;
		goto err;
	}

	dm = *drm_minor_get_slot(dev, minor_type);
	if (dm == NULL) {
		ret = ENXIO;
		goto err;
	}
	dm->index = minor(kdev);

	file_priv = drm_file_alloc(dm);
	if (IS_ERR(file_priv)) {
		ret = ENOMEM;
		goto err;
	}

	/* first opener automatically becomes master */
	if (drm_is_primary_client(file_priv)) {
		ret = drm_master_open(file_priv);
		if (ret != 0)
			goto out_file_free;
	}

	file_priv->filp = (void *)file_priv;
	file_priv->fminor = minor(kdev);

	mutex_lock(&dev->filelist_mutex);
	SPLAY_INSERT(drm_file_tree, &dev->files, file_priv);
	mutex_unlock(&dev->filelist_mutex);

	if (drm_dev_needs_global_mutex(dev))
		mutex_unlock(&drm_global_mutex);

	return 0;

out_file_free:
	drm_file_free(file_priv);
err:
	atomic_dec(&dev->open_count);
	if (drm_dev_needs_global_mutex(dev))
		mutex_unlock(&drm_global_mutex);
	return (ret);
}

int
drmclose(dev_t kdev, int flags, int fmt, struct proc *p)
{
	struct drm_device		*dev = drm_get_device_from_kdev(kdev);
	struct drm_file			*file_priv;
	int				 retcode = 0;

	if (dev == NULL)
		return (ENXIO);

	if (drm_dev_needs_global_mutex(dev))
		mutex_lock(&drm_global_mutex);

	DRM_DEBUG("open_count = %d\n", atomic_read(&dev->open_count));

	mutex_lock(&dev->filelist_mutex);
	file_priv = drm_find_file_by_minor(dev, minor(kdev));
	if (file_priv == NULL) {
		DRM_ERROR("can't find authenticator\n");
		retcode = EINVAL;
		mutex_unlock(&dev->filelist_mutex);
		goto done;
	}

	SPLAY_REMOVE(drm_file_tree, &dev->files, file_priv);
	mutex_unlock(&dev->filelist_mutex);
	drm_file_free(file_priv);
done:
	if (atomic_dec_and_test(&dev->open_count))
		drm_lastclose(dev);

	if (drm_dev_needs_global_mutex(dev))
		mutex_unlock(&drm_global_mutex);

	return (retcode);
}

int
drmread(dev_t kdev, struct uio *uio, int ioflag)
{
	struct drm_device		*dev = drm_get_device_from_kdev(kdev);
	struct drm_file			*file_priv;
	struct drm_pending_event	*ev;
	int		 		 error = 0;

	if (dev == NULL)
		return (ENXIO);

	mutex_lock(&dev->filelist_mutex);
	file_priv = drm_find_file_by_minor(dev, minor(kdev));
	mutex_unlock(&dev->filelist_mutex);
	if (file_priv == NULL)
		return (ENXIO);

	/*
	 * The semantics are a little weird here. We will wait until we
	 * have events to process, but as soon as we have events we will
	 * only deliver as many as we have.
	 * Note that events are atomic, if the read buffer will not fit in
	 * a whole event, we won't read any of it out.
	 */
	mtx_enter(&dev->event_lock);
	while (error == 0 && list_empty(&file_priv->event_list)) {
		if (ioflag & IO_NDELAY) {
			mtx_leave(&dev->event_lock);
			return (EAGAIN);
		}
		error = msleep_nsec(&file_priv->event_wait, &dev->event_lock,
		    PWAIT | PCATCH, "drmread", INFSLP);
	}
	if (error) {
		mtx_leave(&dev->event_lock);
		return (error);
	}
	while (drm_dequeue_event(dev, file_priv, uio->uio_resid, &ev)) {
		MUTEX_ASSERT_UNLOCKED(&dev->event_lock);
		/* XXX we always destroy the event on error. */
		error = uiomove(ev->event, ev->event->length, uio);
		kfree(ev);
		if (error)
			break;
		mtx_enter(&dev->event_lock);
	}
	MUTEX_ASSERT_UNLOCKED(&dev->event_lock);

	return (error);
}

/*
 * Deqeue an event from the file priv in question. returning 1 if an
 * event was found. We take the resid from the read as a parameter because
 * we will only dequeue and event if the read buffer has space to fit the
 * entire thing.
 *
 * We are called locked, but we will *unlock* the queue on return so that
 * we may sleep to copyout the event.
 */
int
drm_dequeue_event(struct drm_device *dev, struct drm_file *file_priv,
    size_t resid, struct drm_pending_event **out)
{
	struct drm_pending_event *e = NULL;
	int gotone = 0;

	MUTEX_ASSERT_LOCKED(&dev->event_lock);

	*out = NULL;
	if (list_empty(&file_priv->event_list))
		goto out;
	e = list_first_entry(&file_priv->event_list,
			     struct drm_pending_event, link);
	if (e->event->length > resid)
		goto out;

	file_priv->event_space += e->event->length;
	list_del(&e->link);
	*out = e;
	gotone = 1;

out:
	mtx_leave(&dev->event_lock);

	return (gotone);
}

paddr_t
drmmmap(dev_t kdev, off_t offset, int prot)
{
	return -1;
}

struct drm_dmamem *
drm_dmamem_alloc(bus_dma_tag_t dmat, bus_size_t size, bus_size_t alignment,
    int nsegments, bus_size_t maxsegsz, int mapflags, int loadflags)
{
	struct drm_dmamem	*mem;
	size_t			 strsize;
	/*
	 * segs is the last member of the struct since we modify the size 
	 * to allow extra segments if more than one are allowed.
	 */
	strsize = sizeof(*mem) + (sizeof(bus_dma_segment_t) * (nsegments - 1));
	mem = malloc(strsize, M_DRM, M_NOWAIT | M_ZERO);
	if (mem == NULL)
		return (NULL);

	mem->size = size;

	if (bus_dmamap_create(dmat, size, nsegments, maxsegsz, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &mem->map) != 0)
		goto strfree;

	if (bus_dmamem_alloc(dmat, size, alignment, 0, mem->segs, nsegments,
	    &mem->nsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0)
		goto destroy;

	if (bus_dmamem_map(dmat, mem->segs, mem->nsegs, size, 
	    &mem->kva, BUS_DMA_NOWAIT | mapflags) != 0)
		goto free;

	if (bus_dmamap_load(dmat, mem->map, mem->kva, size,
	    NULL, BUS_DMA_NOWAIT | loadflags) != 0)
		goto unmap;

	return (mem);

unmap:
	bus_dmamem_unmap(dmat, mem->kva, size);
free:
	bus_dmamem_free(dmat, mem->segs, mem->nsegs);
destroy:
	bus_dmamap_destroy(dmat, mem->map);
strfree:
	free(mem, M_DRM, 0);

	return (NULL);
}

void
drm_dmamem_free(bus_dma_tag_t dmat, struct drm_dmamem *mem)
{
	if (mem == NULL)
		return;

	bus_dmamap_unload(dmat, mem->map);
	bus_dmamem_unmap(dmat, mem->kva, mem->size);
	bus_dmamem_free(dmat, mem->segs, mem->nsegs);
	bus_dmamap_destroy(dmat, mem->map);
	free(mem, M_DRM, 0);
}

/*
 * Compute order.  Can be made faster.
 */
int
drm_order(unsigned long size)
{
	int order;
	unsigned long tmp;

	for (order = 0, tmp = size; tmp >>= 1; ++order)
		;

	if (size & ~(1 << order))
		++order;

	return order;
}

int
drm_getpciinfo(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_pciinfo *info = data;

	if (dev->pdev == NULL)
		return -ENOTTY;

	info->domain = dev->pdev->bus->domain_nr;
	info->bus = dev->pdev->bus->number;
	info->dev = PCI_SLOT(dev->pdev->devfn);
	info->func = PCI_FUNC(dev->pdev->devfn);
	info->vendor_id = dev->pdev->vendor;
	info->device_id = dev->pdev->device;
	info->subvendor_id = dev->pdev->subsystem_vendor;
	info->subdevice_id = dev->pdev->subsystem_device;
	info->revision_id = 0;

	return 0;
}
