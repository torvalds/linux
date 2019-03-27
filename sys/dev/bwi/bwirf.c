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
 * $DragonFly: src/sys/dev/netif/bwi/bwirf.c,v 1.9 2008/08/21 12:19:33 swildner Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_bwi.h"
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

#define RF_LO_WRITE(mac, lo)	bwi_rf_lo_write((mac), (lo))

#define BWI_RF_2GHZ_CHAN(chan)			\
	(ieee80211_ieee2mhz((chan), IEEE80211_CHAN_2GHZ) - 2400)

#define BWI_DEFAULT_IDLE_TSSI	52

struct rf_saveregs {
	uint16_t	phy_01;
	uint16_t	phy_03;
	uint16_t	phy_0a;
	uint16_t	phy_15;
	uint16_t	phy_2a;
	uint16_t	phy_30;
	uint16_t	phy_35;
	uint16_t	phy_60;
	uint16_t	phy_429;
	uint16_t	phy_802;
	uint16_t	phy_811;
	uint16_t	phy_812;
	uint16_t	phy_814;
	uint16_t	phy_815;

	uint16_t	rf_43;
	uint16_t	rf_52;
	uint16_t	rf_7a;
};

#define SAVE_RF_REG(mac, regs, n)	(regs)->rf_##n = RF_READ((mac), 0x##n)
#define RESTORE_RF_REG(mac, regs, n)	RF_WRITE((mac), 0x##n, (regs)->rf_##n)

#define SAVE_PHY_REG(mac, regs, n)	(regs)->phy_##n = PHY_READ((mac), 0x##n)
#define RESTORE_PHY_REG(mac, regs, n)	PHY_WRITE((mac), 0x##n, (regs)->phy_##n)

static int	bwi_rf_calc_txpower(int8_t *, uint8_t, const int16_t[]);
static void	bwi_rf_work_around(struct bwi_mac *, u_int);
static int	bwi_rf_gain_max_reached(struct bwi_mac *, int);
static uint16_t	bwi_rf_calibval(struct bwi_mac *);
static uint16_t	bwi_rf_get_tp_ctrl2(struct bwi_mac *);

static void	bwi_rf_lo_update_11b(struct bwi_mac *);
static uint16_t	bwi_rf_lo_measure_11b(struct bwi_mac *);

static void	bwi_rf_lo_update_11g(struct bwi_mac *);
static uint32_t	bwi_rf_lo_devi_measure(struct bwi_mac *, uint16_t);
static void	bwi_rf_lo_measure_11g(struct bwi_mac *,
			const struct bwi_rf_lo *, struct bwi_rf_lo *, uint8_t);
static uint8_t	_bwi_rf_lo_update_11g(struct bwi_mac *, uint16_t);
static void	bwi_rf_lo_write(struct bwi_mac *, const struct bwi_rf_lo *);

static void	bwi_rf_set_nrssi_ofs_11g(struct bwi_mac *);
static void	bwi_rf_calc_nrssi_slope_11b(struct bwi_mac *);
static void	bwi_rf_calc_nrssi_slope_11g(struct bwi_mac *);
static void	bwi_rf_set_nrssi_thr_11b(struct bwi_mac *);
static void	bwi_rf_set_nrssi_thr_11g(struct bwi_mac *);

static void	bwi_rf_init_sw_nrssi_table(struct bwi_mac *);

static int	bwi_rf_calc_rssi_bcm2050(struct bwi_mac *,
			const struct bwi_rxbuf_hdr *);
static int	bwi_rf_calc_rssi_bcm2053(struct bwi_mac *,
			const struct bwi_rxbuf_hdr *);
static int	bwi_rf_calc_rssi_bcm2060(struct bwi_mac *,
			const struct bwi_rxbuf_hdr *);
static int	bwi_rf_calc_noise_bcm2050(struct bwi_mac *);
static int	bwi_rf_calc_noise_bcm2053(struct bwi_mac *);
static int	bwi_rf_calc_noise_bcm2060(struct bwi_mac *);

static void	bwi_rf_on_11a(struct bwi_mac *);
static void	bwi_rf_on_11bg(struct bwi_mac *);

static void	bwi_rf_off_11a(struct bwi_mac *);
static void	bwi_rf_off_11bg(struct bwi_mac *);
static void	bwi_rf_off_11g_rev5(struct bwi_mac *);

static const int8_t	bwi_txpower_map_11b[BWI_TSSI_MAX] =
	{ BWI_TXPOWER_MAP_11B };
static const int8_t	bwi_txpower_map_11g[BWI_TSSI_MAX] =
	{ BWI_TXPOWER_MAP_11G };

static __inline int16_t
bwi_nrssi_11g(struct bwi_mac *mac)
{
	int16_t val;

#define NRSSI_11G_MASK		__BITS(13, 8)

	val = (int16_t)__SHIFTOUT(PHY_READ(mac, 0x47f), NRSSI_11G_MASK);
	if (val >= 32)
		val -= 64;
	return val;

#undef NRSSI_11G_MASK
}

static __inline struct bwi_rf_lo *
bwi_get_rf_lo(struct bwi_mac *mac, uint16_t rf_atten, uint16_t bbp_atten)
{
	int n;

	n = rf_atten + (14 * (bbp_atten / 2));
	KASSERT(n < BWI_RFLO_MAX, ("n %d", n));

	return &mac->mac_rf.rf_lo[n];
}

static __inline int
bwi_rf_lo_isused(struct bwi_mac *mac, const struct bwi_rf_lo *lo)
{
	struct bwi_rf *rf = &mac->mac_rf;
	int idx;

	idx = lo - rf->rf_lo;
	KASSERT(idx >= 0 && idx < BWI_RFLO_MAX, ("idx %d", idx));

	return isset(rf->rf_lo_used, idx);
}

void
bwi_rf_write(struct bwi_mac *mac, uint16_t ctrl, uint16_t data)
{
	struct bwi_softc *sc = mac->mac_sc;

	CSR_WRITE_2(sc, BWI_RF_CTRL, ctrl);
	CSR_WRITE_2(sc, BWI_RF_DATA_LO, data);
}

uint16_t
bwi_rf_read(struct bwi_mac *mac, uint16_t ctrl)
{
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_softc *sc = mac->mac_sc;

	ctrl |= rf->rf_ctrl_rd;
	if (rf->rf_ctrl_adj) {
		/* XXX */
		if (ctrl < 0x70)
			ctrl += 0x80;
		else if (ctrl < 0x80)
			ctrl += 0x70;
	}

	CSR_WRITE_2(sc, BWI_RF_CTRL, ctrl);
	return CSR_READ_2(sc, BWI_RF_DATA_LO);
}

int
bwi_rf_attach(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	uint16_t type, manu;
	uint8_t rev;

	/*
	 * Get RF manufacture/type/revision
	 */
	if (sc->sc_bbp_id == BWI_BBPID_BCM4317) {
		/*
		 * Fake a BCM2050 RF
		 */
		manu = BWI_RF_MANUFACT_BCM;
		type = BWI_RF_T_BCM2050;
		if (sc->sc_bbp_rev == 0)
			rev = 3;
		else if (sc->sc_bbp_rev == 1)
			rev = 4;
		else
			rev = 5;
	} else {
		uint32_t val;

		CSR_WRITE_2(sc, BWI_RF_CTRL, BWI_RF_CTRL_RFINFO);
		val = CSR_READ_2(sc, BWI_RF_DATA_HI);
		val <<= 16;

		CSR_WRITE_2(sc, BWI_RF_CTRL, BWI_RF_CTRL_RFINFO);
		val |= CSR_READ_2(sc, BWI_RF_DATA_LO);

		manu = __SHIFTOUT(val, BWI_RFINFO_MANUFACT_MASK);
		type = __SHIFTOUT(val, BWI_RFINFO_TYPE_MASK);
		rev = __SHIFTOUT(val, BWI_RFINFO_REV_MASK);
	}
	device_printf(sc->sc_dev, "RF: manu 0x%03x, type 0x%04x, rev %u\n",
		      manu, type, rev);

	/*
	 * Verify whether the RF is supported
	 */
	rf->rf_ctrl_rd = 0;
	rf->rf_ctrl_adj = 0;
	switch (phy->phy_mode) {
	case IEEE80211_MODE_11A:
		if (manu != BWI_RF_MANUFACT_BCM ||
		    type != BWI_RF_T_BCM2060 ||
		    rev != 1) {
			device_printf(sc->sc_dev, "only BCM2060 rev 1 RF "
				      "is supported for 11A PHY\n");
			return ENXIO;
		}
		rf->rf_ctrl_rd = BWI_RF_CTRL_RD_11A;
		rf->rf_on = bwi_rf_on_11a;
		rf->rf_off = bwi_rf_off_11a;
		rf->rf_calc_rssi = bwi_rf_calc_rssi_bcm2060;
		rf->rf_calc_noise = bwi_rf_calc_noise_bcm2060;
		break;
	case IEEE80211_MODE_11B:
		if (type == BWI_RF_T_BCM2050) {
			rf->rf_ctrl_rd = BWI_RF_CTRL_RD_11BG;
			rf->rf_calc_rssi = bwi_rf_calc_rssi_bcm2050;
			rf->rf_calc_noise = bwi_rf_calc_noise_bcm2050;
		} else if (type == BWI_RF_T_BCM2053) {
			rf->rf_ctrl_adj = 1;
			rf->rf_calc_rssi = bwi_rf_calc_rssi_bcm2053;
			rf->rf_calc_noise = bwi_rf_calc_noise_bcm2053;
		} else {
			device_printf(sc->sc_dev, "only BCM2050/BCM2053 RF "
				      "is supported for 11B PHY\n");
			return ENXIO;
		}
		rf->rf_on = bwi_rf_on_11bg;
		rf->rf_off = bwi_rf_off_11bg;
		rf->rf_calc_nrssi_slope = bwi_rf_calc_nrssi_slope_11b;
		rf->rf_set_nrssi_thr = bwi_rf_set_nrssi_thr_11b;
		if (phy->phy_rev == 6)
			rf->rf_lo_update = bwi_rf_lo_update_11g;
		else
			rf->rf_lo_update = bwi_rf_lo_update_11b;
		break;
	case IEEE80211_MODE_11G:
		if (type != BWI_RF_T_BCM2050) {
			device_printf(sc->sc_dev, "only BCM2050 RF "
				      "is supported for 11G PHY\n");
			return ENXIO;
		}
		rf->rf_ctrl_rd = BWI_RF_CTRL_RD_11BG;
		rf->rf_on = bwi_rf_on_11bg;
		if (mac->mac_rev >= 5)
			rf->rf_off = bwi_rf_off_11g_rev5;
		else
			rf->rf_off = bwi_rf_off_11bg;
		rf->rf_calc_nrssi_slope = bwi_rf_calc_nrssi_slope_11g;
		rf->rf_set_nrssi_thr = bwi_rf_set_nrssi_thr_11g;
		rf->rf_calc_rssi = bwi_rf_calc_rssi_bcm2050;
		rf->rf_calc_noise = bwi_rf_calc_noise_bcm2050;
		rf->rf_lo_update = bwi_rf_lo_update_11g;
		break;
	default:
		device_printf(sc->sc_dev, "unsupported PHY mode\n");
		return ENXIO;
	}

	rf->rf_type = type;
	rf->rf_rev = rev;
	rf->rf_manu = manu;
	rf->rf_curchan = IEEE80211_CHAN_ANY;
	rf->rf_ant_mode = BWI_ANT_MODE_AUTO;
	return 0;
}

void
bwi_rf_set_chan(struct bwi_mac *mac, u_int chan, int work_around)
{
	struct bwi_softc *sc = mac->mac_sc;

	if (chan == IEEE80211_CHAN_ANY)
		return;

	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_CHAN, chan);

	/* TODO: 11A */

	if (work_around)
		bwi_rf_work_around(mac, chan);

	CSR_WRITE_2(sc, BWI_RF_CHAN, BWI_RF_2GHZ_CHAN(chan));

	if (chan == 14) {
		if (sc->sc_locale == BWI_SPROM_LOCALE_JAPAN)
			HFLAGS_CLRBITS(mac, BWI_HFLAG_NOT_JAPAN);
		else
			HFLAGS_SETBITS(mac, BWI_HFLAG_NOT_JAPAN);
		CSR_SETBITS_2(sc, BWI_RF_CHAN_EX, (1 << 11)); /* XXX */
	} else {
		CSR_CLRBITS_2(sc, BWI_RF_CHAN_EX, 0x840); /* XXX */
	}
	DELAY(8000);	/* DELAY(2000); */

	mac->mac_rf.rf_curchan = chan;
}

