/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Huang Wen Hui
 * All rights reserved.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/selinfo.h>
#include <sys/poll.h>
#include <sys/sysctl.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbhid.h>

#include "usbdevs.h"

#define	USB_DEBUG_VAR wsp_debug
#include <dev/usb/usb_debug.h>

#include <sys/mouse.h>

#define	WSP_DRIVER_NAME "wsp"
#define	WSP_BUFFER_MAX	1024

#define	WSP_CLAMP(x,low,high) do {		\
	if ((x) < (low))			\
		(x) = (low);			\
	else if ((x) > (high))			\
		(x) = (high);			\
} while (0)

/* Tunables */
static	SYSCTL_NODE(_hw_usb, OID_AUTO, wsp, CTLFLAG_RW, 0, "USB wsp");

#ifdef USB_DEBUG
enum wsp_log_level {
	WSP_LLEVEL_DISABLED = 0,
	WSP_LLEVEL_ERROR,
	WSP_LLEVEL_DEBUG,		/* for troubleshooting */
	WSP_LLEVEL_INFO,		/* for diagnostics */
};
static int wsp_debug = WSP_LLEVEL_ERROR;/* the default is to only log errors */

SYSCTL_INT(_hw_usb_wsp, OID_AUTO, debug, CTLFLAG_RWTUN,
    &wsp_debug, WSP_LLEVEL_ERROR, "WSP debug level");
#endif					/* USB_DEBUG */

static struct wsp_tuning {
	int	scale_factor;
	int	z_factor;
	int	pressure_touch_threshold;
	int	pressure_untouch_threshold;
	int	pressure_tap_threshold;
	int	scr_hor_threshold;
	int	enable_single_tap_clicks;
}
	wsp_tuning =
{
	.scale_factor = 12,
	.z_factor = 5,
	.pressure_touch_threshold = 50,
	.pressure_untouch_threshold = 10,
	.pressure_tap_threshold = 120,
	.scr_hor_threshold = 20,
	.enable_single_tap_clicks = 1,
};

static void
wsp_runing_rangecheck(struct wsp_tuning *ptun)
{
	WSP_CLAMP(ptun->scale_factor, 1, 63);
	WSP_CLAMP(ptun->z_factor, 1, 63);
	WSP_CLAMP(ptun->pressure_touch_threshold, 1, 255);
	WSP_CLAMP(ptun->pressure_untouch_threshold, 1, 255);
	WSP_CLAMP(ptun->pressure_tap_threshold, 1, 255);
	WSP_CLAMP(ptun->scr_hor_threshold, 1, 255);
	WSP_CLAMP(ptun->enable_single_tap_clicks, 0, 1);
}

SYSCTL_INT(_hw_usb_wsp, OID_AUTO, scale_factor, CTLFLAG_RWTUN,
    &wsp_tuning.scale_factor, 0, "movement scale factor");
SYSCTL_INT(_hw_usb_wsp, OID_AUTO, z_factor, CTLFLAG_RWTUN,
    &wsp_tuning.z_factor, 0, "Z-axis scale factor");
SYSCTL_INT(_hw_usb_wsp, OID_AUTO, pressure_touch_threshold, CTLFLAG_RWTUN,
    &wsp_tuning.pressure_touch_threshold, 0, "touch pressure threshold");
SYSCTL_INT(_hw_usb_wsp, OID_AUTO, pressure_untouch_threshold, CTLFLAG_RWTUN,
    &wsp_tuning.pressure_untouch_threshold, 0, "untouch pressure threshold");
SYSCTL_INT(_hw_usb_wsp, OID_AUTO, pressure_tap_threshold, CTLFLAG_RWTUN,
    &wsp_tuning.pressure_tap_threshold, 0, "tap pressure threshold");
SYSCTL_INT(_hw_usb_wsp, OID_AUTO, scr_hor_threshold, CTLFLAG_RWTUN,
    &wsp_tuning.scr_hor_threshold, 0, "horizontal scrolling threshold");
SYSCTL_INT(_hw_usb_wsp, OID_AUTO, enable_single_tap_clicks, CTLFLAG_RWTUN,
    &wsp_tuning.enable_single_tap_clicks, 0, "enable single tap clicks");

/*
 * Some tables, structures, definitions and constant values for the
 * touchpad protocol has been copied from Linux's
 * "drivers/input/mouse/bcm5974.c" which has the following copyright
 * holders under GPLv2. All device specific code in this driver has
 * been written from scratch. The decoding algorithm is based on
 * output from FreeBSD's usbdump.
 *
 * Copyright (C) 2008      Henrik Rydberg (rydberg@euromail.se)
 * Copyright (C) 2008      Scott Shawcroft (scott.shawcroft@gmail.com)
 * Copyright (C) 2001-2004 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2005      Johannes Berg (johannes@sipsolutions.net)
 * Copyright (C) 2005      Stelian Pop (stelian@popies.net)
 * Copyright (C) 2005      Frank Arnold (frank@scirocco-5v-turbo.de)
 * Copyright (C) 2005      Peter Osterlund (petero2@telia.com)
 * Copyright (C) 2005      Michael Hanselmann (linux-kernel@hansmi.ch)
 * Copyright (C) 2006      Nicolas Boichat (nicolas@boichat.ch)
 */

/* button data structure */
struct bt_data {
	uint8_t	unknown1;		/* constant */
	uint8_t	button;			/* left button */
	uint8_t	rel_x;			/* relative x coordinate */
	uint8_t	rel_y;			/* relative y coordinate */
} __packed;

/* trackpad header types */
enum tp_type {
	TYPE1,			/* plain trackpad */
	TYPE2,			/* button integrated in trackpad */
	TYPE3,			/* additional header fields since June 2013 */
	TYPE4                   /* additional header field for pressure data */
};

/* trackpad finger data offsets, le16-aligned */
#define	FINGER_TYPE1		(13 * 2)
#define	FINGER_TYPE2		(15 * 2)
#define	FINGER_TYPE3		(19 * 2)
#define	FINGER_TYPE4		(23 * 2)

/* trackpad button data offsets */
#define	BUTTON_TYPE2		15
#define	BUTTON_TYPE3		23
#define	BUTTON_TYPE4		31

/* list of device capability bits */
#define	HAS_INTEGRATED_BUTTON	1

/* trackpad finger data block size */
#define FSIZE_TYPE1             (14 * 2)
#define FSIZE_TYPE2             (14 * 2)
#define FSIZE_TYPE3             (14 * 2)
#define FSIZE_TYPE4             (15 * 2)

/* trackpad finger header - little endian */
struct tp_header {
	uint8_t	flag;
	uint8_t	sn0;
	uint16_t wFixed0;
	uint32_t dwSn1;
	uint32_t dwFixed1;
	uint16_t wLength;
	uint8_t	nfinger;
	uint8_t	ibt;
	int16_t	wUnknown[6];
	uint8_t	q1;
	uint8_t	q2;
} __packed;

