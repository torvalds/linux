/**
 ** SPDX-License-Identifier: BSD-4-Clause
 **
 ** Copyright (c) 1995 Michael Smith, All rights reserved.
 **
 ** Redistribution and use in source and binary forms, with or without
 ** modification, are permitted provided that the following conditions
 ** are met:
 ** 1. Redistributions of source code must retain the above copyright
 **    notice, this list of conditions and the following disclaimer as
 **    the first lines of this file unmodified.
 ** 2. Redistributions in binary form must reproduce the above copyright
 **    notice, this list of conditions and the following disclaimer in the
 **    documentation and/or other materials provided with the distribution.
 ** 3. All advertising materials mentioning features or use of this software
 **    must display the following acknowledgment:
 **      This product includes software developed by Michael Smith.
 ** 4. The name of the author may not be used to endorse or promote products
 **    derived from this software without specific prior written permission.
 **
 **
 ** THIS SOFTWARE IS PROVIDED BY Michael Smith ``AS IS'' AND ANY
 ** EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 ** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 ** PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL Michael Smith BE LIABLE FOR
 ** ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 ** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 ** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 ** BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 ** WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 ** OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 ** EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **
 **/

/**
 ** MOUSED.C
 **
 ** Mouse daemon : listens to a serial port, the bus mouse interface, or
 ** the PS/2 mouse port for mouse data stream, interprets data and passes
 ** ioctls off to the console driver.
 **
 ** The mouse interface functions are derived closely from the mouse
 ** handler in the XFree86 X server.  Many thanks to the XFree86 people
 ** for their great work!
 **
 **/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/consio.h>
#include <sys/mouse.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <termios.h>
#include <unistd.h>
#include <math.h>

#define MAX_CLICKTHRESHOLD	2000	/* 2 seconds */
#define MAX_BUTTON2TIMEOUT	2000	/* 2 seconds */
#define DFLT_CLICKTHRESHOLD	 500	/* 0.5 second */
#define DFLT_BUTTON2TIMEOUT	 100	/* 0.1 second */
#define DFLT_SCROLLTHRESHOLD	   3	/* 3 pixels */
#define DFLT_SCROLLSPEED	   2	/* 2 pixels */

/* Abort 3-button emulation delay after this many movement events. */
#define BUTTON2_MAXMOVE	3

#define TRUE		1
#define FALSE		0

#define MOUSE_XAXIS	(-1)
#define MOUSE_YAXIS	(-2)

/* Logitech PS2++ protocol */
#define MOUSE_PS2PLUS_CHECKBITS(b)	\
			((((b[2] & 0x03) << 2) | 0x02) == (b[1] & 0x0f))
#define MOUSE_PS2PLUS_PACKET_TYPE(b)	\
			(((b[0] & 0x30) >> 2) | ((b[1] & 0x30) >> 4))

#define	ChordMiddle	0x0001
#define Emulate3Button	0x0002
#define ClearDTR	0x0004
#define ClearRTS	0x0008
#define NoPnP		0x0010
#define VirtualScroll	0x0020
#define HVirtualScroll	0x0040
#define ExponentialAcc	0x0080

#define ID_NONE		0
#define ID_PORT		1
#define ID_IF		2
#define ID_TYPE		4
#define ID_MODEL	8
#define ID_ALL		(ID_PORT | ID_IF | ID_TYPE | ID_MODEL)

/* Operations on timespecs */
#define	tsclr(tvp)	((tvp)->tv_sec = (tvp)->tv_nsec = 0)
#define	tscmp(tvp, uvp, cmp)						\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_nsec cmp (uvp)->tv_nsec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))
#define	tssub(tvp, uvp, vvp)						\
	do {								\
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;		\
		(vvp)->tv_nsec = (tvp)->tv_nsec - (uvp)->tv_nsec;	\
		if ((vvp)->tv_nsec < 0) {				\
			(vvp)->tv_sec--;				\
			(vvp)->tv_nsec += 1000000000;			\
		}							\
	} while (0)

#define debug(...) do {						\
	if (debug && nodaemon)					\
		warnx(__VA_ARGS__);				\
} while (0)

#define logerr(e, ...) do {					\
	log_or_warn(LOG_DAEMON | LOG_ERR, errno, __VA_ARGS__);	\
	exit(e);						\
} while (0)

#define logerrx(e, ...) do {					\
	log_or_warn(LOG_DAEMON | LOG_ERR, 0, __VA_ARGS__);	\
	exit(e);						\
} while (0)

#define logwarn(...)						\
	log_or_warn(LOG_DAEMON | LOG_WARNING, errno, __VA_ARGS__)

#define logwarnx(...)						\
	log_or_warn(LOG_DAEMON | LOG_WARNING, 0, __VA_ARGS__)

/* structures */

/* symbol table entry */
typedef struct {
    const char *name;
    int val;
    int val2;
} symtab_t;

/* serial PnP ID string */
typedef struct {
    int revision;	/* PnP revision, 100 for 1.00 */
    const char *eisaid;	/* EISA ID including mfr ID and product ID */
    char *serial;	/* serial No, optional */
    const char *class;	/* device class, optional */
    char *compat;	/* list of compatible drivers, optional */
    char *description;	/* product description, optional */
    int neisaid;	/* length of the above fields... */
    int nserial;
    int nclass;
    int ncompat;
    int ndescription;
} pnpid_t;

/* global variables */

static int	debug = 0;
static int	nodaemon = FALSE;
static int	background = FALSE;
static int	paused = FALSE;
static int	identify = ID_NONE;
static int	extioctl = FALSE;
static const char *pidfile = "/var/run/moused.pid";
static struct pidfh *pfh;

#define SCROLL_NOTSCROLLING	0
#define SCROLL_PREPARE		1
#define SCROLL_SCROLLING	2

static int	scroll_state;
static int	scroll_movement;
static int	hscroll_movement;

/* local variables */

/* interface (the table must be ordered by MOUSE_IF_XXX in mouse.h) */
static symtab_t rifs[] = {
    { "serial",		MOUSE_IF_SERIAL,	0 },
    { "ps/2",		MOUSE_IF_PS2,		0 },
    { "sysmouse",	MOUSE_IF_SYSMOUSE,	0 },
    { "usb",		MOUSE_IF_USB,		0 },
    { NULL,		MOUSE_IF_UNKNOWN,	0 },
};

/* types (the table must be ordered by MOUSE_PROTO_XXX in mouse.h) */
static const char *rnames[] = {
    "microsoft",
    "mousesystems",
    "logitech",
    "mmseries",
    "mouseman",
    "wasbusmouse",
    "wasinportmouse",
    "ps/2",
    "mmhitab",
    "glidepoint",
    "intellimouse",
    "thinkingmouse",
    "sysmouse",
    "x10mouseremote",
    "kidspad",
    "versapad",
    "jogdial",
#if notyet
    "mariqua",
#endif
    "gtco_digipad",
    NULL
};

/* models */
static symtab_t	rmodels[] = {
    { "NetScroll",		MOUSE_MODEL_NETSCROLL,		0 },
    { "NetMouse/NetScroll Optical", MOUSE_MODEL_NET,		0 },
    { "GlidePoint",		MOUSE_MODEL_GLIDEPOINT,		0 },
    { "ThinkingMouse",		MOUSE_MODEL_THINK,		0 },
    { "IntelliMouse",		MOUSE_MODEL_INTELLI,		0 },
    { "EasyScroll/SmartScroll",	MOUSE_MODEL_EASYSCROLL,		0 },
    { "MouseMan+",		MOUSE_MODEL_MOUSEMANPLUS,	0 },
    { "Kidspad",		MOUSE_MODEL_KIDSPAD,		0 },
    { "VersaPad",		MOUSE_MODEL_VERSAPAD,		0 },
    { "IntelliMouse Explorer",	MOUSE_MODEL_EXPLORER,		0 },
    { "4D Mouse",		MOUSE_MODEL_4D,			0 },
    { "4D+ Mouse",		MOUSE_MODEL_4DPLUS,		0 },
    { "Synaptics Touchpad",	MOUSE_MODEL_SYNAPTICS,		0 },
    { "TrackPoint",		MOUSE_MODEL_TRACKPOINT,		0 },
    { "Elantech Touchpad",	MOUSE_MODEL_ELANTECH,		0 },
    { "generic",		MOUSE_MODEL_GENERIC,		0 },
    { NULL,			MOUSE_MODEL_UNKNOWN,		0 },
};

/* PnP EISA/product IDs */
static symtab_t pnpprod[] = {
    /* Kensignton ThinkingMouse */
    { "KML0001",	MOUSE_PROTO_THINK,	MOUSE_MODEL_THINK },
    /* MS IntelliMouse */
    { "MSH0001",	MOUSE_PROTO_INTELLI,	MOUSE_MODEL_INTELLI },
    /* MS IntelliMouse TrackBall */
    { "MSH0004",	MOUSE_PROTO_INTELLI,	MOUSE_MODEL_INTELLI },
    /* Tremon Wheel Mouse MUSD */
    { "HTK0001",        MOUSE_PROTO_INTELLI,    MOUSE_MODEL_INTELLI },
    /* Genius PnP Mouse */
    { "KYE0001",	MOUSE_PROTO_MS,		MOUSE_MODEL_GENERIC },
    /* MouseSystems SmartScroll Mouse (OEM from Genius?) */
    { "KYE0002",	MOUSE_PROTO_MS,		MOUSE_MODEL_EASYSCROLL },
    /* Genius NetMouse */
    { "KYE0003",	MOUSE_PROTO_INTELLI,	MOUSE_MODEL_NET },
    /* Genius Kidspad, Easypad and other tablets */
    { "KYE0005",	MOUSE_PROTO_KIDSPAD,	MOUSE_MODEL_KIDSPAD },
    /* Genius EZScroll */
    { "KYEEZ00",	MOUSE_PROTO_MS,		MOUSE_MODEL_EASYSCROLL },
    /* Logitech Cordless MouseMan Wheel */
    { "LGI8033",	MOUSE_PROTO_INTELLI,	MOUSE_MODEL_MOUSEMANPLUS },
    /* Logitech MouseMan (new 4 button model) */
    { "LGI800C",	MOUSE_PROTO_INTELLI,	MOUSE_MODEL_MOUSEMANPLUS },
    /* Logitech MouseMan+ */
    { "LGI8050",	MOUSE_PROTO_INTELLI,	MOUSE_MODEL_MOUSEMANPLUS },
    /* Logitech FirstMouse+ */
    { "LGI8051",	MOUSE_PROTO_INTELLI,	MOUSE_MODEL_MOUSEMANPLUS },
    /* Logitech serial */
    { "LGI8001",	MOUSE_PROTO_LOGIMOUSEMAN, MOUSE_MODEL_GENERIC },
    /* A4 Tech 4D/4D+ Mouse */
    { "A4W0005",	MOUSE_PROTO_INTELLI,	MOUSE_MODEL_4D },
    /* 8D Scroll Mouse */
    { "PEC9802",	MOUSE_PROTO_INTELLI,	MOUSE_MODEL_INTELLI },
    /* Mitsumi Wireless Scroll Mouse */
    { "MTM6401",	MOUSE_PROTO_INTELLI,	MOUSE_MODEL_INTELLI },

    /* MS serial */
    { "PNP0F01",	MOUSE_PROTO_MS,		MOUSE_MODEL_GENERIC },
    /* MS PS/2 */
    { "PNP0F03",	MOUSE_PROTO_PS2,	MOUSE_MODEL_GENERIC },
    /*
     * EzScroll returns PNP0F04 in the compatible device field; but it
     * doesn't look compatible... XXX
     */
    /* MouseSystems */
    { "PNP0F04",	MOUSE_PROTO_MSC,	MOUSE_MODEL_GENERIC },
    /* MouseSystems */
    { "PNP0F05",	MOUSE_PROTO_MSC,	MOUSE_MODEL_GENERIC },
#if notyet
    /* Genius Mouse */
    { "PNP0F06",	MOUSE_PROTO_XXX,	MOUSE_MODEL_GENERIC },
    /* Genius Mouse */
    { "PNP0F07",	MOUSE_PROTO_XXX,	MOUSE_MODEL_GENERIC },
#endif
    /* Logitech serial */
    { "PNP0F08",	MOUSE_PROTO_LOGIMOUSEMAN, MOUSE_MODEL_GENERIC },
    /* MS BallPoint serial */
    { "PNP0F09",	MOUSE_PROTO_MS,		MOUSE_MODEL_GENERIC },
    /* MS PnP serial */
    { "PNP0F0A",	MOUSE_PROTO_MS,		MOUSE_MODEL_GENERIC },
    /* MS PnP BallPoint serial */
    { "PNP0F0B",	MOUSE_PROTO_MS,		MOUSE_MODEL_GENERIC },
    /* MS serial comatible */
    { "PNP0F0C",	MOUSE_PROTO_MS,		MOUSE_MODEL_GENERIC },
    /* MS PS/2 comatible */
    { "PNP0F0E",	MOUSE_PROTO_PS2,	MOUSE_MODEL_GENERIC },
    /* MS BallPoint comatible */
    { "PNP0F0F",	MOUSE_PROTO_MS,		MOUSE_MODEL_GENERIC },
#if notyet
    /* TI QuickPort */
    { "PNP0F10",	MOUSE_PROTO_XXX,	MOUSE_MODEL_GENERIC },
#endif
    /* Logitech PS/2 */
    { "PNP0F12",	MOUSE_PROTO_PS2,	MOUSE_MODEL_GENERIC },
    /* PS/2 */
    { "PNP0F13",	MOUSE_PROTO_PS2,	MOUSE_MODEL_GENERIC },
#if notyet
    /* MS Kids Mouse */
    { "PNP0F14",	MOUSE_PROTO_XXX,	MOUSE_MODEL_GENERIC },
#endif
#if notyet
    /* Logitech SWIFT */
    { "PNP0F16",	MOUSE_PROTO_XXX,	MOUSE_MODEL_GENERIC },
#endif
    /* Logitech serial compat */
    { "PNP0F17",	MOUSE_PROTO_LOGIMOUSEMAN, MOUSE_MODEL_GENERIC },
    /* Logitech PS/2 compatible */
    { "PNP0F19",	MOUSE_PROTO_PS2,	MOUSE_MODEL_GENERIC },
#if notyet
    /* Logitech SWIFT compatible */
    { "PNP0F1A",	MOUSE_PROTO_XXX,	MOUSE_MODEL_GENERIC },
    /* HP Omnibook */
    { "PNP0F1B",	MOUSE_PROTO_XXX,	MOUSE_MODEL_GENERIC },
    /* Compaq LTE TrackBall PS/2 */
    { "PNP0F1C",	MOUSE_PROTO_XXX,	MOUSE_MODEL_GENERIC },
    /* Compaq LTE TrackBall serial */
    { "PNP0F1D",	MOUSE_PROTO_XXX,	MOUSE_MODEL_GENERIC },
    /* MS Kidts Trackball */
    { "PNP0F1E",	MOUSE_PROTO_XXX,	MOUSE_MODEL_GENERIC },
#endif
    /* Interlink VersaPad */
    { "LNK0001",	MOUSE_PROTO_VERSAPAD,	MOUSE_MODEL_VERSAPAD },

    { NULL,		MOUSE_PROTO_UNKNOWN,	MOUSE_MODEL_GENERIC },
};

