/*	$OpenBSD: plrtc.c,v 1.4 2022/10/17 19:09:46 kettenis Exp $	*/

/*
 * Copyright (c) 2015 Jonathan Gray <jsg@openbsd.org>
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

#include <sys/param.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <dev/clock_subr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#define RTCDR	0x00
#define RTCMR	0x04
#define RTCLR	0x08
#define RTCCR	0x0c
#define RTCIMSC	0x10
#define RTCRIS	0x14
#define RTCMIS	0x18
#define RTCICR	0x1c

#define RTCCR_START	(1 << 0)

struct plrtc_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
};

int	plrtc_match(struct device *, void *, void *);
void	plrtc_attach(struct device *, struct device *, void *);
int	plrtc_gettime(struct todr_chip_handle *, struct timeval *);
int	plrtc_settime(struct todr_chip_handle *, struct timeval *);


const struct cfattach plrtc_ca = {
	sizeof(struct plrtc_softc), plrtc_match, plrtc_attach
};

struct cfdriver plrtc_cd = {
	NULL, "plrtc", DV_DULL
};

int
plrtc_gettime(todr_chip_handle_t handle, struct timeval *tv)
{
	struct plrtc_softc	*sc = handle->cookie;
	uint32_t		 tod;

	tod = bus_space_read_4(sc->sc_iot, sc->sc_ioh, RTCDR);

	tv->tv_sec = tod;
	tv->tv_usec = 0;

	return (0);
}

int
plrtc_settime(todr_chip_handle_t handle, struct timeval *tv)
{
	struct plrtc_softc	*sc = handle->cookie;

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, RTCLR, tv->tv_sec);

	return (0);
}

int
plrtc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "arm,pl031"));
}

void
plrtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args		*faa = aux;
	struct plrtc_softc		*sc = (struct plrtc_softc *) self;
	todr_chip_handle_t		 handle;

	if (faa->fa_nreg < 1) {
		printf(": no register data\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": failed to map mem space\n");
		return;
	}

	handle = malloc(sizeof(struct todr_chip_handle), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (handle == NULL)
		panic("couldn't allocate todr_handle");

	handle->cookie = sc;
	handle->todr_gettime = plrtc_gettime;
	handle->todr_settime = plrtc_settime;
	handle->todr_quality = 0;
	todr_attach(handle);

	/* enable the rtc */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, RTCCR, RTCCR_START);

	printf("\n");
}
