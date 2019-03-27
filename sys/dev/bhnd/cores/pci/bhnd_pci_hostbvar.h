/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
 * All rights reserved.
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
 * 
 * $FreeBSD$
 */

#ifndef _BHND_CORES_PCI_BHND_PCI_HOSTBVAR_H_
#define _BHND_CORES_PCI_BHND_PCI_HOSTBVAR_H_

/*
 * PCI/PCIe-Gen1 Host Bridge definitions.
 */

#include <sys/param.h>
#include <sys/bus.h>

#include "bhnd_pcivar.h"

DECLARE_CLASS(bhnd_pci_hostb_driver);

/**
 * PCI/PCIe-Gen1 endpoint-mode device quirks
 */
enum {
	/** No quirks */
	BHND_PCI_QUIRK_NONE			= 0,
	
	/**
	 * SBTOPCI_PREF and SBTOPCI_BURST must be set on the
	 * SSB_PCICORE_SBTOPCI2 register.
	 */
	BHND_PCI_QUIRK_SBTOPCI2_PREF_BURST	= (1<<1),

	/**
	 * SBTOPCI_RC_READMULTI must be set on the SSB_PCICORE_SBTOPCI2
	 * register.
	 */
	BHND_PCI_QUIRK_SBTOPCI2_READMULTI	= (1<<2),

	/**
	 * PCI CLKRUN# should be disabled on attach (via CLKRUN_DSBL).
	 * 
	 * The purpose of this work-around is unclear; there is some
	 * documentation regarding earlier Broadcom drivers supporting
	 * a "force CLKRUN#" *enable* registry key for use on mobile
	 * hardware.
	 */
	BHND_PCI_QUIRK_CLKRUN_DSBL		= (1<<3),

	/**
	 * On PCI-attached BCM4321CB* boards, the PCI latency timer must be set
	 * to 960ns on initial attach.
	 */
	BHND_PCI_QUIRK_960NS_LATTIM_OVR		= (1<<4),

	/**
	 * TLP workaround for unmatched address handling is required.
	 * 
	 * This TLP workaround will enable setting of the PCIe UR status bit
	 * on memory access to an unmatched address.
	 */
	BHND_PCIE_QUIRK_UR_STATUS_FIX		= (1<<5),

	/**
	 * PCI-PM power management must be explicitly enabled via
	 * the data link control register.
	 */
	BHND_PCIE_QUIRK_PCIPM_REQEN		= (1<<6),

	/**
	 * Fix L0s to L0 exit transition on SerDes <= rev9 devices.
	 * 
	 * On these devices, PCIe/SerDes symbol lock can be lost if the
	 * reference clock has not fully stabilized during the L0s to L0
	 * exit transition, triggering an internal reset of the chip.
	 * 
	 * The SerDes RX CDR phase lock timers and proportional/integral
	 * filters must be tweaked to ensure the CDR has fully stabilized
	 * before asserting receive sequencer completion.
	 */
	BHND_PCIE_QUIRK_SDR9_L0s_HANG		= (1<<7),

	/**
	 * The idle time for entering L1 low-power state must be
	 * explicitly set (to 114ns) to fix slow L1->L0 transition issues.
	 */
	BHND_PCIE_QUIRK_L1_IDLE_THRESH		= (1<<8),
	
	/**
	 * The ASPM L1 entry timer should be extended for better performance,
	 * and restored for better power savings.
	 */
	BHND_PCIE_QUIRK_L1_TIMER_PERF		= (1<<9),

	/**
	 * ASPM and ECPM settings must be overridden manually.
	 * Applies to 4311B0/4321B1 chipset revisions.
	 * 
	 * The override behavior is controlled by the BHND_BFL2_PCIEWAR_OVR
	 * flag; if set, ASPM and CLKREQ should be explicitly disabled. If not
	 * set, they should be explicitly enabled.
	 * 
	 * Attach/Resume:
	 *   - Update SRSH_ASPM_ENB flag in the SPROM ASPM register.
	 *   - Update SRSH_CLKREQ_ENB flag in the SPROM CLKREQ_REV5
	 *     register.
	 *   - Update ASPM L0S/L1 flags in PCIER_LINK_CTL register.
	 *   - Clear CLKREQ (ECPM) flag in PCIER_LINK_CTL register.
	 * 
	 * Suspend:
	 *   - Clear ASPM L1 flag in the PCIER_LINK_CTL register.
	 *   - Set CLKREQ (ECPM) flag in the PCIER_LINK_CTL register.
	 * 
	 * Detach:
	 *   - Set CLKREQ (ECPM) flag in the PCIER_LINK_CTL register.
	 */
	BHND_PCIE_QUIRK_ASPM_OVR		= (1<<10),

