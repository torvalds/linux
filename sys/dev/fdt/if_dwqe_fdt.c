/*	$OpenBSD: if_dwqe_fdt.c,v 1.19 2025/05/17 13:30:31 kettenis Exp $	*/
/*
 * Copyright (c) 2008, 2019 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2017, 2022 Patrick Wildt <patrick@blueri.se>
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
 * Driver for the Synopsys Designware ethernet controller.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/timeout.h>
#include <sys/task.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <net/if.h>
#include <net/if_media.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/ic/dwqevar.h>
#include <dev/ic/dwqereg.h>

struct dwqe_fdt_softc {
	struct dwqe_softc	sc_sc;
	struct if_device	sc_ifd;
	int			sc_gmac_id;
};

int	dwqe_fdt_match(struct device *, void *, void *);
void	dwqe_fdt_attach(struct device *, struct device *, void *);
void	dwqe_setup_jh7110(struct dwqe_softc *);
void	dwqe_mii_statchg_jh7110(struct device *);
void	dwqe_setup_rk3528(struct dwqe_fdt_softc *);
void	dwqe_mii_statchg_rk3528(struct device *);
void	dwqe_setup_rk3568(struct dwqe_fdt_softc *);
void	dwqe_mii_statchg_rk3568(struct device *);
void	dwqe_setup_rk3588(struct dwqe_fdt_softc *);
void	dwqe_mii_statchg_rk3588(struct device *);

const struct cfattach dwqe_fdt_ca = {
	sizeof(struct dwqe_fdt_softc), dwqe_fdt_match, dwqe_fdt_attach
};

void	dwqe_reset_phy(struct dwqe_softc *, uint32_t);

int
dwqe_fdt_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "snps,dwmac-4.20a") ||
	    OF_is_compatible(faa->fa_node, "snps,dwmac-5.20");
}

void
dwqe_fdt_attach(struct device *parent, struct device *self, void *aux)
{
	struct dwqe_fdt_softc *fsc = (void *)self;
	struct dwqe_softc *sc = &fsc->sc_sc;
	struct fdt_attach_args *faa = aux;
	char phy_mode[16] = { 0 };
	uint32_t phy, phy_supply;
	uint32_t axi_config;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int i, node;

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": cannot map registers\n");
		return;
	}
	sc->sc_dmat = faa->fa_dmat;

	/* Decide GMAC id through address */
	switch (faa->fa_reg[0].addr) {
	case 0xffbd0000:	/* RK3528 */
	case 0xfe2a0000:	/* RK3568 */
	case 0xfe1b0000:	/* RK3588 */
	case 0x16030000:	/* JH7110 */
		fsc->sc_gmac_id = 0;
		break;
	case 0xffbe0000:	/* RK3528 */
	case 0xfe010000:	/* RK3568 */
	case 0xfe1c0000:	/* RK3588 */
	case 0x16040000:	/* JH7110 */
		fsc->sc_gmac_id = 1;
		break;
	default:
		printf(": unknown controller at 0x%llx\n", faa->fa_reg[0].addr);
		return;
	}

	printf(" gmac %d", fsc->sc_gmac_id);

	OF_getprop(faa->fa_node, "phy-mode", phy_mode, sizeof(phy_mode));
	if (strcmp(phy_mode, "rgmii") == 0)
		sc->sc_phy_mode = DWQE_PHY_MODE_RGMII;
	else if (strcmp(phy_mode, "rgmii-rxid") == 0)
		sc->sc_phy_mode = DWQE_PHY_MODE_RGMII_RXID;
	else if (strcmp(phy_mode, "rgmii-txid") == 0)
		sc->sc_phy_mode = DWQE_PHY_MODE_RGMII_TXID;
	else if (strcmp(phy_mode, "rgmii-id") == 0)
		sc->sc_phy_mode = DWQE_PHY_MODE_RGMII_ID;
	else if (strcmp(phy_mode, "rmii") == 0)
		sc->sc_phy_mode = DWQE_PHY_MODE_RMII;
	else
		sc->sc_phy_mode = DWQE_PHY_MODE_UNKNOWN;

	/* Lookup PHY. */
	phy = OF_getpropint(faa->fa_node, "phy", 0);
	if (phy == 0)
		phy = OF_getpropint(faa->fa_node, "phy-handle", 0);
	node = OF_getnodebyphandle(phy);
	if (node)
		sc->sc_phyloc = OF_getpropint(node, "reg", MII_PHY_ANY);
	else
		sc->sc_phyloc = MII_PHY_ANY;
	sc->sc_mii.mii_node = node;

	pinctrl_byname(faa->fa_node, "default");

	/* Enable clocks. */
	clock_set_assigned(faa->fa_node);
	clock_enable(faa->fa_node, "stmmaceth");
	clock_enable(faa->fa_node, "pclk");
	reset_deassert(faa->fa_node, "stmmaceth");
	reset_deassert(faa->fa_node, "ahb");
	if (OF_is_compatible(faa->fa_node, "starfive,jh7110-dwmac")) {
		clock_enable(faa->fa_node, "tx");
		clock_enable(faa->fa_node, "gtx");
	} else if (OF_is_compatible(faa->fa_node, "rockchip,rk3528-gmac") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3568-gmac") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3588-gmac")) {
		clock_enable(faa->fa_node, "aclk_mac");
		clock_enable(faa->fa_node, "pclk_mac");
		clock_enable(faa->fa_node, "mac_clk_tx");
		clock_enable(faa->fa_node, "clk_mac_speed");
		if (strcmp(phy_mode, "rmii") == 0) {
			clock_enable(faa->fa_node, "mac_clk_rx");
			clock_enable(faa->fa_node, "clk_mac_ref");
			clock_enable(faa->fa_node, "clk_mac_refout");
		}
	}
	delay(5000);

	/* Do hardware specific initializations. */
	if (OF_is_compatible(faa->fa_node, "starfive,jh7110-dwmac"))
		dwqe_setup_jh7110(sc);
	else if (OF_is_compatible(faa->fa_node, "rockchip,rk3528-gmac"))
		dwqe_setup_rk3528(fsc);
	else if (OF_is_compatible(faa->fa_node, "rockchip,rk3568-gmac"))
		dwqe_setup_rk3568(fsc);
	else if (OF_is_compatible(faa->fa_node, "rockchip,rk3588-gmac"))
		dwqe_setup_rk3588(fsc);

	/* Power up PHY. */
	phy_supply = OF_getpropint(faa->fa_node, "phy-supply", 0);
	if (phy_supply)
		regulator_enable(phy_supply);

	/* Reset PHY */
	dwqe_reset_phy(sc, phy);

	node = OF_getnodebyname(sc->sc_node, "fixed-link");
	if (node) {
		sc->sc_fixed_link = 1;

		ifp->if_baudrate = IF_Mbps(OF_getpropint(node, "speed", 0));
		ifp->if_link_state = OF_getpropbool(node, "full-duplex") ?
		    LINK_STATE_FULL_DUPLEX : LINK_STATE_HALF_DUPLEX;
	}

	sc->sc_clkrate = clock_get_frequency(faa->fa_node, "stmmaceth");
	if (sc->sc_clkrate > 500000000)
		sc->sc_clk = GMAC_MAC_MDIO_ADDR_CR_500_800;
	else if (sc->sc_clkrate > 300000000)
		sc->sc_clk = GMAC_MAC_MDIO_ADDR_CR_300_500;
	else if (sc->sc_clkrate > 150000000)
		sc->sc_clk = GMAC_MAC_MDIO_ADDR_CR_150_250;
	else if (sc->sc_clkrate > 100000000)
		sc->sc_clk = GMAC_MAC_MDIO_ADDR_CR_100_150;
	else if (sc->sc_clkrate > 60000000)
		sc->sc_clk = GMAC_MAC_MDIO_ADDR_CR_60_100;
	else if (sc->sc_clkrate > 35000000)
		sc->sc_clk = GMAC_MAC_MDIO_ADDR_CR_35_60;
	else
		sc->sc_clk = GMAC_MAC_MDIO_ADDR_CR_20_35;

	for (i = 0; i < 4; i++)
		sc->sc_hw_feature[i] = dwqe_read(sc, GMAC_MAC_HW_FEATURE(i));

	if (OF_getprop(faa->fa_node, "local-mac-address",
	    &sc->sc_lladdr, ETHER_ADDR_LEN) != ETHER_ADDR_LEN)
		dwqe_lladdr_read(sc, sc->sc_lladdr);

	sc->sc_force_thresh_dma_mode =
	    OF_getpropbool(faa->fa_node, "snps,force_thresh_dma_mode");

	dwqe_reset(sc);

	sc->sc_fixed_burst = OF_getpropbool(faa->fa_node, "snps,fixed-burst");
	sc->sc_mixed_burst = OF_getpropbool(faa->fa_node, "snps,mixed-burst");
	sc->sc_aal = OF_getpropbool(faa->fa_node, "snps,aal");
	sc->sc_8xpbl = !OF_getpropbool(faa->fa_node, "snps,no-pbl-x8");
	sc->sc_pbl = OF_getpropint(faa->fa_node, "snps,pbl", 8);
	sc->sc_txpbl = OF_getpropint(faa->fa_node, "snps,txpbl", sc->sc_pbl);
	sc->sc_rxpbl = OF_getpropint(faa->fa_node, "snps,rxpbl", sc->sc_pbl);

	/* Configure AXI master. */
	axi_config = OF_getpropint(faa->fa_node, "snps,axi-config", 0);
	node = OF_getnodebyphandle(axi_config);
	if (node) {
		sc->sc_axi_config = 1;
		sc->sc_lpi_en = OF_getpropbool(node, "snps,lpi_en");
		sc->sc_xit_frm = OF_getpropbool(node, "snps,xit_frm");

		sc->sc_wr_osr_lmt = OF_getpropint(node, "snps,wr_osr_lmt", 1);
		sc->sc_rd_osr_lmt = OF_getpropint(node, "snps,rd_osr_lmt", 1);

		OF_getpropintarray(node, "snps,blen", sc->sc_blen, sizeof(sc->sc_blen));
	}

	if (dwqe_attach(sc) != 0)
		return;

	if (OF_is_compatible(faa->fa_node, "starfive,jh7110-dwmac") &&
	    !OF_getpropbool(faa->fa_node, "starfive,tx-use-rgmii-clk"))
		sc->sc_mii.mii_statchg = dwqe_mii_statchg_jh7110;
	else if (OF_is_compatible(faa->fa_node, "rockchip,rk3528-gmac"))
		sc->sc_mii.mii_statchg = dwqe_mii_statchg_rk3528;
	else if (OF_is_compatible(faa->fa_node, "rockchip,rk3568-gmac"))
		sc->sc_mii.mii_statchg = dwqe_mii_statchg_rk3568;
	else if (OF_is_compatible(faa->fa_node, "rockchip,rk3588-gmac"))
		sc->sc_mii.mii_statchg = dwqe_mii_statchg_rk3588;

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_NET | IPL_MPSAFE,
	    dwqe_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL)
		printf("%s: can't establish interrupt\n", sc->sc_dev.dv_xname);

	fsc->sc_ifd.if_node = faa->fa_node;
	fsc->sc_ifd.if_ifp = ifp;
	if_register(&fsc->sc_ifd);

	/* force a configuration of the clocks/mac */
	if (sc->sc_fixed_link)
		sc->sc_mii.mii_statchg(self);
}

