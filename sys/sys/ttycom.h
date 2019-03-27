/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 * $FreeBSD$
 */

#ifndef	_SYS_TTYCOM_H_
#define	_SYS_TTYCOM_H_

#include <sys/ioccom.h>

/*
 * Tty ioctl's except for those supported only for backwards compatibility
 * with the old tty driver.
 */

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

						/* 0-2 compat */
						/* 3-7 unused */
						/* 8-10 compat */
						/* 11-12 unused */
#define	TIOCEXCL	 _IO('t', 13)		/* set exclusive use of tty */
#define	TIOCNXCL	 _IO('t', 14)		/* reset exclusive use of tty */
#define	TIOCGPTN	_IOR('t', 15, int)	/* Get pts number. */
#define	TIOCFLUSH	_IOW('t', 16, int)	/* flush buffers */
						/* 17-18 compat */
#define	TIOCGETA	_IOR('t', 19, struct termios) /* get termios struct */
#define	TIOCSETA	_IOW('t', 20, struct termios) /* set termios struct */
#define	TIOCSETAW	_IOW('t', 21, struct termios) /* drain output, set */
#define	TIOCSETAF	_IOW('t', 22, struct termios) /* drn out, fls in, set */
						/* 23-25 unused */
#define	TIOCGETD	_IOR('t', 26, int)	/* get line discipline */
#define	TIOCSETD	_IOW('t', 27, int)	/* set line discipline */
#define	TIOCPTMASTER	 _IO('t', 28)		/* pts master validation */
						/* 29-85 unused */
#define	TIOCGDRAINWAIT	_IOR('t', 86, int)	/* get ttywait timeout */
#define	TIOCSDRAINWAIT	_IOW('t', 87, int)	/* set ttywait timeout */
						/* 88 unused */
						/* 89-91 conflicts: tun and tap */
#define	TIOCTIMESTAMP	_IOR('t', 89, struct timeval)	/* enable/get timestamp
						 * of last input event */
#define	TIOCMGDTRWAIT	_IOR('t', 90, int)	/* modem: get wait on close */
#define	TIOCMSDTRWAIT	_IOW('t', 91, int)	/* modem: set wait on close */
						/* 92-93 tun and tap */
						/* 94-97 conflicts: tun and tap */
#define	TIOCDRAIN	 _IO('t', 94)		/* wait till output drained */
#define	TIOCSIG		_IOWINT('t', 95)	/* pty: generate signal */
#define	TIOCEXT		_IOW('t', 96, int)	/* pty: external processing */
#define	TIOCSCTTY	 _IO('t', 97)		/* become controlling tty */
#define	TIOCCONS	_IOW('t', 98, int)	/* become virtual console */
#define	TIOCGSID	_IOR('t', 99, int)	/* get session id */
						/* 100 unused */
#define	TIOCSTAT	 _IO('t', 101)		/* simulate ^T status message */
#define	TIOCUCNTL	_IOW('t', 102, int)	/* pty: set/clr usr cntl mode */
#define		UIOCCMD(n)	_IO('u', n)	/* usr cntl op "n" */
#define	TIOCSWINSZ	_IOW('t', 103, struct winsize)	/* set window size */
#define	TIOCGWINSZ	_IOR('t', 104, struct winsize)	/* get window size */
						/* 105 unused */
#define	TIOCMGET	_IOR('t', 106, int)	/* get all modem bits */
#define		TIOCM_LE	0001		/* line enable */
#define		TIOCM_DTR	0002		/* data terminal ready */
#define		TIOCM_RTS	0004		/* request to send */
#define		TIOCM_ST	0010		/* secondary transmit */
#define		TIOCM_SR	0020		/* secondary receive */
#define		TIOCM_CTS	0040		/* clear to send */
#define		TIOCM_DCD	0100		/* data carrier detect */
#define		TIOCM_RI	0200		/* ring indicate */
#define		TIOCM_DSR	0400		/* data set ready */
#define		TIOCM_CD	TIOCM_DCD
#define		TIOCM_CAR	TIOCM_DCD
#define		TIOCM_RNG	TIOCM_RI
#define	TIOCMBIC	_IOW('t', 107, int)	/* bic modem bits */
#define	TIOCMBIS	_IOW('t', 108, int)	/* bis modem bits */
#define	TIOCMSET	_IOW('t', 109, int)	/* set all modem bits */
#define	TIOCSTART	 _IO('t', 110)		/* start output, like ^Q */
#define	TIOCSTOP	 _IO('t', 111)		/* stop output, like ^S */
#define	TIOCPKT		_IOW('t', 112, int)	/* pty: set/clear packet mode */
#define		TIOCPKT_DATA		0x00	/* data packet */
#define		TIOCPKT_FLUSHREAD	0x01	/* flush packet */
#define		TIOCPKT_FLUSHWRITE	0x02	/* flush packet */
#define		TIOCPKT_STOP		0x04	/* stop output */
#define		TIOCPKT_START		0x08	/* start output */
#define		TIOCPKT_NOSTOP		0x10	/* no more ^S, ^Q */
#define		TIOCPKT_DOSTOP		0x20	/* now do ^S ^Q */
#define		TIOCPKT_IOCTL		0x40	/* state change of pty driver */
#define	TIOCNOTTY	 _IO('t', 113)		/* void tty association */
#define	TIOCSTI		_IOW('t', 114, char)	/* simulate terminal input */
#define	TIOCOUTQ	_IOR('t', 115, int)	/* output queue size */
						/* 116-117 compat */
#define	TIOCSPGRP	_IOW('t', 118, int)	/* set pgrp of tty */
#define	TIOCGPGRP	_IOR('t', 119, int)	/* get pgrp of tty */
#define	TIOCCDTR	 _IO('t', 120)		/* clear data terminal ready */
#define	TIOCSDTR	 _IO('t', 121)		/* set data terminal ready */
#define	TIOCCBRK	 _IO('t', 122)		/* clear break bit */
#define	TIOCSBRK	 _IO('t', 123)		/* set break bit */
						/* 124-127 compat */

#define	TTYDISC		0		/* termios tty line discipline */
#define	SLIPDISC	4		/* serial IP discipline */
#define	PPPDISC		5		/* PPP discipline */
#define	NETGRAPHDISC	6		/* Netgraph tty node discipline */
#define	H4DISC		7		/* Netgraph Bluetooth H4 discipline */

#endif /* !_SYS_TTYCOM_H_ */