/* the table must be ordered by MOUSE_PROTO_XXX in mouse.h */
static unsigned short rodentcflags[] =
{
    (CS7	           | CREAD | CLOCAL | HUPCL),	/* MicroSoft */
    (CS8 | CSTOPB	   | CREAD | CLOCAL | HUPCL),	/* MouseSystems */
    (CS8 | CSTOPB	   | CREAD | CLOCAL | HUPCL),	/* Logitech */
    (CS8 | PARENB | PARODD | CREAD | CLOCAL | HUPCL),	/* MMSeries */
    (CS7		   | CREAD | CLOCAL | HUPCL),	/* MouseMan */
    0,							/* Bus */
    0,							/* InPort */
    0,							/* PS/2 */
    (CS8		   | CREAD | CLOCAL | HUPCL),	/* MM HitTablet */
    (CS7	           | CREAD | CLOCAL | HUPCL),	/* GlidePoint */
    (CS7                   | CREAD | CLOCAL | HUPCL),	/* IntelliMouse */
    (CS7                   | CREAD | CLOCAL | HUPCL),	/* Thinking Mouse */
    (CS8 | CSTOPB	   | CREAD | CLOCAL | HUPCL),	/* sysmouse */
    (CS7	           | CREAD | CLOCAL | HUPCL),	/* X10 MouseRemote */
    (CS8 | PARENB | PARODD | CREAD | CLOCAL | HUPCL),	/* kidspad etc. */
    (CS8		   | CREAD | CLOCAL | HUPCL),	/* VersaPad */
    0,							/* JogDial */
#if notyet
    (CS8 | CSTOPB	   | CREAD | CLOCAL | HUPCL),	/* Mariqua */
#endif
    (CS8		   | CREAD |	      HUPCL ),	/* GTCO Digi-Pad */
};

static struct rodentparam {
    int flags;
    const char *portname;	/* /dev/XXX */
    int rtype;			/* MOUSE_PROTO_XXX */
    int level;			/* operation level: 0 or greater */
    int baudrate;
    int rate;			/* report rate */
    int resolution;		/* MOUSE_RES_XXX or a positive number */
    int zmap[4];		/* MOUSE_{X|Y}AXIS or a button number */
    int wmode;			/* wheel mode button number */
    int mfd;			/* mouse file descriptor */
    int cfd;			/* /dev/consolectl file descriptor */
    int mremsfd;		/* mouse remote server file descriptor */
    int mremcfd;		/* mouse remote client file descriptor */
    int is_removable;		/* set if device is removable, like USB */
    long clickthreshold;	/* double click speed in msec */
    long button2timeout;	/* 3 button emulation timeout */
    mousehw_t hw;		/* mouse device hardware information */
    mousemode_t mode;		/* protocol information */
    float accelx;		/* Acceleration in the X axis */
    float accely;		/* Acceleration in the Y axis */
    float expoaccel;		/* Exponential acceleration */
    float expoffset;		/* Movement offset for exponential accel. */
    float remainx;		/* Remainder on X and Y axis, respectively... */
    float remainy;		/*    ... to compensate for rounding errors. */
    int scrollthreshold;	/* Movement distance before virtual scrolling */
    int scrollspeed;		/* Movement distance to rate of scrolling */
} rodent = {
    .flags = 0,
    .portname = NULL,
    .rtype = MOUSE_PROTO_UNKNOWN,
    .level = -1,
    .baudrate = 1200,
    .rate = 0,
    .resolution = MOUSE_RES_UNKNOWN,
    .zmap = { 0, 0, 0, 0 },
    .wmode = 0,
    .mfd = -1,
    .cfd = -1,
    .mremsfd = -1,
    .mremcfd = -1,
    .is_removable = 0,
    .clickthreshold = DFLT_CLICKTHRESHOLD,
    .button2timeout = DFLT_BUTTON2TIMEOUT,
    .accelx = 1.0,
    .accely = 1.0,
    .expoaccel = 1.0,
    .expoffset = 1.0,
    .remainx = 0.0,
    .remainy = 0.0,
    .scrollthreshold = DFLT_SCROLLTHRESHOLD,
    .scrollspeed = DFLT_SCROLLSPEED,
};

/* button status */
struct button_state {
    int count;		/* 0: up, 1: single click, 2: double click,... */
    struct timespec ts;	/* timestamp on the last button event */
};
static struct button_state	bstate[MOUSE_MAXBUTTON];	/* button state */
static struct button_state	*mstate[MOUSE_MAXBUTTON];/* mapped button st.*/
static struct button_state	zstate[4];		 /* Z/W axis state */

/* state machine for 3 button emulation */

#define S0	0	/* start */
#define S1	1	/* button 1 delayed down */
#define S2	2	/* button 3 delayed down */
#define S3	3	/* both buttons down -> button 2 down */
#define S4	4	/* button 1 delayed up */
#define S5	5	/* button 1 down */
#define S6	6	/* button 3 down */
#define S7	7	/* both buttons down */
#define S8	8	/* button 3 delayed up */
#define S9	9	/* button 1 or 3 up after S3 */

#define A(b1, b3)	(((b1) ? 2 : 0) | ((b3) ? 1 : 0))
#define A_TIMEOUT	4
#define S_DELAYED(st)	(states[st].s[A_TIMEOUT] != (st))

static struct {
    int s[A_TIMEOUT + 1];
    int buttons;
    int mask;
    int timeout;
} states[10] = {
    /* S0 */
    { { S0, S2, S1, S3, S0 }, 0, ~(MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN), FALSE },
    /* S1 */
    { { S4, S2, S1, S3, S5 }, 0, ~MOUSE_BUTTON1DOWN, FALSE },
    /* S2 */
    { { S8, S2, S1, S3, S6 }, 0, ~MOUSE_BUTTON3DOWN, FALSE },
    /* S3 */
    { { S0, S9, S9, S3, S3 }, MOUSE_BUTTON2DOWN, ~0, FALSE },
    /* S4 */
    { { S0, S2, S1, S3, S0 }, MOUSE_BUTTON1DOWN, ~0, TRUE },
    /* S5 */
    { { S0, S2, S5, S7, S5 }, MOUSE_BUTTON1DOWN, ~0, FALSE },
    /* S6 */
    { { S0, S6, S1, S7, S6 }, MOUSE_BUTTON3DOWN, ~0, FALSE },
    /* S7 */
    { { S0, S6, S5, S7, S7 }, MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN, ~0, FALSE },
    /* S8 */
    { { S0, S2, S1, S3, S0 }, MOUSE_BUTTON3DOWN, ~0, TRUE },
    /* S9 */
    { { S0, S9, S9, S3, S9 }, 0, ~(MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN), FALSE },
};
static int		mouse_button_state;
static struct timespec	mouse_button_state_ts;
static int		mouse_move_delayed;

static jmp_buf env;

struct drift_xy {
    int x;
    int y;
};
static int		drift_distance = 4;	/* max steps X+Y */
static int		drift_time = 500;	/* in 0.5 sec */
static struct timespec	drift_time_ts;
static struct timespec	drift_2time_ts;		/* 2*drift_time */
static int		drift_after = 4000;	/* 4 sec */
static struct timespec	drift_after_ts;
static int		drift_terminate = FALSE;
static struct timespec	drift_current_ts;
static struct timespec	drift_tmp;
static struct timespec	drift_last_activity = {0, 0};
static struct timespec	drift_since = {0, 0};
static struct drift_xy	drift_last = {0, 0};	/* steps in last drift_time */
static struct drift_xy  drift_previous = {0, 0};	/* steps in prev. drift_time */

/* function prototypes */

static void	linacc(int, int, int*, int*);
static void	expoacc(int, int, int*, int*);
static void	moused(void);
static void	hup(int sig);
static void	cleanup(int sig);
static void	pause_mouse(int sig);
static void	usage(void);
static void	log_or_warn(int log_pri, int errnum, const char *fmt, ...)
		    __printflike(3, 4);

static int	r_identify(void);
static const char *r_if(int type);
static const char *r_name(int type);
static const char *r_model(int model);
static void	r_init(void);
static int	r_protocol(u_char b, mousestatus_t *act);
static int	r_statetrans(mousestatus_t *a1, mousestatus_t *a2, int trans);
static int	r_installmap(char *arg);
static void	r_map(mousestatus_t *act1, mousestatus_t *act2);
static void	r_timestamp(mousestatus_t *act);
static int	r_timeout(void);
static void	r_click(mousestatus_t *act);
static void	setmousespeed(int old, int new, unsigned cflag);

static int	pnpwakeup1(void);
static int	pnpwakeup2(void);
static int	pnpgets(char *buf);
static int	pnpparse(pnpid_t *id, char *buf, int len);
static symtab_t	*pnpproto(pnpid_t *id);

static symtab_t	*gettoken(symtab_t *tab, const char *s, int len);
static const char *gettokenname(symtab_t *tab, int val);

static void	mremote_serversetup(void);
static void	mremote_clientchg(int add);

static int	kidspad(u_char rxc, mousestatus_t *act);
static int	gtco_digipad(u_char, mousestatus_t *);

int
main(int argc, char *argv[])
{
    int c;
    int	i;
    int	j;

    for (i = 0; i < MOUSE_MAXBUTTON; ++i)
	mstate[i] = &bstate[i];

    while ((c = getopt(argc, argv, "3A:C:DE:F:HI:L:PRS:T:VU:a:cdfhi:l:m:p:r:st:w:z:")) != -1)
	switch(c) {

	case '3':
	    rodent.flags |= Emulate3Button;
	    break;

	case 'E':
	    rodent.button2timeout = atoi(optarg);
	    if ((rodent.button2timeout < 0) ||
		(rodent.button2timeout > MAX_BUTTON2TIMEOUT)) {
		warnx("invalid argument `%s'", optarg);
		usage();
	    }
	    break;

	case 'a':
	    i = sscanf(optarg, "%f,%f", &rodent.accelx, &rodent.accely);
	    if (i == 0) {
		warnx("invalid linear acceleration argument '%s'", optarg);
		usage();
	    }

	    if (i == 1)
		rodent.accely = rodent.accelx;

	    break;

	case 'A':
	    rodent.flags |= ExponentialAcc;
	    i = sscanf(optarg, "%f,%f", &rodent.expoaccel, &rodent.expoffset);
	    if (i == 0) {
		warnx("invalid exponential acceleration argument '%s'", optarg);
		usage();
	    }

	    if (i == 1)
		rodent.expoffset = 1.0;

	    break;

	case 'c':
	    rodent.flags |= ChordMiddle;
	    break;

	case 'd':
	    ++debug;
	    break;

	case 'f':
	    nodaemon = TRUE;
	    break;

	case 'i':
	    if (strcmp(optarg, "all") == 0)
		identify = ID_ALL;
	    else if (strcmp(optarg, "port") == 0)
		identify = ID_PORT;
	    else if (strcmp(optarg, "if") == 0)
		identify = ID_IF;
	    else if (strcmp(optarg, "type") == 0)
		identify = ID_TYPE;
	    else if (strcmp(optarg, "model") == 0)
		identify = ID_MODEL;
	    else {
		warnx("invalid argument `%s'", optarg);
		usage();
	    }
	    nodaemon = TRUE;
	    break;

	case 'l':
	    rodent.level = atoi(optarg);
	    if ((rodent.level < 0) || (rodent.level > 4)) {
		warnx("invalid argument `%s'", optarg);
		usage();
	    }
	    break;

	case 'm':
	    if (!r_installmap(optarg)) {
		warnx("invalid argument `%s'", optarg);
		usage();
	    }
	    break;

	case 'p':
	    rodent.portname = optarg;
	    break;

	case 'r':
	    if (strcmp(optarg, "high") == 0)
		rodent.resolution = MOUSE_RES_HIGH;
	    else if (strcmp(optarg, "medium-high") == 0)
		rodent.resolution = MOUSE_RES_HIGH;
	    else if (strcmp(optarg, "medium-low") == 0)
		rodent.resolution = MOUSE_RES_MEDIUMLOW;
	    else if (strcmp(optarg, "low") == 0)
		rodent.resolution = MOUSE_RES_LOW;
	    else if (strcmp(optarg, "default") == 0)
		rodent.resolution = MOUSE_RES_DEFAULT;
	    else {
		rodent.resolution = atoi(optarg);
		if (rodent.resolution <= 0) {
		    warnx("invalid argument `%s'", optarg);
		    usage();
		}
	    }
	    break;

	case 's':
	    rodent.baudrate = 9600;
	    break;

	case 'w':
	    i = atoi(optarg);
	    if ((i <= 0) || (i > MOUSE_MAXBUTTON)) {
		warnx("invalid argument `%s'", optarg);
		usage();
	    }
	    rodent.wmode = 1 << (i - 1);
	    break;

	case 'z':
	    if (strcmp(optarg, "x") == 0)
		rodent.zmap[0] = MOUSE_XAXIS;
	    else if (strcmp(optarg, "y") == 0)
		rodent.zmap[0] = MOUSE_YAXIS;
	    else {
		i = atoi(optarg);
		/*
		 * Use button i for negative Z axis movement and
		 * button (i + 1) for positive Z axis movement.
		 */
		if ((i <= 0) || (i > MOUSE_MAXBUTTON - 1)) {
		    warnx("invalid argument `%s'", optarg);
		    usage();
		}
		rodent.zmap[0] = i;
		rodent.zmap[1] = i + 1;
		debug("optind: %d, optarg: '%s'", optind, optarg);
		for (j = 1; j < 4; ++j) {
		    if ((optind >= argc) || !isdigit(*argv[optind]))
			break;
		    i = atoi(argv[optind]);
		    if ((i <= 0) || (i > MOUSE_MAXBUTTON - 1)) {
			warnx("invalid argument `%s'", argv[optind]);
			usage();
		    }
		    rodent.zmap[j] = i;
		    ++optind;
		}
		if ((rodent.zmap[2] != 0) && (rodent.zmap[3] == 0))
		    rodent.zmap[3] = rodent.zmap[2] + 1;
	    }
	    break;

	case 'C':
	    rodent.clickthreshold = atoi(optarg);
	    if ((rodent.clickthreshold < 0) ||
		(rodent.clickthreshold > MAX_CLICKTHRESHOLD)) {
		warnx("invalid argument `%s'", optarg);
		usage();
	    }
	    break;

	case 'D':
	    rodent.flags |= ClearDTR;
	    break;

	case 'F':
	    rodent.rate = atoi(optarg);
	    if (rodent.rate <= 0) {
		warnx("invalid argument `%s'", optarg);
		usage();
	    }
	    break;

	case 'H':
	    rodent.flags |= HVirtualScroll;
	    break;
		
	case 'I':
	    pidfile = optarg;
	    break;

	case 'L':
	    rodent.scrollspeed = atoi(optarg);
	    if (rodent.scrollspeed < 0) {
		warnx("invalid argument `%s'", optarg);
		usage();
	    }
	    break;

	case 'P':
	    rodent.flags |= NoPnP;
	    break;

	case 'R':
	    rodent.flags |= ClearRTS;
	    break;

	case 'S':
	    rodent.baudrate = atoi(optarg);
	    if (rodent.baudrate <= 0) {
		warnx("invalid argument `%s'", optarg);
		usage();
	    }
	    debug("rodent baudrate %d", rodent.baudrate);
	    break;

	case 'T':
	    drift_terminate = TRUE;
	    sscanf(optarg, "%d,%d,%d", &drift_distance, &drift_time,
		&drift_after);
	    if (drift_distance <= 0 || drift_time <= 0 || drift_after <= 0) {
		warnx("invalid argument `%s'", optarg);
		usage();
	    }
	    debug("terminate drift: distance %d, time %d, after %d",
		drift_distance, drift_time, drift_after);
	    drift_time_ts.tv_sec = drift_time / 1000;
	    drift_time_ts.tv_nsec = (drift_time % 1000) * 1000000;
 	    drift_2time_ts.tv_sec = (drift_time *= 2) / 1000;
	    drift_2time_ts.tv_nsec = (drift_time % 1000) * 1000000;
	    drift_after_ts.tv_sec = drift_after / 1000;
	    drift_after_ts.tv_nsec = (drift_after % 1000) * 1000000;
	    break;

	case 't':
	    if (strcmp(optarg, "auto") == 0) {
		rodent.rtype = MOUSE_PROTO_UNKNOWN;
		rodent.flags &= ~NoPnP;
		rodent.level = -1;
		break;
	    }
	    for (i = 0; rnames[i] != NULL; i++)
		if (strcmp(optarg, rnames[i]) == 0) {
		    rodent.rtype = i;
		    rodent.flags |= NoPnP;
		    rodent.level = (i == MOUSE_PROTO_SYSMOUSE) ? 1 : 0;
		    break;
		}
	    if (rnames[i] == NULL) {
		warnx("no such mouse type `%s'", optarg);
		usage();
	    }
	    break;

	case 'V':
	    rodent.flags |= VirtualScroll;
	    break;
	case 'U':
	    rodent.scrollthreshold = atoi(optarg);
	    if (rodent.scrollthreshold < 0) {
		warnx("invalid argument `%s'", optarg);
		usage();
	    }
	    break;

	case 'h':
	case '?':
	default:
	    usage();
	}

    /* fix Z axis mapping */
    for (i = 0; i < 4; ++i) {
	if (rodent.zmap[i] > 0) {
	    for (j = 0; j < MOUSE_MAXBUTTON; ++j) {
		if (mstate[j] == &bstate[rodent.zmap[i] - 1])
		    mstate[j] = &zstate[i];
	    }
	    rodent.zmap[i] = 1 << (rodent.zmap[i] - 1);
	}
    }

    /* the default port name */
    switch(rodent.rtype) {

    case MOUSE_PROTO_PS2:
	if (!rodent.portname)
	    rodent.portname = "/dev/psm0";
	break;

    default:
	if (rodent.portname)
	    break;
	warnx("no port name specified");
	usage();
    }

    if (strncmp(rodent.portname, "/dev/ums", 8) == 0)
	rodent.is_removable = 1;

    for (;;) {
	if (setjmp(env) == 0) {
	    signal(SIGHUP, hup);
	    signal(SIGINT , cleanup);
	    signal(SIGQUIT, cleanup);
	    signal(SIGTERM, cleanup);
	    signal(SIGUSR1, pause_mouse);

	    rodent.mfd = open(rodent.portname, O_RDWR | O_NONBLOCK);
	    if (rodent.mfd == -1)
		logerr(1, "unable to open %s", rodent.portname);
	    if (r_identify() == MOUSE_PROTO_UNKNOWN) {
		logwarnx("cannot determine mouse type on %s", rodent.portname);
		close(rodent.mfd);
		rodent.mfd = -1;
	    }

	    /* print some information */
	    if (identify != ID_NONE) {
		if (identify == ID_ALL)
		    printf("%s %s %s %s\n",
			rodent.portname, r_if(rodent.hw.iftype),
			r_name(rodent.rtype), r_model(rodent.hw.model));
		else if (identify & ID_PORT)
		    printf("%s\n", rodent.portname);
		else if (identify & ID_IF)
		    printf("%s\n", r_if(rodent.hw.iftype));
		else if (identify & ID_TYPE)
		    printf("%s\n", r_name(rodent.rtype));
		else if (identify & ID_MODEL)
		    printf("%s\n", r_model(rodent.hw.model));
		exit(0);
	    } else {
		debug("port: %s  interface: %s  type: %s  model: %s",
		    rodent.portname, r_if(rodent.hw.iftype),
		    r_name(rodent.rtype), r_model(rodent.hw.model));
	    }

	    if (rodent.mfd == -1) {
		/*
		 * We cannot continue because of error.  Exit if the
		 * program has not become a daemon.  Otherwise, block
		 * until the user corrects the problem and issues SIGHUP.
		 */
		if (!background)
		    exit(1);
		sigpause(0);
	    }

	    r_init();			/* call init function */
	    moused();
	}

	if (rodent.mfd != -1)
	    close(rodent.mfd);
	if (rodent.cfd != -1)
	    close(rodent.cfd);
	rodent.mfd = rodent.cfd = -1;
	if (rodent.is_removable)
		exit(0);
    }
    /* NOT REACHED */

    exit(0);
}

