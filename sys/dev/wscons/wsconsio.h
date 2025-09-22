/* $OpenBSD: wsconsio.h,v 1.102 2024/09/30 01:41:49 jsg Exp $ */
/* $NetBSD: wsconsio.h,v 1.74 2005/04/28 07:15:44 martin Exp $ */

/*
 * Copyright (c) 1996, 1997 Christopher G. Demetriou.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_WSCONS_WSCONSIO_H_
#define	_DEV_WSCONS_WSCONSIO_H_

/*
 * WSCONS (wsdisplay, wskbd, wsmouse) exported interfaces.
 *
 * Ioctls are all in group 'W'.  Ioctl number space is partitioned like:
 *	0-31	keyboard ioctls (WSKBDIO)
 *	32-63	mouse ioctls (WSMOUSEIO)
 *	64-95	display ioctls (WSDISPLAYIO)
 *	96-127	mux ioctls (WSMUXIO)
 *	128-159 driver private ioctls
 *	160-255 reserved for future use
 */

#include <sys/types.h>
#include <sys/ioccom.h>
#include <dev/wscons/wsksymvar.h>

#include <sys/pciio.h>

#define	WSSCREEN_NAME_SIZE	16
#define	WSEMUL_NAME_SIZE	16
#define	WSFONT_NAME_SIZE	32

/*
 * Common event structure (used by keyboard and mouse)
 */
struct wscons_event {
	u_int		type;
	int		value;
	struct timespec	time;
};

/* Event type definitions.  Comment for each is information in value. */
#define	WSCONS_EVENT_KEY_UP		1	/* key code */
#define	WSCONS_EVENT_KEY_DOWN		2	/* key code */
#define	WSCONS_EVENT_ALL_KEYS_UP	3	/* void */
#define	WSCONS_EVENT_MOUSE_UP		4	/* button # (leftmost = 0) */
#define	WSCONS_EVENT_MOUSE_DOWN		5	/* button # (leftmost = 0)  */
#define	WSCONS_EVENT_MOUSE_DELTA_X	6	/* X delta amount */
#define	WSCONS_EVENT_MOUSE_DELTA_Y	7	/* Y delta amount */
#define	WSCONS_EVENT_MOUSE_ABSOLUTE_X	8	/* X location */
#define	WSCONS_EVENT_MOUSE_ABSOLUTE_Y	9	/* Y location */
#define	WSCONS_EVENT_MOUSE_DELTA_Z	10	/* Z delta amount */
#define	WSCONS_EVENT_MOUSE_ABSOLUTE_Z	11	/* (legacy, see below) */
				     /*	12-15, see below */
#define	WSCONS_EVENT_MOUSE_DELTA_W	16	/* W delta amount */
#define	WSCONS_EVENT_MOUSE_ABSOLUTE_W	17	/* (legacy, see below) */
#define	WSCONS_EVENT_SYNC		18
/*
 * Following events are not real wscons_event but are used as parameters of the
 * WSDISPLAYIO_WSMOUSED ioctl
 */
#define WSCONS_EVENT_WSMOUSED_ON	12	/* wsmoused(8) active */
#define WSCONS_EVENT_WSMOUSED_OFF	13	/* wsmoused(8) inactive */

#define IS_MOTION_EVENT(type) (((type) == WSCONS_EVENT_MOUSE_DELTA_X) || \
			       ((type) == WSCONS_EVENT_MOUSE_DELTA_Y) || \
			       ((type) == WSCONS_EVENT_MOUSE_DELTA_Z) || \
			       ((type) == WSCONS_EVENT_MOUSE_DELTA_W))
#define IS_BUTTON_EVENT(type) (((type) == WSCONS_EVENT_MOUSE_UP) || \
			       ((type) == WSCONS_EVENT_MOUSE_DOWN))
#define IS_CTRL_EVENT(type) ((type == WSCONS_EVENT_WSMOUSED_ON) || \
			     (type == WSCONS_EVENT_WSMOUSED_OFF))


/*
 * (Single-) Touch Events
 *
 * A RESET event will be generated whenever a change of X and Y is
 * coupled with a change of the contact count, or with a change of
 * the pointer-controlling MT slot.
 */
#define	WSCONS_EVENT_TOUCH_PRESSURE	WSCONS_EVENT_MOUSE_ABSOLUTE_Z
#define	WSCONS_EVENT_TOUCH_CONTACTS	WSCONS_EVENT_MOUSE_ABSOLUTE_W

#define	WSCONS_EVENT_TOUCH_WIDTH	24	/* contact width */
#define	WSCONS_EVENT_TOUCH_RESET	25	/* (no value) */

/*
 * Precision Scrolling
 */
