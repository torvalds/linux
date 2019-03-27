/*-
 * Copyright (c) 1992, 1993 Erik Forsberg.
 * Copyright (c) 1996, 1997 Kazutaka YOKOTA.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * THIS SOFTWARE IS PROVIDED BY ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL I BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 *  Ported to 386bsd Oct 17, 1992
 *  Sandi Donno, Computer Science, University of Cape Town, South Africa
 *  Please send bug reports to sandi@cs.uct.ac.za
 *
 *  Thanks are also due to Rick Macklem, rick@snowhite.cis.uoguelph.ca -
 *  although I was only partially successful in getting the alpha release
 *  of his "driver for the Logitech and ATI Inport Bus mice for use with
 *  386bsd and the X386 port" to work with my Microsoft mouse, I nevertheless
 *  found his code to be an invaluable reference when porting this driver
 *  to 386bsd.
 *
 *  Further modifications for latest 386BSD+patchkit and port to NetBSD,
 *  Andrew Herbert <andrew@werple.apana.org.au> - 8 June 1993
 *
 *  Cloned from the Microsoft Bus Mouse driver, also by Erik Forsberg, by
 *  Andrew Herbert - 12 June 1993
 *
 *  Modified for PS/2 mouse by Charles Hannum <mycroft@ai.mit.edu>
 *  - 13 June 1993
 *
 *  Modified for PS/2 AUX mouse by Shoji Yuen <yuen@nuie.nagoya-u.ac.jp>
 *  - 24 October 1993
 *
 *  Hardware access routines and probe logic rewritten by
 *  Kazutaka Yokota <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 *  - 3, 14, 22 October 1996.
 *  - 12 November 1996. IOCTLs and rearranging `psmread', `psmioctl'...
 *  - 14, 30 November 1996. Uses `kbdio.c'.
 *  - 13 December 1996. Uses queuing version of `kbdio.c'.
 *  - January/February 1997. Tweaked probe logic for
 *    HiNote UltraII/Latitude/Armada laptops.
 *  - 30 July 1997. Added APM support.
 *  - 5 March 1997. Defined driver configuration flags (PSM_CONFIG_XXX).
 *    Improved sync check logic.
 *    Vendor specific support routines.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_isa.h"
#include "opt_psm.h"
#include "opt_evdev.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/filio.h>
#include <sys/poll.h>
#include <sys/sigio.h>
#include <sys/signalvar.h>
#include <sys/syslog.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <sys/limits.h>
#include <sys/mouse.h>
#include <machine/resource.h>

#ifdef DEV_ISA
#include <isa/isavar.h>
#endif

#ifdef EVDEV_SUPPORT
#include <dev/evdev/evdev.h>
#include <dev/evdev/input.h>
#endif

#include <dev/atkbdc/atkbdcreg.h>
#include <dev/atkbdc/psm.h>

/*
 * Driver specific options: the following options may be set by
 * `options' statements in the kernel configuration file.
 */

/* debugging */
#ifndef PSM_DEBUG
#define	PSM_DEBUG	0	/*
				 * logging: 0: none, 1: brief, 2: verbose
				 *          3: sync errors, 4: all packets
				 */
#endif
#define	VLOG(level, args)	do {	\
	if (verbose >= level)		\
		log args;		\
} while (0)

#ifndef PSM_INPUT_TIMEOUT
#define	PSM_INPUT_TIMEOUT	2000000	/* 2 sec */
#endif

#ifndef PSM_TAP_TIMEOUT
#define	PSM_TAP_TIMEOUT		125000
#endif

#ifndef PSM_TAP_THRESHOLD
#define	PSM_TAP_THRESHOLD	25
#endif

/* end of driver specific options */

#define	PSMCPNP_DRIVER_NAME	"psmcpnp"

struct psmcpnp_softc {
	enum {
		PSMCPNP_GENERIC,
		PSMCPNP_FORCEPAD,
		PSMCPNP_TOPBUTTONPAD,
	} type;		/* Based on PnP ID */
};

/* input queue */
#define	PSM_BUFSIZE		960
#define	PSM_SMALLBUFSIZE	240

/* operation levels */
#define	PSM_LEVEL_BASE		0
#define	PSM_LEVEL_STANDARD	1
#define	PSM_LEVEL_NATIVE	2
#define	PSM_LEVEL_MIN		PSM_LEVEL_BASE
#define	PSM_LEVEL_MAX		PSM_LEVEL_NATIVE

/* Active PS/2 multiplexing */
#define	PSM_NOMUX		(-1)

/* Logitech PS2++ protocol */
#define	MOUSE_PS2PLUS_CHECKBITS(b)	\
    ((((b[2] & 0x03) << 2) | 0x02) == (b[1] & 0x0f))
#define	MOUSE_PS2PLUS_PACKET_TYPE(b)	\
    (((b[0] & 0x30) >> 2) | ((b[1] & 0x30) >> 4))

/* ring buffer */
typedef struct ringbuf {
	int		count;	/* # of valid elements in the buffer */
	int		head;	/* head pointer */
	int		tail;	/* tail poiner */
	u_char buf[PSM_BUFSIZE];
} ringbuf_t;

/* data buffer */
typedef struct packetbuf {
	u_char	ipacket[16];	/* interim input buffer */
	int	inputbytes;	/* # of bytes in the input buffer */
} packetbuf_t;

#ifndef PSM_PACKETQUEUE
#define	PSM_PACKETQUEUE	128
#endif

typedef struct synapticsinfo {
	struct sysctl_ctx_list	 sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;
	int			 directional_scrolls;
	int			 two_finger_scroll;
	int			 min_pressure;
	int			 max_pressure;
	int			 max_width;
	int			 margin_top;
	int			 margin_right;
	int			 margin_bottom;
	int			 margin_left;
	int			 na_top;
	int			 na_right;
	int			 na_bottom;
	int			 na_left;
	int			 window_min;
	int			 window_max;
	int			 multiplicator;
	int			 weight_current;
	int			 weight_previous;
	int			 weight_previous_na;
	int			 weight_len_squared;
	int			 div_min;
	int			 div_max;
	int			 div_max_na;
	int			 div_len;
	int			 tap_max_delta;
	int			 tap_min_queue;
	int			 taphold_timeout;
	int			 vscroll_ver_area;
	int			 vscroll_hor_area;
	int			 vscroll_min_delta;
	int			 vscroll_div_min;
	int			 vscroll_div_max;
	int			 touchpad_off;
	int			 softbuttons_y;
	int			 softbutton2_x;
	int			 softbutton3_x;
	int			 max_x;
	int			 max_y;
} synapticsinfo_t;

typedef struct synapticspacket {
	int			x;
	int			y;
} synapticspacket_t;

#define	SYNAPTICS_PACKETQUEUE 10
#define SYNAPTICS_QUEUE_CURSOR(x)					\
	(x + SYNAPTICS_PACKETQUEUE) % SYNAPTICS_PACKETQUEUE

#define	SYNAPTICS_VERSION_GE(synhw, major, minor)			\
    ((synhw).infoMajor > (major) ||					\
     ((synhw).infoMajor == (major) && (synhw).infoMinor >= (minor)))

typedef struct smoother {
	synapticspacket_t	queue[SYNAPTICS_PACKETQUEUE];
	int			queue_len;
	int			queue_cursor;
	int			start_x;
	int			start_y;
	int			avg_dx;
	int			avg_dy;
	int			squelch_x;
	int			squelch_y;
	int			is_fuzzy;
	int			active;
} smoother_t;

typedef struct gesture {
	int			window_min;
	int			fingers_nb;
	int			tap_button;
	int			in_taphold;
	int			in_vscroll;
	int			zmax;		/* maximum pressure value */
	struct timeval		taptimeout;	/* tap timeout for touchpads */
} gesture_t;

enum {
	TRACKPOINT_SYSCTL_SENSITIVITY,
	TRACKPOINT_SYSCTL_NEGATIVE_INERTIA,
	TRACKPOINT_SYSCTL_UPPER_PLATEAU,
	TRACKPOINT_SYSCTL_BACKUP_RANGE,
	TRACKPOINT_SYSCTL_DRAG_HYSTERESIS,
	TRACKPOINT_SYSCTL_MINIMUM_DRAG,
	TRACKPOINT_SYSCTL_UP_THRESHOLD,
	TRACKPOINT_SYSCTL_THRESHOLD,
	TRACKPOINT_SYSCTL_JENKS_CURVATURE,
	TRACKPOINT_SYSCTL_Z_TIME,
	TRACKPOINT_SYSCTL_PRESS_TO_SELECT,
	TRACKPOINT_SYSCTL_SKIP_BACKUPS
};

typedef struct trackpointinfo {
	struct sysctl_ctx_list sysctl_ctx;
	struct sysctl_oid *sysctl_tree;
	int	sensitivity;
	int	inertia;
	int	uplateau;
	int	reach;
	int	draghys;
	int	mindrag;
	int	upthresh;
	int	threshold;
	int	jenks;
	int	ztime;
	int	pts;
	int	skipback;
} trackpointinfo_t;

typedef struct finger {
	int			x;
	int			y;
	int			p;
	int			w;
	int			flags;
} finger_t;
#define	PSM_FINGERS		2	/* # of processed fingers */
#define	PSM_FINGER_IS_PEN	(1<<0)
#define	PSM_FINGER_FUZZY	(1<<1)
#define	PSM_FINGER_DEFAULT_P	tap_threshold
#define	PSM_FINGER_DEFAULT_W	1
#define	PSM_FINGER_IS_SET(f) ((f).x != -1 && (f).y != -1 && (f).p != 0)
#define	PSM_FINGER_RESET(f) do { \
	(f) = (finger_t) { .x = -1, .y = -1, .p = 0, .w = 0, .flags = 0 }; \
} while (0)

typedef struct elantechhw {
	int			hwversion;
	int			fwversion;
	int			sizex;
	int			sizey;
	int			dpmmx;
	int			dpmmy;
	int			ntracesx;
	int			ntracesy;
	int			dptracex;
	int			dptracey;
	int			issemimt;
	int			isclickpad;
	int			hascrc;
	int			hastrackpoint;
	int			haspressure;
} elantechhw_t;

/* minimum versions supported by this driver */
#define	ELANTECH_HW_IS_V1(fwver) ((fwver) < 0x020030 || (fwver) == 0x020600)

#define	ELANTECH_MAGIC(magic)				\
	((magic)[0] == 0x3c && (magic)[1] == 0x03 &&	\
	((magic)[2] == 0xc8 || (magic)[2] == 0x00))

#define	ELANTECH_FW_ID		0x00
#define	ELANTECH_FW_VERSION	0x01
#define	ELANTECH_CAPABILITIES	0x02
#define	ELANTECH_SAMPLE		0x03
#define	ELANTECH_RESOLUTION	0x04
#define	ELANTECH_REG_READ	0x10
#define	ELANTECH_REG_WRITE	0x11
#define	ELANTECH_REG_RDWR	0x00
#define	ELANTECH_CUSTOM_CMD	0xf8

#ifdef EVDEV_SUPPORT
#define	ELANTECH_MAX_FINGERS	5
#else
#define	ELANTECH_MAX_FINGERS	PSM_FINGERS
#endif

#define	ELANTECH_FINGER_MAX_P	255
#define	ELANTECH_FINGER_MAX_W	15
#define	ELANTECH_FINGER_SET_XYP(pb) (finger_t) {			\
    .x = (((pb)->ipacket[1] & 0x0f) << 8) | (pb)->ipacket[2],		\
    .y = (((pb)->ipacket[4] & 0x0f) << 8) | (pb)->ipacket[5],		\
    .p = ((pb)->ipacket[1] & 0xf0) | (((pb)->ipacket[4] >> 4) & 0x0f),	\
    .w = PSM_FINGER_DEFAULT_W,						\
    .flags = 0								\
}

enum {
	ELANTECH_PKT_NOP,
	ELANTECH_PKT_TRACKPOINT,
	ELANTECH_PKT_V2_COMMON,
	ELANTECH_PKT_V2_2FINGER,
	ELANTECH_PKT_V3,
	ELANTECH_PKT_V4_STATUS,
	ELANTECH_PKT_V4_HEAD,
	ELANTECH_PKT_V4_MOTION
};

#define	ELANTECH_PKT_IS_TRACKPOINT(pb) (((pb)->ipacket[3] & 0x0f) == 0x06)
#define	ELANTECH_PKT_IS_DEBOUNCE(pb, hwversion) ((hwversion) == 4 ? 0 :	\
    (pb)->ipacket[0] == ((hwversion) == 2 ? 0x84 : 0xc4) &&		\
    (pb)->ipacket[1] == 0xff && (pb)->ipacket[2] == 0xff &&		\
    (pb)->ipacket[3] == 0x02 && (pb)->ipacket[4] == 0xff &&		\
    (pb)->ipacket[5] == 0xff)
#define	ELANTECH_PKT_IS_V2(pb) 						\
    (((pb)->ipacket[0] & 0x0c) == 0x04 && ((pb)->ipacket[3] & 0x0f) == 0x02)
#define	ELANTECH_PKT_IS_V3_HEAD(pb, hascrc) ((hascrc) ? 		\
    ((pb)->ipacket[3] & 0x09) == 0x08 : 				\
    ((pb)->ipacket[0] & 0x0c) == 0x04 && ((pb)->ipacket[3] & 0xcf) == 0x02)
#define	ELANTECH_PKT_IS_V3_TAIL(pb, hascrc) ((hascrc) ? 		\
    ((pb)->ipacket[3] & 0x09) == 0x09 : 				\
    ((pb)->ipacket[0] & 0x0c) == 0x0c && ((pb)->ipacket[3] & 0xce) == 0x0c)
#define	ELANTECH_PKT_IS_V4(pb, hascrc) ((hascrc) ? 			\
    ((pb)->ipacket[3] & 0x08) == 0x00 :					\
    ((pb)->ipacket[0] & 0x0c) == 0x04 && ((pb)->ipacket[3] & 0x1c) == 0x10)

typedef struct elantechaction {
	finger_t		fingers[ELANTECH_MAX_FINGERS];
	int			mask;
	int			mask_v4wait;
} elantechaction_t;

/* driver control block */
struct psm_softc {		/* Driver status information */
	int		unit;
	struct selinfo	rsel;		/* Process selecting for Input */
	u_char		state;		/* Mouse driver state */
	int		config;		/* driver configuration flags */
	int		flags;		/* other flags */
	KBDC		kbdc;		/* handle to access kbd controller */
	struct resource	*intr;		/* IRQ resource */
	void		*ih;		/* interrupt handle */
	mousehw_t	hw;		/* hardware information */
	synapticshw_t	synhw;		/* Synaptics hardware information */
	synapticsinfo_t	syninfo;	/* Synaptics configuration */
	smoother_t	smoother[PSM_FINGERS];	/* Motion smoothing */
	gesture_t	gesture;	/* Gesture context */
	elantechhw_t	elanhw;		/* Elantech hardware information */
	elantechaction_t elanaction;	/* Elantech action context */
	int		tphw;		/* TrackPoint hardware information */
	trackpointinfo_t tpinfo;	/* TrackPoint configuration */
	mousemode_t	mode;		/* operation mode */
	mousemode_t	dflt_mode;	/* default operation mode */
	mousestatus_t	status;		/* accumulated mouse movement */
	ringbuf_t	queue;		/* mouse status queue */
	packetbuf_t	pqueue[PSM_PACKETQUEUE]; /* mouse data queue */
	int		pqueue_start;	/* start of data in queue */
	int		pqueue_end;	/* end of data in queue */
	int		button;		/* the latest button state */
	int		xold;		/* previous absolute X position */
	int		yold;		/* previous absolute Y position */
	int		xaverage;	/* average X position */
	int		yaverage;	/* average Y position */
	int		squelch; /* level to filter movement at low speed */
	int		syncerrors; /* # of bytes discarded to synchronize */
	int		pkterrors;  /* # of packets failed during quaranteen. */
	int		fpcount;	/* forcePad valid packet counter */
	struct timeval	inputtimeout;
	struct timeval	lastsoftintr;	/* time of last soft interrupt */
	struct timeval	lastinputerr;	/* time last sync error happened */
	struct timeval	idletimeout;
	packetbuf_t	idlepacket;	/* packet to send after idle timeout */
	int		watchdog;	/* watchdog timer flag */
	struct callout	callout;	/* watchdog timer call out */
	struct callout	softcallout; /* buffer timer call out */
	struct cdev	*dev;
	struct cdev	*bdev;
	int		lasterr;
	int		cmdcount;
	struct sigio	*async;		/* Processes waiting for SIGIO */
	int		extended_buttons;
	int		muxport;	/* MUX port with attached Synaptics */
	u_char		muxsave[3];	/* 3->6 byte proto conversion buffer */
	int		muxtpbuttons;	/* Touchpad button state */
	int		muxmsbuttons;	/* Mouse (trackpoint) button state */
	struct timeval	muxmidtimeout;	/* middle button supression timeout */
#ifdef EVDEV_SUPPORT
	struct evdev_dev *evdev_a;	/* Absolute reporting device */
	struct evdev_dev *evdev_r;	/* Relative reporting device */
#endif
};
static devclass_t psm_devclass;

/* driver state flags (state) */
#define	PSM_VALID		0x80
#define	PSM_OPEN		1	/* Device is open */
#define	PSM_ASLP		2	/* Waiting for mouse data */
#define	PSM_SOFTARMED		4	/* Software interrupt armed */
#define	PSM_NEED_SYNCBITS	8	/* Set syncbits using next data pkt */
#define	PSM_EV_OPEN_R		0x10	/* Relative evdev device is open */
#define	PSM_EV_OPEN_A		0x20	/* Absolute evdev device is open */

/* driver configuration flags (config) */
#define	PSM_CONFIG_RESOLUTION	0x000f	/* resolution */
#define	PSM_CONFIG_ACCEL	0x00f0  /* acceleration factor */
#define	PSM_CONFIG_NOCHECKSYNC	0x0100  /* disable sync. test */
#define	PSM_CONFIG_NOIDPROBE	0x0200  /* disable mouse model probe */
#define	PSM_CONFIG_NORESET	0x0400  /* don't reset the mouse */
#define	PSM_CONFIG_FORCETAP	0x0800  /* assume `tap' action exists */
#define	PSM_CONFIG_IGNPORTERROR	0x1000  /* ignore error in aux port test */
#define	PSM_CONFIG_HOOKRESUME	0x2000	/* hook the system resume event */
#define	PSM_CONFIG_INITAFTERSUSPEND 0x4000 /* init the device at the resume event */

#define	PSM_CONFIG_FLAGS	\
    (PSM_CONFIG_RESOLUTION |	\
    PSM_CONFIG_ACCEL |		\
    PSM_CONFIG_NOCHECKSYNC |	\
    PSM_CONFIG_NOIDPROBE |	\
    PSM_CONFIG_NORESET |	\
    PSM_CONFIG_FORCETAP |	\
    PSM_CONFIG_IGNPORTERROR |	\
    PSM_CONFIG_HOOKRESUME |	\
    PSM_CONFIG_INITAFTERSUSPEND)

/* other flags (flags) */
#define	PSM_FLAGS_FINGERDOWN	0x0001	/* VersaPad finger down */

#define kbdcp(p)			((atkbdc_softc_t *)(p))
#define ALWAYS_RESTORE_CONTROLLER(kbdc)	!(kbdcp(kbdc)->quirks \
    & KBDC_QUIRK_KEEP_ACTIVATED)

/* Tunables */
static int tap_enabled = -1;
static int verbose = PSM_DEBUG;
static int synaptics_support = 0;
static int trackpoint_support = 0;
static int elantech_support = 0;

/* for backward compatibility */
#define	OLD_MOUSE_GETHWINFO	_IOR('M', 1, old_mousehw_t)
#define	OLD_MOUSE_GETMODE	_IOR('M', 2, old_mousemode_t)
#define	OLD_MOUSE_SETMODE	_IOW('M', 3, old_mousemode_t)

typedef struct old_mousehw {
	int	buttons;
	int	iftype;
	int	type;
	int	hwid;
} old_mousehw_t;

typedef struct old_mousemode {
	int	protocol;
	int	rate;
	int	resolution;
	int	accelfactor;
} old_mousemode_t;

#define SYN_OFFSET(field) offsetof(struct psm_softc, syninfo.field)
enum {
	SYNAPTICS_SYSCTL_MIN_PRESSURE =		SYN_OFFSET(min_pressure),
	SYNAPTICS_SYSCTL_MAX_PRESSURE =		SYN_OFFSET(max_pressure),
	SYNAPTICS_SYSCTL_MAX_WIDTH =		SYN_OFFSET(max_width),
	SYNAPTICS_SYSCTL_MARGIN_TOP =		SYN_OFFSET(margin_top),
	SYNAPTICS_SYSCTL_MARGIN_RIGHT =		SYN_OFFSET(margin_right),
	SYNAPTICS_SYSCTL_MARGIN_BOTTOM =	SYN_OFFSET(margin_bottom),
	SYNAPTICS_SYSCTL_MARGIN_LEFT =		SYN_OFFSET(margin_left),
	SYNAPTICS_SYSCTL_NA_TOP =		SYN_OFFSET(na_top),
	SYNAPTICS_SYSCTL_NA_RIGHT =		SYN_OFFSET(na_right),
	SYNAPTICS_SYSCTL_NA_BOTTOM =		SYN_OFFSET(na_bottom),
	SYNAPTICS_SYSCTL_NA_LEFT = 		SYN_OFFSET(na_left),
	SYNAPTICS_SYSCTL_WINDOW_MIN =		SYN_OFFSET(window_min),
	SYNAPTICS_SYSCTL_WINDOW_MAX =		SYN_OFFSET(window_max),
	SYNAPTICS_SYSCTL_MULTIPLICATOR =	SYN_OFFSET(multiplicator),
	SYNAPTICS_SYSCTL_WEIGHT_CURRENT =	SYN_OFFSET(weight_current),
	SYNAPTICS_SYSCTL_WEIGHT_PREVIOUS =	SYN_OFFSET(weight_previous),
	SYNAPTICS_SYSCTL_WEIGHT_PREVIOUS_NA =	SYN_OFFSET(weight_previous_na),
	SYNAPTICS_SYSCTL_WEIGHT_LEN_SQUARED =	SYN_OFFSET(weight_len_squared),
	SYNAPTICS_SYSCTL_DIV_MIN =		SYN_OFFSET(div_min),
	SYNAPTICS_SYSCTL_DIV_MAX =		SYN_OFFSET(div_max),
	SYNAPTICS_SYSCTL_DIV_MAX_NA =		SYN_OFFSET(div_max_na),
	SYNAPTICS_SYSCTL_DIV_LEN =		SYN_OFFSET(div_len),
	SYNAPTICS_SYSCTL_TAP_MAX_DELTA =	SYN_OFFSET(tap_max_delta),
	SYNAPTICS_SYSCTL_TAP_MIN_QUEUE =	SYN_OFFSET(tap_min_queue),
	SYNAPTICS_SYSCTL_TAPHOLD_TIMEOUT =	SYN_OFFSET(taphold_timeout),
	SYNAPTICS_SYSCTL_VSCROLL_HOR_AREA =	SYN_OFFSET(vscroll_hor_area),
	SYNAPTICS_SYSCTL_VSCROLL_VER_AREA =	SYN_OFFSET(vscroll_ver_area),
	SYNAPTICS_SYSCTL_VSCROLL_MIN_DELTA =	SYN_OFFSET(vscroll_min_delta),
	SYNAPTICS_SYSCTL_VSCROLL_DIV_MIN =	SYN_OFFSET(vscroll_div_min),
	SYNAPTICS_SYSCTL_VSCROLL_DIV_MAX =	SYN_OFFSET(vscroll_div_max),
	SYNAPTICS_SYSCTL_TOUCHPAD_OFF =		SYN_OFFSET(touchpad_off),
	SYNAPTICS_SYSCTL_SOFTBUTTONS_Y =	SYN_OFFSET(softbuttons_y),
	SYNAPTICS_SYSCTL_SOFTBUTTON2_X =	SYN_OFFSET(softbutton2_x),
	SYNAPTICS_SYSCTL_SOFTBUTTON3_X =	SYN_OFFSET(softbutton3_x),
};

/* packet formatting function */
typedef int	packetfunc_t(struct psm_softc *, u_char *, int *, int,
    mousestatus_t *);

/* function prototypes */
static void	psmidentify(driver_t *, device_t);
static int	psmprobe(device_t);
static int	psmattach(device_t);
static int	psmdetach(device_t);
static int	psmresume(device_t);

static d_open_t		psm_cdev_open;
static d_close_t	psm_cdev_close;
static d_read_t		psmread;
static d_write_t	psmwrite;
static d_ioctl_t	psmioctl;
static d_poll_t		psmpoll;

static int	psmopen(struct psm_softc *);
static int	psmclose(struct psm_softc *);

#ifdef EVDEV_SUPPORT
static evdev_open_t	psm_ev_open_r;
static evdev_close_t	psm_ev_close_r;
static evdev_open_t	psm_ev_open_a;
static evdev_close_t	psm_ev_close_a;
#endif

static int	enable_aux_dev(KBDC);
static int	disable_aux_dev(KBDC);
static int	get_mouse_status(KBDC, int *, int, int);
static int	get_aux_id(KBDC);
static int	set_mouse_sampling_rate(KBDC, int);
static int	set_mouse_scaling(KBDC, int);
static int	set_mouse_resolution(KBDC, int);
static int	set_mouse_mode(KBDC);
static int	get_mouse_buttons(KBDC);
static int	is_a_mouse(int);
static void	recover_from_error(KBDC);
static int	restore_controller(KBDC, int);
static int	doinitialize(struct psm_softc *, mousemode_t *);
static int	doopen(struct psm_softc *, int);
static int	reinitialize(struct psm_softc *, int);
static char	*model_name(int);
static void	psmsoftintr(void *);
static void	psmsoftintridle(void *);
static void	psmintr(void *);
static void	psmtimeout(void *);
static int	timeelapsed(const struct timeval *, int, int,
		    const struct timeval *);
static void	dropqueue(struct psm_softc *);
static void	flushpackets(struct psm_softc *);
static void	proc_mmanplus(struct psm_softc *, packetbuf_t *,
		    mousestatus_t *, int *, int *, int *);
static int	proc_synaptics(struct psm_softc *, packetbuf_t *,
		    mousestatus_t *, int *, int *, int *);
static int	proc_synaptics_mux(struct psm_softc *, packetbuf_t *);
static void	proc_versapad(struct psm_softc *, packetbuf_t *,
		    mousestatus_t *, int *, int *, int *);
static int	proc_elantech(struct psm_softc *, packetbuf_t *,
		    mousestatus_t *, int *, int *, int *);
static int	psmpalmdetect(struct psm_softc *, finger_t *, int);
static void	psmgestures(struct psm_softc *, finger_t *, int,
		    mousestatus_t *);
static void	psmsmoother(struct psm_softc *, finger_t *, int,
		    mousestatus_t *, int *, int *);
static int	tame_mouse(struct psm_softc *, packetbuf_t *, mousestatus_t *,
		    u_char *);

/* vendor specific features */
enum probearg { PROBE, REINIT };
typedef int	probefunc_t(struct psm_softc *, enum probearg);

static int	mouse_id_proc1(KBDC, int, int, int *);
static int	mouse_ext_command(KBDC, int);

static probefunc_t	enable_groller;
static probefunc_t	enable_gmouse;
static probefunc_t	enable_aglide;
static probefunc_t	enable_kmouse;
static probefunc_t	enable_msexplorer;
static probefunc_t	enable_msintelli;
static probefunc_t	enable_4dmouse;
static probefunc_t	enable_4dplus;
static probefunc_t	enable_mmanplus;
static probefunc_t	enable_synaptics;
static probefunc_t	enable_synaptics_mux;
static probefunc_t	enable_trackpoint;
static probefunc_t	enable_versapad;
static probefunc_t	enable_elantech;

static void set_trackpoint_parameters(struct psm_softc *sc);
static void synaptics_passthrough_on(struct psm_softc *sc);
static void synaptics_passthrough_off(struct psm_softc *sc);
static int synaptics_preferred_mode(struct psm_softc *sc);
static void synaptics_set_mode(struct psm_softc *sc, int mode_byte);

static struct {
	int		model;
	u_char		syncmask;
	int		packetsize;
	probefunc_t	*probefunc;
} vendortype[] = {
	/*
	 * WARNING: the order of probe is very important.  Don't mess it
	 * unless you know what you are doing.
	 */
	{ MOUSE_MODEL_SYNAPTICS,	/* Synaptics Touchpad on Active Mux */
	  0x00, MOUSE_PS2_PACKETSIZE, enable_synaptics_mux },
	{ MOUSE_MODEL_NET,		/* Genius NetMouse */
	  0x08, MOUSE_PS2INTELLI_PACKETSIZE, enable_gmouse },
	{ MOUSE_MODEL_NETSCROLL,	/* Genius NetScroll */
	  0xc8, 6, enable_groller },
	{ MOUSE_MODEL_MOUSEMANPLUS,	/* Logitech MouseMan+ */
	  0x08, MOUSE_PS2_PACKETSIZE, enable_mmanplus },
	{ MOUSE_MODEL_EXPLORER,		/* Microsoft IntelliMouse Explorer */
	  0x08, MOUSE_PS2INTELLI_PACKETSIZE, enable_msexplorer },
	{ MOUSE_MODEL_4D,		/* A4 Tech 4D Mouse */
	  0x08, MOUSE_4D_PACKETSIZE, enable_4dmouse },
	{ MOUSE_MODEL_4DPLUS,		/* A4 Tech 4D+ Mouse */
	  0xc8, MOUSE_4DPLUS_PACKETSIZE, enable_4dplus },
	{ MOUSE_MODEL_SYNAPTICS,	/* Synaptics Touchpad */
	  0xc0, MOUSE_SYNAPTICS_PACKETSIZE, enable_synaptics },
	{ MOUSE_MODEL_ELANTECH,		/* Elantech Touchpad */
	  0x04, MOUSE_ELANTECH_PACKETSIZE, enable_elantech },
	{ MOUSE_MODEL_INTELLI,		/* Microsoft IntelliMouse */
	  0x08, MOUSE_PS2INTELLI_PACKETSIZE, enable_msintelli },
	{ MOUSE_MODEL_GLIDEPOINT,	/* ALPS GlidePoint */
	  0xc0, MOUSE_PS2_PACKETSIZE, enable_aglide },
	{ MOUSE_MODEL_THINK,		/* Kensington ThinkingMouse */
	  0x80, MOUSE_PS2_PACKETSIZE, enable_kmouse },
	{ MOUSE_MODEL_VERSAPAD,		/* Interlink electronics VersaPad */
	  0xe8, MOUSE_PS2VERSA_PACKETSIZE, enable_versapad },
	{ MOUSE_MODEL_TRACKPOINT,	/* IBM/Lenovo TrackPoint */
	  0xc0, MOUSE_PS2_PACKETSIZE, enable_trackpoint },
	{ MOUSE_MODEL_GENERIC,
	  0xc0, MOUSE_PS2_PACKETSIZE, NULL },
};
#define	GENERIC_MOUSE_ENTRY (nitems(vendortype) - 1)

/* device driver declarateion */
static device_method_t psm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	psmidentify),
	DEVMETHOD(device_probe,		psmprobe),
	DEVMETHOD(device_attach,	psmattach),
	DEVMETHOD(device_detach,	psmdetach),
	DEVMETHOD(device_resume,	psmresume),

	{ 0, 0 }
};

static driver_t psm_driver = {
	PSM_DRIVER_NAME,
	psm_methods,
	sizeof(struct psm_softc),
};

static struct cdevsw psm_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	psm_cdev_open,
	.d_close =	psm_cdev_close,
	.d_read =	psmread,
	.d_write =	psmwrite,
	.d_ioctl =	psmioctl,
	.d_poll =	psmpoll,
	.d_name =	PSM_DRIVER_NAME,
};

#ifdef EVDEV_SUPPORT
static const struct evdev_methods psm_ev_methods_r = {
	.ev_open = psm_ev_open_r,
	.ev_close = psm_ev_close_r,
};
static const struct evdev_methods psm_ev_methods_a = {
	.ev_open = psm_ev_open_a,
	.ev_close = psm_ev_close_a,
};
#endif

/* device I/O routines */
static int
enable_aux_dev(KBDC kbdc)
{
	int res;

	res = send_aux_command(kbdc, PSMC_ENABLE_DEV);
	VLOG(2, (LOG_DEBUG, "psm: ENABLE_DEV return code:%04x\n", res));

	return (res == PSM_ACK);
}

static int
disable_aux_dev(KBDC kbdc)
{
	int res;

	res = send_aux_command(kbdc, PSMC_DISABLE_DEV);
	VLOG(2, (LOG_DEBUG, "psm: DISABLE_DEV return code:%04x\n", res));

	return (res == PSM_ACK);
}

static int
get_mouse_status(KBDC kbdc, int *status, int flag, int len)
{
	int cmd;
	int res;
	int i;

	switch (flag) {
	case 0:
	default:
		cmd = PSMC_SEND_DEV_STATUS;
		break;
	case 1:
		cmd = PSMC_SEND_DEV_DATA;
		break;
	}
	empty_aux_buffer(kbdc, 5);
	res = send_aux_command(kbdc, cmd);
	VLOG(2, (LOG_DEBUG, "psm: SEND_AUX_DEV_%s return code:%04x\n",
	    (flag == 1) ? "DATA" : "STATUS", res));
	if (res != PSM_ACK)
		return (0);

	for (i = 0; i < len; ++i) {
		status[i] = read_aux_data(kbdc);
		if (status[i] < 0)
			break;
	}
	if (len >= 3) {
		for (; i < 3; ++i)
			status[i] = 0;
		VLOG(1, (LOG_DEBUG, "psm: %s %02x %02x %02x\n",
		    (flag == 1) ? "data" : "status", status[0], status[1], status[2]));
	}

	return (i);
}

static int
get_aux_id(KBDC kbdc)
{
	int res;
	int id;

	empty_aux_buffer(kbdc, 5);
	res = send_aux_command(kbdc, PSMC_SEND_DEV_ID);
	VLOG(2, (LOG_DEBUG, "psm: SEND_DEV_ID return code:%04x\n", res));
	if (res != PSM_ACK)
		return (-1);

	/* 10ms delay */
	DELAY(10000);

	id = read_aux_data(kbdc);
	VLOG(2, (LOG_DEBUG, "psm: device ID: %04x\n", id));

	return (id);
}

static int
set_mouse_sampling_rate(KBDC kbdc, int rate)
{
	int res;

	res = send_aux_command_and_data(kbdc, PSMC_SET_SAMPLING_RATE, rate);
	VLOG(2, (LOG_DEBUG, "psm: SET_SAMPLING_RATE (%d) %04x\n", rate, res));

	return ((res == PSM_ACK) ? rate : -1);
}

static int
set_mouse_scaling(KBDC kbdc, int scale)
{
	int res;

	switch (scale) {
	case 1:
	default:
		scale = PSMC_SET_SCALING11;
		break;
	case 2:
		scale = PSMC_SET_SCALING21;
		break;
	}
	res = send_aux_command(kbdc, scale);
	VLOG(2, (LOG_DEBUG, "psm: SET_SCALING%s return code:%04x\n",
	    (scale == PSMC_SET_SCALING21) ? "21" : "11", res));

	return (res == PSM_ACK);
}

/* `val' must be 0 through PSMD_MAX_RESOLUTION */
static int
set_mouse_resolution(KBDC kbdc, int val)
{
	int res;

	res = send_aux_command_and_data(kbdc, PSMC_SET_RESOLUTION, val);
	VLOG(2, (LOG_DEBUG, "psm: SET_RESOLUTION (%d) %04x\n", val, res));

	return ((res == PSM_ACK) ? val : -1);
}

/*
 * NOTE: once `set_mouse_mode()' is called, the mouse device must be
 * re-enabled by calling `enable_aux_dev()'
 */
