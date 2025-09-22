/*	$OpenBSD: rkpciephy.c,v 1.3 2023/07/09 19:11:30 patrick Exp $	*/
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

/* RK3568 GRF registers */
#define GRF_PCIE30PHY_CON(idx)			((idx) * 4)
/* CON1 */
#define  GRF_PCIE30PHY_DA_OCM			0x80008000
/* CON5 */
#define  GRF_PCIE30PHY_LANE0_LINK_NUM_MASK	(0xf << 16)
#define  GRF_PCIE30PHY_LANE0_LINK_NUM_SHIFT	0
/* CON6 */
#define  GRF_PCIE30PHY_LANE1_LINK_NUM_MASK	(0xf << 16)
#define  GRF_PCIE30PHY_LANE1_LINK_NUM_SHIFT	0
/* STATUS0 */
#define GRF_PCIE30PHY_STATUS0			0x80
#define  GRF_PCIE30PHY_SRAM_INIT_DONE		(1 << 14)

/* RK3588 GRF registers */
#define RK3588_PCIE3PHY_GRF_CMN_CON(idx)	((idx) * 4)
#define  RK3588_GRF_PCIE3PHY_DA_OCM		((0x1 << 24) | (1 << 8))
#define  RK3588_GRF_PCIE3PHY_LANE_BIFURCATE_0_1	(1 << 0)
#define  RK3588_GRF_PCIE3PHY_LANE_BIFURCATE_2_3	(1 << 1)
#define  RK3588_GRF_PCIE3PHY_LANE_AGGREGATE	(1 << 2)
#define  RK3588_GRF_PCIE3PHY_LANE_MASK		(0x7 << 16)
#define RK3588_PCIE3PHY_GRF_PHY0_STATUS1	0x904
#define RK3588_PCIE3PHY_GRF_PHY1_STATUS1	0xa04
#define  RK3588_PCIE3PHY_SRAM_INIT_DONE		(1 << 0)
#define RK3588_PHP_GRF_PCIESEL_CON		0x100
#define  RK3588_PHP_GRF_PCIE0L0_PCIE3		(1 << 0)
#define  RK3588_PHP_GRF_PCIE0L1_PCIE3		(1 << 1)
#define  RK3588_PHP_GRF_PCIE0L0_MASK		(0x1 << 16)
#define  RK3588_PHP_GRF_PCIE0L1_MASK		(0x1 << 17)

struct rkpciephy_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct phy_device	sc_pd;
};

int	rkpciephy_match(struct device *, void *, void *);
void	rkpciephy_attach(struct device *, struct device *, void *);

const struct cfattach rkpciephy_ca = {
	sizeof (struct rkpciephy_softc), rkpciephy_match, rkpciephy_attach
};

struct cfdriver rkpciephy_cd = {
	NULL, "rkpciephy", DV_DULL
};

int	rk3568_pciephy_enable(void *, uint32_t *);
int	rk3588_pciephy_enable(void *, uint32_t *);

int
rkpciephy_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "rockchip,rk3568-pcie3-phy") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3588-pcie3-phy"));
}

void
rkpciephy_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkpciephy_softc *sc = (struct rkpciephy_softc *)self;
	struct fdt_attach_args *faa = aux;

	printf("\n");

	sc->sc_pd.pd_node = faa->fa_node;
	sc->sc_pd.pd_cookie = sc;
	if (OF_is_compatible(faa->fa_node, "rockchip,rk3568-pcie3-phy"))
		sc->sc_pd.pd_enable = rk3568_pciephy_enable;
	if (OF_is_compatible(faa->fa_node, "rockchip,rk3588-pcie3-phy"))
		sc->sc_pd.pd_enable = rk3588_pciephy_enable;
	phy_register(&sc->sc_pd);
}

