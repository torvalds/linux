/* SPDX-License-Identifier: GPL-2.0 or MIT */
#ifndef __DRM_PANIC_H__
#define __DRM_PANIC_H__

#include <linux/module.h>
#include <linux/types.h>
#include <linux/iosys-map.h>

#include <drm/drm_device.h>
#include <drm/drm_fourcc.h>
/*
 * Copyright (c) 2024 Intel
 */

/**
 * struct drm_scanout_buffer - DRM scanout buffer
 *
 * This structure holds the information necessary for drm_panic to draw the
 * panic screen, and display it.
 */
struct drm_scanout_buffer {
	/**
	 * @format:
	 *
	 * drm format of the scanout buffer.
	 */
	const struct drm_format_info *format;

	/**
	 * @map:
	 *
	 * Virtual address of the scanout buffer, either in memory or iomem.
	 * The scanout buffer should be in linear format, and can be directly
	 * sent to the display hardware. Tearing is not an issue for the panic
	 * screen.
	 */
	struct iosys_map map[DRM_FORMAT_MAX_PLANES];

	/**
	 * @width: Width of the scanout buffer, in pixels.
	 */
	unsigned int width;

	/**
	 * @height: Height of the scanout buffer, in pixels.
	 */
	unsigned int height;

	/**
	 * @pitch: Length in bytes between the start of two consecutive lines.
	 */
	unsigned int pitch[DRM_FORMAT_MAX_PLANES];

	/**
	 * @set_pixel: Optional function, to set a pixel color on the
	 * framebuffer. It allows to handle special tiling format inside the
	 * driver.
	 */
	void (*set_pixel)(struct drm_scanout_buffer *sb, unsigned int x,
			  unsigned int y, u32 color);

};

/**
 * drm_panic_trylock - try to enter the panic printing critical section
 * @dev: struct drm_device
 * @flags: unsigned long irq flags you need to pass to the unlock() counterpart
 *
 * This function must be called by any panic printing code. The panic printing
 * attempt must be aborted if the trylock fails.
 *
 * Panic printing code can make the following assumptions while holding the
 * panic lock:
 *
 * - Anything protected by drm_panic_lock() and drm_panic_unlock() pairs is safe
 *   to access.
 *
 * - Furthermore the panic printing code only registers in drm_dev_unregister()
 *   and gets removed in drm_dev_unregister(). This allows the panic code to
 *   safely access any state which is invariant in between these two function
 *   calls, like the list of planes &drm_mode_config.plane_list or most of the
 *   struct drm_plane structure.
 *
 * Specifically thanks to the protection around plane updates in
 * drm_atomic_helper_swap_state() the following additional guarantees hold:
 *
 * - It is safe to deference the drm_plane.state pointer.
 *
 * - Anything in struct drm_plane_state or the driver's subclass thereof which
 *   stays invariant after the atomic check code has finished is safe to access.
 *   Specifically this includes the reference counted pointers to framebuffer
 *   and buffer objects.
 *
 * - Anything set up by &drm_plane_helper_funcs.fb_prepare and cleaned up
 *   &drm_plane_helper_funcs.fb_cleanup is safe to access, as long as it stays
 *   invariant between these two calls. This also means that for drivers using
 *   dynamic buffer management the framebuffer is pinned, and therefer all
 *   relevant datastructures can be accessed without taking any further locks
 *   (which would be impossible in panic context anyway).
 *
 * - Importantly, software and hardware state set up by
 *   &drm_plane_helper_funcs.begin_fb_access and
 *   &drm_plane_helper_funcs.end_fb_access is not safe to access.
 *
 * Drivers must not make any assumptions about the actual state of the hardware,
 * unless they explicitly protected these hardware access with drm_panic_lock()
 * and drm_panic_unlock().
 *
 * Return:
 * %0 when failing to acquire the raw spinlock, nonzero on success.
 */
#define drm_panic_trylock(dev, flags) \
	raw_spin_trylock_irqsave(&(dev)->mode_config.panic_lock, flags)

/**
 * drm_panic_lock - protect panic printing relevant state
 * @dev: struct drm_device
 * @flags: unsigned long irq flags you need to pass to the unlock() counterpart
 *
 * This function must be called to protect software and hardware state that the
 * panic printing code must be able to rely on. The protected sections must be
 * as small as possible. It uses the irqsave/irqrestore variant, and can be
 * called from irq handler. Examples include:
 *
 * - Access to peek/poke or other similar registers, if that is the way the
 *   driver prints the pixels into the scanout buffer at panic time.
 *
 * - Updates to pointers like &drm_plane.state, allowing the panic handler to
 *   safely deference these. This is done in drm_atomic_helper_swap_state().
 *
 * - An state that isn't invariant and that the driver must be able to access
 *   during panic printing.
 */

#define drm_panic_lock(dev, flags) \
	raw_spin_lock_irqsave(&(dev)->mode_config.panic_lock, flags)

/**
 * drm_panic_unlock - end of the panic printing critical section
 * @dev: struct drm_device
 * @flags: irq flags that were returned when acquiring the lock
 *
 * Unlocks the raw spinlock acquired by either drm_panic_lock() or
 * drm_panic_trylock().
 */
#define drm_panic_unlock(dev, flags) \
	raw_spin_unlock_irqrestore(&(dev)->mode_config.panic_lock, flags)

#ifdef CONFIG_DRM_PANIC

void drm_panic_register(struct drm_device *dev);
void drm_panic_unregister(struct drm_device *dev);

#else

static inline void drm_panic_register(struct drm_device *dev) {}
static inline void drm_panic_unregister(struct drm_device *dev) {}

#endif

#endif /* __DRM_PANIC_H__ */
