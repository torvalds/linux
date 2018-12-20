/*
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright (c) 2009-2010, Code Aurora Forum.
 * Copyright 2016 Intel Corp.
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
 */

#ifndef _DRM_DRV_H_
#define _DRM_DRV_H_

#include <linux/list.h>
#include <linux/irqreturn.h>

#include <drm/drm_device.h>

struct drm_file;
struct drm_gem_object;
struct drm_master;
struct drm_minor;
struct dma_buf_attachment;
struct drm_display_mode;
struct drm_mode_create_dumb;
struct drm_printer;

/* driver capabilities and requirements mask */
#define DRIVER_USE_AGP			0x1
#define DRIVER_LEGACY			0x2
#define DRIVER_PCI_DMA			0x8
#define DRIVER_SG			0x10
#define DRIVER_HAVE_DMA			0x20
#define DRIVER_HAVE_IRQ			0x40
#define DRIVER_IRQ_SHARED		0x80
#define DRIVER_GEM			0x1000
#define DRIVER_MODESET			0x2000
#define DRIVER_PRIME			0x4000
#define DRIVER_RENDER			0x8000
#define DRIVER_ATOMIC			0x10000
#define DRIVER_KMS_LEGACY_CONTEXT	0x20000
#define DRIVER_SYNCOBJ                  0x40000

/**
 * struct drm_driver - DRM driver structure
 *
 * This structure represent the common code for a family of cards. There will
 * one drm_device for each card present in this family. It contains lots of
 * vfunc entries, and a pile of those probably should be moved to more
 * appropriate places like &drm_mode_config_funcs or into a new operations
 * structure for GEM drivers.
 */
struct drm_driver {
	/**
	 * @load:
	 *
	 * Backward-compatible driver callback to complete
	 * initialization steps after the driver is registered.  For
	 * this reason, may suffer from race conditions and its use is
	 * deprecated for new drivers.  It is therefore only supported
	 * for existing drivers not yet converted to the new scheme.
	 * See drm_dev_init() and drm_dev_register() for proper and
	 * race-free way to set up a &struct drm_device.
	 *
	 * This is deprecated, do not use!
	 *
	 * Returns:
	 *
	 * Zero on success, non-zero value on failure.
	 */
	int (*load) (struct drm_device *, unsigned long flags);

	/**
	 * @open:
	 *
	 * Driver callback when a new &struct drm_file is opened. Useful for
	 * setting up driver-private data structures like buffer allocators,
	 * execution contexts or similar things. Such driver-private resources
	 * must be released again in @postclose.
	 *
	 * Since the display/modeset side of DRM can only be owned by exactly
	 * one &struct drm_file (see &drm_file.is_master and &drm_device.master)
	 * there should never be a need to set up any modeset related resources
	 * in this callback. Doing so would be a driver design bug.
	 *
	 * Returns:
	 *
	 * 0 on success, a negative error code on failure, which will be
	 * promoted to userspace as the result of the open() system call.
	 */
	int (*open) (struct drm_device *, struct drm_file *);

	/**
	 * @postclose:
	 *
	 * One of the driver callbacks when a new &struct drm_file is closed.
	 * Useful for tearing down driver-private data structures allocated in
	 * @open like buffer allocators, execution contexts or similar things.
	 *
	 * Since the display/modeset side of DRM can only be owned by exactly
	 * one &struct drm_file (see &drm_file.is_master and &drm_device.master)
	 * there should never be a need to tear down any modeset related
	 * resources in this callback. Doing so would be a driver design bug.
	 */
	void (*postclose) (struct drm_device *, struct drm_file *);

	/**
	 * @lastclose:
	 *
	 * Called when the last &struct drm_file has been closed and there's
	 * currently no userspace client for the &struct drm_device.
	 *
	 * Modern drivers should only use this to force-restore the fbdev
	 * framebuffer using drm_fb_helper_restore_fbdev_mode_unlocked().
	 * Anything else would indicate there's something seriously wrong.
	 * Modern drivers can also use this to execute delayed power switching
	 * state changes, e.g. in conjunction with the :ref:`vga_switcheroo`
	 * infrastructure.
	 *
	 * This is called after @postclose hook has been called.
	 *
	 * NOTE:
	 *
	 * All legacy drivers use this callback to de-initialize the hardware.
	 * This is purely because of the shadow-attach model, where the DRM
	 * kernel driver does not really own the hardware. Instead ownershipe is
	 * handled with the help of userspace through an inheritedly racy dance
	 * to set/unset the VT into raw mode.
	 *
	 * Legacy drivers initialize the hardware in the @firstopen callback,
	 * which isn't even called for modern drivers.
	 */
	void (*lastclose) (struct drm_device *);