static int
set_mouse_mode(KBDC kbdc)
{
	int res;

	res = send_aux_command(kbdc, PSMC_SET_STREAM_MODE);
	VLOG(2, (LOG_DEBUG, "psm: SET_STREAM_MODE return code:%04x\n", res));

	return (res == PSM_ACK);
}

static int
get_mouse_buttons(KBDC kbdc)
{
	int c = 2;		/* assume two buttons by default */
	int status[3];

	/*
	 * NOTE: a special sequence to obtain Logitech Mouse specific
	 * information: set resolution to 25 ppi, set scaling to 1:1, set
	 * scaling to 1:1, set scaling to 1:1. Then the second byte of the
	 * mouse status bytes is the number of available buttons.
	 * Some manufactures also support this sequence.
	 */
	if (set_mouse_resolution(kbdc, PSMD_RES_LOW) != PSMD_RES_LOW)
		return (c);
	if (set_mouse_scaling(kbdc, 1) && set_mouse_scaling(kbdc, 1) &&
	    set_mouse_scaling(kbdc, 1) &&
	    get_mouse_status(kbdc, status, 0, 3) >= 3 && status[1] != 0)
		return (status[1]);
	return (c);
}

/* misc subroutines */
/*
 * Someday, I will get the complete list of valid pointing devices and
 * their IDs... XXX
 */
static int
is_a_mouse(int id)
{
#if 0
	static int valid_ids[] = {
		PSM_MOUSE_ID,		/* mouse */
		PSM_BALLPOINT_ID,	/* ballpoint device */
		PSM_INTELLI_ID,		/* Intellimouse */
		PSM_EXPLORER_ID,	/* Intellimouse Explorer */
		-1			/* end of table */
	};
	int i;

	for (i = 0; valid_ids[i] >= 0; ++i)
	if (valid_ids[i] == id)
		return (TRUE);
	return (FALSE);
#else
	return (TRUE);
#endif
}

static char *
model_name(int model)
{
	static struct {
		int	model_code;
		char	*model_name;
	} models[] = {
		{ MOUSE_MODEL_NETSCROLL,	"NetScroll" },
		{ MOUSE_MODEL_NET,		"NetMouse/NetScroll Optical" },
		{ MOUSE_MODEL_GLIDEPOINT,	"GlidePoint" },
		{ MOUSE_MODEL_THINK,		"ThinkingMouse" },
		{ MOUSE_MODEL_INTELLI,		"IntelliMouse" },
		{ MOUSE_MODEL_MOUSEMANPLUS,	"MouseMan+" },
		{ MOUSE_MODEL_VERSAPAD,		"VersaPad" },
		{ MOUSE_MODEL_EXPLORER,		"IntelliMouse Explorer" },
		{ MOUSE_MODEL_4D,		"4D Mouse" },
		{ MOUSE_MODEL_4DPLUS,		"4D+ Mouse" },
		{ MOUSE_MODEL_SYNAPTICS,	"Synaptics Touchpad" },
		{ MOUSE_MODEL_TRACKPOINT,	"IBM/Lenovo TrackPoint" },
		{ MOUSE_MODEL_ELANTECH,		"Elantech Touchpad" },
		{ MOUSE_MODEL_GENERIC,		"Generic PS/2 mouse" },
		{ MOUSE_MODEL_UNKNOWN,		"Unknown" },
	};
	int i;

	for (i = 0; models[i].model_code != MOUSE_MODEL_UNKNOWN; ++i)
		if (models[i].model_code == model)
			break;
	return (models[i].model_name);
}

static void
recover_from_error(KBDC kbdc)
{
	/* discard anything left in the output buffer */
	empty_both_buffers(kbdc, 10);

#if 0
	/*
	 * NOTE: KBDC_RESET_KBD may not restore the communication between the
	 * keyboard and the controller.
	 */
	reset_kbd(kbdc);
#else
	/*
	 * NOTE: somehow diagnostic and keyboard port test commands bring the
	 * keyboard back.
	 */
	if (!test_controller(kbdc))
		log(LOG_ERR, "psm: keyboard controller failed.\n");
	/* if there isn't a keyboard in the system, the following error is OK */
	if (test_kbd_port(kbdc) != 0)
		VLOG(1, (LOG_ERR, "psm: keyboard port failed.\n"));
#endif
}

static int
restore_controller(KBDC kbdc, int command_byte)
{
	empty_both_buffers(kbdc, 10);

	if (!set_controller_command_byte(kbdc, 0xff, command_byte)) {
		log(LOG_ERR, "psm: failed to restore the keyboard controller "
		    "command byte.\n");
		empty_both_buffers(kbdc, 10);
		return (FALSE);
	} else {
		empty_both_buffers(kbdc, 10);
		return (TRUE);
	}
}

/*
 * Re-initialize the aux port and device. The aux port must be enabled
 * and its interrupt must be disabled before calling this routine.
 * The aux device will be disabled before returning.
 * The keyboard controller must be locked via `kbdc_lock()' before
 * calling this routine.
 */
static int
doinitialize(struct psm_softc *sc, mousemode_t *mode)
{
	KBDC kbdc = sc->kbdc;
	int stat[3];
	int i;

	switch((i = test_aux_port(kbdc))) {
	case 1:	/* ignore these errors */
	case 2:
	case 3:
	case PSM_ACK:
		if (verbose)
			log(LOG_DEBUG,
			    "psm%d: strange result for test aux port (%d).\n",
			    sc->unit, i);
		/* FALLTHROUGH */
	case 0:		/* no error */
		break;
	case -1:	/* time out */
	default:	/* error */
		recover_from_error(kbdc);
		if (sc->config & PSM_CONFIG_IGNPORTERROR)
			break;
		log(LOG_ERR, "psm%d: the aux port is not functioning (%d).\n",
		    sc->unit, i);
		return (FALSE);
	}

	if (sc->config & PSM_CONFIG_NORESET) {
		/*
		 * Don't try to reset the pointing device.  It may possibly
		 * be left in the unknown state, though...
		 */
	} else {
		/*
		 * NOTE: some controllers appears to hang the `keyboard' when
		 * the aux port doesn't exist and `PSMC_RESET_DEV' is issued.
		 */
		if (!reset_aux_dev(kbdc)) {
			recover_from_error(kbdc);
			log(LOG_ERR, "psm%d: failed to reset the aux device.\n",
			    sc->unit);
			return (FALSE);
		}
	}

	/*
	 * both the aux port and the aux device is functioning, see
	 * if the device can be enabled.
	 */
	if (!enable_aux_dev(kbdc) || !disable_aux_dev(kbdc)) {
		log(LOG_ERR, "psm%d: failed to enable the aux device.\n",
		    sc->unit);
		return (FALSE);
	}
	empty_both_buffers(kbdc, 10);	/* remove stray data if any */

	/* Re-enable the mouse. */
	for (i = 0; vendortype[i].probefunc != NULL; ++i)
		if (vendortype[i].model == sc->hw.model)
			(*vendortype[i].probefunc)(sc, REINIT);

	/* set mouse parameters */
	if (mode != (mousemode_t *)NULL) {
		if (mode->rate > 0)
			mode->rate = set_mouse_sampling_rate(kbdc, mode->rate);
		if (mode->resolution >= 0)
			mode->resolution =
			    set_mouse_resolution(kbdc, mode->resolution);
		set_mouse_scaling(kbdc, 1);
		set_mouse_mode(kbdc);
	}

	/* Record sync on the next data packet we see. */
	sc->flags |= PSM_NEED_SYNCBITS;

	/* just check the status of the mouse */
	if (get_mouse_status(kbdc, stat, 0, 3) < 3)
		log(LOG_DEBUG, "psm%d: failed to get status (doinitialize).\n",
		    sc->unit);

	return (TRUE);
}

static int
doopen(struct psm_softc *sc, int command_byte)
{
	int stat[3];
	int mux_enabled = FALSE;

	/*
	 * FIXME: Synaptics TouchPad seems to go back to Relative Mode with
	 * no obvious reason. Thus we check the current mode and restore the
	 * Absolute Mode if it was cleared.
	 *
	 * The previous hack at the end of psmprobe() wasn't efficient when
	 * moused(8) was restarted.
	 *
	 * A Reset (FF) or Set Defaults (F6) command would clear the
	 * Absolute Mode bit. But a verbose boot or debug.psm.loglevel=5
	 * doesn't show any evidence of such a command.
	 */
	if (sc->hw.model == MOUSE_MODEL_SYNAPTICS) {
		if (sc->muxport != PSM_NOMUX) {
			mux_enabled = enable_aux_mux(sc->kbdc) >= 0;
			if (mux_enabled)
				set_active_aux_mux_port(sc->kbdc, sc->muxport);
			else
				log(LOG_ERR, "psm%d: failed to enable "
				    "active multiplexing mode.\n",
				    sc->unit);
		}
		mouse_ext_command(sc->kbdc, 1);
		get_mouse_status(sc->kbdc, stat, 0, 3);
		if ((SYNAPTICS_VERSION_GE(sc->synhw, 7, 5) ||
		     stat[1] == 0x47) &&
		     stat[2] == 0x40) {
			synaptics_set_mode(sc, synaptics_preferred_mode(sc));
			VLOG(5, (LOG_DEBUG, "psm%d: Synaptis Absolute Mode "
			    "hopefully restored\n",
			    sc->unit));
		}
		if (mux_enabled)
			disable_aux_mux(sc->kbdc);
	}

	/*
	 * A user may want to disable tap and drag gestures on a Synaptics
	 * TouchPad when it operates in Relative Mode.
	 */
	if (sc->hw.model == MOUSE_MODEL_GENERIC) {
		if (tap_enabled > 0) {
			VLOG(2, (LOG_DEBUG,
			    "psm%d: enable tap and drag gestures\n",
			    sc->unit));
			synaptics_set_mode(sc, synaptics_preferred_mode(sc));
		} else if (tap_enabled == 0) {
			VLOG(2, (LOG_DEBUG,
			    "psm%d: disable tap and drag gestures\n",
			    sc->unit));
			synaptics_set_mode(sc, synaptics_preferred_mode(sc));
		}
	}

	/* enable the mouse device */
	if (!enable_aux_dev(sc->kbdc)) {
		/* MOUSE ERROR: failed to enable the mouse because:
		 * 1) the mouse is faulty,
		 * 2) the mouse has been removed(!?)
		 * In the latter case, the keyboard may have hung, and need
		 * recovery procedure...
		 */
		recover_from_error(sc->kbdc);
#if 0
		/* FIXME: we could reset the mouse here and try to enable
		 * it again. But it will take long time and it's not a good
		 * idea to disable the keyboard that long...
		 */
		if (!doinitialize(sc, &sc->mode) || !enable_aux_dev(sc->kbdc)) {
			recover_from_error(sc->kbdc);
#else
		{
#endif
			restore_controller(sc->kbdc, command_byte);
			/* mark this device is no longer available */
			sc->state &= ~PSM_VALID;
			log(LOG_ERR,
			    "psm%d: failed to enable the device (doopen).\n",
			sc->unit);
			return (EIO);
		}
	}

	if (get_mouse_status(sc->kbdc, stat, 0, 3) < 3)
		log(LOG_DEBUG, "psm%d: failed to get status (doopen).\n",
		    sc->unit);

	/* enable the aux port and interrupt */
	if (!set_controller_command_byte(sc->kbdc,
	    kbdc_get_device_mask(sc->kbdc),
	    (command_byte & KBD_KBD_CONTROL_BITS) |
	    KBD_ENABLE_AUX_PORT | KBD_ENABLE_AUX_INT)) {
		/* CONTROLLER ERROR */
		disable_aux_dev(sc->kbdc);
		restore_controller(sc->kbdc, command_byte);
		log(LOG_ERR,
		    "psm%d: failed to enable the aux interrupt (doopen).\n",
		    sc->unit);
		return (EIO);
	}

	/* start the watchdog timer */
	sc->watchdog = FALSE;
	callout_reset(&sc->callout, hz * 2, psmtimeout, sc);

	return (0);
}

static int
reinitialize(struct psm_softc *sc, int doinit)
{
	int err;
	int c;
	int s;

	/* don't let anybody mess with the aux device */
	if (!kbdc_lock(sc->kbdc, TRUE))
		return (EIO);
	s = spltty();

	/* block our watchdog timer */
	sc->watchdog = FALSE;
	callout_stop(&sc->callout);

	/* save the current controller command byte */
	empty_both_buffers(sc->kbdc, 10);
	c = get_controller_command_byte(sc->kbdc);
	VLOG(2, (LOG_DEBUG,
	    "psm%d: current command byte: %04x (reinitialize).\n",
	    sc->unit, c));

	/* enable the aux port but disable the aux interrupt and the keyboard */
	if ((c == -1) || !set_controller_command_byte(sc->kbdc,
	    kbdc_get_device_mask(sc->kbdc),
	    KBD_DISABLE_KBD_PORT | KBD_DISABLE_KBD_INT |
	    KBD_ENABLE_AUX_PORT | KBD_DISABLE_AUX_INT)) {
		/* CONTROLLER ERROR */
		splx(s);
		kbdc_lock(sc->kbdc, FALSE);
		log(LOG_ERR,
		    "psm%d: unable to set the command byte (reinitialize).\n",
		    sc->unit);
		return (EIO);
	}

	/* flush any data */
	if (sc->state & PSM_VALID) {
		/* this may fail; but never mind... */
		disable_aux_dev(sc->kbdc);
		empty_aux_buffer(sc->kbdc, 10);
	}
	flushpackets(sc);
	sc->syncerrors = 0;
	sc->pkterrors = 0;
	memset(&sc->lastinputerr, 0, sizeof(sc->lastinputerr));

	/* try to detect the aux device; are you still there? */
	err = 0;
	if (doinit) {
		if (doinitialize(sc, &sc->mode)) {
			/* yes */
			sc->state |= PSM_VALID;
		} else {
			/* the device has gone! */
			restore_controller(sc->kbdc, c);
			sc->state &= ~PSM_VALID;
			log(LOG_ERR,
			    "psm%d: the aux device has gone! (reinitialize).\n",
			    sc->unit);
			err = ENXIO;
		}
	}
	splx(s);

	/* restore the driver state */
	if ((sc->state & (PSM_OPEN | PSM_EV_OPEN_R | PSM_EV_OPEN_A)) &&
	    (err == 0)) {
		/* enable the aux device and the port again */
		err = doopen(sc, c);
		if (err != 0)
			log(LOG_ERR, "psm%d: failed to enable the device "
			    "(reinitialize).\n", sc->unit);
	} else {
		/* restore the keyboard port and disable the aux port */
		if (!set_controller_command_byte(sc->kbdc,
		    kbdc_get_device_mask(sc->kbdc),
		    (c & KBD_KBD_CONTROL_BITS) |
		    KBD_DISABLE_AUX_PORT | KBD_DISABLE_AUX_INT)) {
			/* CONTROLLER ERROR */
			log(LOG_ERR, "psm%d: failed to disable the aux port "
			    "(reinitialize).\n", sc->unit);
			err = EIO;
		}
	}

	kbdc_lock(sc->kbdc, FALSE);
	return (err);
}

/* psm driver entry points */

static void
psmidentify(driver_t *driver, device_t parent)
{
	device_t psmc;
	device_t psm;
	u_long irq;
	int unit;

	unit = device_get_unit(parent);

	/* always add at least one child */
	psm = BUS_ADD_CHILD(parent, KBDC_RID_AUX, driver->name, unit);
	if (psm == NULL)
		return;

	irq = bus_get_resource_start(psm, SYS_RES_IRQ, KBDC_RID_AUX);
	if (irq > 0)
		return;

	/*
	 * If the PS/2 mouse device has already been reported by ACPI or
	 * PnP BIOS, obtain the IRQ resource from it.
	 * (See psmcpnp_attach() below.)
	 */
	psmc = device_find_child(device_get_parent(parent),
	    PSMCPNP_DRIVER_NAME, unit);
	if (psmc == NULL)
		return;
	irq = bus_get_resource_start(psmc, SYS_RES_IRQ, 0);
	if (irq <= 0)
		return;
	bus_delete_resource(psmc, SYS_RES_IRQ, 0);
	bus_set_resource(psm, SYS_RES_IRQ, KBDC_RID_AUX, irq, 1);
}

#define	endprobe(v)	do {			\
	if (bootverbose)			\
		--verbose;			\
	kbdc_set_device_mask(sc->kbdc, mask);	\
	kbdc_lock(sc->kbdc, FALSE);		\
	return (v);				\
} while (0)

static int
psmprobe(device_t dev)
{
	int unit = device_get_unit(dev);
	struct psm_softc *sc = device_get_softc(dev);
	int stat[3];
	int command_byte;
	int mask;
	int rid;
	int i;

#if 0
	kbdc_debug(TRUE);
#endif

	/* see if IRQ is available */
	rid = KBDC_RID_AUX;
	sc->intr = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (sc->intr == NULL) {
		if (bootverbose)
			device_printf(dev, "unable to allocate IRQ\n");
		return (ENXIO);
	}
	bus_release_resource(dev, SYS_RES_IRQ, rid, sc->intr);

	sc->unit = unit;
	sc->kbdc = atkbdc_open(device_get_unit(device_get_parent(dev)));
	sc->config = device_get_flags(dev) & PSM_CONFIG_FLAGS;
	/* XXX: for backward compatibility */
#if defined(PSM_HOOKRESUME) || defined(PSM_HOOKAPM)
	sc->config |=
#ifdef PSM_RESETAFTERSUSPEND
	PSM_CONFIG_INITAFTERSUSPEND;
#else
	PSM_CONFIG_HOOKRESUME;
#endif
#endif /* PSM_HOOKRESUME | PSM_HOOKAPM */
	sc->flags = 0;
	sc->muxport = PSM_NOMUX;
	if (bootverbose)
		++verbose;

	device_set_desc(dev, "PS/2 Mouse");

	if (!kbdc_lock(sc->kbdc, TRUE)) {
		printf("psm%d: unable to lock the controller.\n", unit);
		if (bootverbose)
			--verbose;
		return (ENXIO);
	}

	/*
	 * NOTE: two bits in the command byte controls the operation of the
	 * aux port (mouse port): the aux port disable bit (bit 5) and the aux
	 * port interrupt (IRQ 12) enable bit (bit 2).
	 */

	/* discard anything left after the keyboard initialization */
	empty_both_buffers(sc->kbdc, 10);

	/* save the current command byte; it will be used later */
	mask = kbdc_get_device_mask(sc->kbdc) & ~KBD_AUX_CONTROL_BITS;
	command_byte = get_controller_command_byte(sc->kbdc);
	if (verbose)
		printf("psm%d: current command byte:%04x\n", unit,
		    command_byte);
	if (command_byte == -1) {
		/* CONTROLLER ERROR */
		printf("psm%d: unable to get the current command byte value.\n",
			unit);
		endprobe(ENXIO);
	}

	/*
	 * disable the keyboard port while probing the aux port, which must be
	 * enabled during this routine
	 */
	if (!set_controller_command_byte(sc->kbdc,
	    KBD_KBD_CONTROL_BITS | KBD_AUX_CONTROL_BITS,
	    KBD_DISABLE_KBD_PORT | KBD_DISABLE_KBD_INT |
	    KBD_ENABLE_AUX_PORT | KBD_DISABLE_AUX_INT)) {
		/*
		 * this is CONTROLLER ERROR; I don't know how to recover
		 * from this error...
		 */
		if (ALWAYS_RESTORE_CONTROLLER(sc->kbdc))
			restore_controller(sc->kbdc, command_byte);
		printf("psm%d: unable to set the command byte.\n", unit);
		endprobe(ENXIO);
	}
	write_controller_command(sc->kbdc, KBDC_ENABLE_AUX_PORT);

	/*
	 * NOTE: `test_aux_port()' is designed to return with zero if the aux
	 * port exists and is functioning. However, some controllers appears
	 * to respond with zero even when the aux port doesn't exist. (It may
	 * be that this is only the case when the controller DOES have the aux
	 * port but the port is not wired on the motherboard.) The keyboard
	 * controllers without the port, such as the original AT, are
	 * supposed to return with an error code or simply time out. In any
	 * case, we have to continue probing the port even when the controller
	 * passes this test.
	 *
	 * XXX: some controllers erroneously return the error code 1, 2 or 3
	 * when it has a perfectly functional aux port. We have to ignore
	 * this error code. Even if the controller HAS error with the aux
	 * port, it will be detected later...
	 * XXX: another incompatible controller returns PSM_ACK (0xfa)...
	 */
	switch ((i = test_aux_port(sc->kbdc))) {
	case 1:		/* ignore these errors */
	case 2:
	case 3:
	case PSM_ACK:
		if (verbose)
			printf("psm%d: strange result for test aux port "
			    "(%d).\n", unit, i);
		/* FALLTHROUGH */
	case 0:		/* no error */
		break;
	case -1:	/* time out */
	default:	/* error */
		recover_from_error(sc->kbdc);
		if (sc->config & PSM_CONFIG_IGNPORTERROR)
			break;
		if (ALWAYS_RESTORE_CONTROLLER(sc->kbdc))
			restore_controller(sc->kbdc, command_byte);
		if (verbose)
			printf("psm%d: the aux port is not functioning (%d).\n",
			    unit, i);
		endprobe(ENXIO);
	}

	if (sc->config & PSM_CONFIG_NORESET) {
		/*
		 * Don't try to reset the pointing device.  It may possibly be
		 * left in an unknown state, though...
		 */
	} else {
		/*
		 * NOTE: some controllers appears to hang the `keyboard' when
		 * the aux port doesn't exist and `PSMC_RESET_DEV' is issued.
		 *
		 * Attempt to reset the controller twice -- this helps
		 * pierce through some KVM switches. The second reset
		 * is non-fatal.
		 */
		if (!reset_aux_dev(sc->kbdc)) {
			recover_from_error(sc->kbdc);
			if (ALWAYS_RESTORE_CONTROLLER(sc->kbdc))
				restore_controller(sc->kbdc, command_byte);
			if (verbose)
				printf("psm%d: failed to reset the aux "
				    "device.\n", unit);
			endprobe(ENXIO);
		} else if (!reset_aux_dev(sc->kbdc)) {
			recover_from_error(sc->kbdc);
			if (verbose >= 2)
				printf("psm%d: failed to reset the aux device "
				    "(2).\n", unit);
		}
	}

	/*
	 * both the aux port and the aux device are functioning, see if the
	 * device can be enabled. NOTE: when enabled, the device will start
	 * sending data; we shall immediately disable the device once we know
	 * the device can be enabled.
	 */
	if (!enable_aux_dev(sc->kbdc) || !disable_aux_dev(sc->kbdc)) {
		/* MOUSE ERROR */
		recover_from_error(sc->kbdc);
		if (ALWAYS_RESTORE_CONTROLLER(sc->kbdc))
			restore_controller(sc->kbdc, command_byte);
		if (verbose)
			printf("psm%d: failed to enable the aux device.\n",
			    unit);
		endprobe(ENXIO);
	}

	/* save the default values after reset */
	if (get_mouse_status(sc->kbdc, stat, 0, 3) >= 3) {
		sc->dflt_mode.rate = sc->mode.rate = stat[2];
		sc->dflt_mode.resolution = sc->mode.resolution = stat[1];
	} else {
		sc->dflt_mode.rate = sc->mode.rate = -1;
		sc->dflt_mode.resolution = sc->mode.resolution = -1;
	}

	/* hardware information */
	sc->hw.iftype = MOUSE_IF_PS2;

	/* verify the device is a mouse */
	sc->hw.hwid = get_aux_id(sc->kbdc);
	if (!is_a_mouse(sc->hw.hwid)) {
		if (ALWAYS_RESTORE_CONTROLLER(sc->kbdc))
			restore_controller(sc->kbdc, command_byte);
		if (verbose)
			printf("psm%d: unknown device type (%d).\n", unit,
			    sc->hw.hwid);
		endprobe(ENXIO);
	}
	switch (sc->hw.hwid) {
	case PSM_BALLPOINT_ID:
		sc->hw.type = MOUSE_TRACKBALL;
		break;
	case PSM_MOUSE_ID:
	case PSM_INTELLI_ID:
	case PSM_EXPLORER_ID:
	case PSM_4DMOUSE_ID:
	case PSM_4DPLUS_ID:
		sc->hw.type = MOUSE_MOUSE;
		break;
	default:
		sc->hw.type = MOUSE_UNKNOWN;
		break;
	}

	if (sc->config & PSM_CONFIG_NOIDPROBE) {
		sc->hw.buttons = 2;
		i = GENERIC_MOUSE_ENTRY;
	} else {
		/* # of buttons */
		sc->hw.buttons = get_mouse_buttons(sc->kbdc);

		/* other parameters */
		for (i = 0; vendortype[i].probefunc != NULL; ++i)
			if ((*vendortype[i].probefunc)(sc, PROBE)) {
				if (verbose >= 2)
					printf("psm%d: found %s\n", unit,
					    model_name(vendortype[i].model));
				break;
			}
	}

	sc->hw.model = vendortype[i].model;

	sc->dflt_mode.level = PSM_LEVEL_BASE;
	sc->dflt_mode.packetsize = MOUSE_PS2_PACKETSIZE;
	sc->dflt_mode.accelfactor = (sc->config & PSM_CONFIG_ACCEL) >> 4;
	if (sc->config & PSM_CONFIG_NOCHECKSYNC)
		sc->dflt_mode.syncmask[0] = 0;
	else
		sc->dflt_mode.syncmask[0] = vendortype[i].syncmask;
	if (sc->config & PSM_CONFIG_FORCETAP)
		sc->dflt_mode.syncmask[0] &= ~MOUSE_PS2_TAP;
	sc->dflt_mode.syncmask[1] = 0;	/* syncbits */
	sc->mode = sc->dflt_mode;
	sc->mode.packetsize = vendortype[i].packetsize;

	/* set mouse parameters */
#if 0
	/*
	 * A version of Logitech FirstMouse+ won't report wheel movement,
	 * if SET_DEFAULTS is sent...  Don't use this command.
	 * This fix was found by Takashi Nishida.
	 */
	i = send_aux_command(sc->kbdc, PSMC_SET_DEFAULTS);
	if (verbose >= 2)
		printf("psm%d: SET_DEFAULTS return code:%04x\n", unit, i);
#endif
	if (sc->config & PSM_CONFIG_RESOLUTION)
		sc->mode.resolution =
		    set_mouse_resolution(sc->kbdc,
		    (sc->config & PSM_CONFIG_RESOLUTION) - 1);
	else if (sc->mode.resolution >= 0)
		sc->mode.resolution =
		    set_mouse_resolution(sc->kbdc, sc->dflt_mode.resolution);
	if (sc->mode.rate > 0)
		sc->mode.rate =
		    set_mouse_sampling_rate(sc->kbdc, sc->dflt_mode.rate);
	set_mouse_scaling(sc->kbdc, 1);

	/* Record sync on the next data packet we see. */
	sc->flags |= PSM_NEED_SYNCBITS;

	/* just check the status of the mouse */
	/*
	 * NOTE: XXX there are some arcane controller/mouse combinations out
	 * there, which hung the controller unless there is data transmission
	 * after ACK from the mouse.
	 */
	if (get_mouse_status(sc->kbdc, stat, 0, 3) < 3)
		printf("psm%d: failed to get status.\n", unit);
	else {
		/*
		 * When in its native mode, some mice operate with different
		 * default parameters than in the PS/2 compatible mode.
		 */
		sc->dflt_mode.rate = sc->mode.rate = stat[2];
		sc->dflt_mode.resolution = sc->mode.resolution = stat[1];
	}

	/* disable the aux port for now... */
	if (!set_controller_command_byte(sc->kbdc,
	    KBD_KBD_CONTROL_BITS | KBD_AUX_CONTROL_BITS,
	    (command_byte & KBD_KBD_CONTROL_BITS) |
	    KBD_DISABLE_AUX_PORT | KBD_DISABLE_AUX_INT)) {
		/*
		 * this is CONTROLLER ERROR; I don't know the proper way to
		 * recover from this error...
		 */
		if (ALWAYS_RESTORE_CONTROLLER(sc->kbdc))
			restore_controller(sc->kbdc, command_byte);
		printf("psm%d: unable to set the command byte.\n", unit);
		endprobe(ENXIO);
	}

	/* done */
	kbdc_set_device_mask(sc->kbdc, mask | KBD_AUX_CONTROL_BITS);
	kbdc_lock(sc->kbdc, FALSE);
	return (0);
}

#ifdef EVDEV_SUPPORT
/* Values are taken from Linux drivers for userland software compatibility */
#define	PS2_MOUSE_VENDOR		0x0002
#define	PS2_MOUSE_GENERIC_PRODUCT	0x0001
#define	PS2_MOUSE_SYNAPTICS_NAME	"SynPS/2 Synaptics TouchPad"
#define	PS2_MOUSE_SYNAPTICS_PRODUCT	0x0007
#define	PS2_MOUSE_TRACKPOINT_NAME	"TPPS/2 IBM TrackPoint"
#define	PS2_MOUSE_TRACKPOINT_PRODUCT	0x000A
#define	PS2_MOUSE_ELANTECH_NAME		"ETPS/2 Elantech Touchpad"
#define	PS2_MOUSE_ELANTECH_ST_NAME	"ETPS/2 Elantech TrackPoint"
#define	PS2_MOUSE_ELANTECH_PRODUCT	0x000E

#define	ABSINFO_END	{ ABS_CNT, 0, 0, 0 }

static void
psm_support_abs_bulk(struct evdev_dev *evdev, const uint16_t info[][4])
{
	size_t i;

	for (i = 0; info[i][0] != ABS_CNT; i++)
		evdev_support_abs(evdev, info[i][0], 0, info[i][1], info[i][2],
		    0, 0, info[i][3]);
}

static void
psm_push_mt_finger(struct psm_softc *sc, int id, const finger_t *f)
{
	int y = sc->synhw.minimumYCoord + sc->synhw.maximumYCoord - f->y;

	evdev_push_abs(sc->evdev_a, ABS_MT_SLOT, id);
	evdev_push_abs(sc->evdev_a, ABS_MT_TRACKING_ID, id);
	evdev_push_abs(sc->evdev_a, ABS_MT_POSITION_X, f->x);
	evdev_push_abs(sc->evdev_a, ABS_MT_POSITION_Y, y);
	evdev_push_abs(sc->evdev_a, ABS_MT_PRESSURE, f->p);
}

static void
psm_push_st_finger(struct psm_softc *sc, const finger_t *f)
{
	int y = sc->synhw.minimumYCoord + sc->synhw.maximumYCoord - f->y;

	evdev_push_abs(sc->evdev_a, ABS_X, f->x);
	evdev_push_abs(sc->evdev_a, ABS_Y, y);
	evdev_push_abs(sc->evdev_a, ABS_PRESSURE, f->p);
	if (sc->synhw.capPalmDetect)
		evdev_push_abs(sc->evdev_a, ABS_TOOL_WIDTH, f->w);
}

static void
psm_release_mt_slot(struct evdev_dev *evdev, int32_t slot)
{

	evdev_push_abs(evdev, ABS_MT_SLOT, slot);
	evdev_push_abs(evdev, ABS_MT_TRACKING_ID, -1);
}

static int
psm_register(device_t dev, int model_code)
{
	struct psm_softc *sc = device_get_softc(dev);
	struct evdev_dev *evdev_r;
	int error, i, nbuttons, nwheels, product;
	bool is_pointing_stick;
	const char *name;

	name = model_name(model_code);
	nbuttons = sc->hw.buttons;
	product = PS2_MOUSE_GENERIC_PRODUCT;
	nwheels = 0;
	is_pointing_stick = false;

	switch (model_code) {
	case MOUSE_MODEL_TRACKPOINT:
		name = PS2_MOUSE_TRACKPOINT_NAME;
		product = PS2_MOUSE_TRACKPOINT_PRODUCT;
		nbuttons = 3;
		is_pointing_stick = true;
		break;

	case MOUSE_MODEL_ELANTECH:
		name = PS2_MOUSE_ELANTECH_ST_NAME;
		product = PS2_MOUSE_ELANTECH_PRODUCT;
		nbuttons = 3;
		is_pointing_stick = true;
		break;

	case MOUSE_MODEL_MOUSEMANPLUS:
	case MOUSE_MODEL_4D:
		nwheels = 2;
		break;

	case MOUSE_MODEL_EXPLORER:
	case MOUSE_MODEL_INTELLI:
	case MOUSE_MODEL_NET:
	case MOUSE_MODEL_NETSCROLL:
	case MOUSE_MODEL_4DPLUS:
		nwheels = 1;
		break;
	}

	evdev_r = evdev_alloc();
	evdev_set_name(evdev_r, name);
	evdev_set_phys(evdev_r, device_get_nameunit(dev));
	evdev_set_id(evdev_r, BUS_I8042, PS2_MOUSE_VENDOR, product, 0);
	evdev_set_methods(evdev_r, sc, &psm_ev_methods_r);

	evdev_support_prop(evdev_r, INPUT_PROP_POINTER);
	if (is_pointing_stick)
		evdev_support_prop(evdev_r, INPUT_PROP_POINTING_STICK);
	evdev_support_event(evdev_r, EV_SYN);
	evdev_support_event(evdev_r, EV_KEY);
	evdev_support_event(evdev_r, EV_REL);
	evdev_support_rel(evdev_r, REL_X);
	evdev_support_rel(evdev_r, REL_Y);
	switch (nwheels) {
	case 2:
		evdev_support_rel(evdev_r, REL_HWHEEL);
		/* FALLTHROUGH */
	case 1:
		evdev_support_rel(evdev_r, REL_WHEEL);
	}
	for (i = 0; i < nbuttons; i++)
		evdev_support_key(evdev_r, BTN_MOUSE + i);

	error = evdev_register_mtx(evdev_r, &Giant);
	if (error)
		evdev_free(evdev_r);
	else
		sc->evdev_r = evdev_r;
	return (error);
}

