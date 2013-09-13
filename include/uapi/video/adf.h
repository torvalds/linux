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

#ifndef _UAPI_VIDEO_ADF_H_
#define _UAPI_VIDEO_ADF_H_

#include <linux/ioctl.h>
#include <linux/types.h>

#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>

#define ADF_NAME_LEN 32
#define ADF_MAX_CUSTOM_DATA_SIZE PAGE_SIZE

enum adf_interface_type {
	ADF_INTF_DSI = 0,
	ADF_INTF_eDP = 1,
	ADF_INTF_DPI = 2,
	ADF_INTF_VGA = 3,
	ADF_INTF_DVI = 4,
	ADF_INTF_HDMI = 5,
	ADF_INTF_MEMORY = 6,
	ADF_INTF_TYPE_DEVICE_CUSTOM = 128,
	ADF_INTF_TYPE_MAX = (~(__u32)0),
};

#define ADF_INTF_FLAG_PRIMARY (1 << 0)
#define ADF_INTF_FLAG_EXTERNAL (1 << 1)

enum adf_event_type {
	ADF_EVENT_VSYNC = 0,
	ADF_EVENT_HOTPLUG = 1,
	ADF_EVENT_DEVICE_CUSTOM = 128,
	ADF_EVENT_TYPE_MAX = 255,
};

/**
 * struct adf_set_event - start or stop subscribing to ADF events
 *
 * @type: the type of event to (un)subscribe
 * @enabled: subscribe or unsubscribe
 *
 * After subscribing to an event, userspace may poll() the ADF object's fd
 * to wait for events or read() to consume the event's data.
 *
 * ADF reserves event types 0 to %ADF_EVENT_DEVICE_CUSTOM-1 for its own events.
 * Devices may use event types %ADF_EVENT_DEVICE_CUSTOM to %ADF_EVENT_TYPE_MAX-1
 * for driver-private events.
 */
struct adf_set_event {
	__u8 type;
	__u8 enabled;
};

/**
 * struct adf_event - common header for ADF event data
 *
 * @type: event type
 * @length: total size of event data, header inclusive
 */
struct adf_event {
	__u8 type;
	__u32 length;
};

/**
 * struct adf_vsync_event - ADF vsync event
 *
 * @base: event header (see &struct adf_event)
 * @timestamp: time of vsync event, in nanoseconds
 */
struct adf_vsync_event {
	struct adf_event base;
	__u64 timestamp;
};

/**
 * struct adf_vsync_event - ADF display hotplug event
 *
 * @base: event header (see &struct adf_event)
 * @connected: whether a display is now connected to the interface
 */
struct adf_hotplug_event {
	struct adf_event base;
	__u8 connected;
};

#define ADF_MAX_PLANES 4
/**
 * struct adf_buffer_config - description of buffer displayed by adf_post_config
 *
 * @overlay_engine: id of the target overlay engine
 * @w: width of display region in pixels
 * @h: height of display region in pixels
 * @format: DRM-style fourcc, see drm_fourcc.h for standard formats
 * @fd: dma_buf fd for each plane
 * @offset: location of first pixel to scan out, in bytes
 * @pitch: stride (i.e. length of a scanline including padding) in bytes
 * @n_planes: number of planes in buffer
 * @acquire_fence: sync_fence fd which will clear when the buffer is
 *	ready for display, or <0 if the buffer is already ready
 */
struct adf_buffer_config {
	__u32 overlay_engine;

	__u32 w;
	__u32 h;
	__u32 format;

	__s64 fd[ADF_MAX_PLANES];
	__u32 offset[ADF_MAX_PLANES];
	__u32 pitch[ADF_MAX_PLANES];
	__u8 n_planes;

	__s64 acquire_fence;
};
#define ADF_MAX_BUFFERS (PAGE_SIZE / sizeof(struct adf_buffer_config))

/**
 * struct adf_post_config - request to flip to a new set of buffers
 *
 * @n_interfaces: number of interfaces targeted by the flip (input)
 * @interfaces: ids of interfaces targeted by the flip (input)
 * @n_bufs: number of buffers displayed (input)
 * @bufs: description of buffers displayed (input)
 * @custom_data_size: size of driver-private data (input)
 * @custom_data: driver-private data (input)
 * @complete_fence: sync_fence fd which will clear when this
 *	configuration has left the screen (output)
 */
struct adf_post_config {
	size_t n_interfaces;
	__u32 __user *interfaces;

	size_t n_bufs;
	struct adf_buffer_config __user *bufs;

	size_t custom_data_size;
	void __user *custom_data;

	__s64 complete_fence;
};
#define ADF_MAX_INTERFACES (PAGE_SIZE / sizeof(__u32))

/**
 * struct adf_simple_buffer_allocate - request to allocate a "simple" buffer
 *
 * @w: width of buffer in pixels (input)
 * @h: height of buffer in pixels (input)
 * @format: DRM-style fourcc (input)
 *
 * @fd: dma_buf fd (output)
 * @offset: location of first pixel, in bytes (output)
 * @pitch: length of a scanline including padding, in bytes (output)
 *
 * Simple buffers are analogous to DRM's "dumb" buffers.  They have a single
 * plane of linear RGB data which can be allocated and scanned out without
 * any driver-private ioctls or data.
 *
 * @format must be a standard RGB format defined in drm_fourcc.h.
 *
 * ADF clients must NOT assume that an interface can scan out a simple buffer
 * allocated by a different ADF interface, even if the two interfaces belong to
 * the same ADF device.
 */
