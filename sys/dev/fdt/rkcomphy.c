/*	$OpenBSD: rkcomphy.c,v 1.2 2023/04/27 08:56:39 kettenis Exp $	*/
/*
 * Copyright (c) 2023 Mark Kettenis <kettenis@openbsd.org>
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

/*
 * WARNING: Most (but not all!) of the register numbers in the Linux
 * driver are off-by-one!  This driver uses 0-based register numbers
 * like in the TRM.
 */

/* Combo PHY registers */
#define COMBO_PIPE_PHY_REG(idx)			((idx) * 4)
/* REG_005 */
#define  COMBO_PIPE_PHY_PLL_DIV_MASK		(0x3 << 6)
#define  COMBO_PIPE_PHY_PLL_DIV_2		(0x1 << 6)
/* REG_006 */
#define  COMBO_PIPE_PHY_TX_RTERM_50OHM		(0x8 << 4)
#define  COMBO_PIPE_PHY_RX_RTERM_44OHM		(0xf << 4)
/* REG_010 */
#define  COMBO_PIPE_PHY_SU_TRIM_0_7		0xf0
/* REG_011 */
#define  COMBO_PIPE_PHY_PLL_LPF_ADJ_VALUE	4
/* REG_014 */
#define  COMBO_PIPE_PHY_SSC_CNT_LO_MASK		(0x3 << 6)
#define  COMBO_PIPE_PHY_SSC_CNT_LO_VALUE	(0x1 << 6)
#define  COMBO_PIPE_PHY_CTLE_EN			(1 << 0)
/* REG_015 */
#define  COMBO_PIPE_PHY_SSC_CNT_HI_MASK		(0xff << 0)
#define  COMBO_PIPE_PHY_SSC_CNT_HI_VALUE	(0x5f << 0)
/* REG_017 */
#define  COMBO_PIPE_PHY_PLL_LOOP		0x32
/* REG_027 */
#define  COMBO_PIPE_PHY_RX_TRIM_RK3588		0x4c
/* REG_031 */
#define  COMBO_PIPE_PHY_SSC_DIR_MASK		(0x3 << 4)
#define  COMBO_PIPE_PHY_SSC_DIR_DOWN		(0x1 << 4)
#define  COMBO_PIPE_PHY_SSC_OFFSET_MASK		(0x3 << 6)
#define  COMBO_PIPE_PHY_SSC_OFFSET_500PPM	(0x1 << 6)
/* REG_032 */
#define  COMBO_PIPE_PHY_PLL_KVCO_MASK		(0x7 << 2)
#define  COMBO_PIPE_PHY_PLL_KVCO_VALUE		(0x2 << 2)
#define  COMBO_PIPE_PHY_PLL_KVCO_VALUE_RK3588	(0x4 << 2)

/* GRF registers */
#define PIPE_GRF_PIPE_CON0			0x0000

/* PHP GRF registers (for RK3588) */
#define PHP_GRF_PCIESEL_CON			0x0100

/* PHY GRF registers */
#define PIPE_PHY_GRF_PIPE_CON(idx)		((idx) * 4)
/* CON0 */
#define  PIPE_PHY_GRF_PIPE_MODE_PCIE		0x003f0000
#define  PIPE_PHY_GRF_PIPE_MODE_USB		0x003f0004
#define  PIPE_PHY_GRF_PIPE_MODE_SATA		0x003f0019
/* CON1 */
#define  PIPE_PHY_GRF_PIPE_CLK_24M		0x60000000
#define  PIPE_PHY_GRF_PIPE_CLK_25M		0x60002000
#define  PIPE_PHY_GRF_PIPE_CLK_100M		0x60004000
/* CON2 */
#define  PIPE_PHY_GRF_PIPE_TXCOMP_SEL_CTRL	0x80000000
#define  PIPE_PHY_GRF_PIPE_TXCOMP_SEL_GRF	0x80008000
#define  PIPE_PHY_GRF_PIPE_TXELEC_SEL_CTRL	0x10000000
#define  PIPE_PHY_GRF_PIPE_TXELEC_SEL_GRF	0x10001000
/* CON3 */
#define  PIPE_PHY_GRF_PIPE_SEL_PCIE		0x60000000
#define  PIPE_PHY_GRF_PIPE_SEL_USB		0x60002000
#define  PIPE_PHY_GRF_PIPE_SEL_SATA		0x60004000
/* STATUS1 */
#define PIPE_PHY_GRF_PIPE_STATUS1		0x34
#define  PIPE_PHY_GRF_PIPE_PHYSTATUS		(1 << 6)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct rkcomphy_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct phy_device	sc_pd;
};