/* trackpad finger structure - little endian */
struct tp_finger {
	int16_t	origin;			/* zero when switching track finger */
	int16_t	abs_x;			/* absolute x coodinate */
	int16_t	abs_y;			/* absolute y coodinate */
	int16_t	rel_x;			/* relative x coodinate */
	int16_t	rel_y;			/* relative y coodinate */
	int16_t	tool_major;		/* tool area, major axis */
	int16_t	tool_minor;		/* tool area, minor axis */
	int16_t	orientation;		/* 16384 when point, else 15 bit angle */
	int16_t	touch_major;		/* touch area, major axis */
	int16_t	touch_minor;		/* touch area, minor axis */
	int16_t	unused[2];		/* zeros */
	int16_t pressure;		/* pressure on forcetouch touchpad */
	int16_t	multi;			/* one finger: varies, more fingers:
				 	 * constant */
} __packed;

/* trackpad finger data size, empirically at least ten fingers */
#define	MAX_FINGERS		16
#define	SIZEOF_FINGER		sizeof(struct tp_finger)
#define	SIZEOF_ALL_FINGERS	(MAX_FINGERS * SIZEOF_FINGER)

#if (WSP_BUFFER_MAX < ((MAX_FINGERS * FSIZE_TYPE4) + FINGER_TYPE4))
#error "WSP_BUFFER_MAX is too small"
#endif

enum {
	WSP_FLAG_WELLSPRING1,
	WSP_FLAG_WELLSPRING2,
	WSP_FLAG_WELLSPRING3,
	WSP_FLAG_WELLSPRING4,
	WSP_FLAG_WELLSPRING4A,
	WSP_FLAG_WELLSPRING5,
	WSP_FLAG_WELLSPRING6A,
	WSP_FLAG_WELLSPRING6,
	WSP_FLAG_WELLSPRING5A,
	WSP_FLAG_WELLSPRING7,
	WSP_FLAG_WELLSPRING7A,
	WSP_FLAG_WELLSPRING8,
	WSP_FLAG_WELLSPRING9,
	WSP_FLAG_MAX,
};

/* device-specific configuration */
struct wsp_dev_params {
	uint8_t	caps;			/* device capability bitmask */
	uint8_t	tp_type;		/* type of trackpad interface */
	uint8_t	tp_button;		/* offset to button data */
	uint8_t	tp_offset;		/* offset to trackpad finger data */
	uint8_t tp_fsize;		/* bytes in single finger block */
	uint8_t tp_delta;		/* offset from header to finger struct */
	uint8_t iface_index;
	uint8_t um_size;		/* usb control message length */
	uint8_t um_req_val;		/* usb control message value */
	uint8_t um_req_idx;		/* usb control message index */
	uint8_t um_switch_idx;		/* usb control message mode switch index */
	uint8_t um_switch_on;		/* usb control message mode switch on */
	uint8_t um_switch_off;		/* usb control message mode switch off */
};

static const struct wsp_dev_params wsp_dev_params[WSP_FLAG_MAX] = {
	[WSP_FLAG_WELLSPRING1] = {
		.caps = 0,
		.tp_type = TYPE1,
		.tp_button = 0,
		.tp_offset = FINGER_TYPE1,
		.tp_fsize = FSIZE_TYPE1,
		.tp_delta = 0,
		.iface_index = 0,
		.um_size = 8,
		.um_req_val = 0x03,
		.um_req_idx = 0x00,
		.um_switch_idx = 0,
		.um_switch_on = 0x01,
		.um_switch_off = 0x08,
	},
	[WSP_FLAG_WELLSPRING2] = {
		.caps = 0,
		.tp_type = TYPE1,
		.tp_button = 0,
		.tp_offset = FINGER_TYPE1,
		.tp_fsize = FSIZE_TYPE1,
		.tp_delta = 0,
		.iface_index = 0,
		.um_size = 8,
		.um_req_val = 0x03,
		.um_req_idx = 0x00,
		.um_switch_idx = 0,
		.um_switch_on = 0x01,
		.um_switch_off = 0x08,
	},
	[WSP_FLAG_WELLSPRING3] = {
		.caps = HAS_INTEGRATED_BUTTON,
		.tp_type = TYPE2,
		.tp_button = BUTTON_TYPE2,
		.tp_offset = FINGER_TYPE2,
		.tp_fsize = FSIZE_TYPE2,
		.tp_delta = 0,
		.iface_index = 0,
		.um_size = 8,
		.um_req_val = 0x03,
		.um_req_idx = 0x00,
		.um_switch_idx = 0,
		.um_switch_on = 0x01,
		.um_switch_off = 0x08,
	},
	[WSP_FLAG_WELLSPRING4] = {
		.caps = HAS_INTEGRATED_BUTTON,
		.tp_type = TYPE2,
		.tp_button = BUTTON_TYPE2,
		.tp_offset = FINGER_TYPE2,
		.tp_fsize = FSIZE_TYPE2,
		.tp_delta = 0,
		.iface_index = 0,
		.um_size = 8,
		.um_req_val = 0x03,
		.um_req_idx = 0x00,
		.um_switch_idx = 0,
		.um_switch_on = 0x01,
		.um_switch_off = 0x08,
	},
	[WSP_FLAG_WELLSPRING4A] = {
		.caps = HAS_INTEGRATED_BUTTON,
		.tp_type = TYPE2,
		.tp_button = BUTTON_TYPE2,
		.tp_offset = FINGER_TYPE2,
		.tp_fsize = FSIZE_TYPE2,
		.tp_delta = 0,
		.iface_index = 0,
		.um_size = 8,
		.um_req_val = 0x03,
		.um_req_idx = 0x00,
		.um_switch_idx = 0,
		.um_switch_on = 0x01,
		.um_switch_off = 0x08,
	},
	[WSP_FLAG_WELLSPRING5] = {
		.caps = HAS_INTEGRATED_BUTTON,
		.tp_type = TYPE2,
		.tp_button = BUTTON_TYPE2,
		.tp_offset = FINGER_TYPE2,
		.tp_fsize = FSIZE_TYPE2,
		.tp_delta = 0,
		.iface_index = 0,
		.um_size = 8,
		.um_req_val = 0x03,
		.um_req_idx = 0x00,
		.um_switch_idx = 0,
		.um_switch_on = 0x01,
		.um_switch_off = 0x08,
	},
	[WSP_FLAG_WELLSPRING6] = {
		.caps = HAS_INTEGRATED_BUTTON,
		.tp_type = TYPE2,
		.tp_button = BUTTON_TYPE2,
		.tp_offset = FINGER_TYPE2,
		.tp_fsize = FSIZE_TYPE2,
		.tp_delta = 0,
		.iface_index = 0,
		.um_size = 8,
		.um_req_val = 0x03,
		.um_req_idx = 0x00,
		.um_switch_idx = 0,
		.um_switch_on = 0x01,
		.um_switch_off = 0x08,
	},
	[WSP_FLAG_WELLSPRING5A] = {
		.caps = HAS_INTEGRATED_BUTTON,
		.tp_type = TYPE2,
		.tp_button = BUTTON_TYPE2,
		.tp_offset = FINGER_TYPE2,
		.tp_fsize = FSIZE_TYPE2,
		.tp_delta = 0,
		.iface_index = 0,
		.um_size = 8,
		.um_req_val = 0x03,
		.um_req_idx = 0x00,
		.um_switch_idx = 0,
		.um_switch_on = 0x01,
		.um_switch_off = 0x08,
	},
	[WSP_FLAG_WELLSPRING6A] = {
		.caps = HAS_INTEGRATED_BUTTON,
		.tp_type = TYPE2,
		.tp_button = BUTTON_TYPE2,
		.tp_offset = FINGER_TYPE2,
		.tp_fsize = FSIZE_TYPE2,
		.tp_delta = 0,
		.um_size = 8,
		.um_req_val = 0x03,
		.um_req_idx = 0x00,
		.um_switch_idx = 0,
		.um_switch_on = 0x01,
		.um_switch_off = 0x08,
	},
	[WSP_FLAG_WELLSPRING7] = {
		.caps = HAS_INTEGRATED_BUTTON,
		.tp_type = TYPE2,
		.tp_button = BUTTON_TYPE2,
		.tp_offset = FINGER_TYPE2,
		.tp_fsize = FSIZE_TYPE2,
		.tp_delta = 0,
		.iface_index = 0,
		.um_size = 8,
		.um_req_val = 0x03,
		.um_req_idx = 0x00,
		.um_switch_idx = 0,
		.um_switch_on = 0x01,
		.um_switch_off = 0x08,
	},
	[WSP_FLAG_WELLSPRING7A] = {
		.caps = HAS_INTEGRATED_BUTTON,
		.tp_type = TYPE2,
		.tp_button = BUTTON_TYPE2,
		.tp_offset = FINGER_TYPE2,
		.tp_fsize = FSIZE_TYPE2,
		.tp_delta = 0,
		.iface_index = 0,
		.um_size = 8,
		.um_req_val = 0x03,
		.um_req_idx = 0x00,
		.um_switch_idx = 0,
		.um_switch_on = 0x01,
		.um_switch_off = 0x08,
	},
	[WSP_FLAG_WELLSPRING8] = {
		.caps = HAS_INTEGRATED_BUTTON,
		.tp_type = TYPE3,
		.tp_button = BUTTON_TYPE3,
		.tp_offset = FINGER_TYPE3,
		.tp_fsize = FSIZE_TYPE3,
		.tp_delta = 0,
		.iface_index = 0,
		.um_size = 8,
		.um_req_val = 0x03,
		.um_req_idx = 0x00,
		.um_switch_idx = 0,
		.um_switch_on = 0x01,
		.um_switch_off = 0x08,
	},
	[WSP_FLAG_WELLSPRING9] = {
		.caps = HAS_INTEGRATED_BUTTON,
		.tp_type = TYPE4,
		.tp_button = BUTTON_TYPE4,
		.tp_offset = FINGER_TYPE4,
		.tp_fsize = FSIZE_TYPE4,
		.tp_delta = 2,
		.iface_index = 2,
		.um_size = 2,
		.um_req_val = 0x03,
		.um_req_idx = 0x02,
		.um_switch_idx = 1,
		.um_switch_on = 0x01,
		.um_switch_off = 0x00,
	},
};

