/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
 * Copyright (c) 1998 The NetBSD Foundation, Inc. All rights reserved.
 * Copyright (c) 1998 Lennart Augustsson. All rights reserved.
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

#ifndef _USB_HID_H_
#define	_USB_HID_H_

#ifndef USB_GLOBAL_INCLUDE_FILE
#include <dev/usb/usb_endian.h>
#endif

#define	UR_GET_HID_DESCRIPTOR	0x06
#define	UDESC_HID		0x21
#define	UDESC_REPORT		0x22
#define	UDESC_PHYSICAL		0x23
#define	UR_SET_HID_DESCRIPTOR	0x07
#define	UR_GET_REPORT		0x01
#define	UR_SET_REPORT		0x09
#define	UR_GET_IDLE		0x02
#define	UR_SET_IDLE		0x0a
#define	UR_GET_PROTOCOL		0x03
#define	UR_SET_PROTOCOL		0x0b

struct usb_hid_descriptor {
	uByte	bLength;
	uByte	bDescriptorType;
	uWord	bcdHID;
	uByte	bCountryCode;
	uByte	bNumDescriptors;
	struct {
		uByte	bDescriptorType;
		uWord	wDescriptorLength;
	}	descrs[1];
} __packed;

#define	USB_HID_DESCRIPTOR_SIZE(n) (9+((n)*3))

/* Usage pages */
#define	HUP_UNDEFINED		0x0000
#define	HUP_GENERIC_DESKTOP	0x0001
#define	HUP_SIMULATION		0x0002
#define	HUP_VR_CONTROLS		0x0003
#define	HUP_SPORTS_CONTROLS	0x0004
#define	HUP_GAMING_CONTROLS	0x0005
#define	HUP_KEYBOARD		0x0007
#define	HUP_LEDS		0x0008
#define	HUP_BUTTON		0x0009
#define	HUP_ORDINALS		0x000a
#define	HUP_TELEPHONY		0x000b
#define	HUP_CONSUMER		0x000c
#define	HUP_DIGITIZERS		0x000d
#define	HUP_PHYSICAL_IFACE	0x000e
#define	HUP_UNICODE		0x0010
#define	HUP_ALPHANUM_DISPLAY	0x0014
#define	HUP_MONITOR		0x0080
#define	HUP_MONITOR_ENUM_VAL	0x0081
#define	HUP_VESA_VC		0x0082
#define	HUP_VESA_CMD		0x0083
#define	HUP_POWER		0x0084
#define	HUP_BATTERY_SYSTEM	0x0085
#define	HUP_BARCODE_SCANNER	0x008b
#define	HUP_SCALE		0x008c
#define	HUP_CAMERA_CONTROL	0x0090
#define	HUP_ARCADE		0x0091
#define	HUP_MICROSOFT		0xff00

/* Usages, generic desktop */
#define	HUG_POINTER		0x0001
#define	HUG_MOUSE		0x0002
#define	HUG_JOYSTICK		0x0004
#define	HUG_GAME_PAD		0x0005
#define	HUG_KEYBOARD		0x0006
#define	HUG_KEYPAD		0x0007
#define	HUG_X			0x0030
#define	HUG_Y			0x0031
#define	HUG_Z			0x0032
#define	HUG_RX			0x0033
#define	HUG_RY			0x0034
#define	HUG_RZ			0x0035
#define	HUG_SLIDER		0x0036
#define	HUG_DIAL		0x0037
#define	HUG_WHEEL		0x0038
#define	HUG_HAT_SWITCH		0x0039
#define	HUG_COUNTED_BUFFER	0x003a
#define	HUG_BYTE_COUNT		0x003b
#define	HUG_MOTION_WAKEUP	0x003c
#define	HUG_VX			0x0040
#define	HUG_VY			0x0041
#define	HUG_VZ			0x0042
#define	HUG_VBRX		0x0043
#define	HUG_VBRY		0x0044
#define	HUG_VBRZ		0x0045
#define	HUG_VNO			0x0046
#define	HUG_TWHEEL		0x0048	/* M$ Wireless Intellimouse Wheel */
#define	HUG_SYSTEM_CONTROL	0x0080
#define	HUG_SYSTEM_POWER_DOWN	0x0081
#define	HUG_SYSTEM_SLEEP	0x0082
#define	HUG_SYSTEM_WAKEUP	0x0083
#define	HUG_SYSTEM_CONTEXT_MENU	0x0084
#define	HUG_SYSTEM_MAIN_MENU	0x0085
#define	HUG_SYSTEM_APP_MENU	0x0086
#define	HUG_SYSTEM_MENU_HELP	0x0087
#define	HUG_SYSTEM_MENU_EXIT	0x0088
#define	HUG_SYSTEM_MENU_SELECT	0x0089
#define	HUG_SYSTEM_MENU_RIGHT	0x008a
#define	HUG_SYSTEM_MENU_LEFT	0x008b
#define	HUG_SYSTEM_MENU_UP	0x008c
#define	HUG_SYSTEM_MENU_DOWN	0x008d
#define	HUG_APPLE_EJECT		0x00b8

