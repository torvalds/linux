/*
 * Internal Header for the Direct Rendering Manager
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright (c) 2009-2010, Code Aurora Forum.
 * All rights reserved.
 *
 * Author: Rickard E. (Rik) Faith <faith@valinux.com>
 * Author: Gareth Hughes <gareth@valinux.com>
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

#ifndef _DRM_IOCTL_H_
#define _DRM_IOCTL_H_

#include <linux/types.h>
#include <linux/bitops.h>

#include <asm/ioctl.h>

struct drm_device;
struct drm_file;
struct file;

/**
 * drm_ioctl_t - DRM ioctl function type.
 * @dev: DRM device inode
 * @data: private pointer of the ioctl call
 * @file_priv: DRM file this ioctl was made on
 *
 * This is the DRM ioctl typedef. Note that drm_ioctl() has alrady copied @data
 * into kernel-space, and will also copy it back, depending upon the read/write
 * settings in the ioctl command code.
 */
typedef int drm_ioctl_t(struct drm_device *dev, void *data,
			struct drm_file *file_priv);

/**
 * drm_ioctl_compat_t - compatibility DRM ioctl function type.
 * @filp: file pointer
 * @cmd: ioctl command code
 * @arg: DRM file this ioctl was made on
 *
 * Just a typedef to make declaring an array of compatibility handlers easier.
 * New drivers shouldn't screw up the structure layout for their ioctl
 * structures and hence never need this.
 */
typedef int drm_ioctl_compat_t(struct file *filp, unsigned int cmd,
			       unsigned long arg);

#define DRM_IOCTL_NR(n)                _IOC_NR(n)
#define DRM_IOCTL_TYPE(n)              _IOC_TYPE(n)
#define DRM_MAJOR       226

/**
 * enum drm_ioctl_flags - DRM ioctl flags
 *
 * Various flags that can be set in &drm_ioctl_desc.flags to control how
 * userspace can use a given ioctl.
 */
enum drm_ioctl_flags {
	/**
	 * @DRM_AUTH:
	 *
	 * This is for ioctl which are used for rendering, and require that the
	 * file descriptor is either for a render node, or if it's a
	 * legacy/primary node, then it must be authenticated.
	 */
	DRM_AUTH		= BIT(0),
	/**
	 * @DRM_MASTER:
	 *
	 * This must be set for any ioctl which can change the modeset or
	 * display state. Userspace must call the ioctl through a primary node,
	 * while it is the active master.
	 *
	 * Note that read-only modeset ioctl can also be called by
	 * unauthenticated clients, or when a master is not the currently active
	 * one.
	 */
	DRM_MASTER		= BIT(1),
	/**
	 * @DRM_ROOT_ONLY:
	 *
	 * Anything that could potentially wreak a master file descriptor needs
	 * to have this flag set. Current that's only for the SETMASTER and
	 * DROPMASTER ioctl, which e.g. logind can call to force a non-behaving
	 * master (display compositor) into compliance.
	 *
	 * This is equivalent to callers with the SYSADMIN capability.
	 */
	DRM_ROOT_ONLY		= BIT(2),
	/**
	 * @DRM_UNLOCKED:
	 *
	 * Whether &drm_ioctl_desc.func should be called with the DRM BKL held
	 * or not. Enforced as the default for all modern drivers, hence there
	 * should never be a need to set this flag.
	 *
	 * Do not use anywhere else than for the VBLANK_WAIT IOCTL, which is the
	 * only legacy IOCTL which needs this.
	 */
	DRM_UNLOCKED		= BIT(4),
	/**
	 * @DRM_RENDER_ALLOW:
	 *
	 * This is used for all ioctl needed for rendering only, for drivers
	 * which support render nodes. This should be all new render drivers,
	 * and hence it should be always set for any ioctl with DRM_AUTH set.
	 * Note though that read-only query ioctl might have this set, but have
	 * not set DRM_AUTH because they do not require authentication.
	 */
	DRM_RENDER_ALLOW	= BIT(5),
};

/**
 * struct drm_ioctl_desc - DRM driver ioctl entry
 * @cmd: ioctl command number, without flags
 * @flags: a bitmask of &enum drm_ioctl_flags
 * @func: handler for this ioctl
 * @name: user-readable name for debug output
 *
 * For convenience it's easier to create these using the DRM_IOCTL_DEF_DRV()
 * macro.
 */
struct drm_ioctl_desc {
	unsigned int cmd;
	enum drm_ioctl_flags flags;
	drm_ioctl_t *func;
	const char *name;
};

/**
 * DRM_IOCTL_DEF_DRV() - helper macro to fill out a &struct drm_ioctl_desc
 * @ioctl: ioctl command suffix
 * @_func: handler for the ioctl
 * @_flags: a bitmask of &enum drm_ioctl_flags
 *
 * Small helper macro to create a &struct drm_ioctl_desc entry. The ioctl
 * command number is constructed by prepending ``DRM_IOCTL\_`` and passing that
 * to DRM_IOCTL_NR().
 */
#define DRM_IOCTL_DEF_DRV(ioctl, _func, _flags)				\
	[DRM_IOCTL_NR(DRM_IOCTL_##ioctl) - DRM_COMMAND_BASE] = {	\
		.cmd = DRM_IOCTL_##ioctl,				\
		.func = _func,						\
		.flags = _flags,					\
		.name = #ioctl						\
	}

int drm_ioctl_permit(u32 flags, struct drm_file *file_priv);
long drm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
long drm_ioctl_kernel(struct file *, drm_ioctl_t, void *, u32);
#ifdef CONFIG_COMPAT
long drm_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
#else
/* Let drm_compat_ioctl be assigned to .compat_ioctl unconditionally */
#define drm_compat_ioctl NULL
#endif
bool drm_ioctl_flags(unsigned int nr, unsigned int *flags);

int drm_noop(struct drm_device *dev, void *data,
	     struct drm_file *file_priv);
int drm_invalid_op(struct drm_device *dev, void *data,
		   struct drm_file *file_priv);

#endif /* _DRM_IOCTL_H_ */
