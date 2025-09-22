/*	$OpenBSD: hiclock.c,v 1.4 2022/06/28 23:43:12 naddy Exp $	*/
/*
 * Copyright (c) 2018, 2019 Mark Kettenis <kettenis@openbsd.org>
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
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

/*
 * This driver includes support for the preliminary device tree
 * bindings used by the default UEFI firmware for the HiKey970 board.
 * Support for these preliminary bindings will be dropped at some
 * point in the future.
 */

struct hiclock_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_space_handle_t	sc_ioh_set;

	struct clock_device	sc_cd;
};

int hiclock_match(struct device *, void *, void *);
void hiclock_attach(struct device *, struct device *, void *);

const struct cfattach	hiclock_ca = {
	sizeof (struct hiclock_softc), hiclock_match, hiclock_attach
};

struct cfdriver hiclock_cd = {
	NULL, "hiclock", DV_DULL
};

struct hiclock_compat {
	const char *compat;
	uint32_t (*get_frequency)(void *, uint32_t *);
	int	(*set_frequency)(void *, uint32_t *, uint32_t);
	void	(*enable)(void *, uint32_t *, int);
};

uint32_t hiclock_get_frequency(void *, uint32_t *);
void	hiclock_enable(void *, uint32_t *, int);

uint32_t hi3670_crgctrl_get_frequency(void *, uint32_t *);
void	hi3670_crgctrl_enable(void *, uint32_t *, int);
uint32_t hi3670_stub_get_frequency(void *, uint32_t *);
int	hi3670_stub_set_frequency(void *, uint32_t *, uint32_t);

const struct hiclock_compat hiclock_compat[] = {
	/* Official Linux device tree bindings. */
	{
		.compat = "hisilicon,hi3670-crgctrl",
		.get_frequency = hi3670_crgctrl_get_frequency,
		.enable = hi3670_crgctrl_enable,
	},
	{ .compat = "hisilicon,hi3670-pctrl" },
	{ .compat = "hisilicon,hi3670-pmuctrl" },
	{ .compat = "hisilicon,hi3670-pmctrl" },
	{ .compat = "hisilicon,hi3670-sctrl" },
	{ .compat = "hisilicon,hi3670-iomcu" },
	{ .compat = "hisilicon,hi3670-media1-crg" },
	{ .compat = "hisilicon,hi3670-media2-crg" },

	/* Preliminary device tree bindings for HiKey970. */
	{
		.compat = "hisilicon,kirin970-crgctrl",
		.get_frequency = hi3670_crgctrl_get_frequency,
		.enable = hi3670_crgctrl_enable,
	},
	{ .compat = "hisilicon,kirin970-pctrl" },
	{ .compat = "hisilicon,kirin970-pmuctrl" },
	{ .compat = "hisilicon,kirin970-pmctrl" },
	{ .compat = "hisilicon,kirin970-sctrl" },
	{ .compat = "hisilicon,kirin970-iomcu" },
	{
		.compat = "hisilicon,kirin970-stub-clk",
		.get_frequency = hi3670_stub_get_frequency,
		.set_frequency = hi3670_stub_set_frequency,
	},
	{ .compat = "hisilicon,media1-crg" },
	{ .compat = "hisilicon,media2-crg" },
};

int
hiclock_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int i;

	for (i = 0; i < nitems(hiclock_compat); i++) {
		if (OF_is_compatible(faa->fa_node, hiclock_compat[i].compat))
			return 10;	/* Must beat syscon(4). */
	}

	return 0;
}

void
hiclock_attach(struct device *parent, struct device *self, void *aux)
{
	struct hiclock_softc *sc = (struct hiclock_softc *)self;
	struct fdt_attach_args *faa = aux;
	int i;

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

	if (faa->fa_nreg > 1) {
		if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
		    faa->fa_reg[1].size, 0, &sc->sc_ioh_set)) {
			printf(": can't map registers\n");
			bus_space_unmap(sc->sc_iot, sc->sc_ioh,
			    faa->fa_reg[0].size);
			return;
		}
	}

	if (OF_is_compatible(faa->fa_node, "syscon")) {
		regmap_register(faa->fa_node, sc->sc_iot, sc->sc_ioh,
		    faa->fa_reg[0].size);
	}

	printf("\n");

	sc->sc_cd.cd_node = faa->fa_node;
	sc->sc_cd.cd_cookie = sc;
	for (i = 0; i < nitems(hiclock_compat); i++) {
		if (OF_is_compatible(faa->fa_node, hiclock_compat[i].compat)) {
			sc->sc_cd.cd_get_frequency =
			    hiclock_compat[i].get_frequency;
			sc->sc_cd.cd_set_frequency =
			    hiclock_compat[i].set_frequency;
			sc->sc_cd.cd_enable = hiclock_compat[i].enable;
			break;
		}
	}
	if (sc->sc_cd.cd_get_frequency == NULL)
		sc->sc_cd.cd_get_frequency = hiclock_get_frequency;
	if (sc->sc_cd.cd_enable == NULL)
		sc->sc_cd.cd_enable = hiclock_enable;
	clock_register(&sc->sc_cd);
}

/* Generic */

