/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Sepherosa Ziehau <sepherosa@gmail.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/dev/netif/bwi/bwiphy.c,v 1.5 2008/01/15 09:01:13 sephe Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
 
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_llc.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_amrr.h>

#include <machine/bus.h>

#include <dev/bwi/bitops.h>
#include <dev/bwi/if_bwireg.h>
#include <dev/bwi/if_bwivar.h>
#include <dev/bwi/bwimac.h>
#include <dev/bwi/bwirf.h>
#include <dev/bwi/bwiphy.h>

static void	bwi_phy_init_11a(struct bwi_mac *);
static void	bwi_phy_init_11g(struct bwi_mac *);
static void	bwi_phy_init_11b_rev2(struct bwi_mac *);
static void	bwi_phy_init_11b_rev4(struct bwi_mac *);
static void	bwi_phy_init_11b_rev5(struct bwi_mac *);
static void	bwi_phy_init_11b_rev6(struct bwi_mac *);

static void	bwi_phy_config_11g(struct bwi_mac *);
static void	bwi_phy_config_agc(struct bwi_mac *);

static void	bwi_tbl_write_2(struct bwi_mac *mac, uint16_t, uint16_t);
static void	bwi_tbl_write_4(struct bwi_mac *mac, uint16_t, uint32_t);

#define SUP_BPHY(num)	{ .rev = num, .init = bwi_phy_init_11b_rev##num }

static const struct {
	uint8_t	rev;
	void	(*init)(struct bwi_mac *);
} bwi_sup_bphy[] = {
	SUP_BPHY(2),
	SUP_BPHY(4),
	SUP_BPHY(5),
	SUP_BPHY(6)
};

#undef SUP_BPHY

#define BWI_PHYTBL_WRSSI	0x1000
#define BWI_PHYTBL_NOISE_SCALE	0x1400
#define BWI_PHYTBL_NOISE	0x1800
#define BWI_PHYTBL_ROTOR	0x2000
#define BWI_PHYTBL_DELAY	0x2400
#define BWI_PHYTBL_RSSI		0x4000
#define BWI_PHYTBL_SIGMA_SQ	0x5000
#define BWI_PHYTBL_WRSSI_REV1	0x5400
#define BWI_PHYTBL_FREQ		0x5800

static const uint16_t	bwi_phy_freq_11g_rev1[] =
	{ BWI_PHY_FREQ_11G_REV1 };
static const uint16_t	bwi_phy_noise_11g_rev1[] =
	{ BWI_PHY_NOISE_11G_REV1 };
static const uint16_t	bwi_phy_noise_11g[] =
	{ BWI_PHY_NOISE_11G };
static const uint32_t	bwi_phy_rotor_11g_rev1[] =
	{ BWI_PHY_ROTOR_11G_REV1 };
static const uint16_t	bwi_phy_noise_scale_11g_rev2[] =
	{ BWI_PHY_NOISE_SCALE_11G_REV2 };
static const uint16_t	bwi_phy_noise_scale_11g_rev7[] =
	{ BWI_PHY_NOISE_SCALE_11G_REV7 };
static const uint16_t	bwi_phy_noise_scale_11g[] =
	{ BWI_PHY_NOISE_SCALE_11G };
static const uint16_t	bwi_phy_sigma_sq_11g_rev2[] =
	{ BWI_PHY_SIGMA_SQ_11G_REV2 };
static const uint16_t	bwi_phy_sigma_sq_11g_rev7[] =
	{ BWI_PHY_SIGMA_SQ_11G_REV7 };
static const uint32_t	bwi_phy_delay_11g_rev1[] =
	{ BWI_PHY_DELAY_11G_REV1 };

void
bwi_phy_write(struct bwi_mac *mac, uint16_t ctrl, uint16_t data)
{
	struct bwi_softc *sc = mac->mac_sc;

	CSR_WRITE_2(sc, BWI_PHY_CTRL, ctrl);
	CSR_WRITE_2(sc, BWI_PHY_DATA, data);
}

uint16_t
bwi_phy_read(struct bwi_mac *mac, uint16_t ctrl)
{
	struct bwi_softc *sc = mac->mac_sc;

	CSR_WRITE_2(sc, BWI_PHY_CTRL, ctrl);
	return CSR_READ_2(sc, BWI_PHY_DATA);
}

