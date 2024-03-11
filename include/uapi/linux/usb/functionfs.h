/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__LINUX_FUNCTIONFS_H__
#define _UAPI__LINUX_FUNCTIONFS_H__


#include <linux/types.h>
#include <linux/ioctl.h>

#include <linux/usb/ch9.h>


enum {
	FUNCTIONFS_DESCRIPTORS_MAGIC = 1,
	FUNCTIONFS_STRINGS_MAGIC = 2,
	FUNCTIONFS_DESCRIPTORS_MAGIC_V2 = 3,
};

enum functionfs_flags {
	FUNCTIONFS_HAS_FS_DESC = 1,
	FUNCTIONFS_HAS_HS_DESC = 2,
	FUNCTIONFS_HAS_SS_DESC = 4,
	FUNCTIONFS_HAS_MS_OS_DESC = 8,
	FUNCTIONFS_VIRTUAL_ADDR = 16,
	FUNCTIONFS_EVENTFD = 32,
	FUNCTIONFS_ALL_CTRL_RECIP = 64,
	FUNCTIONFS_CONFIG0_SETUP = 128,
};

/* Descriptor of an non-audio endpoint */
struct usb_endpoint_descriptor_no_audio {
	__u8  bLength;
	__u8  bDescriptorType;

	__u8  bEndpointAddress;
	__u8  bmAttributes;
	__le16 wMaxPacketSize;
	__u8  bInterval;
} __attribute__((packed));

struct usb_functionfs_descs_head_v2 {
	__le32 magic;
	__le32 length;
	__le32 flags;
	/*
	 * __le32 fs_count, hs_count, fs_count; must be included manually in
	 * the structure taking flags into consideration.
	 */
} __attribute__((packed));

/* Legacy format, deprecated as of 3.14. */
struct usb_functionfs_descs_head {
	__le32 magic;
	__le32 length;
	__le32 fs_count;
	__le32 hs_count;
} __attribute__((packed, deprecated));

/* MS OS Descriptor header */
struct usb_os_desc_header {
	__u8	interface;
	__le32	dwLength;
	__le16	bcdVersion;
	__le16	wIndex;
	union {
		struct {
			__u8	bCount;
			__u8	Reserved;
		};
		__le16	wCount;
	};
} __attribute__((packed));

struct usb_ext_compat_desc {
	__u8	bFirstInterfaceNumber;
	__u8	Reserved1;
	__struct_group(/* no tag */, IDs, /* no attrs */,
		__u8	CompatibleID[8];
		__u8	SubCompatibleID[8];
	);
	__u8	Reserved2[6];
};

struct usb_ext_prop_desc {
	__le32	dwSize;
	__le32	dwPropertyDataType;
	__le16	wPropertyNameLength;
} __attribute__((packed));

#ifndef __KERNEL__

/*
 * Descriptors format:
 *
 * | off | name      | type         | description                          |
 * |-----+-----------+--------------+--------------------------------------|
 * |   0 | magic     | LE32         | FUNCTIONFS_DESCRIPTORS_MAGIC_V2      |
 * |   4 | length    | LE32         | length of the whole data chunk       |
 * |   8 | flags     | LE32         | combination of functionfs_flags      |
 * |     | eventfd   | LE32         | eventfd file descriptor              |
 * |     | fs_count  | LE32         | number of full-speed descriptors     |
 * |     | hs_count  | LE32         | number of high-speed descriptors     |
 * |     | ss_count  | LE32         | number of super-speed descriptors    |
 * |     | os_count  | LE32         | number of MS OS descriptors          |
 * |     | fs_descrs | Descriptor[] | list of full-speed descriptors       |
 * |     | hs_descrs | Descriptor[] | list of high-speed descriptors       |
 * |     | ss_descrs | Descriptor[] | list of super-speed descriptors      |
 * |     | os_descrs | OSDesc[]     | list of MS OS descriptors            |
 *
 * Depending on which flags are set, various fields may be missing in the
 * structure.  Any flags that are not recognised cause the whole block to be
 * rejected with -ENOSYS.
 *
 * Legacy descriptors format (deprecated as of 3.14):
 *
 * | off | name      | type         | description                          |
 * |-----+-----------+--------------+--------------------------------------|
 * |   0 | magic     | LE32         | FUNCTIONFS_DESCRIPTORS_MAGIC         |
 * |   4 | length    | LE32         | length of the whole data chunk       |
 * |   8 | fs_count  | LE32         | number of full-speed descriptors     |
 * |  12 | hs_count  | LE32         | number of high-speed descriptors     |
 * |  16 | fs_descrs | Descriptor[] | list of full-speed descriptors       |
 * |     | hs_descrs | Descriptor[] | list of high-speed descriptors       |
 *
 * All numbers must be in little endian order.
 *
 * Descriptor[] is an array of valid USB descriptors which have the following
 * format:
 *
 * | off | name            | type | description              |
 * |-----+-----------------+------+--------------------------|
 * |   0 | bLength         | U8   | length of the descriptor |
 * |   1 | bDescriptorType | U8   | descriptor type          |
 * |   2 | payload         |      | descriptor's payload     |
 *
 * OSDesc[] is an array of valid MS OS Feature Descriptors which have one of
 * the following formats:
 *
 * | off | name            | type | description              |
 * |-----+-----------------+------+--------------------------|
 * |   0 | inteface        | U8   | related interface number |
 * |   1 | dwLength        | U32  | length of the descriptor |
 * |   5 | bcdVersion      | U16  | currently supported: 1   |
 * |   7 | wIndex          | U16  | currently supported: 4   |
 * |   9 | bCount          | U8   | number of ext. compat.   |
 * |  10 | Reserved        | U8   | 0                        |
 * |  11 | ExtCompat[]     |      | list of ext. compat. d.  |
 *
 * | off | name            | type | description              |
 * |-----+-----------------+------+--------------------------|
 * |   0 | inteface        | U8   | related interface number |
 * |   1 | dwLength        | U32  | length of the descriptor |
 * |   5 | bcdVersion      | U16  | currently supported: 1   |
 * |   7 | wIndex          | U16  | currently supported: 5   |
 * |   9 | wCount          | U16  | number of ext. compat.   |
 * |  11 | ExtProp[]       |      | list of ext. prop. d.    |
 *
 * ExtCompat[] is an array of valid Extended Compatiblity descriptors
 * which have the following format:
 *
 * | off | name                  | type | description                         |
 * |-----+-----------------------+------+-------------------------------------|
 * |   0 | bFirstInterfaceNumber | U8   | index of the interface or of the 1st|
 * |     |                       |      | interface in an IAD group           |
 * |   1 | Reserved              | U8   | 1                                   |
 * |   2 | CompatibleID          | U8[8]| compatible ID string                |
 * |  10 | SubCompatibleID       | U8[8]| subcompatible ID string             |
 * |  18 | Reserved              | U8[6]| 0                                   |
 *
 * ExtProp[] is an array of valid Extended Properties descriptors
 * which have the following format:
 *
 * | off | name                  | type | description                         |
 * |-----+-----------------------+------+-------------------------------------|
 * |   0 | dwSize                | U32  | length of the descriptor            |
 * |   4 | dwPropertyDataType    | U32  | 1..7                                |
 * |   8 | wPropertyNameLength   | U16  | bPropertyName length (NL)           |
 * |  10 | bPropertyName         |U8[NL]| name of this property               |
 * |10+NL| dwPropertyDataLength  | U32  | bPropertyData length (DL)           |
 * |14+NL| bProperty             |U8[DL]| payload of this property            |
 */