void
dwqe_reset_phy(struct dwqe_softc *sc, uint32_t phy)
{
	uint32_t *gpio;
	uint32_t delays[3];
	int active = 1;
	int node, len;

	node = OF_getnodebyphandle(phy);
	if (node && OF_getproplen(node, "reset-gpios") > 0) {
		len = OF_getproplen(node, "reset-gpios");

		gpio = malloc(len, M_TEMP, M_WAITOK);

		/* Gather information. */
		OF_getpropintarray(node, "reset-gpios", gpio, len);
		delays[0] = OF_getpropint(node, "reset-deassert-us", 0);
		delays[1] = OF_getpropint(node, "reset-assert-us", 0);
		delays[2] = OF_getpropint(node, "reset-deassert-us", 0);
	} else {
		len = OF_getproplen(sc->sc_node, "snps,reset-gpio");
		if (len <= 0)
			return;

		gpio = malloc(len, M_TEMP, M_WAITOK);

		/* Gather information. */
		OF_getpropintarray(sc->sc_node, "snps,reset-gpio", gpio, len);
		if (OF_getpropbool(sc->sc_node, "snps-reset-active-low"))
			active = 0;
		delays[0] = delays[1] = delays[2] = 0;
		OF_getpropintarray(sc->sc_node, "snps,reset-delays-us", delays,
		    sizeof(delays));
	}

	/* Perform reset sequence. */
	gpio_controller_config_pin(gpio, GPIO_CONFIG_OUTPUT);
	gpio_controller_set_pin(gpio, !active);
	delay(delays[0]);
	gpio_controller_set_pin(gpio, active);
	delay(delays[1]);
	gpio_controller_set_pin(gpio, !active);
	delay(delays[2]);

	free(gpio, M_TEMP, len);
}

