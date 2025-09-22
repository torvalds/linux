/* $OpenBSD: mouse_protocols.h,v 1.7 2022/12/28 21:30:19 jmc Exp $ */

/*
 * Copyright (c) 2001 Jean-Baptiste Marchand, Julien Montagne and Jerome Verdon
 *
 * Copyright (c) 1998 by Kazutaka Yokota
 *
 * Copyright (c) 1995 Michael Smith
 *
 * Copyright (c) 1993 by David Dawes <dawes@xfree86.org>
 *
 * Copyright (c) 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 *
 * All rights reserved.
 *
 * Most of this code was taken from the FreeBSD moused daemon, written by
 * Michael Smith. The FreeBSD moused daemon already contained code from the
 * Xfree Project, written by David Dawes and Thomas Roell and Kazutaka Yokota.
 *
 * Adaptation to OpenBSD was done by Jean-Baptiste Marchand, Julien Montagne
 * and Jerome Verdon.
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
 *	This product includes software developed by
 *      David Dawes, Jean-Baptiste Marchand, Julien Montagne, Thomas Roell,
 *      Michael Smith, Jerome Verdon and Kazutaka Yokota.
 * 4. The name authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 */


/* Mouse protocols */

#define P_UNKNOWN 	-2
#define	P_WSCONS	-1	/* wsmouse */
#define P_MS 		0	/* Microsoft Serial, 3 bytes */
#define P_MSC 		1	/* Mouse Systems, 5 bytes */
#define P_LOGI 		2	/* Logitech, 3 bytes */
#define P_MM 		3	/* MM series, 3 bytes */
#define P_LOGIMAN	4	/* Logitech MouseMan 3/4 bytes */
#define P_MMHIT		5	/* Hitachi Tablet 3 bytes */
#define P_GLIDEPOINT	6	/* ALPS GlidePoint, 3/4 bytes */
#define P_IMSERIAL	7	/* MS IntelliMouse, 4 bytes */
#define P_THINKING	8	/* Kensignton Thinking Mouse, 3/4 bytes */

/* flags */

#define MOUSE_BUTTON1DOWN	0x0001	/* left */
#define MOUSE_BUTTON2DOWN	0x0002	/* middle */
#define MOUSE_BUTTON3DOWN	0x0004	/* right */
#define MOUSE_BUTTON4DOWN	0x0008
#define MOUSE_BUTTON5DOWN	0x0010
#define MOUSE_BUTTON6DOWN	0x0020
#define MOUSE_BUTTON7DOWN	0x0040
#define MOUSE_BUTTON8DOWN	0x0080

#define MOUSE_BUTTONS		0x00FF
#define MOUSE_POSCHANGED	0x0100

/*
 * List of all the protocols parameters
 * The parameters are :
 * - size of the packet
 * - synchronization mask
 * - synchronization value (must be equal to data ANDed with SYNCMASK)
 * - mask of buttons
 * - mask of each button separately
 */

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
#define MOUSE_INTELLI_BUTTON2DOWN 0x10	/* middle button the 4th byte */

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

/* Mouse resolutions */

#define MOUSE_RES_DEFAULT	254
#define MOUSE_RES_UNKNOWN	255
#define MOUSE_RES_LOW		0
#define MOUSE_RES_MEDIUMLOW	1
#define MOUSE_RES_MEDIUMHIGH	2
#define MOUSE_RES_HIGH		3

/* Mouse report rates */

#define MOUSE_RATE_UNKNOWN	255
#define MOUSE_RATE_DEFAULT	80
#define MOUSE_RATE_VERY_LOW	20
#define MOUSE_RATE_LOW		40
#define MOUSE_RATE_MEDIUM_LOW	60
#define MOUSE_RATE_MEDIUM_HIGH	80
#define MOUSE_RATE_HIGH		100
#define MOUSE_RATE_VERY_HIGH	200

/* serial PnP ID string */
typedef struct {
    int revision;	/* PnP revision, 100 for 1.00 */
    char *eisaid;	/* EISA ID including mfr ID and product ID */
    char *serial;	/* serial No, optional */
    char *class;	/* device class, optional */
    char *compat;	/* list of compatible drivers, optional */
    char *description;	/* product description, optional */
    int neisaid;	/* length of the above fields... */
    int nserial;
    int nclass;
    int ncompat;
    int ndescription;
} pnpid_t;

/* symbol table entry */
typedef struct {
    char *name;
    int val;
} symtab_t;

/* current status of the mouse */
typedef struct mousestatus {
    int     flags;		/* state change flags */
    int     button;		/* button status */
    int     obutton;		/* previous button status */
    int     dx;			/* x movement */
    int     dy;			/* y movement */
    int     dz;			/* z movement */
    int     dw;			/* w movement */
} mousestatus_t;

/* Prototypes */

void mouse_init(void);
int mouse_identify(void);
int mouse_protocol(unsigned char, mousestatus_t *);
const char *mouse_name(int type);
void wsmouse_init(void);