#define WSCONS_EVENT_HSCROLL		26	/* dx * 4096 / scroll_unit */
#define WSCONS_EVENT_VSCROLL		27	/* dy * 4096 / scroll_unit */

/*
 * Keyboard ioctls (0 - 31)
 */

/* Get keyboard type. */
#define	WSKBDIO_GTYPE		_IOR('W', 0, u_int)
#define		WSKBD_TYPE_LK201	1	/* lk-201 */
#define		WSKBD_TYPE_LK401	2	/* lk-401 */
#define		WSKBD_TYPE_PC_XT	3	/* PC-ish, XT scancode */
#define		WSKBD_TYPE_PC_AT	4	/* PC-ish, AT scancode */
#define		WSKBD_TYPE_USB		5	/* USB, XT scancode */
#define		WSKBD_TYPE_NEXT		6	/* NeXT keyboard */
#define		WSKBD_TYPE_HPC_KBD	7	/* HPC builtin keyboard */
#define		WSKBD_TYPE_HPC_BTN	8	/* HPC/PsPC buttons */
#define		WSKBD_TYPE_ARCHIMEDES	9	/* Archimedes keyboard */
#define		WSKBD_TYPE_ADB		10	/* Apple ADB keyboard */
#define		WSKBD_TYPE_SUN		11	/* Sun Type3/4 */
#define		WSKBD_TYPE_SUN5		12	/* Sun Type5 */
#define		WSKBD_TYPE_HIL		13	/* HP HIL */
#define		WSKBD_TYPE_GSC		14	/* HP PS/2 */
#define		WSKBD_TYPE_LUNA		15	/* OMRON Luna */
#define		WSKBD_TYPE_ZAURUS	16	/* Sharp Zaurus */
#define		WSKBD_TYPE_DOMAIN	17	/* Apollo Domain */
#define		WSKBD_TYPE_BLUETOOTH	18	/* Bluetooth keyboard */
#define		WSKBD_TYPE_KPC		19	/* Palm keypad */
#define		WSKBD_TYPE_SGI		20	/* SGI serial keyboard */

/* Manipulate the keyboard bell. */
struct wskbd_bell_data {
	u_int	which;				/* values to get/set */
	u_int	pitch;				/* pitch, in Hz */
	u_int	period;				/* period, in milliseconds */
	u_int	volume;				/* percentage of max volume */
};
#define		WSKBD_BELL_DOPITCH	0x1		/* get/set pitch */
#define		WSKBD_BELL_DOPERIOD	0x2		/* get/set period */
#define		WSKBD_BELL_DOVOLUME	0x4		/* get/set volume */
#define		WSKBD_BELL_DOALL	0x7		/* all of the above */

#define	WSKBDIO_BELL		_IO('W', 1)
#define	WSKBDIO_COMPLEXBELL	_IOW('W', 2, struct wskbd_bell_data)
#define	WSKBDIO_SETBELL		_IOW('W', 3, struct wskbd_bell_data)
#define	WSKBDIO_GETBELL		_IOR('W', 4, struct wskbd_bell_data)
#define	WSKBDIO_SETDEFAULTBELL	_IOW('W', 5, struct wskbd_bell_data)
#define	WSKBDIO_GETDEFAULTBELL	_IOR('W', 6, struct wskbd_bell_data)

/* Manipulate the emulation key repeat settings. */
struct wskbd_keyrepeat_data {
	u_int	which;				/* values to get/set */
	u_int	del1;				/* delay before first, ms */
	u_int	delN;				/* delay before rest, ms */
};
#define		WSKBD_KEYREPEAT_DODEL1	0x1		/* get/set del1 */
#define		WSKBD_KEYREPEAT_DODELN	0x2		/* get/set delN */
#define		WSKBD_KEYREPEAT_DOALL	0x3		/* all of the above */

#define	WSKBDIO_SETKEYREPEAT	_IOW('W', 7, struct wskbd_keyrepeat_data)
#define	WSKBDIO_GETKEYREPEAT	_IOR('W', 8, struct wskbd_keyrepeat_data)
#define	WSKBDIO_SETDEFAULTKEYREPEAT \
	    _IOW('W', 9, struct wskbd_keyrepeat_data)
#define	WSKBDIO_GETDEFAULTKEYREPEAT \
	    _IOR('W', 10, struct wskbd_keyrepeat_data)

/* Get/set keyboard leds */
#define		WSKBD_LED_CAPS		0x01
#define		WSKBD_LED_NUM		0x02
#define		WSKBD_LED_SCROLL	0x04
#define		WSKBD_LED_COMPOSE	0x08

#define	WSKBDIO_SETLEDS		_IOW('W', 11, int)
#define	WSKBDIO_GETLEDS		_IOR('W', 12, int)

