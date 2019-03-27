/*-
 * SPDX-License-Identifier: BSD-1-Clause
 *
 * Copyright (c) 1992, 1993 Erik Forsberg.
 * Copyright (c) 1996, 1997 Kazutaka YOKOTA
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
 *
 * $FreeBSD$
 */

#ifndef _SYS_MOUSE_H_
#define _SYS_MOUSE_H_

#include <sys/types.h>
#include <sys/ioccom.h>

/* ioctls */
#define MOUSE_GETSTATUS		_IOR('M', 0, mousestatus_t)
#define MOUSE_GETHWINFO		_IOR('M', 1, mousehw_t)
#define MOUSE_GETMODE		_IOR('M', 2, mousemode_t)
#define MOUSE_SETMODE		_IOW('M', 3, mousemode_t)
#define MOUSE_GETLEVEL		_IOR('M', 4, int)
#define MOUSE_SETLEVEL		_IOW('M', 5, int)
#define MOUSE_READSTATE		_IOWR('M', 8, mousedata_t)
#define MOUSE_READDATA		_IOWR('M', 9, mousedata_t)

#ifdef notyet
#define MOUSE_SETRESOLUTION	_IOW('M', 10, int)
#define MOUSE_SETSCALING	_IOW('M', 11, int)
#define MOUSE_SETRATE		_IOW('M', 12, int)
#define MOUSE_GETHWID		_IOR('M', 13, int)
#endif

#define MOUSE_SYN_GETHWINFO	_IOR('M', 100, synapticshw_t)

/* mouse status block */
typedef struct mousestatus {
    int     flags;		/* state change flags */
    int     button;		/* button status */
    int     obutton;		/* previous button status */
    int     dx;			/* x movement */
    int     dy;			/* y movement */
    int     dz;			/* z movement */
} mousestatus_t;

/* button */
#define MOUSE_BUTTON1DOWN	0x0001	/* left */
#define MOUSE_BUTTON2DOWN	0x0002	/* middle */
#define MOUSE_BUTTON3DOWN	0x0004	/* right */
#define MOUSE_BUTTON4DOWN	0x0008
#define MOUSE_BUTTON5DOWN	0x0010
#define MOUSE_BUTTON6DOWN	0x0020
#define MOUSE_BUTTON7DOWN	0x0040
#define MOUSE_BUTTON8DOWN	0x0080
#define MOUSE_MAXBUTTON		31
#define MOUSE_STDBUTTONS	0x0007		/* buttons 1-3 */
#define MOUSE_EXTBUTTONS	0x7ffffff8	/* the others (28 of them!) */
#define MOUSE_BUTTONS		(MOUSE_STDBUTTONS | MOUSE_EXTBUTTONS)

/* flags */
#define MOUSE_STDBUTTONSCHANGED	MOUSE_STDBUTTONS
#define MOUSE_EXTBUTTONSCHANGED	MOUSE_EXTBUTTONS
#define MOUSE_BUTTONSCHANGED	MOUSE_BUTTONS
#define MOUSE_POSCHANGED	0x80000000

typedef struct mousehw {
	int buttons;		/* -1 if unknown */
	int iftype;		/* MOUSE_IF_XXX */
	int type;		/* mouse/track ball/pad... */
	int model;		/* I/F dependent model ID: MOUSE_MODEL_XXX */
	int hwid;		/* I/F dependent hardware ID
				 * for the PS/2 mouse, it will be PSM_XXX_ID
				 */
} mousehw_t;

typedef struct synapticshw {
	int infoMajor;
	int infoMinor;
	int infoRot180;
	int infoPortrait;
	int infoSensor;
	int infoHardware;
	int infoNewAbs;
	int capPen;
	int infoSimplC;
	int infoGeometry;
	int capExtended;
	int capSleep;
	int capFourButtons;
	int capMultiFinger;
	int capPalmDetect;
	int capPassthrough;
	int capMiddle;
	int capLowPower;
	int capMultiFingerReport;
	int capBallistics;
	int nExtendedButtons;
	int nExtendedQueries;
	int capClickPad;
	int capDeluxeLEDs;
	int noAbsoluteFilter;
	int capReportsV;
	int capUniformClickPad;
	int capReportsMin;
	int capInterTouch;
	int capReportsMax;
	int capClearPad;
	int capAdvancedGestures;
	int multiFingerMode;
	int capCoveredPad;
	int verticalScroll;
	int horizontalScroll;
	int verticalWheel;
	int capEWmode;
	int minimumXCoord;
	int minimumYCoord;
	int maximumXCoord;
	int maximumYCoord;
	int infoXupmm;
	int infoYupmm;
	int forcePad;
	int topButtonPad;
} synapticshw_t;

