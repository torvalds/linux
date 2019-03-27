/*-
 * Copyright (c) 2009-2010 Weongyo Jeong <weongyo@freebsd.org>
 * Copyright (c) 2016 Adrian Chadd <adrian@freebsd.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_bwn.h"
#include "opt_wlan.h"

/*
 * The Broadcom Wireless LAN controller driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/firmware.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_llc.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_regdomain.h>
#include <net80211/ieee80211_phy.h>
#include <net80211/ieee80211_ratectl.h>

#include <dev/bhnd/bhnd.h>
#include <dev/bhnd/bhnd_ids.h>

#include <dev/bhnd/cores/chipc/chipc.h>
#include <dev/bhnd/cores/pmu/bhnd_pmu.h>

#include <dev/bwn/if_bwnreg.h>
#include <dev/bwn/if_bwnvar.h>

#include <dev/bwn/if_bwn_debug.h>
#include <dev/bwn/if_bwn_misc.h>
#include <dev/bwn/if_bwn_phy_common.h>

void
bwn_mac_switch_freq(struct bwn_mac *mac, bhnd_pmu_spuravoid spurmode)
{
	struct bwn_softc *sc = mac->mac_sc;
	uint16_t chip_id = sc->sc_cid.chip_id;

	if (chip_id == BHND_CHIPID_BCM4331) {
		switch (spurmode) {
		case BHND_PMU_SPURAVOID_M2: /* 168 Mhz: 2^26/168 = 0x61862 */
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_LOW, 0x1862);
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_HIGH, 0x6);
			break;
		case BHND_PMU_SPURAVOID_M1: /* 164 Mhz: 2^26/164 = 0x63e70 */
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_LOW, 0x3e70);
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_HIGH, 0x6);
			break;
		case BHND_PMU_SPURAVOID_NONE: /* 160 Mhz: 2^26/160 = 0x66666 */
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_LOW, 0x6666);
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_HIGH, 0x6);
			break;
		}
	} else if (chip_id == BHND_CHIPID_BCM43131 ||
	    chip_id == BHND_CHIPID_BCM43217 ||
	    chip_id == BHND_CHIPID_BCM43222 ||
	    chip_id == BHND_CHIPID_BCM43224 ||
	    chip_id == BHND_CHIPID_BCM43225 ||
	    chip_id == BHND_CHIPID_BCM43227 ||
	    chip_id == BHND_CHIPID_BCM43228) {
		switch (spurmode) {
		case BHND_PMU_SPURAVOID_M2: /* 126 Mhz */
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_LOW, 0x2082);
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_HIGH, 0x8);
			break;
		case BHND_PMU_SPURAVOID_M1: /* 123 Mhz */
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_LOW, 0x5341);
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_HIGH, 0x8);
			break;
		case BHND_PMU_SPURAVOID_NONE: /* 120 Mhz */
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_LOW, 0x8889);
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_HIGH, 0x8);
			break;
		}
	} else if (mac->mac_phy.type == BWN_PHYTYPE_LCN) {
		switch (spurmode) {
		case BHND_PMU_SPURAVOID_M2:
			device_printf(sc->sc_dev, "invalid spuravoid mode: "
			    "%d\n", spurmode);
			break;
		case BHND_PMU_SPURAVOID_M1: /* 82 Mhz */
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_LOW, 0x7CE0);
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_HIGH, 0xC);
			break;
		case BHND_PMU_SPURAVOID_NONE: /* 80 Mhz */
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_LOW, 0xCCCD);
			BWN_WRITE_2(mac, BWN_TSF_CLK_FRAC_HIGH, 0xC);
			break;
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/BmacPhyClkFgc */
int
bwn_phy_force_clock(struct bwn_mac *mac, int force)
{
	struct bwn_softc	*sc;
	uint32_t		 val, mask;
	int			 error;

	sc = mac->mac_sc;

	/* XXX Only for N, HT and AC PHYs */
	mask = BHND_IOCTL_CLK_FORCE;
	if (force) {
		val = BHND_IOCTL_CLK_FORCE;
	} else {
		val = 0;
	}

	if ((error = bhnd_write_ioctl(sc->sc_dev, val, mask))) {
		device_printf(sc->sc_dev, "failed to set CLK_FORCE ioctl flag: "
		    "%d\n", error);
		return (error);
	}

	return (0);
}

int
bwn_radio_wait_value(struct bwn_mac *mac, uint16_t offset, uint16_t mask,
    uint16_t value, int delay, int timeout)
{
	uint16_t val;
	int i;

	for (i = 0; i < timeout; i += delay) {
		val = BWN_RF_READ(mac, offset);
		if ((val & mask) == value)
			return (1);
		DELAY(delay);
	}
	return (0);
}

int
bwn_mac_phy_clock_set(struct bwn_mac *mac, int enabled)
{
	struct bwn_softc	*sc;
	uint32_t		 val, mask;
	int			 error;

	sc = mac->mac_sc;

	mask = BWN_IOCTL_MACPHYCLKEN;
	if (enabled) {
		val = BWN_IOCTL_MACPHYCLKEN;
	} else {
		val = 0;
	}

	if ((error = bhnd_write_ioctl(sc->sc_dev, val, mask))) {
		device_printf(sc->sc_dev, "failed to set MACPHYCLKEN ioctl "
		    "flag: %d\n", error);
		return (error);
	}

	return (0);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/BmacCorePllReset */
int
bwn_wireless_core_phy_pll_reset(struct bwn_mac *mac)
{
	struct bwn_softc	*sc;
	uint32_t		 pll_flag;

	sc = mac->mac_sc;

	if (sc->sc_pmu == NULL) {
		device_printf(sc->sc_dev, "PMU device not found\n");
		return (ENXIO);
	}

	pll_flag = 0x4;
	bhnd_pmu_write_chipctrl(sc->sc_pmu, 0x0, 0x0, pll_flag);
	bhnd_pmu_write_chipctrl(sc->sc_pmu, 0x0, pll_flag, pll_flag);	
	bhnd_pmu_write_chipctrl(sc->sc_pmu, 0x0, 0x0, pll_flag);

	return (0);
}