/* Manipulate keysym groups. */
struct wskbd_map_data {
	u_int	maplen;				/* number of entries in map */
#define WSKBDIO_MAXMAPLEN	65536
	struct wscons_keymap *map;		/* map to get or set */
};
#define WSKBDIO_GETMAP		_IOWR('W', 13, struct wskbd_map_data)
#define WSKBDIO_SETMAP		_IOW('W', 14, struct wskbd_map_data)
#define WSKBDIO_GETENCODING	_IOR('W', 15, kbd_t)
#define WSKBDIO_SETENCODING	_IOW('W', 16, kbd_t)

/* Get/set keyboard backlight.  Not applicable to all keyboard types. */
struct wskbd_backlight {
	unsigned int min, max, curval;
};
#define	WSKBDIO_GETBACKLIGHT	_IOR('W', 17, struct wskbd_backlight)
#define	WSKBDIO_SETBACKLIGHT	_IOW('W', 18, struct wskbd_backlight)

/* internal use only */
#define WSKBDIO_SETMODE		_IOW('W', 19, int)
#define WSKBDIO_GETMODE		_IOR('W', 20, int)
#define		WSKBD_TRANSLATED	0
#define		WSKBD_RAW		1

struct wskbd_encoding_data {
	int	nencodings;
	kbd_t	*encodings;
};
#define WSKBDIO_GETENCODINGS	_IOWR('W', 21, struct wskbd_encoding_data)

/*
 * Mouse ioctls (32 - 63)
 */

/* Get mouse type */
#define	WSMOUSEIO_GTYPE		_IOR('W', 32, u_int)
#define		WSMOUSE_TYPE_VSXXX	1	/* DEC serial */
#define		WSMOUSE_TYPE_PS2	2	/* PS/2-compatible */
#define		WSMOUSE_TYPE_USB	3	/* USB mouse */
#define		WSMOUSE_TYPE_LMS	4	/* Logitech busmouse */
#define		WSMOUSE_TYPE_MMS	5	/* Microsoft InPort mouse */
#define		WSMOUSE_TYPE_TPANEL	6	/* Generic Touch Panel */
#define		WSMOUSE_TYPE_NEXT	7	/* NeXT mouse */
#define		WSMOUSE_TYPE_ARCHIMEDES	8	/* Archimedes mouse */
#define		WSMOUSE_TYPE_ADB	9	/* ADB */
#define		WSMOUSE_TYPE_HIL	10	/* HP HIL */
#define		WSMOUSE_TYPE_LUNA	11	/* OMRON Luna */
#define		WSMOUSE_TYPE_DOMAIN	12	/* Apollo Domain */
#define		WSMOUSE_TYPE_BLUETOOTH	13	/* Bluetooth mouse */
#define		WSMOUSE_TYPE_SUN	14	/* SUN serial mouse */
#define		WSMOUSE_TYPE_SYNAPTICS	15	/* Synaptics touchpad */
#define		WSMOUSE_TYPE_ALPS	16	/* ALPS touchpad */
#define		WSMOUSE_TYPE_SGI	17	/* SGI serial mouse */
#define		WSMOUSE_TYPE_ELANTECH	18	/* Elantech touchpad */
#define		WSMOUSE_TYPE_SYNAP_SBTN	19	/* Synaptics soft buttons */
#define		WSMOUSE_TYPE_TOUCHPAD	20	/* Generic touchpad */

/* Set resolution.  Not applicable to all mouse types. */
#define	WSMOUSEIO_SRES		_IOW('W', 33, u_int)
#define		WSMOUSE_RES_MIN		0
#define		WSMOUSE_RES_DEFAULT	75
#define		WSMOUSE_RES_MAX		100

/* Set/get sample coordinates for calibration */
#define	WSMOUSE_CALIBCOORDS_MAX		16
#define	WSMOUSE_CALIBCOORDS_RESET	-1
struct wsmouse_calibcoords {
	int minx, miny;		/* minimum value of X/Y */
	int maxx, maxy;		/* maximum value of X/Y */
	int swapxy;		/* swap X/Y axis */
	int resx, resy;		/* X/Y resolution */
	int samplelen;		/* number of samples available or
				   WSMOUSE_CALIBCOORDS_RESET for raw mode */
	struct wsmouse_calibcoord {
		int rawx, rawy;	/* raw coordinate */
		int x, y;	/* translated coordinate */
	} samples[WSMOUSE_CALIBCOORDS_MAX];	/* sample coordinates */
};
#define	WSMOUSEIO_SCALIBCOORDS	_IOW('W', 36, struct wsmouse_calibcoords)
#define	WSMOUSEIO_GCALIBCOORDS	_IOR('W', 37, struct wsmouse_calibcoords)

#define	WSMOUSEIO_SETMODE	_IOW('W', 38, int)
#define		WSMOUSE_COMPAT		0
#define		WSMOUSE_NATIVE		1

