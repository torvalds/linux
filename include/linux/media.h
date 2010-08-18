/*
 * Multimedia device API
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __LINUX_MEDIA_H
#define __LINUX_MEDIA_H

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/version.h>

#define MEDIA_API_VERSION	KERNEL_VERSION(0, 1, 0)

struct media_device_info {
	char driver[16];
	char model[32];
	char serial[40];
	char bus_info[32];
	__u32 media_version;
	__u32 hw_revision;
	__u32 driver_version;
	__u32 reserved[31];
};

#define MEDIA_IOC_DEVICE_INFO		_IOWR('M', 1, struct media_device_info)

#endif /* __LINUX_MEDIA_H */
