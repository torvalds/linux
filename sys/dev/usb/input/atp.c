/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Rohit Grover
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

/*
 * Author's note: 'atp' supports two distinct families of Apple trackpad
 * products: the older Fountain/Geyser and the latest Wellspring trackpads.
 * The first version made its appearance with FreeBSD 8 and worked only with
 * the Fountain/Geyser hardware. A fork of this driver for Wellspring was
 * contributed by Huang Wen Hui. This driver unifies the Wellspring effort
 * and also improves upon the original work.
 *
 * I'm grateful to Stephan Scheunig, Angela Naegele, and Nokia IT-support
 * for helping me with access to hardware. Thanks also go to Nokia for
 * giving me an opportunity to do this work.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/selinfo.h>
#include <sys/poll.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbhid.h>

#include "usbdevs.h"

#define USB_DEBUG_VAR atp_debug
#include <dev/usb/usb_debug.h>

#include <sys/mouse.h>

#define ATP_DRIVER_NAME "atp"

/*
 * Driver specific options: the following options may be set by
 * `options' statements in the kernel configuration file.
 */

/* The divisor used to translate sensor reported positions to mickeys. */
#ifndef ATP_SCALE_FACTOR
#define ATP_SCALE_FACTOR                  16
#endif

/* Threshold for small movement noise (in mickeys) */
#ifndef ATP_SMALL_MOVEMENT_THRESHOLD
#define ATP_SMALL_MOVEMENT_THRESHOLD      30
#endif

/* Threshold of instantaneous deltas beyond which movement is considered fast.*/
#ifndef ATP_FAST_MOVEMENT_TRESHOLD
#define ATP_FAST_MOVEMENT_TRESHOLD        150
#endif

/*
 * This is the age in microseconds beyond which a touch is considered
 * to be a slide; and therefore a tap event isn't registered.
 */
#ifndef ATP_TOUCH_TIMEOUT
#define ATP_TOUCH_TIMEOUT                 125000
#endif

#ifndef ATP_IDLENESS_THRESHOLD
#define	ATP_IDLENESS_THRESHOLD 10
#endif

#ifndef FG_SENSOR_NOISE_THRESHOLD
#define FG_SENSOR_NOISE_THRESHOLD 2
#endif

/*
 * A double-tap followed by a single-finger slide is treated as a
 * special gesture. The driver responds to this gesture by assuming a
 * virtual button-press for the lifetime of the slide. The following
 * threshold is the maximum time gap (in microseconds) between the two
 * tap events preceding the slide for such a gesture.
 */
#ifndef ATP_DOUBLE_TAP_N_DRAG_THRESHOLD
#define ATP_DOUBLE_TAP_N_DRAG_THRESHOLD   200000
#endif

/*
 * The wait duration in ticks after losing a touch contact before
 * zombied strokes are reaped and turned into button events.
 */
#define ATP_ZOMBIE_STROKE_REAP_INTERVAL   (hz / 20)	/* 50 ms */

/* The multiplier used to translate sensor reported positions to mickeys. */
#define FG_SCALE_FACTOR                   380

/*
 * The movement threshold for a stroke; this is the maximum difference
 * in position which will be resolved as a continuation of a stroke
 * component.
 */
#define FG_MAX_DELTA_MICKEYS             ((3 * (FG_SCALE_FACTOR)) >> 1)

/* Distance-squared threshold for matching a finger with a known stroke */
#ifndef WSP_MAX_ALLOWED_MATCH_DISTANCE_SQ
#define WSP_MAX_ALLOWED_MATCH_DISTANCE_SQ 1000000
#endif

/* Ignore pressure spans with cumulative press. below this value. */
#define FG_PSPAN_MIN_CUM_PRESSURE         10

/* Maximum allowed width for pressure-spans.*/
#define FG_PSPAN_MAX_WIDTH                4

/* end of driver specific options */

/* Tunables */
static SYSCTL_NODE(_hw_usb, OID_AUTO, atp, CTLFLAG_RW, 0, "USB ATP");

#ifdef USB_DEBUG
enum atp_log_level {
	ATP_LLEVEL_DISABLED = 0,
	ATP_LLEVEL_ERROR,
	ATP_LLEVEL_DEBUG,       /* for troubleshooting */
	ATP_LLEVEL_INFO,        /* for diagnostics */
};
static int atp_debug = ATP_LLEVEL_ERROR; /* the default is to only log errors */
SYSCTL_INT(_hw_usb_atp, OID_AUTO, debug, CTLFLAG_RWTUN,
    &atp_debug, ATP_LLEVEL_ERROR, "ATP debug level");
#endif /* USB_DEBUG */

static u_int atp_touch_timeout = ATP_TOUCH_TIMEOUT;
SYSCTL_UINT(_hw_usb_atp, OID_AUTO, touch_timeout, CTLFLAG_RWTUN,
    &atp_touch_timeout, 125000, "age threshold in microseconds for a touch");

static u_int atp_double_tap_threshold = ATP_DOUBLE_TAP_N_DRAG_THRESHOLD;
SYSCTL_UINT(_hw_usb_atp, OID_AUTO, double_tap_threshold, CTLFLAG_RWTUN,
    &atp_double_tap_threshold, ATP_DOUBLE_TAP_N_DRAG_THRESHOLD,
    "maximum time in microseconds to allow association between a double-tap and "
    "drag gesture");

static u_int atp_mickeys_scale_factor = ATP_SCALE_FACTOR;
static int atp_sysctl_scale_factor_handler(SYSCTL_HANDLER_ARGS);
SYSCTL_PROC(_hw_usb_atp, OID_AUTO, scale_factor, CTLTYPE_UINT | CTLFLAG_RWTUN,
    &atp_mickeys_scale_factor, sizeof(atp_mickeys_scale_factor),
    atp_sysctl_scale_factor_handler, "IU", "movement scale factor");

static u_int atp_small_movement_threshold = ATP_SMALL_MOVEMENT_THRESHOLD;
SYSCTL_UINT(_hw_usb_atp, OID_AUTO, small_movement, CTLFLAG_RWTUN,
    &atp_small_movement_threshold, ATP_SMALL_MOVEMENT_THRESHOLD,
    "the small movement black-hole for filtering noise");

static u_int atp_tap_minimum = 1;
SYSCTL_UINT(_hw_usb_atp, OID_AUTO, tap_minimum, CTLFLAG_RWTUN,
    &atp_tap_minimum, 1, "Minimum number of taps before detection");

/*
 * Strokes which accumulate at least this amount of absolute movement
 * from the aggregate of their components are considered as
 * slides. Unit: mickeys.
 */
static u_int atp_slide_min_movement = 2 * ATP_SMALL_MOVEMENT_THRESHOLD;
SYSCTL_UINT(_hw_usb_atp, OID_AUTO, slide_min_movement, CTLFLAG_RWTUN,
    &atp_slide_min_movement, 2 * ATP_SMALL_MOVEMENT_THRESHOLD,
    "strokes with at least this amt. of movement are considered slides");

/*
 * The minimum age of a stroke for it to be considered mature; this
 * helps filter movements (noise) from immature strokes. Units: interrupts.
 */
static u_int atp_stroke_maturity_threshold = 4;
SYSCTL_UINT(_hw_usb_atp, OID_AUTO, stroke_maturity_threshold, CTLFLAG_RWTUN,
    &atp_stroke_maturity_threshold, 4,
    "the minimum age of a stroke for it to be considered mature");

typedef enum atp_trackpad_family {
	TRACKPAD_FAMILY_FOUNTAIN_GEYSER,
	TRACKPAD_FAMILY_WELLSPRING,
	TRACKPAD_FAMILY_MAX /* keep this at the tail end of the enumeration */
} trackpad_family_t;

enum fountain_geyser_product {
	FOUNTAIN,
	GEYSER1,
	GEYSER1_17inch,
	GEYSER2,
	GEYSER3,
	GEYSER4,
	FOUNTAIN_GEYSER_PRODUCT_MAX /* keep this at the end */
};

enum wellspring_product {
	WELLSPRING1,
	WELLSPRING2,
	WELLSPRING3,
	WELLSPRING4,
	WELLSPRING4A,
	WELLSPRING5,
	WELLSPRING6A,
	WELLSPRING6,
	WELLSPRING5A,
	WELLSPRING7,
	WELLSPRING7A,
	WELLSPRING8,
	WELLSPRING_PRODUCT_MAX /* keep this at the end of the enumeration */
};

/* trackpad header types */
enum fountain_geyser_trackpad_type {
	FG_TRACKPAD_TYPE_GEYSER1,
	FG_TRACKPAD_TYPE_GEYSER2,
	FG_TRACKPAD_TYPE_GEYSER3,
	FG_TRACKPAD_TYPE_GEYSER4,
};
enum wellspring_trackpad_type {
	WSP_TRACKPAD_TYPE1,      /* plain trackpad */
	WSP_TRACKPAD_TYPE2,      /* button integrated in trackpad */
	WSP_TRACKPAD_TYPE3       /* additional header fields since June 2013 */
};

/*
 * Trackpad family and product and family are encoded together in the
 * driver_info value associated with a trackpad product.
 */
#define N_PROD_BITS 8  /* Number of bits used to encode product */
#define ENCODE_DRIVER_INFO(FAMILY, PROD)      \
    (((FAMILY) << N_PROD_BITS) | (PROD))
#define DECODE_FAMILY_FROM_DRIVER_INFO(INFO)  ((INFO) >> N_PROD_BITS)
#define DECODE_PRODUCT_FROM_DRIVER_INFO(INFO) \
    ((INFO) & ((1 << N_PROD_BITS) - 1))

#define FG_DRIVER_INFO(PRODUCT)               \
    ENCODE_DRIVER_INFO(TRACKPAD_FAMILY_FOUNTAIN_GEYSER, PRODUCT)
#define WELLSPRING_DRIVER_INFO(PRODUCT)       \
    ENCODE_DRIVER_INFO(TRACKPAD_FAMILY_WELLSPRING, PRODUCT)

/*
 * The following structure captures the state of a pressure span along
 * an axis. Each contact with the touchpad results in separate
 * pressure spans along the two axes.
 */
typedef struct fg_pspan {
	u_int width;       /* in units of sensors */
	u_int cum;         /* cumulative compression (from all sensors) */
	u_int cog;         /* center of gravity */
	u_int loc;         /* location (scaled using the mickeys factor) */
	boolean_t matched; /* to track pspans as they match against strokes. */
} fg_pspan;

#define FG_MAX_PSPANS_PER_AXIS 3
#define FG_MAX_STROKES         (2 * FG_MAX_PSPANS_PER_AXIS)

#define WELLSPRING_INTERFACE_INDEX 1

/* trackpad finger data offsets, le16-aligned */
#define WSP_TYPE1_FINGER_DATA_OFFSET  (13 * 2)
#define WSP_TYPE2_FINGER_DATA_OFFSET  (15 * 2)
#define WSP_TYPE3_FINGER_DATA_OFFSET  (19 * 2)

/* trackpad button data offsets */
#define WSP_TYPE2_BUTTON_DATA_OFFSET   15
#define WSP_TYPE3_BUTTON_DATA_OFFSET   23

/* list of device capability bits */
#define HAS_INTEGRATED_BUTTON   1

/* trackpad finger structure - little endian */
struct wsp_finger_sensor_data {
	int16_t origin;       /* zero when switching track finger */
	int16_t abs_x;        /* absolute x coordinate */
	int16_t abs_y;        /* absolute y coordinate */
	int16_t rel_x;        /* relative x coordinate */
	int16_t rel_y;        /* relative y coordinate */
	int16_t tool_major;   /* tool area, major axis */
	int16_t tool_minor;   /* tool area, minor axis */
	int16_t orientation;  /* 16384 when point, else 15 bit angle */
	int16_t touch_major;  /* touch area, major axis */
	int16_t touch_minor;  /* touch area, minor axis */
	int16_t unused[3];    /* zeros */
	int16_t multi;        /* one finger: varies, more fingers: constant */
} __packed;

typedef struct wsp_finger {
	/* to track fingers as they match against strokes. */
	boolean_t matched;

	/* location (scaled using the mickeys factor) */
	int x;
	int y;
} wsp_finger_t;

#define WSP_MAX_FINGERS               16
#define WSP_SIZEOF_FINGER_SENSOR_DATA sizeof(struct wsp_finger_sensor_data)
#define WSP_SIZEOF_ALL_FINGER_DATA    (WSP_MAX_FINGERS * \
				       WSP_SIZEOF_FINGER_SENSOR_DATA)
#define WSP_MAX_FINGER_ORIENTATION    16384

