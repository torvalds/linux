/*	$OpenBSD: mvclock.c,v 1.13 2022/06/05 02:43:44 dlg Exp $	*/
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
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct mvclock_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct clock_device	sc_cd;
};

int mvclock_match(struct device *, void *, void *);
void mvclock_attach(struct device *, struct device *, void *);

const struct cfattach	mvclock_ca = {
	sizeof (struct mvclock_softc), mvclock_match, mvclock_attach
};

struct cfdriver mvclock_cd = {
	NULL, "mvclock", DV_DULL
};

uint32_t ap806_get_frequency(void *, uint32_t *);
uint32_t cp110_get_frequency(void *, uint32_t *);
void	cp110_enable(void *, uint32_t *, int);

void	 a3700_periph_nb_enable(void *, uint32_t *, int);
uint32_t a3700_periph_nb_get_frequency(void *, uint32_t *);
void	 a3700_periph_sb_enable(void *, uint32_t *, int);
uint32_t a3700_periph_sb_get_frequency(void *, uint32_t *);
uint32_t a3700_tbg_get_frequency(void *, uint32_t *);

int
mvclock_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int node = faa->fa_node;

	if (OF_is_compatible(node, "marvell,ap806-clock") ||
	    OF_is_compatible(node, "marvell,ap807-clock") ||
	    OF_is_compatible(node, "marvell,cp110-clock") ||
	    OF_is_compatible(node, "marvell,armada-3700-periph-clock-nb") ||
	    OF_is_compatible(node, "marvell,armada-3700-periph-clock-sb") ||
	    OF_is_compatible(node, "marvell,armada-3700-tbg-clock") ||
	    OF_is_compatible(node, "marvell,armada-3700-xtal-clock"))
		return 10;	/* Must beat syscon(4). */

	return 0;
}

void
mvclock_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvclock_softc *sc = (struct mvclock_softc *)self;
	struct fdt_attach_args *faa = aux;
	int node = faa->fa_node;

	if (faa->fa_nreg > 0) {
		sc->sc_iot = faa->fa_iot;
		if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
		    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
			printf(": can't map registers\n");
			return;
		}
	}

	printf("\n");

	sc->sc_cd.cd_node = node;
	sc->sc_cd.cd_cookie = sc;
	if (OF_is_compatible(node, "marvell,ap806-clock") ||
	    OF_is_compatible(node, "marvell,ap807-clock")) {
		sc->sc_cd.cd_get_frequency = ap806_get_frequency;
	} else if (OF_is_compatible(node, "marvell,cp110-clock")) {
		sc->sc_cd.cd_get_frequency = cp110_get_frequency;
		sc->sc_cd.cd_enable = cp110_enable;
	} else if (OF_is_compatible(node, "marvell,armada-3700-periph-clock-nb")) {
		sc->sc_cd.cd_enable = a3700_periph_nb_enable;
		sc->sc_cd.cd_get_frequency = a3700_periph_nb_get_frequency;
	} else if (OF_is_compatible(node, "marvell,armada-3700-periph-clock-sb")) {
		sc->sc_cd.cd_enable = a3700_periph_sb_enable;
		sc->sc_cd.cd_get_frequency = a3700_periph_sb_get_frequency;
	} else if (OF_is_compatible(node, "marvell,armada-3700-tbg-clock")) {
		sc->sc_cd.cd_get_frequency = a3700_tbg_get_frequency;
	}
	clock_register(&sc->sc_cd);
}

/* AP806 block */

#define AP806_CORE_FIXED	2
#define AP806_CORE_MSS		3
#define AP806_CORE_SDIO		4

