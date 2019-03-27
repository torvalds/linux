/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
 * Copyright (c) 2007-2008 Daniel Drake.  All rights reserved.
 * Copyright (c) 2001 Johannes Erdfelt.  All rights reserved.
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

/*
 * NOTE: This file contains the definition of some standard USB
 * structures. All structures which name ends by *DECODED use host byte
 * order.
 */

/*
 * NOTE: This file uses a lot of macros. If you want to see what the
 * macros become when they are expanded then run the following
 * commands from your shell:
 *
 * cpp libusb20_desc.h > temp.h
 * indent temp.h
 * less temp.h
 */

#ifndef _LIBUSB20_DESC_H_
#define	_LIBUSB20_DESC_H_

#ifndef LIBUSB_GLOBAL_INCLUDE_FILE
#include <stdint.h>
#endif

#ifdef __cplusplus
extern	"C" {
#endif
#if 0
};					/* style */

#endif
/* basic macros */

#define	LIBUSB20__NOT(...) __VA_ARGS__
#define	LIBUSB20_NOT(arg) LIBUSB20__NOT(LIBUSB20_YES arg(() LIBUSB20_NO))
#define	LIBUSB20_YES(...) __VA_ARGS__
#define	LIBUSB20_NO(...)
#define	LIBUSB20_END(...) __VA_ARGS__
#define	LIBUSB20_MAX(a,b) (((a) > (b)) ? (a) : (b))
#define	LIBUSB20_MIN(a,b) (((a) < (b)) ? (a) : (b))

#define	LIBUSB20_ADD_BYTES(ptr,off) \
  ((void *)(((const uint8_t *)(ptr)) + (off) - ((const uint8_t *)0)))

/* basic message elements */
enum {
	LIBUSB20_ME_INT8,
	LIBUSB20_ME_INT16,
	LIBUSB20_ME_INT32,
	LIBUSB20_ME_INT64,
	LIBUSB20_ME_STRUCT,
	LIBUSB20_ME_MAX,		/* used to indicate end */
};

/* basic message element modifiers */
enum {
	LIBUSB20_ME_IS_UNSIGNED = 0x00,
	LIBUSB20_ME_IS_SIGNED = 0x80,
	LIBUSB20_ME_MASK = 0x7F,
};

enum {
	LIBUSB20_ME_IS_RAW,		/* structure excludes length field
					 * (hardcoded value) */
	LIBUSB20_ME_IS_ENCODED,		/* structure includes length field */
	LIBUSB20_ME_IS_EMPTY,		/* no structure */
	LIBUSB20_ME_IS_DECODED,		/* structure is recursive */
};

/* basic helper structures and macros */

#define	LIBUSB20_ME_STRUCT_ALIGN sizeof(void *)

struct libusb20_me_struct {
	void   *ptr;			/* data pointer */
	uint16_t len;			/* defaults to zero */
	uint16_t type;			/* defaults to LIBUSB20_ME_IS_EMPTY */
} __aligned(LIBUSB20_ME_STRUCT_ALIGN);

struct libusb20_me_format {
	const uint8_t *format;		/* always set */
	const char *desc;		/* optionally set */
	const char *fields;		/* optionally set */
};

#define	LIBUSB20_ME_STRUCT(n, field, arg, ismeta)		\
  ismeta ( LIBUSB20_ME_STRUCT, 1, 0, )			\
  LIBUSB20_NOT(ismeta) ( struct libusb20_me_struct field; )

#define	LIBUSB20_ME_STRUCT_ARRAY(n, field, arg, ismeta)	\
  ismeta ( LIBUSB20_ME_STRUCT , (arg) & 0xFF,		\
	   ((arg) / 0x100) & 0xFF, )			\
  LIBUSB20_NOT(ismeta) ( struct libusb20_me_struct field [arg]; )

#define	LIBUSB20_ME_INTEGER(n, field, ismeta, un, u, bits, a, size)	\
  ismeta ( LIBUSB20_ME_INT##bits |					\
	   LIBUSB20_ME_IS_##un##SIGNED ,				\
	   (size) & 0xFF, ((size) / 0x100) & 0xFF, )		\
  LIBUSB20_NOT(ismeta) ( u##int##bits##_t				\
		    __aligned((bits) / 8) field a; )

