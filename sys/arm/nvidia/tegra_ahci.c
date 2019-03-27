/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * AHCI driver for Tegra SoCs.
 */
#include <sys/param.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/ahci/ahci.h>
#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
#include <dev/extres/phy/phy.h>
#include <dev/extres/regulator/regulator.h>
#include <dev/fdt/fdt_pinctrl.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/nvidia/tegra_efuse.h>
#include <arm/nvidia/tegra_pmc.h>

#define	AHCI_WR4(_sc, _r, _v)	bus_write_4((_sc)->ctlr.r_mem, (_r), (_v))
#define	AHCI_RD4(_sc, _r)	bus_read_4((_sc)->ctlr.r_mem, (_r))
#define	SATA_WR4(_sc, _r, _v)	bus_write_4((_sc)->sata_mem, (_r), (_v))
#define	SATA_RD4(_sc, _r)	bus_read_4((_sc)->sata_mem, (_r))

static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-ahci", 	1},
	{NULL,			0}
};

struct tegra_ahci_sc {
	struct ahci_controller	ctlr;	/* Must be first */
	device_t		dev;
	struct resource		*sata_mem;
	clk_t			clk_sata;
	clk_t			clk_sata_oob;
	clk_t			clk_pll_e;
	clk_t			clk_cml;
	hwreset_t		hwreset_sata;
	hwreset_t		hwreset_sata_oob;
	hwreset_t		hwreset_sata_cold;
	regulator_t		supply_hvdd;
	regulator_t		supply_vddio;
	regulator_t		supply_avdd;
	regulator_t		supply_target_5v;
	regulator_t		supply_target_12v;
	phy_t			phy;
};

struct sata_pad_calibration {
	uint32_t gen1_tx_amp;
	uint32_t gen1_tx_peak;
	uint32_t gen2_tx_amp;
	uint32_t gen2_tx_peak;
};

static const struct sata_pad_calibration tegra124_pad_calibration[] = {
	{0x18, 0x04, 0x18, 0x0a},
	{0x0e, 0x04, 0x14, 0x0a},
	{0x0e, 0x07, 0x1a, 0x0e},
	{0x14, 0x0e, 0x1a, 0x0e},
};

#define	SATA_CONFIGURATION			0x180
#define	 SATA_CONFIGURATION_EN_FPCI			(1 << 0)

#define	SATA_FPCI_BAR5				0x94
#define	 SATA_FPCI_BAR5_START_SHIFT			4

#define	SATA_INTR_MASK				0x188
#define	SATA_INTR_MASK_IP_INT_MASK			(1 << 16)

#define	SCFG_OFFSET				0x1000

#define	T_SATA0_CFG_1				0x04
#define	 T_SATA0_CFG_1_IO_SPACE				(1 << 0)
#define	 T_SATA0_CFG_1_MEMORY_SPACE			(1 << 1)
#define	 T_SATA0_CFG_1_BUS_MASTER			(1 << 2)
#define	 T_SATA0_CFG_1_SERR				(1 << 8)

#define	T_SATA0_CFG_9				0x24
#define	 T_SATA0_CFG_9_BASE_ADDRESS_SHIFT		13

#define	T_SATA0_AHCI_HBA_CAP_BKDR		0x300
#define	T_SATA0_BKDOOR_CC			0x4a4
#define	T_SATA0_CFG_SATA			0x54c
#define	 T_SATA0_CFG_SATA_BACKDOOR_PROG_IF_EN		(1 << 12)

#define	T_SATA0_CFG_MISC			0x550
#define	T_SATA0_INDEX				0x680

#define	T_SATA0_CHX_PHY_CTRL1_GEN1		0x690
#define	 T_SATA0_CHX_PHY_CTRL1_GEN1_TX_PEAK_MASK	0xff
#define	 T_SATA0_CHX_PHY_CTRL1_GEN1_TX_PEAK_SHIFT	8
#define	 T_SATA0_CHX_PHY_CTRL1_GEN1_TX_AMP_MASK		0xff
#define	 T_SATA0_CHX_PHY_CTRL1_GEN1_TX_AMP_SHIFT	0


