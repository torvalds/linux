/* $OpenBSD: exclock.c,v 1.10 2021/10/24 17:52:27 mpi Exp $ */
/*
 * Copyright (c) 2012-2013 Patrick Wildt <patrick@blueri.se>
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

/* registers */
#define CLOCK_APLL_CON0				0x0100
#define CLOCK_APLL_CON1				0x0104
#define CLOCK_BPLL_CON0				0x0110
#define CLOCK_BPLL_CON1				0x0114
#define CLOCK_EPLL_CON0				0x0130
#define CLOCK_EPLL_CON1				0x0134
#define CLOCK_EPLL_CON2				0x0138
#define CLOCK_VPLL_CON0				0x0140
#define CLOCK_VPLL_CON1				0x0144
#define CLOCK_VPLL_CON2				0x0148
#define CLOCK_CLK_DIV_CPU0			0x0500
#define CLOCK_CLK_DIV_CPU1			0x0504
#define CLOCK_CLK_DIV_TOP0			0x0510
#define CLOCK_CLK_DIV_TOP1			0x0514
#define CLOCK_PLL_DIV2_SEL			0x0A24
#define CLOCK_MPLL_CON0				0x4100
#define CLOCK_MPLL_CON1				0x4104

/* bits and bytes */
#define MPLL_FOUT_SEL_SHIFT			0x4
#define MPLL_FOUT_SEL_MASK			0x1
#define BPLL_FOUT_SEL_SHIFT			0x0
#define BPLL_FOUT_SEL_MASK			0x1

#define HCLK_FREQ				24000000

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct exclock_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct clock_device	sc_cd;
};

enum clocks {
	/* OSC */
	OSC,		/* 24 MHz OSC */

	/* PLLs */
	APLL,		/* ARM core clock */
	MPLL,		/* System bus clock for memory controller */
	BPLL,		/* Graphic 3D processor clock and 1066 MHz clock for memory controller if necessary */
	CPLL,		/* Multi Format Video Hardware Codec clock */
	GPLL,		/* Graphic 3D processor clock or other clocks for DVFS flexibility */
	EPLL,		/* Audio interface clocks and clocks for other external device interfaces */
	VPLL,		/* dithered PLL, helps to reduce the EMI of display and camera */
	KPLL,
};

struct exclock_softc *exclock_sc;

int	exclock_match(struct device *, void *, void *);
void	exclock_attach(struct device *, struct device *, void *);
uint32_t exclock_decode_pll_clk(enum clocks, unsigned int, unsigned int);
uint32_t exclock_get_pll_clk(struct exclock_softc *, enum clocks);
uint32_t exclock_get_armclk(struct exclock_softc *);
uint32_t exclock_get_kfcclk(struct exclock_softc *);
unsigned int exclock_get_i2cclk(void);

const struct cfattach	exclock_ca = {
	sizeof (struct exclock_softc), exclock_match, exclock_attach
};

struct cfdriver exclock_cd = {
	NULL, "exclock", DV_DULL
};

uint32_t exynos5250_get_frequency(void *, uint32_t *);
int	exynos5250_set_frequency(void *, uint32_t *, uint32_t);
void	exynos5250_enable(void *, uint32_t *, int);
uint32_t exynos5420_get_frequency(void *, uint32_t *);
int	exynos5420_set_frequency(void *, uint32_t *, uint32_t);
void	exynos5420_enable(void *, uint32_t *, int);

int
exclock_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	if (OF_is_compatible(faa->fa_node, "samsung,exynos5250-clock") ||
	    OF_is_compatible(faa->fa_node, "samsung,exynos5800-clock"))
		return 10;	/* Must beat syscon(4). */

	return 0;
}