#define	LIBUSB20_ME_UINT8_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta, UN, u, 8, , 1)

#define	LIBUSB20_ME_UINT8_ARRAY_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta, UN, u, 8, [arg], arg)

#define	LIBUSB20_ME_SINT8_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta,,, 8, , 1)

#define	LIBUSB20_ME_SINT8_ARRAY_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta,,, 8, [arg], arg)

#define	LIBUSB20_ME_UINT16_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta, UN, u, 16, , 1)

#define	LIBUSB20_ME_UINT16_ARRAY_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta, UN, u, 16, [arg], arg)

#define	LIBUSB20_ME_SINT16_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta,,, 16, , 1)

#define	LIBUSB20_ME_SINT16_ARRAY_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta,,, 16, [arg], arg)

#define	LIBUSB20_ME_UINT32_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta, UN, u, 32, , 1)

#define	LIBUSB20_ME_UINT32_ARRAY_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta, UN, u, 32, [arg], arg)

#define	LIBUSB20_ME_SINT32_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta,,, 32, , 1)

#define	LIBUSB20_ME_SINT32_ARRAY_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta,,, 32, [arg], arg)

#define	LIBUSB20_ME_UINT64_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta, UN, u, 64, , 1)

#define	LIBUSB20_ME_UINT64_ARRAY_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta, UN, u, 64, [arg], arg)

#define	LIBUSB20_ME_SINT64_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta,,, 64, , 1)

#define	LIBUSB20_ME_SINT64_ARRAY_T(n, field, arg, ismeta) \
  LIBUSB20_ME_INTEGER(n, field, ismeta,,, 64, [arg], arg)

#define	LIBUSB20_MAKE_DECODED_FIELD(n, type, field, arg) \
  LIBUSB20_ME_##type (n, field, arg, LIBUSB20_NO)

#define	LIBUSB20_MAKE_STRUCT(name)			\
  extern const struct libusb20_me_format			\
	 name##_FORMAT[1];				\
  struct name##_DECODED {				\
    const struct libusb20_me_format *name##_FORMAT;	\
    name (LIBUSB20_MAKE_DECODED_FIELD,)			\
  }

#define	LIBUSB20_MAKE_STRUCT_FORMAT(name)		\
  const struct libusb20_me_format			\
    name##_FORMAT[1] = {{			\
      .format = LIBUSB20_MAKE_FORMAT(name),	\
      .desc = #name,				\
      .fields = NULL,				\
  }}

#define	LIBUSB20_MAKE_FORMAT_SUB(n, type, field, arg) \
  LIBUSB20_ME_##type (n, field, arg, LIBUSB20_YES)

#define	LIBUSB20_MAKE_FORMAT(what) (const uint8_t []) \
  { what (LIBUSB20_MAKE_FORMAT_SUB, ) LIBUSB20_ME_MAX, 0, 0 }

#define	LIBUSB20_INIT(what, ptr) do {		\
    memset(ptr, 0, sizeof(*(ptr)));		\
    (ptr)->what##_FORMAT = what##_FORMAT;	\
} while (0)

#define	LIBUSB20_DEVICE_DESC(m,n) \
  m(n, UINT8_T, bLength, ) \
  m(n, UINT8_T, bDescriptorType, ) \
  m(n, UINT16_T, bcdUSB, ) \
  m(n, UINT8_T, bDeviceClass, ) \
  m(n, UINT8_T, bDeviceSubClass, ) \
  m(n, UINT8_T, bDeviceProtocol, ) \
  m(n, UINT8_T, bMaxPacketSize0, ) \
  m(n, UINT16_T, idVendor, ) \
  m(n, UINT16_T, idProduct, ) \
  m(n, UINT16_T, bcdDevice, ) \
  m(n, UINT8_T, iManufacturer, ) \
  m(n, UINT8_T, iProduct, ) \
  m(n, UINT8_T, iSerialNumber, ) \
  m(n, UINT8_T, bNumConfigurations, ) \

