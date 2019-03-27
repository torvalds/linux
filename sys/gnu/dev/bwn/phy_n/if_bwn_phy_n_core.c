
/*

  Broadcom B43 wireless driver
  IEEE 802.11n PHY data tables

  Copyright (c) 2008 Michael Buesch <m@bues.ch>
  Copyright (c) 2010 Rafał Miłecki <zajec5@gmail.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * The Broadcom Wireless LAN controller driver.
 */

#include "opt_wlan.h"
#include "opt_bwn.h"

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

#include <dev/bhnd/cores/pmu/bhnd_pmu.h>
#include <dev/bhnd/cores/chipc/chipc.h>

#include <dev/bwn/if_bwnreg.h>
#include <dev/bwn/if_bwnvar.h>
#include <dev/bwn/if_bwn_misc.h>
#include <dev/bwn/if_bwn_util.h>
#include <dev/bwn/if_bwn_debug.h>
#include <dev/bwn/if_bwn_phy_common.h>
#include <dev/bwn/if_bwn_cordic.h>

#include <gnu/dev/bwn/phy_n/if_bwn_phy_n_regs.h>
#include <gnu/dev/bwn/phy_n/if_bwn_phy_n_ppr.h>
#include <gnu/dev/bwn/phy_n/if_bwn_phy_n_sprom.h>
#include <gnu/dev/bwn/phy_n/if_bwn_phy_n_tables.h>
#include <gnu/dev/bwn/phy_n/if_bwn_radio_2055.h>
#include <gnu/dev/bwn/phy_n/if_bwn_radio_2056.h>
#include <gnu/dev/bwn/phy_n/if_bwn_radio_2057.h>
#include <gnu/dev/bwn/phy_n/if_bwn_phy_n_core.h>

#include "bhnd_nvram_map.h"

struct bwn_nphy_txgains {
	uint16_t tx_lpf[2];
	uint16_t txgm[2];
	uint16_t pga[2];
	uint16_t pad[2];
	uint16_t ipa[2];
};

struct bwn_nphy_iqcal_params {
	uint16_t tx_lpf;
	uint16_t txgm;
	uint16_t pga;
	uint16_t pad;
	uint16_t ipa;
	uint16_t cal_gain;
	uint16_t ncorr[5];
};

struct bwn_nphy_iq_est {
	int32_t iq0_prod;
	uint32_t i0_pwr;
	uint32_t q0_pwr;
	int32_t iq1_prod;
	uint32_t i1_pwr;
	uint32_t q1_pwr;
};

enum bwn_nphy_rf_sequence {
	BWN_RFSEQ_RX2TX,
	BWN_RFSEQ_TX2RX,
	BWN_RFSEQ_RESET2RX,
	BWN_RFSEQ_UPDATE_GAINH,
	BWN_RFSEQ_UPDATE_GAINL,
	BWN_RFSEQ_UPDATE_GAINU,
};

enum n_rf_ctl_over_cmd {
	N_RF_CTL_OVER_CMD_RXRF_PU = 0,
	N_RF_CTL_OVER_CMD_RX_PU = 1,
	N_RF_CTL_OVER_CMD_TX_PU = 2,
	N_RF_CTL_OVER_CMD_RX_GAIN = 3,
	N_RF_CTL_OVER_CMD_TX_GAIN = 4,
};

enum n_intc_override {
	N_INTC_OVERRIDE_OFF = 0,
	N_INTC_OVERRIDE_TRSW = 1,
	N_INTC_OVERRIDE_PA = 2,
	N_INTC_OVERRIDE_EXT_LNA_PU = 3,
	N_INTC_OVERRIDE_EXT_LNA_GAIN = 4,
};

enum n_rssi_type {
	N_RSSI_W1 = 0,
	N_RSSI_W2,
	N_RSSI_NB,
	N_RSSI_IQ,
	N_RSSI_TSSI_2G,
	N_RSSI_TSSI_5G,
	N_RSSI_TBD,
};

enum n_rail_type {
	N_RAIL_I = 0,
	N_RAIL_Q = 1,
};