#define	T_SATA0_CHX_PHY_CTRL1_GEN2		0x694
#define	 T_SATA0_CHX_PHY_CTRL1_GEN2_TX_PEAK_MASK	0xff
#define	 T_SATA0_CHX_PHY_CTRL1_GEN2_TX_PEAK_SHIFT	12
#define	 T_SATA0_CHX_PHY_CTRL1_GEN2_TX_AMP_MASK		0xff
#define	 T_SATA0_CHX_PHY_CTRL1_GEN2_TX_AMP_SHIFT	0

#define	T_SATA0_CHX_PHY_CTRL2			0x69c
#define	 T_SATA0_CHX_PHY_CTRL2_CDR_CNTL_GEN1		0x23

#define	T_SATA0_CHX_PHY_CTRL11			0x6d0
#define	 T_SATA0_CHX_PHY_CTRL11_GEN2_RX_EQ		(0x2800 << 16)

#define	FUSE_SATA_CALIB				0x124
#define	FUSE_SATA_CALIB_MASK			0x3


#define	SATA_AUX_MISC_CNTL			0x1108
#define	SATA_AUX_PAD_PLL_CTRL_0			0x1120
#define	SATA_AUX_PAD_PLL_CTRL_1			0x1124
#define	SATA_AUX_PAD_PLL_CTRL_2			0x1128
#define	SATA_AUX_PAD_PLL_CTRL_3			0x112c

#define	T_AHCI_HBA_CCC_PORTS			0x0018
#define	T_AHCI_HBA_CAP_BKDR			0x00A0
#define	 T_AHCI_HBA_CAP_BKDR_S64A			(1 << 31)
#define	 T_AHCI_HBA_CAP_BKDR_SNCQ			(1 << 30)
#define	 T_AHCI_HBA_CAP_BKDR_SSNTF			(1 << 29)
#define	 T_AHCI_HBA_CAP_BKDR_SMPS			(1 << 28)
#define	 T_AHCI_HBA_CAP_BKDR_SUPP_STG_SPUP		(1 << 27)
#define	 T_AHCI_HBA_CAP_BKDR_SALP			(1 << 26)
#define	 T_AHCI_HBA_CAP_BKDR_SAL			(1 << 25)
#define	 T_AHCI_HBA_CAP_BKDR_SUPP_CLO			(1 << 24)
#define	 T_AHCI_HBA_CAP_BKDR_INTF_SPD_SUPP(x)		(((x) & 0xF) << 20)
#define	 T_AHCI_HBA_CAP_BKDR_SUPP_NONZERO_OFFSET	(1 << 19)
#define	 T_AHCI_HBA_CAP_BKDR_SUPP_AHCI_ONLY		(1 << 18)
#define	 T_AHCI_HBA_CAP_BKDR_SUPP_PM			(1 << 17)
#define	 T_AHCI_HBA_CAP_BKDR_FIS_SWITCHING		(1 << 16)
#define	 T_AHCI_HBA_CAP_BKDR_PIO_MULT_DRQ_BLK		(1 << 15)
#define	 T_AHCI_HBA_CAP_BKDR_SLUMBER_ST_CAP		(1 << 14)
#define	 T_AHCI_HBA_CAP_BKDR_PARTIAL_ST_CAP		(1 << 13)
#define	 T_AHCI_HBA_CAP_BKDR_NUM_CMD_SLOTS(x)		(((x) & 0x1F) <<  8)
#define	 T_AHCI_HBA_CAP_BKDR_CMD_CMPL_COALESING		(1 <<  7)
#define	 T_AHCI_HBA_CAP_BKDR_ENCL_MGMT_SUPP		(1 <<  6)
#define	 T_AHCI_HBA_CAP_BKDR_EXT_SATA			(1 <<  5)
#define	 T_AHCI_HBA_CAP_BKDR_NUM_PORTS(x)		(((x) & 0xF) <<  0)

#define	T_AHCI_PORT_BKDR			0x0170

