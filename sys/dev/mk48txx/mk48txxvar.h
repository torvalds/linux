/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 *
 *	$NetBSD: mk48txxvar.h,v 1.6 2008/04/28 20:23:50 martin Exp $
 *
 * $FreeBSD$
 */

typedef uint8_t (*mk48txx_nvrd_t)(device_t dev, int off);
typedef void (*mk48txx_nvwr_t)(device_t dev, int off, uint8_t v);

struct mk48txx_softc {
	struct resource		*sc_res;/* bus resource */

	struct mtx		sc_mtx;	/* hardware mutex */
	eventhandler_tag	sc_wet;	/* watchdog event handler tag */

	const char	*sc_model;	/* chip model name */
	bus_size_t	sc_nvramsz;	/* Size of NVRAM on the chip */
	bus_size_t	sc_clkoffset;	/* Offset in NVRAM to clock bits */
	u_int		sc_year0;	/* year counter offset */
	u_int		sc_flag;	/* MD flags */
#define	MK48TXX_NO_CENT_ADJUST	0x0001	/* don't manually adjust century */
#define	MK48TXX_WDOG_REGISTER	0x0002	/* register watchdog */
#define	MK48TXX_WDOG_ENABLE_WDS	0x0004	/* enable watchdog steering bit */

	mk48txx_nvrd_t	sc_nvrd;	/* NVRAM/RTC read function */
	mk48txx_nvwr_t	sc_nvwr;	/* NVRAM/RTC write function */
};

/* Chip attach function */
int mk48txx_attach(device_t dev);

/* Methods for the clock interface */
int mk48txx_gettime(device_t dev, struct timespec *ts);
int mk48txx_settime(device_t dev, struct timespec *ts);