int
bwi_phy_attach(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	uint8_t phyrev, phytype, phyver;
	uint16_t val;
	int i;

	/* Get PHY type/revision/version */
	val = CSR_READ_2(sc, BWI_PHYINFO);
	phyrev = __SHIFTOUT(val, BWI_PHYINFO_REV_MASK);
	phytype = __SHIFTOUT(val, BWI_PHYINFO_TYPE_MASK);
	phyver = __SHIFTOUT(val, BWI_PHYINFO_VER_MASK);
	device_printf(sc->sc_dev, "PHY: type %d, rev %d, ver %d\n",
		      phytype, phyrev, phyver);

	/*
	 * Verify whether the revision of the PHY type is supported
	 * Convert PHY type to ieee80211_phymode
	 */
	switch (phytype) {
	case BWI_PHYINFO_TYPE_11A:
		if (phyrev >= 4) {
			device_printf(sc->sc_dev, "unsupported 11A PHY, "
				      "rev %u\n", phyrev);
			return ENXIO;
		}
		phy->phy_init = bwi_phy_init_11a;
		phy->phy_mode = IEEE80211_MODE_11A;
		phy->phy_tbl_ctrl = BWI_PHYR_TBL_CTRL_11A;
		phy->phy_tbl_data_lo = BWI_PHYR_TBL_DATA_LO_11A;
		phy->phy_tbl_data_hi = BWI_PHYR_TBL_DATA_HI_11A;
		break;
	case BWI_PHYINFO_TYPE_11B:
		for (i = 0; i < nitems(bwi_sup_bphy); ++i) {
			if (phyrev == bwi_sup_bphy[i].rev) {
				phy->phy_init = bwi_sup_bphy[i].init;
				break;
			}
		}
		if (i == nitems(bwi_sup_bphy)) {
			device_printf(sc->sc_dev, "unsupported 11B PHY, "
				      "rev %u\n", phyrev);
			return ENXIO;
		}
		phy->phy_mode = IEEE80211_MODE_11B;
		break;
	case BWI_PHYINFO_TYPE_11G:
		if (phyrev > 8) {
			device_printf(sc->sc_dev, "unsupported 11G PHY, "
				      "rev %u\n", phyrev);
			return ENXIO;
		}
		phy->phy_init = bwi_phy_init_11g;
		phy->phy_mode = IEEE80211_MODE_11G;
		phy->phy_tbl_ctrl = BWI_PHYR_TBL_CTRL_11G;
		phy->phy_tbl_data_lo = BWI_PHYR_TBL_DATA_LO_11G;
		phy->phy_tbl_data_hi = BWI_PHYR_TBL_DATA_HI_11G;
		break;
	default:
		device_printf(sc->sc_dev, "unsupported PHY type %d\n",
			      phytype);
		return ENXIO;
	}
	phy->phy_rev = phyrev;
	phy->phy_version = phyver;
	return 0;
}

void
bwi_phy_set_bbp_atten(struct bwi_mac *mac, uint16_t bbp_atten)
{
	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t mask = __BITS(3, 0);

	if (phy->phy_version == 0) {
		CSR_FILT_SETBITS_2(mac->mac_sc, BWI_BBP_ATTEN, ~mask,
				   __SHIFTIN(bbp_atten, mask));
	} else {
		if (phy->phy_version > 1)
			mask <<= 2;
		else
			mask <<= 3;
		PHY_FILT_SETBITS(mac, BWI_PHYR_BBP_ATTEN, ~mask,
				 __SHIFTIN(bbp_atten, mask));
	}
}

int
bwi_phy_calibrate(struct bwi_mac *mac)
{
	struct bwi_phy *phy = &mac->mac_phy;

	/* Dummy read */
	CSR_READ_4(mac->mac_sc, BWI_MAC_STATUS);

	/* Don't re-init */
	if (phy->phy_flags & BWI_PHY_F_CALIBRATED)
		return 0;

	if (phy->phy_mode == IEEE80211_MODE_11G && phy->phy_rev == 1) {
		bwi_mac_reset(mac, 0);
		bwi_phy_init_11g(mac);
		bwi_mac_reset(mac, 1);
	}

	phy->phy_flags |= BWI_PHY_F_CALIBRATED;
	return 0;
}

static void
bwi_tbl_write_2(struct bwi_mac *mac, uint16_t ofs, uint16_t data)
{
	struct bwi_phy *phy = &mac->mac_phy;

	KASSERT(phy->phy_tbl_ctrl != 0 && phy->phy_tbl_data_lo != 0,
	   ("phy_tbl_ctrl %d phy_tbl_data_lo %d",
	     phy->phy_tbl_ctrl, phy->phy_tbl_data_lo));
	PHY_WRITE(mac, phy->phy_tbl_ctrl, ofs);
	PHY_WRITE(mac, phy->phy_tbl_data_lo, data);
}

static void
bwi_tbl_write_4(struct bwi_mac *mac, uint16_t ofs, uint32_t data)
{
	struct bwi_phy *phy = &mac->mac_phy;

	KASSERT(phy->phy_tbl_data_lo != 0 && phy->phy_tbl_data_hi != 0 &&
		 phy->phy_tbl_ctrl != 0,
	    ("phy_tbl_data_lo %d phy_tbl_data_hi %d phy_tbl_ctrl %d",
	      phy->phy_tbl_data_lo, phy->phy_tbl_data_hi, phy->phy_tbl_ctrl));

	PHY_WRITE(mac, phy->phy_tbl_ctrl, ofs);
	PHY_WRITE(mac, phy->phy_tbl_data_hi, data >> 16);
	PHY_WRITE(mac, phy->phy_tbl_data_lo, data & 0xffff);
}

void
bwi_nrssi_write(struct bwi_mac *mac, uint16_t ofs, int16_t data)
{
	PHY_WRITE(mac, BWI_PHYR_NRSSI_CTRL, ofs);
	PHY_WRITE(mac, BWI_PHYR_NRSSI_DATA, (uint16_t)data);
}