static inline bool bwn_nphy_ipa(struct bwn_mac *mac)
{
	bwn_band_t band = bwn_current_band(mac);
	return ((mac->mac_phy.phy_n->ipa2g_on && band == BWN_BAND_2G) ||
		(mac->mac_phy.phy_n->ipa5g_on && band == BWN_BAND_5G));
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RxCoreGetState */
static uint8_t bwn_nphy_get_rx_core_state(struct bwn_mac *mac)
{
	return (BWN_PHY_READ(mac, BWN_NPHY_RFSEQCA) & BWN_NPHY_RFSEQCA_RXEN) >>
		BWN_NPHY_RFSEQCA_RXEN_SHIFT;
}

/**************************************************
 * RF (just without bwn_nphy_rf_ctl_intc_override)
 **************************************************/

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/ForceRFSeq */
static void bwn_nphy_force_rf_sequence(struct bwn_mac *mac,
				       enum bwn_nphy_rf_sequence seq)
{
	static const uint16_t trigger[] = {
		[BWN_RFSEQ_RX2TX]		= BWN_NPHY_RFSEQTR_RX2TX,
		[BWN_RFSEQ_TX2RX]		= BWN_NPHY_RFSEQTR_TX2RX,
		[BWN_RFSEQ_RESET2RX]		= BWN_NPHY_RFSEQTR_RST2RX,
		[BWN_RFSEQ_UPDATE_GAINH]	= BWN_NPHY_RFSEQTR_UPGH,
		[BWN_RFSEQ_UPDATE_GAINL]	= BWN_NPHY_RFSEQTR_UPGL,
		[BWN_RFSEQ_UPDATE_GAINU]	= BWN_NPHY_RFSEQTR_UPGU,
	};
	int i;
	uint16_t seq_mode = BWN_PHY_READ(mac, BWN_NPHY_RFSEQMODE);

	if (seq >= nitems(trigger)) {
		BWN_WARNPRINTF(mac->mac_sc, "%s: seq %d > max", __func__, seq);
	}

	BWN_PHY_SET(mac, BWN_NPHY_RFSEQMODE,
		    BWN_NPHY_RFSEQMODE_CAOVER | BWN_NPHY_RFSEQMODE_TROVER);
	BWN_PHY_SET(mac, BWN_NPHY_RFSEQTR, trigger[seq]);
	for (i = 0; i < 200; i++) {
		if (!(BWN_PHY_READ(mac, BWN_NPHY_RFSEQST) & trigger[seq]))
			goto ok;
		DELAY(1000);
	}
	BWN_ERRPRINTF(mac->mac_sc, "RF sequence status timeout\n");
ok:
	BWN_PHY_WRITE(mac, BWN_NPHY_RFSEQMODE, seq_mode);
}

static void bwn_nphy_rf_ctl_override_rev19(struct bwn_mac *mac, uint16_t field,
					   uint16_t value, uint8_t core, bool off,
					   uint8_t override_id)
{
	/* TODO */
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RFCtrlOverrideRev7 */
static void bwn_nphy_rf_ctl_override_rev7(struct bwn_mac *mac, uint16_t field,
					  uint16_t value, uint8_t core, bool off,
					  uint8_t override)
{
	struct bwn_phy *phy = &mac->mac_phy;
	const struct bwn_nphy_rf_control_override_rev7 *e;
	uint16_t en_addrs[3][2] = {
		{ 0x0E7, 0x0EC }, { 0x342, 0x343 }, { 0x346, 0x347 }
	};
	uint16_t en_addr;
	uint16_t en_mask = field;
	uint16_t val_addr;
	uint8_t i;

	if (phy->rev >= 19 || phy->rev < 3) {
		BWN_WARNPRINTF(mac->mac_sc, "%s: phy rev %d out of range\n",
		    __func__,
		    phy->rev);
		return;
	}

	/* Remember: we can get NULL! */
	e = bwn_nphy_get_rf_ctl_over_rev7(mac, field, override);

	for (i = 0; i < 2; i++) {
		if (override >= nitems(en_addrs)) {
			BWN_ERRPRINTF(mac->mac_sc, "Invalid override value %d\n", override);
			return;
		}
		en_addr = en_addrs[override][i];

		if (e)
			val_addr = (i == 0) ? e->val_addr_core0 : e->val_addr_core1;

		if (off) {
			BWN_PHY_MASK(mac, en_addr, ~en_mask);
			if (e) /* Do it safer, better than wl */
				BWN_PHY_MASK(mac, val_addr, ~e->val_mask);
		} else {
			if (!core || (core & (1 << i))) {
				BWN_PHY_SET(mac, en_addr, en_mask);
				if (e)
					BWN_PHY_SETMASK(mac, val_addr, ~e->val_mask, (value << e->val_shift));
			}
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RFCtrlOverideOneToMany */
static void bwn_nphy_rf_ctl_override_one_to_many(struct bwn_mac *mac,
						 enum n_rf_ctl_over_cmd cmd,
						 uint16_t value, uint8_t core, bool off)
{
	struct bwn_phy *phy = &mac->mac_phy;
	uint16_t tmp;

	if (phy->rev < 7) {
		BWN_ERRPRINTF(mac->mac_sc, "%s: phy rev %d out of range\n",
		    __func__,
		    phy->rev);
	}

	switch (cmd) {
	case N_RF_CTL_OVER_CMD_RXRF_PU:
		bwn_nphy_rf_ctl_override_rev7(mac, 0x20, value, core, off, 1);
		bwn_nphy_rf_ctl_override_rev7(mac, 0x10, value, core, off, 1);
		bwn_nphy_rf_ctl_override_rev7(mac, 0x08, value, core, off, 1);
		break;
	case N_RF_CTL_OVER_CMD_RX_PU:
		bwn_nphy_rf_ctl_override_rev7(mac, 0x4, value, core, off, 1);
		bwn_nphy_rf_ctl_override_rev7(mac, 0x2, value, core, off, 1);
		bwn_nphy_rf_ctl_override_rev7(mac, 0x1, value, core, off, 1);
		bwn_nphy_rf_ctl_override_rev7(mac, 0x2, value, core, off, 2);
		bwn_nphy_rf_ctl_override_rev7(mac, 0x0800, 0, core, off, 1);
		break;
	case N_RF_CTL_OVER_CMD_TX_PU:
		bwn_nphy_rf_ctl_override_rev7(mac, 0x4, value, core, off, 0);
		bwn_nphy_rf_ctl_override_rev7(mac, 0x2, value, core, off, 1);
		bwn_nphy_rf_ctl_override_rev7(mac, 0x1, value, core, off, 2);
		bwn_nphy_rf_ctl_override_rev7(mac, 0x0800, 1, core, off, 1);
		break;
	case N_RF_CTL_OVER_CMD_RX_GAIN:
		tmp = value & 0xFF;
		bwn_nphy_rf_ctl_override_rev7(mac, 0x0800, tmp, core, off, 0);
		tmp = value >> 8;
		bwn_nphy_rf_ctl_override_rev7(mac, 0x6000, tmp, core, off, 0);
		break;
	case N_RF_CTL_OVER_CMD_TX_GAIN:
		tmp = value & 0x7FFF;
		bwn_nphy_rf_ctl_override_rev7(mac, 0x1000, tmp, core, off, 0);
		tmp = value >> 14;
		bwn_nphy_rf_ctl_override_rev7(mac, 0x4000, tmp, core, off, 0);
		break;
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RFCtrlOverride */
static void bwn_nphy_rf_ctl_override(struct bwn_mac *mac, uint16_t field,
				     uint16_t value, uint8_t core, bool off)
{
	int i;
	uint8_t index = fls(field);
	uint8_t addr, en_addr, val_addr;

	/* we expect only one bit set */
	if (field & (~(1 << (index - 1)))) {
		BWN_ERRPRINTF(mac->mac_sc, "%s: field 0x%04x has >1 bit set\n",
		    __func__,
		    field);
	}

	if (mac->mac_phy.rev >= 3) {
		const struct bwn_nphy_rf_control_override_rev3 *rf_ctrl;
		for (i = 0; i < 2; i++) {
			if (index == 0 || index == 16) {
				BWN_ERRPRINTF(mac->mac_sc,
					"Unsupported RF Ctrl Override call\n");
				return;
			}

			rf_ctrl = &tbl_rf_control_override_rev3[index - 1];
			en_addr = BWN_PHY_N((i == 0) ?
				rf_ctrl->en_addr0 : rf_ctrl->en_addr1);
			val_addr = BWN_PHY_N((i == 0) ?
				rf_ctrl->val_addr0 : rf_ctrl->val_addr1);

			if (off) {
				BWN_PHY_MASK(mac, en_addr, ~(field));
				BWN_PHY_MASK(mac, val_addr,
						~(rf_ctrl->val_mask));
			} else {
				if (core == 0 || ((1 << i) & core)) {
					BWN_PHY_SET(mac, en_addr, field);
					BWN_PHY_SETMASK(mac, val_addr,
						~(rf_ctrl->val_mask),
						(value << rf_ctrl->val_shift));
				}
			}
		}
	} else {
		const struct bwn_nphy_rf_control_override_rev2 *rf_ctrl;
		if (off) {
			BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_OVER, ~(field));
			value = 0;
		} else {
			BWN_PHY_SET(mac, BWN_NPHY_RFCTL_OVER, field);
		}

		for (i = 0; i < 2; i++) {
			if (index <= 1 || index == 16) {
				BWN_ERRPRINTF(mac->mac_sc,
					"Unsupported RF Ctrl Override call\n");
				return;
			}

			if (index == 2 || index == 10 ||
			    (index >= 13 && index <= 15)) {
				core = 1;
			}

			rf_ctrl = &tbl_rf_control_override_rev2[index - 2];
			addr = BWN_PHY_N((i == 0) ?
				rf_ctrl->addr0 : rf_ctrl->addr1);

			if ((1 << i) & core)
				BWN_PHY_SETMASK(mac, addr, ~(rf_ctrl->bmask),
						(value << rf_ctrl->shift));

			BWN_PHY_SET(mac, BWN_NPHY_RFCTL_OVER, 0x1);
			BWN_PHY_SET(mac, BWN_NPHY_RFCTL_CMD,
					BWN_NPHY_RFCTL_CMD_START);
			DELAY(1);
			BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_OVER, 0xFFFE);
		}
	}
}

static void bwn_nphy_rf_ctl_intc_override_rev7(struct bwn_mac *mac,
					       enum n_intc_override intc_override,
					       uint16_t value, uint8_t core_sel)
{
	uint16_t reg, tmp, tmp2, val;
	int core;

	/* TODO: What about rev19+? Revs 3+ and 7+ are a bit similar */

	for (core = 0; core < 2; core++) {
		if ((core_sel == 1 && core != 0) ||
		    (core_sel == 2 && core != 1))
			continue;

		reg = (core == 0) ? BWN_NPHY_RFCTL_INTC1 : BWN_NPHY_RFCTL_INTC2;

		switch (intc_override) {
		case N_INTC_OVERRIDE_OFF:
			BWN_PHY_WRITE(mac, reg, 0);
			BWN_PHY_MASK(mac, 0x2ff, ~0x2000);
			bwn_nphy_force_rf_sequence(mac, BWN_RFSEQ_RESET2RX);
			break;
		case N_INTC_OVERRIDE_TRSW:
			BWN_PHY_SETMASK(mac, reg, ~0xC0, value << 6);
			BWN_PHY_SET(mac, reg, 0x400);

			BWN_PHY_MASK(mac, 0x2ff, ~0xC000 & 0xFFFF);
			BWN_PHY_SET(mac, 0x2ff, 0x2000);
			BWN_PHY_SET(mac, 0x2ff, 0x0001);
			break;
		case N_INTC_OVERRIDE_PA:
			tmp = 0x0030;
			if (bwn_current_band(mac) == BWN_BAND_5G)
				val = value << 5;
			else
				val = value << 4;
			BWN_PHY_SETMASK(mac, reg, ~tmp, val);
			BWN_PHY_SET(mac, reg, 0x1000);
			break;
		case N_INTC_OVERRIDE_EXT_LNA_PU:
			if (bwn_current_band(mac) == BWN_BAND_5G) {
				tmp = 0x0001;
				tmp2 = 0x0004;
				val = value;
			} else {
				tmp = 0x0004;
				tmp2 = 0x0001;
				val = value << 2;
			}
			BWN_PHY_SETMASK(mac, reg, ~tmp, val);
			BWN_PHY_MASK(mac, reg, ~tmp2);
			break;
		case N_INTC_OVERRIDE_EXT_LNA_GAIN:
			if (bwn_current_band(mac) == BWN_BAND_5G) {
				tmp = 0x0002;
				tmp2 = 0x0008;
				val = value << 1;
			} else {
				tmp = 0x0008;
				tmp2 = 0x0002;
				val = value << 3;
			}
			BWN_PHY_SETMASK(mac, reg, ~tmp, val);
			BWN_PHY_MASK(mac, reg, ~tmp2);
			break;
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RFCtrlIntcOverride */
static void bwn_nphy_rf_ctl_intc_override(struct bwn_mac *mac,
					  enum n_intc_override intc_override,
					  uint16_t value, uint8_t core)
{
	uint8_t i, j;
	uint16_t reg, tmp, val;

	if (mac->mac_phy.rev >= 7) {
		bwn_nphy_rf_ctl_intc_override_rev7(mac, intc_override, value,
						   core);
		return;
	}

	if (mac->mac_phy.rev < 3) {
		BWN_ERRPRINTF(mac->mac_sc, "%s: phy rev %d out of range\n",
		    __func__,
		    mac->mac_phy.rev);
	}

	for (i = 0; i < 2; i++) {
		if ((core == 1 && i == 1) || (core == 2 && !i))
			continue;

		reg = (i == 0) ?
			BWN_NPHY_RFCTL_INTC1 : BWN_NPHY_RFCTL_INTC2;
		BWN_PHY_SET(mac, reg, 0x400);

		switch (intc_override) {
		case N_INTC_OVERRIDE_OFF:
			BWN_PHY_WRITE(mac, reg, 0);
			bwn_nphy_force_rf_sequence(mac, BWN_RFSEQ_RESET2RX);
			break;
		case N_INTC_OVERRIDE_TRSW:
			if (!i) {
				BWN_PHY_SETMASK(mac, BWN_NPHY_RFCTL_INTC1,
						0xFC3F, (value << 6));
				BWN_PHY_SETMASK(mac, BWN_NPHY_TXF_40CO_B1S1,
						0xFFFE, 1);
				BWN_PHY_SET(mac, BWN_NPHY_RFCTL_CMD,
						BWN_NPHY_RFCTL_CMD_START);
				for (j = 0; j < 100; j++) {
					if (!(BWN_PHY_READ(mac, BWN_NPHY_RFCTL_CMD) & BWN_NPHY_RFCTL_CMD_START)) {
						j = 0;
						break;
					}
					DELAY(10);
				}
				if (j)
					BWN_ERRPRINTF(mac->mac_sc,
						"intc override timeout\n");
				BWN_PHY_MASK(mac, BWN_NPHY_TXF_40CO_B1S1,
						0xFFFE);
			} else {
				BWN_PHY_SETMASK(mac, BWN_NPHY_RFCTL_INTC2,
						0xFC3F, (value << 6));
				BWN_PHY_SETMASK(mac, BWN_NPHY_RFCTL_OVER,
						0xFFFE, 1);
				BWN_PHY_SET(mac, BWN_NPHY_RFCTL_CMD,
						BWN_NPHY_RFCTL_CMD_RXTX);
				for (j = 0; j < 100; j++) {
					if (!(BWN_PHY_READ(mac, BWN_NPHY_RFCTL_CMD) & BWN_NPHY_RFCTL_CMD_RXTX)) {
						j = 0;
						break;
					}
					DELAY(10);
				}
				if (j)
					BWN_ERRPRINTF(mac->mac_sc,
						"intc override timeout\n");
				BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_OVER,
						0xFFFE);
			}
			break;
		case N_INTC_OVERRIDE_PA:
			if (bwn_current_band(mac) == BWN_BAND_5G) {
				tmp = 0x0020;
				val = value << 5;
			} else {
				tmp = 0x0010;
				val = value << 4;
			}
			BWN_PHY_SETMASK(mac, reg, ~tmp, val);
			break;
		case N_INTC_OVERRIDE_EXT_LNA_PU:
			if (bwn_current_band(mac) == BWN_BAND_5G) {
				tmp = 0x0001;
				val = value;
			} else {
				tmp = 0x0004;
				val = value << 2;
			}
			BWN_PHY_SETMASK(mac, reg, ~tmp, val);
			break;
		case N_INTC_OVERRIDE_EXT_LNA_GAIN:
			if (bwn_current_band(mac) == BWN_BAND_5G) {
				tmp = 0x0002;
				val = value << 1;
			} else {
				tmp = 0x0008;
				val = value << 3;
			}
			BWN_PHY_SETMASK(mac, reg, ~tmp, val);
			break;
		}
	}
}

/**************************************************
 * Various PHY ops
 **************************************************/

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/clip-detection */
static void bwn_nphy_write_clip_detection(struct bwn_mac *mac,
					  const uint16_t *clip_st)
{
	BWN_PHY_WRITE(mac, BWN_NPHY_C1_CLIP1THRES, clip_st[0]);
	BWN_PHY_WRITE(mac, BWN_NPHY_C2_CLIP1THRES, clip_st[1]);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/clip-detection */
static void bwn_nphy_read_clip_detection(struct bwn_mac *mac, uint16_t *clip_st)
{
	clip_st[0] = BWN_PHY_READ(mac, BWN_NPHY_C1_CLIP1THRES);
	clip_st[1] = BWN_PHY_READ(mac, BWN_NPHY_C2_CLIP1THRES);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/classifier */
static uint16_t bwn_nphy_classifier(struct bwn_mac *mac, uint16_t mask, uint16_t val)
{
	struct bwn_softc *sc = mac->mac_sc;
	uint16_t tmp;

	if (bhnd_get_hwrev(sc->sc_dev) == 16)
		bwn_mac_suspend(mac);

	tmp = BWN_PHY_READ(mac, BWN_NPHY_CLASSCTL);
	tmp &= (BWN_NPHY_CLASSCTL_CCKEN | BWN_NPHY_CLASSCTL_OFDMEN |
		BWN_NPHY_CLASSCTL_WAITEDEN);
	tmp &= ~mask;
	tmp |= (val & mask);
	BWN_PHY_SETMASK(mac, BWN_NPHY_CLASSCTL, 0xFFF8, tmp);

	if (bhnd_get_hwrev(sc->sc_dev) == 16)
		bwn_mac_enable(mac);

	return tmp;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/CCA */
static void bwn_nphy_reset_cca(struct bwn_mac *mac)
{
	uint16_t bbcfg;

	bwn_phy_force_clock(mac, 1);
	bbcfg = BWN_PHY_READ(mac, BWN_NPHY_BBCFG);
	BWN_PHY_WRITE(mac, BWN_NPHY_BBCFG, bbcfg | BWN_NPHY_BBCFG_RSTCCA);
	DELAY(1);
	BWN_PHY_WRITE(mac, BWN_NPHY_BBCFG, bbcfg & ~BWN_NPHY_BBCFG_RSTCCA);
	bwn_phy_force_clock(mac, 0);
	bwn_nphy_force_rf_sequence(mac, BWN_RFSEQ_RESET2RX);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/carriersearch */
static void bwn_nphy_stay_in_carrier_search(struct bwn_mac *mac, bool enable)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = phy->phy_n;

	if (enable) {
		static const uint16_t clip[] = { 0xFFFF, 0xFFFF };
		if (nphy->deaf_count++ == 0) {
			nphy->classifier_state = bwn_nphy_classifier(mac, 0, 0);
			bwn_nphy_classifier(mac, 0x7,
					    BWN_NPHY_CLASSCTL_WAITEDEN);
			bwn_nphy_read_clip_detection(mac, nphy->clip_state);
			bwn_nphy_write_clip_detection(mac, clip);
		}
		bwn_nphy_reset_cca(mac);
	} else {
		if (--nphy->deaf_count == 0) {
			bwn_nphy_classifier(mac, 0x7, nphy->classifier_state);
			bwn_nphy_write_clip_detection(mac, nphy->clip_state);
		}
	}
}

/* http://bcm-v4.sipsolutions.net/PHY/N/Read_Lpf_Bw_Ctl */
static uint16_t bwn_nphy_read_lpf_ctl(struct bwn_mac *mac, uint16_t offset)
{
	if (!offset)
		offset = bwn_is_40mhz(mac) ? 0x159 : 0x154;
	return bwn_ntab_read(mac, BWN_NTAB16(7, offset)) & 0x7;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/AdjustLnaGainTbl */
static void bwn_nphy_adjust_lna_gain_table(struct bwn_mac *mac)
{
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;

	uint8_t i;
	int16_t tmp;
	uint16_t data[4];
	int16_t gain[2];
	uint16_t minmax[2];
	static const uint16_t lna_gain[4] = { -2, 10, 19, 25 };

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 1);

	if (nphy->gain_boost) {
		if (bwn_current_band(mac) == BWN_BAND_2G) {
			gain[0] = 6;
			gain[1] = 6;
		} else {
			tmp = 40370 - 315 * bwn_get_chan(mac);
			gain[0] = ((tmp >> 13) + ((tmp >> 12) & 1));
			tmp = 23242 - 224 * bwn_get_chan(mac);
			gain[1] = ((tmp >> 13) + ((tmp >> 12) & 1));
		}
	} else {
		gain[0] = 0;
		gain[1] = 0;
	}

	for (i = 0; i < 2; i++) {
		if (nphy->elna_gain_config) {
			data[0] = 19 + gain[i];
			data[1] = 25 + gain[i];
			data[2] = 25 + gain[i];
			data[3] = 25 + gain[i];
		} else {
			data[0] = lna_gain[0] + gain[i];
			data[1] = lna_gain[1] + gain[i];
			data[2] = lna_gain[2] + gain[i];
			data[3] = lna_gain[3] + gain[i];
		}
		bwn_ntab_write_bulk(mac, BWN_NTAB16(i, 8), 4, data);

		minmax[i] = 23 + gain[i];
	}

	BWN_PHY_SETMASK(mac, BWN_NPHY_C1_MINMAX_GAIN, ~BWN_NPHY_C1_MINGAIN,
				minmax[0] << BWN_NPHY_C1_MINGAIN_SHIFT);
	BWN_PHY_SETMASK(mac, BWN_NPHY_C2_MINMAX_GAIN, ~BWN_NPHY_C2_MINGAIN,
				minmax[1] << BWN_NPHY_C2_MINGAIN_SHIFT);

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 0);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/SetRfSeq */
static void bwn_nphy_set_rf_sequence(struct bwn_mac *mac, uint8_t cmd,
					uint8_t *events, uint8_t *delays, uint8_t length)
{
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	uint8_t i;
	uint8_t end = (mac->mac_phy.rev >= 3) ? 0x1F : 0x0F;
	uint16_t offset1 = cmd << 4;
	uint16_t offset2 = offset1 + 0x80;

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, true);

	bwn_ntab_write_bulk(mac, BWN_NTAB8(7, offset1), length, events);
	bwn_ntab_write_bulk(mac, BWN_NTAB8(7, offset2), length, delays);

	for (i = length; i < 16; i++) {
		bwn_ntab_write(mac, BWN_NTAB8(7, offset1 + i), end);
		bwn_ntab_write(mac, BWN_NTAB8(7, offset2 + i), 1);
	}

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, false);
}

/**************************************************
 * Radio 0x2057
 **************************************************/

static void bwn_radio_2057_chantab_upload(struct bwn_mac *mac,
					  const struct bwn_nphy_chantabent_rev7 *e_r7,
					  const struct bwn_nphy_chantabent_rev7_2g *e_r7_2g)
{
	if (e_r7_2g) {
		BWN_RF_WRITE(mac, R2057_VCOCAL_COUNTVAL0, e_r7_2g->radio_vcocal_countval0);
		BWN_RF_WRITE(mac, R2057_VCOCAL_COUNTVAL1, e_r7_2g->radio_vcocal_countval1);
		BWN_RF_WRITE(mac, R2057_RFPLL_REFMASTER_SPAREXTALSIZE, e_r7_2g->radio_rfpll_refmaster_sparextalsize);
		BWN_RF_WRITE(mac, R2057_RFPLL_LOOPFILTER_R1, e_r7_2g->radio_rfpll_loopfilter_r1);
		BWN_RF_WRITE(mac, R2057_RFPLL_LOOPFILTER_C2, e_r7_2g->radio_rfpll_loopfilter_c2);
		BWN_RF_WRITE(mac, R2057_RFPLL_LOOPFILTER_C1, e_r7_2g->radio_rfpll_loopfilter_c1);
		BWN_RF_WRITE(mac, R2057_CP_KPD_IDAC, e_r7_2g->radio_cp_kpd_idac);
		BWN_RF_WRITE(mac, R2057_RFPLL_MMD0, e_r7_2g->radio_rfpll_mmd0);
		BWN_RF_WRITE(mac, R2057_RFPLL_MMD1, e_r7_2g->radio_rfpll_mmd1);
		BWN_RF_WRITE(mac, R2057_VCOBUF_TUNE, e_r7_2g->radio_vcobuf_tune);
		BWN_RF_WRITE(mac, R2057_LOGEN_MX2G_TUNE, e_r7_2g->radio_logen_mx2g_tune);
		BWN_RF_WRITE(mac, R2057_LOGEN_INDBUF2G_TUNE, e_r7_2g->radio_logen_indbuf2g_tune);
		BWN_RF_WRITE(mac, R2057_TXMIX2G_TUNE_BOOST_PU_CORE0, e_r7_2g->radio_txmix2g_tune_boost_pu_core0);
		BWN_RF_WRITE(mac, R2057_PAD2G_TUNE_PUS_CORE0, e_r7_2g->radio_pad2g_tune_pus_core0);
		BWN_RF_WRITE(mac, R2057_LNA2G_TUNE_CORE0, e_r7_2g->radio_lna2g_tune_core0);
		BWN_RF_WRITE(mac, R2057_TXMIX2G_TUNE_BOOST_PU_CORE1, e_r7_2g->radio_txmix2g_tune_boost_pu_core1);
		BWN_RF_WRITE(mac, R2057_PAD2G_TUNE_PUS_CORE1, e_r7_2g->radio_pad2g_tune_pus_core1);
		BWN_RF_WRITE(mac, R2057_LNA2G_TUNE_CORE1, e_r7_2g->radio_lna2g_tune_core1);

	} else {
		BWN_RF_WRITE(mac, R2057_VCOCAL_COUNTVAL0, e_r7->radio_vcocal_countval0);
		BWN_RF_WRITE(mac, R2057_VCOCAL_COUNTVAL1, e_r7->radio_vcocal_countval1);
		BWN_RF_WRITE(mac, R2057_RFPLL_REFMASTER_SPAREXTALSIZE, e_r7->radio_rfpll_refmaster_sparextalsize);
		BWN_RF_WRITE(mac, R2057_RFPLL_LOOPFILTER_R1, e_r7->radio_rfpll_loopfilter_r1);
		BWN_RF_WRITE(mac, R2057_RFPLL_LOOPFILTER_C2, e_r7->radio_rfpll_loopfilter_c2);
		BWN_RF_WRITE(mac, R2057_RFPLL_LOOPFILTER_C1, e_r7->radio_rfpll_loopfilter_c1);
		BWN_RF_WRITE(mac, R2057_CP_KPD_IDAC, e_r7->radio_cp_kpd_idac);
		BWN_RF_WRITE(mac, R2057_RFPLL_MMD0, e_r7->radio_rfpll_mmd0);
		BWN_RF_WRITE(mac, R2057_RFPLL_MMD1, e_r7->radio_rfpll_mmd1);
		BWN_RF_WRITE(mac, R2057_VCOBUF_TUNE, e_r7->radio_vcobuf_tune);
		BWN_RF_WRITE(mac, R2057_LOGEN_MX2G_TUNE, e_r7->radio_logen_mx2g_tune);
		BWN_RF_WRITE(mac, R2057_LOGEN_MX5G_TUNE, e_r7->radio_logen_mx5g_tune);
		BWN_RF_WRITE(mac, R2057_LOGEN_INDBUF2G_TUNE, e_r7->radio_logen_indbuf2g_tune);
		BWN_RF_WRITE(mac, R2057_LOGEN_INDBUF5G_TUNE, e_r7->radio_logen_indbuf5g_tune);
		BWN_RF_WRITE(mac, R2057_TXMIX2G_TUNE_BOOST_PU_CORE0, e_r7->radio_txmix2g_tune_boost_pu_core0);
		BWN_RF_WRITE(mac, R2057_PAD2G_TUNE_PUS_CORE0, e_r7->radio_pad2g_tune_pus_core0);
		BWN_RF_WRITE(mac, R2057_PGA_BOOST_TUNE_CORE0, e_r7->radio_pga_boost_tune_core0);
		BWN_RF_WRITE(mac, R2057_TXMIX5G_BOOST_TUNE_CORE0, e_r7->radio_txmix5g_boost_tune_core0);
		BWN_RF_WRITE(mac, R2057_PAD5G_TUNE_MISC_PUS_CORE0, e_r7->radio_pad5g_tune_misc_pus_core0);
		BWN_RF_WRITE(mac, R2057_LNA2G_TUNE_CORE0, e_r7->radio_lna2g_tune_core0);
		BWN_RF_WRITE(mac, R2057_LNA5G_TUNE_CORE0, e_r7->radio_lna5g_tune_core0);
		BWN_RF_WRITE(mac, R2057_TXMIX2G_TUNE_BOOST_PU_CORE1, e_r7->radio_txmix2g_tune_boost_pu_core1);
		BWN_RF_WRITE(mac, R2057_PAD2G_TUNE_PUS_CORE1, e_r7->radio_pad2g_tune_pus_core1);
		BWN_RF_WRITE(mac, R2057_PGA_BOOST_TUNE_CORE1, e_r7->radio_pga_boost_tune_core1);
		BWN_RF_WRITE(mac, R2057_TXMIX5G_BOOST_TUNE_CORE1, e_r7->radio_txmix5g_boost_tune_core1);
		BWN_RF_WRITE(mac, R2057_PAD5G_TUNE_MISC_PUS_CORE1, e_r7->radio_pad5g_tune_misc_pus_core1);
		BWN_RF_WRITE(mac, R2057_LNA2G_TUNE_CORE1, e_r7->radio_lna2g_tune_core1);
		BWN_RF_WRITE(mac, R2057_LNA5G_TUNE_CORE1, e_r7->radio_lna5g_tune_core1);
	}
}

static void bwn_radio_2057_setup(struct bwn_mac *mac,
				 const struct bwn_nphy_chantabent_rev7 *tabent_r7,
				 const struct bwn_nphy_chantabent_rev7_2g *tabent_r7_2g)
{
	struct bwn_phy *phy = &mac->mac_phy;

	bwn_radio_2057_chantab_upload(mac, tabent_r7, tabent_r7_2g);

	switch (phy->rf_rev) {
	case 0 ... 4:
	case 6:
		if (bwn_current_band(mac) == BWN_BAND_2G) {
			BWN_RF_WRITE(mac, R2057_RFPLL_LOOPFILTER_R1, 0x3f);
			BWN_RF_WRITE(mac, R2057_CP_KPD_IDAC, 0x3f);
			BWN_RF_WRITE(mac, R2057_RFPLL_LOOPFILTER_C1, 0x8);
			BWN_RF_WRITE(mac, R2057_RFPLL_LOOPFILTER_C2, 0x8);
		} else {
			BWN_RF_WRITE(mac, R2057_RFPLL_LOOPFILTER_R1, 0x1f);
			BWN_RF_WRITE(mac, R2057_CP_KPD_IDAC, 0x3f);
			BWN_RF_WRITE(mac, R2057_RFPLL_LOOPFILTER_C1, 0x8);
			BWN_RF_WRITE(mac, R2057_RFPLL_LOOPFILTER_C2, 0x8);
		}
		break;
	case 9: /* e.g. PHY rev 16 */
		BWN_RF_WRITE(mac, R2057_LOGEN_PTAT_RESETS, 0x20);
		BWN_RF_WRITE(mac, R2057_VCOBUF_IDACS, 0x18);
		if (bwn_current_band(mac) == BWN_BAND_5G) {
			BWN_RF_WRITE(mac, R2057_LOGEN_PTAT_RESETS, 0x38);
			BWN_RF_WRITE(mac, R2057_VCOBUF_IDACS, 0x0f);

			if (bwn_is_40mhz(mac)) {
				/* TODO */
			} else {
				BWN_RF_WRITE(mac,
						R2057_PAD_BIAS_FILTER_BWS_CORE0,
						0x3c);
				BWN_RF_WRITE(mac,
						R2057_PAD_BIAS_FILTER_BWS_CORE1,
						0x3c);
			}
		}
		break;
	case 14: /* 2 GHz only */
		BWN_RF_WRITE(mac, R2057_RFPLL_LOOPFILTER_R1, 0x1b);
		BWN_RF_WRITE(mac, R2057_CP_KPD_IDAC, 0x3f);
		BWN_RF_WRITE(mac, R2057_RFPLL_LOOPFILTER_C1, 0x1f);
		BWN_RF_WRITE(mac, R2057_RFPLL_LOOPFILTER_C2, 0x1f);
		break;
	}

	if (bwn_current_band(mac) == BWN_BAND_2G) {
		uint16_t txmix2g_tune_boost_pu = 0;
		uint16_t pad2g_tune_pus = 0;

		if (bwn_nphy_ipa(mac)) {
			switch (phy->rf_rev) {
			case 9:
				txmix2g_tune_boost_pu = 0x0041;
				/* TODO */
				break;
			case 14:
				txmix2g_tune_boost_pu = 0x21;
				pad2g_tune_pus = 0x23;
				break;
			}
		}

		if (txmix2g_tune_boost_pu)
			BWN_RF_WRITE(mac, R2057_TXMIX2G_TUNE_BOOST_PU_CORE0,
					txmix2g_tune_boost_pu);
		if (pad2g_tune_pus)
			BWN_RF_WRITE(mac, R2057_PAD2G_TUNE_PUS_CORE0,
					pad2g_tune_pus);
		if (txmix2g_tune_boost_pu)
			BWN_RF_WRITE(mac, R2057_TXMIX2G_TUNE_BOOST_PU_CORE1,
					txmix2g_tune_boost_pu);
		if (pad2g_tune_pus)
			BWN_RF_WRITE(mac, R2057_PAD2G_TUNE_PUS_CORE1,
					pad2g_tune_pus);
	}

	/* 50..100 */
	DELAY(100);

	/* VCO calibration */
	BWN_RF_MASK(mac, R2057_RFPLL_MISC_EN, ~0x01);
	BWN_RF_MASK(mac, R2057_RFPLL_MISC_CAL_RESETN, ~0x04);
	BWN_RF_SET(mac, R2057_RFPLL_MISC_CAL_RESETN, 0x4);
	BWN_RF_SET(mac, R2057_RFPLL_MISC_EN, 0x01);
	/* 300..600 */
	DELAY(600);
}

/* Calibrate resistors in LPF of PLL?
 * http://bcm-v4.sipsolutions.net/PHY/radio205x_rcal
 */
static uint8_t bwn_radio_2057_rcal(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	uint16_t saved_regs_phy[12];
	uint16_t saved_regs_phy_rf[6];
	uint16_t saved_regs_radio[2] = { };
	static const uint16_t phy_to_store[] = {
		BWN_NPHY_RFCTL_RSSIO1, BWN_NPHY_RFCTL_RSSIO2,
		BWN_NPHY_RFCTL_LUT_TRSW_LO1, BWN_NPHY_RFCTL_LUT_TRSW_LO2,
		BWN_NPHY_RFCTL_RXG1, BWN_NPHY_RFCTL_RXG2,
		BWN_NPHY_RFCTL_TXG1, BWN_NPHY_RFCTL_TXG2,
		BWN_NPHY_REV7_RF_CTL_MISC_REG3, BWN_NPHY_REV7_RF_CTL_MISC_REG4,
		BWN_NPHY_REV7_RF_CTL_MISC_REG5, BWN_NPHY_REV7_RF_CTL_MISC_REG6,
	};
	static const uint16_t phy_to_store_rf[] = {
		BWN_NPHY_REV3_RFCTL_OVER0, BWN_NPHY_REV3_RFCTL_OVER1,
		BWN_NPHY_REV7_RF_CTL_OVER3, BWN_NPHY_REV7_RF_CTL_OVER4,
		BWN_NPHY_REV7_RF_CTL_OVER5, BWN_NPHY_REV7_RF_CTL_OVER6,
	};
	uint16_t tmp;
	int i;

	/* Save */
	for (i = 0; i < nitems(phy_to_store); i++)
		saved_regs_phy[i] = BWN_PHY_READ(mac, phy_to_store[i]);
	for (i = 0; i < nitems(phy_to_store_rf); i++)
		saved_regs_phy_rf[i] = BWN_PHY_READ(mac, phy_to_store_rf[i]);

	/* Set */
	for (i = 0; i < nitems(phy_to_store); i++)
		BWN_PHY_WRITE(mac, phy_to_store[i], 0);
	BWN_PHY_WRITE(mac, BWN_NPHY_REV3_RFCTL_OVER0, 0x07ff);
	BWN_PHY_WRITE(mac, BWN_NPHY_REV3_RFCTL_OVER1, 0x07ff);
	BWN_PHY_WRITE(mac, BWN_NPHY_REV7_RF_CTL_OVER3, 0x07ff);
	BWN_PHY_WRITE(mac, BWN_NPHY_REV7_RF_CTL_OVER4, 0x07ff);
	BWN_PHY_WRITE(mac, BWN_NPHY_REV7_RF_CTL_OVER5, 0x007f);
	BWN_PHY_WRITE(mac, BWN_NPHY_REV7_RF_CTL_OVER6, 0x007f);

	switch (phy->rf_rev) {
	case 5:
		BWN_PHY_MASK(mac, BWN_NPHY_REV7_RF_CTL_OVER3, ~0x2);
		DELAY(10);
		BWN_RF_SET(mac, R2057_IQTEST_SEL_PU, 0x1);
		BWN_RF_SETMASK(mac, R2057v7_IQTEST_SEL_PU2, ~0x2, 0x1);
		break;
	case 9:
		BWN_PHY_SET(mac, BWN_NPHY_REV7_RF_CTL_OVER3, 0x2);
		BWN_PHY_SET(mac, BWN_NPHY_REV7_RF_CTL_MISC_REG3, 0x2);
		saved_regs_radio[0] = BWN_RF_READ(mac, R2057_IQTEST_SEL_PU);
		BWN_RF_WRITE(mac, R2057_IQTEST_SEL_PU, 0x11);
		break;
	case 14:
		saved_regs_radio[0] = BWN_RF_READ(mac, R2057_IQTEST_SEL_PU);
		saved_regs_radio[1] = BWN_RF_READ(mac, R2057v7_IQTEST_SEL_PU2);
		BWN_PHY_SET(mac, BWN_NPHY_REV7_RF_CTL_MISC_REG3, 0x2);
		BWN_PHY_SET(mac, BWN_NPHY_REV7_RF_CTL_OVER3, 0x2);
		BWN_RF_WRITE(mac, R2057v7_IQTEST_SEL_PU2, 0x2);
		BWN_RF_WRITE(mac, R2057_IQTEST_SEL_PU, 0x1);
		break;
	}

	/* Enable */
	BWN_RF_SET(mac, R2057_RCAL_CONFIG, 0x1);
	DELAY(10);

	/* Start */
	BWN_RF_SET(mac, R2057_RCAL_CONFIG, 0x2);
	/* 100..200 */
	DELAY(200);

	/* Stop */
	BWN_RF_MASK(mac, R2057_RCAL_CONFIG, ~0x2);

	/* Wait and check for result */
	if (!bwn_radio_wait_value(mac, R2057_RCAL_STATUS, 1, 1, 100, 1000000)) {
		BWN_ERRPRINTF(mac->mac_sc, "Radio 0x2057 rcal timeout\n");
		return 0;
	}
	tmp = BWN_RF_READ(mac, R2057_RCAL_STATUS) & 0x3E;

	/* Disable */
	BWN_RF_MASK(mac, R2057_RCAL_CONFIG, ~0x1);

	/* Restore */
	for (i = 0; i < nitems(phy_to_store_rf); i++)
		BWN_PHY_WRITE(mac, phy_to_store_rf[i], saved_regs_phy_rf[i]);
	for (i = 0; i < nitems(phy_to_store); i++)
		BWN_PHY_WRITE(mac, phy_to_store[i], saved_regs_phy[i]);

	switch (phy->rf_rev) {
	case 0 ... 4:
	case 6:
		BWN_RF_SETMASK(mac, R2057_TEMPSENSE_CONFIG, ~0x3C, tmp);
		BWN_RF_SETMASK(mac, R2057_BANDGAP_RCAL_TRIM, ~0xF0,
				  tmp << 2);
		break;
	case 5:
		BWN_RF_MASK(mac, R2057_IPA2G_CASCONV_CORE0, ~0x1);
		BWN_RF_MASK(mac, R2057v7_IQTEST_SEL_PU2, ~0x2);
		break;
	case 9:
		BWN_RF_WRITE(mac, R2057_IQTEST_SEL_PU, saved_regs_radio[0]);
		break;
	case 14:
		BWN_RF_WRITE(mac, R2057_IQTEST_SEL_PU, saved_regs_radio[0]);
		BWN_RF_WRITE(mac, R2057v7_IQTEST_SEL_PU2, saved_regs_radio[1]);
		break;
	}

	return tmp & 0x3e;
}

/* Calibrate the internal RC oscillator?
 * http://bcm-v4.sipsolutions.net/PHY/radio2057_rccal
 */
static uint16_t bwn_radio_2057_rccal(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	bool special = (phy->rf_rev == 3 || phy->rf_rev == 4 ||
			phy->rf_rev == 6);
	uint16_t tmp;

	/* Setup cal */
	if (special) {
		BWN_RF_WRITE(mac, R2057_RCCAL_MASTER, 0x61);
		BWN_RF_WRITE(mac, R2057_RCCAL_TRC0, 0xC0);
	} else {
		BWN_RF_WRITE(mac, R2057v7_RCCAL_MASTER, 0x61);
		BWN_RF_WRITE(mac, R2057_RCCAL_TRC0, 0xE9);
	}
	BWN_RF_WRITE(mac, R2057_RCCAL_X1, 0x6E);

	/* Start, wait, stop */
	BWN_RF_WRITE(mac, R2057_RCCAL_START_R1_Q1_P1, 0x55);
	if (!bwn_radio_wait_value(mac, R2057_RCCAL_DONE_OSCCAP, 2, 2, 500,
				  5000000))
		BWN_DBGPRINTF(mac, "Radio 0x2057 rccal timeout\n");
	/* 35..70 */
	DELAY(70);
	BWN_RF_WRITE(mac, R2057_RCCAL_START_R1_Q1_P1, 0x15);
	/* 70..140 */
	DELAY(140);

	/* Setup cal */
	if (special) {
		BWN_RF_WRITE(mac, R2057_RCCAL_MASTER, 0x69);
		BWN_RF_WRITE(mac, R2057_RCCAL_TRC0, 0xB0);
	} else {
		BWN_RF_WRITE(mac, R2057v7_RCCAL_MASTER, 0x69);
		BWN_RF_WRITE(mac, R2057_RCCAL_TRC0, 0xD5);
	}
	BWN_RF_WRITE(mac, R2057_RCCAL_X1, 0x6E);

	/* Start, wait, stop */
	/* 35..70 */
	DELAY(70);
	BWN_RF_WRITE(mac, R2057_RCCAL_START_R1_Q1_P1, 0x55);
	/* 70..140 */
	DELAY(140);
	if (!bwn_radio_wait_value(mac, R2057_RCCAL_DONE_OSCCAP, 2, 2, 500,
				  5000000))
		BWN_DBGPRINTF(mac, "Radio 0x2057 rccal timeout\n");
	/* 35..70 */
	DELAY(70);
	BWN_RF_WRITE(mac, R2057_RCCAL_START_R1_Q1_P1, 0x15);
	/* 70..140 */
	DELAY(140);

	/* Setup cal */
	if (special) {
		BWN_RF_WRITE(mac, R2057_RCCAL_MASTER, 0x73);
		BWN_RF_WRITE(mac, R2057_RCCAL_X1, 0x28);
		BWN_RF_WRITE(mac, R2057_RCCAL_TRC0, 0xB0);
	} else {
		BWN_RF_WRITE(mac, R2057v7_RCCAL_MASTER, 0x73);
		BWN_RF_WRITE(mac, R2057_RCCAL_X1, 0x6E);
		BWN_RF_WRITE(mac, R2057_RCCAL_TRC0, 0x99);
	}

	/* Start, wait, stop */
	/* 35..70 */
	DELAY(70);
	BWN_RF_WRITE(mac, R2057_RCCAL_START_R1_Q1_P1, 0x55);
	/* 70..140 */
	DELAY(140);
	if (!bwn_radio_wait_value(mac, R2057_RCCAL_DONE_OSCCAP, 2, 2, 500,
				  5000000)) {
		BWN_ERRPRINTF(mac->mac_sc, "Radio 0x2057 rcal timeout\n");
		return 0;
	}
	tmp = BWN_RF_READ(mac, R2057_RCCAL_DONE_OSCCAP);
	/* 35..70 */
	DELAY(70);
	BWN_RF_WRITE(mac, R2057_RCCAL_START_R1_Q1_P1, 0x15);
	/* 70..140 */
	DELAY(140);

	if (special)
		BWN_RF_MASK(mac, R2057_RCCAL_MASTER, ~0x1);
	else
		BWN_RF_MASK(mac, R2057v7_RCCAL_MASTER, ~0x1);

	return tmp;
}

static void bwn_radio_2057_init_pre(struct bwn_mac *mac)
{
	BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_CMD, ~BWN_NPHY_RFCTL_CMD_CHIP0PU);
	/* Maybe wl meant to reset and set (order?) RFCTL_CMD_OEPORFORCE? */
	BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_CMD, BWN_NPHY_RFCTL_CMD_OEPORFORCE);
	BWN_PHY_SET(mac, BWN_NPHY_RFCTL_CMD, ~BWN_NPHY_RFCTL_CMD_OEPORFORCE);
	BWN_PHY_SET(mac, BWN_NPHY_RFCTL_CMD, BWN_NPHY_RFCTL_CMD_CHIP0PU);
}

static void bwn_radio_2057_init_post(struct bwn_mac *mac)
{
	BWN_RF_SET(mac, R2057_XTALPUOVR_PINCTRL, 0x1);

	if (0) /* FIXME: Is this BCM43217 specific? */
		BWN_RF_SET(mac, R2057_XTALPUOVR_PINCTRL, 0x2);

	BWN_RF_SET(mac, R2057_RFPLL_MISC_CAL_RESETN, 0x78);
	BWN_RF_SET(mac, R2057_XTAL_CONFIG2, 0x80);
	DELAY(2000);
	BWN_RF_MASK(mac, R2057_RFPLL_MISC_CAL_RESETN, ~0x78);
	BWN_RF_MASK(mac, R2057_XTAL_CONFIG2, ~0x80);

	if (mac->mac_phy.phy_do_full_init) {
		bwn_radio_2057_rcal(mac);
		bwn_radio_2057_rccal(mac);
	}
	BWN_RF_MASK(mac, R2057_RFPLL_MASTER, ~0x8);
}

/* http://bcm-v4.sipsolutions.net/802.11/Radio/2057/Init */
static void bwn_radio_2057_init(struct bwn_mac *mac)
{
	bwn_radio_2057_init_pre(mac);
	r2057_upload_inittabs(mac);
	bwn_radio_2057_init_post(mac);
}

/**************************************************
 * Radio 0x2056
 **************************************************/

static void bwn_chantab_radio_2056_upload(struct bwn_mac *mac,
				const struct bwn_nphy_channeltab_entry_rev3 *e)
{
	BWN_RF_WRITE(mac, B2056_SYN_PLL_VCOCAL1, e->radio_syn_pll_vcocal1);
	BWN_RF_WRITE(mac, B2056_SYN_PLL_VCOCAL2, e->radio_syn_pll_vcocal2);
	BWN_RF_WRITE(mac, B2056_SYN_PLL_REFDIV, e->radio_syn_pll_refdiv);
	BWN_RF_WRITE(mac, B2056_SYN_PLL_MMD2, e->radio_syn_pll_mmd2);
	BWN_RF_WRITE(mac, B2056_SYN_PLL_MMD1, e->radio_syn_pll_mmd1);
	BWN_RF_WRITE(mac, B2056_SYN_PLL_LOOPFILTER1,
					e->radio_syn_pll_loopfilter1);
	BWN_RF_WRITE(mac, B2056_SYN_PLL_LOOPFILTER2,
					e->radio_syn_pll_loopfilter2);
	BWN_RF_WRITE(mac, B2056_SYN_PLL_LOOPFILTER3,
					e->radio_syn_pll_loopfilter3);
	BWN_RF_WRITE(mac, B2056_SYN_PLL_LOOPFILTER4,
					e->radio_syn_pll_loopfilter4);
	BWN_RF_WRITE(mac, B2056_SYN_PLL_LOOPFILTER5,
					e->radio_syn_pll_loopfilter5);
	BWN_RF_WRITE(mac, B2056_SYN_RESERVED_ADDR27,
					e->radio_syn_reserved_addr27);
	BWN_RF_WRITE(mac, B2056_SYN_RESERVED_ADDR28,
					e->radio_syn_reserved_addr28);
	BWN_RF_WRITE(mac, B2056_SYN_RESERVED_ADDR29,
					e->radio_syn_reserved_addr29);
	BWN_RF_WRITE(mac, B2056_SYN_LOGEN_VCOBUF1,
					e->radio_syn_logen_vcobuf1);
	BWN_RF_WRITE(mac, B2056_SYN_LOGEN_MIXER2, e->radio_syn_logen_mixer2);
	BWN_RF_WRITE(mac, B2056_SYN_LOGEN_BUF3, e->radio_syn_logen_buf3);
	BWN_RF_WRITE(mac, B2056_SYN_LOGEN_BUF4, e->radio_syn_logen_buf4);

	BWN_RF_WRITE(mac, B2056_RX0 | B2056_RX_LNAA_TUNE,
					e->radio_rx0_lnaa_tune);
	BWN_RF_WRITE(mac, B2056_RX0 | B2056_RX_LNAG_TUNE,
					e->radio_rx0_lnag_tune);

	BWN_RF_WRITE(mac, B2056_TX0 | B2056_TX_INTPAA_BOOST_TUNE,
					e->radio_tx0_intpaa_boost_tune);
	BWN_RF_WRITE(mac, B2056_TX0 | B2056_TX_INTPAG_BOOST_TUNE,
					e->radio_tx0_intpag_boost_tune);
	BWN_RF_WRITE(mac, B2056_TX0 | B2056_TX_PADA_BOOST_TUNE,
					e->radio_tx0_pada_boost_tune);
	BWN_RF_WRITE(mac, B2056_TX0 | B2056_TX_PADG_BOOST_TUNE,
					e->radio_tx0_padg_boost_tune);
	BWN_RF_WRITE(mac, B2056_TX0 | B2056_TX_PGAA_BOOST_TUNE,
					e->radio_tx0_pgaa_boost_tune);
	BWN_RF_WRITE(mac, B2056_TX0 | B2056_TX_PGAG_BOOST_TUNE,
					e->radio_tx0_pgag_boost_tune);
	BWN_RF_WRITE(mac, B2056_TX0 | B2056_TX_MIXA_BOOST_TUNE,
					e->radio_tx0_mixa_boost_tune);
	BWN_RF_WRITE(mac, B2056_TX0 | B2056_TX_MIXG_BOOST_TUNE,
					e->radio_tx0_mixg_boost_tune);

	BWN_RF_WRITE(mac, B2056_RX1 | B2056_RX_LNAA_TUNE,
					e->radio_rx1_lnaa_tune);
	BWN_RF_WRITE(mac, B2056_RX1 | B2056_RX_LNAG_TUNE,
					e->radio_rx1_lnag_tune);

	BWN_RF_WRITE(mac, B2056_TX1 | B2056_TX_INTPAA_BOOST_TUNE,
					e->radio_tx1_intpaa_boost_tune);
	BWN_RF_WRITE(mac, B2056_TX1 | B2056_TX_INTPAG_BOOST_TUNE,
					e->radio_tx1_intpag_boost_tune);
	BWN_RF_WRITE(mac, B2056_TX1 | B2056_TX_PADA_BOOST_TUNE,
					e->radio_tx1_pada_boost_tune);
	BWN_RF_WRITE(mac, B2056_TX1 | B2056_TX_PADG_BOOST_TUNE,
					e->radio_tx1_padg_boost_tune);
	BWN_RF_WRITE(mac, B2056_TX1 | B2056_TX_PGAA_BOOST_TUNE,
					e->radio_tx1_pgaa_boost_tune);
	BWN_RF_WRITE(mac, B2056_TX1 | B2056_TX_PGAG_BOOST_TUNE,
					e->radio_tx1_pgag_boost_tune);
	BWN_RF_WRITE(mac, B2056_TX1 | B2056_TX_MIXA_BOOST_TUNE,
					e->radio_tx1_mixa_boost_tune);
	BWN_RF_WRITE(mac, B2056_TX1 | B2056_TX_MIXG_BOOST_TUNE,
					e->radio_tx1_mixg_boost_tune);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/Radio/2056Setup */
static void bwn_radio_2056_setup(struct bwn_mac *mac,
				const struct bwn_nphy_channeltab_entry_rev3 *e)
{
	struct bwn_softc *sc = mac->mac_sc;
	bwn_band_t band = bwn_current_band(mac);
	uint16_t offset;
	uint8_t i;
	uint16_t bias, cbias;
	uint16_t pag_boost, padg_boost, pgag_boost, mixg_boost;
	uint16_t paa_boost, pada_boost, pgaa_boost, mixa_boost;
	bool is_pkg_fab_smic;

	DPRINTF(mac->mac_sc, BWN_DEBUG_RF, "%s: called\n", __func__);

	if (mac->mac_phy.rev < 3) {
		BWN_ERRPRINTF(mac->mac_sc, "%s: phy rev %d out of range\n",
		    __func__,
		    mac->mac_phy.rev);
	}

	is_pkg_fab_smic =
		((sc->sc_cid.chip_id == BHND_CHIPID_BCM43224 ||
		  sc->sc_cid.chip_id == BHND_CHIPID_BCM43225 ||
		  sc->sc_cid.chip_id == BHND_CHIPID_BCM43421) &&
		 sc->sc_cid.chip_pkg == BHND_PKGID_BCM43224_FAB_SMIC);

	bwn_chantab_radio_2056_upload(mac, e);
	b2056_upload_syn_pll_cp2(mac, band == BWN_BAND_5G);

	if (sc->sc_board_info.board_flags2 & BHND_BFL2_GPLL_WAR &&
	    bwn_current_band(mac) == BWN_BAND_2G) {
		BWN_RF_WRITE(mac, B2056_SYN_PLL_LOOPFILTER1, 0x1F);
		BWN_RF_WRITE(mac, B2056_SYN_PLL_LOOPFILTER2, 0x1F);
		if (sc->sc_cid.chip_id == BHND_CHIPID_BCM4716 ||
		    sc->sc_cid.chip_id == BHND_CHIPID_BCM47162) {
			BWN_RF_WRITE(mac, B2056_SYN_PLL_LOOPFILTER4, 0x14);
			BWN_RF_WRITE(mac, B2056_SYN_PLL_CP2, 0);
		} else {
			BWN_RF_WRITE(mac, B2056_SYN_PLL_LOOPFILTER4, 0x0B);
			BWN_RF_WRITE(mac, B2056_SYN_PLL_CP2, 0x14);
		}
	}
	if (sc->sc_board_info.board_flags2 & BHND_BFL2_GPLL_WAR &&
	    bwn_current_band(mac) == BWN_BAND_2G) {
		BWN_RF_WRITE(mac, B2056_SYN_PLL_LOOPFILTER1, 0x1f);
		BWN_RF_WRITE(mac, B2056_SYN_PLL_LOOPFILTER2, 0x1f);
		BWN_RF_WRITE(mac, B2056_SYN_PLL_LOOPFILTER4, 0x0b);
		BWN_RF_WRITE(mac, B2056_SYN_PLL_CP2, 0x20);
	}
	if (sc->sc_board_info.board_flags2 & BHND_BFL2_APLL_WAR &&
	    bwn_current_band(mac) == BWN_BAND_5G) {
		BWN_RF_WRITE(mac, B2056_SYN_PLL_LOOPFILTER1, 0x1F);
		BWN_RF_WRITE(mac, B2056_SYN_PLL_LOOPFILTER2, 0x1F);
		BWN_RF_WRITE(mac, B2056_SYN_PLL_LOOPFILTER4, 0x05);
		BWN_RF_WRITE(mac, B2056_SYN_PLL_CP2, 0x0C);
	}

	if (mac->mac_phy.phy_n->ipa2g_on && band == BWN_BAND_2G) {
		for (i = 0; i < 2; i++) {
			offset = i ? B2056_TX1 : B2056_TX0;
			if (mac->mac_phy.rev >= 5) {
				BWN_RF_WRITE(mac,
					offset | B2056_TX_PADG_IDAC, 0xcc);

				if (sc->sc_cid.chip_id == BHND_CHIPID_BCM4716 ||
				    sc->sc_cid.chip_id == BHND_CHIPID_BCM47162) {
					bias = 0x40;
					cbias = 0x45;
					pag_boost = 0x5;
					pgag_boost = 0x33;
					mixg_boost = 0x55;
				} else {
					bias = 0x25;
					cbias = 0x20;
					if (is_pkg_fab_smic) {
						bias = 0x2a;
						cbias = 0x38;
					}
					pag_boost = 0x4;
					pgag_boost = 0x03;
					mixg_boost = 0x65;
				}
				padg_boost = 0x77;

				BWN_RF_WRITE(mac,
					offset | B2056_TX_INTPAG_IMAIN_STAT,
					bias);
				BWN_RF_WRITE(mac,
					offset | B2056_TX_INTPAG_IAUX_STAT,
					bias);
				BWN_RF_WRITE(mac,
					offset | B2056_TX_INTPAG_CASCBIAS,
					cbias);
				BWN_RF_WRITE(mac,
					offset | B2056_TX_INTPAG_BOOST_TUNE,
					pag_boost);
				BWN_RF_WRITE(mac,
					offset | B2056_TX_PGAG_BOOST_TUNE,
					pgag_boost);
				BWN_RF_WRITE(mac,
					offset | B2056_TX_PADG_BOOST_TUNE,
					padg_boost);
				BWN_RF_WRITE(mac,
					offset | B2056_TX_MIXG_BOOST_TUNE,
					mixg_boost);
			} else {
				bias = bwn_is_40mhz(mac) ? 0x40 : 0x20;
				BWN_RF_WRITE(mac,
					offset | B2056_TX_INTPAG_IMAIN_STAT,
					bias);
				BWN_RF_WRITE(mac,
					offset | B2056_TX_INTPAG_IAUX_STAT,
					bias);
				BWN_RF_WRITE(mac,
					offset | B2056_TX_INTPAG_CASCBIAS,
					0x30);
			}
			BWN_RF_WRITE(mac, offset | B2056_TX_PA_SPARE1, 0xee);
		}
	} else if (mac->mac_phy.phy_n->ipa5g_on && band == BWN_BAND_5G) {
		uint16_t freq = bwn_get_centre_freq(mac);
		/* XXX 5g low/med/high? */
		if (freq < 5100) {
			paa_boost = 0xA;
			pada_boost = 0x77;
			pgaa_boost = 0xF;
			mixa_boost = 0xF;
		} else if (freq < 5340) {
			paa_boost = 0x8;
			pada_boost = 0x77;
			pgaa_boost = 0xFB;
			mixa_boost = 0xF;
		} else if (freq < 5650) {
			paa_boost = 0x0;
			pada_boost = 0x77;
			pgaa_boost = 0xB;
			mixa_boost = 0xF;
		} else {
			paa_boost = 0x0;
			pada_boost = 0x77;
			if (freq != 5825)
				pgaa_boost = -(freq - 18) / 36 + 168;
			else
				pgaa_boost = 6;
			mixa_boost = 0xF;
		}

		cbias = is_pkg_fab_smic ? 0x35 : 0x30;

		for (i = 0; i < 2; i++) {
			offset = i ? B2056_TX1 : B2056_TX0;

			BWN_RF_WRITE(mac,
				offset | B2056_TX_INTPAA_BOOST_TUNE, paa_boost);
			BWN_RF_WRITE(mac,
				offset | B2056_TX_PADA_BOOST_TUNE, pada_boost);
			BWN_RF_WRITE(mac,
				offset | B2056_TX_PGAA_BOOST_TUNE, pgaa_boost);
			BWN_RF_WRITE(mac,
				offset | B2056_TX_MIXA_BOOST_TUNE, mixa_boost);
			BWN_RF_WRITE(mac,
				offset | B2056_TX_TXSPARE1, 0x30);
			BWN_RF_WRITE(mac,
				offset | B2056_TX_PA_SPARE2, 0xee);
			BWN_RF_WRITE(mac,
				offset | B2056_TX_PADA_CASCBIAS, 0x03);
			BWN_RF_WRITE(mac,
				offset | B2056_TX_INTPAA_IAUX_STAT, 0x30);
			BWN_RF_WRITE(mac,
				offset | B2056_TX_INTPAA_IMAIN_STAT, 0x30);
			BWN_RF_WRITE(mac,
				offset | B2056_TX_INTPAA_CASCBIAS, cbias);
		}
	}

	DELAY(50);
	/* VCO calibration */
	BWN_RF_WRITE(mac, B2056_SYN_PLL_VCOCAL12, 0x00);
	BWN_RF_WRITE(mac, B2056_TX_INTPAA_PA_MISC, 0x38);
	BWN_RF_WRITE(mac, B2056_TX_INTPAA_PA_MISC, 0x18);
	BWN_RF_WRITE(mac, B2056_TX_INTPAA_PA_MISC, 0x38);
	BWN_RF_WRITE(mac, B2056_TX_INTPAA_PA_MISC, 0x39);
	DELAY(300);
}

static uint8_t bwn_radio_2056_rcal(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	uint16_t mast2, tmp;

	if (phy->rev != 3)
		return 0;
	DPRINTF(mac->mac_sc, BWN_DEBUG_RF, "%s: called\n", __func__);

	mast2 = BWN_RF_READ(mac, B2056_SYN_PLL_MAST2);
	BWN_RF_WRITE(mac, B2056_SYN_PLL_MAST2, mast2 | 0x7);

	DELAY(10);
	BWN_RF_WRITE(mac, B2056_SYN_RCAL_MASTER, 0x01);
	DELAY(10);
	BWN_RF_WRITE(mac, B2056_SYN_RCAL_MASTER, 0x09);

	if (!bwn_radio_wait_value(mac, B2056_SYN_RCAL_CODE_OUT, 0x80, 0x80, 100,
				  1000000)) {
		BWN_ERRPRINTF(mac->mac_sc, "Radio recalibration timeout\n");
		return 0;
	}

	BWN_RF_WRITE(mac, B2056_SYN_RCAL_MASTER, 0x01);
	tmp = BWN_RF_READ(mac, B2056_SYN_RCAL_CODE_OUT);
	BWN_RF_WRITE(mac, B2056_SYN_RCAL_MASTER, 0x00);

	BWN_RF_WRITE(mac, B2056_SYN_PLL_MAST2, mast2);

	return tmp & 0x1f;
}

static void bwn_radio_init2056_pre(struct bwn_mac *mac)
{
	DPRINTF(mac->mac_sc, BWN_DEBUG_RF, "%s: called\n", __func__);

	BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_CMD,
		     ~BWN_NPHY_RFCTL_CMD_CHIP0PU);
	/* Maybe wl meant to reset and set (order?) RFCTL_CMD_OEPORFORCE? */
	BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_CMD,
		     BWN_NPHY_RFCTL_CMD_OEPORFORCE);
	BWN_PHY_SET(mac, BWN_NPHY_RFCTL_CMD,
		    ~BWN_NPHY_RFCTL_CMD_OEPORFORCE);
	BWN_PHY_SET(mac, BWN_NPHY_RFCTL_CMD,
		    BWN_NPHY_RFCTL_CMD_CHIP0PU);
}

static void bwn_radio_init2056_post(struct bwn_mac *mac)
{
	DPRINTF(mac->mac_sc, BWN_DEBUG_RF, "%s: called\n", __func__);

	BWN_RF_SET(mac, B2056_SYN_COM_CTRL, 0xB);
	BWN_RF_SET(mac, B2056_SYN_COM_PU, 0x2);
	BWN_RF_SET(mac, B2056_SYN_COM_RESET, 0x2);
	DELAY(1000);
	BWN_RF_MASK(mac, B2056_SYN_COM_RESET, ~0x2);
	BWN_RF_MASK(mac, B2056_SYN_PLL_MAST2, ~0xFC);
	BWN_RF_MASK(mac, B2056_SYN_RCCAL_CTRL0, ~0x1);
	if (mac->mac_phy.phy_do_full_init)
		bwn_radio_2056_rcal(mac);
}

/*
 * Initialize a Broadcom 2056 N-radio
 * http://bcm-v4.sipsolutions.net/802.11/Radio/2056/Init
 */
static void bwn_radio_init2056(struct bwn_mac *mac)
{
	DPRINTF(mac->mac_sc, BWN_DEBUG_RF, "%s: called\n", __func__);

	bwn_radio_init2056_pre(mac);
	b2056_upload_inittabs(mac, 0, 0);
	bwn_radio_init2056_post(mac);
}

/**************************************************
 * Radio 0x2055
 **************************************************/

static void bwn_chantab_radio_upload(struct bwn_mac *mac,
				const struct bwn_nphy_channeltab_entry_rev2 *e)
{
	BWN_RF_WRITE(mac, B2055_PLL_REF, e->radio_pll_ref);
	BWN_RF_WRITE(mac, B2055_RF_PLLMOD0, e->radio_rf_pllmod0);
	BWN_RF_WRITE(mac, B2055_RF_PLLMOD1, e->radio_rf_pllmod1);
	BWN_RF_WRITE(mac, B2055_VCO_CAPTAIL, e->radio_vco_captail);
	BWN_READ_4(mac, BWN_MACCTL); /* flush writes */

	BWN_RF_WRITE(mac, B2055_VCO_CAL1, e->radio_vco_cal1);
	BWN_RF_WRITE(mac, B2055_VCO_CAL2, e->radio_vco_cal2);
	BWN_RF_WRITE(mac, B2055_PLL_LFC1, e->radio_pll_lfc1);
	BWN_RF_WRITE(mac, B2055_PLL_LFR1, e->radio_pll_lfr1);
	BWN_READ_4(mac, BWN_MACCTL); /* flush writes */

	BWN_RF_WRITE(mac, B2055_PLL_LFC2, e->radio_pll_lfc2);
	BWN_RF_WRITE(mac, B2055_LGBUF_CENBUF, e->radio_lgbuf_cenbuf);
	BWN_RF_WRITE(mac, B2055_LGEN_TUNE1, e->radio_lgen_tune1);
	BWN_RF_WRITE(mac, B2055_LGEN_TUNE2, e->radio_lgen_tune2);
	BWN_READ_4(mac, BWN_MACCTL); /* flush writes */

	BWN_RF_WRITE(mac, B2055_C1_LGBUF_ATUNE, e->radio_c1_lgbuf_atune);
	BWN_RF_WRITE(mac, B2055_C1_LGBUF_GTUNE, e->radio_c1_lgbuf_gtune);
	BWN_RF_WRITE(mac, B2055_C1_RX_RFR1, e->radio_c1_rx_rfr1);
	BWN_RF_WRITE(mac, B2055_C1_TX_PGAPADTN, e->radio_c1_tx_pgapadtn);
	BWN_READ_4(mac, BWN_MACCTL); /* flush writes */

	BWN_RF_WRITE(mac, B2055_C1_TX_MXBGTRIM, e->radio_c1_tx_mxbgtrim);
	BWN_RF_WRITE(mac, B2055_C2_LGBUF_ATUNE, e->radio_c2_lgbuf_atune);
	BWN_RF_WRITE(mac, B2055_C2_LGBUF_GTUNE, e->radio_c2_lgbuf_gtune);
	BWN_RF_WRITE(mac, B2055_C2_RX_RFR1, e->radio_c2_rx_rfr1);
	BWN_READ_4(mac, BWN_MACCTL); /* flush writes */

	BWN_RF_WRITE(mac, B2055_C2_TX_PGAPADTN, e->radio_c2_tx_pgapadtn);
	BWN_RF_WRITE(mac, B2055_C2_TX_MXBGTRIM, e->radio_c2_tx_mxbgtrim);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/Radio/2055Setup */
static void bwn_radio_2055_setup(struct bwn_mac *mac,
				const struct bwn_nphy_channeltab_entry_rev2 *e)
{

	if (mac->mac_phy.rev >= 3) {
		BWN_ERRPRINTF(mac->mac_sc, "%s: phy rev %d out of range\n",
		    __func__,
		    mac->mac_phy.rev);
	}

	DPRINTF(mac->mac_sc, BWN_DEBUG_RF, "%s: called\n", __func__);

	bwn_chantab_radio_upload(mac, e);
	DELAY(50);
	BWN_RF_WRITE(mac, B2055_VCO_CAL10, 0x05);
	BWN_RF_WRITE(mac, B2055_VCO_CAL10, 0x45);
	BWN_READ_4(mac, BWN_MACCTL); /* flush writes */
	BWN_RF_WRITE(mac, B2055_VCO_CAL10, 0x65);
	DELAY(300);
}

static void bwn_radio_init2055_pre(struct bwn_mac *mac)
{
	BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_CMD,
		     ~BWN_NPHY_RFCTL_CMD_PORFORCE);
	BWN_PHY_SET(mac, BWN_NPHY_RFCTL_CMD,
		    BWN_NPHY_RFCTL_CMD_CHIP0PU |
		    BWN_NPHY_RFCTL_CMD_OEPORFORCE);
	BWN_PHY_SET(mac, BWN_NPHY_RFCTL_CMD,
		    BWN_NPHY_RFCTL_CMD_PORFORCE);
}

static void bwn_radio_init2055_post(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	bool workaround = false;

	if (bhnd_get_hwrev(sc->sc_dev) < 4)
		workaround =
		    (sc->sc_board_info.board_vendor != PCI_VENDOR_BROADCOM)
		    && (sc->sc_board_info.board_type == BHND_BOARD_BCM4321CB2)
		      && (sc->sc_board_info.board_rev >= 0x41);
	else
		workaround =
			!(sc->sc_board_info.board_flags2 & BHND_BFL2_RXBB_INT_REG_DIS);

	BWN_RF_MASK(mac, B2055_MASTER1, 0xFFF3);
	if (workaround) {
		BWN_RF_MASK(mac, B2055_C1_RX_BB_REG, 0x7F);
		BWN_RF_MASK(mac, B2055_C2_RX_BB_REG, 0x7F);
	}
	BWN_RF_SETMASK(mac, B2055_RRCCAL_NOPTSEL, 0xFFC0, 0x2C);
	BWN_RF_WRITE(mac, B2055_CAL_MISC, 0x3C);
	BWN_RF_MASK(mac, B2055_CAL_MISC, 0xFFBE);
	BWN_RF_SET(mac, B2055_CAL_LPOCTL, 0x80);
	BWN_RF_SET(mac, B2055_CAL_MISC, 0x1);
	DELAY(1000);
	BWN_RF_SET(mac, B2055_CAL_MISC, 0x40);
	if (!bwn_radio_wait_value(mac, B2055_CAL_COUT2, 0x80, 0x80, 10, 2000))
		BWN_ERRPRINTF(mac->mac_sc, "radio post init timeout\n");
	BWN_RF_MASK(mac, B2055_CAL_LPOCTL, 0xFF7F);
	bwn_switch_channel(mac, bwn_get_chan(mac));
	BWN_RF_WRITE(mac, B2055_C1_RX_BB_LPF, 0x9);
	BWN_RF_WRITE(mac, B2055_C2_RX_BB_LPF, 0x9);
	BWN_RF_WRITE(mac, B2055_C1_RX_BB_MIDACHP, 0x83);
	BWN_RF_WRITE(mac, B2055_C2_RX_BB_MIDACHP, 0x83);
	BWN_RF_SETMASK(mac, B2055_C1_LNA_GAINBST, 0xFFF8, 0x6);
	BWN_RF_SETMASK(mac, B2055_C2_LNA_GAINBST, 0xFFF8, 0x6);
	if (!nphy->gain_boost) {
		BWN_RF_SET(mac, B2055_C1_RX_RFSPC1, 0x2);
		BWN_RF_SET(mac, B2055_C2_RX_RFSPC1, 0x2);
	} else {
		BWN_RF_MASK(mac, B2055_C1_RX_RFSPC1, 0xFFFD);
		BWN_RF_MASK(mac, B2055_C2_RX_RFSPC1, 0xFFFD);
	}
	DELAY(2);
}

/*
 * Initialize a Broadcom 2055 N-radio
 * http://bcm-v4.sipsolutions.net/802.11/Radio/2055/Init
 */
static void bwn_radio_init2055(struct bwn_mac *mac)
{
	bwn_radio_init2055_pre(mac);
	if (mac->mac_status < BWN_MAC_STATUS_INITED) {
		/* Follow wl, not specs. Do not force uploading all regs */
		b2055_upload_inittab(mac, 0, 0);
	} else {
		bool ghz5 = bwn_current_band(mac) == BWN_BAND_5G;
		b2055_upload_inittab(mac, ghz5, 0);
	}
	bwn_radio_init2055_post(mac);
}

/**************************************************
 * Samples
 **************************************************/

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/LoadSampleTable */
static int bwn_nphy_load_samples(struct bwn_mac *mac,
					struct bwn_c32 *samples, uint16_t len) {
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	uint16_t i;
	uint32_t *data;

	data = malloc(len * sizeof(uint32_t), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!data) {
		BWN_ERRPRINTF(mac->mac_sc, "allocation for samples loading failed\n");
		return -ENOMEM;
	}
	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 1);

	for (i = 0; i < len; i++) {
		data[i] = (samples[i].i & 0x3FF << 10);
		data[i] |= samples[i].q & 0x3FF;
	}
	bwn_ntab_write_bulk(mac, BWN_NTAB32(17, 0), len, data);

	free(data, M_DEVBUF);
	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 0);
	return 0;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/GenLoadSamples */
static uint16_t bwn_nphy_gen_load_samples(struct bwn_mac *mac, uint32_t freq, uint16_t max,
					bool test)
{
	int i;
	uint16_t bw, len, rot, angle;
	struct bwn_c32 *samples;

	bw = bwn_is_40mhz(mac) ? 40 : 20;
	len = bw << 3;

	if (test) {
		if (BWN_PHY_READ(mac, BWN_NPHY_BBCFG) & BWN_NPHY_BBCFG_RSTRX)
			bw = 82;
		else
			bw = 80;

		if (bwn_is_40mhz(mac))
			bw <<= 1;

		len = bw << 1;
	}

	samples = malloc(len * sizeof(struct bwn_c32), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!samples) {
		BWN_ERRPRINTF(mac->mac_sc, "allocation for samples generation failed\n");
		return 0;
	}
	rot = (((freq * 36) / bw) << 16) / 100;
	angle = 0;

	for (i = 0; i < len; i++) {
		samples[i] = bwn_cordic(angle);
		angle += rot;
		samples[i].q = CORDIC_CONVERT(samples[i].q * max);
		samples[i].i = CORDIC_CONVERT(samples[i].i * max);
	}

	i = bwn_nphy_load_samples(mac, samples, len);
	free(samples, M_DEVBUF);
	return (i < 0) ? 0 : len;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RunSamples */
static void bwn_nphy_run_samples(struct bwn_mac *mac, uint16_t samps, uint16_t loops,
				 uint16_t wait, bool iqmode, bool dac_test,
				 bool modify_bbmult)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	int i;
	uint16_t seq_mode;
	uint32_t tmp;

	bwn_nphy_stay_in_carrier_search(mac, true);

	if (phy->rev >= 7) {
		bool lpf_bw3, lpf_bw4;

		lpf_bw3 = BWN_PHY_READ(mac, BWN_NPHY_REV7_RF_CTL_OVER3) & 0x80;
		lpf_bw4 = BWN_PHY_READ(mac, BWN_NPHY_REV7_RF_CTL_OVER4) & 0x80;

		if (lpf_bw3 || lpf_bw4) {
			/* TODO */
		} else {
			uint16_t value = bwn_nphy_read_lpf_ctl(mac, 0);
			if (phy->rev >= 19)
				bwn_nphy_rf_ctl_override_rev19(mac, 0x80, value,
							       0, false, 1);
			else
				bwn_nphy_rf_ctl_override_rev7(mac, 0x80, value,
							      0, false, 1);
			nphy->lpf_bw_overrode_for_sample_play = true;
		}
	}

	if ((nphy->bb_mult_save & 0x80000000) == 0) {
		tmp = bwn_ntab_read(mac, BWN_NTAB16(15, 87));
		nphy->bb_mult_save = (tmp & 0xFFFF) | 0x80000000;
	}

	if (modify_bbmult) {
		tmp = !bwn_is_40mhz(mac) ? 0x6464 : 0x4747;
		bwn_ntab_write(mac, BWN_NTAB16(15, 87), tmp);
	}

	BWN_PHY_WRITE(mac, BWN_NPHY_SAMP_DEPCNT, (samps - 1));

	if (loops != 0xFFFF)
		BWN_PHY_WRITE(mac, BWN_NPHY_SAMP_LOOPCNT, (loops - 1));
	else
		BWN_PHY_WRITE(mac, BWN_NPHY_SAMP_LOOPCNT, loops);

	BWN_PHY_WRITE(mac, BWN_NPHY_SAMP_WAITCNT, wait);

	seq_mode = BWN_PHY_READ(mac, BWN_NPHY_RFSEQMODE);

	BWN_PHY_SET(mac, BWN_NPHY_RFSEQMODE, BWN_NPHY_RFSEQMODE_CAOVER);
	if (iqmode) {
		BWN_PHY_MASK(mac, BWN_NPHY_IQLOCAL_CMDGCTL, 0x7FFF);
		BWN_PHY_SET(mac, BWN_NPHY_IQLOCAL_CMDGCTL, 0x8000);
	} else {
		tmp = dac_test ? 5 : 1;
		BWN_PHY_WRITE(mac, BWN_NPHY_SAMP_CMD, tmp);
	}
	for (i = 0; i < 100; i++) {
		if (!(BWN_PHY_READ(mac, BWN_NPHY_RFSEQST) & 1)) {
			i = 0;
			break;
		}
		DELAY(10);
	}
	if (i)
		BWN_ERRPRINTF(mac->mac_sc, "run samples timeout\n");

	BWN_PHY_WRITE(mac, BWN_NPHY_RFSEQMODE, seq_mode);

	bwn_nphy_stay_in_carrier_search(mac, false);
}

/**************************************************
 * RSSI
 **************************************************/

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/ScaleOffsetRssi */
static void bwn_nphy_scale_offset_rssi(struct bwn_mac *mac, uint16_t scale,
					int8_t offset, uint8_t core,
					enum n_rail_type rail,
					enum n_rssi_type rssi_type)
{
	uint16_t tmp;
	bool core1or5 = (core == 1) || (core == 5);
	bool core2or5 = (core == 2) || (core == 5);

	offset = bwn_clamp_val(offset, -32, 31);
	tmp = ((scale & 0x3F) << 8) | (offset & 0x3F);

	switch (rssi_type) {
	case N_RSSI_NB:
		if (core1or5 && rail == N_RAIL_I)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0I_RSSI_Z, tmp);
		if (core1or5 && rail == N_RAIL_Q)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0Q_RSSI_Z, tmp);
		if (core2or5 && rail == N_RAIL_I)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1I_RSSI_Z, tmp);
		if (core2or5 && rail == N_RAIL_Q)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1Q_RSSI_Z, tmp);
		break;
	case N_RSSI_W1:
		if (core1or5 && rail == N_RAIL_I)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0I_RSSI_X, tmp);
		if (core1or5 && rail == N_RAIL_Q)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0Q_RSSI_X, tmp);
		if (core2or5 && rail == N_RAIL_I)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1I_RSSI_X, tmp);
		if (core2or5 && rail == N_RAIL_Q)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1Q_RSSI_X, tmp);
		break;
	case N_RSSI_W2:
		if (core1or5 && rail == N_RAIL_I)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0I_RSSI_Y, tmp);
		if (core1or5 && rail == N_RAIL_Q)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0Q_RSSI_Y, tmp);
		if (core2or5 && rail == N_RAIL_I)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1I_RSSI_Y, tmp);
		if (core2or5 && rail == N_RAIL_Q)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1Q_RSSI_Y, tmp);
		break;
	case N_RSSI_TBD:
		if (core1or5 && rail == N_RAIL_I)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0I_TBD, tmp);
		if (core1or5 && rail == N_RAIL_Q)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0Q_TBD, tmp);
		if (core2or5 && rail == N_RAIL_I)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1I_TBD, tmp);
		if (core2or5 && rail == N_RAIL_Q)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1Q_TBD, tmp);
		break;
	case N_RSSI_IQ:
		if (core1or5 && rail == N_RAIL_I)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0I_PWRDET, tmp);
		if (core1or5 && rail == N_RAIL_Q)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0Q_PWRDET, tmp);
		if (core2or5 && rail == N_RAIL_I)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1I_PWRDET, tmp);
		if (core2or5 && rail == N_RAIL_Q)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1Q_PWRDET, tmp);
		break;
	case N_RSSI_TSSI_2G:
		if (core1or5)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0I_TSSI, tmp);
		if (core2or5)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1I_TSSI, tmp);
		break;
	case N_RSSI_TSSI_5G:
		if (core1or5)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0Q_TSSI, tmp);
		if (core2or5)
			BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1Q_TSSI, tmp);
		break;
	}
}