	/**
	 * @unload:
	 *
	 * Reverse the effects of the driver load callback.  Ideally,
	 * the clean up performed by the driver should happen in the
	 * reverse order of the initialization.  Similarly to the load
	 * hook, this handler is deprecated and its usage should be
	 * dropped in favor of an open-coded teardown function at the
	 * driver layer.  See drm_dev_unregister() and drm_dev_put()
	 * for the proper way to remove a &struct drm_device.
	 *
	 * The unload() hook is called right after unregistering
	 * the device.
	 *
	 */
	void (*unload) (struct drm_device *);

	/**
	 * @release:
	 *
	 * Optional callback for destroying device data after the final
	 * reference is released, i.e. the device is being destroyed. Drivers
	 * using this callback are responsible for calling drm_dev_fini()
	 * to finalize the device and then freeing the struct themselves.
	 */
	void (*release) (struct drm_device *);

	/**
	 * @get_vblank_counter:
	 *
	 * Driver callback for fetching a raw hardware vblank counter for the
	 * CRTC specified with the pipe argument.  If a device doesn't have a
	 * hardware counter, the driver can simply leave the hook as NULL.
	 * The DRM core will account for missed vblank events while interrupts
	 * where disabled based on system timestamps.
	 *
	 * Wraparound handling and loss of events due to modesetting is dealt
	 * with in the DRM core code, as long as drivers call
	 * drm_crtc_vblank_off() and drm_crtc_vblank_on() when disabling or
	 * enabling a CRTC.
	 *
	 * This is deprecated and should not be used by new drivers.
	 * Use &drm_crtc_funcs.get_vblank_counter instead.
	 *
	 * Returns:
	 *
	 * Raw vblank counter value.
	 */
	u32 (*get_vblank_counter) (struct drm_device *dev, unsigned int pipe);

	/**
	 * @enable_vblank:
	 *
	 * Enable vblank interrupts for the CRTC specified with the pipe
	 * argument.
	 *
	 * This is deprecated and should not be used by new drivers.
	 * Use &drm_crtc_funcs.enable_vblank instead.
	 *
	 * Returns:
	 *
	 * Zero on success, appropriate errno if the given @crtc's vblank
	 * interrupt cannot be enabled.
	 */
	int (*enable_vblank) (struct drm_device *dev, unsigned int pipe);

	/**
	 * @disable_vblank:
	 *
	 * Disable vblank interrupts for the CRTC specified with the pipe
	 * argument.
	 *
	 * This is deprecated and should not be used by new drivers.
	 * Use &drm_crtc_funcs.disable_vblank instead.
	 */
	void (*disable_vblank) (struct drm_device *dev, unsigned int pipe);

	/**
	 * @get_scanout_position:
	 *
	 * Called by vblank timestamping code.
	 *
	 * Returns the current display scanout position from a crtc, and an
	 * optional accurate ktime_get() timestamp of when position was
	 * measured. Note that this is a helper callback which is only used if a
	 * driver uses drm_calc_vbltimestamp_from_scanoutpos() for the
	 * @get_vblank_timestamp callback.
	 *
	 * Parameters:
	 *
	 * dev:
	 *     DRM device.
	 * pipe:
	 *     Id of the crtc to query.
	 * in_vblank_irq:
	 *     True when called from drm_crtc_handle_vblank().  Some drivers
	 *     need to apply some workarounds for gpu-specific vblank irq quirks
	 *     if flag is set.
	 * vpos:
	 *     Target location for current vertical scanout position.
	 * hpos:
	 *     Target location for current horizontal scanout position.
	 * stime:
	 *     Target location for timestamp taken immediately before
	 *     scanout position query. Can be NULL to skip timestamp.
	 * etime:
	 *     Target location for timestamp taken immediately after
	 *     scanout position query. Can be NULL to skip timestamp.
	 * mode:
	 *     Current display timings.
	 *
	 * Returns vpos as a positive number while in active scanout area.
	 * Returns vpos as a negative number inside vblank, counting the number
	 * of scanlines to go until end of vblank, e.g., -1 means "one scanline
	 * until start of active scanout / end of vblank."
	 *
	 * Returns:
	 *
	 * True on success, false if a reliable scanout position counter could
	 * not be read out.
	 *
	 * FIXME:
	 *
	 * Since this is a helper to implement @get_vblank_timestamp, we should
	 * move it to &struct drm_crtc_helper_funcs, like all the other
	 * helper-internal hooks.
	 */
	bool (*get_scanout_position) (struct drm_device *dev, unsigned int pipe,
				      bool in_vblank_irq, int *vpos, int *hpos,
				      ktime_t *stime, ktime_t *etime,
				      const struct drm_display_mode *mode);