/* Usages Digitizers */
#define	HUD_UNDEFINED		0x0000
#define	HUD_DIGITIZER		0x0001
#define	HUD_PEN			0x0002
#define	HUD_TOUCHSCREEN		0x0004
#define	HUD_TOUCHPAD		0x0005
#define	HUD_CONFIG		0x000e
#define	HUD_FINGER		0x0022
#define	HUD_TIP_PRESSURE	0x0030
#define	HUD_BARREL_PRESSURE	0x0031
#define	HUD_IN_RANGE		0x0032
#define	HUD_TOUCH		0x0033
#define	HUD_UNTOUCH		0x0034
#define	HUD_TAP			0x0035
#define	HUD_QUALITY		0x0036
#define	HUD_DATA_VALID		0x0037
#define	HUD_TRANSDUCER_INDEX	0x0038
#define	HUD_TABLET_FKEYS	0x0039
#define	HUD_PROGRAM_CHANGE_KEYS	0x003a
#define	HUD_BATTERY_STRENGTH	0x003b
#define	HUD_INVERT		0x003c
#define	HUD_X_TILT		0x003d
#define	HUD_Y_TILT		0x003e
#define	HUD_AZIMUTH		0x003f
#define	HUD_ALTITUDE		0x0040
#define	HUD_TWIST		0x0041
#define	HUD_TIP_SWITCH		0x0042
#define	HUD_SEC_TIP_SWITCH	0x0043
#define	HUD_BARREL_SWITCH	0x0044
#define	HUD_ERASER		0x0045
#define	HUD_TABLET_PICK		0x0046
#define	HUD_CONFIDENCE		0x0047
#define	HUD_WIDTH		0x0048
#define	HUD_HEIGHT		0x0049
#define	HUD_CONTACTID		0x0051
#define	HUD_INPUT_MODE		0x0052
#define	HUD_DEVICE_INDEX	0x0053
#define	HUD_CONTACTCOUNT	0x0054
#define	HUD_CONTACT_MAX		0x0055
#define	HUD_SCAN_TIME		0x0056
#define	HUD_BUTTON_TYPE		0x0059

/* Usages, Consumer */
#define	HUC_AC_PAN		0x0238

#define	HID_USAGE2(p,u) (((p) << 16) | (u))

#define	UHID_INPUT_REPORT 0x01
#define	UHID_OUTPUT_REPORT 0x02
#define	UHID_FEATURE_REPORT 0x03

/* Bits in the input/output/feature items */
#define	HIO_CONST	0x001
#define	HIO_VARIABLE	0x002
#define	HIO_RELATIVE	0x004
#define	HIO_WRAP	0x008
#define	HIO_NONLINEAR	0x010
#define	HIO_NOPREF	0x020
#define	HIO_NULLSTATE	0x040
#define	HIO_VOLATILE	0x080
#define	HIO_BUFBYTES	0x100

/* Units of Measure */
#define	HUM_CENTIMETER	0x11
#define	HUM_RADIAN	0x12
#define	HUM_INCH	0x13
#define	HUM_DEGREE	0x14

#ifdef _KERNEL
struct usb_config_descriptor;

enum hid_kind {
	hid_input, hid_output, hid_feature, hid_collection, hid_endcollection
};

struct hid_location {
	uint32_t size;
	uint32_t count;
	uint32_t pos;
};

struct hid_item {
	/* Global */
	int32_t	_usage_page;
	int32_t	logical_minimum;
	int32_t	logical_maximum;
	int32_t	physical_minimum;
	int32_t	physical_maximum;
	int32_t	unit_exponent;
	int32_t	unit;
	int32_t	report_ID;
	/* Local */
	int32_t	usage;
	int32_t	usage_minimum;
	int32_t	usage_maximum;
	int32_t	designator_index;
	int32_t	designator_minimum;
	int32_t	designator_maximum;
	int32_t	string_index;
	int32_t	string_minimum;
	int32_t	string_maximum;
	int32_t	set_delimiter;
	/* Misc */
	int32_t	collection;
	int	collevel;
	enum hid_kind kind;
	uint32_t flags;
	/* Location */
	struct hid_location loc;
};

/* prototypes from "usb_hid.c" */

struct hid_data *hid_start_parse(const void *d, usb_size_t len, int kindset);
void	hid_end_parse(struct hid_data *s);
int	hid_get_item(struct hid_data *s, struct hid_item *h);
int	hid_report_size(const void *buf, usb_size_t len, enum hid_kind k,
	    uint8_t *id);
int	hid_locate(const void *desc, usb_size_t size, int32_t usage,
	    enum hid_kind kind, uint8_t index, struct hid_location *loc,
	    uint32_t *flags, uint8_t *id);
int32_t hid_get_data(const uint8_t *buf, usb_size_t len,
	    struct hid_location *loc);
uint32_t hid_get_data_unsigned(const uint8_t *buf, usb_size_t len,
	    struct hid_location *loc);
void hid_put_data_unsigned(uint8_t *buf, usb_size_t len,
	    struct hid_location *loc, unsigned int value);
int	hid_is_collection(const void *desc, usb_size_t size, int32_t usage);
struct usb_hid_descriptor *hid_get_descriptor_from_usb(
	    struct usb_config_descriptor *cd,
	    struct usb_interface_descriptor *id);
usb_error_t usbd_req_get_hid_desc(struct usb_device *udev, struct mtx *mtx,
	    void **descp, uint16_t *sizep, struct malloc_type *mem,
	    uint8_t iface_index);
int32_t	hid_item_resolution(struct hid_item *hi);
int	hid_is_mouse(const void *d_ptr, uint16_t d_len);
int	hid_is_keyboard(const void *d_ptr, uint16_t d_len);
#endif					/* _KERNEL */
#endif					/* _USB_HID_H_ */