static void bwn_nphy_rssi_select_rev19(struct bwn_mac *mac, uint8_t code,
				       enum n_rssi_type rssi_type)
{
	/* TODO */
}

static void bwn_nphy_rev3_rssi_select(struct bwn_mac *mac, uint8_t code,
				      enum n_rssi_type rssi_type)
{
	uint8_t i;
	uint16_t reg, val;

	if (code == 0) {
		BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_OVER1, 0xFDFF);
		BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_OVER, 0xFDFF);
		BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_C1, 0xFCFF);
		BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_C2, 0xFCFF);
		BWN_PHY_MASK(mac, BWN_NPHY_TXF_40CO_B1S0, 0xFFDF);
		BWN_PHY_MASK(mac, BWN_NPHY_TXF_40CO_B32S1, 0xFFDF);
		BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_LUT_TRSW_UP1, 0xFFC3);
		BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_LUT_TRSW_UP2, 0xFFC3);
	} else {
		for (i = 0; i < 2; i++) {
			if ((code == 1 && i == 1) || (code == 2 && !i))
				continue;

			reg = (i == 0) ?
				BWN_NPHY_AFECTL_OVER1 : BWN_NPHY_AFECTL_OVER;
			BWN_PHY_SETMASK(mac, reg, 0xFDFF, 0x0200);

			if (rssi_type == N_RSSI_W1 ||
			    rssi_type == N_RSSI_W2 ||
			    rssi_type == N_RSSI_NB) {
				reg = (i == 0) ?
					BWN_NPHY_AFECTL_C1 :
					BWN_NPHY_AFECTL_C2;
				BWN_PHY_SETMASK(mac, reg, 0xFCFF, 0);

				reg = (i == 0) ?
					BWN_NPHY_RFCTL_LUT_TRSW_UP1 :
					BWN_NPHY_RFCTL_LUT_TRSW_UP2;
				BWN_PHY_SETMASK(mac, reg, 0xFFC3, 0);

				if (rssi_type == N_RSSI_W1)
					val = (bwn_current_band(mac) == BWN_BAND_5G) ? 4 : 8;
				else if (rssi_type == N_RSSI_W2)
					val = 16;
				else
					val = 32;
				BWN_PHY_SET(mac, reg, val);

				reg = (i == 0) ?
					BWN_NPHY_TXF_40CO_B1S0 :
					BWN_NPHY_TXF_40CO_B32S1;
				BWN_PHY_SET(mac, reg, 0x0020);
			} else {
				if (rssi_type == N_RSSI_TBD)
					val = 0x0100;
				else if (rssi_type == N_RSSI_IQ)
					val = 0x0200;
				else
					val = 0x0300;

				reg = (i == 0) ?
					BWN_NPHY_AFECTL_C1 :
					BWN_NPHY_AFECTL_C2;

				BWN_PHY_SETMASK(mac, reg, 0xFCFF, val);
				BWN_PHY_SETMASK(mac, reg, 0xF3FF, val << 2);

				if (rssi_type != N_RSSI_IQ &&
				    rssi_type != N_RSSI_TBD) {
					bwn_band_t band =
						bwn_current_band(mac);

					if (mac->mac_phy.rev < 7) {
						if (bwn_nphy_ipa(mac))
							val = (band == BWN_BAND_5G) ? 0xC : 0xE;
						else
							val = 0x11;
						reg = (i == 0) ? B2056_TX0 : B2056_TX1;
						reg |= B2056_TX_TX_SSI_MUX;
						BWN_RF_WRITE(mac, reg, val);
					}

					reg = (i == 0) ?
						BWN_NPHY_AFECTL_OVER1 :
						BWN_NPHY_AFECTL_OVER;
					BWN_PHY_SET(mac, reg, 0x0200);
				}
			}
		}
	}
}

static void bwn_nphy_rev2_rssi_select(struct bwn_mac *mac, uint8_t code,
				      enum n_rssi_type rssi_type)
{
	uint16_t val;
	bool rssi_w1_w2_nb = false;

	switch (rssi_type) {
	case N_RSSI_W1:
	case N_RSSI_W2:
	case N_RSSI_NB:
		val = 0;
		rssi_w1_w2_nb = true;
		break;
	case N_RSSI_TBD:
		val = 1;
		break;
	case N_RSSI_IQ:
		val = 2;
		break;
	default:
		val = 3;
	}

	val = (val << 12) | (val << 14);
	BWN_PHY_SETMASK(mac, BWN_NPHY_AFECTL_C1, 0x0FFF, val);
	BWN_PHY_SETMASK(mac, BWN_NPHY_AFECTL_C2, 0x0FFF, val);

	if (rssi_w1_w2_nb) {
		BWN_PHY_SETMASK(mac, BWN_NPHY_RFCTL_RSSIO1, 0xFFCF,
				(rssi_type + 1) << 4);
		BWN_PHY_SETMASK(mac, BWN_NPHY_RFCTL_RSSIO2, 0xFFCF,
				(rssi_type + 1) << 4);
	}

	if (code == 0) {
		BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_OVER, ~0x3000);
		if (rssi_w1_w2_nb) {
			BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_CMD,
				~(BWN_NPHY_RFCTL_CMD_RXEN |
				  BWN_NPHY_RFCTL_CMD_CORESEL));
			BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_OVER,
				~(0x1 << 12 |
				  0x1 << 5 |
				  0x1 << 1 |
				  0x1));
			BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_CMD,
				~BWN_NPHY_RFCTL_CMD_START);
			DELAY(20);
			BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_OVER, ~0x1);
		}
	} else {
		BWN_PHY_SET(mac, BWN_NPHY_AFECTL_OVER, 0x3000);
		if (rssi_w1_w2_nb) {
			BWN_PHY_SETMASK(mac, BWN_NPHY_RFCTL_CMD,
				~(BWN_NPHY_RFCTL_CMD_RXEN |
				  BWN_NPHY_RFCTL_CMD_CORESEL),
				(BWN_NPHY_RFCTL_CMD_RXEN |
				 code << BWN_NPHY_RFCTL_CMD_CORESEL_SHIFT));
			BWN_PHY_SET(mac, BWN_NPHY_RFCTL_OVER,
				(0x1 << 12 |
				  0x1 << 5 |
				  0x1 << 1 |
				  0x1));
			BWN_PHY_SET(mac, BWN_NPHY_RFCTL_CMD,
				BWN_NPHY_RFCTL_CMD_START);
			DELAY(20);
			BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_OVER, ~0x1);
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RSSISel */
static void bwn_nphy_rssi_select(struct bwn_mac *mac, uint8_t code,
				 enum n_rssi_type type)
{
	if (mac->mac_phy.rev >= 19)
		bwn_nphy_rssi_select_rev19(mac, code, type);
	else if (mac->mac_phy.rev >= 3)
		bwn_nphy_rev3_rssi_select(mac, code, type);
	else
		bwn_nphy_rev2_rssi_select(mac, code, type);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/SetRssi2055Vcm */
static void bwn_nphy_set_rssi_2055_vcm(struct bwn_mac *mac,
				       enum n_rssi_type rssi_type, uint8_t *buf)
{
	int i;
	for (i = 0; i < 2; i++) {
		if (rssi_type == N_RSSI_NB) {
			if (i == 0) {
				BWN_RF_SETMASK(mac, B2055_C1_B0NB_RSSIVCM,
						  0xFC, buf[0]);
				BWN_RF_SETMASK(mac, B2055_C1_RX_BB_RSSICTL5,
						  0xFC, buf[1]);
			} else {
				BWN_RF_SETMASK(mac, B2055_C2_B0NB_RSSIVCM,
						  0xFC, buf[2 * i]);
				BWN_RF_SETMASK(mac, B2055_C2_RX_BB_RSSICTL5,
						  0xFC, buf[2 * i + 1]);
			}
		} else {
			if (i == 0)
				BWN_RF_SETMASK(mac, B2055_C1_RX_BB_RSSICTL5,
						  0xF3, buf[0] << 2);
			else
				BWN_RF_SETMASK(mac, B2055_C2_RX_BB_RSSICTL5,
						  0xF3, buf[2 * i + 1] << 2);
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/PollRssi */
static int bwn_nphy_poll_rssi(struct bwn_mac *mac, enum n_rssi_type rssi_type,
			      int32_t *buf, uint8_t nsamp)
{
	int i;
	int out;
	uint16_t save_regs_phy[9];
	uint16_t s[2];

	/* TODO: rev7+ is treated like rev3+, what about rev19+? */

	if (mac->mac_phy.rev >= 3) {
		save_regs_phy[0] = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_C1);
		save_regs_phy[1] = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_C2);
		save_regs_phy[2] = BWN_PHY_READ(mac,
						BWN_NPHY_RFCTL_LUT_TRSW_UP1);
		save_regs_phy[3] = BWN_PHY_READ(mac,
						BWN_NPHY_RFCTL_LUT_TRSW_UP2);
		save_regs_phy[4] = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_OVER1);
		save_regs_phy[5] = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_OVER);
		save_regs_phy[6] = BWN_PHY_READ(mac, BWN_NPHY_TXF_40CO_B1S0);
		save_regs_phy[7] = BWN_PHY_READ(mac, BWN_NPHY_TXF_40CO_B32S1);
		save_regs_phy[8] = 0;
	} else {
		save_regs_phy[0] = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_C1);
		save_regs_phy[1] = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_C2);
		save_regs_phy[2] = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_OVER);
		save_regs_phy[3] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_CMD);
		save_regs_phy[4] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_OVER);
		save_regs_phy[5] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_RSSIO1);
		save_regs_phy[6] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_RSSIO2);
		save_regs_phy[7] = 0;
		save_regs_phy[8] = 0;
	}

	bwn_nphy_rssi_select(mac, 5, rssi_type);

	if (mac->mac_phy.rev < 2) {
		save_regs_phy[8] = BWN_PHY_READ(mac, BWN_NPHY_GPIO_SEL);
		BWN_PHY_WRITE(mac, BWN_NPHY_GPIO_SEL, 5);
	}

	for (i = 0; i < 4; i++)
		buf[i] = 0;

	for (i = 0; i < nsamp; i++) {
		if (mac->mac_phy.rev < 2) {
			s[0] = BWN_PHY_READ(mac, BWN_NPHY_GPIO_LOOUT);
			s[1] = BWN_PHY_READ(mac, BWN_NPHY_GPIO_HIOUT);
		} else {
			s[0] = BWN_PHY_READ(mac, BWN_NPHY_RSSI1);
			s[1] = BWN_PHY_READ(mac, BWN_NPHY_RSSI2);
		}

		buf[0] += ((int8_t)((s[0] & 0x3F) << 2)) >> 2;
		buf[1] += ((int8_t)(((s[0] >> 8) & 0x3F) << 2)) >> 2;
		buf[2] += ((int8_t)((s[1] & 0x3F) << 2)) >> 2;
		buf[3] += ((int8_t)(((s[1] >> 8) & 0x3F) << 2)) >> 2;
	}
	out = (buf[0] & 0xFF) << 24 | (buf[1] & 0xFF) << 16 |
		(buf[2] & 0xFF) << 8 | (buf[3] & 0xFF);

	if (mac->mac_phy.rev < 2)
		BWN_PHY_WRITE(mac, BWN_NPHY_GPIO_SEL, save_regs_phy[8]);

	if (mac->mac_phy.rev >= 3) {
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_C1, save_regs_phy[0]);
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_C2, save_regs_phy[1]);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_LUT_TRSW_UP1,
				save_regs_phy[2]);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_LUT_TRSW_UP2,
				save_regs_phy[3]);
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER1, save_regs_phy[4]);
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER, save_regs_phy[5]);
		BWN_PHY_WRITE(mac, BWN_NPHY_TXF_40CO_B1S0, save_regs_phy[6]);
		BWN_PHY_WRITE(mac, BWN_NPHY_TXF_40CO_B32S1, save_regs_phy[7]);
	} else {
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_C1, save_regs_phy[0]);
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_C2, save_regs_phy[1]);
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER, save_regs_phy[2]);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_CMD, save_regs_phy[3]);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_OVER, save_regs_phy[4]);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_RSSIO1, save_regs_phy[5]);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_RSSIO2, save_regs_phy[6]);
	}

	return out;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RSSICalRev3 */
static void bwn_nphy_rev3_rssi_cal(struct bwn_mac *mac)
{
	//struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;

	uint16_t saved_regs_phy_rfctl[2];
	uint16_t saved_regs_phy[22];
	uint16_t regs_to_store_rev3[] = {
		BWN_NPHY_AFECTL_OVER1, BWN_NPHY_AFECTL_OVER,
		BWN_NPHY_AFECTL_C1, BWN_NPHY_AFECTL_C2,
		BWN_NPHY_TXF_40CO_B1S1, BWN_NPHY_RFCTL_OVER,
		BWN_NPHY_TXF_40CO_B1S0, BWN_NPHY_TXF_40CO_B32S1,
		BWN_NPHY_RFCTL_CMD,
		BWN_NPHY_RFCTL_LUT_TRSW_UP1, BWN_NPHY_RFCTL_LUT_TRSW_UP2,
		BWN_NPHY_RFCTL_RSSIO1, BWN_NPHY_RFCTL_RSSIO2
	};
	uint16_t regs_to_store_rev7[] = {
		BWN_NPHY_AFECTL_OVER1, BWN_NPHY_AFECTL_OVER,
		BWN_NPHY_AFECTL_C1, BWN_NPHY_AFECTL_C2,
		BWN_NPHY_TXF_40CO_B1S1, BWN_NPHY_RFCTL_OVER,
		BWN_NPHY_REV7_RF_CTL_OVER3, BWN_NPHY_REV7_RF_CTL_OVER4,
		BWN_NPHY_REV7_RF_CTL_OVER5, BWN_NPHY_REV7_RF_CTL_OVER6,
		0x2ff,
		BWN_NPHY_TXF_40CO_B1S0, BWN_NPHY_TXF_40CO_B32S1,
		BWN_NPHY_RFCTL_CMD,
		BWN_NPHY_RFCTL_LUT_TRSW_UP1, BWN_NPHY_RFCTL_LUT_TRSW_UP2,
		BWN_NPHY_REV7_RF_CTL_MISC_REG3, BWN_NPHY_REV7_RF_CTL_MISC_REG4,
		BWN_NPHY_REV7_RF_CTL_MISC_REG5, BWN_NPHY_REV7_RF_CTL_MISC_REG6,
		BWN_NPHY_RFCTL_RSSIO1, BWN_NPHY_RFCTL_RSSIO2
	};
	uint16_t *regs_to_store;
	int regs_amount;

	uint16_t class;

	uint16_t clip_state[2];
	uint16_t clip_off[2] = { 0xFFFF, 0xFFFF };

	uint8_t vcm_final = 0;
	int32_t offset[4];
	int32_t results[8][4] = { };
	int32_t results_min[4] = { };
	int32_t poll_results[4] = { };

	uint16_t *rssical_radio_regs = NULL;
	uint16_t *rssical_phy_regs = NULL;

	uint16_t r; /* routing */
	uint8_t rx_core_state;
	int core, i, j, vcm;

	if (mac->mac_phy.rev >= 7) {
		regs_to_store = regs_to_store_rev7;
		regs_amount = nitems(regs_to_store_rev7);
	} else {
		regs_to_store = regs_to_store_rev3;
		regs_amount = nitems(regs_to_store_rev3);
	}
	KASSERT((regs_amount <= nitems(saved_regs_phy)),
	    ("%s: reg_amount (%d) too large\n",
	    __func__,
	    regs_amount));

	class = bwn_nphy_classifier(mac, 0, 0);
	bwn_nphy_classifier(mac, 7, 4);
	bwn_nphy_read_clip_detection(mac, clip_state);
	bwn_nphy_write_clip_detection(mac, clip_off);

	saved_regs_phy_rfctl[0] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_INTC1);
	saved_regs_phy_rfctl[1] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_INTC2);
	for (i = 0; i < regs_amount; i++)
		saved_regs_phy[i] = BWN_PHY_READ(mac, regs_to_store[i]);

	bwn_nphy_rf_ctl_intc_override(mac, N_INTC_OVERRIDE_OFF, 0, 7);
	bwn_nphy_rf_ctl_intc_override(mac, N_INTC_OVERRIDE_TRSW, 1, 7);

	if (mac->mac_phy.rev >= 7) {
		bwn_nphy_rf_ctl_override_one_to_many(mac,
						     N_RF_CTL_OVER_CMD_RXRF_PU,
						     0, 0, false);
		bwn_nphy_rf_ctl_override_one_to_many(mac,
						     N_RF_CTL_OVER_CMD_RX_PU,
						     1, 0, false);
		bwn_nphy_rf_ctl_override_rev7(mac, 0x80, 1, 0, false, 0);
		bwn_nphy_rf_ctl_override_rev7(mac, 0x40, 1, 0, false, 0);
		if (bwn_current_band(mac) == BWN_BAND_5G) {
			bwn_nphy_rf_ctl_override_rev7(mac, 0x20, 0, 0, false,
						      0);
			bwn_nphy_rf_ctl_override_rev7(mac, 0x10, 1, 0, false,
						      0);
		} else {
			bwn_nphy_rf_ctl_override_rev7(mac, 0x10, 0, 0, false,
						      0);
			bwn_nphy_rf_ctl_override_rev7(mac, 0x20, 1, 0, false,
						      0);
		}
	} else {
		bwn_nphy_rf_ctl_override(mac, 0x1, 0, 0, false);
		bwn_nphy_rf_ctl_override(mac, 0x2, 1, 0, false);
		bwn_nphy_rf_ctl_override(mac, 0x80, 1, 0, false);
		bwn_nphy_rf_ctl_override(mac, 0x40, 1, 0, false);
		if (bwn_current_band(mac) == BWN_BAND_5G) {
			bwn_nphy_rf_ctl_override(mac, 0x20, 0, 0, false);
			bwn_nphy_rf_ctl_override(mac, 0x10, 1, 0, false);
		} else {
			bwn_nphy_rf_ctl_override(mac, 0x10, 0, 0, false);
			bwn_nphy_rf_ctl_override(mac, 0x20, 1, 0, false);
		}
	}

	rx_core_state = bwn_nphy_get_rx_core_state(mac);
	for (core = 0; core < 2; core++) {
		if (!(rx_core_state & (1 << core)))
			continue;
		r = core ? B2056_RX1 : B2056_RX0;
		bwn_nphy_scale_offset_rssi(mac, 0, 0, core + 1, N_RAIL_I,
					   N_RSSI_NB);
		bwn_nphy_scale_offset_rssi(mac, 0, 0, core + 1, N_RAIL_Q,
					   N_RSSI_NB);

		/* Grab RSSI results for every possible VCM */
		for (vcm = 0; vcm < 8; vcm++) {
			if (mac->mac_phy.rev >= 7)
				BWN_RF_SETMASK(mac,
						  core ? R2057_NB_MASTER_CORE1 :
							 R2057_NB_MASTER_CORE0,
						  ~R2057_VCM_MASK, vcm);
			else
				BWN_RF_SETMASK(mac, r | B2056_RX_RSSI_MISC,
						  0xE3, vcm << 2);
			bwn_nphy_poll_rssi(mac, N_RSSI_NB, results[vcm], 8);
		}

		/* Find out which VCM got the best results */
		for (i = 0; i < 4; i += 2) {
			int32_t currd;
			int32_t mind = 0x100000;
			int32_t minpoll = 249;
			uint8_t minvcm = 0;
			if (2 * core != i)
				continue;
			for (vcm = 0; vcm < 8; vcm++) {
				currd = results[vcm][i] * results[vcm][i] +
					results[vcm][i + 1] * results[vcm][i];
				if (currd < mind) {
					mind = currd;
					minvcm = vcm;
				}
				if (results[vcm][i] < minpoll)
					minpoll = results[vcm][i];
			}
			vcm_final = minvcm;
			results_min[i] = minpoll;
		}

		/* Select the best VCM */
		if (mac->mac_phy.rev >= 7)
			BWN_RF_SETMASK(mac,
					  core ? R2057_NB_MASTER_CORE1 :
						 R2057_NB_MASTER_CORE0,
					  ~R2057_VCM_MASK, vcm);
		else
			BWN_RF_SETMASK(mac, r | B2056_RX_RSSI_MISC,
					  0xE3, vcm_final << 2);

		for (i = 0; i < 4; i++) {
			if (core != i / 2)
				continue;
			offset[i] = -results[vcm_final][i];
			if (offset[i] < 0)
				offset[i] = -((abs(offset[i]) + 4) / 8);
			else
				offset[i] = (offset[i] + 4) / 8;
			if (results_min[i] == 248)
				offset[i] = -32;
			bwn_nphy_scale_offset_rssi(mac, 0, offset[i],
						   (i / 2 == 0) ? 1 : 2,
						   (i % 2 == 0) ? N_RAIL_I : N_RAIL_Q,
						   N_RSSI_NB);
		}
	}

	for (core = 0; core < 2; core++) {
		if (!(rx_core_state & (1 << core)))
			continue;
		for (i = 0; i < 2; i++) {
			bwn_nphy_scale_offset_rssi(mac, 0, 0, core + 1,
						   N_RAIL_I, i);
			bwn_nphy_scale_offset_rssi(mac, 0, 0, core + 1,
						   N_RAIL_Q, i);
			bwn_nphy_poll_rssi(mac, i, poll_results, 8);
			for (j = 0; j < 4; j++) {
				if (j / 2 == core) {
					offset[j] = 232 - poll_results[j];
					if (offset[j] < 0)
						offset[j] = -(abs(offset[j] + 4) / 8);
					else
						offset[j] = (offset[j] + 4) / 8;
					bwn_nphy_scale_offset_rssi(mac, 0,
						offset[2 * core], core + 1, j % 2, i);
				}
			}
		}
	}

	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC1, saved_regs_phy_rfctl[0]);
	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC2, saved_regs_phy_rfctl[1]);

	bwn_nphy_force_rf_sequence(mac, BWN_RFSEQ_RESET2RX);

	BWN_PHY_SET(mac, BWN_NPHY_TXF_40CO_B1S1, 0x1);
	BWN_PHY_SET(mac, BWN_NPHY_RFCTL_CMD, BWN_NPHY_RFCTL_CMD_START);
	BWN_PHY_MASK(mac, BWN_NPHY_TXF_40CO_B1S1, ~0x1);

	BWN_PHY_SET(mac, BWN_NPHY_RFCTL_OVER, 0x1);
	BWN_PHY_SET(mac, BWN_NPHY_RFCTL_CMD, BWN_NPHY_RFCTL_CMD_RXTX);
	BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_OVER, ~0x1);

	for (i = 0; i < regs_amount; i++)
		BWN_PHY_WRITE(mac, regs_to_store[i], saved_regs_phy[i]);

	/* Store for future configuration */
	if (bwn_current_band(mac) == BWN_BAND_2G) {
		rssical_radio_regs = nphy->rssical_cache.rssical_radio_regs_2G;
		rssical_phy_regs = nphy->rssical_cache.rssical_phy_regs_2G;
	} else {
		rssical_radio_regs = nphy->rssical_cache.rssical_radio_regs_5G;
		rssical_phy_regs = nphy->rssical_cache.rssical_phy_regs_5G;
	}
	if (mac->mac_phy.rev >= 7) {
		rssical_radio_regs[0] = BWN_RF_READ(mac,
						       R2057_NB_MASTER_CORE0);
		rssical_radio_regs[1] = BWN_RF_READ(mac,
						       R2057_NB_MASTER_CORE1);
	} else {
		rssical_radio_regs[0] = BWN_RF_READ(mac, B2056_RX0 |
						       B2056_RX_RSSI_MISC);
		rssical_radio_regs[1] = BWN_RF_READ(mac, B2056_RX1 |
						       B2056_RX_RSSI_MISC);
	}
	rssical_phy_regs[0] = BWN_PHY_READ(mac, BWN_NPHY_RSSIMC_0I_RSSI_Z);
	rssical_phy_regs[1] = BWN_PHY_READ(mac, BWN_NPHY_RSSIMC_0Q_RSSI_Z);
	rssical_phy_regs[2] = BWN_PHY_READ(mac, BWN_NPHY_RSSIMC_1I_RSSI_Z);
	rssical_phy_regs[3] = BWN_PHY_READ(mac, BWN_NPHY_RSSIMC_1Q_RSSI_Z);
	rssical_phy_regs[4] = BWN_PHY_READ(mac, BWN_NPHY_RSSIMC_0I_RSSI_X);
	rssical_phy_regs[5] = BWN_PHY_READ(mac, BWN_NPHY_RSSIMC_0Q_RSSI_X);
	rssical_phy_regs[6] = BWN_PHY_READ(mac, BWN_NPHY_RSSIMC_1I_RSSI_X);
	rssical_phy_regs[7] = BWN_PHY_READ(mac, BWN_NPHY_RSSIMC_1Q_RSSI_X);
	rssical_phy_regs[8] = BWN_PHY_READ(mac, BWN_NPHY_RSSIMC_0I_RSSI_Y);
	rssical_phy_regs[9] = BWN_PHY_READ(mac, BWN_NPHY_RSSIMC_0Q_RSSI_Y);
	rssical_phy_regs[10] = BWN_PHY_READ(mac, BWN_NPHY_RSSIMC_1I_RSSI_Y);
	rssical_phy_regs[11] = BWN_PHY_READ(mac, BWN_NPHY_RSSIMC_1Q_RSSI_Y);

	/* Remember for which channel we store configuration */
	if (bwn_current_band(mac) == BWN_BAND_2G)
		nphy->rssical_chanspec_2G.center_freq = bwn_get_centre_freq(mac);
	else
		nphy->rssical_chanspec_5G.center_freq = bwn_get_centre_freq(mac);

	/* End of calibration, restore configuration */
	bwn_nphy_classifier(mac, 7, class);
	bwn_nphy_write_clip_detection(mac, clip_state);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RSSICal */
static void bwn_nphy_rev2_rssi_cal(struct bwn_mac *mac, enum n_rssi_type type)
{
	int i, j, vcm;
	uint8_t state[4];
	uint8_t code, val;
	uint16_t class, override;
	uint8_t regs_save_radio[2];
	uint16_t regs_save_phy[2];

	int32_t offset[4];
	uint8_t core;
	uint8_t rail;

	uint16_t clip_state[2];
	uint16_t clip_off[2] = { 0xFFFF, 0xFFFF };
	int32_t results_min[4] = { };
	uint8_t vcm_final[4] = { };
	int32_t results[4][4] = { };
	int32_t miniq[4][2] = { };

	if (type == N_RSSI_NB) {
		code = 0;
		val = 6;
	} else if (type == N_RSSI_W1 || type == N_RSSI_W2) {
		code = 25;
		val = 4;
	} else {
		BWN_ERRPRINTF(mac->mac_sc, "%s: RSSI type %d invalid\n",
		    __func__,
		    type);
		return;
	}

	class = bwn_nphy_classifier(mac, 0, 0);
	bwn_nphy_classifier(mac, 7, 4);
	bwn_nphy_read_clip_detection(mac, clip_state);
	bwn_nphy_write_clip_detection(mac, clip_off);

	if (bwn_current_band(mac) == BWN_BAND_5G)
		override = 0x140;
	else
		override = 0x110;

	regs_save_phy[0] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_INTC1);
	regs_save_radio[0] = BWN_RF_READ(mac, B2055_C1_PD_RXTX);
	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC1, override);
	BWN_RF_WRITE(mac, B2055_C1_PD_RXTX, val);

	regs_save_phy[1] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_INTC2);
	regs_save_radio[1] = BWN_RF_READ(mac, B2055_C2_PD_RXTX);
	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC2, override);
	BWN_RF_WRITE(mac, B2055_C2_PD_RXTX, val);

	state[0] = BWN_RF_READ(mac, B2055_C1_PD_RSSIMISC) & 0x07;
	state[1] = BWN_RF_READ(mac, B2055_C2_PD_RSSIMISC) & 0x07;
	BWN_RF_MASK(mac, B2055_C1_PD_RSSIMISC, 0xF8);
	BWN_RF_MASK(mac, B2055_C2_PD_RSSIMISC, 0xF8);
	state[2] = BWN_RF_READ(mac, B2055_C1_SP_RSSI) & 0x07;
	state[3] = BWN_RF_READ(mac, B2055_C2_SP_RSSI) & 0x07;

	bwn_nphy_rssi_select(mac, 5, type);
	bwn_nphy_scale_offset_rssi(mac, 0, 0, 5, N_RAIL_I, type);
	bwn_nphy_scale_offset_rssi(mac, 0, 0, 5, N_RAIL_Q, type);

	for (vcm = 0; vcm < 4; vcm++) {
		uint8_t tmp[4];
		for (j = 0; j < 4; j++)
			tmp[j] = vcm;
		if (type != N_RSSI_W2)
			bwn_nphy_set_rssi_2055_vcm(mac, type, tmp);
		bwn_nphy_poll_rssi(mac, type, results[vcm], 8);
		if (type == N_RSSI_W1 || type == N_RSSI_W2)
			for (j = 0; j < 2; j++)
				miniq[vcm][j] = min(results[vcm][2 * j],
						    results[vcm][2 * j + 1]);
	}

	for (i = 0; i < 4; i++) {
		int32_t mind = 0x100000;
		uint8_t minvcm = 0;
		int32_t minpoll = 249;
		int32_t currd;
		for (vcm = 0; vcm < 4; vcm++) {
			if (type == N_RSSI_NB)
				currd = abs(results[vcm][i] - code * 8);
			else
				currd = abs(miniq[vcm][i / 2] - code * 8);

			if (currd < mind) {
				mind = currd;
				minvcm = vcm;
			}

			if (results[vcm][i] < minpoll)
				minpoll = results[vcm][i];
		}
		results_min[i] = minpoll;
		vcm_final[i] = minvcm;
	}

	if (type != N_RSSI_W2)
		bwn_nphy_set_rssi_2055_vcm(mac, type, vcm_final);

	for (i = 0; i < 4; i++) {
		offset[i] = (code * 8) - results[vcm_final[i]][i];

		if (offset[i] < 0)
			offset[i] = -((abs(offset[i]) + 4) / 8);
		else
			offset[i] = (offset[i] + 4) / 8;

		if (results_min[i] == 248)
			offset[i] = code - 32;

		core = (i / 2) ? 2 : 1;
		rail = (i % 2) ? N_RAIL_Q : N_RAIL_I;

		bwn_nphy_scale_offset_rssi(mac, 0, offset[i], core, rail,
						type);
	}

	BWN_RF_SETMASK(mac, B2055_C1_PD_RSSIMISC, 0xF8, state[0]);
	BWN_RF_SETMASK(mac, B2055_C2_PD_RSSIMISC, 0xF8, state[1]);

	switch (state[2]) {
	case 1:
		bwn_nphy_rssi_select(mac, 1, N_RSSI_NB);
		break;
	case 4:
		bwn_nphy_rssi_select(mac, 1, N_RSSI_W1);
		break;
	case 2:
		bwn_nphy_rssi_select(mac, 1, N_RSSI_W2);
		break;
	default:
		bwn_nphy_rssi_select(mac, 1, N_RSSI_W2);
		break;
	}

	switch (state[3]) {
	case 1:
		bwn_nphy_rssi_select(mac, 2, N_RSSI_NB);
		break;
	case 4:
		bwn_nphy_rssi_select(mac, 2, N_RSSI_W1);
		break;
	default:
		bwn_nphy_rssi_select(mac, 2, N_RSSI_W2);
		break;
	}

	bwn_nphy_rssi_select(mac, 0, type);

	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC1, regs_save_phy[0]);
	BWN_RF_WRITE(mac, B2055_C1_PD_RXTX, regs_save_radio[0]);
	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC2, regs_save_phy[1]);
	BWN_RF_WRITE(mac, B2055_C2_PD_RXTX, regs_save_radio[1]);

	bwn_nphy_classifier(mac, 7, class);
	bwn_nphy_write_clip_detection(mac, clip_state);
	/* Specs don't say about reset here, but it makes wl and b43 dumps
	   identical, it really seems wl performs this */
	bwn_nphy_reset_cca(mac);
}