#define	WSP_DEV(v,p,i) { USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, i) }

static const STRUCT_USB_HOST_ID wsp_devs[] = {
	/* MacbookAir1.1 */
	WSP_DEV(APPLE, WELLSPRING_ANSI, WSP_FLAG_WELLSPRING1),
	WSP_DEV(APPLE, WELLSPRING_ISO, WSP_FLAG_WELLSPRING1),
	WSP_DEV(APPLE, WELLSPRING_JIS, WSP_FLAG_WELLSPRING1),

	/* MacbookProPenryn, aka wellspring2 */
	WSP_DEV(APPLE, WELLSPRING2_ANSI, WSP_FLAG_WELLSPRING2),
	WSP_DEV(APPLE, WELLSPRING2_ISO, WSP_FLAG_WELLSPRING2),
	WSP_DEV(APPLE, WELLSPRING2_JIS, WSP_FLAG_WELLSPRING2),

	/* Macbook5,1 (unibody), aka wellspring3 */
	WSP_DEV(APPLE, WELLSPRING3_ANSI, WSP_FLAG_WELLSPRING3),
	WSP_DEV(APPLE, WELLSPRING3_ISO, WSP_FLAG_WELLSPRING3),
	WSP_DEV(APPLE, WELLSPRING3_JIS, WSP_FLAG_WELLSPRING3),

	/* MacbookAir3,2 (unibody), aka wellspring4 */
	WSP_DEV(APPLE, WELLSPRING4_ANSI, WSP_FLAG_WELLSPRING4),
	WSP_DEV(APPLE, WELLSPRING4_ISO, WSP_FLAG_WELLSPRING4),
	WSP_DEV(APPLE, WELLSPRING4_JIS, WSP_FLAG_WELLSPRING4),

	/* MacbookAir3,1 (unibody), aka wellspring4 */
	WSP_DEV(APPLE, WELLSPRING4A_ANSI, WSP_FLAG_WELLSPRING4A),
	WSP_DEV(APPLE, WELLSPRING4A_ISO, WSP_FLAG_WELLSPRING4A),
	WSP_DEV(APPLE, WELLSPRING4A_JIS, WSP_FLAG_WELLSPRING4A),

	/* Macbook8 (unibody, March 2011) */
	WSP_DEV(APPLE, WELLSPRING5_ANSI, WSP_FLAG_WELLSPRING5),
	WSP_DEV(APPLE, WELLSPRING5_ISO, WSP_FLAG_WELLSPRING5),
	WSP_DEV(APPLE, WELLSPRING5_JIS, WSP_FLAG_WELLSPRING5),

	/* MacbookAir4,1 (unibody, July 2011) */
	WSP_DEV(APPLE, WELLSPRING6A_ANSI, WSP_FLAG_WELLSPRING6A),
	WSP_DEV(APPLE, WELLSPRING6A_ISO, WSP_FLAG_WELLSPRING6A),
	WSP_DEV(APPLE, WELLSPRING6A_JIS, WSP_FLAG_WELLSPRING6A),

	/* MacbookAir4,2 (unibody, July 2011) */
	WSP_DEV(APPLE, WELLSPRING6_ANSI, WSP_FLAG_WELLSPRING6),
	WSP_DEV(APPLE, WELLSPRING6_ISO, WSP_FLAG_WELLSPRING6),
	WSP_DEV(APPLE, WELLSPRING6_JIS, WSP_FLAG_WELLSPRING6),

	/* Macbook8,2 (unibody) */
	WSP_DEV(APPLE, WELLSPRING5A_ANSI, WSP_FLAG_WELLSPRING5A),
	WSP_DEV(APPLE, WELLSPRING5A_ISO, WSP_FLAG_WELLSPRING5A),
	WSP_DEV(APPLE, WELLSPRING5A_JIS, WSP_FLAG_WELLSPRING5A),

	/* MacbookPro10,1 (unibody, June 2012) */
	/* MacbookPro11,1-3 (unibody, June 2013) */
	WSP_DEV(APPLE, WELLSPRING7_ANSI, WSP_FLAG_WELLSPRING7),
	WSP_DEV(APPLE, WELLSPRING7_ISO, WSP_FLAG_WELLSPRING7),
	WSP_DEV(APPLE, WELLSPRING7_JIS, WSP_FLAG_WELLSPRING7),

	/* MacbookPro10,2 (unibody, October 2012) */
	WSP_DEV(APPLE, WELLSPRING7A_ANSI, WSP_FLAG_WELLSPRING7A),
	WSP_DEV(APPLE, WELLSPRING7A_ISO, WSP_FLAG_WELLSPRING7A),
	WSP_DEV(APPLE, WELLSPRING7A_JIS, WSP_FLAG_WELLSPRING7A),

	/* MacbookAir6,2 (unibody, June 2013) */
	WSP_DEV(APPLE, WELLSPRING8_ANSI, WSP_FLAG_WELLSPRING8),
	WSP_DEV(APPLE, WELLSPRING8_ISO, WSP_FLAG_WELLSPRING8),
	WSP_DEV(APPLE, WELLSPRING8_JIS, WSP_FLAG_WELLSPRING8),

	/* MacbookPro12,1 MacbookPro11,4 */
	WSP_DEV(APPLE, WELLSPRING9_ANSI, WSP_FLAG_WELLSPRING9),
	WSP_DEV(APPLE, WELLSPRING9_ISO, WSP_FLAG_WELLSPRING9),
	WSP_DEV(APPLE, WELLSPRING9_JIS, WSP_FLAG_WELLSPRING9),
};