LIBUSB20_MAKE_STRUCT(LIBUSB20_DEVICE_DESC);

#define	LIBUSB20_ENDPOINT_DESC(m,n) \
  m(n, UINT8_T,  bLength, ) \
  m(n, UINT8_T,  bDescriptorType, ) \
  m(n, UINT8_T,  bEndpointAddress, ) \
  m(n, UINT8_T,  bmAttributes, ) \
  m(n, UINT16_T, wMaxPacketSize, ) \
  m(n, UINT8_T,  bInterval, ) \
  m(n, UINT8_T,  bRefresh, ) \
  m(n, UINT8_T,  bSynchAddress, ) \

LIBUSB20_MAKE_STRUCT(LIBUSB20_ENDPOINT_DESC);

#define	LIBUSB20_INTERFACE_DESC(m,n) \
  m(n, UINT8_T,  bLength, ) \
  m(n, UINT8_T,  bDescriptorType, ) \
  m(n, UINT8_T,  bInterfaceNumber, ) \
  m(n, UINT8_T,  bAlternateSetting, ) \
  m(n, UINT8_T,  bNumEndpoints, ) \
  m(n, UINT8_T,  bInterfaceClass, ) \
  m(n, UINT8_T,  bInterfaceSubClass, ) \
  m(n, UINT8_T,  bInterfaceProtocol, ) \
  m(n, UINT8_T,  iInterface, ) \

LIBUSB20_MAKE_STRUCT(LIBUSB20_INTERFACE_DESC);

#define	LIBUSB20_CONFIG_DESC(m,n) \
  m(n, UINT8_T,  bLength, ) \
  m(n, UINT8_T,  bDescriptorType, ) \
  m(n, UINT16_T, wTotalLength, ) \
  m(n, UINT8_T,  bNumInterfaces, ) \
  m(n, UINT8_T,  bConfigurationValue, ) \
  m(n, UINT8_T,  iConfiguration, ) \
  m(n, UINT8_T,  bmAttributes, ) \
  m(n, UINT8_T,  bMaxPower, ) \

LIBUSB20_MAKE_STRUCT(LIBUSB20_CONFIG_DESC);

#define	LIBUSB20_CONTROL_SETUP(m,n) \
  m(n, UINT8_T,  bmRequestType, ) \
  m(n, UINT8_T,  bRequest, ) \
  m(n, UINT16_T, wValue, ) \
  m(n, UINT16_T, wIndex, ) \
  m(n, UINT16_T, wLength, ) \

LIBUSB20_MAKE_STRUCT(LIBUSB20_CONTROL_SETUP);

#define	LIBUSB20_SS_ENDPT_COMP_DESC(m,n) \
  m(n, UINT8_T,  bLength, ) \
  m(n, UINT8_T,  bDescriptorType, ) \
  m(n, UINT8_T,  bMaxBurst, ) \
  m(n, UINT8_T,  bmAttributes, ) \
  m(n, UINT16_T, wBytesPerInterval, ) \

LIBUSB20_MAKE_STRUCT(LIBUSB20_SS_ENDPT_COMP_DESC);

#define	LIBUSB20_USB_20_DEVCAP_DESC(m,n) \
  m(n, UINT8_T,  bLength, ) \
  m(n, UINT8_T,  bDescriptorType, ) \
  m(n, UINT8_T,  bDevCapabilityType, ) \
  m(n, UINT32_T, bmAttributes, ) \

LIBUSB20_MAKE_STRUCT(LIBUSB20_USB_20_DEVCAP_DESC);

#define	LIBUSB20_SS_USB_DEVCAP_DESC(m,n) \
  m(n, UINT8_T,  bLength, ) \
  m(n, UINT8_T,  bDescriptorType, ) \
  m(n, UINT8_T,  bDevCapabilityType, ) \
  m(n, UINT8_T,  bmAttributes, ) \
  m(n, UINT16_T, wSpeedSupported, ) \
  m(n, UINT8_T,  bFunctionalitySupport, ) \
  m(n, UINT8_T,  bU1DevExitLat, ) \
  m(n, UINT16_T, wU2DevExitLat, ) \

