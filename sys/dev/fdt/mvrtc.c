/*	$OpenBSD: mvrtc.c,v 1.3 2022/10/17 19:09:46 kettenis Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#include <dev/clock_subr.h>

/* Registers. */
#define RTC_STATUS		0x0000
#define RTC_TIME		0x000c

#define RTC_TIMING_CTL0		0x0000
#define  RTC_TIMING_CTL0_WRCLK_PERIOD_MASK	(0xffff << 0)
#define  RTC_TIMING_CTL0_WRCLK_PERIOD_SHIFT	0
#define  RTC_TIMING_CTL0_WRCLK_SETUP_MASK	(0xffff << 16)
#define  RTC_TIMING_CTL0_WRCLK_SETUP_SHIFT	16
#define RTC_TIMING_CTL1		0x0004
#define  RTC_TIMING_CTL1_READ_DELAY_MASK	(0xffff << 0)
#define  RTC_TIMING_CTL1_READ_DELAY_SHIFT	0

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct mvrtc_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_space_handle_t	sc_soc_ioh;

	struct todr_chip_handle sc_todr;
};

int mvrtc_match(struct device *, void *, void *);
void mvrtc_attach(struct device *, struct device *, void *);

const struct cfattach	mvrtc_ca = {
	sizeof (struct mvrtc_softc), mvrtc_match, mvrtc_attach
};

struct cfdriver mvrtc_cd = {
	NULL, "mvrtc", DV_DULL
};

int	mvrtc_gettime(struct todr_chip_handle *, struct timeval *);
int	mvrtc_settime(struct todr_chip_handle *, struct timeval *);

int
mvrtc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,armada-8k-rtc");
}

void
mvrtc_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvrtc_softc *sc = (struct mvrtc_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t reg;

	if (faa->fa_nreg < 2) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->sc_soc_ioh)) {
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, faa->fa_reg[0].size);
		printf(": can't map soc registers\n");
		return;
	}

	/* Magic to make bus access actually work. */
	reg = bus_space_read_4(sc->sc_iot, sc->sc_soc_ioh, RTC_TIMING_CTL0);
	reg &= ~RTC_TIMING_CTL0_WRCLK_PERIOD_MASK;
	reg |= (0x3ff << RTC_TIMING_CTL0_WRCLK_PERIOD_SHIFT);
	reg &= ~RTC_TIMING_CTL0_WRCLK_SETUP_MASK;
	reg |= (0x29 << RTC_TIMING_CTL0_WRCLK_SETUP_SHIFT);
	bus_space_write_4(sc->sc_iot, sc->sc_soc_ioh, RTC_TIMING_CTL0, reg);
	reg = bus_space_read_4(sc->sc_iot, sc->sc_soc_ioh, RTC_TIMING_CTL1);
	reg &= ~RTC_TIMING_CTL1_READ_DELAY_MASK;
	reg |= (0x3f << RTC_TIMING_CTL1_READ_DELAY_SHIFT);
	bus_space_write_4(sc->sc_iot, sc->sc_soc_ioh, RTC_TIMING_CTL1, reg);

	printf("\n");

	sc->sc_todr.cookie = sc;
	sc->sc_todr.todr_gettime = mvrtc_gettime;
	sc->sc_todr.todr_settime = mvrtc_settime;
	sc->sc_todr.todr_quality = 0;
	todr_attach(&sc->sc_todr);
}

int
mvrtc_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct mvrtc_softc *sc = handle->cookie;

	tv->tv_sec = HREAD4(sc, RTC_TIME);
	tv->tv_usec = 0;
	return 0;
}

int
mvrtc_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct mvrtc_softc *sc = handle->cookie;

	HWRITE4(sc, RTC_STATUS, 0);
	HWRITE4(sc, RTC_STATUS, 0);
	HWRITE4(sc, RTC_TIME, tv->tv_sec);
	delay(10);
	return 0;
}