/* JH7110 registers */
#define JH7110_PHY_INTF_RGMII		1
#define JH7110_PHY_INTF_RMII		4

/* RK3528 registers */
#define RK3528_VO_GRF_GMAC_CON		0x0018
#define  RK3528_GMAC0_PHY_INTF_SEL_RMII		((1U << 1) << 16 | (1 << 1))
#define  RK3528_GMAC0_CLK_RMII_DIV2		((1U << 3) << 16 | (1 << 3))
#define  RK3528_GMAC0_CLK_RMII_DIV20		((1U << 3) << 16 | (0 << 3))
#define RK3528_VPU_GRF_GMAC_CON5	0x0018
#define  RK3528_GMAC1_PHY_INTF_SEL_RMII		((1U << 8) << 16 | (1 << 8))
#define  RK3528_GMAC1_PHY_INTF_SEL_RGMII	((1U << 8) << 16 | (0 << 8))
#define  RK3528_GMAC1_CLK_RMII_DIV2		((1U << 10) << 16 | (1 << 10))
#define  RK3528_GMAC1_CLK_RMII_DIV20		((1U << 10) << 16 | (0 << 10))
#define  RK3528_GMAC_TXCLK_DLY_SET(_v)		((1U << 14) << 16 | ((_v) << 14))
#define  RK3528_GMAC_RXCLK_DLY_SET(_v)		((1U << 15) << 16 | ((_v) << 15))
#define  RK3528_GMAC1_CLK_RGMII_DIV1	((0x3U << 10) << 16 | (0x0 << 10))
#define  RK3528_GMAC1_CLK_RGMII_DIV5	((0x3U << 10) << 16 | (0x3 << 10))
#define  RK3528_GMAC1_CLK_RGMII_DIV50	((0x3U << 10) << 16 | (0x2 << 10))
#define RK3528_VPU_GRF_GMAC_CON6	0x001c
#define  RK3528_GMAC_CLK_RX_DL_CFG(_v)		((0xffU << 8) << 16 | ((_v) << 8))
#define  RK3528_GMAC_CLK_TX_DL_CFG(_v)		((0xffU << 0) << 16 | ((_v) << 0))