#define	WSP_FIFO_BUF_SIZE	 8	/* bytes */
#define	WSP_FIFO_QUEUE_MAXLEN	50	/* units */

enum {
	WSP_INTR_DT,
	WSP_N_TRANSFER,
};

struct wsp_softc {
	struct usb_device *sc_usb_device;
	struct mtx sc_mutex;		/* for synchronization */
	struct usb_xfer *sc_xfer[WSP_N_TRANSFER];
	struct usb_fifo_sc sc_fifo;

	const struct wsp_dev_params *sc_params;	/* device configuration */

	mousehw_t sc_hw;
	mousemode_t sc_mode;
	u_int	sc_pollrate;
	mousestatus_t sc_status;
	u_int	sc_state;
#define	WSP_ENABLED	       0x01

	struct tp_finger *index[MAX_FINGERS];	/* finger index data */
	int16_t	pos_x[MAX_FINGERS];	/* position array */
	int16_t	pos_y[MAX_FINGERS];	/* position array */
	u_int	sc_touch;		/* touch status */
#define	WSP_UNTOUCH		0x00
#define	WSP_FIRST_TOUCH		0x01
#define	WSP_SECOND_TOUCH	0x02
#define	WSP_TOUCHING		0x04
	int16_t	pre_pos_x;		/* previous position array */
	int16_t	pre_pos_y;		/* previous position array */
	int	dx_sum;			/* x axis cumulative movement */
	int	dy_sum;			/* y axis cumulative movement */
	int	dz_sum;			/* z axis cumulative movement */
	int	dz_count;
#define	WSP_DZ_MAX_COUNT	32
	int	dt_sum;			/* T-axis cumulative movement */
	int	rdx;			/* x axis remainder of divide by scale_factor */
	int	rdy;			/* y axis remainder of divide by scale_factor */
	int	rdz;			/* z axis remainder of divide by scale_factor */
	int	tp_datalen;
	uint8_t o_ntouch;		/* old touch finger status */
	uint8_t	finger;			/* 0 or 1 *, check which finger moving */
	uint16_t intr_count;
#define	WSP_TAP_THRESHOLD	3
#define	WSP_TAP_MAX_COUNT	20
	int	distance;		/* the distance of 2 fingers */
#define	MAX_DISTANCE		2500	/* the max allowed distance */
	uint8_t	ibtn;			/* button status in tapping */
	uint8_t	ntaps;			/* finger status in tapping */
	uint8_t	scr_mode;		/* scroll status in movement */
#define	WSP_SCR_NONE		0
#define	WSP_SCR_VER		1
#define	WSP_SCR_HOR		2
	uint8_t tp_data[WSP_BUFFER_MAX] __aligned(4);		/* trackpad transferred data */
};

/*
 * function prototypes
 */
static usb_fifo_cmd_t wsp_start_read;
static usb_fifo_cmd_t wsp_stop_read;
static usb_fifo_open_t wsp_open;
static usb_fifo_close_t wsp_close;
static usb_fifo_ioctl_t wsp_ioctl;

static struct usb_fifo_methods wsp_fifo_methods = {
	.f_open = &wsp_open,
	.f_close = &wsp_close,
	.f_ioctl = &wsp_ioctl,
	.f_start_read = &wsp_start_read,
	.f_stop_read = &wsp_stop_read,
	.basename[0] = WSP_DRIVER_NAME,
};

/* device initialization and shutdown */
static int wsp_enable(struct wsp_softc *sc);
static void wsp_disable(struct wsp_softc *sc);

/* updating fifo */
static void wsp_reset_buf(struct wsp_softc *sc);
static void wsp_add_to_queue(struct wsp_softc *, int, int, int, uint32_t);

/* Device methods. */
static device_probe_t wsp_probe;
static device_attach_t wsp_attach;
static device_detach_t wsp_detach;
static usb_callback_t wsp_intr_callback;

static const struct usb_config wsp_config[WSP_N_TRANSFER] = {
	[WSP_INTR_DT] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {
			.pipe_bof = 0,
			.short_xfer_ok = 1,
		},
		.bufsize = WSP_BUFFER_MAX,
		.callback = &wsp_intr_callback,
	},
};

static usb_error_t
wsp_set_device_mode(struct wsp_softc *sc, uint8_t on)
{
	const struct wsp_dev_params *params = sc->sc_params;
	uint8_t	mode_bytes[8];
	usb_error_t err;

	/* Type 3 does not require a mode switch */
	if (params->tp_type == TYPE3)
		return 0;

	err = usbd_req_get_report(sc->sc_usb_device, NULL,
	    mode_bytes, params->um_size, params->iface_index,
	    params->um_req_val, params->um_req_idx);

	if (err != USB_ERR_NORMAL_COMPLETION) {
		DPRINTF("Failed to read device mode (%d)\n", err);
		return (err);
	}

	/*
	 * XXX Need to wait at least 250ms for hardware to get
	 * ready. The device mode handling appears to be handled
	 * asynchronously and we should not issue these commands too
	 * quickly.
	 */
	pause("WHW", hz / 4);

	mode_bytes[params->um_switch_idx] = 
	    on ? params->um_switch_on : params->um_switch_off;

	return (usbd_req_set_report(sc->sc_usb_device, NULL,
	    mode_bytes, params->um_size, params->iface_index, 
	    params->um_req_val, params->um_req_idx));
}

static int
wsp_enable(struct wsp_softc *sc)
{
	/* reset status */
	memset(&sc->sc_status, 0, sizeof(sc->sc_status));
	sc->sc_state |= WSP_ENABLED;

	DPRINTFN(WSP_LLEVEL_INFO, "enabled wsp\n");
	return (0);
}

static void
wsp_disable(struct wsp_softc *sc)
{
	sc->sc_state &= ~WSP_ENABLED;
	DPRINTFN(WSP_LLEVEL_INFO, "disabled wsp\n");
}