#define ATP_SENSOR_DATA_BUF_MAX       1024
#if (ATP_SENSOR_DATA_BUF_MAX < ((WSP_MAX_FINGERS * 14 * 2) + \
				WSP_TYPE3_FINGER_DATA_OFFSET))
/* note: 14 * 2 in the above is based on sizeof(struct wsp_finger_sensor_data)*/
#error "ATP_SENSOR_DATA_BUF_MAX is too small"
#endif

#define ATP_MAX_STROKES               MAX(WSP_MAX_FINGERS, FG_MAX_STROKES)

#define FG_MAX_XSENSORS 26
#define FG_MAX_YSENSORS 16

/* device-specific configuration */
struct fg_dev_params {
	u_int                              data_len;   /* for sensor data */
	u_int                              n_xsensors;
	u_int                              n_ysensors;
	enum fountain_geyser_trackpad_type prot;
};
struct wsp_dev_params {
	uint8_t  caps;               /* device capability bitmask */
	uint8_t  tp_type;            /* type of trackpad interface */
	uint8_t  finger_data_offset; /* offset to trackpad finger data */
};

static const struct fg_dev_params fg_dev_params[FOUNTAIN_GEYSER_PRODUCT_MAX] = {
	[FOUNTAIN] = {
		.data_len   = 81,
		.n_xsensors = 16,
		.n_ysensors = 16,
		.prot       = FG_TRACKPAD_TYPE_GEYSER1
	},
	[GEYSER1] = {
		.data_len   = 81,
		.n_xsensors = 16,
		.n_ysensors = 16,
		.prot       = FG_TRACKPAD_TYPE_GEYSER1
	},
	[GEYSER1_17inch] = {
		.data_len   = 81,
		.n_xsensors = 26,
		.n_ysensors = 16,
		.prot       = FG_TRACKPAD_TYPE_GEYSER1
	},
	[GEYSER2] = {
		.data_len   = 64,
		.n_xsensors = 15,
		.n_ysensors = 9,
		.prot       = FG_TRACKPAD_TYPE_GEYSER2
	},
	[GEYSER3] = {
		.data_len   = 64,
		.n_xsensors = 20,
		.n_ysensors = 10,
		.prot       = FG_TRACKPAD_TYPE_GEYSER3
	},
	[GEYSER4] = {
		.data_len   = 64,
		.n_xsensors = 20,
		.n_ysensors = 10,
		.prot       = FG_TRACKPAD_TYPE_GEYSER4
	}
};

static const STRUCT_USB_HOST_ID fg_devs[] = {
	/* PowerBooks Feb 2005, iBooks G4 */
	{ USB_VPI(USB_VENDOR_APPLE, 0x020e, FG_DRIVER_INFO(FOUNTAIN)) },
	{ USB_VPI(USB_VENDOR_APPLE, 0x020f, FG_DRIVER_INFO(FOUNTAIN)) },
	{ USB_VPI(USB_VENDOR_APPLE, 0x0210, FG_DRIVER_INFO(FOUNTAIN)) },
	{ USB_VPI(USB_VENDOR_APPLE, 0x030a, FG_DRIVER_INFO(FOUNTAIN)) },
	{ USB_VPI(USB_VENDOR_APPLE, 0x030b, FG_DRIVER_INFO(GEYSER1)) },

	/* PowerBooks Oct 2005 */
	{ USB_VPI(USB_VENDOR_APPLE, 0x0214, FG_DRIVER_INFO(GEYSER2)) },
	{ USB_VPI(USB_VENDOR_APPLE, 0x0215, FG_DRIVER_INFO(GEYSER2)) },
	{ USB_VPI(USB_VENDOR_APPLE, 0x0216, FG_DRIVER_INFO(GEYSER2)) },

	/* Core Duo MacBook & MacBook Pro */
	{ USB_VPI(USB_VENDOR_APPLE, 0x0217, FG_DRIVER_INFO(GEYSER3)) },
	{ USB_VPI(USB_VENDOR_APPLE, 0x0218, FG_DRIVER_INFO(GEYSER3)) },
	{ USB_VPI(USB_VENDOR_APPLE, 0x0219, FG_DRIVER_INFO(GEYSER3)) },

	/* Core2 Duo MacBook & MacBook Pro */
	{ USB_VPI(USB_VENDOR_APPLE, 0x021a, FG_DRIVER_INFO(GEYSER4)) },
	{ USB_VPI(USB_VENDOR_APPLE, 0x021b, FG_DRIVER_INFO(GEYSER4)) },
	{ USB_VPI(USB_VENDOR_APPLE, 0x021c, FG_DRIVER_INFO(GEYSER4)) },

	/* Core2 Duo MacBook3,1 */
	{ USB_VPI(USB_VENDOR_APPLE, 0x0229, FG_DRIVER_INFO(GEYSER4)) },
	{ USB_VPI(USB_VENDOR_APPLE, 0x022a, FG_DRIVER_INFO(GEYSER4)) },
	{ USB_VPI(USB_VENDOR_APPLE, 0x022b, FG_DRIVER_INFO(GEYSER4)) },

	/* 17 inch PowerBook */
	{ USB_VPI(USB_VENDOR_APPLE, 0x020d, FG_DRIVER_INFO(GEYSER1_17inch)) },
};

static const struct wsp_dev_params wsp_dev_params[WELLSPRING_PRODUCT_MAX] = {
	[WELLSPRING1] = {
		.caps       = 0,
		.tp_type    = WSP_TRACKPAD_TYPE1,
		.finger_data_offset  = WSP_TYPE1_FINGER_DATA_OFFSET,
	},
	[WELLSPRING2] = {
		.caps       = 0,
		.tp_type    = WSP_TRACKPAD_TYPE1,
		.finger_data_offset  = WSP_TYPE1_FINGER_DATA_OFFSET,
	},
	[WELLSPRING3] = {
		.caps       = HAS_INTEGRATED_BUTTON,
		.tp_type    = WSP_TRACKPAD_TYPE2,
		.finger_data_offset  = WSP_TYPE2_FINGER_DATA_OFFSET,
	},
	[WELLSPRING4] = {
		.caps       = HAS_INTEGRATED_BUTTON,
		.tp_type    = WSP_TRACKPAD_TYPE2,
		.finger_data_offset  = WSP_TYPE2_FINGER_DATA_OFFSET,
	},
	[WELLSPRING4A] = {
		.caps       = HAS_INTEGRATED_BUTTON,
		.tp_type    = WSP_TRACKPAD_TYPE2,
		.finger_data_offset  = WSP_TYPE2_FINGER_DATA_OFFSET,
	},
	[WELLSPRING5] = {
		.caps       = HAS_INTEGRATED_BUTTON,
		.tp_type    = WSP_TRACKPAD_TYPE2,
		.finger_data_offset  = WSP_TYPE2_FINGER_DATA_OFFSET,
	},
	[WELLSPRING6] = {
		.caps       = HAS_INTEGRATED_BUTTON,
		.tp_type    = WSP_TRACKPAD_TYPE2,
		.finger_data_offset  = WSP_TYPE2_FINGER_DATA_OFFSET,
	},
	[WELLSPRING5A] = {
		.caps       = HAS_INTEGRATED_BUTTON,
		.tp_type    = WSP_TRACKPAD_TYPE2,
		.finger_data_offset  = WSP_TYPE2_FINGER_DATA_OFFSET,
	},
	[WELLSPRING6A] = {
		.caps       = HAS_INTEGRATED_BUTTON,
		.tp_type    = WSP_TRACKPAD_TYPE2,
		.finger_data_offset  = WSP_TYPE2_FINGER_DATA_OFFSET,
	},
	[WELLSPRING7] = {
		.caps       = HAS_INTEGRATED_BUTTON,
		.tp_type    = WSP_TRACKPAD_TYPE2,
		.finger_data_offset  = WSP_TYPE2_FINGER_DATA_OFFSET,
	},
	[WELLSPRING7A] = {
		.caps       = HAS_INTEGRATED_BUTTON,
		.tp_type    = WSP_TRACKPAD_TYPE2,
		.finger_data_offset  = WSP_TYPE2_FINGER_DATA_OFFSET,
	},
	[WELLSPRING8] = {
		.caps       = HAS_INTEGRATED_BUTTON,
		.tp_type    = WSP_TRACKPAD_TYPE3,
		.finger_data_offset  = WSP_TYPE3_FINGER_DATA_OFFSET,
	},
};

#define ATP_DEV(v,p,i) { USB_VPI(USB_VENDOR_##v, USB_PRODUCT_##v##_##p, i) }

/* TODO: STRUCT_USB_HOST_ID */
static const struct usb_device_id wsp_devs[] = {
	/* MacbookAir1.1 */
	ATP_DEV(APPLE, WELLSPRING_ANSI, WELLSPRING_DRIVER_INFO(WELLSPRING1)),
	ATP_DEV(APPLE, WELLSPRING_ISO,  WELLSPRING_DRIVER_INFO(WELLSPRING1)),
	ATP_DEV(APPLE, WELLSPRING_JIS,  WELLSPRING_DRIVER_INFO(WELLSPRING1)),

	/* MacbookProPenryn, aka wellspring2 */
	ATP_DEV(APPLE, WELLSPRING2_ANSI, WELLSPRING_DRIVER_INFO(WELLSPRING2)),
	ATP_DEV(APPLE, WELLSPRING2_ISO,  WELLSPRING_DRIVER_INFO(WELLSPRING2)),
	ATP_DEV(APPLE, WELLSPRING2_JIS,  WELLSPRING_DRIVER_INFO(WELLSPRING2)),

	/* Macbook5,1 (unibody), aka wellspring3 */
	ATP_DEV(APPLE, WELLSPRING3_ANSI, WELLSPRING_DRIVER_INFO(WELLSPRING3)),
	ATP_DEV(APPLE, WELLSPRING3_ISO,  WELLSPRING_DRIVER_INFO(WELLSPRING3)),
	ATP_DEV(APPLE, WELLSPRING3_JIS,  WELLSPRING_DRIVER_INFO(WELLSPRING3)),

	/* MacbookAir3,2 (unibody), aka wellspring4 */
	ATP_DEV(APPLE, WELLSPRING4_ANSI, WELLSPRING_DRIVER_INFO(WELLSPRING4)),
	ATP_DEV(APPLE, WELLSPRING4_ISO,  WELLSPRING_DRIVER_INFO(WELLSPRING4)),
	ATP_DEV(APPLE, WELLSPRING4_JIS,  WELLSPRING_DRIVER_INFO(WELLSPRING4)),

	/* MacbookAir3,1 (unibody), aka wellspring4 */
	ATP_DEV(APPLE, WELLSPRING4A_ANSI, WELLSPRING_DRIVER_INFO(WELLSPRING4A)),
	ATP_DEV(APPLE, WELLSPRING4A_ISO,  WELLSPRING_DRIVER_INFO(WELLSPRING4A)),
	ATP_DEV(APPLE, WELLSPRING4A_JIS,  WELLSPRING_DRIVER_INFO(WELLSPRING4A)),

	/* Macbook8 (unibody, March 2011) */
	ATP_DEV(APPLE, WELLSPRING5_ANSI, WELLSPRING_DRIVER_INFO(WELLSPRING5)),
	ATP_DEV(APPLE, WELLSPRING5_ISO,  WELLSPRING_DRIVER_INFO(WELLSPRING5)),
	ATP_DEV(APPLE, WELLSPRING5_JIS,  WELLSPRING_DRIVER_INFO(WELLSPRING5)),

	/* MacbookAir4,1 (unibody, July 2011) */
	ATP_DEV(APPLE, WELLSPRING6A_ANSI, WELLSPRING_DRIVER_INFO(WELLSPRING6A)),
	ATP_DEV(APPLE, WELLSPRING6A_ISO,  WELLSPRING_DRIVER_INFO(WELLSPRING6A)),
	ATP_DEV(APPLE, WELLSPRING6A_JIS,  WELLSPRING_DRIVER_INFO(WELLSPRING6A)),

	/* MacbookAir4,2 (unibody, July 2011) */
	ATP_DEV(APPLE, WELLSPRING6_ANSI, WELLSPRING_DRIVER_INFO(WELLSPRING6)),
	ATP_DEV(APPLE, WELLSPRING6_ISO,  WELLSPRING_DRIVER_INFO(WELLSPRING6)),
	ATP_DEV(APPLE, WELLSPRING6_JIS,  WELLSPRING_DRIVER_INFO(WELLSPRING6)),

	/* Macbook8,2 (unibody) */
	ATP_DEV(APPLE, WELLSPRING5A_ANSI, WELLSPRING_DRIVER_INFO(WELLSPRING5A)),
	ATP_DEV(APPLE, WELLSPRING5A_ISO,  WELLSPRING_DRIVER_INFO(WELLSPRING5A)),
	ATP_DEV(APPLE, WELLSPRING5A_JIS,  WELLSPRING_DRIVER_INFO(WELLSPRING5A)),

	/* MacbookPro10,1 (unibody, June 2012) */
	/* MacbookPro11,? (unibody, June 2013) */
	ATP_DEV(APPLE, WELLSPRING7_ANSI, WELLSPRING_DRIVER_INFO(WELLSPRING7)),
	ATP_DEV(APPLE, WELLSPRING7_ISO,  WELLSPRING_DRIVER_INFO(WELLSPRING7)),
	ATP_DEV(APPLE, WELLSPRING7_JIS,  WELLSPRING_DRIVER_INFO(WELLSPRING7)),

	/* MacbookPro10,2 (unibody, October 2012) */
	ATP_DEV(APPLE, WELLSPRING7A_ANSI, WELLSPRING_DRIVER_INFO(WELLSPRING7A)),
	ATP_DEV(APPLE, WELLSPRING7A_ISO,  WELLSPRING_DRIVER_INFO(WELLSPRING7A)),
	ATP_DEV(APPLE, WELLSPRING7A_JIS,  WELLSPRING_DRIVER_INFO(WELLSPRING7A)),

	/* MacbookAir6,2 (unibody, June 2013) */
	ATP_DEV(APPLE, WELLSPRING8_ANSI, WELLSPRING_DRIVER_INFO(WELLSPRING8)),
	ATP_DEV(APPLE, WELLSPRING8_ISO,  WELLSPRING_DRIVER_INFO(WELLSPRING8)),
	ATP_DEV(APPLE, WELLSPRING8_JIS,  WELLSPRING_DRIVER_INFO(WELLSPRING8)),
};

