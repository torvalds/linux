/*	$OpenBSD: amlusbphy.c,v 1.4 2024/05/13 01:15:50 jsg Exp $	*/
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
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

/* Registers */
#define PHY_R3				0x0c
#define  PHY_R3_SQUELCH_REF_SHIFT		0
#define  PHY_R3_HDISC_REF_SHIFT			2
#define  PHY_R3_DISC_THRESH_SHIFT		4
#define PHY_R4				0x10
#define  PHY_R4_CALIB_CODE_SHIFT		0
#define  PHY_R4_TEST_BYPASS_MODE_EN		(1 << 27)
#define  PHY_R4_I_C2L_BIAS_TRIM_SHIFT		28
#define PHY_R13				0x34
#define  PHY_R13_UPDATE_PMA_SIGNALS		(1 << 15)
#define  PHY_R13_MIN_COUNT_FOR_SYNC_DET_SHIFT	16
#define PHY_R14				0x38
#define PHY_R16				0x40
#define  PHY_R16_MPLL_M_SHIFT			0
#define  PHY_R16_MPLL_N_SHIFT			10
#define  PHY_R16_MPLL_LOAD			(1 << 22)
#define  PHY_R16_MPLL_LOCK_LONG_SHIFT		24
#define  PHY_R16_MPLL_FAST_LOCK			(1 << 27)
#define  PHY_R16_MPLL_EN			(1 << 28)
#define  PHY_R16_MPLL_RESET			(1 << 29)
#define PHY_R17				0x44
#define  PHY_R17_MPLL_FRAC_IN_SHIFT		0
#define  PHY_R17_MPLL_LAMBDA1_SHIFT		17
#define  PHY_R17_MPLL_LAMBDA0_SHIFT		20
#define  PHY_R17_MPLL_FILTER_PVT2_SHIFT		24
#define  PHY_R17_MPLL_FILTER_PVT1_SHIFT		28
#define PHY_R18				0x48
#define  PHY_R18_MPLL_LKW_SEL_SHIFT		0
#define  PHY_R18_MPLL_LK_W_SHIFT		2
#define  PHY_R18_MPLL_LK_S_SHIFT		6
#define  PHY_R18_MPLL_PFD_GAIN_SHIFT		14
#define  PHY_R18_MPLL_ROU_SHIFT			16
#define  PHY_R18_MPLL_DATA_SEL_SHIFT		19
#define  PHY_R18_MPLL_BIAS_ADJ_SHIFT		22
#define  PHY_R18_MPLL_BB_MODE_SHIFT		24
#define  PHY_R18_MPLL_ALPHA_SHIFT		26
#define  PHY_R18_MPLL_ADJ_LDO_SHIFT		29
#define  PHY_R18_MPLL_ACG_RANGE			(1U << 31)
#define PHY_R20				0x50
#define  PHY_R20_USB2_ITG_VBUS_TRIM_SHIFT	1
#define  PHY_R20_USB2_OTG_VBUSDET_EN		(1 << 4)
#define  PHY_R20_USB2_DMON_SEL_SHIFT		9
#define  PHY_R20_USB2_EDGE_DRV_EN		(1 << 13)
#define  PHY_R20_USB2_EDGE_DRV_TRIM_SHIFT	14
#define  PHY_R20_USB2_BGR_ADJ_SHIFT		16
#define  PHY_R20_USB2_BGR_VREF_SHIFT		24
#define  PHY_R20_USB2_BGR_DBG_SHIFT		29
#define PHY_R21				0x54
#define  PHY_R21_USB2_OTG_ACA_EN		(1 << 2)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct amlusbphy_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct phy_device	sc_pd;
};

int amlusbphy_match(struct device *, void *, void *);
void amlusbphy_attach(struct device *, struct device *, void *);

const struct cfattach	amlusbphy_ca = {
	sizeof (struct amlusbphy_softc), amlusbphy_match, amlusbphy_attach
};

struct cfdriver amlusbphy_cd = {
	NULL, "amlusbphy", DV_DULL
};

int	amlusbphy_enable(void *, uint32_t *);

int
amlusbphy_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "amlogic,g12a-usb2-phy");
}

void
amlusbphy_attach(struct device *parent, struct device *self, void *aux)
{
	struct amlusbphy_softc *sc = (struct amlusbphy_softc *)self;
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

	printf("\n");

	sc->sc_pd.pd_node = faa->fa_node;
	sc->sc_pd.pd_cookie = sc;
	sc->sc_pd.pd_enable = amlusbphy_enable;
	phy_register(&sc->sc_pd);
}

