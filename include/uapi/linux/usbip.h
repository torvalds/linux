/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *	usbip.h
 *
 *	USBIP uapi defines and function prototypes etc.
*/

#ifndef _UAPI_LINUX_USBIP_H
#define _UAPI_LINUX_USBIP_H

/* usbip device status - exported in usbip device sysfs status */
enum usbip_device_status {
	/* sdev is available. */
	SDEV_ST_AVAILABLE = 0x01,
	/* sdev is now used. */
	SDEV_ST_USED,
	/* sdev is unusable because of a fatal error. */
	SDEV_ST_ERROR,

	/* vdev does not connect a remote device. */
	VDEV_ST_NULL,
	/* vdev is used, but the USB address is not assigned yet */
	VDEV_ST_NOTASSIGNED,
	VDEV_ST_USED,
	VDEV_ST_ERROR
};

/* USB URB Transfer flags:
 *
 * USBIP server and client (vchi) pack URBs in TCP packets. The following
 * are the transfer type defines used in USBIP protocol.
 */

#define USBIP_URB_SHORT_NOT_OK		0x0001
#define USBIP_URB_ISO_ASAP		0x0002
#define USBIP_URB_NO_TRANSFER_DMA_MAP	0x0004
#define USBIP_URB_ZERO_PACKET		0x0040
#define USBIP_URB_NO_INTERRUPT		0x0080
#define USBIP_URB_FREE_BUFFER		0x0100
#define USBIP_URB_DIR_IN		0x0200
#define USBIP_URB_DIR_OUT		0
#define USBIP_URB_DIR_MASK		USBIP_URB_DIR_IN

#define USBIP_URB_DMA_MAP_SINGLE	0x00010000
#define USBIP_URB_DMA_MAP_PAGE		0x00020000
#define USBIP_URB_DMA_MAP_SG		0x00040000
#define USBIP_URB_MAP_LOCAL		0x00080000
#define USBIP_URB_SETUP_MAP_SINGLE	0x00100000
#define USBIP_URB_SETUP_MAP_LOCAL	0x00200000
#define USBIP_URB_DMA_SG_COMBINED	0x00400000
#define USBIP_URB_ALIGNED_TEMP_BUFFER	0x00800000

#endif /* _UAPI_LINUX_USBIP_H */
