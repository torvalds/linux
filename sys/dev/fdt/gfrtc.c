/*	$OpenBSD: gfrtc.c,v 1.3 2022/10/17 19:09:46 kettenis Exp $	*/

/*
 * Copyright (c) 2021 Jonathan Gray <jsg@openbsd.org>
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
 * Google Goldfish virtual real-time clock described in
 * https://android.googlesource.com/platform/external/qemu/+/master/docs/GOLDFISH-VIRTUAL-HARDWARE.TXT
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

#define TIME_LOW	0x00
#define TIME_HIGH	0x04
#define ALARM_LOW	0x0c
#define CLEAR_INTERRUPT	0x10

struct gfrtc_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	struct todr_chip_handle	 sc_todr;
};

int	gfrtc_match(struct device *, void *, void *);
void	gfrtc_attach(struct device *, struct device *, void *);
int	gfrtc_gettime(struct todr_chip_handle *, struct timeval *);
int	gfrtc_settime(struct todr_chip_handle *, struct timeval *);

const struct cfattach gfrtc_ca = {
	sizeof(struct gfrtc_softc), gfrtc_match, gfrtc_attach
};

struct cfdriver gfrtc_cd = {
	NULL, "gfrtc", DV_DULL
};

int
gfrtc_gettime(todr_chip_handle_t handle, struct timeval *tv)
{
	struct gfrtc_softc *sc = handle->cookie;
	uint64_t tl, th;

	tl = bus_space_read_4(sc->sc_iot, sc->sc_ioh, TIME_LOW);
	th = bus_space_read_4(sc->sc_iot, sc->sc_ioh, TIME_HIGH);

	NSEC_TO_TIMEVAL((th << 32) | tl, tv);

	return 0;
}

int
gfrtc_settime(todr_chip_handle_t handle, struct timeval *tv)
{
	struct gfrtc_softc *sc = handle->cookie;
	uint64_t ns;

	ns = TIMEVAL_TO_NSEC(tv);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, TIME_HIGH, ns >> 32);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, TIME_LOW, ns);

	return 0;
}

int
gfrtc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "google,goldfish-rtc");
}

void
gfrtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct gfrtc_softc *sc = (struct gfrtc_softc *) self;

	sc->sc_iot = faa->fa_iot;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": failed to map mem space\n");
		return;
	}

	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = gfrtc_gettime;
	sc->sc_todr.todr_settime = gfrtc_settime;
	sc->sc_todr.todr_quality = 1000;
	todr_attach(&sc->sc_todr);

	printf("\n");
}