/*
 * Function to calculate linear acceleration.
 *
 * If there are any rounding errors, the remainder
 * is stored in the remainx and remainy variables
 * and taken into account upon the next movement.
 */

static void
linacc(int dx, int dy, int *movex, int *movey)
{
    float fdx, fdy;

    if (dx == 0 && dy == 0) {
	*movex = *movey = 0;
	return;
    }
    fdx = dx * rodent.accelx + rodent.remainx;
    fdy = dy * rodent.accely + rodent.remainy;
    *movex = lround(fdx);
    *movey = lround(fdy);
    rodent.remainx = fdx - *movex;
    rodent.remainy = fdy - *movey;
}

/*
 * Function to calculate exponential acceleration.
 * (Also includes linear acceleration if enabled.)
 *
 * In order to give a smoother behaviour, we record the four
 * most recent non-zero movements and use their average value
 * to calculate the acceleration.
 */

static void
expoacc(int dx, int dy, int *movex, int *movey)
{
    static float lastlength[3] = {0.0, 0.0, 0.0};
    float fdx, fdy, length, lbase, accel;

    if (dx == 0 && dy == 0) {
	*movex = *movey = 0;
	return;
    }
    fdx = dx * rodent.accelx;
    fdy = dy * rodent.accely;
    length = sqrtf((fdx * fdx) + (fdy * fdy));		/* Pythagoras */
    length = (length + lastlength[0] + lastlength[1] + lastlength[2]) / 4;
    lbase = length / rodent.expoffset;
    accel = powf(lbase, rodent.expoaccel) / lbase;
    fdx = fdx * accel + rodent.remainx;
    fdy = fdy * accel + rodent.remainy;
    *movex = lroundf(fdx);
    *movey = lroundf(fdy);
    rodent.remainx = fdx - *movex;
    rodent.remainy = fdy - *movey;
    lastlength[2] = lastlength[1];
    lastlength[1] = lastlength[0];
    lastlength[0] = length;	/* Insert new average, not original length! */
}

static void
moused(void)
{
    struct mouse_info mouse;
    mousestatus_t action0;		/* original mouse action */
    mousestatus_t action;		/* interim buffer */
    mousestatus_t action2;		/* mapped action */
    struct timeval timeout;
    fd_set fds;
    u_char b;
    pid_t mpid;
    int flags;
    int c;
    int i;

    if ((rodent.cfd = open("/dev/consolectl", O_RDWR, 0)) == -1)
	logerr(1, "cannot open /dev/consolectl");

    if (!nodaemon && !background) {
	pfh = pidfile_open(pidfile, 0600, &mpid);
	if (pfh == NULL) {
	    if (errno == EEXIST)
		logerrx(1, "moused already running, pid: %d", mpid);
	    logwarn("cannot open pid file");
	}
	if (daemon(0, 0)) {
	    int saved_errno = errno;
	    pidfile_remove(pfh);
	    errno = saved_errno;
	    logerr(1, "failed to become a daemon");
	} else {
	    background = TRUE;
	    pidfile_write(pfh);
	}
    }

    /* clear mouse data */
    bzero(&action0, sizeof(action0));
    bzero(&action, sizeof(action));
    bzero(&action2, sizeof(action2));
    bzero(&mouse, sizeof(mouse));
    mouse_button_state = S0;
    clock_gettime(CLOCK_MONOTONIC_FAST, &mouse_button_state_ts);
    mouse_move_delayed = 0;
    for (i = 0; i < MOUSE_MAXBUTTON; ++i) {
	bstate[i].count = 0;
	bstate[i].ts = mouse_button_state_ts;
    }
    for (i = 0; i < (int)(sizeof(zstate) / sizeof(zstate[0])); ++i) {
	zstate[i].count = 0;
	zstate[i].ts = mouse_button_state_ts;
    }

    /* choose which ioctl command to use */
    mouse.operation = MOUSE_MOTION_EVENT;
    extioctl = (ioctl(rodent.cfd, CONS_MOUSECTL, &mouse) == 0);

    /* process mouse data */
    timeout.tv_sec = 0;
    timeout.tv_usec = 20000;		/* 20 msec */
    for (;;) {

	FD_ZERO(&fds);
	FD_SET(rodent.mfd, &fds);
	if (rodent.mremsfd >= 0)
	    FD_SET(rodent.mremsfd, &fds);
	if (rodent.mremcfd >= 0)
	    FD_SET(rodent.mremcfd, &fds);

	c = select(FD_SETSIZE, &fds, NULL, NULL,
		   ((rodent.flags & Emulate3Button) &&
		    S_DELAYED(mouse_button_state)) ? &timeout : NULL);
	if (c < 0) {                    /* error */
	    logwarn("failed to read from mouse");
	    continue;
	} else if (c == 0) {            /* timeout */
	    /* assert(rodent.flags & Emulate3Button) */
	    action0.button = action0.obutton;
	    action0.dx = action0.dy = action0.dz = 0;
	    action0.flags = flags = 0;
	    if (r_timeout() && r_statetrans(&action0, &action, A_TIMEOUT)) {
		if (debug > 2)
		    debug("flags:%08x buttons:%08x obuttons:%08x",
			  action.flags, action.button, action.obutton);
	    } else {
		action0.obutton = action0.button;
		continue;
	    }
	} else {
	    /*  MouseRemote client connect/disconnect  */
	    if ((rodent.mremsfd >= 0) && FD_ISSET(rodent.mremsfd, &fds)) {
		mremote_clientchg(TRUE);
		continue;
	    }
	    if ((rodent.mremcfd >= 0) && FD_ISSET(rodent.mremcfd, &fds)) {
		mremote_clientchg(FALSE);
		continue;
	    }
	    /* mouse movement */
	    if (read(rodent.mfd, &b, 1) == -1) {
		if (errno == EWOULDBLOCK)
		    continue;
		else
		    return;
	    }
	    if ((flags = r_protocol(b, &action0)) == 0)
		continue;

	    if ((rodent.flags & VirtualScroll) || (rodent.flags & HVirtualScroll)) {
		/* Allow middle button drags to scroll up and down */
		if (action0.button == MOUSE_BUTTON2DOWN) {
		    if (scroll_state == SCROLL_NOTSCROLLING) {
			scroll_state = SCROLL_PREPARE;
			scroll_movement = hscroll_movement = 0;
			debug("PREPARING TO SCROLL");
		    }
		    debug("[BUTTON2] flags:%08x buttons:%08x obuttons:%08x",
			  action.flags, action.button, action.obutton);
		} else {
		    debug("[NOTBUTTON2] flags:%08x buttons:%08x obuttons:%08x",
			  action.flags, action.button, action.obutton);

		    /* This isn't a middle button down... move along... */
		    if (scroll_state == SCROLL_SCROLLING) {
			/*
			 * We were scrolling, someone let go of button 2.
			 * Now turn autoscroll off.
			 */
			scroll_state = SCROLL_NOTSCROLLING;
			debug("DONE WITH SCROLLING / %d", scroll_state);
		    } else if (scroll_state == SCROLL_PREPARE) {
			mousestatus_t newaction = action0;

			/* We were preparing to scroll, but we never moved... */
			r_timestamp(&action0);
			r_statetrans(&action0, &newaction,
				     A(newaction.button & MOUSE_BUTTON1DOWN,
				       action0.button & MOUSE_BUTTON3DOWN));

			/* Send middle down */
			newaction.button = MOUSE_BUTTON2DOWN;
			r_click(&newaction);

			/* Send middle up */
			r_timestamp(&newaction);
			newaction.obutton = newaction.button;
			newaction.button = action0.button;
			r_click(&newaction);
		    }
		}
	    }

	    r_timestamp(&action0);
	    r_statetrans(&action0, &action,
			 A(action0.button & MOUSE_BUTTON1DOWN,
			   action0.button & MOUSE_BUTTON3DOWN));
	    debug("flags:%08x buttons:%08x obuttons:%08x", action.flags,
		  action.button, action.obutton);
	}
	action0.obutton = action0.button;
	flags &= MOUSE_POSCHANGED;
	flags |= action.obutton ^ action.button;
	action.flags = flags;

	if (flags) {			/* handler detected action */
	    r_map(&action, &action2);
	    debug("activity : buttons 0x%08x  dx %d  dy %d  dz %d",
		action2.button, action2.dx, action2.dy, action2.dz);

	    if ((rodent.flags & VirtualScroll) || (rodent.flags & HVirtualScroll)) {
		/*
		 * If *only* the middle button is pressed AND we are moving
		 * the stick/trackpoint/nipple, scroll!
		 */
		if (scroll_state == SCROLL_PREPARE) {
			/* Middle button down, waiting for movement threshold */
			if (action2.dy || action2.dx) {
				if (rodent.flags & VirtualScroll) {
					scroll_movement += action2.dy;
					if (scroll_movement < -rodent.scrollthreshold) {
						scroll_state = SCROLL_SCROLLING;
					} else if (scroll_movement > rodent.scrollthreshold) {
						scroll_state = SCROLL_SCROLLING;
					}
				}
				if (rodent.flags & HVirtualScroll) {
					hscroll_movement += action2.dx;
					if (hscroll_movement < -rodent.scrollthreshold) {
						scroll_state = SCROLL_SCROLLING;
					} else if (hscroll_movement > rodent.scrollthreshold) {
						scroll_state = SCROLL_SCROLLING;
					}
				}
				if (scroll_state == SCROLL_SCROLLING) scroll_movement = hscroll_movement = 0;
			}
		} else if (scroll_state == SCROLL_SCROLLING) {
			 if (rodent.flags & VirtualScroll) {
				 scroll_movement += action2.dy;
				 debug("SCROLL: %d", scroll_movement);
			    if (scroll_movement < -rodent.scrollspeed) {
				/* Scroll down */
				action2.dz = -1;
				scroll_movement = 0;
			    }
			    else if (scroll_movement > rodent.scrollspeed) {
				/* Scroll up */
				action2.dz = 1;
				scroll_movement = 0;
			    }
			 }
			 if (rodent.flags & HVirtualScroll) {
				 hscroll_movement += action2.dx;
				 debug("HORIZONTAL SCROLL: %d", hscroll_movement);

				 if (hscroll_movement < -rodent.scrollspeed) {
					 action2.dz = -2;
					 hscroll_movement = 0;
				 }
				 else if (hscroll_movement > rodent.scrollspeed) {
					 action2.dz = 2;
					 hscroll_movement = 0;
				 }
			 }

		    /* Don't move while scrolling */
		    action2.dx = action2.dy = 0;
		}
	    }

	    if (drift_terminate) {
		if ((flags & MOUSE_POSCHANGED) == 0 || action.dz || action2.dz)
		    drift_last_activity = drift_current_ts;
		else {
		    /* X or/and Y movement only - possibly drift */
		    tssub(&drift_current_ts, &drift_last_activity, &drift_tmp);
		    if (tscmp(&drift_tmp, &drift_after_ts, >)) {
			tssub(&drift_current_ts, &drift_since, &drift_tmp);
			if (tscmp(&drift_tmp, &drift_time_ts, <)) {
			    drift_last.x += action2.dx;
			    drift_last.y += action2.dy;
			} else {
			    /* discard old accumulated steps (drift) */
			    if (tscmp(&drift_tmp, &drift_2time_ts, >))
				drift_previous.x = drift_previous.y = 0;
			    else
				drift_previous = drift_last;
			    drift_last.x = action2.dx;
			    drift_last.y = action2.dy;
			    drift_since = drift_current_ts;
			}
			if (abs(drift_last.x) + abs(drift_last.y)
			  > drift_distance) {
			    /* real movement, pass all accumulated steps */
			    action2.dx = drift_previous.x + drift_last.x;
			    action2.dy = drift_previous.y + drift_last.y;
			    /* and reset accumulators */
			    tsclr(&drift_since);
			    drift_last.x = drift_last.y = 0;
			    /* drift_previous will be cleared at next movement*/
			    drift_last_activity = drift_current_ts;
			} else {
			    continue;   /* don't pass current movement to
					 * console driver */
			}
		    }
		}
	    }

	    if (extioctl) {
		/* Defer clicks until we aren't VirtualScroll'ing. */
		if (scroll_state == SCROLL_NOTSCROLLING)
		    r_click(&action2);

		if (action2.flags & MOUSE_POSCHANGED) {
		    mouse.operation = MOUSE_MOTION_EVENT;
		    mouse.u.data.buttons = action2.button;
		    if (rodent.flags & ExponentialAcc) {
			expoacc(action2.dx, action2.dy,
			    &mouse.u.data.x, &mouse.u.data.y);
		    }
		    else {
			linacc(action2.dx, action2.dy,
			    &mouse.u.data.x, &mouse.u.data.y);
		    }
		    mouse.u.data.z = action2.dz;
		    if (debug < 2)
			if (!paused)
				ioctl(rodent.cfd, CONS_MOUSECTL, &mouse);
		}
	    } else {
		mouse.operation = MOUSE_ACTION;
		mouse.u.data.buttons = action2.button;
		if (rodent.flags & ExponentialAcc) {
		    expoacc(action2.dx, action2.dy,
			&mouse.u.data.x, &mouse.u.data.y);
		}
		else {
		    linacc(action2.dx, action2.dy,
			&mouse.u.data.x, &mouse.u.data.y);
		}
		mouse.u.data.z = action2.dz;
		if (debug < 2)
		    if (!paused)
			ioctl(rodent.cfd, CONS_MOUSECTL, &mouse);
	    }

	    /*
	     * If the Z axis movement is mapped to an imaginary physical
	     * button, we need to cook up a corresponding button `up' event
	     * after sending a button `down' event.
	     */
	    if ((rodent.zmap[0] > 0) && (action.dz != 0)) {
		action.obutton = action.button;
		action.dx = action.dy = action.dz = 0;
		r_map(&action, &action2);
		debug("activity : buttons 0x%08x  dx %d  dy %d  dz %d",
		    action2.button, action2.dx, action2.dy, action2.dz);

		if (extioctl) {
		    r_click(&action2);
		} else {
		    mouse.operation = MOUSE_ACTION;
		    mouse.u.data.buttons = action2.button;
		    mouse.u.data.x = mouse.u.data.y = mouse.u.data.z = 0;
		    if (debug < 2)
			if (!paused)
			    ioctl(rodent.cfd, CONS_MOUSECTL, &mouse);
		}
	    }
	}
    }
    /* NOT REACHED */
}

