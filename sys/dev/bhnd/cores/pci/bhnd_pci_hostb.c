/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Landon Fuller
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Broadcom BHND PCI/PCIe-Gen1 PCI-Host Bridge.
 * 
 * This driver handles all interactions with PCI bridge cores operating in
 * endpoint mode.
 * 
 * Host-level PCI operations are handled at the bhndb bridge level by the
 * bhndb_pci driver.
 */

#include <sys/param.h>
#include <sys/kernel.h>

#include <sys/malloc.h>

#include <sys/bus.h>
#include <sys/module.h>

#include <sys/systm.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/bhnd/bhnd.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/bhnd/cores/chipc/chipc.h>
#include <dev/bhnd/cores/chipc/chipcreg.h>

#include "bhnd_pcireg.h"
#include "bhnd_pci_hostbvar.h"

static const struct bhnd_device_quirk bhnd_pci_quirks[];
static const struct bhnd_device_quirk bhnd_pcie_quirks[];

/* Device driver work-around variations */
typedef enum {
	BHND_PCI_WAR_ATTACH,	/**< apply attach workarounds */
	BHND_PCI_WAR_RESUME,	/**< apply resume workarounds */
	BHND_PCI_WAR_SUSPEND,	/**< apply suspend workarounds */
	BHND_PCI_WAR_DETACH	/**< apply detach workarounds */
} bhnd_pci_war_state;

static int	bhnd_pci_wars_early_once(struct bhnd_pcihb_softc *sc);
static int	bhnd_pci_wars_hwup(struct bhnd_pcihb_softc *sc,
		    bhnd_pci_war_state state);
static int	bhnd_pci_wars_hwdown(struct bhnd_pcihb_softc *sc,
		    bhnd_pci_war_state state);

/*
 * device/quirk tables
 */

#define	BHND_PCI_DEV(_core, _quirks)		\
	BHND_DEVICE(BCM, _core, NULL, _quirks, BHND_DF_HOSTB)

static const struct bhnd_device bhnd_pci_devs[] = {
	BHND_PCI_DEV(PCI,	bhnd_pci_quirks),
	BHND_PCI_DEV(PCIE,	bhnd_pcie_quirks),
	BHND_DEVICE_END
};

static const struct bhnd_device_quirk bhnd_pci_quirks[] = {
	/* core revision quirks */
	BHND_CORE_QUIRK	(HWREV_ANY,	BHND_PCI_QUIRK_SBTOPCI2_PREF_BURST),
	BHND_CORE_QUIRK	(HWREV_GTE(11),	BHND_PCI_QUIRK_SBTOPCI2_READMULTI |
					BHND_PCI_QUIRK_CLKRUN_DSBL),

	/* BCM4321CB2 boards that require 960ns latency timer override */
	BHND_BOARD_QUIRK(BCM4321CB2,	BHND_PCI_QUIRK_960NS_LATTIM_OVR),
	BHND_BOARD_QUIRK(BCM4321CB2_AG,	BHND_PCI_QUIRK_960NS_LATTIM_OVR),

	BHND_DEVICE_QUIRK_END
};