typedef enum atp_stroke_type {
	ATP_STROKE_TOUCH,
	ATP_STROKE_SLIDE,
} atp_stroke_type;

typedef enum atp_axis {
	X = 0,
	Y = 1,
	NUM_AXES
} atp_axis;

#define ATP_FIFO_BUF_SIZE        8 /* bytes */
#define ATP_FIFO_QUEUE_MAXLEN   50 /* units */

enum {
	ATP_INTR_DT,
	ATP_RESET,
	ATP_N_TRANSFER,
};

typedef struct fg_stroke_component {
	/* Fields encapsulating the pressure-span. */
	u_int loc;              /* location (scaled) */
	u_int cum_pressure;     /* cumulative compression */
	u_int max_cum_pressure; /* max cumulative compression */
	boolean_t matched; /*to track components as they match against pspans.*/

	int   delta_mickeys;    /* change in location (un-smoothened movement)*/
} fg_stroke_component_t;

/*
 * The following structure captures a finger contact with the
 * touchpad. A stroke comprises two p-span components and some state.
 */
typedef struct atp_stroke {
	TAILQ_ENTRY(atp_stroke) entry;

	atp_stroke_type type;
	uint32_t        flags; /* the state of this stroke */
#define ATSF_ZOMBIE 0x1
	boolean_t       matched;          /* to track match against fingers.*/

	struct timeval  ctime; /* create time; for coincident siblings. */

	/*
	 * Unit: interrupts; we maintain this value in
	 * addition to 'ctime' in order to avoid the
	 * expensive call to microtime() at every
	 * interrupt.
	 */
	uint32_t age;

	/* Location */
	int x;
	int y;

	/* Fields containing information about movement. */
	int   instantaneous_dx; /* curr. change in X location (un-smoothened) */
	int   instantaneous_dy; /* curr. change in Y location (un-smoothened) */
	int   pending_dx;       /* cum. of pending short movements */
	int   pending_dy;       /* cum. of pending short movements */
	int   movement_dx;      /* interpreted smoothened movement */
	int   movement_dy;      /* interpreted smoothened movement */
	int   cum_movement_x;   /* cum. horizontal movement */
	int   cum_movement_y;   /* cum. vertical movement */

	/*
	 * The following member is relevant only for fountain-geyser trackpads.
	 * For these, there is the need to track pressure-spans and cumulative
	 * pressures for stroke components.
	 */
	fg_stroke_component_t components[NUM_AXES];
} atp_stroke_t;

struct atp_softc; /* forward declaration */
typedef void (*sensor_data_interpreter_t)(struct atp_softc *sc, u_int len);

struct atp_softc {
	device_t            sc_dev;
	struct usb_device  *sc_usb_device;
	struct mtx          sc_mutex; /* for synchronization */
	struct usb_fifo_sc  sc_fifo;

#define	MODE_LENGTH 8
	char                sc_mode_bytes[MODE_LENGTH]; /* device mode */

	trackpad_family_t   sc_family;
	const void         *sc_params; /* device configuration */
	sensor_data_interpreter_t sensor_data_interpreter;

	mousehw_t           sc_hw;
	mousemode_t         sc_mode;
	mousestatus_t       sc_status;

	u_int               sc_state;
#define ATP_ENABLED          0x01
#define ATP_ZOMBIES_EXIST    0x02
#define ATP_DOUBLE_TAP_DRAG  0x04
#define ATP_VALID            0x08

	struct usb_xfer    *sc_xfer[ATP_N_TRANSFER];

	u_int               sc_pollrate;
	int                 sc_fflags;

	atp_stroke_t        sc_strokes_data[ATP_MAX_STROKES];
	TAILQ_HEAD(,atp_stroke) sc_stroke_free;
	TAILQ_HEAD(,atp_stroke) sc_stroke_used;
	u_int               sc_n_strokes;

	struct callout	    sc_callout;

	/*
	 * button status. Set to non-zero if the mouse-button is physically
	 * pressed. This state variable is exposed through softc to allow
	 * reap_sibling_zombies to avoid registering taps while the trackpad
	 * button is pressed.
         */
	uint8_t             sc_ibtn;

	/*
	 * Time when touch zombies were last reaped; useful for detecting
	 * double-touch-n-drag.
	 */
	struct timeval      sc_touch_reap_time;

	u_int	            sc_idlecount;

	/* Regarding the data transferred from t-pad in USB INTR packets. */
	u_int   sc_expected_sensor_data_len;
	uint8_t sc_sensor_data[ATP_SENSOR_DATA_BUF_MAX] __aligned(4);

	int      sc_cur_x[FG_MAX_XSENSORS];      /* current sensor readings */
	int      sc_cur_y[FG_MAX_YSENSORS];
	int      sc_base_x[FG_MAX_XSENSORS];     /* base sensor readings */
	int      sc_base_y[FG_MAX_YSENSORS];
	int      sc_pressure_x[FG_MAX_XSENSORS]; /* computed pressures */
	int      sc_pressure_y[FG_MAX_YSENSORS];
	fg_pspan sc_pspans_x[FG_MAX_PSPANS_PER_AXIS];
	fg_pspan sc_pspans_y[FG_MAX_PSPANS_PER_AXIS];
};

/*
 * The last byte of the fountain-geyser sensor data contains status bits; the
 * following values define the meanings of these bits.
 * (only Geyser 3/4)
 */
enum geyser34_status_bits {
	FG_STATUS_BUTTON      = (uint8_t)0x01, /* The button was pressed */
	FG_STATUS_BASE_UPDATE = (uint8_t)0x04, /* Data from an untouched pad.*/
};

typedef enum interface_mode {
	RAW_SENSOR_MODE = (uint8_t)0x01,
	HID_MODE        = (uint8_t)0x08
} interface_mode;


/*
 * function prototypes
 */
static usb_fifo_cmd_t   atp_start_read;
static usb_fifo_cmd_t   atp_stop_read;
static usb_fifo_open_t  atp_open;
static usb_fifo_close_t atp_close;
static usb_fifo_ioctl_t atp_ioctl;

static struct usb_fifo_methods atp_fifo_methods = {
	.f_open       = &atp_open,
	.f_close      = &atp_close,
	.f_ioctl      = &atp_ioctl,
	.f_start_read = &atp_start_read,
	.f_stop_read  = &atp_stop_read,
	.basename[0]  = ATP_DRIVER_NAME,
};

/* device initialization and shutdown */
static usb_error_t   atp_set_device_mode(struct atp_softc *, interface_mode);
static void	     atp_reset_callback(struct usb_xfer *, usb_error_t);
static int	     atp_enable(struct atp_softc *);
static void	     atp_disable(struct atp_softc *);

/* sensor interpretation */
static void	     fg_interpret_sensor_data(struct atp_softc *, u_int);
static void	     fg_extract_sensor_data(const int8_t *, u_int, atp_axis,
    int *, enum fountain_geyser_trackpad_type);
static void	     fg_get_pressures(int *, const int *, const int *, int);
static void	     fg_detect_pspans(int *, u_int, u_int, fg_pspan *, u_int *);
static void	     wsp_interpret_sensor_data(struct atp_softc *, u_int);

/* movement detection */
static boolean_t     fg_match_stroke_component(fg_stroke_component_t *,
    const fg_pspan *, atp_stroke_type);
static void	     fg_match_strokes_against_pspans(struct atp_softc *,
    atp_axis, fg_pspan *, u_int, u_int);
static boolean_t     wsp_match_strokes_against_fingers(struct atp_softc *,
    wsp_finger_t *, u_int);
static boolean_t     fg_update_strokes(struct atp_softc *, fg_pspan *, u_int,
    fg_pspan *, u_int);
static boolean_t     wsp_update_strokes(struct atp_softc *,
    wsp_finger_t [WSP_MAX_FINGERS], u_int);
static void fg_add_stroke(struct atp_softc *, const fg_pspan *, const fg_pspan *);
static void	     fg_add_new_strokes(struct atp_softc *, fg_pspan *,
    u_int, fg_pspan *, u_int);
static void wsp_add_stroke(struct atp_softc *, const wsp_finger_t *);
static void	     atp_advance_stroke_state(struct atp_softc *,
    atp_stroke_t *, boolean_t *);
static boolean_t atp_stroke_has_small_movement(const atp_stroke_t *);
static void	     atp_update_pending_mickeys(atp_stroke_t *);
static boolean_t     atp_compute_stroke_movement(atp_stroke_t *);
static void	     atp_terminate_stroke(struct atp_softc *, atp_stroke_t *);

/* tap detection */
static boolean_t atp_is_horizontal_scroll(const atp_stroke_t *);
static boolean_t atp_is_vertical_scroll(const atp_stroke_t *);
static void	     atp_reap_sibling_zombies(void *);
static void	     atp_convert_to_slide(struct atp_softc *, atp_stroke_t *);

/* updating fifo */
static void	     atp_reset_buf(struct atp_softc *);
static void	     atp_add_to_queue(struct atp_softc *, int, int, int, uint32_t);

/* Device methods. */
static device_probe_t  atp_probe;
static device_attach_t atp_attach;
static device_detach_t atp_detach;
static usb_callback_t  atp_intr;

static const struct usb_config atp_xfer_config[ATP_N_TRANSFER] = {
	[ATP_INTR_DT] = {
		.type      = UE_INTERRUPT,
		.endpoint  = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {
			.pipe_bof = 1, /* block pipe on failure */
			.short_xfer_ok = 1,
		},
		.bufsize   = ATP_SENSOR_DATA_BUF_MAX,
		.callback  = &atp_intr,
	},
	[ATP_RESET] = {
		.type      = UE_CONTROL,
		.endpoint  = 0, /* Control pipe */
		.direction = UE_DIR_ANY,
		.bufsize   = sizeof(struct usb_device_request) + MODE_LENGTH,
		.callback  = &atp_reset_callback,
		.interval  = 0,  /* no pre-delay */
	},
};

static atp_stroke_t *
atp_alloc_stroke(struct atp_softc *sc)
{
	atp_stroke_t *pstroke;

	pstroke = TAILQ_FIRST(&sc->sc_stroke_free);
	if (pstroke == NULL)
		goto done;

	TAILQ_REMOVE(&sc->sc_stroke_free, pstroke, entry);
	memset(pstroke, 0, sizeof(*pstroke));
	TAILQ_INSERT_TAIL(&sc->sc_stroke_used, pstroke, entry);

	sc->sc_n_strokes++;
done:
	return (pstroke);
}

static void
atp_free_stroke(struct atp_softc *sc, atp_stroke_t *pstroke)
{
	if (pstroke == NULL)
		return;

	sc->sc_n_strokes--;

	TAILQ_REMOVE(&sc->sc_stroke_used, pstroke, entry);
	TAILQ_INSERT_TAIL(&sc->sc_stroke_free, pstroke, entry);
}