void
bwi_rf_get_gains(struct bwi_mac *mac)
{
#define SAVE_PHY_MAX	15
#define SAVE_RF_MAX	3

	static const uint16_t save_rf_regs[SAVE_RF_MAX] =
	{ 0x52, 0x43, 0x7a };
	static const uint16_t save_phy_regs[SAVE_PHY_MAX] = {
		0x0429, 0x0001, 0x0811, 0x0812,
		0x0814, 0x0815, 0x005a, 0x0059,
		0x0058, 0x000a, 0x0003, 0x080f,
		0x0810, 0x002b, 0x0015
	};

	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	uint16_t save_phy[SAVE_PHY_MAX];
	uint16_t save_rf[SAVE_RF_MAX];
	uint16_t trsw;
	int i, j, loop1_max, loop1, loop2;

	/*
	 * Save PHY/RF registers for later restoration
	 */
	for (i = 0; i < SAVE_PHY_MAX; ++i)
		save_phy[i] = PHY_READ(mac, save_phy_regs[i]);
	PHY_READ(mac, 0x2d); /* dummy read */

	for (i = 0; i < SAVE_RF_MAX; ++i)
		save_rf[i] = RF_READ(mac, save_rf_regs[i]);

	PHY_CLRBITS(mac, 0x429, 0xc000);
	PHY_SETBITS(mac, 0x1, 0x8000);

	PHY_SETBITS(mac, 0x811, 0x2);
	PHY_CLRBITS(mac, 0x812, 0x2);
	PHY_SETBITS(mac, 0x811, 0x1);
	PHY_CLRBITS(mac, 0x812, 0x1);

	PHY_SETBITS(mac, 0x814, 0x1);
	PHY_CLRBITS(mac, 0x815, 0x1);
	PHY_SETBITS(mac, 0x814, 0x2);
	PHY_CLRBITS(mac, 0x815, 0x2);

	PHY_SETBITS(mac, 0x811, 0xc);
	PHY_SETBITS(mac, 0x812, 0xc);
	PHY_SETBITS(mac, 0x811, 0x30);
	PHY_FILT_SETBITS(mac, 0x812, 0xffcf, 0x10);

	PHY_WRITE(mac, 0x5a, 0x780);
	PHY_WRITE(mac, 0x59, 0xc810);
	PHY_WRITE(mac, 0x58, 0xd);
	PHY_SETBITS(mac, 0xa, 0x2000);

	PHY_SETBITS(mac, 0x814, 0x4);
	PHY_CLRBITS(mac, 0x815, 0x4);

	PHY_FILT_SETBITS(mac, 0x3, 0xff9f, 0x40);

	if (rf->rf_rev == 8) {
		loop1_max = 15;
		RF_WRITE(mac, 0x43, loop1_max);
	} else {
		loop1_max = 9;
	    	RF_WRITE(mac, 0x52, 0x0);
		RF_FILT_SETBITS(mac, 0x43, 0xfff0, loop1_max);
	}

	bwi_phy_set_bbp_atten(mac, 11);

	if (phy->phy_rev >= 3)
		PHY_WRITE(mac, 0x80f, 0xc020);
	else
		PHY_WRITE(mac, 0x80f, 0x8020);
	PHY_WRITE(mac, 0x810, 0);

	PHY_FILT_SETBITS(mac, 0x2b, 0xffc0, 0x1);
	PHY_FILT_SETBITS(mac, 0x2b, 0xc0ff, 0x800);
	PHY_SETBITS(mac, 0x811, 0x100);
	PHY_CLRBITS(mac, 0x812, 0x3000);

	if ((sc->sc_card_flags & BWI_CARD_F_EXT_LNA) &&
	    phy->phy_rev >= 7) {
		PHY_SETBITS(mac, 0x811, 0x800);
		PHY_SETBITS(mac, 0x812, 0x8000);
	}
	RF_CLRBITS(mac, 0x7a, 0xff08);

	/*
	 * Find out 'loop1/loop2', which will be used to calculate
	 * max loopback gain later
	 */
	j = 0;
	for (i = 0; i < loop1_max; ++i) {
		for (j = 0; j < 16; ++j) {
			RF_WRITE(mac, 0x43, i);

			if (bwi_rf_gain_max_reached(mac, j))
				goto loop1_exit;
		}
	}
loop1_exit:
	loop1 = i;
	loop2 = j;

	/*
	 * Find out 'trsw', which will be used to calculate
	 * TRSW(TX/RX switch) RX gain later
	 */
	if (loop2 >= 8) {
		PHY_SETBITS(mac, 0x812, 0x30);
		trsw = 0x1b;
		for (i = loop2 - 8; i < 16; ++i) {
			trsw -= 3;
			if (bwi_rf_gain_max_reached(mac, i))
				break;
		}
	} else {
		trsw = 0x18;
	}

	/*
	 * Restore saved PHY/RF registers
	 */
	/* First 4 saved PHY registers need special processing */
	for (i = 4; i < SAVE_PHY_MAX; ++i)
		PHY_WRITE(mac, save_phy_regs[i], save_phy[i]);

	bwi_phy_set_bbp_atten(mac, mac->mac_tpctl.bbp_atten);

	for (i = 0; i < SAVE_RF_MAX; ++i)
		RF_WRITE(mac, save_rf_regs[i], save_rf[i]);

	PHY_WRITE(mac, save_phy_regs[2], save_phy[2] | 0x3);
	DELAY(10);
	PHY_WRITE(mac, save_phy_regs[2], save_phy[2]);
	PHY_WRITE(mac, save_phy_regs[3], save_phy[3]);
	PHY_WRITE(mac, save_phy_regs[0], save_phy[0]);
	PHY_WRITE(mac, save_phy_regs[1], save_phy[1]);

	/*
	 * Calculate gains
	 */
	rf->rf_lo_gain = (loop2 * 6) - (loop1 * 4) - 11;
	rf->rf_rx_gain = trsw * 2;
	DPRINTF(mac->mac_sc, BWI_DBG_RF | BWI_DBG_INIT,
		"lo gain: %u, rx gain: %u\n",
		rf->rf_lo_gain, rf->rf_rx_gain);

#undef SAVE_RF_MAX
#undef SAVE_PHY_MAX
}

void
bwi_rf_init(struct bwi_mac *mac)
{
	struct bwi_rf *rf = &mac->mac_rf;

	if (rf->rf_type == BWI_RF_T_BCM2060) {
		/* TODO: 11A */
	} else {
		if (rf->rf_flags & BWI_RF_F_INITED)
			RF_WRITE(mac, 0x78, rf->rf_calib);
		else
			bwi_rf_init_bcm2050(mac);
	}
}

static void
bwi_rf_off_11a(struct bwi_mac *mac)
{
	RF_WRITE(mac, 0x4, 0xff);
	RF_WRITE(mac, 0x5, 0xfb);

	PHY_SETBITS(mac, 0x10, 0x8);
	PHY_SETBITS(mac, 0x11, 0x8);

	PHY_WRITE(mac, 0x15, 0xaa00);
}

static void
bwi_rf_off_11bg(struct bwi_mac *mac)
{
	PHY_WRITE(mac, 0x15, 0xaa00);
}

static void
bwi_rf_off_11g_rev5(struct bwi_mac *mac)
{
	PHY_SETBITS(mac, 0x811, 0x8c);
	PHY_CLRBITS(mac, 0x812, 0x8c);
}

static void
bwi_rf_work_around(struct bwi_mac *mac, u_int chan)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;

	if (chan == IEEE80211_CHAN_ANY) {
		device_printf(sc->sc_dev, "%s invalid channel!!\n", __func__);
		return;
	}

	if (rf->rf_type != BWI_RF_T_BCM2050 || rf->rf_rev >= 6)
		return;

	if (chan <= 10)
		CSR_WRITE_2(sc, BWI_RF_CHAN, BWI_RF_2GHZ_CHAN(chan + 4));
	else
		CSR_WRITE_2(sc, BWI_RF_CHAN, BWI_RF_2GHZ_CHAN(1));
	DELAY(1000);
	CSR_WRITE_2(sc, BWI_RF_CHAN, BWI_RF_2GHZ_CHAN(chan));
}

static __inline struct bwi_rf_lo *
bwi_rf_lo_find(struct bwi_mac *mac, const struct bwi_tpctl *tpctl)
{
	uint16_t rf_atten, bbp_atten;
	int remap_rf_atten;

	remap_rf_atten = 1;
	if (tpctl == NULL) {
		bbp_atten = 2;
		rf_atten = 3;
	} else {
		if (tpctl->tp_ctrl1 == 3)
			remap_rf_atten = 0;

		bbp_atten = tpctl->bbp_atten;
		rf_atten = tpctl->rf_atten;

		if (bbp_atten > 6)
			bbp_atten = 6;
	}

	if (remap_rf_atten) {
#define MAP_MAX	10
		static const uint16_t map[MAP_MAX] =
		{ 11, 10, 11, 12, 13, 12, 13, 12, 13, 12 };

#if 0
		KASSERT(rf_atten < MAP_MAX, ("rf_atten %d", rf_atten));
		rf_atten = map[rf_atten];
#else
		if (rf_atten >= MAP_MAX) {
			rf_atten = 0;	/* XXX */
		} else {
			rf_atten = map[rf_atten];
		}
#endif
#undef MAP_MAX
	}

	return bwi_get_rf_lo(mac, rf_atten, bbp_atten);
}

void
bwi_rf_lo_adjust(struct bwi_mac *mac, const struct bwi_tpctl *tpctl)
{
	const struct bwi_rf_lo *lo;

	lo = bwi_rf_lo_find(mac, tpctl);
	RF_LO_WRITE(mac, lo);
}

static void
bwi_rf_lo_write(struct bwi_mac *mac, const struct bwi_rf_lo *lo)
{
	uint16_t val;

	val = (uint8_t)lo->ctrl_lo;
	val |= ((uint8_t)lo->ctrl_hi) << 8;

	PHY_WRITE(mac, BWI_PHYR_RF_LO, val);
}

static int
bwi_rf_gain_max_reached(struct bwi_mac *mac, int idx)
{
	PHY_FILT_SETBITS(mac, 0x812, 0xf0ff, idx << 8);
	PHY_FILT_SETBITS(mac, 0x15, 0xfff, 0xa000);
	PHY_SETBITS(mac, 0x15, 0xf000);

	DELAY(20);

	return (PHY_READ(mac, 0x2d) >= 0xdfc);
}

/* XXX use bitmap array */
static __inline uint16_t
bitswap4(uint16_t val)
{
	uint16_t ret;

	ret = (val & 0x8) >> 3;
	ret |= (val & 0x4) >> 1;
	ret |= (val & 0x2) << 1;
	ret |= (val & 0x1) << 3;
	return ret;
}

static __inline uint16_t
bwi_phy812_value(struct bwi_mac *mac, uint16_t lpd)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	uint16_t lo_gain, ext_lna, loop;

	if ((phy->phy_flags & BWI_PHY_F_LINKED) == 0)
		return 0;

	lo_gain = rf->rf_lo_gain;
	if (rf->rf_rev == 8)
		lo_gain += 0x3e;
	else
		lo_gain += 0x26;

	if (lo_gain >= 0x46) {
		lo_gain -= 0x46;
		ext_lna = 0x3000;
	} else if (lo_gain >= 0x3a) {
		lo_gain -= 0x3a;
		ext_lna = 0x1000;
	} else if (lo_gain >= 0x2e) {
		lo_gain -= 0x2e;
		ext_lna = 0x2000;
	} else {
		lo_gain -= 0x10;
		ext_lna = 0;
	}

	for (loop = 0; loop < 16; ++loop) {
		lo_gain -= (6 * loop);
		if (lo_gain < 6)
			break;
	}

	if (phy->phy_rev >= 7 && (sc->sc_card_flags & BWI_CARD_F_EXT_LNA)) {
		if (ext_lna)
			ext_lna |= 0x8000;
		ext_lna |= (loop << 8);
		switch (lpd) {
		case 0x011:
			return 0x8f92;
		case 0x001:
			return (0x8092 | ext_lna);
		case 0x101:
			return (0x2092 | ext_lna);
		case 0x100:
			return (0x2093 | ext_lna);
		default:
			panic("unsupported lpd\n");
		}
	} else {
		ext_lna |= (loop << 8);
		switch (lpd) {
		case 0x011:
			return 0xf92;
		case 0x001:
		case 0x101:
			return (0x92 | ext_lna);
		case 0x100:
			return (0x93 | ext_lna);
		default:
			panic("unsupported lpd\n");
		}
	}

	panic("never reached\n");
	return 0;
}