static void
hup(__unused int sig)
{
    longjmp(env, 1);
}

static void
cleanup(__unused int sig)
{
    if (rodent.rtype == MOUSE_PROTO_X10MOUSEREM)
	unlink(_PATH_MOUSEREMOTE);
    exit(0);
}

static void
pause_mouse(__unused int sig)
{
    paused = !paused;
}

/**
 ** usage
 **
 ** Complain, and free the CPU for more worthy tasks
 **/
static void
usage(void)
{
    fprintf(stderr, "%s\n%s\n%s\n%s\n%s\n",
	"usage: moused [-DRcdfs] [-I file] [-F rate] [-r resolution] [-S baudrate]",
	"              [-VH [-U threshold]] [-a X[,Y]] [-C threshold] [-m N=M] [-w N]",
	"              [-z N] [-t <mousetype>] [-l level] [-3 [-E timeout]]",
	"              [-T distance[,time[,after]]] -p <port>",
	"       moused [-d] -i <port|if|type|model|all> -p <port>");
    exit(1);
}

/*
 * Output an error message to syslog or stderr as appropriate. If
 * `errnum' is non-zero, append its string form to the message.
 */
static void
log_or_warn(int log_pri, int errnum, const char *fmt, ...)
{
	va_list ap;
	char buf[256];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (errnum) {
		strlcat(buf, ": ", sizeof(buf));
		strlcat(buf, strerror(errnum), sizeof(buf));
	}

	if (background)
		syslog(log_pri, "%s", buf);
	else
		warnx("%s", buf);
}

/**
 ** Mouse interface code, courtesy of XFree86 3.1.2.
 **
 ** Note: Various bits have been trimmed, and in my shortsighted enthusiasm
 ** to clean, reformat and rationalise naming, it's quite possible that
 ** some things in here have been broken.
 **
 ** I hope not 8)
 **
 ** The following code is derived from a module marked :
 **/

/* $XConsortium: xf86_Mouse.c,v 1.2 94/10/12 20:33:21 kaleb Exp $ */
/* $XFree86: xc/programs/Xserver/hw/xfree86/common/xf86_Mouse.c,v 3.2 1995/01/28
 17:03:40 dawes Exp $ */
/*
 *
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 * Copyright 1993 by David Dawes <dawes@physics.su.oz.au>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the names of Thomas Roell and David Dawes not be
 * used in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  Thomas Roell
 * and David Dawes makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * THOMAS ROELL AND DAVID DAWES DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THOMAS ROELL OR DAVID DAWES BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

/**
 ** GlidePoint support from XFree86 3.2.
 ** Derived from the module:
 **/

/* $XFree86: xc/programs/Xserver/hw/xfree86/common/xf86_Mouse.c,v 3.19 1996/10/16 14:40:51 dawes Exp $ */
/* $XConsortium: xf86_Mouse.c /main/10 1996/01/30 15:16:12 kaleb $ */

/* the following table must be ordered by MOUSE_PROTO_XXX in mouse.h */
static unsigned char proto[][7] = {
    /*  hd_mask hd_id   dp_mask dp_id   bytes b4_mask b4_id */
    {	0x40,	0x40,	0x40,	0x00,	3,   ~0x23,  0x00 }, /* MicroSoft */
    {	0xf8,	0x80,	0x00,	0x00,	5,    0x00,  0xff }, /* MouseSystems */
    {	0xe0,	0x80,	0x80,	0x00,	3,    0x00,  0xff }, /* Logitech */
    {	0xe0,	0x80,	0x80,	0x00,	3,    0x00,  0xff }, /* MMSeries */
    {	0x40,	0x40,	0x40,	0x00,	3,   ~0x33,  0x00 }, /* MouseMan */
    {	0xf8,	0x80,	0x00,	0x00,	5,    0x00,  0xff }, /* Bus */
    {	0xf8,	0x80,	0x00,	0x00,	5,    0x00,  0xff }, /* InPort */
    {	0xc0,	0x00,	0x00,	0x00,	3,    0x00,  0xff }, /* PS/2 mouse */
    {	0xe0,	0x80,	0x80,	0x00,	3,    0x00,  0xff }, /* MM HitTablet */
    {	0x40,	0x40,	0x40,	0x00,	3,   ~0x33,  0x00 }, /* GlidePoint */
    {	0x40,	0x40,	0x40,	0x00,	3,   ~0x3f,  0x00 }, /* IntelliMouse */
    {	0x40,	0x40,	0x40,	0x00,	3,   ~0x33,  0x00 }, /* ThinkingMouse */
    {	0xf8,	0x80,	0x00,	0x00,	5,    0x00,  0xff }, /* sysmouse */
    {	0x40,	0x40,	0x40,	0x00,	3,   ~0x23,  0x00 }, /* X10 MouseRem */
    {	0x80,	0x80,	0x00,	0x00,	5,    0x00,  0xff }, /* KIDSPAD */
    {	0xc3,	0xc0,	0x00,	0x00,	6,    0x00,  0xff }, /* VersaPad */
    {	0x00,	0x00,	0x00,	0x00,	1,    0x00,  0xff }, /* JogDial */
#if notyet
    {	0xf8,	0x80,	0x00,	0x00,	5,   ~0x2f,  0x10 }, /* Mariqua */
#endif
};
static unsigned char cur_proto[7];

static int
r_identify(void)
{
    char pnpbuf[256];	/* PnP identifier string may be up to 256 bytes long */
    pnpid_t pnpid;
    symtab_t *t;
    int level;
    int len;

    /* set the driver operation level, if applicable */
    if (rodent.level < 0)
	rodent.level = 1;
    ioctl(rodent.mfd, MOUSE_SETLEVEL, &rodent.level);
    rodent.level = (ioctl(rodent.mfd, MOUSE_GETLEVEL, &level) == 0) ? level : 0;

    /*
     * Interrogate the driver and get some intelligence on the device...
     * The following ioctl functions are not always supported by device
     * drivers.  When the driver doesn't support them, we just trust the
     * user to supply valid information.
     */
    rodent.hw.iftype = MOUSE_IF_UNKNOWN;
    rodent.hw.model = MOUSE_MODEL_GENERIC;
    ioctl(rodent.mfd, MOUSE_GETHWINFO, &rodent.hw);

    if (rodent.rtype != MOUSE_PROTO_UNKNOWN)
	bcopy(proto[rodent.rtype], cur_proto, sizeof(cur_proto));
    rodent.mode.protocol = MOUSE_PROTO_UNKNOWN;
    rodent.mode.rate = -1;
    rodent.mode.resolution = MOUSE_RES_UNKNOWN;
    rodent.mode.accelfactor = 0;
    rodent.mode.level = 0;
    if (ioctl(rodent.mfd, MOUSE_GETMODE, &rodent.mode) == 0) {
	if (rodent.mode.protocol == MOUSE_PROTO_UNKNOWN ||
	    rodent.mode.protocol >= (int)(sizeof(proto) / sizeof(proto[0]))) {
	    logwarnx("unknown mouse protocol (%d)", rodent.mode.protocol);
	    return (MOUSE_PROTO_UNKNOWN);
	} else {
	    if (rodent.mode.protocol != rodent.rtype) {
		/* Hmm, the driver doesn't agree with the user... */
		if (rodent.rtype != MOUSE_PROTO_UNKNOWN)
		    logwarnx("mouse type mismatch (%s != %s), %s is assumed",
			r_name(rodent.mode.protocol), r_name(rodent.rtype),
			r_name(rodent.mode.protocol));
		rodent.rtype = rodent.mode.protocol;
		bcopy(proto[rodent.rtype], cur_proto, sizeof(cur_proto));
	    }
	}
	cur_proto[4] = rodent.mode.packetsize;
	cur_proto[0] = rodent.mode.syncmask[0];	/* header byte bit mask */
	cur_proto[1] = rodent.mode.syncmask[1];	/* header bit pattern */
    }

    /* maybe this is a PnP mouse... */
    if (rodent.mode.protocol == MOUSE_PROTO_UNKNOWN) {

	if (rodent.flags & NoPnP)
	    return (rodent.rtype);
	if (((len = pnpgets(pnpbuf)) <= 0) || !pnpparse(&pnpid, pnpbuf, len))
	    return (rodent.rtype);

	debug("PnP serial mouse: '%*.*s' '%*.*s' '%*.*s'",
	    pnpid.neisaid, pnpid.neisaid, pnpid.eisaid,
	    pnpid.ncompat, pnpid.ncompat, pnpid.compat,
	    pnpid.ndescription, pnpid.ndescription, pnpid.description);

	/* we have a valid PnP serial device ID */
	rodent.hw.iftype = MOUSE_IF_SERIAL;
	t = pnpproto(&pnpid);
	if (t != NULL) {
	    rodent.mode.protocol = t->val;
	    rodent.hw.model = t->val2;
	} else {
	    rodent.mode.protocol = MOUSE_PROTO_UNKNOWN;
	}

	/* make final adjustment */
	if (rodent.mode.protocol != MOUSE_PROTO_UNKNOWN) {
	    if (rodent.mode.protocol != rodent.rtype) {
		/* Hmm, the device doesn't agree with the user... */
		if (rodent.rtype != MOUSE_PROTO_UNKNOWN)
		    logwarnx("mouse type mismatch (%s != %s), %s is assumed",
			r_name(rodent.mode.protocol), r_name(rodent.rtype),
			r_name(rodent.mode.protocol));
		rodent.rtype = rodent.mode.protocol;
		bcopy(proto[rodent.rtype], cur_proto, sizeof(cur_proto));
	    }
	}
    }

    debug("proto params: %02x %02x %02x %02x %d %02x %02x",
	cur_proto[0], cur_proto[1], cur_proto[2], cur_proto[3],
	cur_proto[4], cur_proto[5], cur_proto[6]);

    return (rodent.rtype);
}

static const char *
r_if(int iftype)
{

    return (gettokenname(rifs, iftype));
}

static const char *
r_name(int type)
{
    const char *unknown = "unknown";

    return (type == MOUSE_PROTO_UNKNOWN ||
	type >= (int)(sizeof(rnames) / sizeof(rnames[0])) ?
	unknown : rnames[type]);
}

static const char *
r_model(int model)
{

    return (gettokenname(rmodels, model));
}