static void
atp_init_stroke_pool(struct atp_softc *sc)
{
	u_int x;

	TAILQ_INIT(&sc->sc_stroke_free);
	TAILQ_INIT(&sc->sc_stroke_used);

	sc->sc_n_strokes = 0;

	memset(&sc->sc_strokes_data, 0, sizeof(sc->sc_strokes_data));

	for (x = 0; x != ATP_MAX_STROKES; x++) {
		TAILQ_INSERT_TAIL(&sc->sc_stroke_free, &sc->sc_strokes_data[x],
		    entry);
	}
}

static usb_error_t
atp_set_device_mode(struct atp_softc *sc, interface_mode newMode)
{
	uint8_t mode_value;
	usb_error_t err;

	if ((newMode != RAW_SENSOR_MODE) && (newMode != HID_MODE))
		return (USB_ERR_INVAL);

	if ((newMode == RAW_SENSOR_MODE) &&
	    (sc->sc_family == TRACKPAD_FAMILY_FOUNTAIN_GEYSER))
		mode_value = (uint8_t)0x04;
	else
		mode_value = newMode;

	err = usbd_req_get_report(sc->sc_usb_device, NULL /* mutex */,
	    sc->sc_mode_bytes, sizeof(sc->sc_mode_bytes), 0 /* interface idx */,
	    0x03 /* type */, 0x00 /* id */);
	if (err != USB_ERR_NORMAL_COMPLETION) {
		DPRINTF("Failed to read device mode (%d)\n", err);
		return (err);
	}

	if (sc->sc_mode_bytes[0] == mode_value)
		return (err);

	/*
	 * XXX Need to wait at least 250ms for hardware to get
	 * ready. The device mode handling appears to be handled
	 * asynchronously and we should not issue these commands too
	 * quickly.
	 */
	pause("WHW", hz / 4);

	sc->sc_mode_bytes[0] = mode_value;
	return (usbd_req_set_report(sc->sc_usb_device, NULL /* mutex */,
	    sc->sc_mode_bytes, sizeof(sc->sc_mode_bytes), 0 /* interface idx */,
	    0x03 /* type */, 0x00 /* id */));
}

static void
atp_reset_callback(struct usb_xfer *xfer, usb_error_t error)
{
	usb_device_request_t   req;
	struct usb_page_cache *pc;
	struct atp_softc      *sc = usbd_xfer_softc(xfer);

	uint8_t mode_value;
	if (sc->sc_family == TRACKPAD_FAMILY_FOUNTAIN_GEYSER)
		mode_value = 0x04;
	else
		mode_value = RAW_SENSOR_MODE;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_SETUP:
		sc->sc_mode_bytes[0] = mode_value;
		req.bmRequestType = UT_WRITE_CLASS_INTERFACE;
		req.bRequest = UR_SET_REPORT;
		USETW2(req.wValue,
		    (uint8_t)0x03 /* type */, (uint8_t)0x00 /* id */);
		USETW(req.wIndex, 0);
		USETW(req.wLength, MODE_LENGTH);

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_in(pc, 0, &req, sizeof(req));
		pc = usbd_xfer_get_frame(xfer, 1);
		usbd_copy_in(pc, 0, sc->sc_mode_bytes, MODE_LENGTH);

		usbd_xfer_set_frame_len(xfer, 0, sizeof(req));
		usbd_xfer_set_frame_len(xfer, 1, MODE_LENGTH);
		usbd_xfer_set_frames(xfer, 2);
		usbd_transfer_submit(xfer);
		break;

	case USB_ST_TRANSFERRED:
	default:
		break;
	}
}

static int
atp_enable(struct atp_softc *sc)
{
	if (sc->sc_state & ATP_ENABLED)
		return (0);

	/* reset status */
	memset(&sc->sc_status, 0, sizeof(sc->sc_status));

	atp_init_stroke_pool(sc);

	sc->sc_state |= ATP_ENABLED;

	DPRINTFN(ATP_LLEVEL_INFO, "enabled atp\n");
	return (0);
}

static void
atp_disable(struct atp_softc *sc)
{
	sc->sc_state &= ~(ATP_ENABLED | ATP_VALID);
	DPRINTFN(ATP_LLEVEL_INFO, "disabled atp\n");
}

static void
fg_interpret_sensor_data(struct atp_softc *sc, u_int data_len)
{
	u_int n_xpspans = 0;
	u_int n_ypspans = 0;
	uint8_t status_bits;

	const struct fg_dev_params *params =
	    (const struct fg_dev_params *)sc->sc_params;

	fg_extract_sensor_data(sc->sc_sensor_data, params->n_xsensors, X,
	    sc->sc_cur_x, params->prot);
	fg_extract_sensor_data(sc->sc_sensor_data, params->n_ysensors, Y,
	    sc->sc_cur_y, params->prot);

	/*
	 * If this is the initial update (from an untouched
	 * pad), we should set the base values for the sensor
	 * data; deltas with respect to these base values can
	 * be used as pressure readings subsequently.
	 */
	status_bits = sc->sc_sensor_data[params->data_len - 1];
	if (((params->prot == FG_TRACKPAD_TYPE_GEYSER3) ||
	     (params->prot == FG_TRACKPAD_TYPE_GEYSER4))  &&
	    ((sc->sc_state & ATP_VALID) == 0)) {
		if (status_bits & FG_STATUS_BASE_UPDATE) {
			memcpy(sc->sc_base_x, sc->sc_cur_x,
			    params->n_xsensors * sizeof(*sc->sc_base_x));
			memcpy(sc->sc_base_y, sc->sc_cur_y,
			    params->n_ysensors * sizeof(*sc->sc_base_y));
			sc->sc_state |= ATP_VALID;
			return;
		}
	}

	/* Get pressure readings and detect p-spans for both axes. */
	fg_get_pressures(sc->sc_pressure_x, sc->sc_cur_x, sc->sc_base_x,
	    params->n_xsensors);
	fg_detect_pspans(sc->sc_pressure_x, params->n_xsensors,
	    FG_MAX_PSPANS_PER_AXIS, sc->sc_pspans_x, &n_xpspans);
	fg_get_pressures(sc->sc_pressure_y, sc->sc_cur_y, sc->sc_base_y,
	    params->n_ysensors);
	fg_detect_pspans(sc->sc_pressure_y, params->n_ysensors,
	    FG_MAX_PSPANS_PER_AXIS, sc->sc_pspans_y, &n_ypspans);

	/* Update strokes with new pspans to detect movements. */
	if (fg_update_strokes(sc, sc->sc_pspans_x, n_xpspans, sc->sc_pspans_y, n_ypspans))
		sc->sc_status.flags |= MOUSE_POSCHANGED;

	sc->sc_ibtn = (status_bits & FG_STATUS_BUTTON) ? MOUSE_BUTTON1DOWN : 0;
	sc->sc_status.button = sc->sc_ibtn;

	/*
	 * The Fountain/Geyser device continues to trigger interrupts
	 * at a fast rate even after touchpad activity has
	 * stopped. Upon detecting that the device has remained idle
	 * beyond a threshold, we reinitialize it to silence the
	 * interrupts.
	 */
	if ((sc->sc_status.flags  == 0) && (sc->sc_n_strokes == 0)) {
		sc->sc_idlecount++;
		if (sc->sc_idlecount >= ATP_IDLENESS_THRESHOLD) {
			/*
			 * Use the last frame before we go idle for
			 * calibration on pads which do not send
			 * calibration frames.
			 */
			const struct fg_dev_params *params =
			    (const struct fg_dev_params *)sc->sc_params;

			DPRINTFN(ATP_LLEVEL_INFO, "idle\n");

			if (params->prot < FG_TRACKPAD_TYPE_GEYSER3) {
				memcpy(sc->sc_base_x, sc->sc_cur_x,
				    params->n_xsensors * sizeof(*(sc->sc_base_x)));
				memcpy(sc->sc_base_y, sc->sc_cur_y,
				    params->n_ysensors * sizeof(*(sc->sc_base_y)));
			}

			sc->sc_idlecount = 0;
			usbd_transfer_start(sc->sc_xfer[ATP_RESET]);
		}
	} else {
		sc->sc_idlecount = 0;
	}
}

/*
 * Interpret the data from the X and Y pressure sensors. This function
 * is called separately for the X and Y sensor arrays. The data in the
 * USB packet is laid out in the following manner:
 *
 * sensor_data:
 *            --,--,Y1,Y2,--,Y3,Y4,--,Y5,...,Y10, ... X1,X2,--,X3,X4
 *  indices:   0  1  2  3  4  5  6  7  8 ...  15  ... 20 21 22 23 24
 *
 * '--' (in the above) indicates that the value is unimportant.
 *
 * Information about the above layout was obtained from the
 * implementation of the AppleTouch driver in Linux.
 *
 * parameters:
 *   sensor_data
 *       raw sensor data from the USB packet.
 *   num
 *       The number of elements in the array 'arr'.
 *   axis
 *       Axis of data to fetch
 *   arr
 *       The array to be initialized with the readings.
 *   prot
 *       The protocol to use to interpret the data
 */
static void
fg_extract_sensor_data(const int8_t *sensor_data, u_int num, atp_axis axis,
    int	*arr, enum fountain_geyser_trackpad_type prot)
{
	u_int i;
	u_int di;   /* index into sensor data */

	switch (prot) {
	case FG_TRACKPAD_TYPE_GEYSER1:
		/*
		 * For Geyser 1, the sensors are laid out in pairs
		 * every 5 bytes.
		 */
		for (i = 0, di = (axis == Y) ? 1 : 2; i < 8; di += 5, i++) {
			arr[i] = sensor_data[di];
			arr[i+8] = sensor_data[di+2];
			if ((axis == X) && (num > 16))
				arr[i+16] = sensor_data[di+40];
		}

		break;
	case FG_TRACKPAD_TYPE_GEYSER2:
		for (i = 0, di = (axis == Y) ? 1 : 19; i < num; /* empty */ ) {
			arr[i++] = sensor_data[di++];
			arr[i++] = sensor_data[di++];
			di++;
		}
		break;
	case FG_TRACKPAD_TYPE_GEYSER3:
	case FG_TRACKPAD_TYPE_GEYSER4:
		for (i = 0, di = (axis == Y) ? 2 : 20; i < num; /* empty */ ) {
			arr[i++] = sensor_data[di++];
			arr[i++] = sensor_data[di++];
			di++;
		}
		break;
	default:
		break;
	}
}

static void
fg_get_pressures(int *p, const int *cur, const int *base, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		p[i] = cur[i] - base[i];
		if (p[i] > 127)
			p[i] -= 256;
		if (p[i] < -127)
			p[i] += 256;
		if (p[i] < 0)
			p[i] = 0;

		/*
		 * Shave off pressures below the noise-pressure
		 * threshold; this will reduce the contribution from
		 * lower pressure readings.
		 */
		if ((u_int)p[i] <= FG_SENSOR_NOISE_THRESHOLD)
			p[i] = 0; /* filter away noise */
		else
			p[i] -= FG_SENSOR_NOISE_THRESHOLD;
	}
}

static void
fg_detect_pspans(int *p, u_int num_sensors,
    u_int      max_spans, /* max # of pspans permitted */
    fg_pspan  *spans,     /* finger spans */
    u_int     *nspans_p)  /* num spans detected */
{
	u_int i;
	int   maxp;             /* max pressure seen within a span */
	u_int num_spans = 0;

	enum fg_pspan_state {
		ATP_PSPAN_INACTIVE,
		ATP_PSPAN_INCREASING,
		ATP_PSPAN_DECREASING,
	} state; /* state of the pressure span */

	/*
	 * The following is a simple state machine to track
	 * the phase of the pressure span.
	 */
	memset(spans, 0, max_spans * sizeof(fg_pspan));
	maxp = 0;
	state = ATP_PSPAN_INACTIVE;
	for (i = 0; i < num_sensors; i++) {
		if (num_spans >= max_spans)
			break;

		if (p[i] == 0) {
			if (state == ATP_PSPAN_INACTIVE) {
				/*
				 * There is no pressure information for this
				 * sensor, and we aren't tracking a finger.
				 */
				continue;
			} else {
				state = ATP_PSPAN_INACTIVE;
				maxp = 0;
				num_spans++;
			}
		} else {
			switch (state) {
			case ATP_PSPAN_INACTIVE:
				state = ATP_PSPAN_INCREASING;
				maxp  = p[i];
				break;

			case ATP_PSPAN_INCREASING:
				if (p[i] > maxp)
					maxp = p[i];
				else if (p[i] <= (maxp >> 1))
					state = ATP_PSPAN_DECREASING;
				break;

			case ATP_PSPAN_DECREASING:
				if (p[i] > p[i - 1]) {
					/*
					 * This is the beginning of
					 * another span; change state
					 * to give the appearance that
					 * we're starting from an
					 * inactive span, and then
					 * re-process this reading in
					 * the next iteration.
					 */
					num_spans++;
					state = ATP_PSPAN_INACTIVE;
					maxp  = 0;
					i--;
					continue;
				}
				break;
			}

			/* Update the finger span with this reading. */
			spans[num_spans].width++;
			spans[num_spans].cum += p[i];
			spans[num_spans].cog += p[i] * (i + 1);
		}
	}
	if (state != ATP_PSPAN_INACTIVE)
		num_spans++;    /* close the last finger span */

	/* post-process the spans */
	for (i = 0; i < num_spans; i++) {
		/* filter away unwanted pressure spans */
		if ((spans[i].cum < FG_PSPAN_MIN_CUM_PRESSURE) ||
		    (spans[i].width > FG_PSPAN_MAX_WIDTH)) {
			if ((i + 1) < num_spans) {
				memcpy(&spans[i], &spans[i + 1],
				    (num_spans - i - 1) * sizeof(fg_pspan));
				i--;
			}
			num_spans--;
			continue;
		}

		/* compute this span's representative location */
		spans[i].loc = spans[i].cog * FG_SCALE_FACTOR /
			spans[i].cum;

		spans[i].matched = false; /* not yet matched against a stroke */
	}

	*nspans_p = num_spans;
}

