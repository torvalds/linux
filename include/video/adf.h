/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _VIDEO_ADF_H
#define _VIDEO_ADF_H

#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/idr.h>
#include <linux/kref.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <uapi/video/adf.h>
#include "sync.h"

struct adf_obj;
struct adf_obj_ops;
struct adf_device;
struct adf_device_ops;
struct adf_interface;
struct adf_interface_ops;
struct adf_overlay_engine;
struct adf_overlay_engine_ops;

/**
 * struct adf_buffer - buffer displayed by adf_post
 *
 * @overlay_engine: target overlay engine
 * @w: width of display region in pixels
 * @h: height of display region in pixels
 * @format: DRM-style fourcc, see drm_fourcc.h for standard formats
 * @dma_bufs: dma_buf for each plane
 * @offset: location of first pixel to scan out, in bytes
 * @pitch: length of a scanline including padding, in bytes
 * @n_planes: number of planes in buffer
 * @acquire_fence: sync_fence which will clear when the buffer is
 *	ready for display
 *
 * &struct adf_buffer is the in-kernel counterpart to the userspace-facing
 * &struct adf_buffer_config.
 */
struct adf_buffer {
	struct adf_overlay_engine *overlay_engine;

	u32 w;
	u32 h;
	u32 format;

	struct dma_buf *dma_bufs[ADF_MAX_PLANES];
	u32 offset[ADF_MAX_PLANES];
	u32 pitch[ADF_MAX_PLANES];
	u8 n_planes;

	struct sync_fence *acquire_fence;
};

/**
 * struct adf_buffer_mapping - state for mapping a &struct adf_buffer into the
 * display device
 *
 * @attachments: dma-buf attachment for each plane
 * @sg_tables: SG tables for each plane
 */
struct adf_buffer_mapping {
	struct dma_buf_attachment *attachments[ADF_MAX_PLANES];
	struct sg_table *sg_tables[ADF_MAX_PLANES];
};

/**
 * struct adf_post - request to flip to a new set of buffers
 *
 * @n_bufs: number of buffers displayed
 * @bufs: buffers displayed
 * @mappings: in-device mapping state for each buffer
 * @custom_data_size: size of driver-private data
 * @custom_data: driver-private data
 *
 * &struct adf_post is the in-kernel counterpart to the userspace-facing
 * &struct adf_post_config.
 */
struct adf_post {
	size_t n_bufs;
	struct adf_buffer *bufs;
	struct adf_buffer_mapping *mappings;

	size_t custom_data_size;
	void *custom_data;
};

/**
 * struct adf_attachment - description of attachment between an overlay engine
 * and an interface
 *
 * @overlay_engine: the overlay engine
 * @interface: the interface
 *
 * &struct adf_attachment is the in-kernel counterpart to the userspace-facing
 * &struct adf_attachment_config.
 */
struct adf_attachment {
	struct adf_overlay_engine *overlay_engine;
	struct adf_interface *interface;
};

struct adf_pending_post {
	struct list_head head;
	struct adf_post config;
	void *state;
};

enum adf_obj_type {
	ADF_OBJ_OVERLAY_ENGINE = 0,
	ADF_OBJ_INTERFACE = 1,
	ADF_OBJ_DEVICE = 2,
};

/**
 * struct adf_obj_ops - common ADF object implementation ops
 *
 * @open: handle opening the object's device node
 * @release: handle releasing an open file
 * @ioctl: handle custom ioctls
 *
 * @supports_event: return whether the object supports generating events of type
 *	@type
 * @set_event: enable or disable events of type @type
 * @event_type_str: return a string representation of custom event @type
 *	(@type >= %ADF_EVENT_DEVICE_CUSTOM).
 *
 * @custom_data: copy up to %ADF_MAX_CUSTOM_DATA_SIZE bytes of driver-private
 *	data into @data (allocated by ADF) and return the number of copied bytes
 *	in @size.  Return 0 on success or an error code (<0) on failure.
 */
struct adf_obj_ops {
	/* optional */
	int (*open)(struct adf_obj *obj, struct inode *inode,
			struct file *file);
	/* optional */
	void (*release)(struct adf_obj *obj, struct inode *inode,
			struct file *file);
	/* optional */
	long (*ioctl)(struct adf_obj *obj, unsigned int cmd, unsigned long arg);