static void
r_init(void)
{
    unsigned char buf[16];	/* scrach buffer */
    fd_set fds;
    const char *s;
    char c;
    int i;

    /**
     ** This comment is a little out of context here, but it contains
     ** some useful information...
     ********************************************************************
     **
     ** The following lines take care of the Logitech MouseMan protocols.
     **
     ** NOTE: There are different versions of both MouseMan and TrackMan!
     **       Hence I add another protocol P_LOGIMAN, which the user can
     **       specify as MouseMan in his XF86Config file. This entry was
     **       formerly handled as a special case of P_MS. However, people
     **       who don't have the middle button problem, can still specify
     **       Microsoft and use P_MS.
     **
     ** By default, these mice should use a 3 byte Microsoft protocol
     ** plus a 4th byte for the middle button. However, the mouse might
     ** have switched to a different protocol before we use it, so I send
     ** the proper sequence just in case.
     **
     ** NOTE: - all commands to (at least the European) MouseMan have to
     **         be sent at 1200 Baud.
     **       - each command starts with a '*'.
     **       - whenever the MouseMan receives a '*', it will switch back
     **	 to 1200 Baud. Hence I have to select the desired protocol
     **	 first, then select the baud rate.
     **
     ** The protocols supported by the (European) MouseMan are:
     **   -  5 byte packed binary protocol, as with the Mouse Systems
     **      mouse. Selected by sequence "*U".
     **   -  2 button 3 byte MicroSoft compatible protocol. Selected
     **      by sequence "*V".
     **   -  3 button 3+1 byte MicroSoft compatible protocol (default).
     **      Selected by sequence "*X".
     **
     ** The following baud rates are supported:
     **   -  1200 Baud (default). Selected by sequence "*n".
     **   -  9600 Baud. Selected by sequence "*q".
     **
     ** Selecting a sample rate is no longer supported with the MouseMan!
     ** Some additional lines in xf86Config.c take care of ill configured
     ** baud rates and sample rates. (The user will get an error.)
     */

    switch (rodent.rtype) {

    case MOUSE_PROTO_LOGI:
	/*
	 * The baud rate selection command must be sent at the current
	 * baud rate; try all likely settings
	 */
	setmousespeed(9600, rodent.baudrate, rodentcflags[rodent.rtype]);
	setmousespeed(4800, rodent.baudrate, rodentcflags[rodent.rtype]);
	setmousespeed(2400, rodent.baudrate, rodentcflags[rodent.rtype]);
	setmousespeed(1200, rodent.baudrate, rodentcflags[rodent.rtype]);
	/* select MM series data format */
	write(rodent.mfd, "S", 1);
	setmousespeed(rodent.baudrate, rodent.baudrate,
		      rodentcflags[MOUSE_PROTO_MM]);
	/* select report rate/frequency */
	if      (rodent.rate <= 0)   write(rodent.mfd, "O", 1);
	else if (rodent.rate <= 15)  write(rodent.mfd, "J", 1);
	else if (rodent.rate <= 27)  write(rodent.mfd, "K", 1);
	else if (rodent.rate <= 42)  write(rodent.mfd, "L", 1);
	else if (rodent.rate <= 60)  write(rodent.mfd, "R", 1);
	else if (rodent.rate <= 85)  write(rodent.mfd, "M", 1);
	else if (rodent.rate <= 125) write(rodent.mfd, "Q", 1);
	else			     write(rodent.mfd, "N", 1);
	break;

    case MOUSE_PROTO_LOGIMOUSEMAN:
	/* The command must always be sent at 1200 baud */
	setmousespeed(1200, 1200, rodentcflags[rodent.rtype]);
	write(rodent.mfd, "*X", 2);
	setmousespeed(1200, rodent.baudrate, rodentcflags[rodent.rtype]);
	break;

    case MOUSE_PROTO_HITTAB:
	setmousespeed(1200, rodent.baudrate, rodentcflags[rodent.rtype]);

	/*
	 * Initialize Hitachi PUMA Plus - Model 1212E to desired settings.
	 * The tablet must be configured to be in MM mode, NO parity,
	 * Binary Format.  xf86Info.sampleRate controls the sensativity
	 * of the tablet.  We only use this tablet for it's 4-button puck
	 * so we don't run in "Absolute Mode"
	 */
	write(rodent.mfd, "z8", 2);	/* Set Parity = "NONE" */
	usleep(50000);
	write(rodent.mfd, "zb", 2);	/* Set Format = "Binary" */
	usleep(50000);
	write(rodent.mfd, "@", 1);	/* Set Report Mode = "Stream" */
	usleep(50000);
	write(rodent.mfd, "R", 1);	/* Set Output Rate = "45 rps" */
	usleep(50000);
	write(rodent.mfd, "I\x20", 2);	/* Set Incrememtal Mode "20" */
	usleep(50000);
	write(rodent.mfd, "E", 1);	/* Set Data Type = "Relative */
	usleep(50000);

	/* Resolution is in 'lines per inch' on the Hitachi tablet */
	if      (rodent.resolution == MOUSE_RES_LOW)		c = 'g';
	else if (rodent.resolution == MOUSE_RES_MEDIUMLOW)	c = 'e';
	else if (rodent.resolution == MOUSE_RES_MEDIUMHIGH)	c = 'h';
	else if (rodent.resolution == MOUSE_RES_HIGH)		c = 'd';
	else if (rodent.resolution <=   40)			c = 'g';
	else if (rodent.resolution <=  100)			c = 'd';
	else if (rodent.resolution <=  200)			c = 'e';
	else if (rodent.resolution <=  500)			c = 'h';
	else if (rodent.resolution <= 1000)			c = 'j';
	else			c = 'd';
	write(rodent.mfd, &c, 1);
	usleep(50000);

	write(rodent.mfd, "\021", 1);	/* Resume DATA output */
	break;

    case MOUSE_PROTO_THINK:
	setmousespeed(1200, rodent.baudrate, rodentcflags[rodent.rtype]);
	/* the PnP ID string may be sent again, discard it */
	usleep(200000);
	i = FREAD;
	ioctl(rodent.mfd, TIOCFLUSH, &i);
	/* send the command to initialize the beast */
	for (s = "E5E5"; *s; ++s) {
	    write(rodent.mfd, s, 1);
	    FD_ZERO(&fds);
	    FD_SET(rodent.mfd, &fds);
	    if (select(FD_SETSIZE, &fds, NULL, NULL, NULL) <= 0)
		break;
	    read(rodent.mfd, &c, 1);
	    debug("%c", c);
	    if (c != *s)
		break;
	}
	break;

    case MOUSE_PROTO_JOGDIAL:
	break;
    case MOUSE_PROTO_MSC:
	setmousespeed(1200, rodent.baudrate, rodentcflags[rodent.rtype]);
	if (rodent.flags & ClearDTR) {
	   i = TIOCM_DTR;
	   ioctl(rodent.mfd, TIOCMBIC, &i);
	}
	if (rodent.flags & ClearRTS) {
	   i = TIOCM_RTS;
	   ioctl(rodent.mfd, TIOCMBIC, &i);
	}
	break;

    case MOUSE_PROTO_SYSMOUSE:
	if (rodent.hw.iftype == MOUSE_IF_SYSMOUSE)
	    setmousespeed(1200, rodent.baudrate, rodentcflags[rodent.rtype]);
	/* FALLTHROUGH */

    case MOUSE_PROTO_PS2:
	if (rodent.rate >= 0)
	    rodent.mode.rate = rodent.rate;
	if (rodent.resolution != MOUSE_RES_UNKNOWN)
	    rodent.mode.resolution = rodent.resolution;
	ioctl(rodent.mfd, MOUSE_SETMODE, &rodent.mode);
	break;

    case MOUSE_PROTO_X10MOUSEREM:
	mremote_serversetup();
	setmousespeed(1200, rodent.baudrate, rodentcflags[rodent.rtype]);
	break;


    case MOUSE_PROTO_VERSAPAD:
	tcsendbreak(rodent.mfd, 0);	/* send break for 400 msec */
	i = FREAD;
	ioctl(rodent.mfd, TIOCFLUSH, &i);
	for (i = 0; i < 7; ++i) {
	    FD_ZERO(&fds);
	    FD_SET(rodent.mfd, &fds);
	    if (select(FD_SETSIZE, &fds, NULL, NULL, NULL) <= 0)
		break;
	    read(rodent.mfd, &c, 1);
	    buf[i] = c;
	}
	debug("%s\n", buf);
	if ((buf[0] != 'V') || (buf[1] != 'P')|| (buf[7] != '\r'))
	    break;
	setmousespeed(9600, rodent.baudrate, rodentcflags[rodent.rtype]);
	tcsendbreak(rodent.mfd, 0);	/* send break for 400 msec again */
	for (i = 0; i < 7; ++i) {
	    FD_ZERO(&fds);
	    FD_SET(rodent.mfd, &fds);
	    if (select(FD_SETSIZE, &fds, NULL, NULL, NULL) <= 0)
		break;
	    read(rodent.mfd, &c, 1);
	    debug("%c", c);
	    if (c != buf[i])
		break;
	}
	i = FREAD;
	ioctl(rodent.mfd, TIOCFLUSH, &i);
	break;

    default:
	setmousespeed(1200, rodent.baudrate, rodentcflags[rodent.rtype]);
	break;
    }
}

