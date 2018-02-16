/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*****************************************************************************/

/*
 *	usbdevice_fs.h  --  USB device file system.
 *
 *	Copyright (C) 2000
 *          Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  History:
 *   0.1  04.01.2000  Created
 */

/*****************************************************************************/

#ifndef _UAPI_LINUX_USBDEVICE_FS_H
#define _UAPI_LINUX_USBDEVICE_FS_H

#include <linux/types.h>
#include <linux/magic.h>

/* --------------------------------------------------------------------- */

/* usbdevfs ioctl codes */

struct usbdevfs_ctrltransfer {
	__u8 bRequestType;
	__u8 bRequest;
	__u16 wValue;
	__u16 wIndex;
	__u16 wLength;
	__u32 timeout;  /* in milliseconds */
 	void __user *data;
};

struct usbdevfs_bulktransfer {
	unsigned int ep;
	unsigned int len;
	unsigned int timeout; /* in milliseconds */
	void __user *data;
};

struct usbdevfs_setinterface {
	unsigned int interface;
	unsigned int altsetting;
};

struct usbdevfs_disconnectsignal {
	unsigned int signr;
	void __user *context;
};

#define USBDEVFS_MAXDRIVERNAME 255

struct usbdevfs_getdriver {
	unsigned int interface;
	char driver[USBDEVFS_MAXDRIVERNAME + 1];
};

struct usbdevfs_connectinfo {
	unsigned int devnum;
	unsigned char slow;
};

#define USBDEVFS_URB_SHORT_NOT_OK	0x01
#define USBDEVFS_URB_ISO_ASAP		0x02
#define USBDEVFS_URB_BULK_CONTINUATION	0x04
#define USBDEVFS_URB_NO_FSBR		0x20	/* Not used */
#define USBDEVFS_URB_ZERO_PACKET	0x40
#define USBDEVFS_URB_NO_INTERRUPT	0x80

#define USBDEVFS_URB_TYPE_ISO		   0
#define USBDEVFS_URB_TYPE_INTERRUPT	   1
#define USBDEVFS_URB_TYPE_CONTROL	   2
#define USBDEVFS_URB_TYPE_BULK		   3

struct usbdevfs_iso_packet_desc {
	unsigned int length;
	unsigned int actual_length;
	unsigned int status;
};

struct usbdevfs_urb {
	unsigned char type;
	unsigned char endpoint;
	int status;
	unsigned int flags;
	void __user *buffer;
	int buffer_length;
	int actual_length;
	int start_frame;
	union {
		int number_of_packets;	/* Only used for isoc urbs */
		unsigned int stream_id;	/* Only used with bulk streams */
	};
	int error_count;
	unsigned int signr;	/* signal to be sent on completion,
				  or 0 if none should be sent. */
	void __user *usercontext;
	struct usbdevfs_iso_packet_desc iso_frame_desc[0];
};

/* ioctls for talking directly to drivers */
struct usbdevfs_ioctl {
	int	ifno;		/* interface 0..N ; negative numbers reserved */
	int	ioctl_code;	/* MUST encode size + direction of data so the
				 * macros in <asm/ioctl.h> give correct values */
	void __user *data;	/* param buffer (in, or out) */
};

/* You can do most things with hubs just through control messages,
 * except find out what device connects to what port. */
struct usbdevfs_hub_portinfo {
	char nports;		/* number of downstream ports in this hub */
	char port [127];	/* e.g. port 3 connects to device 27 */
};

/* System and bus capability flags */
#define USBDEVFS_CAP_ZERO_PACKET		0x01
#define USBDEVFS_CAP_BULK_CONTINUATION		0x02
#define USBDEVFS_CAP_NO_PACKET_SIZE_LIM		0x04
#define USBDEVFS_CAP_BULK_SCATTER_GATHER	0x08
#define USBDEVFS_CAP_REAP_AFTER_DISCONNECT	0x10
#define USBDEVFS_CAP_MMAP			0x20
#define USBDEVFS_CAP_DROP_PRIVILEGES		0x40

/* USBDEVFS_DISCONNECT_CLAIM flags & struct */

