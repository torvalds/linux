/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Greybus manifest definition
 *
 * See "Greybus Application Protocol" document (version 0.1) for
 * details on these values and structures.
 *
 * Copyright 2014-2015 Google Inc.
 * Copyright 2014-2015 Linaro Ltd.
 *
 * Released under the GPLv2 and BSD licenses.
 */

#ifndef __GREYBUS_MANIFEST_H
#define __GREYBUS_MANIFEST_H

#include <linux/bits.h>
#include <linux/types.h>

enum greybus_descriptor_type {
	GREYBUS_TYPE_INVALID		= 0x00,
	GREYBUS_TYPE_INTERFACE		= 0x01,
	GREYBUS_TYPE_STRING		= 0x02,
	GREYBUS_TYPE_BUNDLE		= 0x03,
	GREYBUS_TYPE_CPORT		= 0x04,
};

enum greybus_protocol {
	GREYBUS_PROTOCOL_CONTROL	= 0x00,
	/* 0x01 is unused */
	GREYBUS_PROTOCOL_GPIO		= 0x02,
	GREYBUS_PROTOCOL_I2C		= 0x03,
	GREYBUS_PROTOCOL_UART		= 0x04,
	GREYBUS_PROTOCOL_HID		= 0x05,
	GREYBUS_PROTOCOL_USB		= 0x06,
	GREYBUS_PROTOCOL_SDIO		= 0x07,
	GREYBUS_PROTOCOL_POWER_SUPPLY	= 0x08,
	GREYBUS_PROTOCOL_PWM		= 0x09,
	/* 0x0a is unused */
	GREYBUS_PROTOCOL_SPI		= 0x0b,
	GREYBUS_PROTOCOL_DISPLAY	= 0x0c,
	GREYBUS_PROTOCOL_CAMERA_MGMT	= 0x0d,
	GREYBUS_PROTOCOL_SENSOR		= 0x0e,
	GREYBUS_PROTOCOL_LIGHTS		= 0x0f,
	GREYBUS_PROTOCOL_VIBRATOR	= 0x10,
	GREYBUS_PROTOCOL_LOOPBACK	= 0x11,
	GREYBUS_PROTOCOL_AUDIO_MGMT	= 0x12,
	GREYBUS_PROTOCOL_AUDIO_DATA	= 0x13,
	GREYBUS_PROTOCOL_SVC            = 0x14,
	GREYBUS_PROTOCOL_BOOTROM	= 0x15,
	GREYBUS_PROTOCOL_CAMERA_DATA	= 0x16,
	GREYBUS_PROTOCOL_FW_DOWNLOAD	= 0x17,
	GREYBUS_PROTOCOL_FW_MANAGEMENT	= 0x18,
	GREYBUS_PROTOCOL_AUTHENTICATION	= 0x19,
	GREYBUS_PROTOCOL_LOG		= 0x1a,
		/* ... */
	GREYBUS_PROTOCOL_RAW		= 0xfe,
	GREYBUS_PROTOCOL_VENDOR		= 0xff,
};

enum greybus_class_type {
	GREYBUS_CLASS_CONTROL		= 0x00,
	/* 0x01 is unused */
	/* 0x02 is unused */
	/* 0x03 is unused */
	/* 0x04 is unused */
	GREYBUS_CLASS_HID		= 0x05,
	/* 0x06 is unused */
	/* 0x07 is unused */
	GREYBUS_CLASS_POWER_SUPPLY	= 0x08,
	/* 0x09 is unused */
	GREYBUS_CLASS_BRIDGED_PHY	= 0x0a,
	/* 0x0b is unused */
	GREYBUS_CLASS_DISPLAY		= 0x0c,
	GREYBUS_CLASS_CAMERA		= 0x0d,
	GREYBUS_CLASS_SENSOR		= 0x0e,
	GREYBUS_CLASS_LIGHTS		= 0x0f,
	GREYBUS_CLASS_VIBRATOR		= 0x10,
	GREYBUS_CLASS_LOOPBACK		= 0x11,
	GREYBUS_CLASS_AUDIO		= 0x12,
	/* 0x13 is unused */
	/* 0x14 is unused */
	GREYBUS_CLASS_BOOTROM		= 0x15,
	GREYBUS_CLASS_FW_MANAGEMENT	= 0x16,
	GREYBUS_CLASS_LOG		= 0x17,
		/* ... */
	GREYBUS_CLASS_RAW		= 0xfe,
	GREYBUS_CLASS_VENDOR		= 0xff,
};

enum {
	GREYBUS_INTERFACE_FEATURE_TIMESYNC = BIT(0),
};

/*
 * The string in a string descriptor is not NUL-terminated.  The
 * size of the descriptor will be rounded up to a multiple of 4
 * bytes, by padding the string with 0x00 bytes if necessary.
 */
struct greybus_descriptor_string {
	__u8	length;
	__u8	id;
	__u8	string[0];
} __packed;

/*
 * An interface descriptor describes information about an interface as a whole,
 * *not* the functions within it.
 */
struct greybus_descriptor_interface {
	__u8	vendor_stringid;
	__u8	product_stringid;
	__u8	features;
	__u8	pad;
} __packed;

/*
 * An bundle descriptor defines an identification number and a class for
 * each bundle.
 *
 * @id: Uniquely identifies a bundle within a interface, its sole purpose is to
 * allow CPort descriptors to specify which bundle they are associated with.
 * The first bundle will have id 0, second will have 1 and so on.
 *
 * The largest CPort id associated with an bundle (defined by a
 * CPort descriptor in the manifest) is used to determine how to
 * encode the device id and module number in UniPro packets
 * that use the bundle.
 *
 * @class: It is used by kernel to know the functionality provided by the
 * bundle and will be matched against drivers functinality while probing greybus
 * driver. It should contain one of the values defined in
 * 'enum greybus_class_type'.
 *
 */
struct greybus_descriptor_bundle {
	__u8	id;	/* interface-relative id (0..) */
	__u8	class;
	__u8	pad[2];
} __packed;

/*
 * A CPort descriptor indicates the id of the bundle within the
 * module it's associated with, along with the CPort id used to
 * address the CPort.  The protocol id defines the format of messages
 * exchanged using the CPort.
 */
struct greybus_descriptor_cport {
	__le16	id;
	__u8	bundle;
	__u8	protocol_id;	/* enum greybus_protocol */
} __packed;

struct greybus_descriptor_header {
	__le16	size;
	__u8	type;		/* enum greybus_descriptor_type */
	__u8	pad;
} __packed;

struct greybus_descriptor {
	struct greybus_descriptor_header		header;
	union {
		struct greybus_descriptor_string	string;
		struct greybus_descriptor_interface	interface;
		struct greybus_descriptor_bundle	bundle;
		struct greybus_descriptor_cport		cport;
	};
} __packed;

struct greybus_manifest_header {
	__le16	size;
	__u8	version_major;
	__u8	version_minor;
} __packed;

struct greybus_manifest {
	struct greybus_manifest_header		header;
	struct greybus_descriptor		descriptors[0];
} __packed;

#endif /* __GREYBUS_MANIFEST_H */