	/**
	 * @get_vblank_timestamp:
	 *
	 * Called by drm_get_last_vbltimestamp(). Should return a precise
	 * timestamp when the most recent VBLANK interval ended or will end.
	 *
	 * Specifically, the timestamp in @vblank_time should correspond as
	 * closely as possible to the time when the first video scanline of
	 * the video frame after the end of VBLANK will start scanning out,
	 * the time immediately after end of the VBLANK interval. If the
	 * @crtc is currently inside VBLANK, this will be a time in the future.
	 * If the @crtc is currently scanning out a frame, this will be the
	 * past start time of the current scanout. This is meant to adhere
	 * to the OpenML OML_sync_control extension specification.
	 *
	 * Paramters:
	 *
	 * dev:
	 *     dev DRM device handle.
	 * pipe:
	 *     crtc for which timestamp should be returned.
	 * max_error:
	 *     Maximum allowable timestamp error in nanoseconds.
	 *     Implementation should strive to provide timestamp
	 *     with an error of at most max_error nanoseconds.
	 *     Returns true upper bound on error for timestamp.
	 * vblank_time:
	 *     Target location for returned vblank timestamp.
	 * in_vblank_irq:
	 *     True when called from drm_crtc_handle_vblank().  Some drivers
	 *     need to apply some workarounds for gpu-specific vblank irq quirks
	 *     if flag is set.
	 *
	 * Returns:
	 *
	 * True on success, false on failure, which means the core should
	 * fallback to a simple timestamp taken in drm_crtc_handle_vblank().
	 *
	 * FIXME:
	 *
	 * We should move this hook to &struct drm_crtc_funcs like all the other
	 * vblank hooks.
	 */
	bool (*get_vblank_timestamp) (struct drm_device *dev, unsigned int pipe,
				     int *max_error,
				     ktime_t *vblank_time,
				     bool in_vblank_irq);

	/**
	 * @irq_handler:
	 *
	 * Interrupt handler called when using drm_irq_install(). Not used by
	 * drivers which implement their own interrupt handling.
	 */
	irqreturn_t(*irq_handler) (int irq, void *arg);

	/**
	 * @irq_preinstall:
	 *
	 * Optional callback used by drm_irq_install() which is called before
	 * the interrupt handler is registered. This should be used to clear out
	 * any pending interrupts (from e.g. firmware based drives) and reset
	 * the interrupt handling registers.
	 */
	void (*irq_preinstall) (struct drm_device *dev);

	/**
	 * @irq_postinstall:
	 *
	 * Optional callback used by drm_irq_install() which is called after
	 * the interrupt handler is registered. This should be used to enable
	 * interrupt generation in the hardware.
	 */
	int (*irq_postinstall) (struct drm_device *dev);

	/**
	 * @irq_uninstall:
	 *
	 * Optional callback used by drm_irq_uninstall() which is called before
	 * the interrupt handler is unregistered. This should be used to disable
	 * interrupt generation in the hardware.
	 */
	void (*irq_uninstall) (struct drm_device *dev);

	/**
	 * @master_create:
	 *
	 * Called whenever a new master is created. Only used by vmwgfx.
	 */
	int (*master_create)(struct drm_device *dev, struct drm_master *master);

	/**
	 * @master_destroy:
	 *
	 * Called whenever a master is destroyed. Only used by vmwgfx.
	 */
	void (*master_destroy)(struct drm_device *dev, struct drm_master *master);

	/**
	 * @master_set:
	 *
	 * Called whenever the minor master is set. Only used by vmwgfx.
	 */
	int (*master_set)(struct drm_device *dev, struct drm_file *file_priv,
			  bool from_open);
	/**
	 * @master_drop:
	 *
	 * Called whenever the minor master is dropped. Only used by vmwgfx.
	 */
	void (*master_drop)(struct drm_device *dev, struct drm_file *file_priv);

	/**
	 * @debugfs_init:
	 *
	 * Allows drivers to create driver-specific debugfs files.
	 */
	int (*debugfs_init)(struct drm_minor *minor);

	/**
	 * @gem_free_object: deconstructor for drm_gem_objects
	 *
	 * This is deprecated and should not be used by new drivers. Use
	 * @gem_free_object_unlocked instead.
	 */
	void (*gem_free_object) (struct drm_gem_object *obj);