int	rkcomphy_match(struct device *, void *, void *);
void	rkcomphy_attach(struct device *, struct device *, void *);

const struct cfattach rkcomphy_ca = {
	sizeof (struct rkcomphy_softc), rkcomphy_match, rkcomphy_attach
};

struct cfdriver rkcomphy_cd = {
	NULL, "rkcomphy", DV_DULL
};

int	rkcomphy_rk3568_enable(void *, uint32_t *);
int	rkcomphy_rk3588_enable(void *, uint32_t *);

int
rkcomphy_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int node = faa->fa_node;

	return OF_is_compatible(node, "rockchip,rk3568-naneng-combphy") ||
	    OF_is_compatible(node, "rockchip,rk3588-naneng-combphy");
}

void
rkcomphy_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkcomphy_softc *sc = (struct rkcomphy_softc *)self;
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

	reset_assert_all(faa->fa_node);

	printf("\n");

	sc->sc_pd.pd_node = faa->fa_node;
	sc->sc_pd.pd_cookie = sc;
	if (OF_is_compatible(faa->fa_node, "rockchip,rk3568-naneng-combphy"))
	    sc->sc_pd.pd_enable = rkcomphy_rk3568_enable;
	else
	    sc->sc_pd.pd_enable = rkcomphy_rk3588_enable;
	phy_register(&sc->sc_pd);
}

void
rkcomphy_rk3568_pll_tune(struct rkcomphy_softc *sc)
{
	uint32_t reg;

	reg = HREAD4(sc, COMBO_PIPE_PHY_REG(32));
	reg &= ~COMBO_PIPE_PHY_PLL_KVCO_MASK;
	reg |= COMBO_PIPE_PHY_PLL_KVCO_VALUE;
	HWRITE4(sc, COMBO_PIPE_PHY_REG(32), reg);

	HWRITE4(sc, COMBO_PIPE_PHY_REG(11), COMBO_PIPE_PHY_PLL_LPF_ADJ_VALUE);

	reg = HREAD4(sc, COMBO_PIPE_PHY_REG(5));
	reg &= ~COMBO_PIPE_PHY_PLL_DIV_MASK;
	reg |= COMBO_PIPE_PHY_PLL_DIV_2;
	HWRITE4(sc, COMBO_PIPE_PHY_REG(5), reg);

	HWRITE4(sc, COMBO_PIPE_PHY_REG(17), COMBO_PIPE_PHY_PLL_LOOP);
	HWRITE4(sc, COMBO_PIPE_PHY_REG(10), COMBO_PIPE_PHY_SU_TRIM_0_7);
}