#define	 T_AHCI_PORT_BKDR_PXDEVSLP_DETO_OVERRIDE_VAL(x)	(((x) & 0xFF) << 24)
#define	 T_AHCI_PORT_BKDR_PXDEVSLP_MDAT_OVERRIDE_VAL(x)	(((x) & 0x1F) << 16)
#define	 T_AHCI_PORT_BKDR_PXDEVSLP_DETO_OVERRIDE	(1 << 15)
#define	 T_AHCI_PORT_BKDR_PXDEVSLP_MDAT_OVERRIDE	(1 << 14)
#define	 T_AHCI_PORT_BKDR_PXDEVSLP_DM(x)		(((x) & 0xF) << 10)
#define	 T_AHCI_PORT_BKDR_PORT_UNCONNECTED		(1 <<  9)
#define	 T_AHCI_PORT_BKDR_CLK_CLAMP_CTRL_CLAMP_THIS_CH	(1 <<  8)
#define	 T_AHCI_PORT_BKDR_CLK_CLAMP_CTRL_TXRXCLK_UNCLAMP (1 <<  7)
#define	 T_AHCI_PORT_BKDR_CLK_CLAMP_CTRL_TXRXCLK_CLAMP	(1 <<  6)
#define	 T_AHCI_PORT_BKDR_CLK_CLAMP_CTRL_DEVCLK_UNCLAMP	(1 <<  5)
#define	 T_AHCI_PORT_BKDR_CLK_CLAMP_CTRL_DEVCLK_CLAMP	(1 <<  4)
#define	 T_AHCI_PORT_BKDR_HOTPLUG_CAP			(1 <<  3)
#define	 T_AHCI_PORT_BKDR_MECH_SWITCH			(1 <<  2)
#define	 T_AHCI_PORT_BKDR_COLD_PRSN_DET			(1 <<  1)
#define	 T_AHCI_PORT_BKDR_EXT_SATA_SUPP			(1 <<  0)

static int
get_fdt_resources(struct tegra_ahci_sc *sc, phandle_t node)
{
	int rv;


	rv = regulator_get_by_ofw_property(sc->dev, 0, "hvdd-supply",
	    &sc->supply_hvdd );
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'hvdd' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "vddio-supply",
	    &sc->supply_vddio);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'vddio' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "avdd-supply",
	    &sc->supply_avdd);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'avdd' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "target-5v-supply",
	    &sc->supply_target_5v);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'target-5v' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "target-12v-supply",
	    &sc->supply_target_12v);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'target-12v' regulator\n");
		return (ENXIO);
	}

	rv = hwreset_get_by_ofw_name(sc->dev, 0, "sata", &sc->hwreset_sata );
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'sata' reset\n");
		return (ENXIO);
	}
	rv = hwreset_get_by_ofw_name(sc->dev, 0, "sata-oob",
	    &sc->hwreset_sata_oob);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'sata oob' reset\n");
		return (ENXIO);
	}
	rv = hwreset_get_by_ofw_name(sc->dev, 0, "sata-cold",
	    &sc->hwreset_sata_cold);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'sata cold' reset\n");
		return (ENXIO);
	}

	rv = phy_get_by_ofw_name(sc->dev, 0, "sata-0", &sc->phy);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'sata' phy\n");
		return (ENXIO);
	}

	rv = clk_get_by_ofw_name(sc->dev, 0, "sata", &sc->clk_sata);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'sata' clock\n");
		return (ENXIO);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "sata-oob", &sc->clk_sata_oob);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'sata oob' clock\n");
		return (ENXIO);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "cml1", &sc->clk_cml);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'cml1' clock\n");
		return (ENXIO);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "pll_e", &sc->clk_pll_e);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'pll_e' clock\n");
		return (ENXIO);
	}
	return (0);
}