/*
 * RSSI Calibration
 * http://bcm-v4.sipsolutions.net/802.11/PHY/N/RSSICal
 */
static void bwn_nphy_rssi_cal(struct bwn_mac *mac)
{
	if (mac->mac_phy.rev >= 19) {
		/* TODO */
	} else if (mac->mac_phy.rev >= 3) {
		bwn_nphy_rev3_rssi_cal(mac);
	} else {
		bwn_nphy_rev2_rssi_cal(mac, N_RSSI_NB);
		bwn_nphy_rev2_rssi_cal(mac, N_RSSI_W1);
		bwn_nphy_rev2_rssi_cal(mac, N_RSSI_W2);
	}
}

/**************************************************
 * Workarounds
 **************************************************/

static void bwn_nphy_gain_ctl_workarounds_rev19(struct bwn_mac *mac)
{
	/* TODO */
}

static void bwn_nphy_gain_ctl_workarounds_rev7(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;

	switch (phy->rev) {
	/* TODO */
	}
}

static void bwn_nphy_gain_ctl_workarounds_rev3(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	bool ghz5;
	bool ext_lna;
	uint16_t rssi_gain;
	struct bwn_nphy_gain_ctl_workaround_entry *e;
	uint8_t lpf_gain[6] = { 0x00, 0x06, 0x0C, 0x12, 0x12, 0x12 };
	uint8_t lpf_bits[6] = { 0, 1, 2, 3, 3, 3 };

	/* Prepare values */
	ghz5 = BWN_PHY_READ(mac, BWN_NPHY_BANDCTL)
		& BWN_NPHY_BANDCTL_5GHZ;
	ext_lna = ghz5 ? sc->sc_board_info.board_flags & BHND_BFL_EXTLNA_5GHZ :
		sc->sc_board_info.board_flags & BHND_BFL_EXTLNA;
	e = bwn_nphy_get_gain_ctl_workaround_ent(mac, ghz5, ext_lna);
	if (ghz5 && mac->mac_phy.rev >= 5)
		rssi_gain = 0x90;
	else
		rssi_gain = 0x50;

	BWN_PHY_SET(mac, BWN_NPHY_RXCTL, 0x0040);

	/* Set Clip 2 detect */
	BWN_PHY_SET(mac, BWN_NPHY_C1_CGAINI, BWN_NPHY_C1_CGAINI_CL2DETECT);
	BWN_PHY_SET(mac, BWN_NPHY_C2_CGAINI, BWN_NPHY_C2_CGAINI_CL2DETECT);

	BWN_RF_WRITE(mac, B2056_RX0 | B2056_RX_BIASPOLE_LNAG1_IDAC,
			0x17);
	BWN_RF_WRITE(mac, B2056_RX1 | B2056_RX_BIASPOLE_LNAG1_IDAC,
			0x17);
	BWN_RF_WRITE(mac, B2056_RX0 | B2056_RX_LNAG2_IDAC, 0xF0);
	BWN_RF_WRITE(mac, B2056_RX1 | B2056_RX_LNAG2_IDAC, 0xF0);
	BWN_RF_WRITE(mac, B2056_RX0 | B2056_RX_RSSI_POLE, 0x00);
	BWN_RF_WRITE(mac, B2056_RX1 | B2056_RX_RSSI_POLE, 0x00);
	BWN_RF_WRITE(mac, B2056_RX0 | B2056_RX_RSSI_GAIN,
			rssi_gain);
	BWN_RF_WRITE(mac, B2056_RX1 | B2056_RX_RSSI_GAIN,
			rssi_gain);
	BWN_RF_WRITE(mac, B2056_RX0 | B2056_RX_BIASPOLE_LNAA1_IDAC,
			0x17);
	BWN_RF_WRITE(mac, B2056_RX1 | B2056_RX_BIASPOLE_LNAA1_IDAC,
			0x17);
	BWN_RF_WRITE(mac, B2056_RX0 | B2056_RX_LNAA2_IDAC, 0xFF);
	BWN_RF_WRITE(mac, B2056_RX1 | B2056_RX_LNAA2_IDAC, 0xFF);

	bwn_ntab_write_bulk(mac, BWN_NTAB8(0, 8), 4, e->lna1_gain);
	bwn_ntab_write_bulk(mac, BWN_NTAB8(1, 8), 4, e->lna1_gain);
	bwn_ntab_write_bulk(mac, BWN_NTAB8(0, 16), 4, e->lna2_gain);
	bwn_ntab_write_bulk(mac, BWN_NTAB8(1, 16), 4, e->lna2_gain);
	bwn_ntab_write_bulk(mac, BWN_NTAB8(0, 32), 10, e->gain_db);
	bwn_ntab_write_bulk(mac, BWN_NTAB8(1, 32), 10, e->gain_db);
	bwn_ntab_write_bulk(mac, BWN_NTAB8(2, 32), 10, e->gain_bits);
	bwn_ntab_write_bulk(mac, BWN_NTAB8(3, 32), 10, e->gain_bits);
	bwn_ntab_write_bulk(mac, BWN_NTAB8(0, 0x40), 6, lpf_gain);
	bwn_ntab_write_bulk(mac, BWN_NTAB8(1, 0x40), 6, lpf_gain);
	bwn_ntab_write_bulk(mac, BWN_NTAB8(2, 0x40), 6, lpf_bits);
	bwn_ntab_write_bulk(mac, BWN_NTAB8(3, 0x40), 6, lpf_bits);

	BWN_PHY_WRITE(mac, BWN_NPHY_REV3_C1_INITGAIN_A, e->init_gain);
	BWN_PHY_WRITE(mac, BWN_NPHY_REV3_C2_INITGAIN_A, e->init_gain);

	bwn_ntab_write_bulk(mac, BWN_NTAB16(7, 0x106), 2,
				e->rfseq_init);

	BWN_PHY_WRITE(mac, BWN_NPHY_REV3_C1_CLIP_HIGAIN_A, e->cliphi_gain);
	BWN_PHY_WRITE(mac, BWN_NPHY_REV3_C2_CLIP_HIGAIN_A, e->cliphi_gain);
	BWN_PHY_WRITE(mac, BWN_NPHY_REV3_C1_CLIP_MEDGAIN_A, e->clipmd_gain);
	BWN_PHY_WRITE(mac, BWN_NPHY_REV3_C2_CLIP_MEDGAIN_A, e->clipmd_gain);
	BWN_PHY_WRITE(mac, BWN_NPHY_REV3_C1_CLIP_LOGAIN_A, e->cliplo_gain);
	BWN_PHY_WRITE(mac, BWN_NPHY_REV3_C2_CLIP_LOGAIN_A, e->cliplo_gain);

	BWN_PHY_SETMASK(mac, BWN_NPHY_CRSMINPOWER0, 0xFF00, e->crsmin);
	BWN_PHY_SETMASK(mac, BWN_NPHY_CRSMINPOWERL0, 0xFF00, e->crsminl);
	BWN_PHY_SETMASK(mac, BWN_NPHY_CRSMINPOWERU0, 0xFF00, e->crsminu);
	BWN_PHY_WRITE(mac, BWN_NPHY_C1_NBCLIPTHRES, e->nbclip);
	BWN_PHY_WRITE(mac, BWN_NPHY_C2_NBCLIPTHRES, e->nbclip);
	BWN_PHY_SETMASK(mac, BWN_NPHY_C1_CLIPWBTHRES,
			~BWN_NPHY_C1_CLIPWBTHRES_CLIP2, e->wlclip);
	BWN_PHY_SETMASK(mac, BWN_NPHY_C2_CLIPWBTHRES,
			~BWN_NPHY_C2_CLIPWBTHRES_CLIP2, e->wlclip);
	BWN_PHY_WRITE(mac, BWN_NPHY_CCK_SHIFTB_REF, 0x809C);
}

static void bwn_nphy_gain_ctl_workarounds_rev1_2(struct bwn_mac *mac)
{
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;

	uint8_t i, j;
	uint8_t code;
	uint16_t tmp;
	uint8_t rfseq_events[3] = { 6, 8, 7 };
	uint8_t rfseq_delays[3] = { 10, 30, 1 };

	/* Set Clip 2 detect */
	BWN_PHY_SET(mac, BWN_NPHY_C1_CGAINI, BWN_NPHY_C1_CGAINI_CL2DETECT);
	BWN_PHY_SET(mac, BWN_NPHY_C2_CGAINI, BWN_NPHY_C2_CGAINI_CL2DETECT);

	/* Set narrowband clip threshold */
	BWN_PHY_WRITE(mac, BWN_NPHY_C1_NBCLIPTHRES, 0x84);
	BWN_PHY_WRITE(mac, BWN_NPHY_C2_NBCLIPTHRES, 0x84);

	if (!bwn_is_40mhz(mac)) {
		/* Set dwell lengths */
		BWN_PHY_WRITE(mac, BWN_NPHY_CLIP1_NBDWELL_LEN, 0x002B);
		BWN_PHY_WRITE(mac, BWN_NPHY_CLIP2_NBDWELL_LEN, 0x002B);
		BWN_PHY_WRITE(mac, BWN_NPHY_W1CLIP1_DWELL_LEN, 0x0009);
		BWN_PHY_WRITE(mac, BWN_NPHY_W1CLIP2_DWELL_LEN, 0x0009);
	}

	/* Set wideband clip 2 threshold */
	BWN_PHY_SETMASK(mac, BWN_NPHY_C1_CLIPWBTHRES,
			~BWN_NPHY_C1_CLIPWBTHRES_CLIP2, 21);
	BWN_PHY_SETMASK(mac, BWN_NPHY_C2_CLIPWBTHRES,
			~BWN_NPHY_C2_CLIPWBTHRES_CLIP2, 21);

	if (!bwn_is_40mhz(mac)) {
		BWN_PHY_SETMASK(mac, BWN_NPHY_C1_CGAINI,
			~BWN_NPHY_C1_CGAINI_GAINBKOFF, 0x1);
		BWN_PHY_SETMASK(mac, BWN_NPHY_C2_CGAINI,
			~BWN_NPHY_C2_CGAINI_GAINBKOFF, 0x1);
		BWN_PHY_SETMASK(mac, BWN_NPHY_C1_CCK_CGAINI,
			~BWN_NPHY_C1_CCK_CGAINI_GAINBKOFF, 0x1);
		BWN_PHY_SETMASK(mac, BWN_NPHY_C2_CCK_CGAINI,
			~BWN_NPHY_C2_CCK_CGAINI_GAINBKOFF, 0x1);
	}

	BWN_PHY_WRITE(mac, BWN_NPHY_CCK_SHIFTB_REF, 0x809C);

	if (nphy->gain_boost) {
		if (bwn_current_band(mac) == BWN_BAND_2G &&
		    bwn_is_40mhz(mac))
			code = 4;
		else
			code = 5;
	} else {
		code = bwn_is_40mhz(mac) ? 6 : 7;
	}

	/* Set HPVGA2 index */
	BWN_PHY_SETMASK(mac, BWN_NPHY_C1_INITGAIN, ~BWN_NPHY_C1_INITGAIN_HPVGA2,
			code << BWN_NPHY_C1_INITGAIN_HPVGA2_SHIFT);
	BWN_PHY_SETMASK(mac, BWN_NPHY_C2_INITGAIN, ~BWN_NPHY_C2_INITGAIN_HPVGA2,
			code << BWN_NPHY_C2_INITGAIN_HPVGA2_SHIFT);

	BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_ADDR, 0x1D06);
	/* specs say about 2 loops, but wl does 4 */
	for (i = 0; i < 4; i++)
		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATALO, (code << 8 | 0x7C));

	bwn_nphy_adjust_lna_gain_table(mac);

	if (nphy->elna_gain_config) {
		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_ADDR, 0x0808);
		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATALO, 0x0);
		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATALO, 0x1);
		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATALO, 0x1);
		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATALO, 0x1);

		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_ADDR, 0x0C08);
		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATALO, 0x0);
		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATALO, 0x1);
		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATALO, 0x1);
		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATALO, 0x1);

		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_ADDR, 0x1D06);
		/* specs say about 2 loops, but wl does 4 */
		for (i = 0; i < 4; i++)
			BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATALO,
						(code << 8 | 0x74));
	}

	if (mac->mac_phy.rev == 2) {
		for (i = 0; i < 4; i++) {
			BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_ADDR,
					(0x0400 * i) + 0x0020);
			for (j = 0; j < 21; j++) {
				tmp = j * (i < 2 ? 3 : 1);
				BWN_PHY_WRITE(mac,
					BWN_NPHY_TABLE_DATALO, tmp);
			}
		}
	}

	bwn_nphy_set_rf_sequence(mac, 5, rfseq_events, rfseq_delays, 3);
	BWN_PHY_SETMASK(mac, BWN_NPHY_OVER_DGAIN1,
		~BWN_NPHY_OVER_DGAIN_CCKDGECV & 0xFFFF,
		0x5A << BWN_NPHY_OVER_DGAIN_CCKDGECV_SHIFT);

	if (bwn_current_band(mac) == BWN_BAND_2G)
		BWN_PHY_SETMASK(mac, BWN_PHY_N(0xC5D), 0xFF80, 4);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/WorkaroundsGainCtrl */
static void bwn_nphy_gain_ctl_workarounds(struct bwn_mac *mac)
{
	if (mac->mac_phy.rev >= 19)
		bwn_nphy_gain_ctl_workarounds_rev19(mac);
	else if (mac->mac_phy.rev >= 7)
		bwn_nphy_gain_ctl_workarounds_rev7(mac);
	else if (mac->mac_phy.rev >= 3)
		bwn_nphy_gain_ctl_workarounds_rev3(mac);
	else
		bwn_nphy_gain_ctl_workarounds_rev1_2(mac);
}

static int bwn_nphy_workarounds_rev7plus(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_phy *phy = &mac->mac_phy;

	/* TX to RX */
	uint8_t tx2rx_events[7] = { 4, 3, 5, 2, 1, 8, 31, };
	uint8_t tx2rx_delays[7] = { 8, 4, 4, 4, 4, 6, 1, };
	/* RX to TX */
	uint8_t rx2tx_events_ipa[9] = { 0x0, 0x1, 0x2, 0x8, 0x5, 0x6, 0xF, 0x3,
					0x1F };
	uint8_t rx2tx_delays_ipa[9] = { 8, 6, 6, 4, 4, 16, 43, 1, 1 };

	static const uint16_t ntab7_15e_16e[] = { 0, 0x10f, 0x10f };
	uint8_t ntab7_138_146[] = { 0x11, 0x11 };
	uint8_t ntab7_133[] = { 0x77, 0x11, 0x11 };

	uint16_t lpf_ofdm_20mhz[2], lpf_ofdm_40mhz[2], lpf_11b[2];
	uint16_t bcap_val;
	int16_t bcap_val_11b[2], bcap_val_11n_20[2], bcap_val_11n_40[2];
	uint16_t scap_val;
	int16_t scap_val_11b[2], scap_val_11n_20[2], scap_val_11n_40[2];
	bool rccal_ovrd = false;

	uint16_t bias, conv, filt;

	uint32_t noise_tbl[2];

	uint32_t tmp32;
	uint8_t core;

	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_A0, 0x0125);
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_A1, 0x01b3);
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_A2, 0x0105);
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_B0, 0x016e);
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_B1, 0x00cd);
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_B2, 0x0020);

	if (phy->rev == 7) {
		BWN_PHY_SET(mac, BWN_NPHY_FINERX2_CGC, 0x10);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN0, 0xFF80, 0x0020);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN0, 0x80FF, 0x2700);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN1, 0xFF80, 0x002E);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN1, 0x80FF, 0x3300);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN2, 0xFF80, 0x0037);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN2, 0x80FF, 0x3A00);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN3, 0xFF80, 0x003C);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN3, 0x80FF, 0x3E00);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN4, 0xFF80, 0x003E);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN4, 0x80FF, 0x3F00);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN5, 0xFF80, 0x0040);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN5, 0x80FF, 0x4000);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN6, 0xFF80, 0x0040);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN6, 0x80FF, 0x4000);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN7, 0xFF80, 0x0040);
		BWN_PHY_SETMASK(mac, BWN_NPHY_FREQGAIN7, 0x80FF, 0x4000);
	}

	if (phy->rev >= 16) {
		BWN_PHY_WRITE(mac, BWN_NPHY_FORCEFRONT0, 0x7ff);
		BWN_PHY_WRITE(mac, BWN_NPHY_FORCEFRONT1, 0x7ff);
	} else if (phy->rev <= 8) {
		BWN_PHY_WRITE(mac, BWN_NPHY_FORCEFRONT0, 0x1B0);
		BWN_PHY_WRITE(mac, BWN_NPHY_FORCEFRONT1, 0x1B0);
	}

	if (phy->rev >= 16)
		BWN_PHY_SETMASK(mac, BWN_NPHY_TXTAILCNT, ~0xFF, 0xa0);
	else if (phy->rev >= 8)
		BWN_PHY_SETMASK(mac, BWN_NPHY_TXTAILCNT, ~0xFF, 0x72);

	bwn_ntab_write(mac, BWN_NTAB16(8, 0x00), 2);
	bwn_ntab_write(mac, BWN_NTAB16(8, 0x10), 2);
	tmp32 = bwn_ntab_read(mac, BWN_NTAB32(30, 0));
	tmp32 &= 0xffffff;
	bwn_ntab_write(mac, BWN_NTAB32(30, 0), tmp32);
	bwn_ntab_write_bulk(mac, BWN_NTAB16(7, 0x15d), 3, ntab7_15e_16e);
	bwn_ntab_write_bulk(mac, BWN_NTAB16(7, 0x16d), 3, ntab7_15e_16e);

	bwn_nphy_set_rf_sequence(mac, 1, tx2rx_events, tx2rx_delays,
				 nitems(tx2rx_events));
	if (bwn_nphy_ipa(mac))
		bwn_nphy_set_rf_sequence(mac, 0, rx2tx_events_ipa,
				rx2tx_delays_ipa, nitems(rx2tx_events_ipa));

	BWN_PHY_SETMASK(mac, BWN_NPHY_EPS_OVERRIDEI_0, 0x3FFF, 0x4000);
	BWN_PHY_SETMASK(mac, BWN_NPHY_EPS_OVERRIDEI_1, 0x3FFF, 0x4000);

	for (core = 0; core < 2; core++) {
		lpf_ofdm_20mhz[core] = bwn_nphy_read_lpf_ctl(mac, 0x154 + core * 0x10);
		lpf_ofdm_40mhz[core] = bwn_nphy_read_lpf_ctl(mac, 0x159 + core * 0x10);
		lpf_11b[core] = bwn_nphy_read_lpf_ctl(mac, 0x152 + core * 0x10);
	}

	bcap_val = BWN_RF_READ(mac, R2057_RCCAL_BCAP_VAL);
	scap_val = BWN_RF_READ(mac, R2057_RCCAL_SCAP_VAL);

	if (bwn_nphy_ipa(mac)) {
		bool ghz2 = bwn_current_band(mac) == BWN_BAND_2G;

		switch (phy->rf_rev) {
		case 5:
			/* Check radio version (to be 0) by PHY rev for now */
			if (phy->rev == 8 && bwn_is_40mhz(mac)) {
				for (core = 0; core < 2; core++) {
					scap_val_11b[core] = scap_val;
					bcap_val_11b[core] = bcap_val;
					scap_val_11n_20[core] = scap_val;
					bcap_val_11n_20[core] = bcap_val;
					scap_val_11n_40[core] = 0xc;
					bcap_val_11n_40[core] = 0xc;
				}

				rccal_ovrd = true;
			}
			if (phy->rev == 9) {
				/* TODO: Radio version 1 (e.g. BCM5357B0) */
			}
			break;
		case 7:
		case 8:
			for (core = 0; core < 2; core++) {
				scap_val_11b[core] = scap_val;
				bcap_val_11b[core] = bcap_val;
				lpf_ofdm_20mhz[core] = 4;
				lpf_11b[core] = 1;
				if (bwn_current_band(mac) == BWN_BAND_2G) {
					scap_val_11n_20[core] = 0xc;
					bcap_val_11n_20[core] = 0xc;
					scap_val_11n_40[core] = 0xa;
					bcap_val_11n_40[core] = 0xa;
				} else {
					scap_val_11n_20[core] = 0x14;
					bcap_val_11n_20[core] = 0x14;
					scap_val_11n_40[core] = 0xf;
					bcap_val_11n_40[core] = 0xf;
				}
			}

			rccal_ovrd = true;
			break;
		case 9:
			for (core = 0; core < 2; core++) {
				bcap_val_11b[core] = bcap_val;
				scap_val_11b[core] = scap_val;
				lpf_11b[core] = 1;

				if (ghz2) {
					bcap_val_11n_20[core] = bcap_val + 13;
					scap_val_11n_20[core] = scap_val + 15;
				} else {
					bcap_val_11n_20[core] = bcap_val + 14;
					scap_val_11n_20[core] = scap_val + 15;
				}
				lpf_ofdm_20mhz[core] = 4;

				if (ghz2) {
					bcap_val_11n_40[core] = bcap_val - 7;
					scap_val_11n_40[core] = scap_val - 5;
				} else {
					bcap_val_11n_40[core] = bcap_val + 2;
					scap_val_11n_40[core] = scap_val + 4;
				}
				lpf_ofdm_40mhz[core] = 4;
			}

			rccal_ovrd = true;
			break;
		case 14:
			for (core = 0; core < 2; core++) {
				bcap_val_11b[core] = bcap_val;
				scap_val_11b[core] = scap_val;
				lpf_11b[core] = 1;
			}

			bcap_val_11n_20[0] = bcap_val + 20;
			scap_val_11n_20[0] = scap_val + 20;
			lpf_ofdm_20mhz[0] = 3;

			bcap_val_11n_20[1] = bcap_val + 16;
			scap_val_11n_20[1] = scap_val + 16;
			lpf_ofdm_20mhz[1] = 3;

			bcap_val_11n_40[0] = bcap_val + 20;
			scap_val_11n_40[0] = scap_val + 20;
			lpf_ofdm_40mhz[0] = 4;

			bcap_val_11n_40[1] = bcap_val + 10;
			scap_val_11n_40[1] = scap_val + 10;
			lpf_ofdm_40mhz[1] = 4;

			rccal_ovrd = true;
			break;
		}
	} else {
		if (phy->rf_rev == 5) {
			for (core = 0; core < 2; core++) {
				lpf_ofdm_20mhz[core] = 1;
				lpf_ofdm_40mhz[core] = 3;
				scap_val_11b[core] = scap_val;
				bcap_val_11b[core] = bcap_val;
				scap_val_11n_20[core] = 0x11;
				scap_val_11n_40[core] = 0x11;
				bcap_val_11n_20[core] = 0x13;
				bcap_val_11n_40[core] = 0x13;
			}

			rccal_ovrd = true;
		}
	}
	if (rccal_ovrd) {
		uint16_t rx2tx_lut_20_11b[2], rx2tx_lut_20_11n[2], rx2tx_lut_40_11n[2];
		uint8_t rx2tx_lut_extra = 1;

		for (core = 0; core < 2; core++) {
			bcap_val_11b[core] = bwn_clamp_val(bcap_val_11b[core], 0, 0x1f);
			scap_val_11b[core] = bwn_clamp_val(scap_val_11b[core], 0, 0x1f);
			bcap_val_11n_20[core] = bwn_clamp_val(bcap_val_11n_20[core], 0, 0x1f);
			scap_val_11n_20[core] = bwn_clamp_val(scap_val_11n_20[core], 0, 0x1f);
			bcap_val_11n_40[core] = bwn_clamp_val(bcap_val_11n_40[core], 0, 0x1f);
			scap_val_11n_40[core] = bwn_clamp_val(scap_val_11n_40[core], 0, 0x1f);

			rx2tx_lut_20_11b[core] = (rx2tx_lut_extra << 13) |
						 (bcap_val_11b[core] << 8) |
						 (scap_val_11b[core] << 3) |
						 lpf_11b[core];
			rx2tx_lut_20_11n[core] = (rx2tx_lut_extra << 13) |
						 (bcap_val_11n_20[core] << 8) |
						 (scap_val_11n_20[core] << 3) |
						 lpf_ofdm_20mhz[core];
			rx2tx_lut_40_11n[core] = (rx2tx_lut_extra << 13) |
						 (bcap_val_11n_40[core] << 8) |
						 (scap_val_11n_40[core] << 3) |
						 lpf_ofdm_40mhz[core];
		}

		for (core = 0; core < 2; core++) {
			bwn_ntab_write(mac, BWN_NTAB16(7, 0x152 + core * 16),
				       rx2tx_lut_20_11b[core]);
			bwn_ntab_write(mac, BWN_NTAB16(7, 0x153 + core * 16),
				       rx2tx_lut_20_11n[core]);
			bwn_ntab_write(mac, BWN_NTAB16(7, 0x154 + core * 16),
				       rx2tx_lut_20_11n[core]);
			bwn_ntab_write(mac, BWN_NTAB16(7, 0x155 + core * 16),
				       rx2tx_lut_40_11n[core]);
			bwn_ntab_write(mac, BWN_NTAB16(7, 0x156 + core * 16),
				       rx2tx_lut_40_11n[core]);
			bwn_ntab_write(mac, BWN_NTAB16(7, 0x157 + core * 16),
				       rx2tx_lut_40_11n[core]);
			bwn_ntab_write(mac, BWN_NTAB16(7, 0x158 + core * 16),
				       rx2tx_lut_40_11n[core]);
			bwn_ntab_write(mac, BWN_NTAB16(7, 0x159 + core * 16),
				       rx2tx_lut_40_11n[core]);
		}
	}

	BWN_PHY_WRITE(mac, 0x32F, 0x3);

	if (phy->rf_rev == 4 || phy->rf_rev == 6)
		bwn_nphy_rf_ctl_override_rev7(mac, 4, 1, 3, false, 0);

	if (phy->rf_rev == 3 || phy->rf_rev == 4 || phy->rf_rev == 6) {
		if (sc->sc_board_info.board_srom_rev &&
		    sc->sc_board_info.board_flags2 & BHND_BFL2_IPALVLSHIFT_3P3) {
			BWN_RF_WRITE(mac, 0x5, 0x05);
			BWN_RF_WRITE(mac, 0x6, 0x30);
			BWN_RF_WRITE(mac, 0x7, 0x00);
			BWN_RF_SET(mac, 0x4f, 0x1);
			BWN_RF_SET(mac, 0xd4, 0x1);
			bias = 0x1f;
			conv = 0x6f;
			filt = 0xaa;
		} else {
			bias = 0x2b;
			conv = 0x7f;
			filt = 0xee;
		}
		if (bwn_current_band(mac) == BWN_BAND_2G) {
			for (core = 0; core < 2; core++) {
				if (core == 0) {
					BWN_RF_WRITE(mac, 0x5F, bias);
					BWN_RF_WRITE(mac, 0x64, conv);
					BWN_RF_WRITE(mac, 0x66, filt);
				} else {
					BWN_RF_WRITE(mac, 0xE8, bias);
					BWN_RF_WRITE(mac, 0xE9, conv);
					BWN_RF_WRITE(mac, 0xEB, filt);
				}
			}
		}
	}

	if (bwn_nphy_ipa(mac)) {
		if (bwn_current_band(mac) == BWN_BAND_2G) {
			if (phy->rf_rev == 3 || phy->rf_rev == 4 ||
			    phy->rf_rev == 6) {
				for (core = 0; core < 2; core++) {
					if (core == 0)
						BWN_RF_WRITE(mac, 0x51,
								0x7f);
					else
						BWN_RF_WRITE(mac, 0xd6,
								0x7f);
				}
			}
			switch (phy->rf_rev) {
			case 3:
				for (core = 0; core < 2; core++) {
					if (core == 0) {
						BWN_RF_WRITE(mac, 0x64,
								0x13);
						BWN_RF_WRITE(mac, 0x5F,
								0x1F);
						BWN_RF_WRITE(mac, 0x66,
								0xEE);
						BWN_RF_WRITE(mac, 0x59,
								0x8A);
						BWN_RF_WRITE(mac, 0x80,
								0x3E);
					} else {
						BWN_RF_WRITE(mac, 0x69,
								0x13);
						BWN_RF_WRITE(mac, 0xE8,
								0x1F);
						BWN_RF_WRITE(mac, 0xEB,
								0xEE);
						BWN_RF_WRITE(mac, 0xDE,
								0x8A);
						BWN_RF_WRITE(mac, 0x105,
								0x3E);
					}
				}
				break;
			case 7:
			case 8:
				if (!bwn_is_40mhz(mac)) {
					BWN_RF_WRITE(mac, 0x5F, 0x14);
					BWN_RF_WRITE(mac, 0xE8, 0x12);
				} else {
					BWN_RF_WRITE(mac, 0x5F, 0x16);
					BWN_RF_WRITE(mac, 0xE8, 0x16);
				}
				break;
			case 14:
				for (core = 0; core < 2; core++) {
					int o = core ? 0x85 : 0;

					BWN_RF_WRITE(mac, o + R2057_IPA2G_CASCONV_CORE0, 0x13);
					BWN_RF_WRITE(mac, o + R2057_TXMIX2G_TUNE_BOOST_PU_CORE0, 0x21);
					BWN_RF_WRITE(mac, o + R2057_IPA2G_BIAS_FILTER_CORE0, 0xff);
					BWN_RF_WRITE(mac, o + R2057_PAD2G_IDACS_CORE0, 0x88);
					BWN_RF_WRITE(mac, o + R2057_PAD2G_TUNE_PUS_CORE0, 0x23);
					BWN_RF_WRITE(mac, o + R2057_IPA2G_IMAIN_CORE0, 0x16);
					BWN_RF_WRITE(mac, o + R2057_PAD_BIAS_FILTER_BWS_CORE0, 0x3e);
					BWN_RF_WRITE(mac, o + R2057_BACKUP1_CORE0, 0x10);
				}
				break;
			}
		} else {
			uint16_t freq = bwn_get_centre_freq(mac);
			if ((freq >= 5180 && freq <= 5230) ||
			    (freq >= 5745 && freq <= 5805)) {
				BWN_RF_WRITE(mac, 0x7D, 0xFF);
				BWN_RF_WRITE(mac, 0xFE, 0xFF);
			}
		}
	} else {
		if (phy->rf_rev != 5) {
			for (core = 0; core < 2; core++) {
				if (core == 0) {
					BWN_RF_WRITE(mac, 0x5c, 0x61);
					BWN_RF_WRITE(mac, 0x51, 0x70);
				} else {
					BWN_RF_WRITE(mac, 0xe1, 0x61);
					BWN_RF_WRITE(mac, 0xd6, 0x70);
				}
			}
		}
	}

	if (phy->rf_rev == 4) {
		bwn_ntab_write(mac, BWN_NTAB16(8, 0x05), 0x20);
		bwn_ntab_write(mac, BWN_NTAB16(8, 0x15), 0x20);
		for (core = 0; core < 2; core++) {
			if (core == 0) {
				BWN_RF_WRITE(mac, 0x1a1, 0x00);
				BWN_RF_WRITE(mac, 0x1a2, 0x3f);
				BWN_RF_WRITE(mac, 0x1a6, 0x3f);
			} else {
				BWN_RF_WRITE(mac, 0x1a7, 0x00);
				BWN_RF_WRITE(mac, 0x1ab, 0x3f);
				BWN_RF_WRITE(mac, 0x1ac, 0x3f);
			}
		}
	} else {
		BWN_PHY_SET(mac, BWN_NPHY_AFECTL_C1, 0x4);
		BWN_PHY_SET(mac, BWN_NPHY_AFECTL_OVER1, 0x4);
		BWN_PHY_SET(mac, BWN_NPHY_AFECTL_C2, 0x4);
		BWN_PHY_SET(mac, BWN_NPHY_AFECTL_OVER, 0x4);

		BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_C1, ~0x1);
		BWN_PHY_SET(mac, BWN_NPHY_AFECTL_OVER1, 0x1);
		BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_C2, ~0x1);
		BWN_PHY_SET(mac, BWN_NPHY_AFECTL_OVER, 0x1);
		bwn_ntab_write(mac, BWN_NTAB16(8, 0x05), 0);
		bwn_ntab_write(mac, BWN_NTAB16(8, 0x15), 0);

		BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_C1, ~0x4);
		BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_OVER1, ~0x4);
		BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_C2, ~0x4);
		BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_OVER, ~0x4);
	}

	BWN_PHY_WRITE(mac, BWN_NPHY_ENDROP_TLEN, 0x2);

	bwn_ntab_write(mac, BWN_NTAB32(16, 0x100), 20);
	bwn_ntab_write_bulk(mac, BWN_NTAB8(7, 0x138), 2, ntab7_138_146);
	bwn_ntab_write(mac, BWN_NTAB16(7, 0x141), 0x77);
	bwn_ntab_write_bulk(mac, BWN_NTAB8(7, 0x133), 3, ntab7_133);
	bwn_ntab_write_bulk(mac, BWN_NTAB8(7, 0x146), 2, ntab7_138_146);
	bwn_ntab_write(mac, BWN_NTAB16(7, 0x123), 0x77);
	bwn_ntab_write(mac, BWN_NTAB16(7, 0x12A), 0x77);

	bwn_ntab_read_bulk(mac, BWN_NTAB32(16, 0x02), 1, noise_tbl);
	noise_tbl[1] = bwn_is_40mhz(mac) ? 0x14D : 0x18D;
	bwn_ntab_write_bulk(mac, BWN_NTAB32(16, 0x02), 2, noise_tbl);

	bwn_ntab_read_bulk(mac, BWN_NTAB32(16, 0x7E), 1, noise_tbl);
	noise_tbl[1] = bwn_is_40mhz(mac) ? 0x14D : 0x18D;
	bwn_ntab_write_bulk(mac, BWN_NTAB32(16, 0x7E), 2, noise_tbl);

	bwn_nphy_gain_ctl_workarounds(mac);

	/* TODO
	bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x08), 4,
			    aux_adc_vmid_rev7_core0);
	bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x18), 4,
			    aux_adc_vmid_rev7_core1);
	bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x0C), 4,
			    aux_adc_gain_rev7);
	bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x1C), 4,
			    aux_adc_gain_rev7);
	*/

	return (0);
}

