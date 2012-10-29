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
#ifndef _LINUX_USBDEVICE_FS_H
#define _LINUX_USBDEVICE_FS_H

#include <uapi/linux/usbdevice_fs.h>

#ifdef CONFIG_COMPAT
#include <linux/compat.h>

struct usbdevfs_ctrltransfer32 {
        u8 bRequestType;
        u8 bRequest;
        u16 wValue;
        u16 wIndex;
        u16 wLength;
        u32 timeout;  /* in milliseconds */
        compat_caddr_t data;
};

struct usbdevfs_bulktransfer32 {
        compat_uint_t ep;
        compat_uint_t len;
        compat_uint_t timeout; /* in milliseconds */
        compat_caddr_t data;
};

struct usbdevfs_disconnectsignal32 {
        compat_int_t signr;
        compat_caddr_t context;
};

struct usbdevfs_urb32 {
	unsigned char type;
	unsigned char endpoint;
	compat_int_t status;
	compat_uint_t flags;
	compat_caddr_t buffer;
	compat_int_t buffer_length;
	compat_int_t actual_length;
	compat_int_t start_frame;
	compat_int_t number_of_packets;
	compat_int_t error_count;
	compat_uint_t signr;
	compat_caddr_t usercontext; /* unused */
	struct usbdevfs_iso_packet_desc iso_frame_desc[0];
};

struct usbdevfs_ioctl32 {
	s32 ifno;
	s32 ioctl_code;
	compat_caddr_t data;
};
#endif
#endif /* _LINUX_USBDEVICE_FS_H */