void
bwi_rf_init_bcm2050(struct bwi_mac *mac)
{
#define SAVE_RF_MAX		3
#define SAVE_PHY_COMM_MAX	4
#define SAVE_PHY_11G_MAX	6

	static const uint16_t save_rf_regs[SAVE_RF_MAX] =
	{ 0x0043, 0x0051, 0x0052 };
	static const uint16_t save_phy_regs_comm[SAVE_PHY_COMM_MAX] =
	{ 0x0015, 0x005a, 0x0059, 0x0058 };
	static const uint16_t save_phy_regs_11g[SAVE_PHY_11G_MAX] =
	{ 0x0811, 0x0812, 0x0814, 0x0815, 0x0429, 0x0802 };

	uint16_t save_rf[SAVE_RF_MAX];
	uint16_t save_phy_comm[SAVE_PHY_COMM_MAX];
	uint16_t save_phy_11g[SAVE_PHY_11G_MAX];
	uint16_t phyr_35, phyr_30 = 0, rfr_78, phyr_80f = 0, phyr_810 = 0;
	uint16_t bphy_ctrl = 0, bbp_atten, rf_chan_ex;
	uint16_t phy812_val;
	uint16_t calib;
	uint32_t test_lim, test;
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	int i;

	/*
	 * Save registers for later restoring
	 */
	for (i = 0; i < SAVE_RF_MAX; ++i)
		save_rf[i] = RF_READ(mac, save_rf_regs[i]);
	for (i = 0; i < SAVE_PHY_COMM_MAX; ++i)
		save_phy_comm[i] = PHY_READ(mac, save_phy_regs_comm[i]);

	if (phy->phy_mode == IEEE80211_MODE_11B) {
		phyr_30 = PHY_READ(mac, 0x30);
		bphy_ctrl = CSR_READ_2(sc, BWI_BPHY_CTRL);

		PHY_WRITE(mac, 0x30, 0xff);
		CSR_WRITE_2(sc, BWI_BPHY_CTRL, 0x3f3f);
	} else if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
		for (i = 0; i < SAVE_PHY_11G_MAX; ++i) {
			save_phy_11g[i] =
				PHY_READ(mac, save_phy_regs_11g[i]);
		}

		PHY_SETBITS(mac, 0x814, 0x3);
		PHY_CLRBITS(mac, 0x815, 0x3);
		PHY_CLRBITS(mac, 0x429, 0x8000);
		PHY_CLRBITS(mac, 0x802, 0x3);

		phyr_80f = PHY_READ(mac, 0x80f);
		phyr_810 = PHY_READ(mac, 0x810);

		if (phy->phy_rev >= 3)
			PHY_WRITE(mac, 0x80f, 0xc020);
		else
			PHY_WRITE(mac, 0x80f, 0x8020);
		PHY_WRITE(mac, 0x810, 0);

		phy812_val = bwi_phy812_value(mac, 0x011);
		PHY_WRITE(mac, 0x812, phy812_val);
		if (phy->phy_rev < 7 ||
		    (sc->sc_card_flags & BWI_CARD_F_EXT_LNA) == 0)
			PHY_WRITE(mac, 0x811, 0x1b3);
		else
			PHY_WRITE(mac, 0x811, 0x9b3);
	}
	CSR_SETBITS_2(sc, BWI_RF_ANTDIV, 0x8000);

	phyr_35 = PHY_READ(mac, 0x35);
	PHY_CLRBITS(mac, 0x35, 0x80);

	bbp_atten = CSR_READ_2(sc, BWI_BBP_ATTEN);
	rf_chan_ex = CSR_READ_2(sc, BWI_RF_CHAN_EX);

	if (phy->phy_version == 0) {
		CSR_WRITE_2(sc, BWI_BBP_ATTEN, 0x122);
	} else {
		if (phy->phy_version >= 2)
			PHY_FILT_SETBITS(mac, 0x3, 0xffbf, 0x40);
		CSR_SETBITS_2(sc, BWI_RF_CHAN_EX, 0x2000);
	}

	calib = bwi_rf_calibval(mac);

	if (phy->phy_mode == IEEE80211_MODE_11B)
		RF_WRITE(mac, 0x78, 0x26);

	if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
		phy812_val = bwi_phy812_value(mac, 0x011);
		PHY_WRITE(mac, 0x812, phy812_val);
	}

	PHY_WRITE(mac, 0x15, 0xbfaf);
	PHY_WRITE(mac, 0x2b, 0x1403);

	if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
		phy812_val = bwi_phy812_value(mac, 0x001);
		PHY_WRITE(mac, 0x812, phy812_val);
	}

	PHY_WRITE(mac, 0x15, 0xbfa0);

	RF_SETBITS(mac, 0x51, 0x4);
	if (rf->rf_rev == 8) {
		RF_WRITE(mac, 0x43, 0x1f);
	} else {
		RF_WRITE(mac, 0x52, 0);
		RF_FILT_SETBITS(mac, 0x43, 0xfff0, 0x9);
	}

	test_lim = 0;
	PHY_WRITE(mac, 0x58, 0);
	for (i = 0; i < 16; ++i) {
		PHY_WRITE(mac, 0x5a, 0x480);
		PHY_WRITE(mac, 0x59, 0xc810);

		PHY_WRITE(mac, 0x58, 0xd);
		if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
			phy812_val = bwi_phy812_value(mac, 0x101);
			PHY_WRITE(mac, 0x812, phy812_val);
		}
		PHY_WRITE(mac, 0x15, 0xafb0);
		DELAY(10);

		if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
			phy812_val = bwi_phy812_value(mac, 0x101);
			PHY_WRITE(mac, 0x812, phy812_val);
		}
		PHY_WRITE(mac, 0x15, 0xefb0);
		DELAY(10);

		if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
			phy812_val = bwi_phy812_value(mac, 0x100);
			PHY_WRITE(mac, 0x812, phy812_val);
		}
		PHY_WRITE(mac, 0x15, 0xfff0);
		DELAY(20);

		test_lim += PHY_READ(mac, 0x2d);

		PHY_WRITE(mac, 0x58, 0);
		if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
			phy812_val = bwi_phy812_value(mac, 0x101);
			PHY_WRITE(mac, 0x812, phy812_val);
		}
		PHY_WRITE(mac, 0x15, 0xafb0);
	}
	++test_lim;
	test_lim >>= 9;

	DELAY(10);

	test = 0;
	PHY_WRITE(mac, 0x58, 0);
	for (i = 0; i < 16; ++i) {
		int j;

		rfr_78 = (bitswap4(i) << 1) | 0x20;
		RF_WRITE(mac, 0x78, rfr_78);
		DELAY(10);

		/* NB: This block is slight different than the above one */
		for (j = 0; j < 16; ++j) {
			PHY_WRITE(mac, 0x5a, 0xd80);
			PHY_WRITE(mac, 0x59, 0xc810);

			PHY_WRITE(mac, 0x58, 0xd);
			if ((phy->phy_flags & BWI_PHY_F_LINKED) ||
			    phy->phy_rev >= 2) {
				phy812_val = bwi_phy812_value(mac, 0x101);
				PHY_WRITE(mac, 0x812, phy812_val);
			}
			PHY_WRITE(mac, 0x15, 0xafb0);
			DELAY(10);

			if ((phy->phy_flags & BWI_PHY_F_LINKED) ||
			    phy->phy_rev >= 2) {
				phy812_val = bwi_phy812_value(mac, 0x101);
				PHY_WRITE(mac, 0x812, phy812_val);
			}
			PHY_WRITE(mac, 0x15, 0xefb0);
			DELAY(10);

			if ((phy->phy_flags & BWI_PHY_F_LINKED) ||
			    phy->phy_rev >= 2) {
				phy812_val = bwi_phy812_value(mac, 0x100);
				PHY_WRITE(mac, 0x812, phy812_val);
			}
			PHY_WRITE(mac, 0x15, 0xfff0);
			DELAY(10);

			test += PHY_READ(mac, 0x2d);

			PHY_WRITE(mac, 0x58, 0);
			if ((phy->phy_flags & BWI_PHY_F_LINKED) ||
			    phy->phy_rev >= 2) {
				phy812_val = bwi_phy812_value(mac, 0x101);
				PHY_WRITE(mac, 0x812, phy812_val);
			}
			PHY_WRITE(mac, 0x15, 0xafb0);
		}

		++test;
		test >>= 8;

		if (test > test_lim)
			break;
	}
	if (i > 15)
		rf->rf_calib = rfr_78;
	else
		rf->rf_calib = calib;
	if (rf->rf_calib != 0xffff) {
		DPRINTF(sc, BWI_DBG_RF | BWI_DBG_INIT,
			"RF calibration value: 0x%04x\n", rf->rf_calib);
		rf->rf_flags |= BWI_RF_F_INITED;
	}

	/*
	 * Restore trashes registers
	 */
	PHY_WRITE(mac, save_phy_regs_comm[0], save_phy_comm[0]);

	for (i = 0; i < SAVE_RF_MAX; ++i) {
		int pos = (i + 1) % SAVE_RF_MAX;

		RF_WRITE(mac, save_rf_regs[pos], save_rf[pos]);
	}
	for (i = 1; i < SAVE_PHY_COMM_MAX; ++i)
		PHY_WRITE(mac, save_phy_regs_comm[i], save_phy_comm[i]);

	CSR_WRITE_2(sc, BWI_BBP_ATTEN, bbp_atten);
	if (phy->phy_version != 0)
		CSR_WRITE_2(sc, BWI_RF_CHAN_EX, rf_chan_ex);

	PHY_WRITE(mac, 0x35, phyr_35);
	bwi_rf_work_around(mac, rf->rf_curchan);

	if (phy->phy_mode == IEEE80211_MODE_11B) {
		PHY_WRITE(mac, 0x30, phyr_30);
		CSR_WRITE_2(sc, BWI_BPHY_CTRL, bphy_ctrl);
	} else if ((phy->phy_flags & BWI_PHY_F_LINKED) || phy->phy_rev >= 2) {
		/* XXX Spec only says when PHY is linked (gmode) */
		CSR_CLRBITS_2(sc, BWI_RF_ANTDIV, 0x8000);

		for (i = 0; i < SAVE_PHY_11G_MAX; ++i) {
			PHY_WRITE(mac, save_phy_regs_11g[i],
				  save_phy_11g[i]);
		}

		PHY_WRITE(mac, 0x80f, phyr_80f);
		PHY_WRITE(mac, 0x810, phyr_810);
	}

#undef SAVE_PHY_11G_MAX
#undef SAVE_PHY_COMM_MAX
#undef SAVE_RF_MAX
}

static uint16_t
bwi_rf_calibval(struct bwi_mac *mac)
{
	/* http://bcm-specs.sipsolutions.net/RCCTable */
	static const uint16_t rf_calibvals[] = {
		0x2, 0x3, 0x1, 0xf, 0x6, 0x7, 0x5, 0xf,
		0xa, 0xb, 0x9, 0xf, 0xe, 0xf, 0xd, 0xf
	};
	uint16_t val, calib;
	int idx;

	val = RF_READ(mac, BWI_RFR_BBP_ATTEN);
	idx = __SHIFTOUT(val, BWI_RFR_BBP_ATTEN_CALIB_IDX);
	KASSERT(idx < (int)nitems(rf_calibvals), ("idx %d", idx));

	calib = rf_calibvals[idx] << 1;
	if (val & BWI_RFR_BBP_ATTEN_CALIB_BIT)
		calib |= 0x1;
	calib |= 0x20;

	return calib;
}

static __inline int32_t
_bwi_adjust_devide(int32_t num, int32_t den)
{
	if (num < 0)
		return (num / den);
	else
		return (num + den / 2) / den;
}

/*
 * http://bcm-specs.sipsolutions.net/TSSI_to_DBM_Table
 * "calculating table entries"
 */