/* RK3568 registers */
#define RK3568_GRF_GMACx_CON0(x)	(0x0380 + (x) * 0x8)
#define  RK3568_GMAC_CLK_RX_DL_CFG(val)		((0x7f << 8) << 16 | ((val) << 8))
#define  RK3568_GMAC_CLK_TX_DL_CFG(val)		((0x7f << 0) << 16 | ((val) << 0))
#define RK3568_GRF_GMACx_CON1(x)	(0x0384 + (x) * 0x8)
#define  RK3568_GMAC_PHY_INTF_SEL_RGMII		((0x7 << 4) << 16 | (0x1 << 4))
#define  RK3568_GMAC_PHY_INTF_SEL_RMII		((0x7 << 4) << 16 | (0x4 << 4))
#define  RK3568_GMAC_TXCLK_DLY_SET(_v)		((1 << 0) << 16 | ((_v) << 0))
#define  RK3568_GMAC_RXCLK_DLY_SET(_v)		((1 << 1) << 16 | ((_v) << 1))

/* RK3588 registers */
#define RK3588_GRF_GMAC_CON7		0x031c
#define  RK3588_GMACx_RXCLK_DLY_ENA(id)		((1 << (2 * (id) + 3)) << 16 | (1 << (2 * (id) + 3)))
#define  RK3588_GMACx_TXCLK_DLY_ENA(id)		((1 << (2 * (id) + 2)) << 16 | (1 << (2 * (id) + 2)))
#define RK3588_GRF_GMAC_CON8		0x0320
#define RK3588_GRF_GMAC_CON9		0x0324
#define  RK3588_GMAC_CLK_RX_DL_CFG(val)		((0x7f << 8) << 16 | ((val) << 8))
#define  RK3588_GMAC_CLK_TX_DL_CFG(val)		((0x7f << 0) << 16 | ((val) << 0))
#define RK3588_PHP_GRF_GMAC_CON0	0x0008
#define  RK3588_GMACx_PHY_INTF_SEL_RGMII(id)	((0x7 << (6 * (id) + 3)) << 16 | (0x1 << (6 * (id) + 3)))
#define  RK3588_GMACx_PHY_INTF_SEL_RMII(id)	((0x7 << (6 * (id) + 3)) << 16 | (0x4 << (6 * (id) + 3)))
#define RK3588_PHP_GRF_CLK_CON1		0x0070
#define  RK3588_RMII_MODE_GMACx_RMII(id)	((0x1 << (5 * (id))) << 16 | (0x1 << (5 * (id))))
#define  RK3588_RMII_MODE_GMACx_RGMII(id)	((0x1 << (5 * (id))) << 16 | (0x0 << (5 * (id))))
#define  RK3588_MII_TX_CLK_SEL_RMII_2_5(id)	((0x3 << (5 * (id) + 2)) << 16 | (0x1 << (5 * (id) + 2)))
#define  RK3588_MII_TX_CLK_SEL_RMII_25(id)	((0x3 << (5 * (id) + 2)) << 16 | (0x0 << (5 * (id) + 2)))
#define  RK3588_MII_TX_CLK_SEL_RGMII_2_5(id)	((0x3 << (5 * (id) + 2)) << 16 | (0x2 << (5 * (id) + 2)))
#define  RK3588_MII_TX_CLK_SEL_RGMII_25(id)	((0x3 << (5 * (id) + 2)) << 16 | (0x3 << (5 * (id) + 2)))
#define  RK3588_MII_TX_CLK_SEL_RGMII_125(id)	((0x3 << (5 * (id) + 2)) << 16 | (0x0 << (5 * (id) + 2)))

