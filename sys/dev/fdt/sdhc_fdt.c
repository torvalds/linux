/*	$OpenBSD: sdhc_fdt.c,v 1.23 2025/08/15 13:31:58 kettenis Exp $	*/
/*
 * Copyright (c) 2017 Mark Kettenis
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
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#include <dev/sdmmc/sdhcreg.h>
#include <dev/sdmmc/sdhcvar.h>
#include <dev/sdmmc/sdmmcvar.h>

/* RK3399 */
#define GRF_EMMCCORE_CON0_BASECLOCK		0xf000
#define  GRF_EMMCCORE_CON0_BASECLOCK_CLR		(0xff << 24)
#define  GRF_EMMCCORE_CON0_BASECLOCK_VAL(x)		(((x) & 0xff) << 8)
#define GRF_EMMCCORE_CON11			0xf02c
#define  GRF_EMMCCORE_CON11_CLOCKMULT_CLR		(0xff << 16)
#define  GRF_EMMCCORE_CON11_CLOCKMULT_VAL(x)		(((x) & 0xff) << 0)

/* Marvell Xenon */
#define XENON_SYS_OP_CTRL			0x108
#define  XENON_SYS_OP_CTRL_SLOT_ENABLE(x)		(1 << (x))
#define  XENON_SYS_OP_CTRL_SDCLK_IDLEOFF_ENABLE(x)	(1 << ((x) + 8))
#define  XENON_SYS_OP_CTRL_AUTO_CLKGATE_DISABLE		(1 << 20)
#define XENON_SYS_EXT_OP_CTRL			0x10c
#define  XENON_SYS_EXT_OP_CTRL_PARALLEL_TRAN(x)		(1 << (x))
#define  XENON_SYS_EXT_OP_CTRL_MASK_CMD_CONFLICT_ERR	(1 << 8)
#define XENON_SLOT_EMMC_CTRL			0x130
#define  XENON_SLOT_EMMC_CTRL_ENABLE_DATA_STROBE	(1 << 24)
#define  XENON_SLOT_EMMC_CTRL_ENABLE_RESP_STROBE	(1 << 25)
#define XENON_EMMC_PHY_TIMING_ADJUST		0x170
#define  XENON_EMMC_PHY_TIMING_ADJUST_SAMPL_INV_QSP_PHASE_SELECT (1 << 18)
#define  XENON_EMMC_PHY_TIMING_ADJUST_SDIO_MODE		(1 << 28)
#define  XENON_EMMC_PHY_TIMING_ADJUST_SLOW_MODE		(1 << 29)
#define  XENON_EMMC_PHY_TIMING_ADJUST_INIT		(1U << 31)
#define XENON_EMMC_PHY_FUNC_CONTROL		0x174
#define  XENON_EMMC_PHY_FUNC_CONTROL_DQ_ASYNC_MODE	(1 << 4)
#define  XENON_EMMC_PHY_FUNC_CONTROL_DQ_DDR_MODE	(0xff << 8)
#define  XENON_EMMC_PHY_FUNC_CONTROL_CMD_DDR_MODE	(1 << 16)
#define XENON_EMMC_PHY_PAD_CONTROL		0x178
#define  XENON_EMMC_PHY_PAD_CONTROL_FC_DQ_RECEN		(1 << 24)
#define  XENON_EMMC_PHY_PAD_CONTROL_FC_CMD_RECEN	(1 << 25)
#define  XENON_EMMC_PHY_PAD_CONTROL_FC_QSP_RECEN	(1 << 26)
#define  XENON_EMMC_PHY_PAD_CONTROL_FC_QSN_RECEN	(1 << 27)
#define  XENON_EMMC_PHY_PAD_CONTROL_FC_ALL_CMOS_RECVR	0xf000
#define XENON_EMMC_PHY_PAD_CONTROL1		0x17c
#define  XENON_EMMC_PHY_PAD_CONTROL1_FC_CMD_PD		(1 << 8)
#define  XENON_EMMC_PHY_PAD_CONTROL1_FC_QSP_PD		(1 << 9)
#define  XENON_EMMC_PHY_PAD_CONTROL1_FC_CMD_PU		(1 << 24)
#define  XENON_EMMC_PHY_PAD_CONTROL1_FC_QSP_PU		(1 << 25)
#define  XENON_EMMC_PHY_PAD_CONTROL1_FC_DQ_PD		0xff
#define  XENON_EMMC_PHY_PAD_CONTROL1_FC_DQ_PU		(0xff << 16)
#define XENON_EMMC_PHY_PAD_CONTROL2		0x180
#define  XENON_EMMC_PHY_PAD_CONTROL2_ZPR_SHIFT		0
#define  XENON_EMMC_PHY_PAD_CONTROL2_ZPR_MASK		0x1f
#define  XENON_EMMC_PHY_PAD_CONTROL2_ZNR_SHIFT		8
#define  XENON_EMMC_PHY_PAD_CONTROL2_ZNR_MASK		0x1f