/* iftype */
#define MOUSE_IF_UNKNOWN	(-1)
#define MOUSE_IF_SERIAL		0
/* 1 was bus */
/* 2 was inport */
#define MOUSE_IF_PS2		3
#define MOUSE_IF_SYSMOUSE	4
#define MOUSE_IF_USB		5

/* type */
#define MOUSE_UNKNOWN		(-1)	/* should be treated as a mouse */
#define MOUSE_MOUSE		0
#define MOUSE_TRACKBALL		1
#define MOUSE_STICK		2
#define MOUSE_PAD		3

/* model */
#define MOUSE_MODEL_UNKNOWN		(-1)
#define MOUSE_MODEL_GENERIC		0
#define MOUSE_MODEL_GLIDEPOINT		1
#define MOUSE_MODEL_NETSCROLL		2
#define MOUSE_MODEL_NET			3
#define MOUSE_MODEL_INTELLI		4
#define MOUSE_MODEL_THINK		5
#define MOUSE_MODEL_EASYSCROLL		6
#define MOUSE_MODEL_MOUSEMANPLUS	7
#define MOUSE_MODEL_KIDSPAD		8
#define MOUSE_MODEL_VERSAPAD		9
#define MOUSE_MODEL_EXPLORER		10
#define MOUSE_MODEL_4D			11
#define MOUSE_MODEL_4DPLUS		12
#define MOUSE_MODEL_SYNAPTICS		13
#define	MOUSE_MODEL_TRACKPOINT		14
#define	MOUSE_MODEL_ELANTECH		15

typedef struct mousemode {
	int protocol;		/* MOUSE_PROTO_XXX */
	int rate;		/* report rate (per sec), -1 if unknown */
	int resolution;		/* MOUSE_RES_XXX, -1 if unknown */
	int accelfactor;	/* accelation factor (must be 1 or greater) */
	int level;		/* driver operation level */
	int packetsize;		/* the length of the data packet */
	unsigned char syncmask[2]; /* sync. data bits in the header byte */
} mousemode_t;

/* protocol */
/*
 * Serial protocols:
 *   Microsoft, MouseSystems, Logitech, MM series, MouseMan, Hitachi Tablet,
 *   GlidePoint, IntelliMouse, Thinking Mouse, MouseRemote, Kidspad,
 *   VersaPad
 * Bus mouse protocols:
 *   bus, InPort -- both of these are now obsolete and will be remvoed soon.
 * PS/2 mouse protocol:
 *   PS/2
 */
#define MOUSE_PROTO_UNKNOWN	(-1)
#define MOUSE_PROTO_MS		0	/* Microsoft Serial, 3 bytes */
#define MOUSE_PROTO_MSC		1	/* Mouse Systems, 5 bytes */
#define MOUSE_PROTO_LOGI	2	/* Logitech, 3 bytes */
#define MOUSE_PROTO_MM		3	/* MM series, 3 bytes */
#define MOUSE_PROTO_LOGIMOUSEMAN 4	/* Logitech MouseMan 3/4 bytes */
#define	MOUSE_PROTO_BUS		5	/* bus mouse -- obsolete */
#define	MOUSE_PROTO_INPORT	6	/* inport mosue -- obsolete */
#define MOUSE_PROTO_PS2		7	/* PS/2 mouse, 3 bytes */
#define MOUSE_PROTO_HITTAB	8	/* Hitachi Tablet 3 bytes */
#define MOUSE_PROTO_GLIDEPOINT	9	/* ALPS GlidePoint, 3/4 bytes */
#define MOUSE_PROTO_INTELLI	10	/* MS IntelliMouse, 4 bytes */
#define MOUSE_PROTO_THINK	11	/* Kensington Thinking Mouse, 3/4 bytes */
#define MOUSE_PROTO_SYSMOUSE	12	/* /dev/sysmouse */
#define MOUSE_PROTO_X10MOUSEREM	13	/* X10 MouseRemote, 3 bytes */
#define MOUSE_PROTO_KIDSPAD	14	/* Genius Kidspad */
#define MOUSE_PROTO_VERSAPAD	15	/* Interlink VersaPad, 6 bytes */
#define MOUSE_PROTO_JOGDIAL	16	/* Vaio's JogDial */
#define MOUSE_PROTO_GTCO_DIGIPAD	17

#define MOUSE_RES_UNKNOWN	(-1)
#define MOUSE_RES_DEFAULT	0
#define MOUSE_RES_LOW		(-2)
#define MOUSE_RES_MEDIUMLOW	(-3)
#define MOUSE_RES_MEDIUMHIGH	(-4)
#define MOUSE_RES_HIGH		(-5)

