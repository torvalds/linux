/*	$OpenBSD: sfclock.c,v 1.3 2024/10/17 01:57:18 jsg Exp $	*/
/*
 * Copyright (c) 2021 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

/* Clock IDs */
#define FU740_CLK_COREPLL	0
#define FU740_CLK_DDRPLL	1
#define FU740_CLK_GEMGXLPLL	2
#define FU740_CLK_DVFSCOREPLL	3
#define FU740_CLK_HFPCLKPLL	4
#define FU740_CLK_CLTXPLL	5
#define FU740_CLK_TLCLK		6
#define FU740_CLK_PCLK		7
#define FU740_CLK_PCIE_AUX	8

/* Registers */
#define CORE_PLLCFG		0x04
#define GEMGXL_PLLCFG		0x1c
#define HFPCLK_PLLCFG		0x50
#define HFPCLK_PLLOUTDIV	0x54
#define HFPCLKPLLSEL		0x58
#define  HFPCLKPLLSEL_HFCLK	(1 << 0)
#define HFPCLK_DIV		0x5c

#define PLLCFG_PLLR(x)		(((x) >> 0) & 0x3f)
#define PLLCFG_PLLF(x)		(((x) >> 6) & 0x1ff)
#define PLLCFG_PLLQ(x)		(((x) >> 15) & 0x7)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct sfclock_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_node;

	struct clock_device	sc_cd;
};

int	sfclock_match(struct device *, void *, void *);
void	sfclock_attach(struct device *, struct device *, void *);

const struct cfattach sfclock_ca = {
	sizeof (struct sfclock_softc), sfclock_match, sfclock_attach
};

struct cfdriver sfclock_cd = {
	NULL, "sfclock", DV_DULL
};

uint32_t sfclock_get_frequency(void *, uint32_t *);
int	sfclock_set_frequency(void *, uint32_t *, uint32_t);
void	sfclock_enable(void *, uint32_t *, int);

int
sfclock_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "sifive,fu740-c000-prci");
}

void
sfclock_attach(struct device *parent, struct device *self, void *aux)
{
	struct sfclock_softc *sc = (struct sfclock_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;

	printf("\n");

	sc->sc_cd.cd_node = faa->fa_node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_get_frequency = sfclock_get_frequency;
	sc->sc_cd.cd_set_frequency = sfclock_set_frequency;
	sc->sc_cd.cd_enable = sfclock_enable;
	clock_register(&sc->sc_cd);
}

uint32_t
sfclock_getpll_frequency(struct sfclock_softc *sc, bus_size_t off)
{
	uint64_t parent_freq = clock_get_frequency_idx(sc->sc_node, 0);
	uint32_t pllr, pllf, pllq;
	uint32_t reg;

	reg = HREAD4(sc, off);
	pllr = PLLCFG_PLLR(reg);
	pllf = PLLCFG_PLLF(reg);
	pllq = PLLCFG_PLLQ(reg);
	return ((parent_freq * 2 * (pllf + 1)) / (pllr + 1)) >> pllq;
}

uint32_t
sfclock_get_frequency(void *cookie, uint32_t *cells)
{
	struct sfclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t reg, div;

	switch (idx) {
	case FU740_CLK_COREPLL:
		return sfclock_getpll_frequency(sc, CORE_PLLCFG);
	case FU740_CLK_GEMGXLPLL:
		return sfclock_getpll_frequency(sc, GEMGXL_PLLCFG);
	case FU740_CLK_HFPCLKPLL:
		reg = HREAD4(sc, HFPCLKPLLSEL);
		if (reg & HFPCLKPLLSEL_HFCLK)
			return clock_get_frequency_idx(sc->sc_node, 0);
		return sfclock_getpll_frequency(sc, HFPCLK_PLLCFG);
	case FU740_CLK_PCLK:
		div = HREAD4(sc, HFPCLK_DIV) + 2;
		idx = FU740_CLK_HFPCLKPLL;
		return sfclock_get_frequency(sc, &idx) / div;
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

int
sfclock_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	uint32_t idx = cells[0];

	printf("%s: 0x%08x\n", __func__, idx);
	return -1;
}

void
sfclock_enable(void *cookie, uint32_t *cells, int on)
{
	uint32_t idx = cells[0];

	switch (idx) {
	case FU740_CLK_PCLK:
		return;
	}

	printf("%s: 0x%08x\n", __func__, idx);
}
