/*	$OpenBSD: mk48txx.c,v 1.10 2022/10/16 01:22:39 jsg Exp $	*/
/*	$NetBSD: mk48txx.c,v 1.7 2001/04/08 17:05:10 tsutsui Exp $ */
/*-
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
 */

/*
 * Mostek MK48T02, MK48T08, MK48T59 time-of-day chip subroutines.
 */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <sys/errno.h>

#include <machine/bus.h>
#include <dev/clock_subr.h>
#include <dev/ic/mk48txxreg.h>


struct mk48txx {
	bus_space_tag_t	mk_bt;		/* bus tag & handle */
	bus_space_handle_t mk_bh;	/* */
	bus_size_t	mk_nvramsz;	/* Size of NVRAM on the chip */
	bus_size_t	mk_clkoffset;	/* Offset in NVRAM to clock bits */
	u_int		mk_year0;	/* What year is represented on the system
					   by the chip's year counter at 0 */
};

int mk48txx_gettime(todr_chip_handle_t, struct timeval *);
int mk48txx_settime(todr_chip_handle_t, struct timeval *);

int mk48txx_auto_century_adjust = 1;

struct {
	const char *name;
	bus_size_t nvramsz;
	bus_size_t clkoff;
	int flags;
#define MK48TXX_EXT_REGISTERS	1	/* Has extended register set */
} mk48txx_models[] = {
	{ "mk48t02", MK48T02_CLKSZ, MK48T02_CLKOFF, 0 },
	{ "mk48t08", MK48T08_CLKSZ, MK48T08_CLKOFF, 0 },
	{ "mk48t18", MK48T18_CLKSZ, MK48T18_CLKOFF, 0 },
	{ "mk48t59", MK48T59_CLKSZ, MK48T59_CLKOFF, MK48TXX_EXT_REGISTERS },
};

todr_chip_handle_t
mk48txx_attach(bus_space_tag_t bt, bus_space_handle_t bh, const char *model,
    int year0)
{
	todr_chip_handle_t handle;
	struct mk48txx *mk;
	bus_size_t nvramsz, clkoff;
	int sz;
	int i;

	printf(": %s", model);

	i = nitems(mk48txx_models);
	while (--i >= 0) {
		if (strcmp(model, mk48txx_models[i].name) == 0) {
			nvramsz = mk48txx_models[i].nvramsz;
			clkoff = mk48txx_models[i].clkoff;
			break;
		}
	}
	if (i < 0) {
		printf(": unsupported model");
		return (NULL);
	}

	sz = ALIGN(sizeof(struct todr_chip_handle)) + sizeof(struct mk48txx);
	handle = malloc(sz, M_DEVBUF, M_NOWAIT);
	if (handle == NULL) {
		printf(": failed to allocate memory");
		return NULL;
	}
	mk = (struct mk48txx *)((u_long)handle +
				 ALIGN(sizeof(struct todr_chip_handle)));
	handle->cookie = mk;
	handle->todr_gettime = mk48txx_gettime;
	handle->todr_settime = mk48txx_settime;
	handle->todr_setwen = NULL;
	handle->todr_quality = 0;
	mk->mk_bt = bt;
	mk->mk_bh = bh;
	mk->mk_nvramsz = nvramsz;
	mk->mk_clkoffset = clkoff;
	mk->mk_year0 = year0;

	return (handle);
}

/*
 * Get time-of-day and convert to a `struct timeval'
 * Return 0 on success; an error number otherwise.
 */