void	dwqe_mii_statchg_jh7110_task(void *);
void	dwqe_mii_statchg_rk3568_task(void *);

void
dwqe_setup_jh7110(struct dwqe_softc *sc)
{
	struct regmap *rm;
	uint32_t cells[3];
	uint32_t phandle, offset, reg, shift;
	char phy_mode[32];
	uint32_t iface;

	if (OF_getpropintarray(sc->sc_node, "starfive,syscon", cells,
	    sizeof(cells)) != sizeof(cells)) {
		printf("%s: failed to get starfive,syscon\n", __func__);
		return;
	}
	phandle = cells[0];
	offset = cells[1];
	shift = cells[2];

	rm = regmap_byphandle(phandle);
	if (rm == NULL) {
		printf("%s: failed to get regmap\n", __func__);
		return;
	}

	if (OF_getprop(sc->sc_node, "phy-mode", phy_mode,
	    sizeof(phy_mode)) <= 0)
		return;

	if (strcmp(phy_mode, "rgmii") == 0 ||
	    strcmp(phy_mode, "rgmii-id") == 0) {
		iface = JH7110_PHY_INTF_RGMII;
	} else if (strcmp(phy_mode, "rmii") == 0) {
		iface = JH7110_PHY_INTF_RMII;
	} else
		return;

	reg = regmap_read_4(rm, offset);
	reg &= ~(((1U << 3) - 1) << shift);
	reg |= iface << shift;
	regmap_write_4(rm, offset, reg);

	task_set(&sc->sc_statchg_task,
	    dwqe_mii_statchg_jh7110_task, sc);
}

void
dwqe_mii_statchg_jh7110_task(void *arg)
{
	struct dwqe_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	dwqe_mii_statchg(&sc->sc_dev);

	switch (ifp->if_baudrate) {
	case IF_Mbps(10):
		clock_set_frequency(sc->sc_node, "tx", 2500000);
		break;
	case IF_Mbps(100):
		clock_set_frequency(sc->sc_node, "tx", 25000000);
		break;
	case IF_Mbps(1000):
		clock_set_frequency(sc->sc_node, "tx", 125000000);
		break;
	}
}