static int
r_protocol(u_char rBuf, mousestatus_t *act)
{
    /* MOUSE_MSS_BUTTON?DOWN -> MOUSE_BUTTON?DOWN */
    static int butmapmss[4] = {	/* Microsoft, MouseMan, GlidePoint,
				   IntelliMouse, Thinking Mouse */
	0,
	MOUSE_BUTTON3DOWN,
	MOUSE_BUTTON1DOWN,
	MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN,
    };
    static int butmapmss2[4] = { /* Microsoft, MouseMan, GlidePoint,
				    Thinking Mouse */
	0,
	MOUSE_BUTTON4DOWN,
	MOUSE_BUTTON2DOWN,
	MOUSE_BUTTON2DOWN | MOUSE_BUTTON4DOWN,
    };
    /* MOUSE_INTELLI_BUTTON?DOWN -> MOUSE_BUTTON?DOWN */
    static int butmapintelli[4] = { /* IntelliMouse, NetMouse, Mie Mouse,
				       MouseMan+ */
	0,
	MOUSE_BUTTON2DOWN,
	MOUSE_BUTTON4DOWN,
	MOUSE_BUTTON2DOWN | MOUSE_BUTTON4DOWN,
    };
    /* MOUSE_MSC_BUTTON?UP -> MOUSE_BUTTON?DOWN */
    static int butmapmsc[8] = {	/* MouseSystems, MMSeries, Logitech,
				   Bus, sysmouse */
	0,
	MOUSE_BUTTON3DOWN,
	MOUSE_BUTTON2DOWN,
	MOUSE_BUTTON2DOWN | MOUSE_BUTTON3DOWN,
	MOUSE_BUTTON1DOWN,
	MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN,
	MOUSE_BUTTON1DOWN | MOUSE_BUTTON2DOWN,
	MOUSE_BUTTON1DOWN | MOUSE_BUTTON2DOWN | MOUSE_BUTTON3DOWN
    };
    /* MOUSE_PS2_BUTTON?DOWN -> MOUSE_BUTTON?DOWN */
    static int butmapps2[8] = {	/* PS/2 */
	0,
	MOUSE_BUTTON1DOWN,
	MOUSE_BUTTON3DOWN,
	MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN,
	MOUSE_BUTTON2DOWN,
	MOUSE_BUTTON1DOWN | MOUSE_BUTTON2DOWN,
	MOUSE_BUTTON2DOWN | MOUSE_BUTTON3DOWN,
	MOUSE_BUTTON1DOWN | MOUSE_BUTTON2DOWN | MOUSE_BUTTON3DOWN
    };
    /* for Hitachi tablet */
    static int butmaphit[8] = {	/* MM HitTablet */
	0,
	MOUSE_BUTTON3DOWN,
	MOUSE_BUTTON2DOWN,
	MOUSE_BUTTON1DOWN,
	MOUSE_BUTTON4DOWN,
	MOUSE_BUTTON5DOWN,
	MOUSE_BUTTON6DOWN,
	MOUSE_BUTTON7DOWN,
    };
    /* for serial VersaPad */
    static int butmapversa[8] = { /* VersaPad */
	0,
	0,
	MOUSE_BUTTON3DOWN,
	MOUSE_BUTTON3DOWN,
	MOUSE_BUTTON1DOWN,
	MOUSE_BUTTON1DOWN,
	MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN,
	MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN,
    };
    /* for PS/2 VersaPad */
    static int butmapversaps2[8] = { /* VersaPad */
	0,
	MOUSE_BUTTON3DOWN,
	0,
	MOUSE_BUTTON3DOWN,
	MOUSE_BUTTON1DOWN,
	MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN,
	MOUSE_BUTTON1DOWN,
	MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN,
    };
    static int           pBufP = 0;
    static unsigned char pBuf[8];
    static int		 prev_x, prev_y;
    static int		 on = FALSE;
    int			 x, y;

    debug("received char 0x%x",(int)rBuf);
    if (rodent.rtype == MOUSE_PROTO_KIDSPAD)
	return (kidspad(rBuf, act));
    if (rodent.rtype == MOUSE_PROTO_GTCO_DIGIPAD)
	return (gtco_digipad(rBuf, act));

    /*
     * Hack for resyncing: We check here for a package that is:
     *  a) illegal (detected by wrong data-package header)
     *  b) invalid (0x80 == -128 and that might be wrong for MouseSystems)
     *  c) bad header-package
     *
     * NOTE: b) is a voilation of the MouseSystems-Protocol, since values of
     *       -128 are allowed, but since they are very seldom we can easily
     *       use them as package-header with no button pressed.
     * NOTE/2: On a PS/2 mouse any byte is valid as a data byte. Furthermore,
     *         0x80 is not valid as a header byte. For a PS/2 mouse we skip
     *         checking data bytes.
     *         For resyncing a PS/2 mouse we require the two most significant
     *         bits in the header byte to be 0. These are the overflow bits,
     *         and in case of an overflow we actually lose sync. Overflows
     *         are very rare, however, and we quickly gain sync again after
     *         an overflow condition. This is the best we can do. (Actually,
     *         we could use bit 0x08 in the header byte for resyncing, since
     *         that bit is supposed to be always on, but nobody told
     *         Microsoft...)
     */

    if (pBufP != 0 && rodent.rtype != MOUSE_PROTO_PS2 &&
	((rBuf & cur_proto[2]) != cur_proto[3] || rBuf == 0x80))
    {
	pBufP = 0;		/* skip package */
    }

    if (pBufP == 0 && (rBuf & cur_proto[0]) != cur_proto[1])
	return (0);

    /* is there an extra data byte? */
    if (pBufP >= cur_proto[4] && (rBuf & cur_proto[0]) != cur_proto[1])
    {
	/*
	 * Hack for Logitech MouseMan Mouse - Middle button
	 *
	 * Unfortunately this mouse has variable length packets: the standard
	 * Microsoft 3 byte packet plus an optional 4th byte whenever the
	 * middle button status changes.
	 *
	 * We have already processed the standard packet with the movement
	 * and button info.  Now post an event message with the old status
	 * of the left and right buttons and the updated middle button.
	 */

	/*
	 * Even worse, different MouseMen and TrackMen differ in the 4th
	 * byte: some will send 0x00/0x20, others 0x01/0x21, or even
	 * 0x02/0x22, so I have to strip off the lower bits.
	 *
	 * [JCH-96/01/21]
	 * HACK for ALPS "fourth button". (It's bit 0x10 of the "fourth byte"
	 * and it is activated by tapping the glidepad with the finger! 8^)
	 * We map it to bit bit3, and the reverse map in xf86Events just has
	 * to be extended so that it is identified as Button 4. The lower
	 * half of the reverse-map may remain unchanged.
	 */

	/*
	 * [KY-97/08/03]
	 * Receive the fourth byte only when preceding three bytes have
	 * been detected (pBufP >= cur_proto[4]).  In the previous
	 * versions, the test was pBufP == 0; thus, we may have mistakingly
	 * received a byte even if we didn't see anything preceding
	 * the byte.
	 */

	if ((rBuf & cur_proto[5]) != cur_proto[6]) {
	    pBufP = 0;
	    return (0);
	}

	switch (rodent.rtype) {
#if notyet
	case MOUSE_PROTO_MARIQUA:
	    /*
	     * This mouse has 16! buttons in addition to the standard
	     * three of them.  They return 0x10 though 0x1f in the
	     * so-called `ten key' mode and 0x30 though 0x3f in the
	     * `function key' mode.  As there are only 31 bits for
	     * button state (including the standard three), we ignore
	     * the bit 0x20 and don't distinguish the two modes.
	     */
	    act->dx = act->dy = act->dz = 0;
	    act->obutton = act->button;
	    rBuf &= 0x1f;
	    act->button = (1 << (rBuf - 13))
		| (act->obutton & (MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN));
	    /*
	     * FIXME: this is a button "down" event. There needs to be
	     * a corresponding button "up" event... XXX
	     */
	    break;
#endif /* notyet */
	case MOUSE_PROTO_JOGDIAL:
	    break;

	/*
	 * IntelliMouse, NetMouse (including NetMouse Pro) and Mie Mouse
	 * always send the fourth byte, whereas the fourth byte is
	 * optional for GlidePoint and ThinkingMouse. The fourth byte
	 * is also optional for MouseMan+ and FirstMouse+ in their
	 * native mode. It is always sent if they are in the IntelliMouse
	 * compatible mode.
	 */
	case MOUSE_PROTO_INTELLI:	/* IntelliMouse, NetMouse, Mie Mouse,
					   MouseMan+ */
	    act->dx = act->dy = 0;
	    act->dz = (rBuf & 0x08) ? (rBuf & 0x0f) - 16 : (rBuf & 0x0f);
	    if ((act->dz >= 7) || (act->dz <= -7))
		act->dz = 0;
	    act->obutton = act->button;
	    act->button = butmapintelli[(rBuf & MOUSE_MSS_BUTTONS) >> 4]
		| (act->obutton & (MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN));
	    break;

	default:
	    act->dx = act->dy = act->dz = 0;
	    act->obutton = act->button;
	    act->button = butmapmss2[(rBuf & MOUSE_MSS_BUTTONS) >> 4]
		| (act->obutton & (MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN));
	    break;
	}

	act->flags = ((act->dx || act->dy || act->dz) ? MOUSE_POSCHANGED : 0)
	    | (act->obutton ^ act->button);
	pBufP = 0;
	return (act->flags);
    }

    if (pBufP >= cur_proto[4])
	pBufP = 0;
    pBuf[pBufP++] = rBuf;
    if (pBufP != cur_proto[4])
	return (0);

    /*
     * assembly full package
     */

    debug("assembled full packet (len %d) %x,%x,%x,%x,%x,%x,%x,%x",
	cur_proto[4],
	pBuf[0], pBuf[1], pBuf[2], pBuf[3],
	pBuf[4], pBuf[5], pBuf[6], pBuf[7]);

    act->dz = 0;
    act->obutton = act->button;
    switch (rodent.rtype)
    {
    case MOUSE_PROTO_MS:		/* Microsoft */
    case MOUSE_PROTO_LOGIMOUSEMAN:	/* MouseMan/TrackMan */
    case MOUSE_PROTO_X10MOUSEREM:	/* X10 MouseRemote */
	act->button = act->obutton & MOUSE_BUTTON4DOWN;
	if (rodent.flags & ChordMiddle)
	    act->button |= ((pBuf[0] & MOUSE_MSS_BUTTONS) == MOUSE_MSS_BUTTONS)
		? MOUSE_BUTTON2DOWN
		: butmapmss[(pBuf[0] & MOUSE_MSS_BUTTONS) >> 4];
	else
	    act->button |= (act->obutton & MOUSE_BUTTON2DOWN)
		| butmapmss[(pBuf[0] & MOUSE_MSS_BUTTONS) >> 4];

	/* Send X10 btn events to remote client (ensure -128-+127 range) */
	if ((rodent.rtype == MOUSE_PROTO_X10MOUSEREM) &&
	    ((pBuf[0] & 0xFC) == 0x44) && (pBuf[2] == 0x3F)) {
	    if (rodent.mremcfd >= 0) {
		unsigned char key = (signed char)(((pBuf[0] & 0x03) << 6) |
						  (pBuf[1] & 0x3F));
		write(rodent.mremcfd, &key, 1);
	    }
	    return (0);
	}

	act->dx = (signed char)(((pBuf[0] & 0x03) << 6) | (pBuf[1] & 0x3F));
	act->dy = (signed char)(((pBuf[0] & 0x0C) << 4) | (pBuf[2] & 0x3F));
	break;

    case MOUSE_PROTO_GLIDEPOINT:	/* GlidePoint */
    case MOUSE_PROTO_THINK:		/* ThinkingMouse */
    case MOUSE_PROTO_INTELLI:		/* IntelliMouse, NetMouse, Mie Mouse,
					   MouseMan+ */
	act->button = (act->obutton & (MOUSE_BUTTON2DOWN | MOUSE_BUTTON4DOWN))
	    | butmapmss[(pBuf[0] & MOUSE_MSS_BUTTONS) >> 4];
	act->dx = (signed char)(((pBuf[0] & 0x03) << 6) | (pBuf[1] & 0x3F));
	act->dy = (signed char)(((pBuf[0] & 0x0C) << 4) | (pBuf[2] & 0x3F));
	break;

    case MOUSE_PROTO_MSC:		/* MouseSystems Corp */
#if notyet
    case MOUSE_PROTO_MARIQUA:		/* Mariqua */
#endif
	act->button = butmapmsc[(~pBuf[0]) & MOUSE_MSC_BUTTONS];
	act->dx =    (signed char)(pBuf[1]) + (signed char)(pBuf[3]);
	act->dy = - ((signed char)(pBuf[2]) + (signed char)(pBuf[4]));
	break;

    case MOUSE_PROTO_JOGDIAL:		/* JogDial */
	    if (rBuf == 0x6c)
	      act->dz = -1;
	    if (rBuf == 0x72)
	      act->dz = 1;
	    if (rBuf == 0x64)
	      act->button = MOUSE_BUTTON1DOWN;
	    if (rBuf == 0x75)
	      act->button = 0;
	break;

    case MOUSE_PROTO_HITTAB:		/* MM HitTablet */
	act->button = butmaphit[pBuf[0] & 0x07];
	act->dx = (pBuf[0] & MOUSE_MM_XPOSITIVE) ?   pBuf[1] : - pBuf[1];
	act->dy = (pBuf[0] & MOUSE_MM_YPOSITIVE) ? - pBuf[2] :   pBuf[2];
	break;

    case MOUSE_PROTO_MM:		/* MM Series */
    case MOUSE_PROTO_LOGI:		/* Logitech Mice */
	act->button = butmapmsc[pBuf[0] & MOUSE_MSC_BUTTONS];
	act->dx = (pBuf[0] & MOUSE_MM_XPOSITIVE) ?   pBuf[1] : - pBuf[1];
	act->dy = (pBuf[0] & MOUSE_MM_YPOSITIVE) ? - pBuf[2] :   pBuf[2];
	break;

    case MOUSE_PROTO_VERSAPAD:		/* VersaPad */
	act->button = butmapversa[(pBuf[0] & MOUSE_VERSA_BUTTONS) >> 3];
	act->button |= (pBuf[0] & MOUSE_VERSA_TAP) ? MOUSE_BUTTON4DOWN : 0;
	act->dx = act->dy = 0;
	if (!(pBuf[0] & MOUSE_VERSA_IN_USE)) {
	    on = FALSE;
	    break;
	}
	x = (pBuf[2] << 6) | pBuf[1];
	if (x & 0x800)
	    x -= 0x1000;
	y = (pBuf[4] << 6) | pBuf[3];
	if (y & 0x800)
	    y -= 0x1000;
	if (on) {
	    act->dx = prev_x - x;
	    act->dy = prev_y - y;
	} else {
	    on = TRUE;
	}
	prev_x = x;
	prev_y = y;
	break;

    case MOUSE_PROTO_PS2:		/* PS/2 */
	act->button = butmapps2[pBuf[0] & MOUSE_PS2_BUTTONS];
	act->dx = (pBuf[0] & MOUSE_PS2_XNEG) ?    pBuf[1] - 256  :  pBuf[1];
	act->dy = (pBuf[0] & MOUSE_PS2_YNEG) ?  -(pBuf[2] - 256) : -pBuf[2];
	/*
	 * Moused usually operates the psm driver at the operation level 1
	 * which sends mouse data in MOUSE_PROTO_SYSMOUSE protocol.
	 * The following code takes effect only when the user explicitly
	 * requets the level 2 at which wheel movement and additional button
	 * actions are encoded in model-dependent formats. At the level 0
	 * the following code is no-op because the psm driver says the model
	 * is MOUSE_MODEL_GENERIC.
	 */
	switch (rodent.hw.model) {
	case MOUSE_MODEL_EXPLORER:
	    /* wheel and additional button data is in the fourth byte */
	    act->dz = (pBuf[3] & MOUSE_EXPLORER_ZNEG)
		? (pBuf[3] & 0x0f) - 16 : (pBuf[3] & 0x0f);
	    act->button |= (pBuf[3] & MOUSE_EXPLORER_BUTTON4DOWN)
		? MOUSE_BUTTON4DOWN : 0;
	    act->button |= (pBuf[3] & MOUSE_EXPLORER_BUTTON5DOWN)
		? MOUSE_BUTTON5DOWN : 0;
	    break;
	case MOUSE_MODEL_INTELLI:
	case MOUSE_MODEL_NET:
	    /* wheel data is in the fourth byte */
	    act->dz = (signed char)pBuf[3];
	    if ((act->dz >= 7) || (act->dz <= -7))
		act->dz = 0;
	    /* some compatible mice may have additional buttons */
	    act->button |= (pBuf[0] & MOUSE_PS2INTELLI_BUTTON4DOWN)
		? MOUSE_BUTTON4DOWN : 0;
	    act->button |= (pBuf[0] & MOUSE_PS2INTELLI_BUTTON5DOWN)
		? MOUSE_BUTTON5DOWN : 0;
	    break;
	case MOUSE_MODEL_MOUSEMANPLUS:
	    if (((pBuf[0] & MOUSE_PS2PLUS_SYNCMASK) == MOUSE_PS2PLUS_SYNC)
		    && (abs(act->dx) > 191)
		    && MOUSE_PS2PLUS_CHECKBITS(pBuf)) {
		/* the extended data packet encodes button and wheel events */
		switch (MOUSE_PS2PLUS_PACKET_TYPE(pBuf)) {
		case 1:
		    /* wheel data packet */
		    act->dx = act->dy = 0;
		    if (pBuf[2] & 0x80) {
			/* horizontal roller count - ignore it XXX*/
		    } else {
			/* vertical roller count */
			act->dz = (pBuf[2] & MOUSE_PS2PLUS_ZNEG)
			    ? (pBuf[2] & 0x0f) - 16 : (pBuf[2] & 0x0f);
		    }
		    act->button |= (pBuf[2] & MOUSE_PS2PLUS_BUTTON4DOWN)
			? MOUSE_BUTTON4DOWN : 0;
		    act->button |= (pBuf[2] & MOUSE_PS2PLUS_BUTTON5DOWN)
			? MOUSE_BUTTON5DOWN : 0;
		    break;
		case 2:
		    /* this packet type is reserved by Logitech */
		    /*
		     * IBM ScrollPoint Mouse uses this packet type to
		     * encode both vertical and horizontal scroll movement.
		     */
		    act->dx = act->dy = 0;
		    /* horizontal roller count */
		    if (pBuf[2] & 0x0f)
			act->dz = (pBuf[2] & MOUSE_SPOINT_WNEG) ? -2 : 2;
		    /* vertical roller count */
		    if (pBuf[2] & 0xf0)
			act->dz = (pBuf[2] & MOUSE_SPOINT_ZNEG) ? -1 : 1;
#if 0
		    /* vertical roller count */
		    act->dz = (pBuf[2] & MOUSE_SPOINT_ZNEG)
			? ((pBuf[2] >> 4) & 0x0f) - 16
			: ((pBuf[2] >> 4) & 0x0f);
		    /* horizontal roller count */
		    act->dw = (pBuf[2] & MOUSE_SPOINT_WNEG)
			? (pBuf[2] & 0x0f) - 16 : (pBuf[2] & 0x0f);
#endif
		    break;
		case 0:
		    /* device type packet - shouldn't happen */
		    /* FALLTHROUGH */
		default:
		    act->dx = act->dy = 0;
		    act->button = act->obutton;
		    debug("unknown PS2++ packet type %d: 0x%02x 0x%02x 0x%02x\n",
			  MOUSE_PS2PLUS_PACKET_TYPE(pBuf),
			  pBuf[0], pBuf[1], pBuf[2]);
		    break;
		}
	    } else {
		/* preserve button states */
		act->button |= act->obutton & MOUSE_EXTBUTTONS;
	    }
	    break;
	case MOUSE_MODEL_GLIDEPOINT:
	    /* `tapping' action */
	    act->button |= ((pBuf[0] & MOUSE_PS2_TAP)) ? 0 : MOUSE_BUTTON4DOWN;
	    break;
	case MOUSE_MODEL_NETSCROLL:
	    /* three additional bytes encode buttons and wheel events */
	    act->button |= (pBuf[3] & MOUSE_PS2_BUTTON3DOWN)
		? MOUSE_BUTTON4DOWN : 0;
	    act->button |= (pBuf[3] & MOUSE_PS2_BUTTON1DOWN)
		? MOUSE_BUTTON5DOWN : 0;
	    act->dz = (pBuf[3] & MOUSE_PS2_XNEG) ? pBuf[4] - 256 : pBuf[4];
	    break;
	case MOUSE_MODEL_THINK:
	    /* the fourth button state in the first byte */
	    act->button |= (pBuf[0] & MOUSE_PS2_TAP) ? MOUSE_BUTTON4DOWN : 0;
	    break;
	case MOUSE_MODEL_VERSAPAD:
	    act->button = butmapversaps2[pBuf[0] & MOUSE_PS2VERSA_BUTTONS];
	    act->button |=
		(pBuf[0] & MOUSE_PS2VERSA_TAP) ? MOUSE_BUTTON4DOWN : 0;
	    act->dx = act->dy = 0;
	    if (!(pBuf[0] & MOUSE_PS2VERSA_IN_USE)) {
		on = FALSE;
		break;
	    }
	    x = ((pBuf[4] << 8) & 0xf00) | pBuf[1];
	    if (x & 0x800)
		x -= 0x1000;
	    y = ((pBuf[4] << 4) & 0xf00) | pBuf[2];
	    if (y & 0x800)
		y -= 0x1000;
	    if (on) {
		act->dx = prev_x - x;
		act->dy = prev_y - y;
	    } else {
		on = TRUE;
	    }
	    prev_x = x;
	    prev_y = y;
	    break;
	case MOUSE_MODEL_4D:
	    act->dx = (pBuf[1] & 0x80) ?    pBuf[1] - 256  :  pBuf[1];
	    act->dy = (pBuf[2] & 0x80) ?  -(pBuf[2] - 256) : -pBuf[2];
	    switch (pBuf[0] & MOUSE_4D_WHEELBITS) {
	    case 0x10:
		act->dz = 1;
		break;
	    case 0x30:
		act->dz = -1;
		break;
	    case 0x40:	/* 2nd wheel rolling right XXX */
		act->dz = 2;
		break;
	    case 0xc0:	/* 2nd wheel rolling left XXX */
		act->dz = -2;
		break;
	    }
	    break;
	case MOUSE_MODEL_4DPLUS:
	    if ((act->dx < 16 - 256) && (act->dy > 256 - 16)) {
		act->dx = act->dy = 0;
		if (pBuf[2] & MOUSE_4DPLUS_BUTTON4DOWN)
		    act->button |= MOUSE_BUTTON4DOWN;
		act->dz = (pBuf[2] & MOUSE_4DPLUS_ZNEG)
			      ? ((pBuf[2] & 0x07) - 8) : (pBuf[2] & 0x07);
	    } else {
		/* preserve previous button states */
		act->button |= act->obutton & MOUSE_EXTBUTTONS;
	    }
	    break;
	case MOUSE_MODEL_GENERIC:
	default:
	    break;
	}
	break;

    case MOUSE_PROTO_SYSMOUSE:		/* sysmouse */
	act->button = butmapmsc[(~pBuf[0]) & MOUSE_SYS_STDBUTTONS];
	act->dx =    (signed char)(pBuf[1]) + (signed char)(pBuf[3]);
	act->dy = - ((signed char)(pBuf[2]) + (signed char)(pBuf[4]));
	if (rodent.level == 1) {
	    act->dz = ((signed char)(pBuf[5] << 1) + (signed char)(pBuf[6] << 1)) >> 1;
	    act->button |= ((~pBuf[7] & MOUSE_SYS_EXTBUTTONS) << 3);
	}
	break;

    default:
	return (0);
    }
    /*
     * We don't reset pBufP here yet, as there may be an additional data
     * byte in some protocols. See above.
     */

    /* has something changed? */
    act->flags = ((act->dx || act->dy || act->dz) ? MOUSE_POSCHANGED : 0)
	| (act->obutton ^ act->button);

    return (act->flags);
}

static int
r_statetrans(mousestatus_t *a1, mousestatus_t *a2, int trans)
{
    int changed;
    int flags;

    a2->dx = a1->dx;
    a2->dy = a1->dy;
    a2->dz = a1->dz;
    a2->obutton = a2->button;
    a2->button = a1->button;
    a2->flags = a1->flags;
    changed = FALSE;

    if (rodent.flags & Emulate3Button) {
	if (debug > 2)
	    debug("state:%d, trans:%d -> state:%d",
		  mouse_button_state, trans,
		  states[mouse_button_state].s[trans]);
	/*
	 * Avoid re-ordering button and movement events. While a button
	 * event is deferred, throw away up to BUTTON2_MAXMOVE movement
	 * events to allow for mouse jitter. If more movement events
	 * occur, then complete the deferred button events immediately.
	 */
	if ((a2->dx != 0 || a2->dy != 0) &&
	    S_DELAYED(states[mouse_button_state].s[trans])) {
		if (++mouse_move_delayed > BUTTON2_MAXMOVE) {
			mouse_move_delayed = 0;
			mouse_button_state =
			    states[mouse_button_state].s[A_TIMEOUT];
			changed = TRUE;
		} else
			a2->dx = a2->dy = 0;
	} else
		mouse_move_delayed = 0;
	if (mouse_button_state != states[mouse_button_state].s[trans])
		changed = TRUE;
	if (changed)
		clock_gettime(CLOCK_MONOTONIC_FAST, &mouse_button_state_ts);
	mouse_button_state = states[mouse_button_state].s[trans];
	a2->button &=
	    ~(MOUSE_BUTTON1DOWN | MOUSE_BUTTON2DOWN | MOUSE_BUTTON3DOWN);
	a2->button &= states[mouse_button_state].mask;
	a2->button |= states[mouse_button_state].buttons;
	flags = a2->flags & MOUSE_POSCHANGED;
	flags |= a2->obutton ^ a2->button;
	if (flags & MOUSE_BUTTON2DOWN) {
	    a2->flags = flags & MOUSE_BUTTON2DOWN;
	    r_timestamp(a2);
	}
	a2->flags = flags;
    }
    return (changed);
}