static void
wsp_interpret_sensor_data(struct atp_softc *sc, u_int data_len)
{
	const struct wsp_dev_params *params = sc->sc_params;
	wsp_finger_t fingers[WSP_MAX_FINGERS];
	struct wsp_finger_sensor_data *source_fingerp;
	u_int n_source_fingers;
	u_int n_fingers;
	u_int i;

	/* validate sensor data length */
	if ((data_len < params->finger_data_offset) ||
	    ((data_len - params->finger_data_offset) %
	     WSP_SIZEOF_FINGER_SENSOR_DATA) != 0)
		return;

	/* compute number of source fingers */
	n_source_fingers = (data_len - params->finger_data_offset) /
	    WSP_SIZEOF_FINGER_SENSOR_DATA;

	if (n_source_fingers > WSP_MAX_FINGERS)
		n_source_fingers = WSP_MAX_FINGERS;

	/* iterate over the source data collecting useful fingers */
	n_fingers = 0;
	source_fingerp = (struct wsp_finger_sensor_data *)(sc->sc_sensor_data +
	     params->finger_data_offset);

	for (i = 0; i < n_source_fingers; i++, source_fingerp++) {
		/* swap endianness, if any */
		if (le16toh(0x1234) != 0x1234) {
			source_fingerp->origin      = le16toh((uint16_t)source_fingerp->origin);
			source_fingerp->abs_x       = le16toh((uint16_t)source_fingerp->abs_x);
			source_fingerp->abs_y       = le16toh((uint16_t)source_fingerp->abs_y);
			source_fingerp->rel_x       = le16toh((uint16_t)source_fingerp->rel_x);
			source_fingerp->rel_y       = le16toh((uint16_t)source_fingerp->rel_y);
			source_fingerp->tool_major  = le16toh((uint16_t)source_fingerp->tool_major);
			source_fingerp->tool_minor  = le16toh((uint16_t)source_fingerp->tool_minor);
			source_fingerp->orientation = le16toh((uint16_t)source_fingerp->orientation);
			source_fingerp->touch_major = le16toh((uint16_t)source_fingerp->touch_major);
			source_fingerp->touch_minor = le16toh((uint16_t)source_fingerp->touch_minor);
			source_fingerp->multi       = le16toh((uint16_t)source_fingerp->multi);
		}

		/* check for minium threshold */
		if (source_fingerp->touch_major == 0)
			continue;

		fingers[n_fingers].matched = false;
		fingers[n_fingers].x       = source_fingerp->abs_x;
		fingers[n_fingers].y       = -source_fingerp->abs_y;

		n_fingers++;
	}

	if ((sc->sc_n_strokes == 0) && (n_fingers == 0))
		return;

	if (wsp_update_strokes(sc, fingers, n_fingers))
		sc->sc_status.flags |= MOUSE_POSCHANGED;

	switch(params->tp_type) {
	case WSP_TRACKPAD_TYPE2:
		sc->sc_ibtn = sc->sc_sensor_data[WSP_TYPE2_BUTTON_DATA_OFFSET];
		break;
	case WSP_TRACKPAD_TYPE3:
		sc->sc_ibtn = sc->sc_sensor_data[WSP_TYPE3_BUTTON_DATA_OFFSET];
		break;
	default:
		break;
	}
	sc->sc_status.button = sc->sc_ibtn ? MOUSE_BUTTON1DOWN : 0;
}

/*
 * Match a pressure-span against a stroke-component. If there is a
 * match, update the component's state and return true.
 */
static boolean_t
fg_match_stroke_component(fg_stroke_component_t *component,
    const fg_pspan *pspan, atp_stroke_type stroke_type)
{
	int   delta_mickeys;
	u_int min_pressure;

	delta_mickeys = pspan->loc - component->loc;

	if (abs(delta_mickeys) > (int)FG_MAX_DELTA_MICKEYS)
		return (false); /* the finger span is too far out; no match */

	component->loc = pspan->loc;

	/*
	 * A sudden and significant increase in a pspan's cumulative
	 * pressure indicates the incidence of a new finger
	 * contact. This usually revises the pspan's
	 * centre-of-gravity, and hence the location of any/all
	 * matching stroke component(s). But such a change should
	 * *not* be interpreted as a movement.
	 */
	if (pspan->cum > ((3 * component->cum_pressure) >> 1))
		delta_mickeys = 0;

	component->cum_pressure = pspan->cum;
	if (pspan->cum > component->max_cum_pressure)
		component->max_cum_pressure = pspan->cum;

	/*
	 * Disregard the component's movement if its cumulative
	 * pressure drops below a fraction of the maximum; this
	 * fraction is determined based on the stroke's type.
	 */
	if (stroke_type == ATP_STROKE_TOUCH)
		min_pressure = (3 * component->max_cum_pressure) >> 2;
	else
		min_pressure = component->max_cum_pressure >> 2;
	if (component->cum_pressure < min_pressure)
		delta_mickeys = 0;

	component->delta_mickeys = delta_mickeys;
	return (true);
}

static void
fg_match_strokes_against_pspans(struct atp_softc *sc, atp_axis axis,
    fg_pspan *pspans, u_int n_pspans, u_int repeat_count)
{
	atp_stroke_t *strokep;
	u_int repeat_index = 0;
	u_int i;

	/* Determine the index of the multi-span. */
	if (repeat_count) {
		for (i = 0; i < n_pspans; i++) {
			if (pspans[i].cum > pspans[repeat_index].cum)
				repeat_index = i;
		}
	}

	TAILQ_FOREACH(strokep, &sc->sc_stroke_used, entry) {
		if (strokep->components[axis].matched)
			continue; /* skip matched components */

		for (i = 0; i < n_pspans; i++) {
			if (pspans[i].matched)
				continue; /* skip matched pspans */

			if (fg_match_stroke_component(
			    &strokep->components[axis], &pspans[i],
			    strokep->type)) {

				/* There is a match. */
				strokep->components[axis].matched = true;

				/* Take care to repeat at the multi-span. */
				if ((repeat_count > 0) && (i == repeat_index))
					repeat_count--;
				else
					pspans[i].matched = true;

				break; /* skip to the next strokep */
			}
		} /* loop over pspans */
	} /* loop over strokes */
}

static boolean_t
wsp_match_strokes_against_fingers(struct atp_softc *sc,
    wsp_finger_t *fingers, u_int n_fingers)
{
	boolean_t movement = false;
	atp_stroke_t *strokep;
	u_int i;

	/* reset the matched status for all strokes */
	TAILQ_FOREACH(strokep, &sc->sc_stroke_used, entry)
		strokep->matched = false;

	for (i = 0; i != n_fingers; i++) {
		u_int least_distance_sq = WSP_MAX_ALLOWED_MATCH_DISTANCE_SQ;
		atp_stroke_t *strokep_best = NULL;

		TAILQ_FOREACH(strokep, &sc->sc_stroke_used, entry) {
			int instantaneous_dx;
			int instantaneous_dy;
			u_int d_squared;

			if (strokep->matched)
				continue;

			instantaneous_dx = fingers[i].x - strokep->x;
			instantaneous_dy = fingers[i].y - strokep->y;

			/* skip strokes which are far away */
			d_squared =
			    (instantaneous_dx * instantaneous_dx) +
			    (instantaneous_dy * instantaneous_dy);

			if (d_squared < least_distance_sq) {
				least_distance_sq = d_squared;
				strokep_best = strokep;
			}
		}

		strokep = strokep_best;

		if (strokep != NULL) {
			fingers[i].matched = true;

			strokep->matched          = true;
			strokep->instantaneous_dx = fingers[i].x - strokep->x;
			strokep->instantaneous_dy = fingers[i].y - strokep->y;
			strokep->x                = fingers[i].x;
			strokep->y                = fingers[i].y;

			atp_advance_stroke_state(sc, strokep, &movement);
		}
	}
	return (movement);
}

/*
 * Update strokes by matching against current pressure-spans.
 * Return true if any movement is detected.
 */
static boolean_t
fg_update_strokes(struct atp_softc *sc, fg_pspan *pspans_x,
    u_int n_xpspans, fg_pspan *pspans_y, u_int n_ypspans)
{
	atp_stroke_t *strokep;
	atp_stroke_t *strokep_next;
	boolean_t movement = false;
	u_int repeat_count = 0;
	u_int i;
	u_int j;

	/* Reset X and Y components of all strokes as unmatched. */
	TAILQ_FOREACH(strokep, &sc->sc_stroke_used, entry) {
		strokep->components[X].matched = false;
		strokep->components[Y].matched = false;
	}

	/*
	 * Usually, the X and Y pspans come in pairs (the common case
	 * being a single pair). It is possible, however, that
	 * multiple contacts resolve to a single pspan along an
	 * axis, as illustrated in the following:
	 *
	 *   F = finger-contact
	 *
	 *                pspan  pspan
	 *        +-----------------------+
	 *        |         .      .      |
	 *        |         .      .      |
	 *        |         .      .      |
	 *        |         .      .      |
	 *  pspan |.........F......F      |
	 *        |                       |
	 *        |                       |
	 *        |                       |
	 *        +-----------------------+
	 *
	 *
	 * The above case can be detected by a difference in the
	 * number of X and Y pspans. When this happens, X and Y pspans
	 * aren't easy to pair or match against strokes.
	 *
	 * When X and Y pspans differ in number, the axis with the
	 * smaller number of pspans is regarded as having a repeating
	 * pspan (or a multi-pspan)--in the above illustration, the
	 * Y-axis has a repeating pspan. Our approach is to try to
	 * match the multi-pspan repeatedly against strokes. The
	 * difference between the number of X and Y pspans gives us a
	 * crude repeat_count for matching multi-pspans--i.e. the
	 * multi-pspan along the Y axis (above) has a repeat_count of 1.
	 */
	repeat_count = abs(n_xpspans - n_ypspans);

	fg_match_strokes_against_pspans(sc, X, pspans_x, n_xpspans,
	    (((repeat_count != 0) && ((n_xpspans < n_ypspans))) ?
		repeat_count : 0));
	fg_match_strokes_against_pspans(sc, Y, pspans_y, n_ypspans,
	    (((repeat_count != 0) && (n_ypspans < n_xpspans)) ?
		repeat_count : 0));

	/* Update the state of strokes based on the above pspan matches. */
	TAILQ_FOREACH_SAFE(strokep, &sc->sc_stroke_used, entry, strokep_next) {

		if (strokep->components[X].matched &&
		    strokep->components[Y].matched) {
			strokep->matched = true;
			strokep->instantaneous_dx =
			    strokep->components[X].delta_mickeys;
			strokep->instantaneous_dy =
			    strokep->components[Y].delta_mickeys;
			atp_advance_stroke_state(sc, strokep, &movement);
		} else {
			/*
			 * At least one component of this stroke
			 * didn't match against current pspans;
			 * terminate it.
			 */
			atp_terminate_stroke(sc, strokep);
		}
	}

	/* Add new strokes for pairs of unmatched pspans */
	for (i = 0; i < n_xpspans; i++) {
		if (pspans_x[i].matched == false) break;
	}
	for (j = 0; j < n_ypspans; j++) {
		if (pspans_y[j].matched == false) break;
	}
	if ((i < n_xpspans) && (j < n_ypspans)) {
#ifdef USB_DEBUG
		if (atp_debug >= ATP_LLEVEL_INFO) {
			printf("unmatched pspans:");
			for (; i < n_xpspans; i++) {
				if (pspans_x[i].matched)
					continue;
				printf(" X:[loc:%u,cum:%u]",
				    pspans_x[i].loc, pspans_x[i].cum);
			}
			for (; j < n_ypspans; j++) {
				if (pspans_y[j].matched)
					continue;
				printf(" Y:[loc:%u,cum:%u]",
				    pspans_y[j].loc, pspans_y[j].cum);
			}
			printf("\n");
		}
#endif /* USB_DEBUG */
		if ((n_xpspans == 1) && (n_ypspans == 1))
			/* The common case of a single pair of new pspans. */
			fg_add_stroke(sc, &pspans_x[0], &pspans_y[0]);
		else
			fg_add_new_strokes(sc, pspans_x, n_xpspans,
			    pspans_y, n_ypspans);
	}

#ifdef USB_DEBUG
	if (atp_debug >= ATP_LLEVEL_INFO) {
		TAILQ_FOREACH(strokep, &sc->sc_stroke_used, entry) {
			printf(" %s%clc:%u,dm:%d,cum:%d,max:%d,%c"
			    ",%clc:%u,dm:%d,cum:%d,max:%d,%c",
			    (strokep->flags & ATSF_ZOMBIE) ? "zomb:" : "",
			    (strokep->type == ATP_STROKE_TOUCH) ? '[' : '<',
			    strokep->components[X].loc,
			    strokep->components[X].delta_mickeys,
			    strokep->components[X].cum_pressure,
			    strokep->components[X].max_cum_pressure,
			    (strokep->type == ATP_STROKE_TOUCH) ? ']' : '>',
			    (strokep->type == ATP_STROKE_TOUCH) ? '[' : '<',
			    strokep->components[Y].loc,
			    strokep->components[Y].delta_mickeys,
			    strokep->components[Y].cum_pressure,
			    strokep->components[Y].max_cum_pressure,
			    (strokep->type == ATP_STROKE_TOUCH) ? ']' : '>');
		}
		if (TAILQ_FIRST(&sc->sc_stroke_used) != NULL)
			printf("\n");
	}
#endif /* USB_DEBUG */
	return (movement);
}