typedef struct mousedata {
	int len;		/* # of data in the buffer */
	int buf[16];		/* data buffer */
} mousedata_t;

/* Synaptics Touchpad */
#define MOUSE_SYNAPTICS_PACKETSIZE	6	/* '3' works better */

/* Elantech Touchpad */
#define MOUSE_ELANTECH_PACKETSIZE	6

/* Microsoft Serial mouse data packet */
#define MOUSE_MSS_PACKETSIZE	3
#define MOUSE_MSS_SYNCMASK	0x40
#define MOUSE_MSS_SYNC		0x40
#define MOUSE_MSS_BUTTONS	0x30
#define MOUSE_MSS_BUTTON1DOWN	0x20	/* left */
#define MOUSE_MSS_BUTTON2DOWN	0x00	/* no middle button */
#define MOUSE_MSS_BUTTON3DOWN	0x10	/* right */

/* Logitech MouseMan data packet (M+ protocol) */
#define MOUSE_LMAN_BUTTON2DOWN	0x20	/* middle button, the 4th byte */

/* ALPS GlidePoint extension (variant of M+ protocol) */
#define MOUSE_ALPS_BUTTON2DOWN	0x20	/* middle button, the 4th byte */
#define MOUSE_ALPS_TAP		0x10	/* `tapping' action, the 4th byte */

/* Kinsington Thinking Mouse extension (variant of M+ protocol) */
#define MOUSE_THINK_BUTTON2DOWN 0x20	/* lower-left button, the 4th byte */
#define MOUSE_THINK_BUTTON4DOWN 0x10	/* lower-right button, the 4th byte */

/* MS IntelliMouse (variant of MS Serial) */
#define MOUSE_INTELLI_PACKETSIZE 4
#define MOUSE_INTELLI_BUTTON2DOWN 0x10	/* middle button in the 4th byte */

/* Mouse Systems Corp. mouse data packet */
#define MOUSE_MSC_PACKETSIZE	5
#define MOUSE_MSC_SYNCMASK	0xf8
#define MOUSE_MSC_SYNC		0x80
#define MOUSE_MSC_BUTTONS	0x07
#define MOUSE_MSC_BUTTON1UP	0x04	/* left */
#define MOUSE_MSC_BUTTON2UP	0x02	/* middle */
#define MOUSE_MSC_BUTTON3UP	0x01	/* right */
#define MOUSE_MSC_MAXBUTTON	3

/* MM series mouse data packet */
#define MOUSE_MM_PACKETSIZE	3
#define MOUSE_MM_SYNCMASK	0xe0
#define MOUSE_MM_SYNC		0x80
#define MOUSE_MM_BUTTONS	0x07
#define MOUSE_MM_BUTTON1DOWN	0x04	/* left */
#define MOUSE_MM_BUTTON2DOWN	0x02	/* middle */
#define MOUSE_MM_BUTTON3DOWN	0x01	/* right */
#define MOUSE_MM_XPOSITIVE	0x10
#define MOUSE_MM_YPOSITIVE	0x08

/* PS/2 mouse data packet */
#define MOUSE_PS2_PACKETSIZE	3
#define MOUSE_PS2_SYNCMASK	0xc8
#define MOUSE_PS2_SYNC		0x08
#define MOUSE_PS2_BUTTONS	0x07	/* 0x03 for 2 button mouse */
#define MOUSE_PS2_BUTTON1DOWN	0x01	/* left */
#define MOUSE_PS2_BUTTON2DOWN	0x04	/* middle */
#define MOUSE_PS2_BUTTON3DOWN	0x02	/* right */
#define MOUSE_PS2_TAP		MOUSE_PS2_SYNC /* GlidePoint (PS/2) `tapping'
					        * Yes! this is the same bit
						* as SYNC!
					 	*/

#define MOUSE_PS2_XNEG		0x10
#define MOUSE_PS2_YNEG		0x20
#define MOUSE_PS2_XOVERFLOW	0x40
#define MOUSE_PS2_YOVERFLOW	0x80

/* Logitech MouseMan+ (PS/2) data packet (PS/2++ protocol) */
#define MOUSE_PS2PLUS_SYNCMASK	0x48
#define MOUSE_PS2PLUS_SYNC	0x48
#define MOUSE_PS2PLUS_ZNEG	0x08	/* sign bit */
#define MOUSE_PS2PLUS_BUTTON4DOWN 0x10	/* 4th button on MouseMan+ */
#define MOUSE_PS2PLUS_BUTTON5DOWN 0x20