static int
psm_register_synaptics(device_t dev)
{
	struct psm_softc *sc = device_get_softc(dev);
	const uint16_t synaptics_absinfo_st[][4] = {
		{ ABS_X,		sc->synhw.minimumXCoord,
		    sc->synhw.maximumXCoord, sc->synhw.infoXupmm },
		{ ABS_Y,		sc->synhw.minimumYCoord,
		    sc->synhw.maximumYCoord, sc->synhw.infoYupmm },
		{ ABS_PRESSURE,		0, ELANTECH_FINGER_MAX_P, 0 },
		ABSINFO_END,
	};
	const uint16_t synaptics_absinfo_mt[][4] = {
		{ ABS_MT_SLOT,		0, PSM_FINGERS-1, 0},
		{ ABS_MT_TRACKING_ID,	-1, PSM_FINGERS-1, 0},
		{ ABS_MT_POSITION_X,	sc->synhw.minimumXCoord,
		    sc->synhw.maximumXCoord, sc->synhw.infoXupmm },
		{ ABS_MT_POSITION_Y,	sc->synhw.minimumYCoord,
		    sc->synhw.maximumYCoord, sc->synhw.infoYupmm },
		{ ABS_MT_PRESSURE,	0, ELANTECH_FINGER_MAX_P, 0 },
		ABSINFO_END,
	};
	struct evdev_dev *evdev_a;
	int error, i, guest_model;

	evdev_a = evdev_alloc();
	evdev_set_name(evdev_a, PS2_MOUSE_SYNAPTICS_NAME);
	evdev_set_phys(evdev_a, device_get_nameunit(dev));
	evdev_set_id(evdev_a, BUS_I8042, PS2_MOUSE_VENDOR,
	    PS2_MOUSE_SYNAPTICS_PRODUCT, 0);
	evdev_set_methods(evdev_a, sc, &psm_ev_methods_a);

	evdev_support_event(evdev_a, EV_SYN);
	evdev_support_event(evdev_a, EV_KEY);
	evdev_support_event(evdev_a, EV_ABS);
	evdev_support_prop(evdev_a, INPUT_PROP_POINTER);
	if (sc->synhw.capAdvancedGestures)
		evdev_support_prop(evdev_a, INPUT_PROP_SEMI_MT);
	if (sc->synhw.capClickPad)
		evdev_support_prop(evdev_a, INPUT_PROP_BUTTONPAD);
	if (sc->synhw.capClickPad && sc->synhw.topButtonPad)
		evdev_support_prop(evdev_a, INPUT_PROP_TOPBUTTONPAD);
	evdev_support_key(evdev_a, BTN_TOUCH);
	evdev_support_nfingers(evdev_a, 3);
	psm_support_abs_bulk(evdev_a, synaptics_absinfo_st);
	if (sc->synhw.capAdvancedGestures || sc->synhw.capReportsV)
		psm_support_abs_bulk(evdev_a, synaptics_absinfo_mt);
	if (sc->synhw.capPalmDetect)
		evdev_support_abs(evdev_a, ABS_TOOL_WIDTH, 0, 0, 15, 0, 0, 0);
	evdev_support_key(evdev_a, BTN_LEFT);
	if (!sc->synhw.capClickPad) {
		evdev_support_key(evdev_a, BTN_RIGHT);
		if (sc->synhw.capExtended && sc->synhw.capMiddle)
			evdev_support_key(evdev_a, BTN_MIDDLE);
	}
	if (sc->synhw.capExtended && sc->synhw.capFourButtons) {
		evdev_support_key(evdev_a, BTN_BACK);
		evdev_support_key(evdev_a, BTN_FORWARD);
	}
	if (sc->synhw.capExtended && (sc->synhw.nExtendedButtons > 0))
		for (i = 0; i < sc->synhw.nExtendedButtons; i++)
			evdev_support_key(evdev_a, BTN_0 + i);

	error = evdev_register_mtx(evdev_a, &Giant);
	if (!error && (sc->synhw.capPassthrough || sc->muxport != PSM_NOMUX)) {
		guest_model = sc->tpinfo.sysctl_tree != NULL ?
		    MOUSE_MODEL_TRACKPOINT : MOUSE_MODEL_GENERIC;
		error = psm_register(dev, guest_model);
	}
	if (error)
		evdev_free(evdev_a);
	else
		sc->evdev_a = evdev_a;
	return (error);
}

static int
psm_register_elantech(device_t dev)
{
	struct psm_softc *sc = device_get_softc(dev);
	const uint16_t elantech_absinfo[][4] = {
		{ ABS_X,		0, sc->elanhw.sizex,
					   sc->elanhw.dpmmx },
		{ ABS_Y,		0, sc->elanhw.sizey,
					   sc->elanhw.dpmmy },
		{ ABS_PRESSURE,		0, ELANTECH_FINGER_MAX_P, 0 },
		{ ABS_TOOL_WIDTH,	0, ELANTECH_FINGER_MAX_W, 0 },
		{ ABS_MT_SLOT,		0, ELANTECH_MAX_FINGERS - 1, 0 },
		{ ABS_MT_TRACKING_ID,	-1, ELANTECH_MAX_FINGERS - 1, 0 },
		{ ABS_MT_POSITION_X,	0, sc->elanhw.sizex,
					   sc->elanhw.dpmmx },
		{ ABS_MT_POSITION_Y,	0, sc->elanhw.sizey,
					   sc->elanhw.dpmmy },
		{ ABS_MT_PRESSURE,	0, ELANTECH_FINGER_MAX_P, 0 },
		{ ABS_MT_TOUCH_MAJOR,	0, ELANTECH_FINGER_MAX_W *
					   sc->elanhw.dptracex, 0 },
		ABSINFO_END,
	};
	struct evdev_dev *evdev_a;
	int error;

	evdev_a = evdev_alloc();
	evdev_set_name(evdev_a, PS2_MOUSE_ELANTECH_NAME);
	evdev_set_phys(evdev_a, device_get_nameunit(dev));
	evdev_set_id(evdev_a, BUS_I8042, PS2_MOUSE_VENDOR,
	    PS2_MOUSE_ELANTECH_PRODUCT, 0);
	evdev_set_methods(evdev_a, sc, &psm_ev_methods_a);

	evdev_support_event(evdev_a, EV_SYN);
	evdev_support_event(evdev_a, EV_KEY);
	evdev_support_event(evdev_a, EV_ABS);
	evdev_support_prop(evdev_a, INPUT_PROP_POINTER);
	if (sc->elanhw.issemimt)
		evdev_support_prop(evdev_a, INPUT_PROP_SEMI_MT);
	if (sc->elanhw.isclickpad)
		evdev_support_prop(evdev_a, INPUT_PROP_BUTTONPAD);
	evdev_support_key(evdev_a, BTN_TOUCH);
	evdev_support_nfingers(evdev_a, ELANTECH_MAX_FINGERS);
	evdev_support_key(evdev_a, BTN_LEFT);
	if (!sc->elanhw.isclickpad)
		evdev_support_key(evdev_a, BTN_RIGHT);
	psm_support_abs_bulk(evdev_a, elantech_absinfo);

	error = evdev_register_mtx(evdev_a, &Giant);
	if (!error && sc->elanhw.hastrackpoint)
		error = psm_register(dev, MOUSE_MODEL_ELANTECH);
	if (error)
		evdev_free(evdev_a);
	else
		sc->evdev_a = evdev_a;
	return (error);
}
#endif

static int
psmattach(device_t dev)
{
	int unit = device_get_unit(dev);
	struct psm_softc *sc = device_get_softc(dev);
	int error;
	int rid;

	/* Setup initial state */
	sc->state = PSM_VALID;
	callout_init(&sc->callout, 0);
	callout_init(&sc->softcallout, 0);

	/* Setup our interrupt handler */
	rid = KBDC_RID_AUX;
	sc->intr = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (sc->intr == NULL)
		return (ENXIO);
	error = bus_setup_intr(dev, sc->intr, INTR_TYPE_TTY, NULL, psmintr, sc,
	    &sc->ih);
	if (error) {
		bus_release_resource(dev, SYS_RES_IRQ, rid, sc->intr);
		return (error);
	}

	/* Done */
	sc->dev = make_dev(&psm_cdevsw, 0, 0, 0, 0666, "psm%d", unit);
	sc->dev->si_drv1 = sc;
	sc->bdev = make_dev(&psm_cdevsw, 0, 0, 0, 0666, "bpsm%d", unit);
	sc->bdev->si_drv1 = sc;

#ifdef EVDEV_SUPPORT
	switch (sc->hw.model) {
	case MOUSE_MODEL_SYNAPTICS:
		error = psm_register_synaptics(dev);
		break;

	case MOUSE_MODEL_ELANTECH:
		error = psm_register_elantech(dev);
		break;

	default:
		error = psm_register(dev, sc->hw.model);
	}

	if (error)
		return (error);
#endif

	/* Some touchpad devices need full reinitialization after suspend. */
	switch (sc->hw.model) {
	case MOUSE_MODEL_SYNAPTICS:
	case MOUSE_MODEL_GLIDEPOINT:
	case MOUSE_MODEL_VERSAPAD:
	case MOUSE_MODEL_ELANTECH:
		sc->config |= PSM_CONFIG_INITAFTERSUSPEND;
		break;
	default:
		if (sc->synhw.infoMajor >= 4 || sc->tphw > 0)
			sc->config |= PSM_CONFIG_INITAFTERSUSPEND;
		break;
	}

	/* Elantech trackpad`s sync bit differs from touchpad`s one */
	if (sc->hw.model == MOUSE_MODEL_ELANTECH &&
	    (sc->elanhw.hascrc || sc->elanhw.hastrackpoint)) {
		sc->config |= PSM_CONFIG_NOCHECKSYNC;
		sc->flags &= ~PSM_NEED_SYNCBITS;
	}

	if (!verbose)
		printf("psm%d: model %s, device ID %d\n",
		    unit, model_name(sc->hw.model), sc->hw.hwid & 0x00ff);
	else {
		printf("psm%d: model %s, device ID %d-%02x, %d buttons\n",
		    unit, model_name(sc->hw.model), sc->hw.hwid & 0x00ff,
		    sc->hw.hwid >> 8, sc->hw.buttons);
		printf("psm%d: config:%08x, flags:%08x, packet size:%d\n",
		    unit, sc->config, sc->flags, sc->mode.packetsize);
		printf("psm%d: syncmask:%02x, syncbits:%02x%s\n",
		    unit, sc->mode.syncmask[0], sc->mode.syncmask[1],
		    sc->config & PSM_CONFIG_NOCHECKSYNC ? " (sync not checked)" : "");
	}

	if (bootverbose)
		--verbose;

	return (0);
}

static int
psmdetach(device_t dev)
{
	struct psm_softc *sc;
	int rid;

	sc = device_get_softc(dev);
	if (sc->state & PSM_OPEN)
		return (EBUSY);

#ifdef EVDEV_SUPPORT
	evdev_free(sc->evdev_r);
	evdev_free(sc->evdev_a);
#endif

	rid = KBDC_RID_AUX;
	bus_teardown_intr(dev, sc->intr, sc->ih);
	bus_release_resource(dev, SYS_RES_IRQ, rid, sc->intr);

	destroy_dev(sc->dev);
	destroy_dev(sc->bdev);

	callout_drain(&sc->callout);
	callout_drain(&sc->softcallout);

	return (0);
}

#ifdef EVDEV_SUPPORT
static int
psm_ev_open_r(struct evdev_dev *evdev)
{
	struct psm_softc *sc = evdev_get_softc(evdev);
	int err = 0;

	/* Get device data */
	if ((sc->state & PSM_VALID) == 0) {
		/* the device is no longer valid/functioning */
		return (ENXIO);
	}

	if (!(sc->state & (PSM_OPEN | PSM_EV_OPEN_A)))
		err = psmopen(sc);

	if (err == 0)
		sc->state |= PSM_EV_OPEN_R;

	return (err);
}

static int
psm_ev_close_r(struct evdev_dev *evdev)
{
	struct psm_softc *sc = evdev_get_softc(evdev);
	int err = 0;

	sc->state &= ~PSM_EV_OPEN_R;

	if (sc->state & (PSM_OPEN | PSM_EV_OPEN_A))
		return (0);

	if (sc->state & PSM_VALID)
		err = psmclose(sc);

	return (err);
}

static int
psm_ev_open_a(struct evdev_dev *evdev)
{
	struct psm_softc *sc = evdev_get_softc(evdev);
	int err = 0;

	/* Get device data */
	if ((sc->state & PSM_VALID) == 0) {
		/* the device is no longer valid/functioning */
		return (ENXIO);
	}

	if (!(sc->state & (PSM_OPEN | PSM_EV_OPEN_R)))
		err = psmopen(sc);

	if (err == 0)
		sc->state |= PSM_EV_OPEN_A;

	return (err);
}

static int
psm_ev_close_a(struct evdev_dev *evdev)
{
	struct psm_softc *sc = evdev_get_softc(evdev);
	int err = 0;

	sc->state &= ~PSM_EV_OPEN_A;

	if (sc->state & (PSM_OPEN | PSM_EV_OPEN_R))
		return (0);

	if (sc->state & PSM_VALID)
		err = psmclose(sc);

	return (err);
}
#endif

static int
psm_cdev_open(struct cdev *dev, int flag, int fmt, struct thread *td)
{
	struct psm_softc *sc;
	int err = 0;

	/* Get device data */
	sc = dev->si_drv1;
	if ((sc == NULL) || (sc->state & PSM_VALID) == 0) {
		/* the device is no longer valid/functioning */
		return (ENXIO);
	}

	/* Disallow multiple opens */
	if (sc->state & PSM_OPEN)
		return (EBUSY);

	device_busy(devclass_get_device(psm_devclass, sc->unit));

#ifdef EVDEV_SUPPORT
	/* Already opened by evdev */
	if (!(sc->state & (PSM_EV_OPEN_R | PSM_EV_OPEN_A)))
#endif
		err = psmopen(sc);

	if (err == 0)
		sc->state |= PSM_OPEN;
	else
		device_unbusy(devclass_get_device(psm_devclass, sc->unit));

	return (err);
}

static int
psm_cdev_close(struct cdev *dev, int flag, int fmt, struct thread *td)
{
	struct psm_softc *sc;
	int err = 0;

	/* Get device data */
	sc = dev->si_drv1;
	if ((sc == NULL) || (sc->state & PSM_VALID) == 0) {
		/* the device is no longer valid/functioning */
		return (ENXIO);
	}

#ifdef EVDEV_SUPPORT
	/* Still opened by evdev */
	if (!(sc->state & (PSM_EV_OPEN_R | PSM_EV_OPEN_A)))
#endif
		err = psmclose(sc);

	if (err == 0) {
		sc->state &= ~PSM_OPEN;
		/* clean up and sigio requests */
		if (sc->async != NULL) {
			funsetown(&sc->async);
			sc->async = NULL;
		}
		device_unbusy(devclass_get_device(psm_devclass, sc->unit));
	}

	return (err);
}

static int
psmopen(struct psm_softc *sc)
{
	int command_byte;
	int err;
	int s;

	/* Initialize state */
	sc->mode.level = sc->dflt_mode.level;
	sc->mode.protocol = sc->dflt_mode.protocol;
	sc->watchdog = FALSE;
	sc->async = NULL;

	/* flush the event queue */
	sc->queue.count = 0;
	sc->queue.head = 0;
	sc->queue.tail = 0;
	sc->status.flags = 0;
	sc->status.button = 0;
	sc->status.obutton = 0;
	sc->status.dx = 0;
	sc->status.dy = 0;
	sc->status.dz = 0;
	sc->button = 0;
	sc->pqueue_start = 0;
	sc->pqueue_end = 0;

	/* empty input buffer */
	flushpackets(sc);
	sc->syncerrors = 0;
	sc->pkterrors = 0;

	/* don't let timeout routines in the keyboard driver to poll the kbdc */
	if (!kbdc_lock(sc->kbdc, TRUE))
		return (EIO);

	/* save the current controller command byte */
	s = spltty();
	command_byte = get_controller_command_byte(sc->kbdc);

	/* enable the aux port and temporalily disable the keyboard */
	if (command_byte == -1 || !set_controller_command_byte(sc->kbdc,
	    kbdc_get_device_mask(sc->kbdc),
	    KBD_DISABLE_KBD_PORT | KBD_DISABLE_KBD_INT |
	    KBD_ENABLE_AUX_PORT | KBD_DISABLE_AUX_INT)) {
		/* CONTROLLER ERROR; do you know how to get out of this? */
		kbdc_lock(sc->kbdc, FALSE);
		splx(s);
		log(LOG_ERR,
		    "psm%d: unable to set the command byte (psmopen).\n",
		    sc->unit);
		return (EIO);
	}
	/*
	 * Now that the keyboard controller is told not to generate
	 * the keyboard and mouse interrupts, call `splx()' to allow
	 * the other tty interrupts. The clock interrupt may also occur,
	 * but timeout routines will be blocked by the poll flag set
	 * via `kbdc_lock()'
	 */
	splx(s);

	/* enable the mouse device */
	err = doopen(sc, command_byte);

	/* done */
	kbdc_lock(sc->kbdc, FALSE);
	return (err);
}

static int
psmclose(struct psm_softc *sc)
{
	int stat[3];
	int command_byte;
	int s;

	/* don't let timeout routines in the keyboard driver to poll the kbdc */
	if (!kbdc_lock(sc->kbdc, TRUE))
		return (EIO);

	/* save the current controller command byte */
	s = spltty();
	command_byte = get_controller_command_byte(sc->kbdc);
	if (command_byte == -1) {
		kbdc_lock(sc->kbdc, FALSE);
		splx(s);
		return (EIO);
	}

	/* disable the aux interrupt and temporalily disable the keyboard */
	if (!set_controller_command_byte(sc->kbdc,
	    kbdc_get_device_mask(sc->kbdc),
	    KBD_DISABLE_KBD_PORT | KBD_DISABLE_KBD_INT |
	    KBD_ENABLE_AUX_PORT | KBD_DISABLE_AUX_INT)) {
		log(LOG_ERR,
		    "psm%d: failed to disable the aux int (psmclose).\n",
		    sc->unit);
		/* CONTROLLER ERROR;
		 * NOTE: we shall force our way through. Because the only
		 * ill effect we shall see is that we may not be able
		 * to read ACK from the mouse, and it doesn't matter much
		 * so long as the mouse will accept the DISABLE command.
		 */
	}
	splx(s);

	/* stop the watchdog timer */
	callout_stop(&sc->callout);

	/* remove anything left in the output buffer */
	empty_aux_buffer(sc->kbdc, 10);

	/* disable the aux device, port and interrupt */
	if (sc->state & PSM_VALID) {
		if (!disable_aux_dev(sc->kbdc)) {
			/* MOUSE ERROR;
			 * NOTE: we don't return (error) and continue,
			 * pretending we have successfully disabled the device.
			 * It's OK because the interrupt routine will discard
			 * any data from the mouse hereafter.
			 */
			log(LOG_ERR,
			    "psm%d: failed to disable the device (psmclose).\n",
			    sc->unit);
		}

		if (get_mouse_status(sc->kbdc, stat, 0, 3) < 3)
			log(LOG_DEBUG,
			    "psm%d: failed to get status (psmclose).\n",
			    sc->unit);
	}

	if (!set_controller_command_byte(sc->kbdc,
	    kbdc_get_device_mask(sc->kbdc),
	    (command_byte & KBD_KBD_CONTROL_BITS) |
	    KBD_DISABLE_AUX_PORT | KBD_DISABLE_AUX_INT)) {
		/*
		 * CONTROLLER ERROR;
		 * we shall ignore this error; see the above comment.
		 */
		log(LOG_ERR,
		    "psm%d: failed to disable the aux port (psmclose).\n",
		    sc->unit);
	}

	/* remove anything left in the output buffer */
	empty_aux_buffer(sc->kbdc, 10);

	/* close is almost always successful */
	kbdc_lock(sc->kbdc, FALSE);
	return (0);
}

static int
tame_mouse(struct psm_softc *sc, packetbuf_t *pb, mousestatus_t *status,
    u_char *buf)
{
	static u_char butmapps2[8] = {
		0,
		MOUSE_PS2_BUTTON1DOWN,
		MOUSE_PS2_BUTTON2DOWN,
		MOUSE_PS2_BUTTON1DOWN | MOUSE_PS2_BUTTON2DOWN,
		MOUSE_PS2_BUTTON3DOWN,
		MOUSE_PS2_BUTTON1DOWN | MOUSE_PS2_BUTTON3DOWN,
		MOUSE_PS2_BUTTON2DOWN | MOUSE_PS2_BUTTON3DOWN,
		MOUSE_PS2_BUTTON1DOWN | MOUSE_PS2_BUTTON2DOWN |
		    MOUSE_PS2_BUTTON3DOWN,
	};
	static u_char butmapmsc[8] = {
		MOUSE_MSC_BUTTON1UP | MOUSE_MSC_BUTTON2UP |
		    MOUSE_MSC_BUTTON3UP,
		MOUSE_MSC_BUTTON2UP | MOUSE_MSC_BUTTON3UP,
		MOUSE_MSC_BUTTON1UP | MOUSE_MSC_BUTTON3UP,
		MOUSE_MSC_BUTTON3UP,
		MOUSE_MSC_BUTTON1UP | MOUSE_MSC_BUTTON2UP,
		MOUSE_MSC_BUTTON2UP,
		MOUSE_MSC_BUTTON1UP,
		0,
	};
	int mapped;
	int i;

	if (sc->mode.level == PSM_LEVEL_BASE) {
		mapped = status->button & ~MOUSE_BUTTON4DOWN;
		if (status->button & MOUSE_BUTTON4DOWN)
			mapped |= MOUSE_BUTTON1DOWN;
		status->button = mapped;
		buf[0] = MOUSE_PS2_SYNC | butmapps2[mapped & MOUSE_STDBUTTONS];
		i = imax(imin(status->dx, 255), -256);
		if (i < 0)
			buf[0] |= MOUSE_PS2_XNEG;
		buf[1] = i;
		i = imax(imin(status->dy, 255), -256);
		if (i < 0)
			buf[0] |= MOUSE_PS2_YNEG;
		buf[2] = i;
		return (MOUSE_PS2_PACKETSIZE);
	} else if (sc->mode.level == PSM_LEVEL_STANDARD) {
		buf[0] = MOUSE_MSC_SYNC |
		    butmapmsc[status->button & MOUSE_STDBUTTONS];
		i = imax(imin(status->dx, 255), -256);
		buf[1] = i >> 1;
		buf[3] = i - buf[1];
		i = imax(imin(status->dy, 255), -256);
		buf[2] = i >> 1;
		buf[4] = i - buf[2];
		i = imax(imin(status->dz, 127), -128);
		buf[5] = (i >> 1) & 0x7f;
		buf[6] = (i - (i >> 1)) & 0x7f;
		buf[7] = (~status->button >> 3) & 0x7f;
		return (MOUSE_SYS_PACKETSIZE);
	}
	return (pb->inputbytes);
}

static int
psmread(struct cdev *dev, struct uio *uio, int flag)
{
	struct psm_softc *sc = dev->si_drv1;
	u_char buf[PSM_SMALLBUFSIZE];
	int error = 0;
	int s;
	int l;

	if ((sc->state & PSM_VALID) == 0)
		return (EIO);

	/* block until mouse activity occurred */
	s = spltty();
	while (sc->queue.count <= 0) {
		if (dev != sc->bdev) {
			splx(s);
			return (EWOULDBLOCK);
		}
		sc->state |= PSM_ASLP;
		error = tsleep(sc, PZERO | PCATCH, "psmrea", 0);
		sc->state &= ~PSM_ASLP;
		if (error) {
			splx(s);
			return (error);
		} else if ((sc->state & PSM_VALID) == 0) {
			/* the device disappeared! */
			splx(s);
			return (EIO);
		}
	}
	splx(s);

	/* copy data to the user land */
	while ((sc->queue.count > 0) && (uio->uio_resid > 0)) {
		s = spltty();
		l = imin(sc->queue.count, uio->uio_resid);
		if (l > sizeof(buf))
			l = sizeof(buf);
		if (l > sizeof(sc->queue.buf) - sc->queue.head) {
			bcopy(&sc->queue.buf[sc->queue.head], &buf[0],
			    sizeof(sc->queue.buf) - sc->queue.head);
			bcopy(&sc->queue.buf[0],
			    &buf[sizeof(sc->queue.buf) - sc->queue.head],
			    l - (sizeof(sc->queue.buf) - sc->queue.head));
		} else
			bcopy(&sc->queue.buf[sc->queue.head], &buf[0], l);
		sc->queue.count -= l;
		sc->queue.head = (sc->queue.head + l) % sizeof(sc->queue.buf);
		splx(s);
		error = uiomove(buf, l, uio);
		if (error)
			break;
	}

	return (error);
}

static int
block_mouse_data(struct psm_softc *sc, int *c)
{
	int s;

	if (!kbdc_lock(sc->kbdc, TRUE))
		return (EIO);

	s = spltty();
	*c = get_controller_command_byte(sc->kbdc);
	if ((*c == -1) || !set_controller_command_byte(sc->kbdc,
	    kbdc_get_device_mask(sc->kbdc),
	    KBD_DISABLE_KBD_PORT | KBD_DISABLE_KBD_INT |
	    KBD_ENABLE_AUX_PORT | KBD_DISABLE_AUX_INT)) {
		/* this is CONTROLLER ERROR */
		splx(s);
		kbdc_lock(sc->kbdc, FALSE);
		return (EIO);
	}

	/*
	 * The device may be in the middle of status data transmission.
	 * The transmission will be interrupted, thus, incomplete status
	 * data must be discarded. Although the aux interrupt is disabled
	 * at the keyboard controller level, at most one aux interrupt
	 * may have already been pending and a data byte is in the
	 * output buffer; throw it away. Note that the second argument
	 * to `empty_aux_buffer()' is zero, so that the call will just
	 * flush the internal queue.
	 * `psmintr()' will be invoked after `splx()' if an interrupt is
	 * pending; it will see no data and returns immediately.
	 */
	empty_aux_buffer(sc->kbdc, 0);		/* flush the queue */
	read_aux_data_no_wait(sc->kbdc);	/* throw away data if any */
	flushpackets(sc);
	splx(s);

	return (0);
}

static void
dropqueue(struct psm_softc *sc)
{

	sc->queue.count = 0;
	sc->queue.head = 0;
	sc->queue.tail = 0;
	if ((sc->state & PSM_SOFTARMED) != 0) {
		sc->state &= ~PSM_SOFTARMED;
		callout_stop(&sc->softcallout);
	}
	sc->pqueue_start = sc->pqueue_end;
}

static void
flushpackets(struct psm_softc *sc)
{

	dropqueue(sc);
	bzero(&sc->pqueue, sizeof(sc->pqueue));
}

static int
unblock_mouse_data(struct psm_softc *sc, int c)
{
	int error = 0;

	/*
	 * We may have seen a part of status data during `set_mouse_XXX()'.
	 * they have been queued; flush it.
	 */
	empty_aux_buffer(sc->kbdc, 0);

	/* restore ports and interrupt */
	if (!set_controller_command_byte(sc->kbdc,
	    kbdc_get_device_mask(sc->kbdc),
	    c & (KBD_KBD_CONTROL_BITS | KBD_AUX_CONTROL_BITS))) {
		/*
		 * CONTROLLER ERROR; this is serious, we may have
		 * been left with the inaccessible keyboard and
		 * the disabled mouse interrupt.
		 */
		error = EIO;
	}

	kbdc_lock(sc->kbdc, FALSE);
	return (error);
}

static int
psmwrite(struct cdev *dev, struct uio *uio, int flag)
{
	struct psm_softc *sc = dev->si_drv1;
	u_char buf[PSM_SMALLBUFSIZE];
	int error = 0, i, l;

	if ((sc->state & PSM_VALID) == 0)
		return (EIO);

	if (sc->mode.level < PSM_LEVEL_NATIVE)
		return (ENODEV);

	/* copy data from the user land */
	while (uio->uio_resid > 0) {
		l = imin(PSM_SMALLBUFSIZE, uio->uio_resid);
		error = uiomove(buf, l, uio);
		if (error)
			break;
		for (i = 0; i < l; i++) {
			VLOG(4, (LOG_DEBUG, "psm: cmd 0x%x\n", buf[i]));
			if (!write_aux_command(sc->kbdc, buf[i])) {
				VLOG(2, (LOG_DEBUG,
				    "psm: cmd 0x%x failed.\n", buf[i]));
				return (reinitialize(sc, FALSE));
			}
		}
	}

	return (error);
}

static int
psmioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flag,
    struct thread *td)
{
	struct psm_softc *sc = dev->si_drv1;
	mousemode_t mode;
	mousestatus_t status;
	mousedata_t *data;
	int stat[3];
	int command_byte;
	int error = 0;
	int s;

	/* Perform IOCTL command */
	switch (cmd) {

	case OLD_MOUSE_GETHWINFO:
		s = spltty();
		((old_mousehw_t *)addr)->buttons = sc->hw.buttons;
		((old_mousehw_t *)addr)->iftype = sc->hw.iftype;
		((old_mousehw_t *)addr)->type = sc->hw.type;
		((old_mousehw_t *)addr)->hwid = sc->hw.hwid & 0x00ff;
		splx(s);
		break;

	case MOUSE_GETHWINFO:
		s = spltty();
		*(mousehw_t *)addr = sc->hw;
		if (sc->mode.level == PSM_LEVEL_BASE)
			((mousehw_t *)addr)->model = MOUSE_MODEL_GENERIC;
		splx(s);
		break;

	case MOUSE_SYN_GETHWINFO:
		s = spltty();
		if (sc->synhw.infoMajor >= 4)
			*(synapticshw_t *)addr = sc->synhw;
		else
			error = EINVAL;
		splx(s);
		break;

	case OLD_MOUSE_GETMODE:
		s = spltty();
		switch (sc->mode.level) {
		case PSM_LEVEL_BASE:
			((old_mousemode_t *)addr)->protocol = MOUSE_PROTO_PS2;
			break;
		case PSM_LEVEL_STANDARD:
			((old_mousemode_t *)addr)->protocol =
			    MOUSE_PROTO_SYSMOUSE;
			break;
		case PSM_LEVEL_NATIVE:
			((old_mousemode_t *)addr)->protocol = MOUSE_PROTO_PS2;
			break;
		}
		((old_mousemode_t *)addr)->rate = sc->mode.rate;
		((old_mousemode_t *)addr)->resolution = sc->mode.resolution;
		((old_mousemode_t *)addr)->accelfactor = sc->mode.accelfactor;
		splx(s);
		break;

	case MOUSE_GETMODE:
		s = spltty();
		*(mousemode_t *)addr = sc->mode;
		if ((sc->flags & PSM_NEED_SYNCBITS) != 0) {
			((mousemode_t *)addr)->syncmask[0] = 0;
			((mousemode_t *)addr)->syncmask[1] = 0;
		}
		((mousemode_t *)addr)->resolution =
			MOUSE_RES_LOW - sc->mode.resolution;
		switch (sc->mode.level) {
		case PSM_LEVEL_BASE:
			((mousemode_t *)addr)->protocol = MOUSE_PROTO_PS2;
			((mousemode_t *)addr)->packetsize =
			    MOUSE_PS2_PACKETSIZE;
			break;
		case PSM_LEVEL_STANDARD:
			((mousemode_t *)addr)->protocol = MOUSE_PROTO_SYSMOUSE;
			((mousemode_t *)addr)->packetsize =
			    MOUSE_SYS_PACKETSIZE;
			((mousemode_t *)addr)->syncmask[0] = MOUSE_SYS_SYNCMASK;
			((mousemode_t *)addr)->syncmask[1] = MOUSE_SYS_SYNC;
			break;
		case PSM_LEVEL_NATIVE:
			/* FIXME: this isn't quite correct... XXX */
			((mousemode_t *)addr)->protocol = MOUSE_PROTO_PS2;
			break;
		}
		splx(s);
		break;

	case OLD_MOUSE_SETMODE:
	case MOUSE_SETMODE:
		if (cmd == OLD_MOUSE_SETMODE) {
			mode.rate = ((old_mousemode_t *)addr)->rate;
			/*
			 * resolution  old I/F   new I/F
			 * default        0         0
			 * low            1        -2
			 * medium low     2        -3
			 * medium high    3        -4
			 * high           4        -5
			 */
			if (((old_mousemode_t *)addr)->resolution > 0)
				mode.resolution =
				    -((old_mousemode_t *)addr)->resolution - 1;
			else
				mode.resolution = 0;
			mode.accelfactor =
			    ((old_mousemode_t *)addr)->accelfactor;
			mode.level = -1;
		} else
			mode = *(mousemode_t *)addr;

		/* adjust and validate parameters. */
		if (mode.rate > UCHAR_MAX)
			return (EINVAL);
		if (mode.rate == 0)
			mode.rate = sc->dflt_mode.rate;
		else if (mode.rate == -1)
			/* don't change the current setting */
			;
		else if (mode.rate < 0)
			return (EINVAL);
		if (mode.resolution >= UCHAR_MAX)
			return (EINVAL);
		if (mode.resolution >= 200)
			mode.resolution = MOUSE_RES_HIGH;
		else if (mode.resolution >= 100)
			mode.resolution = MOUSE_RES_MEDIUMHIGH;
		else if (mode.resolution >= 50)
			mode.resolution = MOUSE_RES_MEDIUMLOW;
		else if (mode.resolution > 0)
			mode.resolution = MOUSE_RES_LOW;
		if (mode.resolution == MOUSE_RES_DEFAULT)
			mode.resolution = sc->dflt_mode.resolution;
		else if (mode.resolution == -1)
			/* don't change the current setting */
			;
		else if (mode.resolution < 0) /* MOUSE_RES_LOW/MEDIUM/HIGH */
			mode.resolution = MOUSE_RES_LOW - mode.resolution;
		if (mode.level == -1)
			/* don't change the current setting */
			mode.level = sc->mode.level;
		else if ((mode.level < PSM_LEVEL_MIN) ||
		    (mode.level > PSM_LEVEL_MAX))
			return (EINVAL);
		if (mode.accelfactor == -1)
			/* don't change the current setting */
			mode.accelfactor = sc->mode.accelfactor;
		else if (mode.accelfactor < 0)
			return (EINVAL);

		/* don't allow anybody to poll the keyboard controller */
		error = block_mouse_data(sc, &command_byte);
		if (error)
			return (error);

		/* set mouse parameters */
		if (mode.rate > 0)
			mode.rate = set_mouse_sampling_rate(sc->kbdc,
			    mode.rate);
		if (mode.resolution >= 0)
			mode.resolution =
			    set_mouse_resolution(sc->kbdc, mode.resolution);
		set_mouse_scaling(sc->kbdc, 1);
		get_mouse_status(sc->kbdc, stat, 0, 3);

		s = spltty();
		sc->mode.rate = mode.rate;
		sc->mode.resolution = mode.resolution;
		sc->mode.accelfactor = mode.accelfactor;
		sc->mode.level = mode.level;
		splx(s);

		unblock_mouse_data(sc, command_byte);
		break;

	case MOUSE_GETLEVEL:
		*(int *)addr = sc->mode.level;
		break;

	case MOUSE_SETLEVEL:
		if ((*(int *)addr < PSM_LEVEL_MIN) ||
		    (*(int *)addr > PSM_LEVEL_MAX))
			return (EINVAL);
		sc->mode.level = *(int *)addr;
		break;

	case MOUSE_GETSTATUS:
		s = spltty();
		status = sc->status;
		sc->status.flags = 0;
		sc->status.obutton = sc->status.button;
		sc->status.button = 0;
		sc->status.dx = 0;
		sc->status.dy = 0;
		sc->status.dz = 0;
		splx(s);
		*(mousestatus_t *)addr = status;
		break;

	case MOUSE_READSTATE:
	case MOUSE_READDATA:
		data = (mousedata_t *)addr;
		if (data->len > sizeof(data->buf)/sizeof(data->buf[0]))
			return (EINVAL);

		error = block_mouse_data(sc, &command_byte);
		if (error)
			return (error);
		if ((data->len = get_mouse_status(sc->kbdc, data->buf,
		    (cmd == MOUSE_READDATA) ? 1 : 0, data->len)) <= 0)
			error = EIO;
		unblock_mouse_data(sc, command_byte);
		break;

#if (defined(MOUSE_SETRESOLUTION))
	case MOUSE_SETRESOLUTION:
		mode.resolution = *(int *)addr;
		if (mode.resolution >= UCHAR_MAX)
			return (EINVAL);
		else if (mode.resolution >= 200)
			mode.resolution = MOUSE_RES_HIGH;
		else if (mode.resolution >= 100)
			mode.resolution = MOUSE_RES_MEDIUMHIGH;
		else if (mode.resolution >= 50)
			mode.resolution = MOUSE_RES_MEDIUMLOW;
		else if (mode.resolution > 0)
			mode.resolution = MOUSE_RES_LOW;
		if (mode.resolution == MOUSE_RES_DEFAULT)
			mode.resolution = sc->dflt_mode.resolution;
		else if (mode.resolution == -1)
			mode.resolution = sc->mode.resolution;
		else if (mode.resolution < 0) /* MOUSE_RES_LOW/MEDIUM/HIGH */
			mode.resolution = MOUSE_RES_LOW - mode.resolution;

		error = block_mouse_data(sc, &command_byte);
		if (error)
			return (error);
		sc->mode.resolution =
		    set_mouse_resolution(sc->kbdc, mode.resolution);
		if (sc->mode.resolution != mode.resolution)
			error = EIO;
		unblock_mouse_data(sc, command_byte);
		break;
#endif /* MOUSE_SETRESOLUTION */

#if (defined(MOUSE_SETRATE))
	case MOUSE_SETRATE:
		mode.rate = *(int *)addr;
		if (mode.rate > UCHAR_MAX)
			return (EINVAL);
		if (mode.rate == 0)
			mode.rate = sc->dflt_mode.rate;
		else if (mode.rate < 0)
			mode.rate = sc->mode.rate;

		error = block_mouse_data(sc, &command_byte);
		if (error)
			return (error);
		sc->mode.rate = set_mouse_sampling_rate(sc->kbdc, mode.rate);
		if (sc->mode.rate != mode.rate)
			error = EIO;
		unblock_mouse_data(sc, command_byte);
		break;
#endif /* MOUSE_SETRATE */

#if (defined(MOUSE_SETSCALING))
	case MOUSE_SETSCALING:
		if ((*(int *)addr <= 0) || (*(int *)addr > 2))
			return (EINVAL);

		error = block_mouse_data(sc, &command_byte);
		if (error)
			return (error);
		if (!set_mouse_scaling(sc->kbdc, *(int *)addr))
			error = EIO;
		unblock_mouse_data(sc, command_byte);
		break;
#endif /* MOUSE_SETSCALING */

#if (defined(MOUSE_GETHWID))
	case MOUSE_GETHWID:
		error = block_mouse_data(sc, &command_byte);
		if (error)
			return (error);
		sc->hw.hwid &= ~0x00ff;
		sc->hw.hwid |= get_aux_id(sc->kbdc);
		*(int *)addr = sc->hw.hwid & 0x00ff;
		unblock_mouse_data(sc, command_byte);
		break;
#endif /* MOUSE_GETHWID */

	case FIONBIO:
	case FIOASYNC:
		break;
	case FIOSETOWN:
		error = fsetown(*(int *)addr, &sc->async);
		break;
	case FIOGETOWN:
		*(int *) addr = fgetown(&sc->async);
		break;
	default:
		return (ENOTTY);
	}