int
rkcomphy_rk3568_enable(void *cookie, uint32_t *cells)
{
	struct rkcomphy_softc *sc = cookie;
	struct regmap *rm, *phy_rm;
	int node = sc->sc_pd.pd_node;
	uint32_t type = cells[0];
	uint32_t freq, grf, phy_grf, reg;
	int stat, timo;

	/* We only support PCIe, SATA and USB 3 for now. */
	switch (type) {
	case PHY_TYPE_PCIE:
	case PHY_TYPE_SATA:
	case PHY_TYPE_USB3:
		break;
	default:
		return EINVAL;
	}

	grf = OF_getpropint(node, "rockchip,pipe-grf", 0);
	rm = regmap_byphandle(grf);
	if (rm == NULL)
		return ENXIO;

	phy_grf = OF_getpropint(node, "rockchip,pipe-phy-grf", 0);
	phy_rm = regmap_byphandle(phy_grf);
	if (phy_rm == NULL)
		return ENXIO;

	clock_set_assigned(node);
	clock_enable_all(node);

	if (type == PHY_TYPE_PCIE || type == PHY_TYPE_USB3) {
		reg = HREAD4(sc, COMBO_PIPE_PHY_REG(31));
		reg &= ~COMBO_PIPE_PHY_SSC_OFFSET_MASK;
		reg &= ~COMBO_PIPE_PHY_SSC_DIR_MASK;
		reg |= COMBO_PIPE_PHY_SSC_DIR_DOWN;
		HWRITE4(sc, COMBO_PIPE_PHY_REG(31), reg);
	}

	if (type == PHY_TYPE_SATA || type == PHY_TYPE_USB3) {
		reg = HREAD4(sc, COMBO_PIPE_PHY_REG(14));
		reg |= COMBO_PIPE_PHY_CTLE_EN;
		HWRITE4(sc, COMBO_PIPE_PHY_REG(14), reg);
	}

	switch (type) {
	case PHY_TYPE_PCIE:
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(0), 0xffff1000);
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(1), 0xffff0000);
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(2), 0xffff0101);
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(3), 0xffff0200);
		break;
	case PHY_TYPE_SATA:
		HWRITE4(sc, COMBO_PIPE_PHY_REG(6),
		    COMBO_PIPE_PHY_TX_RTERM_50OHM |
		    COMBO_PIPE_PHY_RX_RTERM_44OHM);

		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(0), 0xffff0119);
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(1), 0xffff0040);
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(2), 0xffff80c3);
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(3), 0xffff4407);
		regmap_write_4(rm, PIPE_GRF_PIPE_CON0, 0xffff2220);
		break;
	case PHY_TYPE_USB3:
		rkcomphy_rk3568_pll_tune(sc);

		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(0),
		    PIPE_PHY_GRF_PIPE_MODE_USB);
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(2),
		    PIPE_PHY_GRF_PIPE_TXCOMP_SEL_CTRL);
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(2),
		    PIPE_PHY_GRF_PIPE_TXELEC_SEL_CTRL);
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(3),
		    PIPE_PHY_GRF_PIPE_SEL_USB);
		break;
	}

	freq = clock_get_frequency(node, "ref");
	switch (freq) {
	case 24000000:
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(1),
		    PIPE_PHY_GRF_PIPE_CLK_24M);
		if (type == PHY_TYPE_SATA || type == PHY_TYPE_USB3) {
			reg = HREAD4(sc, COMBO_PIPE_PHY_REG(14));
			reg &= ~COMBO_PIPE_PHY_SSC_CNT_LO_MASK;
			reg |= COMBO_PIPE_PHY_SSC_CNT_LO_VALUE;
			HWRITE4(sc, COMBO_PIPE_PHY_REG(14), reg);
			reg = HREAD4(sc, COMBO_PIPE_PHY_REG(15));
			reg &= ~COMBO_PIPE_PHY_SSC_CNT_HI_MASK;
			reg |= COMBO_PIPE_PHY_SSC_CNT_HI_VALUE;
			HWRITE4(sc, COMBO_PIPE_PHY_REG(15), reg);
		}
		break;
	case 25000000:
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(1),
		    PIPE_PHY_GRF_PIPE_CLK_25M);
		break;
	case 100000000:
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(1),
		    PIPE_PHY_GRF_PIPE_CLK_100M);
		switch (type) {
		case PHY_TYPE_PCIE:
			rkcomphy_rk3568_pll_tune(sc);
			break;
		case PHY_TYPE_SATA:
			reg = HREAD4(sc, COMBO_PIPE_PHY_REG(31));
			reg &= ~COMBO_PIPE_PHY_SSC_OFFSET_MASK;
			reg |= COMBO_PIPE_PHY_SSC_OFFSET_500PPM;
			reg &= ~COMBO_PIPE_PHY_SSC_DIR_MASK;
			reg |= COMBO_PIPE_PHY_SSC_DIR_DOWN;
			HWRITE4(sc, COMBO_PIPE_PHY_REG(31), reg);
			break;
		}
		break;
	}

	reset_deassert_all(node);

	if (type == PHY_TYPE_USB3) {
		for (timo = 100; timo > 0; timo--) {
			stat = regmap_read_4(phy_rm,
			    PIPE_PHY_GRF_PIPE_STATUS1);
			if ((stat & PIPE_PHY_GRF_PIPE_PHYSTATUS) == 0)
				break;
			delay(10);
		}
		if (timo == 0) {
			printf("%s: timeout\n", sc->sc_dev.dv_xname);
			return ETIMEDOUT;
		}
	}

	return 0;
}

