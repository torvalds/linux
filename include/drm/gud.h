/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2020 Noralf Trønnes
 */

#ifndef __LINUX_GUD_H
#define __LINUX_GUD_H

#include <linux/types.h>

/*
 * struct gud_display_descriptor_req - Display descriptor
 * @magic: Magic value GUD_DISPLAY_MAGIC
 * @version: Protocol version
 * @flags: Flags
 *         - STATUS_ON_SET: Always do a status request after a SET request.
 *                          This is used by the Linux gadget driver since it has
 *                          no way to control the status stage of a control OUT
 *                          request that has a payload.
 *         - FULL_UPDATE:   Always send the entire framebuffer when flushing changes.
 *                          The GUD_REQ_SET_BUFFER request will not be sent
 *                          before each bulk transfer, it will only be sent if the
 *                          previous bulk transfer had failed. This gives the device
 *                          a chance to reset its state machine if needed.
 *                          This flag can not be used in combination with compression.
 * @compression: Supported compression types
 *               - GUD_COMPRESSION_LZ4: LZ4 lossless compression.
 * @max_buffer_size: Maximum buffer size the device can handle (optional).
 *                   This is useful for devices that don't have a big enough
 *                   buffer to decompress the entire framebuffer in one go.
 * @min_width: Minimum pixel width the controller can handle
 * @max_width: Maximum width
 * @min_height: Minimum height
 * @max_height: Maximum height
 *
 * Devices that have only one display mode will have min_width == max_width
 * and min_height == max_height.
 */
struct gud_display_descriptor_req {
	__le32 magic;
#define GUD_DISPLAY_MAGIC			0x1d50614d
	__u8 version;
	__le32 flags;
#define GUD_DISPLAY_FLAG_STATUS_ON_SET		BIT(0)
#define GUD_DISPLAY_FLAG_FULL_UPDATE		BIT(1)
	__u8 compression;
#define GUD_COMPRESSION_LZ4			BIT(0)
	__le32 max_buffer_size;
	__le32 min_width;
	__le32 max_width;
	__le32 min_height;
	__le32 max_height;
} __packed;

/*
 * struct gud_property_req - Property
 * @prop: Property
 * @val: Value
 */
struct gud_property_req {
	__le16 prop;
	__le64 val;
} __packed;

/*
 * struct gud_display_mode_req - Display mode
 * @clock: Pixel clock in kHz
 * @hdisplay: Horizontal display size
 * @hsync_start: Horizontal sync start
 * @hsync_end: Horizontal sync end
 * @htotal: Horizontal total size
 * @vdisplay: Vertical display size
 * @vsync_start: Vertical sync start
 * @vsync_end: Vertical sync end
 * @vtotal: Vertical total size
 * @flags: Bits 0-13 are the same as in the RandR protocol and also what DRM uses.
 *         The deprecated bits are reused for internal protocol flags leaving us
 *         free to follow DRM for the other bits in the future.
 *         - FLAG_PREFERRED: Set on the preferred display mode.
 */