void
exclock_attach(struct device *parent, struct device *self, void *aux)
{
	struct exclock_softc *sc = (struct exclock_softc *)self;
	struct fdt_attach_args *faa = aux;

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	exclock_sc = sc;

	printf("\n");

	if (OF_is_compatible(faa->fa_node, "samsung,exynos5250-clock")) {
		/* Exynos 5250 */
		sc->sc_cd.cd_enable = exynos5250_enable;
		sc->sc_cd.cd_get_frequency = exynos5250_get_frequency;
		sc->sc_cd.cd_set_frequency = exynos5250_set_frequency;
	} else {
		/* Exynos 5420/5800 */
		sc->sc_cd.cd_enable = exynos5420_enable;
		sc->sc_cd.cd_get_frequency = exynos5420_get_frequency;
		sc->sc_cd.cd_set_frequency = exynos5420_set_frequency;
	}

	sc->sc_cd.cd_node = faa->fa_node;
	sc->sc_cd.cd_cookie = sc;
	clock_register(&sc->sc_cd);
}

/*
 * Exynos 5250
 */

/* Clocks */
#define EXYNOS5250_CLK_ARM_CLK		9

uint32_t
exynos5250_get_frequency(void *cookie, uint32_t *cells)
{
	struct exclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case EXYNOS5250_CLK_ARM_CLK:
		return exclock_get_armclk(sc);
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

int
exynos5250_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	uint32_t idx = cells[0];

	switch (idx) {
	case EXYNOS5250_CLK_ARM_CLK:
		return -1;
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return -1;
}

void
exynos5250_enable(void *cookie, uint32_t *cells, int on)
{
	uint32_t idx = cells[0];

	printf("%s: 0x%08x\n", __func__, idx);
}

/*
 * Exynos 5420/5800
 */

/* Clocks */
#define EXYNOS5420_CLK_FIN_PLL		1
#define EXYNOS5420_CLK_FOUT_RPLL	6
#define EXYNOS5420_CLK_FOUT_SPLL	8
#define EXYNOS5420_CLK_ARM_CLK		13
#define EXYNOS5420_CLK_KFC_CLK		14
#define EXYNOS5420_CLK_SCLK_MMC2	134
#define EXYNOS5420_CLK_MMC2		353
#define EXYNOS5420_CLK_USBH20		365
#define EXYNOS5420_CLK_USBD300		366
#define EXYNOS5420_CLK_USBD301		367
#define EXYNOS5420_CLK_SCLK_SPLL	-1

/* Registers */
#define EXYNOS5420_RPLL_CON0		0x10140
#define EXYNOS5420_RPLL_CON1		0x10144
#define EXYNOS5420_SPLL_CON0		0x10160
#define EXYNOS5420_SRC_TOP6		0x10218
#define EXYNOS5420_DIV_FSYS1		0x1054c
#define EXYNOS5420_SRC_FSYS		0x10244
#define EXYNOS5420_GATE_TOP_SCLK_FSYS	0x10840
#define EXYNOS5420_GATE_IP_FSYS		0x10944
#define EXYNOS5420_KPLL_CON0		0x28100
#define EXYNOS5420_SRC_KFC		0x28200
#define EXYNOS5420_DIV_KFC0		0x28500

uint32_t
exynos5420_get_frequency(void *cookie, uint32_t *cells)
{
	struct exclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t reg, div, mux;
	uint32_t kdiv, mdiv, pdiv, sdiv;
	uint64_t freq;

	switch (idx) {
	case EXYNOS5420_CLK_FIN_PLL:
		return 24000000;
	case EXYNOS5420_CLK_ARM_CLK:
		return exclock_get_armclk(sc);
	case EXYNOS5420_CLK_KFC_CLK:
		return exclock_get_kfcclk(sc);
	case EXYNOS5420_CLK_SCLK_MMC2:
		reg = HREAD4(sc, EXYNOS5420_DIV_FSYS1);
		div = ((reg >> 20) & ((1 << 10) - 1)) + 1;
		reg = HREAD4(sc, EXYNOS5420_SRC_FSYS);
		mux = ((reg >> 16) & ((1 << 3) - 1));
		switch (mux) {
		case 0:
			idx = EXYNOS5420_CLK_FIN_PLL;
			break;
		case 4:
			idx = EXYNOS5420_CLK_SCLK_SPLL;
			break;
		default:
			idx = 0;
			break;
		}
		return exynos5420_get_frequency(sc, &idx) / div;
	case EXYNOS5420_CLK_SCLK_SPLL:
		reg = HREAD4(sc, EXYNOS5420_SRC_TOP6);
		mux = ((reg >> 8) & ((1 << 1) - 1));
		switch (mux) {
		case 0:
			idx = EXYNOS5420_CLK_FIN_PLL;
			break;
		case 1:
			idx = EXYNOS5420_CLK_FOUT_SPLL;
			break;
		}
		return exynos5420_get_frequency(sc, &idx);
	case EXYNOS5420_CLK_FOUT_RPLL:
		reg = HREAD4(sc, EXYNOS5420_RPLL_CON0);
		mdiv = (reg >> 16) & 0x1ff;
		pdiv = (reg >> 8) & 0x3f;
		sdiv = (reg >> 0) & 0x7;
		reg = HREAD4(sc, EXYNOS5420_RPLL_CON1);
		kdiv = (reg >> 0) & 0xffff;
		idx = EXYNOS5420_CLK_FIN_PLL;
		freq = exynos5420_get_frequency(sc, &idx);
		freq = ((mdiv << 16) + kdiv) * freq / (pdiv * (1 << sdiv));
		return (freq >> 16);
	case EXYNOS5420_CLK_FOUT_SPLL:
		reg = HREAD4(sc, EXYNOS5420_SPLL_CON0);
		mdiv = (reg >> 16) & 0x3ff;
		pdiv = (reg >> 8) & 0x3f;
		sdiv = (reg >> 0) & 0x7;
		idx = EXYNOS5420_CLK_FIN_PLL;
		freq = exynos5420_get_frequency(sc, &idx);
		return mdiv * freq / (pdiv * (1 << sdiv));
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

int
exynos5420_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	uint32_t idx = cells[0];

	switch (idx) {
	case EXYNOS5420_CLK_ARM_CLK:
	case EXYNOS5420_CLK_KFC_CLK:
		return -1;
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return -1;
}

void
exynos5420_enable(void *cookie, uint32_t *cells, int on)
{
	uint32_t idx = cells[0];

	switch (idx) {
	case EXYNOS5420_CLK_SCLK_MMC2:	/* CLK_GATE_TOP_SCLK_FSYS */
	case EXYNOS5420_CLK_MMC2:	/* CLK_GATE_IP_FSYS */
	case EXYNOS5420_CLK_USBH20:	/* CLK_GATE_IP_FSYS */
	case EXYNOS5420_CLK_USBD300:	/* CLK_GATE_IP_FSYS */
	case EXYNOS5420_CLK_USBD301:	/* CLK_GATE_IP_FSYS */
		/* Enabled by default. */
		return;
	}

	printf("%s: 0x%08x\n", __func__, idx);
}

uint32_t
exclock_decode_pll_clk(enum clocks pll, unsigned int r, unsigned int k)
{
	uint64_t freq;
	uint32_t m, p, s = 0, mask, fout;
	/*
	 * APLL_CON: MIDV [25:16]
	 * MPLL_CON: MIDV [25:16]
	 * EPLL_CON: MIDV [24:16]
	 * VPLL_CON: MIDV [24:16]
	 * BPLL_CON: MIDV [25:16]: Exynos5
	 */

	switch (pll)
	{
	case APLL:
	case MPLL:
	case BPLL:
	case KPLL:
		mask = 0x3ff;
		break;
	default:
		mask = 0x1ff;
	}

	m = (r >> 16) & mask;

	/* PDIV [13:8] */
	p = (r >> 8) & 0x3f;
	/* SDIV [2:0] */
	s = r & 0x7;

	freq = HCLK_FREQ;

	if (pll == EPLL) {
		k = k & 0xffff;
		/* FOUT = (MDIV + K / 65536) * FIN / (PDIV * 2^SDIV) */
		fout = (m + k / 65536) * (freq / (p * (1 << s)));
	} else if (pll == VPLL) {
		k = k & 0xfff;
		/* FOUT = (MDIV + K / 1024) * FIN / (PDIV * 2^SDIV) */
		fout = (m + k / 1024) * (freq / (p * (1 << s)));
	} else {
		/* FOUT = MDIV * FIN / (PDIV * 2^(SDIV - 1)) */
		fout = m * (freq / (p * (1 << s)));
	}

	return fout;
}

uint32_t
exclock_get_pll_clk(struct exclock_softc *sc, enum clocks pll)
{
	uint32_t freq;

	switch (pll) {
	case APLL:
		freq = exclock_decode_pll_clk(pll,
		    HREAD4(sc, CLOCK_APLL_CON0), 0);
		break;
	case MPLL:
		freq = exclock_decode_pll_clk(pll,
		    HREAD4(sc, CLOCK_MPLL_CON0), 0);
		break;
	case BPLL:
		freq = exclock_decode_pll_clk(pll,
		    HREAD4(sc, CLOCK_BPLL_CON0), 0);
		break;
	case EPLL:
		freq = exclock_decode_pll_clk(pll,
		    HREAD4(sc, CLOCK_EPLL_CON0),
		    HREAD4(sc, CLOCK_EPLL_CON1));
		break;
	case VPLL:
		freq = exclock_decode_pll_clk(pll,
		    HREAD4(sc, CLOCK_VPLL_CON0),
		    HREAD4(sc, CLOCK_VPLL_CON1));
		break;
	case KPLL:
		freq = exclock_decode_pll_clk(pll,
		    HREAD4(sc, EXYNOS5420_KPLL_CON0), 0);
		break;
	default:
		return 0;
	}

	/*
	 * According to the user manual, in EVT1 MPLL and BPLL always gives
	 * 1.6GHz clock, so divide by 2 to get 800MHz MPLL clock.
	 */
	if (pll == MPLL || pll == BPLL) {
		uint32_t freq_sel;
		uint32_t pll_div2_sel = HREAD4(sc, CLOCK_PLL_DIV2_SEL);

		switch (pll) {
		case MPLL:
			freq_sel = (pll_div2_sel >> MPLL_FOUT_SEL_SHIFT)
					& MPLL_FOUT_SEL_MASK;
			break;
		case BPLL:
			freq_sel = (pll_div2_sel >> BPLL_FOUT_SEL_SHIFT)
					& BPLL_FOUT_SEL_MASK;
			break;
		default:
			freq_sel = -1;
			break;
		}

		if (freq_sel == 0)
			freq /= 2;
	}

	return freq;
}

uint32_t
exclock_get_armclk(struct exclock_softc *sc)
{
	uint32_t div, armclk, arm_ratio, arm2_ratio;

	div = HREAD4(sc, CLOCK_CLK_DIV_CPU0);

	/* ARM_RATIO: [2:0], ARM2_RATIO: [30:28] */
	arm_ratio = (div >> 0) & 0x7;
	arm2_ratio = (div >> 28) & 0x7;

	armclk = exclock_get_pll_clk(sc, APLL) / (arm_ratio + 1);
	armclk /= (arm2_ratio + 1);

	return armclk;
}

uint32_t
exclock_get_kfcclk(struct exclock_softc *sc)
{
	uint32_t div, kfc_ratio;

	div = HREAD4(sc, EXYNOS5420_DIV_KFC0);

	/* KFC_RATIO: [2:0] */
	kfc_ratio = (div >> 0) & 0x7;

	return exclock_get_pll_clk(sc, KPLL) / (kfc_ratio + 1);
}

unsigned int
exclock_get_i2cclk(void)
{
	struct exclock_softc *sc = exclock_sc;
	uint32_t aclk_66, aclk_66_pre, div, ratio;

	div = HREAD4(sc, CLOCK_CLK_DIV_TOP1);
	ratio = (div >> 24) & 0x7;
	aclk_66_pre = exclock_get_pll_clk(sc, MPLL) / (ratio + 1);
	div = HREAD4(sc, CLOCK_CLK_DIV_TOP0);
	ratio = (div >> 0) & 0x7;
	aclk_66 = aclk_66_pre / (ratio + 1);

	return aclk_66 / 1000;
}