static int
enable_fdt_resources(struct tegra_ahci_sc *sc)
{
	int rv;

	rv = regulator_enable(sc->supply_hvdd);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable  'hvdd' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_vddio);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable  'vddio' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_avdd);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable  'avdd' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_target_5v);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable  'target-5v' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_target_12v);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable  'sc->target-12v' regulator\n");
		return (rv);
	}

	/* Stop clocks */
	clk_stop(sc->clk_sata);
	clk_stop(sc->clk_sata_oob);
	tegra_powergate_power_off(TEGRA_POWERGATE_SAX);

	rv = hwreset_assert(sc->hwreset_sata);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot assert 'sata' reset\n");
		return (rv);
	}
	rv = hwreset_assert(sc->hwreset_sata_oob);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot assert 'sata oob' reset\n");
		return (rv);
	}

	rv = hwreset_assert(sc->hwreset_sata_cold);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot assert 'sata cold' reset\n");
		return (rv);
	}
	rv = tegra_powergate_sequence_power_up(TEGRA_POWERGATE_SAX,
	    sc->clk_sata, sc->hwreset_sata);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable 'SAX' powergate\n");
		return (rv);
	}

	rv = clk_enable(sc->clk_sata_oob);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable 'sata oob' clock\n");
		return (rv);
	}
	rv = clk_enable(sc->clk_cml);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable 'cml' clock\n");
		return (rv);
	}
	rv = clk_enable(sc->clk_pll_e);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable 'pll e' clock\n");
		return (rv);
	}

	rv = hwreset_deassert(sc->hwreset_sata_cold);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot unreset 'sata cold' reset\n");
		return (rv);
	}
	rv = hwreset_deassert(sc->hwreset_sata_oob);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot unreset 'sata oob' reset\n");
		return (rv);
	}

	rv = phy_enable(sc->phy);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable SATA phy\n");
		return (rv);
	}

	return (0);
}

static int
tegra_ahci_ctrl_init(struct tegra_ahci_sc *sc)
{
	uint32_t val;
	const struct sata_pad_calibration *calib;

	val = SATA_RD4(sc, SATA_CONFIGURATION);
	val |= SATA_CONFIGURATION_EN_FPCI;
	SATA_WR4(sc, SATA_CONFIGURATION, val);


	/* Pad calibration. */
	val = tegra_fuse_read_4(FUSE_SATA_CALIB);
	calib = tegra124_pad_calibration + (val & FUSE_SATA_CALIB_MASK);
	SATA_WR4(sc, SCFG_OFFSET + T_SATA0_INDEX, 1);

	val = SATA_RD4(sc, SCFG_OFFSET + T_SATA0_CHX_PHY_CTRL1_GEN1);
	val &= ~(T_SATA0_CHX_PHY_CTRL1_GEN1_TX_AMP_MASK <<
	    T_SATA0_CHX_PHY_CTRL1_GEN1_TX_AMP_SHIFT);
	val &= ~(T_SATA0_CHX_PHY_CTRL1_GEN1_TX_PEAK_MASK <<
	    T_SATA0_CHX_PHY_CTRL1_GEN1_TX_PEAK_SHIFT);
	val |= calib->gen1_tx_amp << T_SATA0_CHX_PHY_CTRL1_GEN1_TX_AMP_SHIFT;
	val |= calib->gen1_tx_peak << T_SATA0_CHX_PHY_CTRL1_GEN1_TX_PEAK_SHIFT;
	SATA_WR4(sc, SCFG_OFFSET + T_SATA0_CHX_PHY_CTRL1_GEN1, val);

	val = SATA_RD4(sc, SCFG_OFFSET + T_SATA0_CHX_PHY_CTRL1_GEN2);
	val &= ~(T_SATA0_CHX_PHY_CTRL1_GEN2_TX_AMP_MASK <<
	    T_SATA0_CHX_PHY_CTRL1_GEN2_TX_AMP_SHIFT);
	val &= ~(T_SATA0_CHX_PHY_CTRL1_GEN2_TX_PEAK_MASK <<
	    T_SATA0_CHX_PHY_CTRL1_GEN2_TX_PEAK_SHIFT);
	val |= calib->gen2_tx_amp << T_SATA0_CHX_PHY_CTRL1_GEN2_TX_AMP_SHIFT;
	val |= calib->gen2_tx_peak << T_SATA0_CHX_PHY_CTRL1_GEN2_TX_PEAK_SHIFT;
	SATA_WR4(sc, SCFG_OFFSET + T_SATA0_CHX_PHY_CTRL1_GEN2, val);

	SATA_WR4(sc, SCFG_OFFSET + T_SATA0_CHX_PHY_CTRL11,
	    T_SATA0_CHX_PHY_CTRL11_GEN2_RX_EQ);

	SATA_WR4(sc, SCFG_OFFSET + T_SATA0_CHX_PHY_CTRL2,
	    T_SATA0_CHX_PHY_CTRL2_CDR_CNTL_GEN1);

	SATA_WR4(sc, SCFG_OFFSET + T_SATA0_INDEX, 0);

	/* Set device ID. */
	val = SATA_RD4(sc, SCFG_OFFSET + T_SATA0_CFG_SATA);
	val |= T_SATA0_CFG_SATA_BACKDOOR_PROG_IF_EN;
	SATA_WR4(sc, SCFG_OFFSET + T_SATA0_CFG_SATA, val);

	SATA_WR4(sc, SCFG_OFFSET + T_SATA0_BKDOOR_CC, 0x01060100);

	val = SATA_RD4(sc, SCFG_OFFSET + T_SATA0_CFG_SATA);
	val &= ~T_SATA0_CFG_SATA_BACKDOOR_PROG_IF_EN;
	SATA_WR4(sc, SCFG_OFFSET + T_SATA0_CFG_SATA, val);

	/* Enable IO & memory access, bus master mode */
	val = SATA_RD4(sc, SCFG_OFFSET + T_SATA0_CFG_1);
	val |= T_SATA0_CFG_1_IO_SPACE;
	val |= T_SATA0_CFG_1_MEMORY_SPACE;
	val |= T_SATA0_CFG_1_BUS_MASTER;
	val |= T_SATA0_CFG_1_SERR;
	SATA_WR4(sc, SCFG_OFFSET + T_SATA0_CFG_1, val);

	/* SATA MMIO. */
	SATA_WR4(sc, SATA_FPCI_BAR5, 0x10000 << SATA_FPCI_BAR5_START_SHIFT);
	/* AHCI bar */
	SATA_WR4(sc,  SCFG_OFFSET + T_SATA0_CFG_9,
	    0x08000 << T_SATA0_CFG_9_BASE_ADDRESS_SHIFT);

	/* Unmask  interrupts. */
	val = SATA_RD4(sc, SATA_INTR_MASK);
	val |= SATA_INTR_MASK_IP_INT_MASK;
	SATA_WR4(sc, SATA_INTR_MASK, val);

	return (0);
}