static int
wsp_probe(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);
	struct usb_interface_descriptor *id;
	struct usb_interface *iface;
	uint8_t i;

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);

	/* figure out first interface matching */
	for (i = 1;; i++) {
		iface = usbd_get_iface(uaa->device, i);
		if (iface == NULL || i == 3)
			return (ENXIO);
		id = iface->idesc;
		if ((id == NULL) ||
		    (id->bInterfaceClass != UICLASS_HID) ||
		    (id->bInterfaceProtocol != 0 &&
		    id->bInterfaceProtocol != UIPROTO_MOUSE))
			continue;
		break;
	}
	/* check if we are attaching to the first match */
	if (uaa->info.bIfaceIndex != i)
		return (ENXIO);
	return (usbd_lookup_id_by_uaa(wsp_devs, sizeof(wsp_devs), uaa));
}

static int
wsp_attach(device_t dev)
{
	struct wsp_softc *sc = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	usb_error_t err;
	void *d_ptr = NULL;
	uint16_t d_len;

	DPRINTFN(WSP_LLEVEL_INFO, "sc=%p\n", sc);

	/* Get HID descriptor */
	err = usbd_req_get_hid_desc(uaa->device, NULL, &d_ptr,
	    &d_len, M_TEMP, uaa->info.bIfaceIndex);

	if (err == USB_ERR_NORMAL_COMPLETION) {
		/* Get HID report descriptor length */
		sc->tp_datalen = hid_report_size(d_ptr, d_len, hid_input, NULL);
		free(d_ptr, M_TEMP);

		if (sc->tp_datalen <= 0 || sc->tp_datalen > WSP_BUFFER_MAX) {
			DPRINTF("Invalid datalength or too big "
			    "datalength: %d\n", sc->tp_datalen);
			return (ENXIO);
		}
	} else {
		return (ENXIO);
	}

	sc->sc_usb_device = uaa->device;

	/* get device specific configuration */
	sc->sc_params = wsp_dev_params + USB_GET_DRIVER_INFO(uaa);

	/*
	 * By default the touchpad behaves like a HID device, sending
	 * packets with reportID = 8. Such reports contain only
	 * limited information. They encode movement deltas and button
	 * events, but do not include data from the pressure
	 * sensors. The device input mode can be switched from HID
	 * reports to raw sensor data using vendor-specific USB
	 * control commands:
	 */

	/*
	 * During re-enumeration of the device we need to force the
	 * device back into HID mode before switching it to RAW
	 * mode. Else the device does not work like expected.
	 */
	err = wsp_set_device_mode(sc, 0);
	if (err != USB_ERR_NORMAL_COMPLETION) {
		DPRINTF("Failed to set mode to HID MODE (%d)\n", err);
		return (ENXIO);
	}

	err = wsp_set_device_mode(sc, 1);
	if (err != USB_ERR_NORMAL_COMPLETION) {
		DPRINTF("failed to set mode to RAW MODE (%d)\n", err);
		return (ENXIO);
	}

	mtx_init(&sc->sc_mutex, "wspmtx", NULL, MTX_DEF | MTX_RECURSE);

	err = usbd_transfer_setup(uaa->device,
	    &uaa->info.bIfaceIndex, sc->sc_xfer, wsp_config,
	    WSP_N_TRANSFER, sc, &sc->sc_mutex);
	if (err) {
		DPRINTF("error=%s\n", usbd_errstr(err));
		goto detach;
	}
	if (usb_fifo_attach(sc->sc_usb_device, sc, &sc->sc_mutex,
	    &wsp_fifo_methods, &sc->sc_fifo,
	    device_get_unit(dev), -1, uaa->info.bIfaceIndex,
	    UID_ROOT, GID_OPERATOR, 0644)) {
		goto detach;
	}
	device_set_usb_desc(dev);

	sc->sc_hw.buttons = 3;
	sc->sc_hw.iftype = MOUSE_IF_USB;
	sc->sc_hw.type = MOUSE_PAD;
	sc->sc_hw.model = MOUSE_MODEL_GENERIC;
	sc->sc_mode.protocol = MOUSE_PROTO_MSC;
	sc->sc_mode.rate = -1;
	sc->sc_mode.resolution = MOUSE_RES_UNKNOWN;
	sc->sc_mode.packetsize = MOUSE_MSC_PACKETSIZE;
	sc->sc_mode.syncmask[0] = MOUSE_MSC_SYNCMASK;
	sc->sc_mode.syncmask[1] = MOUSE_MSC_SYNC;

	sc->sc_touch = WSP_UNTOUCH;
	sc->scr_mode = WSP_SCR_NONE;

	return (0);

detach:
	wsp_detach(dev);
	return (ENOMEM);
}

static int
wsp_detach(device_t dev)
{
	struct wsp_softc *sc = device_get_softc(dev);

	(void) wsp_set_device_mode(sc, 0);

	mtx_lock(&sc->sc_mutex);
	if (sc->sc_state & WSP_ENABLED)
		wsp_disable(sc);
	mtx_unlock(&sc->sc_mutex);

	usb_fifo_detach(&sc->sc_fifo);

	usbd_transfer_unsetup(sc->sc_xfer, WSP_N_TRANSFER);

	mtx_destroy(&sc->sc_mutex);

	return (0);
}