/*
 * Update strokes by matching against current pressure-spans.
 * Return true if any movement is detected.
 */
static boolean_t
wsp_update_strokes(struct atp_softc *sc, wsp_finger_t *fingers, u_int n_fingers)
{
	boolean_t movement = false;
	atp_stroke_t *strokep_next;
	atp_stroke_t *strokep;
	u_int i;

	if (sc->sc_n_strokes > 0) {
		movement = wsp_match_strokes_against_fingers(
		    sc, fingers, n_fingers);

		/* handle zombie strokes */
		TAILQ_FOREACH_SAFE(strokep, &sc->sc_stroke_used, entry, strokep_next) {
			if (strokep->matched)
				continue;
			atp_terminate_stroke(sc, strokep);
		}
	}

	/* initialize unmatched fingers as strokes */
	for (i = 0; i != n_fingers; i++) {
		if (fingers[i].matched)
			continue;

		wsp_add_stroke(sc, fingers + i);
	}
	return (movement);
}

/* Initialize a stroke using a pressure-span. */
static void
fg_add_stroke(struct atp_softc *sc, const fg_pspan *pspan_x,
    const fg_pspan *pspan_y)
{
	atp_stroke_t *strokep;

	strokep = atp_alloc_stroke(sc);
	if (strokep == NULL)
		return;

	/*
	 * Strokes begin as potential touches. If a stroke survives
	 * longer than a threshold, or if it records significant
	 * cumulative movement, then it is considered a 'slide'.
	 */
	strokep->type    = ATP_STROKE_TOUCH;
	strokep->matched = false;
	microtime(&strokep->ctime);
	strokep->age     = 1;		/* number of interrupts */
	strokep->x       = pspan_x->loc;
	strokep->y       = pspan_y->loc;

	strokep->components[X].loc              = pspan_x->loc;
	strokep->components[X].cum_pressure     = pspan_x->cum;
	strokep->components[X].max_cum_pressure = pspan_x->cum;
	strokep->components[X].matched          = true;

	strokep->components[Y].loc              = pspan_y->loc;
	strokep->components[Y].cum_pressure     = pspan_y->cum;
	strokep->components[Y].max_cum_pressure = pspan_y->cum;
	strokep->components[Y].matched          = true;

	if (sc->sc_n_strokes > 1) {
		/* Reset double-tap-n-drag if we have more than one strokes. */
		sc->sc_state &= ~ATP_DOUBLE_TAP_DRAG;
	}

	DPRINTFN(ATP_LLEVEL_INFO, "[%u,%u], time: %u,%ld\n",
	    strokep->components[X].loc,
	    strokep->components[Y].loc,
	    (u_int)strokep->ctime.tv_sec,
	    (unsigned long int)strokep->ctime.tv_usec);
}

static void
fg_add_new_strokes(struct atp_softc *sc, fg_pspan *pspans_x,
    u_int n_xpspans, fg_pspan *pspans_y, u_int n_ypspans)
{
	fg_pspan spans[2][FG_MAX_PSPANS_PER_AXIS];
	u_int nspans[2];
	u_int i;
	u_int j;

	/* Copy unmatched pspans into the local arrays. */
	for (i = 0, nspans[X] = 0; i < n_xpspans; i++) {
		if (pspans_x[i].matched == false) {
			spans[X][nspans[X]] = pspans_x[i];
			nspans[X]++;
		}
	}
	for (j = 0, nspans[Y] = 0; j < n_ypspans; j++) {
		if (pspans_y[j].matched == false) {
			spans[Y][nspans[Y]] = pspans_y[j];
			nspans[Y]++;
		}
	}

	if (nspans[X] == nspans[Y]) {
		/* Create new strokes from pairs of unmatched pspans */
		for (i = 0, j = 0; (i < nspans[X]) && (j < nspans[Y]); i++, j++)
			fg_add_stroke(sc, &spans[X][i], &spans[Y][j]);
	} else {
		u_int    cum = 0;
		atp_axis repeat_axis;      /* axis with multi-pspans */
		u_int    repeat_count;     /* repeat count for the multi-pspan*/
		u_int    repeat_index = 0; /* index of the multi-span */

		repeat_axis  = (nspans[X] > nspans[Y]) ? Y : X;
		repeat_count = abs(nspans[X] - nspans[Y]);
		for (i = 0; i < nspans[repeat_axis]; i++) {
			if (spans[repeat_axis][i].cum > cum) {
				repeat_index = i;
				cum = spans[repeat_axis][i].cum;
			}
		}

		/* Create new strokes from pairs of unmatched pspans */
		i = 0, j = 0;
		for (; (i < nspans[X]) && (j < nspans[Y]); i++, j++) {
			fg_add_stroke(sc, &spans[X][i], &spans[Y][j]);

			/* Take care to repeat at the multi-pspan. */
			if (repeat_count > 0) {
				if ((repeat_axis == X) &&
				    (repeat_index == i)) {
					i--; /* counter loop increment */
					repeat_count--;
				} else if ((repeat_axis == Y) &&
				    (repeat_index == j)) {
					j--; /* counter loop increment */
					repeat_count--;
				}
			}
		}
	}
}

/* Initialize a stroke from an unmatched finger. */
static void
wsp_add_stroke(struct atp_softc *sc, const wsp_finger_t *fingerp)
{
	atp_stroke_t *strokep;

	strokep = atp_alloc_stroke(sc);
	if (strokep == NULL)
		return;

	/*
	 * Strokes begin as potential touches. If a stroke survives
	 * longer than a threshold, or if it records significant
	 * cumulative movement, then it is considered a 'slide'.
	 */
	strokep->type    = ATP_STROKE_TOUCH;
	strokep->matched = true;
	microtime(&strokep->ctime);
	strokep->age = 1;	/* number of interrupts */
	strokep->x = fingerp->x;
	strokep->y = fingerp->y;

	/* Reset double-tap-n-drag if we have more than one strokes. */
	if (sc->sc_n_strokes > 1)
		sc->sc_state &= ~ATP_DOUBLE_TAP_DRAG;

	DPRINTFN(ATP_LLEVEL_INFO, "[%d,%d]\n", strokep->x, strokep->y);
}

static void
atp_advance_stroke_state(struct atp_softc *sc, atp_stroke_t *strokep,
    boolean_t *movementp)
{
	/* Revitalize stroke if it had previously been marked as a zombie. */
	if (strokep->flags & ATSF_ZOMBIE)
		strokep->flags &= ~ATSF_ZOMBIE;

	strokep->age++;
	if (strokep->age <= atp_stroke_maturity_threshold) {
		/* Avoid noise from immature strokes. */
		strokep->instantaneous_dx = 0;
		strokep->instantaneous_dy = 0;
	}

	if (atp_compute_stroke_movement(strokep))
		*movementp = true;

	if (strokep->type != ATP_STROKE_TOUCH)
		return;

	/* Convert touch strokes to slides upon detecting movement or age. */
	if ((abs(strokep->cum_movement_x) > atp_slide_min_movement) ||
	    (abs(strokep->cum_movement_y) > atp_slide_min_movement))
		atp_convert_to_slide(sc, strokep);
	else {
		/* Compute the stroke's age. */
		struct timeval tdiff;
		getmicrotime(&tdiff);
		if (timevalcmp(&tdiff, &strokep->ctime, >)) {
			timevalsub(&tdiff, &strokep->ctime);

			if ((tdiff.tv_sec > (atp_touch_timeout / 1000000)) ||
			    ((tdiff.tv_sec == (atp_touch_timeout / 1000000)) &&
			     (tdiff.tv_usec >= (atp_touch_timeout % 1000000))))
				atp_convert_to_slide(sc, strokep);
		}
	}
}

static boolean_t
atp_stroke_has_small_movement(const atp_stroke_t *strokep)
{
	return (((u_int)abs(strokep->instantaneous_dx) <=
		 atp_small_movement_threshold) &&
		((u_int)abs(strokep->instantaneous_dy) <=
		 atp_small_movement_threshold));
}

/*
 * Accumulate instantaneous changes into the stroke's 'pending' bucket; if
 * the aggregate exceeds the small_movement_threshold, then retain
 * instantaneous changes for later.
 */
static void
atp_update_pending_mickeys(atp_stroke_t *strokep)
{
	/* accumulate instantaneous movement */
	strokep->pending_dx += strokep->instantaneous_dx;
	strokep->pending_dy += strokep->instantaneous_dy;

#define UPDATE_INSTANTANEOUS_AND_PENDING(I, P)                          \
	if (abs((P)) <= atp_small_movement_threshold)                   \
		(I) = 0; /* clobber small movement */                   \
	else {                                                          \
		if ((I) > 0) {                                          \
			/*                                              \
			 * Round up instantaneous movement to the nearest \
			 * ceiling. This helps preserve small mickey    \
			 * movements from being lost in following scaling \
			 * operation.                                   \
			 */                                             \
			(I) = (((I) + (atp_mickeys_scale_factor - 1)) / \
			       atp_mickeys_scale_factor) *              \
			      atp_mickeys_scale_factor;                 \
									\
			/*                                              \
			 * Deduct the rounded mickeys from pending mickeys. \
			 * Note: we multiply by 2 to offset the previous \
			 * accumulation of instantaneous movement into  \
			 * pending.                                     \
			 */                                             \
			(P) -= ((I) << 1);                              \
									\
			/* truncate pending to 0 if it becomes negative. */ \
			(P) = imax((P), 0);                             \
		} else {                                                \
			/*                                              \
			 * Round down instantaneous movement to the nearest \
			 * ceiling. This helps preserve small mickey    \
			 * movements from being lost in following scaling \
			 * operation.                                   \
			 */                                             \
			(I) = (((I) - (atp_mickeys_scale_factor - 1)) / \
			       atp_mickeys_scale_factor) *              \
			      atp_mickeys_scale_factor;                 \
									\
			/*                                              \
			 * Deduct the rounded mickeys from pending mickeys. \
			 * Note: we multiply by 2 to offset the previous \
			 * accumulation of instantaneous movement into  \
			 * pending.                                     \
			 */                                             \
			(P) -= ((I) << 1);                              \
									\
			/* truncate pending to 0 if it becomes positive. */ \
			(P) = imin((P), 0);                             \
		}                                                       \
	}

	UPDATE_INSTANTANEOUS_AND_PENDING(strokep->instantaneous_dx,
	    strokep->pending_dx);
	UPDATE_INSTANTANEOUS_AND_PENDING(strokep->instantaneous_dy,
	    strokep->pending_dy);
}