	/* optional */
	bool (*supports_event)(struct adf_obj *obj, enum adf_event_type type);
	/* required if supports_event is implemented */
	void (*set_event)(struct adf_obj *obj, enum adf_event_type type,
			bool enabled);
	/* optional */
	const char *(*event_type_str)(struct adf_obj *obj,
			enum adf_event_type type);

	/* optional */
	int (*custom_data)(struct adf_obj *obj, void *data, size_t *size);
};

struct adf_obj {
	enum adf_obj_type type;
	char name[ADF_NAME_LEN];
	struct adf_device *parent;

	const struct adf_obj_ops *ops;

	struct device dev;

	struct spinlock file_lock;
	struct list_head file_list;

	struct mutex event_lock;
	struct rb_root event_refcount;

	int id;
	int minor;
};

/**
 * struct adf_device_quirks - common display device quirks
 *
 * @buffer_padding: whether the last scanline of a buffer extends to the
 * 	buffer's pitch (@ADF_BUFFER_PADDED_TO_PITCH) or just to the visible
 * 	width (@ADF_BUFFER_UNPADDED)
 */
struct adf_device_quirks {
	/* optional, defaults to ADF_BUFFER_PADDED_TO_PITCH */
	enum {
		ADF_BUFFER_PADDED_TO_PITCH = 0,
		ADF_BUFFER_UNPADDED = 1,
	} buffer_padding;
};

/**
 * struct adf_device_ops - display device implementation ops
 *
 * @owner: device's module
 * @base: common operations (see &struct adf_obj_ops)
 * @quirks: device's quirks (see &struct adf_device_quirks)
 *
 * @attach: attach overlay engine @eng to interface @intf.  Return 0 on success
 *	or error code (<0) on failure.
 * @detach: detach overlay engine @eng from interface @intf.  Return 0 on
 *	success or error code (<0) on failure.
 *
 * @validate_custom_format: validate the number and size of planes
 *	in buffers with a custom format (i.e., not one of the @DRM_FORMAT_*
 *	types defined in drm/drm_fourcc.h).  Return 0 if the buffer is valid or
 *	an error code (<0) otherwise.
 *
 * @validate: validate that the proposed configuration @cfg is legal.  The
 *	driver may optionally allocate and return some driver-private state in
 *	@driver_state, which will be passed to the corresponding post().  The
 *	driver may NOT commit any changes to hardware.  Return 0 if @cfg is
 *	valid or an error code (<0) otherwise.
 * @complete_fence: create a hardware-backed sync fence to be signaled when
 *	@cfg is removed from the screen.  If unimplemented, ADF automatically
 *	creates an sw_sync fence.  Return the sync fence on success or a
 *	PTR_ERR() on failure.
 * @post: flip @cfg onto the screen.  Wait for the display to begin scanning out
 *	@cfg before returning.
 * @advance_timeline: signal the sync fence for the last configuration to leave
 *	the display.  If unimplemented, ADF automatically advances an sw_sync
 *	timeline.
 * @state_free: free driver-private state allocated during validate()
 */
struct adf_device_ops {
	/* required */
	struct module *owner;
	const struct adf_obj_ops base;
	/* optional */
	const struct adf_device_quirks quirks;

	/* optional */
	int (*attach)(struct adf_device *dev, struct adf_overlay_engine *eng,
			struct adf_interface *intf);
	/* optional */
	int (*detach)(struct adf_device *dev, struct adf_overlay_engine *eng,
			struct adf_interface *intf);

	/* required if any of the device's overlay engines supports at least one
	   custom format */
	int (*validate_custom_format)(struct adf_device *dev,
			struct adf_buffer *buf);

	/* required */
	int (*validate)(struct adf_device *dev, struct adf_post *cfg,
			void **driver_state);
	/* optional */
	struct sync_fence *(*complete_fence)(struct adf_device *dev,
			struct adf_post *cfg, void *driver_state);
	/* required */
	void (*post)(struct adf_device *dev, struct adf_post *cfg,
			void *driver_state);
	/* required if complete_fence is implemented */
	void (*advance_timeline)(struct adf_device *dev,
			struct adf_post *cfg, void *driver_state);
	/* required if validate allocates driver state */
	void (*state_free)(struct adf_device *dev, void *driver_state);
};

