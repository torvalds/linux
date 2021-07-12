/*
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

#ifndef _DRM_FILE_H_
#define _DRM_FILE_H_

#include <linux/types.h>
#include <linux/completion.h>
#include <linux/idr.h>

#include <uapi/drm/drm.h>

#include <drm/drm_prime.h>

struct dma_fence;
struct drm_file;
struct drm_device;
struct device;
struct file;

/*
 * FIXME: Not sure we want to have drm_minor here in the end, but to avoid
 * header include loops we need it here for now.
 */

/* Note that the order of this enum is ABI (it determines
 * /dev/dri/renderD* numbers).
 */
enum drm_minor_type {
	DRM_MINOR_PRIMARY,
	DRM_MINOR_CONTROL,
	DRM_MINOR_RENDER,
};

/**
 * struct drm_minor - DRM device minor structure
 *
 * This structure represents a DRM minor number for device nodes in /dev.
 * Entirely opaque to drivers and should never be inspected directly by drivers.
 * Drivers instead should only interact with &struct drm_file and of course
 * &struct drm_device, which is also where driver-private data and resources can
 * be attached to.
 */
struct drm_minor {
	/* private: */
	int index;			/* Minor device number */
	int type;                       /* Control or render */
	struct device *kdev;		/* Linux device */
	struct drm_device *dev;

	struct dentry *debugfs_root;

	struct list_head debugfs_list;
	struct mutex debugfs_lock; /* Protects debugfs_list. */
};

/**
 * struct drm_pending_event - Event queued up for userspace to read
 *
 * This represents a DRM event. Drivers can use this as a generic completion
 * mechanism, which supports kernel-internal &struct completion, &struct dma_fence
 * and also the DRM-specific &struct drm_event delivery mechanism.
 */
struct drm_pending_event {
	/**
	 * @completion:
	 *
	 * Optional pointer to a kernel internal completion signalled when
	 * drm_send_event() is called, useful to internally synchronize with
	 * nonblocking operations.
	 */
	struct completion *completion;

	/**
	 * @completion_release:
	 *
	 * Optional callback currently only used by the atomic modeset helpers
	 * to clean up the reference count for the structure @completion is
	 * stored in.
	 */
	void (*completion_release)(struct completion *completion);

	/**
	 * @event:
	 *
	 * Pointer to the actual event that should be sent to userspace to be
	 * read using drm_read(). Can be optional, since nowadays events are
	 * also used to signal kernel internal threads with @completion or DMA
	 * transactions using @fence.
	 */
	struct drm_event *event;

	/**
	 * @fence:
	 *
	 * Optional DMA fence to unblock other hardware transactions which
	 * depend upon the nonblocking DRM operation this event represents.
	 */
	struct dma_fence *fence;

	/**
	 * @file_priv:
	 *
	 * &struct drm_file where @event should be delivered to. Only set when
	 * @event is set.
	 */
	struct drm_file *file_priv;

	/**
	 * @link:
	 *
	 * Double-linked list to keep track of this event. Can be used by the
	 * driver up to the point when it calls drm_send_event(), after that
	 * this list entry is owned by the core for its own book-keeping.
	 */
	struct list_head link;

	/**
	 * @pending_link:
	 *
	 * Entry on &drm_file.pending_event_list, to keep track of all pending
	 * events for @file_priv, to allow correct unwinding of them when
	 * userspace closes the file before the event is delivered.
	 */
	struct list_head pending_link;
};

/**
 * struct drm_file - DRM file private data
 *
 * This structure tracks DRM state per open file descriptor.
 */
struct drm_file {
	/**
	 * @authenticated:
	 *
	 * Whether the client is allowed to submit rendering, which for legacy
	 * nodes means it must be authenticated.
	 *
	 * See also the :ref:`section on primary nodes and authentication
	 * <drm_primary_node>`.
	 */
	bool authenticated;