static int bwn_nphy_workarounds_rev3plus(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;

	/* TX to RX */
	uint8_t tx2rx_events[7] = { 0x4, 0x3, 0x5, 0x2, 0x1, 0x8, 0x1F };
	uint8_t tx2rx_delays[7] = { 8, 4, 4, 4, 4, 6, 1 };
	/* RX to TX */
	uint8_t rx2tx_events_ipa[9] = { 0x0, 0x1, 0x2, 0x8, 0x5, 0x6, 0xF, 0x3,
					0x1F };
	uint8_t rx2tx_delays_ipa[9] = { 8, 6, 6, 4, 4, 16, 43, 1, 1 };
	uint8_t rx2tx_events[9] = { 0x0, 0x1, 0x2, 0x8, 0x5, 0x6, 0x3, 0x4, 0x1F };
	uint8_t rx2tx_delays[9] = { 8, 6, 6, 4, 4, 18, 42, 1, 1 };

	uint16_t vmids[5][4] = {
		{ 0xa2, 0xb4, 0xb4, 0x89, }, /* 0 */
		{ 0xb4, 0xb4, 0xb4, 0x24, }, /* 1 */
		{ 0xa2, 0xb4, 0xb4, 0x74, }, /* 2 */
		{ 0xa2, 0xb4, 0xb4, 0x270, }, /* 3 */
		{ 0xa2, 0xb4, 0xb4, 0x00, }, /* 4 and 5 */
	};
	uint16_t gains[5][4] = {
		{ 0x02, 0x02, 0x02, 0x00, }, /* 0 */
		{ 0x02, 0x02, 0x02, 0x02, }, /* 1 */
		{ 0x02, 0x02, 0x02, 0x04, }, /* 2 */
		{ 0x02, 0x02, 0x02, 0x00, }, /* 3 */
		{ 0x02, 0x02, 0x02, 0x00, }, /* 4 and 5 */
	};
	uint16_t *vmid, *gain;

	const char *pdet_range_var;
	uint8_t pdet_range;
	uint16_t tmp16;
	uint32_t tmp32;
	int error;

	BWN_PHY_WRITE(mac, BWN_NPHY_FORCEFRONT0, 0x1f8);
	BWN_PHY_WRITE(mac, BWN_NPHY_FORCEFRONT1, 0x1f8);

	tmp32 = bwn_ntab_read(mac, BWN_NTAB32(30, 0));
	tmp32 &= 0xffffff;
	bwn_ntab_write(mac, BWN_NTAB32(30, 0), tmp32);

	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_A0, 0x0125);
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_A1, 0x01B3);
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_A2, 0x0105);
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_B0, 0x016E);
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_B1, 0x00CD);
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_B2, 0x0020);

	BWN_PHY_WRITE(mac, BWN_NPHY_REV3_C1_CLIP_LOGAIN_B, 0x000C);
	BWN_PHY_WRITE(mac, BWN_NPHY_REV3_C2_CLIP_LOGAIN_B, 0x000C);

	/* TX to RX */
	bwn_nphy_set_rf_sequence(mac, 1, tx2rx_events, tx2rx_delays,
				 nitems(tx2rx_events));

	/* RX to TX */
	if (bwn_nphy_ipa(mac))
		bwn_nphy_set_rf_sequence(mac, 0, rx2tx_events_ipa,
				rx2tx_delays_ipa, nitems(rx2tx_events_ipa));
	if (nphy->hw_phyrxchain != 3 &&
	    nphy->hw_phyrxchain != nphy->hw_phytxchain) {
		if (bwn_nphy_ipa(mac)) {
			rx2tx_delays[5] = 59;
			rx2tx_delays[6] = 1;
			rx2tx_events[7] = 0x1F;
		}
		bwn_nphy_set_rf_sequence(mac, 0, rx2tx_events, rx2tx_delays,
					 nitems(rx2tx_events));
	}

	tmp16 = (bwn_current_band(mac) == BWN_BAND_2G) ?
		0x2 : 0x9C40;
	BWN_PHY_WRITE(mac, BWN_NPHY_ENDROP_TLEN, tmp16);

	BWN_PHY_SETMASK(mac, BWN_NPHY_SGILTRNOFFSET, 0xF0FF, 0x0700);

	if (!bwn_is_40mhz(mac)) {
		bwn_ntab_write(mac, BWN_NTAB32(16, 3), 0x18D);
		bwn_ntab_write(mac, BWN_NTAB32(16, 127), 0x18D);
	} else {
		bwn_ntab_write(mac, BWN_NTAB32(16, 3), 0x14D);
		bwn_ntab_write(mac, BWN_NTAB32(16, 127), 0x14D);
	}

	bwn_nphy_gain_ctl_workarounds(mac);

	bwn_ntab_write(mac, BWN_NTAB16(8, 0), 2);
	bwn_ntab_write(mac, BWN_NTAB16(8, 16), 2);

	if (bwn_current_band(mac) == BWN_BAND_2G)
		pdet_range_var = BHND_NVAR_PDETRANGE2G;
	else
		pdet_range_var = BHND_NVAR_PDETRANGE5G;

	error = bhnd_nvram_getvar_uint8(sc->sc_dev, pdet_range_var,
	    &pdet_range);
	if (error) {
		BWN_ERRPRINTF(mac->mac_sc, "Error reading PDet range %s from "
		    "NVRAM: %d\n", pdet_range_var, error);
		return (error);
	}

	/* uint16_t min() */
	vmid = vmids[min(pdet_range, 4)];
	gain = gains[min(pdet_range, 4)];
	switch (pdet_range) {
	case 3:
		if (!(mac->mac_phy.rev >= 4 &&
		      bwn_current_band(mac) == BWN_BAND_2G))
			break;
		/* FALL THROUGH */
	case 0:
	case 1:
		bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x08), 4, vmid);
		bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x18), 4, vmid);
		bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x0c), 4, gain);
		bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x1c), 4, gain);
		break;
	case 2:
		if (mac->mac_phy.rev >= 6) {
			if (bwn_current_band(mac) == BWN_BAND_2G)
				vmid[3] = 0x94;
			else
				vmid[3] = 0x8e;
			gain[3] = 3;
		} else if (mac->mac_phy.rev == 5) {
			vmid[3] = 0x84;
			gain[3] = 2;
		}
		bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x08), 4, vmid);
		bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x18), 4, vmid);
		bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x0c), 4, gain);
		bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x1c), 4, gain);
		break;
	case 4:
	case 5:
		if (bwn_current_band(mac) != BWN_BAND_2G) {
			if (pdet_range == 4) {
				vmid[3] = 0x8e;
				tmp16 = 0x96;
				gain[3] = 0x2;
			} else {
				vmid[3] = 0x89;
				tmp16 = 0x89;
				gain[3] = 0;
			}
		} else {
			if (pdet_range == 4) {
				vmid[3] = 0x89;
				tmp16 = 0x8b;
				gain[3] = 0x2;
			} else {
				vmid[3] = 0x74;
				tmp16 = 0x70;
				gain[3] = 0;
			}
		}
		bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x08), 4, vmid);
		bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x0c), 4, gain);
		vmid[3] = tmp16;
		bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x18), 4, vmid);
		bwn_ntab_write_bulk(mac, BWN_NTAB16(8, 0x1c), 4, gain);
		break;
	}

	BWN_RF_WRITE(mac, B2056_RX0 | B2056_RX_MIXA_MAST_BIAS, 0x00);
	BWN_RF_WRITE(mac, B2056_RX1 | B2056_RX_MIXA_MAST_BIAS, 0x00);
	BWN_RF_WRITE(mac, B2056_RX0 | B2056_RX_MIXA_BIAS_MAIN, 0x06);
	BWN_RF_WRITE(mac, B2056_RX1 | B2056_RX_MIXA_BIAS_MAIN, 0x06);
	BWN_RF_WRITE(mac, B2056_RX0 | B2056_RX_MIXA_BIAS_AUX, 0x07);
	BWN_RF_WRITE(mac, B2056_RX1 | B2056_RX_MIXA_BIAS_AUX, 0x07);
	BWN_RF_WRITE(mac, B2056_RX0 | B2056_RX_MIXA_LOB_BIAS, 0x88);
	BWN_RF_WRITE(mac, B2056_RX1 | B2056_RX_MIXA_LOB_BIAS, 0x88);
	BWN_RF_WRITE(mac, B2056_RX0 | B2056_RX_MIXA_CMFB_IDAC, 0x00);
	BWN_RF_WRITE(mac, B2056_RX1 | B2056_RX_MIXA_CMFB_IDAC, 0x00);
	BWN_RF_WRITE(mac, B2056_RX0 | B2056_RX_MIXG_CMFB_IDAC, 0x00);
	BWN_RF_WRITE(mac, B2056_RX1 | B2056_RX_MIXG_CMFB_IDAC, 0x00);

	/* N PHY WAR TX Chain Update with hw_phytxchain as argument */

	if ((sc->sc_board_info.board_flags2 & BHND_BFL2_APLL_WAR &&
	     bwn_current_band(mac) == BWN_BAND_5G) ||
	    (sc->sc_board_info.board_flags2 & BHND_BFL2_GPLL_WAR2 &&
	     bwn_current_band(mac) == BWN_BAND_2G))
		tmp32 = 0x00088888;
	else
		tmp32 = 0x88888888;
	bwn_ntab_write(mac, BWN_NTAB32(30, 1), tmp32);
	bwn_ntab_write(mac, BWN_NTAB32(30, 2), tmp32);
	bwn_ntab_write(mac, BWN_NTAB32(30, 3), tmp32);

	if (mac->mac_phy.rev == 4 &&
	    bwn_current_band(mac) == BWN_BAND_5G) {
		BWN_RF_WRITE(mac, B2056_TX0 | B2056_TX_GMBB_IDAC,
				0x70);
		BWN_RF_WRITE(mac, B2056_TX1 | B2056_TX_GMBB_IDAC,
				0x70);
	}

	/* Dropped probably-always-true condition */
	BWN_PHY_WRITE(mac, BWN_NPHY_ED_CRS40ASSERTTHRESH0, 0x03eb);
	BWN_PHY_WRITE(mac, BWN_NPHY_ED_CRS40ASSERTTHRESH1, 0x03eb);
	BWN_PHY_WRITE(mac, BWN_NPHY_ED_CRS40DEASSERTTHRESH0, 0x0341);
	BWN_PHY_WRITE(mac, BWN_NPHY_ED_CRS40DEASSERTTHRESH1, 0x0341);
	BWN_PHY_WRITE(mac, BWN_NPHY_ED_CRS20LASSERTTHRESH0, 0x042b);
	BWN_PHY_WRITE(mac, BWN_NPHY_ED_CRS20LASSERTTHRESH1, 0x042b);
	BWN_PHY_WRITE(mac, BWN_NPHY_ED_CRS20LDEASSERTTHRESH0, 0x0381);
	BWN_PHY_WRITE(mac, BWN_NPHY_ED_CRS20LDEASSERTTHRESH1, 0x0381);
	BWN_PHY_WRITE(mac, BWN_NPHY_ED_CRS20UASSERTTHRESH0, 0x042b);
	BWN_PHY_WRITE(mac, BWN_NPHY_ED_CRS20UASSERTTHRESH1, 0x042b);
	BWN_PHY_WRITE(mac, BWN_NPHY_ED_CRS20UDEASSERTTHRESH0, 0x0381);
	BWN_PHY_WRITE(mac, BWN_NPHY_ED_CRS20UDEASSERTTHRESH1, 0x0381);

	if (mac->mac_phy.rev >= 6 && sc->sc_board_info.board_flags2 & BHND_BFL2_SINGLEANT_CCK)
		; /* TODO: 0x0080000000000000 HF */

	return (0);
}

static int bwn_nphy_workarounds_rev1_2(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = phy->phy_n;

	uint8_t events1[7] = { 0x0, 0x1, 0x2, 0x8, 0x4, 0x5, 0x3 };
	uint8_t delays1[7] = { 0x8, 0x6, 0x6, 0x2, 0x4, 0x3C, 0x1 };

	uint8_t events2[7] = { 0x0, 0x3, 0x5, 0x4, 0x2, 0x1, 0x8 };
	uint8_t delays2[7] = { 0x8, 0x6, 0x2, 0x4, 0x4, 0x6, 0x1 };

	if (sc->sc_board_info.board_flags2 & BHND_BFL2_SKWRKFEM_BRD ||
	    sc->sc_board_info.board_type == BHND_BOARD_BCM943224M93) {
		delays1[0] = 0x1;
		delays1[5] = 0x14;
	}

	if (bwn_current_band(mac) == BWN_BAND_5G &&
	    nphy->band5g_pwrgain) {
		BWN_RF_MASK(mac, B2055_C1_TX_RF_SPARE, ~0x8);
		BWN_RF_MASK(mac, B2055_C2_TX_RF_SPARE, ~0x8);
	} else {
		BWN_RF_SET(mac, B2055_C1_TX_RF_SPARE, 0x8);
		BWN_RF_SET(mac, B2055_C2_TX_RF_SPARE, 0x8);
	}

	bwn_ntab_write(mac, BWN_NTAB16(8, 0x00), 0x000A);
	bwn_ntab_write(mac, BWN_NTAB16(8, 0x10), 0x000A);
	if (mac->mac_phy.rev < 3) {
		bwn_ntab_write(mac, BWN_NTAB16(8, 0x02), 0xCDAA);
		bwn_ntab_write(mac, BWN_NTAB16(8, 0x12), 0xCDAA);
	}

	if (mac->mac_phy.rev < 2) {
		bwn_ntab_write(mac, BWN_NTAB16(8, 0x08), 0x0000);
		bwn_ntab_write(mac, BWN_NTAB16(8, 0x18), 0x0000);
		bwn_ntab_write(mac, BWN_NTAB16(8, 0x07), 0x7AAB);
		bwn_ntab_write(mac, BWN_NTAB16(8, 0x17), 0x7AAB);
		bwn_ntab_write(mac, BWN_NTAB16(8, 0x06), 0x0800);
		bwn_ntab_write(mac, BWN_NTAB16(8, 0x16), 0x0800);
	}

	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_LUT_TRSW_LO1, 0x2D8);
	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_LUT_TRSW_UP1, 0x301);
	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_LUT_TRSW_LO2, 0x2D8);
	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_LUT_TRSW_UP2, 0x301);

	bwn_nphy_set_rf_sequence(mac, 0, events1, delays1, 7);
	bwn_nphy_set_rf_sequence(mac, 1, events2, delays2, 7);

	bwn_nphy_gain_ctl_workarounds(mac);

	if (mac->mac_phy.rev < 2) {
		if (BWN_PHY_READ(mac, BWN_NPHY_RXCTL) & 0x2)
			bwn_hf_write(mac, bwn_hf_read(mac) |
					BWN_HF_MLADVW);
	} else if (mac->mac_phy.rev == 2) {
		BWN_PHY_WRITE(mac, BWN_NPHY_CRSCHECK2, 0);
		BWN_PHY_WRITE(mac, BWN_NPHY_CRSCHECK3, 0);
	}

	if (mac->mac_phy.rev < 2)
		BWN_PHY_MASK(mac, BWN_NPHY_SCRAM_SIGCTL,
				~BWN_NPHY_SCRAM_SIGCTL_SCM);

	/* Set phase track alpha and beta */
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_A0, 0x125);
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_A1, 0x1B3);
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_A2, 0x105);
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_B0, 0x16E);
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_B1, 0xCD);
	BWN_PHY_WRITE(mac, BWN_NPHY_PHASETR_B2, 0x20);

	if (mac->mac_phy.rev < 3) {
		BWN_PHY_MASK(mac, BWN_NPHY_PIL_DW1,
			     ~BWN_NPHY_PIL_DW_64QAM & 0xFFFF);
		BWN_PHY_WRITE(mac, BWN_NPHY_TXF_20CO_S2B1, 0xB5);
		BWN_PHY_WRITE(mac, BWN_NPHY_TXF_20CO_S2B2, 0xA4);
		BWN_PHY_WRITE(mac, BWN_NPHY_TXF_20CO_S2B3, 0x00);
	}

	if (mac->mac_phy.rev == 2)
		BWN_PHY_SET(mac, BWN_NPHY_FINERX2_CGC,
				BWN_NPHY_FINERX2_CGC_DECGC);

	return (0);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/Workarounds */
static int bwn_nphy_workarounds(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = phy->phy_n;
	int error;

	if (bwn_current_band(mac) == BWN_BAND_5G)
		bwn_nphy_classifier(mac, 1, 0);
	else
		bwn_nphy_classifier(mac, 1, 1);

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 1);

	BWN_PHY_SET(mac, BWN_NPHY_IQFLIP,
		    BWN_NPHY_IQFLIP_ADC1 | BWN_NPHY_IQFLIP_ADC2);

	/* TODO: rev19+ */
	if (mac->mac_phy.rev >= 7)
		error = bwn_nphy_workarounds_rev7plus(mac);
	else if (mac->mac_phy.rev >= 3)
		error = bwn_nphy_workarounds_rev3plus(mac);
	else
		error = bwn_nphy_workarounds_rev1_2(mac);

	if (error)
		return (error);

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 0);

	return (0);
}

/**************************************************
 * Tx/Rx common
 **************************************************/

/*
 * Transmits a known value for LO calibration
 * http://bcm-v4.sipsolutions.net/802.11/PHY/N/TXTone
 */
static int bwn_nphy_tx_tone(struct bwn_mac *mac, uint32_t freq, uint16_t max_val,
			    bool iqmode, bool dac_test, bool modify_bbmult)
{
	uint16_t samp = bwn_nphy_gen_load_samples(mac, freq, max_val, dac_test);
	if (samp == 0)
		return -1;
	bwn_nphy_run_samples(mac, samp, 0xFFFF, 0, iqmode, dac_test,
			     modify_bbmult);
	return 0;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/Chains */
static void bwn_nphy_update_txrx_chain(struct bwn_mac *mac)
{
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;

	bool override = false;
	uint16_t chain = 0x33;

	if (nphy->txrx_chain == 0) {
		chain = 0x11;
		override = true;
	} else if (nphy->txrx_chain == 1) {
		chain = 0x22;
		override = true;
	}

	BWN_PHY_SETMASK(mac, BWN_NPHY_RFSEQCA,
			~(BWN_NPHY_RFSEQCA_TXEN | BWN_NPHY_RFSEQCA_RXEN),
			chain);

	if (override)
		BWN_PHY_SET(mac, BWN_NPHY_RFSEQMODE,
				BWN_NPHY_RFSEQMODE_CAOVER);
	else
		BWN_PHY_MASK(mac, BWN_NPHY_RFSEQMODE,
				~BWN_NPHY_RFSEQMODE_CAOVER);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/stop-playback */
static void bwn_nphy_stop_playback(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	uint16_t tmp;

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 1);

	tmp = BWN_PHY_READ(mac, BWN_NPHY_SAMP_STAT);
	if (tmp & 0x1)
		BWN_PHY_SET(mac, BWN_NPHY_SAMP_CMD, BWN_NPHY_SAMP_CMD_STOP);
	else if (tmp & 0x2)
		BWN_PHY_MASK(mac, BWN_NPHY_IQLOCAL_CMDGCTL, 0x7FFF);

	BWN_PHY_MASK(mac, BWN_NPHY_SAMP_CMD, ~0x0004);

	if (nphy->bb_mult_save & 0x80000000) {
		tmp = nphy->bb_mult_save & 0xFFFF;
		bwn_ntab_write(mac, BWN_NTAB16(15, 87), tmp);
		nphy->bb_mult_save = 0;
	}

	if (phy->rev >= 7 && nphy->lpf_bw_overrode_for_sample_play) {
		if (phy->rev >= 19)
			bwn_nphy_rf_ctl_override_rev19(mac, 0x80, 0, 0, true,
						       1);
		else
			bwn_nphy_rf_ctl_override_rev7(mac, 0x80, 0, 0, true, 1);
		nphy->lpf_bw_overrode_for_sample_play = false;
	}

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 0);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/IqCalGainParams */
static void bwn_nphy_iq_cal_gain_params(struct bwn_mac *mac, uint16_t core,
					struct bwn_nphy_txgains target,
					struct bwn_nphy_iqcal_params *params)
{
	struct bwn_phy *phy = &mac->mac_phy;
	int i, j, indx;
	uint16_t gain;

	if (mac->mac_phy.rev >= 3) {
		params->tx_lpf = target.tx_lpf[core]; /* Rev 7+ */
		params->txgm = target.txgm[core];
		params->pga = target.pga[core];
		params->pad = target.pad[core];
		params->ipa = target.ipa[core];
		if (phy->rev >= 19) {
			/* TODO */
		} else if (phy->rev >= 7) {
			params->cal_gain = (params->txgm << 12) | (params->pga << 8) | (params->pad << 3) | (params->ipa) | (params->tx_lpf << 15);
		} else {
			params->cal_gain = (params->txgm << 12) | (params->pga << 8) | (params->pad << 4) | (params->ipa);
		}
		for (j = 0; j < 5; j++)
			params->ncorr[j] = 0x79;
	} else {
		gain = (target.pad[core]) | (target.pga[core] << 4) |
			(target.txgm[core] << 8);

		indx = (bwn_current_band(mac) == BWN_BAND_5G) ?
			1 : 0;
		for (i = 0; i < 9; i++)
			if (tbl_iqcal_gainparams[indx][i][0] == gain)
				break;
		i = min(i, 8);

		params->txgm = tbl_iqcal_gainparams[indx][i][1];
		params->pga = tbl_iqcal_gainparams[indx][i][2];
		params->pad = tbl_iqcal_gainparams[indx][i][3];
		params->cal_gain = (params->txgm << 7) | (params->pga << 4) |
					(params->pad << 2);
		for (j = 0; j < 4; j++)
			params->ncorr[j] = tbl_iqcal_gainparams[indx][i][4 + j];
	}
}

/**************************************************
 * Tx and Rx
 **************************************************/

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxPwrCtrlEnable */
static void bwn_nphy_tx_power_ctrl(struct bwn_mac *mac, bool enable)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	uint8_t i;
	uint16_t bmask, val, tmp;
	bwn_band_t band = bwn_current_band(mac);

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 1);

	nphy->txpwrctrl = enable;
	if (!enable) {
		if (mac->mac_phy.rev >= 3 &&
		    (BWN_PHY_READ(mac, BWN_NPHY_TXPCTL_CMD) &
		     (BWN_NPHY_TXPCTL_CMD_COEFF |
		      BWN_NPHY_TXPCTL_CMD_HWPCTLEN |
		      BWN_NPHY_TXPCTL_CMD_PCTLEN))) {
			/* We disable enabled TX pwr ctl, save it's state */
			nphy->tx_pwr_idx[0] = BWN_PHY_READ(mac,
						BWN_NPHY_C1_TXPCTL_STAT) & 0x7f;
			nphy->tx_pwr_idx[1] = BWN_PHY_READ(mac,
						BWN_NPHY_C2_TXPCTL_STAT) & 0x7f;
		}

		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_ADDR, 0x6840);
		for (i = 0; i < 84; i++)
			BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATALO, 0);

		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_ADDR, 0x6C40);
		for (i = 0; i < 84; i++)
			BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATALO, 0);

		tmp = BWN_NPHY_TXPCTL_CMD_COEFF | BWN_NPHY_TXPCTL_CMD_HWPCTLEN;
		if (mac->mac_phy.rev >= 3)
			tmp |= BWN_NPHY_TXPCTL_CMD_PCTLEN;
		BWN_PHY_MASK(mac, BWN_NPHY_TXPCTL_CMD, ~tmp);

		if (mac->mac_phy.rev >= 3) {
			BWN_PHY_SET(mac, BWN_NPHY_AFECTL_OVER1, 0x0100);
			BWN_PHY_SET(mac, BWN_NPHY_AFECTL_OVER, 0x0100);
		} else {
			BWN_PHY_SET(mac, BWN_NPHY_AFECTL_OVER, 0x4000);
		}

		if (mac->mac_phy.rev == 2)
			BWN_PHY_SETMASK(mac, BWN_NPHY_BPHY_CTL3,
				~BWN_NPHY_BPHY_CTL3_SCALE, 0x53);
		else if (mac->mac_phy.rev < 2)
			BWN_PHY_SETMASK(mac, BWN_NPHY_BPHY_CTL3,
				~BWN_NPHY_BPHY_CTL3_SCALE, 0x5A);

		if (mac->mac_phy.rev < 2 && bwn_is_40mhz(mac))
			bwn_hf_write(mac, bwn_hf_read(mac) | BWN_HF_TSSI_RESET_PSM_WORKAROUN);
	} else {
		bwn_ntab_write_bulk(mac, BWN_NTAB16(26, 64), 84,
				    nphy->adj_pwr_tbl);
		bwn_ntab_write_bulk(mac, BWN_NTAB16(27, 64), 84,
				    nphy->adj_pwr_tbl);

		bmask = BWN_NPHY_TXPCTL_CMD_COEFF |
			BWN_NPHY_TXPCTL_CMD_HWPCTLEN;
		/* wl does useless check for "enable" param here */
		val = BWN_NPHY_TXPCTL_CMD_COEFF | BWN_NPHY_TXPCTL_CMD_HWPCTLEN;
		if (mac->mac_phy.rev >= 3) {
			bmask |= BWN_NPHY_TXPCTL_CMD_PCTLEN;
			if (val)
				val |= BWN_NPHY_TXPCTL_CMD_PCTLEN;
		}
		BWN_PHY_SETMASK(mac, BWN_NPHY_TXPCTL_CMD, ~(bmask), val);

		if (band == BWN_BAND_5G) {
			if (phy->rev >= 19) {
				/* TODO */
			} else if (phy->rev >= 7) {
				BWN_PHY_SETMASK(mac, BWN_NPHY_TXPCTL_CMD,
						~BWN_NPHY_TXPCTL_CMD_INIT,
						0x32);
				BWN_PHY_SETMASK(mac, BWN_NPHY_TXPCTL_INIT,
						~BWN_NPHY_TXPCTL_INIT_PIDXI1,
						0x32);
			} else {
				BWN_PHY_SETMASK(mac, BWN_NPHY_TXPCTL_CMD,
						~BWN_NPHY_TXPCTL_CMD_INIT,
						0x64);
				if (phy->rev > 1)
					BWN_PHY_SETMASK(mac,
							BWN_NPHY_TXPCTL_INIT,
							~BWN_NPHY_TXPCTL_INIT_PIDXI1,
							0x64);
			}
		}

		if (mac->mac_phy.rev >= 3) {
			if (nphy->tx_pwr_idx[0] != 128 &&
			    nphy->tx_pwr_idx[1] != 128) {
				/* Recover TX pwr ctl state */
				BWN_PHY_SETMASK(mac, BWN_NPHY_TXPCTL_CMD,
						~BWN_NPHY_TXPCTL_CMD_INIT,
						nphy->tx_pwr_idx[0]);
				if (mac->mac_phy.rev > 1)
					BWN_PHY_SETMASK(mac,
						BWN_NPHY_TXPCTL_INIT,
						~0xff, nphy->tx_pwr_idx[1]);
			}
		}

		if (phy->rev >= 7) {
			/* TODO */
		}

		if (mac->mac_phy.rev >= 3) {
			BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_OVER1, ~0x100);
			BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_OVER, ~0x100);
		} else {
			BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_OVER, ~0x4000);
		}

		if (mac->mac_phy.rev == 2)
			BWN_PHY_SETMASK(mac, BWN_NPHY_BPHY_CTL3, ~0xFF, 0x3b);
		else if (mac->mac_phy.rev < 2)
			BWN_PHY_SETMASK(mac, BWN_NPHY_BPHY_CTL3, ~0xFF, 0x40);

		if (mac->mac_phy.rev < 2 && bwn_is_40mhz(mac))
			bwn_hf_write(mac, bwn_hf_read(mac) & ~BWN_HF_TSSI_RESET_PSM_WORKAROUN);

		if (bwn_nphy_ipa(mac)) {
			BWN_PHY_MASK(mac, BWN_NPHY_PAPD_EN0, ~0x4);
			BWN_PHY_MASK(mac, BWN_NPHY_PAPD_EN1, ~0x4);
		}
	}

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 0);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxPwrFix */
static int bwn_nphy_tx_power_fix(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;

	uint8_t txpi[2], bbmult, i;
	uint16_t tmp, radio_gain, dac_gain;
	uint16_t freq = bwn_get_centre_freq(mac);
	uint32_t txgain;
	/* uint32_t gaintbl; rev3+ */

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 1);

	/* TODO: rev19+ */
	if (mac->mac_phy.rev >= 7) {
		txpi[0] = txpi[1] = 30;
	} else if (mac->mac_phy.rev >= 3) {
		txpi[0] = 40;
		txpi[1] = 40;
	} else if (sc->sc_board_info.board_srom_rev < 4) {
		txpi[0] = 72;
		txpi[1] = 72;
	} else if (sc->sc_board_info.board_srom_rev > 7) {
		txpi[0] = 0;
		txpi[1] = 0;
	} else {
#define	BWN_NPHY_GET_TXPI(_name, _result)				\
do {									\
	int error;							\
	error = bhnd_nvram_getvar_uint8(sc->sc_dev, (_name),		\
	    (_result));							\
	if (error) {							\
		device_printf(sc->sc_dev, "NVRAM variable %s "		\
		     "unreadable: %d\n", (_name), error);		\
		return (error);						\
	}								\
} while(0)

		if (bwn_current_band(mac) == BWN_BAND_2G) {
			BWN_NPHY_GET_TXPI(BHND_NVAR_TXPID2GA0, &txpi[0]);
			BWN_NPHY_GET_TXPI(BHND_NVAR_TXPID2GA1, &txpi[1]);
		} else if (freq >= 4900 && freq < 5100) {
			BWN_NPHY_GET_TXPI(BHND_NVAR_TXPID5GLA0, &txpi[0]);
			BWN_NPHY_GET_TXPI(BHND_NVAR_TXPID5GLA1, &txpi[1]);
		} else if (freq >= 5100 && freq < 5500) {
			BWN_NPHY_GET_TXPI(BHND_NVAR_TXPID5GA0, &txpi[0]);
			BWN_NPHY_GET_TXPI(BHND_NVAR_TXPID5GA1, &txpi[1]);
		} else if (freq >= 5500) {
			BWN_NPHY_GET_TXPI(BHND_NVAR_TXPID5GHA0, &txpi[0]);
			BWN_NPHY_GET_TXPI(BHND_NVAR_TXPID5GHA1, &txpi[1]);
		} else {
			txpi[0] = 91;
			txpi[1] = 91;
		}

#undef	BWN_NPHY_GET_TXPI
	}
	if (mac->mac_phy.rev < 7 &&
	    (txpi[0] < 40 || txpi[0] > 100 || txpi[1] < 40 || txpi[1] > 100))
		txpi[0] = txpi[1] = 91;

	/*
	for (i = 0; i < 2; i++) {
		nphy->txpwrindex[i].index_internal = txpi[i];
		nphy->txpwrindex[i].index_internal_save = txpi[i];
	}
	*/

	for (i = 0; i < 2; i++) {
		const uint32_t *table = bwn_nphy_get_tx_gain_table(mac);

		if (!table)
			break;
		txgain = *(table + txpi[i]);

		if (mac->mac_phy.rev >= 3)
			radio_gain = (txgain >> 16) & 0x1FFFF;
		else
			radio_gain = (txgain >> 16) & 0x1FFF;

		if (mac->mac_phy.rev >= 7)
			dac_gain = (txgain >> 8) & 0x7;
		else
			dac_gain = (txgain >> 8) & 0x3F;
		bbmult = txgain & 0xFF;

		if (mac->mac_phy.rev >= 3) {
			if (i == 0)
				BWN_PHY_SET(mac, BWN_NPHY_AFECTL_OVER1, 0x0100);
			else
				BWN_PHY_SET(mac, BWN_NPHY_AFECTL_OVER, 0x0100);
		} else {
			BWN_PHY_SET(mac, BWN_NPHY_AFECTL_OVER, 0x4000);
		}

		if (i == 0)
			BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_DACGAIN1, dac_gain);
		else
			BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_DACGAIN2, dac_gain);

		bwn_ntab_write(mac, BWN_NTAB16(0x7, 0x110 + i), radio_gain);

		tmp = bwn_ntab_read(mac, BWN_NTAB16(0xF, 0x57));
		if (i == 0)
			tmp = (tmp & 0x00FF) | (bbmult << 8);
		else
			tmp = (tmp & 0xFF00) | bbmult;
		bwn_ntab_write(mac, BWN_NTAB16(0xF, 0x57), tmp);

		if (bwn_nphy_ipa(mac)) {
			uint32_t tmp32;
			uint16_t reg = (i == 0) ?
				BWN_NPHY_PAPD_EN0 : BWN_NPHY_PAPD_EN1;
			tmp32 = bwn_ntab_read(mac, BWN_NTAB32(26 + i,
							      576 + txpi[i]));
			BWN_PHY_SETMASK(mac, reg, 0xE00F, (uint32_t) tmp32 << 4);
			BWN_PHY_SET(mac, reg, 0x4);
		}
	}

	BWN_PHY_MASK(mac, BWN_NPHY_BPHY_CTL2, ~BWN_NPHY_BPHY_CTL2_LUT);

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 0);

	return (0);
}

static void bwn_nphy_ipa_internal_tssi_setup(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;

	uint8_t core;
	uint16_t r; /* routing */

	if (phy->rev >= 19) {
		/* TODO */
	} else if (phy->rev >= 7) {
		for (core = 0; core < 2; core++) {
			r = core ? 0x190 : 0x170;
			if (bwn_current_band(mac) == BWN_BAND_2G) {
				BWN_RF_WRITE(mac, r + 0x5, 0x5);
				BWN_RF_WRITE(mac, r + 0x9, 0xE);
				if (phy->rev != 5)
					BWN_RF_WRITE(mac, r + 0xA, 0);
				if (phy->rev != 7)
					BWN_RF_WRITE(mac, r + 0xB, 1);
				else
					BWN_RF_WRITE(mac, r + 0xB, 0x31);
			} else {
				BWN_RF_WRITE(mac, r + 0x5, 0x9);
				BWN_RF_WRITE(mac, r + 0x9, 0xC);
				BWN_RF_WRITE(mac, r + 0xB, 0x0);
				if (phy->rev != 5)
					BWN_RF_WRITE(mac, r + 0xA, 1);
				else
					BWN_RF_WRITE(mac, r + 0xA, 0x31);
			}
			BWN_RF_WRITE(mac, r + 0x6, 0);
			BWN_RF_WRITE(mac, r + 0x7, 0);
			BWN_RF_WRITE(mac, r + 0x8, 3);
			BWN_RF_WRITE(mac, r + 0xC, 0);
		}
	} else {
		if (bwn_current_band(mac) == BWN_BAND_2G)
			BWN_RF_WRITE(mac, B2056_SYN_RESERVED_ADDR31, 0x128);
		else
			BWN_RF_WRITE(mac, B2056_SYN_RESERVED_ADDR31, 0x80);
		BWN_RF_WRITE(mac, B2056_SYN_RESERVED_ADDR30, 0);
		BWN_RF_WRITE(mac, B2056_SYN_GPIO_MASTER1, 0x29);

		for (core = 0; core < 2; core++) {
			r = core ? B2056_TX1 : B2056_TX0;

			BWN_RF_WRITE(mac, r | B2056_TX_IQCAL_VCM_HG, 0);
			BWN_RF_WRITE(mac, r | B2056_TX_IQCAL_IDAC, 0);
			BWN_RF_WRITE(mac, r | B2056_TX_TSSI_VCM, 3);
			BWN_RF_WRITE(mac, r | B2056_TX_TX_AMP_DET, 0);
			BWN_RF_WRITE(mac, r | B2056_TX_TSSI_MISC1, 8);
			BWN_RF_WRITE(mac, r | B2056_TX_TSSI_MISC2, 0);
			BWN_RF_WRITE(mac, r | B2056_TX_TSSI_MISC3, 0);
			if (bwn_current_band(mac) == BWN_BAND_2G) {
				BWN_RF_WRITE(mac, r | B2056_TX_TX_SSI_MASTER,
						0x5);
				if (phy->rev != 5)
					BWN_RF_WRITE(mac, r | B2056_TX_TSSIA,
							0x00);
				if (phy->rev >= 5)
					BWN_RF_WRITE(mac, r | B2056_TX_TSSIG,
							0x31);
				else
					BWN_RF_WRITE(mac, r | B2056_TX_TSSIG,
							0x11);
				BWN_RF_WRITE(mac, r | B2056_TX_TX_SSI_MUX,
						0xE);
			} else {
				BWN_RF_WRITE(mac, r | B2056_TX_TX_SSI_MASTER,
						0x9);
				BWN_RF_WRITE(mac, r | B2056_TX_TSSIA, 0x31);
				BWN_RF_WRITE(mac, r | B2056_TX_TSSIG, 0x0);
				BWN_RF_WRITE(mac, r | B2056_TX_TX_SSI_MUX,
						0xC);
			}
		}
	}
}

/*
 * Stop radio and transmit known signal. Then check received signal strength to
 * get TSSI (Transmit Signal Strength Indicator).
 * http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxPwrCtrlIdleTssi
 */
static void bwn_nphy_tx_power_ctl_idle_tssi(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;

	uint32_t tmp;
	int32_t rssi[4] = { };

	if (bwn_is_chan_passive(mac))
		return;

	if (bwn_nphy_ipa(mac))
		bwn_nphy_ipa_internal_tssi_setup(mac);

	if (phy->rev >= 19)
		bwn_nphy_rf_ctl_override_rev19(mac, 0x1000, 0, 3, false, 0);
	else if (phy->rev >= 7)
		bwn_nphy_rf_ctl_override_rev7(mac, 0x1000, 0, 3, false, 0);
	else if (phy->rev >= 3)
		bwn_nphy_rf_ctl_override(mac, 0x2000, 0, 3, false);

	bwn_nphy_stop_playback(mac);
	bwn_nphy_tx_tone(mac, 4000, 0, false, false, false);
	DELAY(20);
	tmp = bwn_nphy_poll_rssi(mac, N_RSSI_TSSI_2G, rssi, 1);
	bwn_nphy_stop_playback(mac);

	bwn_nphy_rssi_select(mac, 0, N_RSSI_W1);

	if (phy->rev >= 19)
		bwn_nphy_rf_ctl_override_rev19(mac, 0x1000, 0, 3, true, 0);
	else if (phy->rev >= 7)
		bwn_nphy_rf_ctl_override_rev7(mac, 0x1000, 0, 3, true, 0);
	else if (phy->rev >= 3)
		bwn_nphy_rf_ctl_override(mac, 0x2000, 0, 3, true);

	if (phy->rev >= 19) {
		/* TODO */
		return;
	} else if (phy->rev >= 3) {
		nphy->pwr_ctl_info[0].idle_tssi_5g = (tmp >> 24) & 0xFF;
		nphy->pwr_ctl_info[1].idle_tssi_5g = (tmp >> 8) & 0xFF;
	} else {
		nphy->pwr_ctl_info[0].idle_tssi_5g = (tmp >> 16) & 0xFF;
		nphy->pwr_ctl_info[1].idle_tssi_5g = tmp & 0xFF;
	}
	nphy->pwr_ctl_info[0].idle_tssi_2g = (tmp >> 24) & 0xFF;
	nphy->pwr_ctl_info[1].idle_tssi_2g = (tmp >> 8) & 0xFF;
}