/*
 * Keys of the configuration parameters in WSMOUSEIO_GETPARAMS/
 * WSMOUSEIO_SETPARAMS calls. Arbitrary subsets can be passed, provided
 * that all keys are valid and that the number of key/value pairs doesn't
 * exceed WSMOUSECFG_MAX.
 *
 * The keys are divided into various groups, which end with marker entries
 * of the form WSMOUSECFG__*.
 */
enum wsmousecfg {
 	/*
 	 * Coordinate handling.
 	 */
	WSMOUSECFG_DX_SCALE = 0,/* Xscale factor in [*.12] fixed-point format */
	WSMOUSECFG_DY_SCALE,	/* Yscale factor in [*.12] fixed-point format */
	WSMOUSECFG_PRESSURE_LO,	/* pressure limits defining start of touch */
	WSMOUSECFG_PRESSURE_HI,	/* pressure limits defining end of touch */
	WSMOUSECFG_TRKMAXDIST,	/* max distance to pair points for MT contact */
	WSMOUSECFG_SWAPXY,	/* swap X- and Y-axis */
	WSMOUSECFG_X_INV,	/* map absolute coordinate X to (INV - X) */
	WSMOUSECFG_Y_INV,	/* map absolute coordinate Y to (INV - Y) */
	WSMOUSECFG_REVERSE_SCROLLING,
				/* reverse scroll directions */

        WSMOUSECFG__FILTERS,

	/*
	 * Coordinate handling, applying only in WSMOUSE_COMPAT  mode.
	 */
	WSMOUSECFG_DX_MAX = 32,	/* ignore X deltas greater than this limit */
	WSMOUSECFG_DY_MAX,	/* ignore Y deltas greater than this limit */
	WSMOUSECFG_X_HYSTERESIS,/* retard value for X coordinates */
	WSMOUSECFG_Y_HYSTERESIS,/* retard value for Y coordinates */
	WSMOUSECFG_DECELERATION,/* threshold (distance) for deceleration */
	WSMOUSECFG_STRONG_HYSTERESIS,	/* FALSE and read-only, the fea-
					   ture is not supported anymore. */
	WSMOUSECFG_SMOOTHING,	/* smoothing factor (0-7) */

	WSMOUSECFG__TPFILTERS,

	/*
	 * Touchpad features
	 */
	WSMOUSECFG_SOFTBUTTONS = 64,	/* 2 soft-buttons at the bottom edge */
	WSMOUSECFG_SOFTMBTN,		/* add a middle-button area */
	WSMOUSECFG_TOPBUTTONS,		/* 3 soft-buttons at the top edge */
	WSMOUSECFG_TWOFINGERSCROLL,	/* enable two-finger scrolling */
	WSMOUSECFG_EDGESCROLL,		/* enable edge scrolling */
	WSMOUSECFG_HORIZSCROLL,		/* enable horizontal edge scrolling */
	WSMOUSECFG_SWAPSIDES,		/* invert soft-button/scroll areas */
	WSMOUSECFG_DISABLE,		/* disable all output except for
					   clicks in the top-button area */
	WSMOUSECFG_MTBUTTONS,		/* multi-touch buttons */

	WSMOUSECFG__TPFEATURES,

	/*
	 * Touchpad options
	 */
	WSMOUSECFG_LEFT_EDGE = 128,	/* ratio: left edge / total width */
	WSMOUSECFG_RIGHT_EDGE,		/* ratio: right edge / total width */
	WSMOUSECFG_TOP_EDGE,		/* ratio: top edge / total height */
	WSMOUSECFG_BOTTOM_EDGE,		/* ratio: bottom edge / total height */
	WSMOUSECFG_CENTERWIDTH,		/* ratio: center width / total width */
	WSMOUSECFG_HORIZSCROLLDIST,	/* distance mapped to a scroll event */
	WSMOUSECFG_VERTSCROLLDIST,	/* distance mapped to a scroll event */
	WSMOUSECFG_F2WIDTH,		/* width limit for single touches */
	WSMOUSECFG_F2PRESSURE,		/* pressure limit for single touches */
	WSMOUSECFG_TAP_MAXTIME,		/* max. duration of tap contacts (ms) */
	WSMOUSECFG_TAP_CLICKTIME,	/* time between the end of a tap and
					   the button-up-event (ms) */
	WSMOUSECFG_TAP_LOCKTIME,	/* time between a tap-and-drag action
					   and the button-up-event (ms) */
	WSMOUSECFG_TAP_ONE_BTNMAP,	/* one-finger tap button mapping */
	WSMOUSECFG_TAP_TWO_BTNMAP,	/* two-finger tap button mapping */
	WSMOUSECFG_TAP_THREE_BTNMAP,	/* three-finger tap button mapping */
	WSMOUSECFG_MTBTN_MAXDIST,	/* MTBUTTONS: distance limit for
					   two-finger clicks */