void
rkcomphy_rk3588_pll_tune(struct rkcomphy_softc *sc)
{
	uint32_t reg;

	reg = HREAD4(sc, COMBO_PIPE_PHY_REG(32));
	reg &= ~COMBO_PIPE_PHY_PLL_KVCO_MASK;
	reg |= COMBO_PIPE_PHY_PLL_KVCO_VALUE_RK3588;
	HWRITE4(sc, COMBO_PIPE_PHY_REG(32), reg);

	HWRITE4(sc, COMBO_PIPE_PHY_REG(11), COMBO_PIPE_PHY_PLL_LPF_ADJ_VALUE);

	HWRITE4(sc, COMBO_PIPE_PHY_REG(27), COMBO_PIPE_PHY_RX_TRIM_RK3588);
	HWRITE4(sc, COMBO_PIPE_PHY_REG(10), COMBO_PIPE_PHY_SU_TRIM_0_7);
}

int
rkcomphy_rk3588_enable(void *cookie, uint32_t *cells)
{
	struct rkcomphy_softc *sc = cookie;
	struct regmap *rm, *phy_rm;
	int node = sc->sc_pd.pd_node;
	uint32_t type = cells[0];
	uint32_t freq, grf, phy_grf;

	/* We only support PCIe for now. */
	switch (type) {
	case PHY_TYPE_PCIE:
		break;
	default:
		return EINVAL;
	}

	grf = OF_getpropint(node, "rockchip,pipe-grf", 0);
	rm = regmap_byphandle(grf);
	if (rm == NULL)
		return ENXIO;

	phy_grf = OF_getpropint(node, "rockchip,pipe-phy-grf", 0);
	phy_rm = regmap_byphandle(phy_grf);
	if (phy_rm == NULL)
		return ENXIO;

	clock_set_assigned(node);
	clock_enable_all(node);

	switch (type) {
	case PHY_TYPE_PCIE:
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(0), 0xffff1000);
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(1), 0xffff0000);
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(2), 0xffff0101);
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(3), 0xffff0200);
		regmap_write_4(rm, PHP_GRF_PCIESEL_CON, 0x00010000);
		regmap_write_4(rm, PHP_GRF_PCIESEL_CON, 0x00020000);
		break;
	}

	freq = clock_get_frequency(node, "ref");
	switch (freq) {
	case 25000000:
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(1),
		    PIPE_PHY_GRF_PIPE_CLK_25M);
		break;
	case 100000000:
		regmap_write_4(phy_rm, PIPE_PHY_GRF_PIPE_CON(1),
		    PIPE_PHY_GRF_PIPE_CLK_100M);
		switch (type) {
		case PHY_TYPE_PCIE:
			rkcomphy_rk3588_pll_tune(sc);
			break;
		}
	}

	reset_deassert_all(node);

	return 0;
}