static int
bwi_rf_calc_txpower(int8_t *txpwr, uint8_t idx, const int16_t pa_params[])
{
	int32_t m1, m2, f, dbm;
	int i;

	m1 = _bwi_adjust_devide(16 * pa_params[0] + idx * pa_params[1], 32);
	m2 = imax(_bwi_adjust_devide(32768 + idx * pa_params[2], 256), 1);

#define ITER_MAX	16

	f = 256;
	for (i = 0; i < ITER_MAX; ++i) {
		int32_t q, d;

		q = _bwi_adjust_devide(
			f * 4096 - _bwi_adjust_devide(m2 * f, 16) * f, 2048);
		d = abs(q - f);
		f = q;

		if (d < 2)
			break;
	}
	if (i == ITER_MAX)
		return EINVAL;

#undef ITER_MAX

	dbm = _bwi_adjust_devide(m1 * f, 8192);
	if (dbm < -127)
		dbm = -127;
	else if (dbm > 128)
		dbm = 128;

	*txpwr = dbm;
	return 0;
}

int
bwi_rf_map_txpower(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t sprom_ofs, val, mask;
	int16_t pa_params[3];
	int error = 0, i, ant_gain, reg_txpower_max;

	/*
	 * Find out max TX power
	 */
	val = bwi_read_sprom(sc, BWI_SPROM_MAX_TXPWR);
	if (phy->phy_mode == IEEE80211_MODE_11A) {
		rf->rf_txpower_max = __SHIFTOUT(val,
				     BWI_SPROM_MAX_TXPWR_MASK_11A);
	} else {
		rf->rf_txpower_max = __SHIFTOUT(val,
				     BWI_SPROM_MAX_TXPWR_MASK_11BG);

		if ((sc->sc_card_flags & BWI_CARD_F_PA_GPIO9) &&
		    phy->phy_mode == IEEE80211_MODE_11G)
			rf->rf_txpower_max -= 3;
	}
	if (rf->rf_txpower_max <= 0) {
		device_printf(sc->sc_dev, "invalid max txpower in sprom\n");
		rf->rf_txpower_max = 74;
	}
	DPRINTF(sc, BWI_DBG_RF | BWI_DBG_TXPOWER | BWI_DBG_ATTACH,
		"max txpower from sprom: %d dBm\n", rf->rf_txpower_max);

	/*
	 * Find out region/domain max TX power, which is adjusted
	 * by antenna gain and 1.5 dBm fluctuation as mentioned
	 * in v3 spec.
	 */
	val = bwi_read_sprom(sc, BWI_SPROM_ANT_GAIN);
	if (phy->phy_mode == IEEE80211_MODE_11A)
		ant_gain = __SHIFTOUT(val, BWI_SPROM_ANT_GAIN_MASK_11A);
	else
		ant_gain = __SHIFTOUT(val, BWI_SPROM_ANT_GAIN_MASK_11BG);
	if (ant_gain == 0xff) {
		device_printf(sc->sc_dev, "invalid antenna gain in sprom\n");
		ant_gain = 2;
	}
	ant_gain *= 4;
	DPRINTF(sc, BWI_DBG_RF | BWI_DBG_TXPOWER | BWI_DBG_ATTACH,
		"ant gain %d dBm\n", ant_gain);

	reg_txpower_max = 90 - ant_gain - 6;	/* XXX magic number */
	DPRINTF(sc, BWI_DBG_RF | BWI_DBG_TXPOWER | BWI_DBG_ATTACH,
		"region/domain max txpower %d dBm\n", reg_txpower_max);

	/*
	 * Force max TX power within region/domain TX power limit
	 */
	if (rf->rf_txpower_max > reg_txpower_max)
		rf->rf_txpower_max = reg_txpower_max;
	DPRINTF(sc, BWI_DBG_RF | BWI_DBG_TXPOWER | BWI_DBG_ATTACH,
		"max txpower %d dBm\n", rf->rf_txpower_max);

	/*
	 * Create TSSI to TX power mapping
	 */

	if (sc->sc_bbp_id == BWI_BBPID_BCM4301 &&
	    rf->rf_type != BWI_RF_T_BCM2050) {
		rf->rf_idle_tssi0 = BWI_DEFAULT_IDLE_TSSI;
		bcopy(bwi_txpower_map_11b, rf->rf_txpower_map0,
		      sizeof(rf->rf_txpower_map0));
		goto back;
	}

#define IS_VALID_PA_PARAM(p)	((p) != 0 && (p) != -1)

	/*
	 * Extract PA parameters
	 */
	if (phy->phy_mode == IEEE80211_MODE_11A)
		sprom_ofs = BWI_SPROM_PA_PARAM_11A;
	else
		sprom_ofs = BWI_SPROM_PA_PARAM_11BG;
	for (i = 0; i < nitems(pa_params); ++i)
		pa_params[i] = (int16_t)bwi_read_sprom(sc, sprom_ofs + (i * 2));

	for (i = 0; i < nitems(pa_params); ++i) {
		/*
		 * If one of the PA parameters from SPROM is not valid,
		 * fall back to the default values, if there are any.
		 */
		if (!IS_VALID_PA_PARAM(pa_params[i])) {
			const int8_t *txpower_map;

			if (phy->phy_mode == IEEE80211_MODE_11A) {
				device_printf(sc->sc_dev,
					  "no tssi2dbm table for 11a PHY\n");
				return ENXIO;
			}

			if (phy->phy_mode == IEEE80211_MODE_11G) {
				DPRINTF(sc,
				BWI_DBG_RF | BWI_DBG_TXPOWER | BWI_DBG_ATTACH,
				"%s\n", "use default 11g TSSI map");
				txpower_map = bwi_txpower_map_11g;
			} else {
				DPRINTF(sc,
				BWI_DBG_RF | BWI_DBG_TXPOWER | BWI_DBG_ATTACH,
				"%s\n", "use default 11b TSSI map");
				txpower_map = bwi_txpower_map_11b;
			}

			rf->rf_idle_tssi0 = BWI_DEFAULT_IDLE_TSSI;
			bcopy(txpower_map, rf->rf_txpower_map0,
			      sizeof(rf->rf_txpower_map0));
			goto back;
		}
	}

	/*
	 * All of the PA parameters from SPROM are valid.
	 */

	/*
	 * Extract idle TSSI from SPROM.
	 */
	val = bwi_read_sprom(sc, BWI_SPROM_IDLE_TSSI);
	DPRINTF(sc, BWI_DBG_RF | BWI_DBG_TXPOWER | BWI_DBG_ATTACH,
		"sprom idle tssi: 0x%04x\n", val);

	if (phy->phy_mode == IEEE80211_MODE_11A)
		mask = BWI_SPROM_IDLE_TSSI_MASK_11A;
	else
		mask = BWI_SPROM_IDLE_TSSI_MASK_11BG;

	rf->rf_idle_tssi0 = (int)__SHIFTOUT(val, mask);
	if (!IS_VALID_PA_PARAM(rf->rf_idle_tssi0))
		rf->rf_idle_tssi0 = 62;

#undef IS_VALID_PA_PARAM

	/*
	 * Calculate TX power map, which is indexed by TSSI
	 */
	DPRINTF(sc, BWI_DBG_RF | BWI_DBG_ATTACH | BWI_DBG_TXPOWER,
		"%s\n", "TSSI-TX power map:");
	for (i = 0; i < BWI_TSSI_MAX; ++i) {
		error = bwi_rf_calc_txpower(&rf->rf_txpower_map0[i], i,
					    pa_params);
		if (error) {
			device_printf(sc->sc_dev,
				  "bwi_rf_calc_txpower failed\n");
			break;
		}

#ifdef BWI_DEBUG
		if (i != 0 && i % 8 == 0) {
			_DPRINTF(sc,
			BWI_DBG_RF | BWI_DBG_ATTACH | BWI_DBG_TXPOWER,
			"%s\n", "");
		}
#endif
		_DPRINTF(sc, BWI_DBG_RF | BWI_DBG_ATTACH | BWI_DBG_TXPOWER,
			 "%d ", rf->rf_txpower_map0[i]);
	}
	_DPRINTF(sc, BWI_DBG_RF | BWI_DBG_ATTACH | BWI_DBG_TXPOWER,
		 "%s\n", "");
back:
	DPRINTF(sc, BWI_DBG_RF | BWI_DBG_TXPOWER | BWI_DBG_ATTACH,
		"idle tssi0: %d\n", rf->rf_idle_tssi0);
	return error;
}

static void
bwi_rf_lo_update_11g(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_tpctl *tpctl = &mac->mac_tpctl;
	struct rf_saveregs regs;
	uint16_t ant_div, chan_ex;
	uint8_t devi_ctrl;
	u_int orig_chan;

	/*
	 * Save RF/PHY registers for later restoration
	 */
	orig_chan = rf->rf_curchan;
	bzero(&regs, sizeof(regs));

	if (phy->phy_flags & BWI_PHY_F_LINKED) {
		SAVE_PHY_REG(mac, &regs, 429);
		SAVE_PHY_REG(mac, &regs, 802);

		PHY_WRITE(mac, 0x429, regs.phy_429 & 0x7fff);
		PHY_WRITE(mac, 0x802, regs.phy_802 & 0xfffc);
	}

	ant_div = CSR_READ_2(sc, BWI_RF_ANTDIV);
	CSR_WRITE_2(sc, BWI_RF_ANTDIV, ant_div | 0x8000);
	chan_ex = CSR_READ_2(sc, BWI_RF_CHAN_EX);

	SAVE_PHY_REG(mac, &regs, 15);
	SAVE_PHY_REG(mac, &regs, 2a);
	SAVE_PHY_REG(mac, &regs, 35);
	SAVE_PHY_REG(mac, &regs, 60);
	SAVE_RF_REG(mac, &regs, 43);
	SAVE_RF_REG(mac, &regs, 7a);
	SAVE_RF_REG(mac, &regs, 52);
	if (phy->phy_flags & BWI_PHY_F_LINKED) {
		SAVE_PHY_REG(mac, &regs, 811);
		SAVE_PHY_REG(mac, &regs, 812);
		SAVE_PHY_REG(mac, &regs, 814);
		SAVE_PHY_REG(mac, &regs, 815);
	}

	/* Force to channel 6 */
	bwi_rf_set_chan(mac, 6, 0);

	if (phy->phy_flags & BWI_PHY_F_LINKED) {
		PHY_WRITE(mac, 0x429, regs.phy_429 & 0x7fff);
		PHY_WRITE(mac, 0x802, regs.phy_802 & 0xfffc);
		bwi_mac_dummy_xmit(mac);
	}
	RF_WRITE(mac, 0x43, 0x6);

	bwi_phy_set_bbp_atten(mac, 2);

	CSR_WRITE_2(sc, BWI_RF_CHAN_EX, 0);

	PHY_WRITE(mac, 0x2e, 0x7f);
	PHY_WRITE(mac, 0x80f, 0x78);
	PHY_WRITE(mac, 0x35, regs.phy_35 & 0xff7f);
	RF_WRITE(mac, 0x7a, regs.rf_7a & 0xfff0);
	PHY_WRITE(mac, 0x2b, 0x203);
	PHY_WRITE(mac, 0x2a, 0x8a3);

	if (phy->phy_flags & BWI_PHY_F_LINKED) {
		PHY_WRITE(mac, 0x814, regs.phy_814 | 0x3);
		PHY_WRITE(mac, 0x815, regs.phy_815 & 0xfffc);
		PHY_WRITE(mac, 0x811, 0x1b3);
		PHY_WRITE(mac, 0x812, 0xb2);
	}

	if ((sc->sc_flags & BWI_F_RUNNING) == 0)
		tpctl->tp_ctrl2 = bwi_rf_get_tp_ctrl2(mac);
	PHY_WRITE(mac, 0x80f, 0x8078);

	/*
	 * Measure all RF LO
	 */
	devi_ctrl = _bwi_rf_lo_update_11g(mac, regs.rf_7a);

	/*
	 * Restore saved RF/PHY registers
	 */
	if (phy->phy_flags & BWI_PHY_F_LINKED) {
		PHY_WRITE(mac, 0x15, 0xe300);
		PHY_WRITE(mac, 0x812, (devi_ctrl << 8) | 0xa0);
		DELAY(5);
		PHY_WRITE(mac, 0x812, (devi_ctrl << 8) | 0xa2);
		DELAY(2);
		PHY_WRITE(mac, 0x812, (devi_ctrl << 8) | 0xa3);
	} else {
		PHY_WRITE(mac, 0x15, devi_ctrl | 0xefa0);
	}

	if ((sc->sc_flags & BWI_F_RUNNING) == 0)
		tpctl = NULL;
	bwi_rf_lo_adjust(mac, tpctl);

	PHY_WRITE(mac, 0x2e, 0x807f);
	if (phy->phy_flags & BWI_PHY_F_LINKED)
		PHY_WRITE(mac, 0x2f, 0x202);
	else
		PHY_WRITE(mac, 0x2f, 0x101);

	CSR_WRITE_2(sc, BWI_RF_CHAN_EX, chan_ex);

	RESTORE_PHY_REG(mac, &regs, 15);
	RESTORE_PHY_REG(mac, &regs, 2a);
	RESTORE_PHY_REG(mac, &regs, 35);
	RESTORE_PHY_REG(mac, &regs, 60);

	RESTORE_RF_REG(mac, &regs, 43);
	RESTORE_RF_REG(mac, &regs, 7a);

	regs.rf_52 &= 0xf0;
	regs.rf_52 |= (RF_READ(mac, 0x52) & 0xf);
	RF_WRITE(mac, 0x52, regs.rf_52);

	CSR_WRITE_2(sc, BWI_RF_ANTDIV, ant_div);

	if (phy->phy_flags & BWI_PHY_F_LINKED) {
		RESTORE_PHY_REG(mac, &regs, 811);
		RESTORE_PHY_REG(mac, &regs, 812);
		RESTORE_PHY_REG(mac, &regs, 814);
		RESTORE_PHY_REG(mac, &regs, 815);
		RESTORE_PHY_REG(mac, &regs, 429);
		RESTORE_PHY_REG(mac, &regs, 802);
	}

	bwi_rf_set_chan(mac, orig_chan, 1);
}