	WSMOUSECFG__TPSETUP,

	/*
	 * Enable/Disable debug output.
	 */
	WSMOUSECFG_LOG_INPUT = 256,
	WSMOUSECFG_LOG_EVENTS,

	WSMOUSECFG__DEBUG,
};

#define WSMOUSECFG_MAX ((WSMOUSECFG__FILTERS - WSMOUSECFG_DX_SCALE)	\
    + (WSMOUSECFG__TPFILTERS - WSMOUSECFG_DX_MAX)			\
    + (WSMOUSECFG__TPFEATURES - WSMOUSECFG_SOFTBUTTONS)			\
    + (WSMOUSECFG__TPSETUP - WSMOUSECFG_LEFT_EDGE)			\
    + (WSMOUSECFG__DEBUG - WSMOUSECFG_LOG_INPUT))

struct wsmouse_param {
	enum wsmousecfg key;
	int value;
};

struct wsmouse_parameters {
	struct wsmouse_param *params;
	u_int nparams;
};

#define WSMOUSEIO_GETPARAMS	_IOW('W', 39, struct wsmouse_parameters)
#define WSMOUSEIO_SETPARAMS	_IOW('W', 40, struct wsmouse_parameters)

/*
 * Display ioctls (64 - 95)
 */

/* Get display type */
#define	WSDISPLAYIO_GTYPE	_IOR('W', 64, u_int)
#define		WSDISPLAY_TYPE_UNKNOWN	0	/* unknown */
#define		WSDISPLAY_TYPE_PM_MONO	1	/* DEC [23]100 mono */
#define		WSDISPLAY_TYPE_PM_COLOR	2	/* DEC [23]100 color */
#define		WSDISPLAY_TYPE_CFB	3	/* DEC TC CFB (CX) */
#define		WSDISPLAY_TYPE_XCFB	4	/* DEC `maxine' onboard fb */
#define		WSDISPLAY_TYPE_MFB	5	/* DEC TC MFB (MX) */
#define		WSDISPLAY_TYPE_SFB	6	/* DEC TC SFB (HX) */
#define		WSDISPLAY_TYPE_ISAVGA	7	/* (generic) ISA VGA */
#define		WSDISPLAY_TYPE_PCIVGA	8	/* (generic) PCI VGA */
#define		WSDISPLAY_TYPE_TGA	9	/* DEC PCI TGA */
#define		WSDISPLAY_TYPE_SFBP	10	/* DEC TC SFB+ (HX+) */
#define		WSDISPLAY_TYPE_PCIMISC	11	/* (generic) PCI misc. disp. */
#define		WSDISPLAY_TYPE_NEXTMONO	12	/* NeXT mono display */
#define		WSDISPLAY_TYPE_PX	13	/* DEC TC PX */
#define		WSDISPLAY_TYPE_PXG	14	/* DEC TC PXG */
#define		WSDISPLAY_TYPE_TX	15	/* DEC TC TX */
#define		WSDISPLAY_TYPE_HPCFB	16	/* Handheld/PalmSize PC */
#define		WSDISPLAY_TYPE_VIDC	17	/* Acorn/ARM VIDC */
#define		WSDISPLAY_TYPE_SPX	18	/* DEC SPX (VS3100/VS4000) */
#define		WSDISPLAY_TYPE_GPX	19	/* DEC GPX (uVAX/VS2K/VS3100) */
#define		WSDISPLAY_TYPE_LCG	20	/* DEC LCG (VS4000) */
#define		WSDISPLAY_TYPE_VAX_MONO	21	/* DEC VS2K/VS3100 mono */
#define		WSDISPLAY_TYPE_SB_P9100	22	/* Tadpole SPARCbook P9100 */
#define		WSDISPLAY_TYPE_EGA	23	/* (generic) EGA */
#define		WSDISPLAY_TYPE_DCPVR	24	/* Dreamcast PowerVR */
#define		WSDISPLAY_TYPE_SUN24	25	/* Sun 24 bit framebuffers */
#define		WSDISPLAY_TYPE_SUNBW	26	/* Sun black and white fb */
#define		WSDISPLAY_TYPE_STI	27	/* HP STI framebuffers */
#define		WSDISPLAY_TYPE_SUNCG3	28	/* Sun cgthree */
#define		WSDISPLAY_TYPE_SUNCG6	29	/* Sun cgsix */
#define		WSDISPLAY_TYPE_SUNFFB	30	/* Sun creator FFB */
#define		WSDISPLAY_TYPE_SUNCG14	31	/* Sun cgfourteen */
#define		WSDISPLAY_TYPE_SUNCG2	32	/* Sun cgtwo */
#define		WSDISPLAY_TYPE_SUNCG4	33	/* Sun cgfour */
#define		WSDISPLAY_TYPE_SUNCG8	34	/* Sun cgeight */
#define		WSDISPLAY_TYPE_SUNTCX	35	/* Sun TCX */
#define		WSDISPLAY_TYPE_AGTEN	36	/* AG10E */
#define		WSDISPLAY_TYPE_XVIDEO	37	/* Xvideo */
#define		WSDISPLAY_TYPE_SUNCG12	38	/* Sun cgtwelve */
#define		WSDISPLAY_TYPE_MGX	39	/* SMS MGX */
#define		WSDISPLAY_TYPE_SB_P9000	40	/* Tadpole SPARCbook P9000 */
#define		WSDISPLAY_TYPE_RFLEX	41	/* RasterFlex series */
#define		WSDISPLAY_TYPE_LUNA	42	/* OMRON Luna */
#define		WSDISPLAY_TYPE_DVBOX	43	/* HP DaVinci */
#define		WSDISPLAY_TYPE_GBOX	44	/* HP Gatorbox */
#define		WSDISPLAY_TYPE_RBOX	45	/* HP Renaissance */
#define		WSDISPLAY_TYPE_HYPERION	46	/* HP Hyperion */
#define		WSDISPLAY_TYPE_TOPCAT	47	/* HP Topcat */
#define		WSDISPLAY_TYPE_PXALCD	48	/* PXALCD (Zaurus) */
#define		WSDISPLAY_TYPE_MAC68K	49	/* Generic mac68k framebuffer */
#define		WSDISPLAY_TYPE_SUNLEO	50	/* Sun ZX/Leo */
#define		WSDISPLAY_TYPE_TVRX	51	/* HP TurboVRX */
#define		WSDISPLAY_TYPE_CFXGA	52	/* CF VoyagerVGA */
#define		WSDISPLAY_TYPE_LCSPX	53	/* DEC LCSPX (VS4000) */
#define		WSDISPLAY_TYPE_GBE	54	/* SGI GBE frame buffer */
#define		WSDISPLAY_TYPE_LEGSS	55	/* DEC LEGSS (VS35x0) */
#define		WSDISPLAY_TYPE_IFB	56	/* Sun Expert3D{,-Lite} */
#define		WSDISPLAY_TYPE_RAPTOR	57	/* Tech Source Raptor */
#define		WSDISPLAY_TYPE_DL	58	/* DisplayLink DL-120/DL-160 */
#define		WSDISPLAY_TYPE_MACHFB	59	/* Sun PGX/PGX64 */
#define		WSDISPLAY_TYPE_GFXP	60	/* Sun PGX32 */
#define		WSDISPLAY_TYPE_RADEONFB	61	/* Sun XVR-100 */
#define		WSDISPLAY_TYPE_SMFB	62	/* SiliconMotion SM712 */
#define		WSDISPLAY_TYPE_SISFB	63	/* SiS 315 Pro */
#define		WSDISPLAY_TYPE_ODYSSEY	64	/* SGI Odyssey */
#define		WSDISPLAY_TYPE_IMPACT	65	/* SGI Impact */
#define		WSDISPLAY_TYPE_GRTWO	66	/* SGI GR2 */
#define		WSDISPLAY_TYPE_NEWPORT	67	/* SGI Newport */
#define		WSDISPLAY_TYPE_LIGHT	68	/* SGI Light */
#define		WSDISPLAY_TYPE_INTELDRM	69	/* Intel KMS framebuffer */
#define		WSDISPLAY_TYPE_RADEONDRM 70	/* ATI Radeon KMS framebuffer */
#define		WSDISPLAY_TYPE_EFIFB	71	/* EFI framebuffer */
#define		WSDISPLAY_TYPE_KMS	72	/* Generic KMS framebuffer */
#define		WSDISPLAY_TYPE_ASTFB	73	/* AST framebuffer */
#define		WSDISPLAY_TYPE_VIOGPU	74	/* VirtIO GPU */