int16_t
bwi_nrssi_read(struct bwi_mac *mac, uint16_t ofs)
{
	PHY_WRITE(mac, BWI_PHYR_NRSSI_CTRL, ofs);
	return (int16_t)PHY_READ(mac, BWI_PHYR_NRSSI_DATA);
}

static void
bwi_phy_init_11a(struct bwi_mac *mac)
{
	/* TODO:11A */
}

static void
bwi_phy_init_11g(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	const struct bwi_tpctl *tpctl = &mac->mac_tpctl;

	if (phy->phy_rev == 1)
		bwi_phy_init_11b_rev5(mac);
	else
		bwi_phy_init_11b_rev6(mac);

	if (phy->phy_rev >= 2 || (phy->phy_flags & BWI_PHY_F_LINKED))
		bwi_phy_config_11g(mac);

	if (phy->phy_rev >= 2) {
		PHY_WRITE(mac, 0x814, 0);
		PHY_WRITE(mac, 0x815, 0);

		if (phy->phy_rev == 2) {
			PHY_WRITE(mac, 0x811, 0);
			PHY_WRITE(mac, 0x15, 0xc0);
		} else if (phy->phy_rev > 5) {
			PHY_WRITE(mac, 0x811, 0x400);
			PHY_WRITE(mac, 0x15, 0xc0);
		}
	}

	if (phy->phy_rev >= 2 || (phy->phy_flags & BWI_PHY_F_LINKED)) {
		uint16_t val;

		val = PHY_READ(mac, 0x400) & 0xff;
		if (val == 3 || val == 5) {
			PHY_WRITE(mac, 0x4c2, 0x1816);
			PHY_WRITE(mac, 0x4c3, 0x8006);
			if (val == 5) {
				PHY_FILT_SETBITS(mac, 0x4cc,
						 0xff, 0x1f00);
			}
		}
	}

	if ((phy->phy_rev <= 2 && (phy->phy_flags & BWI_PHY_F_LINKED)) ||
	    phy->phy_rev >= 2)
		PHY_WRITE(mac, 0x47e, 0x78);

	if (rf->rf_rev == 8) {
		PHY_SETBITS(mac, 0x801, 0x80);
		PHY_SETBITS(mac, 0x43e, 0x4);
	}

	if (phy->phy_rev >= 2 && (phy->phy_flags & BWI_PHY_F_LINKED))
		bwi_rf_get_gains(mac);

	if (rf->rf_rev != 8)
		bwi_rf_init(mac);

	if (tpctl->tp_ctrl2 == 0xffff) {
		bwi_rf_lo_update(mac);
	} else {
		if (rf->rf_type == BWI_RF_T_BCM2050 && rf->rf_rev == 8) {
			RF_WRITE(mac, 0x52,
				 (tpctl->tp_ctrl1 << 4) | tpctl->tp_ctrl2);
		} else {
			RF_FILT_SETBITS(mac, 0x52, 0xfff0, tpctl->tp_ctrl2);
		}

		if (phy->phy_rev >= 6) {
			PHY_FILT_SETBITS(mac, 0x36, 0xfff,
					 tpctl->tp_ctrl2 << 12);
		}

		if (sc->sc_card_flags & BWI_CARD_F_PA_GPIO9)
			PHY_WRITE(mac, 0x2e, 0x8075);
		else
			PHY_WRITE(mac, 0x2e, 0x807f);

		if (phy->phy_rev < 2)
			PHY_WRITE(mac, 0x2f, 0x101);
		else
			PHY_WRITE(mac, 0x2f, 0x202);
	}

	if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
		bwi_rf_lo_adjust(mac, tpctl);
		PHY_WRITE(mac, 0x80f, 0x8078);
	}

	if ((sc->sc_card_flags & BWI_CARD_F_SW_NRSSI) == 0) {
		bwi_rf_init_hw_nrssi_table(mac, 0xffff /* XXX */);
		bwi_rf_set_nrssi_thr(mac);
	} else if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
		if (rf->rf_nrssi[0] == BWI_INVALID_NRSSI) {
			KASSERT(rf->rf_nrssi[1] == BWI_INVALID_NRSSI,
			    ("rf_nrssi[1] %d", rf->rf_nrssi[1]));
			bwi_rf_calc_nrssi_slope(mac);
		} else {
			KASSERT(rf->rf_nrssi[1] != BWI_INVALID_NRSSI,
			    ("rf_nrssi[1] %d", rf->rf_nrssi[1]));
			bwi_rf_set_nrssi_thr(mac);
		}
	}

	if (rf->rf_rev == 8)
		PHY_WRITE(mac, 0x805, 0x3230);

	bwi_mac_init_tpctl_11bg(mac);

	if (sc->sc_bbp_id == BWI_BBPID_BCM4306 && sc->sc_bbp_pkg == 2) {
		PHY_CLRBITS(mac, 0x429, 0x4000);
		PHY_CLRBITS(mac, 0x4c3, 0x8000);
	}
}