	/**
	 * @gem_free_object_unlocked: deconstructor for drm_gem_objects
	 *
	 * This is for drivers which are not encumbered with &drm_device.struct_mutex
	 * legacy locking schemes. Use this hook instead of @gem_free_object.
	 */
	void (*gem_free_object_unlocked) (struct drm_gem_object *obj);

	/**
	 * @gem_open_object:
	 *
	 * Driver hook called upon gem handle creation
	 */
	int (*gem_open_object) (struct drm_gem_object *, struct drm_file *);

	/**
	 * @gem_close_object:
	 *
	 * Driver hook called upon gem handle release
	 */
	void (*gem_close_object) (struct drm_gem_object *, struct drm_file *);

	/**
	 * @gem_print_info:
	 *
	 * If driver subclasses struct &drm_gem_object, it can implement this
	 * optional hook for printing additional driver specific info.
	 *
	 * drm_printf_indent() should be used in the callback passing it the
	 * indent argument.
	 *
	 * This callback is called from drm_gem_print_info().
	 */
	void (*gem_print_info)(struct drm_printer *p, unsigned int indent,
			       const struct drm_gem_object *obj);

	/**
	 * @gem_create_object: constructor for gem objects
	 *
	 * Hook for allocating the GEM object struct, for use by core
	 * helpers.
	 */
	struct drm_gem_object *(*gem_create_object)(struct drm_device *dev,
						    size_t size);

	/* prime: */
	/**
	 * @prime_handle_to_fd:
	 *
	 * export handle -> fd (see drm_gem_prime_handle_to_fd() helper)
	 */
	int (*prime_handle_to_fd)(struct drm_device *dev, struct drm_file *file_priv,
				uint32_t handle, uint32_t flags, int *prime_fd);
	/**
	 * @prime_fd_to_handle:
	 *
	 * import fd -> handle (see drm_gem_prime_fd_to_handle() helper)
	 */
	int (*prime_fd_to_handle)(struct drm_device *dev, struct drm_file *file_priv,
				int prime_fd, uint32_t *handle);
	/**
	 * @gem_prime_export:
	 *
	 * export GEM -> dmabuf
	 */
	struct dma_buf * (*gem_prime_export)(struct drm_device *dev,
				struct drm_gem_object *obj, int flags);
	/**
	 * @gem_prime_import:
	 *
	 * import dmabuf -> GEM
	 */
	struct drm_gem_object * (*gem_prime_import)(struct drm_device *dev,
				struct dma_buf *dma_buf);
	int (*gem_prime_pin)(struct drm_gem_object *obj);
	void (*gem_prime_unpin)(struct drm_gem_object *obj);
	struct reservation_object * (*gem_prime_res_obj)(
				struct drm_gem_object *obj);
	struct sg_table *(*gem_prime_get_sg_table)(struct drm_gem_object *obj);
	struct drm_gem_object *(*gem_prime_import_sg_table)(
				struct drm_device *dev,
				struct dma_buf_attachment *attach,
				struct sg_table *sgt);
	void *(*gem_prime_vmap)(struct drm_gem_object *obj);
	void (*gem_prime_vunmap)(struct drm_gem_object *obj, void *vaddr);
	int (*gem_prime_mmap)(struct drm_gem_object *obj,
				struct vm_area_struct *vma);

	/**
	 * @dumb_create:
	 *
	 * This creates a new dumb buffer in the driver's backing storage manager (GEM,
	 * TTM or something else entirely) and returns the resulting buffer handle. This
	 * handle can then be wrapped up into a framebuffer modeset object.
	 *
	 * Note that userspace is not allowed to use such objects for render
	 * acceleration - drivers must create their own private ioctls for such a use
	 * case.
	 *
	 * Width, height and depth are specified in the &drm_mode_create_dumb
	 * argument. The callback needs to fill the handle, pitch and size for
	 * the created buffer.
	 *
	 * Called by the user via ioctl.
	 *
	 * Returns:
	 *
	 * Zero on success, negative errno on failure.
	 */
	int (*dumb_create)(struct drm_file *file_priv,
			   struct drm_device *dev,
			   struct drm_mode_create_dumb *args);
	/**
	 * @dumb_map_offset:
	 *
	 * Allocate an offset in the drm device node's address space to be able to
	 * memory map a dumb buffer. GEM-based drivers must use
	 * drm_gem_create_mmap_offset() to implement this.
	 *
	 * Called by the user via ioctl.
	 *
	 * Returns:
	 *
	 * Zero on success, negative errno on failure.
	 */
	int (*dumb_map_offset)(struct drm_file *file_priv,
			       struct drm_device *dev, uint32_t handle,
			       uint64_t *offset);
	/**
	 * @dumb_destroy:
	 *
	 * This destroys the userspace handle for the given dumb backing storage buffer.
	 * Since buffer objects must be reference counted in the kernel a buffer object
	 * won't be immediately freed if a framebuffer modeset object still uses it.
	 *
	 * Called by the user via ioctl.
	 *
	 * Returns:
	 *
	 * Zero on success, negative errno on failure.
	 */
	int (*dumb_destroy)(struct drm_file *file_priv,
			    struct drm_device *dev,
			    uint32_t handle);