/* phisical to logical button mapping */
static int p2l[MOUSE_MAXBUTTON] = {
    MOUSE_BUTTON1DOWN, MOUSE_BUTTON2DOWN, MOUSE_BUTTON3DOWN, MOUSE_BUTTON4DOWN,
    MOUSE_BUTTON5DOWN, MOUSE_BUTTON6DOWN, MOUSE_BUTTON7DOWN, MOUSE_BUTTON8DOWN,
    0x00000100,        0x00000200,        0x00000400,        0x00000800,
    0x00001000,        0x00002000,        0x00004000,        0x00008000,
    0x00010000,        0x00020000,        0x00040000,        0x00080000,
    0x00100000,        0x00200000,        0x00400000,        0x00800000,
    0x01000000,        0x02000000,        0x04000000,        0x08000000,
    0x10000000,        0x20000000,        0x40000000,
};

static char *
skipspace(char *s)
{
    while(isspace(*s))
	++s;
    return (s);
}

static int
r_installmap(char *arg)
{
    int pbutton;
    int lbutton;
    char *s;

    while (*arg) {
	arg = skipspace(arg);
	s = arg;
	while (isdigit(*arg))
	    ++arg;
	arg = skipspace(arg);
	if ((arg <= s) || (*arg != '='))
	    return (FALSE);
	lbutton = atoi(s);

	arg = skipspace(++arg);
	s = arg;
	while (isdigit(*arg))
	    ++arg;
	if ((arg <= s) || (!isspace(*arg) && (*arg != '\0')))
	    return (FALSE);
	pbutton = atoi(s);

	if ((lbutton <= 0) || (lbutton > MOUSE_MAXBUTTON))
	    return (FALSE);
	if ((pbutton <= 0) || (pbutton > MOUSE_MAXBUTTON))
	    return (FALSE);
	p2l[pbutton - 1] = 1 << (lbutton - 1);
	mstate[lbutton - 1] = &bstate[pbutton - 1];
    }

    return (TRUE);
}

static void
r_map(mousestatus_t *act1, mousestatus_t *act2)
{
    register int pb;
    register int pbuttons;
    int lbuttons;

    pbuttons = act1->button;
    lbuttons = 0;

    act2->obutton = act2->button;
    if (pbuttons & rodent.wmode) {
	pbuttons &= ~rodent.wmode;
	act1->dz = act1->dy;
	act1->dx = 0;
	act1->dy = 0;
    }
    act2->dx = act1->dx;
    act2->dy = act1->dy;
    act2->dz = act1->dz;

    switch (rodent.zmap[0]) {
    case 0:	/* do nothing */
	break;
    case MOUSE_XAXIS:
	if (act1->dz != 0) {
	    act2->dx = act1->dz;
	    act2->dz = 0;
	}
	break;
    case MOUSE_YAXIS:
	if (act1->dz != 0) {
	    act2->dy = act1->dz;
	    act2->dz = 0;
	}
	break;
    default:	/* buttons */
	pbuttons &= ~(rodent.zmap[0] | rodent.zmap[1]
		    | rodent.zmap[2] | rodent.zmap[3]);
	if ((act1->dz < -1) && rodent.zmap[2]) {
	    pbuttons |= rodent.zmap[2];
	    zstate[2].count = 1;
	} else if (act1->dz < 0) {
	    pbuttons |= rodent.zmap[0];
	    zstate[0].count = 1;
	} else if ((act1->dz > 1) && rodent.zmap[3]) {
	    pbuttons |= rodent.zmap[3];
	    zstate[3].count = 1;
	} else if (act1->dz > 0) {
	    pbuttons |= rodent.zmap[1];
	    zstate[1].count = 1;
	}
	act2->dz = 0;
	break;
    }

    for (pb = 0; (pb < MOUSE_MAXBUTTON) && (pbuttons != 0); ++pb) {
	lbuttons |= (pbuttons & 1) ? p2l[pb] : 0;
	pbuttons >>= 1;
    }
    act2->button = lbuttons;

    act2->flags = ((act2->dx || act2->dy || act2->dz) ? MOUSE_POSCHANGED : 0)
	| (act2->obutton ^ act2->button);
}

static void
r_timestamp(mousestatus_t *act)
{
    struct timespec ts;
    struct timespec ts1;
    struct timespec ts2;
    struct timespec ts3;
    int button;
    int mask;
    int i;

    mask = act->flags & MOUSE_BUTTONS;
#if 0
    if (mask == 0)
	return;
#endif

    clock_gettime(CLOCK_MONOTONIC_FAST, &ts1);
    drift_current_ts = ts1;

    /* double click threshold */
    ts2.tv_sec = rodent.clickthreshold / 1000;
    ts2.tv_nsec = (rodent.clickthreshold % 1000) * 1000000;
    tssub(&ts1, &ts2, &ts);
    debug("ts:  %jd %ld", (intmax_t)ts.tv_sec, ts.tv_nsec);

    /* 3 button emulation timeout */
    ts2.tv_sec = rodent.button2timeout / 1000;
    ts2.tv_nsec = (rodent.button2timeout % 1000) * 1000000;
    tssub(&ts1, &ts2, &ts3);

    button = MOUSE_BUTTON1DOWN;
    for (i = 0; (i < MOUSE_MAXBUTTON) && (mask != 0); ++i) {
	if (mask & 1) {
	    if (act->button & button) {
		/* the button is down */
		debug("  :  %jd %ld",
		    (intmax_t)bstate[i].ts.tv_sec, bstate[i].ts.tv_nsec);
		if (tscmp(&ts, &bstate[i].ts, >)) {
		    bstate[i].count = 1;
		} else {
		    ++bstate[i].count;
		}
		bstate[i].ts = ts1;
	    } else {
		/* the button is up */
		bstate[i].ts = ts1;
	    }
	} else {
	    if (act->button & button) {
		/* the button has been down */
		if (tscmp(&ts3, &bstate[i].ts, >)) {
		    bstate[i].count = 1;
		    bstate[i].ts = ts1;
		    act->flags |= button;
		    debug("button %d timeout", i + 1);
		}
	    } else {
		/* the button has been up */
	    }
	}
	button <<= 1;
	mask >>= 1;
    }
}

static int
r_timeout(void)
{
    struct timespec ts;
    struct timespec ts1;
    struct timespec ts2;

    if (states[mouse_button_state].timeout)
	return (TRUE);
    clock_gettime(CLOCK_MONOTONIC_FAST, &ts1);
    ts2.tv_sec = rodent.button2timeout / 1000;
    ts2.tv_nsec = (rodent.button2timeout % 1000) * 1000000;
    tssub(&ts1, &ts2, &ts);
    return (tscmp(&ts, &mouse_button_state_ts, >));
}

static void
r_click(mousestatus_t *act)
{
    struct mouse_info mouse;
    int button;
    int mask;
    int i;

    mask = act->flags & MOUSE_BUTTONS;
    if (mask == 0)
	return;

    button = MOUSE_BUTTON1DOWN;
    for (i = 0; (i < MOUSE_MAXBUTTON) && (mask != 0); ++i) {
	if (mask & 1) {
	    debug("mstate[%d]->count:%d", i, mstate[i]->count);
	    if (act->button & button) {
		/* the button is down */
		mouse.u.event.value = mstate[i]->count;
	    } else {
		/* the button is up */
		mouse.u.event.value = 0;
	    }
	    mouse.operation = MOUSE_BUTTON_EVENT;
	    mouse.u.event.id = button;
	    if (debug < 2)
		if (!paused)
		    ioctl(rodent.cfd, CONS_MOUSECTL, &mouse);
	    debug("button %d  count %d", i + 1, mouse.u.event.value);
	}
	button <<= 1;
	mask >>= 1;
    }
}

/* $XConsortium: posix_tty.c,v 1.3 95/01/05 20:42:55 kaleb Exp $ */
/* $XFree86: xc/programs/Xserver/hw/xfree86/os-support/shared/posix_tty.c,v 3.4 1995/01/28 17:05:03 dawes Exp $ */
/*
 * Copyright 1993 by David Dawes <dawes@physics.su.oz.au>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of David Dawes
 * not be used in advertising or publicity pertaining to distribution of
 * the software without specific, written prior permission.
 * David Dawes makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 *
 * DAVID DAWES DISCLAIMS ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL DAVID DAWES BE LIABLE FOR
 * ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */


static void
setmousespeed(int old, int new, unsigned cflag)
{
	struct termios tty;
	const char *c;

	if (tcgetattr(rodent.mfd, &tty) < 0)
	{
		logwarn("unable to get status of mouse fd");
		return;
	}

	tty.c_iflag = IGNBRK | IGNPAR;
	tty.c_oflag = 0;
	tty.c_lflag = 0;
	tty.c_cflag = (tcflag_t)cflag;
	tty.c_cc[VTIME] = 0;
	tty.c_cc[VMIN] = 1;

	switch (old)
	{
	case 9600:
		cfsetispeed(&tty, B9600);
		cfsetospeed(&tty, B9600);
		break;
	case 4800:
		cfsetispeed(&tty, B4800);
		cfsetospeed(&tty, B4800);
		break;
	case 2400:
		cfsetispeed(&tty, B2400);
		cfsetospeed(&tty, B2400);
		break;
	case 1200:
	default:
		cfsetispeed(&tty, B1200);
		cfsetospeed(&tty, B1200);
	}

	if (tcsetattr(rodent.mfd, TCSADRAIN, &tty) < 0)
	{
		logwarn("unable to set status of mouse fd");
		return;
	}

	switch (new)
	{
	case 9600:
		c = "*q";
		cfsetispeed(&tty, B9600);
		cfsetospeed(&tty, B9600);
		break;
	case 4800:
		c = "*p";
		cfsetispeed(&tty, B4800);
		cfsetospeed(&tty, B4800);
		break;
	case 2400:
		c = "*o";
		cfsetispeed(&tty, B2400);
		cfsetospeed(&tty, B2400);
		break;
	case 1200:
	default:
		c = "*n";
		cfsetispeed(&tty, B1200);
		cfsetospeed(&tty, B1200);
	}

	if (rodent.rtype == MOUSE_PROTO_LOGIMOUSEMAN
	    || rodent.rtype == MOUSE_PROTO_LOGI)
	{
		if (write(rodent.mfd, c, 2) != 2)
		{
			logwarn("unable to write to mouse fd");
			return;
		}
	}
	usleep(100000);

	if (tcsetattr(rodent.mfd, TCSADRAIN, &tty) < 0)
		logwarn("unable to set status of mouse fd");
}

/*
 * PnP COM device support
 *
 * It's a simplistic implementation, but it works :-)
 * KY, 31/7/97.
 */

/*
 * Try to elicit a PnP ID as described in
 * Microsoft, Hayes: "Plug and Play External COM Device Specification,
 * rev 1.00", 1995.
 *
 * The routine does not fully implement the COM Enumerator as par Section
 * 2.1 of the document.  In particular, we don't have idle state in which
 * the driver software monitors the com port for dynamic connection or
 * removal of a device at the port, because `moused' simply quits if no
 * device is found.
 *
 * In addition, as PnP COM device enumeration procedure slightly has
 * changed since its first publication, devices which follow earlier
 * revisions of the above spec. may fail to respond if the rev 1.0
 * procedure is used. XXX
 */