static void
bwi_phy_init_11b_rev2(struct bwi_mac *mac)
{ 
	/* TODO:11B */
	device_printf(mac->mac_sc->sc_dev,
		  "%s is not implemented yet\n", __func__);
}

static void
bwi_phy_init_11b_rev4(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	uint16_t val, ofs;
	u_int chan;

	CSR_WRITE_2(sc, BWI_BPHY_CTRL, BWI_BPHY_CTRL_INIT);

	PHY_WRITE(mac, 0x20, 0x301c);
	PHY_WRITE(mac, 0x26, 0);
	PHY_WRITE(mac, 0x30, 0xc6);
	PHY_WRITE(mac, 0x88, 0x3e00);

	for (ofs = 0, val = 0x3c3d; ofs < 30; ++ofs, val -= 0x202)
		PHY_WRITE(mac, 0x89 + ofs, val);

	CSR_WRITE_2(sc, BWI_PHY_MAGIC_REG1, BWI_PHY_MAGIC_REG1_VAL1);

	chan = rf->rf_curchan;
	if (chan == IEEE80211_CHAN_ANY)
		chan = 6;	/* Force to channel 6 */
	bwi_rf_set_chan(mac, chan, 0);

	if (rf->rf_type != BWI_RF_T_BCM2050) {
		RF_WRITE(mac, 0x75, 0x80);
		RF_WRITE(mac, 0x79, 0x81);
	}

	RF_WRITE(mac, 0x50, 0x20);
	RF_WRITE(mac, 0x50, 0x23);

	if (rf->rf_type == BWI_RF_T_BCM2050) {
		RF_WRITE(mac, 0x50, 0x20);
		RF_WRITE(mac, 0x5a, 0x70);
		RF_WRITE(mac, 0x5b, 0x7b);
		RF_WRITE(mac, 0x5c, 0xb0);
		RF_WRITE(mac, 0x7a, 0xf);
		PHY_WRITE(mac, 0x38, 0x677);
		bwi_rf_init_bcm2050(mac);
	}

	PHY_WRITE(mac, 0x14, 0x80);
	PHY_WRITE(mac, 0x32, 0xca);
	if (rf->rf_type == BWI_RF_T_BCM2050)
		PHY_WRITE(mac, 0x32, 0xe0);
	PHY_WRITE(mac, 0x35, 0x7c2);

	bwi_rf_lo_update(mac);

	PHY_WRITE(mac, 0x26, 0xcc00);
	if (rf->rf_type == BWI_RF_T_BCM2050)
		PHY_WRITE(mac, 0x26, 0xce00);

	CSR_WRITE_2(sc, BWI_RF_CHAN_EX, 0x1100);

	PHY_WRITE(mac, 0x2a, 0x88a3);
	if (rf->rf_type == BWI_RF_T_BCM2050)
		PHY_WRITE(mac, 0x2a, 0x88c2);

	bwi_mac_set_tpctl_11bg(mac, NULL);
	if (sc->sc_card_flags & BWI_CARD_F_SW_NRSSI) {
		bwi_rf_calc_nrssi_slope(mac);
		bwi_rf_set_nrssi_thr(mac);
	}
	bwi_mac_init_tpctl_11bg(mac);
}

