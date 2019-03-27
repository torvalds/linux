/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Poul-Henning Kamp
 * Copyright (c) 2013 iXsystems.com,
 *                    author: Alfred Perlstein <alfred@freebsd.org>
 *
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
 *
 * $FreeBSD$
 */
#ifndef _SYS_WATCHDOG_H
#define	_SYS_WATCHDOG_H

#include <sys/ioccom.h>

#define	_PATH_WATCHDOG	"fido"

#define WDIOCPATPAT	_IOW('W', 42, u_int)	/* pat the watchdog */
#define WDIOC_SETTIMEOUT    _IOW('W', 43, int)	/* set/reset the timer */
#define WDIOC_GETTIMEOUT    _IOR('W', 44, int)	/* get total timeout */
#define WDIOC_GETTIMELEFT   _IOR('W', 45, int)	/* get time left */
#define WDIOC_GETPRETIMEOUT _IOR('W', 46, int)	/* get the pre-timeout */
#define WDIOC_SETPRETIMEOUT _IOW('W', 47, int)	/* set the pre-timeout */
/* set the action when a pre-timeout occurs see: WD_SOFT_* */
#define WDIOC_SETPRETIMEOUTACT _IOW('W', 48, int)

/* use software watchdog instead of hardware */
#define WDIOC_SETSOFT	_IOW('W', 49, int)
#define WDIOC_SETSOFTTIMEOUTACT	_IOW('W', 50, int)

#define WD_ACTIVE	0x8000000
	/* 
	 * Watchdog reset, timeout set to value in WD_INTERVAL field.
	 * The kernel will arm the watchdog and unless the userland
	 * program calls WDIOCPATPAT again before the timer expires
	 * the system will reinitialize.
	 */

#define WD_PASSIVE	0x0400000
	/*
	 * Set the watchdog in passive mode.
	 * The kernel will chose an appropriate timeout duration and
	 * periodically reset the timer provided everything looks all
	 * right to the kernel.
 	 */

#define WD_LASTVAL	0x0200000
	/*
	 * Use the already last used timeout value.
	 * The kernel will use as timeout the last valid timeout provided.
 	 */

#define WD_INTERVAL	0x00000ff
	/*
	 * Mask for duration bits.
	 * The watchdog will have a nominal patience of 2^N * nanoseconds.
	 * Example:  N == 30 gives a patience of 2^30 nanoseconds ~= 1 second.
	 * NB: Expect variance in the +/- 10-20% range.
	 */

/* Handy macros for humans not used to power of two nanoseconds */
#define WD_TO_NEVER	0
#define WD_TO_1MS	20
#define WD_TO_125MS	27
#define WD_TO_250MS	28
#define WD_TO_500MS	29
#define WD_TO_1SEC	30
#define WD_TO_2SEC	31
#define WD_TO_4SEC	32
#define WD_TO_8SEC	33
#define WD_TO_16SEC	34
#define WD_TO_32SEC	35
#define WD_TO_64SEC	36
#define WD_TO_128SEC	37

/* action on pre-timeout trigger */
#define	WD_SOFT_PANIC	0x01	/* panic */
#define	WD_SOFT_DDB	0x02	/* enter debugger */
#define	WD_SOFT_LOG	0x04	/* log(9) */
#define	WD_SOFT_PRINTF	0x08	/* printf(9) */
#define WD_SOFT_MASK	0x0f	/* all of the above */

#ifdef _KERNEL

#include <sys/eventhandler.h>

typedef void (*watchdog_fn)(void *, u_int, int *);

EVENTHANDLER_DECLARE(watchdog_list, watchdog_fn);

u_int	wdog_kern_last_timeout(void);
int	wdog_kern_pat(u_int utim);

/*
 * The following function pointer is used to attach a software watchdog
 * if no hardware watchdog has been attached, and if the software module
 * has initialized the function pointer.
 */

extern void (*wdog_software_attach)(void);
#endif

#endif /* _SYS_WATCHDOG_H */