struct usb_functionfs_strings_head {
	__le32 magic;
	__le32 length;
	__le32 str_count;
	__le32 lang_count;
} __attribute__((packed));

/*
 * Strings format:
 *
 * | off | name       | type                  | description                |
 * |-----+------------+-----------------------+----------------------------|
 * |   0 | magic      | LE32                  | FUNCTIONFS_STRINGS_MAGIC   |
 * |   4 | length     | LE32                  | length of the data chunk   |
 * |   8 | str_count  | LE32                  | number of strings          |
 * |  12 | lang_count | LE32                  | number of languages        |
 * |  16 | stringtab  | StringTab[lang_count] | table of strings per lang  |
 *
 * For each language there is one stringtab entry (ie. there are lang_count
 * stringtab entires).  Each StringTab has following format:
 *
 * | off | name    | type              | description                        |
 * |-----+---------+-------------------+------------------------------------|
 * |   0 | lang    | LE16              | language code                      |
 * |   2 | strings | String[str_count] | array of strings in given language |
 *
 * For each string there is one strings entry (ie. there are str_count
 * string entries).  Each String is a NUL terminated string encoded in
 * UTF-8.
 */

#endif


/*
 * Events are delivered on the ep0 file descriptor, when the user mode driver
 * reads from this file descriptor after writing the descriptors.  Don't
 * stop polling this descriptor.
 */

enum usb_functionfs_event_type {
	FUNCTIONFS_BIND,
	FUNCTIONFS_UNBIND,

	FUNCTIONFS_ENABLE,
	FUNCTIONFS_DISABLE,

	FUNCTIONFS_SETUP,

	FUNCTIONFS_SUSPEND,
	FUNCTIONFS_RESUME
};

/* NOTE:  this structure must stay the same size and layout on
 * both 32-bit and 64-bit kernels.
 */
struct usb_functionfs_event {
	union {
		/* SETUP: packet; DATA phase i/o precedes next event
		 *(setup.bmRequestType & USB_DIR_IN) flags direction */
		struct usb_ctrlrequest	setup;
	} __attribute__((packed)) u;

	/* enum usb_functionfs_event_type */
	__u8				type;
	__u8				_pad[3];
} __attribute__((packed));


/* Endpoint ioctls */
/* The same as in gadgetfs */

/* IN transfers may be reported to the gadget driver as complete
 *	when the fifo is loaded, before the host reads the data;
 * OUT transfers may be reported to the host's "client" driver as
 *	complete when they're sitting in the FIFO unread.
 * THIS returns how many bytes are "unclaimed" in the endpoint fifo
 * (needed for precise fault handling, when the hardware allows it)
 */
#define	FUNCTIONFS_FIFO_STATUS	_IO('g', 1)

/* discards any unclaimed data in the fifo. */
#define	FUNCTIONFS_FIFO_FLUSH	_IO('g', 2)

/* resets endpoint halt+toggle; used to implement set_interface.
 * some hardware (like pxa2xx) can't support this.
 */
#define	FUNCTIONFS_CLEAR_HALT	_IO('g', 3)

/* Specific for functionfs */

/*
 * Returns reverse mapping of an interface.  Called on EP0.  If there
 * is no such interface returns -EDOM.  If function is not active
 * returns -ENODEV.
 */
#define	FUNCTIONFS_INTERFACE_REVMAP	_IO('g', 128)

/*
 * Returns real bEndpointAddress of an endpoint. If endpoint shuts down
 * during the call, returns -ESHUTDOWN.
 */
#define	FUNCTIONFS_ENDPOINT_REVMAP	_IO('g', 129)

/*
 * Returns endpoint descriptor. If endpoint shuts down during the call,
 * returns -ESHUTDOWN.
 */
#define	FUNCTIONFS_ENDPOINT_DESC	_IOR('g', 130, \
					     struct usb_endpoint_descriptor)



#endif /* _UAPI__LINUX_FUNCTIONFS_H__ */