struct gud_display_mode_req {
	__le32 clock;
	__le16 hdisplay;
	__le16 hsync_start;
	__le16 hsync_end;
	__le16 htotal;
	__le16 vdisplay;
	__le16 vsync_start;
	__le16 vsync_end;
	__le16 vtotal;
	__le32 flags;
#define GUD_DISPLAY_MODE_FLAG_PHSYNC		BIT(0)
#define GUD_DISPLAY_MODE_FLAG_NHSYNC		BIT(1)
#define GUD_DISPLAY_MODE_FLAG_PVSYNC		BIT(2)
#define GUD_DISPLAY_MODE_FLAG_NVSYNC		BIT(3)
#define GUD_DISPLAY_MODE_FLAG_INTERLACE		BIT(4)
#define GUD_DISPLAY_MODE_FLAG_DBLSCAN		BIT(5)
#define GUD_DISPLAY_MODE_FLAG_CSYNC		BIT(6)
#define GUD_DISPLAY_MODE_FLAG_PCSYNC		BIT(7)
#define GUD_DISPLAY_MODE_FLAG_NCSYNC		BIT(8)
#define GUD_DISPLAY_MODE_FLAG_HSKEW		BIT(9)
/* BCast and PixelMultiplex are deprecated */
#define GUD_DISPLAY_MODE_FLAG_DBLCLK		BIT(12)
#define GUD_DISPLAY_MODE_FLAG_CLKDIV2		BIT(13)
#define GUD_DISPLAY_MODE_FLAG_USER_MASK		\
		(GUD_DISPLAY_MODE_FLAG_PHSYNC | GUD_DISPLAY_MODE_FLAG_NHSYNC | \
		GUD_DISPLAY_MODE_FLAG_PVSYNC | GUD_DISPLAY_MODE_FLAG_NVSYNC | \
		GUD_DISPLAY_MODE_FLAG_INTERLACE | GUD_DISPLAY_MODE_FLAG_DBLSCAN | \
		GUD_DISPLAY_MODE_FLAG_CSYNC | GUD_DISPLAY_MODE_FLAG_PCSYNC | \
		GUD_DISPLAY_MODE_FLAG_NCSYNC | GUD_DISPLAY_MODE_FLAG_HSKEW | \
		GUD_DISPLAY_MODE_FLAG_DBLCLK | GUD_DISPLAY_MODE_FLAG_CLKDIV2)
/* Internal protocol flags */
#define GUD_DISPLAY_MODE_FLAG_PREFERRED		BIT(10)
} __packed;

/*
 * struct gud_connector_descriptor_req - Connector descriptor
 * @connector_type: Connector type (GUD_CONNECTOR_TYPE_*).
 *                  If the host doesn't support the type it should fall back to PANEL.
 * @flags: Flags
 *         - POLL_STATUS: Connector status can change (polled every 10 seconds)
 *         - INTERLACE: Interlaced modes are supported
 *         - DOUBLESCAN: Doublescan modes are supported
 */
struct gud_connector_descriptor_req {
	__u8 connector_type;
#define GUD_CONNECTOR_TYPE_PANEL		0
#define GUD_CONNECTOR_TYPE_VGA			1
#define GUD_CONNECTOR_TYPE_COMPOSITE		2
#define GUD_CONNECTOR_TYPE_SVIDEO		3
#define GUD_CONNECTOR_TYPE_COMPONENT		4
#define GUD_CONNECTOR_TYPE_DVI			5
#define GUD_CONNECTOR_TYPE_DISPLAYPORT		6
#define GUD_CONNECTOR_TYPE_HDMI			7
	__le32 flags;
#define GUD_CONNECTOR_FLAGS_POLL_STATUS		BIT(0)
#define GUD_CONNECTOR_FLAGS_INTERLACE		BIT(1)
#define GUD_CONNECTOR_FLAGS_DOUBLESCAN		BIT(2)
} __packed;

/*
 * struct gud_set_buffer_req - Set buffer transfer info
 * @x: X position of rectangle
 * @y: Y position
 * @width: Pixel width of rectangle
 * @height: Pixel height
 * @length: Buffer length in bytes
 * @compression: Transfer compression
 * @compressed_length: Compressed buffer length
 *
 * This request is issued right before the bulk transfer.
 * @x, @y, @width and @height specifies the rectangle where the buffer should be
 * placed inside the framebuffer.
 */
struct gud_set_buffer_req {
	__le32 x;
	__le32 y;
	__le32 width;
	__le32 height;
	__le32 length;
	__u8 compression;
	__le32 compressed_length;
} __packed;

/*
 * struct gud_state_req - Display state
 * @mode: Display mode
 * @format: Pixel format GUD_PIXEL_FORMAT_*
 * @connector: Connector index
 * @properties: Array of properties
 *
 * The entire state is transferred each time there's a change.
 */