static const struct bhnd_device_quirk bhnd_pcie_quirks[] = {
	/* core revision quirks */
	BHND_CORE_QUIRK	(HWREV_EQ (0),	BHND_PCIE_QUIRK_SDR9_L0s_HANG),
	BHND_CORE_QUIRK	(HWREV_RANGE(0,1),
	    BHND_PCIE_QUIRK_UR_STATUS_FIX),

	BHND_CORE_QUIRK	(HWREV_EQ (1),	BHND_PCIE_QUIRK_PCIPM_REQEN),

	BHND_CORE_QUIRK	(HWREV_RANGE(3,5),
	    BHND_PCIE_QUIRK_ASPM_OVR | BHND_PCIE_QUIRK_SDR9_POLARITY |
	    BHND_PCIE_QUIRK_SDR9_NO_FREQRETRY),

	BHND_CORE_QUIRK	(HWREV_LTE(6),	BHND_PCIE_QUIRK_L1_IDLE_THRESH),
	BHND_CORE_QUIRK	(HWREV_GTE(6),	BHND_PCIE_QUIRK_SPROM_L23_PCI_RESET),
	BHND_CORE_QUIRK	(HWREV_EQ (7),	BHND_PCIE_QUIRK_SERDES_NOPLLDOWN),
	BHND_CORE_QUIRK	(HWREV_GTE(8),	BHND_PCIE_QUIRK_L1_TIMER_PERF),

	BHND_CORE_QUIRK	(HWREV_LTE(17),	BHND_PCIE_QUIRK_MAX_MRRS_128),

	/* Apple boards on which BHND_BFL2_PCIEWAR_OVR should be assumed
	 * to be set. */
	{{ BHND_MATCH_BOARD_VENDOR	(PCI_VENDOR_APPLE),
	   BHND_MATCH_BOARD_REV		(HWREV_LTE(0x71)),
	   BHND_MATCH_SROMREV		(EQ(4)) },
		BHND_PCIE_QUIRK_BFL2_PCIEWAR_EN },

	/* Apple BCM4322 boards that require 700mV SerDes TX drive strength. */
	{{ BHND_MATCH_CHIP_ID(BCM4322),
	   BHND_MATCH_BOARD(PCI_VENDOR_APPLE, BCM94322X9), },
		BHND_PCIE_QUIRK_SERDES_TXDRV_700MV },

	/* Apple BCM4331 board-specific quirks */
#define	BHND_A4331_QUIRK(_board, ...)	\
	{{ BHND_MATCH_CHIP_ID(BCM4331),		\
	    BHND_MATCH_BOARD(PCI_VENDOR_APPLE, _board) }, __VA_ARGS__ }

	BHND_A4331_QUIRK(BCM94331X19,	BHND_PCIE_QUIRK_SERDES_TXDRV_MAX |
					BHND_PCIE_QUIRK_DEFAULT_MRRS_512),

	BHND_A4331_QUIRK(BCM94331X28,	BHND_PCIE_QUIRK_SERDES_TXDRV_MAX | 
					BHND_PCIE_QUIRK_DEFAULT_MRRS_512),

	BHND_A4331_QUIRK(BCM94331X28B,	BHND_PCIE_QUIRK_DEFAULT_MRRS_512),

	BHND_A4331_QUIRK(BCM94331X29B,	BHND_PCIE_QUIRK_SERDES_TXDRV_MAX | 
					BHND_PCIE_QUIRK_DEFAULT_MRRS_512),

	BHND_A4331_QUIRK(BCM94331X19C,	BHND_PCIE_QUIRK_SERDES_TXDRV_MAX |
					BHND_PCIE_QUIRK_DEFAULT_MRRS_512),
	    
	BHND_A4331_QUIRK(BCM94331X29D,	BHND_PCIE_QUIRK_DEFAULT_MRRS_512),

	BHND_A4331_QUIRK(BCM94331X33,	BHND_PCIE_QUIRK_DEFAULT_MRRS_512),
			 
#undef BHND_A4331_QUIRK

	BHND_DEVICE_QUIRK_END
};


#define	BHND_PCI_SOFTC(_sc)	(&((_sc)->common))

#define	BHND_PCI_READ_2(_sc, _reg)		\
	bhnd_bus_read_2(BHND_PCI_SOFTC(_sc)->mem_res, (_reg))

#define	BHND_PCI_READ_4(_sc, _reg)		\
	bhnd_bus_read_4(BHND_PCI_SOFTC(_sc)->mem_res, (_reg))

#define	BHND_PCI_WRITE_2(_sc, _reg, _val)	\
	bhnd_bus_write_2(BHND_PCI_SOFTC(_sc)->mem_res, (_reg), (_val))
	
#define	BHND_PCI_WRITE_4(_sc, _reg, _val)	\
	bhnd_bus_write_4(BHND_PCI_SOFTC(_sc)->mem_res, (_reg), (_val))

#define	BHND_PCI_PROTO_READ_4(_sc, _reg)	\
	bhnd_pcie_read_proto_reg(BHND_PCI_SOFTC(_sc), (_reg))

#define	BHND_PCI_PROTO_WRITE_4(_sc, _reg, _val)	\
	bhnd_pcie_write_proto_reg(BHND_PCI_SOFTC(_sc), (_reg), (_val))
	
#define	BHND_PCI_MDIO_READ(_sc, _phy, _reg)	\
	bhnd_pcie_mdio_read(BHND_PCI_SOFTC(_sc), (_phy), (_reg))

#define	BHND_PCI_MDIO_WRITE(_sc, _phy, _reg, _val)		\
	bhnd_pcie_mdio_write(BHND_PCI_SOFTC(_sc), (_phy), (_reg), (_val))

