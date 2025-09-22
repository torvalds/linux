/*
 * Created: Fri Jan  8 09:01:26 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Author Rickard E. (Rik) Faith <faith@valinux.com>
 * Author Gareth Hughes <gareth@valinux.com>
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

#include <sys/filio.h>

#include <linux/export.h>
#include <linux/nospec.h>
#include <linux/pci.h>
#include <linux/uaccess.h>

#include <drm/drm_auth.h>
#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_print.h>

#include "drm_crtc_internal.h"
#include "drm_internal.h"

/**
 * DOC: getunique and setversion story
 *
 * BEWARE THE DRAGONS! MIND THE TRAPDOORS!
 *
 * In an attempt to warn anyone else who's trying to figure out what's going
 * on here, I'll try to summarize the story. First things first, let's clear up
 * the names, because the kernel internals, libdrm and the ioctls are all named
 * differently:
 *
 *  - GET_UNIQUE ioctl, implemented by drm_getunique is wrapped up in libdrm
 *    through the drmGetBusid function.
 *  - The libdrm drmSetBusid function is backed by the SET_UNIQUE ioctl. All
 *    that code is nerved in the kernel with drm_invalid_op().
 *  - The internal set_busid kernel functions and driver callbacks are
 *    exclusively use by the SET_VERSION ioctl, because only drm 1.0 (which is
 *    nerved) allowed userspace to set the busid through the above ioctl.
 *  - Other ioctls and functions involved are named consistently.
 *
 * For anyone wondering what's the difference between drm 1.1 and 1.4: Correctly
 * handling pci domains in the busid on ppc. Doing this correctly was only
 * implemented in libdrm in 2010, hence can't be nerved yet. No one knows what's
 * special with drm 1.2 and 1.3.
 *
 * Now the actual horror story of how device lookup in drm works. At large,
 * there's 2 different ways, either by busid, or by device driver name.
 *
 * Opening by busid is fairly simple:
 *
 * 1. First call SET_VERSION to make sure pci domains are handled properly. As a
 *    side-effect this fills out the unique name in the master structure.
 * 2. Call GET_UNIQUE to read out the unique name from the master structure,
 *    which matches the busid thanks to step 1. If it doesn't, proceed to try
 *    the next device node.
 *
 * Opening by name is slightly different:
 *
 * 1. Directly call VERSION to get the version and to match against the driver
 *    name returned by that ioctl. Note that SET_VERSION is not called, which
 *    means the unique name for the master node just opening is _not_ filled
 *    out. This despite that with current drm device nodes are always bound to
 *    one device, and can't be runtime assigned like with drm 1.0.
 * 2. Match driver name. If it mismatches, proceed to the next device node.
 * 3. Call GET_UNIQUE, and check whether the unique name has length zero (by
 *    checking that the first byte in the string is 0). If that's not the case
 *    libdrm skips and proceeds to the next device node. Probably this is just
 *    copypasta from drm 1.0 times where a set unique name meant that the driver
 *    was in use already, but that's just conjecture.
 *
 * Long story short: To keep the open by name logic working, GET_UNIQUE must
 * _not_ return a unique string when SET_VERSION hasn't been called yet,
 * otherwise libdrm breaks. Even when that unique string can't ever change, and
 * is totally irrelevant for actually opening the device because runtime
 * assignable device instances were only support in drm 1.0, which is long dead.
 * But the libdrm code in drmOpenByName somehow survived, hence this can't be
 * broken.
 */

/*
 * Get the bus id.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_unique structure.
 * \return zero on success or a negative number on failure.
 *
 * Copies the bus id from drm_device::unique into user space.
 */
int drm_getunique(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct drm_unique *u = data;
	struct drm_master *master;

	mutex_lock(&dev->master_mutex);
	master = file_priv->master;
	if (u->unique_len >= master->unique_len) {
		if (copy_to_user(u->unique, master->unique, master->unique_len)) {
			mutex_unlock(&dev->master_mutex);
			return -EFAULT;
		}
	}
	u->unique_len = master->unique_len;
	mutex_unlock(&dev->master_mutex);

	return 0;
}

static void
drm_unset_busid(struct drm_device *dev,
		struct drm_master *master)
{
	kfree(master->unique);
	master->unique = NULL;
	master->unique_len = 0;
}

