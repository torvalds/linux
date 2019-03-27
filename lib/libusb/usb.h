/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _LIBUSB20_COMPAT_01_H_
#define	_LIBUSB20_COMPAT_01_H_

#ifndef LIBUSB_GLOBAL_INCLUDE_FILE
#include <stdint.h>
#include <sys/param.h>
#include <sys/endian.h>
#endif

/* USB interface class codes */

#define	USB_CLASS_PER_INTERFACE         0
#define	USB_CLASS_AUDIO                 1
#define	USB_CLASS_COMM                  2
#define	USB_CLASS_HID                   3
#define	USB_CLASS_PRINTER               7
#define	USB_CLASS_PTP                   6
#define	USB_CLASS_MASS_STORAGE          8
#define	USB_CLASS_HUB                   9
#define	USB_CLASS_DATA                  10
#define	USB_CLASS_VENDOR_SPEC           0xff

/* USB descriptor types */

#define	USB_DT_DEVICE                   0x01
#define	USB_DT_CONFIG                   0x02
#define	USB_DT_STRING                   0x03
#define	USB_DT_INTERFACE                0x04
#define	USB_DT_ENDPOINT                 0x05

#define	USB_DT_HID                      0x21
#define	USB_DT_REPORT                   0x22
#define	USB_DT_PHYSICAL                 0x23
#define	USB_DT_HUB                      0x29

/* USB descriptor type sizes */

#define	USB_DT_DEVICE_SIZE              18
#define	USB_DT_CONFIG_SIZE              9
#define	USB_DT_INTERFACE_SIZE           9
#define	USB_DT_ENDPOINT_SIZE            7
#define	USB_DT_ENDPOINT_AUDIO_SIZE      9
#define	USB_DT_HUB_NONVAR_SIZE          7

/* USB descriptor header */
struct usb_descriptor_header {
	uint8_t	bLength;
	uint8_t	bDescriptorType;
};

/* USB string descriptor */
struct usb_string_descriptor {
	uint8_t	bLength;
	uint8_t	bDescriptorType;
	uint16_t wData[1];
};

/* USB HID descriptor */
struct usb_hid_descriptor {
	uint8_t	bLength;
	uint8_t	bDescriptorType;
	uint16_t bcdHID;
	uint8_t	bCountryCode;
	uint8_t	bNumDescriptors;
	/* uint8_t  bReportDescriptorType; */
	/* uint16_t wDescriptorLength; */
	/* ... */
};

/* USB endpoint descriptor */
#define	USB_MAXENDPOINTS        32
struct usb_endpoint_descriptor {
	uint8_t	bLength;
	uint8_t	bDescriptorType;
	uint8_t	bEndpointAddress;
#define	USB_ENDPOINT_ADDRESS_MASK       0x0f
#define	USB_ENDPOINT_DIR_MASK           0x80
	uint8_t	bmAttributes;
#define	USB_ENDPOINT_TYPE_MASK          0x03
#define	USB_ENDPOINT_TYPE_CONTROL       0
#define	USB_ENDPOINT_TYPE_ISOCHRONOUS   1
#define	USB_ENDPOINT_TYPE_BULK          2
#define	USB_ENDPOINT_TYPE_INTERRUPT     3
	uint16_t wMaxPacketSize;
	uint8_t	bInterval;
	uint8_t	bRefresh;
	uint8_t	bSynchAddress;

	uint8_t *extra;			/* Extra descriptors */
	int	extralen;
};

/* USB interface descriptor */
#define	USB_MAXINTERFACES       32
struct usb_interface_descriptor {
	uint8_t	bLength;
	uint8_t	bDescriptorType;
	uint8_t	bInterfaceNumber;
	uint8_t	bAlternateSetting;
	uint8_t	bNumEndpoints;
	uint8_t	bInterfaceClass;
	uint8_t	bInterfaceSubClass;
	uint8_t	bInterfaceProtocol;
	uint8_t	iInterface;

	struct usb_endpoint_descriptor *endpoint;

	uint8_t *extra;			/* Extra descriptors */
	int	extralen;
};

#define	USB_MAXALTSETTING       128	/* Hard limit */
struct usb_interface {
	struct usb_interface_descriptor *altsetting;

	int	num_altsetting;
};

/* USB configuration descriptor */
#define	USB_MAXCONFIG           8
struct usb_config_descriptor {
	uint8_t	bLength;
	uint8_t	bDescriptorType;
	uint16_t wTotalLength;
	uint8_t	bNumInterfaces;
	uint8_t	bConfigurationValue;
	uint8_t	iConfiguration;
	uint8_t	bmAttributes;
	uint8_t	MaxPower;

	struct usb_interface *interface;

	uint8_t *extra;			/* Extra descriptors */
	int	extralen;
};

/* USB device descriptor */
struct usb_device_descriptor {
	uint8_t	bLength;
	uint8_t	bDescriptorType;
	uint16_t bcdUSB;
	uint8_t	bDeviceClass;
	uint8_t	bDeviceSubClass;
	uint8_t	bDeviceProtocol;
	uint8_t	bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t	iManufacturer;
	uint8_t	iProduct;
	uint8_t	iSerialNumber;
	uint8_t	bNumConfigurations;
};