/* Basic display information.  Not applicable to all display types. */
struct wsdisplay_fbinfo {
	u_int	height;				/* height in pixels */
	u_int	width;				/* width in pixels */
	u_int	depth;				/* bits per pixel */
	u_int	stride;				/* bytes per line */
	u_int	offset;				/* first pixel offset (bytes) */
	u_int	cmsize;				/* color map size (entries) */
};
#define	WSDISPLAYIO_GINFO	_IOR('W', 65, struct wsdisplay_fbinfo)

/* Colormap operations.  Not applicable to all display types. */
struct wsdisplay_cmap {
	u_int	index;				/* first element (0 origin) */
	u_int	count;				/* number of elements */
	u_char	*red;				/* red color map elements */
	u_char	*green;				/* green color map elements */
	u_char	*blue;				/* blue color map elements */
};
#define WSDISPLAYIO_GETCMAP	_IOW('W', 66, struct wsdisplay_cmap)
#define WSDISPLAYIO_PUTCMAP	_IOW('W', 67, struct wsdisplay_cmap)

/* Video control.  Not applicable to all display types. */
#define	WSDISPLAYIO_GVIDEO	_IOR('W', 68, u_int)
#define	WSDISPLAYIO_SVIDEO	_IOW('W', 69, u_int)
#define		WSDISPLAYIO_VIDEO_OFF	0	/* video off */
#define		WSDISPLAYIO_VIDEO_ON	1	/* video on */

