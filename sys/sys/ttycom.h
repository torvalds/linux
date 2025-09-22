/*	$OpenBSD: ttycom.h,v 1.17 2018/06/16 13:55:03 deraadt Exp $	*/
/*	$NetBSD: ttycom.h,v 1.4 1996/05/19 17:17:53 jonathan Exp $	*/

/*-
 * Copyright (c) 1982, 1986, 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)ttycom.h	8.1 (Berkeley) 3/28/94
 */

#ifndef	_SYS_TTYCOM_H_
#define	_SYS_TTYCOM_H_

#include <sys/ioccom.h>

/* Tty ioctl's. */

/*
 * Window/terminal size structure.  This information is stored by the kernel
 * in order to provide a consistent interface, but is not used by the kernel.
 */
struct winsize {
	unsigned short	ws_row;		/* rows, in characters */
	unsigned short	ws_col;		/* columns, in characters */
	unsigned short	ws_xpixel;	/* horizontal size, pixels */
	unsigned short	ws_ypixel;	/* vertical size, pixels */
};

struct tstamps {
	int	ts_set;		/* TIOCM_CAR and/or TIOCM_CTS */
	int	ts_clr;
};

#define		TIOCM_LE	0001		/* line enable */
#define		TIOCM_DTR	0002		/* data terminal ready */
#define		TIOCM_RTS	0004		/* request to send */
#define		TIOCM_ST	0010		/* secondary transmit */
#define		TIOCM_SR	0020		/* secondary receive */
#define		TIOCM_CTS	0040		/* clear to send */
#define		TIOCM_CAR	0100		/* carrier detect */
#define		TIOCM_CD	TIOCM_CAR
#define		TIOCM_RNG	0200		/* ring */
#define		TIOCM_RI	TIOCM_RNG
#define		TIOCM_DSR	0400		/* data set ready */
						/* 8-10 compat */
#define	TIOCEXCL	 _IO('t', 13)		/* set exclusive use of tty */
#define	TIOCNXCL	 _IO('t', 14)		/* reset exclusive use of tty */
						/* 15 unused */
#define	TIOCFLUSH	_IOW('t', 16, int)	/* flush buffers */
						/* 17-18 compat */
#define	TIOCGETA	_IOR('t', 19, struct termios) /* get termios struct */
#define	TIOCSETA	_IOW('t', 20, struct termios) /* set termios struct */
#define	TIOCSETAW	_IOW('t', 21, struct termios) /* drain output, set */
#define	TIOCSETAF	_IOW('t', 22, struct termios) /* drn out, fls in, set */
#define	TIOCGETD	_IOR('t', 26, int)	/* get line discipline */
#define	TIOCSETD	_IOW('t', 27, int)	/* set line discipline */
#define	TIOCSETVERAUTH	_IOW('t', 28, int)	/* set verified auth */
#define	TIOCCLRVERAUTH	 _IO('t', 29)		/* clear verified auth */
#define	TIOCCHKVERAUTH	 _IO('t', 30)		/* check verified auth */
						/* 127-124 compat */
#define	TIOCSBRK	 _IO('t', 123)		/* set break bit */
#define	TIOCCBRK	 _IO('t', 122)		/* clear break bit */
#define	TIOCSDTR	 _IO('t', 121)		/* set data terminal ready */
#define	TIOCCDTR	 _IO('t', 120)		/* clear data terminal ready */
#define	TIOCGPGRP	_IOR('t', 119, int)	/* get pgrp of tty */
#define	TIOCSPGRP	_IOW('t', 118, int)	/* set pgrp of tty */
						/* 117-116 compat */