	/**
	 * @stereo_allowed:
	 *
	 * True when the client has asked us to expose stereo 3D mode flags.
	 */
	bool stereo_allowed;

	/**
	 * @universal_planes:
	 *
	 * True if client understands CRTC primary planes and cursor planes
	 * in the plane list. Automatically set when @atomic is set.
	 */
	bool universal_planes;

	/** @atomic: True if client understands atomic properties. */
	bool atomic;

	/**
	 * @aspect_ratio_allowed:
	 *
	 * True, if client can handle picture aspect ratios, and has requested
	 * to pass this information along with the mode.
	 */
	bool aspect_ratio_allowed;

	/**
	 * @writeback_connectors:
	 *
	 * True if client understands writeback connectors
	 */
	bool writeback_connectors;

	/**
	 * @was_master:
	 *
	 * This client has or had, master capability. Protected by struct
	 * &drm_device.master_mutex.
	 *
	 * This is used to ensure that CAP_SYS_ADMIN is not enforced, if the
	 * client is or was master in the past.
	 */
	bool was_master;

	/**
	 * @is_master:
	 *
	 * This client is the creator of @master. Protected by struct
	 * &drm_device.master_mutex.
	 *
	 * See also the :ref:`section on primary nodes and authentication
	 * <drm_primary_node>`.
	 */
	bool is_master;

	/**
	 * @master:
	 *
	 * Master this node is currently associated with. Protected by struct
	 * &drm_device.master_mutex, and serialized by @master_lookup_lock.
	 *
	 * Only relevant if drm_is_primary_client() returns true. Note that
	 * this only matches &drm_device.master if the master is the currently
	 * active one.
	 *
	 * When dereferencing this pointer, either hold struct
	 * &drm_device.master_mutex for the duration of the pointer's use, or
	 * use drm_file_get_master() if struct &drm_device.master_mutex is not
	 * currently held and there is no other need to hold it. This prevents
	 * @master from being freed during use.
	 *
	 * See also @authentication and @is_master and the :ref:`section on
	 * primary nodes and authentication <drm_primary_node>`.
	 */
	struct drm_master *master;

	/** @master_lock: Serializes @master. */
	spinlock_t master_lookup_lock;

	/** @pid: Process that opened this file. */
	struct pid *pid;

	/** @magic: Authentication magic, see @authenticated. */
	drm_magic_t magic;

	/**
	 * @lhead:
	 *
	 * List of all open files of a DRM device, linked into
	 * &drm_device.filelist. Protected by &drm_device.filelist_mutex.
	 */
	struct list_head lhead;

	/** @minor: &struct drm_minor for this file. */
	struct drm_minor *minor;

	/**
	 * @object_idr:
	 *
	 * Mapping of mm object handles to object pointers. Used by the GEM
	 * subsystem. Protected by @table_lock.
	 */
	struct idr object_idr;

	/** @table_lock: Protects @object_idr. */
	spinlock_t table_lock;

	/** @syncobj_idr: Mapping of sync object handles to object pointers. */
	struct idr syncobj_idr;
	/** @syncobj_table_lock: Protects @syncobj_idr. */
	spinlock_t syncobj_table_lock;

	/** @filp: Pointer to the core file structure. */
	struct file *filp;

	/**
	 * @driver_priv:
	 *
	 * Optional pointer for driver private data. Can be allocated in
	 * &drm_driver.open and should be freed in &drm_driver.postclose.
	 */
	void *driver_priv;

	/**
	 * @fbs:
	 *
	 * List of &struct drm_framebuffer associated with this file, using the
	 * &drm_framebuffer.filp_head entry.
	 *
	 * Protected by @fbs_lock. Note that the @fbs list holds a reference on
	 * the framebuffer object to prevent it from untimely disappearing.
	 */
	struct list_head fbs;

	/** @fbs_lock: Protects @fbs. */
	struct mutex fbs_lock;