	return (error);
}

static void
psmtimeout(void *arg)
{
	struct psm_softc *sc;
	int s;

	sc = (struct psm_softc *)arg;
	s = spltty();
	if (sc->watchdog && kbdc_lock(sc->kbdc, TRUE)) {
		VLOG(6, (LOG_DEBUG, "psm%d: lost interrupt?\n", sc->unit));
		psmintr(sc);
		kbdc_lock(sc->kbdc, FALSE);
	}
	sc->watchdog = TRUE;
	splx(s);
	callout_reset(&sc->callout, hz, psmtimeout, sc);
}

/* Add all sysctls under the debug.psm and hw.psm nodes */
static SYSCTL_NODE(_debug, OID_AUTO, psm, CTLFLAG_RD, 0, "ps/2 mouse");
static SYSCTL_NODE(_hw, OID_AUTO, psm, CTLFLAG_RD, 0, "ps/2 mouse");

SYSCTL_INT(_debug_psm, OID_AUTO, loglevel, CTLFLAG_RWTUN, &verbose, 0,
    "Verbosity level");

static int psmhz = 20;
SYSCTL_INT(_debug_psm, OID_AUTO, hz, CTLFLAG_RW, &psmhz, 0,
    "Frequency of the softcallout (in hz)");
static int psmerrsecs = 2;
SYSCTL_INT(_debug_psm, OID_AUTO, errsecs, CTLFLAG_RW, &psmerrsecs, 0,
    "Number of seconds during which packets will dropped after a sync error");
static int psmerrusecs = 0;
SYSCTL_INT(_debug_psm, OID_AUTO, errusecs, CTLFLAG_RW, &psmerrusecs, 0,
    "Microseconds to add to psmerrsecs");
static int psmsecs = 0;
SYSCTL_INT(_debug_psm, OID_AUTO, secs, CTLFLAG_RW, &psmsecs, 0,
    "Max number of seconds between soft interrupts");
static int psmusecs = 500000;
SYSCTL_INT(_debug_psm, OID_AUTO, usecs, CTLFLAG_RW, &psmusecs, 0,
    "Microseconds to add to psmsecs");
static int pkterrthresh = 2;
SYSCTL_INT(_debug_psm, OID_AUTO, pkterrthresh, CTLFLAG_RW, &pkterrthresh, 0,
    "Number of error packets allowed before reinitializing the mouse");

SYSCTL_INT(_hw_psm, OID_AUTO, tap_enabled, CTLFLAG_RWTUN, &tap_enabled, 0,
    "Enable tap and drag gestures");
static int tap_threshold = PSM_TAP_THRESHOLD;
SYSCTL_INT(_hw_psm, OID_AUTO, tap_threshold, CTLFLAG_RW, &tap_threshold, 0,
    "Button tap threshold");
static int tap_timeout = PSM_TAP_TIMEOUT;
SYSCTL_INT(_hw_psm, OID_AUTO, tap_timeout, CTLFLAG_RW, &tap_timeout, 0,
    "Tap timeout for touchpads");

/* Tunables */
SYSCTL_INT(_hw_psm, OID_AUTO, synaptics_support, CTLFLAG_RDTUN,
    &synaptics_support, 0, "Enable support for Synaptics touchpads");

SYSCTL_INT(_hw_psm, OID_AUTO, trackpoint_support, CTLFLAG_RDTUN,
    &trackpoint_support, 0, "Enable support for IBM/Lenovo TrackPoint");

SYSCTL_INT(_hw_psm, OID_AUTO, elantech_support, CTLFLAG_RDTUN,
    &elantech_support, 0, "Enable support for Elantech touchpads");

static void
psmintr(void *arg)
{
	struct psm_softc *sc = arg;
	struct timeval now;
	int c;
	packetbuf_t *pb;

	if (aux_mux_is_enabled(sc->kbdc))
		VLOG(2, (LOG_DEBUG, "psmintr: active multiplexing mode is not "
		    "supported!\n"));

	/* read until there is nothing to read */
	while((c = read_aux_data_no_wait(sc->kbdc)) != -1) {
		pb = &sc->pqueue[sc->pqueue_end];

		/* discard the byte if the device is not open */
		if (!(sc->state & (PSM_OPEN | PSM_EV_OPEN_R | PSM_EV_OPEN_A)))
			continue;

		getmicrouptime(&now);
		if ((pb->inputbytes > 0) &&
		    timevalcmp(&now, &sc->inputtimeout, >)) {
			VLOG(3, (LOG_DEBUG, "psmintr: delay too long; "
			    "resetting byte count\n"));
			pb->inputbytes = 0;
			sc->syncerrors = 0;
			sc->pkterrors = 0;
		}
		sc->inputtimeout.tv_sec = PSM_INPUT_TIMEOUT / 1000000;
		sc->inputtimeout.tv_usec = PSM_INPUT_TIMEOUT % 1000000;
		timevaladd(&sc->inputtimeout, &now);

		pb->ipacket[pb->inputbytes++] = c;

		if (sc->mode.level == PSM_LEVEL_NATIVE) {
			VLOG(4, (LOG_DEBUG, "psmintr: %02x\n", pb->ipacket[0]));
			sc->syncerrors = 0;
			sc->pkterrors = 0;
			goto next;
		} else {
			if (pb->inputbytes < sc->mode.packetsize)
				continue;

			VLOG(4, (LOG_DEBUG,
			    "psmintr: %02x %02x %02x %02x %02x %02x\n",
			    pb->ipacket[0], pb->ipacket[1], pb->ipacket[2],
			    pb->ipacket[3], pb->ipacket[4], pb->ipacket[5]));
		}

		c = pb->ipacket[0];

		if ((sc->flags & PSM_NEED_SYNCBITS) != 0) {
			sc->mode.syncmask[1] = (c & sc->mode.syncmask[0]);
			sc->flags &= ~PSM_NEED_SYNCBITS;
			VLOG(2, (LOG_DEBUG,
			    "psmintr: Sync bytes now %04x,%04x\n",
			    sc->mode.syncmask[0], sc->mode.syncmask[1]));
		} else if ((sc->config & PSM_CONFIG_NOCHECKSYNC) == 0 &&
		    (c & sc->mode.syncmask[0]) != sc->mode.syncmask[1]) {
			VLOG(3, (LOG_DEBUG, "psmintr: out of sync "
			    "(%04x != %04x) %d cmds since last error.\n",
			    c & sc->mode.syncmask[0], sc->mode.syncmask[1],
			    sc->cmdcount - sc->lasterr));
			sc->lasterr = sc->cmdcount;
			/*
			 * The sync byte test is a weak measure of packet
			 * validity.  Conservatively discard any input yet
			 * to be seen by userland when we detect a sync
			 * error since there is a good chance some of
			 * the queued packets have undetected errors.
			 */
			dropqueue(sc);
			if (sc->syncerrors == 0)
				sc->pkterrors++;
			++sc->syncerrors;
			sc->lastinputerr = now;
			if (sc->syncerrors >= sc->mode.packetsize * 2 ||
			    sc->pkterrors >= pkterrthresh) {
				/*
				 * If we've failed to find a single sync byte
				 * in 2 packets worth of data, or we've seen
				 * persistent packet errors during the
				 * validation period, reinitialize the mouse
				 * in hopes of returning it to the expected
				 * mode.
				 */
				VLOG(3, (LOG_DEBUG,
				    "psmintr: reset the mouse.\n"));
				reinitialize(sc, TRUE);
			} else if (sc->syncerrors == sc->mode.packetsize) {
				/*
				 * Try a soft reset after searching for a sync
				 * byte through a packet length of bytes.
				 */
				VLOG(3, (LOG_DEBUG,
				    "psmintr: re-enable the mouse.\n"));
				pb->inputbytes = 0;
				disable_aux_dev(sc->kbdc);
				enable_aux_dev(sc->kbdc);
			} else {
				VLOG(3, (LOG_DEBUG,
				    "psmintr: discard a byte (%d)\n",
				    sc->syncerrors));
				pb->inputbytes--;
				bcopy(&pb->ipacket[1], &pb->ipacket[0],
				    pb->inputbytes);
			}
			continue;
		}

		/*
		 * We have what appears to be a valid packet.
		 * Reset the error counters.
		 */
		sc->syncerrors = 0;

		/*
		 * Drop even good packets if they occur within a timeout
		 * period of a sync error.  This allows the detection of
		 * a change in the mouse's packet mode without exposing
		 * erratic mouse behavior to the user.  Some KVMs forget
		 * enhanced mouse modes during switch events.
		 */
		if (!timeelapsed(&sc->lastinputerr, psmerrsecs, psmerrusecs,
		    &now)) {
			pb->inputbytes = 0;
			continue;
		}

		/*
		 * Now that we're out of the validation period, reset
		 * the packet error count.
		 */
		sc->pkterrors = 0;

		sc->cmdcount++;
next:
		if (++sc->pqueue_end >= PSM_PACKETQUEUE)
			sc->pqueue_end = 0;
		/*
		 * If we've filled the queue then call the softintr ourselves,
		 * otherwise schedule the interrupt for later.
		 */
		if (!timeelapsed(&sc->lastsoftintr, psmsecs, psmusecs, &now) ||
		    (sc->pqueue_end == sc->pqueue_start)) {
			if ((sc->state & PSM_SOFTARMED) != 0) {
				sc->state &= ~PSM_SOFTARMED;
				callout_stop(&sc->softcallout);
			}
			psmsoftintr(arg);
		} else if ((sc->state & PSM_SOFTARMED) == 0) {
			sc->state |= PSM_SOFTARMED;
			callout_reset(&sc->softcallout,
			    psmhz < 1 ? 1 : (hz/psmhz), psmsoftintr, arg);
		}
	}
}

static void
proc_mmanplus(struct psm_softc *sc, packetbuf_t *pb, mousestatus_t *ms,
    int *x, int *y, int *z)
{

	/*
	 * PS2++ protocol packet
	 *
	 *          b7 b6 b5 b4 b3 b2 b1 b0
	 * byte 1:  *  1  p3 p2 1  *  *  *
	 * byte 2:  c1 c2 p1 p0 d1 d0 1  0
	 *
	 * p3-p0: packet type
	 * c1, c2: c1 & c2 == 1, if p2 == 0
	 *         c1 & c2 == 0, if p2 == 1
	 *
	 * packet type: 0 (device type)
	 * See comments in enable_mmanplus() below.
	 *
	 * packet type: 1 (wheel data)
	 *
	 *          b7 b6 b5 b4 b3 b2 b1 b0
	 * byte 3:  h  *  B5 B4 s  d2 d1 d0
	 *
	 * h: 1, if horizontal roller data
	 *    0, if vertical roller data
	 * B4, B5: button 4 and 5
	 * s: sign bit
	 * d2-d0: roller data
	 *
	 * packet type: 2 (reserved)
	 */
	if (((pb->ipacket[0] & MOUSE_PS2PLUS_SYNCMASK) == MOUSE_PS2PLUS_SYNC) &&
	    (abs(*x) > 191) && MOUSE_PS2PLUS_CHECKBITS(pb->ipacket)) {
		/*
		 * the extended data packet encodes button
		 * and wheel events
		 */
		switch (MOUSE_PS2PLUS_PACKET_TYPE(pb->ipacket)) {
		case 1:
			/* wheel data packet */
			*x = *y = 0;
			if (pb->ipacket[2] & 0x80) {
				/* XXX horizontal roller count - ignore it */
				;
			} else {
				/* vertical roller count */
				*z = (pb->ipacket[2] & MOUSE_PS2PLUS_ZNEG) ?
				    (pb->ipacket[2] & 0x0f) - 16 :
				    (pb->ipacket[2] & 0x0f);
			}
			ms->button |= (pb->ipacket[2] &
			    MOUSE_PS2PLUS_BUTTON4DOWN) ?
			    MOUSE_BUTTON4DOWN : 0;
			ms->button |= (pb->ipacket[2] &
			    MOUSE_PS2PLUS_BUTTON5DOWN) ?
			    MOUSE_BUTTON5DOWN : 0;
			break;
		case 2:
			/*
			 * this packet type is reserved by
			 * Logitech...
			 */
			/*
			 * IBM ScrollPoint Mouse uses this
			 * packet type to encode both vertical
			 * and horizontal scroll movement.
			 */
			*x = *y = 0;
			/* horizontal count */
			if (pb->ipacket[2] & 0x0f)
				*z = (pb->ipacket[2] & MOUSE_SPOINT_WNEG) ?
				    -2 : 2;
			/* vertical count */
			if (pb->ipacket[2] & 0xf0)
				*z = (pb->ipacket[2] & MOUSE_SPOINT_ZNEG) ?
				    -1 : 1;
			break;
		case 0:
			/* device type packet - shouldn't happen */
			/* FALLTHROUGH */
		default:
			*x = *y = 0;
			ms->button = ms->obutton;
			VLOG(1, (LOG_DEBUG, "psmintr: unknown PS2++ packet "
			    "type %d: 0x%02x 0x%02x 0x%02x\n",
			    MOUSE_PS2PLUS_PACKET_TYPE(pb->ipacket),
			    pb->ipacket[0], pb->ipacket[1], pb->ipacket[2]));
			break;
		}
	} else {
		/* preserve button states */
		ms->button |= ms->obutton & MOUSE_EXTBUTTONS;
	}
}

static int
proc_synaptics(struct psm_softc *sc, packetbuf_t *pb, mousestatus_t *ms,
    int *x, int *y, int *z)
{
	static int touchpad_buttons;
	static int guest_buttons;
	static finger_t f[PSM_FINGERS];
	int w, id, nfingers, ewcode, extended_buttons, clickpad_pressed;

	extended_buttons = 0;

	/* TouchPad PS/2 absolute mode message format with capFourButtons:
	 *
	 *  Bits:        7   6   5   4   3   2   1   0 (LSB)
	 *  ------------------------------------------------
	 *  ipacket[0]:  1   0  W3  W2   0  W1   R   L
	 *  ipacket[1]: Yb  Ya  Y9  Y8  Xb  Xa  X9  X8
	 *  ipacket[2]: Z7  Z6  Z5  Z4  Z3  Z2  Z1  Z0
	 *  ipacket[3]:  1   1  Yc  Xc   0  W0 D^R U^L
	 *  ipacket[4]: X7  X6  X5  X4  X3  X2  X1  X0
	 *  ipacket[5]: Y7  Y6  Y5  Y4  Y3  Y2  Y1  Y0
	 *
	 * Legend:
	 *  L: left physical mouse button
	 *  R: right physical mouse button
	 *  D: down button
	 *  U: up button
	 *  W: "wrist" value
	 *  X: x position
	 *  Y: y position
	 *  Z: pressure
	 *
	 * Without capFourButtons but with nExtendeButtons and/or capMiddle
	 *
	 *  Bits:        7   6   5   4      3      2      1      0 (LSB)
	 *  ------------------------------------------------------
	 *  ipacket[3]:  1   1  Yc  Xc      0     W0    E^R    M^L
	 *  ipacket[4]: X7  X6  X5  X4  X3|b7  X2|b5  X1|b3  X0|b1
	 *  ipacket[5]: Y7  Y6  Y5  Y4  Y3|b8  Y2|b6  Y1|b4  Y0|b2
	 *
	 * Legend:
	 *  M: Middle physical mouse button
	 *  E: Extended mouse buttons reported instead of low bits of X and Y
	 *  b1-b8: Extended mouse buttons
	 *    Only ((nExtendedButtons + 1) >> 1) bits are used in packet
	 *    4 and 5, for reading X and Y value they should be zeroed.
	 *
	 * Absolute reportable limits:    0 - 6143.
	 * Typical bezel limits:       1472 - 5472.
	 * Typical edge marings:       1632 - 5312.
	 *
	 * w = 3 Passthrough Packet
	 *
	 * Byte 2,5,6 == Byte 1,2,3 of "Guest"
	 */

	if (!synaptics_support)
		return (0);

	/* Sanity check for out of sync packets. */
	if ((pb->ipacket[0] & 0xc8) != 0x80 ||
	    (pb->ipacket[3] & 0xc8) != 0xc0)
		return (-1);

	*x = *y = 0;
	ms->button = ms->obutton;

	/*
	 * Pressure value.
	 * Interpretation:
	 *   z = 0      No finger contact
	 *   z = 10     Finger hovering near the pad
	 *   z = 30     Very light finger contact
	 *   z = 80     Normal finger contact
	 *   z = 110    Very heavy finger contact
	 *   z = 200    Finger lying flat on pad surface
	 *   z = 255    Maximum reportable Z
	 */
	*z = pb->ipacket[2];

	/*
	 * Finger width value
	 * Interpretation:
	 *   w = 0      Two finger on the pad (capMultiFinger needed)
	 *   w = 1      Three or more fingers (capMultiFinger needed)
	 *   w = 2      Pen (instead of finger) (capPen needed)
	 *   w = 3      Reserved (passthrough?)
	 *   w = 4-7    Finger of normal width (capPalmDetect needed)
	 *   w = 8-14   Very wide finger or palm (capPalmDetect needed)
	 *   w = 15     Maximum reportable width (capPalmDetect needed)
	 */
	/* XXX Is checking capExtended enough? */
	if (sc->synhw.capExtended)
		w = ((pb->ipacket[0] & 0x30) >> 2) |
		    ((pb->ipacket[0] & 0x04) >> 1) |
		    ((pb->ipacket[3] & 0x04) >> 2);
	else {
		/* Assume a finger of regular width. */
		w = 4;
	}

	switch (w) {
	case 3:
		/*
		 * Handle packets from the guest device. See:
		 * Synaptics PS/2 TouchPad Interfacing Guide, Section 5.1
		 */
		if (sc->synhw.capPassthrough || sc->muxport != PSM_NOMUX) {
			*x = ((pb->ipacket[1] & 0x10) ?
			    pb->ipacket[4] - 256 : pb->ipacket[4]);
			*y = ((pb->ipacket[1] & 0x20) ?
			    pb->ipacket[5] - 256 : pb->ipacket[5]);
			*z = 0;

			guest_buttons = 0;
			if (pb->ipacket[1] & 0x01)
				guest_buttons |= MOUSE_BUTTON1DOWN;
			if (pb->ipacket[1] & 0x04)
				guest_buttons |= MOUSE_BUTTON2DOWN;
			if (pb->ipacket[1] & 0x02)
				guest_buttons |= MOUSE_BUTTON3DOWN;
#ifdef EVDEV_SUPPORT
			if (evdev_rcpt_mask & EVDEV_RCPT_HW_MOUSE) {
				evdev_push_rel(sc->evdev_r, REL_X, *x);
				evdev_push_rel(sc->evdev_r, REL_Y, -*y);
				evdev_push_mouse_btn(sc->evdev_r,
				    guest_buttons);
				evdev_sync(sc->evdev_r);
			}
#endif
			ms->button = touchpad_buttons | guest_buttons |
			    sc->extended_buttons;
		}
		goto SYNAPTICS_END;

	case 2:
		/* Handle Extended W mode packets */
		ewcode = (pb->ipacket[5] & 0xf0) >> 4;
#if PSM_FINGERS > 1
		switch (ewcode) {
		case 1:
			/* Secondary finger */
			if (sc->synhw.capAdvancedGestures)
				f[1] = (finger_t) {
					.x = (((pb->ipacket[4] & 0x0f) << 8) |
					    pb->ipacket[1]) << 1,
					.y = (((pb->ipacket[4] & 0xf0) << 4) |
					    pb->ipacket[2]) << 1,
					.p = ((pb->ipacket[3] & 0x30) |
					    (pb->ipacket[5] & 0x0f)) << 1,
					.w = PSM_FINGER_DEFAULT_W,
					.flags = PSM_FINGER_FUZZY,
				};
			else if (sc->synhw.capReportsV)
				f[1] = (finger_t) {
					.x = (((pb->ipacket[4] & 0x0f) << 8) |
					    (pb->ipacket[1] & 0xfe)) << 1,
					.y = (((pb->ipacket[4] & 0xf0) << 4) |
					    (pb->ipacket[2] & 0xfe)) << 1,
					.p = ((pb->ipacket[3] & 0x30) |
					    (pb->ipacket[5] & 0x0e)) << 1,
					.w = (((pb->ipacket[5] & 0x01) << 2) |
					    ((pb->ipacket[2] & 0x01) << 1) |
					    (pb->ipacket[1] & 0x01)) + 8,
					.flags = PSM_FINGER_FUZZY,
				};
		default:
			break;
		}
#endif
		goto SYNAPTICS_END;

	case 1:
	case 0:
		nfingers = w + 2;
		break;

	default:
		nfingers = 1;
	}

	if (sc->syninfo.touchpad_off)
		goto SYNAPTICS_END;

	/* Button presses */
	touchpad_buttons = 0;
	if (pb->ipacket[0] & 0x01)
		touchpad_buttons |= MOUSE_BUTTON1DOWN;
	if (pb->ipacket[0] & 0x02)
		touchpad_buttons |= MOUSE_BUTTON3DOWN;

	if (sc->synhw.capExtended && sc->synhw.capFourButtons) {
		if ((pb->ipacket[3] ^ pb->ipacket[0]) & 0x01)
			touchpad_buttons |= MOUSE_BUTTON4DOWN;
		if ((pb->ipacket[3] ^ pb->ipacket[0]) & 0x02)
			touchpad_buttons |= MOUSE_BUTTON5DOWN;
	} else if (sc->synhw.capExtended && sc->synhw.capMiddle &&
	    !sc->synhw.capClickPad) {
		/* Middle Button */
		if ((pb->ipacket[0] ^ pb->ipacket[3]) & 0x01)
			touchpad_buttons |= MOUSE_BUTTON2DOWN;
	} else if (sc->synhw.capExtended && (sc->synhw.nExtendedButtons > 0)) {
		/* Extended Buttons */
		if ((pb->ipacket[0] ^ pb->ipacket[3]) & 0x02) {
			if (sc->syninfo.directional_scrolls) {
				if (pb->ipacket[4] & 0x01)
					extended_buttons |= MOUSE_BUTTON4DOWN;
				if (pb->ipacket[5] & 0x01)
					extended_buttons |= MOUSE_BUTTON5DOWN;
				if (pb->ipacket[4] & 0x02)
					extended_buttons |= MOUSE_BUTTON6DOWN;
				if (pb->ipacket[5] & 0x02)
					extended_buttons |= MOUSE_BUTTON7DOWN;
			} else {
				if (pb->ipacket[4] & 0x01)
					extended_buttons |= MOUSE_BUTTON1DOWN;
				if (pb->ipacket[5] & 0x01)
					extended_buttons |= MOUSE_BUTTON3DOWN;
				if (pb->ipacket[4] & 0x02)
					extended_buttons |= MOUSE_BUTTON2DOWN;
				sc->extended_buttons = extended_buttons;
			}

			/*
			 * Zero out bits used by extended buttons to avoid
			 * misinterpretation of the data absolute position.
			 *
			 * The bits represented by
			 *
			 *     (nExtendedButtons + 1) >> 1
			 *
			 * will be masked out in both bytes.
			 * The mask for n bits is computed with the formula
			 *
			 *     (1 << n) - 1
			 */
			int maskedbits = 0;
			int mask = 0;
			maskedbits = (sc->synhw.nExtendedButtons + 1) >> 1;
			mask = (1 << maskedbits) - 1;
#ifdef EVDEV_SUPPORT
			int i;
			if (evdev_rcpt_mask & EVDEV_RCPT_HW_MOUSE) {
				if (sc->synhw.capPassthrough) {
					evdev_push_mouse_btn(sc->evdev_r,
						extended_buttons);
					evdev_sync(sc->evdev_r);
				}
				for (i = 0; i < maskedbits; i++) {
					evdev_push_key(sc->evdev_a,
					    BTN_0 + i * 2,
					    pb->ipacket[4] & (1 << i));
					evdev_push_key(sc->evdev_a,
					    BTN_0 + i * 2 + 1,
					    pb->ipacket[5] & (1 << i));
				}
			}
#endif
			pb->ipacket[4] &= ~(mask);
			pb->ipacket[5] &= ~(mask);
		} else	if (!sc->syninfo.directional_scrolls &&
		    !sc->gesture.in_vscroll) {
			/*
			 * Keep reporting MOUSE DOWN until we get a new packet
			 * indicating otherwise.
			 */
			extended_buttons |= sc->extended_buttons;
		}
	}

	if (sc->synhw.capReportsV && nfingers > 1)
		f[0] = (finger_t) {
			.x = ((pb->ipacket[3] & 0x10) << 8) |
			    ((pb->ipacket[1] & 0x0f) << 8) |
			    (pb->ipacket[4] & 0xfd),
			.y = ((pb->ipacket[3] & 0x20) << 7) |
			    ((pb->ipacket[1] & 0xf0) << 4) |
			    (pb->ipacket[5] & 0xfd),
			.p = *z & 0xfe,
			.w = (((pb->ipacket[2] & 0x01) << 2) |
			    (pb->ipacket[5] & 0x02) |
			    ((pb->ipacket[4] & 0x02) >> 1)) + 8,
			.flags = PSM_FINGER_FUZZY,
		};
	else
		f[0] = (finger_t) {
			.x = ((pb->ipacket[3] & 0x10) << 8) |
			    ((pb->ipacket[1] & 0x0f) << 8) |
			    pb->ipacket[4],
			.y = ((pb->ipacket[3] & 0x20) << 7) |
			    ((pb->ipacket[1] & 0xf0) << 4) |
			    pb->ipacket[5],
			.p = *z,
			.w = w,
			.flags = nfingers > 1 ? PSM_FINGER_FUZZY : 0,
		};

	/* Ignore hovering and unmeasurable touches */
	if (f[0].p < sc->syninfo.min_pressure || f[0].x < 2)
		nfingers = 0;

	/* Handle ClickPad */
	if (sc->synhw.capClickPad) {
		clickpad_pressed = (pb->ipacket[0] ^ pb->ipacket[3]) & 0x01;
		if (sc->synhw.forcePad) {
			/*
			 * Forcepads erroneously report button click if there
			 * are 2 or more fingers on the touchpad breaking
			 * multifinger gestures. To workaround this start
			 * reporting a click only after 4 consecutive single
			 * touch packets has been received.
			 * Skip these packets in case more contacts appear.
			 */
			switch (nfingers) {
			case 0:
				sc->fpcount = 0;
				break;
			case 1:
				if (clickpad_pressed && sc->fpcount < INT_MAX)
					++sc->fpcount;
				/* FALLTHROUGH */
			default:
				if (!clickpad_pressed)
					sc->fpcount = 0;
				if (sc->fpcount >= sc->syninfo.window_min)
					touchpad_buttons |= MOUSE_BUTTON1DOWN;
			}
		} else if (clickpad_pressed)
			touchpad_buttons |= MOUSE_BUTTON1DOWN;
	}

	for (id = 0; id < PSM_FINGERS; id++)
		if (id >= nfingers)
			PSM_FINGER_RESET(f[id]);

#ifdef EVDEV_SUPPORT
	if (evdev_rcpt_mask & EVDEV_RCPT_HW_MOUSE) {
		for (id = 0; id < PSM_FINGERS; id++) {
			if (PSM_FINGER_IS_SET(f[id]))
				psm_push_mt_finger(sc, id, &f[id]);
			else
				psm_release_mt_slot(sc->evdev_a, id);
		}
		evdev_push_key(sc->evdev_a, BTN_TOUCH, nfingers > 0);
		evdev_push_nfingers(sc->evdev_a, nfingers);
		if (nfingers > 0)
			psm_push_st_finger(sc, &f[0]);
		else
			evdev_push_abs(sc->evdev_a, ABS_PRESSURE, 0);
		evdev_push_mouse_btn(sc->evdev_a, touchpad_buttons);
		if (sc->synhw.capExtended && sc->synhw.capFourButtons) {
			evdev_push_key(sc->evdev_a, BTN_FORWARD,
			    touchpad_buttons & MOUSE_BUTTON4DOWN);
			evdev_push_key(sc->evdev_a, BTN_BACK,
			    touchpad_buttons & MOUSE_BUTTON5DOWN);
		}
		evdev_sync(sc->evdev_a);
	}
#endif

	ms->button = touchpad_buttons;

	psmgestures(sc, &f[0], nfingers, ms);
	for (id = 0; id < PSM_FINGERS; id++)
		psmsmoother(sc, &f[id], id, ms, x, y);

	/* Palm detection doesn't terminate the current action. */
	if (psmpalmdetect(sc, &f[0], nfingers)) {
		*x = *y = *z = 0;
		ms->button = ms->obutton;
		return (0);
	}

	ms->button |= extended_buttons | guest_buttons;

SYNAPTICS_END:
	/*
	 * Use the extra buttons as a scrollwheel
	 *
	 * XXX X.Org uses the Z axis for vertical wheel only,
	 * whereas moused(8) understands special values to differ
	 * vertical and horizontal wheels.
	 *
	 * xf86-input-mouse needs therefore a small patch to
	 * understand these special values. Without it, the
	 * horizontal wheel acts as a vertical wheel in X.Org.
	 *
	 * That's why the horizontal wheel is disabled by
	 * default for now.
	 */
	if (ms->button & MOUSE_BUTTON4DOWN)
		*z = -1;
	else if (ms->button & MOUSE_BUTTON5DOWN)
		*z = 1;
	else if (ms->button & MOUSE_BUTTON6DOWN)
		*z = -2;
	else if (ms->button & MOUSE_BUTTON7DOWN)
		*z = 2;
	else
		*z = 0;
	ms->button &= ~(MOUSE_BUTTON4DOWN | MOUSE_BUTTON5DOWN |
	    MOUSE_BUTTON6DOWN | MOUSE_BUTTON7DOWN);

	return (0);
}

static int
proc_synaptics_mux(struct psm_softc *sc, packetbuf_t *pb)
{
	int butt;

	/*
	 * Convert 3-byte interleaved mixture of Synaptics and generic mouse
	 * packets into plain 6-byte Synaptics packet protocol.
	 * While in hidden multiplexing mode KBC does some editing of the
	 * packet stream. It remembers the button bits from the last packet
	 * received from each device, and replaces the button bits of every
	 * packet with the logical OR of all devices’ most recent button bits.
	 * This button crosstalk should be filtered out as Synaptics and
	 * generic mouse encode middle button presses in a different way.
	 */
	switch (pb->ipacket[0] & 0xc0) {
	case 0x80:	/* First 3 bytes of Synaptics packet */
		bcopy(pb->ipacket, sc->muxsave, 3);
		/* Compute middle mouse button supression timeout. */
		sc->muxmidtimeout.tv_sec  = 0;
		sc->muxmidtimeout.tv_usec = 50000;	/* ~2-3 ints */
		timevaladd(&sc->muxmidtimeout, &sc->lastsoftintr);
		return (1);

	case 0xc0:	/* Second 3 bytes of Synaptics packet */
		/* Join two 3-bytes absolute packets */
		bcopy(pb->ipacket, pb->ipacket + 3, 3);
		bcopy(sc->muxsave, pb->ipacket, 3);
		/* Prefer trackpoint buttons over touchpad's */
		pb->ipacket[0] &= ~(0x08 | sc->muxmsbuttons);
		pb->ipacket[3] &= ~(0x08 | sc->muxmsbuttons);
		butt = (pb->ipacket[3] & 0x03) << 2 | (pb->ipacket[0] & 0x03);
		/* Add hysteresis to remove spurious middle button events */
		if (butt != sc->muxtpbuttons && sc->fpcount < 1) {
			pb->ipacket[0] &= 0xfc;
			pb->ipacket[0] |= sc->muxtpbuttons & 0x03;
			pb->ipacket[3] &= 0xfc;
			pb->ipacket[3] |= sc->muxtpbuttons >> 2 & 0x03;
			++sc->fpcount;
		} else {
			sc->fpcount = 0;
			sc->muxtpbuttons = butt;
		}
		/* Filter out impossible w induced by middle trackpoint btn */
		if (sc->synhw.capExtended && !sc->synhw.capPassthrough &&
		    (pb->ipacket[0] & 0x34) == 0x04 &&
		    (pb->ipacket[3] & 0x04) == 0x04) {
			pb->ipacket[0] &= 0xfb;
			pb->ipacket[3] &= 0xfb;
		}
		sc->muxsave[0] &= 0x30;
		break;

	default:	/* Generic mouse (Trackpoint) packet */
		/* Filter out middle button events induced by some w values */
		if (sc->muxmsbuttons & 0x03 || pb->ipacket[0] & 0x03 ||
		    (timevalcmp(&sc->lastsoftintr, &sc->muxmidtimeout, <=) &&
		     (sc->muxsave[0] & 0x30 || sc->muxsave[2] > 8)))
			pb->ipacket[0] &= 0xfb;
		sc->muxmsbuttons = pb->ipacket[0] & 0x07;
		/* Convert to Synaptics pass-through protocol */
		pb->ipacket[4] = pb->ipacket[1];
		pb->ipacket[5] = pb->ipacket[2];
		pb->ipacket[1] = pb->ipacket[0];
		pb->ipacket[2] = 0;
		pb->ipacket[0] = 0x84 | (sc->muxtpbuttons & 0x03);
		pb->ipacket[3] = 0xc4 | (sc->muxtpbuttons >> 2 & 0x03);
	}

	VLOG(4, (LOG_DEBUG, "synaptics: %02x %02x %02x %02x %02x %02x\n",
	    pb->ipacket[0], pb->ipacket[1], pb->ipacket[2],
	    pb->ipacket[3], pb->ipacket[4], pb->ipacket[5]));

	pb->inputbytes = MOUSE_SYNAPTICS_PACKETSIZE;
	return (0);
}

static int
psmpalmdetect(struct psm_softc *sc, finger_t *f, int nfingers)
{
	if (!(
	    ((sc->synhw.capMultiFinger || sc->synhw.capAdvancedGestures) &&
	      !sc->synhw.capReportsV && nfingers > 1) ||
	    (sc->synhw.capReportsV && nfingers > 2) ||
	    (sc->synhw.capPalmDetect && f->w <= sc->syninfo.max_width) ||
	    (!sc->synhw.capPalmDetect && f->p <= sc->syninfo.max_pressure) ||
	    (sc->synhw.capPen && f->flags & PSM_FINGER_IS_PEN))) {
		/*
		 * We consider the packet irrelevant for the current
		 * action when:
		 *  - the width isn't comprised in:
		 *    [1; max_width]
		 *  - the pressure isn't comprised in:
		 *    [min_pressure; max_pressure]
		 *  - pen aren't supported but PSM_FINGER_IS_PEN is set
		 */
		VLOG(2, (LOG_DEBUG, "synaptics: palm detected! (%d)\n", f->w));
		return (1);
	}
	return (0);
}