static uint32_t
bwi_rf_lo_devi_measure(struct bwi_mac *mac, uint16_t ctrl)
{
	struct bwi_phy *phy = &mac->mac_phy;
	uint32_t devi = 0;
	int i;

	if (phy->phy_flags & BWI_PHY_F_LINKED)
		ctrl <<= 8;

	for (i = 0; i < 8; ++i) {
		if (phy->phy_flags & BWI_PHY_F_LINKED) {
			PHY_WRITE(mac, 0x15, 0xe300);
			PHY_WRITE(mac, 0x812, ctrl | 0xb0);
			DELAY(5);
			PHY_WRITE(mac, 0x812, ctrl | 0xb2);
			DELAY(2);
			PHY_WRITE(mac, 0x812, ctrl | 0xb3);
			DELAY(4);
			PHY_WRITE(mac, 0x15, 0xf300);
		} else {
			PHY_WRITE(mac, 0x15, ctrl | 0xefa0);
			DELAY(2);
			PHY_WRITE(mac, 0x15, ctrl | 0xefe0);
			DELAY(4);
			PHY_WRITE(mac, 0x15, ctrl | 0xffe0);
		}
		DELAY(8);
		devi += PHY_READ(mac, 0x2d);
	}
	return devi;
}

static uint16_t
bwi_rf_get_tp_ctrl2(struct bwi_mac *mac)
{
	uint32_t devi_min;
	uint16_t tp_ctrl2 = 0;
	int i;

	RF_WRITE(mac, 0x52, 0);
	DELAY(10);
	devi_min = bwi_rf_lo_devi_measure(mac, 0);

	for (i = 0; i < 16; ++i) {
		uint32_t devi;

		RF_WRITE(mac, 0x52, i);
		DELAY(10);
		devi = bwi_rf_lo_devi_measure(mac, 0);

		if (devi < devi_min) {
			devi_min = devi;
			tp_ctrl2 = i;
		}
	}
	return tp_ctrl2;
}

static uint8_t
_bwi_rf_lo_update_11g(struct bwi_mac *mac, uint16_t orig_rf7a)
{
#define RF_ATTEN_LISTSZ	14
#define BBP_ATTEN_MAX	4	/* half */

	static const int rf_atten_list[RF_ATTEN_LISTSZ] =
	{ 3, 1, 5, 7, 9, 2, 0, 4, 6, 8, 1, 2, 3, 4 };
	static const int rf_atten_init_list[RF_ATTEN_LISTSZ] =
        { 0, 3, 1, 5, 7, 3, 2, 0, 4, 6, -1, -1, -1, -1 };
	static const int rf_lo_measure_order[RF_ATTEN_LISTSZ] =
	{ 3, 1, 5, 7, 9, 2, 0, 4, 6, 8, 10, 11, 12, 13 };

	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf_lo lo_save, *lo;
	uint8_t devi_ctrl = 0;
	int idx, adj_rf7a = 0;

	bzero(&lo_save, sizeof(lo_save));
	for (idx = 0; idx < RF_ATTEN_LISTSZ; ++idx) {
		int init_rf_atten = rf_atten_init_list[idx];
		int rf_atten = rf_atten_list[idx];
		int bbp_atten;

		for (bbp_atten = 0; bbp_atten < BBP_ATTEN_MAX; ++bbp_atten) {
			uint16_t tp_ctrl2, rf7a;

			if ((sc->sc_flags & BWI_F_RUNNING) == 0) {
				if (idx == 0) {
					bzero(&lo_save, sizeof(lo_save));
				} else if (init_rf_atten < 0) {
					lo = bwi_get_rf_lo(mac,
						rf_atten, 2 * bbp_atten);
					bcopy(lo, &lo_save, sizeof(lo_save));
				} else {
					lo = bwi_get_rf_lo(mac,
						init_rf_atten, 0);
					bcopy(lo, &lo_save, sizeof(lo_save));
				}

				devi_ctrl = 0;
				adj_rf7a = 0;

				/*
				 * XXX
				 * Linux driver overflows 'val'
				 */
				if (init_rf_atten >= 0) {
					int val;

					val = rf_atten * 2 + bbp_atten;
					if (val > 14) {
						adj_rf7a = 1;
						if (val > 17)
							devi_ctrl = 1;
						if (val > 19)
							devi_ctrl = 2;
					}
				}
			} else {
				lo = bwi_get_rf_lo(mac,
					rf_atten, 2 * bbp_atten);
				if (!bwi_rf_lo_isused(mac, lo))
					continue;
				bcopy(lo, &lo_save, sizeof(lo_save));

				devi_ctrl = 3;
				adj_rf7a = 0;
			}

			RF_WRITE(mac, BWI_RFR_ATTEN, rf_atten);

			tp_ctrl2 = mac->mac_tpctl.tp_ctrl2;
			if (init_rf_atten < 0)
				tp_ctrl2 |= (3 << 4);
			RF_WRITE(mac, BWI_RFR_TXPWR, tp_ctrl2);

			DELAY(10);

			bwi_phy_set_bbp_atten(mac, bbp_atten * 2);

			rf7a = orig_rf7a & 0xfff0;
			if (adj_rf7a)
				rf7a |= 0x8;
			RF_WRITE(mac, 0x7a, rf7a);

			lo = bwi_get_rf_lo(mac,
				rf_lo_measure_order[idx], bbp_atten * 2);
			bwi_rf_lo_measure_11g(mac, &lo_save, lo, devi_ctrl);
		}
	}
	return devi_ctrl;

#undef RF_ATTEN_LISTSZ
#undef BBP_ATTEN_MAX
}

static void
bwi_rf_lo_measure_11g(struct bwi_mac *mac, const struct bwi_rf_lo *src_lo,
	struct bwi_rf_lo *dst_lo, uint8_t devi_ctrl)
{
#define LO_ADJUST_MIN	1
#define LO_ADJUST_MAX	8
#define LO_ADJUST(hi, lo)	{ .ctrl_hi = hi, .ctrl_lo = lo }
	static const struct bwi_rf_lo rf_lo_adjust[LO_ADJUST_MAX] = {
		LO_ADJUST(1,	1),
		LO_ADJUST(1,	0),
		LO_ADJUST(1,	-1),
		LO_ADJUST(0,	-1),
		LO_ADJUST(-1,	-1),
		LO_ADJUST(-1,	0),
		LO_ADJUST(-1,	1),
		LO_ADJUST(0,	1)
	};
#undef LO_ADJUST

	struct bwi_rf_lo lo_min;
	uint32_t devi_min;
	int found, loop_count, adjust_state;

	bcopy(src_lo, &lo_min, sizeof(lo_min));
	RF_LO_WRITE(mac, &lo_min);
	devi_min = bwi_rf_lo_devi_measure(mac, devi_ctrl);

	loop_count = 12;	/* XXX */
	adjust_state = 0;
	do {
		struct bwi_rf_lo lo_base;
		int i, fin;

		found = 0;
		if (adjust_state == 0) {
			i = LO_ADJUST_MIN;
			fin = LO_ADJUST_MAX;
		} else if (adjust_state % 2 == 0) {
			i = adjust_state - 1;
			fin = adjust_state + 1;
		} else {
			i = adjust_state - 2;
			fin = adjust_state + 2;
		}

		if (i < LO_ADJUST_MIN)
			i += LO_ADJUST_MAX;
		KASSERT(i <= LO_ADJUST_MAX && i >= LO_ADJUST_MIN, ("i %d", i));

		if (fin > LO_ADJUST_MAX)
			fin -= LO_ADJUST_MAX;
		KASSERT(fin <= LO_ADJUST_MAX && fin >= LO_ADJUST_MIN,
		    ("fin %d", fin));

		bcopy(&lo_min, &lo_base, sizeof(lo_base));
		for (;;) {
			struct bwi_rf_lo lo;

			lo.ctrl_hi = lo_base.ctrl_hi +
				rf_lo_adjust[i - 1].ctrl_hi;
			lo.ctrl_lo = lo_base.ctrl_lo +
				rf_lo_adjust[i - 1].ctrl_lo;

			if (abs(lo.ctrl_lo) < 9 && abs(lo.ctrl_hi) < 9) {
				uint32_t devi;

				RF_LO_WRITE(mac, &lo);
				devi = bwi_rf_lo_devi_measure(mac, devi_ctrl);
				if (devi < devi_min) {
					devi_min = devi;
					adjust_state = i;
					found = 1;
					bcopy(&lo, &lo_min, sizeof(lo_min));
				}
			}
			if (i == fin)
				break;
			if (i == LO_ADJUST_MAX)
				i = LO_ADJUST_MIN;
			else
				++i;
		}
	} while (loop_count-- && found);

	bcopy(&lo_min, dst_lo, sizeof(*dst_lo));

#undef LO_ADJUST_MIN
#undef LO_ADJUST_MAX
}