/* Cursor control.  Not applicable to all display types. */
struct wsdisplay_curpos {			/* cursor "position" */
	u_int x, y;
};

struct wsdisplay_cursor {
	u_int	which;				/* values to get/set */
	u_int	enable;				/* enable/disable */
	struct wsdisplay_curpos pos;		/* position */
	struct wsdisplay_curpos hot;		/* hot spot */
	struct wsdisplay_cmap cmap;		/* color map info */
	struct wsdisplay_curpos size;		/* bit map size */
	u_char *image;				/* image data */
	u_char *mask;				/* mask data */
};
#define		WSDISPLAY_CURSOR_DOCUR		0x01	/* get/set enable */
#define		WSDISPLAY_CURSOR_DOPOS		0x02	/* get/set pos */
#define		WSDISPLAY_CURSOR_DOHOT		0x04	/* get/set hot spot */
#define		WSDISPLAY_CURSOR_DOCMAP		0x08	/* get/set cmap */
#define		WSDISPLAY_CURSOR_DOSHAPE	0x10	/* get/set img/mask */
#define		WSDISPLAY_CURSOR_DOALL		0x1f	/* all of the above */

/* Cursor control: get and set position */
#define	WSDISPLAYIO_GCURPOS	_IOR('W', 70, struct wsdisplay_curpos)
#define	WSDISPLAYIO_SCURPOS	_IOW('W', 71, struct wsdisplay_curpos)

/* Cursor control: get maximum size */
#define	WSDISPLAYIO_GCURMAX	_IOR('W', 72, struct wsdisplay_curpos)

/* Cursor control: get/set cursor attributes/shape */
#define	WSDISPLAYIO_GCURSOR	_IOWR('W', 73, struct wsdisplay_cursor)
#define	WSDISPLAYIO_SCURSOR	_IOW('W', 74, struct wsdisplay_cursor)

/* Display mode: Emulation (text) vs. Mapped (graphics) mode */
#define	WSDISPLAYIO_GMODE	_IOR('W', 75, u_int)
#define	WSDISPLAYIO_SMODE	_IOW('W', 76, u_int)
#define		WSDISPLAYIO_MODE_EMUL	0	/* emulation (text) mode */
#define		WSDISPLAYIO_MODE_MAPPED	1	/* mapped (graphics) mode */
#define		WSDISPLAYIO_MODE_DUMBFB	2	/* mapped (graphics) fb mode */

struct wsdisplay_font {
	char name[WSFONT_NAME_SIZE];
	int index;
#define WSDISPLAY_MAXFONTCOUNT	8
	int firstchar, numchars;
	int encoding;
#define WSDISPLAY_FONTENC_ISO 0
#define WSDISPLAY_FONTENC_IBM 1
	u_int fontwidth, fontheight, stride;
#define WSDISPLAY_MAXFONTSZ	(512*1024)
	int bitorder, byteorder;
#define	WSDISPLAY_FONTORDER_KNOWN	0	/* i.e, no need to convert */
#define	WSDISPLAY_FONTORDER_L2R		1
#define	WSDISPLAY_FONTORDER_R2L		2
	void *cookie;
	void *data;
};
#define WSDISPLAYIO_LDFONT	_IOW ('W', 77, struct wsdisplay_font)
#define	WSDISPLAYIO_LSFONT	_IOWR('W', 78, struct wsdisplay_font)
#define	WSDISPLAYIO_DELFONT	_IOW ('W', 79, struct wsdisplay_font)
#define WSDISPLAYIO_USEFONT	_IOW ('W', 80, struct wsdisplay_font)

struct wsdisplay_burner {
	u_int	off;
	u_int	on;
	u_int	flags;
#define	WSDISPLAY_BURN_VBLANK	0x0001
#define	WSDISPLAY_BURN_KBD	0x0002
#define	WSDISPLAY_BURN_MOUSE	0x0004
#define	WSDISPLAY_BURN_OUTPUT	0x0008
};
#define	WSDISPLAYIO_SBURNER	_IOW('W', 81, struct wsdisplay_burner)
#define	WSDISPLAYIO_GBURNER	_IOR('W', 82, struct wsdisplay_burner)