static void
psmgestures(struct psm_softc *sc, finger_t *fingers, int nfingers,
    mousestatus_t *ms)
{
	smoother_t *smoother;
	gesture_t *gest;
	finger_t *f;
	int y_ok, center_button, center_x, right_button, right_x, i;

	f = &fingers[0];
	smoother = &sc->smoother[0];
	gest = &sc->gesture;

	/* Find first active finger. */
	if (nfingers > 0) {
		for (i = 0; i < PSM_FINGERS; i++) {
			if (PSM_FINGER_IS_SET(fingers[i])) {
				f = &fingers[i];
				smoother = &sc->smoother[i];
				break;
			}
		}
	}

	/*
	 * Check pressure to detect a real wanted action on the
	 * touchpad.
	 */
	if (f->p >= sc->syninfo.min_pressure) {
		int x0, y0;
		int dxp, dyp;
		int start_x, start_y;
		int queue_len;
		int margin_top, margin_right, margin_bottom, margin_left;
		int window_min, window_max;
		int vscroll_hor_area, vscroll_ver_area;
		int two_finger_scroll;
		int max_x, max_y;

		/* Read sysctl. */
		/* XXX Verify values? */
		margin_top = sc->syninfo.margin_top;
		margin_right = sc->syninfo.margin_right;
		margin_bottom = sc->syninfo.margin_bottom;
		margin_left = sc->syninfo.margin_left;
		window_min = sc->syninfo.window_min;
		window_max = sc->syninfo.window_max;
		vscroll_hor_area = sc->syninfo.vscroll_hor_area;
		vscroll_ver_area = sc->syninfo.vscroll_ver_area;
		two_finger_scroll = sc->syninfo.two_finger_scroll;
		max_x = sc->syninfo.max_x;
		max_y = sc->syninfo.max_y;

		/* Read current absolute position. */
		x0 = f->x;
		y0 = f->y;

		/*
		 * Limit the coordinates to the specified margins because
		 * this area isn't very reliable.
		 */
		if (x0 <= margin_left)
			x0 = margin_left;
		else if (x0 >= max_x - margin_right)
			x0 = max_x - margin_right;
		if (y0 <= margin_bottom)
			y0 = margin_bottom;
		else if (y0 >= max_y - margin_top)
			y0 = max_y - margin_top;

		VLOG(3, (LOG_DEBUG, "synaptics: ipacket: [%d, %d], %d, %d\n",
		    x0, y0, f->p, f->w));

		/*
		 * If the action is just beginning, init the structure and
		 * compute tap timeout.
		 */
		if (!(sc->flags & PSM_FLAGS_FINGERDOWN)) {
			VLOG(3, (LOG_DEBUG, "synaptics: ----\n"));

			/* Initialize queue. */
			gest->window_min = window_min;

			/* Reset pressure peak. */
			gest->zmax = 0;

			/* Reset fingers count. */
			gest->fingers_nb = 0;

			/* Reset virtual scrolling state. */
			gest->in_vscroll = 0;

			/* Compute tap timeout. */
			gest->taptimeout.tv_sec  = tap_timeout / 1000000;
			gest->taptimeout.tv_usec = tap_timeout % 1000000;
			timevaladd(&gest->taptimeout, &sc->lastsoftintr);

			sc->flags |= PSM_FLAGS_FINGERDOWN;

			/* Smoother has not been reset yet */
			queue_len = 1;
			start_x = x0;
			start_y = y0;
		} else {
			queue_len = smoother->queue_len + 1;
			start_x = smoother->start_x;
			start_y = smoother->start_y;
		}

		/* Process ClickPad softbuttons */
		if (sc->synhw.capClickPad && ms->button & MOUSE_BUTTON1DOWN) {
			y_ok = sc->syninfo.softbuttons_y >= 0 ?
			    start_y < sc->syninfo.softbuttons_y :
			    start_y > max_y + sc->syninfo.softbuttons_y;

			center_button = MOUSE_BUTTON2DOWN;
			center_x = sc->syninfo.softbutton2_x;
			right_button = MOUSE_BUTTON3DOWN;
			right_x = sc->syninfo.softbutton3_x;

			if (center_x > 0 && right_x > 0 && center_x > right_x) {
				center_button = MOUSE_BUTTON3DOWN;
				center_x = sc->syninfo.softbutton3_x;
				right_button = MOUSE_BUTTON2DOWN;
				right_x = sc->syninfo.softbutton2_x;
			}

			if (right_x > 0 && start_x > right_x && y_ok)
				ms->button = (ms->button &
				    ~MOUSE_BUTTON1DOWN) | right_button;
			else if (center_x > 0 && start_x > center_x && y_ok)
				ms->button = (ms->button &
				    ~MOUSE_BUTTON1DOWN) | center_button;
		}

		/* If in tap-hold, add the recorded button. */
		if (gest->in_taphold)
			ms->button |= gest->tap_button;

		/*
		 * For tap, we keep the maximum number of fingers and the
		 * pressure peak. Also with multiple fingers, we increase
		 * the minimum window.
		 */
		if (nfingers > 1)
			gest->window_min = window_max;
		gest->fingers_nb = imax(nfingers, gest->fingers_nb);
		gest->zmax = imax(f->p, gest->zmax);

		/* Do we have enough packets to consider this a gesture? */
		if (queue_len < gest->window_min)
			return;

		dyp = -1;
		dxp = -1;

		/* Is a scrolling action occurring? */
		if (!gest->in_taphold && !ms->button &&
		    (!gest->in_vscroll || two_finger_scroll)) {
			/*
			 * A scrolling action must not conflict with a tap
			 * action. Here are the conditions to consider a
			 * scrolling action:
			 *  - the action in a configurable area
			 *  - one of the following:
			 *     . the distance between the last packet and the
			 *       first should be above a configurable minimum
			 *     . tap timed out
			 */
			dxp = abs(x0 - start_x);
			dyp = abs(y0 - start_y);

			if (timevalcmp(&sc->lastsoftintr, &gest->taptimeout, >) ||
			    dxp >= sc->syninfo.vscroll_min_delta ||
			    dyp >= sc->syninfo.vscroll_min_delta) {
				/*
				 * Handle two finger scrolling.
				 * Note that we don't rely on fingers_nb
				 * as that keeps the maximum number of fingers.
				 */
				if (two_finger_scroll) {
					if (nfingers == 2) {
						gest->in_vscroll +=
						    dyp ? 2 : 0;
						gest->in_vscroll +=
						    dxp ? 1 : 0;
					}
				} else {
					/* Check for horizontal scrolling. */
					if ((vscroll_hor_area > 0 &&
					    start_y <= vscroll_hor_area) ||
					    (vscroll_hor_area < 0 &&
					     start_y >=
					     max_y + vscroll_hor_area))
						gest->in_vscroll += 2;

					/* Check for vertical scrolling. */
					if ((vscroll_ver_area > 0 &&
					    start_x <= vscroll_ver_area) ||
					    (vscroll_ver_area < 0 &&
					     start_x >=
					     max_x + vscroll_ver_area))
						gest->in_vscroll += 1;
				}

				/* Avoid conflicts if area overlaps. */
				if (gest->in_vscroll >= 3)
					gest->in_vscroll =
					    (dxp > dyp) ? 2 : 1;
			}
		}
		/*
		 * Reset two finger scrolling when the number of fingers
		 * is different from two or any button is pressed.
		 */
		if (two_finger_scroll && gest->in_vscroll != 0 &&
		    (nfingers != 2 || ms->button))
			gest->in_vscroll = 0;

		VLOG(5, (LOG_DEBUG,
			"synaptics: virtual scrolling: %s "
			"(direction=%d, dxp=%d, dyp=%d, fingers=%d)\n",
			gest->in_vscroll ? "YES" : "NO",
			gest->in_vscroll, dxp, dyp,
			gest->fingers_nb));

	} else if (sc->flags & PSM_FLAGS_FINGERDOWN) {
		/*
		 * An action is currently taking place but the pressure
		 * dropped under the minimum, putting an end to it.
		 */
		int taphold_timeout, dx, dy, tap_max_delta;

		dx = abs(smoother->queue[smoother->queue_cursor].x -
		    smoother->start_x);
		dy = abs(smoother->queue[smoother->queue_cursor].y -
		    smoother->start_y);

		/* Max delta is disabled for multi-fingers tap. */
		if (gest->fingers_nb > 1)
			tap_max_delta = imax(dx, dy);
		else
			tap_max_delta = sc->syninfo.tap_max_delta;

		sc->flags &= ~PSM_FLAGS_FINGERDOWN;

		/* Check for tap. */
		VLOG(3, (LOG_DEBUG,
		    "synaptics: zmax=%d, dx=%d, dy=%d, "
		    "delta=%d, fingers=%d, queue=%d\n",
		    gest->zmax, dx, dy, tap_max_delta, gest->fingers_nb,
		    smoother->queue_len));
		if (!gest->in_vscroll && gest->zmax >= tap_threshold &&
		    timevalcmp(&sc->lastsoftintr, &gest->taptimeout, <=) &&
		    dx <= tap_max_delta && dy <= tap_max_delta &&
		    smoother->queue_len >= sc->syninfo.tap_min_queue) {
			/*
			 * We have a tap if:
			 *   - the maximum pressure went over tap_threshold
			 *   - the action ended before tap_timeout
			 *
			 * To handle tap-hold, we must delay any button push to
			 * the next action.
			 */
			if (gest->in_taphold) {
				/*
				 * This is the second and last tap of a
				 * double tap action, not a tap-hold.
				 */
				gest->in_taphold = 0;

				/*
				 * For double-tap to work:
				 *   - no button press is emitted (to
				 *     simulate a button release)
				 *   - PSM_FLAGS_FINGERDOWN is set to
				 *     force the next packet to emit a
				 *     button press)
				 */
				VLOG(2, (LOG_DEBUG,
				    "synaptics: button RELEASE: %d\n",
				    gest->tap_button));
				sc->flags |= PSM_FLAGS_FINGERDOWN;

				/* Schedule button press on next interrupt */
				sc->idletimeout.tv_sec  = psmhz > 1 ?
				    0 : 1;
				sc->idletimeout.tv_usec = psmhz > 1 ?
				    1000000 / psmhz : 0;
			} else {
				/*
				 * This is the first tap: we set the
				 * tap-hold state and notify the button
				 * down event.
				 */
				gest->in_taphold = 1;
				taphold_timeout = sc->syninfo.taphold_timeout;
				gest->taptimeout.tv_sec  = taphold_timeout /
				    1000000;
				gest->taptimeout.tv_usec = taphold_timeout %
				    1000000;
				sc->idletimeout = gest->taptimeout;
				timevaladd(&gest->taptimeout,
				    &sc->lastsoftintr);

				switch (gest->fingers_nb) {
				case 3:
					gest->tap_button =
					    MOUSE_BUTTON2DOWN;
					break;
				case 2:
					gest->tap_button =
					    MOUSE_BUTTON3DOWN;
					break;
				default:
					gest->tap_button =
					    MOUSE_BUTTON1DOWN;
				}
				VLOG(2, (LOG_DEBUG,
				    "synaptics: button PRESS: %d\n",
				    gest->tap_button));
				ms->button |= gest->tap_button;
			}
		} else {
			/*
			 * Not enough pressure or timeout: reset
			 * tap-hold state.
			 */
			if (gest->in_taphold) {
				VLOG(2, (LOG_DEBUG,
				    "synaptics: button RELEASE: %d\n",
				    gest->tap_button));
				gest->in_taphold = 0;
			} else {
				VLOG(2, (LOG_DEBUG,
				    "synaptics: not a tap-hold\n"));
			}
		}
	} else if (!(sc->flags & PSM_FLAGS_FINGERDOWN) && gest->in_taphold) {
		/*
		 * For a tap-hold to work, the button must remain down at
		 * least until timeout (where the in_taphold flags will be
		 * cleared) or during the next action.
		 */
		if (timevalcmp(&sc->lastsoftintr, &gest->taptimeout, <=)) {
			ms->button |= gest->tap_button;
		} else {
			VLOG(2, (LOG_DEBUG, "synaptics: button RELEASE: %d\n",
			    gest->tap_button));
			gest->in_taphold = 0;
		}
	}

	return;
}

static void
psmsmoother(struct psm_softc *sc, finger_t *f, int smoother_id,
    mousestatus_t *ms, int *x, int *y)
{
	smoother_t *smoother = &sc->smoother[smoother_id];
	gesture_t *gest = &(sc->gesture);

	/*
	 * Check pressure to detect a real wanted action on the
	 * touchpad.
	 */
	if (f->p >= sc->syninfo.min_pressure) {
		int x0, y0;
		int cursor, peer, window;
		int dx, dy, dxp, dyp;
		int max_width, max_pressure;
		int margin_top, margin_right, margin_bottom, margin_left;
		int na_top, na_right, na_bottom, na_left;
		int window_min, window_max;
		int multiplicator;
		int weight_current, weight_previous, weight_len_squared;
		int div_min, div_max, div_len;
		int vscroll_hor_area, vscroll_ver_area;
		int two_finger_scroll;
		int max_x, max_y;
		int len, weight_prev_x, weight_prev_y;
		int div_max_x, div_max_y, div_x, div_y;
		int is_fuzzy;

		/* Read sysctl. */
		/* XXX Verify values? */
		max_width = sc->syninfo.max_width;
		max_pressure = sc->syninfo.max_pressure;
		margin_top = sc->syninfo.margin_top;
		margin_right = sc->syninfo.margin_right;
		margin_bottom = sc->syninfo.margin_bottom;
		margin_left = sc->syninfo.margin_left;
		na_top = sc->syninfo.na_top;
		na_right = sc->syninfo.na_right;
		na_bottom = sc->syninfo.na_bottom;
		na_left = sc->syninfo.na_left;
		window_min = sc->syninfo.window_min;
		window_max = sc->syninfo.window_max;
		multiplicator = sc->syninfo.multiplicator;
		weight_current = sc->syninfo.weight_current;
		weight_previous = sc->syninfo.weight_previous;
		weight_len_squared = sc->syninfo.weight_len_squared;
		div_min = sc->syninfo.div_min;
		div_max = sc->syninfo.div_max;
		div_len = sc->syninfo.div_len;
		vscroll_hor_area = sc->syninfo.vscroll_hor_area;
		vscroll_ver_area = sc->syninfo.vscroll_ver_area;
		two_finger_scroll = sc->syninfo.two_finger_scroll;
		max_x = sc->syninfo.max_x;
		max_y = sc->syninfo.max_y;

		is_fuzzy = (f->flags & PSM_FINGER_FUZZY) != 0;

		/* Read current absolute position. */
		x0 = f->x;
		y0 = f->y;

		/*
		 * Limit the coordinates to the specified margins because
		 * this area isn't very reliable.
		 */
		if (x0 <= margin_left)
			x0 = margin_left;
		else if (x0 >= max_x - margin_right)
			x0 = max_x - margin_right;
		if (y0 <= margin_bottom)
			y0 = margin_bottom;
		else if (y0 >= max_y - margin_top)
			y0 = max_y - margin_top;

		/* If the action is just beginning, init the structure. */
		if (smoother->active == 0) {
			VLOG(3, (LOG_DEBUG, "smoother%d: ---\n", smoother_id));

			/* Store the first point of this action. */
			smoother->start_x = x0;
			smoother->start_y = y0;
			dx = dy = 0;

			/* Initialize queue. */
			smoother->queue_cursor = SYNAPTICS_PACKETQUEUE;
			smoother->queue_len = 0;

			/* Reset average. */
			smoother->avg_dx = 0;
			smoother->avg_dy = 0;

			/* Reset squelch. */
			smoother->squelch_x = 0;
			smoother->squelch_y = 0;

			/* Activate queue */
			smoother->active = 1;
		} else {
			/* Calculate the current delta. */
			cursor = smoother->queue_cursor;
			dx = x0 - smoother->queue[cursor].x;
			dy = y0 - smoother->queue[cursor].y;
		}

		VLOG(3, (LOG_DEBUG, "smoother%d: ipacket: [%d, %d], %d, %d\n",
		    smoother_id, x0, y0, f->p, f->w));

		/* Queue this new packet. */
		cursor = SYNAPTICS_QUEUE_CURSOR(smoother->queue_cursor - 1);
		smoother->queue[cursor].x = x0;
		smoother->queue[cursor].y = y0;
		smoother->queue_cursor = cursor;
		if (smoother->queue_len < SYNAPTICS_PACKETQUEUE)
			smoother->queue_len++;
		VLOG(5, (LOG_DEBUG,
		    "smoother%d: cursor[%d]: x=%d, y=%d, dx=%d, dy=%d\n",
		    smoother_id, cursor, x0, y0, dx, dy));

		/* Do we have enough packets to consider this a movement? */
		if (smoother->queue_len < gest->window_min)
			return;

		weight_prev_x = weight_prev_y = weight_previous;
		div_max_x = div_max_y = div_max;

		if (gest->in_vscroll) {
			/* Dividers are different with virtual scrolling. */
			div_min = sc->syninfo.vscroll_div_min;
			div_max_x = div_max_y = sc->syninfo.vscroll_div_max;
		} else {
			/*
			 * There's a lot of noise in coordinates when
			 * the finger is on the touchpad's borders. When
			 * using this area, we apply a special weight and
			 * div.
			 */
			if (x0 <= na_left || x0 >= max_x - na_right) {
				weight_prev_x = sc->syninfo.weight_previous_na;
				div_max_x = sc->syninfo.div_max_na;
			}

			if (y0 <= na_bottom || y0 >= max_y - na_top) {
				weight_prev_y = sc->syninfo.weight_previous_na;
				div_max_y = sc->syninfo.div_max_na;
			}
		}

		/*
		 * Calculate weights for the average operands and
		 * the divisor. Both depend on the distance between
		 * the current packet and a previous one (based on the
		 * window width).
		 */
		window = imin(smoother->queue_len, window_max);
		peer = SYNAPTICS_QUEUE_CURSOR(cursor + window - 1);
		dxp = abs(x0 - smoother->queue[peer].x) + 1;
		dyp = abs(y0 - smoother->queue[peer].y) + 1;
		len = (dxp * dxp) + (dyp * dyp);
		weight_prev_x = imin(weight_prev_x,
		    weight_len_squared * weight_prev_x / len);
		weight_prev_y = imin(weight_prev_y,
		    weight_len_squared * weight_prev_y / len);

		len = (dxp + dyp) / 2;
		div_x = div_len * div_max_x / len;
		div_x = imin(div_max_x, div_x);
		div_x = imax(div_min, div_x);
		div_y = div_len * div_max_y / len;
		div_y = imin(div_max_y, div_y);
		div_y = imax(div_min, div_y);

		VLOG(3, (LOG_DEBUG,
		    "smoother%d: peer=%d, len=%d, weight=%d/%d, div=%d/%d\n",
		    smoother_id, peer, len, weight_prev_x, weight_prev_y,
		    div_x, div_y));

		/* Compute averages. */
		smoother->avg_dx =
		    (weight_current * dx * multiplicator +
		     weight_prev_x * smoother->avg_dx) /
		    (weight_current + weight_prev_x);

		smoother->avg_dy =
		    (weight_current * dy * multiplicator +
		     weight_prev_y * smoother->avg_dy) /
		    (weight_current + weight_prev_y);

		VLOG(5, (LOG_DEBUG,
		    "smoother%d: avg_dx~=%d, avg_dy~=%d\n", smoother_id,
		    smoother->avg_dx / multiplicator,
		    smoother->avg_dy / multiplicator));

		/* Use these averages to calculate x & y. */
		smoother->squelch_x += smoother->avg_dx;
		dxp = smoother->squelch_x / (div_x * multiplicator);
		smoother->squelch_x = smoother->squelch_x %
		    (div_x * multiplicator);

		smoother->squelch_y += smoother->avg_dy;
		dyp = smoother->squelch_y / (div_y * multiplicator);
		smoother->squelch_y = smoother->squelch_y %
		    (div_y * multiplicator);

		switch(gest->in_vscroll) {
		case 0: /* Pointer movement. */
			/* On real<->fuzzy finger switch the x/y pos jumps */
			if (is_fuzzy == smoother->is_fuzzy) {
				*x += dxp;
				*y += dyp;
			}

			VLOG(3, (LOG_DEBUG, "smoother%d: [%d, %d] -> [%d, %d]\n",
			    smoother_id, dx, dy, dxp, dyp));
			break;
		case 1: /* Vertical scrolling. */
			if (dyp != 0)
				ms->button |= (dyp > 0) ?
				    MOUSE_BUTTON4DOWN : MOUSE_BUTTON5DOWN;
			break;
		case 2: /* Horizontal scrolling. */
			if (dxp != 0)
				ms->button |= (dxp > 0) ?
				    MOUSE_BUTTON7DOWN : MOUSE_BUTTON6DOWN;
			break;
		}

		smoother->is_fuzzy = is_fuzzy;

	} else {
		/*
		 * Deactivate queue. Note: We can not just reset queue here
		 * as these values are still used by gesture processor.
		 * So postpone reset till next touch.
		 */
		smoother->active = 0;
	}
}

static int
proc_elantech(struct psm_softc *sc, packetbuf_t *pb, mousestatus_t *ms,
    int *x, int *y, int *z)
{
	static int touchpad_button, trackpoint_button;
	finger_t fn, f[ELANTECH_MAX_FINGERS];
	int pkt, id, scale, i, nfingers, mask;

	if (!elantech_support)
		return (0);

	/* Determine packet format and do a sanity check for out of sync packets. */
	if (ELANTECH_PKT_IS_DEBOUNCE(pb, sc->elanhw.hwversion))
		pkt = ELANTECH_PKT_NOP;
	else if (sc->elanhw.hastrackpoint && ELANTECH_PKT_IS_TRACKPOINT(pb))
		pkt = ELANTECH_PKT_TRACKPOINT;
	else
	switch (sc->elanhw.hwversion) {
	case 2:
		if (!ELANTECH_PKT_IS_V2(pb))
			return (-1);

		pkt = (pb->ipacket[0] & 0xc0) == 0x80 ?
		    ELANTECH_PKT_V2_2FINGER : ELANTECH_PKT_V2_COMMON;
		break;
	case 3:
		if (!ELANTECH_PKT_IS_V3_HEAD(pb, sc->elanhw.hascrc) &&
		    !ELANTECH_PKT_IS_V3_TAIL(pb, sc->elanhw.hascrc))
			return (-1);

		pkt = ELANTECH_PKT_V3;
		break;
	case 4:
		if (!ELANTECH_PKT_IS_V4(pb, sc->elanhw.hascrc))
			return (-1);

		switch (pb->ipacket[3] & 0x03) {
		case 0x00:
			pkt = ELANTECH_PKT_V4_STATUS;
			break;
		case 0x01:
			pkt = ELANTECH_PKT_V4_HEAD;
			break;
		case 0x02:
			pkt = ELANTECH_PKT_V4_MOTION;
			break;
		default:
			return (-1);
		}
		break;
	default:
		return (-1);
	}

	VLOG(5, (LOG_DEBUG, "elantech: ipacket format: %d\n", pkt));

	for (id = 0; id < ELANTECH_MAX_FINGERS; id++)
		PSM_FINGER_RESET(f[id]);

	*x = *y = *z = 0;
	ms->button = ms->obutton;

	if (sc->syninfo.touchpad_off)
		return (0);

	/* Common legend
	 * L: Left mouse button pressed
	 * R: Right mouse button pressed
	 * N: number of fingers on touchpad
	 * X: absolute x value (horizontal)
	 * Y: absolute y value (vertical)
	 * W; width of the finger touch
	 * P: pressure
	 */
	switch (pkt) {
	case ELANTECH_PKT_V2_COMMON:	/* HW V2. One/Three finger touch */
		/*               7   6   5   4   3   2   1   0 (LSB)
		 * -------------------------------------------
		 * ipacket[0]:  N1  N0  W3  W2   .   .   R   L
		 * ipacket[1]:  P7  P6  P5  P4 X11 X10  X9  X8
		 * ipacket[2]:  X7  X6  X5  X4  X3  X2  X1  X0
		 * ipacket[3]:  N4  VF  W1  W0   .   .   .  B2
		 * ipacket[4]:  P3  P1  P2  P0 Y11 Y10  Y9  Y8
		 * ipacket[5]:  Y7  Y6  Y5  Y4  Y3  Y2  Y1  Y0
		 * -------------------------------------------
		 * N4: set if more than 3 fingers (only in 3 fingers mode)
		 * VF: a kind of flag? (only on EF123, 0 when finger
		 *     is over one of the buttons, 1 otherwise)
		 * B2: (on EF113 only, 0 otherwise), one button pressed
		 * P & W is not reported on EF113 touchpads
		 */
		nfingers = (pb->ipacket[0] & 0xc0) >> 6;
		if (nfingers == 3 && (pb->ipacket[3] & 0x80))
			nfingers = 4;

		if (nfingers == 0) {
			mask = (1 << nfingers) - 1;	/* = 0x00 */
			break;
		}

		/* Map 3-rd and 4-th fingers to first finger */
		mask = (1 << 1) - 1;	/* = 0x01 */
		f[0] = ELANTECH_FINGER_SET_XYP(pb);
		if (sc->elanhw.haspressure) {
			f[0].w = ((pb->ipacket[0] & 0x30) >> 2) |
			    ((pb->ipacket[3] & 0x30) >> 4);
		} else {
			f[0].p = PSM_FINGER_DEFAULT_P;
			f[0].w = PSM_FINGER_DEFAULT_W;
		}

		/*
		 * HW v2 dont report exact finger positions when 3 or more
		 * fingers are on touchpad.
		 */
		if (nfingers > 2)
			f[0].flags = PSM_FINGER_FUZZY;

		break;

	case ELANTECH_PKT_V2_2FINGER:	/*HW V2. Two finger touch */
		/*               7   6   5   4   3   2   1   0 (LSB)
		 * -------------------------------------------
		 * ipacket[0]:  N1  N0 AY8 AX8   .   .   R   L
		 * ipacket[1]: AX7 AX6 AX5 AX4 AX3 AX2 AX1 AX0
		 * ipacket[2]: AY7 AY6 AY5 AY4 AY3 AY2 AY1 AY0
		 * ipacket[3]:   .   . BY8 BX8   .   .   .   .
		 * ipacket[4]: BX7 BX6 BX5 BX4 BX3 BX2 BX1 BX0
		 * ipacket[5]: BY7 BY6 BY5 BY4 BY3 BY2 BY1 BY0
		 * -------------------------------------------
		 * AX: lower-left finger absolute x value
		 * AY: lower-left finger absolute y value
		 * BX: upper-right finger absolute x value
		 * BY: upper-right finger absolute y value
		 */
		nfingers = 2;
		mask = (1 << nfingers) - 1;

		for (id = 0; id < imin(2, ELANTECH_MAX_FINGERS); id ++)
			f[id] = (finger_t) {
				.x = (((pb->ipacket[id * 3] & 0x10) << 4) |
				    pb->ipacket[id * 3 + 1]) << 2,
				.y = (((pb->ipacket[id * 3] & 0x20) << 3) |
				    pb->ipacket[id * 3 + 2]) << 2,
				.p = PSM_FINGER_DEFAULT_P,
				.w = PSM_FINGER_DEFAULT_W,
				/* HW ver.2 sends bounding box */
				.flags = PSM_FINGER_FUZZY
			};
		break;

	case ELANTECH_PKT_V3:	/* HW Version 3 */
		/*               7   6   5   4   3   2   1   0 (LSB)
		 * -------------------------------------------
		 * ipacket[0]:  N1  N0  W3  W2   0   1   R   L
		 * ipacket[1]:  P7  P6  P5  P4 X11 X10  X9  X8
		 * ipacket[2]:  X7  X6  X5  X4  X3  X2  X1  X0
		 * ipacket[3]:   0   0  W1  W0   0   0   1   0
		 * ipacket[4]:  P3  P1  P2  P0 Y11 Y10  Y9  Y8
		 * ipacket[5]:  Y7  Y6  Y5  Y4  Y3  Y2  Y1  Y0
		 * -------------------------------------------
		 */
		nfingers = (pb->ipacket[0] & 0xc0) >> 6;
		/* Map 3-rd finger to first finger */
		id = nfingers > 2 ? 0 : nfingers - 1;
		mask = (1 << (id + 1)) - 1;

		if (nfingers == 0)
			break;

		fn = ELANTECH_FINGER_SET_XYP(pb);
		fn.w = ((pb->ipacket[0] & 0x30) >> 2) |
		    ((pb->ipacket[3] & 0x30) >> 4);

		/*
		 * HW v3 dont report exact finger positions when 3 or more
		 * fingers are on touchpad.
		 */
		if (nfingers > 1)
			fn.flags = PSM_FINGER_FUZZY;

		if (nfingers == 2) {
			if (ELANTECH_PKT_IS_V3_HEAD(pb, sc->elanhw.hascrc)) {
				sc->elanaction.fingers[0] = fn;
				return (0);
			} else
				f[0] = sc->elanaction.fingers[0];
		}
		f[id] = fn;
		break;

	case ELANTECH_PKT_V4_STATUS:	/* HW Version 4. Status packet */
		/*               7   6   5   4   3   2   1   0 (LSB)
		 * -------------------------------------------
		 * ipacket[0]:   .   .   .   .   0   1   R   L
		 * ipacket[1]:   .   .   .  F4  F3  F2  F1  F0
		 * ipacket[2]:   .   .   .   .   .   .   .   .
		 * ipacket[3]:   .   .   .   1   0   0   0   0
		 * ipacket[4]:  PL   .   .   .   .   .   .   .
		 * ipacket[5]:   .   .   .   .   .   .   .   .
		 * -------------------------------------------
		 * Fn: finger n is on touchpad
		 * PL: palm
		 * HV ver4 sends a status packet to indicate that the numbers
		 * or identities of the fingers has been changed
		 */

		mask = pb->ipacket[1] & 0x1f;
		nfingers = bitcount(mask);

		if (sc->elanaction.mask_v4wait != 0)
			VLOG(3, (LOG_DEBUG, "elantech: HW v4 status packet"
			    " when not all previous head packets received\n"));

		/* Bitmap of fingers to receive before gesture processing */
		sc->elanaction.mask_v4wait = mask & ~sc->elanaction.mask;

		/* Skip "new finger is on touchpad" packets */
		if (sc->elanaction.mask_v4wait) {
			sc->elanaction.mask = mask;
			return (0);
		}

		break;

	case ELANTECH_PKT_V4_HEAD:	/* HW Version 4. Head packet */
		/*               7   6   5   4   3   2   1   0 (LSB)
		 * -------------------------------------------
		 * ipacket[0]:  W3  W2  W1  W0   0   1   R   L
		 * ipacket[1]:  P7  P6  P5  P4 X11 X10  X9  X8
		 * ipacket[2]:  X7  X6  X5  X4  X3  X2  X1  X0
		 * ipacket[3]: ID2 ID1 ID0   1   0   0   0   1
		 * ipacket[4]:  P3  P1  P2  P0 Y11 Y10  Y9  Y8
		 * ipacket[5]:  Y7  Y6  Y5  Y4  Y3  Y2  Y1  Y0
		 * -------------------------------------------
		 * ID: finger id
		 * HW ver 4 sends head packets in two cases:
		 * 1. One finger touch and movement.
		 * 2. Next after status packet to tell new finger positions.
		 */
		mask = sc->elanaction.mask;
		nfingers = bitcount(mask);
		id = ((pb->ipacket[3] & 0xe0) >> 5) - 1;
		fn = ELANTECH_FINGER_SET_XYP(pb);
		fn.w =(pb->ipacket[0] & 0xf0) >> 4;

		if (id < 0)
			return (0);

		/* Packet is finger position update. Report it */
		if (sc->elanaction.mask_v4wait == 0) {
			if (id < ELANTECH_MAX_FINGERS)
				f[id] = fn;
			break;
		}

		/* Remove finger from waiting bitmap and store into context */
		sc->elanaction.mask_v4wait &= ~(1 << id);
		if (id < ELANTECH_MAX_FINGERS)
			sc->elanaction.fingers[id] = fn;

		/* Wait for other fingers if needed */
		if (sc->elanaction.mask_v4wait != 0)
			return (0);

		/* All new fingers are received. Report them from context */
		for (id = 0; id < ELANTECH_MAX_FINGERS; id++)
			if (sc->elanaction.mask & (1 << id))
				f[id] =  sc->elanaction.fingers[id];

		break;

	case ELANTECH_PKT_V4_MOTION:	/* HW Version 4. Motion packet */
		/*               7   6   5   4   3   2   1   0 (LSB)
		 * -------------------------------------------
		 * ipacket[0]: ID2 ID1 ID0  OF   0   1   R   L
		 * ipacket[1]: DX7 DX6 DX5 DX4 DX3 DX2 DX1 DX0
		 * ipacket[2]: DY7 DY6 DY5 DY4 DY3 DY2 DY1 DY0
		 * ipacket[3]: ID2 ID1 ID0   1   0   0   1   0
		 * ipacket[4]: DX7 DX6 DX5 DX4 DX3 DX2 DX1 DX0
		 * ipacket[5]: DY7 DY6 DY5 DY4 DY3 DY2 DY1 DY0
		 * -------------------------------------------
		 * OF: delta overflows (> 127 or < -128), in this case
		 *     firmware sends us (delta x / 5) and (delta y / 5)
		 * ID: finger id
		 * DX: delta x (two's complement)
		 * XY: delta y (two's complement)
		 * byte 0 ~ 2 for one finger
		 * byte 3 ~ 5 for another finger
		 */
		mask = sc->elanaction.mask;
		nfingers = bitcount(mask);

		scale = (pb->ipacket[0] & 0x10) ? 5 : 1;
		for (i = 0; i <= 3; i += 3) {
			id = ((pb->ipacket[i] & 0xe0) >> 5) - 1;
			if (id < 0 || id >= ELANTECH_MAX_FINGERS)
				continue;

			if (PSM_FINGER_IS_SET(sc->elanaction.fingers[id])) {
				f[id] = sc->elanaction.fingers[id];
				f[id].x += imax(-f[id].x,
				    (signed char)pb->ipacket[i+1] * scale);
				f[id].y += imax(-f[id].y,
				    (signed char)pb->ipacket[i+2] * scale);
			} else {
				VLOG(3, (LOG_DEBUG, "elantech: "
				    "HW v4 motion packet skipped\n"));
			}
		}

		break;

	case ELANTECH_PKT_TRACKPOINT:
		/*               7   6   5   4   3   2   1   0 (LSB)
		 * -------------------------------------------
		 * ipacket[0]:   0   0  SX  SY   0   M   R   L
		 * ipacket[1]: ~SX   0   0   0   0   0   0   0
		 * ipacket[2]: ~SY   0   0   0   0   0   0   0
		 * ipacket[3]:   0   0 ~SY ~SX   0   1   1   0
		 * ipacket[4]:  X7  X6  X5  X4  X3  X2  X1  X0
		 * ipacket[5]:  Y7  Y6  Y5  Y4  Y3  Y2  Y1  Y0
		 * -------------------------------------------
		 * X and Y are written in two's complement spread
		 * over 9 bits with SX/SY the relative top bit and
		 * X7..X0 and Y7..Y0 the lower bits.
		 */
		*x = (pb->ipacket[0] & 0x20) ?
		    pb->ipacket[4] - 256 : pb->ipacket[4];
		*y = (pb->ipacket[0] & 0x10) ?
		    pb->ipacket[5] - 256 : pb->ipacket[5];

		trackpoint_button =
		    ((pb->ipacket[0] & 0x01) ? MOUSE_BUTTON1DOWN : 0) |
		    ((pb->ipacket[0] & 0x02) ? MOUSE_BUTTON3DOWN : 0) |
		    ((pb->ipacket[0] & 0x04) ? MOUSE_BUTTON2DOWN : 0);
#ifdef EVDEV_SUPPORT
		evdev_push_rel(sc->evdev_r, REL_X, *x);
		evdev_push_rel(sc->evdev_r, REL_Y, -*y);
		evdev_push_mouse_btn(sc->evdev_r, trackpoint_button);
		evdev_sync(sc->evdev_r);
#endif
		ms->button = touchpad_button | trackpoint_button;
		return (0);

	case ELANTECH_PKT_NOP:
		return (0);

	default:
		return (-1);
	}

	for (id = 0; id < ELANTECH_MAX_FINGERS; id++)
		if (PSM_FINGER_IS_SET(f[id]))
			VLOG(2, (LOG_DEBUG, "elantech: "
			    "finger %d: down [%d, %d], %d, %d, %d\n", id + 1,
			    f[id].x, f[id].y, f[id].p, f[id].w, f[id].flags));

	/* Touchpad button presses */
	if (sc->elanhw.isclickpad) {
		touchpad_button =
		    ((pb->ipacket[0] & 0x03) ? MOUSE_BUTTON1DOWN : 0);
	} else {
		touchpad_button =
		    ((pb->ipacket[0] & 0x01) ? MOUSE_BUTTON1DOWN : 0) |
		    ((pb->ipacket[0] & 0x02) ? MOUSE_BUTTON3DOWN : 0);
	}

#ifdef EVDEV_SUPPORT
	if (evdev_rcpt_mask & EVDEV_RCPT_HW_MOUSE) {
		for (id = 0; id < ELANTECH_MAX_FINGERS; id++) {
			if (PSM_FINGER_IS_SET(f[id])) {
				psm_push_mt_finger(sc, id, &f[id]);
				/* Convert touch width to surface units */
				evdev_push_abs(sc->evdev_a, ABS_MT_TOUCH_MAJOR,
				    f[id].w * sc->elanhw.dptracex);
			}
			if (sc->elanaction.mask & (1 << id) &&
			    !(mask & (1 << id)))
				psm_release_mt_slot(sc->evdev_a, id);
		}
		evdev_push_key(sc->evdev_a, BTN_TOUCH, nfingers > 0);
		evdev_push_nfingers(sc->evdev_a, nfingers);
		if (nfingers > 0) {
			if (PSM_FINGER_IS_SET(f[0]))
				psm_push_st_finger(sc, &f[0]);
		} else
			evdev_push_abs(sc->evdev_a, ABS_PRESSURE, 0);
		evdev_push_mouse_btn(sc->evdev_a, touchpad_button);
		evdev_sync(sc->evdev_a);
	}
#endif

	ms->button = touchpad_button | trackpoint_button;

	/* Send finger 1 position to gesture processor */
	if (PSM_FINGER_IS_SET(f[0]) || PSM_FINGER_IS_SET(f[1]) ||
	    nfingers == 0)
		psmgestures(sc, &f[0], imin(nfingers, 3), ms);
	/* Send fingers positions to movement smoothers */
	for (id = 0; id < PSM_FINGERS; id++)
		if (PSM_FINGER_IS_SET(f[id]) || !(mask & (1 << id)))
			psmsmoother(sc, &f[id], id, ms, x, y);

	/* Store current finger positions in action context */
	for (id = 0; id < ELANTECH_MAX_FINGERS; id++) {
		if (PSM_FINGER_IS_SET(f[id]))
			sc->elanaction.fingers[id] = f[id];
		if ((sc->elanaction.mask & (1 << id)) && !(mask & (1 << id)))
			PSM_FINGER_RESET(sc->elanaction.fingers[id]);
	}
	sc->elanaction.mask = mask;

	/* Palm detection doesn't terminate the current action. */
	if (psmpalmdetect(sc, &f[0], nfingers)) {
		*x = *y = *z = 0;
		ms->button = ms->obutton;
		return (0);
	}

	/* Use the extra buttons as a scrollwheel */
	if (ms->button & MOUSE_BUTTON4DOWN)
		*z = -1;
	else if (ms->button & MOUSE_BUTTON5DOWN)
		*z = 1;
	else if (ms->button & MOUSE_BUTTON6DOWN)
		*z = -2;
	else if (ms->button & MOUSE_BUTTON7DOWN)
		*z = 2;
	else
		*z = 0;
	ms->button &= ~(MOUSE_BUTTON4DOWN | MOUSE_BUTTON5DOWN |
	    MOUSE_BUTTON6DOWN | MOUSE_BUTTON7DOWN);

	return (0);
}