#define ARMADA_3700_SOC_PAD_CTL			0
#define  ARMADA_3700_SOC_PAD_CTL_3_3V			0
#define  ARMADA_3700_SOC_PAD_CTL_1_8V			1

struct sdhc_fdt_softc {
	struct sdhc_softc 	sc;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_space_handle_t	sc_pad_ioh;
	bus_size_t		sc_size;
	void			*sc_ih;
	int			sc_node;
	uint32_t		sc_gpio[3];
	uint32_t		sc_vmmc;
	uint32_t		sc_vqmmc;

	/* Marvell Xenon */
	int			sc_sdhc_id;
	int			sc_slow_mode;
	uint32_t		sc_znr;
	uint32_t		sc_zpr;

	struct sdhc_host 	*sc_host;
	struct clock_device	sc_cd;
};

int	sdhc_fdt_match(struct device *, void *, void *);
void	sdhc_fdt_attach(struct device *, struct device *, void *);

const struct cfattach sdhc_fdt_ca = {
	sizeof(struct sdhc_fdt_softc), sdhc_fdt_match, sdhc_fdt_attach,
	NULL, sdhc_activate
};

int	sdhc_fdt_card_detect(struct sdhc_softc *);
int	sdhc_fdt_signal_voltage(struct sdhc_softc *, int);
uint32_t sdhc_fdt_get_frequency(void *, uint32_t *);

void	sdhc_fdt_xenon_bus_clock_post(struct sdhc_softc *, int, int);

int
sdhc_fdt_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "arasan,sdhci-5.1") ||
	    OF_is_compatible(faa->fa_node, "arasan,sdhci-8.9a") ||
	    OF_is_compatible(faa->fa_node, "brcm,bcm2711-emmc2") ||
	    OF_is_compatible(faa->fa_node, "brcm,bcm2712-sdhci") ||
	    OF_is_compatible(faa->fa_node, "brcm,bcm2835-sdhci") ||
	    OF_is_compatible(faa->fa_node, "marvell,armada-3700-sdhci") ||
	    OF_is_compatible(faa->fa_node, "marvell,armada-ap806-sdhci") ||
	    OF_is_compatible(faa->fa_node, "marvell,armada-cp110-sdhci"));
}