	/**
	 * @blobs:
	 *
	 * User-created blob properties; this retains a reference on the
	 * property.
	 *
	 * Protected by @drm_mode_config.blob_lock;
	 */
	struct list_head blobs;

	/** @event_wait: Waitqueue for new events added to @event_list. */
	wait_queue_head_t event_wait;

	/**
	 * @pending_event_list:
	 *
	 * List of pending &struct drm_pending_event, used to clean up pending
	 * events in case this file gets closed before the event is signalled.
	 * Uses the &drm_pending_event.pending_link entry.
	 *
	 * Protect by &drm_device.event_lock.
	 */
	struct list_head pending_event_list;

	/**
	 * @event_list:
	 *
	 * List of &struct drm_pending_event, ready for delivery to userspace
	 * through drm_read(). Uses the &drm_pending_event.link entry.
	 *
	 * Protect by &drm_device.event_lock.
	 */
	struct list_head event_list;

	/**
	 * @event_space:
	 *
	 * Available event space to prevent userspace from
	 * exhausting kernel memory. Currently limited to the fairly arbitrary
	 * value of 4KB.
	 */
	int event_space;

	/** @event_read_lock: Serializes drm_read(). */
	struct mutex event_read_lock;

	/**
	 * @prime:
	 *
	 * Per-file buffer caches used by the PRIME buffer sharing code.
	 */
	struct drm_prime_file_private prime;

	/* private: */
#if IS_ENABLED(CONFIG_DRM_LEGACY)
	unsigned long lock_count; /* DRI1 legacy lock count */
#endif
};

/**
 * drm_is_primary_client - is this an open file of the primary node
 * @file_priv: DRM file
 *
 * Returns true if this is an open file of the primary node, i.e.
 * &drm_file.minor of @file_priv is a primary minor.
 *
 * See also the :ref:`section on primary nodes and authentication
 * <drm_primary_node>`.
 */
static inline bool drm_is_primary_client(const struct drm_file *file_priv)
{
	return file_priv->minor->type == DRM_MINOR_PRIMARY;
}

/**
 * drm_is_render_client - is this an open file of the render node
 * @file_priv: DRM file
 *
 * Returns true if this is an open file of the render node, i.e.
 * &drm_file.minor of @file_priv is a render minor.
 *
 * See also the :ref:`section on render nodes <drm_render_node>`.
 */
static inline bool drm_is_render_client(const struct drm_file *file_priv)
{
	return file_priv->minor->type == DRM_MINOR_RENDER;
}

int drm_open(struct inode *inode, struct file *filp);
ssize_t drm_read(struct file *filp, char __user *buffer,
		 size_t count, loff_t *offset);
int drm_release(struct inode *inode, struct file *filp);
int drm_release_noglobal(struct inode *inode, struct file *filp);
__poll_t drm_poll(struct file *filp, struct poll_table_struct *wait);
int drm_event_reserve_init_locked(struct drm_device *dev,
				  struct drm_file *file_priv,
				  struct drm_pending_event *p,
				  struct drm_event *e);
int drm_event_reserve_init(struct drm_device *dev,
			   struct drm_file *file_priv,
			   struct drm_pending_event *p,
			   struct drm_event *e);
void drm_event_cancel_free(struct drm_device *dev,
			   struct drm_pending_event *p);
void drm_send_event_locked(struct drm_device *dev, struct drm_pending_event *e);
void drm_send_event(struct drm_device *dev, struct drm_pending_event *e);

struct file *mock_drm_getfile(struct drm_minor *minor, unsigned int flags);

#ifdef CONFIG_MMU
struct drm_vma_offset_manager;
unsigned long drm_get_unmapped_area(struct file *file,
				    unsigned long uaddr, unsigned long len,
				    unsigned long pgoff, unsigned long flags,
				    struct drm_vma_offset_manager *mgr);
#endif /* CONFIG_MMU */


#endif /* _DRM_FILE_H_ */