static int drm_set_busid(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_master *master = file_priv->master;
	int ret;

	if (master->unique != NULL)
		drm_unset_busid(dev, master);

#ifdef __linux__
	if (dev->dev && dev_is_pci(dev->dev)) {
#else
	if (1) {
#endif
		ret = drm_pci_set_busid(dev, master);
		if (ret) {
			drm_unset_busid(dev, master);
			return ret;
		}
	} else {
		WARN_ON(!dev->unique);
		master->unique = kstrdup(dev->unique, GFP_KERNEL);
		if (master->unique)
			master->unique_len = strlen(dev->unique);
	}

	return 0;
}

/*
 * Get client information.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_client structure.
 *
 * \return zero on success or a negative number on failure.
 *
 * Searches for the client with the specified index and copies its information
 * into userspace
 */
int drm_getclient(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct drm_client *client = data;

	/*
	 * Hollowed-out getclient ioctl to keep some dead old drm tests/tools
	 * not breaking completely. Userspace tools stop enumerating one they
	 * get -EINVAL, hence this is the return value we need to hand back for
	 * no clients tracked.
	 *
	 * Unfortunately some clients (*cough* libva *cough*) use this in a fun
	 * attempt to figure out whether they're authenticated or not. Since
	 * that's the only thing they care about, give it to the directly
	 * instead of walking one giant list.
	 */
	if (client->idx == 0) {
		client->auth = file_priv->authenticated;
#ifdef __linux__
		client->pid = task_pid_vnr(current);
		client->uid = overflowuid;
#else
		client->pid = curproc->p_p->ps_pid;
		client->uid = 0xfffe;
#endif
		client->magic = 0;
		client->iocs = 0;

		return 0;
	} else {
		return -EINVAL;
	}
}

/*
 * Get statistics information.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_stats structure.
 *
 * \return zero on success or a negative number on failure.
 */
static int drm_getstats(struct drm_device *dev, void *data,
		 struct drm_file *file_priv)
{
	struct drm_stats *stats = data;

	/* Clear stats to prevent userspace from eating its stack garbage. */
	memset(stats, 0, sizeof(*stats));

	return 0;
}

/*
 * Get device/driver capabilities
 */
static int drm_getcap(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_get_cap *req = data;
	struct drm_crtc *crtc;

	req->value = 0;

	/* Only some caps make sense with UMS/render-only drivers. */
	switch (req->capability) {
	case DRM_CAP_TIMESTAMP_MONOTONIC:
		req->value = 1;
		return 0;
	case DRM_CAP_PRIME:
		req->value = DRM_PRIME_CAP_IMPORT | DRM_PRIME_CAP_EXPORT;
		return 0;
	case DRM_CAP_SYNCOBJ:
		req->value = drm_core_check_feature(dev, DRIVER_SYNCOBJ);
		return 0;
	case DRM_CAP_SYNCOBJ_TIMELINE:
		req->value = drm_core_check_feature(dev, DRIVER_SYNCOBJ_TIMELINE);
		return 0;
	}

	/* Other caps only work with KMS drivers */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	switch (req->capability) {
	case DRM_CAP_DUMB_BUFFER:
		if (dev->driver->dumb_create)
			req->value = 1;
		break;
	case DRM_CAP_VBLANK_HIGH_CRTC:
		req->value = 1;
		break;
	case DRM_CAP_DUMB_PREFERRED_DEPTH:
		req->value = dev->mode_config.preferred_depth;
		break;
	case DRM_CAP_DUMB_PREFER_SHADOW:
		req->value = dev->mode_config.prefer_shadow;
		break;
	case DRM_CAP_ASYNC_PAGE_FLIP:
		req->value = dev->mode_config.async_page_flip;
		break;
	case DRM_CAP_PAGE_FLIP_TARGET:
		req->value = 1;
		drm_for_each_crtc(crtc, dev) {
			if (!crtc->funcs->page_flip_target)
				req->value = 0;
		}
		break;
	case DRM_CAP_CURSOR_WIDTH:
		if (dev->mode_config.cursor_width)
			req->value = dev->mode_config.cursor_width;
		else
			req->value = 64;
		break;
	case DRM_CAP_CURSOR_HEIGHT:
		if (dev->mode_config.cursor_height)
			req->value = dev->mode_config.cursor_height;
		else
			req->value = 64;
		break;
	case DRM_CAP_ADDFB2_MODIFIERS:
		req->value = !dev->mode_config.fb_modifiers_not_supported;
		break;
	case DRM_CAP_CRTC_IN_VBLANK_EVENT:
		req->value = 1;
		break;
	case DRM_CAP_ATOMIC_ASYNC_PAGE_FLIP:
		req->value = drm_core_check_feature(dev, DRIVER_ATOMIC) &&
			     dev->mode_config.async_page_flip;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * Set device/driver capabilities
 */
static int
drm_setclientcap(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_set_client_cap *req = data;

	/* No render-only settable capabilities for now */

	/* Below caps that only works with KMS drivers */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -EOPNOTSUPP;

	switch (req->capability) {
	case DRM_CLIENT_CAP_STEREO_3D:
		if (req->value > 1)
			return -EINVAL;
		file_priv->stereo_allowed = req->value;
		break;
	case DRM_CLIENT_CAP_UNIVERSAL_PLANES:
		if (req->value > 1)
			return -EINVAL;
		file_priv->universal_planes = req->value;
		break;
	case DRM_CLIENT_CAP_ATOMIC:
		if (!drm_core_check_feature(dev, DRIVER_ATOMIC))
			return -EOPNOTSUPP;
		/* The modesetting DDX has a totally broken idea of atomic. */
#ifdef __linux__
		if (current->comm[0] == 'X' && req->value == 1) {
#else
		if (curproc->p_p->ps_comm[0] == 'X' && req->value == 1) {
#endif
			pr_info("broken atomic modeset userspace detected, disabling atomic\n");
			return -EOPNOTSUPP;
		}
		if (req->value > 2)
			return -EINVAL;
		file_priv->atomic = req->value;
		file_priv->universal_planes = req->value;
		/*
		 * No atomic user-space blows up on aspect ratio mode bits.
		 */
		file_priv->aspect_ratio_allowed = req->value;
		break;
	case DRM_CLIENT_CAP_ASPECT_RATIO:
		if (req->value > 1)
			return -EINVAL;
		file_priv->aspect_ratio_allowed = req->value;
		break;
	case DRM_CLIENT_CAP_WRITEBACK_CONNECTORS:
		if (!file_priv->atomic)
			return -EINVAL;
		if (req->value > 1)
			return -EINVAL;
		file_priv->writeback_connectors = req->value;
		break;
	case DRM_CLIENT_CAP_CURSOR_PLANE_HOTSPOT:
		if (!drm_core_check_feature(dev, DRIVER_CURSOR_HOTSPOT))
			return -EOPNOTSUPP;
		if (!file_priv->atomic)
			return -EINVAL;
		if (req->value > 1)
			return -EINVAL;
		file_priv->supports_virtualized_cursor_plane = req->value;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Setversion ioctl.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_lock structure.
 * \return zero on success or negative number on failure.
 *
 * Sets the requested interface version
 */
static int drm_setversion(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_set_version *sv = data;
	int if_version, retcode = 0;

	mutex_lock(&dev->master_mutex);
	if (sv->drm_di_major != -1) {
		if (sv->drm_di_major != DRM_IF_MAJOR ||
		    sv->drm_di_minor < 0 || sv->drm_di_minor > DRM_IF_MINOR) {
			retcode = -EINVAL;
			goto done;
		}
		if_version = DRM_IF_VERSION(sv->drm_di_major,
					    sv->drm_di_minor);
		dev->if_version = max(if_version, dev->if_version);
		if (sv->drm_di_minor >= 1) {
			/*
			 * Version 1.1 includes tying of DRM to specific device
			 * Version 1.4 has proper PCI domain support
			 */
			retcode = drm_set_busid(dev, file_priv);
			if (retcode)
				goto done;
		}
	}

	if (sv->drm_dd_major != -1) {
		if (sv->drm_dd_major != dev->driver->major ||
		    sv->drm_dd_minor < 0 || sv->drm_dd_minor >
		    dev->driver->minor) {
			retcode = -EINVAL;
			goto done;
		}
	}

done:
	sv->drm_di_major = DRM_IF_MAJOR;
	sv->drm_di_minor = DRM_IF_MINOR;
	sv->drm_dd_major = dev->driver->major;
	sv->drm_dd_minor = dev->driver->minor;
	mutex_unlock(&dev->master_mutex);

	return retcode;
}

/**
 * drm_noop - DRM no-op ioctl implementation
 * @dev: DRM device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: DRM file for the ioctl call
 *
 * This no-op implementation for drm ioctls is useful for deprecated
 * functionality where we can't return a failure code because existing userspace
 * checks the result of the ioctl, but doesn't care about the action.
 *
 * Always returns successfully with 0.
 */
int drm_noop(struct drm_device *dev, void *data,
	     struct drm_file *file_priv)
{
	drm_dbg_core(dev, "\n");
	return 0;
}
EXPORT_SYMBOL(drm_noop);

/**
 * drm_invalid_op - DRM invalid ioctl implementation
 * @dev: DRM device for the ioctl
 * @data: data pointer for the ioctl
 * @file_priv: DRM file for the ioctl call
 *
 * This no-op implementation for drm ioctls is useful for deprecated
 * functionality where we really don't want to allow userspace to call the ioctl
 * any more. This is the case for old ums interfaces for drivers that
 * transitioned to kms gradually and so kept the old legacy tables around. This
 * only applies to radeon and i915 kms drivers, other drivers shouldn't need to
 * use this function.
 *
 * Always fails with a return value of -EINVAL.
 */
int drm_invalid_op(struct drm_device *dev, void *data,
		   struct drm_file *file_priv)
{
	return -EINVAL;
}
EXPORT_SYMBOL(drm_invalid_op);

/*
 * Copy and IOCTL return string to user space
 */
static int drm_copy_field(char __user *buf, size_t *buf_len, const char *value)
{
	size_t len;

	/* don't attempt to copy a NULL pointer */
	if (WARN_ONCE(!value, "BUG: the value to copy was not set!")) {
		*buf_len = 0;
		return 0;
	}

	/* don't overflow userbuf */
	len = strlen(value);
	if (len > *buf_len)
		len = *buf_len;

	/* let userspace know exact length of driver value (which could be
	 * larger than the userspace-supplied buffer) */
	*buf_len = strlen(value);

	/* finally, try filling in the userbuf */
	if (len && buf)
		if (copy_to_user(buf, value, len))
			return -EFAULT;
	return 0;
}

/*
 * Get version information
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_version structure.
 * \return zero on success or negative number on failure.
 *
 * Fills in the version information in \p arg.
 */
int drm_version(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct drm_version *version = data;
	int err;

	version->version_major = dev->driver->major;
	version->version_minor = dev->driver->minor;
	version->version_patchlevel = dev->driver->patchlevel;
	err = drm_copy_field(version->name, &version->name_len,
			dev->driver->name);

	/* Driver date is deprecated. Userspace expects a non-empty string. */
	if (!err)
		err = drm_copy_field(version->date, &version->date_len, "0");
	if (!err)
		err = drm_copy_field(version->desc, &version->desc_len,
				dev->driver->desc);

	return err;
}

static int drm_ioctl_permit(u32 flags, struct drm_file *file_priv)
{
	/* ROOT_ONLY is only for CAP_SYS_ADMIN */
	if (unlikely((flags & DRM_ROOT_ONLY) && !capable(CAP_SYS_ADMIN)))
		return -EACCES;

	/* AUTH is only for authenticated or render client */
	if (unlikely((flags & DRM_AUTH) && !drm_is_render_client(file_priv) &&
		     !file_priv->authenticated))
		return -EACCES;

	/* MASTER is only for master or control clients */
	if (unlikely((flags & DRM_MASTER) &&
		     !drm_is_current_master(file_priv)))
		return -EACCES;

	/* Render clients must be explicitly allowed */
	if (unlikely(!(flags & DRM_RENDER_ALLOW) &&
		     drm_is_render_client(file_priv)))
		return -EACCES;

	return 0;
}

#define DRM_IOCTL_DEF(ioctl, _func, _flags)	\
	[DRM_IOCTL_NR(ioctl)] = {		\
		.cmd = ioctl,			\
		.func = _func,			\
		.flags = _flags,		\
		.name = #ioctl			\
	}

/* Ioctl table */
static const struct drm_ioctl_desc drm_ioctls[] = {
	DRM_IOCTL_DEF(DRM_IOCTL_VERSION, drm_version, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_UNIQUE, drm_getunique, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_MAGIC, drm_getmagic, 0),

	DRM_IOCTL_DEF(DRM_IOCTL_GET_CLIENT, drm_getclient, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_STATS, drm_getstats, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_GET_CAP, drm_getcap, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_SET_CLIENT_CAP, drm_setclientcap, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_SET_VERSION, drm_setversion, DRM_MASTER),

	DRM_IOCTL_DEF(DRM_IOCTL_SET_UNIQUE, drm_invalid_op, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_BLOCK, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_UNBLOCK, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_AUTH_MAGIC, drm_authmagic, DRM_MASTER),

#ifdef __OpenBSD__
	DRM_IOCTL_DEF(DRM_IOCTL_GET_PCIINFO, drm_getpciinfo, DRM_RENDER_ALLOW),
#endif

#ifdef __linux__
	DRM_IOCTL_DEF(DRM_IOCTL_SET_MASTER, drm_setmaster_ioctl, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_DROP_MASTER, drm_dropmaster_ioctl, 0),
#else
	/* On OpenBSD xorg privdrop has already occurred before this point */
	DRM_IOCTL_DEF(DRM_IOCTL_SET_MASTER, drm_noop, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_DROP_MASTER, drm_noop, 0),
#endif

	DRM_IOCTL_DEF(DRM_IOCTL_ADD_DRAW, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF(DRM_IOCTL_RM_DRAW, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),

	DRM_IOCTL_DEF(DRM_IOCTL_FINISH, drm_noop, DRM_AUTH),

	DRM_IOCTL_DEF(DRM_IOCTL_WAIT_VBLANK, drm_wait_vblank_ioctl, 0),

	DRM_IOCTL_DEF(DRM_IOCTL_UPDATE_DRAW, drm_noop, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),

	DRM_IOCTL_DEF(DRM_IOCTL_GEM_CLOSE, drm_gem_close_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_GEM_FLINK, drm_gem_flink_ioctl, DRM_AUTH),
	DRM_IOCTL_DEF(DRM_IOCTL_GEM_OPEN, drm_gem_open_ioctl, DRM_AUTH),

	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETRESOURCES, drm_mode_getresources, 0),

	DRM_IOCTL_DEF(DRM_IOCTL_PRIME_HANDLE_TO_FD, drm_prime_handle_to_fd_ioctl, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_PRIME_FD_TO_HANDLE, drm_prime_fd_to_handle_ioctl, DRM_RENDER_ALLOW),

	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETPLANERESOURCES, drm_mode_getplane_res, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETCRTC, drm_mode_getcrtc, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_SETCRTC, drm_mode_setcrtc, DRM_MASTER),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETPLANE, drm_mode_getplane, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_SETPLANE, drm_mode_setplane, DRM_MASTER),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_CURSOR, drm_mode_cursor_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETGAMMA, drm_mode_gamma_get_ioctl, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_SETGAMMA, drm_mode_gamma_set_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETENCODER, drm_mode_getencoder, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETCONNECTOR, drm_mode_getconnector, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_ATTACHMODE, drm_noop, DRM_MASTER),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_DETACHMODE, drm_noop, DRM_MASTER),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETPROPERTY, drm_mode_getproperty_ioctl, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_SETPROPERTY, drm_connector_property_set_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETPROPBLOB, drm_mode_getblob_ioctl, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETFB, drm_mode_getfb, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GETFB2, drm_mode_getfb2_ioctl, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_ADDFB, drm_mode_addfb_ioctl, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_ADDFB2, drm_mode_addfb2_ioctl, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_RMFB, drm_mode_rmfb_ioctl, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_CLOSEFB, drm_mode_closefb_ioctl, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_PAGE_FLIP, drm_mode_page_flip_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_DIRTYFB, drm_mode_dirtyfb_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_CREATE_DUMB, drm_mode_create_dumb_ioctl, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_MAP_DUMB, drm_mode_mmap_dumb_ioctl, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_DESTROY_DUMB, drm_mode_destroy_dumb_ioctl, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_OBJ_GETPROPERTIES, drm_mode_obj_get_properties_ioctl, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_OBJ_SETPROPERTY, drm_mode_obj_set_property_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_CURSOR2, drm_mode_cursor2_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_ATOMIC, drm_mode_atomic_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_CREATEPROPBLOB, drm_mode_createblob_ioctl, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_DESTROYPROPBLOB, drm_mode_destroyblob_ioctl, 0),

	DRM_IOCTL_DEF(DRM_IOCTL_SYNCOBJ_CREATE, drm_syncobj_create_ioctl,
		      DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_SYNCOBJ_DESTROY, drm_syncobj_destroy_ioctl,
		      DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, drm_syncobj_handle_to_fd_ioctl,
		      DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, drm_syncobj_fd_to_handle_ioctl,
		      DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_SYNCOBJ_TRANSFER, drm_syncobj_transfer_ioctl,
		      DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_SYNCOBJ_WAIT, drm_syncobj_wait_ioctl,
		      DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT, drm_syncobj_timeline_wait_ioctl,
		      DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_SYNCOBJ_EVENTFD, drm_syncobj_eventfd_ioctl,
		      DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_SYNCOBJ_RESET, drm_syncobj_reset_ioctl,
		      DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_SYNCOBJ_SIGNAL, drm_syncobj_signal_ioctl,
		      DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL, drm_syncobj_timeline_signal_ioctl,
		      DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_SYNCOBJ_QUERY, drm_syncobj_query_ioctl,
		      DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF(DRM_IOCTL_CRTC_GET_SEQUENCE, drm_crtc_get_sequence_ioctl, 0),
	DRM_IOCTL_DEF(DRM_IOCTL_CRTC_QUEUE_SEQUENCE, drm_crtc_queue_sequence_ioctl, 0),
#ifdef __linux__
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_CREATE_LEASE, drm_mode_create_lease_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_LIST_LESSEES, drm_mode_list_lessees_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_GET_LEASE, drm_mode_get_lease_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF(DRM_IOCTL_MODE_REVOKE_LEASE, drm_mode_revoke_lease_ioctl, DRM_MASTER),
#endif
};

#define DRM_CORE_IOCTL_COUNT	ARRAY_SIZE(drm_ioctls)

int
pledge_ioctl_drm(struct proc *p, long com, dev_t device)
{
	struct drm_device *dev = drm_get_device_from_kdev(device);
	unsigned int nr = DRM_IOCTL_NR(com);
	const struct drm_ioctl_desc *ioctl;

	if (dev == NULL)
		return EPERM;

	if (nr < DRM_CORE_IOCTL_COUNT &&
	    ((nr < DRM_COMMAND_BASE || nr >= DRM_COMMAND_END)))
		ioctl = &drm_ioctls[nr];
	else if (nr >= DRM_COMMAND_BASE && nr < DRM_COMMAND_END &&
	    nr < DRM_COMMAND_BASE + dev->driver->num_ioctls)
		ioctl = &dev->driver->ioctls[nr - DRM_COMMAND_BASE];
	else
		return EPERM;

	if (ioctl->flags & DRM_RENDER_ALLOW)
		return 0;

	/*
	 * These are dangerous, but we have to allow them until we
	 * have prime/dma-buf support.
	 */
	switch (com) {
	case DRM_IOCTL_GET_MAGIC:
	case DRM_IOCTL_GEM_OPEN:
		return 0;
	}

	/* for amdgpu libdrm */
	if (com == DRM_IOCTL_GET_CLIENT)
		return 0;

	return EPERM;
}

/**
 * DOC: driver specific ioctls
 *
 * First things first, driver private IOCTLs should only be needed for drivers
 * supporting rendering. Kernel modesetting is all standardized, and extended
 * through properties. There are a few exceptions in some existing drivers,
 * which define IOCTL for use by the display DRM master, but they all predate
 * properties.
 *
 * Now if you do have a render driver you always have to support it through
 * driver private properties. There's a few steps needed to wire all the things
 * up.
 *
 * First you need to define the structure for your IOCTL in your driver private
 * UAPI header in ``include/uapi/drm/my_driver_drm.h``::
 *
 *     struct my_driver_operation {
 *             u32 some_thing;
 *             u32 another_thing;
 *     };
 *
 * Please make sure that you follow all the best practices from
 * ``Documentation/process/botching-up-ioctls.rst``. Note that drm_ioctl()
 * automatically zero-extends structures, hence make sure you can add more stuff
 * at the end, i.e. don't put a variable sized array there.
 *
 * Then you need to define your IOCTL number, using one of DRM_IO(), DRM_IOR(),
 * DRM_IOW() or DRM_IOWR(). It must start with the DRM_IOCTL\_ prefix::
 *
 *     ##define DRM_IOCTL_MY_DRIVER_OPERATION \
 *         DRM_IOW(DRM_COMMAND_BASE, struct my_driver_operation)
 *
 * DRM driver private IOCTL must be in the range from DRM_COMMAND_BASE to
 * DRM_COMMAND_END. Finally you need an array of &struct drm_ioctl_desc to wire
 * up the handlers and set the access rights::
 *
 *     static const struct drm_ioctl_desc my_driver_ioctls[] = {
 *         DRM_IOCTL_DEF_DRV(MY_DRIVER_OPERATION, my_driver_operation,
 *                 DRM_AUTH|DRM_RENDER_ALLOW),
 *     };
 *
 * And then assign this to the &drm_driver.ioctls field in your driver
 * structure.
 *
 * See the separate chapter on :ref:`file operations<drm_driver_fops>` for how
 * the driver-specific IOCTLs are wired up.
 */

long drm_ioctl_kernel(struct file *file, drm_ioctl_t *func, void *kdata,
		      u32 flags)
{
	STUB();
	return -ENOSYS;
#ifdef notyet
	struct drm_file *file_priv = file->private_data;
	struct drm_device *dev = file_priv->minor->dev;
	int ret;

	/* Update drm_file owner if fd was passed along. */
	drm_file_update_pid(file_priv);

	if (drm_dev_is_unplugged(dev))
		return -ENODEV;

	ret = drm_ioctl_permit(flags, file_priv);
	if (unlikely(ret))
		return ret;

	return func(dev, kdata, file_priv);
}
EXPORT_SYMBOL(drm_ioctl_kernel);

/**
 * drm_ioctl - ioctl callback implementation for DRM drivers
 * @filp: file this ioctl is called on
 * @cmd: ioctl cmd number
 * @arg: user argument
 *
 * Looks up the ioctl function in the DRM core and the driver dispatch table,
 * stored in &drm_driver.ioctls. It checks for necessary permission by calling
 * drm_ioctl_permit(), and dispatches to the respective function.
 *
 * Returns:
 * Zero on success, negative error code on failure.
 */
long drm_ioctl(struct file *filp,
	      unsigned int cmd, unsigned long arg)
{
	STUB();
	return -ENOSYS;
#ifdef notyet
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev;
	const struct drm_ioctl_desc *ioctl = NULL;
	drm_ioctl_t *func;
	unsigned int nr = DRM_IOCTL_NR(cmd);
	int retcode = -EINVAL;
	char stack_kdata[128];
	char *kdata = NULL;
	unsigned int in_size, out_size, drv_size, ksize;
	bool is_driver_ioctl;

	dev = file_priv->minor->dev;

	if (drm_dev_is_unplugged(dev))
		return -ENODEV;

	if (DRM_IOCTL_TYPE(cmd) != DRM_IOCTL_BASE)
		return -ENOTTY;

	is_driver_ioctl = nr >= DRM_COMMAND_BASE && nr < DRM_COMMAND_END;

	if (is_driver_ioctl) {
		/* driver ioctl */
		unsigned int index = nr - DRM_COMMAND_BASE;

		if (index >= dev->driver->num_ioctls)
			goto err_i1;
		index = array_index_nospec(index, dev->driver->num_ioctls);
		ioctl = &dev->driver->ioctls[index];
	} else {
		/* core ioctl */
		if (nr >= DRM_CORE_IOCTL_COUNT)
			goto err_i1;
		nr = array_index_nospec(nr, DRM_CORE_IOCTL_COUNT);
		ioctl = &drm_ioctls[nr];
	}

	drv_size = _IOC_SIZE(ioctl->cmd);
	out_size = in_size = _IOC_SIZE(cmd);
	if ((cmd & ioctl->cmd & IOC_IN) == 0)
		in_size = 0;
	if ((cmd & ioctl->cmd & IOC_OUT) == 0)
		out_size = 0;
	ksize = max(max(in_size, out_size), drv_size);

	drm_dbg_core(dev, "comm=\"%s\" pid=%d, dev=0x%lx, auth=%d, %s\n",
		     current->comm, task_pid_nr(current),
		     (long)old_encode_dev(file_priv->minor->kdev->devt),
		     file_priv->authenticated, ioctl->name);

	/* Do not trust userspace, use our own definition */
	func = ioctl->func;

	if (unlikely(!func)) {
		drm_dbg_core(dev, "no function\n");
		retcode = -EINVAL;
		goto err_i1;
	}

	if (ksize <= sizeof(stack_kdata)) {
		kdata = stack_kdata;
	} else {
		kdata = kmalloc(ksize, GFP_KERNEL);
		if (!kdata) {
			retcode = -ENOMEM;
			goto err_i1;
		}
	}

	if (copy_from_user(kdata, (void __user *)arg, in_size) != 0) {
		retcode = -EFAULT;
		goto err_i1;
	}

	if (ksize > in_size)
		memset(kdata + in_size, 0, ksize - in_size);

	retcode = drm_ioctl_kernel(filp, func, kdata, ioctl->flags);
	if (copy_to_user((void __user *)arg, kdata, out_size) != 0)
		retcode = -EFAULT;

      err_i1:
	if (!ioctl)
		drm_dbg_core(dev,
			     "invalid ioctl: comm=\"%s\", pid=%d, dev=0x%lx, auth=%d, cmd=0x%02x, nr=0x%02x\n",
			     current->comm, task_pid_nr(current),
			     (long)old_encode_dev(file_priv->minor->kdev->devt),
			     file_priv->authenticated, cmd, nr);

	if (kdata != stack_kdata)
		kfree(kdata);
	if (retcode)
		drm_dbg_core(dev, "comm=\"%s\", pid=%d, ret=%d\n",
			     current->comm, task_pid_nr(current), retcode);
	return retcode;
#endif
#endif
}
EXPORT_SYMBOL(drm_ioctl);

/**
 * drm_ioctl_flags - Check for core ioctl and return ioctl permission flags
 * @nr: ioctl number
 * @flags: where to return the ioctl permission flags
 *
 * This ioctl is only used by the vmwgfx driver to augment the access checks
 * done by the drm core and insofar a pretty decent layering violation. This
 * shouldn't be used by any drivers.
 *
 * Returns:
 * True if the @nr corresponds to a DRM core ioctl number, false otherwise.
 */
bool drm_ioctl_flags(unsigned int nr, unsigned int *flags)
{
	if (nr >= DRM_COMMAND_BASE && nr < DRM_COMMAND_END)
		return false;

	if (nr >= DRM_CORE_IOCTL_COUNT)
		return false;
	nr = array_index_nospec(nr, DRM_CORE_IOCTL_COUNT);

	*flags = drm_ioctls[nr].flags;
	return true;
}
EXPORT_SYMBOL(drm_ioctl_flags);

int
drm_do_ioctl(struct drm_device *dev, int minor, u_long cmd, caddr_t data)
{
	struct drm_file *file_priv;
	const struct drm_ioctl_desc *ioctl;
	drm_ioctl_t *func;
	unsigned int nr = DRM_IOCTL_NR(cmd);
	int retcode = -EINVAL;
	unsigned int usize, asize;
	caddr_t adata = data;

	mutex_lock(&dev->filelist_mutex);
	file_priv = drm_find_file_by_minor(dev, minor);
	mutex_unlock(&dev->filelist_mutex);
	if (file_priv == NULL) {
		DRM_ERROR("can't find authenticator\n");
		return -EINVAL;
	}

	DRM_DEBUG("pid=%d, cmd=0x%02lx, nr=0x%02x, dev 0x%lx, auth=%d\n",
	    curproc->p_p->ps_pid, cmd, (u_int)DRM_IOCTL_NR(cmd), (long)&dev->dev,
	    file_priv->authenticated);

	switch (cmd) {
	case FIOASYNC:
		return 0;
	}

	if ((nr >= DRM_CORE_IOCTL_COUNT) &&
	    ((nr < DRM_COMMAND_BASE) || (nr >= DRM_COMMAND_END)))
		return (-EINVAL);
	if ((nr >= DRM_COMMAND_BASE) && (nr < DRM_COMMAND_END) &&
	    (nr < DRM_COMMAND_BASE + dev->driver->num_ioctls)) {
		uint32_t drv_size;
		ioctl = &dev->driver->ioctls[nr - DRM_COMMAND_BASE];
		drv_size = IOCPARM_LEN(ioctl->cmd);
		usize = asize = IOCPARM_LEN(cmd);
		if (drv_size > asize)
			asize = drv_size;
	} else if ((nr >= DRM_COMMAND_END) || (nr < DRM_COMMAND_BASE)) {
		uint32_t drv_size;
		ioctl = &drm_ioctls[nr];

		drv_size = IOCPARM_LEN(ioctl->cmd);
		usize = asize = IOCPARM_LEN(cmd);
		if (drv_size > asize)
			asize = drv_size;
		cmd = ioctl->cmd;
	} else
		return (-EINVAL);

	func = ioctl->func;
	if (!func) {
		DRM_DEBUG("no function\n");
		return (-EINVAL);
	}

	retcode = drm_ioctl_permit(ioctl->flags, file_priv);
	if (unlikely(retcode))
		return retcode;

	if (asize > usize) {
		adata = malloc(asize, M_DRM, M_WAITOK | M_ZERO);
		memcpy(adata, data, usize);
	}

	retcode = func(dev, adata, file_priv);

	if (asize > usize) {
		memcpy(data, adata, usize);
		free(adata, M_DRM, asize);
	}

	return (retcode);
}

/* drmioctl is called whenever a process performs an ioctl on /dev/drm.
 */
int
drmioctl(dev_t kdev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct drm_device *dev = drm_get_device_from_kdev(kdev);
	int error;

	if (dev == NULL)
		return ENODEV;

	mtx_enter(&dev->quiesce_mtx);
	while (dev->quiesce)
		msleep_nsec(&dev->quiesce, &dev->quiesce_mtx, PZERO, "drmioc",
		    INFSLP);
	dev->quiesce_count++;
	mtx_leave(&dev->quiesce_mtx);

	error = -drm_do_ioctl(dev, minor(kdev), cmd, data);
	if (error < 0 && error != ERESTART && error != EJUSTRETURN)
		printf("%s: cmd 0x%lx errno %d\n", __func__, cmd, error);

	mtx_enter(&dev->quiesce_mtx);
	dev->quiesce_count--;
	if (dev->quiesce)
		wakeup(&dev->quiesce_count);
	mtx_leave(&dev->quiesce_mtx);

	return (error);
}
