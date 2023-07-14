/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * WebUSB descriptors and constants
 *
 * Copyright (C) 2023 Jó Ágila Bitsch <jgilab@gmail.com>
 */

#ifndef	__LINUX_USB_WEBUSB_H
#define	__LINUX_USB_WEBUSB_H

#include "uapi/linux/usb/ch9.h"

/*
 * Little Endian PlatformCapablityUUID for WebUSB
 * 3408b638-09a9-47a0-8bfd-a0768815b665
 * to identify Platform Device Capability descriptors as referring to WebUSB.
 */
#define WEBUSB_UUID \
	GUID_INIT(0x3408b638, 0x09a9, 0x47a0, 0x8b, 0xfd, 0xa0, 0x76, 0x88, 0x15, 0xb6, 0x65)

/*
 * WebUSB Platform Capability data
 *
 * A device announces support for the
 * WebUSB command set by including the following Platform Descriptor Data in its
 * Binary Object Store associated with the WebUSB_UUID above.
 * See: https://wicg.github.io/webusb/#webusb-platform-capability-descriptor
 */
struct usb_webusb_cap_data {
	__le16 bcdVersion;
#define WEBUSB_VERSION_1_00	cpu_to_le16(0x0100) /* currently only version 1.00 is defined */
	u8  bVendorCode;
	u8  iLandingPage;
#define WEBUSB_LANDING_PAGE_NOT_PRESENT	0
#define WEBUSB_LANDING_PAGE_PRESENT	1 /* we chose the fixed index 1 for the URL descriptor */
} __packed;

#define USB_WEBUSB_CAP_DATA_SIZE	4

/*
 * Get URL Request
 *
 * The request to fetch an URL is defined in https://wicg.github.io/webusb/#get-url as:
 * bmRequestType: (USB_DIR_IN | USB_TYPE_VENDOR) = 11000000B
 * bRequest: bVendorCode
 * wValue: iLandingPage
 * wIndex: GET_URL = 2
 * wLength: Descriptor Length (typically U8_MAX = 255)
 * Data: URL Descriptor
 */
#define WEBUSB_GET_URL 2

/*
 * This descriptor contains a single URL and is returned by the Get URL request.
 *
 * See: https://wicg.github.io/webusb/#url-descriptor
 */
struct webusb_url_descriptor {
	u8  bLength;
#define WEBUSB_URL_DESCRIPTOR_HEADER_LENGTH	3
	u8  bDescriptorType;
#define WEBUSB_URL_DESCRIPTOR_TYPE		3
	u8  bScheme;
#define WEBUSB_URL_SCHEME_HTTP			0
#define WEBUSB_URL_SCHEME_HTTPS			1
#define WEBUSB_URL_SCHEME_NONE			255
	u8  URL[U8_MAX - WEBUSB_URL_DESCRIPTOR_HEADER_LENGTH];
} __packed;

/*
 * Buffer size to hold the longest URL that can be in an URL descriptor
 *
 * The descriptor can be U8_MAX  bytes long.
 * WEBUSB_URL_DESCRIPTOR_HEADER_LENGTH bytes are used for a header.
 * Since the longest prefix that might be stripped is "https://", we may accommodate an additional
 * 8 bytes.
 */
#define WEBUSB_URL_RAW_MAX_LENGTH (U8_MAX - WEBUSB_URL_DESCRIPTOR_HEADER_LENGTH + 8)

#endif /* __LINUX_USB_USBNET_H */
