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

/* Indexes for strings sent by the host via ACCESSORY_SEND_STRING */
#define ACCESSORY_STRING_MANUFACTURER   0
#define ACCESSORY_STRING_MODEL          1
#define ACCESSORY_STRING_DESCRIPTION    2
#define ACCESSORY_STRING_VERSION        3
#define ACCESSORY_STRING_URI            4
#define ACCESSORY_STRING_SERIAL         5

/* Control request for retrieving device's protocol version (currently 1)
 *
 *	requestType:    USB_DIR_IN | USB_TYPE_VENDOR
 *	request:        ACCESSORY_GET_PROTOCOL
 *	value:          0
 *	index:          0
 *	data            version number (16 bits little endian)
 */
#define ACCESSORY_GET_PROTOCOL  51

/* Control request for host to send a string to the device
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
#define ACCESSORY_SEND_STRING   52

/* Control request for starting device in accessory mode.
 * The host sends this after setting all its strings to the device.
 *
 *	requestType:    USB_DIR_OUT | USB_TYPE_VENDOR
 *	request:        ACCESSORY_START
 *	value:          0
 *	index:          0
 *	data            none
 */
#define ACCESSORY_START         53

/* ioctls for retrieving strings set by the host */
#define ACCESSORY_GET_STRING_MANUFACTURER   _IOW('M', 1, char[256])
#define ACCESSORY_GET_STRING_MODEL          _IOW('M', 2, char[256])
#define ACCESSORY_GET_STRING_DESCRIPTION    _IOW('M', 3, char[256])
#define ACCESSORY_GET_STRING_VERSION        _IOW('M', 4, char[256])
#define ACCESSORY_GET_STRING_URI            _IOW('M', 5, char[256])
#define ACCESSORY_GET_STRING_SERIAL         _IOW('M', 6, char[256])

#endif /* __LINUX_USB_F_ACCESSORY_H */