uint32_t
ap806_get_frequency(void *cookie, uint32_t *cells)
{
	uint32_t idx = cells[0];

	switch (idx) {
	case AP806_CORE_FIXED:
		/* fixed PLL at 1200MHz */
		return 1200000000;
	case AP806_CORE_MSS:
		/* MSS clock is fixed clock divided by 6 */
		return 200000000;
	case AP806_CORE_SDIO:
		/* SDIO/eMMC clock is fixed clock divided by 3 */
		return 400000000;
	default:
		break;
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

/* CP110 block */

#define CP110_PM_CLOCK_GATING_CTRL	0x220

#define CP110_CORE_APLL		0
#define CP110_CORE_PPV2		1
#define CP110_CORE_X2CORE	2
#define CP110_CORE_CORE		3
#define CP110_CORE_SDIO		5

#define CP110_GATE_PPV2		3
#define CP110_GATE_SDIO		4
#define CP110_GATE_SLOW_IO	21

uint32_t
cp110_get_frequency(void *cookie, uint32_t *cells)
{
	struct mvclock_softc *sc = cookie;
	uint32_t mod = cells[0];
	uint32_t idx = cells[1];
	uint32_t parent[2] = { 0, 0 };

	/* Core clocks */
	if (mod == 0) {
		switch (idx) {
		case CP110_CORE_APLL:
			/* fixed PLL at 1GHz */
			return 1000000000;
		case CP110_CORE_PPV2:
			/* PPv2 clock is APLL/3 */
			return 333333333;
		case CP110_CORE_X2CORE:
			/* X2CORE clock is APLL/2 */
			return 500000000;
		case CP110_CORE_CORE:
			/* Core clock is X2CORE/2 */
			return 250000000;
		case CP110_CORE_SDIO:
			/* SDIO clock is APLL/2.5 */
			return 400000000;
		default:
			break;
		}
	}

	/* Gateable clocks */
	if (mod == 1) {
		switch (idx) {
		case CP110_GATE_PPV2:
			parent[1] = CP110_CORE_PPV2;
			break;
		case CP110_GATE_SDIO:
			parent[1] = CP110_CORE_SDIO;
			break;
		case CP110_GATE_SLOW_IO:
			parent[1] = CP110_CORE_X2CORE;
			break;
		default:
			break;
		}

		if (parent[1] != 0)
			return cp110_get_frequency(sc, parent);
	}

	printf("%s: 0x%08x 0x%08x\n", __func__, mod, idx);
	return 0;
}

void
cp110_enable(void *cookie, uint32_t *cells, int on)
{
	struct mvclock_softc *sc = cookie;
	uint32_t mod = cells[0];
	uint32_t idx = cells[1];

	/* Gateable clocks */
	if (mod == 1 && idx < 32) {
		struct regmap *rm;
		uint32_t reg;

		rm = regmap_bynode(OF_parent(sc->sc_cd.cd_node));
		if (rm == NULL) {
			printf("%s: can't enable clock 0x%08x 0x%08x\n",
			    sc->sc_dev.dv_xname, mod, idx);
			return;
		}
		reg = regmap_read_4(rm, CP110_PM_CLOCK_GATING_CTRL);
		if (on)
			reg |= (1U << idx);
		else
			reg &= ~(1U << idx);
		regmap_write_4(rm, CP110_PM_CLOCK_GATING_CTRL, reg);
		return;
	}

	printf("%s: 0x%08x 0x%08x\n", __func__, mod, idx);
}

/* Armada 3700 Periph block */

/* North Bridge clocks */
#define PERIPH_NB_MMC			0x00
#define PERIPH_NB_SATA			0x01 /* SATA Host */
#define PERIPH_NB_SEC_AT		0x02 /* Security AT */
#define PERIPH_NB_SEC_DAP		0x03 /* Security DAP */
#define PERIPH_NB_TSECM			0x04 /* Security Engine */
#define PERIPH_NB_SETM_TMX		0x05 /* Serial Embedded Trace Module */
#define PERIPH_NB_AVS			0x06 /* Adaptive Voltage Scaling */
#define PERIPH_NB_SQF			0x07 /* SPI */
#define PERIPH_NB_I2C2			0x09
#define PERIPH_NB_I2C1			0x0a
#define PERIPH_NB_DDR_PHY		0x0b
#define PERIPH_NB_DDR_FCLK		0x0c
#define PERIPH_NB_TRACE			0x0d
#define PERIPH_NB_COUNTER		0x0e
#define PERIPH_NB_EIO97			0x0f
#define PERIPH_NB_CPU			0x10

/* South Bridge clocks */
#define PERIPH_SB_GBE_50		0x00 /* 50MHz parent for gbe */
#define PERIPH_SB_GBE_CORE		0x01 /* parent for gbe core */
#define PERIPH_SB_GBE_125		0x02 /* 125MHz parent for gbe */
#define PERIPH_SB_GBE1_50		0x03 /* 50MHz parent for gbe port 1 */
#define PERIPH_SB_GBE0_50		0x04 /* 50MHz parent for gbe port 0 */
#define PERIPH_SB_GBE1_125		0x05 /* 125MHz parent for gbe port 1 */
#define PERIPH_SB_GBE0_125		0x06 /* 125MHz parent for gbe port 0 */
#define PERIPH_SB_GBE1_CORE		0x07 /* gbe core port 1 */
#define PERIPH_SB_GBE0_CORE		0x08 /* gbe core port 0 */
#define PERIPH_SB_GBE_BM		0x09 /* gbe buffer manager */
#define PERIPH_SB_SDIO			0x0a
#define PERIPH_SB_USB32_USB2_SYS	0x0b /* USB 2 clock */
#define PERIPH_SB_USB32_SS_SYS		0x0c /* USB 3 clock */
#define PERIPH_SB_PCIE			0x0d

#define PERIPH_TBG_SEL			0x0
#define  PERIPH_TBG_SEL_MASK			0x3
#define PERIPH_DIV_SEL0			0x4
#define PERIPH_DIV_SEL1			0x8
#define PERIPH_DIV_SEL2			0xc
#define  PERIPH_DIV_SEL_MASK			0x7
#define PERIPH_CLK_SEL			0x10
#define PERIPH_CLK_DIS			0x14

void	 a3700_periph_enable(struct mvclock_softc *, uint32_t, int);
uint32_t a3700_periph_tbg_get_frequency(struct mvclock_softc *, uint32_t);
uint32_t a3700_periph_get_div(struct mvclock_softc *, uint32_t, uint32_t);
uint32_t a3700_periph_get_double_div(struct mvclock_softc *, uint32_t,
	   uint32_t, uint32_t);

void
a3700_periph_nb_enable(void *cookie, uint32_t *cells, int on)
{
	struct mvclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case PERIPH_NB_MMC:
		return a3700_periph_enable(sc, 2, on);
	case PERIPH_NB_SQF:
		return a3700_periph_enable(sc, 12, on);
	case PERIPH_NB_I2C2:
		return a3700_periph_enable(sc, 16, on);
	case PERIPH_NB_I2C1:
		return a3700_periph_enable(sc, 17, on);
	default:
		break;
	}

	printf("%s: 0x%08x\n", __func__, idx);
}

uint32_t
a3700_periph_nb_get_frequency(void *cookie, uint32_t *cells)
{
	struct mvclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t freq;

	switch (idx) {
	case PERIPH_NB_MMC:
		freq = a3700_periph_tbg_get_frequency(sc, 0);
		freq /= a3700_periph_get_double_div(sc,
		    PERIPH_DIV_SEL2, 16, 13);
		return freq;
	case PERIPH_NB_SQF:
		freq = a3700_periph_tbg_get_frequency(sc, 12);
		freq /= a3700_periph_get_double_div(sc,
		    PERIPH_DIV_SEL1, 27, 24);
		return freq;
	case PERIPH_NB_CPU:
		freq = a3700_periph_tbg_get_frequency(sc, 22);
		freq /= a3700_periph_get_div(sc, PERIPH_DIV_SEL0, 28);
		return freq;
	default:
		break;
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

void
a3700_periph_sb_enable(void *cookie, uint32_t *cells, int on)
{
	struct mvclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case PERIPH_SB_GBE1_CORE:
		return a3700_periph_enable(sc, 4, on);
	case PERIPH_SB_GBE0_CORE:
		return a3700_periph_enable(sc, 5, on);
	case PERIPH_SB_USB32_USB2_SYS:
		return a3700_periph_enable(sc, 16, on);
	case PERIPH_SB_USB32_SS_SYS:
		return a3700_periph_enable(sc, 17, on);
	default:
		break;
	}

	printf("%s: 0x%08x\n", __func__, idx);
}

uint32_t
a3700_periph_sb_get_frequency(void *cookie, uint32_t *cells)
{
	struct mvclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t freq;

	switch (idx) {
	case PERIPH_SB_GBE_CORE:
		freq = a3700_periph_tbg_get_frequency(sc, 8);
		freq /= a3700_periph_get_double_div(sc,
		    PERIPH_DIV_SEL1, 18, 21);
		return freq;
	case PERIPH_SB_GBE1_CORE:
		idx = PERIPH_SB_GBE_CORE;
		freq = a3700_periph_sb_get_frequency(sc, &idx);
		freq /= a3700_periph_get_div(sc, PERIPH_DIV_SEL1, 13) + 1;
		return freq;
	case PERIPH_SB_GBE0_CORE:
		idx = PERIPH_SB_GBE_CORE;
		freq = a3700_periph_sb_get_frequency(sc, &idx);
		freq /= a3700_periph_get_div(sc, PERIPH_DIV_SEL1, 14) + 1;
		return freq;
	default:
		break;
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

void
a3700_periph_enable(struct mvclock_softc *sc, uint32_t idx, int on)
{
	uint32_t reg;

	reg = HREAD4(sc, PERIPH_CLK_DIS);
	reg &= ~(1 << idx);
	if (!on)
		reg |= (1 << idx);
	HWRITE4(sc, PERIPH_CLK_DIS, reg);
}

uint32_t
a3700_periph_tbg_get_frequency(struct mvclock_softc *sc, uint32_t idx)
{
	uint32_t reg;

	reg = HREAD4(sc, PERIPH_TBG_SEL);
	reg >>= idx;
	reg &= PERIPH_TBG_SEL_MASK;

	return clock_get_frequency_idx(sc->sc_cd.cd_node, reg);
}

uint32_t
a3700_periph_get_div(struct mvclock_softc *sc, uint32_t off, uint32_t idx)
{
	uint32_t reg = HREAD4(sc, off);
	return ((reg >> idx) & PERIPH_DIV_SEL_MASK);
}

uint32_t
a3700_periph_get_double_div(struct mvclock_softc *sc, uint32_t off,
    uint32_t idx0, uint32_t idx1)
{
	uint32_t reg = HREAD4(sc, off);
	return ((reg >> idx0) & PERIPH_DIV_SEL_MASK) *
	    ((reg >> idx1) & PERIPH_DIV_SEL_MASK);
}

/* Armada 3700 TBG block */

#define TBG_A_P				0
#define TBG_B_P				1
#define TBG_A_S				2
#define TBG_B_S				3

#define TBG_CTRL0			0x4
#define  TBG_A_FBDIV_SHIFT			2
#define  TBG_B_FBDIV_SHIFT			18
#define TBG_CTRL1			0x8
#define  TBG_A_VCODIV_SE_SHIFT			0
#define  TBG_B_VCODIV_SE_SHIFT			16
#define TBG_CTRL7			0x20
#define  TBG_A_REFDIV_SHIFT			0
#define  TBG_B_REFDIV_SHIFT			16
#define TBG_CTRL8			0x30
#define  TBG_A_VCODIV_DIFF_SHIFT		1
#define  TBG_B_VCODIV_DIFF_SHIFT		17
#define TBG_DIV_MASK			0x1ff

uint32_t
a3700_tbg_get_frequency(void *cookie, uint32_t *cells)
{
	struct mvclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint64_t mult, div, freq;
	uint32_t reg, vcodiv;

	switch (idx) {
	case TBG_A_P:
		vcodiv = HREAD4(sc, TBG_CTRL8);
		vcodiv >>= TBG_A_VCODIV_DIFF_SHIFT;
		vcodiv &= TBG_DIV_MASK;
		break;
	case TBG_B_P:
		vcodiv = HREAD4(sc, TBG_CTRL8);
		vcodiv >>= TBG_B_VCODIV_DIFF_SHIFT;
		vcodiv &= TBG_DIV_MASK;
		break;
	case TBG_A_S:
		vcodiv = HREAD4(sc, TBG_CTRL1);
		vcodiv >>= TBG_A_VCODIV_SE_SHIFT;
		vcodiv &= TBG_DIV_MASK;
		break;
	case TBG_B_S:
		vcodiv = HREAD4(sc, TBG_CTRL1);
		vcodiv >>= TBG_B_VCODIV_SE_SHIFT;
		vcodiv &= TBG_DIV_MASK;
		break;
	default:
		printf("%s: 0x%08x\n", __func__, idx);
		return 0;
	}

	reg = HREAD4(sc, TBG_CTRL0);
	if (idx == TBG_A_P || idx == TBG_A_S)
		reg >>= TBG_A_FBDIV_SHIFT;
	else
		reg >>= TBG_B_FBDIV_SHIFT;
	reg &= TBG_DIV_MASK;
	mult = reg << 2;

	reg = HREAD4(sc, TBG_CTRL7);
	if (idx == TBG_A_P || idx == TBG_A_S)
		reg >>= TBG_A_REFDIV_SHIFT;
	else
		reg >>= TBG_B_REFDIV_SHIFT;
	reg &= TBG_DIV_MASK;
	div = reg;

	if (div == 0)
		div = 1;
	div *= 1 << vcodiv;

	freq = clock_get_frequency(sc->sc_cd.cd_node, NULL);
	return (freq * mult) / div;
}
