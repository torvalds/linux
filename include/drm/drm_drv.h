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

struct drm_device;
struct drm_file;
struct drm_gem_object;
struct drm_master;
struct drm_minor;
struct dma_buf_attachment;
struct drm_display_mode;
struct drm_mode_create_dumb;

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
	 * @preclose:
	 *
	 * One of the driver callbacks when a new &struct drm_file is closed.
	 * Useful for tearing down driver-private data structures allocated in
	 * @open like buffer allocators, execution contexts or similar things.
	 *
	 * Since the display/modeset side of DRM can only be owned by exactly
	 * one &struct drm_file (see &drm_file.is_master and &drm_device.master)
	 * there should never be a need to tear down any modeset related
	 * resources in this callback. Doing so would be a driver design bug.
	 *
	 * FIXME: It is not really clear why there's both @preclose and
	 * @postclose. Without a really good reason, use @postclose only.
	 */
	void (*preclose) (struct drm_device *, struct drm_file *file_priv);

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
	 *
	 * FIXME: It is not really clear why there's both @preclose and
	 * @postclose. Without a really good reason, use @postclose only.
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
	 * This is called after @preclose and @postclose have been called.
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
	 * driver layer.  See drm_dev_unregister() and drm_dev_unref()
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

	int (*set_busid)(struct drm_device *dev, struct drm_master *master);

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
	 * flags:
	 *     Flags from the caller (DRM_CALLED_FROM_VBLIRQ or 0).
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
	 * Flags, or'ed together as follows:
	 *
	 * DRM_SCANOUTPOS_VALID:
	 *     Query successful.
	 * DRM_SCANOUTPOS_INVBL:
	 *     Inside vblank.
	 * DRM_SCANOUTPOS_ACCURATE: Returned position is accurate. A lack of
	 *     this flag means that returned position may be offset by a
	 *     constant but unknown small number of scanlines wrt. real scanout
	 *     position.
	 *
	 */
	int (*get_scanout_position) (struct drm_device *dev, unsigned int pipe,
				     unsigned int flags, int *vpos, int *hpos,
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
	 * flags:
	 *     0 = Defaults, no special treatment needed.
	 *     DRM_CALLED_FROM_VBLIRQ = Function is called from vblank
	 *     irq handler. Some drivers need to apply some workarounds
	 *     for gpu-specific vblank irq quirks if flag is set.
	 *
	 * Returns:
	 *
	 * Zero if timestamping isn't supported in current display mode or a
	 * negative number on failure. A positive status code on success,
	 * which describes how the vblank_time timestamp was computed.
	 */
	int (*get_vblank_timestamp) (struct drm_device *dev, unsigned int pipe,
				     int *max_error,
				     struct timeval *vblank_time,
				     unsigned flags);

	/* these have to be filled in */

	irqreturn_t(*irq_handler) (int irq, void *arg);
	void (*irq_preinstall) (struct drm_device *dev);
	int (*irq_postinstall) (struct drm_device *dev);
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

	int (*gem_open_object) (struct drm_gem_object *, struct drm_file *);
	void (*gem_close_object) (struct drm_gem_object *, struct drm_file *);

	/**
	 * @gem_create_object: constructor for gem objects
	 *
	 * Hook for allocating the GEM object struct, for use by core
	 * helpers.
	 */
	struct drm_gem_object *(*gem_create_object)(struct drm_device *dev,
						    size_t size);

	/* prime: */
	/* export handle -> fd (see drm_gem_prime_handle_to_fd() helper) */
	int (*prime_handle_to_fd)(struct drm_device *dev, struct drm_file *file_priv,
				uint32_t handle, uint32_t flags, int *prime_fd);
	/* import fd -> handle (see drm_gem_prime_fd_to_handle() helper) */
	int (*prime_fd_to_handle)(struct drm_device *dev, struct drm_file *file_priv,
				int prime_fd, uint32_t *handle);
	/* export GEM -> dmabuf */
	struct dma_buf * (*gem_prime_export)(struct drm_device *dev,
				struct drm_gem_object *obj, int flags);
	/* import dmabuf -> GEM */
	struct drm_gem_object * (*gem_prime_import)(struct drm_device *dev,
				struct dma_buf *dma_buf);
	/* low-level interface used by drm_gem_prime_{import,export} */
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

	/* Driver private ops for this object */
	const struct vm_operations_struct *gem_vm_ops;

	int major;
	int minor;
	int patchlevel;
	char *name;
	char *desc;
	char *date;

	u32 driver_features;
	const struct drm_ioctl_desc *ioctls;
	int num_ioctls;
	const struct file_operations *fops;

	/* Everything below here is for legacy driver, never use! */
	/* private: */

	/* List of devices hanging off this driver with stealth attach. */
	struct list_head legacy_dev_list;
	int (*firstopen) (struct drm_device *);
	int (*dma_ioctl) (struct drm_device *dev, void *data, struct drm_file *file_priv);
	int (*dma_quiescent) (struct drm_device *);
	int (*context_dtor) (struct drm_device *dev, int context);
	int dev_priv_size;
};

__printf(6, 7)
void drm_dev_printk(const struct device *dev, const char *level,
		    unsigned int category, const char *function_name,
		    const char *prefix, const char *format, ...);
__printf(3, 4)
void drm_printk(const char *level, unsigned int category,
		const char *format, ...);
extern unsigned int drm_debug;

int drm_dev_init(struct drm_device *dev,
		 struct drm_driver *driver,
		 struct device *parent);
void drm_dev_fini(struct drm_device *dev);

struct drm_device *drm_dev_alloc(struct drm_driver *driver,
				 struct device *parent);
int drm_dev_register(struct drm_device *dev, unsigned long flags);
void drm_dev_unregister(struct drm_device *dev);

void drm_dev_ref(struct drm_device *dev);
void drm_dev_unref(struct drm_device *dev);
void drm_put_dev(struct drm_device *dev);
void drm_unplug_dev(struct drm_device *dev);

int drm_dev_set_unique(struct drm_device *dev, const char *name);


#endif