void
dwqe_mii_statchg_jh7110(struct device *self)
{
	struct dwqe_softc *sc = (void *)self;

	task_add(systq, &sc->sc_statchg_task);
}

void
dwqe_setup_rk3528(struct dwqe_fdt_softc *fsc)
{
	struct dwqe_softc *sc = &fsc->sc_sc;
	struct regmap *rm;
	uint32_t grf;
	int tx_delay, rx_delay;
	uint32_t iface;

	grf = OF_getpropint(sc->sc_node, "rockchip,grf", 0);
	rm = regmap_byphandle(grf);
	if (rm == NULL)
		return;

	if (fsc->sc_gmac_id == 0) {
		regmap_write_4(rm, RK3528_VO_GRF_GMAC_CON,
		    RK3528_GMAC0_PHY_INTF_SEL_RMII |
		    RK3528_GMAC0_CLK_RMII_DIV2);
		return;
	}

	switch (sc->sc_phy_mode) {
	case DWQE_PHY_MODE_RGMII:
	case DWQE_PHY_MODE_RGMII_ID:
	case DWQE_PHY_MODE_RGMII_RXID:
	case DWQE_PHY_MODE_RGMII_TXID:
		iface = RK3528_GMAC1_PHY_INTF_SEL_RGMII;
		break;
	case DWQE_PHY_MODE_RMII:
		iface = RK3528_GMAC1_PHY_INTF_SEL_RMII;
		break;
	default:
		return;
	}

	tx_delay = OF_getpropint(sc->sc_node, "tx_delay", 0x30);
	rx_delay = OF_getpropint(sc->sc_node, "rx_delay", 0x10);
	switch (sc->sc_phy_mode) {
	case DWQE_PHY_MODE_RGMII_ID:
		tx_delay = rx_delay = 0;
		break;
	case DWQE_PHY_MODE_RGMII_RXID:
		rx_delay = 0;
		break;
	case DWQE_PHY_MODE_RGMII_TXID:
		tx_delay = 0;
		break;
	default:
		break;
	}

	/* Program clock delay lines. */
	regmap_write_4(rm, RK3528_VPU_GRF_GMAC_CON6,
	    RK3528_GMAC_CLK_TX_DL_CFG(tx_delay) |
	    RK3528_GMAC_CLK_RX_DL_CFG(rx_delay));

	/* Set interface and enable/disable clock delay. */
	regmap_write_4(rm, RK3528_VPU_GRF_GMAC_CON5, iface |
	    RK3528_GMAC_TXCLK_DLY_SET(tx_delay > 0 ? 1 : 0) |
	    RK3528_GMAC_RXCLK_DLY_SET(rx_delay > 0 ? 1 : 0));
}

void
dwqe_mii_statchg_rk3528(struct device *self)
{
	struct dwqe_fdt_softc *fsc = (void *)self;
	struct dwqe_softc *sc = &fsc->sc_sc;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct regmap *rm;
	uint32_t grf;
	uint32_t reg, clk_sel = 0;

	dwqe_mii_statchg(self);

	grf = OF_getpropint(sc->sc_node, "rockchip,grf", 0);
	rm = regmap_byphandle(grf);
	if (rm == NULL)
		return;

	if (fsc->sc_gmac_id == 1)
		reg = RK3528_VPU_GRF_GMAC_CON5;
	else
		reg = RK3528_VO_GRF_GMAC_CON;

	if (sc->sc_phy_mode == DWQE_PHY_MODE_RMII) {
		switch (ifp->if_baudrate) {
		case IF_Mbps(10):
			if (fsc->sc_gmac_id == 1)
				clk_sel = RK3528_GMAC1_CLK_RMII_DIV20;
			else
				clk_sel = RK3528_GMAC0_CLK_RMII_DIV20;
			break;
		case IF_Mbps(100):
			if (fsc->sc_gmac_id == 1)
				clk_sel = RK3528_GMAC1_CLK_RMII_DIV2;
			else
				clk_sel = RK3528_GMAC0_CLK_RMII_DIV2;
			break;
		}
	} else {
		switch (ifp->if_baudrate) {
		case IF_Mbps(10):
			clk_sel = RK3528_GMAC1_CLK_RGMII_DIV50;
			break;
		case IF_Mbps(100):
			clk_sel = RK3528_GMAC1_CLK_RGMII_DIV5;
			break;
		case IF_Mbps(1000):
			clk_sel = RK3528_GMAC1_CLK_RGMII_DIV1;
			break;
		}
	}

	regmap_write_4(rm, reg, clk_sel);
}