	/**
	 * A subset of Apple devices did not set the BHND_BFL2_PCIEWAR_OVR
	 * flag in SPROM; on these devices, the BHND_BFL2_PCIEWAR_OVR flag
	 * should always be treated as if set.
	 */
	BHND_PCIE_QUIRK_BFL2_PCIEWAR_EN		= (1<<11),

	/**
	 * Fix SerDes polarity on SerDes <= rev9 devices.
	 *
	 * The SerDes polarity must be saved at device attachment, and
	 * restored on suspend/resume.
	 */
	BHND_PCIE_QUIRK_SDR9_POLARITY		= (1<<12),

	/**
	 * SerDes PLL down flag must be manually disabled (by ChipCommon) on
	 * resume.
	 */
	BHND_PCIE_QUIRK_SERDES_NOPLLDOWN	= (1<<13),

        /**
	 * On attach and resume, consult the SPROM to determine whether
	 * the L2/L3-Ready w/o PCI RESET work-around must be applied.
	 *
	 * If L23READY_EXIT_NOPRST is not already set in the SPROM, set it
	 */
	BHND_PCIE_QUIRK_SPROM_L23_PCI_RESET	= (1<<14),
	
	/**
	 * The PCIe SerDes PLL must be configured to not retry the startup
	 * sequence upon frequency detection failure on SerDes <= rev9 devices
	 * 
	 * The issue this workaround resolves is unknown.
	 */
	BHND_PCIE_QUIRK_SDR9_NO_FREQRETRY	= (1<<15),

	/**
	 * Common flag for quirks that require PCIe SerDes TX
	 * drive strength adjustment.
	 * 
	 * Only applies to PCIe >= rev10 devices.
	 */
	BHND_PCIE_QUIRK_SERDES_TXDRV_ADJUST	= (1<<16),

	/**
	 * On Apple BCM94322X9 devices, the PCIe SerDes TX drive strength
	 * should be set to 700mV.
	 *
	 * The exact issue is unknown, but presumably this workaround
	 * resolves signal integrity issues with these devices.
	 * 
	 * Only applies to PCIe >= rev10 devices.
	 */
	BHND_PCIE_QUIRK_SERDES_TXDRV_700MV	= (1<<17) |
	    BHND_PCIE_QUIRK_SERDES_TXDRV_ADJUST,

	/**
	 * On some Apple BCM4331-based devices, the PCIe SerDes TX drive
	 * strength should be set to its maximum.
	 *
	 * The exact issue is unknown, but presumably this workaround
	 * resolves signal integrity issues with these devices.
	 */
	BHND_PCIE_QUIRK_SERDES_TXDRV_MAX	= (1<<18) |
	    BHND_PCIE_QUIRK_SERDES_TXDRV_ADJUST,

	/**
	 * PCIe cores prior to rev18 do not support an MRRS larger than
	 * 128 bytes.
	 */
	BHND_PCIE_QUIRK_MAX_MRRS_128		= (1<<19),

	/**
	 * The PCIe core should be configured with an MRRS of 512 bytes.
	 */
	BHND_PCIE_QUIRK_DEFAULT_MRRS_512	= (1<<20),
};

/**
 * bhnd_pci_hostb driver instance state.
 */
struct bhnd_pcihb_softc {
	struct bhnd_pci_softc	common;		/**< common bhnd_pci state */
	device_t		dev;
	device_t		pci_dev;	/**< host PCI device */
	uint32_t		quirks;		/**< hostb device quirks */

	/** BHND_PCIE_QUIRK_ASPM_OVR state. */
	struct {
		/**
		 * ASPM/CLKREQ override setting.
		 * 
		 * If true, ASPM/CLKREQ should be overridden as enabled.
		 * If false, ASPM/CLKREQ should be overridden as disabled.
		 */
		bool aspm_en;
	} aspm_quirk_override;

	/** BHND_PCIE_QUIRK_SDR9_POLARITY state. */
	struct {
		/** 
		 * PCIe SerDes RX polarity.
		 *
		 * Initialized to the PCIe link's RX polarity
		 * at attach time. This is used to restore the
		 * correct polarity on resume */
		bool	inv;
	} sdr9_quirk_polarity;
};


#endif /* _BHND_CORES_PCI_BHND_PCI_HOSTBVAR_H_ */
