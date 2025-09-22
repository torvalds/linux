/*	$OpenBSD: amlclock.c,v 1.15 2023/08/15 08:27:29 miod Exp $	*/
/*
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
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

/* Clock IDs */
#define G12A_SYS_PLL		0
#define G12A_FCLK_DIV2		2
#define G12A_FCLK_DIV3		3
#define G12A_FCLK_DIV4		4
#define G12A_FCLK_DIV5		5
#define G12A_FCLK_DIV7		6
#define G12A_MPLL1		12
#define G12A_MPLL2		13
#define G12A_I2C		24
#define G12A_SD_EMMC_A		33
#define G12A_SD_EMMC_B		34
#define G12A_SD_EMMC_C		35
#define G12A_PCIE_COMB		45
#define G12A_PCIE_PHY		48
#define G12A_SD_EMMC_A_CLK0	60
#define G12A_SD_EMMC_B_CLK0	61
#define G12A_SD_EMMC_C_CLK0	62
#define G12A_USB		47
#define G12A_FCLK_DIV2P5	99
#define G12A_CPU_CLK		187
#define G12A_PCIE_PLL		201
#define G12A_TS			212
#define G12B_SYS1_PLL		214
#define G12B_CPUB_CLK		224

/* Registers */
#define HHI_PCIE_PLL_CNTL0	0x26
#define HHI_PCIE_PLL_CNTL1	0x27
#define HHI_PCIE_PLL_CNTL2	0x28
#define HHI_PCIE_PLL_CNTL3	0x29
#define HHI_PCIE_PLL_CNTL4	0x2a
#define HHI_PCIE_PLL_CNTL5	0x2b
#define HHI_GCLK_MPEG0		0x50
#define HHI_GCLK_MPEG1		0x51
#define HHI_MPEG_CLK_CNTL	0x5d
#define HHI_TS_CLK_CNTL		0x64
#define HHI_SYS_CPU_CLK_CNTL0	0x67
#define  HHI_SYS_CPU_CLK_DYN_ENABLE		(1 << 26)
#define  HHI_SYS_CPU_CLK_MUX1_DIVN_TCNT(x)	(((x) >> 20) & 0x3f)
#define  HHI_SYS_CPU_CLK_MUX1_DIVN_TCNT_MASK	(0x3f << 20)
#define  HHI_SYS_CPU_CLK_MUX1_DIVN_TCNT_SHIFT	20
#define  HHI_SYS_CPU_CLK_POSTMUX1		(1 << 18)
#define  HHI_SYS_CPU_CLK_PREMUX1(x)		(((x) >> 16) & 0x3)
#define  HHI_SYS_CPU_CLK_PREMUX1_MASK		(0x3 << 16)
#define  HHI_SYS_CPU_CLK_PREMUX1_FCLK_DIV2	(0x1 << 16)
#define  HHI_SYS_CPU_CLK_PREMUX1_FCLK_DIV3	(0x2 << 16)
#define  HHI_SYS_CPU_CLK_FINAL_MUX_SEL		(1 << 11)
#define  HHI_SYS_CPU_CLK_FINAL_DYN_MUX_SEL	(1 << 10)
#define  HHI_SYS_CPU_CLK_MUX0_DIVN_TCNT(x)	(((x) >> 4) & 0x3f)
#define  HHI_SYS_CPU_CLK_MUX0_DIVN_TCNT_MASK	(0x3f << 4)
#define  HHI_SYS_CPU_CLK_MUX0_DIVN_TCNT_SHIFT	4
#define  HHI_SYS_CPU_CLK_POSTMUX0		(1 << 2)
#define  HHI_SYS_CPU_CLK_PREMUX0(x)		(((x) >> 0) & 0x3)
#define  HHI_SYS_CPU_CLK_PREMUX0_MASK		(0x3 << 0)
#define  HHI_SYS_CPU_CLK_PREMUX0_FCLK_DIV2	(0x1 << 0)
#define  HHI_SYS_CPU_CLK_PREMUX0_FCLK_DIV3	(0x2 << 0)
#define HHI_SYS_CPUB_CLK_CNTL	0x82
#define HHI_NAND_CLK_CNTL	0x97
#define HHI_SD_EMMC_CLK_CNTL	0x99
#define HHI_SYS_PLL_CNTL0	0xbd
#define  HHI_SYS_DPLL_LOCK	(1U << 31)
#define  HHI_SYS_DPLL_RESET	(1 << 29)
#define  HHI_SYS_DPLL_EN	(1 << 28)
#define  HHI_SYS_DPLL_OD(x)	(((x) >> 16) & 0x7)
#define  HHI_SYS_DPLL_OD_MASK	(0x7 << 16)
#define  HHI_SYS_DPLL_OD_SHIFT	16
#define  HHI_SYS_DPLL_N(x)	(((x) >> 10) & 0x1f)
#define  HHI_SYS_DPLL_N_MASK	(0x1f << 10)
#define  HHI_SYS_DPLL_N_SHIFT	10
#define  HHI_SYS_DPLL_M(x)	(((x) >> 0) & 0xff)
#define  HHI_SYS_DPLL_M_MASK	(0xff << 0)
#define  HHI_SYS_DPLL_M_SHIFT	0
#define HHI_SYS1_PLL_CNTL0	0xe0

