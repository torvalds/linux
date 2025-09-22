/*	$OpenBSD: tty_conf.c,v 1.23 2015/12/22 20:31:51 sf Exp $	*/
/*	$NetBSD: tty_conf.c,v 1.18 1996/05/19 17:17:55 jonathan Exp $	*/

/*-
 * Copyright (c) 1982, 1986, 1991, 1993
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
 *	@(#)tty_conf.c	8.4 (Berkeley) 1/21/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/conf.h>

#include "ppp.h"
#include "nmea.h"
#include "msts.h"
#include "endrun.h"

#define	ttynodisc ((int (*)(dev_t, struct tty *, struct proc *))enodev)
#define	ttyerrclose ((int (*)(struct tty *, int flags, struct proc *))enodev)
#define	ttyerrio ((int (*)(struct tty *, struct uio *, int))enodev)
#define	ttyerrinput ((int (*)(int c, struct tty *))enodev)
#define	ttyerrstart ((int (*)(struct tty *))enodev)

struct	linesw linesw[] =
{
	{ ttyopen, ttylclose, ttread, ttwrite, nullioctl,
	  ttyinput, ttstart, ttymodem },		/* 0- termios */

	{ ttynodisc, ttyerrclose, ttyerrio, ttyerrio, nullioctl,
	  ttyerrinput, ttyerrstart, nullmodem },	/* 1- defunct */

	/* 2- old NTTYDISC (defunct) */
	{ ttynodisc, ttyerrclose, ttyerrio, ttyerrio, nullioctl,
	  ttyerrinput, ttyerrstart, nullmodem },

	/* 3- TABLDISC (defunct) */
	{ ttynodisc, ttyerrclose, ttyerrio, ttyerrio, nullioctl,
	  ttyerrinput, ttyerrstart, nullmodem },

	/* 4- SLIPDISC (defunct) */
	{ ttynodisc, ttyerrclose, ttyerrio, ttyerrio, nullioctl,
	  ttyerrinput, ttyerrstart, nullmodem },

#if NPPP > 0
	{ pppopen, pppclose, pppread, pppwrite, ppptioctl,
	  pppinput, pppstart, ttymodem },		/* 5- PPPDISC */
#else
	{ ttynodisc, ttyerrclose, ttyerrio, ttyerrio, nullioctl,
	  ttyerrinput, ttyerrstart, nullmodem },
#endif

	/* 6- STRIPDISC (defunct) */
	{ ttynodisc, ttyerrclose, ttyerrio, ttyerrio, nullioctl,
	  ttyerrinput, ttyerrstart, nullmodem },

#if NNMEA > 0
	{ nmeaopen, nmeaclose, ttread, ttwrite, nullioctl,
	  nmeainput, ttstart, ttymodem },		/* 7- NMEADISC */
#else
	{ ttynodisc, ttyerrclose, ttyerrio, ttyerrio, nullioctl,
	  ttyerrinput, ttyerrstart, nullmodem },
#endif

#if NMSTS > 0
	{ mstsopen, mstsclose, ttread, ttwrite, nullioctl,
	  mstsinput, ttstart, ttymodem },		/* 8- MSTSDISC */
#else
	{ ttynodisc, ttyerrclose, ttyerrio, ttyerrio, nullioctl,
	  ttyerrinput, ttyerrstart, nullmodem },
#endif

#if NENDRUN > 0
	{ endrunopen, endrunclose, ttread, ttwrite, nullioctl,
	  endruninput, ttstart, ttymodem },		/* 9- ENDRUNDISC */
#else
	{ ttynodisc, ttyerrclose, ttyerrio, ttyerrio, nullioctl,
	  ttyerrinput, ttyerrstart, nullmodem },
#endif
};

int	nlinesw = sizeof (linesw) / sizeof (linesw[0]);

/*
 * Do nothing specific version of line
 * discipline specific ioctl command.
 */
int
nullioctl(struct tty *tp, u_long cmd, char *data, int flags, struct proc *p)
{
	return (-1);
}