	/**
	 * @gem_vm_ops: Driver private ops for this object
	 */
	const struct vm_operations_struct *gem_vm_ops;

	/** @major: driver major number */
	int major;
	/** @minor: driver minor number */
	int minor;
	/** @patchlevel: driver patch level */
	int patchlevel;
	/** @name: driver name */
	char *name;
	/** @desc: driver description */
	char *desc;
	/** @date: driver date */
	char *date;

	/** @driver_features: driver features */
	u32 driver_features;

	/**
	 * @ioctls:
	 *
	 * Array of driver-private IOCTL description entries. See the chapter on
	 * :ref:`IOCTL support in the userland interfaces
	 * chapter<drm_driver_ioctl>` for the full details.
	 */

	const struct drm_ioctl_desc *ioctls;
	/** @num_ioctls: Number of entries in @ioctls. */
	int num_ioctls;

	/**
	 * @fops:
	 *
	 * File operations for the DRM device node. See the discussion in
	 * :ref:`file operations<drm_driver_fops>` for in-depth coverage and
	 * some examples.
	 */
	const struct file_operations *fops;

	/* Everything below here is for legacy driver, never use! */
	/* private: */

	/* List of devices hanging off this driver with stealth attach. */
	struct list_head legacy_dev_list;
	int (*firstopen) (struct drm_device *);
	void (*preclose) (struct drm_device *, struct drm_file *file_priv);
	int (*dma_ioctl) (struct drm_device *dev, void *data, struct drm_file *file_priv);
	int (*dma_quiescent) (struct drm_device *);
	int (*context_dtor) (struct drm_device *dev, int context);
	int dev_priv_size;
};

extern unsigned int drm_debug;

int drm_dev_init(struct drm_device *dev,
		 struct drm_driver *driver,
		 struct device *parent);
void drm_dev_fini(struct drm_device *dev);

struct drm_device *drm_dev_alloc(struct drm_driver *driver,
				 struct device *parent);
int drm_dev_register(struct drm_device *dev, unsigned long flags);
void drm_dev_unregister(struct drm_device *dev);

void drm_dev_get(struct drm_device *dev);
void drm_dev_put(struct drm_device *dev);
void drm_dev_unref(struct drm_device *dev);
void drm_put_dev(struct drm_device *dev);
bool drm_dev_enter(struct drm_device *dev, int *idx);
void drm_dev_exit(int idx);
void drm_dev_unplug(struct drm_device *dev);

/**
 * drm_dev_is_unplugged - is a DRM device unplugged
 * @dev: DRM device
 *
 * This function can be called to check whether a hotpluggable is unplugged.
 * Unplugging itself is singalled through drm_dev_unplug(). If a device is
 * unplugged, these two functions guarantee that any store before calling
 * drm_dev_unplug() is visible to callers of this function after it completes
 */
static inline bool drm_dev_is_unplugged(struct drm_device *dev)
{
	int idx;

	if (drm_dev_enter(dev, &idx)) {
		drm_dev_exit(idx);
		return false;
	}

	return true;
}

/**
 * drm_core_check_feature - check driver feature flags
 * @dev: DRM device to check
 * @feature: feature flag
 *
 * This checks @dev for driver features, see &drm_driver.driver_features,
 * &drm_device.driver_features, and the various DRIVER_\* flags.
 *
 * Returns true if the @feature is supported, false otherwise.
 */
static inline bool drm_core_check_feature(struct drm_device *dev, u32 feature)
{
	return dev->driver->driver_features & dev->driver_features & feature;
}

/**
 * drm_drv_uses_atomic_modeset - check if the driver implements
 * atomic_commit()
 * @dev: DRM device
 *
 * This check is useful if drivers do not have DRIVER_ATOMIC set but
 * have atomic modesetting internally implemented.
 */
static inline bool drm_drv_uses_atomic_modeset(struct drm_device *dev)
{
	return drm_core_check_feature(dev, DRIVER_ATOMIC) ||
		(dev->mode_config.funcs && dev->mode_config.funcs->atomic_commit != NULL);
}


int drm_dev_set_unique(struct drm_device *dev, const char *name);


#endif