LIBUSB20_MAKE_STRUCT(LIBUSB20_SS_USB_DEVCAP_DESC);

#define	LIBUSB20_BOS_DESCRIPTOR(m,n) \
  m(n, UINT8_T,  bLength, ) \
  m(n, UINT8_T,  bDescriptorType, ) \
  m(n, UINT16_T, wTotalLength, ) \
  m(n, UINT8_T,  bNumDeviceCapabilities, ) \

LIBUSB20_MAKE_STRUCT(LIBUSB20_BOS_DESCRIPTOR);

/* standard USB stuff */

/** \ingroup desc
 * Device and/or Interface Class codes */
enum libusb20_class_code {
	/** In the context of a \ref LIBUSB20_DEVICE_DESC "device
	 * descriptor", this bDeviceClass value indicates that each
	 * interface specifies its own class information and all
	 * interfaces operate independently.
	 */
	LIBUSB20_CLASS_PER_INTERFACE = 0,

	/** Audio class */
	LIBUSB20_CLASS_AUDIO = 1,

	/** Communications class */
	LIBUSB20_CLASS_COMM = 2,

	/** Human Interface Device class */
	LIBUSB20_CLASS_HID = 3,

	/** Printer dclass */
	LIBUSB20_CLASS_PRINTER = 7,

	/** Picture transfer protocol class */
	LIBUSB20_CLASS_PTP = 6,

	/** Mass storage class */
	LIBUSB20_CLASS_MASS_STORAGE = 8,

	/** Hub class */
	LIBUSB20_CLASS_HUB = 9,

	/** Data class */
	LIBUSB20_CLASS_DATA = 10,

	/** Class is vendor-specific */
	LIBUSB20_CLASS_VENDOR_SPEC = 0xff,
};

/** \ingroup desc
 * Descriptor types as defined by the USB specification. */
enum libusb20_descriptor_type {
	/** Device descriptor. See LIBUSB20_DEVICE_DESC. */
	LIBUSB20_DT_DEVICE = 0x01,

	/** Configuration descriptor. See LIBUSB20_CONFIG_DESC. */
	LIBUSB20_DT_CONFIG = 0x02,

	/** String descriptor */
	LIBUSB20_DT_STRING = 0x03,

	/** Interface descriptor. See LIBUSB20_INTERFACE_DESC. */
	LIBUSB20_DT_INTERFACE = 0x04,

	/** Endpoint descriptor. See LIBUSB20_ENDPOINT_DESC. */
	LIBUSB20_DT_ENDPOINT = 0x05,

	/** HID descriptor */
	LIBUSB20_DT_HID = 0x21,

	/** HID report descriptor */
	LIBUSB20_DT_REPORT = 0x22,

	/** Physical descriptor */
	LIBUSB20_DT_PHYSICAL = 0x23,

	/** Hub descriptor */
	LIBUSB20_DT_HUB = 0x29,

	/** Binary Object Store, BOS */
	LIBUSB20_DT_BOS = 0x0f,

	/** Device Capability */
	LIBUSB20_DT_DEVICE_CAPABILITY = 0x10,

	/** SuperSpeed endpoint companion */
	LIBUSB20_DT_SS_ENDPOINT_COMPANION = 0x30,
};

/** \ingroup desc
 * Device capability types as defined by the USB specification. */
enum libusb20_device_capability_type {
	LIBUSB20_WIRELESS_USB_DEVICE_CAPABILITY = 0x1,
	LIBUSB20_USB_2_0_EXTENSION_DEVICE_CAPABILITY = 0x2,
	LIBUSB20_SS_USB_DEVICE_CAPABILITY = 0x3,
	LIBUSB20_CONTAINER_ID_DEVICE_CAPABILITY = 0x4,
};

