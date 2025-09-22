/*	$OpenBSD: clock_subr.h,v 1.8 2022/10/12 13:39:50 kettenis Exp $	*/
/*	$NetBSD: clock_subr.h,v 1.2 1997/03/15 18:11:17 is Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Gordon W. Ross
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Interface to time-of-day clock devices.
 *
 * todr_gettime: convert time-of-day clock into a `struct timeval'
 * todr_settime: set time-of-day clock from a `struct timeval'
 *
 * (this is probably not so useful:)
 * todr_setwen: provide a machine-dependent TOD clock write-enable callback
 *              function which takes one boolean argument:
 *                      1 to enable writes; 0 to disable writes.
 */
struct todr_chip_handle {
	void	*cookie;        /* Device specific data */
	void	*bus_cookie;    /* Bus specific data */

	int	todr_quality;
	int	(*todr_gettime)(struct todr_chip_handle *, struct timeval *);
	int	(*todr_settime)(struct todr_chip_handle *, struct timeval *);
	int	(*todr_setwen)(struct todr_chip_handle *, int);
};
typedef struct todr_chip_handle *todr_chip_handle_t;

#define todr_gettime(ct, t)	((*(ct)->todr_gettime)(ct, t))
#define todr_settime(ct, t)	((*(ct)->todr_settime)(ct, t))
#define todr_wenable(ct, v)	if ((ct)->todr_setwen) \
					((*(ct)->todr_setwen)(ct, v))

void	todr_attach(struct todr_chip_handle *);