/* disconnect-and-claim if the driver matches the driver field */
#define USBDEVFS_DISCONNECT_CLAIM_IF_DRIVER	0x01
/* disconnect-and-claim except when the driver matches the driver field */
#define USBDEVFS_DISCONNECT_CLAIM_EXCEPT_DRIVER	0x02

struct usbdevfs_disconnect_claim {
	unsigned int interface;
	unsigned int flags;
	char driver[USBDEVFS_MAXDRIVERNAME + 1];
};

struct usbdevfs_streams {
	unsigned int num_streams; /* Not used by USBDEVFS_FREE_STREAMS */
	unsigned int num_eps;
	unsigned char eps[0];
};

/*
 * USB_SPEED_* values returned by USBDEVFS_GET_SPEED are defined in
 * linux/usb/ch9.h
 */

#define USBDEVFS_CONTROL           _IOWR('U', 0, struct usbdevfs_ctrltransfer)
#define USBDEVFS_CONTROL32           _IOWR('U', 0, struct usbdevfs_ctrltransfer32)
#define USBDEVFS_BULK              _IOWR('U', 2, struct usbdevfs_bulktransfer)
#define USBDEVFS_BULK32              _IOWR('U', 2, struct usbdevfs_bulktransfer32)
#define USBDEVFS_RESETEP           _IOR('U', 3, unsigned int)
#define USBDEVFS_SETINTERFACE      _IOR('U', 4, struct usbdevfs_setinterface)
#define USBDEVFS_SETCONFIGURATION  _IOR('U', 5, unsigned int)
#define USBDEVFS_GETDRIVER         _IOW('U', 8, struct usbdevfs_getdriver)
#define USBDEVFS_SUBMITURB         _IOR('U', 10, struct usbdevfs_urb)
#define USBDEVFS_SUBMITURB32       _IOR('U', 10, struct usbdevfs_urb32)
#define USBDEVFS_DISCARDURB        _IO('U', 11)
#define USBDEVFS_REAPURB           _IOW('U', 12, void *)
#define USBDEVFS_REAPURB32         _IOW('U', 12, __u32)
#define USBDEVFS_REAPURBNDELAY     _IOW('U', 13, void *)
#define USBDEVFS_REAPURBNDELAY32   _IOW('U', 13, __u32)
#define USBDEVFS_DISCSIGNAL        _IOR('U', 14, struct usbdevfs_disconnectsignal)
#define USBDEVFS_DISCSIGNAL32      _IOR('U', 14, struct usbdevfs_disconnectsignal32)
#define USBDEVFS_CLAIMINTERFACE    _IOR('U', 15, unsigned int)
#define USBDEVFS_RELEASEINTERFACE  _IOR('U', 16, unsigned int)
#define USBDEVFS_CONNECTINFO       _IOW('U', 17, struct usbdevfs_connectinfo)
#define USBDEVFS_IOCTL             _IOWR('U', 18, struct usbdevfs_ioctl)
#define USBDEVFS_IOCTL32           _IOWR('U', 18, struct usbdevfs_ioctl32)
#define USBDEVFS_HUB_PORTINFO      _IOR('U', 19, struct usbdevfs_hub_portinfo)
#define USBDEVFS_RESET             _IO('U', 20)
#define USBDEVFS_CLEAR_HALT        _IOR('U', 21, unsigned int)
#define USBDEVFS_DISCONNECT        _IO('U', 22)
#define USBDEVFS_CONNECT           _IO('U', 23)
#define USBDEVFS_CLAIM_PORT        _IOR('U', 24, unsigned int)
#define USBDEVFS_RELEASE_PORT      _IOR('U', 25, unsigned int)
#define USBDEVFS_GET_CAPABILITIES  _IOR('U', 26, __u32)
#define USBDEVFS_DISCONNECT_CLAIM  _IOR('U', 27, struct usbdevfs_disconnect_claim)
#define USBDEVFS_ALLOC_STREAMS     _IOR('U', 28, struct usbdevfs_streams)
#define USBDEVFS_FREE_STREAMS      _IOR('U', 29, struct usbdevfs_streams)
#define USBDEVFS_DROP_PRIVILEGES   _IOW('U', 30, __u32)
#define USBDEVFS_GET_SPEED         _IO('U', 31)

#endif /* _UAPI_LINUX_USBDEVICE_FS_H */