static void
bwi_rf_calc_nrssi_slope_11b(struct bwi_mac *mac)
{
#define SAVE_RF_MAX	3
#define SAVE_PHY_MAX	8

	static const uint16_t save_rf_regs[SAVE_RF_MAX] =
	{ 0x7a, 0x52, 0x43 };
	static const uint16_t save_phy_regs[SAVE_PHY_MAX] =
	{ 0x30, 0x26, 0x15, 0x2a, 0x20, 0x5a, 0x59, 0x58 };

	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t save_rf[SAVE_RF_MAX];
	uint16_t save_phy[SAVE_PHY_MAX];
	uint16_t ant_div, bbp_atten, chan_ex;
	int16_t nrssi[2];
	int i;

	/*
	 * Save RF/PHY registers for later restoration
	 */
	for (i = 0; i < SAVE_RF_MAX; ++i)
		save_rf[i] = RF_READ(mac, save_rf_regs[i]);
	for (i = 0; i < SAVE_PHY_MAX; ++i)
		save_phy[i] = PHY_READ(mac, save_phy_regs[i]);

	ant_div = CSR_READ_2(sc, BWI_RF_ANTDIV);
	bbp_atten = CSR_READ_2(sc, BWI_BBP_ATTEN);
	chan_ex = CSR_READ_2(sc, BWI_RF_CHAN_EX);

	/*
	 * Calculate nrssi0
	 */
	if (phy->phy_rev >= 5)
		RF_CLRBITS(mac, 0x7a, 0xff80);
	else
		RF_CLRBITS(mac, 0x7a, 0xfff0);
	PHY_WRITE(mac, 0x30, 0xff);

	CSR_WRITE_2(sc, BWI_BPHY_CTRL, 0x7f7f);

	PHY_WRITE(mac, 0x26, 0);
	PHY_SETBITS(mac, 0x15, 0x20);
	PHY_WRITE(mac, 0x2a, 0x8a3);
	RF_SETBITS(mac, 0x7a, 0x80);

	nrssi[0] = (int16_t)PHY_READ(mac, 0x27);

	/*
	 * Calculate nrssi1
	 */
	RF_CLRBITS(mac, 0x7a, 0xff80);
	if (phy->phy_version >= 2)
		CSR_WRITE_2(sc, BWI_BBP_ATTEN, 0x40);
	else if (phy->phy_version == 0)
		CSR_WRITE_2(sc, BWI_BBP_ATTEN, 0x122);
	else
		CSR_CLRBITS_2(sc, BWI_RF_CHAN_EX, 0xdfff);

	PHY_WRITE(mac, 0x20, 0x3f3f);
	PHY_WRITE(mac, 0x15, 0xf330);

	RF_WRITE(mac, 0x5a, 0x60);
	RF_CLRBITS(mac, 0x43, 0xff0f);

	PHY_WRITE(mac, 0x5a, 0x480);
	PHY_WRITE(mac, 0x59, 0x810);
	PHY_WRITE(mac, 0x58, 0xd);

	DELAY(20);

	nrssi[1] = (int16_t)PHY_READ(mac, 0x27);

	/*
	 * Restore saved RF/PHY registers
	 */
	PHY_WRITE(mac, save_phy_regs[0], save_phy[0]);
	RF_WRITE(mac, save_rf_regs[0], save_rf[0]);

	CSR_WRITE_2(sc, BWI_RF_ANTDIV, ant_div);

	for (i = 1; i < 4; ++i)
		PHY_WRITE(mac, save_phy_regs[i], save_phy[i]);

	bwi_rf_work_around(mac, rf->rf_curchan);

	if (phy->phy_version != 0)
		CSR_WRITE_2(sc, BWI_RF_CHAN_EX, chan_ex);

	for (; i < SAVE_PHY_MAX; ++i)
		PHY_WRITE(mac, save_phy_regs[i], save_phy[i]);

	for (i = 1; i < SAVE_RF_MAX; ++i)
		RF_WRITE(mac, save_rf_regs[i], save_rf[i]);

	/*
	 * Install calculated narrow RSSI values
	 */
	if (nrssi[0] == nrssi[1])
		rf->rf_nrssi_slope = 0x10000;
	else
		rf->rf_nrssi_slope = 0x400000 / (nrssi[0] - nrssi[1]);
	if (nrssi[0] <= -4) {
		rf->rf_nrssi[0] = nrssi[0];
		rf->rf_nrssi[1] = nrssi[1];
	}

#undef SAVE_RF_MAX
#undef SAVE_PHY_MAX
}

static void
bwi_rf_set_nrssi_ofs_11g(struct bwi_mac *mac)
{
#define SAVE_RF_MAX		2
#define SAVE_PHY_COMM_MAX	10
#define SAVE_PHY6_MAX		8

	static const uint16_t save_rf_regs[SAVE_RF_MAX] =
	{ 0x7a, 0x43 };
	static const uint16_t save_phy_comm_regs[SAVE_PHY_COMM_MAX] = {
		0x0001, 0x0811, 0x0812, 0x0814,
		0x0815, 0x005a, 0x0059, 0x0058,
		0x000a, 0x0003
	};
	static const uint16_t save_phy6_regs[SAVE_PHY6_MAX] = {
		0x002e, 0x002f, 0x080f, 0x0810,
		0x0801, 0x0060, 0x0014, 0x0478
	};

	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t save_rf[SAVE_RF_MAX];
	uint16_t save_phy_comm[SAVE_PHY_COMM_MAX];
	uint16_t save_phy6[SAVE_PHY6_MAX];
	uint16_t rf7b = 0xffff;
	int16_t nrssi;
	int i, phy6_idx = 0;

	for (i = 0; i < SAVE_PHY_COMM_MAX; ++i)
		save_phy_comm[i] = PHY_READ(mac, save_phy_comm_regs[i]);
	for (i = 0; i < SAVE_RF_MAX; ++i)
		save_rf[i] = RF_READ(mac, save_rf_regs[i]);

	PHY_CLRBITS(mac, 0x429, 0x8000);
	PHY_FILT_SETBITS(mac, 0x1, 0x3fff, 0x4000);
	PHY_SETBITS(mac, 0x811, 0xc);
	PHY_FILT_SETBITS(mac, 0x812, 0xfff3, 0x4);
	PHY_CLRBITS(mac, 0x802, 0x3);

	if (phy->phy_rev >= 6) {
		for (i = 0; i < SAVE_PHY6_MAX; ++i)
			save_phy6[i] = PHY_READ(mac, save_phy6_regs[i]);

		PHY_WRITE(mac, 0x2e, 0);
		PHY_WRITE(mac, 0x2f, 0);
		PHY_WRITE(mac, 0x80f, 0);
		PHY_WRITE(mac, 0x810, 0);
		PHY_SETBITS(mac, 0x478, 0x100);
		PHY_SETBITS(mac, 0x801, 0x40);
		PHY_SETBITS(mac, 0x60, 0x40);
		PHY_SETBITS(mac, 0x14, 0x200);
	}

	RF_SETBITS(mac, 0x7a, 0x70);
	RF_SETBITS(mac, 0x7a, 0x80);

	DELAY(30);

	nrssi = bwi_nrssi_11g(mac);
	if (nrssi == 31) {
		for (i = 7; i >= 4; --i) {
			RF_WRITE(mac, 0x7b, i);
			DELAY(20);
			nrssi = bwi_nrssi_11g(mac);
			if (nrssi < 31 && rf7b == 0xffff)
				rf7b = i;
		}
		if (rf7b == 0xffff)
			rf7b = 4;
	} else {
		struct bwi_gains gains;

		RF_CLRBITS(mac, 0x7a, 0xff80);

		PHY_SETBITS(mac, 0x814, 0x1);
		PHY_CLRBITS(mac, 0x815, 0x1);
		PHY_SETBITS(mac, 0x811, 0xc);
		PHY_SETBITS(mac, 0x812, 0xc);
		PHY_SETBITS(mac, 0x811, 0x30);
		PHY_SETBITS(mac, 0x812, 0x30);
		PHY_WRITE(mac, 0x5a, 0x480);
		PHY_WRITE(mac, 0x59, 0x810);
		PHY_WRITE(mac, 0x58, 0xd);
		if (phy->phy_version == 0)
			PHY_WRITE(mac, 0x3, 0x122);
		else
			PHY_SETBITS(mac, 0xa, 0x2000);
		PHY_SETBITS(mac, 0x814, 0x4);
		PHY_CLRBITS(mac, 0x815, 0x4);
		PHY_FILT_SETBITS(mac, 0x3, 0xff9f, 0x40);
		RF_SETBITS(mac, 0x7a, 0xf);

		bzero(&gains, sizeof(gains));
		gains.tbl_gain1 = 3;
		gains.tbl_gain2 = 0;
		gains.phy_gain = 1;
		bwi_set_gains(mac, &gains);

		RF_FILT_SETBITS(mac, 0x43, 0xf0, 0xf);
		DELAY(30);

		nrssi = bwi_nrssi_11g(mac);
		if (nrssi == -32) {
			for (i = 0; i < 4; ++i) {
				RF_WRITE(mac, 0x7b, i);
				DELAY(20);
				nrssi = bwi_nrssi_11g(mac);
				if (nrssi > -31 && rf7b == 0xffff)
					rf7b = i;
			}
			if (rf7b == 0xffff)
				rf7b = 3;
		} else {
			rf7b = 0;
		}
	}
	RF_WRITE(mac, 0x7b, rf7b);

	/*
	 * Restore saved RF/PHY registers
	 */
	if (phy->phy_rev >= 6) {
		for (phy6_idx = 0; phy6_idx < 4; ++phy6_idx) {
			PHY_WRITE(mac, save_phy6_regs[phy6_idx],
				  save_phy6[phy6_idx]);
		}
	}

	/* Saved PHY registers 0, 1, 2 are handled later */
	for (i = 3; i < SAVE_PHY_COMM_MAX; ++i)
		PHY_WRITE(mac, save_phy_comm_regs[i], save_phy_comm[i]);

	for (i = SAVE_RF_MAX - 1; i >= 0; --i)
		RF_WRITE(mac, save_rf_regs[i], save_rf[i]);

	PHY_SETBITS(mac, 0x802, 0x3);
	PHY_SETBITS(mac, 0x429, 0x8000);

	bwi_set_gains(mac, NULL);

	if (phy->phy_rev >= 6) {
		for (; phy6_idx < SAVE_PHY6_MAX; ++phy6_idx) {
			PHY_WRITE(mac, save_phy6_regs[phy6_idx],
				  save_phy6[phy6_idx]);
		}
	}

	PHY_WRITE(mac, save_phy_comm_regs[0], save_phy_comm[0]);
	PHY_WRITE(mac, save_phy_comm_regs[2], save_phy_comm[2]);
	PHY_WRITE(mac, save_phy_comm_regs[1], save_phy_comm[1]);

#undef SAVE_RF_MAX
#undef SAVE_PHY_COMM_MAX
#undef SAVE_PHY6_MAX
}