#define HREAD4(sc, reg)							\
	(regmap_read_4((sc)->sc_rm, (reg) << 2))
#define HWRITE4(sc, reg, val)						\
	regmap_write_4((sc)->sc_rm, (reg) << 2, (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct amlclock_gate {
	uint8_t reg;
	uint8_t bit;
};

const struct amlclock_gate aml_g12a_gates[] = {
	[G12A_I2C] = { HHI_GCLK_MPEG0, 9 },
	[G12A_SD_EMMC_A] = { HHI_GCLK_MPEG0, 24 },
	[G12A_SD_EMMC_B] = { HHI_GCLK_MPEG0, 25 },
	[G12A_SD_EMMC_C] = { HHI_GCLK_MPEG0, 26 },
	[G12A_PCIE_COMB] = { HHI_GCLK_MPEG1, 24 },
	[G12A_USB] = { HHI_GCLK_MPEG1, 26 },
	[G12A_PCIE_PHY] = { HHI_GCLK_MPEG1, 27 },

	[G12A_SD_EMMC_A_CLK0] = { HHI_SD_EMMC_CLK_CNTL, 7 },
	[G12A_SD_EMMC_B_CLK0] = { HHI_SD_EMMC_CLK_CNTL, 23 },
	[G12A_SD_EMMC_C_CLK0] = { HHI_NAND_CLK_CNTL, 7 },

	[G12A_TS] = { HHI_TS_CLK_CNTL, 8 },
};

struct amlclock_softc {
	struct device		sc_dev;
	struct regmap		*sc_rm;
	int			sc_node;
	uint32_t		sc_g12b;

	const struct amlclock_gate *sc_gates;
	int			sc_ngates;

	struct clock_device	sc_cd;
	uint32_t		sc_xtal;
};

int amlclock_match(struct device *, void *, void *);
void amlclock_attach(struct device *, struct device *, void *);

const struct cfattach	amlclock_ca = {
	sizeof (struct amlclock_softc), amlclock_match, amlclock_attach
};

struct cfdriver amlclock_cd = {
	NULL, "amlclock", DV_DULL
};

uint32_t amlclock_get_frequency(void *, uint32_t *);
int	amlclock_set_frequency(void *, uint32_t *, uint32_t);
void	amlclock_enable(void *, uint32_t *, int);

int
amlclock_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "amlogic,g12a-clkc") ||
	    OF_is_compatible(faa->fa_node, "amlogic,g12b-clkc") ||
	    OF_is_compatible(faa->fa_node, "amlogic,sm1-clkc"));
}