struct adf_attachment_list {
	struct adf_attachment attachment;
	struct list_head head;
};

struct adf_device {
	struct adf_obj base;
	struct device *dev;

	const struct adf_device_ops *ops;

	struct mutex client_lock;

	struct idr interfaces;
	size_t n_interfaces;
	struct idr overlay_engines;

	struct list_head post_list;
	struct mutex post_lock;
	struct kthread_worker post_worker;
	struct task_struct *post_thread;
	struct kthread_work post_work;

	struct list_head attached;
	size_t n_attached;
	struct list_head attach_allowed;
	size_t n_attach_allowed;

	struct adf_pending_post *onscreen;

	struct sw_sync_timeline *timeline;
	int timeline_max;
};

/**
 * struct adf_interface_ops - display interface implementation ops
 *
 * @base: common operations (see &struct adf_obj_ops)
 *
 * @blank: change the display's DPMS state.  Return 0 on success or error
 *	code (<0) on failure.
 *
 * @alloc_simple_buffer: allocate a buffer with the specified @w, @h, and
 *	@format.  @format will be a standard RGB format (i.e.,
 *	adf_format_is_rgb(@format) == true).  Return 0 on success or error code
 *	(<0) on failure.  On success, return the buffer, offset, and pitch in
 *	@dma_buf, @offset, and @pitch respectively.
 * @describe_simple_post: provide driver-private data needed to post a single
 *	buffer @buf.  Copy up to ADF_MAX_CUSTOM_DATA_SIZE bytes into @data
 *	(allocated by ADF) and return the number of bytes in @size.  Return 0 on
 *	success or error code (<0) on failure.
 *
 * @modeset: change the interface's mode.  @mode is not necessarily part of the
 *	modelist passed to adf_hotplug_notify_connected(); the driver may
 *	accept or reject custom modes at its discretion.  Return 0 on success or
 *	error code (<0) if the mode could not be set.
 *
 * @screen_size: copy the screen dimensions in millimeters into @width_mm
 *	and @height_mm.  Return 0 on success or error code (<0) if the display
 *	dimensions are unknown.
 *
 * @type_str: return a string representation of custom @intf->type
 *	(@intf->type >= @ADF_INTF_TYPE_DEVICE_CUSTOM).
 */
struct adf_interface_ops {
	const struct adf_obj_ops base;

	/* optional */
	int (*blank)(struct adf_interface *intf, u8 state);

	/* optional */
	int (*alloc_simple_buffer)(struct adf_interface *intf,
			u16 w, u16 h, u32 format,
			struct dma_buf **dma_buf, u32 *offset, u32 *pitch);
	/* optional */
	int (*describe_simple_post)(struct adf_interface *intf,
			struct adf_buffer *fb, void *data, size_t *size);

	/* optional */
	int (*modeset)(struct adf_interface *intf,
			struct drm_mode_modeinfo *mode);

	/* optional */
	int (*screen_size)(struct adf_interface *intf, u16 *width_mm,
			u16 *height_mm);

	/* optional */
	const char *(*type_str)(struct adf_interface *intf);
};

struct adf_interface {
	struct adf_obj base;
	const struct adf_interface_ops *ops;

	struct drm_mode_modeinfo current_mode;

	enum adf_interface_type type;
	u32 idx;
	u32 flags;

	wait_queue_head_t vsync_wait;
	ktime_t vsync_timestamp;
	rwlock_t vsync_lock;

	u8 dpms_state;

	bool hotplug_detect;
	struct drm_mode_modeinfo *modelist;
	size_t n_modes;
	rwlock_t hotplug_modelist_lock;
};

/**
 * struct adf_interface_ops - overlay engine implementation ops
 *
 * @base: common operations (see &struct adf_obj_ops)
 *
 * @supported_formats: list of fourccs the overlay engine can scan out
 * @n_supported_formats: length of supported_formats, up to
 *	ADF_MAX_SUPPORTED_FORMATS
 */