/* IBM ScrollPoint (PS/2) also uses PS/2++ protocol */
#define MOUSE_SPOINT_ZNEG	0x80	/* sign bits */
#define MOUSE_SPOINT_WNEG	0x08

/* MS IntelliMouse (PS/2) data packet */
#define MOUSE_PS2INTELLI_PACKETSIZE 4
/* some compatible mice have additional buttons */
#define MOUSE_PS2INTELLI_BUTTON4DOWN 0x40
#define MOUSE_PS2INTELLI_BUTTON5DOWN 0x80

/* MS IntelliMouse Explorer (PS/2) data packet (variation of IntelliMouse) */
#define MOUSE_EXPLORER_ZNEG	0x08	/* sign bit */
/* IntelliMouse Explorer has additional button data in the fourth byte */
#define MOUSE_EXPLORER_BUTTON4DOWN 0x10
#define MOUSE_EXPLORER_BUTTON5DOWN 0x20

/* Interlink VersaPad (serial I/F) data packet */
#define MOUSE_VERSA_PACKETSIZE	6
#define MOUSE_VERSA_IN_USE	0x04
#define MOUSE_VERSA_SYNCMASK	0xc3
#define MOUSE_VERSA_SYNC	0xc0
#define MOUSE_VERSA_BUTTONS	0x30
#define MOUSE_VERSA_BUTTON1DOWN	0x20	/* left */
#define MOUSE_VERSA_BUTTON2DOWN	0x00	/* middle */
#define MOUSE_VERSA_BUTTON3DOWN	0x10	/* right */
#define MOUSE_VERSA_TAP		0x08

/* Interlink VersaPad (PS/2 I/F) data packet */
#define MOUSE_PS2VERSA_PACKETSIZE	6
#define MOUSE_PS2VERSA_IN_USE		0x10
#define MOUSE_PS2VERSA_SYNCMASK		0xe8
#define MOUSE_PS2VERSA_SYNC		0xc8
#define MOUSE_PS2VERSA_BUTTONS		0x05
#define MOUSE_PS2VERSA_BUTTON1DOWN	0x04	/* left */
#define MOUSE_PS2VERSA_BUTTON2DOWN	0x00	/* middle */
#define MOUSE_PS2VERSA_BUTTON3DOWN	0x01	/* right */
#define MOUSE_PS2VERSA_TAP		0x02

/* A4 Tech 4D Mouse (PS/2) data packet */
#define MOUSE_4D_PACKETSIZE		3
#define MOUSE_4D_WHEELBITS		0xf0

/* A4 Tech 4D+ Mouse (PS/2) data packet */
#define MOUSE_4DPLUS_PACKETSIZE		3
#define MOUSE_4DPLUS_ZNEG		0x04	/* sign bit */
#define MOUSE_4DPLUS_BUTTON4DOWN	0x08

/* sysmouse extended data packet */
/*
 * /dev/sysmouse sends data in two formats, depending on the protocol
 * level.  At the level 0, format is exactly the same as MousSystems'
 * five byte packet.  At the level 1, the first five bytes are the same
 * as at the level 0.  There are additional three bytes which shows
 * `dz' and the states of additional buttons.  `dz' is expressed as the
 * sum of the byte 5 and 6 which contain signed seven bit values.
 * The states of the button 4 though 10 are in the bit 0 though 6 in
 * the byte 7 respectively: 1 indicates the button is up.
 */
#define MOUSE_SYS_PACKETSIZE	8
#define MOUSE_SYS_SYNCMASK	0xf8
#define MOUSE_SYS_SYNC		0x80
#define MOUSE_SYS_BUTTON1UP	0x04	/* left, 1st byte */
#define MOUSE_SYS_BUTTON2UP	0x02	/* middle, 1st byte */
#define MOUSE_SYS_BUTTON3UP	0x01	/* right, 1st byte */
#define MOUSE_SYS_BUTTON4UP	0x0001	/* 7th byte */
#define MOUSE_SYS_BUTTON5UP	0x0002
#define MOUSE_SYS_BUTTON6UP	0x0004
#define MOUSE_SYS_BUTTON7UP	0x0008
#define MOUSE_SYS_BUTTON8UP	0x0010
#define MOUSE_SYS_BUTTON9UP	0x0020
#define MOUSE_SYS_BUTTON10UP	0x0040
#define MOUSE_SYS_MAXBUTTON	10
#define MOUSE_SYS_STDBUTTONS	0x07
#define MOUSE_SYS_EXTBUTTONS	0x7f	/* the others */

/* Mouse remote socket */
#define _PATH_MOUSEREMOTE	"/var/run/MouseRemote"

#endif /* _SYS_MOUSE_H_ */