static int
pnpwakeup1(void)
{
    struct timeval timeout;
    fd_set fds;
    int i;

    /*
     * This is the procedure described in rev 1.0 of PnP COM device spec.
     * Unfortunately, some devices which comform to earlier revisions of
     * the spec gets confused and do not return the ID string...
     */
    debug("PnP COM device rev 1.0 probe...");

    /* port initialization (2.1.2) */
    ioctl(rodent.mfd, TIOCMGET, &i);
    i |= TIOCM_DTR;		/* DTR = 1 */
    i &= ~TIOCM_RTS;		/* RTS = 0 */
    ioctl(rodent.mfd, TIOCMSET, &i);
    usleep(240000);

    /*
     * The PnP COM device spec. dictates that the mouse must set DSR
     * in response to DTR (by hardware or by software) and that if DSR is
     * not asserted, the host computer should think that there is no device
     * at this serial port.  But some mice just don't do that...
     */
    ioctl(rodent.mfd, TIOCMGET, &i);
    debug("modem status 0%o", i);
    if ((i & TIOCM_DSR) == 0)
	return (FALSE);

    /* port setup, 1st phase (2.1.3) */
    setmousespeed(1200, 1200, (CS7 | CREAD | CLOCAL | HUPCL));
    i = TIOCM_DTR | TIOCM_RTS;	/* DTR = 0, RTS = 0 */
    ioctl(rodent.mfd, TIOCMBIC, &i);
    usleep(240000);
    i = TIOCM_DTR;		/* DTR = 1, RTS = 0 */
    ioctl(rodent.mfd, TIOCMBIS, &i);
    usleep(240000);

    /* wait for response, 1st phase (2.1.4) */
    i = FREAD;
    ioctl(rodent.mfd, TIOCFLUSH, &i);
    i = TIOCM_RTS;		/* DTR = 1, RTS = 1 */
    ioctl(rodent.mfd, TIOCMBIS, &i);

    /* try to read something */
    FD_ZERO(&fds);
    FD_SET(rodent.mfd, &fds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 240000;
    if (select(FD_SETSIZE, &fds, NULL, NULL, &timeout) > 0) {
	debug("pnpwakeup1(): valid response in first phase.");
	return (TRUE);
    }

    /* port setup, 2nd phase (2.1.5) */
    i = TIOCM_DTR | TIOCM_RTS;	/* DTR = 0, RTS = 0 */
    ioctl(rodent.mfd, TIOCMBIC, &i);
    usleep(240000);

    /* wait for respose, 2nd phase (2.1.6) */
    i = FREAD;
    ioctl(rodent.mfd, TIOCFLUSH, &i);
    i = TIOCM_DTR | TIOCM_RTS;	/* DTR = 1, RTS = 1 */
    ioctl(rodent.mfd, TIOCMBIS, &i);

    /* try to read something */
    FD_ZERO(&fds);
    FD_SET(rodent.mfd, &fds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 240000;
    if (select(FD_SETSIZE, &fds, NULL, NULL, &timeout) > 0) {
	debug("pnpwakeup1(): valid response in second phase.");
	return (TRUE);
    }

    return (FALSE);
}

static int
pnpwakeup2(void)
{
    struct timeval timeout;
    fd_set fds;
    int i;

    /*
     * This is a simplified procedure; it simply toggles RTS.
     */
    debug("alternate probe...");

    ioctl(rodent.mfd, TIOCMGET, &i);
    i |= TIOCM_DTR;		/* DTR = 1 */
    i &= ~TIOCM_RTS;		/* RTS = 0 */
    ioctl(rodent.mfd, TIOCMSET, &i);
    usleep(240000);

    setmousespeed(1200, 1200, (CS7 | CREAD | CLOCAL | HUPCL));

    /* wait for respose */
    i = FREAD;
    ioctl(rodent.mfd, TIOCFLUSH, &i);
    i = TIOCM_DTR | TIOCM_RTS;	/* DTR = 1, RTS = 1 */
    ioctl(rodent.mfd, TIOCMBIS, &i);

    /* try to read something */
    FD_ZERO(&fds);
    FD_SET(rodent.mfd, &fds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 240000;
    if (select(FD_SETSIZE, &fds, NULL, NULL, &timeout) > 0) {
	debug("pnpwakeup2(): valid response.");
	return (TRUE);
    }

    return (FALSE);
}

static int
pnpgets(char *buf)
{
    struct timeval timeout;
    fd_set fds;
    int begin;
    int i;
    char c;

    if (!pnpwakeup1() && !pnpwakeup2()) {
	/*
	 * According to PnP spec, we should set DTR = 1 and RTS = 0 while
	 * in idle state.  But, `moused' shall set DTR = RTS = 1 and proceed,
	 * assuming there is something at the port even if it didn't
	 * respond to the PnP enumeration procedure.
	 */
	i = TIOCM_DTR | TIOCM_RTS;		/* DTR = 1, RTS = 1 */
	ioctl(rodent.mfd, TIOCMBIS, &i);
	return (0);
    }

    /* collect PnP COM device ID (2.1.7) */
    begin = -1;
    i = 0;
    usleep(240000);	/* the mouse must send `Begin ID' within 200msec */
    while (read(rodent.mfd, &c, 1) == 1) {
	/* we may see "M", or "M3..." before `Begin ID' */
	buf[i++] = c;
	if ((c == 0x08) || (c == 0x28)) {	/* Begin ID */
	    debug("begin-id %02x", c);
	    begin = i - 1;
	    break;
	}
	debug("%c %02x", c, c);
	if (i >= 256)
	    break;
    }
    if (begin < 0) {
	/* we haven't seen `Begin ID' in time... */
	goto connect_idle;
    }

    ++c;			/* make it `End ID' */
    for (;;) {
	FD_ZERO(&fds);
	FD_SET(rodent.mfd, &fds);
	timeout.tv_sec = 0;
	timeout.tv_usec = 240000;
	if (select(FD_SETSIZE, &fds, NULL, NULL, &timeout) <= 0)
	    break;

	read(rodent.mfd, &buf[i], 1);
	if (buf[i++] == c)	/* End ID */
	    break;
	if (i >= 256)
	    break;
    }
    if (begin > 0) {
	i -= begin;
	bcopy(&buf[begin], &buf[0], i);
    }
    /* string may not be human readable... */
    debug("len:%d, '%-*.*s'", i, i, i, buf);

    if (buf[i - 1] == c)
	return (i);		/* a valid PnP string */

    /*
     * According to PnP spec, we should set DTR = 1 and RTS = 0 while
     * in idle state.  But, `moused' shall leave the modem control lines
     * as they are. See above.
     */
connect_idle:

    /* we may still have something in the buffer */
    return (MAX(i, 0));
}

static int
pnpparse(pnpid_t *id, char *buf, int len)
{
    char s[3];
    int offset;
    int sum = 0;
    int i, j;

    id->revision = 0;
    id->eisaid = NULL;
    id->serial = NULL;
    id->class = NULL;
    id->compat = NULL;
    id->description = NULL;
    id->neisaid = 0;
    id->nserial = 0;
    id->nclass = 0;
    id->ncompat = 0;
    id->ndescription = 0;

    if ((buf[0] != 0x28) && (buf[0] != 0x08)) {
	/* non-PnP mice */
	switch(buf[0]) {
	default:
	    return (FALSE);
	case 'M': /* Microsoft */
	    id->eisaid = "PNP0F01";
	    break;
	case 'H': /* MouseSystems */
	    id->eisaid = "PNP0F04";
	    break;
	}
	id->neisaid = strlen(id->eisaid);
	id->class = "MOUSE";
	id->nclass = strlen(id->class);
	debug("non-PnP mouse '%c'", buf[0]);
	return (TRUE);
    }

    /* PnP mice */
    offset = 0x28 - buf[0];

    /* calculate checksum */
    for (i = 0; i < len - 3; ++i) {
	sum += buf[i];
	buf[i] += offset;
    }
    sum += buf[len - 1];
    for (; i < len; ++i)
	buf[i] += offset;
    debug("PnP ID string: '%*.*s'", len, len, buf);

    /* revision */
    buf[1] -= offset;
    buf[2] -= offset;
    id->revision = ((buf[1] & 0x3f) << 6) | (buf[2] & 0x3f);
    debug("PnP rev %d.%02d", id->revision / 100, id->revision % 100);

    /* EISA vender and product ID */
    id->eisaid = &buf[3];
    id->neisaid = 7;

    /* option strings */
    i = 10;
    if (buf[i] == '\\') {
	/* device serial # */
	for (j = ++i; i < len; ++i) {
	    if (buf[i] == '\\')
		break;
	}
	if (i >= len)
	    i -= 3;
	if (i - j == 8) {
	    id->serial = &buf[j];
	    id->nserial = 8;
	}
    }
    if (buf[i] == '\\') {
	/* PnP class */
	for (j = ++i; i < len; ++i) {
	    if (buf[i] == '\\')
		break;
	}
	if (i >= len)
	    i -= 3;
	if (i > j + 1) {
	    id->class = &buf[j];
	    id->nclass = i - j;
	}
    }
    if (buf[i] == '\\') {
	/* compatible driver */
	for (j = ++i; i < len; ++i) {
	    if (buf[i] == '\\')
		break;
	}
	/*
	 * PnP COM spec prior to v0.96 allowed '*' in this field,
	 * it's not allowed now; just igore it.
	 */
	if (buf[j] == '*')
	    ++j;
	if (i >= len)
	    i -= 3;
	if (i > j + 1) {
	    id->compat = &buf[j];
	    id->ncompat = i - j;
	}
    }
    if (buf[i] == '\\') {
	/* product description */
	for (j = ++i; i < len; ++i) {
	    if (buf[i] == ';')
		break;
	}
	if (i >= len)
	    i -= 3;
	if (i > j + 1) {
	    id->description = &buf[j];
	    id->ndescription = i - j;
	}
    }

    /* checksum exists if there are any optional fields */
    if ((id->nserial > 0) || (id->nclass > 0)
	|| (id->ncompat > 0) || (id->ndescription > 0)) {
	debug("PnP checksum: 0x%X", sum);
	sprintf(s, "%02X", sum & 0x0ff);
	if (strncmp(s, &buf[len - 3], 2) != 0) {
#if 0
	    /*
	     * I found some mice do not comply with the PnP COM device
	     * spec regarding checksum... XXX
	     */
	    logwarnx("PnP checksum error", 0);
	    return (FALSE);
#endif
	}
    }

    return (TRUE);
}

static symtab_t *
pnpproto(pnpid_t *id)
{
    symtab_t *t;
    int i, j;

    if (id->nclass > 0)
	if (strncmp(id->class, "MOUSE", id->nclass) != 0 &&
	    strncmp(id->class, "TABLET", id->nclass) != 0)
	    /* this is not a mouse! */
	    return (NULL);

    if (id->neisaid > 0) {
	t = gettoken(pnpprod, id->eisaid, id->neisaid);
	if (t->val != MOUSE_PROTO_UNKNOWN)
	    return (t);
    }

    /*
     * The 'Compatible drivers' field may contain more than one
     * ID separated by ','.
     */
    if (id->ncompat <= 0)
	return (NULL);
    for (i = 0; i < id->ncompat; ++i) {
	for (j = i; id->compat[i] != ','; ++i)
	    if (i >= id->ncompat)
		break;
	if (i > j) {
	    t = gettoken(pnpprod, id->compat + j, i - j);
	    if (t->val != MOUSE_PROTO_UNKNOWN)
		return (t);
	}
    }

    return (NULL);
}

/* name/val mapping */

static symtab_t *
gettoken(symtab_t *tab, const char *s, int len)
{
    int i;

    for (i = 0; tab[i].name != NULL; ++i) {
	if (strncmp(tab[i].name, s, len) == 0)
	    break;
    }
    return (&tab[i]);
}

static const char *
gettokenname(symtab_t *tab, int val)
{
    static const char unknown[] = "unknown";
    int i;

    for (i = 0; tab[i].name != NULL; ++i) {
	if (tab[i].val == val)
	    return (tab[i].name);
    }
    return (unknown);
}


/*
 * code to read from the Genius Kidspad tablet.

The tablet responds to the COM PnP protocol 1.0 with EISA-ID KYE0005,
and to pre-pnp probes (RTS toggle) with 'T' (tablet ?)
9600, 8 bit, parity odd.

The tablet puts out 5 bytes. b0 (mask 0xb8, value 0xb8) contains
the proximity, tip and button info:
   (byte0 & 0x1)	true = tip pressed
   (byte0 & 0x2)	true = button pressed
   (byte0 & 0x40)	false = pen in proximity of tablet.

The next 4 bytes are used for coordinates xl, xh, yl, yh (7 bits valid).

Only absolute coordinates are returned, so we use the following approach:
we store the last coordinates sent when the pen went out of the tablet,


 *
 */

typedef enum {
    S_IDLE, S_PROXY, S_FIRST, S_DOWN, S_UP
} k_status;

static int
kidspad(u_char rxc, mousestatus_t *act)
{
    static int buf[5];
    static int buflen = 0, b_prev = 0 , x_prev = -1, y_prev = -1;
    static k_status status = S_IDLE;
    static struct timespec now;

    int x, y;

    if (buflen > 0 && (rxc & 0x80)) {
	fprintf(stderr, "invalid code %d 0x%x\n", buflen, rxc);
	buflen = 0;
    }
    if (buflen == 0 && (rxc & 0xb8) != 0xb8) {
	fprintf(stderr, "invalid code 0 0x%x\n", rxc);
	return (0);	/* invalid code, no action */
    }
    buf[buflen++] = rxc;
    if (buflen < 5)
	return (0);

    buflen = 0;	/* for next time... */

    x = buf[1]+128*(buf[2] - 7);
    if (x < 0) x = 0;
    y = 28*128 - (buf[3] + 128* (buf[4] - 7));
    if (y < 0) y = 0;

    x /= 8;
    y /= 8;

    act->flags = 0;
    act->obutton = act->button;
    act->dx = act->dy = act->dz = 0;
    clock_gettime(CLOCK_MONOTONIC_FAST, &now);
    if (buf[0] & 0x40) /* pen went out of reach */
	status = S_IDLE;
    else if (status == S_IDLE) { /* pen is newly near the tablet */
	act->flags |= MOUSE_POSCHANGED;	/* force update */
	status = S_PROXY;
	x_prev = x;
	y_prev = y;
    }
    act->dx = x - x_prev;
    act->dy = y - y_prev;
    if (act->dx || act->dy)
	act->flags |= MOUSE_POSCHANGED;
    x_prev = x;
    y_prev = y;
    if (b_prev != 0 && b_prev != buf[0]) { /* possibly record button change */
	act->button = 0;
	if (buf[0] & 0x01) /* tip pressed */
	    act->button |= MOUSE_BUTTON1DOWN;
	if (buf[0] & 0x02) /* button pressed */
	    act->button |= MOUSE_BUTTON2DOWN;
	act->flags |= MOUSE_BUTTONSCHANGED;
    }
    b_prev = buf[0];
    return (act->flags);
}

static int
gtco_digipad (u_char rxc, mousestatus_t *act)
{
	static u_char buf[5];
 	static int buflen = 0, b_prev = 0 , x_prev = -1, y_prev = -1;
	static k_status status = S_IDLE;
	int x, y;

#define	GTCO_HEADER	0x80
#define	GTCO_PROXIMITY	0x40
#define	GTCO_START	(GTCO_HEADER|GTCO_PROXIMITY)
#define	GTCO_BUTTONMASK	0x3c


	if (buflen > 0 && ((rxc & GTCO_HEADER) != GTCO_HEADER)) {
		fprintf(stderr, "invalid code %d 0x%x\n", buflen, rxc);
		buflen = 0;
	}
	if (buflen == 0 && (rxc & GTCO_START) != GTCO_START) {
		fprintf(stderr, "invalid code 0 0x%x\n", rxc);
		return (0);	/* invalid code, no action */
	}

	buf[buflen++] = rxc;
	if (buflen < 5)
		return (0);

	buflen = 0;	/* for next time... */

	x = ((buf[2] & ~GTCO_START) << 6 | (buf[1] & ~GTCO_START));
	y = 4768 - ((buf[4] & ~GTCO_START) << 6 | (buf[3] & ~GTCO_START));

	x /= 2.5;
	y /= 2.5;

	act->flags = 0;
	act->obutton = act->button;
	act->dx = act->dy = act->dz = 0;

	if ((buf[0] & 0x40) == 0) /* pen went out of reach */
		status = S_IDLE;
	else if (status == S_IDLE) { /* pen is newly near the tablet */
		act->flags |= MOUSE_POSCHANGED;	/* force update */
		status = S_PROXY;
		x_prev = x;
		y_prev = y;
	}

	act->dx = x - x_prev;
	act->dy = y - y_prev;
	if (act->dx || act->dy)
		act->flags |= MOUSE_POSCHANGED;
	x_prev = x;
	y_prev = y;

	/* possibly record button change */
	if (b_prev != 0 && b_prev != buf[0]) {
		act->button = 0;
		if (buf[0] & 0x04) {
			/* tip pressed/yellow */
			act->button |= MOUSE_BUTTON1DOWN;
		}
		if (buf[0] & 0x08) {
			/* grey/white */
			act->button |= MOUSE_BUTTON2DOWN;
		}
		if (buf[0] & 0x10) {
			/* black/green */
			act->button |= MOUSE_BUTTON3DOWN;
		}
		if (buf[0] & 0x20) {
			/* tip+grey/blue */
			act->button |= MOUSE_BUTTON4DOWN;
		}
		act->flags |= MOUSE_BUTTONSCHANGED;
	}
	b_prev = buf[0];
	return (act->flags);
}

static void
mremote_serversetup(void)
{
    struct sockaddr_un ad;

    /* Open a UNIX domain stream socket to listen for mouse remote clients */
    unlink(_PATH_MOUSEREMOTE);

    if ((rodent.mremsfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
	logerrx(1, "unable to create unix domain socket %s",_PATH_MOUSEREMOTE);

    umask(0111);

    bzero(&ad, sizeof(ad));
    ad.sun_family = AF_UNIX;
    strcpy(ad.sun_path, _PATH_MOUSEREMOTE);
#ifndef SUN_LEN
#define SUN_LEN(unp) (((char *)(unp)->sun_path - (char *)(unp)) + \
		       strlen((unp)->path))
#endif
    if (bind(rodent.mremsfd, (struct sockaddr *) &ad, SUN_LEN(&ad)) < 0)
	logerrx(1, "unable to bind unix domain socket %s", _PATH_MOUSEREMOTE);

    listen(rodent.mremsfd, 1);
}

static void
mremote_clientchg(int add)
{
    struct sockaddr_un ad;
    socklen_t ad_len;
    int fd;

    if (rodent.rtype != MOUSE_PROTO_X10MOUSEREM)
	return;

    if (add) {
	/*  Accept client connection, if we don't already have one  */
	ad_len = sizeof(ad);
	fd = accept(rodent.mremsfd, (struct sockaddr *) &ad, &ad_len);
	if (fd < 0)
	    logwarnx("failed accept on mouse remote socket");

	if (rodent.mremcfd < 0) {
	    rodent.mremcfd = fd;
	    debug("remote client connect...accepted");
	}
	else {
	    close(fd);
	    debug("another remote client connect...disconnected");
	}
    }
    else {
	/* Client disconnected */
	debug("remote client disconnected");
	close(rodent.mremcfd);
	rodent.mremcfd = -1;
    }
}