static void
bwi_rf_calc_nrssi_slope_11g(struct bwi_mac *mac)
{
#define SAVE_RF_MAX		3
#define SAVE_PHY_COMM_MAX	4
#define SAVE_PHY3_MAX		8

	static const uint16_t save_rf_regs[SAVE_RF_MAX] =
	{ 0x7a, 0x52, 0x43 };
	static const uint16_t save_phy_comm_regs[SAVE_PHY_COMM_MAX] =
	{ 0x15, 0x5a, 0x59, 0x58 };
	static const uint16_t save_phy3_regs[SAVE_PHY3_MAX] = {
		0x002e, 0x002f, 0x080f, 0x0810,
		0x0801, 0x0060, 0x0014, 0x0478
	};

	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	uint16_t save_rf[SAVE_RF_MAX];
	uint16_t save_phy_comm[SAVE_PHY_COMM_MAX];
	uint16_t save_phy3[SAVE_PHY3_MAX];
	uint16_t ant_div, bbp_atten, chan_ex;
	struct bwi_gains gains;
	int16_t nrssi[2];
	int i, phy3_idx = 0;

	if (rf->rf_rev >= 9)
		return;
	else if (rf->rf_rev == 8)
		bwi_rf_set_nrssi_ofs_11g(mac);

	PHY_CLRBITS(mac, 0x429, 0x8000);
	PHY_CLRBITS(mac, 0x802, 0x3);

	/*
	 * Save RF/PHY registers for later restoration
	 */
	ant_div = CSR_READ_2(sc, BWI_RF_ANTDIV);
	CSR_SETBITS_2(sc, BWI_RF_ANTDIV, 0x8000);

	for (i = 0; i < SAVE_RF_MAX; ++i)
		save_rf[i] = RF_READ(mac, save_rf_regs[i]);
	for (i = 0; i < SAVE_PHY_COMM_MAX; ++i)
		save_phy_comm[i] = PHY_READ(mac, save_phy_comm_regs[i]);

	bbp_atten = CSR_READ_2(sc, BWI_BBP_ATTEN);
	chan_ex = CSR_READ_2(sc, BWI_RF_CHAN_EX);

	if (phy->phy_rev >= 3) {
		for (i = 0; i < SAVE_PHY3_MAX; ++i)
			save_phy3[i] = PHY_READ(mac, save_phy3_regs[i]);

		PHY_WRITE(mac, 0x2e, 0);
		PHY_WRITE(mac, 0x810, 0);

		if (phy->phy_rev == 4 || phy->phy_rev == 6 ||
		    phy->phy_rev == 7) {
			PHY_SETBITS(mac, 0x478, 0x100);
			PHY_SETBITS(mac, 0x810, 0x40);
		} else if (phy->phy_rev == 3 || phy->phy_rev == 5) {
			PHY_CLRBITS(mac, 0x810, 0x40);
		}

		PHY_SETBITS(mac, 0x60, 0x40);
		PHY_SETBITS(mac, 0x14, 0x200);
	}

	/*
	 * Calculate nrssi0
	 */
	RF_SETBITS(mac, 0x7a, 0x70);

	bzero(&gains, sizeof(gains));
	gains.tbl_gain1 = 0;
	gains.tbl_gain2 = 8;
	gains.phy_gain = 0;
	bwi_set_gains(mac, &gains);

	RF_CLRBITS(mac, 0x7a, 0xff08);
	if (phy->phy_rev >= 2) {
		PHY_FILT_SETBITS(mac, 0x811, 0xffcf, 0x30);
		PHY_FILT_SETBITS(mac, 0x812, 0xffcf, 0x10);
	}

	RF_SETBITS(mac, 0x7a, 0x80);
	DELAY(20);
	nrssi[0] = bwi_nrssi_11g(mac);

	/*
	 * Calculate nrssi1
	 */
	RF_CLRBITS(mac, 0x7a, 0xff80);
	if (phy->phy_version >= 2)
		PHY_FILT_SETBITS(mac, 0x3, 0xff9f, 0x40);
	CSR_SETBITS_2(sc, BWI_RF_CHAN_EX, 0x2000);

	RF_SETBITS(mac, 0x7a, 0xf);
	PHY_WRITE(mac, 0x15, 0xf330);
	if (phy->phy_rev >= 2) {
		PHY_FILT_SETBITS(mac, 0x812, 0xffcf, 0x20);
		PHY_FILT_SETBITS(mac, 0x811, 0xffcf, 0x20);
	}

	bzero(&gains, sizeof(gains));
	gains.tbl_gain1 = 3;
	gains.tbl_gain2 = 0;
	gains.phy_gain = 1;
	bwi_set_gains(mac, &gains);

	if (rf->rf_rev == 8) {
		RF_WRITE(mac, 0x43, 0x1f);
	} else {
		RF_FILT_SETBITS(mac, 0x52, 0xff0f, 0x60);
		RF_FILT_SETBITS(mac, 0x43, 0xfff0, 0x9);
	}
	PHY_WRITE(mac, 0x5a, 0x480);
	PHY_WRITE(mac, 0x59, 0x810);
	PHY_WRITE(mac, 0x58, 0xd);
	DELAY(20);

	nrssi[1] = bwi_nrssi_11g(mac);

	/*
	 * Install calculated narrow RSSI values
	 */
	if (nrssi[1] == nrssi[0])
		rf->rf_nrssi_slope = 0x10000;
	else
		rf->rf_nrssi_slope = 0x400000 / (nrssi[0] - nrssi[1]);
	if (nrssi[0] >= -4) {
		rf->rf_nrssi[0] = nrssi[1];
		rf->rf_nrssi[1] = nrssi[0];
	}

	/*
	 * Restore saved RF/PHY registers
	 */
	if (phy->phy_rev >= 3) {
		for (phy3_idx = 0; phy3_idx < 4; ++phy3_idx) {
			PHY_WRITE(mac, save_phy3_regs[phy3_idx],
				  save_phy3[phy3_idx]);
		}
	}
	if (phy->phy_rev >= 2) {
		PHY_CLRBITS(mac, 0x812, 0x30);
		PHY_CLRBITS(mac, 0x811, 0x30);
	}

	for (i = 0; i < SAVE_RF_MAX; ++i)
		RF_WRITE(mac, save_rf_regs[i], save_rf[i]);

	CSR_WRITE_2(sc, BWI_RF_ANTDIV, ant_div);
	CSR_WRITE_2(sc, BWI_BBP_ATTEN, bbp_atten);
	CSR_WRITE_2(sc, BWI_RF_CHAN_EX, chan_ex);

	for (i = 0; i < SAVE_PHY_COMM_MAX; ++i)
		PHY_WRITE(mac, save_phy_comm_regs[i], save_phy_comm[i]);

	bwi_rf_work_around(mac, rf->rf_curchan);
	PHY_SETBITS(mac, 0x802, 0x3);
	bwi_set_gains(mac, NULL);
	PHY_SETBITS(mac, 0x429, 0x8000);

	if (phy->phy_rev >= 3) {
		for (; phy3_idx < SAVE_PHY3_MAX; ++phy3_idx) {
			PHY_WRITE(mac, save_phy3_regs[phy3_idx],
				  save_phy3[phy3_idx]);
		}
	}

	bwi_rf_init_sw_nrssi_table(mac);
	bwi_rf_set_nrssi_thr_11g(mac);

#undef SAVE_RF_MAX
#undef SAVE_PHY_COMM_MAX
#undef SAVE_PHY3_MAX
}

static void
bwi_rf_init_sw_nrssi_table(struct bwi_mac *mac)
{
	struct bwi_rf *rf = &mac->mac_rf;
	int d, i;

	d = 0x1f - rf->rf_nrssi[0];
	for (i = 0; i < BWI_NRSSI_TBLSZ; ++i) {
		int val;

		val = (((i - d) * rf->rf_nrssi_slope) / 0x10000) + 0x3a;
		if (val < 0)
			val = 0;
		else if (val > 0x3f)
			val = 0x3f;

		rf->rf_nrssi_table[i] = val;
	}
}

void
bwi_rf_init_hw_nrssi_table(struct bwi_mac *mac, uint16_t adjust)
{
	int i;

	for (i = 0; i < BWI_NRSSI_TBLSZ; ++i) {
		int16_t val;

		val = bwi_nrssi_read(mac, i);

		val -= adjust;
		if (val < -32)
			val = -32;
		else if (val > 31)
			val = 31;

		bwi_nrssi_write(mac, i, val);
	}
}

static void
bwi_rf_set_nrssi_thr_11b(struct bwi_mac *mac)
{
	struct bwi_rf *rf = &mac->mac_rf;
	int32_t thr;

	if (rf->rf_type != BWI_RF_T_BCM2050 ||
	    (mac->mac_sc->sc_card_flags & BWI_CARD_F_SW_NRSSI) == 0)
		return;

	/*
	 * Calculate nrssi threshold
	 */
	if (rf->rf_rev >= 6) {
		thr = (rf->rf_nrssi[1] - rf->rf_nrssi[0]) * 32;
		thr += 20 * (rf->rf_nrssi[0] + 1);
		thr /= 40;
	} else {
		thr = rf->rf_nrssi[1] - 5;
	}
	if (thr < 0)
		thr = 0;
	else if (thr > 0x3e)
		thr = 0x3e;

	PHY_READ(mac, BWI_PHYR_NRSSI_THR_11B);	/* dummy read */
	PHY_WRITE(mac, BWI_PHYR_NRSSI_THR_11B, (((uint16_t)thr) << 8) | 0x1c);

	if (rf->rf_rev >= 6) {
		PHY_WRITE(mac, 0x87, 0xe0d);
		PHY_WRITE(mac, 0x86, 0xc0b);
		PHY_WRITE(mac, 0x85, 0xa09);
		PHY_WRITE(mac, 0x84, 0x808);
		PHY_WRITE(mac, 0x83, 0x808);
		PHY_WRITE(mac, 0x82, 0x604);
		PHY_WRITE(mac, 0x81, 0x302);
		PHY_WRITE(mac, 0x80, 0x100);
	}
}

static __inline int32_t
_nrssi_threshold(const struct bwi_rf *rf, int32_t val)
{
	val *= (rf->rf_nrssi[1] - rf->rf_nrssi[0]);
	val += (rf->rf_nrssi[0] << 6);
	if (val < 32)
		val += 31;
	else
		val += 32;
	val >>= 6;
	if (val < -31)
		val = -31;
	else if (val > 31)
		val = 31;
	return val;
}

static void
bwi_rf_set_nrssi_thr_11g(struct bwi_mac *mac)
{
	int32_t thr1, thr2;
	uint16_t thr;

	/*
	 * Find the two nrssi thresholds
	 */
	if ((mac->mac_phy.phy_flags & BWI_PHY_F_LINKED) == 0 ||
	    (mac->mac_sc->sc_card_flags & BWI_CARD_F_SW_NRSSI) == 0) {
	    	int16_t nrssi;

		nrssi = bwi_nrssi_read(mac, 0x20);
		if (nrssi >= 32)
			nrssi -= 64;

		if (nrssi < 3) {
			thr1 = 0x2b;
			thr2 = 0x27;
		} else {
			thr1 = 0x2d;
			thr2 = 0x2b;
		}
	} else {
		/* TODO Interfere mode */
		thr1 = _nrssi_threshold(&mac->mac_rf, 0x11);
		thr2 = _nrssi_threshold(&mac->mac_rf, 0xe);
	}

#define NRSSI_THR1_MASK	__BITS(5, 0)
#define NRSSI_THR2_MASK	__BITS(11, 6)

	thr = __SHIFTIN((uint32_t)thr1, NRSSI_THR1_MASK) |
	      __SHIFTIN((uint32_t)thr2, NRSSI_THR2_MASK);
	PHY_FILT_SETBITS(mac, BWI_PHYR_NRSSI_THR_11G, 0xf000, thr);

#undef NRSSI_THR1_MASK
#undef NRSSI_THR2_MASK
}

void
bwi_rf_clear_tssi(struct bwi_mac *mac)
{
	/* XXX use function pointer */
	if (mac->mac_phy.phy_mode == IEEE80211_MODE_11A) {
		/* TODO:11A */
	} else {
		uint16_t val;
		int i;

		val = __SHIFTIN(BWI_INVALID_TSSI, BWI_LO_TSSI_MASK) |
		      __SHIFTIN(BWI_INVALID_TSSI, BWI_HI_TSSI_MASK);

		for (i = 0; i < 2; ++i) {
			MOBJ_WRITE_2(mac, BWI_COMM_MOBJ,
				BWI_COMM_MOBJ_TSSI_DS + (i * 2), val);
		}

		for (i = 0; i < 2; ++i) {
			MOBJ_WRITE_2(mac, BWI_COMM_MOBJ,
				BWI_COMM_MOBJ_TSSI_OFDM + (i * 2), val);
		}
	}
}

void
bwi_rf_clear_state(struct bwi_rf *rf)
{
	int i;

	rf->rf_flags &= ~BWI_RF_CLEAR_FLAGS;
	bzero(rf->rf_lo, sizeof(rf->rf_lo));
	bzero(rf->rf_lo_used, sizeof(rf->rf_lo_used));

	rf->rf_nrssi_slope = 0;
	rf->rf_nrssi[0] = BWI_INVALID_NRSSI;
	rf->rf_nrssi[1] = BWI_INVALID_NRSSI;

	for (i = 0; i < BWI_NRSSI_TBLSZ; ++i)
		rf->rf_nrssi_table[i] = i;

	rf->rf_lo_gain = 0;
	rf->rf_rx_gain = 0;

	bcopy(rf->rf_txpower_map0, rf->rf_txpower_map,
	      sizeof(rf->rf_txpower_map));
	rf->rf_idle_tssi = rf->rf_idle_tssi0;
}

static void
bwi_rf_on_11a(struct bwi_mac *mac)
{
	/* TODO:11A */
}

static void
bwi_rf_on_11bg(struct bwi_mac *mac)
{
	struct bwi_phy *phy = &mac->mac_phy;

	PHY_WRITE(mac, 0x15, 0x8000);
	PHY_WRITE(mac, 0x15, 0xcc00);
	if (phy->phy_flags & BWI_PHY_F_LINKED)
		PHY_WRITE(mac, 0x15, 0xc0);
	else
		PHY_WRITE(mac, 0x15, 0);

	bwi_rf_set_chan(mac, 6 /* XXX */, 1);
}