struct adf_overlay_engine_ops {
	const struct adf_obj_ops base;

	/* required */
	const u32 *supported_formats;
	/* required */
	const size_t n_supported_formats;
};

struct adf_overlay_engine {
	struct adf_obj base;

	const struct adf_overlay_engine_ops *ops;
};

#define adf_obj_to_device(ptr) \
	container_of((ptr), struct adf_device, base)

#define adf_obj_to_interface(ptr) \
	container_of((ptr), struct adf_interface, base)

#define adf_obj_to_overlay_engine(ptr) \
	container_of((ptr), struct adf_overlay_engine, base)

int __printf(4, 5) adf_device_init(struct adf_device *dev,
		struct device *parent, const struct adf_device_ops *ops,
		const char *fmt, ...);
void adf_device_destroy(struct adf_device *dev);
int __printf(7, 8) adf_interface_init(struct adf_interface *intf,
		struct adf_device *dev, enum adf_interface_type type, u32 idx,
		u32 flags, const struct adf_interface_ops *ops, const char *fmt,
		...);
void adf_interface_destroy(struct adf_interface *intf);
static inline struct adf_device *adf_interface_parent(
		struct adf_interface *intf)
{
	return intf->base.parent;
}
int __printf(4, 5) adf_overlay_engine_init(struct adf_overlay_engine *eng,
		struct adf_device *dev,
		const struct adf_overlay_engine_ops *ops, const char *fmt, ...);
void adf_overlay_engine_destroy(struct adf_overlay_engine *eng);
static inline struct adf_device *adf_overlay_engine_parent(
		struct adf_overlay_engine *eng)
{
	return eng->base.parent;
}

int adf_attachment_allow(struct adf_device *dev, struct adf_overlay_engine *eng,
		struct adf_interface *intf);

const char *adf_obj_type_str(enum adf_obj_type type);
const char *adf_interface_type_str(struct adf_interface *intf);
const char *adf_event_type_str(struct adf_obj *obj, enum adf_event_type type);

#define ADF_FORMAT_STR_SIZE 5
void adf_format_str(u32 format, char buf[ADF_FORMAT_STR_SIZE]);
int adf_format_validate_yuv(struct adf_device *dev, struct adf_buffer *buf,
		u8 num_planes, u8 hsub, u8 vsub, u8 cpp[]);
/**
 * adf_format_validate_rgb - validate the number and size of planes in buffers
 * with a custom RGB format.
 *
 * @dev: ADF device performing the validation
 * @buf: buffer to validate
 * @cpp: expected bytes per pixel
 *
 * adf_format_validate_rgb() is intended to be called as a helper from @dev's
 * validate_custom_format() op.  @buf must have a single RGB plane.
 *
 * Returns 0 if @buf has a single plane with sufficient size, or -EINVAL
 * otherwise.
 */
static inline int adf_format_validate_rgb(struct adf_device *dev,
		struct adf_buffer *buf, u8 cpp)
{
	return adf_format_validate_yuv(dev, buf, 1, 1, 1, &cpp);
}

int adf_event_get(struct adf_obj *obj, enum adf_event_type type);
int adf_event_put(struct adf_obj *obj, enum adf_event_type type);
int adf_event_notify(struct adf_obj *obj, struct adf_event *event);

static inline void adf_vsync_get(struct adf_interface *intf)
{
	adf_event_get(&intf->base, ADF_EVENT_VSYNC);
}

static inline void adf_vsync_put(struct adf_interface *intf)
{
	adf_event_put(&intf->base, ADF_EVENT_VSYNC);
}

int adf_vsync_wait(struct adf_interface *intf, long timeout);
void adf_vsync_notify(struct adf_interface *intf, ktime_t timestamp);

int adf_hotplug_notify_connected(struct adf_interface *intf,
		struct drm_mode_modeinfo *modelist, size_t n_modes);
void adf_hotplug_notify_disconnected(struct adf_interface *intf);

void adf_modeinfo_set_name(struct drm_mode_modeinfo *mode);
void adf_modeinfo_set_vrefresh(struct drm_mode_modeinfo *mode);

#endif /* _VIDEO_ADF_H */