/* USB setup packet */
struct usb_ctrl_setup {
	uint8_t	bRequestType;
#define	USB_RECIP_DEVICE                0x00
#define	USB_RECIP_INTERFACE             0x01
#define	USB_RECIP_ENDPOINT              0x02
#define	USB_RECIP_OTHER                 0x03
#define	USB_TYPE_STANDARD               (0x00 << 5)
#define	USB_TYPE_CLASS                  (0x01 << 5)
#define	USB_TYPE_VENDOR                 (0x02 << 5)
#define	USB_TYPE_RESERVED               (0x03 << 5)
#define	USB_ENDPOINT_IN                 0x80
#define	USB_ENDPOINT_OUT                0x00
	uint8_t	bRequest;
#define	USB_REQ_GET_STATUS              0x00
#define	USB_REQ_CLEAR_FEATURE           0x01
#define	USB_REQ_SET_FEATURE             0x03
#define	USB_REQ_SET_ADDRESS             0x05
#define	USB_REQ_GET_DESCRIPTOR          0x06
#define	USB_REQ_SET_DESCRIPTOR          0x07
#define	USB_REQ_GET_CONFIGURATION       0x08
#define	USB_REQ_SET_CONFIGURATION       0x09
#define	USB_REQ_GET_INTERFACE           0x0A
#define	USB_REQ_SET_INTERFACE           0x0B
#define	USB_REQ_SYNCH_FRAME             0x0C
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
};

/* Error codes */
#define	USB_ERROR_BEGIN                 500000

/* Byte swapping */
#define	USB_LE16_TO_CPU(x) le16toh(x)

/* Data types */
struct usb_device;
struct usb_bus;

/*
 * To maintain compatibility with applications already built with libusb,
 * we must only add entries to the end of this structure. NEVER delete or
 * move members and only change types if you really know what you're doing.
 */
struct usb_device {
	struct usb_device *next;
	struct usb_device *prev;

	char	filename[PATH_MAX + 1];

	struct usb_bus *bus;

	struct usb_device_descriptor descriptor;
	struct usb_config_descriptor *config;

	void   *dev;

	uint8_t	devnum;

	uint8_t	num_children;
	struct usb_device **children;
};

struct usb_bus {
	struct usb_bus *next;
	struct usb_bus *prev;

	char	dirname[PATH_MAX + 1];

	struct usb_device *devices;
	uint32_t location;

	struct usb_device *root_dev;
};

struct usb_dev_handle;
typedef struct usb_dev_handle usb_dev_handle;

/* Variables */
extern struct usb_bus *usb_busses;

#ifdef __cplusplus
extern	"C" {
#endif
#if 0
}					/* style */

#endif

/* Function prototypes from "libusb20_compat01.c" */

usb_dev_handle *usb_open(struct usb_device *dev);
int	usb_close(usb_dev_handle * dev);
int	usb_get_string(usb_dev_handle * dev, int index, int langid, char *buf, size_t buflen);
int	usb_get_string_simple(usb_dev_handle * dev, int index, char *buf, size_t buflen);
int	usb_get_descriptor_by_endpoint(usb_dev_handle * udev, int ep, uint8_t type, uint8_t index, void *buf, int size);
int	usb_get_descriptor(usb_dev_handle * udev, uint8_t type, uint8_t index, void *buf, int size);
int	usb_parse_descriptor(uint8_t *source, char *description, void *dest);
int	usb_parse_configuration(struct usb_config_descriptor *config, uint8_t *buffer);
void	usb_destroy_configuration(struct usb_device *dev);
void	usb_fetch_and_parse_descriptors(usb_dev_handle * udev);
int	usb_bulk_write(usb_dev_handle * dev, int ep, char *bytes, int size, int timeout);
int	usb_bulk_read(usb_dev_handle * dev, int ep, char *bytes, int size, int timeout);
int	usb_interrupt_write(usb_dev_handle * dev, int ep, char *bytes, int size, int timeout);
int	usb_interrupt_read(usb_dev_handle * dev, int ep, char *bytes, int size, int timeout);
int	usb_control_msg(usb_dev_handle * dev, int requesttype, int request, int value, int index, char *bytes, int size, int timeout);
int	usb_set_configuration(usb_dev_handle * dev, int configuration);
int	usb_claim_interface(usb_dev_handle * dev, int interface);
int	usb_release_interface(usb_dev_handle * dev, int interface);
int	usb_set_altinterface(usb_dev_handle * dev, int alternate);
int	usb_resetep(usb_dev_handle * dev, unsigned int ep);
int	usb_clear_halt(usb_dev_handle * dev, unsigned int ep);
int	usb_reset(usb_dev_handle * dev);
int	usb_check_connected(usb_dev_handle * dev);
const char *usb_strerror(void);
void	usb_init(void);
void	usb_set_debug(int level);
int	usb_find_busses(void);
int	usb_find_devices(void);
struct usb_device *usb_device(usb_dev_handle * dev);
struct usb_bus *usb_get_busses(void);
int	usb_get_driver_np(usb_dev_handle * dev, int interface, char *name, int namelen);
int	usb_detach_kernel_driver_np(usb_dev_handle * dev, int interface);

#if 0
{					/* style */
#endif
#ifdef __cplusplus
}

#endif

#endif					/* _LIBUSB20_COMPAT01_H_ */