static int
tegra_ahci_ctlr_reset(device_t dev)
{
	struct tegra_ahci_sc *sc;
	int rv;
	uint32_t reg;

	sc = device_get_softc(dev);
	rv = ahci_ctlr_reset(dev);
	if (rv != 0)
		return (0);
	AHCI_WR4(sc, T_AHCI_HBA_CCC_PORTS, 1);

	/* Overwrite AHCI capabilites. */
	reg  = AHCI_RD4(sc, T_AHCI_HBA_CAP_BKDR);
	reg &= ~T_AHCI_HBA_CAP_BKDR_NUM_PORTS(~0);
	reg |= T_AHCI_HBA_CAP_BKDR_NUM_PORTS(0);
	reg |= T_AHCI_HBA_CAP_BKDR_EXT_SATA;
	reg |= T_AHCI_HBA_CAP_BKDR_ENCL_MGMT_SUPP;
	reg |= T_AHCI_HBA_CAP_BKDR_CMD_CMPL_COALESING;
	reg |= T_AHCI_HBA_CAP_BKDR_FIS_SWITCHING;
	reg |= T_AHCI_HBA_CAP_BKDR_SUPP_PM;
	reg |= T_AHCI_HBA_CAP_BKDR_SUPP_CLO;
	reg |= T_AHCI_HBA_CAP_BKDR_SUPP_STG_SPUP;
	AHCI_WR4(sc, T_AHCI_HBA_CAP_BKDR, reg);

	/* Overwrite AHCI portcapabilites. */
	reg  = AHCI_RD4(sc, T_AHCI_PORT_BKDR);
	reg |= T_AHCI_PORT_BKDR_COLD_PRSN_DET;
	reg |= T_AHCI_PORT_BKDR_HOTPLUG_CAP;
	reg |= T_AHCI_PORT_BKDR_EXT_SATA_SUPP;
	AHCI_WR4(sc, T_AHCI_PORT_BKDR, reg);

	return (0);
}