struct gud_state_req {
	struct gud_display_mode_req mode;
	__u8 format;
	__u8 connector;
	struct gud_property_req properties[];
} __packed;

/* List of supported connector properties: */

/* Margins in pixels to deal with overscan, range 0-100 */
#define GUD_PROPERTY_TV_LEFT_MARGIN			1
#define GUD_PROPERTY_TV_RIGHT_MARGIN			2
#define GUD_PROPERTY_TV_TOP_MARGIN			3
#define GUD_PROPERTY_TV_BOTTOM_MARGIN			4
#define GUD_PROPERTY_TV_MODE				5
/* Brightness in percent, range 0-100 */
#define GUD_PROPERTY_TV_BRIGHTNESS			6
/* Contrast in percent, range 0-100 */
#define GUD_PROPERTY_TV_CONTRAST			7
/* Flicker reduction in percent, range 0-100 */
#define GUD_PROPERTY_TV_FLICKER_REDUCTION		8
/* Overscan in percent, range 0-100 */
#define GUD_PROPERTY_TV_OVERSCAN			9
/* Saturation in percent, range 0-100 */
#define GUD_PROPERTY_TV_SATURATION			10
/* Hue in percent, range 0-100 */
#define GUD_PROPERTY_TV_HUE				11

/*
 * Backlight brightness is in the range 0-100 inclusive. The value represents the human perceptual
 * brightness and not a linear PWM value. 0 is minimum brightness which should not turn the
 * backlight completely off. The DPMS connector property should be used to control power which will
 * trigger a GUD_REQ_SET_DISPLAY_ENABLE request.
 *
 * This does not map to a DRM property, it is used with the backlight device.
 */
#define GUD_PROPERTY_BACKLIGHT_BRIGHTNESS		12

/* List of supported properties that are not connector propeties: */

/*
 * Plane rotation. Should return the supported bitmask on
 * GUD_REQ_GET_PROPERTIES. GUD_ROTATION_0 is mandatory.
 *
 * Note: This is not display rotation so 90/270 will need scaling to make it fit (unless squared).
 */
#define GUD_PROPERTY_ROTATION				50
  #define GUD_ROTATION_0			BIT(0)
  #define GUD_ROTATION_90			BIT(1)
  #define GUD_ROTATION_180			BIT(2)
  #define GUD_ROTATION_270			BIT(3)
  #define GUD_ROTATION_REFLECT_X		BIT(4)
  #define GUD_ROTATION_REFLECT_Y		BIT(5)
  #define GUD_ROTATION_MASK			(GUD_ROTATION_0 | GUD_ROTATION_90 | \
						GUD_ROTATION_180 | GUD_ROTATION_270 | \
						GUD_ROTATION_REFLECT_X | GUD_ROTATION_REFLECT_Y)

/* USB Control requests: */

/* Get status from the last GET/SET control request. Value is u8. */
#define GUD_REQ_GET_STATUS				0x00
  /* Status values: */
  #define GUD_STATUS_OK				0x00
  #define GUD_STATUS_BUSY			0x01
  #define GUD_STATUS_REQUEST_NOT_SUPPORTED	0x02
  #define GUD_STATUS_PROTOCOL_ERROR		0x03
  #define GUD_STATUS_INVALID_PARAMETER		0x04
  #define GUD_STATUS_ERROR			0x05

/* Get display descriptor as a &gud_display_descriptor_req */
#define GUD_REQ_GET_DESCRIPTOR				0x01

/* Get supported pixel formats as a byte array of GUD_PIXEL_FORMAT_* */
#define GUD_REQ_GET_FORMATS				0x40
  #define GUD_FORMATS_MAX_NUM			32
  #define GUD_PIXEL_FORMAT_R1			0x01 /* 1-bit monochrome */
  #define GUD_PIXEL_FORMAT_R8			0x08 /* 8-bit greyscale */
  #define GUD_PIXEL_FORMAT_XRGB1111		0x20
  #define GUD_PIXEL_FORMAT_RGB332		0x30
  #define GUD_PIXEL_FORMAT_RGB565		0x40
  #define GUD_PIXEL_FORMAT_RGB888		0x50
  #define GUD_PIXEL_FORMAT_XRGB8888		0x80
  #define GUD_PIXEL_FORMAT_ARGB8888		0x81

