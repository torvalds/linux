#ifndef _UAPI__LINUX_FUNCTIONFS_H__
#define _UAPI__LINUX_FUNCTIONFS_H__


#include <linux/types.h>
#include <linux/ioctl.h>

#include <linux/usb/ch9.h>


enum {
	FUNCTIONFS_DESCRIPTORS_MAGIC = 1,
	FUNCTIONFS_STRINGS_MAGIC     = 2
};


#ifndef __KERNEL__

/* Descriptor of an non-audio endpoint */
struct usb_endpoint_descriptor_no_audio {
	__u8  bLength;
	__u8  bDescriptorType;

	__u8  bEndpointAddress;
	__u8  bmAttributes;
	__le16 wMaxPacketSize;
	__u8  bInterval;
} __attribute__((packed));


/*
 * All numbers must be in little endian order.
 */

struct usb_functionfs_descs_head {
	__le32 magic;
	__le32 length;
	__le32 fs_count;
	__le32 hs_count;
} __attribute__((packed));

/*
 * Descriptors format:
 *
 * | off | name      | type         | description                          |
 * |-----+-----------+--------------+--------------------------------------|
 * |   0 | magic     | LE32         | FUNCTIONFS_{FS,HS}_DESCRIPTORS_MAGIC |
 * |   4 | length    | LE32         | length of the whole data chunk       |
 * |   8 | fs_count  | LE32         | number of full-speed descriptors     |
 * |  12 | hs_count  | LE32         | number of high-speed descriptors     |
 * |  16 | fs_descrs | Descriptor[] | list of full-speed descriptors       |
 * |     | hs_descrs | Descriptor[] | list of high-speed descriptors       |
 *
 * descs are just valid USB descriptors and have the following format:
 *
 * | off | name            | type | description              |
 * |-----+-----------------+------+--------------------------|
 * |   0 | bLength         | U8   | length of the descriptor |
 * |   1 | bDescriptorType | U8   | descriptor type          |
 * |   2 | payload         |      | descriptor's payload     |
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
 * Returns real bEndpointAddress of an endpoint.  If function is not
 * active returns -ENODEV.
 */
#define	FUNCTIONFS_ENDPOINT_REVMAP	_IO('g', 129)



#endif /* _UAPI__LINUX_FUNCTIONFS_H__ */