static int
tegra_ahci_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc_copy(dev, "AHCI SATA controller");
	return (BUS_PROBE_DEFAULT);
}

static int
tegra_ahci_attach(device_t dev)
{
	struct tegra_ahci_sc *sc;
	struct ahci_controller *ctlr;
	phandle_t node;
	int rv, rid;

	sc = device_get_softc(dev);
	sc->dev = dev;
	ctlr = &sc->ctlr;
	node = ofw_bus_get_node(dev);

	ctlr->r_rid = 0;
	ctlr->r_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &ctlr->r_rid, RF_ACTIVE);
	if (ctlr->r_mem == NULL)
		return (ENXIO);

	rid = 1;
	sc->sata_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE);
	if (sc->sata_mem == NULL) {
		rv = ENXIO;
		goto fail;
	}
	rv = get_fdt_resources(sc, node);
	if (rv != 0) {
		device_printf(sc->dev, "Failed to allocate FDT resource(s)\n");
		goto fail;
	}

	rv = enable_fdt_resources(sc);
	if (rv != 0) {
		device_printf(sc->dev, "Failed to enable FDT resource(s)\n");
		goto fail;
	}
	rv = tegra_ahci_ctrl_init(sc);
	if (rv != 0) {
		device_printf(sc->dev, "Failed to initialize controller)\n");
		goto fail;
	}

	/* Setup controller defaults. */
	ctlr->msi = 0;
	ctlr->numirqs = 1;
	ctlr->ccc = 0;

	/* Reset controller. */
	rv = tegra_ahci_ctlr_reset(dev);
	if (rv != 0)
		goto fail;
	rv = ahci_attach(dev);
	return (rv);

fail:
	/* XXX FDT  stuff */
	if (sc->sata_mem != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 1, sc->sata_mem);
	if (ctlr->r_mem != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, ctlr->r_rid,
		    ctlr->r_mem);
	return (rv);
}

static int
tegra_ahci_detach(device_t dev)
{

	ahci_detach(dev);
	return (0);
}

static int
tegra_ahci_suspend(device_t dev)
{
	struct tegra_ahci_sc *sc = device_get_softc(dev);

	bus_generic_suspend(dev);
	/* Disable interupts, so the state change(s) doesn't trigger. */
	ATA_OUTL(sc->ctlr.r_mem, AHCI_GHC,
	     ATA_INL(sc->ctlr.r_mem, AHCI_GHC) & (~AHCI_GHC_IE));
	return (0);
}

static int
tegra_ahci_resume(device_t dev)
{
	int res;

	if ((res = tegra_ahci_ctlr_reset(dev)) != 0)
		return (res);
	ahci_ctlr_setup(dev);
	return (bus_generic_resume(dev));
}

static device_method_t tegra_ahci_methods[] = {
	DEVMETHOD(device_probe,		tegra_ahci_probe),
	DEVMETHOD(device_attach,	tegra_ahci_attach),
	DEVMETHOD(device_detach,	tegra_ahci_detach),
	DEVMETHOD(device_suspend,	tegra_ahci_suspend),
	DEVMETHOD(device_resume,	tegra_ahci_resume),
	DEVMETHOD(bus_print_child,	ahci_print_child),
	DEVMETHOD(bus_alloc_resource,	ahci_alloc_resource),
	DEVMETHOD(bus_release_resource,	ahci_release_resource),
	DEVMETHOD(bus_setup_intr,	ahci_setup_intr),
	DEVMETHOD(bus_teardown_intr,	ahci_teardown_intr),
	DEVMETHOD(bus_child_location_str, ahci_child_location_str),
	DEVMETHOD(bus_get_dma_tag,	ahci_get_dma_tag),

	DEVMETHOD_END
};

static DEFINE_CLASS_0(ahci, tegra_ahci_driver, tegra_ahci_methods,
    sizeof(struct tegra_ahci_sc));
DRIVER_MODULE(tegra_ahci, simplebus, tegra_ahci_driver, ahci_devclass,
    NULL, NULL);