/*
 * Compute a smoothened value for the stroke's movement from
 * instantaneous changes in the X and Y components.
 */
static boolean_t
atp_compute_stroke_movement(atp_stroke_t *strokep)
{
	/*
	 * Short movements are added first to the 'pending' bucket,
	 * and then acted upon only when their aggregate exceeds a
	 * threshold. This has the effect of filtering away movement
	 * noise.
	 */
	if (atp_stroke_has_small_movement(strokep))
		atp_update_pending_mickeys(strokep);
	else {                /* large movement */
		/* clear away any pending mickeys if there are large movements*/
		strokep->pending_dx = 0;
		strokep->pending_dy = 0;
	}

	/* scale movement */
	strokep->movement_dx = (strokep->instantaneous_dx) /
	    (int)atp_mickeys_scale_factor;
	strokep->movement_dy = (strokep->instantaneous_dy) /
	    (int)atp_mickeys_scale_factor;

	if ((abs(strokep->instantaneous_dx) >= ATP_FAST_MOVEMENT_TRESHOLD) ||
	    (abs(strokep->instantaneous_dy) >= ATP_FAST_MOVEMENT_TRESHOLD)) {
		strokep->movement_dx <<= 1;
		strokep->movement_dy <<= 1;
	}

	strokep->cum_movement_x += strokep->movement_dx;
	strokep->cum_movement_y += strokep->movement_dy;

	return ((strokep->movement_dx != 0) || (strokep->movement_dy != 0));
}

/*
 * Terminate a stroke. Aside from immature strokes, a slide or touch is
 * retained as a zombies so as to reap all their termination siblings
 * together; this helps establish the number of fingers involved at the
 * end of a multi-touch gesture.
 */
static void
atp_terminate_stroke(struct atp_softc *sc, atp_stroke_t *strokep)
{
	if (strokep->flags & ATSF_ZOMBIE)
		return;

	/* Drop immature strokes rightaway. */
	if (strokep->age <= atp_stroke_maturity_threshold) {
		atp_free_stroke(sc, strokep);
		return;
	}

	strokep->flags |= ATSF_ZOMBIE;
	sc->sc_state |= ATP_ZOMBIES_EXIST;

	callout_reset(&sc->sc_callout, ATP_ZOMBIE_STROKE_REAP_INTERVAL,
	    atp_reap_sibling_zombies, sc);

	/*
	 * Reset the double-click-n-drag at the termination of any
	 * slide stroke.
	 */
	if (strokep->type == ATP_STROKE_SLIDE)
		sc->sc_state &= ~ATP_DOUBLE_TAP_DRAG;
}

static boolean_t
atp_is_horizontal_scroll(const atp_stroke_t *strokep)
{
	if (abs(strokep->cum_movement_x) < atp_slide_min_movement)
		return (false);
	if (strokep->cum_movement_y == 0)
		return (true);
	return (abs(strokep->cum_movement_x / strokep->cum_movement_y) >= 4);
}

static boolean_t
atp_is_vertical_scroll(const atp_stroke_t *strokep)
{
	if (abs(strokep->cum_movement_y) < atp_slide_min_movement)
		return (false);
	if (strokep->cum_movement_x == 0)
		return (true);
	return (abs(strokep->cum_movement_y / strokep->cum_movement_x) >= 4);
}

static void
atp_reap_sibling_zombies(void *arg)
{
	struct atp_softc *sc = (struct atp_softc *)arg;
	u_int8_t n_touches_reaped = 0;
	u_int8_t n_slides_reaped = 0;
	u_int8_t n_horizontal_scrolls = 0;
	u_int8_t n_vertical_scrolls = 0;
	int horizontal_scroll = 0;
	int vertical_scroll = 0;
	atp_stroke_t *strokep;
	atp_stroke_t *strokep_next;

	DPRINTFN(ATP_LLEVEL_INFO, "\n");

	TAILQ_FOREACH_SAFE(strokep, &sc->sc_stroke_used, entry, strokep_next) {
		if ((strokep->flags & ATSF_ZOMBIE) == 0)
			continue;

		if (strokep->type == ATP_STROKE_TOUCH) {
			n_touches_reaped++;
		} else {
			n_slides_reaped++;

			if (atp_is_horizontal_scroll(strokep)) {
				n_horizontal_scrolls++;
				horizontal_scroll += strokep->cum_movement_x;
			} else if (atp_is_vertical_scroll(strokep)) {
				n_vertical_scrolls++;
				vertical_scroll +=  strokep->cum_movement_y;
			}
		}

		atp_free_stroke(sc, strokep);
	}

	DPRINTFN(ATP_LLEVEL_INFO, "reaped %u zombies\n",
	    n_touches_reaped + n_slides_reaped);
	sc->sc_state &= ~ATP_ZOMBIES_EXIST;

	/* No further processing necessary if physical button is depressed. */
	if (sc->sc_ibtn != 0)
		return;

	if ((n_touches_reaped == 0) && (n_slides_reaped == 0))
		return;

	/* Add a pair of virtual button events (button-down and button-up) if
	 * the physical button isn't pressed. */
	if (n_touches_reaped != 0) {
		if (n_touches_reaped < atp_tap_minimum)
			return;

		switch (n_touches_reaped) {
		case 1:
			atp_add_to_queue(sc, 0, 0, 0, MOUSE_BUTTON1DOWN);
			microtime(&sc->sc_touch_reap_time); /* remember this time */
			break;
		case 2:
			atp_add_to_queue(sc, 0, 0, 0, MOUSE_BUTTON3DOWN);
			break;
		case 3:
			atp_add_to_queue(sc, 0, 0, 0, MOUSE_BUTTON2DOWN);
			break;
		default:
			/* we handle taps of only up to 3 fingers */
			return;
		}
		atp_add_to_queue(sc, 0, 0, 0, 0); /* button release */

	} else if ((n_slides_reaped == 2) && (n_horizontal_scrolls == 2)) {
		if (horizontal_scroll < 0)
			atp_add_to_queue(sc, 0, 0, 0, MOUSE_BUTTON4DOWN);
		else
			atp_add_to_queue(sc, 0, 0, 0, MOUSE_BUTTON5DOWN);
		atp_add_to_queue(sc, 0, 0, 0, 0); /* button release */
	}
}

/* Switch a given touch stroke to being a slide. */
static void
atp_convert_to_slide(struct atp_softc *sc, atp_stroke_t *strokep)
{
	strokep->type = ATP_STROKE_SLIDE;

	/* Are we at the beginning of a double-click-n-drag? */
	if ((sc->sc_n_strokes == 1) &&
	    ((sc->sc_state & ATP_ZOMBIES_EXIST) == 0) &&
	    timevalcmp(&strokep->ctime, &sc->sc_touch_reap_time, >)) {
		struct timeval delta;
		struct timeval window = {
			atp_double_tap_threshold / 1000000,
			atp_double_tap_threshold % 1000000
		};

		delta = strokep->ctime;
		timevalsub(&delta, &sc->sc_touch_reap_time);
		if (timevalcmp(&delta, &window, <=))
			sc->sc_state |= ATP_DOUBLE_TAP_DRAG;
	}
}

static void
atp_reset_buf(struct atp_softc *sc)
{
	/* reset read queue */
	usb_fifo_reset(sc->sc_fifo.fp[USB_FIFO_RX]);
}

static void
atp_add_to_queue(struct atp_softc *sc, int dx, int dy, int dz,
    uint32_t buttons_in)
{
	uint32_t buttons_out;
	uint8_t  buf[8];

	dx = imin(dx,  254); dx = imax(dx, -256);
	dy = imin(dy,  254); dy = imax(dy, -256);
	dz = imin(dz,  126); dz = imax(dz, -128);

	buttons_out = MOUSE_MSC_BUTTONS;
	if (buttons_in & MOUSE_BUTTON1DOWN)
		buttons_out &= ~MOUSE_MSC_BUTTON1UP;
	else if (buttons_in & MOUSE_BUTTON2DOWN)
		buttons_out &= ~MOUSE_MSC_BUTTON2UP;
	else if (buttons_in & MOUSE_BUTTON3DOWN)
		buttons_out &= ~MOUSE_MSC_BUTTON3UP;

	DPRINTFN(ATP_LLEVEL_INFO, "dx=%d, dy=%d, buttons=%x\n",
	    dx, dy, buttons_out);

	/* Encode the mouse data in standard format; refer to mouse(4) */
	buf[0] = sc->sc_mode.syncmask[1];
	buf[0] |= buttons_out;
	buf[1] = dx >> 1;
	buf[2] = dy >> 1;
	buf[3] = dx - (dx >> 1);
	buf[4] = dy - (dy >> 1);
	/* Encode extra bytes for level 1 */
	if (sc->sc_mode.level == 1) {
		buf[5] = dz >> 1;
		buf[6] = dz - (dz >> 1);
		buf[7] = (((~buttons_in) >> 3) & MOUSE_SYS_EXTBUTTONS);
	}

	usb_fifo_put_data_linear(sc->sc_fifo.fp[USB_FIFO_RX], buf,
	    sc->sc_mode.packetsize, 1);
}

static int
atp_probe(device_t self)
{
	struct usb_attach_arg *uaa = device_get_ivars(self);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);

	if (uaa->info.bInterfaceClass != UICLASS_HID)
		return (ENXIO);
	/*
	 * Note: for some reason, the check
	 * (uaa->info.bInterfaceProtocol == UIPROTO_MOUSE) doesn't hold true
	 * for wellspring trackpads, so we've removed it from the common path.
	 */

	if ((usbd_lookup_id_by_uaa(fg_devs, sizeof(fg_devs), uaa)) == 0)
		return ((uaa->info.bInterfaceProtocol == UIPROTO_MOUSE) ?
			0 : ENXIO);

	if ((usbd_lookup_id_by_uaa(wsp_devs, sizeof(wsp_devs), uaa)) == 0)
		if (uaa->info.bIfaceIndex == WELLSPRING_INTERFACE_INDEX)
			return (0);

	return (ENXIO);
}

static int
atp_attach(device_t dev)
{
	struct atp_softc      *sc  = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	usb_error_t            err;
	void *descriptor_ptr = NULL;
	uint16_t descriptor_len;
	unsigned long di;

	DPRINTFN(ATP_LLEVEL_INFO, "sc=%p\n", sc);

	sc->sc_dev        = dev;
	sc->sc_usb_device = uaa->device;

	/* Get HID descriptor */
	if (usbd_req_get_hid_desc(uaa->device, NULL, &descriptor_ptr,
	    &descriptor_len, M_TEMP, uaa->info.bIfaceIndex) !=
	    USB_ERR_NORMAL_COMPLETION)
		return (ENXIO);

	/* Get HID report descriptor length */
	sc->sc_expected_sensor_data_len = hid_report_size(descriptor_ptr,
	    descriptor_len, hid_input, NULL);
	free(descriptor_ptr, M_TEMP);

	if ((sc->sc_expected_sensor_data_len <= 0) ||
	    (sc->sc_expected_sensor_data_len > ATP_SENSOR_DATA_BUF_MAX)) {
		DPRINTF("atp_attach: datalength invalid or too large: %d\n",
			sc->sc_expected_sensor_data_len);
		return (ENXIO);
	}

	/*
	 * By default the touchpad behaves like an HID device, sending
	 * packets with reportID = 2. Such reports contain only
	 * limited information--they encode movement deltas and button
	 * events,--but do not include data from the pressure
	 * sensors. The device input mode can be switched from HID
	 * reports to raw sensor data using vendor-specific USB
	 * control commands.
	 */
	if ((err = atp_set_device_mode(sc, RAW_SENSOR_MODE)) != 0) {
		DPRINTF("failed to set mode to 'RAW_SENSOR' (%d)\n", err);
		return (ENXIO);
	}

	mtx_init(&sc->sc_mutex, "atpmtx", NULL, MTX_DEF | MTX_RECURSE);

	di = USB_GET_DRIVER_INFO(uaa);

	sc->sc_family = DECODE_FAMILY_FROM_DRIVER_INFO(di);

	switch(sc->sc_family) {
	case TRACKPAD_FAMILY_FOUNTAIN_GEYSER:
		sc->sc_params =
		    &fg_dev_params[DECODE_PRODUCT_FROM_DRIVER_INFO(di)];
		sc->sensor_data_interpreter = fg_interpret_sensor_data;
		break;
	case TRACKPAD_FAMILY_WELLSPRING:
		sc->sc_params =
		    &wsp_dev_params[DECODE_PRODUCT_FROM_DRIVER_INFO(di)];
		sc->sensor_data_interpreter = wsp_interpret_sensor_data;
		break;
	default:
		goto detach;
	}

	err = usbd_transfer_setup(uaa->device,
	    &uaa->info.bIfaceIndex, sc->sc_xfer, atp_xfer_config,
	    ATP_N_TRANSFER, sc, &sc->sc_mutex);
	if (err) {
		DPRINTF("error=%s\n", usbd_errstr(err));
		goto detach;
	}

	if (usb_fifo_attach(sc->sc_usb_device, sc, &sc->sc_mutex,
	    &atp_fifo_methods, &sc->sc_fifo,
	    device_get_unit(dev), -1, uaa->info.bIfaceIndex,
	    UID_ROOT, GID_OPERATOR, 0644)) {
		goto detach;
	}

	device_set_usb_desc(dev);

	sc->sc_hw.buttons       = 3;
	sc->sc_hw.iftype        = MOUSE_IF_USB;
	sc->sc_hw.type          = MOUSE_PAD;
	sc->sc_hw.model         = MOUSE_MODEL_GENERIC;
	sc->sc_hw.hwid          = 0;
	sc->sc_mode.protocol    = MOUSE_PROTO_MSC;
	sc->sc_mode.rate        = -1;
	sc->sc_mode.resolution  = MOUSE_RES_UNKNOWN;
	sc->sc_mode.packetsize  = MOUSE_MSC_PACKETSIZE;
	sc->sc_mode.syncmask[0] = MOUSE_MSC_SYNCMASK;
	sc->sc_mode.syncmask[1] = MOUSE_MSC_SYNC;
	sc->sc_mode.accelfactor = 0;
	sc->sc_mode.level       = 0;

	sc->sc_state            = 0;
	sc->sc_ibtn             = 0;

	callout_init_mtx(&sc->sc_callout, &sc->sc_mutex, 0);

	return (0);

detach:
	atp_detach(dev);
	return (ENOMEM);
}