static void
proc_versapad(struct psm_softc *sc, packetbuf_t *pb, mousestatus_t *ms,
    int *x, int *y, int *z)
{
	static int butmap_versapad[8] = {
		0,
		MOUSE_BUTTON3DOWN,
		0,
		MOUSE_BUTTON3DOWN,
		MOUSE_BUTTON1DOWN,
		MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN,
		MOUSE_BUTTON1DOWN,
		MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN
	};
	int c, x0, y0;

	/* VersaPad PS/2 absolute mode message format
	 *
	 * [packet1]     7   6   5   4   3   2   1   0(LSB)
	 *  ipacket[0]:  1   1   0   A   1   L   T   R
	 *  ipacket[1]: H7  H6  H5  H4  H3  H2  H1  H0
	 *  ipacket[2]: V7  V6  V5  V4  V3  V2  V1  V0
	 *  ipacket[3]:  1   1   1   A   1   L   T   R
	 *  ipacket[4]:V11 V10  V9  V8 H11 H10  H9  H8
	 *  ipacket[5]:  0  P6  P5  P4  P3  P2  P1  P0
	 *
	 * [note]
	 *  R: right physical mouse button (1=on)
	 *  T: touch pad virtual button (1=tapping)
	 *  L: left physical mouse button (1=on)
	 *  A: position data is valid (1=valid)
	 *  H: horizontal data (12bit signed integer. H11 is sign bit.)
	 *  V: vertical data (12bit signed integer. V11 is sign bit.)
	 *  P: pressure data
	 *
	 * Tapping is mapped to MOUSE_BUTTON4.
	 */
	c = pb->ipacket[0];
	*x = *y = 0;
	ms->button = butmap_versapad[c & MOUSE_PS2VERSA_BUTTONS];
	ms->button |= (c & MOUSE_PS2VERSA_TAP) ? MOUSE_BUTTON4DOWN : 0;
	if (c & MOUSE_PS2VERSA_IN_USE) {
		x0 = pb->ipacket[1] | (((pb->ipacket[4]) & 0x0f) << 8);
		y0 = pb->ipacket[2] | (((pb->ipacket[4]) & 0xf0) << 4);
		if (x0 & 0x800)
			x0 -= 0x1000;
		if (y0 & 0x800)
			y0 -= 0x1000;
		if (sc->flags & PSM_FLAGS_FINGERDOWN) {
			*x = sc->xold - x0;
			*y = y0 - sc->yold;
			if (*x < 0)	/* XXX */
				++*x;
			else if (*x)
				--*x;
			if (*y < 0)
				++*y;
			else if (*y)
				--*y;
		} else
			sc->flags |= PSM_FLAGS_FINGERDOWN;
		sc->xold = x0;
		sc->yold = y0;
	} else
		sc->flags &= ~PSM_FLAGS_FINGERDOWN;
}

static void
psmsoftintridle(void *arg)
{
	struct psm_softc *sc = arg;
	packetbuf_t *pb;

	/* Invoke soft handler only when pqueue is empty. Otherwise it will be
	 * invoked from psmintr soon with pqueue filled with real data */
	if (sc->pqueue_start == sc->pqueue_end &&
	    sc->idlepacket.inputbytes > 0) {
		/* Grow circular queue backwards to avoid race with psmintr */
		if (--sc->pqueue_start < 0)
			sc->pqueue_start = PSM_PACKETQUEUE - 1;

		pb = &sc->pqueue[sc->pqueue_start];
		memcpy(pb, &sc->idlepacket, sizeof(packetbuf_t));
		VLOG(4, (LOG_DEBUG,
		    "psmsoftintridle: %02x %02x %02x %02x %02x %02x\n",
		    pb->ipacket[0], pb->ipacket[1], pb->ipacket[2],
		    pb->ipacket[3], pb->ipacket[4], pb->ipacket[5]));

		psmsoftintr(arg);
	}
}

static void
psmsoftintr(void *arg)
{
	/*
	 * the table to turn PS/2 mouse button bits (MOUSE_PS2_BUTTON?DOWN)
	 * into `mousestatus' button bits (MOUSE_BUTTON?DOWN).
	 */
	static int butmap[8] = {
		0,
		MOUSE_BUTTON1DOWN,
		MOUSE_BUTTON3DOWN,
		MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN,
		MOUSE_BUTTON2DOWN,
		MOUSE_BUTTON1DOWN | MOUSE_BUTTON2DOWN,
		MOUSE_BUTTON2DOWN | MOUSE_BUTTON3DOWN,
		MOUSE_BUTTON1DOWN | MOUSE_BUTTON2DOWN | MOUSE_BUTTON3DOWN
	};
	struct psm_softc *sc = arg;
	mousestatus_t ms;
	packetbuf_t *pb;
	int x, y, z, c, l, s;

	getmicrouptime(&sc->lastsoftintr);

	s = spltty();

	do {
		pb = &sc->pqueue[sc->pqueue_start];

		if (sc->mode.level == PSM_LEVEL_NATIVE)
			goto next_native;

		c = pb->ipacket[0];
		/*
		 * A kludge for Kensington device!
		 * The MSB of the horizontal count appears to be stored in
		 * a strange place.
		 */
		if (sc->hw.model == MOUSE_MODEL_THINK)
			pb->ipacket[1] |= (c & MOUSE_PS2_XOVERFLOW) ? 0x80 : 0;

		/* ignore the overflow bits... */
		x = (c & MOUSE_PS2_XNEG) ?
		    pb->ipacket[1] - 256 : pb->ipacket[1];
		y = (c & MOUSE_PS2_YNEG) ?
		    pb->ipacket[2] - 256 : pb->ipacket[2];
		z = 0;
		ms.obutton = sc->button;	  /* previous button state */
		ms.button = butmap[c & MOUSE_PS2_BUTTONS];
		/* `tapping' action */
		if (sc->config & PSM_CONFIG_FORCETAP)
			ms.button |= ((c & MOUSE_PS2_TAP)) ?
			    0 : MOUSE_BUTTON4DOWN;
		timevalclear(&sc->idletimeout);
		sc->idlepacket.inputbytes = 0;

		switch (sc->hw.model) {

		case MOUSE_MODEL_EXPLORER:
			/*
			 *          b7 b6 b5 b4 b3 b2 b1 b0
			 * byte 1:  oy ox sy sx 1  M  R  L
			 * byte 2:  x  x  x  x  x  x  x  x
			 * byte 3:  y  y  y  y  y  y  y  y
			 * byte 4:  *  *  S2 S1 s  d2 d1 d0
			 *
			 * L, M, R, S1, S2: left, middle, right and side buttons
			 * s: wheel data sign bit
			 * d2-d0: wheel data
			 */
			z = (pb->ipacket[3] & MOUSE_EXPLORER_ZNEG) ?
			    (pb->ipacket[3] & 0x0f) - 16 :
			    (pb->ipacket[3] & 0x0f);
			ms.button |=
			    (pb->ipacket[3] & MOUSE_EXPLORER_BUTTON4DOWN) ?
			    MOUSE_BUTTON4DOWN : 0;
			ms.button |=
			    (pb->ipacket[3] & MOUSE_EXPLORER_BUTTON5DOWN) ?
			    MOUSE_BUTTON5DOWN : 0;
			break;

		case MOUSE_MODEL_INTELLI:
		case MOUSE_MODEL_NET:
			/* wheel data is in the fourth byte */
			z = (char)pb->ipacket[3];
			/*
			 * XXX some mice may send 7 when there is no Z movement?			 */
			if ((z >= 7) || (z <= -7))
				z = 0;
			/* some compatible mice have additional buttons */
			ms.button |= (c & MOUSE_PS2INTELLI_BUTTON4DOWN) ?
			    MOUSE_BUTTON4DOWN : 0;
			ms.button |= (c & MOUSE_PS2INTELLI_BUTTON5DOWN) ?
			    MOUSE_BUTTON5DOWN : 0;
			break;

		case MOUSE_MODEL_MOUSEMANPLUS:
			proc_mmanplus(sc, pb, &ms, &x, &y, &z);
			break;

		case MOUSE_MODEL_GLIDEPOINT:
			/* `tapping' action */
			ms.button |= ((c & MOUSE_PS2_TAP)) ? 0 :
			    MOUSE_BUTTON4DOWN;
			break;

		case MOUSE_MODEL_NETSCROLL:
			/*
			 * three additional bytes encode buttons and
			 * wheel events
			 */
			ms.button |= (pb->ipacket[3] & MOUSE_PS2_BUTTON3DOWN) ?
			    MOUSE_BUTTON4DOWN : 0;
			ms.button |= (pb->ipacket[3] & MOUSE_PS2_BUTTON1DOWN) ?
			    MOUSE_BUTTON5DOWN : 0;
			z = (pb->ipacket[3] & MOUSE_PS2_XNEG) ?
			    pb->ipacket[4] - 256 : pb->ipacket[4];
			break;

		case MOUSE_MODEL_THINK:
			/* the fourth button state in the first byte */
			ms.button |= (c & MOUSE_PS2_TAP) ?
			    MOUSE_BUTTON4DOWN : 0;
			break;

		case MOUSE_MODEL_VERSAPAD:
			proc_versapad(sc, pb, &ms, &x, &y, &z);
			c = ((x < 0) ? MOUSE_PS2_XNEG : 0) |
			    ((y < 0) ? MOUSE_PS2_YNEG : 0);
			break;

		case MOUSE_MODEL_4D:
			/*
			 *          b7 b6 b5 b4 b3 b2 b1 b0
			 * byte 1:  s2 d2 s1 d1 1  M  R  L
			 * byte 2:  sx x  x  x  x  x  x  x
			 * byte 3:  sy y  y  y  y  y  y  y
			 *
			 * s1: wheel 1 direction
			 * d1: wheel 1 data
			 * s2: wheel 2 direction
			 * d2: wheel 2 data
			 */
			x = (pb->ipacket[1] & 0x80) ?
			    pb->ipacket[1] - 256 : pb->ipacket[1];
			y = (pb->ipacket[2] & 0x80) ?
			    pb->ipacket[2] - 256 : pb->ipacket[2];
			switch (c & MOUSE_4D_WHEELBITS) {
			case 0x10:
				z = 1;
				break;
			case 0x30:
				z = -1;
				break;
			case 0x40:	/* XXX 2nd wheel turning right */
				z = 2;
				break;
			case 0xc0:	/* XXX 2nd wheel turning left */
				z = -2;
				break;
			}
			break;

		case MOUSE_MODEL_4DPLUS:
			if ((x < 16 - 256) && (y < 16 - 256)) {
				/*
				 *          b7 b6 b5 b4 b3 b2 b1 b0
				 * byte 1:  0  0  1  1  1  M  R  L
				 * byte 2:  0  0  0  0  1  0  0  0
				 * byte 3:  0  0  0  0  S  s  d1 d0
				 *
				 * L, M, R, S: left, middle, right,
				 *             and side buttons
				 * s: wheel data sign bit
				 * d1-d0: wheel data
				 */
				x = y = 0;
				if (pb->ipacket[2] & MOUSE_4DPLUS_BUTTON4DOWN)
					ms.button |= MOUSE_BUTTON4DOWN;
				z = (pb->ipacket[2] & MOUSE_4DPLUS_ZNEG) ?
				    ((pb->ipacket[2] & 0x07) - 8) :
				    (pb->ipacket[2] & 0x07) ;
			} else {
				/* preserve previous button states */
				ms.button |= ms.obutton & MOUSE_EXTBUTTONS;
			}
			break;

		case MOUSE_MODEL_SYNAPTICS:
			if (pb->inputbytes == MOUSE_PS2_PACKETSIZE)
				if (proc_synaptics_mux(sc, pb))
					goto next;

			if (proc_synaptics(sc, pb, &ms, &x, &y, &z) != 0) {
				VLOG(3, (LOG_DEBUG, "synaptics: "
				    "packet rejected\n"));
				goto next;
			}
			break;

		case MOUSE_MODEL_ELANTECH:
			if (proc_elantech(sc, pb, &ms, &x, &y, &z) != 0) {
				VLOG(3, (LOG_DEBUG, "elantech: "
				    "packet rejected\n"));
				goto next;
			}
			break;

		case MOUSE_MODEL_TRACKPOINT:
		case MOUSE_MODEL_GENERIC:
		default:
			break;
		}

#ifdef EVDEV_SUPPORT
	if (evdev_rcpt_mask & EVDEV_RCPT_HW_MOUSE &&
	    sc->hw.model != MOUSE_MODEL_ELANTECH &&
	    sc->hw.model != MOUSE_MODEL_SYNAPTICS) {
		evdev_push_rel(sc->evdev_r, REL_X, x);
		evdev_push_rel(sc->evdev_r, REL_Y, -y);

		switch (sc->hw.model) {
		case MOUSE_MODEL_EXPLORER:
		case MOUSE_MODEL_INTELLI:
		case MOUSE_MODEL_NET:
		case MOUSE_MODEL_NETSCROLL:
		case MOUSE_MODEL_4DPLUS:
			evdev_push_rel(sc->evdev_r, REL_WHEEL, -z);
			break;
		case MOUSE_MODEL_MOUSEMANPLUS:
		case MOUSE_MODEL_4D:
			switch (z) {
			case 1:
			case -1:
				evdev_push_rel(sc->evdev_r, REL_WHEEL, -z);
				break;
			case 2:
			case -2:
				evdev_push_rel(sc->evdev_r, REL_HWHEEL, z / 2);
				break;
			}
			break;
		}

		evdev_push_mouse_btn(sc->evdev_r, ms.button);
		evdev_sync(sc->evdev_r);
	}
#endif

	/* scale values */
	if (sc->mode.accelfactor >= 1) {
		if (x != 0) {
			x = x * x / sc->mode.accelfactor;
			if (x == 0)
				x = 1;
			if (c & MOUSE_PS2_XNEG)
				x = -x;
		}
		if (y != 0) {
			y = y * y / sc->mode.accelfactor;
			if (y == 0)
				y = 1;
			if (c & MOUSE_PS2_YNEG)
				y = -y;
		}
	}

	/* Store last packet for reinjection if it has not been set already */
	if (timevalisset(&sc->idletimeout) && sc->idlepacket.inputbytes == 0)
		sc->idlepacket = *pb;

	ms.dx = x;
	ms.dy = y;
	ms.dz = z;
	ms.flags = ((x || y || z) ? MOUSE_POSCHANGED : 0) |
	    (ms.obutton ^ ms.button);

	pb->inputbytes = tame_mouse(sc, pb, &ms, pb->ipacket);

	sc->status.flags |= ms.flags;
	sc->status.dx += ms.dx;
	sc->status.dy += ms.dy;
	sc->status.dz += ms.dz;
	sc->status.button = ms.button;
	sc->button = ms.button;

next_native:
	sc->watchdog = FALSE;

	/* queue data */
	if (sc->queue.count + pb->inputbytes < sizeof(sc->queue.buf)) {
		l = imin(pb->inputbytes,
		    sizeof(sc->queue.buf) - sc->queue.tail);
		bcopy(&pb->ipacket[0], &sc->queue.buf[sc->queue.tail], l);
		if (pb->inputbytes > l)
			bcopy(&pb->ipacket[l], &sc->queue.buf[0],
			    pb->inputbytes - l);
		sc->queue.tail = (sc->queue.tail + pb->inputbytes) %
		    sizeof(sc->queue.buf);
		sc->queue.count += pb->inputbytes;
	}

next:
	pb->inputbytes = 0;
	if (++sc->pqueue_start >= PSM_PACKETQUEUE)
		sc->pqueue_start = 0;
	} while (sc->pqueue_start != sc->pqueue_end);

	if (sc->state & PSM_ASLP) {
		sc->state &= ~PSM_ASLP;
		wakeup(sc);
	}
	selwakeuppri(&sc->rsel, PZERO);
	if (sc->async != NULL) {
		pgsigio(&sc->async, SIGIO, 0);
	}
	sc->state &= ~PSM_SOFTARMED;

	/* schedule injection of predefined packet after idletimeout
	 * if no data packets have been received from psmintr */
	if (timevalisset(&sc->idletimeout)) {
		sc->state |= PSM_SOFTARMED;
		callout_reset(&sc->softcallout, tvtohz(&sc->idletimeout),
		    psmsoftintridle, sc);
		VLOG(2, (LOG_DEBUG, "softintr: callout set: %d ticks\n",
		    tvtohz(&sc->idletimeout)));
	}
	splx(s);
}

static int
psmpoll(struct cdev *dev, int events, struct thread *td)
{
	struct psm_softc *sc = dev->si_drv1;
	int s;
	int revents = 0;

	/* Return true if a mouse event available */
	s = spltty();
	if (events & (POLLIN | POLLRDNORM)) {
		if (sc->queue.count > 0)
			revents |= events & (POLLIN | POLLRDNORM);
		else
			selrecord(td, &sc->rsel);
	}
	splx(s);

	return (revents);
}

/* vendor/model specific routines */

static int mouse_id_proc1(KBDC kbdc, int res, int scale, int *status)
{
	if (set_mouse_resolution(kbdc, res) != res)
		return (FALSE);
	if (set_mouse_scaling(kbdc, scale) &&
	    set_mouse_scaling(kbdc, scale) &&
	    set_mouse_scaling(kbdc, scale) &&
	    (get_mouse_status(kbdc, status, 0, 3) >= 3))
		return (TRUE);
	return (FALSE);
}

static int
mouse_ext_command(KBDC kbdc, int command)
{
	int c;

	c = (command >> 6) & 0x03;
	if (set_mouse_resolution(kbdc, c) != c)
		return (FALSE);
	c = (command >> 4) & 0x03;
	if (set_mouse_resolution(kbdc, c) != c)
		return (FALSE);
	c = (command >> 2) & 0x03;
	if (set_mouse_resolution(kbdc, c) != c)
		return (FALSE);
	c = (command >> 0) & 0x03;
	if (set_mouse_resolution(kbdc, c) != c)
		return (FALSE);
	return (TRUE);
}

#ifdef notyet
/* Logitech MouseMan Cordless II */
static int
enable_lcordless(struct psm_softc *sc, enum probearg arg)
{
	KBDC kbdc = sc->kbdc;
	int status[3];
	int ch;

	if (!mouse_id_proc1(kbdc, PSMD_RES_HIGH, 2, status))
		return (FALSE);
	if (status[1] == PSMD_RES_HIGH)
		return (FALSE);
	ch = (status[0] & 0x07) - 1;	/* channel # */
	if ((ch <= 0) || (ch > 4))
		return (FALSE);
	/*
	 * status[1]: always one?
	 * status[2]: battery status? (0-100)
	 */
	return (TRUE);
}
#endif /* notyet */

/* Genius NetScroll Mouse, MouseSystems SmartScroll Mouse */
static int
enable_groller(struct psm_softc *sc, enum probearg arg)
{
	KBDC kbdc = sc->kbdc;
	int status[3];

	/*
	 * The special sequence to enable the fourth button and the
	 * roller. Immediately after this sequence check status bytes.
	 * if the mouse is NetScroll, the second and the third bytes are
	 * '3' and 'D'.
	 */

	/*
	 * If the mouse is an ordinary PS/2 mouse, the status bytes should
	 * look like the following.
	 *
	 * byte 1 bit 7 always 0
	 *        bit 6 stream mode (0)
	 *        bit 5 disabled (0)
	 *        bit 4 1:1 scaling (0)
	 *        bit 3 always 0
	 *        bit 0-2 button status
	 * byte 2 resolution (PSMD_RES_HIGH)
	 * byte 3 report rate (?)
	 */

	if (!mouse_id_proc1(kbdc, PSMD_RES_HIGH, 1, status))
		return (FALSE);
	if ((status[1] != '3') || (status[2] != 'D'))
		return (FALSE);
	/* FIXME: SmartScroll Mouse has 5 buttons! XXX */
	if (arg == PROBE)
		sc->hw.buttons = 4;
	return (TRUE);
}

/* Genius NetMouse/NetMouse Pro, ASCII Mie Mouse, NetScroll Optical */
static int
enable_gmouse(struct psm_softc *sc, enum probearg arg)
{
	KBDC kbdc = sc->kbdc;
	int status[3];

	/*
	 * The special sequence to enable the middle, "rubber" button.
	 * Immediately after this sequence check status bytes.
	 * if the mouse is NetMouse, NetMouse Pro, or ASCII MIE Mouse,
	 * the second and the third bytes are '3' and 'U'.
	 * NOTE: NetMouse reports that it has three buttons although it has
	 * two buttons and a rubber button. NetMouse Pro and MIE Mouse
	 * say they have three buttons too and they do have a button on the
	 * side...
	 */
	if (!mouse_id_proc1(kbdc, PSMD_RES_HIGH, 1, status))
		return (FALSE);
	if ((status[1] != '3') || (status[2] != 'U'))
		return (FALSE);
	return (TRUE);
}

/* ALPS GlidePoint */
static int
enable_aglide(struct psm_softc *sc, enum probearg arg)
{
	KBDC kbdc = sc->kbdc;
	int status[3];

	/*
	 * The special sequence to obtain ALPS GlidePoint specific
	 * information. Immediately after this sequence, status bytes will
	 * contain something interesting.
	 * NOTE: ALPS produces several models of GlidePoint. Some of those
	 * do not respond to this sequence, thus, cannot be detected this way.
	 */
	if (set_mouse_sampling_rate(kbdc, 100) != 100)
		return (FALSE);
	if (!mouse_id_proc1(kbdc, PSMD_RES_LOW, 2, status))
		return (FALSE);
	if ((status[1] == PSMD_RES_LOW) || (status[2] == 100))
		return (FALSE);
	return (TRUE);
}

/* Kensington ThinkingMouse/Trackball */
static int
enable_kmouse(struct psm_softc *sc, enum probearg arg)
{
	static u_char rate[] = { 20, 60, 40, 20, 20, 60, 40, 20, 20 };
	KBDC kbdc = sc->kbdc;
	int status[3];
	int id1;
	int id2;
	int i;

	id1 = get_aux_id(kbdc);
	if (set_mouse_sampling_rate(kbdc, 10) != 10)
		return (FALSE);
	/*
	 * The device is now in the native mode? It returns a different
	 * ID value...
	 */
	id2 = get_aux_id(kbdc);
	if ((id1 == id2) || (id2 != 2))
		return (FALSE);

	if (set_mouse_resolution(kbdc, PSMD_RES_LOW) != PSMD_RES_LOW)
		return (FALSE);
#if PSM_DEBUG >= 2
	/* at this point, resolution is LOW, sampling rate is 10/sec */
	if (get_mouse_status(kbdc, status, 0, 3) < 3)
		return (FALSE);
#endif

	/*
	 * The special sequence to enable the third and fourth buttons.
	 * Otherwise they behave like the first and second buttons.
	 */
	for (i = 0; i < nitems(rate); ++i)
		if (set_mouse_sampling_rate(kbdc, rate[i]) != rate[i])
			return (FALSE);

	/*
	 * At this point, the device is using default resolution and
	 * sampling rate for the native mode.
	 */
	if (get_mouse_status(kbdc, status, 0, 3) < 3)
		return (FALSE);
	if ((status[1] == PSMD_RES_LOW) || (status[2] == rate[i - 1]))
		return (FALSE);

	/* the device appears be enabled by this sequence, diable it for now */
	disable_aux_dev(kbdc);
	empty_aux_buffer(kbdc, 5);

	return (TRUE);
}

/* Logitech MouseMan+/FirstMouse+, IBM ScrollPoint Mouse */
static int
enable_mmanplus(struct psm_softc *sc, enum probearg arg)
{
	KBDC kbdc = sc->kbdc;
	int data[3];

	/* the special sequence to enable the fourth button and the roller. */
	/*
	 * NOTE: for ScrollPoint to respond correctly, the SET_RESOLUTION
	 * must be called exactly three times since the last RESET command
	 * before this sequence. XXX
	 */
	if (!set_mouse_scaling(kbdc, 1))
		return (FALSE);
	if (!mouse_ext_command(kbdc, 0x39) || !mouse_ext_command(kbdc, 0xdb))
		return (FALSE);
	if (get_mouse_status(kbdc, data, 1, 3) < 3)
		return (FALSE);

	/*
	 * PS2++ protocol, packet type 0
	 *
	 *          b7 b6 b5 b4 b3 b2 b1 b0
	 * byte 1:  *  1  p3 p2 1  *  *  *
	 * byte 2:  1  1  p1 p0 m1 m0 1  0
	 * byte 3:  m7 m6 m5 m4 m3 m2 m1 m0
	 *
	 * p3-p0: packet type: 0
	 * m7-m0: model ID: MouseMan+:0x50,
	 *		    FirstMouse+:0x51,
	 *		    ScrollPoint:0x58...
	 */
	/* check constant bits */
	if ((data[0] & MOUSE_PS2PLUS_SYNCMASK) != MOUSE_PS2PLUS_SYNC)
		return (FALSE);
	if ((data[1] & 0xc3) != 0xc2)
		return (FALSE);
	/* check d3-d0 in byte 2 */
	if (!MOUSE_PS2PLUS_CHECKBITS(data))
		return (FALSE);
	/* check p3-p0 */
	if (MOUSE_PS2PLUS_PACKET_TYPE(data) != 0)
		return (FALSE);

	if (arg == PROBE) {
		sc->hw.hwid &= 0x00ff;
		sc->hw.hwid |= data[2] << 8;	/* save model ID */
	}

	/*
	 * MouseMan+ (or FirstMouse+) is now in its native mode, in which
	 * the wheel and the fourth button events are encoded in the
	 * special data packet. The mouse may be put in the IntelliMouse mode
	 * if it is initialized by the IntelliMouse's method.
	 */
	return (TRUE);
}

/* MS IntelliMouse Explorer */
static int
enable_msexplorer(struct psm_softc *sc, enum probearg arg)
{
	KBDC kbdc = sc->kbdc;
	static u_char rate0[] = { 200, 100, 80, };
	static u_char rate1[] = { 200, 200, 80, };
	int id;
	int i;

	/*
	 * This is needed for at least A4Tech X-7xx mice - they do not go
	 * straight to Explorer mode, but need to be set to Intelli mode
	 * first.
	 */
	enable_msintelli(sc, arg);

	/* the special sequence to enable the extra buttons and the roller. */
	for (i = 0; i < nitems(rate1); ++i)
		if (set_mouse_sampling_rate(kbdc, rate1[i]) != rate1[i])
			return (FALSE);
	/* the device will give the genuine ID only after the above sequence */
	id = get_aux_id(kbdc);
	if (id != PSM_EXPLORER_ID)
		return (FALSE);

	if (arg == PROBE) {
		sc->hw.buttons = 5;	/* IntelliMouse Explorer XXX */
		sc->hw.hwid = id;
	}

	/*
	 * XXX: this is a kludge to fool some KVM switch products
	 * which think they are clever enough to know the 4-byte IntelliMouse
	 * protocol, and assume any other protocols use 3-byte packets.
	 * They don't convey 4-byte data packets from the IntelliMouse Explorer
	 * correctly to the host computer because of this!
	 * The following sequence is actually IntelliMouse's "wake up"
	 * sequence; it will make the KVM think the mouse is IntelliMouse
	 * when it is in fact IntelliMouse Explorer.
	 */
	for (i = 0; i < nitems(rate0); ++i)
		if (set_mouse_sampling_rate(kbdc, rate0[i]) != rate0[i])
			break;
	get_aux_id(kbdc);

	return (TRUE);
}

/*
 * MS IntelliMouse
 * Logitech MouseMan+ and FirstMouse+ will also respond to this
 * probe routine and act like IntelliMouse.
 */
static int
enable_msintelli(struct psm_softc *sc, enum probearg arg)
{
	KBDC kbdc = sc->kbdc;
	static u_char rate[] = { 200, 100, 80, };
	int id;
	int i;

	/* the special sequence to enable the third button and the roller. */
	for (i = 0; i < nitems(rate); ++i)
		if (set_mouse_sampling_rate(kbdc, rate[i]) != rate[i])
			return (FALSE);
	/* the device will give the genuine ID only after the above sequence */
	id = get_aux_id(kbdc);
	if (id != PSM_INTELLI_ID)
		return (FALSE);

	if (arg == PROBE) {
		sc->hw.buttons = 3;
		sc->hw.hwid = id;
	}

	return (TRUE);
}

/*
 * A4 Tech 4D Mouse
 * Newer wheel mice from A4 Tech may use the 4D+ protocol.
 */
static int
enable_4dmouse(struct psm_softc *sc, enum probearg arg)
{
	static u_char rate[] = { 200, 100, 80, 60, 40, 20 };
	KBDC kbdc = sc->kbdc;
	int id;
	int i;

	for (i = 0; i < nitems(rate); ++i)
		if (set_mouse_sampling_rate(kbdc, rate[i]) != rate[i])
			return (FALSE);
	id = get_aux_id(kbdc);
	/*
	 * WinEasy 4D, 4 Way Scroll 4D: 6
	 * Cable-Free 4D: 8 (4DPLUS)
	 * WinBest 4D+, 4 Way Scroll 4D+: 8 (4DPLUS)
	 */
	if (id != PSM_4DMOUSE_ID)
		return (FALSE);

	if (arg == PROBE) {
		sc->hw.buttons = 3;	/* XXX some 4D mice have 4? */
		sc->hw.hwid = id;
	}

	return (TRUE);
}

/*
 * A4 Tech 4D+ Mouse
 * Newer wheel mice from A4 Tech seem to use this protocol.
 * Older models are recognized as either 4D Mouse or IntelliMouse.
 */
static int
enable_4dplus(struct psm_softc *sc, enum probearg arg)
{
	KBDC kbdc = sc->kbdc;
	int id;

	/*
	 * enable_4dmouse() already issued the following ID sequence...
	static u_char rate[] = { 200, 100, 80, 60, 40, 20 };
	int i;

	for (i = 0; i < sizeof(rate)/sizeof(rate[0]); ++i)
		if (set_mouse_sampling_rate(kbdc, rate[i]) != rate[i])
			return (FALSE);
	*/

	id = get_aux_id(kbdc);
	switch (id) {
	case PSM_4DPLUS_ID:
		break;
	case PSM_4DPLUS_RFSW35_ID:
		break;
	default:
		return (FALSE);
	}

	if (arg == PROBE) {
		sc->hw.buttons = (id == PSM_4DPLUS_ID) ? 4 : 3;
		sc->hw.hwid = id;
	}

	return (TRUE);
}