void
sdhc_fdt_attach(struct device *parent, struct device *self, void *aux)
{
	struct sdhc_fdt_softc *sc = (struct sdhc_fdt_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct regmap *rm = NULL;
	uint64_t capmask, capset;
	uint32_t reg, phandle, freq;
	char pad_type[16] = { 0 };

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	sc->sc_size = faa->fa_reg[0].size;
	sc->sc_node = faa->fa_node;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	pinctrl_byname(faa->fa_node, "default");

	clock_set_assigned(faa->fa_node);
	clock_enable_all(faa->fa_node);
	reset_deassert_all(faa->fa_node);

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_BIO,
	    sdhc_intr, sc, sc->sc.sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	capmask = OF_getpropint64(sc->sc_node, "sdhci-caps-mask", 0);
	capset = OF_getpropint64(sc->sc_node, "sdhci-caps", 0);

	if (OF_getproplen(faa->fa_node, "cd-gpios") > 0 ||
	    OF_getproplen(faa->fa_node, "non-removable") == 0) {
		OF_getpropintarray(faa->fa_node, "cd-gpios", sc->sc_gpio,
		    sizeof(sc->sc_gpio));
		gpio_controller_config_pin(sc->sc_gpio, GPIO_CONFIG_INPUT);
		sc->sc.sc_card_detect = sdhc_fdt_card_detect;
	}

	sc->sc_vmmc = OF_getpropint(sc->sc_node, "vmmc-supply", 0);
	sc->sc_vqmmc = OF_getpropint(sc->sc_node, "vqmmc-supply", 0);

	printf("\n");

	sc->sc.sc_host = &sc->sc_host;
	sc->sc.sc_dmat = faa->fa_dmat;

	/*
	 * Arasan controller always uses 1.8V and doesn't like an
	 * explicit switch.
	 */
	if (OF_is_compatible(faa->fa_node, "arasan,sdhci-5.1"))
		sc->sc.sc_signal_voltage = sdhc_fdt_signal_voltage;

	/*
	 * Rockchip RK3399 PHY doesn't like being powered down at low
	 * clock speeds and needs to be powered up explicitly.
	 */
	if (OF_is_compatible(faa->fa_node, "rockchip,rk3399-sdhci-5.1")) {
		/*
		 * The eMMC core's clock multiplier is of no use, so we just
		 * clear it.  Also make sure to set the base clock frequency.
		 */
		freq = clock_get_frequency(faa->fa_node, "clk_xin");
		freq /= 1000 * 1000; /* in MHz */
		phandle = OF_getpropint(faa->fa_node,
		    "arasan,soc-ctl-syscon", 0);
		if (phandle)
			rm = regmap_byphandle(phandle);
		if (rm) {
			regmap_write_4(rm, GRF_EMMCCORE_CON11,
			    GRF_EMMCCORE_CON11_CLOCKMULT_CLR |
			    GRF_EMMCCORE_CON11_CLOCKMULT_VAL(0));
			regmap_write_4(rm, GRF_EMMCCORE_CON0_BASECLOCK,
			    GRF_EMMCCORE_CON0_BASECLOCK_CLR |
			    GRF_EMMCCORE_CON0_BASECLOCK_VAL(freq));
		}
		/* Provide base clock frequency for the PHY driver. */
		sc->sc_cd.cd_node = faa->fa_node;
		sc->sc_cd.cd_cookie = sc;
		sc->sc_cd.cd_get_frequency = sdhc_fdt_get_frequency;
		clock_register(&sc->sc_cd);
		/*
		 * Enable the PHY.  The PHY should be powered on/off in
		 * the bus_clock function, but it's good enough to just
		 * enable it here right away and to keep it powered on.
		 */
		phy_enable(faa->fa_node, "phy_arasan");
		sc->sc.sc_flags |= SDHC_F_NOPWR0;

		/* XXX Doesn't work on Rockchip RK3399. */
		capmask |= (uint64_t)SDHC_DDR50_SUPP << 32;
	}

	if (OF_is_compatible(faa->fa_node, "arasan,sdhci-8.9a")) {
		freq = clock_get_frequency(faa->fa_node, "clk_xin");
		sc->sc.sc_clkbase = freq / 1000;
	}

	if (OF_is_compatible(faa->fa_node, "brcm,bcm2711-emmc2"))
		sc->sc.sc_flags |= SDHC_F_NOPWR0;

	if (OF_is_compatible(faa->fa_node, "brcm,bcm2835-sdhci")) {
		capmask = 0xffffffff;
		capset = SDHC_VOLTAGE_SUPP_3_3V | SDHC_HIGH_SPEED_SUPP;
		capset |= SDHC_MAX_BLK_LEN_1024 << SDHC_MAX_BLK_LEN_SHIFT;

		freq = clock_get_frequency(faa->fa_node, NULL);
		sc->sc.sc_clkbase = freq / 1000;

		sc->sc.sc_flags |= SDHC_F_32BIT_ACCESS;
		sc->sc.sc_flags |= SDHC_F_NO_HS_BIT;
	}

	if (OF_is_compatible(faa->fa_node, "marvell,armada-3700-sdhci") ||
	    OF_is_compatible(faa->fa_node, "marvell,armada-ap806-sdhci") ||
	    OF_is_compatible(faa->fa_node, "marvell,armada-cp110-sdhci")) {
		if (OF_is_compatible(faa->fa_node,
		    "marvell,armada-3700-sdhci")) {
			KASSERT(faa->fa_nreg > 1);
			if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
			    faa->fa_reg[1].size, 0, &sc->sc_pad_ioh)) {
				printf("%s: can't map registers\n",
				    sc->sc.sc_dev.dv_xname);
				return;
			}
			OF_getprop(faa->fa_node, "marvell,pad-type",
			    pad_type, sizeof(pad_type));
			if (!strcmp(pad_type, "fixed-1-8v")) {
				bus_space_write_4(sc->sc_iot, sc->sc_pad_ioh,
				    ARMADA_3700_SOC_PAD_CTL,
				    ARMADA_3700_SOC_PAD_CTL_1_8V);
			} else {
				bus_space_write_4(sc->sc_iot, sc->sc_pad_ioh,
				    ARMADA_3700_SOC_PAD_CTL,
				    ARMADA_3700_SOC_PAD_CTL_3_3V);
				regulator_set_voltage(sc->sc_vqmmc, 3300000);
			}
		}

		if (OF_getpropint(faa->fa_node, "bus-width", 1) != 8)
			capmask |= SDHC_8BIT_MODE_SUPP;
		if (OF_getproplen(faa->fa_node, "no-1-8-v") == 0) {
			capmask |= SDHC_VOLTAGE_SUPP_1_8V;
			capmask |= (uint64_t)SDHC_DDR50_SUPP << 32;
		}
		if (OF_getproplen(faa->fa_node,
		    "marvell,xenon-phy-slow-mode") == 0)
			sc->sc_slow_mode = 1;

		sc->sc_znr = OF_getpropint(faa->fa_node,
		    "marvell,xenon-phy-znr", 0xf);
		sc->sc_znr &= XENON_EMMC_PHY_PAD_CONTROL2_ZNR_MASK;
		sc->sc_zpr = OF_getpropint(faa->fa_node,
		    "marvell,xenon-phy-zpr", 0xf);
		sc->sc_zpr &= XENON_EMMC_PHY_PAD_CONTROL2_ZPR_MASK;
		sc->sc_sdhc_id = OF_getpropint(faa->fa_node,
		    "marvell,xenon-sdhc-id", 0);

		reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    XENON_SYS_OP_CTRL);
		reg |= XENON_SYS_OP_CTRL_SLOT_ENABLE(sc->sc_sdhc_id);
		reg &= ~XENON_SYS_OP_CTRL_SDCLK_IDLEOFF_ENABLE(sc->sc_sdhc_id);
		reg &= ~XENON_SYS_OP_CTRL_AUTO_CLKGATE_DISABLE;
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    XENON_SYS_OP_CTRL, reg);
		reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    XENON_SYS_EXT_OP_CTRL);
		reg |= XENON_SYS_EXT_OP_CTRL_PARALLEL_TRAN(sc->sc_sdhc_id);
		reg |= XENON_SYS_EXT_OP_CTRL_MASK_CMD_CONFLICT_ERR;
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    XENON_SYS_EXT_OP_CTRL, reg);

		freq = clock_get_frequency(faa->fa_node, NULL);
		sc->sc.sc_clkbase = freq / 1000;
		sc->sc.sc_bus_clock_post = sdhc_fdt_xenon_bus_clock_post;
	}

	if (sc->sc_vmmc)
		regulator_enable(sc->sc_vmmc);

	sdhc_host_found(&sc->sc, sc->sc_iot, sc->sc_ioh, sc->sc_size, 1,
	    capmask, capset);
	return;

unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_size);
}

int
sdhc_fdt_card_detect(struct sdhc_softc *ssc)
{
	struct sdhc_fdt_softc *sc = (struct sdhc_fdt_softc *)ssc;

	if (OF_getproplen(sc->sc_node, "non-removable") == 0)
		return 1;

	return gpio_controller_get_pin(sc->sc_gpio);
}

int
sdhc_fdt_signal_voltage(struct sdhc_softc *sc, int signal_voltage)
{
	switch (signal_voltage) {
	case SDMMC_SIGNAL_VOLTAGE_180:
		return 0;
	default:
		return EINVAL;
	}
}

uint32_t
sdhc_fdt_get_frequency(void *cookie, uint32_t *cells)
{
	struct sdhc_fdt_softc *sc = cookie;
	return clock_get_frequency(sc->sc_cd.cd_node, "clk_xin");
}

/* Marvell Xenon */
void
sdhc_fdt_xenon_bus_clock_post(struct sdhc_softc *ssc, int freq, int timing)
{
	struct sdhc_fdt_softc *sc = (struct sdhc_fdt_softc *)ssc;
	uint32_t reg;
	int i;

	if (freq == 0)
		return;

	reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_PAD_CONTROL);
	reg |= (XENON_EMMC_PHY_PAD_CONTROL_FC_DQ_RECEN |
		XENON_EMMC_PHY_PAD_CONTROL_FC_CMD_RECEN |
		XENON_EMMC_PHY_PAD_CONTROL_FC_QSP_RECEN |
		XENON_EMMC_PHY_PAD_CONTROL_FC_QSN_RECEN |
		XENON_EMMC_PHY_PAD_CONTROL_FC_ALL_CMOS_RECVR);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_PAD_CONTROL, reg);
	reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_PAD_CONTROL1);
	reg &= ~(XENON_EMMC_PHY_PAD_CONTROL1_FC_CMD_PD |
		XENON_EMMC_PHY_PAD_CONTROL1_FC_DQ_PD);
	reg |= (XENON_EMMC_PHY_PAD_CONTROL1_FC_CMD_PU |
		XENON_EMMC_PHY_PAD_CONTROL1_FC_DQ_PU);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_PAD_CONTROL1, reg);

	if (timing == SDMMC_TIMING_LEGACY)
		goto phy_init;

	/* TODO: check for SMF_IO_MODE and set flag */
	reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_TIMING_ADJUST);
	reg &= ~XENON_EMMC_PHY_TIMING_ADJUST_SDIO_MODE;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_TIMING_ADJUST, reg);

	reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_PAD_CONTROL2);
	reg &= ~(XENON_EMMC_PHY_PAD_CONTROL2_ZPR_MASK <<
	    XENON_EMMC_PHY_PAD_CONTROL2_ZPR_SHIFT |
	    XENON_EMMC_PHY_PAD_CONTROL2_ZNR_MASK <<
	    XENON_EMMC_PHY_PAD_CONTROL2_ZNR_SHIFT);
	reg |= sc->sc_zpr << XENON_EMMC_PHY_PAD_CONTROL2_ZPR_SHIFT |
	     sc->sc_znr << XENON_EMMC_PHY_PAD_CONTROL2_ZNR_SHIFT;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_PAD_CONTROL2, reg);

	reg = bus_space_read_2(sc->sc_iot, sc->sc_ioh, SDHC_CLOCK_CTL);
	reg &= ~SDHC_SDCLK_ENABLE;
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, SDHC_CLOCK_CTL, reg);

	reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_FUNC_CONTROL);
	reg &= ~(XENON_EMMC_PHY_FUNC_CONTROL_DQ_DDR_MODE |
	     XENON_EMMC_PHY_FUNC_CONTROL_CMD_DDR_MODE);
	reg |= XENON_EMMC_PHY_FUNC_CONTROL_DQ_ASYNC_MODE;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_FUNC_CONTROL, reg);

	reg = bus_space_read_2(sc->sc_iot, sc->sc_ioh, SDHC_CLOCK_CTL);
	reg |= SDHC_SDCLK_ENABLE;
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, SDHC_CLOCK_CTL, reg);

	reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    XENON_SLOT_EMMC_CTRL);
	reg &= ~(XENON_SLOT_EMMC_CTRL_ENABLE_DATA_STROBE |
	    XENON_SLOT_EMMC_CTRL_ENABLE_RESP_STROBE);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
	    XENON_SLOT_EMMC_CTRL, reg);

	reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_PAD_CONTROL1);
	reg &= ~(XENON_EMMC_PHY_PAD_CONTROL1_FC_QSP_PD |
		XENON_EMMC_PHY_PAD_CONTROL1_FC_QSP_PU);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_PAD_CONTROL1, reg);

