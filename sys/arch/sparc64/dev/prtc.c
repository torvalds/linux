/*	$OpenBSD: prtc.c,v 1.7 2022/10/12 13:39:50 kettenis Exp $	*/

/*
 * Copyright (c) 2008 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Driver to get the time-of-day from the PROM for machines that don't
 * have a hardware real-time clock, like the Enterprise 10000,
 * Fire 12K and Fire 15K.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/autoconf.h>
#include <machine/openfirm.h>
#include <machine/sparc64.h>

#include <dev/clock_subr.h>

extern todr_chip_handle_t todr_handle;

struct prtc_softc {
	struct device	sc_dev;
	struct todr_chip_handle
			sc_todr_chip;
};

int	prtc_match(struct device *, void *, void *);
void	prtc_attach(struct device *, struct device *, void *);

const struct cfattach prtc_ca = {
	sizeof(struct prtc_softc), prtc_match, prtc_attach
};

struct cfdriver prtc_cd = {
	NULL, "prtc", DV_DULL
};

int	prtc_gettime(todr_chip_handle_t, struct timeval *);
int	prtc_settime(todr_chip_handle_t, struct timeval *);

int	prtc_opl_gettime(todr_chip_handle_t, struct timeval *);
int	prtc_opl_settime(todr_chip_handle_t, struct timeval *);

int
prtc_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, "prtc") == 0)
		return (1);

	return (0);
}

void
prtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct prtc_softc *sc = (struct prtc_softc *)self;
	todr_chip_handle_t handle = &sc->sc_todr_chip;
	char buf[32];
	int opl;

	opl = OF_getprop(findroot(), "name", buf, sizeof(buf)) > 0 &&
	    strcmp(buf, "SUNW,SPARC-Enterprise") == 0;

	if (opl)
		printf(": OPL");

	printf("\n");

	handle->cookie = sc;
	if (opl) {
		handle->todr_gettime = prtc_opl_gettime;
		handle->todr_settime = prtc_opl_settime;
	} else {
		handle->todr_gettime = prtc_gettime;
		handle->todr_settime = prtc_settime;
	}

	handle->bus_cookie = NULL;
	handle->todr_setwen = NULL;
	handle->todr_quality = 0;
	todr_handle = handle;
}

int
prtc_gettime(todr_chip_handle_t handle, struct timeval *tv)
{
	char buf[32];
	u_int32_t tod = 0;

	snprintf(buf, sizeof(buf), "h# %08lx unix-gettod", (long)&tod);
	OF_interpret(buf, 0);

	tv->tv_sec = tod;
	tv->tv_usec = 0;
	return (0);
}

int
prtc_settime(todr_chip_handle_t handle, struct timeval *tv)
{
	return (0);
}

int
prtc_opl_gettime(todr_chip_handle_t handle, struct timeval *tv)
{
	struct {
		cell_t	name;
		cell_t	nargs;
		cell_t	nrets;
		cell_t	stick;
		cell_t	time;
	} args = {
		.name	= ADR2CELL("FJSV,get-tod"),
		.nargs	= 0,
		.nrets	= 2,
	};

	if (openfirmware(&args) == -1)
		return (-1);

	tv->tv_sec = args.time;
	tv->tv_usec = 0;

	return (0);
}

int
prtc_opl_settime(todr_chip_handle_t handle, struct timeval *tv)
{
	struct timeval otv;
	struct {
		cell_t	name;
		cell_t	nargs;
		cell_t	nrets;
		cell_t	diff;
	} args = {
		.name	= ADR2CELL("FJSV,set-domain-time"),
		.nargs	= 1,
		.nrets	= 0,
	};

	if (prtc_opl_gettime(handle, &otv) == -1)
		return (-1);

	args.diff = tv->tv_sec - otv.tv_sec;
	if (args.diff == 0)
		return (0);

	if (openfirmware(&args) == -1)
		return (-1);

	return (0);
}