/* Descriptor sizes per descriptor type */
#define	LIBUSB20_DT_DEVICE_SIZE			18
#define	LIBUSB20_DT_CONFIG_SIZE			9
#define	LIBUSB20_DT_INTERFACE_SIZE		9
#define	LIBUSB20_DT_ENDPOINT_SIZE		7
#define	LIBUSB20_DT_ENDPOINT_AUDIO_SIZE		9	/* Audio extension */
#define	LIBUSB20_DT_HUB_NONVAR_SIZE		7
#define	LIBUSB20_DT_SS_ENDPOINT_COMPANION_SIZE	6
#define	LIBUSB20_DT_BOS_SIZE		5
#define	LIBUSB20_USB_2_0_EXTENSION_DEVICE_CAPABILITY_SIZE	7
#define	LIBUSB20_SS_USB_DEVICE_CAPABILITY_SIZE	10

#define	LIBUSB20_ENDPOINT_ADDRESS_MASK	0x0f	/* in bEndpointAddress */
#define	LIBUSB20_ENDPOINT_DIR_MASK	0x80

/** \ingroup desc
 * Endpoint direction. Values for bit 7 of the
 * \ref LIBUSB20_ENDPOINT_DESC::bEndpointAddress "endpoint address" scheme.
 */
enum libusb20_endpoint_direction {
	/** In: device-to-host */
	LIBUSB20_ENDPOINT_IN = 0x80,

	/** Out: host-to-device */
	LIBUSB20_ENDPOINT_OUT = 0x00,
};

#define	LIBUSB20_TRANSFER_TYPE_MASK	0x03	/* in bmAttributes */

/** \ingroup desc
 * Endpoint transfer type. Values for bits 0:1 of the
 * \ref LIBUSB20_ENDPOINT_DESC::bmAttributes "endpoint attributes" field.
 */
enum libusb20_transfer_type {
	/** Control endpoint */
	LIBUSB20_TRANSFER_TYPE_CONTROL = 0,

	/** Isochronous endpoint */
	LIBUSB20_TRANSFER_TYPE_ISOCHRONOUS = 1,

	/** Bulk endpoint */
	LIBUSB20_TRANSFER_TYPE_BULK = 2,

	/** Interrupt endpoint */
	LIBUSB20_TRANSFER_TYPE_INTERRUPT = 3,
};

/** \ingroup misc
 * Standard requests, as defined in table 9-3 of the USB2 specifications */
enum libusb20_standard_request {
	/** Request status of the specific recipient */
	LIBUSB20_REQUEST_GET_STATUS = 0x00,

	/** Clear or disable a specific feature */
	LIBUSB20_REQUEST_CLEAR_FEATURE = 0x01,

	/* 0x02 is reserved */

	/** Set or enable a specific feature */
	LIBUSB20_REQUEST_SET_FEATURE = 0x03,

	/* 0x04 is reserved */

	/** Set device address for all future accesses */
	LIBUSB20_REQUEST_SET_ADDRESS = 0x05,

	/** Get the specified descriptor */
	LIBUSB20_REQUEST_GET_DESCRIPTOR = 0x06,

	/** Used to update existing descriptors or add new descriptors */
	LIBUSB20_REQUEST_SET_DESCRIPTOR = 0x07,

	/** Get the current device configuration value */
	LIBUSB20_REQUEST_GET_CONFIGURATION = 0x08,

	/** Set device configuration */
	LIBUSB20_REQUEST_SET_CONFIGURATION = 0x09,

	/** Return the selected alternate setting for the specified
	 * interface */
	LIBUSB20_REQUEST_GET_INTERFACE = 0x0A,

	/** Select an alternate interface for the specified interface */
	LIBUSB20_REQUEST_SET_INTERFACE = 0x0B,

	/** Set then report an endpoint's synchronization frame */
	LIBUSB20_REQUEST_SYNCH_FRAME = 0x0C,

	/** Set U1 and U2 system exit latency */
	LIBUSB20_REQUEST_SET_SEL = 0x30,

	/** Set isochronous delay */
	LIBUSB20_REQUEST_SET_ISOCH_DELAY = 0x31,
};

/** \ingroup misc
 * Request type bits of the
 * \ref libusb20_control_setup::bmRequestType "bmRequestType" field in
 * control transfers. */
enum libusb20_request_type {
	/** Standard */
	LIBUSB20_REQUEST_TYPE_STANDARD = (0x00 << 5),