uint32_t
hiclock_get_frequency(void *cookie, uint32_t *cells)
{
	uint32_t idx = cells[0];

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

void
hiclock_enable(void *cookie, uint32_t *cells, int on)
{
	uint32_t idx = cells[0];

	printf("%s: 0x%08x\n", __func__, idx);
}

/* Hi3670 */

#define HI3670_CLKIN_SYS		0
#define HI3670_CLK_PPLL0		3
#define HI3670_CLK_PPLL2		5
#define HI3670_CLK_PPLL3		6

#define HI3670_CLK_SD_SYS		22
#define HI3670_CLK_SDIO_SYS		23
#define HI3670_CLK_GATE_ABB_USB	29
#define HI3670_CLK_MUX_SD_SYS		68
#define HI3670_CLK_MUX_SD_PLL 	69
#define HI3670_CLK_MUX_SDIO_SYS	70
#define HI3670_CLK_MUX_SDIO_PLL 	71
#define HI3670_CLK_DIV_SD		93
#define HI3670_CLK_DIV_SDIO		94
#define HI3670_HCLK_GATE_USB3OTG	147
#define HI3670_HCLK_GATE_USB3DVFS	148
#define HI3670_HCLK_GATE_SDIO		149
#define HI3670_CLK_GATE_SD		159
#define HI3670_HCLK_GATE_SD		160
#define HI3670_CLK_GATE_SDIO		161
#define HI3670_CLK_GATE_USB3OTG_REF	189

uint32_t
hi3670_crgctrl_get_frequency(void *cookie, uint32_t *cells)
{
	struct hiclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t reg, freq, div;
	int mux;

	switch (idx) {
	case HI3670_CLKIN_SYS:
		return 19200000;
	case HI3670_CLK_PPLL0:
		return 1660000000;
	case HI3670_CLK_PPLL2:
		return 1920000000;
	case HI3670_CLK_PPLL3:
		return 1200000000;
	case HI3670_CLK_SD_SYS:
	case HI3670_CLK_SDIO_SYS:
		idx = HI3670_CLKIN_SYS;
		freq = hi3670_crgctrl_get_frequency(cookie, &idx);
		return freq / 6;
	case HI3670_CLK_MUX_SD_SYS:
		reg = HREAD4(sc, 0x0b8);
		mux = (reg >> 6) & 0x1;
		idx = mux ? HI3670_CLK_DIV_SD : HI3670_CLK_SD_SYS;
		return hi3670_crgctrl_get_frequency(cookie, &idx);
	case HI3670_CLK_MUX_SD_PLL:
		reg = HREAD4(sc, 0x0b8);
		mux = (reg >> 4) & 0x3;
		switch (mux) {
		case 0:
			idx = HI3670_CLK_PPLL0;
			break;
		case 1:
			idx = HI3670_CLK_PPLL3;
			break;
		case 2:
		case 3:
			idx = HI3670_CLK_PPLL2;
			break;
		}
		return hi3670_crgctrl_get_frequency(cookie, &idx);
	case HI3670_CLK_DIV_SD:
		reg = HREAD4(sc, 0x0b8);
		div = (reg >> 0) & 0xf;
		idx = HI3670_CLK_MUX_SD_PLL;
		freq = hi3670_crgctrl_get_frequency(cookie, &idx);
		return freq / (div + 1);
	case HI3670_CLK_GATE_SD:
		idx = HI3670_CLK_MUX_SD_SYS;
		return hi3670_crgctrl_get_frequency(cookie, &idx);
	case HI3670_CLK_GATE_SDIO:
		idx = HI3670_CLK_MUX_SDIO_SYS;
		return hi3670_crgctrl_get_frequency(cookie, &idx);
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

void
hi3670_crgctrl_enable(void *cookie, uint32_t *cells, int on)
{
	uint32_t idx = cells[0];

	switch (idx) {
	case HI3670_CLK_GATE_ABB_USB:
	case HI3670_HCLK_GATE_USB3OTG:
	case HI3670_HCLK_GATE_USB3DVFS:
	case HI3670_CLK_GATE_SD:
	case HI3670_HCLK_GATE_SD:
	case HI3670_CLK_GATE_USB3OTG_REF:
		/* Enabled by default. */
		return;
	}

	printf("%s: 0x%08x\n", __func__, idx);
}

#define HI3670_CLK_STUB_CLUSTER0	0
#define HI3670_CLK_STUB_CLUSTER1	1

uint32_t
hi3670_stub_get_frequency(void *cookie, uint32_t *cells)
{
	struct hiclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t reg;

	switch (idx) {
	case HI3670_CLK_STUB_CLUSTER0:
		reg = HREAD4(sc, 0x070);
		return reg * 1000000;
	case HI3670_CLK_STUB_CLUSTER1:
		reg = HREAD4(sc, 0x074);
		return reg * 1000000;
	default:
		break;
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

int
hi3670_stub_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct hiclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t reg;

	switch (idx) {
	case HI3670_CLK_STUB_CLUSTER0:
		reg = freq / 16000000;
		reg |= (0xff << 16);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh_set, 0x280, reg);
		return 0;
	case HI3670_CLK_STUB_CLUSTER1:
		reg = freq / 16000000;
		reg |= (0xff << 16);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh_set, 0x270, reg);
		return 0;
	default:
		break;
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return -1;
}