/* Synaptics Touchpad */
static int
synaptics_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct psm_softc *sc;
	int error, arg;

	if (oidp->oid_arg1 == NULL || oidp->oid_arg2 < 0 ||
	    oidp->oid_arg2 > SYNAPTICS_SYSCTL_SOFTBUTTON3_X)
		return (EINVAL);

	sc = oidp->oid_arg1;

	/* Read the current value. */
	arg = *(int *)((char *)sc + oidp->oid_arg2);
	error = sysctl_handle_int(oidp, &arg, 0, req);

	/* Sanity check. */
	if (error || !req->newptr)
		return (error);

	/*
	 * Check that the new value is in the concerned node's range
	 * of values.
	 */
	switch (oidp->oid_arg2) {
	case SYNAPTICS_SYSCTL_MIN_PRESSURE:
	case SYNAPTICS_SYSCTL_MAX_PRESSURE:
		if (arg < 0 || arg > 255)
			return (EINVAL);
		break;
	case SYNAPTICS_SYSCTL_MAX_WIDTH:
		if (arg < 4 || arg > 15)
			return (EINVAL);
		break;
	case SYNAPTICS_SYSCTL_MARGIN_TOP:
	case SYNAPTICS_SYSCTL_MARGIN_BOTTOM:
	case SYNAPTICS_SYSCTL_NA_TOP:
	case SYNAPTICS_SYSCTL_NA_BOTTOM:
		if (arg < 0 || arg > sc->synhw.maximumYCoord)
			return (EINVAL);
		break;
	case SYNAPTICS_SYSCTL_SOFTBUTTON2_X:
	case SYNAPTICS_SYSCTL_SOFTBUTTON3_X:
		/* Softbuttons is clickpad only feature */
		if (!sc->synhw.capClickPad && arg != 0)
			return (EINVAL);
		/* FALLTHROUGH */
	case SYNAPTICS_SYSCTL_MARGIN_RIGHT:
	case SYNAPTICS_SYSCTL_MARGIN_LEFT:
	case SYNAPTICS_SYSCTL_NA_RIGHT:
	case SYNAPTICS_SYSCTL_NA_LEFT:
		if (arg < 0 || arg > sc->synhw.maximumXCoord)
			return (EINVAL);
		break;
	case SYNAPTICS_SYSCTL_WINDOW_MIN:
	case SYNAPTICS_SYSCTL_WINDOW_MAX:
	case SYNAPTICS_SYSCTL_TAP_MIN_QUEUE:
		if (arg < 1 || arg > SYNAPTICS_PACKETQUEUE)
			return (EINVAL);
		break;
	case SYNAPTICS_SYSCTL_MULTIPLICATOR:
	case SYNAPTICS_SYSCTL_WEIGHT_CURRENT:
	case SYNAPTICS_SYSCTL_WEIGHT_PREVIOUS:
	case SYNAPTICS_SYSCTL_WEIGHT_PREVIOUS_NA:
	case SYNAPTICS_SYSCTL_WEIGHT_LEN_SQUARED:
	case SYNAPTICS_SYSCTL_DIV_MIN:
	case SYNAPTICS_SYSCTL_DIV_MAX:
	case SYNAPTICS_SYSCTL_DIV_MAX_NA:
	case SYNAPTICS_SYSCTL_DIV_LEN:
	case SYNAPTICS_SYSCTL_VSCROLL_DIV_MIN:
	case SYNAPTICS_SYSCTL_VSCROLL_DIV_MAX:
		if (arg < 1)
			return (EINVAL);
		break;
	case SYNAPTICS_SYSCTL_TAP_MAX_DELTA:
	case SYNAPTICS_SYSCTL_TAPHOLD_TIMEOUT:
	case SYNAPTICS_SYSCTL_VSCROLL_MIN_DELTA:
		if (arg < 0)
			return (EINVAL);
		break;
	case SYNAPTICS_SYSCTL_VSCROLL_HOR_AREA:
		if (arg < -sc->synhw.maximumXCoord ||
		    arg > sc->synhw.maximumXCoord)
			return (EINVAL);
		break;
	case SYNAPTICS_SYSCTL_SOFTBUTTONS_Y:
		/* Softbuttons is clickpad only feature */
		if (!sc->synhw.capClickPad && arg != 0)
			return (EINVAL);
		/* FALLTHROUGH */
	case SYNAPTICS_SYSCTL_VSCROLL_VER_AREA:
		if (arg < -sc->synhw.maximumYCoord ||
		    arg > sc->synhw.maximumYCoord)
			return (EINVAL);
		break;
        case SYNAPTICS_SYSCTL_TOUCHPAD_OFF:
		if (arg < 0 || arg > 1)
			return (EINVAL);
		break;
	default:
		return (EINVAL);
	}

	/* Update. */
	*(int *)((char *)sc + oidp->oid_arg2) = arg;

	return (error);
}

static void
synaptics_sysctl_create_softbuttons_tree(struct psm_softc *sc)
{
	/*
	 * Set predefined sizes for softbuttons.
	 * Values are taken to match HP Pavilion dv6 clickpad drawings
	 * with thin middle softbutton placed on separator
	 */

	/* hw.psm.synaptics.softbuttons_y */
	sc->syninfo.softbuttons_y = sc->synhw.topButtonPad ? -1700 : 1700;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "softbuttons_y", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_SOFTBUTTONS_Y,
	    synaptics_sysctl, "I",
	    "Vertical size of softbuttons area");

	/* hw.psm.synaptics.softbutton2_x */
	sc->syninfo.softbutton2_x = 3100;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "softbutton2_x", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_SOFTBUTTON2_X,
	    synaptics_sysctl, "I",
	    "Horisontal position of 2-nd softbutton left edge (0-disable)");

	/* hw.psm.synaptics.softbutton3_x */
	sc->syninfo.softbutton3_x = 3900;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "softbutton3_x", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_SOFTBUTTON3_X,
	    synaptics_sysctl, "I",
	    "Horisontal position of 3-rd softbutton left edge (0-disable)");
}

static void
synaptics_sysctl_create_tree(struct psm_softc *sc, const char *name,
    const char *descr)
{

	if (sc->syninfo.sysctl_tree != NULL)
		return;

	/* Attach extra synaptics sysctl nodes under hw.psm.synaptics */
	sysctl_ctx_init(&sc->syninfo.sysctl_ctx);
	sc->syninfo.sysctl_tree = SYSCTL_ADD_NODE(&sc->syninfo.sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_hw_psm), OID_AUTO, name, CTLFLAG_RD,
	    0, descr);

	/* hw.psm.synaptics.directional_scrolls. */
	sc->syninfo.directional_scrolls = 0;
	SYSCTL_ADD_INT(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "directional_scrolls", CTLFLAG_RW|CTLFLAG_ANYBODY,
	    &sc->syninfo.directional_scrolls, 0,
	    "Enable hardware scrolling pad (if non-zero) or register it as "
	    "extended buttons (if 0)");

	/* hw.psm.synaptics.max_x. */
	sc->syninfo.max_x = 6143;
	SYSCTL_ADD_INT(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "max_x", CTLFLAG_RD|CTLFLAG_ANYBODY,
	    &sc->syninfo.max_x, 0,
	    "Horizontal reporting range");

	/* hw.psm.synaptics.max_y. */
	sc->syninfo.max_y = 6143;
	SYSCTL_ADD_INT(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "max_y", CTLFLAG_RD|CTLFLAG_ANYBODY,
	    &sc->syninfo.max_y, 0,
	    "Vertical reporting range");

	/*
	 * Turn off two finger scroll if we have a
	 * physical area reserved for scrolling or when
	 * there's no multi finger support.
	 */
	if (sc->synhw.verticalScroll || (sc->synhw.capMultiFinger == 0 &&
					 sc->synhw.capAdvancedGestures == 0))
		sc->syninfo.two_finger_scroll = 0;
	else
		sc->syninfo.two_finger_scroll = 1;
	/* hw.psm.synaptics.two_finger_scroll. */
	SYSCTL_ADD_INT(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "two_finger_scroll", CTLFLAG_RW|CTLFLAG_ANYBODY,
	    &sc->syninfo.two_finger_scroll, 0,
	    "Enable two finger scrolling");

	/* hw.psm.synaptics.min_pressure. */
	sc->syninfo.min_pressure = 32;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "min_pressure", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_MIN_PRESSURE,
	    synaptics_sysctl, "I",
	    "Minimum pressure required to start an action");

	/* hw.psm.synaptics.max_pressure. */
	sc->syninfo.max_pressure = 220;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "max_pressure", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_MAX_PRESSURE,
	    synaptics_sysctl, "I",
	    "Maximum pressure to detect palm");

	/* hw.psm.synaptics.max_width. */
	sc->syninfo.max_width = 10;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "max_width", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_MAX_WIDTH,
	    synaptics_sysctl, "I",
	    "Maximum finger width to detect palm");

	/* hw.psm.synaptics.top_margin. */
	sc->syninfo.margin_top = 200;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "margin_top", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_MARGIN_TOP,
	    synaptics_sysctl, "I",
	    "Top margin");

	/* hw.psm.synaptics.right_margin. */
	sc->syninfo.margin_right = 200;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "margin_right", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_MARGIN_RIGHT,
	    synaptics_sysctl, "I",
	    "Right margin");

	/* hw.psm.synaptics.bottom_margin. */
	sc->syninfo.margin_bottom = 200;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "margin_bottom", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_MARGIN_BOTTOM,
	    synaptics_sysctl, "I",
	    "Bottom margin");

	/* hw.psm.synaptics.left_margin. */
	sc->syninfo.margin_left = 200;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "margin_left", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_MARGIN_LEFT,
	    synaptics_sysctl, "I",
	    "Left margin");

	/* hw.psm.synaptics.na_top. */
	sc->syninfo.na_top = 1783;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "na_top", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_NA_TOP,
	    synaptics_sysctl, "I",
	    "Top noisy area, where weight_previous_na is used instead "
	    "of weight_previous");

	/* hw.psm.synaptics.na_right. */
	sc->syninfo.na_right = 563;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "na_right", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_NA_RIGHT,
	    synaptics_sysctl, "I",
	    "Right noisy area, where weight_previous_na is used instead "
	    "of weight_previous");

	/* hw.psm.synaptics.na_bottom. */
	sc->syninfo.na_bottom = 1408;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "na_bottom", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_NA_BOTTOM,
	    synaptics_sysctl, "I",
	    "Bottom noisy area, where weight_previous_na is used instead "
	    "of weight_previous");

	/* hw.psm.synaptics.na_left. */
	sc->syninfo.na_left = 1600;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "na_left", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_NA_LEFT,
	    synaptics_sysctl, "I",
	    "Left noisy area, where weight_previous_na is used instead "
	    "of weight_previous");

	/* hw.psm.synaptics.window_min. */
	sc->syninfo.window_min = 4;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "window_min", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_WINDOW_MIN,
	    synaptics_sysctl, "I",
	    "Minimum window size to start an action");

	/* hw.psm.synaptics.window_max. */
	sc->syninfo.window_max = 10;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "window_max", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_WINDOW_MAX,
	    synaptics_sysctl, "I",
	    "Maximum window size");

	/* hw.psm.synaptics.multiplicator. */
	sc->syninfo.multiplicator = 10000;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "multiplicator", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_MULTIPLICATOR,
	    synaptics_sysctl, "I",
	    "Multiplicator to increase precision in averages and divisions");

	/* hw.psm.synaptics.weight_current. */
	sc->syninfo.weight_current = 3;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "weight_current", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_WEIGHT_CURRENT,
	    synaptics_sysctl, "I",
	    "Weight of the current movement in the new average");

	/* hw.psm.synaptics.weight_previous. */
	sc->syninfo.weight_previous = 6;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "weight_previous", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_WEIGHT_PREVIOUS,
	    synaptics_sysctl, "I",
	    "Weight of the previous average");

	/* hw.psm.synaptics.weight_previous_na. */
	sc->syninfo.weight_previous_na = 20;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "weight_previous_na", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_WEIGHT_PREVIOUS_NA,
	    synaptics_sysctl, "I",
	    "Weight of the previous average (inside the noisy area)");

	/* hw.psm.synaptics.weight_len_squared. */
	sc->syninfo.weight_len_squared = 2000;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "weight_len_squared", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_WEIGHT_LEN_SQUARED,
	    synaptics_sysctl, "I",
	    "Length (squared) of segments where weight_previous "
	    "starts to decrease");

	/* hw.psm.synaptics.div_min. */
	sc->syninfo.div_min = 9;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "div_min", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_DIV_MIN,
	    synaptics_sysctl, "I",
	    "Divisor for fast movements");

	/* hw.psm.synaptics.div_max. */
	sc->syninfo.div_max = 17;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "div_max", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_DIV_MAX,
	    synaptics_sysctl, "I",
	    "Divisor for slow movements");

	/* hw.psm.synaptics.div_max_na. */
	sc->syninfo.div_max_na = 30;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "div_max_na", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_DIV_MAX_NA,
	    synaptics_sysctl, "I",
	    "Divisor with slow movements (inside the noisy area)");

	/* hw.psm.synaptics.div_len. */
	sc->syninfo.div_len = 100;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "div_len", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_DIV_LEN,
	    synaptics_sysctl, "I",
	    "Length of segments where div_max starts to decrease");

	/* hw.psm.synaptics.tap_max_delta. */
	sc->syninfo.tap_max_delta = 80;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "tap_max_delta", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_TAP_MAX_DELTA,
	    synaptics_sysctl, "I",
	    "Length of segments above which a tap is ignored");

	/* hw.psm.synaptics.tap_min_queue. */
	sc->syninfo.tap_min_queue = 2;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "tap_min_queue", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_TAP_MIN_QUEUE,
	    synaptics_sysctl, "I",
	    "Number of packets required to consider a tap");

	/* hw.psm.synaptics.taphold_timeout. */
	sc->gesture.in_taphold = 0;
	sc->syninfo.taphold_timeout = tap_timeout;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "taphold_timeout", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_TAPHOLD_TIMEOUT,
	    synaptics_sysctl, "I",
	    "Maximum elapsed time between two taps to consider a tap-hold "
	    "action");

	/* hw.psm.synaptics.vscroll_hor_area. */
	sc->syninfo.vscroll_hor_area = 0; /* 1300 */
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "vscroll_hor_area", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_VSCROLL_HOR_AREA,
	    synaptics_sysctl, "I",
	    "Area reserved for horizontal virtual scrolling");

	/* hw.psm.synaptics.vscroll_ver_area. */
	sc->syninfo.vscroll_ver_area = -400 - sc->syninfo.margin_right;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "vscroll_ver_area", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_VSCROLL_VER_AREA,
	    synaptics_sysctl, "I",
	    "Area reserved for vertical virtual scrolling");

	/* hw.psm.synaptics.vscroll_min_delta. */
	sc->syninfo.vscroll_min_delta = 50;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "vscroll_min_delta", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_VSCROLL_MIN_DELTA,
	    synaptics_sysctl, "I",
	    "Minimum movement to consider virtual scrolling");

	/* hw.psm.synaptics.vscroll_div_min. */
	sc->syninfo.vscroll_div_min = 100;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "vscroll_div_min", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_VSCROLL_DIV_MIN,
	    synaptics_sysctl, "I",
	    "Divisor for fast scrolling");

	/* hw.psm.synaptics.vscroll_div_min. */
	sc->syninfo.vscroll_div_max = 150;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "vscroll_div_max", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_VSCROLL_DIV_MAX,
	    synaptics_sysctl, "I",
	    "Divisor for slow scrolling");

	/* hw.psm.synaptics.touchpad_off. */
	sc->syninfo.touchpad_off = 0;
	SYSCTL_ADD_PROC(&sc->syninfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->syninfo.sysctl_tree), OID_AUTO,
	    "touchpad_off", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, SYNAPTICS_SYSCTL_TOUCHPAD_OFF,
	    synaptics_sysctl, "I",
	    "Turn off touchpad");

	sc->syninfo.softbuttons_y = 0;
	sc->syninfo.softbutton2_x = 0;
	sc->syninfo.softbutton3_x = 0;

	/* skip softbuttons sysctl on not clickpads */
	if (sc->synhw.capClickPad)
		synaptics_sysctl_create_softbuttons_tree(sc);
}

static int
synaptics_preferred_mode(struct psm_softc *sc) {
	int mode_byte;

	/* Check if we are in relative mode */
	if (sc->hw.model != MOUSE_MODEL_SYNAPTICS) {
		if (tap_enabled == 0)
			/*
			 * Disable tap & drag gestures. We use a Mode Byte
			 * and set the DisGest bit (see §2.5 of Synaptics
			 * TouchPad Interfacing Guide).
			 */
			return (0x04);
		else
			/*
			 * Enable tap & drag gestures. We use a Mode Byte
			 * and clear the DisGest bit (see §2.5 of Synaptics
			 * TouchPad Interfacing Guide).
			 */
			return (0x00);
	}

	mode_byte = 0xc4;

	/* request wmode where available */
	if (sc->synhw.capExtended)
		mode_byte |= 1;

	return mode_byte;
}

static void
synaptics_set_mode(struct psm_softc *sc, int mode_byte) {
	mouse_ext_command(sc->kbdc, mode_byte);

	/* "Commit" the Set Mode Byte command sent above. */
	set_mouse_sampling_rate(sc->kbdc, 20);

	/*
	 * Enable advanced gestures mode if supported and we are not entering
	 * passthrough or relative mode.
	 */
	if ((sc->synhw.capAdvancedGestures || sc->synhw.capReportsV) &&
	    sc->hw.model == MOUSE_MODEL_SYNAPTICS && !(mode_byte & (1 << 5))) {
		mouse_ext_command(sc->kbdc, 3);
		set_mouse_sampling_rate(sc->kbdc, 0xc8);
	}
}

/*
 * AUX MUX detection code should be placed at very beginning of probe sequence
 * at least before 4-byte protocol mouse probes e.g. MS IntelliMouse probe as
 * latter can trigger switching the MUX to incompatible state.
 */
static int
enable_synaptics_mux(struct psm_softc *sc, enum probearg arg)
{
	KBDC kbdc = sc->kbdc;
	int port, version;
	int probe = FALSE;
	int active_ports_count = 0;
	int active_ports_mask = 0;

	version = enable_aux_mux(kbdc);
	if (version == -1)
		return (FALSE);

	for (port = 0; port < KBDC_AUX_MUX_NUM_PORTS; port++) {
		VLOG(3, (LOG_DEBUG, "aux_mux: ping port %d\n", port));
		set_active_aux_mux_port(kbdc, port);
		if (enable_aux_dev(kbdc) && disable_aux_dev(kbdc)) {
			active_ports_count++;
			active_ports_mask |= 1 << port;
		}
	}

	if (verbose >= 2)
		printf("Active Multiplexing PS/2 controller v%d.%d with %d "
		    "active port(s)\n", version >> 4 & 0x0f, version & 0x0f,
		    active_ports_count);

	/* psm has a special support for GenMouse + SynTouchpad combination */
	if (active_ports_count >= 2) {
		for (port = 0; port < KBDC_AUX_MUX_NUM_PORTS; port++) {
			if ((active_ports_mask & 1 << port) == 0)
				continue;
			VLOG(3, (LOG_DEBUG, "aux_mux: probe port %d\n", port));
			set_active_aux_mux_port(kbdc, port);
			probe = enable_synaptics(sc, arg);
			if (probe) {
				if (arg == PROBE)
					sc->muxport = port;
				break;
			}
		}
	}

	/* IRQ handler does not support active multiplexing mode */
	disable_aux_mux(kbdc);

	return (probe);
}

static int
enable_synaptics(struct psm_softc *sc, enum probearg arg)
{
	device_t psmcpnp;
	struct psmcpnp_softc *psmcpnp_sc;
	KBDC kbdc = sc->kbdc;
	synapticshw_t synhw;
	int status[3];
	int buttons;

	VLOG(3, (LOG_DEBUG, "synaptics: BEGIN init\n"));

	/*
	 * Just to be on the safe side: this avoids troubles with
	 * following mouse_ext_command() when the previous command
	 * was PSMC_SET_RESOLUTION. Set Scaling has no effect on
	 * Synaptics Touchpad behaviour.
	 */
	set_mouse_scaling(kbdc, 1);

	/* Identify the Touchpad version. */
	if (mouse_ext_command(kbdc, 0) == 0)
		return (FALSE);
	if (get_mouse_status(kbdc, status, 0, 3) != 3)
		return (FALSE);
	if (status[1] != 0x47)
		return (FALSE);

	bzero(&synhw, sizeof(synhw));
	synhw.infoMinor = status[0];
	synhw.infoMajor = status[2] & 0x0f;

	if (verbose >= 2)
		printf("Synaptics Touchpad v%d.%d\n", synhw.infoMajor,
		    synhw.infoMinor);

	if (synhw.infoMajor < 4) {
		printf("  Unsupported (pre-v4) Touchpad detected\n");
		return (FALSE);
	}

	/* Get the Touchpad model information. */
	if (mouse_ext_command(kbdc, 3) == 0)
		return (FALSE);
	if (get_mouse_status(kbdc, status, 0, 3) != 3)
		return (FALSE);
	if ((status[1] & 0x01) != 0) {
		printf("  Failed to read model information\n");
		return (FALSE);
	}

	synhw.infoRot180   = (status[0] & 0x80) != 0;
	synhw.infoPortrait = (status[0] & 0x40) != 0;
	synhw.infoSensor   =  status[0] & 0x3f;
	synhw.infoHardware = (status[1] & 0xfe) >> 1;
	synhw.infoNewAbs   = (status[2] & 0x80) != 0;
	synhw.capPen       = (status[2] & 0x40) != 0;
	synhw.infoSimplC   = (status[2] & 0x20) != 0;
	synhw.infoGeometry =  status[2] & 0x0f;

	if (verbose >= 2) {
		printf("  Model information:\n");
		printf("   infoRot180: %d\n", synhw.infoRot180);
		printf("   infoPortrait: %d\n", synhw.infoPortrait);
		printf("   infoSensor: %d\n", synhw.infoSensor);
		printf("   infoHardware: %d\n", synhw.infoHardware);
		printf("   infoNewAbs: %d\n", synhw.infoNewAbs);
		printf("   capPen: %d\n", synhw.capPen);
		printf("   infoSimplC: %d\n", synhw.infoSimplC);
		printf("   infoGeometry: %d\n", synhw.infoGeometry);
	}

	/* Read the extended capability bits. */
	if (mouse_ext_command(kbdc, 2) == 0)
		return (FALSE);
	if (get_mouse_status(kbdc, status, 0, 3) != 3)
		return (FALSE);
	if (!SYNAPTICS_VERSION_GE(synhw, 7, 5) && status[1] != 0x47) {
		printf("  Failed to read extended capability bits\n");
		return (FALSE);
	}

	psmcpnp = devclass_get_device(devclass_find(PSMCPNP_DRIVER_NAME),
	    sc->unit);
	psmcpnp_sc = (psmcpnp != NULL) ? device_get_softc(psmcpnp) : NULL;

	/* Set the different capabilities when they exist. */
	buttons = 0;
	synhw.capExtended = (status[0] & 0x80) != 0;
	if (synhw.capExtended) {
		synhw.nExtendedQueries = (status[0] & 0x70) >> 4;
		synhw.capMiddle        = (status[0] & 0x04) != 0;
		synhw.capPassthrough   = (status[2] & 0x80) != 0;
		synhw.capLowPower      = (status[2] & 0x40) != 0;
		synhw.capMultiFingerReport =
					 (status[2] & 0x20) != 0;
		synhw.capSleep         = (status[2] & 0x10) != 0;
		synhw.capFourButtons   = (status[2] & 0x08) != 0;
		synhw.capBallistics    = (status[2] & 0x04) != 0;
		synhw.capMultiFinger   = (status[2] & 0x02) != 0;
		synhw.capPalmDetect    = (status[2] & 0x01) != 0;

		if (!set_mouse_scaling(kbdc, 1))
			return (FALSE);
		if (mouse_ext_command(kbdc, 0x08) == 0)
			return (FALSE);
		if (get_mouse_status(kbdc, status, 0, 3) != 3)
			return (FALSE);

		if (status[0] != 0 && (status[1] & 0x80) && status[2] != 0) {
			synhw.infoXupmm = status[0];
			synhw.infoYupmm = status[2];
		}

		if (verbose >= 2) {
			printf("  Extended capabilities:\n");
			printf("   capExtended: %d\n", synhw.capExtended);
			printf("   capMiddle: %d\n", synhw.capMiddle);
			printf("   nExtendedQueries: %d\n",
			    synhw.nExtendedQueries);
			printf("   capPassthrough: %d\n", synhw.capPassthrough);
			printf("   capLowPower: %d\n", synhw.capLowPower);
			printf("   capMultiFingerReport: %d\n",
			    synhw.capMultiFingerReport);
			printf("   capSleep: %d\n", synhw.capSleep);
			printf("   capFourButtons: %d\n", synhw.capFourButtons);
			printf("   capBallistics: %d\n", synhw.capBallistics);
			printf("   capMultiFinger: %d\n", synhw.capMultiFinger);
			printf("   capPalmDetect: %d\n", synhw.capPalmDetect);
			printf("   infoXupmm: %d\n", synhw.infoXupmm);
			printf("   infoYupmm: %d\n", synhw.infoYupmm);
		}

		/*
		 * If nExtendedQueries is 1 or greater, then the TouchPad
		 * supports this number of extended queries. We can load
		 * more information about buttons using query 0x09.
		 */
		if (synhw.nExtendedQueries >= 1) {
			if (!set_mouse_scaling(kbdc, 1))
				return (FALSE);
			if (mouse_ext_command(kbdc, 0x09) == 0)
				return (FALSE);
			if (get_mouse_status(kbdc, status, 0, 3) != 3)
				return (FALSE);
			synhw.verticalScroll   = (status[0] & 0x01) != 0;
			synhw.horizontalScroll = (status[0] & 0x02) != 0;
			synhw.verticalWheel    = (status[0] & 0x08) != 0;
			synhw.nExtendedButtons = (status[1] & 0xf0) >> 4;
			synhw.capEWmode        = (status[0] & 0x04) != 0;
			if (verbose >= 2) {
				printf("  Extended model ID:\n");
				printf("   verticalScroll: %d\n",
				    synhw.verticalScroll);
				printf("   horizontalScroll: %d\n",
				    synhw.horizontalScroll);
				printf("   verticalWheel: %d\n",
				    synhw.verticalWheel);
				printf("   nExtendedButtons: %d\n",
				    synhw.nExtendedButtons);
				printf("   capEWmode: %d\n",
				    synhw.capEWmode);
			}
			/*
			 * Add the number of extended buttons to the total
			 * button support count, including the middle button
			 * if capMiddle support bit is set.
			 */
			buttons = synhw.nExtendedButtons + synhw.capMiddle;
		} else
			/*
			 * If the capFourButtons support bit is set,
			 * add a fourth button to the total button count.
			 */
			buttons = synhw.capFourButtons ? 1 : 0;

		/* Read the continued capabilities bits. */
		if (synhw.nExtendedQueries >= 4) {
			if (!set_mouse_scaling(kbdc, 1))
				return (FALSE);
			if (mouse_ext_command(kbdc, 0x0c) == 0)
				return (FALSE);
			if (get_mouse_status(kbdc, status, 0, 3) != 3)
				return (FALSE);

			synhw.capClickPad         = (status[1] & 0x01) << 1;
			synhw.capClickPad        |= (status[0] & 0x10) != 0;
			synhw.capDeluxeLEDs       = (status[1] & 0x02) != 0;
			synhw.noAbsoluteFilter    = (status[1] & 0x04) != 0;
			synhw.capReportsV         = (status[1] & 0x08) != 0;
			synhw.capUniformClickPad  = (status[1] & 0x10) != 0;
			synhw.capReportsMin       = (status[1] & 0x20) != 0;
			synhw.capInterTouch       = (status[1] & 0x40) != 0;
			synhw.capReportsMax       = (status[0] & 0x02) != 0;
			synhw.capClearPad         = (status[0] & 0x04) != 0;
			synhw.capAdvancedGestures = (status[0] & 0x08) != 0;
			synhw.capCoveredPad       = (status[0] & 0x80) != 0;

			if (synhw.capReportsMax) {
				if (!set_mouse_scaling(kbdc, 1))
					return (FALSE);
				if (mouse_ext_command(kbdc, 0x0d) == 0)
					return (FALSE);
				if (get_mouse_status(kbdc, status, 0, 3) != 3)
					return (FALSE);

				synhw.maximumXCoord = (status[0] << 5) |
						     ((status[1] & 0x0f) << 1);
				synhw.maximumYCoord = (status[2] << 5) |
						     ((status[1] & 0xf0) >> 3);
			} else {
				/*
				 * Typical bezel limits. Taken from 'Synaptics
				 * PS/2 * TouchPad Interfacing Guide' p.3.2.3.
				 */
				synhw.maximumXCoord = 5472;
				synhw.maximumYCoord = 4448;
			}

			if (synhw.capReportsMin) {
				if (!set_mouse_scaling(kbdc, 1))
					return (FALSE);
				if (mouse_ext_command(kbdc, 0x0f) == 0)
					return (FALSE);
				if (get_mouse_status(kbdc, status, 0, 3) != 3)
					return (FALSE);

				synhw.minimumXCoord = (status[0] << 5) |
						     ((status[1] & 0x0f) << 1);
				synhw.minimumYCoord = (status[2] << 5) |
						     ((status[1] & 0xf0) >> 3);
			} else {
				/*
				 * Typical bezel limits. Taken from 'Synaptics
				 * PS/2 * TouchPad Interfacing Guide' p.3.2.3.
				 */
				synhw.minimumXCoord = 1472;
				synhw.minimumYCoord = 1408;
			}

			/*
			 * ClickPad properties are not exported through PS/2
			 * protocol. Detection is based on controller's PnP ID.
			 */
			if (synhw.capClickPad && psmcpnp_sc != NULL) {
				switch (psmcpnp_sc->type) {
				case PSMCPNP_FORCEPAD:
					synhw.forcePad = 1;
					break;
				case PSMCPNP_TOPBUTTONPAD:
					synhw.topButtonPad = 1;
					break;
				default:
					break;
				}
			}

			if (verbose >= 2) {
				printf("  Continued capabilities:\n");
				printf("   capClickPad: %d\n",
				       synhw.capClickPad);
				printf("   capDeluxeLEDs: %d\n",
				       synhw.capDeluxeLEDs);
				printf("   noAbsoluteFilter: %d\n",
				       synhw.noAbsoluteFilter);
				printf("   capReportsV: %d\n",
				       synhw.capReportsV);
				printf("   capUniformClickPad: %d\n",
				       synhw.capUniformClickPad);
				printf("   capReportsMin: %d\n",
				       synhw.capReportsMin);
				printf("   capInterTouch: %d\n",
				       synhw.capInterTouch);
				printf("   capReportsMax: %d\n",
				       synhw.capReportsMax);
				printf("   capClearPad: %d\n",
				       synhw.capClearPad);
				printf("   capAdvancedGestures: %d\n",
				       synhw.capAdvancedGestures);
				printf("   capCoveredPad: %d\n",
				       synhw.capCoveredPad);
				if (synhw.capReportsMax) {
					printf("   maximumXCoord: %d\n",
					       synhw.maximumXCoord);
					printf("   maximumYCoord: %d\n",
					       synhw.maximumYCoord);
				}
				if (synhw.capReportsMin) {
					printf("   minimumXCoord: %d\n",
					       synhw.minimumXCoord);
					printf("   minimumYCoord: %d\n",
					       synhw.minimumYCoord);
				}
				if (synhw.capClickPad) {
					printf("  Clickpad capabilities:\n");
					printf("   forcePad: %d\n",
					       synhw.forcePad);
					printf("   topButtonPad: %d\n",
					       synhw.topButtonPad);
				}
			}
			buttons += synhw.capClickPad;
		}
	}

	if (verbose >= 2) {
		if (synhw.capExtended)
			printf("  Additional Buttons: %d\n", buttons);
		else
			printf("  No extended capabilities\n");
	}

	/*
	 * Add the default number of 3 buttons to the total
	 * count of supported buttons reported above.
	 */
	buttons += 3;

	/*
	 * Read the mode byte.
	 *
	 * XXX: Note the Synaptics documentation also defines the first
	 * byte of the response to this query to be a constant 0x3b, this
	 * does not appear to be true for Touchpads with guest devices.
	 */
	if (mouse_ext_command(kbdc, 1) == 0)
		return (FALSE);
	if (get_mouse_status(kbdc, status, 0, 3) != 3)
		return (FALSE);
	if (!SYNAPTICS_VERSION_GE(synhw, 7, 5) && status[1] != 0x47) {
		printf("  Failed to read mode byte\n");
		return (FALSE);
	}

	if (arg == PROBE)
		sc->synhw = synhw;
	if (!synaptics_support)
		return (FALSE);

	/* Set mouse type just now for synaptics_set_mode() */
	sc->hw.model = MOUSE_MODEL_SYNAPTICS;

	synaptics_set_mode(sc, synaptics_preferred_mode(sc));

	if (trackpoint_support && synhw.capPassthrough) {
		enable_trackpoint(sc, arg);
	}

	VLOG(3, (LOG_DEBUG, "synaptics: END init (%d buttons)\n", buttons));

	if (arg == PROBE) {
		/* Create sysctl tree. */
		synaptics_sysctl_create_tree(sc, "synaptics",
		    "Synaptics TouchPad");
		sc->hw.buttons = buttons;
	}

	return (TRUE);
}

static void
synaptics_passthrough_on(struct psm_softc *sc)
{
	VLOG(2, (LOG_NOTICE, "psm: setting pass-through mode.\n"));
	synaptics_set_mode(sc, synaptics_preferred_mode(sc) | (1 << 5));
}

static void
synaptics_passthrough_off(struct psm_softc *sc)
{
	VLOG(2, (LOG_NOTICE, "psm: turning pass-through mode off.\n"));
	set_mouse_scaling(sc->kbdc, 2);
	set_mouse_scaling(sc->kbdc, 1);
	synaptics_set_mode(sc, synaptics_preferred_mode(sc));
}

/* IBM/Lenovo TrackPoint */
static int
trackpoint_command(struct psm_softc *sc, int cmd, int loc, int val)
{
	const int seq[] = { 0xe2, cmd, loc, val };
	int i;

	if (sc->synhw.capPassthrough)
		synaptics_passthrough_on(sc);

	for (i = 0; i < nitems(seq); i++) {
		if (sc->synhw.capPassthrough &&
		    (seq[i] == 0xff || seq[i] == 0xe7))
			if (send_aux_command(sc->kbdc, 0xe7) != PSM_ACK) {
				synaptics_passthrough_off(sc);
				return (EIO);
			}
		if (send_aux_command(sc->kbdc, seq[i]) != PSM_ACK) {
			if (sc->synhw.capPassthrough)
				synaptics_passthrough_off(sc);
			return (EIO);
		}
	}

	if (sc->synhw.capPassthrough)
		synaptics_passthrough_off(sc);

	return (0);
}

#define	PSM_TPINFO(x)	offsetof(struct psm_softc, tpinfo.x)
#define	TPMASK		0
#define	TPLOC		1
#define	TPINFO		2

static int
trackpoint_sysctl(SYSCTL_HANDLER_ARGS)
{
	static const int data[][3] = {
		{ 0x00, 0x4a, PSM_TPINFO(sensitivity) },
		{ 0x00, 0x4d, PSM_TPINFO(inertia) },
		{ 0x00, 0x60, PSM_TPINFO(uplateau) },
		{ 0x00, 0x57, PSM_TPINFO(reach) },
		{ 0x00, 0x58, PSM_TPINFO(draghys) },
		{ 0x00, 0x59, PSM_TPINFO(mindrag) },
		{ 0x00, 0x5a, PSM_TPINFO(upthresh) },
		{ 0x00, 0x5c, PSM_TPINFO(threshold) },
		{ 0x00, 0x5d, PSM_TPINFO(jenks) },
		{ 0x00, 0x5e, PSM_TPINFO(ztime) },
		{ 0x01, 0x2c, PSM_TPINFO(pts) },
		{ 0x08, 0x2d, PSM_TPINFO(skipback) }
	};
	struct psm_softc *sc;
	int error, newval, *oldvalp;
	const int *tp;

	if (arg1 == NULL || arg2 < 0 || arg2 >= nitems(data))
		return (EINVAL);
	sc = arg1;
	tp = data[arg2];
	oldvalp = (int *)((intptr_t)sc + tp[TPINFO]);
	newval = *oldvalp;
	error = sysctl_handle_int(oidp, &newval, 0, req);
	if (error != 0)
		return (error);
	if (newval == *oldvalp)
		return (0);
	if (newval < 0 || newval > (tp[TPMASK] == 0 ? 255 : 1))
		return (EINVAL);
	error = trackpoint_command(sc, tp[TPMASK] == 0 ? 0x81 : 0x47,
	    tp[TPLOC], tp[TPMASK] == 0 ? newval : tp[TPMASK]);
	if (error != 0)
		return (error);
	*oldvalp = newval;

	return (0);
}