static void
wsp_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct wsp_softc *sc = usbd_xfer_softc(xfer);
	const struct wsp_dev_params *params = sc->sc_params;
	struct usb_page_cache *pc;
	struct tp_finger *f;
	struct tp_header *h;
	struct wsp_tuning tun = wsp_tuning;
	int ntouch = 0;			/* the finger number in touch */
	int ibt = 0;			/* button status */
	int dx = 0;
	int dy = 0;
	int dz = 0;
	int rdx = 0;
	int rdy = 0;
	int rdz = 0;
	int len;
	int i;

	wsp_runing_rangecheck(&tun);

	if (sc->dz_count == 0)
		sc->dz_count = WSP_DZ_MAX_COUNT;

	usbd_xfer_status(xfer, &len, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:

		/* copy out received data */
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, sc->tp_data, len);

		if ((len < params->tp_offset + params->tp_fsize) ||
		    ((len - params->tp_offset) % params->tp_fsize) != 0) {
			DPRINTFN(WSP_LLEVEL_INFO, "Invalid length: %d, %x, %x\n",
			    len, sc->tp_data[0], sc->tp_data[1]);
			goto tr_setup;
		}

		if (len < sc->tp_datalen) {
			/* make sure we don't process old data */
			memset(sc->tp_data + len, 0, sc->tp_datalen - len);
		}

		h = (struct tp_header *)(sc->tp_data);

		if (params->tp_type >= TYPE2) {
			ibt = sc->tp_data[params->tp_button];
			ntouch = sc->tp_data[params->tp_button - 1];
		}
		/* range check */
		if (ntouch < 0)
			ntouch = 0;
		else if (ntouch > MAX_FINGERS)
			ntouch = MAX_FINGERS;

		for (i = 0; i != ntouch; i++) {
			f = (struct tp_finger *)(sc->tp_data + params->tp_offset + params->tp_delta + i * params->tp_fsize);
			/* swap endianness, if any */
			if (le16toh(0x1234) != 0x1234) {
				f->origin = le16toh((uint16_t)f->origin);
				f->abs_x = le16toh((uint16_t)f->abs_x);
				f->abs_y = le16toh((uint16_t)f->abs_y);
				f->rel_x = le16toh((uint16_t)f->rel_x);
				f->rel_y = le16toh((uint16_t)f->rel_y);
				f->tool_major = le16toh((uint16_t)f->tool_major);
				f->tool_minor = le16toh((uint16_t)f->tool_minor);
				f->orientation = le16toh((uint16_t)f->orientation);
				f->touch_major = le16toh((uint16_t)f->touch_major);
				f->touch_minor = le16toh((uint16_t)f->touch_minor);
				f->pressure = le16toh((uint16_t)f->pressure);
				f->multi = le16toh((uint16_t)f->multi);
			}
			DPRINTFN(WSP_LLEVEL_INFO, 
			    "[%d]ibt=%d, taps=%d, o=%4d, ax=%5d, ay=%5d, "
			    "rx=%5d, ry=%5d, tlmaj=%4d, tlmin=%4d, ot=%4x, "
			    "tchmaj=%4d, tchmin=%4d, presure=%4d, m=%4x\n",
			    i, ibt, ntouch, f->origin, f->abs_x, f->abs_y,
			    f->rel_x, f->rel_y, f->tool_major, f->tool_minor, f->orientation,
			    f->touch_major, f->touch_minor, f->pressure, f->multi);
			sc->pos_x[i] = f->abs_x;
			sc->pos_y[i] = -f->abs_y;
			sc->index[i] = f;
		}

		sc->sc_status.flags &= ~MOUSE_POSCHANGED;
		sc->sc_status.flags &= ~MOUSE_STDBUTTONSCHANGED;
		sc->sc_status.obutton = sc->sc_status.button;
		sc->sc_status.button = 0;

		if (ibt != 0) {
			if ((params->caps & HAS_INTEGRATED_BUTTON) && ntouch == 2)
				sc->sc_status.button |= MOUSE_BUTTON3DOWN;
			else if ((params->caps & HAS_INTEGRATED_BUTTON) && ntouch == 3)
				sc->sc_status.button |= MOUSE_BUTTON2DOWN;
			else 
				sc->sc_status.button |= MOUSE_BUTTON1DOWN;
			sc->ibtn = 1;
		}
		sc->intr_count++;

		if (sc->ntaps < ntouch) {
			switch (ntouch) {
			case 1:
				if (sc->index[0]->touch_major > tun.pressure_tap_threshold &&
				    sc->index[0]->tool_major <= 1200)
					sc->ntaps = 1;
				break;
			case 2:
				if (sc->index[0]->touch_major > tun.pressure_tap_threshold-30 &&
				    sc->index[1]->touch_major > tun.pressure_tap_threshold-30)
					sc->ntaps = 2;
				break;
			case 3:
				if (sc->index[0]->touch_major > tun.pressure_tap_threshold-40 &&
				    sc->index[1]->touch_major > tun.pressure_tap_threshold-40 &&
				    sc->index[2]->touch_major > tun.pressure_tap_threshold-40)
					sc->ntaps = 3;
				break;
			default:
				break;
			}
		}
		if (ntouch == 2) {
			sc->distance = max(sc->distance, max(
			    abs(sc->pos_x[0] - sc->pos_x[1]),
			    abs(sc->pos_y[0] - sc->pos_y[1])));
		}
		if (sc->index[0]->touch_major < tun.pressure_untouch_threshold &&
		    sc->sc_status.button == 0) {
			sc->sc_touch = WSP_UNTOUCH;
			if (sc->intr_count < WSP_TAP_MAX_COUNT &&
			    sc->intr_count > WSP_TAP_THRESHOLD &&
			    sc->ntaps && sc->ibtn == 0) {
				/*
				 * Add a pair of events (button-down and
				 * button-up).
				 */
				switch (sc->ntaps) {
				case 1:
					if (!(params->caps & HAS_INTEGRATED_BUTTON) || tun.enable_single_tap_clicks) {
						wsp_add_to_queue(sc, 0, 0, 0, MOUSE_BUTTON1DOWN);
						DPRINTFN(WSP_LLEVEL_INFO, "LEFT CLICK!\n");
					}
					break;
				case 2:
					DPRINTFN(WSP_LLEVEL_INFO, "sum_x=%5d, sum_y=%5d\n",
					    sc->dx_sum, sc->dy_sum);
					if (sc->distance < MAX_DISTANCE && abs(sc->dx_sum) < 5 &&
					    abs(sc->dy_sum) < 5) {
						wsp_add_to_queue(sc, 0, 0, 0, MOUSE_BUTTON3DOWN);
						DPRINTFN(WSP_LLEVEL_INFO, "RIGHT CLICK!\n");
					}
					break;
				case 3:
					wsp_add_to_queue(sc, 0, 0, 0, MOUSE_BUTTON2DOWN);
					break;
				default:
					/* we don't handle taps of more than three fingers */
					break;
				}
				wsp_add_to_queue(sc, 0, 0, 0, 0);	/* button release */
			}
			if ((sc->dt_sum / tun.scr_hor_threshold) != 0 &&
			    sc->ntaps == 2 && sc->scr_mode == WSP_SCR_HOR) {

				/*
				 * translate T-axis into button presses
				 * until further
				 */
				if (sc->dt_sum > 0)
					wsp_add_to_queue(sc, 0, 0, 0, 1UL << 3);
				else if (sc->dt_sum < 0)
					wsp_add_to_queue(sc, 0, 0, 0, 1UL << 4);
			}
			sc->dz_count = WSP_DZ_MAX_COUNT;
			sc->dz_sum = 0;
			sc->intr_count = 0;
			sc->ibtn = 0;
			sc->ntaps = 0;
			sc->finger = 0;
			sc->distance = 0;
			sc->dt_sum = 0;
			sc->dx_sum = 0;
			sc->dy_sum = 0;
			sc->rdx = 0;
			sc->rdy = 0;
			sc->rdz = 0;
			sc->scr_mode = WSP_SCR_NONE;
		} else if (sc->index[0]->touch_major >= tun.pressure_touch_threshold &&
		    sc->sc_touch == WSP_UNTOUCH) {	/* ignore first touch */
			sc->sc_touch = WSP_FIRST_TOUCH;
		} else if (sc->index[0]->touch_major >= tun.pressure_touch_threshold &&
		    sc->sc_touch == WSP_FIRST_TOUCH) {	/* ignore second touch */
			sc->sc_touch = WSP_SECOND_TOUCH;
			DPRINTFN(WSP_LLEVEL_INFO, "Fist pre_x=%5d, pre_y=%5d\n",
			    sc->pre_pos_x, sc->pre_pos_y);
		} else {
			if (sc->sc_touch == WSP_SECOND_TOUCH)
				sc->sc_touch = WSP_TOUCHING;

			if (ntouch != 0 &&
			    sc->index[0]->touch_major >= tun.pressure_touch_threshold) {
				dx = sc->pos_x[0] - sc->pre_pos_x;
				dy = sc->pos_y[0] - sc->pre_pos_y;

				/* Ignore movement during button is releasing */
				if (sc->ibtn != 0 && sc->sc_status.button == 0)
					dx = dy = 0;

				/* Ignore movement if ntouch changed */
				if (sc->o_ntouch != ntouch)
					dx = dy = 0;

				/* Ignore unexpeted movement when typing */
				if (ntouch == 1 && sc->index[0]->tool_major > 1200)
					dx = dy = 0;

				if (sc->ibtn != 0 && ntouch == 1 && 
				    sc->intr_count < WSP_TAP_MAX_COUNT && 
				    abs(sc->dx_sum) < 1 && abs(sc->dy_sum) < 1 )
					dx = dy = 0;

				if (ntouch == 2 && sc->sc_status.button != 0) {
					dx = sc->pos_x[sc->finger] - sc->pre_pos_x;
					dy = sc->pos_y[sc->finger] - sc->pre_pos_y;
					
					/*
					 * Ignore movement of switch finger or
					 * movement from ibt=0 to ibt=1
					 */
					if (sc->index[0]->origin == 0 || sc->index[1]->origin == 0 ||
					    sc->sc_status.obutton != sc->sc_status.button) {
						dx = dy = 0;
						sc->finger = 0;
					}
					if ((abs(sc->index[0]->rel_x) + abs(sc->index[0]->rel_y)) <
					    (abs(sc->index[1]->rel_x) + abs(sc->index[1]->rel_y)) &&
					    sc->finger == 0) {
						sc->sc_touch = WSP_SECOND_TOUCH;
						dx = dy = 0;
						sc->finger = 1;
					}
					if ((abs(sc->index[0]->rel_x) + abs(sc->index[0]->rel_y)) >=
					    (abs(sc->index[1]->rel_x) + abs(sc->index[1]->rel_y)) &&
					    sc->finger == 1) {
						sc->sc_touch = WSP_SECOND_TOUCH;
						dx = dy = 0;
						sc->finger = 0;
					}
					DPRINTFN(WSP_LLEVEL_INFO, "dx=%5d, dy=%5d, mov=%5d\n",
					    dx, dy, sc->finger);
				}
				if (sc->dz_count--) {
					rdz = (dy + sc->rdz) % tun.scale_factor;
					sc->dz_sum -= (dy + sc->rdz) / tun.scale_factor;
					sc->rdz = rdz;
				}
				if ((sc->dz_sum / tun.z_factor) != 0)
					sc->dz_count = 0;
			}
			rdx = (dx + sc->rdx) % tun.scale_factor;
			dx = (dx + sc->rdx) / tun.scale_factor;
			sc->rdx = rdx;

			rdy = (dy + sc->rdy) % tun.scale_factor;
			dy = (dy + sc->rdy) / tun.scale_factor;
			sc->rdy = rdy;

			sc->dx_sum += dx;
			sc->dy_sum += dy;

			if (ntouch == 2 && sc->sc_status.button == 0) {
				if (sc->scr_mode == WSP_SCR_NONE &&
				    abs(sc->dx_sum) + abs(sc->dy_sum) > tun.scr_hor_threshold)
					sc->scr_mode = abs(sc->dx_sum) >
					    abs(sc->dy_sum) * 2 ? WSP_SCR_HOR : WSP_SCR_VER;
				DPRINTFN(WSP_LLEVEL_INFO, "scr_mode=%5d, count=%d, dx_sum=%d, dy_sum=%d\n",
				    sc->scr_mode, sc->intr_count, sc->dx_sum, sc->dy_sum);
				if (sc->scr_mode == WSP_SCR_HOR)
					sc->dt_sum += dx;
				else
					sc->dt_sum = 0;

				dx = dy = 0;
				if (sc->dz_count == 0)
					dz = sc->dz_sum / tun.z_factor;
				if (sc->scr_mode == WSP_SCR_HOR || 
				    abs(sc->pos_x[0] - sc->pos_x[1]) > MAX_DISTANCE ||
				    abs(sc->pos_y[0] - sc->pos_y[1]) > MAX_DISTANCE)
					dz = 0;
			}
			if (ntouch == 3)
				dx = dy = dz = 0;
			if (sc->intr_count < WSP_TAP_MAX_COUNT &&
			    abs(dx) < 3 && abs(dy) < 3 && abs(dz) < 3)
				dx = dy = dz = 0;
			else
				sc->intr_count = WSP_TAP_MAX_COUNT;
			if (dx || dy || dz)
				sc->sc_status.flags |= MOUSE_POSCHANGED;
			DPRINTFN(WSP_LLEVEL_INFO, "dx=%5d, dy=%5d, dz=%5d, sc_touch=%x, btn=%x\n",
			    dx, dy, dz, sc->sc_touch, sc->sc_status.button);
			sc->sc_status.dx += dx;
			sc->sc_status.dy += dy;
			sc->sc_status.dz += dz;

			wsp_add_to_queue(sc, dx, -dy, dz, sc->sc_status.button);
			if (sc->dz_count == 0) {
				sc->dz_sum = 0;
				sc->rdz = 0;
			}

		}
		sc->pre_pos_x = sc->pos_x[0];
		sc->pre_pos_y = sc->pos_y[0];

		if (ntouch == 2 && sc->sc_status.button != 0) {
			sc->pre_pos_x = sc->pos_x[sc->finger];
			sc->pre_pos_y = sc->pos_y[sc->finger];
		}
		sc->o_ntouch = ntouch;

	case USB_ST_SETUP:
tr_setup:
		/* check if we can put more data into the FIFO */
		if (usb_fifo_put_bytes_max(
		    sc->sc_fifo.fp[USB_FIFO_RX]) != 0) {
			usbd_xfer_set_frame_len(xfer, 0,
			    sc->tp_datalen);
			usbd_transfer_submit(xfer);
		}
		break;

	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
wsp_add_to_queue(struct wsp_softc *sc, int dx, int dy, int dz,
    uint32_t buttons_in)
{
	uint32_t buttons_out;
	uint8_t buf[8];

	dx = imin(dx, 254);
	dx = imax(dx, -256);
	dy = imin(dy, 254);
	dy = imax(dy, -256);
	dz = imin(dz, 126);
	dz = imax(dz, -128);

	buttons_out = MOUSE_MSC_BUTTONS;
	if (buttons_in & MOUSE_BUTTON1DOWN)
		buttons_out &= ~MOUSE_MSC_BUTTON1UP;
	else if (buttons_in & MOUSE_BUTTON2DOWN)
		buttons_out &= ~MOUSE_MSC_BUTTON2UP;
	else if (buttons_in & MOUSE_BUTTON3DOWN)
		buttons_out &= ~MOUSE_MSC_BUTTON3UP;

	/* Encode the mouse data in standard format; refer to mouse(4) */
	buf[0] = sc->sc_mode.syncmask[1];
	buf[0] |= buttons_out;
	buf[1] = dx >> 1;
	buf[2] = dy >> 1;
	buf[3] = dx - (dx >> 1);
	buf[4] = dy - (dy >> 1);
	/* Encode extra bytes for level 1 */
	if (sc->sc_mode.level == 1) {
		buf[5] = dz >> 1;	/* dz / 2 */
		buf[6] = dz - (dz >> 1);/* dz - (dz / 2) */
		buf[7] = (((~buttons_in) >> 3) & MOUSE_SYS_EXTBUTTONS);
	}
	usb_fifo_put_data_linear(sc->sc_fifo.fp[USB_FIFO_RX], buf,
	    sc->sc_mode.packetsize, 1);
}