void
amlclock_attach(struct device *parent, struct device *self, void *aux)
{
	struct amlclock_softc *sc = (struct amlclock_softc *)self;
	struct fdt_attach_args *faa = aux;

	sc->sc_rm = regmap_bynode(OF_parent(faa->fa_node));
	if (sc->sc_rm == NULL) {
		printf(": no registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;
	printf("\n");

	if (OF_is_compatible(faa->fa_node, "amlogic,g12b-clkc"))
		sc->sc_g12b = 1;

	sc->sc_gates = aml_g12a_gates;
	sc->sc_ngates = nitems(aml_g12a_gates);

	sc->sc_xtal = clock_get_frequency(sc->sc_node, "xtal");

	sc->sc_cd.cd_node = faa->fa_node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_get_frequency = amlclock_get_frequency;
	sc->sc_cd.cd_set_frequency = amlclock_set_frequency;
	sc->sc_cd.cd_enable = amlclock_enable;
	clock_register(&sc->sc_cd);
}

uint32_t
amlclock_get_cpu_freq(struct amlclock_softc *sc, bus_size_t offset)
{
	uint32_t reg, mux, div;
	uint32_t idx;

	reg = HREAD4(sc, offset);
	if (reg & HHI_SYS_CPU_CLK_FINAL_MUX_SEL) {
		if (sc->sc_g12b && offset == HHI_SYS_CPU_CLK_CNTL0)
			idx = G12B_SYS1_PLL;
		else
			idx = G12A_SYS_PLL;
		return amlclock_get_frequency(sc, &idx);
	}
	if (reg & HHI_SYS_CPU_CLK_FINAL_DYN_MUX_SEL) {
		div = (reg & HHI_SYS_CPU_CLK_POSTMUX1) ?
		    (HHI_SYS_CPU_CLK_MUX1_DIVN_TCNT(reg) + 1) : 1;
		mux = HHI_SYS_CPU_CLK_PREMUX1(reg);
	} else {
		div = (reg & HHI_SYS_CPU_CLK_POSTMUX0) ?
		    (HHI_SYS_CPU_CLK_MUX0_DIVN_TCNT(reg) + 1) : 1;
		mux = HHI_SYS_CPU_CLK_PREMUX0(reg);
	}
	switch (mux) {
	case 0:
		return sc->sc_xtal / div;
	case 1:
		idx = G12A_FCLK_DIV2;
		break;
	case 2:
		idx = G12A_FCLK_DIV3;
		break;
	case 3:
		return 0;
	}
	return amlclock_get_frequency(sc, &idx) / div;
}

int
amlclock_set_cpu_freq(struct amlclock_softc *sc, bus_size_t offset,
    uint32_t freq)
{
	uint32_t reg, div;
	uint32_t parent_freq;
	uint32_t idx;

	/*
	 * For clock frequencies above 1GHz we have to use
	 * SYS_PLL/SYS1_PLL.
	 */
	reg = HREAD4(sc, offset);
	if (freq > 1000000000) {
		/* 
		 * Switch to a fixed clock if we're currently using
		 * SYS_PLL/SYS1_PLL.  Doesn't really matter which one.
		 */
		if (reg & HHI_SYS_CPU_CLK_FINAL_MUX_SEL) {
			reg &= ~HHI_SYS_CPU_CLK_FINAL_MUX_SEL;
			HWRITE4(sc, offset, reg);
			delay(100);
		}

		if (sc->sc_g12b && offset == HHI_SYS_CPU_CLK_CNTL0)
			idx = G12B_SYS1_PLL;
		else
			idx = G12A_SYS_PLL;
		amlclock_set_frequency(sc, &idx, freq);

		/* Switch to SYS_PLL/SYS1_PLL. */
		reg |= HHI_SYS_CPU_CLK_FINAL_MUX_SEL;
		HWRITE4(sc, offset, reg);
		delay(100);

		return 0;
	}

	/*
	 * There are two signal paths for frequencies up to 1GHz.  If
	 * we're using one, we can program the dividers for the other
	 * one and switch to it.  The pre-divider can be either 2 or 3
	 * and can't be bypassed, so take this into account and only
	 * allow frequencies that include such a divider.
	 */
	div = 2;
	parent_freq = 2000000000;
	while (parent_freq / div > freq)
		div++;
	while ((div % 2) != 0 && (div % 3) != 0)
		div++;
	if (div > 32)
		return EINVAL;
	if ((div % 2) == 0) {
		parent_freq /= 2;
		div /= 2;
	} else {	
		parent_freq /= 3;
		div /= 3;
	}

	if (reg & HHI_SYS_CPU_CLK_FINAL_DYN_MUX_SEL) {
		/* premux0 */
		reg = HREAD4(sc, offset);
		reg &= ~HHI_SYS_CPU_CLK_PREMUX0_MASK;
		if (parent_freq == 1000000000)
			reg |= HHI_SYS_CPU_CLK_PREMUX0_FCLK_DIV2;
		else
			reg |= HHI_SYS_CPU_CLK_PREMUX0_FCLK_DIV3;
		HWRITE4(sc, offset, reg);
		delay(100);

		/* mux0 divider */
		HSET4(sc, offset, HHI_SYS_CPU_CLK_DYN_ENABLE);
		reg = HREAD4(sc, offset);
		reg &= ~HHI_SYS_CPU_CLK_DYN_ENABLE;
		reg &= ~HHI_SYS_CPU_CLK_MUX0_DIVN_TCNT_MASK;
		reg |= ((div - 1) << HHI_SYS_CPU_CLK_MUX0_DIVN_TCNT_SHIFT);
		HWRITE4(sc, offset, reg);

		/* postmux0 */
		if (div != 1)
			HSET4(sc, offset, HHI_SYS_CPU_CLK_POSTMUX0);
		else
			HCLR4(sc, offset, HHI_SYS_CPU_CLK_POSTMUX0);

		/* final_dyn_mux_sel and final_mux_sel */
		reg = HREAD4(sc, offset);
		reg &= ~HHI_SYS_CPU_CLK_FINAL_DYN_MUX_SEL;
		reg &= ~HHI_SYS_CPU_CLK_FINAL_MUX_SEL;
		HWRITE4(sc, offset, reg);
		delay(100);
	} else {
		/* premux1 */
		reg = HREAD4(sc, offset);
		reg &= ~HHI_SYS_CPU_CLK_PREMUX1_MASK;
		if (parent_freq == 1000000000)
			reg |= HHI_SYS_CPU_CLK_PREMUX1_FCLK_DIV2;
		else
			reg |= HHI_SYS_CPU_CLK_PREMUX1_FCLK_DIV3;
		HWRITE4(sc, offset, reg);
		delay(100);

		/* mux1 divider */
		HSET4(sc, offset, HHI_SYS_CPU_CLK_DYN_ENABLE);
		reg = HREAD4(sc, offset);
		reg &= ~HHI_SYS_CPU_CLK_DYN_ENABLE;
		reg &= ~HHI_SYS_CPU_CLK_MUX1_DIVN_TCNT_MASK;
		reg |= ((div - 1) << HHI_SYS_CPU_CLK_MUX1_DIVN_TCNT_SHIFT);
		HWRITE4(sc, offset, reg);

		/* postmux1 */
		if (div != 1)
			HSET4(sc, offset, HHI_SYS_CPU_CLK_POSTMUX1);
		else
			HCLR4(sc, offset, HHI_SYS_CPU_CLK_POSTMUX1);

		/* final_dyn_mux_sel and final_mux_sel */
		reg = HREAD4(sc, offset);
		reg |= HHI_SYS_CPU_CLK_FINAL_DYN_MUX_SEL;
		reg &= ~HHI_SYS_CPU_CLK_FINAL_MUX_SEL;
		HWRITE4(sc, offset, reg);
		delay(100);
	}

	return 0;
}

int
amlclock_set_pll_freq(struct amlclock_softc *sc, bus_size_t offset,
    uint32_t freq)
{
	uint32_t reg, div;
	uint32_t m, n = 1;
	int timo;

	/*
	 * The multiplier should be between 128 and 255.  If
	 * necessary, adjust the divider to achieve this.
	 */
	div = 1;
	while ((div * (uint64_t)freq) / sc->sc_xtal < 128)
		div *= 2;
	if (div > 128)
		return EINVAL;
	m = (div * (uint64_t)freq) / sc->sc_xtal;
	if (m > 255)
		return EINVAL;

	HSET4(sc, offset, HHI_SYS_DPLL_RESET);
	HCLR4(sc, offset, HHI_SYS_DPLL_EN);

	reg = HREAD4(sc, offset);
	reg &= ~HHI_SYS_DPLL_OD_MASK;
	reg |= ((fls(div) - 1) << HHI_SYS_DPLL_OD_SHIFT);
	reg &= ~(HHI_SYS_DPLL_M_MASK | HHI_SYS_DPLL_N_MASK);
	reg |= (m << HHI_SYS_DPLL_M_SHIFT);
	reg |= (n << HHI_SYS_DPLL_N_SHIFT);
	HWRITE4(sc, offset, reg);

	HSET4(sc, offset, HHI_SYS_DPLL_RESET);
	HSET4(sc, offset, HHI_SYS_DPLL_EN);
	HCLR4(sc, offset, HHI_SYS_DPLL_RESET);

	for (timo = 24000000; timo > 0; timo--) {
		if (HREAD4(sc, offset) & HHI_SYS_DPLL_LOCK)
			return 0;
	}

	return ETIMEDOUT;
}

uint32_t
amlclock_get_frequency(void *cookie, uint32_t *cells)
{
	struct amlclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t reg, mux, div;
	uint32_t m, n;

	switch (idx) {
	case G12A_SYS_PLL:
		reg = HREAD4(sc, HHI_SYS_PLL_CNTL0);
		div = 1 << HHI_SYS_DPLL_OD(reg);
		m = HHI_SYS_DPLL_M(reg);
		n = HHI_SYS_DPLL_N(reg);
		return (((uint64_t)sc->sc_xtal * m) / n) / div;
	case G12B_SYS1_PLL:
		reg = HREAD4(sc, HHI_SYS1_PLL_CNTL0);
		div = 1 << HHI_SYS_DPLL_OD(reg);
		m = HHI_SYS_DPLL_M(reg);
		n = HHI_SYS_DPLL_N(reg);
		return (((uint64_t)sc->sc_xtal * m) / n) / div;
	case G12A_FCLK_DIV2:
		return 1000000000;
	case G12A_FCLK_DIV3:
		return 666666666;
	case G12A_FCLK_DIV4:
		return 500000000;
	case G12A_FCLK_DIV5:
		return 400000000;
	case G12A_FCLK_DIV7:
		return 285714285;
	case G12A_FCLK_DIV2P5:
		return 800000000;

	case G12A_I2C:
		reg = HREAD4(sc, HHI_MPEG_CLK_CNTL);
		mux = (reg >> 12) & 0x7;
		div = ((reg >> 0) & 0x7f) + 1;
		switch (mux) {
		case 0:
			return sc->sc_xtal / div;
		case 2:
			idx = G12A_FCLK_DIV7;
			break;
		case 3:
			idx = G12A_MPLL1;
			break;
		case 4:
			idx = G12A_MPLL2;
			break;
		case 5:
			idx = G12A_FCLK_DIV4;
			break;
		case 6:
			idx = G12A_FCLK_DIV3;
			break;
		case 7:
			idx = G12A_FCLK_DIV5;
			break;
		default:
			goto fail;
		}
		return amlclock_get_frequency(sc, &idx) / div;
	case G12A_SD_EMMC_A_CLK0:
		reg = HREAD4(sc, HHI_SD_EMMC_CLK_CNTL);
		mux = (reg >> 9) & 0x7;
		div = ((reg >> 0) & 0x7f) + 1;
		switch (mux) {
		case 0:
			return sc->sc_xtal / div;
		case 1:
			idx = G12A_FCLK_DIV2;
			break;
		case 2:
			idx = G12A_FCLK_DIV3;
			break;
		case 3:
			idx = G12A_FCLK_DIV5;
			break;
		case 4:
			idx = G12A_FCLK_DIV7;
			break;
		default:
			goto fail;
		}
		return amlclock_get_frequency(sc, &idx) / div;
	case G12A_SD_EMMC_B_CLK0:
		reg = HREAD4(sc, HHI_SD_EMMC_CLK_CNTL);
		mux = (reg >> 25) & 0x7;
		div = ((reg >> 16) & 0x7f) + 1;
		switch (mux) {
		case 0:
			return sc->sc_xtal / div;
		case 1:
			idx = G12A_FCLK_DIV2;
			break;
		case 2:
			idx = G12A_FCLK_DIV3;
			break;
		case 3:
			idx = G12A_FCLK_DIV5;
			break;
		case 4:
			idx = G12A_FCLK_DIV7;
			break;
		default:
			goto fail;
		}
		return amlclock_get_frequency(sc, &idx) / div;
	case G12A_SD_EMMC_C_CLK0:
		reg = HREAD4(sc, HHI_NAND_CLK_CNTL);
		mux = (reg >> 9) & 0x7;
		div = ((reg >> 0) & 0x7f) + 1;
		switch (mux) {
		case 0:
			return sc->sc_xtal / div;
		case 1:
			idx = G12A_FCLK_DIV2;
			break;
		case 2:
			idx = G12A_FCLK_DIV3;
			break;
		case 3:
			idx = G12A_FCLK_DIV5;
			break;
		case 4:
			idx = G12A_FCLK_DIV7;
			break;
		default:
			goto fail;
		}
		return amlclock_get_frequency(sc, &idx) / div;
	case G12A_CPU_CLK:
		return amlclock_get_cpu_freq(sc, HHI_SYS_CPU_CLK_CNTL0);
	case G12B_CPUB_CLK:
		return amlclock_get_cpu_freq(sc, HHI_SYS_CPUB_CLK_CNTL);
	}

fail:
	printf("%s: 0x%08x\n", __func__, idx);
	return 0;
}

int
amlclock_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct amlclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	switch (idx) {
	case G12A_SYS_PLL:
		return amlclock_set_pll_freq(sc, HHI_SYS_PLL_CNTL0, freq);
	case G12B_SYS1_PLL:
		return amlclock_set_pll_freq(sc, HHI_SYS1_PLL_CNTL0, freq);
	case G12A_PCIE_PLL:
		/* Fixed at 100 MHz. */
		if (freq != 100000000)
			return -1;
		HWRITE4(sc, HHI_PCIE_PLL_CNTL0, 0x20090496);
		HWRITE4(sc, HHI_PCIE_PLL_CNTL0, 0x30090496);
		HWRITE4(sc, HHI_PCIE_PLL_CNTL1, 0x00000000);
		HWRITE4(sc, HHI_PCIE_PLL_CNTL2, 0x00001100);
		HWRITE4(sc, HHI_PCIE_PLL_CNTL3, 0x10058e00);
		HWRITE4(sc, HHI_PCIE_PLL_CNTL4, 0x000100c0);
		HWRITE4(sc, HHI_PCIE_PLL_CNTL5, 0x68000048);
		HWRITE4(sc, HHI_PCIE_PLL_CNTL5, 0x68000068);
		delay(20);
		HWRITE4(sc, HHI_PCIE_PLL_CNTL4, 0x008100c0);
		delay(10);
		HWRITE4(sc, HHI_PCIE_PLL_CNTL0, 0x34090496);
		HWRITE4(sc, HHI_PCIE_PLL_CNTL0, 0x14090496);
		delay(10);
		HWRITE4(sc, HHI_PCIE_PLL_CNTL2, 0x00001000);
		return 0;
	case G12A_CPU_CLK:
		return amlclock_set_cpu_freq(sc, HHI_SYS_CPU_CLK_CNTL0, freq);
	case G12B_CPUB_CLK:
		return amlclock_set_cpu_freq(sc, HHI_SYS_CPUB_CLK_CNTL, freq);
	}

	printf("%s: 0x%08x\n", __func__, idx);
	return -1;
}

void
amlclock_enable(void *cookie, uint32_t *cells, int on)
{
	struct amlclock_softc *sc = cookie;
	uint32_t idx = cells[0];

	if (idx < sc->sc_ngates && sc->sc_gates[idx].reg != 0) {
		if (on)
			HSET4(sc, sc->sc_gates[idx].reg,
			    (1U << sc->sc_gates[idx].bit));
		else
			HCLR4(sc, sc->sc_gates[idx].reg,
			    (1U << sc->sc_gates[idx].bit));
		return;
	}

	switch (idx) {
	case G12A_FCLK_DIV2:
	case G12A_PCIE_PLL:
		/* Already enabled. */
		return;
	}

	printf("%s: 0x%08x\n", __func__, idx);
}