struct adf_simple_buffer_alloc {
	__u16 w;
	__u16 h;
	__u32 format;

	__s64 fd;
	__u32 offset;
	__u32 pitch;
};

/**
 * struct adf_simple_post_config - request to flip to a single buffer without
 * driver-private data
 *
 * @buf: description of buffer displayed (input)
 * @complete_fence: sync_fence fd which will clear when this buffer has left the
 * screen (output)
 */
struct adf_simple_post_config {
	struct adf_buffer_config buf;
	__s64 complete_fence;
};

/**
 * struct adf_attachment_config - description of attachment between an overlay
 * engine and an interface
 *
 * @overlay_engine: id of the overlay engine
 * @interface: id of the interface
 */
struct adf_attachment_config {
	__u32 overlay_engine;
	__u32 interface;
};

/**
 * struct adf_device_data - describes a display device
 *
 * @name: display device's name
 * @n_attachments: the number of current attachments
 * @attachments: list of current attachments
 * @n_allowed_attachments: the number of allowed attachments
 * @allowed_attachments: list of allowed attachments
 * @custom_data_size: size of driver-private data
 * @custom_data: driver-private data
 */
struct adf_device_data {
	char name[ADF_NAME_LEN];

	size_t n_attachments;
	struct adf_attachment_config __user *attachments;

	size_t n_allowed_attachments;
	struct adf_attachment_config __user *allowed_attachments;

	size_t custom_data_size;
	void __user *custom_data;
};
#define ADF_MAX_ATTACHMENTS (PAGE_SIZE / sizeof(struct adf_attachment))

/**
 * struct adf_device_data - describes a display interface
 *
 * @name: display interface's name
 * @type: interface type (see enum @adf_interface_type)
 * @id: which interface of type @type;
 *	e.g. interface DSI.1 -> @type=@ADF_INTF_TYPE_DSI, @id=1
 * @flags: informational flags (bitmask of %ADF_INTF_FLAG_* values)
 * @dpms_state: DPMS state (one of @DRM_MODE_DPMS_* defined in drm_mode.h)
 * @hotplug_detect: whether a display is plugged in
 * @width_mm: screen width in millimeters, or 0 if unknown
 * @height_mm: screen height in millimeters, or 0 if unknown
 * @current_mode: current display mode
 * @n_available_modes: the number of hardware display modes
 * @available_modes: list of hardware display modes
 * @custom_data_size: size of driver-private data
 * @custom_data: driver-private data
 */
struct adf_interface_data {
	char name[ADF_NAME_LEN];

	__u32 type;
	__u32 id;
	/* e.g. type=ADF_INTF_TYPE_DSI, id=1 => DSI.1 */
	__u32 flags;

	__u8 dpms_state;
	__u8 hotplug_detect;
	__u16 width_mm;
	__u16 height_mm;

	struct drm_mode_modeinfo current_mode;
	size_t n_available_modes;
	struct drm_mode_modeinfo __user *available_modes;

	size_t custom_data_size;
	void __user *custom_data;
};
#define ADF_MAX_MODES (PAGE_SIZE / sizeof(struct drm_mode_modeinfo))

/**
 * struct adf_overlay_engine_data - describes an overlay engine
 *
 * @name: overlay engine's name
 * @n_supported_formats: number of supported formats
 * @supported_formats: list of supported formats
 * @custom_data_size: size of driver-private data
 * @custom_data: driver-private data
 */
struct adf_overlay_engine_data {
	char name[ADF_NAME_LEN];

	size_t n_supported_formats;
	__u32 __user *supported_formats;

	size_t custom_data_size;
	void __user *custom_data;
};
#define ADF_MAX_SUPPORTED_FORMATS (PAGE_SIZE / sizeof(__u32))

#define ADF_SET_EVENT		_IOW('D', 0, struct adf_set_event)
#define ADF_BLANK		_IOW('D', 1, __u8)
#define ADF_POST_CONFIG		_IOW('D', 2, struct adf_post_config)
#define ADF_SET_MODE		_IOW('D', 3, struct drm_mode_modeinfo)
#define ADF_GET_DEVICE_DATA	_IOR('D', 4, struct adf_device_data)
#define ADF_GET_INTERFACE_DATA	_IOR('D', 5, struct adf_interface_data)
#define ADF_GET_OVERLAY_ENGINE_DATA \
				_IOR('D', 6, struct adf_overlay_engine_data)
#define ADF_SIMPLE_POST_CONFIG	_IOW('D', 7, struct adf_simple_post_config)
#define ADF_SIMPLE_BUFFER_ALLOC	_IOW('D', 8, struct adf_simple_buffer_alloc)
#define ADF_ATTACH		_IOW('D', 9, struct adf_attachment_config)
#define ADF_DETACH		_IOW('D', 10, struct adf_attachment_config)

#endif /* _UAPI_VIDEO_ADF_H_ */