static void
bwi_phy_init_11b_rev5(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_phy *phy = &mac->mac_phy;
	u_int orig_chan;

	if (phy->phy_version == 1)
		RF_SETBITS(mac, 0x7a, 0x50);

	if (sc->sc_pci_subvid != PCI_VENDOR_BROADCOM &&
	    sc->sc_pci_subdid != BWI_PCI_SUBDEVICE_BU4306) {
		uint16_t ofs, val;

		val = 0x2120;
		for (ofs = 0xa8; ofs < 0xc7; ++ofs) {
			PHY_WRITE(mac, ofs, val);
			val += 0x202;
		}
	}

	PHY_FILT_SETBITS(mac, 0x35, 0xf0ff, 0x700);

	if (rf->rf_type == BWI_RF_T_BCM2050)
		PHY_WRITE(mac, 0x38, 0x667);

	if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
		if (rf->rf_type == BWI_RF_T_BCM2050) {
			RF_SETBITS(mac, 0x7a, 0x20);
			RF_SETBITS(mac, 0x51, 0x4);
		}

		CSR_WRITE_2(sc, BWI_RF_ANTDIV, 0);

		PHY_SETBITS(mac, 0x802, 0x100);
		PHY_SETBITS(mac, 0x42b, 0x2000);
		PHY_WRITE(mac, 0x1c, 0x186a);

		PHY_FILT_SETBITS(mac, 0x13, 0xff, 0x1900);
		PHY_FILT_SETBITS(mac, 0x35, 0xffc0, 0x64);
		PHY_FILT_SETBITS(mac, 0x5d, 0xff80, 0xa);
	}

	/* TODO: bad_frame_preempt? */

	if (phy->phy_version == 1) {
	    	PHY_WRITE(mac, 0x26, 0xce00);
		PHY_WRITE(mac, 0x21, 0x3763);
		PHY_WRITE(mac, 0x22, 0x1bc3);
		PHY_WRITE(mac, 0x23, 0x6f9);
		PHY_WRITE(mac, 0x24, 0x37e);
	} else {
		PHY_WRITE(mac, 0x26, 0xcc00);
	}
	PHY_WRITE(mac, 0x30, 0xc6);

	CSR_WRITE_2(sc, BWI_BPHY_CTRL, BWI_BPHY_CTRL_INIT);

	if (phy->phy_version == 1)
		PHY_WRITE(mac, 0x20, 0x3e1c);
	else
		PHY_WRITE(mac, 0x20, 0x301c);

	if (phy->phy_version == 0)
		CSR_WRITE_2(sc, BWI_PHY_MAGIC_REG1, BWI_PHY_MAGIC_REG1_VAL1);

	/* Force to channel 7 */
	orig_chan = rf->rf_curchan;
	bwi_rf_set_chan(mac, 7, 0);

	if (rf->rf_type != BWI_RF_T_BCM2050) {
		RF_WRITE(mac, 0x75, 0x80);
		RF_WRITE(mac, 0x79, 0x81);
	}

	RF_WRITE(mac, 0x50, 0x20);
	RF_WRITE(mac, 0x50, 0x23);

	if (rf->rf_type == BWI_RF_T_BCM2050) {
		RF_WRITE(mac, 0x50, 0x20);
		RF_WRITE(mac, 0x5a, 0x70);
	}

	RF_WRITE(mac, 0x5b, 0x7b);
	RF_WRITE(mac, 0x5c, 0xb0);
	RF_SETBITS(mac, 0x7a, 0x7);

	bwi_rf_set_chan(mac, orig_chan, 0);

	PHY_WRITE(mac, 0x14, 0x80);
	PHY_WRITE(mac, 0x32, 0xca);
	PHY_WRITE(mac, 0x2a, 0x88a3);

	bwi_mac_set_tpctl_11bg(mac, NULL);

	if (rf->rf_type == BWI_RF_T_BCM2050)
		RF_WRITE(mac, 0x5d, 0xd);

	CSR_FILT_SETBITS_2(sc, BWI_PHY_MAGIC_REG1, 0xffc0, 0x4);
}

