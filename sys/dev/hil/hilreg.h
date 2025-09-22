/*	$OpenBSD: hilreg.h,v 1.6 2006/08/10 23:43:45 miod Exp $	*/
/*	$NetBSD: hilreg.h,v 1.6 1997/02/02 09:39:21 thorpej Exp $	*/

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * from: Utah $Hdr: hilreg.h 1.10 92/01/21$
 *
 *	@(#)hilreg.h	8.1 (Berkeley) 6/10/93
 */

#include <machine/hil_machdep.h>

#define	HIL_BUSY		0x02
#define	HIL_DATA_RDY		0x01

/* HIL status bits */
#define	HIL_POLLDATA	0x10		/* HIL poll data follows */
#define	HIL_COMMAND	0x08		/* Start of original command */
#define	HIL_ERROR	0x80		/* HIL error */

#define	HIL_RECONFIG	0x80		/* HIL has reconfigured */
#define	HIL_UNPLUGGED	0x84		/* HIL cable unplugged */

#define	HIL_SSHIFT	4		/* Bits to shift status over */
#define	HIL_SMASK	0x0f		/* Service request status mask */
#define	HIL_DEVMASK	0x07

/* HIL status types */
#define	HIL_68K		0x04		/* Data from the 68k is ready */
#define	HIL_STATUS	0x05		/* HIL status in data register */
#define	HIL_DATA	0x06		/* HIL data in data register */
#define	HIL_CTRLSHIFT	0x08		/* key + CTRL + SHIFT */
#define	HIL_CTRL	0x09		/* key + CTRL */
#define	HIL_SHIFT	0x0a		/* key + SHIFT */
#define	HIL_KEY		0x0b		/* key only */

/* HIL commands */
#define HIL_IDENTIFY	0x03		/* Get device information */
#define	HIL_READTIME	0x13		/* Read real time register */
#define	HIL_RNAME	0x30		/* Report name */
#define	HIL_RSTATUS	0x31		/* Report status */
#define	HIL_DESCRIBE	0x32		/* Extended describe */
#define HIL_SECURITY	0x33		/* Read security bits */
#define	HIL_DKR		0x3d		/* Disable auto repeat */
#define	HIL_ER1		0x3e		/* Enable auto repeat 1/30 */
#define	HIL_ER2		0x3f		/* Enable auto repeat 1/60 */
#define	HIL_PROMPT1	0x40		/* Prompt #1 */
#define	HIL_PROMPT2	0x41		/* Prompt #2 */
#define	HIL_PROMPT3	0x42		/* Prompt #3 */
#define	HIL_PROMPT4	0x43		/* Prompt #4 */
#define	HIL_PROMPT5	0x44		/* Prompt #5 */
#define	HIL_PROMPT6	0x45		/* Prompt #6 */
#define	HIL_PROMPT7	0x46		/* Prompt #7 */
#define	HIL_PROMPT	0x47		/* Prompt */
#define	HIL_ACK1	0x48		/* Acknowledge #1 */
#define	HIL_ACK2	0x49		/* Acknowledge #2 */
#define	HIL_ACK3	0x4a		/* Acknowledge #3 */
#define	HIL_ACK4	0x4b		/* Acknowledge #4 */
#define	HIL_ACK5	0x4c		/* Acknowledge #5 */
#define	HIL_ACK6	0x4d		/* Acknowledge #6 */
#define	HIL_ACK7	0x4e		/* Acknowledge #7 */
#define	HIL_ACK		0x4f		/* Acknowledge */
#define	HIL_INTON	0x5c		/* Turn on interrupts. */
#define	HIL_INTOFF	0x5d		/* Turn off interrupts. */
#define	HIL_SETARD	0xa0		/* Set auto-repeat delay */
#define	HIL_SETARR	0xa2		/* Set auto-repeat rate */
#define	HIL_SETTONE	0xa3		/* Set tone generator */
#define	HIL_CNMT	0xb2		/* Clear nmi */
#define	HIL_TRIGGER	0xc5		/* Trigger command */
#define	HIL_STARTCMD	0xe0		/* Start loop command */
#define	HIL_TIMEOUT	0xfe		/* Timeout */

/* Read/write various registers on the 8042. */
#define	HIL_READBUSY		0x02	/* internal "busy" register */
#define	HIL_READKBDLANG		0x12	/* read keyboard language code */
#define	HIL_WRITEKBDSADR 	0xe9
#define	HIL_WRITELPSTAT 	0xea
#define	HIL_WRITELPCTRL 	0xeb
#define	HIL_READKBDSADR	 	0xf9
#define	HIL_READLPSTAT  	0xfa
#define	HIL_READLPCTRL  	0xfb

/* BUSY bits */
#define	BSY_LOOPBUSY	0x04

/* LPCTRL bits */
#define	LPC_AUTOPOLL	0x01	/* enable auto-polling */
#define	LPC_NOERROR	0x02	/* don't report errors */
#define	LPC_NORECONF	0x04	/* don't report reconfigure */
#define	LPC_KBDCOOK	0x10	/* cook all keyboards */
#define	LPC_RECONF	0x80	/* reconfigure the loop */

/* LPSTAT bits */
#define	LPS_DEVMASK	0x07	/* number of loop devices */
#define	LPS_CONFGOOD	0x08	/* reconfiguration worked */
#define	LPS_CONFFAIL	0x80	/* reconfiguration failed */

/* HIL packet headers */
#define	HIL_MOUSEDATA   0x02
#define	HIL_KBDDATA     0x70
  
#define	HIL_MOUSEMOTION	0x02	/* mouse movement event */
#define	HIL_TABLET      0x02	/* tablet motion event */
#define	HIL_KNOBBOX     0x03	/* knob box motion data */
#define	HIL_KBDBUTTON	0x40	/* keyboard button event */
#define	HIL_MOUSEBUTTON 0x40	/* mouse button event */
#define	HIL_BUTTONBOX   0x60	/* button box event */

/* ID module defines */
#define HILSCBIT	0x04

/* For setting auto repeat on the keyboard */
#define	ar_format(x)	~((x - 10) / 10)
#define	KBD_ARD		400	/* initial delay in msec (10 - 2560) */
#define	KBD_ARR		60	/* rate (10 - 2550 msec, 2551 == off) */

/* Device information bits */
#define	HIL_ABSOLUTE	0x40	/* absolute positioning data */
#define	HIL_16_BITS	0x20	/* 16 bit position accuracy */
#define	HIL_IOB		0x10	/* I/O description byte follows */
#define	HIL_AXMASK	0x03	/* Number of axes supported */

#define	HILIOB_PROMPT	0x80	/* prompt and acknowledge (leds) supported */
#define	HILIOB_PMASK	0x70	/* number of prompt & acknowledge supported */
#define	HILIOB_PIO	0x08	/* proximity in/out (pressure) supported */
#define	HILIOB_BMASK	0x07	/* number of buttons supported */