int
amlusbphy_enable(void *cookie, uint32_t *cells)
{
	struct amlusbphy_softc *sc = cookie;
	int node = sc->sc_pd.pd_node;
	uint32_t phy_supply;

	clock_enable_all(node);

	reset_assert_all(node);
	delay(10);
	reset_deassert_all(node);
	delay(1000);

	phy_supply = OF_getpropint(node, "phy-supply", 0);
	if (phy_supply)
		regulator_enable(phy_supply);

	HCLR4(sc, PHY_R21, PHY_R21_USB2_OTG_ACA_EN);

	/* Set PLL to 480 MHz. */
	HWRITE4(sc, PHY_R16, (20 << PHY_R16_MPLL_M_SHIFT) |
	    (1 << PHY_R16_MPLL_N_SHIFT) | PHY_R16_MPLL_LOAD |
	    (1 << PHY_R16_MPLL_LOCK_LONG_SHIFT) | PHY_R16_MPLL_FAST_LOCK |
	    PHY_R16_MPLL_EN | PHY_R16_MPLL_RESET);
	HWRITE4(sc, PHY_R17, (0 << PHY_R17_MPLL_FRAC_IN_SHIFT) |
	    (7 << PHY_R17_MPLL_LAMBDA0_SHIFT) |
	    (7 << PHY_R17_MPLL_LAMBDA1_SHIFT) |
	    (9 << PHY_R17_MPLL_FILTER_PVT1_SHIFT) |
	    (2 << PHY_R17_MPLL_FILTER_PVT2_SHIFT));
	HWRITE4(sc, PHY_R18, (1 << PHY_R18_MPLL_LKW_SEL_SHIFT) |
	    (9 << PHY_R18_MPLL_LK_W_SHIFT) | (39 << PHY_R18_MPLL_LK_S_SHIFT) |
	    (1 << PHY_R18_MPLL_PFD_GAIN_SHIFT) |
	    (7 << PHY_R18_MPLL_ROU_SHIFT) |
	    (3 << PHY_R18_MPLL_DATA_SEL_SHIFT) |
	    (1 << PHY_R18_MPLL_BIAS_ADJ_SHIFT) |
	    (0 << PHY_R18_MPLL_BB_MODE_SHIFT) |
	    (3 << PHY_R18_MPLL_ALPHA_SHIFT) |
	    (1 << PHY_R18_MPLL_ADJ_LDO_SHIFT) |
	    PHY_R18_MPLL_ACG_RANGE);
	delay(100);
	HWRITE4(sc, PHY_R16, (20 << PHY_R16_MPLL_M_SHIFT) |
	    (1 << PHY_R16_MPLL_N_SHIFT) | PHY_R16_MPLL_LOAD |
	    (1 << PHY_R16_MPLL_LOCK_LONG_SHIFT) |  PHY_R16_MPLL_FAST_LOCK |
	    PHY_R16_MPLL_EN);

	/* Tune PHY. */
	HWRITE4(sc, PHY_R20, (4 << PHY_R20_USB2_ITG_VBUS_TRIM_SHIFT) |
	    PHY_R20_USB2_OTG_VBUSDET_EN | (15 << PHY_R20_USB2_DMON_SEL_SHIFT) |
	    PHY_R20_USB2_EDGE_DRV_EN |
	    (3 << PHY_R20_USB2_EDGE_DRV_TRIM_SHIFT) |
	    (0 << PHY_R20_USB2_BGR_ADJ_SHIFT) |
	    (0 << PHY_R20_USB2_BGR_VREF_SHIFT) |
	    (0 << PHY_R20_USB2_BGR_DBG_SHIFT));
	HWRITE4(sc, PHY_R4, (0xfff << PHY_R4_CALIB_CODE_SHIFT) |
	    PHY_R4_TEST_BYPASS_MODE_EN |
	    (0 << PHY_R4_I_C2L_BIAS_TRIM_SHIFT));

	/* Tune disconnect threshold. */
	HWRITE4(sc, PHY_R3, (0 << PHY_R3_SQUELCH_REF_SHIFT) |
	    (1 << PHY_R3_HDISC_REF_SHIFT) | (3 << PHY_R3_DISC_THRESH_SHIFT));

	/* Analog settings. */
	HWRITE4(sc, PHY_R14, 0);
	HWRITE4(sc, PHY_R13, PHY_R13_UPDATE_PMA_SIGNALS |
	    (7 << PHY_R13_MIN_COUNT_FOR_SYNC_DET_SHIFT));

	return 0;
}