void
dwqe_setup_rk3568(struct dwqe_fdt_softc *fsc)
{
	struct dwqe_softc *sc = &fsc->sc_sc;
	struct regmap *rm;
	uint32_t grf;
	int tx_delay, rx_delay;
	uint32_t iface;

	grf = OF_getpropint(sc->sc_node, "rockchip,grf", 0);
	rm = regmap_byphandle(grf);
	if (rm == NULL)
		return;

	switch (sc->sc_phy_mode) {
	case DWQE_PHY_MODE_RGMII:
	case DWQE_PHY_MODE_RGMII_ID:
	case DWQE_PHY_MODE_RGMII_RXID:
	case DWQE_PHY_MODE_RGMII_TXID:
		iface = RK3568_GMAC_PHY_INTF_SEL_RGMII;
		break;
	case DWQE_PHY_MODE_RMII:
		iface = RK3568_GMAC_PHY_INTF_SEL_RMII;
		break;
	default:
		return;
	}

	tx_delay = OF_getpropint(sc->sc_node, "tx_delay", 0x30);
	rx_delay = OF_getpropint(sc->sc_node, "rx_delay", 0x10);
	switch (sc->sc_phy_mode) {
	case DWQE_PHY_MODE_RGMII_ID:
		tx_delay = rx_delay = 0;
		break;
	case DWQE_PHY_MODE_RGMII_RXID:
		rx_delay = 0;
		break;
	case DWQE_PHY_MODE_RGMII_TXID:
		tx_delay = 0;
		break;
	default:
		break;
	}

	/* Program clock delay lines. */
	regmap_write_4(rm, RK3568_GRF_GMACx_CON0(fsc->sc_gmac_id),
	    RK3568_GMAC_CLK_TX_DL_CFG(tx_delay) |
	    RK3568_GMAC_CLK_RX_DL_CFG(rx_delay));

	/* Set interface and enable/disable clock delay. */
	regmap_write_4(rm, RK3568_GRF_GMACx_CON1(fsc->sc_gmac_id), iface |
	    RK3568_GMAC_TXCLK_DLY_SET(tx_delay > 0 ? 1 : 0) |
	    RK3568_GMAC_RXCLK_DLY_SET(rx_delay > 0 ? 1 : 0));

	task_set(&sc->sc_statchg_task,
	    dwqe_mii_statchg_rk3568_task, sc);
}

void
dwqe_mii_statchg_rk3568_task(void *arg)
{
	struct dwqe_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	dwqe_mii_statchg(&sc->sc_dev);

	switch (ifp->if_baudrate) {
	case IF_Mbps(10):
		clock_set_frequency(sc->sc_node, "clk_mac_speed", 2500000);
		break;
	case IF_Mbps(100):
		clock_set_frequency(sc->sc_node, "clk_mac_speed", 25000000);
		break;
	case IF_Mbps(1000):
		clock_set_frequency(sc->sc_node, "clk_mac_speed", 125000000);
		break;
	}
}

void
dwqe_mii_statchg_rk3568(struct device *self)
{
	struct dwqe_softc *sc = (void *)self;

	task_add(systq, &sc->sc_statchg_task);
}