static void
trackpoint_sysctl_create_tree(struct psm_softc *sc)
{

	if (sc->tpinfo.sysctl_tree != NULL)
		return;

	/* Attach extra trackpoint sysctl nodes under hw.psm.trackpoint */
	sysctl_ctx_init(&sc->tpinfo.sysctl_ctx);
	sc->tpinfo.sysctl_tree = SYSCTL_ADD_NODE(&sc->tpinfo.sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_hw_psm), OID_AUTO, "trackpoint", CTLFLAG_RD,
	    0, "IBM/Lenovo TrackPoint");

	/* hw.psm.trackpoint.sensitivity */
	sc->tpinfo.sensitivity = 0x80;
	SYSCTL_ADD_PROC(&sc->tpinfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->tpinfo.sysctl_tree), OID_AUTO,
	    "sensitivity", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, TRACKPOINT_SYSCTL_SENSITIVITY,
	    trackpoint_sysctl, "I",
	    "Sensitivity");

	/* hw.psm.trackpoint.negative_inertia */
	sc->tpinfo.inertia = 0x06;
	SYSCTL_ADD_PROC(&sc->tpinfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->tpinfo.sysctl_tree), OID_AUTO,
	    "negative_inertia", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, TRACKPOINT_SYSCTL_NEGATIVE_INERTIA,
	    trackpoint_sysctl, "I",
	    "Negative inertia factor");

	/* hw.psm.trackpoint.upper_plateau */
	sc->tpinfo.uplateau = 0x61;
	SYSCTL_ADD_PROC(&sc->tpinfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->tpinfo.sysctl_tree), OID_AUTO,
	    "upper_plateau", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, TRACKPOINT_SYSCTL_UPPER_PLATEAU,
	    trackpoint_sysctl, "I",
	    "Transfer function upper plateau speed");

	/* hw.psm.trackpoint.backup_range */
	sc->tpinfo.reach = 0x0a;
	SYSCTL_ADD_PROC(&sc->tpinfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->tpinfo.sysctl_tree), OID_AUTO,
	    "backup_range", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, TRACKPOINT_SYSCTL_BACKUP_RANGE,
	    trackpoint_sysctl, "I",
	    "Backup range");

	/* hw.psm.trackpoint.drag_hysteresis */
	sc->tpinfo.draghys = 0xff;
	SYSCTL_ADD_PROC(&sc->tpinfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->tpinfo.sysctl_tree), OID_AUTO,
	    "drag_hysteresis", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, TRACKPOINT_SYSCTL_DRAG_HYSTERESIS,
	    trackpoint_sysctl, "I",
	    "Drag hysteresis");

	/* hw.psm.trackpoint.minimum_drag */
	sc->tpinfo.mindrag = 0x14;
	SYSCTL_ADD_PROC(&sc->tpinfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->tpinfo.sysctl_tree), OID_AUTO,
	    "minimum_drag", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, TRACKPOINT_SYSCTL_MINIMUM_DRAG,
	    trackpoint_sysctl, "I",
	    "Minimum drag");

	/* hw.psm.trackpoint.up_threshold */
	sc->tpinfo.upthresh = 0xff;
	SYSCTL_ADD_PROC(&sc->tpinfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->tpinfo.sysctl_tree), OID_AUTO,
	    "up_threshold", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, TRACKPOINT_SYSCTL_UP_THRESHOLD,
	    trackpoint_sysctl, "I",
	    "Up threshold for release");

	/* hw.psm.trackpoint.threshold */
	sc->tpinfo.threshold = 0x08;
	SYSCTL_ADD_PROC(&sc->tpinfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->tpinfo.sysctl_tree), OID_AUTO,
	    "threshold", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, TRACKPOINT_SYSCTL_THRESHOLD,
	    trackpoint_sysctl, "I",
	    "Threshold");

	/* hw.psm.trackpoint.jenks_curvature */
	sc->tpinfo.jenks = 0x87;
	SYSCTL_ADD_PROC(&sc->tpinfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->tpinfo.sysctl_tree), OID_AUTO,
	    "jenks_curvature", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, TRACKPOINT_SYSCTL_JENKS_CURVATURE,
	    trackpoint_sysctl, "I",
	    "Jenks curvature");

	/* hw.psm.trackpoint.z_time */
	sc->tpinfo.ztime = 0x26;
	SYSCTL_ADD_PROC(&sc->tpinfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->tpinfo.sysctl_tree), OID_AUTO,
	    "z_time", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, TRACKPOINT_SYSCTL_Z_TIME,
	    trackpoint_sysctl, "I",
	    "Z time constant");

	/* hw.psm.trackpoint.press_to_select */
	sc->tpinfo.pts = 0x00;
	SYSCTL_ADD_PROC(&sc->tpinfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->tpinfo.sysctl_tree), OID_AUTO,
	    "press_to_select", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, TRACKPOINT_SYSCTL_PRESS_TO_SELECT,
	    trackpoint_sysctl, "I",
	    "Press to Select");

	/* hw.psm.trackpoint.skip_backups */
	sc->tpinfo.skipback = 0x00;
	SYSCTL_ADD_PROC(&sc->tpinfo.sysctl_ctx,
	    SYSCTL_CHILDREN(sc->tpinfo.sysctl_tree), OID_AUTO,
	    "skip_backups", CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_ANYBODY,
	    sc, TRACKPOINT_SYSCTL_SKIP_BACKUPS,
	    trackpoint_sysctl, "I",
	    "Skip backups from drags");
}

static void
set_trackpoint_parameters(struct psm_softc *sc)
{
	trackpoint_command(sc, 0x81, 0x4a, sc->tpinfo.sensitivity);
	trackpoint_command(sc, 0x81, 0x60, sc->tpinfo.uplateau);
	trackpoint_command(sc, 0x81, 0x4d, sc->tpinfo.inertia);
	trackpoint_command(sc, 0x81, 0x57, sc->tpinfo.reach);
	trackpoint_command(sc, 0x81, 0x58, sc->tpinfo.draghys);
	trackpoint_command(sc, 0x81, 0x59, sc->tpinfo.mindrag);
	trackpoint_command(sc, 0x81, 0x5a, sc->tpinfo.upthresh);
	trackpoint_command(sc, 0x81, 0x5c, sc->tpinfo.threshold);
	trackpoint_command(sc, 0x81, 0x5d, sc->tpinfo.jenks);
	trackpoint_command(sc, 0x81, 0x5e, sc->tpinfo.ztime);
	if (sc->tpinfo.pts == 0x01)
		trackpoint_command(sc, 0x47, 0x2c, 0x01);
	if (sc->tpinfo.skipback == 0x01)
		trackpoint_command(sc, 0x47, 0x2d, 0x08);
}

static int
enable_trackpoint(struct psm_softc *sc, enum probearg arg)
{
	KBDC kbdc = sc->kbdc;
	int id;

	/*
	 * If called from enable_synaptics(), make sure that passthrough
	 * mode is enabled so we can reach the trackpoint.
	 * However, passthrough mode must be disabled before setting the
	 * trackpoint parameters, as rackpoint_command() enables and disables
	 * passthrough mode on its own.
	 */
	if (sc->synhw.capPassthrough)
		synaptics_passthrough_on(sc);

	if (send_aux_command(kbdc, 0xe1) != PSM_ACK ||
	    read_aux_data(kbdc) != 0x01)
		goto no_trackpoint;
	id = read_aux_data(kbdc);
	if (id < 0x01)
		goto no_trackpoint;
	if (arg == PROBE)
		sc->tphw = id;
	if (!trackpoint_support)
		goto no_trackpoint;

	if (sc->synhw.capPassthrough)
		synaptics_passthrough_off(sc);

	if (arg == PROBE) {
		trackpoint_sysctl_create_tree(sc);
		/*
		 * Don't overwrite hwid and buttons when we are
		 * a guest device.
		 */
		if (!sc->synhw.capPassthrough) {
			sc->hw.hwid = id;
			sc->hw.buttons = 3;
		}
	}

	set_trackpoint_parameters(sc);

	return (TRUE);

no_trackpoint:
	if (sc->synhw.capPassthrough)
		synaptics_passthrough_off(sc);

	return (FALSE);
}

/* Interlink electronics VersaPad */
static int
enable_versapad(struct psm_softc *sc, enum probearg arg)
{
	KBDC kbdc = sc->kbdc;
	int data[3];

	set_mouse_resolution(kbdc, PSMD_RES_MEDIUM_HIGH); /* set res. 2 */
	set_mouse_sampling_rate(kbdc, 100);		/* set rate 100 */
	set_mouse_scaling(kbdc, 1);			/* set scale 1:1 */
	set_mouse_scaling(kbdc, 1);			/* set scale 1:1 */
	set_mouse_scaling(kbdc, 1);			/* set scale 1:1 */
	set_mouse_scaling(kbdc, 1);			/* set scale 1:1 */
	if (get_mouse_status(kbdc, data, 0, 3) < 3)	/* get status */
		return (FALSE);
	if (data[2] != 0xa || data[1] != 0 )	/* rate == 0xa && res. == 0 */
		return (FALSE);
	set_mouse_scaling(kbdc, 1);			/* set scale 1:1 */

	return (TRUE);				/* PS/2 absolute mode */
}

/* Elantech Touchpad */
static int
elantech_read_1(KBDC kbdc, int hwversion, int reg, int *val)
{
	int res, readcmd, retidx;
	int resp[3];

	readcmd = hwversion == 2 ? ELANTECH_REG_READ : ELANTECH_REG_RDWR;
	retidx = hwversion == 4 ? 1 : 0;

	res = send_aux_command(kbdc, ELANTECH_CUSTOM_CMD) != PSM_ACK;
	res |= send_aux_command(kbdc, readcmd) != PSM_ACK;
	res |= send_aux_command(kbdc, ELANTECH_CUSTOM_CMD) != PSM_ACK;
	res |= send_aux_command(kbdc, reg) != PSM_ACK;
	res |= get_mouse_status(kbdc, resp, 0, 3) != 3;

	if (res == 0)
		*val = resp[retidx];

	return (res);
}

static int
elantech_write_1(KBDC kbdc, int hwversion, int reg, int val)
{
	int res, writecmd;

	writecmd = hwversion == 2 ? ELANTECH_REG_WRITE : ELANTECH_REG_RDWR;

	res = send_aux_command(kbdc, ELANTECH_CUSTOM_CMD) != PSM_ACK;
	res |= send_aux_command(kbdc, writecmd) != PSM_ACK;
	res |= send_aux_command(kbdc, ELANTECH_CUSTOM_CMD) != PSM_ACK;
	res |= send_aux_command(kbdc, reg) != PSM_ACK;
	if (hwversion == 4) {
		res |= send_aux_command(kbdc, ELANTECH_CUSTOM_CMD) != PSM_ACK;
		res |= send_aux_command(kbdc, writecmd) != PSM_ACK;
	}
	res |= send_aux_command(kbdc, ELANTECH_CUSTOM_CMD) != PSM_ACK;
	res |= send_aux_command(kbdc, val) != PSM_ACK;
	res |= set_mouse_scaling(kbdc, 1) == 0;

	return (res);
}

static int
elantech_cmd(KBDC kbdc, int hwversion, int cmd, int *resp)
{
	int res;

	if (hwversion == 2) {
		res = set_mouse_scaling(kbdc, 1) == 0;
		res |= mouse_ext_command(kbdc, cmd) == 0;
	} else {
		res = send_aux_command(kbdc, ELANTECH_CUSTOM_CMD) != PSM_ACK;
		res |= send_aux_command(kbdc, cmd) != PSM_ACK;
	}
	res |= get_mouse_status(kbdc, resp, 0, 3) != 3;

	return (res);
}

static int
elantech_init(KBDC kbdc, elantechhw_t *elanhw)
{
	int i, val, res, hwversion, reg10;

	/* set absolute mode */
	hwversion = elanhw->hwversion;
	reg10 = -1;
	switch (hwversion) {
	case 2:
		reg10 = elanhw->fwversion == 0x020030 ? 0x54 : 0xc4;
		res = elantech_write_1(kbdc, hwversion, 0x10, reg10);
		if (res)
			break;
		res = elantech_write_1(kbdc, hwversion, 0x11, 0x8A);
		break;
	case 3:
		reg10 = 0x0b;
		res = elantech_write_1(kbdc, hwversion, 0x10, reg10);
		break;
	case 4:
		res = elantech_write_1(kbdc, hwversion, 0x07, 0x01);
		break;
	default:
		res = 1;
	}

	/* Read back reg 0x10 to ensure hardware is ready. */
	if (res == 0 && reg10 >= 0) {
		for (i = 0; i < 5; i++) {
			if (elantech_read_1(kbdc, hwversion, 0x10, &val) == 0)
				break;
			DELAY(2000);
		}
		if (i == 5)
			res = 1;
	}

	if (res)
		printf("couldn't set absolute mode\n");

	return (res);
}

static void
elantech_init_synaptics(struct psm_softc *sc)
{

	/* Set capabilites required by movement smother */
	sc->synhw.infoMajor = sc->elanhw.hwversion;
	sc->synhw.infoMinor = sc->elanhw.fwversion;
	sc->synhw.infoXupmm = sc->elanhw.dpmmx;
	sc->synhw.infoYupmm = sc->elanhw.dpmmy;
	sc->synhw.verticalScroll = 0;
	sc->synhw.nExtendedQueries = 4;
	sc->synhw.capExtended = 1;
	sc->synhw.capPassthrough = sc->elanhw.hastrackpoint;
	sc->synhw.capClickPad = sc->elanhw.isclickpad;
	sc->synhw.capMultiFinger = 1;
	if (sc->elanhw.issemimt)
		sc->synhw.capAdvancedGestures = 1;
	else
		sc->synhw.capReportsV = 1;
	sc->synhw.capPalmDetect = 1;
	sc->synhw.capPen = 0;
	sc->synhw.capReportsMax = 1;
	sc->synhw.maximumXCoord = sc->elanhw.sizex;
	sc->synhw.maximumYCoord = sc->elanhw.sizey;
	sc->synhw.capReportsMin = 1;
	sc->synhw.minimumXCoord = 0;
	sc->synhw.minimumYCoord = 0;

	if (sc->syninfo.sysctl_tree == NULL) {
		synaptics_sysctl_create_tree(sc, "elantech",
		    "Elantech Touchpad");

		/*
		 * Adjust synaptic smoother tunables
		 * 1. Disable finger detection pressure threshold. Unlike
		 *    synaptics we assume the finger is acting when packet with
		 *    its X&Y arrives not when pressure exceedes some threshold
		 * 2. Disable unrelated features like margins and noisy areas
		 * 3. Disable virtual scroll areas as 2nd finger is preferable
		 * 4. For clickpads set bottom quarter as 42% - 16% - 42% sized
		 *    softbuttons
		 * 5. Scale down divisors and movement lengths by a factor of 3
		 *    where 3 is Synaptics to Elantech (~2200/800) dpi ratio
		 */

		/* Set reporting range to be equal touchpad size */
		sc->syninfo.max_x = sc->elanhw.sizex;
		sc->syninfo.max_y = sc->elanhw.sizey;

		/* Disable finger detection pressure threshold */
		sc->syninfo.min_pressure = 1;

		/* Adjust palm width to nearly match synaptics w=10 */
		sc->syninfo.max_width = 7;

		/* Elans often report double & triple taps as single event */
		sc->syninfo.tap_min_queue = 1;

		/* Use full area of touchpad */
		sc->syninfo.margin_top = 0;
		sc->syninfo.margin_right = 0;
		sc->syninfo.margin_bottom = 0;
		sc->syninfo.margin_left = 0;

		/* Disable noisy area */
		sc->syninfo.na_top = 0;
		sc->syninfo.na_right = 0;
		sc->syninfo.na_bottom = 0;
		sc->syninfo.na_left = 0;

		/* Tune divisors and movement lengths */
		sc->syninfo.weight_len_squared = 200;
		sc->syninfo.div_min = 3;
		sc->syninfo.div_max = 6;
		sc->syninfo.div_max_na = 10;
		sc->syninfo.div_len = 30;
		sc->syninfo.tap_max_delta = 25;

		/* Disable virtual scrolling areas and tune its divisors */
		sc->syninfo.vscroll_hor_area = 0;
		sc->syninfo.vscroll_ver_area = 0;
		sc->syninfo.vscroll_min_delta = 15;
		sc->syninfo.vscroll_div_min = 30;
		sc->syninfo.vscroll_div_max = 50;

		/* Set bottom quarter as 42% - 16% - 42% sized softbuttons */
		if (sc->elanhw.isclickpad) {
			sc->syninfo.softbuttons_y = sc->elanhw.sizey / 4;
			sc->syninfo.softbutton2_x = sc->elanhw.sizex * 11 / 25;
			sc->syninfo.softbutton3_x = sc->elanhw.sizex * 14 / 25;
		}
	}

	return;
}

static int
enable_elantech(struct psm_softc *sc, enum probearg arg)
{
	static const int ic2hw[] =
	/*IC: 0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f */
	    { 0, 0, 2, 0, 2, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0 };
	static const int fw_sizes[][3] = {
		/* FW.vers  MaxX  MaxY */
		{ 0x020030, 1152,  768 },
		{ 0x020800, 1152,  768 },
		{ 0x020b00, 1152,  768 },
		{ 0x040215,  900,  500 },
		{ 0x040216,  819,  405 },
		{ 0x040219,  900,  500 },
	};
	elantechhw_t elanhw;
	int icversion, hwversion, xtr, i, id, resp[3], dpix, dpiy;
	KBDC kbdc = sc->kbdc;

	VLOG(3, (LOG_DEBUG, "elantech: BEGIN init\n"));

	set_mouse_scaling(kbdc, 1);
	set_mouse_scaling(kbdc, 1);
	set_mouse_scaling(kbdc, 1);
	if (get_mouse_status(kbdc, resp, 0, 3) != 3)
		return (FALSE);

	if (!ELANTECH_MAGIC(resp))
		return (FALSE);

	/* Identify the Touchpad version. */
	if (elantech_cmd(kbdc, 2, ELANTECH_FW_VERSION, resp))
		return (FALSE);

	bzero(&elanhw, sizeof(elanhw));

	elanhw.fwversion = (resp[0] << 16) | (resp[1] << 8) | resp[2];
	icversion = resp[0] & 0x0f;
	hwversion = ic2hw[icversion];

	if (verbose >= 2)
		printf("Elantech touchpad hardware v.%d firmware v.0x%06x\n",
		    hwversion, elanhw.fwversion);

	if (ELANTECH_HW_IS_V1(elanhw.fwversion)) {
		printf ("  Unsupported touchpad hardware (v1)\n");
		return (FALSE);
	}
	if (hwversion == 0) {
		printf ("  Unknown touchpad hardware (firmware v.0x%06x)\n",
		    elanhw.fwversion);
		return (FALSE);
	}

	/* Get the Touchpad model information. */
	elanhw.hwversion = hwversion;
	elanhw.issemimt = hwversion == 2;
	elanhw.isclickpad = (resp[1] & 0x10) != 0;
	elanhw.hascrc = (resp[1] & 0x40) != 0;
	elanhw.haspressure = elanhw.fwversion >= 0x020800;

	/* Read the capability bits. */
	if (elantech_cmd(kbdc, hwversion, ELANTECH_CAPABILITIES, resp) != 0) {
		printf("  Failed to read capability bits\n");
		return (FALSE);
	}

	elanhw.ntracesx = imax(resp[1], 3);
	elanhw.ntracesy = imax(resp[2], 3);
	elanhw.hastrackpoint = (resp[0] & 0x80) != 0;

	/* Get the touchpad resolution */
	switch (hwversion) {
	case 4:
		if (elantech_cmd(kbdc, hwversion, ELANTECH_RESOLUTION, resp)
		    == 0) {
			dpix = (resp[1] & 0x0f) * 10 + 790;
			dpiy = ((resp[1] & 0xf0) >> 4) * 10 + 790;
			elanhw.dpmmx = (dpix * 10 + 5) / 254;
			elanhw.dpmmy = (dpiy * 10 + 5) / 254;
			break;
		}
		/* FALLTHROUGH */
	case 2:
	case 3:
		elanhw.dpmmx = elanhw.dpmmy = 32; /* 800 dpi */
		break;
	}

	if (!elantech_support)
		return (FALSE);

	if (elantech_init(kbdc, &elanhw)) {
		printf("couldn't initialize elantech touchpad\n");
		return (FALSE);
	}

	/*
	 * Get the touchpad reporting range.
	 * On HW v.3 touchpads it should be done after switching hardware
	 * to real resolution mode (by setting bit 3 of reg10)
	 */
	elanhw.dptracex = elanhw.dptracey = 64;
	for (i = 0; i < nitems(fw_sizes); i++) {
		if (elanhw.fwversion == fw_sizes[i][0]) {
			elanhw.sizex = fw_sizes[i][1];
			elanhw.sizey = fw_sizes[i][2];
			goto found;
		}
	}
	if (elantech_cmd(kbdc, hwversion, ELANTECH_FW_ID, resp) != 0) {
		printf("  Failed to read touchpad size\n");
		elanhw.sizex = 10000; /* Arbitrary high values to     */
		elanhw.sizey = 10000; /* prevent clipping in smoother */
	} else if (hwversion == 2) {
		if ((elanhw.fwversion >> 16) == 0x14 && (resp[1] & 0x10) &&
		    !elantech_cmd(kbdc, hwversion, ELANTECH_SAMPLE, resp)) {
			elanhw.dptracex = resp[1] / 2;
			elanhw.dptracey = resp[2] / 2;
		}
		xtr = ((elanhw.fwversion >> 8) == 0x0208) ? 1 : 2;
		elanhw.sizex = (elanhw.ntracesx - xtr) * elanhw.dptracex;
		elanhw.sizey = (elanhw.ntracesy - xtr) * elanhw.dptracey;
	} else {
		elanhw.sizex = (resp[0] & 0x0f) << 8 | resp[1];
		elanhw.sizey = (resp[0] & 0xf0) << 4 | resp[2];
		xtr = (elanhw.sizex % (elanhw.ntracesx - 2) == 0) ? 2 : 1;
		elanhw.dptracex = elanhw.sizex / (elanhw.ntracesx - xtr);
		elanhw.dptracey = elanhw.sizey / (elanhw.ntracesy - xtr);
	}
found:
	if (verbose >= 2) {
		printf("  Model information:\n");
		printf("   MaxX:       %d\n", elanhw.sizex);
		printf("   MaxY:       %d\n", elanhw.sizey);
		printf("   DpmmX:      %d\n", elanhw.dpmmx);
		printf("   DpmmY:      %d\n", elanhw.dpmmy);
		printf("   TracesX:    %d\n", elanhw.ntracesx);
		printf("   TracesY:    %d\n", elanhw.ntracesy);
		printf("   DptraceX:   %d\n", elanhw.dptracex);
		printf("   DptraceY:   %d\n", elanhw.dptracey);
		printf("   SemiMT:     %d\n", elanhw.issemimt);
		printf("   Clickpad:   %d\n", elanhw.isclickpad);
		printf("   Trackpoint: %d\n", elanhw.hastrackpoint);
		printf("   CRC:        %d\n", elanhw.hascrc);
		printf("   Pressure:   %d\n", elanhw.haspressure);
	}

	VLOG(3, (LOG_DEBUG, "elantech: END init\n"));

	if (arg == PROBE) {
		sc->elanhw = elanhw;
		sc->hw.buttons = 3;

		/* Initialize synaptics movement smoother */
		elantech_init_synaptics(sc);

		for (id = 0; id < ELANTECH_MAX_FINGERS; id++)
			PSM_FINGER_RESET(sc->elanaction.fingers[id]);
	}

	return (TRUE);
}

/*
 * Return true if 'now' is earlier than (start + (secs.usecs)).
 * Now may be NULL and the function will fetch the current time from
 * getmicrouptime(), or a cached 'now' can be passed in.
 * All values should be numbers derived from getmicrouptime().
 */
static int
timeelapsed(start, secs, usecs, now)
	const struct timeval *start, *now;
	int secs, usecs;
{
	struct timeval snow, tv;

	/* if there is no 'now' passed in, the get it as a convience. */
	if (now == NULL) {
		getmicrouptime(&snow);
		now = &snow;
	}

	tv.tv_sec = secs;
	tv.tv_usec = usecs;
	timevaladd(&tv, start);
	return (timevalcmp(&tv, now, <));
}

static int
psmresume(device_t dev)
{
	struct psm_softc *sc = device_get_softc(dev);
	int unit = device_get_unit(dev);
	int err;

	VLOG(2, (LOG_NOTICE, "psm%d: system resume hook called.\n", unit));

	if ((sc->config &
	    (PSM_CONFIG_HOOKRESUME | PSM_CONFIG_INITAFTERSUSPEND)) == 0)
		return (0);

	err = reinitialize(sc, sc->config & PSM_CONFIG_INITAFTERSUSPEND);

	if ((sc->state & PSM_ASLP) && !(sc->state & PSM_VALID)) {
		/*
		 * Release the blocked process; it must be notified that
		 * the device cannot be accessed anymore.
		 */
		sc->state &= ~PSM_ASLP;
		wakeup(sc);
	}

	VLOG(2, (LOG_DEBUG, "psm%d: system resume hook exiting.\n", unit));

	return (err);
}

DRIVER_MODULE(psm, atkbdc, psm_driver, psm_devclass, 0, 0);
#ifdef EVDEV_SUPPORT
MODULE_DEPEND(psm, evdev, 1, 1, 1);
#endif

#ifdef DEV_ISA

/*
 * This sucks up assignments from PNPBIOS and ACPI.
 */

/*
 * When the PS/2 mouse device is reported by ACPI or PnP BIOS, it may
 * appear BEFORE the AT keyboard controller.  As the PS/2 mouse device
 * can be probed and attached only after the AT keyboard controller is
 * attached, we shall quietly reserve the IRQ resource for later use.
 * If the PS/2 mouse device is reported to us AFTER the keyboard controller,
 * copy the IRQ resource to the PS/2 mouse device instance hanging
 * under the keyboard controller, then probe and attach it.
 */

static	devclass_t			psmcpnp_devclass;

static	device_probe_t			psmcpnp_probe;
static	device_attach_t			psmcpnp_attach;

static device_method_t psmcpnp_methods[] = {
	DEVMETHOD(device_probe,		psmcpnp_probe),
	DEVMETHOD(device_attach,	psmcpnp_attach),

	{ 0, 0 }
};

static driver_t psmcpnp_driver = {
	PSMCPNP_DRIVER_NAME,
	psmcpnp_methods,
	sizeof(struct psmcpnp_softc),
};

static struct isa_pnp_id psmcpnp_ids[] = {
	{ 0x030fd041, "PS/2 mouse port" },		/* PNP0F03 */
	{ 0x0e0fd041, "PS/2 mouse port" },		/* PNP0F0E */
	{ 0x120fd041, "PS/2 mouse port" },		/* PNP0F12 */
	{ 0x130fd041, "PS/2 mouse port" },		/* PNP0F13 */
	{ 0x1303d041, "PS/2 port" },			/* PNP0313, XXX */
	{ 0x02002e4f, "Dell PS/2 mouse port" },		/* Lat. X200, Dell */
	{ 0x0002a906, "ALPS Glide Point" },		/* ALPS Glide Point */
	{ 0x80374d24, "IBM PS/2 mouse port" },		/* IBM3780, ThinkPad */
	{ 0x81374d24, "IBM PS/2 mouse port" },		/* IBM3781, ThinkPad */
	{ 0x0190d94d, "SONY VAIO PS/2 mouse port"},     /* SNY9001, Vaio */
	{ 0x0290d94d, "SONY VAIO PS/2 mouse port"},	/* SNY9002, Vaio */
	{ 0x0390d94d, "SONY VAIO PS/2 mouse port"},	/* SNY9003, Vaio */
	{ 0x0490d94d, "SONY VAIO PS/2 mouse port"},     /* SNY9004, Vaio */
	{ 0 }
};

/* _HID list for quirk detection. Any device below has _CID from psmcpnp_ids */
static struct isa_pnp_id topbtpad_ids[] = {
	{ 0x1700ae30, "Lenovo PS/2 clickpad port" },	/* LEN0017, ThinkPad */
	{ 0x1800ae30, "Lenovo PS/2 clickpad port" },	/* LEN0018, ThinkPad */
	{ 0x1900ae30, "Lenovo PS/2 clickpad port" },	/* LEN0019, ThinkPad */
	{ 0x2300ae30, "Lenovo PS/2 clickpad port" },	/* LEN0023, ThinkPad */
	{ 0x2a00ae30, "Lenovo PS/2 clickpad port" },	/* LEN002a, ThinkPad */
	{ 0x2b00ae30, "Lenovo PS/2 clickpad port" },	/* LEN002b, ThinkPad */
	{ 0x2c00ae30, "Lenovo PS/2 clickpad port" },	/* LEN002c, ThinkPad */
	{ 0x2d00ae30, "Lenovo PS/2 clickpad port" },	/* LEN002d, ThinkPad */
	{ 0x2e00ae30, "Lenovo PS/2 clickpad port" },	/* LEN002e, ThinkPad */
	{ 0x3300ae30, "Lenovo PS/2 clickpad port" },	/* LEN0033, ThinkPad */
	{ 0x3400ae30, "Lenovo PS/2 clickpad port" },	/* LEN0034, ThinkPad */
	{ 0x3500ae30, "Lenovo PS/2 clickpad port" },	/* LEN0035, ThinkPad */
	{ 0x3600ae30, "Lenovo PS/2 clickpad port" },	/* LEN0036, ThinkPad */
	{ 0x3700ae30, "Lenovo PS/2 clickpad port" },	/* LEN0037, ThinkPad */
	{ 0x3800ae30, "Lenovo PS/2 clickpad port" },	/* LEN0038, ThinkPad */
	{ 0x3900ae30, "Lenovo PS/2 clickpad port" },	/* LEN0039, ThinkPad */
	{ 0x4100ae30, "Lenovo PS/2 clickpad port" },	/* LEN0041, ThinkPad */
	{ 0x4200ae30, "Lenovo PS/2 clickpad port" },	/* LEN0042, ThinkPad */
	{ 0x4500ae30, "Lenovo PS/2 clickpad port" },	/* LEN0045, ThinkPad */
	{ 0x4700ae30, "Lenovo PS/2 clickpad port" },	/* LEN0047, ThinkPad */
	{ 0x4900ae30, "Lenovo PS/2 clickpad port" },	/* LEN0049, ThinkPad */
	{ 0x0020ae30, "Lenovo PS/2 clickpad port" },	/* LEN2000, ThinkPad */
	{ 0x0120ae30, "Lenovo PS/2 clickpad port" },	/* LEN2001, ThinkPad */
	{ 0x0220ae30, "Lenovo PS/2 clickpad port" },	/* LEN2002, ThinkPad */
	{ 0x0320ae30, "Lenovo PS/2 clickpad port" },	/* LEN2003, ThinkPad */
	{ 0x0420ae30, "Lenovo PS/2 clickpad port" },	/* LEN2004, ThinkPad */
	{ 0x0520ae30, "Lenovo PS/2 clickpad port" },	/* LEN2005, ThinkPad */
	{ 0x0620ae30, "Lenovo PS/2 clickpad port" },	/* LEN2006, ThinkPad */
	{ 0x0720ae30, "Lenovo PS/2 clickpad port" },	/* LEN2007, ThinkPad */
	{ 0x0820ae30, "Lenovo PS/2 clickpad port" },	/* LEN2008, ThinkPad */
	{ 0x0920ae30, "Lenovo PS/2 clickpad port" },	/* LEN2009, ThinkPad */
	{ 0x0a20ae30, "Lenovo PS/2 clickpad port" },	/* LEN200a, ThinkPad */
	{ 0x0b20ae30, "Lenovo PS/2 clickpad port" },	/* LEN200b, ThinkPad */
	{ 0 }
};

/* _HID list for quirk detection. Any device below has _CID from psmcpnp_ids */
static struct isa_pnp_id forcepad_ids[] = {
	{ 0x0d302e4f, "HP PS/2 forcepad port" },	/* SYN300D, EB 1040 */
	{ 0x14302e4f, "HP PS/2 forcepad port" },	/* SYN3014, EB 1040 */
	{ 0 }
};

static int
create_a_copy(device_t atkbdc, device_t me)
{
	device_t psm;
	u_long irq;

	/* find the PS/2 mouse device instance under the keyboard controller */
	psm = device_find_child(atkbdc, PSM_DRIVER_NAME,
	    device_get_unit(atkbdc));
	if (psm == NULL)
		return (ENXIO);
	if (device_get_state(psm) != DS_NOTPRESENT)
		return (0);

	/* move our resource to the found device */
	irq = bus_get_resource_start(me, SYS_RES_IRQ, 0);
	bus_delete_resource(me, SYS_RES_IRQ, 0);
	bus_set_resource(psm, SYS_RES_IRQ, KBDC_RID_AUX, irq, 1);

	/* ...then probe and attach it */
	return (device_probe_and_attach(psm));
}

static int
psmcpnp_probe(device_t dev)
{
	struct psmcpnp_softc *sc = device_get_softc(dev);
	struct resource *res;
	u_long irq;
	int rid;

	if (ISA_PNP_PROBE(device_get_parent(dev), dev, forcepad_ids) == 0)
		sc->type = PSMCPNP_FORCEPAD;
	else if (ISA_PNP_PROBE(device_get_parent(dev), dev, topbtpad_ids) == 0)
		sc->type = PSMCPNP_TOPBUTTONPAD;
	else if (ISA_PNP_PROBE(device_get_parent(dev), dev, psmcpnp_ids) == 0)
		sc->type = PSMCPNP_GENERIC;
	else
		return (ENXIO);

	/*
	 * The PnP BIOS and ACPI are supposed to assign an IRQ (12)
	 * to the PS/2 mouse device node. But, some buggy PnP BIOS
	 * declares the PS/2 mouse device node without an IRQ resource!
	 * If this happens, we shall refer to device hints.
	 * If we still don't find it there, use a hardcoded value... XXX
	 */
	rid = 0;
	irq = bus_get_resource_start(dev, SYS_RES_IRQ, rid);
	if (irq <= 0) {
		if (resource_long_value(PSM_DRIVER_NAME,
		    device_get_unit(dev),"irq", &irq) != 0)
			irq = 12;	/* XXX */
		device_printf(dev, "irq resource info is missing; "
		    "assuming irq %ld\n", irq);
		bus_set_resource(dev, SYS_RES_IRQ, rid, irq, 1);
	}
	res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, 0);
	bus_release_resource(dev, SYS_RES_IRQ, rid, res);

	/* keep quiet */
	if (!bootverbose)
		device_quiet(dev);

	return ((res == NULL) ? ENXIO : 0);
}

static int
psmcpnp_attach(device_t dev)
{
	device_t atkbdc;

	/* find the keyboard controller, which may be on acpi* or isa* bus */
	atkbdc = devclass_get_device(devclass_find(ATKBDC_DRIVER_NAME),
	    device_get_unit(dev));
	if ((atkbdc != NULL) && (device_get_state(atkbdc) == DS_ATTACHED))
		create_a_copy(atkbdc, dev);

	return (0);
}

DRIVER_MODULE(psmcpnp, isa, psmcpnp_driver, psmcpnp_devclass, 0, 0);
DRIVER_MODULE(psmcpnp, acpi, psmcpnp_driver, psmcpnp_devclass, 0, 0);
ISA_PNP_INFO(psmcpnp_ids);
#endif /* DEV_ISA */