/* http://bcm-v4.sipsolutions.net/PHY/N/TxPwrLimitToTbl */
static void bwn_nphy_tx_prepare_adjusted_power_table(struct bwn_mac *mac)
{
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;

	uint8_t idx, delta;
	uint8_t i, stf_mode;

	/* Array adj_pwr_tbl corresponds to the hardware table. It consists of
	 * 21 groups, each containing 4 entries.
	 *
	 * First group has entries for CCK modulation.
	 * The rest of groups has 1 entry per modulation (SISO, CDD, STBC, SDM).
	 *
	 * Group 0 is for CCK
	 * Groups 1..4 use BPSK (group per coding rate)
	 * Groups 5..8 use QPSK (group per coding rate)
	 * Groups 9..12 use 16-QAM (group per coding rate)
	 * Groups 13..16 use 64-QAM (group per coding rate)
	 * Groups 17..20 are unknown
	 */

	for (i = 0; i < 4; i++)
		nphy->adj_pwr_tbl[i] = nphy->tx_power_offset[i];

	for (stf_mode = 0; stf_mode < 4; stf_mode++) {
		delta = 0;
		switch (stf_mode) {
		case 0:
			if (bwn_is_40mhz(mac) && mac->mac_phy.rev >= 5) {
				idx = 68;
			} else {
				delta = 1;
				idx = bwn_is_40mhz(mac) ? 52 : 4;
			}
			break;
		case 1:
			idx = bwn_is_40mhz(mac) ? 76 : 28;
			break;
		case 2:
			idx = bwn_is_40mhz(mac) ? 84 : 36;
			break;
		case 3:
			idx = bwn_is_40mhz(mac) ? 92 : 44;
			break;
		}

		for (i = 0; i < 20; i++) {
			nphy->adj_pwr_tbl[4 + 4 * i + stf_mode] =
				nphy->tx_power_offset[idx];
			if (i == 0)
				idx += delta;
			if (i == 14)
				idx += 1 - delta;
			if (i == 3 || i == 4 || i == 7 || i == 8 || i == 11 ||
			    i == 13)
				idx += 1;
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxPwrCtrlSetup */
static void bwn_nphy_tx_power_ctl_setup(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	struct bwn_phy_n_core_pwr_info core_pwr_info[4];
	int n;

	int16_t a1[2], b0[2], b1[2];
	uint8_t idle[2];
	uint8_t ppr_max;
	int8_t target[2];
	int32_t num, den, pwr;
	uint32_t regval[64];

	uint16_t freq = bwn_get_centre_freq(mac);
	uint16_t tmp;
	uint16_t r; /* routing */
	uint8_t i, c;

	for (n = 0; n < 4; n++) {
		bzero(&core_pwr_info[n], sizeof(core_pwr_info[n]));
		if (bwn_nphy_get_core_power_info(mac, n,
		    &core_pwr_info[n]) != 0) {
			BWN_ERRPRINTF(mac->mac_sc,
			    "%s: failed to get core_pwr_info for core %d\n",
			    __func__,
			    n);
		}
	}

	if (bhnd_get_hwrev(sc->sc_dev) == 11 || bhnd_get_hwrev(sc->sc_dev) == 12) {
		BWN_WRITE_SETMASK4(mac, BWN_MACCTL, ~0, 0x200000);
		BWN_READ_4(mac, BWN_MACCTL);
		DELAY(1);
	}

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, true);

	BWN_PHY_SET(mac, BWN_NPHY_TSSIMODE, BWN_NPHY_TSSIMODE_EN);
	if (mac->mac_phy.rev >= 3)
		BWN_PHY_MASK(mac, BWN_NPHY_TXPCTL_CMD,
			     ~BWN_NPHY_TXPCTL_CMD_PCTLEN & 0xFFFF);
	else
		BWN_PHY_SET(mac, BWN_NPHY_TXPCTL_CMD,
			    BWN_NPHY_TXPCTL_CMD_PCTLEN);

	if (bhnd_get_hwrev(sc->sc_dev) == 11 || bhnd_get_hwrev(sc->sc_dev) == 12)
		BWN_WRITE_SETMASK4(mac, BWN_MACCTL, ~0x200000, 0);

	/*
	 * XXX TODO: see if those bandsbelow map to 5g-lo, 5g-mid, 5g-hi in
	 * any way.
	 */
	if (sc->sc_board_info.board_srom_rev < 4) {
		idle[0] = nphy->pwr_ctl_info[0].idle_tssi_2g;
		idle[1] = nphy->pwr_ctl_info[1].idle_tssi_2g;
		target[0] = target[1] = 52;
		a1[0] = a1[1] = -424;
		b0[0] = b0[1] = 5612;
		b1[0] = b1[1] = -1393;
	} else {
		if (bwn_current_band(mac) == BWN_BAND_2G) {
			for (c = 0; c < 2; c++) {
				idle[c] = nphy->pwr_ctl_info[c].idle_tssi_2g;
				target[c] = core_pwr_info[c].maxpwr_2g;
				a1[c] = core_pwr_info[c].pa_2g[0];
				b0[c] = core_pwr_info[c].pa_2g[1];
				b1[c] = core_pwr_info[c].pa_2g[2];
			}
		} else if (freq >= 4900 && freq < 5100) {
			for (c = 0; c < 2; c++) {
				idle[c] = nphy->pwr_ctl_info[c].idle_tssi_5g;
				target[c] = core_pwr_info[c].maxpwr_5gl;
				a1[c] = core_pwr_info[c].pa_5gl[0];
				b0[c] = core_pwr_info[c].pa_5gl[1];
				b1[c] = core_pwr_info[c].pa_5gl[2];
			}
		} else if (freq >= 5100 && freq < 5500) {
			for (c = 0; c < 2; c++) {
				idle[c] = nphy->pwr_ctl_info[c].idle_tssi_5g;
				target[c] = core_pwr_info[c].maxpwr_5g;
				a1[c] = core_pwr_info[c].pa_5g[0];
				b0[c] = core_pwr_info[c].pa_5g[1];
				b1[c] = core_pwr_info[c].pa_5g[2];
			}
		} else if (freq >= 5500) {
			for (c = 0; c < 2; c++) {
				idle[c] = nphy->pwr_ctl_info[c].idle_tssi_5g;
				target[c] = core_pwr_info[c].maxpwr_5gh;
				a1[c] = core_pwr_info[c].pa_5gh[0];
				b0[c] = core_pwr_info[c].pa_5gh[1];
				b1[c] = core_pwr_info[c].pa_5gh[2];
			}
		} else {
			idle[0] = nphy->pwr_ctl_info[0].idle_tssi_5g;
			idle[1] = nphy->pwr_ctl_info[1].idle_tssi_5g;
			target[0] = target[1] = 52;
			a1[0] = a1[1] = -424;
			b0[0] = b0[1] = 5612;
			b1[0] = b1[1] = -1393;
		}
	}

	ppr_max = bwn_ppr_get_max(mac, &nphy->tx_pwr_max_ppr);
	if (ppr_max) {
		target[0] = ppr_max;
		target[1] = ppr_max;
	}

	if (mac->mac_phy.rev >= 3) {
		if (nphy->tsspos_2g)
			BWN_PHY_SET(mac, BWN_NPHY_TXPCTL_ITSSI, 0x4000);
		if (mac->mac_phy.rev >= 7) {
			for (c = 0; c < 2; c++) {
				r = c ? 0x190 : 0x170;
				if (bwn_nphy_ipa(mac))
					BWN_RF_WRITE(mac, r + 0x9, (bwn_current_band(mac) == BWN_BAND_2G) ? 0xE : 0xC);
			}
		} else {
			if (bwn_nphy_ipa(mac)) {
				tmp = (bwn_current_band(mac) == BWN_BAND_5G) ? 0xC : 0xE;
				BWN_RF_WRITE(mac,
					B2056_TX0 | B2056_TX_TX_SSI_MUX, tmp);
				BWN_RF_WRITE(mac,
					B2056_TX1 | B2056_TX_TX_SSI_MUX, tmp);
			} else {
				BWN_RF_WRITE(mac,
					B2056_TX0 | B2056_TX_TX_SSI_MUX, 0x11);
				BWN_RF_WRITE(mac,
					B2056_TX1 | B2056_TX_TX_SSI_MUX, 0x11);
			}
		}
	}

	if (bhnd_get_hwrev(sc->sc_dev) == 11 || bhnd_get_hwrev(sc->sc_dev) == 12) {
		BWN_WRITE_SETMASK4(mac, BWN_MACCTL, ~0, 0x200000);
		BWN_READ_4(mac, BWN_MACCTL);
		DELAY(1);
	}

	if (phy->rev >= 19) {
		/* TODO */
	} else if (phy->rev >= 7) {
		BWN_PHY_SETMASK(mac, BWN_NPHY_TXPCTL_CMD,
				~BWN_NPHY_TXPCTL_CMD_INIT, 0x19);
		BWN_PHY_SETMASK(mac, BWN_NPHY_TXPCTL_INIT,
				~BWN_NPHY_TXPCTL_INIT_PIDXI1, 0x19);
	} else {
		BWN_PHY_SETMASK(mac, BWN_NPHY_TXPCTL_CMD,
				~BWN_NPHY_TXPCTL_CMD_INIT, 0x40);
		if (mac->mac_phy.rev > 1)
			BWN_PHY_SETMASK(mac, BWN_NPHY_TXPCTL_INIT,
				~BWN_NPHY_TXPCTL_INIT_PIDXI1, 0x40);
	}

	if (bhnd_get_hwrev(sc->sc_dev) == 11 || bhnd_get_hwrev(sc->sc_dev) == 12)
		BWN_WRITE_SETMASK4(mac, BWN_MACCTL, ~0x200000, 0);

	BWN_PHY_WRITE(mac, BWN_NPHY_TXPCTL_N,
		      0xF0 << BWN_NPHY_TXPCTL_N_TSSID_SHIFT |
		      3 << BWN_NPHY_TXPCTL_N_NPTIL2_SHIFT);
	BWN_PHY_WRITE(mac, BWN_NPHY_TXPCTL_ITSSI,
		      idle[0] << BWN_NPHY_TXPCTL_ITSSI_0_SHIFT |
		      idle[1] << BWN_NPHY_TXPCTL_ITSSI_1_SHIFT |
		      BWN_NPHY_TXPCTL_ITSSI_BINF);
	BWN_PHY_WRITE(mac, BWN_NPHY_TXPCTL_TPWR,
		      target[0] << BWN_NPHY_TXPCTL_TPWR_0_SHIFT |
		      target[1] << BWN_NPHY_TXPCTL_TPWR_1_SHIFT);

	for (c = 0; c < 2; c++) {
		for (i = 0; i < 64; i++) {
			num = 8 * (16 * b0[c] + b1[c] * i);
			den = 32768 + a1[c] * i;
			pwr = max((4 * num + den / 2) / den, -8);
			if (mac->mac_phy.rev < 3 && (i <= (31 - idle[c] + 1)))
				pwr = max(pwr, target[c] + 1);
			regval[i] = pwr;
		}
		bwn_ntab_write_bulk(mac, BWN_NTAB32(26 + c, 0), 64, regval);
	}

	bwn_nphy_tx_prepare_adjusted_power_table(mac);
	bwn_ntab_write_bulk(mac, BWN_NTAB16(26, 64), 84, nphy->adj_pwr_tbl);
	bwn_ntab_write_bulk(mac, BWN_NTAB16(27, 64), 84, nphy->adj_pwr_tbl);

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, false);
}

static void bwn_nphy_tx_gain_table_upload(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;

	const uint32_t *table = NULL;
	uint32_t rfpwr_offset;
	uint8_t pga_gain, pad_gain;
	int i;
	const int16_t *rf_pwr_offset_table = NULL;

	table = bwn_nphy_get_tx_gain_table(mac);
	if (!table)
		return;

	bwn_ntab_write_bulk(mac, BWN_NTAB32(26, 192), 128, table);
	bwn_ntab_write_bulk(mac, BWN_NTAB32(27, 192), 128, table);

	if (phy->rev < 3)
		return;

#if 0
	nphy->gmval = (table[0] >> 16) & 0x7000;
#endif

	if (phy->rev >= 19) {
		return;
	} else if (phy->rev >= 7) {
		rf_pwr_offset_table = bwn_ntab_get_rf_pwr_offset_table(mac);
		if (!rf_pwr_offset_table)
			return;
		/* TODO: Enable this once we have gains configured */
		return;
	}

	for (i = 0; i < 128; i++) {
		if (phy->rev >= 19) {
			/* TODO */
			return;
		} else if (phy->rev >= 7) {
			pga_gain = (table[i] >> 24) & 0xf;
			pad_gain = (table[i] >> 19) & 0x1f;
			if (bwn_current_band(mac) == BWN_BAND_2G)
				rfpwr_offset = rf_pwr_offset_table[pad_gain];
			else
				rfpwr_offset = rf_pwr_offset_table[pga_gain];
		} else {
			pga_gain = (table[i] >> 24) & 0xF;
			if (bwn_current_band(mac) == BWN_BAND_2G)
				rfpwr_offset = bwn_ntab_papd_pga_gain_delta_ipa_2g[pga_gain];
			else
				rfpwr_offset = 0; /* FIXME */
		}

		bwn_ntab_write(mac, BWN_NTAB32(26, 576 + i), rfpwr_offset);
		bwn_ntab_write(mac, BWN_NTAB32(27, 576 + i), rfpwr_offset);
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/PA%20override */
static void bwn_nphy_pa_override(struct bwn_mac *mac, bool enable)
{
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	bwn_band_t band;
	uint16_t tmp;

	if (!enable) {
		nphy->rfctrl_intc1_save = BWN_PHY_READ(mac,
						       BWN_NPHY_RFCTL_INTC1);
		nphy->rfctrl_intc2_save = BWN_PHY_READ(mac,
						       BWN_NPHY_RFCTL_INTC2);
		band = bwn_current_band(mac);
		if (mac->mac_phy.rev >= 7) {
			tmp = 0x1480;
		} else if (mac->mac_phy.rev >= 3) {
			if (band == BWN_BAND_5G)
				tmp = 0x600;
			else
				tmp = 0x480;
		} else {
			if (band == BWN_BAND_5G)
				tmp = 0x180;
			else
				tmp = 0x120;
		}
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC1, tmp);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC2, tmp);
	} else {
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC1,
				nphy->rfctrl_intc1_save);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC2,
				nphy->rfctrl_intc2_save);
	}
}

/*
 * TX low-pass filter bandwidth setup
 * http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxLpFbw
 */
static void bwn_nphy_tx_lpf_bw(struct bwn_mac *mac)
{
	uint16_t tmp;

	if (mac->mac_phy.rev < 3 || mac->mac_phy.rev >= 7)
		return;

	if (bwn_nphy_ipa(mac))
		tmp = bwn_is_40mhz(mac) ? 5 : 4;
	else
		tmp = bwn_is_40mhz(mac) ? 3 : 1;
	BWN_PHY_WRITE(mac, BWN_NPHY_TXF_40CO_B32S2,
		      (tmp << 9) | (tmp << 6) | (tmp << 3) | tmp);

	if (bwn_nphy_ipa(mac)) {
		tmp = bwn_is_40mhz(mac) ? 4 : 1;
		BWN_PHY_WRITE(mac, BWN_NPHY_TXF_40CO_B1S2,
			      (tmp << 9) | (tmp << 6) | (tmp << 3) | tmp);
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RxIqEst */
static void bwn_nphy_rx_iq_est(struct bwn_mac *mac, struct bwn_nphy_iq_est *est,
				uint16_t samps, uint8_t time, bool wait)
{
	int i;
	uint16_t tmp;

	BWN_PHY_WRITE(mac, BWN_NPHY_IQEST_SAMCNT, samps);
	BWN_PHY_SETMASK(mac, BWN_NPHY_IQEST_WT, ~BWN_NPHY_IQEST_WT_VAL, time);
	if (wait)
		BWN_PHY_SET(mac, BWN_NPHY_IQEST_CMD, BWN_NPHY_IQEST_CMD_MODE);
	else
		BWN_PHY_MASK(mac, BWN_NPHY_IQEST_CMD, ~BWN_NPHY_IQEST_CMD_MODE);

	BWN_PHY_SET(mac, BWN_NPHY_IQEST_CMD, BWN_NPHY_IQEST_CMD_START);

	for (i = 1000; i; i--) {
		tmp = BWN_PHY_READ(mac, BWN_NPHY_IQEST_CMD);
		if (!(tmp & BWN_NPHY_IQEST_CMD_START)) {
			est->i0_pwr = (BWN_PHY_READ(mac, BWN_NPHY_IQEST_IPACC_HI0) << 16) |
					BWN_PHY_READ(mac, BWN_NPHY_IQEST_IPACC_LO0);
			est->q0_pwr = (BWN_PHY_READ(mac, BWN_NPHY_IQEST_QPACC_HI0) << 16) |
					BWN_PHY_READ(mac, BWN_NPHY_IQEST_QPACC_LO0);
			est->iq0_prod = (BWN_PHY_READ(mac, BWN_NPHY_IQEST_IQACC_HI0) << 16) |
					BWN_PHY_READ(mac, BWN_NPHY_IQEST_IQACC_LO0);

			est->i1_pwr = (BWN_PHY_READ(mac, BWN_NPHY_IQEST_IPACC_HI1) << 16) |
					BWN_PHY_READ(mac, BWN_NPHY_IQEST_IPACC_LO1);
			est->q1_pwr = (BWN_PHY_READ(mac, BWN_NPHY_IQEST_QPACC_HI1) << 16) |
					BWN_PHY_READ(mac, BWN_NPHY_IQEST_QPACC_LO1);
			est->iq1_prod = (BWN_PHY_READ(mac, BWN_NPHY_IQEST_IQACC_HI1) << 16) |
					BWN_PHY_READ(mac, BWN_NPHY_IQEST_IQACC_LO1);
			return;
		}
		DELAY(10);
	}
	memset(est, 0, sizeof(*est));
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RxIqCoeffs */
static void bwn_nphy_rx_iq_coeffs(struct bwn_mac *mac, bool write,
					struct bwn_phy_n_iq_comp *pcomp)
{
	if (write) {
		BWN_PHY_WRITE(mac, BWN_NPHY_C1_RXIQ_COMPA0, pcomp->a0);
		BWN_PHY_WRITE(mac, BWN_NPHY_C1_RXIQ_COMPB0, pcomp->b0);
		BWN_PHY_WRITE(mac, BWN_NPHY_C2_RXIQ_COMPA1, pcomp->a1);
		BWN_PHY_WRITE(mac, BWN_NPHY_C2_RXIQ_COMPB1, pcomp->b1);
	} else {
		pcomp->a0 = BWN_PHY_READ(mac, BWN_NPHY_C1_RXIQ_COMPA0);
		pcomp->b0 = BWN_PHY_READ(mac, BWN_NPHY_C1_RXIQ_COMPB0);
		pcomp->a1 = BWN_PHY_READ(mac, BWN_NPHY_C2_RXIQ_COMPA1);
		pcomp->b1 = BWN_PHY_READ(mac, BWN_NPHY_C2_RXIQ_COMPB1);
	}
}

#if 0
/* Ready but not used anywhere */
/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RxCalPhyCleanup */
static void bwn_nphy_rx_cal_phy_cleanup(struct bwn_mac *mac, uint8_t core)
{
	uint16_t *regs = mac->mac_phy.phy_n->tx_rx_cal_phy_saveregs;

	BWN_PHY_WRITE(mac, BWN_NPHY_RFSEQCA, regs[0]);
	if (core == 0) {
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_C1, regs[1]);
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER1, regs[2]);
	} else {
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_C2, regs[1]);
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER, regs[2]);
	}
	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC1, regs[3]);
	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC2, regs[4]);
	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_RSSIO1, regs[5]);
	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_RSSIO2, regs[6]);
	BWN_PHY_WRITE(mac, BWN_NPHY_TXF_40CO_B1S1, regs[7]);
	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_OVER, regs[8]);
	BWN_PHY_WRITE(mac, BWN_NPHY_PAPD_EN0, regs[9]);
	BWN_PHY_WRITE(mac, BWN_NPHY_PAPD_EN1, regs[10]);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RxCalPhySetup */
static void bwn_nphy_rx_cal_phy_setup(struct bwn_mac *mac, uint8_t core)
{
	uint8_t rxval, txval;
	uint16_t *regs = mac->mac_phy.phy_n->tx_rx_cal_phy_saveregs;

	regs[0] = BWN_PHY_READ(mac, BWN_NPHY_RFSEQCA);
	if (core == 0) {
		regs[1] = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_C1);
		regs[2] = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_OVER1);
	} else {
		regs[1] = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_C2);
		regs[2] = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_OVER);
	}
	regs[3] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_INTC1);
	regs[4] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_INTC2);
	regs[5] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_RSSIO1);
	regs[6] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_RSSIO2);
	regs[7] = BWN_PHY_READ(mac, BWN_NPHY_TXF_40CO_B1S1);
	regs[8] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_OVER);
	regs[9] = BWN_PHY_READ(mac, BWN_NPHY_PAPD_EN0);
	regs[10] = BWN_PHY_READ(mac, BWN_NPHY_PAPD_EN1);

	BWN_PHY_MASK(mac, BWN_NPHY_PAPD_EN0, ~0x0001);
	BWN_PHY_MASK(mac, BWN_NPHY_PAPD_EN1, ~0x0001);

	BWN_PHY_SETMASK(mac, BWN_NPHY_RFSEQCA,
			~BWN_NPHY_RFSEQCA_RXDIS & 0xFFFF,
			((1 - core) << BWN_NPHY_RFSEQCA_RXDIS_SHIFT));
	BWN_PHY_SETMASK(mac, BWN_NPHY_RFSEQCA, ~BWN_NPHY_RFSEQCA_TXEN,
			((1 - core) << BWN_NPHY_RFSEQCA_TXEN_SHIFT));
	BWN_PHY_SETMASK(mac, BWN_NPHY_RFSEQCA, ~BWN_NPHY_RFSEQCA_RXEN,
			(core << BWN_NPHY_RFSEQCA_RXEN_SHIFT));
	BWN_PHY_SETMASK(mac, BWN_NPHY_RFSEQCA, ~BWN_NPHY_RFSEQCA_TXDIS,
			(core << BWN_NPHY_RFSEQCA_TXDIS_SHIFT));

	if (core == 0) {
		BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_C1, ~0x0007);
		BWN_PHY_SET(mac, BWN_NPHY_AFECTL_OVER1, 0x0007);
	} else {
		BWN_PHY_MASK(mac, BWN_NPHY_AFECTL_C2, ~0x0007);
		BWN_PHY_SET(mac, BWN_NPHY_AFECTL_OVER, 0x0007);
	}

	bwn_nphy_rf_ctl_intc_override(mac, N_INTC_OVERRIDE_PA, 0, 3);
	bwn_nphy_rf_ctl_override(mac, 8, 0, 3, false);
	bwn_nphy_force_rf_sequence(mac, BWN_RFSEQ_RX2TX);

	if (core == 0) {
		rxval = 1;
		txval = 8;
	} else {
		rxval = 4;
		txval = 2;
	}
	bwn_nphy_rf_ctl_intc_override(mac, N_INTC_OVERRIDE_TRSW, rxval,
				      core + 1);
	bwn_nphy_rf_ctl_intc_override(mac, N_INTC_OVERRIDE_TRSW, txval,
				      2 - core);
}
#endif

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/CalcRxIqComp */
static void bwn_nphy_calc_rx_iq_comp(struct bwn_mac *mac, uint8_t mask)
{
	int i;
	int32_t iq;
	uint32_t ii;
	uint32_t qq;
	int iq_nbits, qq_nbits;
	int arsh, brsh;
	uint16_t tmp, a, b;

	struct bwn_nphy_iq_est est;
	struct bwn_phy_n_iq_comp old;
	struct bwn_phy_n_iq_comp new = { };
	bool error = false;

	if (mask == 0)
		return;

	bwn_nphy_rx_iq_coeffs(mac, false, &old);
	bwn_nphy_rx_iq_coeffs(mac, true, &new);
	bwn_nphy_rx_iq_est(mac, &est, 0x4000, 32, false);
	new = old;

	for (i = 0; i < 2; i++) {
		if (i == 0 && (mask & 1)) {
			iq = est.iq0_prod;
			ii = est.i0_pwr;
			qq = est.q0_pwr;
		} else if (i == 1 && (mask & 2)) {
			iq = est.iq1_prod;
			ii = est.i1_pwr;
			qq = est.q1_pwr;
		} else {
			continue;
		}

		if (ii + qq < 2) {
			error = true;
			break;
		}

		iq_nbits = fls(abs(iq));
		qq_nbits = fls(qq);

		arsh = iq_nbits - 20;
		if (arsh >= 0) {
			a = -((iq << (30 - iq_nbits)) + (ii >> (1 + arsh)));
			tmp = ii >> arsh;
		} else {
			a = -((iq << (30 - iq_nbits)) + (ii << (-1 - arsh)));
			tmp = ii << -arsh;
		}
		if (tmp == 0) {
			error = true;
			break;
		}
		a /= tmp;

		brsh = qq_nbits - 11;
		if (brsh >= 0) {
			b = (qq << (31 - qq_nbits));
			tmp = ii >> brsh;
		} else {
			b = (qq << (31 - qq_nbits));
			tmp = ii << -brsh;
		}
		if (tmp == 0) {
			error = true;
			break;
		}
		b = bwn_sqrt(mac, b / tmp - a * a) - (1 << 10);

		if (i == 0 && (mask & 0x1)) {
			if (mac->mac_phy.rev >= 3) {
				new.a0 = a & 0x3FF;
				new.b0 = b & 0x3FF;
			} else {
				new.a0 = b & 0x3FF;
				new.b0 = a & 0x3FF;
			}
		} else if (i == 1 && (mask & 0x2)) {
			if (mac->mac_phy.rev >= 3) {
				new.a1 = a & 0x3FF;
				new.b1 = b & 0x3FF;
			} else {
				new.a1 = b & 0x3FF;
				new.b1 = a & 0x3FF;
			}
		}
	}

	if (error)
		new = old;

	bwn_nphy_rx_iq_coeffs(mac, true, &new);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxIqWar */
static void bwn_nphy_tx_iq_workaround(struct bwn_mac *mac)
{
	uint16_t array[4];
	bwn_ntab_read_bulk(mac, BWN_NTAB16(0xF, 0x50), 4, array);

	bwn_shm_write_2(mac, BWN_SHARED, BWN_SHM_SH_NPHY_TXIQW0, array[0]);
	bwn_shm_write_2(mac, BWN_SHARED, BWN_SHM_SH_NPHY_TXIQW1, array[1]);
	bwn_shm_write_2(mac, BWN_SHARED, BWN_SHM_SH_NPHY_TXIQW2, array[2]);
	bwn_shm_write_2(mac, BWN_SHARED, BWN_SHM_SH_NPHY_TXIQW3, array[3]);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/SpurWar */
static void bwn_nphy_spur_workaround(struct bwn_mac *mac)
{
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;

	uint8_t channel = bwn_get_chan(mac);
	int tone[2] = { 57, 58 };
	uint32_t noise[2] = { 0x3FF, 0x3FF };

	if (mac->mac_phy.rev < 3) {
		BWN_ERRPRINTF(mac->mac_sc, "%s: phy rev %d out of range\n",
		    __func__,
		    mac->mac_phy.rev);
	}

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 1);

	if (nphy->gband_spurwar_en) {
		/* TODO: N PHY Adjust Analog Pfbw (7) */
		if (channel == 11 && bwn_is_40mhz(mac))
			; /* TODO: N PHY Adjust Min Noise Var(2, tone, noise)*/
		else
			; /* TODO: N PHY Adjust Min Noise Var(0, NULL, NULL)*/
		/* TODO: N PHY Adjust CRS Min Power (0x1E) */
	}

	if (nphy->aband_spurwar_en) {
		if (channel == 54) {
			tone[0] = 0x20;
			noise[0] = 0x25F;
		} else if (channel == 38 || channel == 102 || channel == 118) {
			if (0 /* FIXME */) {
				tone[0] = 0x20;
				noise[0] = 0x21F;
			} else {
				tone[0] = 0;
				noise[0] = 0;
			}
		} else if (channel == 134) {
			tone[0] = 0x20;
			noise[0] = 0x21F;
		} else if (channel == 151) {
			tone[0] = 0x10;
			noise[0] = 0x23F;
		} else if (channel == 153 || channel == 161) {
			tone[0] = 0x30;
			noise[0] = 0x23F;
		} else {
			tone[0] = 0;
			noise[0] = 0;
		}

		if (!tone[0] && !noise[0])
			; /* TODO: N PHY Adjust Min Noise Var(1, tone, noise)*/
		else
			; /* TODO: N PHY Adjust Min Noise Var(0, NULL, NULL)*/
	}

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 0);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxPwrCtrlCoefSetup */
static void bwn_nphy_tx_pwr_ctrl_coef_setup(struct bwn_mac *mac)
{
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	int i, j;
	uint32_t tmp;
	uint32_t cur_real, cur_imag, real_part, imag_part;

	uint16_t buffer[7];

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, true);

	bwn_ntab_read_bulk(mac, BWN_NTAB16(15, 80), 7, buffer);

	for (i = 0; i < 2; i++) {
		tmp = ((buffer[i * 2] & 0x3FF) << 10) |
			(buffer[i * 2 + 1] & 0x3FF);
		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_ADDR,
				(((i + 26) << 10) | 320));
		for (j = 0; j < 128; j++) {
			BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATAHI,
					((tmp >> 16) & 0xFFFF));
			BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATALO,
					(tmp & 0xFFFF));
		}
	}

	for (i = 0; i < 2; i++) {
		tmp = buffer[5 + i];
		real_part = (tmp >> 8) & 0xFF;
		imag_part = (tmp & 0xFF);
		BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_ADDR,
				(((i + 26) << 10) | 448));

		if (mac->mac_phy.rev >= 3) {
			cur_real = real_part;
			cur_imag = imag_part;
			tmp = ((cur_real & 0xFF) << 8) | (cur_imag & 0xFF);
		}

		for (j = 0; j < 128; j++) {
			if (mac->mac_phy.rev < 3) {
				cur_real = (real_part * loscale[j] + 128) >> 8;
				cur_imag = (imag_part * loscale[j] + 128) >> 8;
				tmp = ((cur_real & 0xFF) << 8) |
					(cur_imag & 0xFF);
			}
			BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATAHI,
					((tmp >> 16) & 0xFFFF));
			BWN_PHY_WRITE(mac, BWN_NPHY_TABLE_DATALO,
					(tmp & 0xFFFF));
		}
	}

	if (mac->mac_phy.rev >= 3) {
		bwn_shm_write_2(mac, BWN_SHARED,
				BWN_SHM_SH_NPHY_TXPWR_INDX0, 0xFFFF);
		bwn_shm_write_2(mac, BWN_SHARED,
				BWN_SHM_SH_NPHY_TXPWR_INDX1, 0xFFFF);
	}

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, false);
}

/*
 * Restore RSSI Calibration
 * http://bcm-v4.sipsolutions.net/802.11/PHY/N/RestoreRssiCal
 */
static void bwn_nphy_restore_rssi_cal(struct bwn_mac *mac)
{
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;

	uint16_t *rssical_radio_regs = NULL;
	uint16_t *rssical_phy_regs = NULL;

	if (bwn_current_band(mac) == BWN_BAND_2G) {
		if (!nphy->rssical_chanspec_2G.center_freq)
			return;
		rssical_radio_regs = nphy->rssical_cache.rssical_radio_regs_2G;
		rssical_phy_regs = nphy->rssical_cache.rssical_phy_regs_2G;
	} else {
		if (!nphy->rssical_chanspec_5G.center_freq)
			return;
		rssical_radio_regs = nphy->rssical_cache.rssical_radio_regs_5G;
		rssical_phy_regs = nphy->rssical_cache.rssical_phy_regs_5G;
	}

	if (mac->mac_phy.rev >= 19) {
		/* TODO */
	} else if (mac->mac_phy.rev >= 7) {
		BWN_RF_SETMASK(mac, R2057_NB_MASTER_CORE0, ~R2057_VCM_MASK,
				  rssical_radio_regs[0]);
		BWN_RF_SETMASK(mac, R2057_NB_MASTER_CORE1, ~R2057_VCM_MASK,
				  rssical_radio_regs[1]);
	} else {
		BWN_RF_SETMASK(mac, B2056_RX0 | B2056_RX_RSSI_MISC, 0xE3,
				  rssical_radio_regs[0]);
		BWN_RF_SETMASK(mac, B2056_RX1 | B2056_RX_RSSI_MISC, 0xE3,
				  rssical_radio_regs[1]);
	}

	BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0I_RSSI_Z, rssical_phy_regs[0]);
	BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0Q_RSSI_Z, rssical_phy_regs[1]);
	BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1I_RSSI_Z, rssical_phy_regs[2]);
	BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1Q_RSSI_Z, rssical_phy_regs[3]);

	BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0I_RSSI_X, rssical_phy_regs[4]);
	BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0Q_RSSI_X, rssical_phy_regs[5]);
	BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1I_RSSI_X, rssical_phy_regs[6]);
	BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1Q_RSSI_X, rssical_phy_regs[7]);

	BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0I_RSSI_Y, rssical_phy_regs[8]);
	BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_0Q_RSSI_Y, rssical_phy_regs[9]);
	BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1I_RSSI_Y, rssical_phy_regs[10]);
	BWN_PHY_WRITE(mac, BWN_NPHY_RSSIMC_1Q_RSSI_Y, rssical_phy_regs[11]);
}

static void bwn_nphy_tx_cal_radio_setup_rev19(struct bwn_mac *mac)
{
	/* TODO */
}