void
bwi_rf_set_ant_mode(struct bwi_mac *mac, int ant_mode)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	uint16_t val;

	KASSERT(ant_mode == BWI_ANT_MODE_0 ||
		ant_mode == BWI_ANT_MODE_1 ||
		ant_mode == BWI_ANT_MODE_AUTO, ("ant_mode %d", ant_mode));

	HFLAGS_CLRBITS(mac, BWI_HFLAG_AUTO_ANTDIV);

	if (phy->phy_mode == IEEE80211_MODE_11B) {
		/* NOTE: v4/v3 conflicts, take v3 */
		if (mac->mac_rev == 2)
			val = BWI_ANT_MODE_AUTO;
		else
			val = ant_mode;
		val <<= 7;
		PHY_FILT_SETBITS(mac, 0x3e2, 0xfe7f, val);
	} else {	/* 11a/g */
		/* XXX reg/value naming */
		val = ant_mode << 7;
		PHY_FILT_SETBITS(mac, 0x401, 0x7e7f, val);

		if (ant_mode == BWI_ANT_MODE_AUTO)
			PHY_CLRBITS(mac, 0x42b, 0x100);

		if (phy->phy_mode == IEEE80211_MODE_11A) {
			/* TODO:11A */
		} else {	/* 11g */
			if (ant_mode == BWI_ANT_MODE_AUTO)
				PHY_SETBITS(mac, 0x48c, 0x2000);
			else
				PHY_CLRBITS(mac, 0x48c, 0x2000);

			if (phy->phy_rev >= 2) {
				PHY_SETBITS(mac, 0x461, 0x10);
				PHY_FILT_SETBITS(mac, 0x4ad, 0xff00, 0x15);
				if (phy->phy_rev == 2) {
					PHY_WRITE(mac, 0x427, 0x8);
				} else {
					PHY_FILT_SETBITS(mac, 0x427,
							 0xff00, 0x8);
				}

				if (phy->phy_rev >= 6)
					PHY_WRITE(mac, 0x49b, 0xdc);
			}
		}
	}

	/* XXX v4 set AUTO_ANTDIV unconditionally */
	if (ant_mode == BWI_ANT_MODE_AUTO)
		HFLAGS_SETBITS(mac, BWI_HFLAG_AUTO_ANTDIV);

	val = ant_mode << 8;
	MOBJ_FILT_SETBITS_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_TX_BEACON,
			    0xfc3f, val);
	MOBJ_FILT_SETBITS_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_TX_ACK,
			    0xfc3f, val);
	MOBJ_FILT_SETBITS_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_TX_PROBE_RESP,
			    0xfc3f, val);

	/* XXX what's these */
	if (phy->phy_mode == IEEE80211_MODE_11B)
		CSR_SETBITS_2(sc, 0x5e, 0x4);

	CSR_WRITE_4(sc, 0x100, 0x1000000);
	if (mac->mac_rev < 5)
		CSR_WRITE_4(sc, 0x10c, 0x1000000);

	mac->mac_rf.rf_ant_mode = ant_mode;
}

int
bwi_rf_get_latest_tssi(struct bwi_mac *mac, int8_t tssi[], uint16_t ofs)
{
	int i;

	for (i = 0; i < 4; ) {
		uint16_t val;

		val = MOBJ_READ_2(mac, BWI_COMM_MOBJ, ofs + i);
		tssi[i++] = (int8_t)__SHIFTOUT(val, BWI_LO_TSSI_MASK);
		tssi[i++] = (int8_t)__SHIFTOUT(val, BWI_HI_TSSI_MASK);
	}

	for (i = 0; i < 4; ++i) {
		if (tssi[i] == BWI_INVALID_TSSI)
			return EINVAL;
	}
	return 0;
}

int
bwi_rf_tssi2dbm(struct bwi_mac *mac, int8_t tssi, int8_t *txpwr)
{
	struct bwi_rf *rf = &mac->mac_rf;
	int pwr_idx;

	pwr_idx = rf->rf_idle_tssi + (int)tssi - rf->rf_base_tssi;
#if 0
	if (pwr_idx < 0 || pwr_idx >= BWI_TSSI_MAX)
		return EINVAL;
#else
	if (pwr_idx < 0)
		pwr_idx = 0;
	else if (pwr_idx >= BWI_TSSI_MAX)
		pwr_idx = BWI_TSSI_MAX - 1;
#endif

	*txpwr = rf->rf_txpower_map[pwr_idx];
	return 0;
}

static int
bwi_rf_calc_rssi_bcm2050(struct bwi_mac *mac, const struct bwi_rxbuf_hdr *hdr)
{
	uint16_t flags1, flags3;
	int rssi, lna_gain;

	rssi = hdr->rxh_rssi;
	flags1 = le16toh(hdr->rxh_flags1);
	flags3 = le16toh(hdr->rxh_flags3);

	if (flags1 & BWI_RXH_F1_OFDM) {
		if (rssi > 127)
			rssi -= 256;
		if (flags3 & BWI_RXH_F3_BCM2050_RSSI)
			rssi += 17;
		else
			rssi -= 4;
		return rssi;
	}

	if (mac->mac_sc->sc_card_flags & BWI_CARD_F_SW_NRSSI) {
		struct bwi_rf *rf = &mac->mac_rf;

		if (rssi >= BWI_NRSSI_TBLSZ)
			rssi = BWI_NRSSI_TBLSZ - 1;

		rssi = ((31 - (int)rf->rf_nrssi_table[rssi]) * -131) / 128;
		rssi -= 67;
	} else {
		rssi = ((31 - rssi) * -149) / 128;
		rssi -= 68;
	}

	if (mac->mac_phy.phy_mode != IEEE80211_MODE_11G)
		return rssi;

	if (flags3 & BWI_RXH_F3_BCM2050_RSSI)
		rssi += 20;

	lna_gain = __SHIFTOUT(le16toh(hdr->rxh_phyinfo),
			      BWI_RXH_PHYINFO_LNAGAIN);
	DPRINTF(mac->mac_sc, BWI_DBG_RF | BWI_DBG_RX,
		"lna_gain %d, phyinfo 0x%04x\n",
		lna_gain, le16toh(hdr->rxh_phyinfo));
	switch (lna_gain) {
	case 0:
		rssi += 27;
		break;
	case 1:
		rssi += 6;
		break;
	case 2:
		rssi += 12;
		break;
	case 3:
		/*
		 * XXX
		 * According to v3 spec, we should do _nothing_ here,
		 * but it seems that the result RSSI will be too low
		 * (relative to what ath(4) says).  Raise it a little
		 * bit.
		 */
		rssi += 5;
		break;
	default:
		panic("impossible lna gain %d", lna_gain);
	}
	return rssi;
}

static int
bwi_rf_calc_rssi_bcm2053(struct bwi_mac *mac, const struct bwi_rxbuf_hdr *hdr)
{
	uint16_t flags1;
	int rssi;

	rssi = (((int)hdr->rxh_rssi - 11) * 103) / 64;

	flags1 = le16toh(hdr->rxh_flags1);
	if (flags1 & BWI_RXH_F1_BCM2053_RSSI)
		rssi -= 109;
	else
		rssi -= 83;
	return rssi;
}

static int
bwi_rf_calc_rssi_bcm2060(struct bwi_mac *mac, const struct bwi_rxbuf_hdr *hdr)
{
	int rssi;

	rssi = hdr->rxh_rssi;
	if (rssi > 127)
		rssi -= 256;
	return rssi;
}

static int
bwi_rf_calc_noise_bcm2050(struct bwi_mac *mac)
{
	uint16_t val;
	int noise;

	val = MOBJ_READ_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_RF_NOISE);
	noise = (int)val;	/* XXX check bounds? */

	if (mac->mac_sc->sc_card_flags & BWI_CARD_F_SW_NRSSI) {
		struct bwi_rf *rf = &mac->mac_rf;

		if (noise >= BWI_NRSSI_TBLSZ)
			noise = BWI_NRSSI_TBLSZ - 1;

		noise = ((31 - (int)rf->rf_nrssi_table[noise]) * -131) / 128;
		noise -= 67;
	} else {
		noise = ((31 - noise) * -149) / 128;
		noise -= 68;
	}
	return noise;
}

static int
bwi_rf_calc_noise_bcm2053(struct bwi_mac *mac)
{
	uint16_t val;
	int noise;

	val = MOBJ_READ_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_RF_NOISE);
	noise = (int)val;	/* XXX check bounds? */

	noise = ((noise - 11) * 103) / 64;
	noise -= 109;
	return noise;
}

static int
bwi_rf_calc_noise_bcm2060(struct bwi_mac *mac)
{
	/* XXX Dont know how to calc */
	return (BWI_NOISE_FLOOR);
}

static uint16_t
bwi_rf_lo_measure_11b(struct bwi_mac *mac)
{
	uint16_t val;
	int i;

	val = 0;
	for (i = 0; i < 10; ++i) {
		PHY_WRITE(mac, 0x15, 0xafa0);
		DELAY(1);
		PHY_WRITE(mac, 0x15, 0xefa0);
		DELAY(10);
		PHY_WRITE(mac, 0x15, 0xffa0);
		DELAY(40);

		val += PHY_READ(mac, 0x2c);
	}
	return val;
}

static void
bwi_rf_lo_update_11b(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	struct rf_saveregs regs;
	uint16_t rf_val, phy_val, min_val, val;
	uint16_t rf52, bphy_ctrl;
	int i;

	DPRINTF(sc, BWI_DBG_RF | BWI_DBG_INIT, "%s enter\n", __func__);

	bzero(&regs, sizeof(regs));
	bphy_ctrl = 0;

	/*
	 * Save RF/PHY registers for later restoration
	 */
	SAVE_PHY_REG(mac, &regs, 15);
	rf52 = RF_READ(mac, 0x52) & 0xfff0;
	if (rf->rf_type == BWI_RF_T_BCM2050) {
		SAVE_PHY_REG(mac, &regs, 0a);
		SAVE_PHY_REG(mac, &regs, 2a);
		SAVE_PHY_REG(mac, &regs, 35);
		SAVE_PHY_REG(mac, &regs, 03);
		SAVE_PHY_REG(mac, &regs, 01);
		SAVE_PHY_REG(mac, &regs, 30);

		SAVE_RF_REG(mac, &regs, 43);
		SAVE_RF_REG(mac, &regs, 7a);

		bphy_ctrl = CSR_READ_2(sc, BWI_BPHY_CTRL);

		SAVE_RF_REG(mac, &regs, 52);
		regs.rf_52 &= 0xf0;

		PHY_WRITE(mac, 0x30, 0xff);
		CSR_WRITE_2(sc, BWI_PHY_CTRL, 0x3f3f);
		PHY_WRITE(mac, 0x35, regs.phy_35 & 0xff7f);
		RF_WRITE(mac, 0x7a, regs.rf_7a & 0xfff0);
	}

	PHY_WRITE(mac, 0x15, 0xb000);

	if (rf->rf_type == BWI_RF_T_BCM2050) {
		PHY_WRITE(mac, 0x2b, 0x203);
		PHY_WRITE(mac, 0x2a, 0x8a3);
	} else {
		PHY_WRITE(mac, 0x2b, 0x1402);
	}

	/*
	 * Setup RF signal
	 */
	rf_val = 0;
	min_val = UINT16_MAX;

	for (i = 0; i < 4; ++i) {
		RF_WRITE(mac, 0x52, rf52 | i);
		bwi_rf_lo_measure_11b(mac);	/* Ignore return value */
	}
	for (i = 0; i < 10; ++i) {
		RF_WRITE(mac, 0x52, rf52 | i);

		val = bwi_rf_lo_measure_11b(mac) / 10;
		if (val < min_val) {
			min_val = val;
			rf_val = i;
		}
	}
	RF_WRITE(mac, 0x52, rf52 | rf_val);

	/*
	 * Setup PHY signal
	 */
	phy_val = 0;
	min_val = UINT16_MAX;

	for (i = -4; i < 5; i += 2) {
		int j;

		for (j = -4; j < 5; j += 2) {
			uint16_t phy2f;

			phy2f = (0x100 * i) + j;
			if (j < 0)
				phy2f += 0x100;
			PHY_WRITE(mac, 0x2f, phy2f);

			val = bwi_rf_lo_measure_11b(mac) / 10;
			if (val < min_val) {
				min_val = val;
				phy_val = phy2f;
			}
		}
	}
	PHY_WRITE(mac, 0x2f, phy_val + 0x101);

	/*
	 * Restore saved RF/PHY registers
	 */
	if (rf->rf_type == BWI_RF_T_BCM2050) {
		RESTORE_PHY_REG(mac, &regs, 0a);
		RESTORE_PHY_REG(mac, &regs, 2a);
		RESTORE_PHY_REG(mac, &regs, 35);
		RESTORE_PHY_REG(mac, &regs, 03);
		RESTORE_PHY_REG(mac, &regs, 01);
		RESTORE_PHY_REG(mac, &regs, 30);

		RESTORE_RF_REG(mac, &regs, 43);
		RESTORE_RF_REG(mac, &regs, 7a);

		RF_FILT_SETBITS(mac, 0x52, 0xf, regs.rf_52);

		CSR_WRITE_2(sc, BWI_BPHY_CTRL, bphy_ctrl);
	}
	RESTORE_PHY_REG(mac, &regs, 15);

	bwi_rf_work_around(mac, rf->rf_curchan);
}