static void
bwi_phy_init_11b_rev6(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t val, ofs;
	u_int orig_chan;

	PHY_WRITE(mac, 0x3e, 0x817a);
	RF_SETBITS(mac, 0x7a, 0x58);

	if (rf->rf_rev == 4 || rf->rf_rev == 5) {
		RF_WRITE(mac, 0x51, 0x37);
		RF_WRITE(mac, 0x52, 0x70);
		RF_WRITE(mac, 0x53, 0xb3);
		RF_WRITE(mac, 0x54, 0x9b);
		RF_WRITE(mac, 0x5a, 0x88);
		RF_WRITE(mac, 0x5b, 0x88);
		RF_WRITE(mac, 0x5d, 0x88);
		RF_WRITE(mac, 0x5e, 0x88);
		RF_WRITE(mac, 0x7d, 0x88);
		HFLAGS_SETBITS(mac, BWI_HFLAG_MAGIC1);
	} else if (rf->rf_rev == 8) {
		RF_WRITE(mac, 0x51, 0);
		RF_WRITE(mac, 0x52, 0x40);
		RF_WRITE(mac, 0x53, 0xb7);
		RF_WRITE(mac, 0x54, 0x98);
		RF_WRITE(mac, 0x5a, 0x88);
		RF_WRITE(mac, 0x5b, 0x6b);
		RF_WRITE(mac, 0x5c, 0xf);
		if (sc->sc_card_flags & BWI_CARD_F_ALT_IQ) {
			RF_WRITE(mac, 0x5d, 0xfa);
			RF_WRITE(mac, 0x5e, 0xd8);
		} else {
			RF_WRITE(mac, 0x5d, 0xf5);
			RF_WRITE(mac, 0x5e, 0xb8);
		}
		RF_WRITE(mac, 0x73, 0x3);
		RF_WRITE(mac, 0x7d, 0xa8);
		RF_WRITE(mac, 0x7c, 0x1);
		RF_WRITE(mac, 0x7e, 0x8);
	}

	val = 0x1e1f;
	for (ofs = 0x88; ofs < 0x98; ++ofs) {
		PHY_WRITE(mac, ofs, val);
		val -= 0x202;
	}

	val = 0x3e3f;
	for (ofs = 0x98; ofs < 0xa8; ++ofs) {
		PHY_WRITE(mac, ofs, val);
		val -= 0x202;
	}

	val = 0x2120;
	for (ofs = 0xa8; ofs < 0xc8; ++ofs) {
		PHY_WRITE(mac, ofs, (val & 0x3f3f));
		val += 0x202;

		/* XXX: delay 10 us to avoid PCI parity errors with BCM4318 */
		DELAY(10);
	}

	if (phy->phy_mode == IEEE80211_MODE_11G) {
		RF_SETBITS(mac, 0x7a, 0x20);
		RF_SETBITS(mac, 0x51, 0x4);
		PHY_SETBITS(mac, 0x802, 0x100);
		PHY_SETBITS(mac, 0x42b, 0x2000);
		PHY_WRITE(mac, 0x5b, 0);
		PHY_WRITE(mac, 0x5c, 0);
	}

	/* Force to channel 7 */
	orig_chan = rf->rf_curchan;
	if (orig_chan >= 8)
		bwi_rf_set_chan(mac, 1, 0);
	else
		bwi_rf_set_chan(mac, 13, 0);

	RF_WRITE(mac, 0x50, 0x20);
	RF_WRITE(mac, 0x50, 0x23);

	DELAY(40);

	if (rf->rf_rev < 6 || rf->rf_rev == 8) {
		RF_SETBITS(mac, 0x7c, 0x2);
		RF_WRITE(mac, 0x50, 0x20);
	}
	if (rf->rf_rev <= 2) {
		RF_WRITE(mac, 0x7c, 0x20);
		RF_WRITE(mac, 0x5a, 0x70);
		RF_WRITE(mac, 0x5b, 0x7b);
		RF_WRITE(mac, 0x5c, 0xb0);
	}

	RF_FILT_SETBITS(mac, 0x7a, 0xf8, 0x7);

	bwi_rf_set_chan(mac, orig_chan, 0);

	PHY_WRITE(mac, 0x14, 0x200);
	if (rf->rf_rev >= 6)
		PHY_WRITE(mac, 0x2a, 0x88c2);
	else
		PHY_WRITE(mac, 0x2a, 0x8ac0);
	PHY_WRITE(mac, 0x38, 0x668);

	bwi_mac_set_tpctl_11bg(mac, NULL);

	if (rf->rf_rev <= 5) {
		PHY_FILT_SETBITS(mac, 0x5d, 0xff80, 0x3);
		if (rf->rf_rev <= 2)
			RF_WRITE(mac, 0x5d, 0xd);
	}

	if (phy->phy_version == 4) {
		CSR_WRITE_2(sc, BWI_PHY_MAGIC_REG1, BWI_PHY_MAGIC_REG1_VAL2);
		PHY_CLRBITS(mac, 0x61, 0xf000);
	} else {
		PHY_FILT_SETBITS(mac, 0x2, 0xffc0, 0x4);
	}

	if (phy->phy_mode == IEEE80211_MODE_11B) {
		CSR_WRITE_2(sc, BWI_BBP_ATTEN, BWI_BBP_ATTEN_MAGIC2);
		PHY_WRITE(mac, 0x16, 0x410);
		PHY_WRITE(mac, 0x17, 0x820);
		PHY_WRITE(mac, 0x62, 0x7);

		bwi_rf_init_bcm2050(mac);
		bwi_rf_lo_update(mac);
		if (sc->sc_card_flags & BWI_CARD_F_SW_NRSSI) {
			bwi_rf_calc_nrssi_slope(mac);
			bwi_rf_set_nrssi_thr(mac);
		}
		bwi_mac_init_tpctl_11bg(mac);
	} else {
		CSR_WRITE_2(sc, BWI_BBP_ATTEN, 0);
	}
}