void
dwqe_setup_rk3588(struct dwqe_fdt_softc *fsc)
{
	struct dwqe_softc *sc = &fsc->sc_sc;
	struct regmap *rm;
	struct regmap *php_rm;
	uint32_t grf, php_grf;
	int tx_delay, rx_delay;
	uint32_t iface, clk;

	grf = OF_getpropint(sc->sc_node, "rockchip,grf", 0);
	rm = regmap_byphandle(grf);
	if (rm == NULL)
		return;

	php_grf = OF_getpropint(sc->sc_node, "rockchip,php-grf", 0);
	php_rm = regmap_byphandle(php_grf);
	if (php_rm == NULL)
		return;

	switch (sc->sc_phy_mode) {
	case DWQE_PHY_MODE_RGMII:
	case DWQE_PHY_MODE_RGMII_ID:
	case DWQE_PHY_MODE_RGMII_RXID:
	case DWQE_PHY_MODE_RGMII_TXID:
		iface = RK3588_GMACx_PHY_INTF_SEL_RGMII(fsc->sc_gmac_id);
		clk = RK3588_RMII_MODE_GMACx_RGMII(fsc->sc_gmac_id);
		sc->sc_clk_sel_2_5 =
		    RK3588_MII_TX_CLK_SEL_RGMII_2_5(fsc->sc_gmac_id);
		sc->sc_clk_sel_25 =
		    RK3588_MII_TX_CLK_SEL_RGMII_25(fsc->sc_gmac_id);
		sc->sc_clk_sel_125 =
		    RK3588_MII_TX_CLK_SEL_RGMII_125(fsc->sc_gmac_id);
		break;
	case DWQE_PHY_MODE_RMII:
		iface = RK3588_GMACx_PHY_INTF_SEL_RMII(fsc->sc_gmac_id);
		clk = RK3588_RMII_MODE_GMACx_RMII(fsc->sc_gmac_id);
		sc->sc_clk_sel_2_5 =
		    RK3588_MII_TX_CLK_SEL_RMII_2_5(fsc->sc_gmac_id);
		sc->sc_clk_sel_25 =
		    RK3588_MII_TX_CLK_SEL_RMII_25(fsc->sc_gmac_id);
		break;
	default:
		return;
	}

	tx_delay = OF_getpropint(sc->sc_node, "tx_delay", 0x30);
	rx_delay = OF_getpropint(sc->sc_node, "rx_delay", 0x10);
	switch (sc->sc_phy_mode) {
	case DWQE_PHY_MODE_RGMII_ID:
		tx_delay = rx_delay = 0;
		break;
	case DWQE_PHY_MODE_RGMII_RXID:
		rx_delay = 0;
		break;
	case DWQE_PHY_MODE_RGMII_TXID:
		tx_delay = 0;
		break;
	default:
		break;
	}

	/* Set interface and clock. */
	regmap_write_4(php_rm, RK3588_PHP_GRF_GMAC_CON0, iface);
	regmap_write_4(php_rm, RK3588_PHP_GRF_CLK_CON1, clk);

	/* Enable clock delay. */
	regmap_write_4(rm, RK3588_GRF_GMAC_CON7,
	    RK3588_GMACx_TXCLK_DLY_ENA(fsc->sc_gmac_id) |
	    RK3588_GMACx_RXCLK_DLY_ENA(fsc->sc_gmac_id));

	/* Program clock delay lines. */
	regmap_write_4(rm, fsc->sc_gmac_id == 1 ?
	    RK3588_GRF_GMAC_CON9 : RK3588_GRF_GMAC_CON8,
	    RK3588_GMAC_CLK_TX_DL_CFG(tx_delay) |
	    RK3588_GMAC_CLK_RX_DL_CFG(rx_delay));
}

void
dwqe_mii_statchg_rk3588(struct device *self)
{
	struct dwqe_softc *sc = (void *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct regmap *php_rm;
	uint32_t php_grf;
	uint32_t gmac_clk_sel = 0;

	dwqe_mii_statchg(self);

	php_grf = OF_getpropint(sc->sc_node, "rockchip,php-grf", 0);
	php_rm = regmap_byphandle(php_grf);
	if (php_rm == NULL)
		return;

	switch (ifp->if_baudrate) {
	case IF_Mbps(10):
		gmac_clk_sel = sc->sc_clk_sel_2_5;
		break;
	case IF_Mbps(100):
		gmac_clk_sel = sc->sc_clk_sel_25;
		break;
	case IF_Mbps(1000):
		gmac_clk_sel = sc->sc_clk_sel_125;
		break;
	}

	regmap_write_4(php_rm, RK3588_PHP_GRF_CLK_CON1, gmac_clk_sel);
}