int
rk3568_pciephy_enable(void *cookie, uint32_t *cells)
{
	struct rkpciephy_softc *sc = cookie;
	struct regmap *rm;
	int node = sc->sc_pd.pd_node;
	uint32_t data_lanes[2] = { 0, 0 };
	uint32_t grf, stat;
	int timo;

	grf = OF_getpropint(node, "rockchip,phy-grf", 0);
	rm = regmap_byphandle(grf);
	if (rm == NULL)
		return ENXIO;

	clock_enable_all(node);
	reset_assert(node, "phy");
	delay(1);

	regmap_write_4(rm, GRF_PCIE30PHY_CON(9), GRF_PCIE30PHY_DA_OCM);

	OF_getpropintarray(node, "data-lanes", data_lanes, sizeof(data_lanes));
	if (data_lanes[0] > 0) {
		regmap_write_4(rm, GRF_PCIE30PHY_CON(5),
		    GRF_PCIE30PHY_LANE0_LINK_NUM_MASK |
		    (data_lanes[0] - 1) << GRF_PCIE30PHY_LANE0_LINK_NUM_SHIFT);
	}
	if (data_lanes[1] > 0) {
		regmap_write_4(rm, GRF_PCIE30PHY_CON(6),
		    GRF_PCIE30PHY_LANE1_LINK_NUM_MASK |
		    (data_lanes[1] - 1) << GRF_PCIE30PHY_LANE1_LINK_NUM_SHIFT);
	}
	if (data_lanes[0] > 1 || data_lanes[1] > 1)
		regmap_write_4(rm, GRF_PCIE30PHY_CON(1), GRF_PCIE30PHY_DA_OCM);

	reset_deassert(node, "phy");

	for (timo = 500; timo > 0; timo--) {
		stat = regmap_read_4(rm, GRF_PCIE30PHY_STATUS0);
		if (stat & GRF_PCIE30PHY_SRAM_INIT_DONE)
			break;
		delay(100);
	}
	if (timo == 0) {
		printf("%s: timeout\n", sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}

	return 0;
}

int
rk3588_pciephy_enable(void *cookie, uint32_t *cells)
{
	struct rkpciephy_softc *sc = cookie;
	struct regmap *phy, *pipe;
	int node = sc->sc_pd.pd_node;
	uint32_t data_lanes[4] = { 1, 1, 1, 1 };
	uint32_t grf, reg, stat;
	int num_lanes, timo;

	grf = OF_getpropint(node, "rockchip,phy-grf", 0);
	phy = regmap_byphandle(grf);
	if (phy == NULL)
		return ENXIO;

	clock_enable_all(node);
	reset_assert(node, "phy");
	delay(1);

	regmap_write_4(phy, RK3588_PCIE3PHY_GRF_CMN_CON(0),
	    RK3588_GRF_PCIE3PHY_DA_OCM);

	num_lanes = OF_getpropintarray(node, "data-lanes", data_lanes,
	    sizeof(data_lanes));
	/* Use default setting in case of missing properties. */
	if (num_lanes <= 0)
		num_lanes = sizeof(data_lanes);
	num_lanes /= sizeof(uint32_t);

	reg = RK3588_GRF_PCIE3PHY_LANE_MASK;
	/* If all links go to the first, aggregate toward x4 */
	if (num_lanes >= 4 &&
	    data_lanes[0] == 1 && data_lanes[1] == 1 &&
	    data_lanes[2] == 1 && data_lanes[3] == 1) {
		reg |= RK3588_GRF_PCIE3PHY_LANE_AGGREGATE;
	} else {
		/* If lanes 0+1 are not towards the same controller, split. */
		if (num_lanes >= 2 && data_lanes[0] != data_lanes[1])
			reg |= RK3588_GRF_PCIE3PHY_LANE_BIFURCATE_0_1;
		/* If lanes 2+3 are not towards the same controller, split. */
		if (num_lanes >= 4 && data_lanes[2] != data_lanes[3])
			reg |= RK3588_GRF_PCIE3PHY_LANE_BIFURCATE_2_3;
	}
	regmap_write_4(phy, RK3588_PCIE3PHY_GRF_CMN_CON(0), reg);

	grf = OF_getpropint(node, "rockchip,pipe-grf", 0);
	pipe = regmap_byphandle(grf);
	if (pipe != NULL) {
		reg = RK3588_PHP_GRF_PCIE0L0_MASK | RK3588_PHP_GRF_PCIE0L1_MASK;
		/* If lane 1 goes to PCIe3_1L0, move from Combo to PCIE3 PHY */
		if (num_lanes >= 2 && data_lanes[1] == 2)
			reg |= RK3588_PHP_GRF_PCIE0L0_PCIE3;
		/* If lane 3 goes to PCIe3_1L1, move from Combo to PCIE3 PHY */
		if (num_lanes >= 4 && data_lanes[3] == 4)
			reg |= RK3588_PHP_GRF_PCIE0L1_PCIE3;
		regmap_write_4(pipe, RK3588_PHP_GRF_PCIESEL_CON, reg);
	}

	reset_deassert(node, "phy");

	for (timo = 500; timo > 0; timo--) {
		stat = regmap_read_4(phy, RK3588_PCIE3PHY_GRF_PHY0_STATUS1);
		if (stat & RK3588_PCIE3PHY_SRAM_INIT_DONE)
			break;
		delay(100);
	}
	for (; timo > 0; timo--) {
		stat = regmap_read_4(phy, RK3588_PCIE3PHY_GRF_PHY1_STATUS1);
		if (stat & RK3588_PCIE3PHY_SRAM_INIT_DONE)
			break;
		delay(100);
	}
	if (timo == 0) {
		printf("%s: timeout\n", sc->sc_dev.dv_xname);
		return ETIMEDOUT;
	}

	return 0;
}