/*
 * Get supported properties that are not connector propeties as a &gud_property_req array.
 * gud_property_req.val often contains the initial value for the property.
 */
#define GUD_REQ_GET_PROPERTIES				0x41
  #define GUD_PROPERTIES_MAX_NUM		32

/* Connector requests have the connector index passed in the wValue field */

/* Get connector descriptors as an array of &gud_connector_descriptor_req */
#define GUD_REQ_GET_CONNECTORS				0x50
  #define GUD_CONNECTORS_MAX_NUM		32

/*
 * Get properties supported by the connector as a &gud_property_req array.
 * gud_property_req.val often contains the initial value for the property.
 */
#define GUD_REQ_GET_CONNECTOR_PROPERTIES		0x51
  #define GUD_CONNECTOR_PROPERTIES_MAX_NUM	32

/*
 * Issued when there's a TV_MODE property present.
 * Gets an array of the supported TV_MODE names each entry of length
 * GUD_CONNECTOR_TV_MODE_NAME_LEN. Names must be NUL-terminated.
 */
#define GUD_REQ_GET_CONNECTOR_TV_MODE_VALUES		0x52
  #define GUD_CONNECTOR_TV_MODE_NAME_LEN	16
  #define GUD_CONNECTOR_TV_MODE_MAX_NUM		16

/* When userspace checks connector status, this is issued first, not used for poll requests. */
#define GUD_REQ_SET_CONNECTOR_FORCE_DETECT		0x53

/*
 * Get connector status. Value is u8.
 *
 * Userspace will get a HOTPLUG uevent if one of the following is true:
 * - Connection status has changed since last
 * - CHANGED is set
 */
#define GUD_REQ_GET_CONNECTOR_STATUS			0x54
  #define GUD_CONNECTOR_STATUS_DISCONNECTED	0x00
  #define GUD_CONNECTOR_STATUS_CONNECTED	0x01
  #define GUD_CONNECTOR_STATUS_UNKNOWN		0x02
  #define GUD_CONNECTOR_STATUS_CONNECTED_MASK	0x03
  #define GUD_CONNECTOR_STATUS_CHANGED		BIT(7)

/*
 * Display modes can be fetched as either EDID data or an array of &gud_display_mode_req.
 *
 * If GUD_REQ_GET_CONNECTOR_MODES returns zero, EDID is used to create display modes.
 * If both display modes and EDID are returned, EDID is just passed on to userspace
 * in the EDID connector property.
 */

/* Get &gud_display_mode_req array of supported display modes */
#define GUD_REQ_GET_CONNECTOR_MODES			0x55
  #define GUD_CONNECTOR_MAX_NUM_MODES		128

/* Get Extended Display Identification Data */
#define GUD_REQ_GET_CONNECTOR_EDID			0x56
  #define GUD_CONNECTOR_MAX_EDID_LEN		2048

/* Set buffer properties before bulk transfer as &gud_set_buffer_req */
#define GUD_REQ_SET_BUFFER				0x60

/* Check display configuration as &gud_state_req */
#define GUD_REQ_SET_STATE_CHECK				0x61

/* Apply the previous STATE_CHECK configuration */
#define GUD_REQ_SET_STATE_COMMIT			0x62

/* Enable/disable the display controller, value is u8: 0/1 */
#define GUD_REQ_SET_CONTROLLER_ENABLE			0x63

/* Enable/disable display/output (DPMS), value is u8: 0/1 */
#define GUD_REQ_SET_DISPLAY_ENABLE			0x64

#endif