#define	BHND_PCI_MDIO_READ_EXT(_sc, _phy, _devaddr, _reg)		\
	bhnd_pcie_mdio_read_ext(BHND_PCI_SOFTC(_sc), (_phy), (_devaddr), (_reg))

#define	BHND_PCI_MDIO_WRITE_EXT(_sc, _phy, _devaddr, _reg, _val)	\
	bhnd_pcie_mdio_write_ext(BHND_PCI_SOFTC(_sc), (_phy),		\
	    (_devaddr), (_reg), (_val))

#define	BPCI_REG_SET(_regv, _attr, _val)	\
	BHND_PCI_REG_SET((_regv), BHND_ ## _attr, (_val))

#define	BPCI_REG_GET(_regv, _attr)	\
	BHND_PCI_REG_GET((_regv), BHND_ ## _attr)

#define	BPCI_CMN_REG_SET(_regv, _attr, _val)			\
	BHND_PCI_CMN_REG_SET(BHND_PCI_SOFTC(_sc)->regfmt, (_regv),	\
	    BHND_ ## _attr, (_val))

#define	BPCI_CMN_REG_GET(_regv, _attr)				\
	BHND_PCI_CMN_REG_GET(BHND_PCI_SOFTC(_sc)->regfmt, (_regv),	\
	    BHND_ ## _attr)

static int
bhnd_pci_hostb_attach(device_t dev)
{
	struct bhnd_pcihb_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->quirks = bhnd_device_quirks(dev, bhnd_pci_devs,
	    sizeof(bhnd_pci_devs[0]));

	/* Find the host PCI bridge device */
	sc->pci_dev = bhnd_find_bridge_root(dev, devclass_find("pci"));
	if (sc->pci_dev == NULL) {
		device_printf(dev, "parent pci bridge device not found\n");
		return (ENXIO);
	}

	/* Common setup */
	if ((error = bhnd_pci_generic_attach(dev)))
		return (error);

	/* Apply early single-shot work-arounds */
	if ((error = bhnd_pci_wars_early_once(sc)))
		goto failed;

	/* Apply attach/resume work-arounds */
	if ((error = bhnd_pci_wars_hwup(sc, BHND_PCI_WAR_ATTACH)))
		goto failed;

	return (0);
	
failed:
	bhnd_pci_generic_detach(dev);
	return (error);
}

static int
bhnd_pci_hostb_detach(device_t dev)
{
	struct bhnd_pcihb_softc *sc;
	int			 error;

	sc = device_get_softc(dev);

	/* Apply suspend/detach work-arounds */
	if ((error = bhnd_pci_wars_hwdown(sc, BHND_PCI_WAR_DETACH)))
		return (error);

	return (bhnd_pci_generic_detach(dev));
}

static int
bhnd_pci_hostb_suspend(device_t dev)
{
	struct bhnd_pcihb_softc *sc;
	int			 error;

	sc = device_get_softc(dev);

	/* Apply suspend/detach work-arounds */
	if ((error = bhnd_pci_wars_hwdown(sc, BHND_PCI_WAR_SUSPEND)))
		return (error);

	return (bhnd_pci_generic_suspend(dev));
}

static int
bhnd_pci_hostb_resume(device_t dev)
{
	struct bhnd_pcihb_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);

	if ((error = bhnd_pci_generic_resume(dev)))
		return (error);

	/* Apply attach/resume work-arounds */
	if ((error = bhnd_pci_wars_hwup(sc, BHND_PCI_WAR_RESUME))) {
		bhnd_pci_generic_detach(dev);
		return (error);
	}

	return (0);
}

/**
 * Apply any hardware work-arounds that must be executed exactly once, early in
 * the attach process.
 * 
 * This must be called after core enumeration and discovery of all applicable
 * quirks, but prior to probe/attach of any cores, parsing of
 * SPROM, etc.
 */
static int
bhnd_pci_wars_early_once(struct bhnd_pcihb_softc *sc)
{
	int error;

	/* Set PCI latency timer */
	if (sc->quirks & BHND_PCI_QUIRK_960NS_LATTIM_OVR) {
		pci_write_config(sc->pci_dev, PCIR_LATTIMER, 0x20 /* 960ns */,
		    1); 
	}

	/* Determine whether ASPM/CLKREQ should be forced on, or forced off. */
	if (sc->quirks & BHND_PCIE_QUIRK_ASPM_OVR) {
		struct bhnd_board_info	board;
		bool			aspm_en;

		/* Fetch board info */
		if ((error = bhnd_read_board_info(sc->dev, &board)))
			return (error);
		
		/* Check board flags */
		aspm_en = true;
		if (board.board_flags2 & BHND_BFL2_PCIEWAR_OVR)
			aspm_en = false;

		/* Early Apple devices did not (but should have) set
		 * BHND_BFL2_PCIEWAR_OVR in SPROM. */
		if (sc->quirks & BHND_PCIE_QUIRK_BFL2_PCIEWAR_EN)
			aspm_en = false;

		sc->aspm_quirk_override.aspm_en = aspm_en;
	}

	/* Determine correct polarity by observing the attach-time PCIe PHY
	 * link status. This is used later to reset/force the SerDes
	 * polarity */
	if (sc->quirks & BHND_PCIE_QUIRK_SDR9_POLARITY) {
		uint32_t st;
		bool inv;

		st = BHND_PCI_PROTO_READ_4(sc, BHND_PCIE_PLP_STATUSREG);
		inv = ((st & BHND_PCIE_PLP_POLARITY_INV) != 0);
		sc->sdr9_quirk_polarity.inv = inv;
	}

	/* Override maximum read request size */
	if (bhnd_get_class(sc->dev) == BHND_DEVCLASS_PCIE) {
		int	msize;

		msize = 128; /* compatible with all PCIe-G1 core revisions */
		if (sc->quirks & BHND_PCIE_QUIRK_DEFAULT_MRRS_512)
			msize = 512;

		if (pci_set_max_read_req(sc->pci_dev, msize) == 0)
			panic("set mrrs on non-PCIe device");
	}

	return (0);
}

/**
 * Apply any hardware workarounds that are required upon attach or resume
 * of the bridge device.
 */
static int
bhnd_pci_wars_hwup(struct bhnd_pcihb_softc *sc, bhnd_pci_war_state state)
{
	/* Note that the order here matters; these work-arounds
	 * should not be re-ordered without careful review of their
	 * interdependencies */

	/* Enable PCI prefetch/burst/readmulti flags */
	if (sc->quirks & BHND_PCI_QUIRK_SBTOPCI2_PREF_BURST ||
	    sc->quirks & BHND_PCI_QUIRK_SBTOPCI2_READMULTI)
	{
		uint32_t sbp2;
		sbp2 = BHND_PCI_READ_4(sc, BHND_PCI_SBTOPCI2);

		if (sc->quirks & BHND_PCI_QUIRK_SBTOPCI2_PREF_BURST)
			sbp2 |= (BHND_PCI_SBTOPCI_PREF|BHND_PCI_SBTOPCI_BURST);

		if (sc->quirks & BHND_PCI_QUIRK_SBTOPCI2_READMULTI)
			sbp2 |= BHND_PCI_SBTOPCI_RC_READMULTI;

		BHND_PCI_WRITE_4(sc, BHND_PCI_SBTOPCI2, sbp2);
	}

	/* Disable PCI CLKRUN# */
	if (sc->quirks & BHND_PCI_QUIRK_CLKRUN_DSBL) {
		uint32_t ctl;
	
		ctl = BHND_PCI_READ_4(sc, BHND_PCI_CLKRUN_CTL);
		ctl |= BHND_PCI_CLKRUN_DSBL;
		BHND_PCI_WRITE_4(sc, BHND_PCI_CLKRUN_CTL, ctl);
	}
	
	/* Enable TLP unmatched address handling work-around */
	if (sc->quirks & BHND_PCIE_QUIRK_UR_STATUS_FIX) {
		uint32_t wrs;
		wrs = BHND_PCI_PROTO_READ_4(sc, BHND_PCIE_TLP_WORKAROUNDSREG);
		wrs |= BHND_PCIE_TLP_WORKAROUND_URBIT;
		BHND_PCI_PROTO_WRITE_4(sc, BHND_PCIE_TLP_WORKAROUNDSREG, wrs);
	}

	/* Adjust SerDes CDR tuning to ensure that CDR is stable before sending
	 * data during L0s to L0 exit transitions. */
	if (sc->quirks & BHND_PCIE_QUIRK_SDR9_L0s_HANG) {
		uint16_t sdv;

		/* Set RX track/acquire timers to 2.064us/40.96us */
		sdv = BPCI_REG_SET(0, PCIE_SDR9_RX_TIMER1_LKTRK, (2064/16));
		sdv = BPCI_REG_SET(sdv, PCIE_SDR9_RX_TIMER1_LKACQ,
		    (40960/1024));
		BHND_PCI_MDIO_WRITE(sc, BHND_PCIE_PHY_SDR9_TXRX,
		    BHND_PCIE_SDR9_RX_TIMER1, sdv);

		/* Apply CDR frequency workaround */
		sdv = BHND_PCIE_SDR9_RX_CDR_FREQ_OVR_EN;
		sdv = BPCI_REG_SET(sdv, PCIE_SDR9_RX_CDR_FREQ_OVR, 0x0);
		BHND_PCI_MDIO_WRITE(sc, BHND_PCIE_PHY_SDR9_TXRX,
		    BHND_PCIE_SDR9_RX_CDR, sdv);

		/* Apply CDR BW tunings */
		sdv = 0;
		sdv = BPCI_REG_SET(sdv, PCIE_SDR9_RX_CDRBW_INTGTRK, 0x2);
		sdv = BPCI_REG_SET(sdv, PCIE_SDR9_RX_CDRBW_INTGACQ, 0x4);
		sdv = BPCI_REG_SET(sdv, PCIE_SDR9_RX_CDRBW_PROPTRK, 0x6);
		sdv = BPCI_REG_SET(sdv, PCIE_SDR9_RX_CDRBW_PROPACQ, 0x6);
		BHND_PCI_MDIO_WRITE(sc, BHND_PCIE_PHY_SDR9_TXRX,
		    BHND_PCIE_SDR9_RX_CDRBW, sdv);
	}

	/* Force correct SerDes polarity */
	if (sc->quirks & BHND_PCIE_QUIRK_SDR9_POLARITY) {
		uint16_t	rxctl;

		rxctl = BHND_PCI_MDIO_READ(sc, BHND_PCIE_PHY_SDR9_TXRX,
		    BHND_PCIE_SDR9_RX_CTRL);

		rxctl |= BHND_PCIE_SDR9_RX_CTRL_FORCE;
		if (sc->sdr9_quirk_polarity.inv)
			rxctl |= BHND_PCIE_SDR9_RX_CTRL_POLARITY_INV;
		else
			rxctl &= ~BHND_PCIE_SDR9_RX_CTRL_POLARITY_INV;

		BHND_PCI_MDIO_WRITE(sc, BHND_PCIE_PHY_SDR9_TXRX,
		    BHND_PCIE_SDR9_RX_CTRL, rxctl);
	}

	/* Disable startup retry on PLL frequency detection failure */
	if (sc->quirks & BHND_PCIE_QUIRK_SDR9_NO_FREQRETRY) {
		uint16_t	pctl;

		pctl = BHND_PCI_MDIO_READ(sc, BHND_PCIE_PHY_SDR9_PLL,
		    BHND_PCIE_SDR9_PLL_CTRL);

		pctl &= ~BHND_PCIE_SDR9_PLL_CTRL_FREQDET_EN;
		BHND_PCI_MDIO_WRITE(sc, BHND_PCIE_PHY_SDR9_PLL,
		    BHND_PCIE_SDR9_PLL_CTRL, pctl);
	}
	
	/* Explicitly enable PCI-PM */
	if (sc->quirks & BHND_PCIE_QUIRK_PCIPM_REQEN) {
		uint32_t lcreg;
		lcreg = BHND_PCI_PROTO_READ_4(sc, BHND_PCIE_DLLP_LCREG);
		lcreg |= BHND_PCIE_DLLP_LCREG_PCIPM_EN;
		BHND_PCI_PROTO_WRITE_4(sc, BHND_PCIE_DLLP_LCREG, lcreg);
	}

	/* Adjust L1 timer to fix slow L1->L0 transitions */
	if (sc->quirks & BHND_PCIE_QUIRK_L1_IDLE_THRESH) {
		uint32_t pmt;
		pmt = BHND_PCI_PROTO_READ_4(sc, BHND_PCIE_DLLP_PMTHRESHREG);
		pmt = BPCI_REG_SET(pmt, PCIE_L1THRESHOLDTIME,
		    BHND_PCIE_L1THRESHOLD_WARVAL);
		BHND_PCI_PROTO_WRITE_4(sc, BHND_PCIE_DLLP_PMTHRESHREG, pmt);
	}

	/* Extend L1 timer for better performance.
	 * TODO: We could enable/disable this on demand for better power
	 * savings if we tie this to HT clock request handling */
	if (sc->quirks & BHND_PCIE_QUIRK_L1_TIMER_PERF) {
		uint32_t pmt;
		pmt = BHND_PCI_PROTO_READ_4(sc, BHND_PCIE_DLLP_PMTHRESHREG);
		pmt |= BHND_PCIE_ASPMTIMER_EXTEND;
		BHND_PCI_PROTO_WRITE_4(sc, BHND_PCIE_DLLP_PMTHRESHREG, pmt);
	}

	/* Override ASPM/ECPM settings in SPROM shadow and PCIER_LINK_CTL */
	if (sc->quirks & BHND_PCIE_QUIRK_ASPM_OVR) {
		bus_size_t	reg;
		uint16_t	cfg;

		/* Set ASPM L1/L0s flags in SPROM shadow */
		reg = BHND_PCIE_SPROM_SHADOW + BHND_PCIE_SRSH_ASPM_OFFSET;
		cfg = BHND_PCI_READ_2(sc, reg);

		if (sc->aspm_quirk_override.aspm_en)
			cfg |= BHND_PCIE_SRSH_ASPM_ENB;
		else
			cfg &= ~BHND_PCIE_SRSH_ASPM_ENB;
		
		BHND_PCI_WRITE_2(sc, reg, cfg);


		/* Set ASPM/ECPM (CLKREQ) flags in PCIe link control register */
		cfg = pcie_read_config(sc->pci_dev, PCIER_LINK_CTL, 2);

		if (sc->aspm_quirk_override.aspm_en)
			cfg |= PCIEM_LINK_CTL_ASPMC;
		else
			cfg &= ~PCIEM_LINK_CTL_ASPMC;

		cfg &= ~PCIEM_LINK_CTL_ECPM;		/* CLKREQ# */

		pcie_write_config(sc->pci_dev, PCIER_LINK_CTL, cfg, 2); 

		/* Set CLKREQ (ECPM) flags in SPROM shadow */
		reg = BHND_PCIE_SPROM_SHADOW + BHND_PCIE_SRSH_CLKREQ_OFFSET_R5;
		cfg = BHND_PCI_READ_2(sc, reg);
		
		if (sc->aspm_quirk_override.aspm_en)
			cfg |= BHND_PCIE_SRSH_CLKREQ_ENB;
		else
			cfg &= ~BHND_PCIE_SRSH_CLKREQ_ENB;

		BHND_PCI_WRITE_2(sc, reg, cfg);
	}

	/* Enable L23READY_EXIT_NOPRST if not already set in SPROM. */
	if (sc->quirks & BHND_PCIE_QUIRK_SPROM_L23_PCI_RESET) {
		bus_size_t	reg;
		uint16_t	cfg;

		/* Fetch the misc cfg flags from SPROM */
		reg = BHND_PCIE_SPROM_SHADOW + BHND_PCIE_SRSH_PCIE_MISC_CONFIG;
		cfg = BHND_PCI_READ_2(sc, reg);

		/* Write EXIT_NOPRST flag if not already set in SPROM */
		if (!(cfg & BHND_PCIE_SRSH_L23READY_EXIT_NOPRST)) {
			cfg |= BHND_PCIE_SRSH_L23READY_EXIT_NOPRST;
			BHND_PCI_WRITE_2(sc, reg, cfg);
		}
	}

	/* Disable SerDes PLL down */
	if (sc->quirks & BHND_PCIE_QUIRK_SERDES_NOPLLDOWN) {
		device_t	bhnd, chipc;
		bus_size_t	reg;
		
		bhnd = device_get_parent(sc->dev);
		chipc = bhnd_bus_find_child(bhnd, BHND_DEVCLASS_CC, 0);
		KASSERT(chipc != NULL, ("missing chipcommon device"));

		/* Write SerDes PLL disable flag to the ChipCommon core */
		BHND_CHIPC_WRITE_CHIPCTRL(chipc, CHIPCTRL_4321_PLL_DOWN,
		    CHIPCTRL_4321_PLL_DOWN);

		/* Clear SPROM shadow backdoor register */
		reg = BHND_PCIE_SPROM_SHADOW + BHND_PCIE_SRSH_BD_OFFSET;
		BHND_PCI_WRITE_2(sc, reg, 0);
	}

	/* Adjust TX drive strength and pre-emphasis coefficient */
	if (sc->quirks & BHND_PCIE_QUIRK_SERDES_TXDRV_ADJUST) {
		uint16_t txdrv;

		/* Fetch current TX driver parameters */
		txdrv = BHND_PCI_MDIO_READ_EXT(sc, BHND_PCIE_PHYADDR_SD,
		    BHND_PCIE_SD_REGS_TX0, BHND_PCIE_SD_TX_DRIVER);

		/* Set 700mV drive strength */
		if (sc->quirks & BHND_PCIE_QUIRK_SERDES_TXDRV_700MV) {
			txdrv = BPCI_REG_SET(txdrv, PCIE_SD_TX_DRIVER_P2_COEFF,
			    BHND_PCIE_APPLE_TX_P2_COEFF_700MV);

			txdrv = BPCI_REG_SET(txdrv, PCIE_SD_TX_DRIVER_IDRIVER,
			    BHND_PCIE_APPLE_TX_IDRIVER_700MV);
		}

		/* ... or, set max drive strength */
		if (sc->quirks & BHND_PCIE_QUIRK_SERDES_TXDRV_MAX) {
			txdrv = BPCI_REG_SET(txdrv, PCIE_SD_TX_DRIVER_P2_COEFF,
			    BHND_PCIE_APPLE_TX_P2_COEFF_MAX);
			
			txdrv = BPCI_REG_SET(txdrv, PCIE_SD_TX_DRIVER_IDRIVER,
			    BHND_PCIE_APPLE_TX_IDRIVER_MAX);
		}

		BHND_PCI_MDIO_WRITE_EXT(sc, BHND_PCIE_PHYADDR_SD,
		    BHND_PCIE_SD_REGS_TX0, BHND_PCIE_SD_TX_DRIVER, txdrv);
	}

	return (0);
}

/**
 * Apply any hardware workarounds that are required upon detach or suspend
 * of the bridge device.
 */
static int
bhnd_pci_wars_hwdown(struct bhnd_pcihb_softc *sc, bhnd_pci_war_state state)
{
	/* Reduce L1 timer for better power savings.
	 * TODO: We could enable/disable this on demand for better power
	 * savings if we tie this to HT clock request handling */
	if (sc->quirks & BHND_PCIE_QUIRK_L1_TIMER_PERF) {
		uint32_t pmt;
		pmt = BHND_PCI_PROTO_READ_4(sc, BHND_PCIE_DLLP_PMTHRESHREG);
		pmt &= ~BHND_PCIE_ASPMTIMER_EXTEND;
		BHND_PCI_PROTO_WRITE_4(sc, BHND_PCIE_DLLP_PMTHRESHREG, pmt);
	}

	/* Enable CLKREQ (ECPM). If suspending, also disable ASPM L1 entry */
	if (sc->quirks & BHND_PCIE_QUIRK_ASPM_OVR) {
		uint16_t	lcreg;

		lcreg = pcie_read_config(sc->pci_dev, PCIER_LINK_CTL, 2);

		lcreg |= PCIEM_LINK_CTL_ECPM;	/* CLKREQ# */
		if (state == BHND_PCI_WAR_SUSPEND)
			lcreg &= ~PCIEM_LINK_CTL_ASPMC_L1;

		pcie_write_config(sc->pci_dev, PCIER_LINK_CTL, lcreg, 2);
	}

	return (0);
}

static device_method_t bhnd_pci_hostb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_attach,		bhnd_pci_hostb_attach),
	DEVMETHOD(device_detach,		bhnd_pci_hostb_detach),
	DEVMETHOD(device_suspend,		bhnd_pci_hostb_suspend),
	DEVMETHOD(device_resume,		bhnd_pci_hostb_resume),	

	DEVMETHOD_END
};

DEFINE_CLASS_1(bhnd_hostb, bhnd_pci_hostb_driver, bhnd_pci_hostb_methods, 
    sizeof(struct bhnd_pcihb_softc), bhnd_pci_driver);
DRIVER_MODULE(bhnd_pci_hostb, bhnd, bhnd_pci_hostb_driver, bhnd_hostb_devclass, 0, 0);

MODULE_VERSION(bhnd_pci_hostb, 1);
MODULE_DEPEND(bhnd_pci_hostb, bhnd, 1, 1, 1);
MODULE_DEPEND(bhnd_pci_hostb, bhnd_pci, 1, 1, 1);