static void
bwi_phy_config_11g(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	const uint16_t *tbl;
	uint16_t wrd_ofs1, wrd_ofs2;
	int i, n;

	if (phy->phy_rev == 1) {
		PHY_WRITE(mac, 0x406, 0x4f19);
		PHY_FILT_SETBITS(mac, 0x429, 0xfc3f, 0x340);
		PHY_WRITE(mac, 0x42c, 0x5a);
		PHY_WRITE(mac, 0x427, 0x1a);

		/* Fill frequency table */
		for (i = 0; i < nitems(bwi_phy_freq_11g_rev1); ++i) {
			bwi_tbl_write_2(mac, BWI_PHYTBL_FREQ + i,
					bwi_phy_freq_11g_rev1[i]);
		}

		/* Fill noise table */
		for (i = 0; i < nitems(bwi_phy_noise_11g_rev1); ++i) {
			bwi_tbl_write_2(mac, BWI_PHYTBL_NOISE + i,
					bwi_phy_noise_11g_rev1[i]);
		}

		/* Fill rotor table */
		for (i = 0; i < nitems(bwi_phy_rotor_11g_rev1); ++i) {
			/* NB: data length is 4 bytes */
			bwi_tbl_write_4(mac, BWI_PHYTBL_ROTOR + i,
					bwi_phy_rotor_11g_rev1[i]);
		}
	} else {
		bwi_nrssi_write(mac, 0xba98, (int16_t)0x7654); /* XXX */

		if (phy->phy_rev == 2) {
			PHY_WRITE(mac, 0x4c0, 0x1861);
			PHY_WRITE(mac, 0x4c1, 0x271);
		} else if (phy->phy_rev > 2) {
			PHY_WRITE(mac, 0x4c0, 0x98);
			PHY_WRITE(mac, 0x4c1, 0x70);
			PHY_WRITE(mac, 0x4c9, 0x80);
		}
		PHY_SETBITS(mac, 0x42b, 0x800);

		/* Fill RSSI table */
		for (i = 0; i < 64; ++i)
			bwi_tbl_write_2(mac, BWI_PHYTBL_RSSI + i, i);

		/* Fill noise table */
		for (i = 0; i < nitems(bwi_phy_noise_11g); ++i) {
			bwi_tbl_write_2(mac, BWI_PHYTBL_NOISE + i,
					bwi_phy_noise_11g[i]);
		}
	}

	/*
	 * Fill noise scale table
	 */
	if (phy->phy_rev <= 2) {
		tbl = bwi_phy_noise_scale_11g_rev2;
		n = nitems(bwi_phy_noise_scale_11g_rev2);
	} else if (phy->phy_rev >= 7 && (PHY_READ(mac, 0x449) & 0x200)) {
		tbl = bwi_phy_noise_scale_11g_rev7;
		n = nitems(bwi_phy_noise_scale_11g_rev7);
	} else {
		tbl = bwi_phy_noise_scale_11g;
		n = nitems(bwi_phy_noise_scale_11g);
	}
	for (i = 0; i < n; ++i)
		bwi_tbl_write_2(mac, BWI_PHYTBL_NOISE_SCALE + i, tbl[i]);

	/*
	 * Fill sigma square table
	 */
	if (phy->phy_rev == 2) {
		tbl = bwi_phy_sigma_sq_11g_rev2;
		n = nitems(bwi_phy_sigma_sq_11g_rev2);
	} else if (phy->phy_rev > 2 && phy->phy_rev <= 8) {
		tbl = bwi_phy_sigma_sq_11g_rev7;
		n = nitems(bwi_phy_sigma_sq_11g_rev7);
	} else {
		tbl = NULL;
		n = 0;
	}
	for (i = 0; i < n; ++i)
		bwi_tbl_write_2(mac, BWI_PHYTBL_SIGMA_SQ + i, tbl[i]);

	if (phy->phy_rev == 1) {
		/* Fill delay table */
		for (i = 0; i < nitems(bwi_phy_delay_11g_rev1); ++i) {
			bwi_tbl_write_4(mac, BWI_PHYTBL_DELAY + i,
					bwi_phy_delay_11g_rev1[i]);
		}

		/* Fill WRSSI (Wide-Band RSSI) table */
		for (i = 4; i < 20; ++i)
			bwi_tbl_write_2(mac, BWI_PHYTBL_WRSSI_REV1 + i, 0x20);

		bwi_phy_config_agc(mac);

		wrd_ofs1 = 0x5001;
		wrd_ofs2 = 0x5002;
	} else {
		/* Fill WRSSI (Wide-Band RSSI) table */
		for (i = 0; i < 0x20; ++i)
			bwi_tbl_write_2(mac, BWI_PHYTBL_WRSSI + i, 0x820);

		bwi_phy_config_agc(mac);

		PHY_READ(mac, 0x400);	/* Dummy read */
		PHY_WRITE(mac, 0x403, 0x1000);
		bwi_tbl_write_2(mac, 0x3c02, 0xf);
		bwi_tbl_write_2(mac, 0x3c03, 0x14);

		wrd_ofs1 = 0x401;
		wrd_ofs2 = 0x402;
	}

	if (!(BWI_IS_BRCM_BU4306(sc) && sc->sc_pci_revid == 0x17)) {
		bwi_tbl_write_2(mac, wrd_ofs1, 0x2);
		bwi_tbl_write_2(mac, wrd_ofs2, 0x1);
	}

	/* phy->phy_flags & BWI_PHY_F_LINKED ? */
	if (sc->sc_card_flags & BWI_CARD_F_PA_GPIO9)
		PHY_WRITE(mac, 0x46e, 0x3cf);
}

/*
 * Configure Automatic Gain Controller
 */
