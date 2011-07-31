/*
 * Copyright (C) 2011 Motorola, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */
#ifndef __USB_OOB_WAKE_H__
#define __USB_OOB_WAKE_H__
#include <linux/usb.h>

struct oob_wake_platform_data {
	unsigned int gpio;
	__le16 vendor;
	__le16 product;
};

int oob_wake_register(struct usb_interface *intf);
void oob_wake_unregister(struct usb_interface *intf);
#endif /* __USB_OOB_WAKE_H__ */
