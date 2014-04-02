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

#ifndef _UAPI_LINUX_USB_F_ACCESSORY_H
#define _UAPI_LINUX_USB_F_ACCESSORY_H

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

/* Control request for retrieving device's protocol version
 *
 *	requestType:    USB_DIR_IN | USB_TYPE_VENDOR
 *	request:        ACCESSORY_GET_PROTOCOL
 *	value:          0
 *	index:          0
 *	data            version number (16 bits little endian)
 *                     1 for original accessory support
 *                     2 adds HID and device to host audio support
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

/* Control request for registering a HID device.
 * Upon registering, a unique ID is sent by the accessory in the
 * value parameter. This ID will be used for future commands for
 * the device
 *
 *	requestType:    USB_DIR_OUT | USB_TYPE_VENDOR
 *	request:        ACCESSORY_REGISTER_HID_DEVICE
 *	value:          Accessory assigned ID for the HID device
 *	index:          total length of the HID report descriptor
 *	data            none
 */
#define ACCESSORY_REGISTER_HID         54

/* Control request for unregistering a HID device.
 *
 *	requestType:    USB_DIR_OUT | USB_TYPE_VENDOR
 *	request:        ACCESSORY_REGISTER_HID
 *	value:          Accessory assigned ID for the HID device
 *	index:          0
 *	data            none
 */
#define ACCESSORY_UNREGISTER_HID         55

/* Control request for sending the HID report descriptor.
 * If the HID descriptor is longer than the endpoint zero max packet size,
 * the descriptor will be sent in multiple ACCESSORY_SET_HID_REPORT_DESC
 * commands. The data for the descriptor must be sent sequentially
 * if multiple packets are needed.
 *
 *	requestType:    USB_DIR_OUT | USB_TYPE_VENDOR
 *	request:        ACCESSORY_SET_HID_REPORT_DESC
 *	value:          Accessory assigned ID for the HID device
 *	index:          offset of data in descriptor
 *                      (needed when HID descriptor is too big for one packet)
 *	data            the HID report descriptor
 */
#define ACCESSORY_SET_HID_REPORT_DESC         56

/* Control request for sending HID events.
 *
 *	requestType:    USB_DIR_OUT | USB_TYPE_VENDOR
 *	request:        ACCESSORY_SEND_HID_EVENT
 *	value:          Accessory assigned ID for the HID device
 *	index:          0
 *	data            the HID report for the event
 */
#define ACCESSORY_SEND_HID_EVENT         57

/* Control request for setting the audio mode.
 *
 *	requestType:	USB_DIR_OUT | USB_TYPE_VENDOR
 *	request:        ACCESSORY_SET_AUDIO_MODE
 *	value:          0 - no audio
 *                     1 - device to host, 44100 16-bit stereo PCM
 *	index:          0
 *	data            none
 */
#define ACCESSORY_SET_AUDIO_MODE         58

/* ioctls for retrieving strings set by the host */
#define ACCESSORY_GET_STRING_MANUFACTURER   _IOW('M', 1, char[256])
#define ACCESSORY_GET_STRING_MODEL          _IOW('M', 2, char[256])
#define ACCESSORY_GET_STRING_DESCRIPTION    _IOW('M', 3, char[256])
#define ACCESSORY_GET_STRING_VERSION        _IOW('M', 4, char[256])
#define ACCESSORY_GET_STRING_URI            _IOW('M', 5, char[256])
#define ACCESSORY_GET_STRING_SERIAL         _IOW('M', 6, char[256])
/* returns 1 if there is a start request pending */
#define ACCESSORY_IS_START_REQUESTED        _IO('M', 7)
/* returns audio mode (set via the ACCESSORY_SET_AUDIO_MODE control request) */
#define ACCESSORY_GET_AUDIO_MODE            _IO('M', 8)

#endif /* _UAPI_LINUX_USB_F_ACCESSORY_H */