static void
wsp_reset_buf(struct wsp_softc *sc)
{
	/* reset read queue */
	usb_fifo_reset(sc->sc_fifo.fp[USB_FIFO_RX]);
}

static void
wsp_start_read(struct usb_fifo *fifo)
{
	struct wsp_softc *sc = usb_fifo_softc(fifo);
	int rate;

	/* Check if we should override the default polling interval */
	rate = sc->sc_pollrate;
	/* Range check rate */
	if (rate > 1000)
		rate = 1000;
	/* Check for set rate */
	if ((rate > 0) && (sc->sc_xfer[WSP_INTR_DT] != NULL)) {
		/* Stop current transfer, if any */
		usbd_transfer_stop(sc->sc_xfer[WSP_INTR_DT]);
		/* Set new interval */
		usbd_xfer_set_interval(sc->sc_xfer[WSP_INTR_DT], 1000 / rate);
		/* Only set pollrate once */
		sc->sc_pollrate = 0;
	}
	usbd_transfer_start(sc->sc_xfer[WSP_INTR_DT]);
}

static void
wsp_stop_read(struct usb_fifo *fifo)
{
	struct wsp_softc *sc = usb_fifo_softc(fifo);

	usbd_transfer_stop(sc->sc_xfer[WSP_INTR_DT]);
}


static int
wsp_open(struct usb_fifo *fifo, int fflags)
{
	DPRINTFN(WSP_LLEVEL_INFO, "\n");

	if (fflags & FREAD) {
		struct wsp_softc *sc = usb_fifo_softc(fifo);
		int rc;

		if (sc->sc_state & WSP_ENABLED)
			return (EBUSY);

		if (usb_fifo_alloc_buffer(fifo,
		    WSP_FIFO_BUF_SIZE, WSP_FIFO_QUEUE_MAXLEN)) {
			return (ENOMEM);
		}
		rc = wsp_enable(sc);
		if (rc != 0) {
			usb_fifo_free_buffer(fifo);
			return (rc);
		}
	}
	return (0);
}

static void
wsp_close(struct usb_fifo *fifo, int fflags)
{
	if (fflags & FREAD) {
		struct wsp_softc *sc = usb_fifo_softc(fifo);

		wsp_disable(sc);
		usb_fifo_free_buffer(fifo);
	}
}

int
wsp_ioctl(struct usb_fifo *fifo, u_long cmd, void *addr, int fflags)
{
	struct wsp_softc *sc = usb_fifo_softc(fifo);
	mousemode_t mode;
	int error = 0;

	mtx_lock(&sc->sc_mutex);

	switch (cmd) {
	case MOUSE_GETHWINFO:
		*(mousehw_t *)addr = sc->sc_hw;
		break;
	case MOUSE_GETMODE:
		*(mousemode_t *)addr = sc->sc_mode;
		break;
	case MOUSE_SETMODE:
		mode = *(mousemode_t *)addr;

		if (mode.level == -1)
			/* Don't change the current setting */
			;
		else if ((mode.level < 0) || (mode.level > 1)) {
			error = EINVAL;
			goto done;
		}
		sc->sc_mode.level = mode.level;
		sc->sc_pollrate = mode.rate;
		sc->sc_hw.buttons = 3;

		if (sc->sc_mode.level == 0) {
			sc->sc_mode.protocol = MOUSE_PROTO_MSC;
			sc->sc_mode.packetsize = MOUSE_MSC_PACKETSIZE;
			sc->sc_mode.syncmask[0] = MOUSE_MSC_SYNCMASK;
			sc->sc_mode.syncmask[1] = MOUSE_MSC_SYNC;
		} else if (sc->sc_mode.level == 1) {
			sc->sc_mode.protocol = MOUSE_PROTO_SYSMOUSE;
			sc->sc_mode.packetsize = MOUSE_SYS_PACKETSIZE;
			sc->sc_mode.syncmask[0] = MOUSE_SYS_SYNCMASK;
			sc->sc_mode.syncmask[1] = MOUSE_SYS_SYNC;
		}
		wsp_reset_buf(sc);
		break;
	case MOUSE_GETLEVEL:
		*(int *)addr = sc->sc_mode.level;
		break;
	case MOUSE_SETLEVEL:
		if (*(int *)addr < 0 || *(int *)addr > 1) {
			error = EINVAL;
			goto done;
		}
		sc->sc_mode.level = *(int *)addr;
		sc->sc_hw.buttons = 3;

		if (sc->sc_mode.level == 0) {
			sc->sc_mode.protocol = MOUSE_PROTO_MSC;
			sc->sc_mode.packetsize = MOUSE_MSC_PACKETSIZE;
			sc->sc_mode.syncmask[0] = MOUSE_MSC_SYNCMASK;
			sc->sc_mode.syncmask[1] = MOUSE_MSC_SYNC;
		} else if (sc->sc_mode.level == 1) {
			sc->sc_mode.protocol = MOUSE_PROTO_SYSMOUSE;
			sc->sc_mode.packetsize = MOUSE_SYS_PACKETSIZE;
			sc->sc_mode.syncmask[0] = MOUSE_SYS_SYNCMASK;
			sc->sc_mode.syncmask[1] = MOUSE_SYS_SYNC;
		}
		wsp_reset_buf(sc);
		break;
	case MOUSE_GETSTATUS:{
			mousestatus_t *status = (mousestatus_t *)addr;

			*status = sc->sc_status;
			sc->sc_status.obutton = sc->sc_status.button;
			sc->sc_status.button = 0;
			sc->sc_status.dx = 0;
			sc->sc_status.dy = 0;
			sc->sc_status.dz = 0;

			if (status->dx || status->dy || status->dz)
				status->flags |= MOUSE_POSCHANGED;
			if (status->button != status->obutton)
				status->flags |= MOUSE_BUTTONSCHANGED;
			break;
		}
	default:
		error = ENOTTY;
	}

done:
	mtx_unlock(&sc->sc_mutex);
	return (error);
}

static device_method_t wsp_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, wsp_probe),
	DEVMETHOD(device_attach, wsp_attach),
	DEVMETHOD(device_detach, wsp_detach),
	DEVMETHOD_END
};

static driver_t wsp_driver = {
	.name = WSP_DRIVER_NAME,
	.methods = wsp_methods,
	.size = sizeof(struct wsp_softc)
};

static devclass_t wsp_devclass;

DRIVER_MODULE(wsp, uhub, wsp_driver, wsp_devclass, NULL, 0);
MODULE_DEPEND(wsp, usb, 1, 1, 1);
MODULE_VERSION(wsp, 1);
USB_PNP_HOST_INFO(wsp_devs);