#define	TIOCOUTQ	_IOR('t', 115, int)	/* output queue size */
#define	TIOCNOTTY	 _IO('t', 113)		/* void tty association */
#define	TIOCPKT		_IOW('t', 112, int)	/* pty: set/clear packet mode */
#define		TIOCPKT_DATA		0x00	/* data packet */
#define		TIOCPKT_FLUSHREAD	0x01	/* flush packet */
#define		TIOCPKT_FLUSHWRITE	0x02	/* flush packet */
#define		TIOCPKT_STOP		0x04	/* stop output */
#define		TIOCPKT_START		0x08	/* start output */
#define		TIOCPKT_NOSTOP		0x10	/* no more ^S, ^Q */
#define		TIOCPKT_DOSTOP		0x20	/* now do ^S ^Q */
#define		TIOCPKT_IOCTL		0x40	/* state change of pty driver */
#define	TIOCSTOP	 _IO('t', 111)		/* stop output, like ^S */
#define	TIOCSTART	 _IO('t', 110)		/* start output, like ^Q */
#define	TIOCMSET	_IOW('t', 109, int)	/* set all modem bits */
#define	TIOCMBIS	_IOW('t', 108, int)	/* bis modem bits */
#define	TIOCMBIC	_IOW('t', 107, int)	/* bic modem bits */
#define	TIOCMGET	_IOR('t', 106, int)	/* get all modem bits */
#define	TIOCREMOTE	_IOW('t', 105, int)	/* remote input editing */
#define	TIOCGWINSZ	_IOR('t', 104, struct winsize)	/* get window size */
#define	TIOCSWINSZ	_IOW('t', 103, struct winsize)	/* set window size */
#define	TIOCUCNTL	_IOW('t', 102, int)	/* pty: set/clr usr cntl mode */
#define		UIOCCMD(n)	_IO('u', n)	/* usr cntl op "n" */
#define		TIOCUCNTL_SBRK (TIOCSBRK & 0xff)/* set break bit, usr ctnl */
#define		TIOCUCNTL_CBRK (TIOCCBRK & 0xff)/* clear break bit, usr ctnl */
#define	TIOCSTAT	_IO('t', 101)		/* generate status message */
#define	TIOCGSID	_IOR('t', 99, int)	/* get sid of tty */
#define	TIOCCONS	_IOW('t', 98, int)	/* become virtual console */
#define	TIOCSCTTY	 _IO('t', 97)		/* become controlling tty */
#define	TIOCEXT		_IOW('t', 96, int)	/* pty: external processing */
#define	TIOCSIG		_IOW('t', 95, int)	/* pty: generate signal */
#define	TIOCDRAIN	 _IO('t', 94)		/* wait till output drained */
#define	TIOCGFLAGS	_IOR('t', 93, int)	/* get device flags */
#define	TIOCSFLAGS	_IOW('t', 92, int)	/* set device flags */
#define		TIOCFLAG_SOFTCAR	0x01	/* ignore hardware carrier */
#define		TIOCFLAG_CLOCAL		0x02	/* set clocal on open */
#define		TIOCFLAG_CRTSCTS	0x04	/* set crtscts on open */
#define		TIOCFLAG_MDMBUF		0x08	/* set mdmbuf on open */
#define		TIOCFLAG_PPS		0x10	/* call hardpps on carrier up */
#define	TIOCGTSTAMP	_IOR('t', 91, struct timeval)	/* get timestamp */
#define	TIOCSTSTAMP	_IOW('t', 90, struct tstamps)	/* timestamp reasons */

/* Backwards compatibility */
#define	TIOCMODG	TIOCMGET
#define	TIOCMODS	TIOCMSET

#define	TTYDISC		0		/* termios tty line discipline */
#define	TABLDISC	3		/* tablet discipline */
#define	SLIPDISC	4		/* serial IP discipline */
#define	PPPDISC		5		/* ppp discipline */
#define	STRIPDISC	6		/* metricom wireless IP discipline */
#define	NMEADISC	7		/* NMEA0183 discipline */
#define	MSTSDISC	8		/* Meinberg time string discipline */
#define	ENDRUNDISC	9		/* EndRun time format discipline */

#endif /* !_SYS_TTYCOM_H_ */