static void bwn_nphy_tx_cal_radio_setup_rev7(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	uint16_t *save = nphy->tx_rx_cal_radio_saveregs;
	int core, off;
	uint16_t r, tmp;

	for (core = 0; core < 2; core++) {
		r = core ? 0x20 : 0;
		off = core * 11;

		save[off + 0] = BWN_RF_READ(mac, r + R2057_TX0_TX_SSI_MASTER);
		save[off + 1] = BWN_RF_READ(mac, r + R2057_TX0_IQCAL_VCM_HG);
		save[off + 2] = BWN_RF_READ(mac, r + R2057_TX0_IQCAL_IDAC);
		save[off + 3] = BWN_RF_READ(mac, r + R2057_TX0_TSSI_VCM);
		save[off + 4] = 0;
		save[off + 5] = BWN_RF_READ(mac, r + R2057_TX0_TX_SSI_MUX);
		if (phy->rf_rev != 5)
			save[off + 6] = BWN_RF_READ(mac, r + R2057_TX0_TSSIA);
		save[off + 7] = BWN_RF_READ(mac, r + R2057_TX0_TSSIG);
		save[off + 8] = BWN_RF_READ(mac, r + R2057_TX0_TSSI_MISC1);

		if (bwn_current_band(mac) == BWN_BAND_5G) {
			BWN_RF_WRITE(mac, r + R2057_TX0_TX_SSI_MASTER, 0xA);
			BWN_RF_WRITE(mac, r + R2057_TX0_IQCAL_VCM_HG, 0x43);
			BWN_RF_WRITE(mac, r + R2057_TX0_IQCAL_IDAC, 0x55);
			BWN_RF_WRITE(mac, r + R2057_TX0_TSSI_VCM, 0);
			BWN_RF_WRITE(mac, r + R2057_TX0_TSSIG, 0);
			if (nphy->use_int_tx_iq_lo_cal) {
				BWN_RF_WRITE(mac, r + R2057_TX0_TX_SSI_MUX, 0x4);
				tmp = true ? 0x31 : 0x21; /* TODO */
				BWN_RF_WRITE(mac, r + R2057_TX0_TSSIA, tmp);
			}
			BWN_RF_WRITE(mac, r + R2057_TX0_TSSI_MISC1, 0x00);
		} else {
			BWN_RF_WRITE(mac, r + R2057_TX0_TX_SSI_MASTER, 0x6);
			BWN_RF_WRITE(mac, r + R2057_TX0_IQCAL_VCM_HG, 0x43);
			BWN_RF_WRITE(mac, r + R2057_TX0_IQCAL_IDAC, 0x55);
			BWN_RF_WRITE(mac, r + R2057_TX0_TSSI_VCM, 0);

			if (phy->rf_rev != 5)
				BWN_RF_WRITE(mac, r + R2057_TX0_TSSIA, 0);
			if (nphy->use_int_tx_iq_lo_cal) {
				BWN_RF_WRITE(mac, r + R2057_TX0_TX_SSI_MUX, 0x6);
				tmp = true ? 0x31 : 0x21; /* TODO */
				BWN_RF_WRITE(mac, r + R2057_TX0_TSSIG, tmp);
			}
			BWN_RF_WRITE(mac, r + R2057_TX0_TSSI_MISC1, 0);
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxCalRadioSetup */
static void bwn_nphy_tx_cal_radio_setup(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	uint16_t *save = nphy->tx_rx_cal_radio_saveregs;
	uint16_t tmp;
	uint8_t offset, i;

	if (phy->rev >= 19) {
		bwn_nphy_tx_cal_radio_setup_rev19(mac);
	} else if (phy->rev >= 7) {
		bwn_nphy_tx_cal_radio_setup_rev7(mac);
	} else if (phy->rev >= 3) {
	    for (i = 0; i < 2; i++) {
		tmp = (i == 0) ? 0x2000 : 0x3000;
		offset = i * 11;

		save[offset + 0] = BWN_RF_READ(mac, B2055_CAL_RVARCTL);
		save[offset + 1] = BWN_RF_READ(mac, B2055_CAL_LPOCTL);
		save[offset + 2] = BWN_RF_READ(mac, B2055_CAL_TS);
		save[offset + 3] = BWN_RF_READ(mac, B2055_CAL_RCCALRTS);
		save[offset + 4] = BWN_RF_READ(mac, B2055_CAL_RCALRTS);
		save[offset + 5] = BWN_RF_READ(mac, B2055_PADDRV);
		save[offset + 6] = BWN_RF_READ(mac, B2055_XOCTL1);
		save[offset + 7] = BWN_RF_READ(mac, B2055_XOCTL2);
		save[offset + 8] = BWN_RF_READ(mac, B2055_XOREGUL);
		save[offset + 9] = BWN_RF_READ(mac, B2055_XOMISC);
		save[offset + 10] = BWN_RF_READ(mac, B2055_PLL_LFC1);

		if (bwn_current_band(mac) == BWN_BAND_5G) {
			BWN_RF_WRITE(mac, tmp | B2055_CAL_RVARCTL, 0x0A);
			BWN_RF_WRITE(mac, tmp | B2055_CAL_LPOCTL, 0x40);
			BWN_RF_WRITE(mac, tmp | B2055_CAL_TS, 0x55);
			BWN_RF_WRITE(mac, tmp | B2055_CAL_RCCALRTS, 0);
			BWN_RF_WRITE(mac, tmp | B2055_CAL_RCALRTS, 0);
			if (nphy->ipa5g_on) {
				BWN_RF_WRITE(mac, tmp | B2055_PADDRV, 4);
				BWN_RF_WRITE(mac, tmp | B2055_XOCTL1, 1);
			} else {
				BWN_RF_WRITE(mac, tmp | B2055_PADDRV, 0);
				BWN_RF_WRITE(mac, tmp | B2055_XOCTL1, 0x2F);
			}
			BWN_RF_WRITE(mac, tmp | B2055_XOCTL2, 0);
		} else {
			BWN_RF_WRITE(mac, tmp | B2055_CAL_RVARCTL, 0x06);
			BWN_RF_WRITE(mac, tmp | B2055_CAL_LPOCTL, 0x40);
			BWN_RF_WRITE(mac, tmp | B2055_CAL_TS, 0x55);
			BWN_RF_WRITE(mac, tmp | B2055_CAL_RCCALRTS, 0);
			BWN_RF_WRITE(mac, tmp | B2055_CAL_RCALRTS, 0);
			BWN_RF_WRITE(mac, tmp | B2055_XOCTL1, 0);
			if (nphy->ipa2g_on) {
				BWN_RF_WRITE(mac, tmp | B2055_PADDRV, 6);
				BWN_RF_WRITE(mac, tmp | B2055_XOCTL2,
					(mac->mac_phy.rev < 5) ? 0x11 : 0x01);
			} else {
				BWN_RF_WRITE(mac, tmp | B2055_PADDRV, 0);
				BWN_RF_WRITE(mac, tmp | B2055_XOCTL2, 0);
			}
		}
		BWN_RF_WRITE(mac, tmp | B2055_XOREGUL, 0);
		BWN_RF_WRITE(mac, tmp | B2055_XOMISC, 0);
		BWN_RF_WRITE(mac, tmp | B2055_PLL_LFC1, 0);
	    }
	} else {
		save[0] = BWN_RF_READ(mac, B2055_C1_TX_RF_IQCAL1);
		BWN_RF_WRITE(mac, B2055_C1_TX_RF_IQCAL1, 0x29);

		save[1] = BWN_RF_READ(mac, B2055_C1_TX_RF_IQCAL2);
		BWN_RF_WRITE(mac, B2055_C1_TX_RF_IQCAL2, 0x54);

		save[2] = BWN_RF_READ(mac, B2055_C2_TX_RF_IQCAL1);
		BWN_RF_WRITE(mac, B2055_C2_TX_RF_IQCAL1, 0x29);

		save[3] = BWN_RF_READ(mac, B2055_C2_TX_RF_IQCAL2);
		BWN_RF_WRITE(mac, B2055_C2_TX_RF_IQCAL2, 0x54);

		save[3] = BWN_RF_READ(mac, B2055_C1_PWRDET_RXTX);
		save[4] = BWN_RF_READ(mac, B2055_C2_PWRDET_RXTX);

		if (!(BWN_PHY_READ(mac, BWN_NPHY_BANDCTL) &
		    BWN_NPHY_BANDCTL_5GHZ)) {
			BWN_RF_WRITE(mac, B2055_C1_PWRDET_RXTX, 0x04);
			BWN_RF_WRITE(mac, B2055_C2_PWRDET_RXTX, 0x04);
		} else {
			BWN_RF_WRITE(mac, B2055_C1_PWRDET_RXTX, 0x20);
			BWN_RF_WRITE(mac, B2055_C2_PWRDET_RXTX, 0x20);
		}

		if (mac->mac_phy.rev < 2) {
			BWN_RF_SET(mac, B2055_C1_TX_BB_MXGM, 0x20);
			BWN_RF_SET(mac, B2055_C2_TX_BB_MXGM, 0x20);
		} else {
			BWN_RF_MASK(mac, B2055_C1_TX_BB_MXGM, ~0x20);
			BWN_RF_MASK(mac, B2055_C2_TX_BB_MXGM, ~0x20);
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/UpdateTxCalLadder */
static void bwn_nphy_update_tx_cal_ladder(struct bwn_mac *mac, uint16_t core)
{
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	int i;
	uint16_t scale, entry;

	uint16_t tmp = nphy->txcal_bbmult;
	if (core == 0)
		tmp >>= 8;
	tmp &= 0xff;

	for (i = 0; i < 18; i++) {
		scale = (ladder_lo[i].percent * tmp) / 100;
		entry = ((scale & 0xFF) << 8) | ladder_lo[i].g_env;
		bwn_ntab_write(mac, BWN_NTAB16(15, i), entry);

		scale = (ladder_iq[i].percent * tmp) / 100;
		entry = ((scale & 0xFF) << 8) | ladder_iq[i].g_env;
		bwn_ntab_write(mac, BWN_NTAB16(15, i + 32), entry);
	}
}

static void bwn_nphy_pa_set_tx_dig_filter(struct bwn_mac *mac, uint16_t offset,
					  const int16_t *filter)
{
	int i;

	offset = BWN_PHY_N(offset);

	for (i = 0; i < 15; i++, offset++)
		BWN_PHY_WRITE(mac, offset, filter[i]);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/ExtPaSetTxDigiFilts */
static void bwn_nphy_ext_pa_set_tx_dig_filters(struct bwn_mac *mac)
{
	bwn_nphy_pa_set_tx_dig_filter(mac, 0x2C5,
				      tbl_tx_filter_coef_rev4[2]);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/IpaSetTxDigiFilts */
static void bwn_nphy_int_pa_set_tx_dig_filters(struct bwn_mac *mac)
{
	/* BWN_NPHY_TXF_20CO_S0A1, BWN_NPHY_TXF_40CO_S0A1, unknown */
	static const uint16_t offset[] = { 0x186, 0x195, 0x2C5 };
	static const int16_t dig_filter_phy_rev16[] = {
		-375, 136, -407, 208, -1527,
		956, 93, 186, 93, 230,
		-44, 230, 201, -191, 201,
	};
	int i;

	for (i = 0; i < 3; i++)
		bwn_nphy_pa_set_tx_dig_filter(mac, offset[i],
					      tbl_tx_filter_coef_rev4[i]);

	/* Verified with BCM43227 and BCM43228 */
	if (mac->mac_phy.rev == 16)
		bwn_nphy_pa_set_tx_dig_filter(mac, 0x186, dig_filter_phy_rev16);

	/* Verified with BCM43131 and BCM43217 */
	if (mac->mac_phy.rev == 17) {
		bwn_nphy_pa_set_tx_dig_filter(mac, 0x186, dig_filter_phy_rev16);
		bwn_nphy_pa_set_tx_dig_filter(mac, 0x195,
					      tbl_tx_filter_coef_rev4[1]);
	}

	if (bwn_is_40mhz(mac)) {
		bwn_nphy_pa_set_tx_dig_filter(mac, 0x186,
					      tbl_tx_filter_coef_rev4[3]);
	} else {
		if (bwn_current_band(mac) == BWN_BAND_5G)
			bwn_nphy_pa_set_tx_dig_filter(mac, 0x186,
						      tbl_tx_filter_coef_rev4[5]);
		if (bwn_get_chan(mac) == 14)
			bwn_nphy_pa_set_tx_dig_filter(mac, 0x186,
						      tbl_tx_filter_coef_rev4[6]);
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/GetTxGain */
static struct bwn_nphy_txgains bwn_nphy_get_tx_gains(struct bwn_mac *mac)
{
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;

	uint16_t curr_gain[2];
	struct bwn_nphy_txgains target;
	const uint32_t *table = NULL;

	if (!nphy->txpwrctrl) {
		int i;

		if (nphy->hang_avoid)
			bwn_nphy_stay_in_carrier_search(mac, true);
		bwn_ntab_read_bulk(mac, BWN_NTAB16(7, 0x110), 2, curr_gain);
		if (nphy->hang_avoid)
			bwn_nphy_stay_in_carrier_search(mac, false);

		for (i = 0; i < 2; ++i) {
			if (mac->mac_phy.rev >= 7) {
				target.ipa[i] = curr_gain[i] & 0x0007;
				target.pad[i] = (curr_gain[i] & 0x00F8) >> 3;
				target.pga[i] = (curr_gain[i] & 0x0F00) >> 8;
				target.txgm[i] = (curr_gain[i] & 0x7000) >> 12;
				target.tx_lpf[i] = (curr_gain[i] & 0x8000) >> 15;
			} else if (mac->mac_phy.rev >= 3) {
				target.ipa[i] = curr_gain[i] & 0x000F;
				target.pad[i] = (curr_gain[i] & 0x00F0) >> 4;
				target.pga[i] = (curr_gain[i] & 0x0F00) >> 8;
				target.txgm[i] = (curr_gain[i] & 0x7000) >> 12;
			} else {
				target.ipa[i] = curr_gain[i] & 0x0003;
				target.pad[i] = (curr_gain[i] & 0x000C) >> 2;
				target.pga[i] = (curr_gain[i] & 0x0070) >> 4;
				target.txgm[i] = (curr_gain[i] & 0x0380) >> 7;
			}
		}
	} else {
		int i;
		uint16_t index[2];
		index[0] = (BWN_PHY_READ(mac, BWN_NPHY_C1_TXPCTL_STAT) &
			BWN_NPHY_TXPCTL_STAT_BIDX) >>
			BWN_NPHY_TXPCTL_STAT_BIDX_SHIFT;
		index[1] = (BWN_PHY_READ(mac, BWN_NPHY_C2_TXPCTL_STAT) &
			BWN_NPHY_TXPCTL_STAT_BIDX) >>
			BWN_NPHY_TXPCTL_STAT_BIDX_SHIFT;

		for (i = 0; i < 2; ++i) {
			table = bwn_nphy_get_tx_gain_table(mac);
			if (!table)
				break;

			if (mac->mac_phy.rev >= 7) {
				target.ipa[i] = (table[index[i]] >> 16) & 0x7;
				target.pad[i] = (table[index[i]] >> 19) & 0x1F;
				target.pga[i] = (table[index[i]] >> 24) & 0xF;
				target.txgm[i] = (table[index[i]] >> 28) & 0x7;
				target.tx_lpf[i] = (table[index[i]] >> 31) & 0x1;
			} else if (mac->mac_phy.rev >= 3) {
				target.ipa[i] = (table[index[i]] >> 16) & 0xF;
				target.pad[i] = (table[index[i]] >> 20) & 0xF;
				target.pga[i] = (table[index[i]] >> 24) & 0xF;
				target.txgm[i] = (table[index[i]] >> 28) & 0xF;
			} else {
				target.ipa[i] = (table[index[i]] >> 16) & 0x3;
				target.pad[i] = (table[index[i]] >> 18) & 0x3;
				target.pga[i] = (table[index[i]] >> 20) & 0x7;
				target.txgm[i] = (table[index[i]] >> 23) & 0x7;
			}
		}
	}

	return target;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxCalPhyCleanup */
static void bwn_nphy_tx_cal_phy_cleanup(struct bwn_mac *mac)
{
	uint16_t *regs = mac->mac_phy.phy_n->tx_rx_cal_phy_saveregs;

	if (mac->mac_phy.rev >= 3) {
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_C1, regs[0]);
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_C2, regs[1]);
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER1, regs[2]);
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER, regs[3]);
		BWN_PHY_WRITE(mac, BWN_NPHY_BBCFG, regs[4]);
		bwn_ntab_write(mac, BWN_NTAB16(8, 3), regs[5]);
		bwn_ntab_write(mac, BWN_NTAB16(8, 19), regs[6]);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC1, regs[7]);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC2, regs[8]);
		BWN_PHY_WRITE(mac, BWN_NPHY_PAPD_EN0, regs[9]);
		BWN_PHY_WRITE(mac, BWN_NPHY_PAPD_EN1, regs[10]);
		bwn_nphy_reset_cca(mac);
	} else {
		BWN_PHY_SETMASK(mac, BWN_NPHY_AFECTL_C1, 0x0FFF, regs[0]);
		BWN_PHY_SETMASK(mac, BWN_NPHY_AFECTL_C2, 0x0FFF, regs[1]);
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER, regs[2]);
		bwn_ntab_write(mac, BWN_NTAB16(8, 2), regs[3]);
		bwn_ntab_write(mac, BWN_NTAB16(8, 18), regs[4]);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC1, regs[5]);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC2, regs[6]);
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/TxCalPhySetup */
static void bwn_nphy_tx_cal_phy_setup(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	uint16_t *regs = mac->mac_phy.phy_n->tx_rx_cal_phy_saveregs;
	uint16_t tmp;

	regs[0] = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_C1);
	regs[1] = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_C2);
	if (mac->mac_phy.rev >= 3) {
		BWN_PHY_SETMASK(mac, BWN_NPHY_AFECTL_C1, 0xF0FF, 0x0A00);
		BWN_PHY_SETMASK(mac, BWN_NPHY_AFECTL_C2, 0xF0FF, 0x0A00);

		tmp = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_OVER1);
		regs[2] = tmp;
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER1, tmp | 0x0600);

		tmp = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_OVER);
		regs[3] = tmp;
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER, tmp | 0x0600);

		regs[4] = BWN_PHY_READ(mac, BWN_NPHY_BBCFG);
		BWN_PHY_MASK(mac, BWN_NPHY_BBCFG,
			     ~BWN_NPHY_BBCFG_RSTRX & 0xFFFF);

		tmp = bwn_ntab_read(mac, BWN_NTAB16(8, 3));
		regs[5] = tmp;
		bwn_ntab_write(mac, BWN_NTAB16(8, 3), 0);

		tmp = bwn_ntab_read(mac, BWN_NTAB16(8, 19));
		regs[6] = tmp;
		bwn_ntab_write(mac, BWN_NTAB16(8, 19), 0);
		regs[7] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_INTC1);
		regs[8] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_INTC2);

		if (!nphy->use_int_tx_iq_lo_cal)
			bwn_nphy_rf_ctl_intc_override(mac, N_INTC_OVERRIDE_PA,
						      1, 3);
		else
			bwn_nphy_rf_ctl_intc_override(mac, N_INTC_OVERRIDE_PA,
						      0, 3);
		bwn_nphy_rf_ctl_intc_override(mac, N_INTC_OVERRIDE_TRSW, 2, 1);
		bwn_nphy_rf_ctl_intc_override(mac, N_INTC_OVERRIDE_TRSW, 8, 2);

		regs[9] = BWN_PHY_READ(mac, BWN_NPHY_PAPD_EN0);
		regs[10] = BWN_PHY_READ(mac, BWN_NPHY_PAPD_EN1);
		BWN_PHY_MASK(mac, BWN_NPHY_PAPD_EN0, ~0x0001);
		BWN_PHY_MASK(mac, BWN_NPHY_PAPD_EN1, ~0x0001);

		tmp = bwn_nphy_read_lpf_ctl(mac, 0);
		if (phy->rev >= 19)
			bwn_nphy_rf_ctl_override_rev19(mac, 0x80, tmp, 0, false,
						       1);
		else if (phy->rev >= 7)
			bwn_nphy_rf_ctl_override_rev7(mac, 0x80, tmp, 0, false,
						      1);

		if (nphy->use_int_tx_iq_lo_cal && true /* FIXME */) {
			if (phy->rev >= 19) {
				bwn_nphy_rf_ctl_override_rev19(mac, 0x8, 0, 0x3,
							       false, 0);
			} else if (phy->rev >= 8) {
				bwn_nphy_rf_ctl_override_rev7(mac, 0x8, 0, 0x3,
							      false, 0);
			} else if (phy->rev == 7) {
				BWN_RF_SETMASK(mac, R2057_OVR_REG0, 1 << 4, 1 << 4);
				if (bwn_current_band(mac) == BWN_BAND_2G) {
					BWN_RF_SETMASK(mac, R2057_PAD2G_TUNE_PUS_CORE0, ~1, 0);
					BWN_RF_SETMASK(mac, R2057_PAD2G_TUNE_PUS_CORE1, ~1, 0);
				} else {
					BWN_RF_SETMASK(mac, R2057_IPA5G_CASCOFFV_PU_CORE0, ~1, 0);
					BWN_RF_SETMASK(mac, R2057_IPA5G_CASCOFFV_PU_CORE1, ~1, 0);
				}
			}
		}
	} else {
		BWN_PHY_SETMASK(mac, BWN_NPHY_AFECTL_C1, 0x0FFF, 0xA000);
		BWN_PHY_SETMASK(mac, BWN_NPHY_AFECTL_C2, 0x0FFF, 0xA000);
		tmp = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_OVER);
		regs[2] = tmp;
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER, tmp | 0x3000);
		tmp = bwn_ntab_read(mac, BWN_NTAB16(8, 2));
		regs[3] = tmp;
		tmp |= 0x2000;
		bwn_ntab_write(mac, BWN_NTAB16(8, 2), tmp);
		tmp = bwn_ntab_read(mac, BWN_NTAB16(8, 18));
		regs[4] = tmp;
		tmp |= 0x2000;
		bwn_ntab_write(mac, BWN_NTAB16(8, 18), tmp);
		regs[5] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_INTC1);
		regs[6] = BWN_PHY_READ(mac, BWN_NPHY_RFCTL_INTC2);
		if (bwn_current_band(mac) == BWN_BAND_5G)
			tmp = 0x0180;
		else
			tmp = 0x0120;
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC1, tmp);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC2, tmp);
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/SaveCal */
static void bwn_nphy_save_cal(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;

	struct bwn_phy_n_iq_comp *rxcal_coeffs = NULL;
	uint16_t *txcal_radio_regs = NULL;
	struct bwn_chanspec *iqcal_chanspec;
	uint16_t *table = NULL;

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 1);

	if (bwn_current_band(mac) == BWN_BAND_2G) {
		rxcal_coeffs = &nphy->cal_cache.rxcal_coeffs_2G;
		txcal_radio_regs = nphy->cal_cache.txcal_radio_regs_2G;
		iqcal_chanspec = &nphy->iqcal_chanspec_2G;
		table = nphy->cal_cache.txcal_coeffs_2G;
	} else {
		rxcal_coeffs = &nphy->cal_cache.rxcal_coeffs_5G;
		txcal_radio_regs = nphy->cal_cache.txcal_radio_regs_5G;
		iqcal_chanspec = &nphy->iqcal_chanspec_5G;
		table = nphy->cal_cache.txcal_coeffs_5G;
	}

	bwn_nphy_rx_iq_coeffs(mac, false, rxcal_coeffs);
	/* TODO use some definitions */
	if (phy->rev >= 19) {
		/* TODO */
	} else if (phy->rev >= 7) {
		txcal_radio_regs[0] = BWN_RF_READ(mac,
						     R2057_TX0_LOFT_FINE_I);
		txcal_radio_regs[1] = BWN_RF_READ(mac,
						     R2057_TX0_LOFT_FINE_Q);
		txcal_radio_regs[4] = BWN_RF_READ(mac,
						     R2057_TX0_LOFT_COARSE_I);
		txcal_radio_regs[5] = BWN_RF_READ(mac,
						     R2057_TX0_LOFT_COARSE_Q);
		txcal_radio_regs[2] = BWN_RF_READ(mac,
						     R2057_TX1_LOFT_FINE_I);
		txcal_radio_regs[3] = BWN_RF_READ(mac,
						     R2057_TX1_LOFT_FINE_Q);
		txcal_radio_regs[6] = BWN_RF_READ(mac,
						     R2057_TX1_LOFT_COARSE_I);
		txcal_radio_regs[7] = BWN_RF_READ(mac,
						     R2057_TX1_LOFT_COARSE_Q);
	} else if (phy->rev >= 3) {
		txcal_radio_regs[0] = BWN_RF_READ(mac, 0x2021);
		txcal_radio_regs[1] = BWN_RF_READ(mac, 0x2022);
		txcal_radio_regs[2] = BWN_RF_READ(mac, 0x3021);
		txcal_radio_regs[3] = BWN_RF_READ(mac, 0x3022);
		txcal_radio_regs[4] = BWN_RF_READ(mac, 0x2023);
		txcal_radio_regs[5] = BWN_RF_READ(mac, 0x2024);
		txcal_radio_regs[6] = BWN_RF_READ(mac, 0x3023);
		txcal_radio_regs[7] = BWN_RF_READ(mac, 0x3024);
	} else {
		txcal_radio_regs[0] = BWN_RF_READ(mac, 0x8B);
		txcal_radio_regs[1] = BWN_RF_READ(mac, 0xBA);
		txcal_radio_regs[2] = BWN_RF_READ(mac, 0x8D);
		txcal_radio_regs[3] = BWN_RF_READ(mac, 0xBC);
	}
	iqcal_chanspec->center_freq = bwn_get_centre_freq(mac);
	iqcal_chanspec->channel_type = bwn_get_chan_type(mac, NULL);
	bwn_ntab_read_bulk(mac, BWN_NTAB16(15, 80), 8, table);

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, 0);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RestoreCal */
static void bwn_nphy_restore_cal(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;

	uint16_t coef[4];
	uint16_t *loft = NULL;
	uint16_t *table = NULL;

	int i;
	uint16_t *txcal_radio_regs = NULL;
	struct bwn_phy_n_iq_comp *rxcal_coeffs = NULL;

	if (bwn_current_band(mac) == BWN_BAND_2G) {
		if (!nphy->iqcal_chanspec_2G.center_freq)
			return;
		table = nphy->cal_cache.txcal_coeffs_2G;
		loft = &nphy->cal_cache.txcal_coeffs_2G[5];
	} else {
		if (!nphy->iqcal_chanspec_5G.center_freq)
			return;
		table = nphy->cal_cache.txcal_coeffs_5G;
		loft = &nphy->cal_cache.txcal_coeffs_5G[5];
	}

	bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 80), 4, table);

	for (i = 0; i < 4; i++) {
		if (mac->mac_phy.rev >= 3)
			table[i] = coef[i];
		else
			coef[i] = 0;
	}

	bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 88), 4, coef);
	bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 85), 2, loft);
	bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 93), 2, loft);

	if (mac->mac_phy.rev < 2)
		bwn_nphy_tx_iq_workaround(mac);

	if (bwn_current_band(mac) == BWN_BAND_2G) {
		txcal_radio_regs = nphy->cal_cache.txcal_radio_regs_2G;
		rxcal_coeffs = &nphy->cal_cache.rxcal_coeffs_2G;
	} else {
		txcal_radio_regs = nphy->cal_cache.txcal_radio_regs_5G;
		rxcal_coeffs = &nphy->cal_cache.rxcal_coeffs_5G;
	}

	/* TODO use some definitions */
	if (phy->rev >= 19) {
		/* TODO */
	} else if (phy->rev >= 7) {
		BWN_RF_WRITE(mac, R2057_TX0_LOFT_FINE_I,
				txcal_radio_regs[0]);
		BWN_RF_WRITE(mac, R2057_TX0_LOFT_FINE_Q,
				txcal_radio_regs[1]);
		BWN_RF_WRITE(mac, R2057_TX0_LOFT_COARSE_I,
				txcal_radio_regs[4]);
		BWN_RF_WRITE(mac, R2057_TX0_LOFT_COARSE_Q,
				txcal_radio_regs[5]);
		BWN_RF_WRITE(mac, R2057_TX1_LOFT_FINE_I,
				txcal_radio_regs[2]);
		BWN_RF_WRITE(mac, R2057_TX1_LOFT_FINE_Q,
				txcal_radio_regs[3]);
		BWN_RF_WRITE(mac, R2057_TX1_LOFT_COARSE_I,
				txcal_radio_regs[6]);
		BWN_RF_WRITE(mac, R2057_TX1_LOFT_COARSE_Q,
				txcal_radio_regs[7]);
	} else if (phy->rev >= 3) {
		BWN_RF_WRITE(mac, 0x2021, txcal_radio_regs[0]);
		BWN_RF_WRITE(mac, 0x2022, txcal_radio_regs[1]);
		BWN_RF_WRITE(mac, 0x3021, txcal_radio_regs[2]);
		BWN_RF_WRITE(mac, 0x3022, txcal_radio_regs[3]);
		BWN_RF_WRITE(mac, 0x2023, txcal_radio_regs[4]);
		BWN_RF_WRITE(mac, 0x2024, txcal_radio_regs[5]);
		BWN_RF_WRITE(mac, 0x3023, txcal_radio_regs[6]);
		BWN_RF_WRITE(mac, 0x3024, txcal_radio_regs[7]);
	} else {
		BWN_RF_WRITE(mac, 0x8B, txcal_radio_regs[0]);
		BWN_RF_WRITE(mac, 0xBA, txcal_radio_regs[1]);
		BWN_RF_WRITE(mac, 0x8D, txcal_radio_regs[2]);
		BWN_RF_WRITE(mac, 0xBC, txcal_radio_regs[3]);
	}
	bwn_nphy_rx_iq_coeffs(mac, true, rxcal_coeffs);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/CalTxIqlo */
static int bwn_nphy_cal_tx_iq_lo(struct bwn_mac *mac,
				struct bwn_nphy_txgains target,
				bool full, bool mphase)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	int i;
	int error = 0;
	int freq;
	bool avoid = false;
	uint8_t length;
	uint16_t tmp, core, type, count, max, numb, last = 0, cmd;
	const uint16_t *table;
	bool phy6or5x;

	uint16_t buffer[11];
	uint16_t diq_start = 0;
	uint16_t save[2];
	uint16_t gain[2];
	struct bwn_nphy_iqcal_params params[2];
	bool updated[2] = { };

	bwn_nphy_stay_in_carrier_search(mac, true);

	if (mac->mac_phy.rev >= 4) {
		avoid = nphy->hang_avoid;
		nphy->hang_avoid = false;
	}

	bwn_ntab_read_bulk(mac, BWN_NTAB16(7, 0x110), 2, save);

	for (i = 0; i < 2; i++) {
		bwn_nphy_iq_cal_gain_params(mac, i, target, &params[i]);
		gain[i] = params[i].cal_gain;
	}

	bwn_ntab_write_bulk(mac, BWN_NTAB16(7, 0x110), 2, gain);

	bwn_nphy_tx_cal_radio_setup(mac);
	bwn_nphy_tx_cal_phy_setup(mac);

	phy6or5x = mac->mac_phy.rev >= 6 ||
		(mac->mac_phy.rev == 5 && nphy->ipa2g_on &&
		bwn_current_band(mac) == BWN_BAND_2G);
	if (phy6or5x) {
		if (bwn_is_40mhz(mac)) {
			bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 0), 18,
					tbl_tx_iqlo_cal_loft_ladder_40);
			bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 32), 18,
					tbl_tx_iqlo_cal_iqimb_ladder_40);
		} else {
			bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 0), 18,
					tbl_tx_iqlo_cal_loft_ladder_20);
			bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 32), 18,
					tbl_tx_iqlo_cal_iqimb_ladder_20);
		}
	}

	if (phy->rev >= 19) {
		/* TODO */
	} else if (phy->rev >= 7) {
		BWN_PHY_WRITE(mac, BWN_NPHY_IQLOCAL_CMDGCTL, 0x8AD9);
	} else {
		BWN_PHY_WRITE(mac, BWN_NPHY_IQLOCAL_CMDGCTL, 0x8AA9);
	}

	if (!bwn_is_40mhz(mac))
		freq = 2500;
	else
		freq = 5000;

	if (nphy->mphase_cal_phase_id > 2)
		bwn_nphy_run_samples(mac, (bwn_is_40mhz(mac) ? 40 : 20) * 8,
				     0xFFFF, 0, true, false, false);
	else
		error = bwn_nphy_tx_tone(mac, freq, 250, true, false, false);

	if (error == 0) {
		if (nphy->mphase_cal_phase_id > 2) {
			table = nphy->mphase_txcal_bestcoeffs;
			length = 11;
			if (mac->mac_phy.rev < 3)
				length -= 2;
		} else {
			if (!full && nphy->txiqlocal_coeffsvalid) {
				table = nphy->txiqlocal_bestc;
				length = 11;
				if (mac->mac_phy.rev < 3)
					length -= 2;
			} else {
				full = true;
				if (mac->mac_phy.rev >= 3) {
					table = tbl_tx_iqlo_cal_startcoefs_nphyrev3;
					length = BWN_NTAB_TX_IQLO_CAL_STARTCOEFS_REV3;
				} else {
					table = tbl_tx_iqlo_cal_startcoefs;
					length = BWN_NTAB_TX_IQLO_CAL_STARTCOEFS;
				}
			}
		}

		bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 64), length, table);

		if (full) {
			if (mac->mac_phy.rev >= 3)
				max = BWN_NTAB_TX_IQLO_CAL_CMDS_FULLCAL_REV3;
			else
				max = BWN_NTAB_TX_IQLO_CAL_CMDS_FULLCAL;
		} else {
			if (mac->mac_phy.rev >= 3)
				max = BWN_NTAB_TX_IQLO_CAL_CMDS_RECAL_REV3;
			else
				max = BWN_NTAB_TX_IQLO_CAL_CMDS_RECAL;
		}

		if (mphase) {
			count = nphy->mphase_txcal_cmdidx;
			numb = min(max,
				(uint16_t)(count + nphy->mphase_txcal_numcmds));
		} else {
			count = 0;
			numb = max;
		}

		for (; count < numb; count++) {
			if (full) {
				if (mac->mac_phy.rev >= 3)
					cmd = tbl_tx_iqlo_cal_cmds_fullcal_nphyrev3[count];
				else
					cmd = tbl_tx_iqlo_cal_cmds_fullcal[count];
			} else {
				if (mac->mac_phy.rev >= 3)
					cmd = tbl_tx_iqlo_cal_cmds_recal_nphyrev3[count];
				else
					cmd = tbl_tx_iqlo_cal_cmds_recal[count];
			}

			core = (cmd & 0x3000) >> 12;
			type = (cmd & 0x0F00) >> 8;

			if (phy6or5x && updated[core] == 0) {
				bwn_nphy_update_tx_cal_ladder(mac, core);
				updated[core] = true;
			}

			tmp = (params[core].ncorr[type] << 8) | 0x66;
			BWN_PHY_WRITE(mac, BWN_NPHY_IQLOCAL_CMDNNUM, tmp);

			if (type == 1 || type == 3 || type == 4) {
				buffer[0] = bwn_ntab_read(mac,
						BWN_NTAB16(15, 69 + core));
				diq_start = buffer[0];
				buffer[0] = 0;
				bwn_ntab_write(mac, BWN_NTAB16(15, 69 + core),
						0);
			}

			BWN_PHY_WRITE(mac, BWN_NPHY_IQLOCAL_CMD, cmd);
			for (i = 0; i < 2000; i++) {
				tmp = BWN_PHY_READ(mac, BWN_NPHY_IQLOCAL_CMD);
				if (tmp & 0xC000)
					break;
				DELAY(10);
			}

			bwn_ntab_read_bulk(mac, BWN_NTAB16(15, 96), length,
						buffer);
			bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 64), length,
						buffer);

			if (type == 1 || type == 3 || type == 4)
				buffer[0] = diq_start;
		}

		if (mphase)
			nphy->mphase_txcal_cmdidx = (numb >= max) ? 0 : numb;

		last = (mac->mac_phy.rev < 3) ? 6 : 7;

		if (!mphase || nphy->mphase_cal_phase_id == last) {
			bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 96), 4, buffer);
			bwn_ntab_read_bulk(mac, BWN_NTAB16(15, 80), 4, buffer);
			if (mac->mac_phy.rev < 3) {
				buffer[0] = 0;
				buffer[1] = 0;
				buffer[2] = 0;
				buffer[3] = 0;
			}
			bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 88), 4,
						buffer);
			bwn_ntab_read_bulk(mac, BWN_NTAB16(15, 101), 2,
						buffer);
			bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 85), 2,
						buffer);
			bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 93), 2,
						buffer);
			length = 11;
			if (mac->mac_phy.rev < 3)
				length -= 2;
			bwn_ntab_read_bulk(mac, BWN_NTAB16(15, 96), length,
						nphy->txiqlocal_bestc);
			nphy->txiqlocal_coeffsvalid = true;
			nphy->txiqlocal_chanspec.center_freq =
						bwn_get_centre_freq(mac);
			nphy->txiqlocal_chanspec.channel_type = bwn_get_chan_type(mac, NULL);
		} else {
			length = 11;
			if (mac->mac_phy.rev < 3)
				length -= 2;
			bwn_ntab_read_bulk(mac, BWN_NTAB16(15, 96), length,
						nphy->mphase_txcal_bestcoeffs);
		}

		bwn_nphy_stop_playback(mac);
		BWN_PHY_WRITE(mac, BWN_NPHY_IQLOCAL_CMDGCTL, 0);
	}

	bwn_nphy_tx_cal_phy_cleanup(mac);
	bwn_ntab_write_bulk(mac, BWN_NTAB16(7, 0x110), 2, save);

	if (mac->mac_phy.rev < 2 && (!mphase || nphy->mphase_cal_phase_id == last))
		bwn_nphy_tx_iq_workaround(mac);

	if (mac->mac_phy.rev >= 4)
		nphy->hang_avoid = avoid;

	bwn_nphy_stay_in_carrier_search(mac, false);

	return error;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/ReapplyTxCalCoeffs */
static void bwn_nphy_reapply_tx_cal_coeffs(struct bwn_mac *mac)
{
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	uint8_t i;
	uint16_t buffer[7];
	bool equal = true;

	if (!nphy->txiqlocal_coeffsvalid ||
	    nphy->txiqlocal_chanspec.center_freq != bwn_get_centre_freq(mac) ||
	    nphy->txiqlocal_chanspec.channel_type != bwn_get_chan_type(mac, NULL))
		return;

	bwn_ntab_read_bulk(mac, BWN_NTAB16(15, 80), 7, buffer);
	for (i = 0; i < 4; i++) {
		if (buffer[i] != nphy->txiqlocal_bestc[i]) {
			equal = false;
			break;
		}
	}

	if (!equal) {
		bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 80), 4,
					nphy->txiqlocal_bestc);
		for (i = 0; i < 4; i++)
			buffer[i] = 0;
		bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 88), 4,
					buffer);
		bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 85), 2,
					&nphy->txiqlocal_bestc[5]);
		bwn_ntab_write_bulk(mac, BWN_NTAB16(15, 93), 2,
					&nphy->txiqlocal_bestc[5]);
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/CalRxIqRev2 */
static int bwn_nphy_rev2_cal_rx_iq(struct bwn_mac *mac,
			struct bwn_nphy_txgains target, uint8_t type, bool debug)
{
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	int i, j, index;
	uint8_t rfctl[2];
	uint8_t afectl_core;
	uint16_t tmp[6];
	uint16_t cur_hpf1, cur_hpf2, cur_lna;
	uint32_t real, imag;
	bwn_band_t band;

	uint8_t use;
	uint16_t cur_hpf;
	uint16_t lna[3] = { 3, 3, 1 };
	uint16_t hpf1[3] = { 7, 2, 0 };
	uint16_t hpf2[3] = { 2, 0, 0 };
	uint32_t power[3] = { };
	uint16_t gain_save[2];
	uint16_t cal_gain[2];
	struct bwn_nphy_iqcal_params cal_params[2];
	struct bwn_nphy_iq_est est;
	int ret = 0;
	bool playtone = true;
	int desired = 13;

	bwn_nphy_stay_in_carrier_search(mac, 1);

	if (mac->mac_phy.rev < 2)
		bwn_nphy_reapply_tx_cal_coeffs(mac);
	bwn_ntab_read_bulk(mac, BWN_NTAB16(7, 0x110), 2, gain_save);
	for (i = 0; i < 2; i++) {
		bwn_nphy_iq_cal_gain_params(mac, i, target, &cal_params[i]);
		cal_gain[i] = cal_params[i].cal_gain;
	}
	bwn_ntab_write_bulk(mac, BWN_NTAB16(7, 0x110), 2, cal_gain);

	for (i = 0; i < 2; i++) {
		if (i == 0) {
			rfctl[0] = BWN_NPHY_RFCTL_INTC1;
			rfctl[1] = BWN_NPHY_RFCTL_INTC2;
			afectl_core = BWN_NPHY_AFECTL_C1;
		} else {
			rfctl[0] = BWN_NPHY_RFCTL_INTC2;
			rfctl[1] = BWN_NPHY_RFCTL_INTC1;
			afectl_core = BWN_NPHY_AFECTL_C2;
		}

		tmp[1] = BWN_PHY_READ(mac, BWN_NPHY_RFSEQCA);
		tmp[2] = BWN_PHY_READ(mac, afectl_core);
		tmp[3] = BWN_PHY_READ(mac, BWN_NPHY_AFECTL_OVER);
		tmp[4] = BWN_PHY_READ(mac, rfctl[0]);
		tmp[5] = BWN_PHY_READ(mac, rfctl[1]);

		BWN_PHY_SETMASK(mac, BWN_NPHY_RFSEQCA,
				~BWN_NPHY_RFSEQCA_RXDIS & 0xFFFF,
				((1 - i) << BWN_NPHY_RFSEQCA_RXDIS_SHIFT));
		BWN_PHY_SETMASK(mac, BWN_NPHY_RFSEQCA, ~BWN_NPHY_RFSEQCA_TXEN,
				(1 - i));
		BWN_PHY_SET(mac, afectl_core, 0x0006);
		BWN_PHY_SET(mac, BWN_NPHY_AFECTL_OVER, 0x0006);

		band = bwn_current_band(mac);

		if (nphy->rxcalparams & 0xFF000000) {
			if (band == BWN_BAND_5G)
				BWN_PHY_WRITE(mac, rfctl[0], 0x140);
			else
				BWN_PHY_WRITE(mac, rfctl[0], 0x110);
		} else {
			if (band == BWN_BAND_5G)
				BWN_PHY_WRITE(mac, rfctl[0], 0x180);
			else
				BWN_PHY_WRITE(mac, rfctl[0], 0x120);
		}

		if (band == BWN_BAND_5G)
			BWN_PHY_WRITE(mac, rfctl[1], 0x148);
		else
			BWN_PHY_WRITE(mac, rfctl[1], 0x114);

		if (nphy->rxcalparams & 0x10000) {
			BWN_RF_SETMASK(mac, B2055_C1_GENSPARE2, 0xFC,
					(i + 1));
			BWN_RF_SETMASK(mac, B2055_C2_GENSPARE2, 0xFC,
					(2 - i));
		}

		for (j = 0; j < 4; j++) {
			if (j < 3) {
				cur_lna = lna[j];
				cur_hpf1 = hpf1[j];
				cur_hpf2 = hpf2[j];
			} else {
				if (power[1] > 10000) {
					use = 1;
					cur_hpf = cur_hpf1;
					index = 2;
				} else {
					if (power[0] > 10000) {
						use = 1;
						cur_hpf = cur_hpf1;
						index = 1;
					} else {
						index = 0;
						use = 2;
						cur_hpf = cur_hpf2;
					}
				}
				cur_lna = lna[index];
				cur_hpf1 = hpf1[index];
				cur_hpf2 = hpf2[index];
				cur_hpf += desired - bwn_hweight32(power[index]);
				cur_hpf = bwn_clamp_val(cur_hpf, 0, 10);
				if (use == 1)
					cur_hpf1 = cur_hpf;
				else
					cur_hpf2 = cur_hpf;
			}

			tmp[0] = ((cur_hpf2 << 8) | (cur_hpf1 << 4) |
					(cur_lna << 2));
			bwn_nphy_rf_ctl_override(mac, 0x400, tmp[0], 3,
									false);
			bwn_nphy_force_rf_sequence(mac, BWN_RFSEQ_RESET2RX);
			bwn_nphy_stop_playback(mac);

			if (playtone) {
				ret = bwn_nphy_tx_tone(mac, 4000,
						(nphy->rxcalparams & 0xFFFF),
						false, false, true);
				playtone = false;
			} else {
				bwn_nphy_run_samples(mac, 160, 0xFFFF, 0, false,
						     false, true);
			}

			if (ret == 0) {
				if (j < 3) {
					bwn_nphy_rx_iq_est(mac, &est, 1024, 32,
									false);
					if (i == 0) {
						real = est.i0_pwr;
						imag = est.q0_pwr;
					} else {
						real = est.i1_pwr;
						imag = est.q1_pwr;
					}
					power[i] = ((real + imag) / 1024) + 1;
				} else {
					bwn_nphy_calc_rx_iq_comp(mac, 1 << i);
				}
				bwn_nphy_stop_playback(mac);
			}

			if (ret != 0)
				break;
		}

		BWN_RF_MASK(mac, B2055_C1_GENSPARE2, 0xFC);
		BWN_RF_MASK(mac, B2055_C2_GENSPARE2, 0xFC);
		BWN_PHY_WRITE(mac, rfctl[1], tmp[5]);
		BWN_PHY_WRITE(mac, rfctl[0], tmp[4]);
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER, tmp[3]);
		BWN_PHY_WRITE(mac, afectl_core, tmp[2]);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFSEQCA, tmp[1]);

		if (ret != 0)
			break;
	}

	bwn_nphy_rf_ctl_override(mac, 0x400, 0, 3, true);
	bwn_nphy_force_rf_sequence(mac, BWN_RFSEQ_RESET2RX);
	bwn_ntab_write_bulk(mac, BWN_NTAB16(7, 0x110), 2, gain_save);

	bwn_nphy_stay_in_carrier_search(mac, 0);

	return ret;
}

static int bwn_nphy_rev3_cal_rx_iq(struct bwn_mac *mac,
			struct bwn_nphy_txgains target, uint8_t type, bool debug)
{
	return -1;
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/CalRxIq */
static int bwn_nphy_cal_rx_iq(struct bwn_mac *mac,
			struct bwn_nphy_txgains target, uint8_t type, bool debug)
{
	if (mac->mac_phy.rev >= 7)
		type = 0;

	if (mac->mac_phy.rev >= 3)
		return bwn_nphy_rev3_cal_rx_iq(mac, target, type, debug);
	else
		return bwn_nphy_rev2_cal_rx_iq(mac, target, type, debug);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/RxCoreSetState */
static void bwn_nphy_set_rx_core_state(struct bwn_mac *mac, uint8_t mask)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = phy->phy_n;
	/* uint16_t buf[16]; it's rev3+ */

	nphy->phyrxchain = mask;

	if (0 /* FIXME clk */)
		return;

	bwn_mac_suspend(mac);

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, true);