static int
atp_detach(device_t dev)
{
	struct atp_softc *sc;

	sc = device_get_softc(dev);
	atp_set_device_mode(sc, HID_MODE);

	mtx_lock(&sc->sc_mutex);
	callout_drain(&sc->sc_callout);
	if (sc->sc_state & ATP_ENABLED)
		atp_disable(sc);
	mtx_unlock(&sc->sc_mutex);

	usb_fifo_detach(&sc->sc_fifo);

	usbd_transfer_unsetup(sc->sc_xfer, ATP_N_TRANSFER);

	mtx_destroy(&sc->sc_mutex);

	return (0);
}

static void
atp_intr(struct usb_xfer *xfer, usb_error_t error)
{
	struct atp_softc      *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	int len;

	usbd_xfer_status(xfer, &len, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, sc->sc_sensor_data, len);
		if (len < sc->sc_expected_sensor_data_len) {
			/* make sure we don't process old data */
			memset(sc->sc_sensor_data + len, 0,
			    sc->sc_expected_sensor_data_len - len);
		}

		sc->sc_status.flags &= ~(MOUSE_STDBUTTONSCHANGED |
		    MOUSE_POSCHANGED);
		sc->sc_status.obutton = sc->sc_status.button;

		(sc->sensor_data_interpreter)(sc, len);

		if (sc->sc_status.button != 0) {
			/* Reset DOUBLE_TAP_N_DRAG if the button is pressed. */
			sc->sc_state &= ~ATP_DOUBLE_TAP_DRAG;
		} else if (sc->sc_state & ATP_DOUBLE_TAP_DRAG) {
			/* Assume a button-press with DOUBLE_TAP_N_DRAG. */
			sc->sc_status.button = MOUSE_BUTTON1DOWN;
		}

		sc->sc_status.flags |=
		    sc->sc_status.button ^ sc->sc_status.obutton;
		if (sc->sc_status.flags & MOUSE_STDBUTTONSCHANGED) {
		    DPRINTFN(ATP_LLEVEL_INFO, "button %s\n",
			((sc->sc_status.button & MOUSE_BUTTON1DOWN) ?
			"pressed" : "released"));
		}

		if (sc->sc_status.flags & (MOUSE_POSCHANGED |
		    MOUSE_STDBUTTONSCHANGED)) {

			atp_stroke_t *strokep;
			u_int8_t n_movements = 0;
			int dx = 0;
			int dy = 0;
			int dz = 0;

			TAILQ_FOREACH(strokep, &sc->sc_stroke_used, entry) {
				if (strokep->flags & ATSF_ZOMBIE)
					continue;

				dx += strokep->movement_dx;
				dy += strokep->movement_dy;
				if (strokep->movement_dx ||
				    strokep->movement_dy)
					n_movements++;
			}

			/* average movement if multiple strokes record motion.*/
			if (n_movements > 1) {
				dx /= (int)n_movements;
				dy /= (int)n_movements;
			}

			/* detect multi-finger vertical scrolls */
			if (n_movements >= 2) {
				boolean_t all_vertical_scrolls = true;
				TAILQ_FOREACH(strokep, &sc->sc_stroke_used, entry) {
					if (strokep->flags & ATSF_ZOMBIE)
						continue;

					if (!atp_is_vertical_scroll(strokep))
						all_vertical_scrolls = false;
				}
				if (all_vertical_scrolls) {
					dz = dy;
					dy = dx = 0;
				}
			}

			sc->sc_status.dx += dx;
			sc->sc_status.dy += dy;
			sc->sc_status.dz += dz;
			atp_add_to_queue(sc, dx, -dy, -dz, sc->sc_status.button);
		}

	case USB_ST_SETUP:
	tr_setup:
		/* check if we can put more data into the FIFO */
		if (usb_fifo_put_bytes_max(sc->sc_fifo.fp[USB_FIFO_RX]) != 0) {
			usbd_xfer_set_frame_len(xfer, 0,
			    sc->sc_expected_sensor_data_len);
			usbd_transfer_submit(xfer);
		}
		break;

	default:                        /* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
atp_start_read(struct usb_fifo *fifo)
{
	struct atp_softc *sc = usb_fifo_softc(fifo);
	int rate;

	/* Check if we should override the default polling interval */
	rate = sc->sc_pollrate;
	/* Range check rate */
	if (rate > 1000)
		rate = 1000;
	/* Check for set rate */
	if ((rate > 0) && (sc->sc_xfer[ATP_INTR_DT] != NULL)) {
		/* Stop current transfer, if any */
		usbd_transfer_stop(sc->sc_xfer[ATP_INTR_DT]);
		/* Set new interval */
		usbd_xfer_set_interval(sc->sc_xfer[ATP_INTR_DT], 1000 / rate);
		/* Only set pollrate once */
		sc->sc_pollrate = 0;
	}

	usbd_transfer_start(sc->sc_xfer[ATP_INTR_DT]);
}

static void
atp_stop_read(struct usb_fifo *fifo)
{
	struct atp_softc *sc = usb_fifo_softc(fifo);
	usbd_transfer_stop(sc->sc_xfer[ATP_INTR_DT]);
}

static int
atp_open(struct usb_fifo *fifo, int fflags)
{
	struct atp_softc *sc = usb_fifo_softc(fifo);

	/* check for duplicate open, should not happen */
	if (sc->sc_fflags & fflags)
		return (EBUSY);

	/* check for first open */
	if (sc->sc_fflags == 0) {
		int rc;
		if ((rc = atp_enable(sc)) != 0)
			return (rc);
	}

	if (fflags & FREAD) {
		if (usb_fifo_alloc_buffer(fifo,
		    ATP_FIFO_BUF_SIZE, ATP_FIFO_QUEUE_MAXLEN)) {
			return (ENOMEM);
		}
	}

	sc->sc_fflags |= (fflags & (FREAD | FWRITE));
	return (0);
}

static void
atp_close(struct usb_fifo *fifo, int fflags)
{
	struct atp_softc *sc = usb_fifo_softc(fifo);
	if (fflags & FREAD)
		usb_fifo_free_buffer(fifo);

	sc->sc_fflags &= ~(fflags & (FREAD | FWRITE));
	if (sc->sc_fflags == 0) {
		atp_disable(sc);
	}
}

static int
atp_ioctl(struct usb_fifo *fifo, u_long cmd, void *addr, int fflags)
{
	struct atp_softc *sc = usb_fifo_softc(fifo);
	mousemode_t mode;
	int error = 0;

	mtx_lock(&sc->sc_mutex);

	switch(cmd) {
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
			break;
		}
		sc->sc_mode.level = mode.level;
		sc->sc_pollrate   = mode.rate;
		sc->sc_hw.buttons = 3;

		if (sc->sc_mode.level == 0) {
			sc->sc_mode.protocol    = MOUSE_PROTO_MSC;
			sc->sc_mode.packetsize  = MOUSE_MSC_PACKETSIZE;
			sc->sc_mode.syncmask[0] = MOUSE_MSC_SYNCMASK;
			sc->sc_mode.syncmask[1] = MOUSE_MSC_SYNC;
		} else if (sc->sc_mode.level == 1) {
			sc->sc_mode.protocol    = MOUSE_PROTO_SYSMOUSE;
			sc->sc_mode.packetsize  = MOUSE_SYS_PACKETSIZE;
			sc->sc_mode.syncmask[0] = MOUSE_SYS_SYNCMASK;
			sc->sc_mode.syncmask[1] = MOUSE_SYS_SYNC;
		}
		atp_reset_buf(sc);
		break;
	case MOUSE_GETLEVEL:
		*(int *)addr = sc->sc_mode.level;
		break;
	case MOUSE_SETLEVEL:
		if ((*(int *)addr < 0) || (*(int *)addr > 1)) {
			error = EINVAL;
			break;
		}
		sc->sc_mode.level = *(int *)addr;
		sc->sc_hw.buttons = 3;

		if (sc->sc_mode.level == 0) {
			sc->sc_mode.protocol    = MOUSE_PROTO_MSC;
			sc->sc_mode.packetsize  = MOUSE_MSC_PACKETSIZE;
			sc->sc_mode.syncmask[0] = MOUSE_MSC_SYNCMASK;
			sc->sc_mode.syncmask[1] = MOUSE_MSC_SYNC;
		} else if (sc->sc_mode.level == 1) {
			sc->sc_mode.protocol    = MOUSE_PROTO_SYSMOUSE;
			sc->sc_mode.packetsize  = MOUSE_SYS_PACKETSIZE;
			sc->sc_mode.syncmask[0] = MOUSE_SYS_SYNCMASK;
			sc->sc_mode.syncmask[1] = MOUSE_SYS_SYNC;
		}
		atp_reset_buf(sc);
		break;
	case MOUSE_GETSTATUS: {
		mousestatus_t *status = (mousestatus_t *)addr;

		*status = sc->sc_status;
		sc->sc_status.obutton = sc->sc_status.button;
		sc->sc_status.button  = 0;
		sc->sc_status.dx      = 0;
		sc->sc_status.dy      = 0;
		sc->sc_status.dz      = 0;

		if (status->dx || status->dy || status->dz)
			status->flags |= MOUSE_POSCHANGED;
		if (status->button != status->obutton)
			status->flags |= MOUSE_BUTTONSCHANGED;
		break;
	}

	default:
		error = ENOTTY;
		break;
	}

	mtx_unlock(&sc->sc_mutex);
	return (error);
}

static int
atp_sysctl_scale_factor_handler(SYSCTL_HANDLER_ARGS)
{
	int error;
	u_int tmp;

	tmp = atp_mickeys_scale_factor;
	error = sysctl_handle_int(oidp, &tmp, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);

	if (tmp == atp_mickeys_scale_factor)
		return (0);     /* no change */
	if ((tmp == 0) || (tmp > (10 * ATP_SCALE_FACTOR)))
		return (EINVAL);

	atp_mickeys_scale_factor = tmp;
	DPRINTFN(ATP_LLEVEL_INFO, "%s: resetting mickeys_scale_factor to %u\n",
	    ATP_DRIVER_NAME, tmp);

	return (0);
}

static devclass_t atp_devclass;

static device_method_t atp_methods[] = {
	DEVMETHOD(device_probe,  atp_probe),
	DEVMETHOD(device_attach, atp_attach),
	DEVMETHOD(device_detach, atp_detach),

	DEVMETHOD_END
};

static driver_t atp_driver = {
	.name    = ATP_DRIVER_NAME,
	.methods = atp_methods,
	.size    = sizeof(struct atp_softc)
};

DRIVER_MODULE(atp, uhub, atp_driver, atp_devclass, NULL, 0);
MODULE_DEPEND(atp, usb, 1, 1, 1);
MODULE_VERSION(atp, 1);
USB_PNP_HOST_INFO(fg_devs);
USB_PNP_HOST_INFO(wsp_devs);