static void
bwi_phy_config_agc(struct bwi_mac *mac)
{
	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t ofs;

	ofs = phy->phy_rev == 1 ? 0x4c00 : 0;

	bwi_tbl_write_2(mac, ofs, 0xfe);
	bwi_tbl_write_2(mac, ofs + 1, 0xd);
	bwi_tbl_write_2(mac, ofs + 2, 0x13);
	bwi_tbl_write_2(mac, ofs + 3, 0x19);

	if (phy->phy_rev == 1) {
		bwi_tbl_write_2(mac, 0x1800, 0x2710);
		bwi_tbl_write_2(mac, 0x1801, 0x9b83);
		bwi_tbl_write_2(mac, 0x1802, 0x9b83);
		bwi_tbl_write_2(mac, 0x1803, 0xf8d);
		PHY_WRITE(mac, 0x455, 0x4);
	}

	PHY_FILT_SETBITS(mac, 0x4a5, 0xff, 0x5700);
	PHY_FILT_SETBITS(mac, 0x41a, 0xff80, 0xf);
	PHY_FILT_SETBITS(mac, 0x41a, 0xc07f, 0x2b80);
	PHY_FILT_SETBITS(mac, 0x48c, 0xf0ff, 0x300);

	RF_SETBITS(mac, 0x7a, 0x8);

	PHY_FILT_SETBITS(mac, 0x4a0, 0xfff0, 0x8);
	PHY_FILT_SETBITS(mac, 0x4a1, 0xf0ff, 0x600);
	PHY_FILT_SETBITS(mac, 0x4a2, 0xf0ff, 0x700);
	PHY_FILT_SETBITS(mac, 0x4a0, 0xf0ff, 0x100);

	if (phy->phy_rev == 1)
		PHY_FILT_SETBITS(mac, 0x4a2, 0xfff0, 0x7);

	PHY_FILT_SETBITS(mac, 0x488, 0xff00, 0x1c);
	PHY_FILT_SETBITS(mac, 0x488, 0xc0ff, 0x200);
	PHY_FILT_SETBITS(mac, 0x496, 0xff00, 0x1c);
	PHY_FILT_SETBITS(mac, 0x489, 0xff00, 0x20);
	PHY_FILT_SETBITS(mac, 0x489, 0xc0ff, 0x200);
	PHY_FILT_SETBITS(mac, 0x482, 0xff00, 0x2e);
	PHY_FILT_SETBITS(mac, 0x496, 0xff, 0x1a00);
	PHY_FILT_SETBITS(mac, 0x481, 0xff00, 0x28);
	PHY_FILT_SETBITS(mac, 0x481, 0xff, 0x2c00);

	if (phy->phy_rev == 1) {
		PHY_WRITE(mac, 0x430, 0x92b);
		PHY_FILT_SETBITS(mac, 0x41b, 0xffe1, 0x2);
	} else {
		PHY_CLRBITS(mac, 0x41b, 0x1e);
		PHY_WRITE(mac, 0x41f, 0x287a);
		PHY_FILT_SETBITS(mac, 0x420, 0xfff0, 0x4);

		if (phy->phy_rev >= 6) {
			PHY_WRITE(mac, 0x422, 0x287a);
			PHY_FILT_SETBITS(mac, 0x420, 0xfff, 0x3000);
		}
	}

	PHY_FILT_SETBITS(mac, 0x4a8, 0x8080, 0x7874);
	PHY_WRITE(mac, 0x48e, 0x1c00);

	if (phy->phy_rev == 1) {
		PHY_FILT_SETBITS(mac, 0x4ab, 0xf0ff, 0x600);
		PHY_WRITE(mac, 0x48b, 0x5e);
		PHY_FILT_SETBITS(mac, 0x48c, 0xff00, 0x1e);
		PHY_WRITE(mac, 0x48d, 0x2);
	}

	bwi_tbl_write_2(mac, ofs + 0x800, 0);
	bwi_tbl_write_2(mac, ofs + 0x801, 7);
	bwi_tbl_write_2(mac, ofs + 0x802, 16);
	bwi_tbl_write_2(mac, ofs + 0x803, 28);

	if (phy->phy_rev >= 6) {
		PHY_CLRBITS(mac, 0x426, 0x3);
		PHY_CLRBITS(mac, 0x426, 0x1000);
	}
}

void
bwi_set_gains(struct bwi_mac *mac, const struct bwi_gains *gains)
{
	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t tbl_gain_ofs1, tbl_gain_ofs2, tbl_gain;
	int i;

	if (phy->phy_rev <= 1) {
		tbl_gain_ofs1 = 0x5000;
		tbl_gain_ofs2 = tbl_gain_ofs1 + 16;
	} else {
		tbl_gain_ofs1 = 0x400;
		tbl_gain_ofs2 = tbl_gain_ofs1 + 8;
	}

	for (i = 0; i < 4; ++i) {
		if (gains != NULL) {
			tbl_gain = gains->tbl_gain1;
		} else {
			/* Bit swap */
			tbl_gain = (i & 0x1) << 1;
			tbl_gain |= (i & 0x2) >> 1;
		}
		bwi_tbl_write_2(mac, tbl_gain_ofs1 + i, tbl_gain);
	}

	for (i = 0; i < 16; ++i) {
		if (gains != NULL)
			tbl_gain = gains->tbl_gain2;
		else
			tbl_gain = i;
		bwi_tbl_write_2(mac, tbl_gain_ofs2 + i, tbl_gain);
	}

	if (gains == NULL || (gains != NULL && gains->phy_gain != -1)) {
		uint16_t phy_gain1, phy_gain2;

		if (gains != NULL) {
			phy_gain1 =
			((uint16_t)gains->phy_gain << 14) |
			((uint16_t)gains->phy_gain << 6);
			phy_gain2 = phy_gain1;
		} else {
			phy_gain1 = 0x4040;
			phy_gain2 = 0x4000;
		}
		PHY_FILT_SETBITS(mac, 0x4a0, 0xbfbf, phy_gain1);
		PHY_FILT_SETBITS(mac, 0x4a1, 0xbfbf, phy_gain1);
		PHY_FILT_SETBITS(mac, 0x4a2, 0xbfbf, phy_gain2);
	}
	bwi_mac_dummy_xmit(mac);
}

void
bwi_phy_clear_state(struct bwi_phy *phy)
{
	phy->phy_flags &= ~BWI_CLEAR_PHY_FLAGS;
}