phy_init:
	reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_TIMING_ADJUST);
	reg |= XENON_EMMC_PHY_TIMING_ADJUST_SAMPL_INV_QSP_PHASE_SELECT;
	reg &= ~XENON_EMMC_PHY_TIMING_ADJUST_SLOW_MODE;
	if (timing == SDMMC_TIMING_LEGACY ||
	    timing == SDMMC_TIMING_HIGHSPEED || sc->sc_slow_mode)
		reg |= XENON_EMMC_PHY_TIMING_ADJUST_SLOW_MODE;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_TIMING_ADJUST, reg);

	reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_TIMING_ADJUST);
	reg |= XENON_EMMC_PHY_TIMING_ADJUST_INIT;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh,
	    XENON_EMMC_PHY_TIMING_ADJUST, reg);

	for (i = 1000; i > 0; i--) {
		reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    XENON_EMMC_PHY_TIMING_ADJUST);
		if (!(reg & XENON_EMMC_PHY_TIMING_ADJUST_INIT))
			break;
		delay(10);
	}
	if (i == 0)
		printf("%s: phy initialization timeout\n",
		    sc->sc.sc_dev.dv_xname);

	if (freq > SDMMC_SDCLK_400KHZ) {
		reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    XENON_SYS_OP_CTRL);
		reg |= XENON_SYS_OP_CTRL_SDCLK_IDLEOFF_ENABLE(sc->sc_sdhc_id);
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    XENON_SYS_OP_CTRL, reg);
	}
}