/*
 * XXX WARNING
 * XXX The following definitions are very preliminary and are likely
 * XXX to be changed without care about backwards compatibility!
 */
struct wsdisplay_addscreendata {
	int idx; /* screen index */
	char screentype[WSSCREEN_NAME_SIZE];
	char emul[WSEMUL_NAME_SIZE];
};
#define WSDISPLAYIO_ADDSCREEN	_IOW('W', 83, struct wsdisplay_addscreendata)

struct wsdisplay_delscreendata {
	int idx; /* screen index */
	int flags;
#define	WSDISPLAY_DELSCR_FORCE	0x01
#define	WSDISPLAY_DELSCR_QUIET	0x02
};
#define WSDISPLAYIO_DELSCREEN	_IOW('W', 84, struct wsdisplay_delscreendata)

#define WSDISPLAYIO_GETSCREEN	_IOWR('W', 85, struct wsdisplay_addscreendata)
#define	WSDISPLAYIO_SETSCREEN	_IOW('W', 86, u_int)

/* Display information: number of bytes per row, may be same as pixels */
#define	WSDISPLAYIO_LINEBYTES	_IOR('W', 95, u_int)

/* Mouse console support */
#define WSDISPLAYIO_WSMOUSED	_IOW('W', 88, struct wscons_event)

/* Misc control.  Not applicable to all display types. */
struct wsdisplay_param {
        int param;
#define	WSDISPLAYIO_PARAM_BACKLIGHT	1
#define	WSDISPLAYIO_PARAM_BRIGHTNESS	2
#define	WSDISPLAYIO_PARAM_CONTRAST	3
        int min, max, curval;
        int reserved[4];
};
#define	WSDISPLAYIO_GETPARAM	_IOWR('W', 89, struct wsdisplay_param)
#define	WSDISPLAYIO_SETPARAM	_IOWR('W', 90, struct wsdisplay_param)

#define WSDISPLAYIO_GPCIID	_IOR('W', 91, struct pcisel)

/* graphical mode control */

#define WSDISPLAYIO_DEPTH_1		0x1
#define WSDISPLAYIO_DEPTH_4		0x2
#define WSDISPLAYIO_DEPTH_8		0x4
#define WSDISPLAYIO_DEPTH_15		0x8
#define WSDISPLAYIO_DEPTH_16		0x10
#define WSDISPLAYIO_DEPTH_24_24		0x20
#define WSDISPLAYIO_DEPTH_24_32		0x40
#define WSDISPLAYIO_DEPTH_24 (WSDISPLAYIO_DEPTH_24_24|WSDISPLAYIO_DEPTH_24_32)
#define WSDISPLAYIO_DEPTH_30		0x80

#define WSDISPLAYIO_GETSUPPORTEDDEPTH	_IOR('W', 92, unsigned int)

struct wsdisplay_gfx_mode {
	int width;
	int height;
	int depth;
};

#define WSDISPLAYIO_SETGFXMODE	_IOW('W', 92, struct wsdisplay_gfx_mode)

struct wsdisplay_screentype {
	int idx;
	int nidx;
	char name[WSSCREEN_NAME_SIZE];
	int ncols, nrows;
	int fontwidth, fontheight;
};

#define	WSDISPLAYIO_GETSCREENTYPE	_IOWR('W', 93, struct wsdisplay_screentype)

struct wsdisplay_emultype {
	int idx;
	char name[WSSCREEN_NAME_SIZE];
};

#define	WSDISPLAYIO_GETEMULTYPE	_IOWR('W', 94, struct wsdisplay_emultype)

/* XXX NOT YET DEFINED */
/* Mapping information retrieval. */

/*
 * Mux ioctls (96 - 127)
 */

#define WSMUXIO_INJECTEVENT	_IOW('W', 96, struct wscons_event)

struct wsmux_device {
	int type;
#define WSMUX_MOUSE	1
#define WSMUX_KBD	2
#define WSMUX_MUX	3
	int idx;
};
#define WSMUXIO_ADD_DEVICE	_IOW('W', 97, struct wsmux_device)
#define WSMUXIO_REMOVE_DEVICE	_IOW('W', 98, struct wsmux_device)

#define WSMUX_MAXDEV 32
struct wsmux_device_list {
	int ndevices;
	struct wsmux_device devices[WSMUX_MAXDEV];
};
#define WSMUXIO_LIST_DEVICES	_IOWR('W', 99, struct wsmux_device_list)

#endif /* _DEV_WSCONS_WSCONSIO_H_ */