int
mk48txx_gettime(todr_chip_handle_t handle, struct timeval *tv)
{
	struct mk48txx *mk = handle->cookie;
	bus_space_tag_t bt = mk->mk_bt;
	bus_space_handle_t bh = mk->mk_bh;
	bus_size_t clkoff = mk->mk_clkoffset;
	struct clock_ymdhms dt;
	int year;
	u_int8_t csr;

	todr_wenable(handle, 1);

	/* enable read (stop time) */
	csr = bus_space_read_1(bt, bh, clkoff + MK48TXX_ICSR);
	csr |= MK48TXX_CSR_READ;
	bus_space_write_1(bt, bh, clkoff + MK48TXX_ICSR, csr);

	dt.dt_sec = FROMBCD(bus_space_read_1(bt, bh, clkoff + MK48TXX_ISEC));
	dt.dt_min = FROMBCD(bus_space_read_1(bt, bh, clkoff + MK48TXX_IMIN));
	dt.dt_hour = FROMBCD(bus_space_read_1(bt, bh, clkoff + MK48TXX_IHOUR));
	dt.dt_day = FROMBCD(bus_space_read_1(bt, bh, clkoff + MK48TXX_IDAY));
	dt.dt_wday = FROMBCD(bus_space_read_1(bt, bh, clkoff + MK48TXX_IWDAY));
	dt.dt_mon = FROMBCD(bus_space_read_1(bt, bh, clkoff + MK48TXX_IMON));
	year = FROMBCD(bus_space_read_1(bt, bh, clkoff + MK48TXX_IYEAR));

	year += mk->mk_year0;
	if (year < POSIX_BASE_YEAR && mk48txx_auto_century_adjust != 0 &&
	    mk->mk_year0 >= POSIX_BASE_YEAR)
		year += 100;

	dt.dt_year = year;

	/* time wears on */
	csr = bus_space_read_1(bt, bh, clkoff + MK48TXX_ICSR);
	csr &= ~MK48TXX_CSR_READ;
	bus_space_write_1(bt, bh, clkoff + MK48TXX_ICSR, csr);
	todr_wenable(handle, 0);

	/* simple sanity checks */
	if (dt.dt_mon > 12 || dt.dt_day > 31 ||
	    dt.dt_hour >= 24 || dt.dt_min >= 60 || dt.dt_sec >= 60)
		return (1);

	tv->tv_sec = clock_ymdhms_to_secs(&dt);
	tv->tv_usec = 0;
	return (0);
}

/*
 * Set the time-of-day clock based on the value of the `struct timeval' arg.
 * Return 0 on success; an error number otherwise.
 */
int
mk48txx_settime(todr_chip_handle_t handle, struct timeval *tv)
{
	struct mk48txx *mk = handle->cookie;
	bus_space_tag_t bt = mk->mk_bt;
	bus_space_handle_t bh = mk->mk_bh;
	bus_size_t clkoff = mk->mk_clkoffset;
	struct clock_ymdhms dt;
	u_int8_t csr;
	int year;

	/* Note: we ignore `tv_usec' */
	clock_secs_to_ymdhms(tv->tv_sec, &dt);

	year = dt.dt_year - mk->mk_year0;
	if (year > 99 && mk48txx_auto_century_adjust != 0)
		year -= 100;

	todr_wenable(handle, 1);
	/* enable write */
	csr = bus_space_read_1(bt, bh, clkoff + MK48TXX_ICSR);
	csr |= MK48TXX_CSR_WRITE;
	bus_space_write_1(bt, bh, clkoff + MK48TXX_ICSR, csr);

	bus_space_write_1(bt, bh, clkoff + MK48TXX_ISEC, TOBCD(dt.dt_sec));
	bus_space_write_1(bt, bh, clkoff + MK48TXX_IMIN, TOBCD(dt.dt_min));
	bus_space_write_1(bt, bh, clkoff + MK48TXX_IHOUR, TOBCD(dt.dt_hour));
	bus_space_write_1(bt, bh, clkoff + MK48TXX_IWDAY, TOBCD(dt.dt_wday));
	bus_space_write_1(bt, bh, clkoff + MK48TXX_IDAY, TOBCD(dt.dt_day));
	bus_space_write_1(bt, bh, clkoff + MK48TXX_IMON, TOBCD(dt.dt_mon));
	bus_space_write_1(bt, bh, clkoff + MK48TXX_IYEAR, TOBCD(year));

	/* load them up */
	csr = bus_space_read_1(bt, bh, clkoff + MK48TXX_ICSR);
	csr &= ~MK48TXX_CSR_WRITE;
	bus_space_write_1(bt, bh, clkoff + MK48TXX_ICSR, csr);
	todr_wenable(handle, 0);
	return (0);
}

int
mk48txx_get_nvram_size(todr_chip_handle_t handle, bus_size_t *vp)
{
	struct mk48txx *mk = handle->cookie;
	*vp = mk->mk_nvramsz;
	return (0);
}