	/** Class */
	LIBUSB20_REQUEST_TYPE_CLASS = (0x01 << 5),

	/** Vendor */
	LIBUSB20_REQUEST_TYPE_VENDOR = (0x02 << 5),

	/** Reserved */
	LIBUSB20_REQUEST_TYPE_RESERVED = (0x03 << 5),
};

/** \ingroup misc
 * Recipient bits of the
 * \ref libusb20_control_setup::bmRequestType "bmRequestType" field in
 * control transfers. Values 4 through 31 are reserved. */
enum libusb20_request_recipient {
	/** Device */
	LIBUSB20_RECIPIENT_DEVICE = 0x00,

	/** Interface */
	LIBUSB20_RECIPIENT_INTERFACE = 0x01,

	/** Endpoint */
	LIBUSB20_RECIPIENT_ENDPOINT = 0x02,

	/** Other */
	LIBUSB20_RECIPIENT_OTHER = 0x03,
};

#define	LIBUSB20_ISO_SYNC_TYPE_MASK		0x0C

/** \ingroup desc
 * Synchronization type for isochronous endpoints. Values for bits 2:3
 * of the \ref LIBUSB20_ENDPOINT_DESC::bmAttributes "bmAttributes"
 * field in LIBUSB20_ENDPOINT_DESC.
 */
enum libusb20_iso_sync_type {
	/** No synchronization */
	LIBUSB20_ISO_SYNC_TYPE_NONE = 0,

	/** Asynchronous */
	LIBUSB20_ISO_SYNC_TYPE_ASYNC = 1,

	/** Adaptive */
	LIBUSB20_ISO_SYNC_TYPE_ADAPTIVE = 2,

	/** Synchronous */
	LIBUSB20_ISO_SYNC_TYPE_SYNC = 3,
};

#define	LIBUSB20_ISO_USAGE_TYPE_MASK 0x30

/** \ingroup desc
 * Usage type for isochronous endpoints. Values for bits 4:5 of the
 * \ref LIBUSB20_ENDPOINT_DESC::bmAttributes "bmAttributes" field in
 * LIBUSB20_ENDPOINT_DESC.
 */
enum libusb20_iso_usage_type {
	/** Data endpoint */
	LIBUSB20_ISO_USAGE_TYPE_DATA = 0,

	/** Feedback endpoint */
	LIBUSB20_ISO_USAGE_TYPE_FEEDBACK = 1,

	/** Implicit feedback Data endpoint */
	LIBUSB20_ISO_USAGE_TYPE_IMPLICIT = 2,
};

struct libusb20_endpoint {
	struct LIBUSB20_ENDPOINT_DESC_DECODED desc;
	struct libusb20_me_struct extra;
} __aligned(sizeof(void *));

struct libusb20_interface {
	struct LIBUSB20_INTERFACE_DESC_DECODED desc;
	struct libusb20_me_struct extra;
	struct libusb20_interface *altsetting;
	struct libusb20_endpoint *endpoints;
	uint8_t	num_altsetting;
	uint8_t	num_endpoints;
} __aligned(sizeof(void *));

struct libusb20_config {
	struct LIBUSB20_CONFIG_DESC_DECODED desc;
	struct libusb20_me_struct extra;
	struct libusb20_interface *interface;
	uint8_t	num_interface;
} __aligned(sizeof(void *));

uint8_t	libusb20_me_get_1(const struct libusb20_me_struct *ie, uint16_t offset);
uint16_t libusb20_me_get_2(const struct libusb20_me_struct *ie, uint16_t offset);
uint16_t libusb20_me_encode(void *ptr, uint16_t len, const void *pd);
uint16_t libusb20_me_decode(const void *ptr, uint16_t len, void *pd);
const uint8_t *libusb20_desc_foreach(const struct libusb20_me_struct *pdesc, const uint8_t *psubdesc);
struct libusb20_config *libusb20_parse_config_desc(const void *config_desc);

#if 0
{					/* style */
#endif
#ifdef __cplusplus
}

#endif

#endif					/* _LIBUSB20_DESC_H_ */
