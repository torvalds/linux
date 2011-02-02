/*
 * Gadget Function Driver for Android USB accessories
 *
 * Copyright (C) 2011 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
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

#ifndef __LINUX_USB_F_ACCESSORY_H
#define __LINUX_USB_F_ACCESSORY_H

/* Use Google Vendor ID when in accessory mode */
#define USB_ACCESSORY_VENDOR_ID 0x18D1


/* Product ID to use when in accessory mode */
#define USB_ACCESSORY_PRODUCT_ID 0x2D00

/* Product ID to use when in accessory mode and adb is enabled */
#define USB_ACCESSORY_ADB_PRODUCT_ID 0x2D01

/*
 * Indexes for strings sent by the host to identify the accessory.
 * The host sends these as vendor requests:
 *
 *	requestType:    USB_DIR_OUT | USB_TYPE_VENDOR
 *	request:        ACCESSORY_SEND_STRING
 *	value:          0
 *	index:          string ID
 *	data            zero terminated UTF8 string
 *
 *  The device can later retrieve these strings via the
 *  ACCESSORY_GET_STRING_* ioctls
 */
#define ACCESSORY_STRING_MANUFACTURER   0
#define ACCESSORY_STRING_MODEL          1
#define ACCESSORY_STRING_TYPE           2
#define ACCESSORY_STRING_VERSION        3

/* control requests */
#define ACCESSORY_SEND_STRING   52
#define ACCESSORY_START         53

/* Sends an event to the accessory via the interrupt endpoint */
#define ACCESSORY_GET_STRING_MANUFACTURER   _IOW('M', 1, char[256])
#define ACCESSORY_GET_STRING_MODEL          _IOW('M', 2, char[256])
#define ACCESSORY_GET_STRING_TYPE           _IOW('M', 3, char[256])
#define ACCESSORY_GET_STRING_VERSION        _IOW('M', 4, char[256])

#endif /* __LINUX_USB_F_ACCESSORY_H */