	BWN_PHY_SETMASK(mac, BWN_NPHY_RFSEQCA, ~BWN_NPHY_RFSEQCA_RXEN,
			(mask & 0x3) << BWN_NPHY_RFSEQCA_RXEN_SHIFT);

	if ((mask & 0x3) != 0x3) {
		BWN_PHY_WRITE(mac, BWN_NPHY_HPANT_SWTHRES, 1);
		if (mac->mac_phy.rev >= 3) {
			/* TODO */
		}
	} else {
		BWN_PHY_WRITE(mac, BWN_NPHY_HPANT_SWTHRES, 0x1E);
		if (mac->mac_phy.rev >= 3) {
			/* TODO */
		}
	}

	bwn_nphy_force_rf_sequence(mac, BWN_RFSEQ_RESET2RX);

	if (nphy->hang_avoid)
		bwn_nphy_stay_in_carrier_search(mac, false);

	bwn_mac_enable(mac);
}

bwn_txpwr_result_t
bwn_nphy_op_recalc_txpower(struct bwn_mac *mac, bool ignore_tssi)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	struct ieee80211_channel *channel = bwn_get_channel(mac);
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_ppr *ppr = &nphy->tx_pwr_max_ppr;
	uint8_t max; /* qdBm */
	bool tx_pwr_state;

	if (nphy->tx_pwr_last_recalc_freq == bwn_get_centre_freq(mac) &&
	    nphy->tx_pwr_last_recalc_limit == phy->txpower)
		return BWN_TXPWR_RES_DONE;

	/* Make sure we have a clean PPR */
	bwn_ppr_clear(mac, ppr);

	/* HW limitations */
	bwn_ppr_load_max_from_sprom(mac, ppr, BWN_PHY_BAND_2G);
	/* XXX TODO: other bands? */

	/* Regulatory & user settings */
	max = INT_TO_Q52(bwn_get_chan_power(mac, channel));
	/* uint8_t */
	if (phy->txpower)
		max = min(max, INT_TO_Q52(phy->txpower));
	bwn_ppr_apply_max(mac, ppr, max);
	DPRINTF(mac->mac_sc, BWN_DEBUG_XMIT_POWER,
	    "Calculated TX power: " Q52_FMT "\n",
	     Q52_ARG(bwn_ppr_get_max(mac, ppr)));

	/* TODO: Enable this once we get gains working */
#if 0
	/* Some extra gains */
	hw_gain = 6; /* N-PHY specific */
	if (bwn_current_band(mac) == BWN_BAND_2G)
		hw_gain += sprom->antenna_gain.a0;
	else
		hw_gain += sprom->antenna_gain.a1;
	bwn_ppr_add(mac, ppr, -hw_gain);
#endif

	/* Make sure we didn't go too low */
	bwn_ppr_apply_min(mac, ppr, INT_TO_Q52(8));

	/* Apply */
	tx_pwr_state = nphy->txpwrctrl;
	bwn_mac_suspend(mac);
	bwn_nphy_tx_power_ctl_setup(mac);
	if (bhnd_get_hwrev(sc->sc_dev) == 11 || bhnd_get_hwrev(sc->sc_dev) == 12) {
		BWN_WRITE_SETMASK4(mac, BWN_MACCTL, ~0, BWN_MACCTL_PHY_LOCK);
		BWN_READ_4(mac, BWN_MACCTL);
		DELAY(1);
	}
	bwn_nphy_tx_power_ctrl(mac, nphy->txpwrctrl);
	if (bhnd_get_hwrev(sc->sc_dev) == 11 || bhnd_get_hwrev(sc->sc_dev) == 12)
		BWN_WRITE_SETMASK4(mac, BWN_MACCTL, ~BWN_MACCTL_PHY_LOCK, 0);
	bwn_mac_enable(mac);

	nphy->tx_pwr_last_recalc_freq = bwn_get_centre_freq(mac);
	nphy->tx_pwr_last_recalc_limit = phy->txpower;

	return BWN_TXPWR_RES_DONE;
}

/**************************************************
 * N-PHY init
 **************************************************/

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/MIMOConfig */
static void bwn_nphy_update_mimo_config(struct bwn_mac *mac, int32_t preamble)
{
	uint16_t mimocfg = BWN_PHY_READ(mac, BWN_NPHY_MIMOCFG);

	mimocfg |= BWN_NPHY_MIMOCFG_AUTO;
	if (preamble == 1)
		mimocfg |= BWN_NPHY_MIMOCFG_GFMIX;
	else
		mimocfg &= ~BWN_NPHY_MIMOCFG_GFMIX;

	BWN_PHY_WRITE(mac, BWN_NPHY_MIMOCFG, mimocfg);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/BPHYInit */
static void bwn_nphy_bphy_init(struct bwn_mac *mac)
{
	unsigned int i;
	uint16_t val;

	val = 0x1E1F;
	for (i = 0; i < 16; i++) {
		BWN_PHY_WRITE(mac, BWN_PHY_N_BMODE(0x88 + i), val);
		val -= 0x202;
	}
	val = 0x3E3F;
	for (i = 0; i < 16; i++) {
		BWN_PHY_WRITE(mac, BWN_PHY_N_BMODE(0x98 + i), val);
		val -= 0x202;
	}
	BWN_PHY_WRITE(mac, BWN_PHY_N_BMODE(0x38), 0x668);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/SuperSwitchInit */
static int bwn_nphy_superswitch_init(struct bwn_mac *mac, bool init)
{
	int error;

	if (mac->mac_phy.rev >= 7)
		return (0);

	if (mac->mac_phy.rev >= 3) {
		if (!init)
			return (0);
		if (0 /* FIXME */) {
			bwn_ntab_write(mac, BWN_NTAB16(9, 2), 0x211);
			bwn_ntab_write(mac, BWN_NTAB16(9, 3), 0x222);
			bwn_ntab_write(mac, BWN_NTAB16(9, 8), 0x144);
			bwn_ntab_write(mac, BWN_NTAB16(9, 12), 0x188);
		}
	} else {
		BWN_PHY_WRITE(mac, BWN_NPHY_GPIO_LOOEN, 0);
		BWN_PHY_WRITE(mac, BWN_NPHY_GPIO_HIOEN, 0);

		if ((error = bwn_gpio_control(mac, 0xfc00)))
			return (error);

		BWN_WRITE_SETMASK4(mac, BWN_MACCTL, ~BWN_MACCTL_GPOUT_MASK, 0);
		BWN_WRITE_SETMASK2(mac, BWN_GPIO_MASK, ~0, 0xFC00);
		BWN_WRITE_SETMASK2(mac, BWN_GPIO_CONTROL, (~0xFC00 & 0xFFFF),
			      0);

		if (init) {
			BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_LUT_TRSW_LO1, 0x2D8);
			BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_LUT_TRSW_UP1, 0x301);
			BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_LUT_TRSW_LO2, 0x2D8);
			BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_LUT_TRSW_UP2, 0x301);
		}
	}

	return (0);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/Init/N */
static int bwn_phy_initn(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = phy->phy_n;
	uint8_t tx_pwr_state;
	struct bwn_nphy_txgains target;
	int error;
	uint16_t tmp;
	bwn_band_t tmp2;
	bool do_rssi_cal;

	uint16_t clip[2];
	bool do_cal = false;

	if (mac->mac_phy.rev >= 3) {
		error = bhnd_nvram_getvar_uint8(sc->sc_dev, BHND_NVAR_TSSIPOS2G,
		    &nphy->tsspos_2g);
		if (error) {
			BWN_ERRPRINTF(mac->mac_sc, "Error reading %s from "
			    "NVRAM: %d\n", BHND_NVAR_TSSIPOS2G, error);
			return (error);
		}
	} else {
		nphy->tsspos_2g = 0;
	}

	if ((mac->mac_phy.rev >= 3) &&
	   (sc->sc_board_info.board_flags & BHND_BFL_EXTLNA) &&
	   (bwn_current_band(mac) == BWN_BAND_2G))
	{
		BHND_CHIPC_WRITE_CHIPCTRL(sc->sc_chipc, 0x40, 0x40);
	}
	nphy->use_int_tx_iq_lo_cal = bwn_nphy_ipa(mac) ||
		phy->rev >= 7 ||
		(phy->rev >= 5 &&
		 sc->sc_board_info.board_flags2 & BHND_BFL2_INTERNDET_TXIQCAL);
	nphy->deaf_count = 0;
	bwn_nphy_tables_init(mac);
	nphy->crsminpwr_adjusted = false;
	nphy->noisevars_adjusted = false;

	/* Clear all overrides */
	if (mac->mac_phy.rev >= 3) {
		BWN_PHY_WRITE(mac, BWN_NPHY_TXF_40CO_B1S1, 0);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_OVER, 0);
		if (phy->rev >= 7) {
			BWN_PHY_WRITE(mac, BWN_NPHY_REV7_RF_CTL_OVER3, 0);
			BWN_PHY_WRITE(mac, BWN_NPHY_REV7_RF_CTL_OVER4, 0);
			BWN_PHY_WRITE(mac, BWN_NPHY_REV7_RF_CTL_OVER5, 0);
			BWN_PHY_WRITE(mac, BWN_NPHY_REV7_RF_CTL_OVER6, 0);
		}
		if (phy->rev >= 19) {
			/* TODO */
		}

		BWN_PHY_WRITE(mac, BWN_NPHY_TXF_40CO_B1S0, 0);
		BWN_PHY_WRITE(mac, BWN_NPHY_TXF_40CO_B32S1, 0);
	} else {
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_OVER, 0);
	}
	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC1, 0);
	BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC2, 0);
	if (mac->mac_phy.rev < 6) {
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC3, 0);
		BWN_PHY_WRITE(mac, BWN_NPHY_RFCTL_INTC4, 0);
	}
	BWN_PHY_MASK(mac, BWN_NPHY_RFSEQMODE,
		     ~(BWN_NPHY_RFSEQMODE_CAOVER |
		       BWN_NPHY_RFSEQMODE_TROVER));
	if (mac->mac_phy.rev >= 3)
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER1, 0);
	BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER, 0);

	if (mac->mac_phy.rev <= 2) {
		tmp = (mac->mac_phy.rev == 2) ? 0x3B : 0x40;
		BWN_PHY_SETMASK(mac, BWN_NPHY_BPHY_CTL3,
				~BWN_NPHY_BPHY_CTL3_SCALE,
				tmp << BWN_NPHY_BPHY_CTL3_SCALE_SHIFT);
	}
	BWN_PHY_WRITE(mac, BWN_NPHY_AFESEQ_TX2RX_PUD_20M, 0x20);
	BWN_PHY_WRITE(mac, BWN_NPHY_AFESEQ_TX2RX_PUD_40M, 0x20);

	if (sc->sc_board_info.board_flags2 & BHND_BFL2_SKWRKFEM_BRD ||
	    (sc->sc_board_info.board_vendor == PCI_VENDOR_APPLE &&
	     sc->sc_board_info.board_type == BHND_BOARD_BCM943224M93))
		BWN_PHY_WRITE(mac, BWN_NPHY_TXREALFD, 0xA0);
	else
		BWN_PHY_WRITE(mac, BWN_NPHY_TXREALFD, 0xB8);
	BWN_PHY_WRITE(mac, BWN_NPHY_MIMO_CRSTXEXT, 0xC8);
	BWN_PHY_WRITE(mac, BWN_NPHY_PLOAD_CSENSE_EXTLEN, 0x50);
	BWN_PHY_WRITE(mac, BWN_NPHY_TXRIFS_FRDEL, 0x30);

	if (phy->rev < 8)
		bwn_nphy_update_mimo_config(mac, nphy->preamble_override);

	bwn_nphy_update_txrx_chain(mac);

	if (phy->rev < 2) {
		BWN_PHY_WRITE(mac, BWN_NPHY_DUP40_GFBL, 0xAA8);
		BWN_PHY_WRITE(mac, BWN_NPHY_DUP40_BL, 0x9A4);
	}

	tmp2 = bwn_current_band(mac);
	if (bwn_nphy_ipa(mac)) {
		BWN_PHY_SET(mac, BWN_NPHY_PAPD_EN0, 0x1);
		BWN_PHY_SETMASK(mac, BWN_NPHY_EPS_TABLE_ADJ0, 0x007F,
				nphy->papd_epsilon_offset[0] << 7);
		BWN_PHY_SET(mac, BWN_NPHY_PAPD_EN1, 0x1);
		BWN_PHY_SETMASK(mac, BWN_NPHY_EPS_TABLE_ADJ1, 0x007F,
				nphy->papd_epsilon_offset[1] << 7);
		bwn_nphy_int_pa_set_tx_dig_filters(mac);
	} else if (phy->rev >= 5) {
		bwn_nphy_ext_pa_set_tx_dig_filters(mac);
	}

	if ((error = bwn_nphy_workarounds(mac)))
		return (error);

	/* Reset CCA, in init code it differs a little from standard way */
	bwn_phy_force_clock(mac, 1);
	tmp = BWN_PHY_READ(mac, BWN_NPHY_BBCFG);
	BWN_PHY_WRITE(mac, BWN_NPHY_BBCFG, tmp | BWN_NPHY_BBCFG_RSTCCA);
	BWN_PHY_WRITE(mac, BWN_NPHY_BBCFG, tmp & ~BWN_NPHY_BBCFG_RSTCCA);
	bwn_phy_force_clock(mac, 0);

	bwn_mac_phy_clock_set(mac, true);

	if (phy->rev < 7) {
		bwn_nphy_pa_override(mac, false);
		bwn_nphy_force_rf_sequence(mac, BWN_RFSEQ_RX2TX);
		bwn_nphy_force_rf_sequence(mac, BWN_RFSEQ_RESET2RX);
		bwn_nphy_pa_override(mac, true);
	}

	bwn_nphy_classifier(mac, 0, 0);
	bwn_nphy_read_clip_detection(mac, clip);
	if (bwn_current_band(mac) == BWN_BAND_2G)
		bwn_nphy_bphy_init(mac);

	tx_pwr_state = nphy->txpwrctrl;
	bwn_nphy_tx_power_ctrl(mac, false);
	if ((error = bwn_nphy_tx_power_fix(mac)))
		return (error);
	bwn_nphy_tx_power_ctl_idle_tssi(mac);
	bwn_nphy_tx_power_ctl_setup(mac);
	bwn_nphy_tx_gain_table_upload(mac);

	if (nphy->phyrxchain != 3)
		bwn_nphy_set_rx_core_state(mac, nphy->phyrxchain);
	if (nphy->mphase_cal_phase_id > 0)
		;/* TODO PHY Periodic Calibration Multi-Phase Restart */

	do_rssi_cal = false;
	if (phy->rev >= 3) {
		if (bwn_current_band(mac) == BWN_BAND_2G)
			do_rssi_cal = !nphy->rssical_chanspec_2G.center_freq;
		else
			do_rssi_cal = !nphy->rssical_chanspec_5G.center_freq;

		if (do_rssi_cal)
			bwn_nphy_rssi_cal(mac);
		else
			bwn_nphy_restore_rssi_cal(mac);
	} else {
		bwn_nphy_rssi_cal(mac);
	}

	if (!((nphy->measure_hold & 0x6) != 0)) {
		if (bwn_current_band(mac) == BWN_BAND_2G)
			do_cal = !nphy->iqcal_chanspec_2G.center_freq;
		else
			do_cal = !nphy->iqcal_chanspec_5G.center_freq;

		if (nphy->mute)
			do_cal = false;

		if (do_cal) {
			target = bwn_nphy_get_tx_gains(mac);

			if (nphy->antsel_type == 2) {
				error = bwn_nphy_superswitch_init(mac, true);
				if (error)
					return (error);
			}
			if (nphy->perical != 2) {
				bwn_nphy_rssi_cal(mac);
				if (phy->rev >= 3) {
					nphy->cal_orig_pwr_idx[0] =
					    nphy->txpwrindex[0].index_internal;
					nphy->cal_orig_pwr_idx[1] =
					    nphy->txpwrindex[1].index_internal;
					/* TODO N PHY Pre Calibrate TX Gain */
					target = bwn_nphy_get_tx_gains(mac);
				}
				if (!bwn_nphy_cal_tx_iq_lo(mac, target, true, false))
					if (bwn_nphy_cal_rx_iq(mac, target, 2, 0) == 0)
						bwn_nphy_save_cal(mac);
			} else if (nphy->mphase_cal_phase_id == 0)
				;/* N PHY Periodic Calibration with arg 3 */
		} else {
			bwn_nphy_restore_cal(mac);
		}
	}

	bwn_nphy_tx_pwr_ctrl_coef_setup(mac);
	bwn_nphy_tx_power_ctrl(mac, tx_pwr_state);
	BWN_PHY_WRITE(mac, BWN_NPHY_TXMACIF_HOLDOFF, 0x0015);
	BWN_PHY_WRITE(mac, BWN_NPHY_TXMACDELAY, 0x0320);
	if (phy->rev >= 3 && phy->rev <= 6)
		BWN_PHY_WRITE(mac, BWN_NPHY_PLOAD_CSENSE_EXTLEN, 0x0032);
	bwn_nphy_tx_lpf_bw(mac);
	if (phy->rev >= 3)
		bwn_nphy_spur_workaround(mac);

	return 0;
}

/**************************************************
 * Channel switching ops.
 **************************************************/

static void bwn_chantab_phy_upload(struct bwn_mac *mac,
				   const struct bwn_phy_n_sfo_cfg *e)
{
	BWN_PHY_WRITE(mac, BWN_NPHY_BW1A, e->phy_bw1a);
	BWN_PHY_WRITE(mac, BWN_NPHY_BW2, e->phy_bw2);
	BWN_PHY_WRITE(mac, BWN_NPHY_BW3, e->phy_bw3);
	BWN_PHY_WRITE(mac, BWN_NPHY_BW4, e->phy_bw4);
	BWN_PHY_WRITE(mac, BWN_NPHY_BW5, e->phy_bw5);
	BWN_PHY_WRITE(mac, BWN_NPHY_BW6, e->phy_bw6);
}

/* http://bcm-v4.sipsolutions.net/802.11/PmuSpurAvoid */
static void bwn_nphy_pmu_spur_avoid(struct bwn_mac *mac,
				    bhnd_pmu_spuravoid mode)
{
	struct bwn_softc *sc = mac->mac_sc;
	int error;

	DPRINTF(sc, BWN_DEBUG_RESET, "%s: spuravoid %d\n", __func__, mode);

	if (sc->sc_pmu == NULL) {
		BWN_ERRPRINTF(mac->mac_sc, "no PMU; cannot configure spurious "
		    "signal avoidance\n");
		return;
	}

	if ((error = bhnd_pmu_request_spuravoid(sc->sc_pmu, mode))) {
		device_printf(sc->sc_dev, "spuravoid request failed: %d",
		    error);
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/ChanspecSetup */
static int bwn_nphy_channel_setup(struct bwn_mac *mac,
				const struct bwn_phy_n_sfo_cfg *e,
				struct ieee80211_channel *new_channel)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = mac->mac_phy.phy_n;
	int ch = new_channel->ic_ieee;
	int error;
	uint16_t tmp16;

	if (bwn_channel_band(mac, new_channel) == BWN_BAND_5G) {
		DPRINTF(sc, BWN_DEBUG_RESET, "%s: BAND_5G; chan=%d\n", __func__, ch);
		/* Switch to 2 GHz for a moment to access BWN_PHY_B_BBCFG */
		BWN_PHY_MASK(mac, BWN_NPHY_BANDCTL, ~BWN_NPHY_BANDCTL_5GHZ);

		tmp16 = BWN_READ_2(mac, BWN_PSM_PHY_HDR);
		BWN_WRITE_2(mac, BWN_PSM_PHY_HDR, tmp16 | 4);
		/* Put BPHY in the reset */
		BWN_PHY_SET(mac, BWN_PHY_B_BBCFG,
			    BWN_PHY_B_BBCFG_RSTCCA | BWN_PHY_B_BBCFG_RSTRX);
		BWN_WRITE_2(mac, BWN_PSM_PHY_HDR, tmp16);
		BWN_PHY_SET(mac, BWN_NPHY_BANDCTL, BWN_NPHY_BANDCTL_5GHZ);
	} else if (bwn_channel_band(mac, new_channel) == BWN_BAND_2G) {
		DPRINTF(sc, BWN_DEBUG_RESET, "%s: BAND_2G; chan=%d\n", __func__, ch);
		BWN_PHY_MASK(mac, BWN_NPHY_BANDCTL, ~BWN_NPHY_BANDCTL_5GHZ);
		tmp16 = BWN_READ_2(mac, BWN_PSM_PHY_HDR);
		BWN_WRITE_2(mac, BWN_PSM_PHY_HDR, tmp16 | 4);
		/* Take BPHY out of the reset */
		BWN_PHY_MASK(mac, BWN_PHY_B_BBCFG,
			     (uint16_t)~(BWN_PHY_B_BBCFG_RSTCCA | BWN_PHY_B_BBCFG_RSTRX));
		BWN_WRITE_2(mac, BWN_PSM_PHY_HDR, tmp16);
	} else {
		BWN_ERRPRINTF(mac->mac_sc, "%s: unknown band?\n", __func__);
	}

	bwn_chantab_phy_upload(mac, e);

	if (new_channel->ic_ieee == 14) {
		bwn_nphy_classifier(mac, 2, 0);
		BWN_PHY_SET(mac, BWN_PHY_B_TEST, 0x0800);
	} else {
		bwn_nphy_classifier(mac, 2, 2);
		if (bwn_channel_band(mac, new_channel) == BWN_BAND_2G)
			BWN_PHY_MASK(mac, BWN_PHY_B_TEST, ~0x840);
	}

	if (!nphy->txpwrctrl) {
		if ((error = bwn_nphy_tx_power_fix(mac)))
			return (error);
	}

	if (mac->mac_phy.rev < 3)
		bwn_nphy_adjust_lna_gain_table(mac);

	bwn_nphy_tx_lpf_bw(mac);

	if (mac->mac_phy.rev >= 3 &&
	    mac->mac_phy.phy_n->spur_avoid != BWN_SPUR_AVOID_DISABLE) {
		bhnd_pmu_spuravoid spuravoid = BHND_PMU_SPURAVOID_NONE;

		if (mac->mac_phy.phy_n->spur_avoid == BWN_SPUR_AVOID_FORCE) {
			spuravoid = BHND_PMU_SPURAVOID_M1;
		} else if (phy->rev >= 19) {
			/* TODO */
		} else if (phy->rev >= 18) {
			/* TODO */
		} else if (phy->rev >= 17) {
			/* TODO: Off for channels 1-11, but check 12-14! */
		} else if (phy->rev >= 16) {
			/* TODO: Off for 2 GHz, but check 5 GHz! */
		} else if (phy->rev >= 7) {
			if (!bwn_is_40mhz(mac)) { /* 20MHz */
				if (ch == 13 || ch == 14 || ch == 153)
					spuravoid = BHND_PMU_SPURAVOID_M1;
			} else { /* 40 MHz */
				if (ch == 54)
					spuravoid = BHND_PMU_SPURAVOID_M1;
			}
		} else {
			if (!bwn_is_40mhz(mac)) { /* 20MHz */
				if ((ch >= 5 && ch <= 8) || ch == 13 || ch == 14)
					spuravoid = BHND_PMU_SPURAVOID_M1;
			} else { /* 40MHz */
				if (nphy->aband_spurwar_en &&
				    (ch == 38 || ch == 102 || ch == 118) &&
				    sc->sc_cid.chip_id == BHND_CHIPID_BCM4716)
					spuravoid = BHND_PMU_SPURAVOID_M1;
			}
		}

		bwn_nphy_pmu_spur_avoid(mac, spuravoid);

		bwn_mac_switch_freq(mac, spuravoid);

		if (mac->mac_phy.rev == 3 || mac->mac_phy.rev == 4)
			bwn_wireless_core_phy_pll_reset(mac);

		if (spuravoid != BHND_PMU_SPURAVOID_NONE)
			BWN_PHY_SET(mac, BWN_NPHY_BBCFG, BWN_NPHY_BBCFG_RSTRX);
		else
			BWN_PHY_MASK(mac, BWN_NPHY_BBCFG,
				     ~BWN_NPHY_BBCFG_RSTRX & 0xFFFF);

		bwn_nphy_reset_cca(mac);

		/* wl sets useless phy_isspuravoid here */
	}

	BWN_PHY_WRITE(mac, BWN_NPHY_NDATAT_DUP40, 0x3830);

	if (phy->rev >= 3)
		bwn_nphy_spur_workaround(mac);

	return (0);
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/N/SetChanspec */
static int bwn_nphy_set_channel(struct bwn_mac *mac,
				struct ieee80211_channel *channel,
				bwn_chan_type_t channel_type)
{
	struct bwn_phy *phy = &mac->mac_phy;

	const struct bwn_nphy_channeltab_entry_rev2 *tabent_r2 = NULL;
	const struct bwn_nphy_channeltab_entry_rev3 *tabent_r3 = NULL;
	const struct bwn_nphy_chantabent_rev7 *tabent_r7 = NULL;
	const struct bwn_nphy_chantabent_rev7_2g *tabent_r7_2g = NULL;

	int error;
	uint8_t tmp;

	if (phy->rev >= 19) {
		return -ESRCH;
		/* TODO */
	} else if (phy->rev >= 7) {
		r2057_get_chantabent_rev7(mac, bwn_get_chan_centre_freq(mac, channel),
					  &tabent_r7, &tabent_r7_2g);
		if (!tabent_r7 && !tabent_r7_2g)
			return -ESRCH;
	} else if (phy->rev >= 3) {
		tabent_r3 = bwn_nphy_get_chantabent_rev3(mac,
		    bwn_get_chan_centre_freq(mac, channel));
		if (!tabent_r3)
			return -ESRCH;
	} else {
		tabent_r2 = bwn_nphy_get_chantabent_rev2(mac,
		    channel->ic_ieee);
		if (!tabent_r2)
			return -ESRCH;
	}

	/* Channel is set later in common code, but we need to set it on our
	   own to let this function's subcalls work properly. */
#if 0
	phy->channel = channel->ic_ieee;
#endif

#if 0
	if (bwn_channel_type_is_40mhz(phy->channel_type) !=
		bwn_channel_type_is_40mhz(channel_type))
		; /* TODO: BMAC BW Set (channel_type) */
#endif

	if (channel_type == BWN_CHAN_TYPE_40_HT_U) {
		BWN_PHY_SET(mac, BWN_NPHY_RXCTL, BWN_NPHY_RXCTL_BSELU20);
		if (phy->rev >= 7)
			BWN_PHY_SET(mac, 0x310, 0x8000);
	} else if (channel_type == BWN_CHAN_TYPE_40_HT_D) {
		BWN_PHY_MASK(mac, BWN_NPHY_RXCTL, ~BWN_NPHY_RXCTL_BSELU20);
		if (phy->rev >= 7)
			BWN_PHY_MASK(mac, 0x310, (uint16_t)~0x8000);
	}

	if (phy->rev >= 19) {
		/* TODO */
		error = ENODEV;
	} else if (phy->rev >= 7) {
		const struct bwn_phy_n_sfo_cfg *phy_regs = tabent_r7 ?
			&(tabent_r7->phy_regs) : &(tabent_r7_2g->phy_regs);

		if (phy->rf_rev <= 4 || phy->rf_rev == 6) {
			tmp = (bwn_channel_band(mac, channel) == BWN_BAND_5G) ? 2 : 0;
			BWN_RF_SETMASK(mac, R2057_TIA_CONFIG_CORE0, ~2, tmp);
			BWN_RF_SETMASK(mac, R2057_TIA_CONFIG_CORE1, ~2, tmp);
		}

		bwn_radio_2057_setup(mac, tabent_r7, tabent_r7_2g);
		error = bwn_nphy_channel_setup(mac, phy_regs, channel);
	} else if (phy->rev >= 3) {
		tmp = (bwn_channel_band(mac, channel) == BWN_BAND_5G) ? 4 : 0;
		BWN_RF_SETMASK(mac, 0x08, 0xFFFB, tmp);
		bwn_radio_2056_setup(mac, tabent_r3);
		error = bwn_nphy_channel_setup(mac, &(tabent_r3->phy_regs),
		    channel);
	} else {
		tmp = (bwn_channel_band(mac, channel) == BWN_BAND_5G) ? 0x0020 : 0x0050;
		BWN_RF_SETMASK(mac, B2055_MASTER1, 0xFF8F, tmp);
		bwn_radio_2055_setup(mac, tabent_r2);
		error = bwn_nphy_channel_setup(mac, &(tabent_r2->phy_regs),
		    channel);
	}

	return (error);
}

/**************************************************
 * Basic PHY ops.
 **************************************************/

int
bwn_nphy_op_allocate(struct bwn_mac *mac)
{
	struct bwn_phy_n *nphy;

	nphy = malloc(sizeof(*nphy), M_DEVBUF, M_ZERO | M_NOWAIT);
	if (!nphy)
		return -ENOMEM;

	mac->mac_phy.phy_n = nphy;

	return 0;
}

int
bwn_nphy_op_prepare_structs(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = phy->phy_n;
	int error;

	memset(nphy, 0, sizeof(*nphy));

	nphy->hang_avoid = (phy->rev == 3 || phy->rev == 4);
	nphy->spur_avoid = (phy->rev >= 3) ?
				BWN_SPUR_AVOID_AUTO : BWN_SPUR_AVOID_DISABLE;
	nphy->gain_boost = true; /* this way we follow wl, assume it is true */
	nphy->txrx_chain = 2; /* sth different than 0 and 1 for now */
	nphy->phyrxchain = 3; /* to avoid bwn_nphy_set_rx_core_state like wl */
	nphy->perical = 2; /* avoid additional rssi cal on init (like wl) */
	/* 128 can mean disabled-by-default state of TX pwr ctl. Max value is
	 * 0x7f == 127 and we check for 128 when restoring TX pwr ctl. */
	nphy->tx_pwr_idx[0] = 128;
	nphy->tx_pwr_idx[1] = 128;

	/* Hardware TX power control and 5GHz power gain */
	nphy->txpwrctrl = false;
	nphy->pwg_gain_5ghz = false;
	if (mac->mac_phy.rev >= 3 ||
	    (sc->sc_board_info.board_vendor == PCI_VENDOR_APPLE &&
	     (bhnd_get_hwrev(sc->sc_dev) == 11 || bhnd_get_hwrev(sc->sc_dev) == 12))) {
		nphy->txpwrctrl = true;
		nphy->pwg_gain_5ghz = true;
	} else if (sc->sc_board_info.board_srom_rev >= 4) {
		if (mac->mac_phy.rev >= 2 &&
		    (sc->sc_board_info.board_flags2 & BHND_BFL2_TXPWRCTRL_EN)) {
			nphy->txpwrctrl = true;
			if ((sc->sc_board_info.board_devid == PCI_DEVID_BCM4321_D11N) ||
			    (sc->sc_board_info.board_devid == PCI_DEVID_BCM4321_D11N5G))
				nphy->pwg_gain_5ghz = true;
		} else if (sc->sc_board_info.board_flags2 & BHND_BFL2_5G_PWRGAIN) {
			nphy->pwg_gain_5ghz = true;
		}
	}

	if (mac->mac_phy.rev >= 3) {
		uint8_t extpa_gain2g, extpa_gain5g;

		error = bhnd_nvram_getvar_uint8(sc->sc_dev,
		    BHND_NVAR_EXTPAGAIN2G, &extpa_gain2g);
		if (error) {
			BWN_ERRPRINTF(mac->mac_sc, "Error reading 2GHz EPA "
			    "gain configuration from NVRAM: %d\n", error);
			return (error);
		}

		error = bhnd_nvram_getvar_uint8(sc->sc_dev,
		    BHND_NVAR_EXTPAGAIN5G, &extpa_gain5g);
		if (error) {
			BWN_ERRPRINTF(mac->mac_sc, "Error reading 5GHz EPA "
			    "gain configuration from NVRAM: %d\n", error);
			return (error);
		}

		nphy->ipa2g_on = (extpa_gain2g == 2);
		nphy->ipa5g_on = (extpa_gain5g == 2);
	}

	return (0);
}

void
bwn_nphy_op_free(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_n *nphy = phy->phy_n;

	free(nphy, M_DEVBUF);
	phy->phy_n = NULL;
}

int
bwn_nphy_op_init(struct bwn_mac *mac)
{
	return bwn_phy_initn(mac);
}

static inline void check_phyreg(struct bwn_mac *mac, uint16_t offset)
{
#ifdef	BWN_DEBUG
	if ((offset & BWN_PHYROUTE_MASK) == BWN_PHYROUTE_OFDM_GPHY) {
		/* OFDM registers are onnly available on A/G-PHYs */
		BWN_ERRPRINTF(mac->mac_sc, "Invalid OFDM PHY access at "
		       "0x%04X on N-PHY\n", offset);
	}
	if ((offset & BWN_PHYROUTE_MASK) == BWN_PHYROUTE_EXT_GPHY) {
		/* Ext-G registers are only available on G-PHYs */
		BWN_ERRPRINTF(mac->mac_sc, "Invalid EXT-G PHY access at "
		       "0x%04X on N-PHY\n", offset);
	}
#endif /* BWN_DEBUG */
}

void
bwn_nphy_op_maskset(struct bwn_mac *mac, uint16_t reg, uint16_t mask,
    uint16_t set)
{
	check_phyreg(mac, reg);
	BWN_WRITE_2_F(mac, BWN_PHYCTL, reg);
	BWN_WRITE_SETMASK2(mac, BWN_PHYDATA, mask, set);
}

#if 0
uint16_t
bwn_nphy_op_radio_read(struct bwn_mac *mac, uint16_t reg)
{
	/* Register 1 is a 32-bit register. */
	if (mac->mac_phy.rev < 7 && reg == 1) {
		BWN_ERRPRINTF(mac->mac_sc, "%s: bad reg access\n", __func__);
	}

	if (mac->mac_phy.rev >= 7)
		reg |= 0x200; /* Radio 0x2057 */
	else
		reg |= 0x100;

	BWN_WRITE_2_F(mac, BWN_RFCTL, reg);
	return BWN_READ_2(mac, BWN_RFDATALO);
}
#endif

#if 0
void
bwn_nphy_op_radio_write(struct bwn_mac *mac, uint16_t reg, uint16_t value)
{
	/* Register 1 is a 32-bit register. */
	if (mac->mac_phy.rev < 7 && reg == 1) {
		BWN_ERRPRINTF(mac->mac_sc, "%s: bad reg access\n", __func__);
	}

	BWN_WRITE_2_F(mac, BWN_RFCTL, reg);
	BWN_WRITE_2(mac, BWN_RFDATALO, value);
}
#endif

/* http://bcm-v4.sipsolutions.net/802.11/Radio/Switch%20Radio */
void
bwn_nphy_op_software_rfkill(struct bwn_mac *mac, bool active)
{
	struct bwn_phy *phy = &mac->mac_phy;

	if (BWN_READ_4(mac, BWN_MACCTL) & BWN_MACCTL_ON)
		BWN_ERRPRINTF(mac->mac_sc, "MAC not suspended\n");

	DPRINTF(mac->mac_sc, BWN_DEBUG_RESET | BWN_DEBUG_PHY,
	    "%s: called; rev=%d, rf_on=%d, active=%d\n", __func__,
	    phy->rev, mac->mac_phy.rf_on, active);

	/*
	 * XXX TODO: don't bother doing RF programming if it's
	 * already done.  But, bwn(4) currently sets rf_on in the
	 * PHY setup and leaves it on after startup, which causes
	 * the below to not init the 2056/2057 radios.
	 */
	if (active) {
		if (phy->rev >= 19) {
			/* TODO */
		} else if (phy->rev >= 7) {
//			if (!mac->mac_phy.rf_on)
				bwn_radio_2057_init(mac);
			bwn_switch_channel(mac, bwn_get_chan(mac));
		} else if (phy->rev >= 3) {
//			if (!mac->mac_phy.rf_on)
				bwn_radio_init2056(mac);
			bwn_switch_channel(mac, bwn_get_chan(mac));
		} else {
			bwn_radio_init2055(mac);
		}
	} else {
		if (phy->rev >= 19) {
			/* TODO */
		} else if (phy->rev >= 8) {
			BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_CMD,
				     ~BWN_NPHY_RFCTL_CMD_CHIP0PU);
		} else if (phy->rev >= 7) {
			/* Nothing needed */
		} else if (phy->rev >= 3) {
			BWN_PHY_MASK(mac, BWN_NPHY_RFCTL_CMD,
				     ~BWN_NPHY_RFCTL_CMD_CHIP0PU);

			BWN_RF_MASK(mac, 0x09, ~0x2);

			BWN_RF_WRITE(mac, 0x204D, 0);
			BWN_RF_WRITE(mac, 0x2053, 0);
			BWN_RF_WRITE(mac, 0x2058, 0);
			BWN_RF_WRITE(mac, 0x205E, 0);
			BWN_RF_MASK(mac, 0x2062, ~0xF0);
			BWN_RF_WRITE(mac, 0x2064, 0);

			BWN_RF_WRITE(mac, 0x304D, 0);
			BWN_RF_WRITE(mac, 0x3053, 0);
			BWN_RF_WRITE(mac, 0x3058, 0);
			BWN_RF_WRITE(mac, 0x305E, 0);
			BWN_RF_MASK(mac, 0x3062, ~0xF0);
			BWN_RF_WRITE(mac, 0x3064, 0);
		}
	}
}

/* http://bcm-v4.sipsolutions.net/802.11/PHY/Anacore */
void
bwn_nphy_op_switch_analog(struct bwn_mac *mac, bool on)
{
	struct bwn_phy *phy = &mac->mac_phy;
	uint16_t override = on ? 0x0 : 0x7FFF;
	uint16_t core = on ? 0xD : 0x00FD;

	if (phy->rev >= 19) {
		/* TODO */
		device_printf(mac->mac_sc->sc_dev, "%s: TODO\n", __func__);
	} else if (phy->rev >= 3) {
		if (on) {
			BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_C1, core);
			BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER1, override);
			BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_C2, core);
			BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER, override);
		} else {
			BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER1, override);
			BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_C1, core);
			BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER, override);
			BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_C2, core);
		}
	} else {
		BWN_PHY_WRITE(mac, BWN_NPHY_AFECTL_OVER, override);
	}
}

int
bwn_nphy_op_switch_channel(struct bwn_mac *mac, unsigned int new_channel)
{
	struct ieee80211_channel *channel = bwn_get_channel(mac);
	bwn_chan_type_t channel_type = bwn_get_chan_type(mac, NULL);

	if (bwn_current_band(mac) == BWN_BAND_2G) {
		if ((new_channel < 1) || (new_channel > 14))
			return -EINVAL;
	} else {
		if (new_channel > 200)
			return -EINVAL;
	}

	return bwn_nphy_set_channel(mac, channel, channel_type);
}

#if 0
unsigned int
bwn_nphy_op_get_default_chan(struct bwn_mac *mac)
{
	if (bwn_current_band(mac) == BWN_BAND_2G)
		return 1;
	return 36;
}
#endif
