/*-
 * Copyright (c) 2009-2010 Weongyo Jeong <weongyo@freebsd.org>
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

#include <dev/bwn/if_bwnreg.h>
#include <dev/bwn/if_bwnvar.h>

#include <dev/bwn/if_bwn_debug.h>
#include <dev/bwn/if_bwn_misc.h>
#include <dev/bwn/if_bwn_phy_g.h>

#include "bhnd_nvram_map.h"

static void	bwn_phy_g_init_sub(struct bwn_mac *);
static uint8_t	bwn_has_hwpctl(struct bwn_mac *);
static void	bwn_phy_init_b5(struct bwn_mac *);
static void	bwn_phy_init_b6(struct bwn_mac *);
static void	bwn_phy_init_a(struct bwn_mac *);
static void	bwn_loopback_calcgain(struct bwn_mac *);
static uint16_t	bwn_rf_init_bcm2050(struct bwn_mac *);
static void	bwn_lo_g_init(struct bwn_mac *);
static void	bwn_lo_g_adjust(struct bwn_mac *);
static void	bwn_lo_get_powervector(struct bwn_mac *);
static struct bwn_lo_calib *bwn_lo_calibset(struct bwn_mac *,
		    const struct bwn_bbatt *, const struct bwn_rfatt *);
static void	bwn_lo_write(struct bwn_mac *, struct bwn_loctl *);
static void	bwn_phy_hwpctl_init(struct bwn_mac *);
static void	bwn_phy_g_switch_chan(struct bwn_mac *, int, uint8_t);
static void	bwn_phy_g_set_txpwr_sub(struct bwn_mac *,
		    const struct bwn_bbatt *, const struct bwn_rfatt *,
		    uint8_t);
static void	bwn_phy_g_set_bbatt(struct bwn_mac *, uint16_t);
static uint16_t	bwn_rf_2050_rfoverval(struct bwn_mac *, uint16_t, uint32_t);
static void	bwn_spu_workaround(struct bwn_mac *, uint8_t);
static void	bwn_wa_init(struct bwn_mac *);
static void	bwn_ofdmtab_write_2(struct bwn_mac *, uint16_t, uint16_t,
		    uint16_t);
static void	bwn_ofdmtab_write_4(struct bwn_mac *, uint16_t, uint16_t,
		    uint32_t);
static void	bwn_gtab_write(struct bwn_mac *, uint16_t, uint16_t,
		    uint16_t);
static int16_t	bwn_nrssi_read(struct bwn_mac *, uint16_t);
static void	bwn_nrssi_offset(struct bwn_mac *);
static void	bwn_nrssi_threshold(struct bwn_mac *);
static void	bwn_nrssi_slope_11g(struct bwn_mac *);
static void	bwn_set_all_gains(struct bwn_mac *, int16_t, int16_t,
		    int16_t);
static void	bwn_set_original_gains(struct bwn_mac *);
static void	bwn_hwpctl_early_init(struct bwn_mac *);
static void	bwn_hwpctl_init_gphy(struct bwn_mac *);
static uint16_t	bwn_phy_g_chan2freq(uint8_t);
static void	bwn_phy_g_dc_lookup_init(struct bwn_mac *, uint8_t);

/* Stuff we need */

static uint16_t	bwn_phy_g_txctl(struct bwn_mac *mac);
static int	bwn_phy_shm_tssi_read(struct bwn_mac *mac, uint16_t shm_offset);
static void	bwn_phy_g_setatt(struct bwn_mac *mac, int *bbattp, int *rfattp);
static void	bwn_phy_lock(struct bwn_mac *mac);
static void	bwn_phy_unlock(struct bwn_mac *mac);
static void	bwn_rf_lock(struct bwn_mac *mac);
static void	bwn_rf_unlock(struct bwn_mac *mac);

static const uint16_t bwn_tab_noise_g1[] = BWN_TAB_NOISE_G1;
static const uint16_t bwn_tab_noise_g2[] = BWN_TAB_NOISE_G2;
static const uint16_t bwn_tab_noisescale_g1[] = BWN_TAB_NOISESCALE_G1;
static const uint16_t bwn_tab_noisescale_g2[] = BWN_TAB_NOISESCALE_G2;
static const uint16_t bwn_tab_noisescale_g3[] = BWN_TAB_NOISESCALE_G3;
const uint8_t bwn_bitrev_table[256] = BWN_BITREV_TABLE;

static uint8_t
bwn_has_hwpctl(struct bwn_mac *mac)
{

	if (mac->mac_phy.hwpctl == 0 || mac->mac_phy.use_hwpctl == NULL)
		return (0);
	return (mac->mac_phy.use_hwpctl(mac));
}

int
bwn_phy_g_attach(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	unsigned int i;
	int16_t pab0, pab1, pab2;
	static int8_t bwn_phy_g_tssi2dbm_table[] = BWN_PHY_G_TSSI2DBM_TABLE;
	int8_t bg;
	int error;

	/* Fetch SPROM configuration */
#define	BWN_PHY_G_READVAR(_dev, _type, _name, _result)		\
do {									\
	error = bhnd_nvram_getvar_ ##_type((_dev), (_name), (_result));	\
	if (error) {							\
		device_printf((_dev), "NVRAM variable %s unreadable: "	\
		    "%d\n", (_name), error);				\
		return (error);						\
	}								\
} while(0)

	BWN_PHY_G_READVAR(sc->sc_dev, int8, BHND_NVAR_PA0ITSSIT, &bg);
	BWN_PHY_G_READVAR(sc->sc_dev, int16, BHND_NVAR_PA0B0, &pab0);
	BWN_PHY_G_READVAR(sc->sc_dev, int16, BHND_NVAR_PA0B1, &pab1);
	BWN_PHY_G_READVAR(sc->sc_dev, int16, BHND_NVAR_PA0B2, &pab2);
	BWN_PHY_G_READVAR(sc->sc_dev, int16, BHND_NVAR_PA0MAXPWR,
	    &pg->pg_pa0maxpwr);

#undef	BWN_PHY_G_READVAR

	pg->pg_flags = 0;
	if (pab0 == 0 || pab1 == 0 || pab2 == 0 || pab0 == -1 || pab1 == -1 ||
	    pab2 == -1) {
		pg->pg_idletssi = 52;
		pg->pg_tssi2dbm = bwn_phy_g_tssi2dbm_table;
		return (0);
	}

	pg->pg_idletssi = (bg == 0 || bg == -1) ? 62 : bg;
	pg->pg_tssi2dbm = (uint8_t *)malloc(64, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (pg->pg_tssi2dbm == NULL) {
		device_printf(sc->sc_dev, "failed to allocate buffer\n");
		return (ENOMEM);
	}
	for (i = 0; i < 64; i++) {
		int32_t m1, m2, f, q, delta;
		int8_t j = 0;

		m1 = BWN_TSSI2DBM(16 * pab0 + i * pab1, 32);
		m2 = MAX(BWN_TSSI2DBM(32768 + i * pab2, 256), 1);
		f = 256;

		do {
			if (j > 15) {
				device_printf(sc->sc_dev,
				    "failed to generate tssi2dBm\n");
				free(pg->pg_tssi2dbm, M_DEVBUF);
				return (ENOMEM);
			}
			q = BWN_TSSI2DBM(f * 4096 - BWN_TSSI2DBM(m2 * f, 16) *
			    f, 2048);
			delta = abs(q - f);
			f = q;
			j++;
		} while (delta >= 2);

		pg->pg_tssi2dbm[i] = MIN(MAX(BWN_TSSI2DBM(m1 * f, 8192), -127),
		    127);
	}

	pg->pg_flags |= BWN_PHY_G_FLAG_TSSITABLE_ALLOC;
	return (0);
}

void
bwn_phy_g_detach(struct bwn_mac *mac)
{
	struct bwn_phy_g *pg = &mac->mac_phy.phy_g;

	if (pg->pg_flags & BWN_PHY_G_FLAG_TSSITABLE_ALLOC) {
		free(pg->pg_tssi2dbm, M_DEVBUF);
		pg->pg_tssi2dbm = NULL;
	}
	pg->pg_flags = 0;
}

void
bwn_phy_g_init_pre(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	void *tssi2dbm;
	int idletssi;
	unsigned int i;

	tssi2dbm = pg->pg_tssi2dbm;
	idletssi = pg->pg_idletssi;

	memset(pg, 0, sizeof(*pg));

	pg->pg_tssi2dbm = tssi2dbm;
	pg->pg_idletssi = idletssi;

	memset(pg->pg_minlowsig, 0xff, sizeof(pg->pg_minlowsig));

	for (i = 0; i < N(pg->pg_nrssi); i++)
		pg->pg_nrssi[i] = -1000;
	for (i = 0; i < N(pg->pg_nrssi_lt); i++)
		pg->pg_nrssi_lt[i] = i;
	pg->pg_lofcal = 0xffff;
	pg->pg_initval = 0xffff;
	pg->pg_immode = BWN_IMMODE_NONE;
	pg->pg_ofdmtab_dir = BWN_OFDMTAB_DIR_UNKNOWN;
	pg->pg_avgtssi = 0xff;

	pg->pg_loctl.tx_bias = 0xff;
	TAILQ_INIT(&pg->pg_loctl.calib_list);
}

int
bwn_phy_g_prepare_hw(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_txpwr_loctl *lo = &pg->pg_loctl;
	static const struct bwn_rfatt rfatt0[] = {
		{ 3, 0 }, { 1, 0 }, { 5, 0 }, { 7, 0 },	{ 9, 0 }, { 2, 0 },
		{ 0, 0 }, { 4, 0 }, { 6, 0 }, { 8, 0 }, { 1, 1 }, { 2, 1 },
		{ 3, 1 }, { 4, 1 }
	};
	static const struct bwn_rfatt rfatt1[] = {
		{ 2, 1 }, { 4, 1 }, { 6, 1 }, { 8, 1 }, { 10, 1 }, { 12, 1 },
		{ 14, 1 }
	};
	static const struct bwn_rfatt rfatt2[] = {
		{ 0, 1 }, { 2, 1 }, { 4, 1 }, { 6, 1 }, { 8, 1 }, { 9, 1 },
		{ 9, 1 }
	};
	static const struct bwn_bbatt bbatt_0[] = {
		{ 0 }, { 1 }, { 2 }, { 3 }, { 4 }, { 5 }, { 6 }, { 7 }, { 8 }
	};

	KASSERT(phy->type == BWN_PHYTYPE_G, ("%s fail", __func__));

	if (phy->rf_ver == 0x2050 && phy->rf_rev < 6)
		pg->pg_bbatt.att = 0;
	else
		pg->pg_bbatt.att = 2;

	/* prepare Radio Attenuation */
	pg->pg_rfatt.padmix = 0;

	if (sc->sc_board_info.board_vendor == PCI_VENDOR_BROADCOM &&
	    sc->sc_board_info.board_type == BHND_BOARD_BCM94309G) {
		if (sc->sc_board_info.board_rev < 0x43) {
			pg->pg_rfatt.att = 2;
			goto done;
		} else if (sc->sc_board_info.board_rev < 0x51) {
			pg->pg_rfatt.att = 3;
			goto done;
		}
	}

	if (phy->type == BWN_PHYTYPE_A) {
		pg->pg_rfatt.att = 0x60;
		goto done;
	}

	switch (phy->rf_ver) {
	case 0x2050:
		switch (phy->rf_rev) {
		case 0:
			pg->pg_rfatt.att = 5;
			goto done;
		case 1:
			if (phy->type == BWN_PHYTYPE_G) {
				if (sc->sc_board_info.board_vendor ==
				    PCI_VENDOR_BROADCOM &&
				    sc->sc_board_info.board_type ==
				    BHND_BOARD_BCM94309G &&
				    sc->sc_board_info.board_rev >= 30)
					pg->pg_rfatt.att = 3;
				else if (sc->sc_board_info.board_vendor ==
				    PCI_VENDOR_BROADCOM &&
				    sc->sc_board_info.board_type ==
				    BHND_BOARD_BU4306)
					pg->pg_rfatt.att = 3;
				else
					pg->pg_rfatt.att = 1;
			} else {
				if (sc->sc_board_info.board_vendor ==
				    PCI_VENDOR_BROADCOM &&
				    sc->sc_board_info.board_type ==
				    BHND_BOARD_BCM94309G &&
				    sc->sc_board_info.board_rev >= 30)
					pg->pg_rfatt.att = 7;
				else
					pg->pg_rfatt.att = 6;
			}
			goto done;
		case 2:
			if (phy->type == BWN_PHYTYPE_G) {
				if (sc->sc_board_info.board_vendor ==
				    PCI_VENDOR_BROADCOM &&
				    sc->sc_board_info.board_type ==
				    BHND_BOARD_BCM94309G &&
				    sc->sc_board_info.board_rev >= 30)
					pg->pg_rfatt.att = 3;
				else if (sc->sc_board_info.board_vendor ==
				    PCI_VENDOR_BROADCOM &&
				    sc->sc_board_info.board_type ==
				    BHND_BOARD_BU4306)
					pg->pg_rfatt.att = 5;
				else if (sc->sc_cid.chip_id ==
				    BHND_CHIPID_BCM4320)
					pg->pg_rfatt.att = 4;
				else
					pg->pg_rfatt.att = 3;
			} else
				pg->pg_rfatt.att = 6;
			goto done;
		case 3:
			pg->pg_rfatt.att = 5;
			goto done;
		case 4:
		case 5:
			pg->pg_rfatt.att = 1;
			goto done;
		case 6:
		case 7:
			pg->pg_rfatt.att = 5;
			goto done;
		case 8:
			pg->pg_rfatt.att = 0xa;
			pg->pg_rfatt.padmix = 1;
			goto done;
		case 9:
		default:
			pg->pg_rfatt.att = 5;
			goto done;
		}
		break;
	case 0x2053:
		switch (phy->rf_rev) {
		case 1:
			pg->pg_rfatt.att = 6;
			goto done;
		}
		break;
	}
	pg->pg_rfatt.att = 5;
done:
	pg->pg_txctl = (bwn_phy_g_txctl(mac) << 4);

	if (!bwn_has_hwpctl(mac)) {
		lo->rfatt.array = rfatt0;
		lo->rfatt.len = N(rfatt0);
		lo->rfatt.min = 0;
		lo->rfatt.max = 9;
		goto genbbatt;
	}
	if (phy->rf_ver == 0x2050 && phy->rf_rev == 8) {
		lo->rfatt.array = rfatt1;
		lo->rfatt.len = N(rfatt1);
		lo->rfatt.min = 0;
		lo->rfatt.max = 14;
		goto genbbatt;
	}
	lo->rfatt.array = rfatt2;
	lo->rfatt.len = N(rfatt2);
	lo->rfatt.min = 0;
	lo->rfatt.max = 9;
genbbatt:
	lo->bbatt.array = bbatt_0;
	lo->bbatt.len = N(bbatt_0);
	lo->bbatt.min = 0;
	lo->bbatt.max = 8;

	BWN_READ_4(mac, BWN_MACCTL);
	if (phy->rev == 1) {
		phy->gmode = 0;
		bwn_reset_core(mac, 0);
		bwn_phy_g_init_sub(mac);
		phy->gmode = 1;
		bwn_reset_core(mac, 1);
	}
	return (0);
}

static uint16_t
bwn_phy_g_txctl(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;

	if (phy->rf_ver != 0x2050)
		return (0);
	if (phy->rf_rev == 1)
		return (BWN_TXCTL_PA2DB | BWN_TXCTL_TXMIX);
	if (phy->rf_rev < 6)
		return (BWN_TXCTL_PA2DB);
	if (phy->rf_rev == 8)
		return (BWN_TXCTL_TXMIX);
	return (0);
}

int
bwn_phy_g_init(struct bwn_mac *mac)
{

	bwn_phy_g_init_sub(mac);
	return (0);
}

void
bwn_phy_g_exit(struct bwn_mac *mac)
{
	struct bwn_txpwr_loctl *lo = &mac->mac_phy.phy_g.pg_loctl;
	struct bwn_lo_calib *cal, *tmp;

	if (lo == NULL)
		return;
	TAILQ_FOREACH_SAFE(cal, &lo->calib_list, list, tmp) {
		TAILQ_REMOVE(&lo->calib_list, cal, list);
		free(cal, M_DEVBUF);
	}
}

uint16_t
bwn_phy_g_read(struct bwn_mac *mac, uint16_t reg)
{

	BWN_WRITE_2(mac, BWN_PHYCTL, reg);
	return (BWN_READ_2(mac, BWN_PHYDATA));
}

void
bwn_phy_g_write(struct bwn_mac *mac, uint16_t reg, uint16_t value)
{

	BWN_WRITE_2(mac, BWN_PHYCTL, reg);
	BWN_WRITE_2(mac, BWN_PHYDATA, value);
}

uint16_t
bwn_phy_g_rf_read(struct bwn_mac *mac, uint16_t reg)
{

	KASSERT(reg != 1, ("%s:%d: fail", __func__, __LINE__));
	BWN_WRITE_2(mac, BWN_RFCTL, reg | 0x80);
	return (BWN_READ_2(mac, BWN_RFDATALO));
}

void
bwn_phy_g_rf_write(struct bwn_mac *mac, uint16_t reg, uint16_t value)
{

	KASSERT(reg != 1, ("%s:%d: fail", __func__, __LINE__));
	BWN_WRITE_2(mac, BWN_RFCTL, reg);
	BWN_WRITE_2(mac, BWN_RFDATALO, value);
}

int
bwn_phy_g_hwpctl(struct bwn_mac *mac)
{

	return (mac->mac_phy.rev >= 6);
}

void
bwn_phy_g_rf_onoff(struct bwn_mac *mac, int on)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	unsigned int channel;
	uint16_t rfover, rfoverval;

	if (on) {
		if (phy->rf_on)
			return;

		BWN_PHY_WRITE(mac, 0x15, 0x8000);
		BWN_PHY_WRITE(mac, 0x15, 0xcc00);
		BWN_PHY_WRITE(mac, 0x15, (phy->gmode ? 0xc0 : 0x0));
		if (pg->pg_flags & BWN_PHY_G_FLAG_RADIOCTX_VALID) {
			BWN_PHY_WRITE(mac, BWN_PHY_RFOVER,
			    pg->pg_radioctx_over);
			BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL,
			    pg->pg_radioctx_overval);
			pg->pg_flags &= ~BWN_PHY_G_FLAG_RADIOCTX_VALID;
		}
		channel = phy->chan;
		bwn_phy_g_switch_chan(mac, 6, 1);
		bwn_phy_g_switch_chan(mac, channel, 0);
		return;
	}

	rfover = BWN_PHY_READ(mac, BWN_PHY_RFOVER);
	rfoverval = BWN_PHY_READ(mac, BWN_PHY_RFOVERVAL);
	pg->pg_radioctx_over = rfover;
	pg->pg_radioctx_overval = rfoverval;
	pg->pg_flags |= BWN_PHY_G_FLAG_RADIOCTX_VALID;
	BWN_PHY_WRITE(mac, BWN_PHY_RFOVER, rfover | 0x008c);
	BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL, rfoverval & 0xff73);
}

int
bwn_phy_g_switch_channel(struct bwn_mac *mac, uint32_t newchan)
{

	if ((newchan < 1) || (newchan > 14))
		return (EINVAL);
	bwn_phy_g_switch_chan(mac, newchan, 0);

	return (0);
}

uint32_t
bwn_phy_g_get_default_chan(struct bwn_mac *mac)
{

	return (1);
}

void
bwn_phy_g_set_antenna(struct bwn_mac *mac, int antenna)
{
	struct bwn_phy *phy = &mac->mac_phy;
	uint64_t hf;
	int autodiv = 0;
	uint16_t tmp;

	if (antenna == BWN_ANTAUTO0 || antenna == BWN_ANTAUTO1)
		autodiv = 1;

	hf = bwn_hf_read(mac) & ~BWN_HF_UCODE_ANTDIV_HELPER;
	bwn_hf_write(mac, hf);

	BWN_PHY_WRITE(mac, BWN_PHY_BBANDCFG,
	    (BWN_PHY_READ(mac, BWN_PHY_BBANDCFG) & ~BWN_PHY_BBANDCFG_RXANT) |
	    ((autodiv ? BWN_ANTAUTO1 : antenna)
		<< BWN_PHY_BBANDCFG_RXANT_SHIFT));

	if (autodiv) {
		tmp = BWN_PHY_READ(mac, BWN_PHY_ANTDWELL);
		if (antenna == BWN_ANTAUTO1)
			tmp &= ~BWN_PHY_ANTDWELL_AUTODIV1;
		else
			tmp |= BWN_PHY_ANTDWELL_AUTODIV1;
		BWN_PHY_WRITE(mac, BWN_PHY_ANTDWELL, tmp);
	}
	tmp = BWN_PHY_READ(mac, BWN_PHY_ANTWRSETT);
	if (autodiv)
		tmp |= BWN_PHY_ANTWRSETT_ARXDIV;
	else
		tmp &= ~BWN_PHY_ANTWRSETT_ARXDIV;
	BWN_PHY_WRITE(mac, BWN_PHY_ANTWRSETT, tmp);
	if (phy->rev >= 2) {
		BWN_PHY_WRITE(mac, BWN_PHY_OFDM61,
		    BWN_PHY_READ(mac, BWN_PHY_OFDM61) | BWN_PHY_OFDM61_10);
		BWN_PHY_WRITE(mac, BWN_PHY_DIVSRCHGAINBACK,
		    (BWN_PHY_READ(mac, BWN_PHY_DIVSRCHGAINBACK) & 0xff00) |
		    0x15);
		if (phy->rev == 2)
			BWN_PHY_WRITE(mac, BWN_PHY_ADIVRELATED, 8);
		else
			BWN_PHY_WRITE(mac, BWN_PHY_ADIVRELATED,
			    (BWN_PHY_READ(mac, BWN_PHY_ADIVRELATED) & 0xff00) |
			    8);
	}
	if (phy->rev >= 6)
		BWN_PHY_WRITE(mac, BWN_PHY_OFDM9B, 0xdc);

	hf |= BWN_HF_UCODE_ANTDIV_HELPER;
	bwn_hf_write(mac, hf);
}

int
bwn_phy_g_im(struct bwn_mac *mac, int mode)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;

	KASSERT(phy->type == BWN_PHYTYPE_G, ("%s: fail", __func__));
	KASSERT(mode == BWN_IMMODE_NONE, ("%s: fail", __func__));

	if (phy->rev == 0 || !phy->gmode)
		return (ENODEV);

	pg->pg_aci_wlan_automatic = 0;
	return (0);
}

bwn_txpwr_result_t
bwn_phy_g_recalc_txpwr(struct bwn_mac *mac, int ignore_tssi)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_softc *sc = mac->mac_sc;
	unsigned int tssi;
	int cck, ofdm;
	int power;
	int rfatt, bbatt;
	unsigned int max;

	KASSERT(phy->type == BWN_PHYTYPE_G, ("%s: fail", __func__));

	cck = bwn_phy_shm_tssi_read(mac, BWN_SHARED_TSSI_CCK);
	ofdm = bwn_phy_shm_tssi_read(mac, BWN_SHARED_TSSI_OFDM_G);
	if (cck < 0 && ofdm < 0) {
		if (ignore_tssi == 0)
			return (BWN_TXPWR_RES_DONE);
		cck = 0;
		ofdm = 0;
	}
	tssi = (cck < 0) ? ofdm : ((ofdm < 0) ? cck : (cck + ofdm) / 2);
	if (pg->pg_avgtssi != 0xff)
		tssi = (tssi + pg->pg_avgtssi) / 2;
	pg->pg_avgtssi = tssi;
	KASSERT(tssi < BWN_TSSI_MAX, ("%s:%d: fail", __func__, __LINE__));

	max = pg->pg_pa0maxpwr;
	if (sc->sc_board_info.board_flags & BHND_BFL_PACTRL)
		max -= 3;
	if (max >= 120) {
		device_printf(sc->sc_dev, "invalid max TX-power value\n");
		max = 80;
		pg->pg_pa0maxpwr = max;
	}

	power = MIN(MAX((phy->txpower < 0) ? 0 : (phy->txpower << 2), 0), max) -
	    (pg->pg_tssi2dbm[MIN(MAX(pg->pg_idletssi - pg->pg_curtssi +
	     tssi, 0x00), 0x3f)]);
	if (power == 0)
		return (BWN_TXPWR_RES_DONE);

	rfatt = -((power + 7) / 8);
	bbatt = (-(power / 2)) - (4 * rfatt);
	if ((rfatt == 0) && (bbatt == 0))
		return (BWN_TXPWR_RES_DONE);
	pg->pg_bbatt_delta = bbatt;
	pg->pg_rfatt_delta = rfatt;
	return (BWN_TXPWR_RES_NEED_ADJUST);
}

void
bwn_phy_g_set_txpwr(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_softc *sc = mac->mac_sc;
	int rfatt, bbatt;
	uint8_t txctl;

	bwn_mac_suspend(mac);

	BWN_ASSERT_LOCKED(sc);

	bbatt = pg->pg_bbatt.att;
	bbatt += pg->pg_bbatt_delta;
	rfatt = pg->pg_rfatt.att;
	rfatt += pg->pg_rfatt_delta;

	bwn_phy_g_setatt(mac, &bbatt, &rfatt);
	txctl = pg->pg_txctl;
	if ((phy->rf_ver == 0x2050) && (phy->rf_rev == 2)) {
		if (rfatt <= 1) {
			if (txctl == 0) {
				txctl = BWN_TXCTL_PA2DB | BWN_TXCTL_TXMIX;
				rfatt += 2;
				bbatt += 2;
			} else if (sc->sc_board_info.board_flags &
			    BHND_BFL_PACTRL) {
				bbatt += 4 * (rfatt - 2);
				rfatt = 2;
			}
		} else if (rfatt > 4 && txctl) {
			txctl = 0;
			if (bbatt < 3) {
				rfatt -= 3;
				bbatt += 2;
			} else {
				rfatt -= 2;
				bbatt -= 2;
			}
		}
	}
	pg->pg_txctl = txctl;
	bwn_phy_g_setatt(mac, &bbatt, &rfatt);
	pg->pg_rfatt.att = rfatt;
	pg->pg_bbatt.att = bbatt;

	DPRINTF(sc, BWN_DEBUG_TXPOW, "%s: adjust TX power\n", __func__);

	bwn_phy_lock(mac);
	bwn_rf_lock(mac);
	bwn_phy_g_set_txpwr_sub(mac, &pg->pg_bbatt, &pg->pg_rfatt,
	    pg->pg_txctl);
	bwn_rf_unlock(mac);
	bwn_phy_unlock(mac);

	bwn_mac_enable(mac);
}

void
bwn_phy_g_task_15s(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_txpwr_loctl *lo = &pg->pg_loctl;
	unsigned long expire, now;
	struct bwn_lo_calib *cal, *tmp;
	uint8_t expired = 0;

	bwn_mac_suspend(mac);

	if (lo == NULL)
		goto fail;

	BWN_GETTIME(now);
	if (bwn_has_hwpctl(mac)) {
		expire = now - BWN_LO_PWRVEC_EXPIRE;
		if (ieee80211_time_before(lo->pwr_vec_read_time, expire)) {
			bwn_lo_get_powervector(mac);
			bwn_phy_g_dc_lookup_init(mac, 0);
		}
		goto fail;
	}

	expire = now - BWN_LO_CALIB_EXPIRE;
	TAILQ_FOREACH_SAFE(cal, &lo->calib_list, list, tmp) {
		if (!ieee80211_time_before(cal->calib_time, expire))
			continue;
		if (BWN_BBATTCMP(&cal->bbatt, &pg->pg_bbatt) &&
		    BWN_RFATTCMP(&cal->rfatt, &pg->pg_rfatt)) {
			KASSERT(!expired, ("%s:%d: fail", __func__, __LINE__));
			expired = 1;
		}

		DPRINTF(sc, BWN_DEBUG_LO, "expired BB %u RF %u %u I %d Q %d\n",
		    cal->bbatt.att, cal->rfatt.att, cal->rfatt.padmix,
		    cal->ctl.i, cal->ctl.q);

		TAILQ_REMOVE(&lo->calib_list, cal, list);
		free(cal, M_DEVBUF);
	}
	if (expired || TAILQ_EMPTY(&lo->calib_list)) {
		cal = bwn_lo_calibset(mac, &pg->pg_bbatt,
		    &pg->pg_rfatt);
		if (cal == NULL) {
			device_printf(sc->sc_dev,
			    "failed to recalibrate LO\n");
			goto fail;
		}
		TAILQ_INSERT_TAIL(&lo->calib_list, cal, list);
		bwn_lo_write(mac, &cal->ctl);
	}

fail:
	bwn_mac_enable(mac);
}

void
bwn_phy_g_task_60s(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_softc *sc = mac->mac_sc;
	uint8_t old = phy->chan;

	if (!(sc->sc_board_info.board_flags & BHND_BFL_ADCDIV))
		return;

	bwn_mac_suspend(mac);
	bwn_nrssi_slope_11g(mac);
	if ((phy->rf_ver == 0x2050) && (phy->rf_rev == 8)) {
		bwn_switch_channel(mac, (old >= 8) ? 1 : 13);
		bwn_switch_channel(mac, old);
	}
	bwn_mac_enable(mac);
}

void
bwn_phy_switch_analog(struct bwn_mac *mac, int on)
{

	BWN_WRITE_2(mac, BWN_PHY0, on ? 0 : 0xf4);
}

static void
bwn_phy_g_init_sub(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_softc *sc = mac->mac_sc;
	uint16_t i, tmp;

	if (phy->rev == 1)
		bwn_phy_init_b5(mac);
	else
		bwn_phy_init_b6(mac);

	if (phy->rev >= 2 || phy->gmode)
		bwn_phy_init_a(mac);

	if (phy->rev >= 2) {
		BWN_PHY_WRITE(mac, BWN_PHY_ANALOGOVER, 0);
		BWN_PHY_WRITE(mac, BWN_PHY_ANALOGOVERVAL, 0);
	}
	if (phy->rev == 2) {
		BWN_PHY_WRITE(mac, BWN_PHY_RFOVER, 0);
		BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, 0xc0);
	}
	if (phy->rev > 5) {
		BWN_PHY_WRITE(mac, BWN_PHY_RFOVER, 0x400);
		BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, 0xc0);
	}
	if (phy->gmode || phy->rev >= 2) {
		tmp = BWN_PHY_READ(mac, BWN_PHY_VERSION_OFDM);
		tmp &= BWN_PHYVER_VERSION;
		if (tmp == 3 || tmp == 5) {
			BWN_PHY_WRITE(mac, BWN_PHY_OFDM(0xc2), 0x1816);
			BWN_PHY_WRITE(mac, BWN_PHY_OFDM(0xc3), 0x8006);
		}
		if (tmp == 5) {
			BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0xcc), 0x00ff,
			    0x1f00);
		}
	}
	if ((phy->rev <= 2 && phy->gmode) || phy->rev >= 2)
		BWN_PHY_WRITE(mac, BWN_PHY_OFDM(0x7e), 0x78);
	if (phy->rf_rev == 8) {
		BWN_PHY_SET(mac, BWN_PHY_EXTG(0x01), 0x80);
		BWN_PHY_SET(mac, BWN_PHY_OFDM(0x3e), 0x4);
	}
	if (BWN_HAS_LOOPBACK(phy))
		bwn_loopback_calcgain(mac);

	if (phy->rf_rev != 8) {
		if (pg->pg_initval == 0xffff)
			pg->pg_initval = bwn_rf_init_bcm2050(mac);
		else
			BWN_RF_WRITE(mac, 0x0078, pg->pg_initval);
	}
	bwn_lo_g_init(mac);
	if (BWN_HAS_TXMAG(phy)) {
		BWN_RF_WRITE(mac, 0x52,
		    (BWN_RF_READ(mac, 0x52) & 0xff00)
		    | pg->pg_loctl.tx_bias |
		    pg->pg_loctl.tx_magn);
	} else {
		BWN_RF_SETMASK(mac, 0x52, 0xfff0, pg->pg_loctl.tx_bias);
	}
	if (phy->rev >= 6) {
		BWN_PHY_SETMASK(mac, BWN_PHY_CCK(0x36), 0x0fff,
		    (pg->pg_loctl.tx_bias << 12));
	}
	if (sc->sc_board_info.board_flags & BHND_BFL_PACTRL)
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2e), 0x8075);
	else
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2e), 0x807f);
	if (phy->rev < 2)
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2f), 0x101);
	else
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2f), 0x202);
	if (phy->gmode || phy->rev >= 2) {
		bwn_lo_g_adjust(mac);
		BWN_PHY_WRITE(mac, BWN_PHY_LO_MASK, 0x8078);
	}

	if (!(sc->sc_board_info.board_flags & BHND_BFL_ADCDIV)) {
		for (i = 0; i < 64; i++) {
			BWN_PHY_WRITE(mac, BWN_PHY_NRSSI_CTRL, i);
			BWN_PHY_WRITE(mac, BWN_PHY_NRSSI_DATA,
			    (uint16_t)MIN(MAX(bwn_nrssi_read(mac, i) - 0xffff,
			    -32), 31));
		}
		bwn_nrssi_threshold(mac);
	} else if (phy->gmode || phy->rev >= 2) {
		if (pg->pg_nrssi[0] == -1000) {
			KASSERT(pg->pg_nrssi[1] == -1000,
			    ("%s:%d: fail", __func__, __LINE__));
			bwn_nrssi_slope_11g(mac);
		} else
			bwn_nrssi_threshold(mac);
	}
	if (phy->rf_rev == 8)
		BWN_PHY_WRITE(mac, BWN_PHY_EXTG(0x05), 0x3230);
	bwn_phy_hwpctl_init(mac);
	if ((sc->sc_cid.chip_id == BHND_CHIPID_BCM4306
	     && sc->sc_cid.chip_pkg == 2) || 0) {
		BWN_PHY_MASK(mac, BWN_PHY_CRS0, 0xbfff);
		BWN_PHY_MASK(mac, BWN_PHY_OFDM(0xc3), 0x7fff);
	}
}

static void
bwn_phy_init_b5(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_softc *sc = mac->mac_sc;
	uint16_t offset, value;
	uint8_t old_channel;

	if (phy->analog == 1)
		BWN_RF_SET(mac, 0x007a, 0x0050);
	if ((sc->sc_board_info.board_vendor != PCI_VENDOR_BROADCOM) &&
	    (sc->sc_board_info.board_type != BHND_BOARD_BU4306)) {
		value = 0x2120;
		for (offset = 0x00a8; offset < 0x00c7; offset++) {
			BWN_PHY_WRITE(mac, offset, value);
			value += 0x202;
		}
	}
	BWN_PHY_SETMASK(mac, 0x0035, 0xf0ff, 0x0700);
	if (phy->rf_ver == 0x2050)
		BWN_PHY_WRITE(mac, 0x0038, 0x0667);

	if (phy->gmode || phy->rev >= 2) {
		if (phy->rf_ver == 0x2050) {
			BWN_RF_SET(mac, 0x007a, 0x0020);
			BWN_RF_SET(mac, 0x0051, 0x0004);
		}
		BWN_WRITE_2(mac, BWN_PHY_RADIO, 0x0000);

		BWN_PHY_SET(mac, 0x0802, 0x0100);
		BWN_PHY_SET(mac, 0x042b, 0x2000);

		BWN_PHY_WRITE(mac, 0x001c, 0x186a);

		BWN_PHY_SETMASK(mac, 0x0013, 0x00ff, 0x1900);
		BWN_PHY_SETMASK(mac, 0x0035, 0xffc0, 0x0064);
		BWN_PHY_SETMASK(mac, 0x005d, 0xff80, 0x000a);
	}

	if (mac->mac_flags & BWN_MAC_FLAG_BADFRAME_PREEMP)
		BWN_PHY_SET(mac, BWN_PHY_RADIO_BITFIELD, (1 << 11));

	if (phy->analog == 1) {
		BWN_PHY_WRITE(mac, 0x0026, 0xce00);
		BWN_PHY_WRITE(mac, 0x0021, 0x3763);
		BWN_PHY_WRITE(mac, 0x0022, 0x1bc3);
		BWN_PHY_WRITE(mac, 0x0023, 0x06f9);
		BWN_PHY_WRITE(mac, 0x0024, 0x037e);
	} else
		BWN_PHY_WRITE(mac, 0x0026, 0xcc00);
	BWN_PHY_WRITE(mac, 0x0030, 0x00c6);
	BWN_WRITE_2(mac, 0x03ec, 0x3f22);

	if (phy->analog == 1)
		BWN_PHY_WRITE(mac, 0x0020, 0x3e1c);
	else
		BWN_PHY_WRITE(mac, 0x0020, 0x301c);

	if (phy->analog == 0)
		BWN_WRITE_2(mac, 0x03e4, 0x3000);

	old_channel = phy->chan;
	bwn_phy_g_switch_chan(mac, 7, 0);

	if (phy->rf_ver != 0x2050) {
		BWN_RF_WRITE(mac, 0x0075, 0x0080);
		BWN_RF_WRITE(mac, 0x0079, 0x0081);
	}

	BWN_RF_WRITE(mac, 0x0050, 0x0020);
	BWN_RF_WRITE(mac, 0x0050, 0x0023);

	if (phy->rf_ver == 0x2050) {
		BWN_RF_WRITE(mac, 0x0050, 0x0020);
		BWN_RF_WRITE(mac, 0x005a, 0x0070);
	}

	BWN_RF_WRITE(mac, 0x005b, 0x007b);
	BWN_RF_WRITE(mac, 0x005c, 0x00b0);
	BWN_RF_SET(mac, 0x007a, 0x0007);

	bwn_phy_g_switch_chan(mac, old_channel, 0);
	BWN_PHY_WRITE(mac, 0x0014, 0x0080);
	BWN_PHY_WRITE(mac, 0x0032, 0x00ca);
	BWN_PHY_WRITE(mac, 0x002a, 0x88a3);

	bwn_phy_g_set_txpwr_sub(mac, &pg->pg_bbatt, &pg->pg_rfatt,
	    pg->pg_txctl);

	if (phy->rf_ver == 0x2050)
		BWN_RF_WRITE(mac, 0x005d, 0x000d);

	BWN_WRITE_2(mac, 0x03e4, (BWN_READ_2(mac, 0x03e4) & 0xffc0) | 0x0004);
}

static void
bwn_loopback_calcgain(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_softc *sc = mac->mac_sc;
	uint16_t backup_phy[16] = { 0 };
	uint16_t backup_radio[3];
	uint16_t backup_bband;
	uint16_t i, j, loop_i_max;
	uint16_t trsw_rx;
	uint16_t loop1_outer_done, loop1_inner_done;

	backup_phy[0] = BWN_PHY_READ(mac, BWN_PHY_CRS0);
	backup_phy[1] = BWN_PHY_READ(mac, BWN_PHY_CCKBBANDCFG);
	backup_phy[2] = BWN_PHY_READ(mac, BWN_PHY_RFOVER);
	backup_phy[3] = BWN_PHY_READ(mac, BWN_PHY_RFOVERVAL);
	if (phy->rev != 1) {
		backup_phy[4] = BWN_PHY_READ(mac, BWN_PHY_ANALOGOVER);
		backup_phy[5] = BWN_PHY_READ(mac, BWN_PHY_ANALOGOVERVAL);
	}
	backup_phy[6] = BWN_PHY_READ(mac, BWN_PHY_CCK(0x5a));
	backup_phy[7] = BWN_PHY_READ(mac, BWN_PHY_CCK(0x59));
	backup_phy[8] = BWN_PHY_READ(mac, BWN_PHY_CCK(0x58));
	backup_phy[9] = BWN_PHY_READ(mac, BWN_PHY_CCK(0x0a));
	backup_phy[10] = BWN_PHY_READ(mac, BWN_PHY_CCK(0x03));
	backup_phy[11] = BWN_PHY_READ(mac, BWN_PHY_LO_MASK);
	backup_phy[12] = BWN_PHY_READ(mac, BWN_PHY_LO_CTL);
	backup_phy[13] = BWN_PHY_READ(mac, BWN_PHY_CCK(0x2b));
	backup_phy[14] = BWN_PHY_READ(mac, BWN_PHY_PGACTL);
	backup_phy[15] = BWN_PHY_READ(mac, BWN_PHY_LO_LEAKAGE);
	backup_bband = pg->pg_bbatt.att;
	backup_radio[0] = BWN_RF_READ(mac, 0x52);
	backup_radio[1] = BWN_RF_READ(mac, 0x43);
	backup_radio[2] = BWN_RF_READ(mac, 0x7a);

	BWN_PHY_MASK(mac, BWN_PHY_CRS0, 0x3fff);
	BWN_PHY_SET(mac, BWN_PHY_CCKBBANDCFG, 0x8000);
	BWN_PHY_SET(mac, BWN_PHY_RFOVER, 0x0002);
	BWN_PHY_MASK(mac, BWN_PHY_RFOVERVAL, 0xfffd);
	BWN_PHY_SET(mac, BWN_PHY_RFOVER, 0x0001);
	BWN_PHY_MASK(mac, BWN_PHY_RFOVERVAL, 0xfffe);
	if (phy->rev != 1) {
		BWN_PHY_SET(mac, BWN_PHY_ANALOGOVER, 0x0001);
		BWN_PHY_MASK(mac, BWN_PHY_ANALOGOVERVAL, 0xfffe);
		BWN_PHY_SET(mac, BWN_PHY_ANALOGOVER, 0x0002);
		BWN_PHY_MASK(mac, BWN_PHY_ANALOGOVERVAL, 0xfffd);
	}
	BWN_PHY_SET(mac, BWN_PHY_RFOVER, 0x000c);
	BWN_PHY_SET(mac, BWN_PHY_RFOVERVAL, 0x000c);
	BWN_PHY_SET(mac, BWN_PHY_RFOVER, 0x0030);
	BWN_PHY_SETMASK(mac, BWN_PHY_RFOVERVAL, 0xffcf, 0x10);

	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x5a), 0x0780);
	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x59), 0xc810);
	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x58), 0x000d);

	BWN_PHY_SET(mac, BWN_PHY_CCK(0x0a), 0x2000);
	if (phy->rev != 1) {
		BWN_PHY_SET(mac, BWN_PHY_ANALOGOVER, 0x0004);
		BWN_PHY_MASK(mac, BWN_PHY_ANALOGOVERVAL, 0xfffb);
	}
	BWN_PHY_SETMASK(mac, BWN_PHY_CCK(0x03), 0xff9f, 0x40);

	if (phy->rf_rev == 8)
		BWN_RF_WRITE(mac, 0x43, 0x000f);
	else {
		BWN_RF_WRITE(mac, 0x52, 0);
		BWN_RF_SETMASK(mac, 0x43, 0xfff0, 0x9);
	}
	bwn_phy_g_set_bbatt(mac, 11);

	if (phy->rev >= 3)
		BWN_PHY_WRITE(mac, BWN_PHY_LO_MASK, 0xc020);
	else
		BWN_PHY_WRITE(mac, BWN_PHY_LO_MASK, 0x8020);
	BWN_PHY_WRITE(mac, BWN_PHY_LO_CTL, 0);

	BWN_PHY_SETMASK(mac, BWN_PHY_CCK(0x2b), 0xffc0, 0x01);
	BWN_PHY_SETMASK(mac, BWN_PHY_CCK(0x2b), 0xc0ff, 0x800);

	BWN_PHY_SET(mac, BWN_PHY_RFOVER, 0x0100);
	BWN_PHY_MASK(mac, BWN_PHY_RFOVERVAL, 0xcfff);

	if (sc->sc_board_info.board_flags & BHND_BFL_EXTLNA) {
		if (phy->rev >= 7) {
			BWN_PHY_SET(mac, BWN_PHY_RFOVER, 0x0800);
			BWN_PHY_SET(mac, BWN_PHY_RFOVERVAL, 0x8000);
		}
	}
	BWN_RF_MASK(mac, 0x7a, 0x00f7);

	j = 0;
	loop_i_max = (phy->rf_rev == 8) ? 15 : 9;
	for (i = 0; i < loop_i_max; i++) {
		for (j = 0; j < 16; j++) {
			BWN_RF_WRITE(mac, 0x43, i);
			BWN_PHY_SETMASK(mac, BWN_PHY_RFOVERVAL, 0xf0ff,
			    (j << 8));
			BWN_PHY_SETMASK(mac, BWN_PHY_PGACTL, 0x0fff, 0xa000);
			BWN_PHY_SET(mac, BWN_PHY_PGACTL, 0xf000);
			DELAY(20);
			if (BWN_PHY_READ(mac, BWN_PHY_LO_LEAKAGE) >= 0xdfc)
				goto done0;
		}
	}
done0:
	loop1_outer_done = i;
	loop1_inner_done = j;
	if (j >= 8) {
		BWN_PHY_SET(mac, BWN_PHY_RFOVERVAL, 0x30);
		trsw_rx = 0x1b;
		for (j = j - 8; j < 16; j++) {
			BWN_PHY_SETMASK(mac, BWN_PHY_RFOVERVAL, 0xf0ff, j << 8);
			BWN_PHY_SETMASK(mac, BWN_PHY_PGACTL, 0x0fff, 0xa000);
			BWN_PHY_SET(mac, BWN_PHY_PGACTL, 0xf000);
			DELAY(20);
			trsw_rx -= 3;
			if (BWN_PHY_READ(mac, BWN_PHY_LO_LEAKAGE) >= 0xdfc)
				goto done1;
		}
	} else
		trsw_rx = 0x18;
done1:

	if (phy->rev != 1) {
		BWN_PHY_WRITE(mac, BWN_PHY_ANALOGOVER, backup_phy[4]);
		BWN_PHY_WRITE(mac, BWN_PHY_ANALOGOVERVAL, backup_phy[5]);
	}
	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x5a), backup_phy[6]);
	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x59), backup_phy[7]);
	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x58), backup_phy[8]);
	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x0a), backup_phy[9]);
	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x03), backup_phy[10]);
	BWN_PHY_WRITE(mac, BWN_PHY_LO_MASK, backup_phy[11]);
	BWN_PHY_WRITE(mac, BWN_PHY_LO_CTL, backup_phy[12]);
	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2b), backup_phy[13]);
	BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, backup_phy[14]);

	bwn_phy_g_set_bbatt(mac, backup_bband);

	BWN_RF_WRITE(mac, 0x52, backup_radio[0]);
	BWN_RF_WRITE(mac, 0x43, backup_radio[1]);
	BWN_RF_WRITE(mac, 0x7a, backup_radio[2]);

	BWN_PHY_WRITE(mac, BWN_PHY_RFOVER, backup_phy[2] | 0x0003);
	DELAY(10);
	BWN_PHY_WRITE(mac, BWN_PHY_RFOVER, backup_phy[2]);
	BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL, backup_phy[3]);
	BWN_PHY_WRITE(mac, BWN_PHY_CRS0, backup_phy[0]);
	BWN_PHY_WRITE(mac, BWN_PHY_CCKBBANDCFG, backup_phy[1]);

	pg->pg_max_lb_gain =
	    ((loop1_inner_done * 6) - (loop1_outer_done * 4)) - 11;
	pg->pg_trsw_rx_gain = trsw_rx * 2;
}

static uint16_t
bwn_rf_init_bcm2050(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	uint32_t tmp1 = 0, tmp2 = 0;
	uint16_t rcc, i, j, pgactl, cck0, cck1, cck2, cck3, rfover, rfoverval,
	    analogover, analogoverval, crs0, classctl, lomask, loctl, syncctl,
	    radio0, radio1, radio2, reg0, reg1, reg2, radio78, reg, index;
	static const uint8_t rcc_table[] = {
		0x02, 0x03, 0x01, 0x0f,
		0x06, 0x07, 0x05, 0x0f,
		0x0a, 0x0b, 0x09, 0x0f,
		0x0e, 0x0f, 0x0d, 0x0f,
	};

	loctl = lomask = reg0 = classctl = crs0 = analogoverval = analogover =
	    rfoverval = rfover = cck3 = 0;
	radio0 = BWN_RF_READ(mac, 0x43);
	radio1 = BWN_RF_READ(mac, 0x51);
	radio2 = BWN_RF_READ(mac, 0x52);
	pgactl = BWN_PHY_READ(mac, BWN_PHY_PGACTL);
	cck0 = BWN_PHY_READ(mac, BWN_PHY_CCK(0x5a));
	cck1 = BWN_PHY_READ(mac, BWN_PHY_CCK(0x59));
	cck2 = BWN_PHY_READ(mac, BWN_PHY_CCK(0x58));

	if (phy->type == BWN_PHYTYPE_B) {
		cck3 = BWN_PHY_READ(mac, BWN_PHY_CCK(0x30));
		reg0 = BWN_READ_2(mac, 0x3ec);

		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x30), 0xff);
		BWN_WRITE_2(mac, 0x3ec, 0x3f3f);
	} else if (phy->gmode || phy->rev >= 2) {
		rfover = BWN_PHY_READ(mac, BWN_PHY_RFOVER);
		rfoverval = BWN_PHY_READ(mac, BWN_PHY_RFOVERVAL);
		analogover = BWN_PHY_READ(mac, BWN_PHY_ANALOGOVER);
		analogoverval = BWN_PHY_READ(mac, BWN_PHY_ANALOGOVERVAL);
		crs0 = BWN_PHY_READ(mac, BWN_PHY_CRS0);
		classctl = BWN_PHY_READ(mac, BWN_PHY_CLASSCTL);

		BWN_PHY_SET(mac, BWN_PHY_ANALOGOVER, 0x0003);
		BWN_PHY_MASK(mac, BWN_PHY_ANALOGOVERVAL, 0xfffc);
		BWN_PHY_MASK(mac, BWN_PHY_CRS0, 0x7fff);
		BWN_PHY_MASK(mac, BWN_PHY_CLASSCTL, 0xfffc);
		if (BWN_HAS_LOOPBACK(phy)) {
			lomask = BWN_PHY_READ(mac, BWN_PHY_LO_MASK);
			loctl = BWN_PHY_READ(mac, BWN_PHY_LO_CTL);
			if (phy->rev >= 3)
				BWN_PHY_WRITE(mac, BWN_PHY_LO_MASK, 0xc020);
			else
				BWN_PHY_WRITE(mac, BWN_PHY_LO_MASK, 0x8020);
			BWN_PHY_WRITE(mac, BWN_PHY_LO_CTL, 0);
		}

		BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL,
		    bwn_rf_2050_rfoverval(mac, BWN_PHY_RFOVERVAL,
			BWN_LPD(0, 1, 1)));
		BWN_PHY_WRITE(mac, BWN_PHY_RFOVER,
		    bwn_rf_2050_rfoverval(mac, BWN_PHY_RFOVER, 0));
	}
	BWN_WRITE_2(mac, 0x3e2, BWN_READ_2(mac, 0x3e2) | 0x8000);

	syncctl = BWN_PHY_READ(mac, BWN_PHY_SYNCCTL);
	BWN_PHY_MASK(mac, BWN_PHY_SYNCCTL, 0xff7f);
	reg1 = BWN_READ_2(mac, 0x3e6);
	reg2 = BWN_READ_2(mac, 0x3f4);

	if (phy->analog == 0)
		BWN_WRITE_2(mac, 0x03e6, 0x0122);
	else {
		if (phy->analog >= 2)
			BWN_PHY_SETMASK(mac, BWN_PHY_CCK(0x03), 0xffbf, 0x40);
		BWN_WRITE_2(mac, BWN_CHANNEL_EXT,
		    (BWN_READ_2(mac, BWN_CHANNEL_EXT) | 0x2000));
	}

	reg = BWN_RF_READ(mac, 0x60);
	index = (reg & 0x001e) >> 1;
	rcc = (((rcc_table[index] << 1) | (reg & 0x0001)) | 0x0020);

	if (phy->type == BWN_PHYTYPE_B)
		BWN_RF_WRITE(mac, 0x78, 0x26);
	if (phy->gmode || phy->rev >= 2) {
		BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL,
		    bwn_rf_2050_rfoverval(mac, BWN_PHY_RFOVERVAL,
			BWN_LPD(0, 1, 1)));
	}
	BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, 0xbfaf);
	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2b), 0x1403);
	if (phy->gmode || phy->rev >= 2) {
		BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL,
		    bwn_rf_2050_rfoverval(mac, BWN_PHY_RFOVERVAL,
			BWN_LPD(0, 0, 1)));
	}
	BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, 0xbfa0);
	BWN_RF_SET(mac, 0x51, 0x0004);
	if (phy->rf_rev == 8)
		BWN_RF_WRITE(mac, 0x43, 0x1f);
	else {
		BWN_RF_WRITE(mac, 0x52, 0);
		BWN_RF_SETMASK(mac, 0x43, 0xfff0, 0x0009);
	}
	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x58), 0);

	for (i = 0; i < 16; i++) {
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x5a), 0x0480);
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x59), 0xc810);
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x58), 0x000d);
		if (phy->gmode || phy->rev >= 2) {
			BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL,
			    bwn_rf_2050_rfoverval(mac,
				BWN_PHY_RFOVERVAL, BWN_LPD(1, 0, 1)));
		}
		BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, 0xafb0);
		DELAY(10);
		if (phy->gmode || phy->rev >= 2) {
			BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL,
			    bwn_rf_2050_rfoverval(mac,
				BWN_PHY_RFOVERVAL, BWN_LPD(1, 0, 1)));
		}
		BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, 0xefb0);
		DELAY(10);
		if (phy->gmode || phy->rev >= 2) {
			BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL,
			    bwn_rf_2050_rfoverval(mac,
				BWN_PHY_RFOVERVAL, BWN_LPD(1, 0, 0)));
		}
		BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, 0xfff0);
		DELAY(20);
		tmp1 += BWN_PHY_READ(mac, BWN_PHY_LO_LEAKAGE);
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x58), 0);
		if (phy->gmode || phy->rev >= 2) {
			BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL,
			    bwn_rf_2050_rfoverval(mac,
				BWN_PHY_RFOVERVAL, BWN_LPD(1, 0, 1)));
		}
		BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, 0xafb0);
	}
	DELAY(10);

	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x58), 0);
	tmp1++;
	tmp1 >>= 9;

	for (i = 0; i < 16; i++) {
		radio78 = (BWN_BITREV4(i) << 1) | 0x0020;
		BWN_RF_WRITE(mac, 0x78, radio78);
		DELAY(10);
		for (j = 0; j < 16; j++) {
			BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x5a), 0x0d80);
			BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x59), 0xc810);
			BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x58), 0x000d);
			if (phy->gmode || phy->rev >= 2) {
				BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL,
				    bwn_rf_2050_rfoverval(mac,
					BWN_PHY_RFOVERVAL, BWN_LPD(1, 0, 1)));
			}
			BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, 0xafb0);
			DELAY(10);
			if (phy->gmode || phy->rev >= 2) {
				BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL,
				    bwn_rf_2050_rfoverval(mac,
					BWN_PHY_RFOVERVAL, BWN_LPD(1, 0, 1)));
			}
			BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, 0xefb0);
			DELAY(10);
			if (phy->gmode || phy->rev >= 2) {
				BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL,
				    bwn_rf_2050_rfoverval(mac,
					BWN_PHY_RFOVERVAL, BWN_LPD(1, 0, 0)));
			}
			BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, 0xfff0);
			DELAY(10);
			tmp2 += BWN_PHY_READ(mac, BWN_PHY_LO_LEAKAGE);
			BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x58), 0);
			if (phy->gmode || phy->rev >= 2) {
				BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL,
				    bwn_rf_2050_rfoverval(mac,
					BWN_PHY_RFOVERVAL, BWN_LPD(1, 0, 1)));
			}
			BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, 0xafb0);
		}
		tmp2++;
		tmp2 >>= 8;
		if (tmp1 < tmp2)
			break;
	}

	BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, pgactl);
	BWN_RF_WRITE(mac, 0x51, radio1);
	BWN_RF_WRITE(mac, 0x52, radio2);
	BWN_RF_WRITE(mac, 0x43, radio0);
	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x5a), cck0);
	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x59), cck1);
	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x58), cck2);
	BWN_WRITE_2(mac, 0x3e6, reg1);
	if (phy->analog != 0)
		BWN_WRITE_2(mac, 0x3f4, reg2);
	BWN_PHY_WRITE(mac, BWN_PHY_SYNCCTL, syncctl);
	bwn_spu_workaround(mac, phy->chan);
	if (phy->type == BWN_PHYTYPE_B) {
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x30), cck3);
		BWN_WRITE_2(mac, 0x3ec, reg0);
	} else if (phy->gmode) {
		BWN_WRITE_2(mac, BWN_PHY_RADIO,
			    BWN_READ_2(mac, BWN_PHY_RADIO)
			    & 0x7fff);
		BWN_PHY_WRITE(mac, BWN_PHY_RFOVER, rfover);
		BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL, rfoverval);
		BWN_PHY_WRITE(mac, BWN_PHY_ANALOGOVER, analogover);
		BWN_PHY_WRITE(mac, BWN_PHY_ANALOGOVERVAL,
			      analogoverval);
		BWN_PHY_WRITE(mac, BWN_PHY_CRS0, crs0);
		BWN_PHY_WRITE(mac, BWN_PHY_CLASSCTL, classctl);
		if (BWN_HAS_LOOPBACK(phy)) {
			BWN_PHY_WRITE(mac, BWN_PHY_LO_MASK, lomask);
			BWN_PHY_WRITE(mac, BWN_PHY_LO_CTL, loctl);
		}
	}

	return ((i > 15) ? radio78 : rcc);
}

static void
bwn_phy_init_b6(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_softc *sc = mac->mac_sc;
	uint16_t offset, val;
	uint8_t old_channel;

	KASSERT(!(phy->rf_rev == 6 || phy->rf_rev == 7),
	    ("%s:%d: fail", __func__, __LINE__));

	BWN_PHY_WRITE(mac, 0x003e, 0x817a);
	BWN_RF_WRITE(mac, 0x007a, BWN_RF_READ(mac, 0x007a) | 0x0058);
	if (phy->rf_rev == 4 || phy->rf_rev == 5) {
		BWN_RF_WRITE(mac, 0x51, 0x37);
		BWN_RF_WRITE(mac, 0x52, 0x70);
		BWN_RF_WRITE(mac, 0x53, 0xb3);
		BWN_RF_WRITE(mac, 0x54, 0x9b);
		BWN_RF_WRITE(mac, 0x5a, 0x88);
		BWN_RF_WRITE(mac, 0x5b, 0x88);
		BWN_RF_WRITE(mac, 0x5d, 0x88);
		BWN_RF_WRITE(mac, 0x5e, 0x88);
		BWN_RF_WRITE(mac, 0x7d, 0x88);
		bwn_hf_write(mac,
		    bwn_hf_read(mac) | BWN_HF_TSSI_RESET_PSM_WORKAROUN);
	}
	if (phy->rf_rev == 8) {
		BWN_RF_WRITE(mac, 0x51, 0);
		BWN_RF_WRITE(mac, 0x52, 0x40);
		BWN_RF_WRITE(mac, 0x53, 0xb7);
		BWN_RF_WRITE(mac, 0x54, 0x98);
		BWN_RF_WRITE(mac, 0x5a, 0x88);
		BWN_RF_WRITE(mac, 0x5b, 0x6b);
		BWN_RF_WRITE(mac, 0x5c, 0x0f);
		if (sc->sc_board_info.board_flags & BHND_BFL_ALTIQ) {
			BWN_RF_WRITE(mac, 0x5d, 0xfa);
			BWN_RF_WRITE(mac, 0x5e, 0xd8);
		} else {
			BWN_RF_WRITE(mac, 0x5d, 0xf5);
			BWN_RF_WRITE(mac, 0x5e, 0xb8);
		}
		BWN_RF_WRITE(mac, 0x0073, 0x0003);
		BWN_RF_WRITE(mac, 0x007d, 0x00a8);
		BWN_RF_WRITE(mac, 0x007c, 0x0001);
		BWN_RF_WRITE(mac, 0x007e, 0x0008);
	}
	for (val = 0x1e1f, offset = 0x0088; offset < 0x0098; offset++) {
		BWN_PHY_WRITE(mac, offset, val);
		val -= 0x0202;
	}
	for (val = 0x3e3f, offset = 0x0098; offset < 0x00a8; offset++) {
		BWN_PHY_WRITE(mac, offset, val);
		val -= 0x0202;
	}
	for (val = 0x2120, offset = 0x00a8; offset < 0x00c8; offset++) {
		BWN_PHY_WRITE(mac, offset, (val & 0x3f3f));
		val += 0x0202;
	}
	if (phy->type == BWN_PHYTYPE_G) {
		BWN_RF_SET(mac, 0x007a, 0x0020);
		BWN_RF_SET(mac, 0x0051, 0x0004);
		BWN_PHY_SET(mac, 0x0802, 0x0100);
		BWN_PHY_SET(mac, 0x042b, 0x2000);
		BWN_PHY_WRITE(mac, 0x5b, 0);
		BWN_PHY_WRITE(mac, 0x5c, 0);
	}

	old_channel = phy->chan;
	bwn_phy_g_switch_chan(mac, (old_channel >= 8) ? 1 : 13, 0);

	BWN_RF_WRITE(mac, 0x0050, 0x0020);
	BWN_RF_WRITE(mac, 0x0050, 0x0023);
	DELAY(40);
	if (phy->rf_rev < 6 || phy->rf_rev == 8) {
		BWN_RF_WRITE(mac, 0x7c, BWN_RF_READ(mac, 0x7c) | 0x0002);
		BWN_RF_WRITE(mac, 0x50, 0x20);
	}
	if (phy->rf_rev <= 2) {
		BWN_RF_WRITE(mac, 0x7c, 0x20);
		BWN_RF_WRITE(mac, 0x5a, 0x70);
		BWN_RF_WRITE(mac, 0x5b, 0x7b);
		BWN_RF_WRITE(mac, 0x5c, 0xb0);
	}
	BWN_RF_SETMASK(mac, 0x007a, 0x00f8, 0x0007);

	bwn_phy_g_switch_chan(mac, old_channel, 0);

	BWN_PHY_WRITE(mac, 0x0014, 0x0200);
	if (phy->rf_rev >= 6)
		BWN_PHY_WRITE(mac, 0x2a, 0x88c2);
	else
		BWN_PHY_WRITE(mac, 0x2a, 0x8ac0);
	BWN_PHY_WRITE(mac, 0x0038, 0x0668);
	bwn_phy_g_set_txpwr_sub(mac, &pg->pg_bbatt, &pg->pg_rfatt,
	    pg->pg_txctl);
	if (phy->rf_rev <= 5)
		BWN_PHY_SETMASK(mac, 0x5d, 0xff80, 0x0003);
	if (phy->rf_rev <= 2)
		BWN_RF_WRITE(mac, 0x005d, 0x000d);

	if (phy->analog == 4) {
		BWN_WRITE_2(mac, 0x3e4, 9);
		BWN_PHY_MASK(mac, 0x61, 0x0fff);
	} else
		BWN_PHY_SETMASK(mac, 0x0002, 0xffc0, 0x0004);
	if (phy->type == BWN_PHYTYPE_B)
		KASSERT(0 == 1, ("%s:%d: fail", __func__, __LINE__));
	else if (phy->type == BWN_PHYTYPE_G)
		BWN_WRITE_2(mac, 0x03e6, 0x0);
}

static void
bwn_phy_init_a(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_softc *sc = mac->mac_sc;

	KASSERT(phy->type == BWN_PHYTYPE_A || phy->type == BWN_PHYTYPE_G,
	    ("%s:%d: fail", __func__, __LINE__));

	if (phy->rev >= 6) {
		if (phy->type == BWN_PHYTYPE_A)
			BWN_PHY_MASK(mac, BWN_PHY_OFDM(0x1b), ~0x1000);
		if (BWN_PHY_READ(mac, BWN_PHY_ENCORE) & BWN_PHY_ENCORE_EN)
			BWN_PHY_SET(mac, BWN_PHY_ENCORE, 0x0010);
		else
			BWN_PHY_MASK(mac, BWN_PHY_ENCORE, ~0x1010);
	}

	bwn_wa_init(mac);

	if (phy->type == BWN_PHYTYPE_G &&
	    (sc->sc_board_info.board_flags & BHND_BFL_PACTRL))
		BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0x6e), 0xe000, 0x3cf);
}

static void
bwn_wa_write_noisescale(struct bwn_mac *mac, const uint16_t *nst)
{
	int i;

	for (i = 0; i < BWN_TAB_NOISESCALE_SIZE; i++)
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_NOISESCALE, i, nst[i]);
}

static void
bwn_wa_agc(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;

	if (phy->rev == 1) {
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC1_R1, 0, 254);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC1_R1, 1, 13);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC1_R1, 2, 19);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC1_R1, 3, 25);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC2, 0, 0x2710);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC2, 1, 0x9b83);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC2, 2, 0x9b83);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC2, 3, 0x0f8d);
		BWN_PHY_WRITE(mac, BWN_PHY_LMS, 4);
	} else {
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC1, 0, 254);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC1, 1, 13);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC1, 2, 19);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC1, 3, 25);
	}

	BWN_PHY_SETMASK(mac, BWN_PHY_CCKSHIFTBITS_WA, (uint16_t)~0xff00,
	    0x5700);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0x1a), ~0x007f, 0x000f);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0x1a), ~0x3f80, 0x2b80);
	BWN_PHY_SETMASK(mac, BWN_PHY_ANTWRSETT, 0xf0ff, 0x0300);
	BWN_RF_SET(mac, 0x7a, 0x0008);
	BWN_PHY_SETMASK(mac, BWN_PHY_N1P1GAIN, ~0x000f, 0x0008);
	BWN_PHY_SETMASK(mac, BWN_PHY_P1P2GAIN, ~0x0f00, 0x0600);
	BWN_PHY_SETMASK(mac, BWN_PHY_N1N2GAIN, ~0x0f00, 0x0700);
	BWN_PHY_SETMASK(mac, BWN_PHY_N1P1GAIN, ~0x0f00, 0x0100);
	if (phy->rev == 1)
		BWN_PHY_SETMASK(mac, BWN_PHY_N1N2GAIN, ~0x000f, 0x0007);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0x88), ~0x00ff, 0x001c);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0x88), ~0x3f00, 0x0200);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0x96), ~0x00ff, 0x001c);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0x89), ~0x00ff, 0x0020);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0x89), ~0x3f00, 0x0200);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0x82), ~0x00ff, 0x002e);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0x96), (uint16_t)~0xff00, 0x1a00);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0x81), ~0x00ff, 0x0028);
	BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0x81), (uint16_t)~0xff00, 0x2c00);
	if (phy->rev == 1) {
		BWN_PHY_WRITE(mac, BWN_PHY_PEAK_COUNT, 0x092b);
		BWN_PHY_SETMASK(mac, BWN_PHY_OFDM(0x1b), ~0x001e, 0x0002);
	} else {
		BWN_PHY_MASK(mac, BWN_PHY_OFDM(0x1b), ~0x001e);
		BWN_PHY_WRITE(mac, BWN_PHY_OFDM(0x1f), 0x287a);
		BWN_PHY_SETMASK(mac, BWN_PHY_LPFGAINCTL, ~0x000f, 0x0004);
		if (phy->rev >= 6) {
			BWN_PHY_WRITE(mac, BWN_PHY_OFDM(0x22), 0x287a);
			BWN_PHY_SETMASK(mac, BWN_PHY_LPFGAINCTL,
			    (uint16_t)~0xf000, 0x3000);
		}
	}
	BWN_PHY_SETMASK(mac, BWN_PHY_DIVSRCHIDX, 0x8080, 0x7874);
	BWN_PHY_WRITE(mac, BWN_PHY_OFDM(0x8e), 0x1c00);
	if (phy->rev == 1) {
		BWN_PHY_SETMASK(mac, BWN_PHY_DIVP1P2GAIN, ~0x0f00, 0x0600);
		BWN_PHY_WRITE(mac, BWN_PHY_OFDM(0x8b), 0x005e);
		BWN_PHY_SETMASK(mac, BWN_PHY_ANTWRSETT, ~0x00ff, 0x001e);
		BWN_PHY_WRITE(mac, BWN_PHY_OFDM(0x8d), 0x0002);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC3_R1, 0, 0);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC3_R1, 1, 7);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC3_R1, 2, 16);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC3_R1, 3, 28);
	} else {
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC3, 0, 0);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC3, 1, 7);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC3, 2, 16);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC3, 3, 28);
	}
	if (phy->rev >= 6) {
		BWN_PHY_MASK(mac, BWN_PHY_OFDM(0x26), ~0x0003);
		BWN_PHY_MASK(mac, BWN_PHY_OFDM(0x26), ~0x1000);
	}
	BWN_PHY_READ(mac, BWN_PHY_VERSION_OFDM);
}

static void
bwn_wa_grev1(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	int i;
	static const uint16_t bwn_tab_finefreqg[] = BWN_TAB_FINEFREQ_G;
	static const uint32_t bwn_tab_retard[] = BWN_TAB_RETARD;
	static const uint32_t bwn_tab_rotor[] = BWN_TAB_ROTOR;

	KASSERT(phy->type == BWN_PHYTYPE_G, ("%s fail", __func__));

	/* init CRSTHRES and ANTDWELL */
	if (phy->rev == 1) {
		BWN_PHY_WRITE(mac, BWN_PHY_CRSTHRES1_R1, 0x4f19);
	} else if (phy->rev == 2) {
		BWN_PHY_WRITE(mac, BWN_PHY_CRSTHRES1, 0x1861);
		BWN_PHY_WRITE(mac, BWN_PHY_CRSTHRES2, 0x0271);
		BWN_PHY_SET(mac, BWN_PHY_ANTDWELL, 0x0800);
	} else {
		BWN_PHY_WRITE(mac, BWN_PHY_CRSTHRES1, 0x0098);
		BWN_PHY_WRITE(mac, BWN_PHY_CRSTHRES2, 0x0070);
		BWN_PHY_WRITE(mac, BWN_PHY_OFDM(0xc9), 0x0080);
		BWN_PHY_SET(mac, BWN_PHY_ANTDWELL, 0x0800);
	}
	BWN_PHY_SETMASK(mac, BWN_PHY_CRS0, ~0x03c0, 0xd000);
	BWN_PHY_WRITE(mac, BWN_PHY_OFDM(0x2c), 0x005a);
	BWN_PHY_WRITE(mac, BWN_PHY_CCKSHIFTBITS, 0x0026);

	/* XXX support PHY-A??? */
	for (i = 0; i < N(bwn_tab_finefreqg); i++)
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_DACRFPABB, i,
		    bwn_tab_finefreqg[i]);

	/* XXX support PHY-A??? */
	if (phy->rev == 1)
		for (i = 0; i < N(bwn_tab_noise_g1); i++)
			bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC2, i,
			    bwn_tab_noise_g1[i]);
	else
		for (i = 0; i < N(bwn_tab_noise_g2); i++)
			bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC2, i,
			    bwn_tab_noise_g2[i]);


	for (i = 0; i < N(bwn_tab_rotor); i++)
		bwn_ofdmtab_write_4(mac, BWN_OFDMTAB_ROTOR, i,
		    bwn_tab_rotor[i]);

	/* XXX support PHY-A??? */
	if (phy->rev >= 6) {
		if (BWN_PHY_READ(mac, BWN_PHY_ENCORE) &
		    BWN_PHY_ENCORE_EN)
			bwn_wa_write_noisescale(mac, bwn_tab_noisescale_g3);
		else
			bwn_wa_write_noisescale(mac, bwn_tab_noisescale_g2);
	} else
		bwn_wa_write_noisescale(mac, bwn_tab_noisescale_g1);

	for (i = 0; i < N(bwn_tab_retard); i++)
		bwn_ofdmtab_write_4(mac, BWN_OFDMTAB_ADVRETARD, i,
		    bwn_tab_retard[i]);

	if (phy->rev == 1) {
		for (i = 0; i < 16; i++)
			bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_WRSSI_R1,
			    i, 0x0020);
	} else {
		for (i = 0; i < 32; i++)
			bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_WRSSI, i, 0x0820);
	}

	bwn_wa_agc(mac);
}

static void
bwn_wa_grev26789(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	int i;
	static const uint16_t bwn_tab_sigmasqr2[] = BWN_TAB_SIGMASQR2;
	uint16_t ofdmrev;

	KASSERT(phy->type == BWN_PHYTYPE_G, ("%s fail", __func__));

	bwn_gtab_write(mac, BWN_GTAB_ORIGTR, 0, 0xc480);

	/* init CRSTHRES and ANTDWELL */
	if (phy->rev == 1)
		BWN_PHY_WRITE(mac, BWN_PHY_CRSTHRES1_R1, 0x4f19);
	else if (phy->rev == 2) {
		BWN_PHY_WRITE(mac, BWN_PHY_CRSTHRES1, 0x1861);
		BWN_PHY_WRITE(mac, BWN_PHY_CRSTHRES2, 0x0271);
		BWN_PHY_SET(mac, BWN_PHY_ANTDWELL, 0x0800);
	} else {
		BWN_PHY_WRITE(mac, BWN_PHY_CRSTHRES1, 0x0098);
		BWN_PHY_WRITE(mac, BWN_PHY_CRSTHRES2, 0x0070);
		BWN_PHY_WRITE(mac, BWN_PHY_OFDM(0xc9), 0x0080);
		BWN_PHY_SET(mac, BWN_PHY_ANTDWELL, 0x0800);
	}

	for (i = 0; i < 64; i++)
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_RSSI, i, i);

	/* XXX support PHY-A??? */
	if (phy->rev == 1)
		for (i = 0; i < N(bwn_tab_noise_g1); i++)
			bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC2, i,
			    bwn_tab_noise_g1[i]);
	else
		for (i = 0; i < N(bwn_tab_noise_g2); i++)
			bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_AGC2, i,
			    bwn_tab_noise_g2[i]);

	/* XXX support PHY-A??? */
	if (phy->rev >= 6) {
		if (BWN_PHY_READ(mac, BWN_PHY_ENCORE) &
		    BWN_PHY_ENCORE_EN)
			bwn_wa_write_noisescale(mac, bwn_tab_noisescale_g3);
		else
			bwn_wa_write_noisescale(mac, bwn_tab_noisescale_g2);
	} else
		bwn_wa_write_noisescale(mac, bwn_tab_noisescale_g1);

	for (i = 0; i < N(bwn_tab_sigmasqr2); i++)
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_MINSIGSQ, i,
		    bwn_tab_sigmasqr2[i]);

	if (phy->rev == 1) {
		for (i = 0; i < 16; i++)
			bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_WRSSI_R1, i,
			    0x0020);
	} else {
		for (i = 0; i < 32; i++)
			bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_WRSSI, i, 0x0820);
	}

	bwn_wa_agc(mac);

	ofdmrev = BWN_PHY_READ(mac, BWN_PHY_VERSION_OFDM) & BWN_PHYVER_VERSION;
	if (ofdmrev > 2) {
		if (phy->type == BWN_PHYTYPE_A)
			BWN_PHY_WRITE(mac, BWN_PHY_PWRDOWN, 0x1808);
		else
			BWN_PHY_WRITE(mac, BWN_PHY_PWRDOWN, 0x1000);
	} else {
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_DAC, 3, 0x1044);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_DAC, 4, 0x7201);
		bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_DAC, 6, 0x0040);
	}

	bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_UNKNOWN_0F, 2, 15);
	bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_UNKNOWN_0F, 3, 20);
}

static void
bwn_wa_init(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_softc *sc = mac->mac_sc;

	KASSERT(phy->type == BWN_PHYTYPE_G, ("%s fail", __func__));

	switch (phy->rev) {
	case 1:
		bwn_wa_grev1(mac);
		break;
	case 2:
	case 6:
	case 7:
	case 8:
	case 9:
		bwn_wa_grev26789(mac);
		break;
	default:
		KASSERT(0 == 1, ("%s:%d: fail", __func__, __LINE__));
	}

	if (sc->sc_board_info.board_vendor != PCI_VENDOR_BROADCOM ||
	    sc->sc_board_info.board_type != BHND_BOARD_BU4306 ||
	    sc->sc_board_info.board_rev != 0x17) {
		if (phy->rev < 2) {
			bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_GAINX_R1, 1,
			    0x0002);
			bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_GAINX_R1, 2,
			    0x0001);
		} else {
			bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_GAINX, 1, 0x0002);
			bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_GAINX, 2, 0x0001);
			if ((sc->sc_board_info.board_flags &
			     BHND_BFL_EXTLNA) &&
			    (phy->rev >= 7)) {
				BWN_PHY_MASK(mac, BWN_PHY_EXTG(0x11), 0xf7ff);
				bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_GAINX,
				    0x0020, 0x0001);
				bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_GAINX,
				    0x0021, 0x0001);
				bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_GAINX,
				    0x0022, 0x0001);
				bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_GAINX,
				    0x0023, 0x0000);
				bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_GAINX,
				    0x0000, 0x0000);
				bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_GAINX,
				    0x0003, 0x0002);
			}
		}
	}
	if (sc->sc_board_info.board_flags & BHND_BFL_FEM) {
		BWN_PHY_WRITE(mac, BWN_PHY_GTABCTL, 0x3120);
		BWN_PHY_WRITE(mac, BWN_PHY_GTABDATA, 0xc480);
	}

	bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_UNKNOWN_11, 0, 0);
	bwn_ofdmtab_write_2(mac, BWN_OFDMTAB_UNKNOWN_11, 1, 0);
}

static void
bwn_ofdmtab_write_2(struct bwn_mac *mac, uint16_t table, uint16_t offset,
    uint16_t value)
{
	struct bwn_phy_g *pg = &mac->mac_phy.phy_g;
	uint16_t addr;

	addr = table + offset;
	if ((pg->pg_ofdmtab_dir != BWN_OFDMTAB_DIR_WRITE) ||
	    (addr - 1 != pg->pg_ofdmtab_addr)) {
		BWN_PHY_WRITE(mac, BWN_PHY_OTABLECTL, addr);
		pg->pg_ofdmtab_dir = BWN_OFDMTAB_DIR_WRITE;
	}
	pg->pg_ofdmtab_addr = addr;
	BWN_PHY_WRITE(mac, BWN_PHY_OTABLEI, value);
}

static void
bwn_ofdmtab_write_4(struct bwn_mac *mac, uint16_t table, uint16_t offset,
    uint32_t value)
{
	struct bwn_phy_g *pg = &mac->mac_phy.phy_g;
	uint16_t addr;

	addr = table + offset;
	if ((pg->pg_ofdmtab_dir != BWN_OFDMTAB_DIR_WRITE) ||
	    (addr - 1 != pg->pg_ofdmtab_addr)) {
		BWN_PHY_WRITE(mac, BWN_PHY_OTABLECTL, addr);
		pg->pg_ofdmtab_dir = BWN_OFDMTAB_DIR_WRITE;
	}
	pg->pg_ofdmtab_addr = addr;

	BWN_PHY_WRITE(mac, BWN_PHY_OTABLEI, value);
	BWN_PHY_WRITE(mac, BWN_PHY_OTABLEQ, (value >> 16));
}

static void
bwn_gtab_write(struct bwn_mac *mac, uint16_t table, uint16_t offset,
    uint16_t value)
{

	BWN_PHY_WRITE(mac, BWN_PHY_GTABCTL, table + offset);
	BWN_PHY_WRITE(mac, BWN_PHY_GTABDATA, value);
}

static void
bwn_lo_write(struct bwn_mac *mac, struct bwn_loctl *ctl)
{
	uint16_t value;

	KASSERT(mac->mac_phy.type == BWN_PHYTYPE_G,
	    ("%s:%d: fail", __func__, __LINE__));

	value = (uint8_t) (ctl->q);
	value |= ((uint8_t) (ctl->i)) << 8;
	BWN_PHY_WRITE(mac, BWN_PHY_LO_CTL, value);
}

static uint16_t
bwn_lo_calcfeed(struct bwn_mac *mac,
    uint16_t lna, uint16_t pga, uint16_t trsw_rx)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_softc *sc = mac->mac_sc;
	uint16_t rfover;
	uint16_t feedthrough;

	if (phy->gmode) {
		lna <<= BWN_PHY_RFOVERVAL_LNA_SHIFT;
		pga <<= BWN_PHY_RFOVERVAL_PGA_SHIFT;

		KASSERT((lna & ~BWN_PHY_RFOVERVAL_LNA) == 0,
		    ("%s:%d: fail", __func__, __LINE__));
		KASSERT((pga & ~BWN_PHY_RFOVERVAL_PGA) == 0,
		    ("%s:%d: fail", __func__, __LINE__));

		trsw_rx &= (BWN_PHY_RFOVERVAL_TRSWRX | BWN_PHY_RFOVERVAL_BW);

		rfover = BWN_PHY_RFOVERVAL_UNK | pga | lna | trsw_rx;
		if ((sc->sc_board_info.board_flags & BHND_BFL_EXTLNA) &&
		    phy->rev > 6)
			rfover |= BWN_PHY_RFOVERVAL_EXTLNA;

		BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, 0xe300);
		BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL, rfover);
		DELAY(10);
		rfover |= BWN_PHY_RFOVERVAL_BW_LBW;
		BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL, rfover);
		DELAY(10);
		rfover |= BWN_PHY_RFOVERVAL_BW_LPF;
		BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL, rfover);
		DELAY(10);
		BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, 0xf300);
	} else {
		pga |= BWN_PHY_PGACTL_UNKNOWN;
		BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, pga);
		DELAY(10);
		pga |= BWN_PHY_PGACTL_LOWBANDW;
		BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, pga);
		DELAY(10);
		pga |= BWN_PHY_PGACTL_LPF;
		BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, pga);
	}
	DELAY(21);
	feedthrough = BWN_PHY_READ(mac, BWN_PHY_LO_LEAKAGE);

	return (feedthrough);
}

static uint16_t
bwn_lo_txctl_regtable(struct bwn_mac *mac,
    uint16_t *value, uint16_t *pad_mix_gain)
{
	struct bwn_phy *phy = &mac->mac_phy;
	uint16_t reg, v, padmix;

	if (phy->type == BWN_PHYTYPE_B) {
		v = 0x30;
		if (phy->rf_rev <= 5) {
			reg = 0x43;
			padmix = 0;
		} else {
			reg = 0x52;
			padmix = 5;
		}
	} else {
		if (phy->rev >= 2 && phy->rf_rev == 8) {
			reg = 0x43;
			v = 0x10;
			padmix = 2;
		} else {
			reg = 0x52;
			v = 0x30;
			padmix = 5;
		}
	}
	if (value)
		*value = v;
	if (pad_mix_gain)
		*pad_mix_gain = padmix;

	return (reg);
}

static void
bwn_lo_measure_txctl_values(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_txpwr_loctl *lo = &pg->pg_loctl;
	uint16_t reg, mask;
	uint16_t trsw_rx, pga;
	uint16_t rf_pctl_reg;

	static const uint8_t tx_bias_values[] = {
		0x09, 0x08, 0x0a, 0x01, 0x00,
		0x02, 0x05, 0x04, 0x06,
	};
	static const uint8_t tx_magn_values[] = {
		0x70, 0x40,
	};

	if (!BWN_HAS_LOOPBACK(phy)) {
		rf_pctl_reg = 6;
		trsw_rx = 2;
		pga = 0;
	} else {
		int lb_gain;

		trsw_rx = 0;
		lb_gain = pg->pg_max_lb_gain / 2;
		if (lb_gain > 10) {
			rf_pctl_reg = 0;
			pga = abs(10 - lb_gain) / 6;
			pga = MIN(MAX(pga, 0), 15);
		} else {
			int cmp_val;
			int tmp;

			pga = 0;
			cmp_val = 0x24;
			if ((phy->rev >= 2) &&
			    (phy->rf_ver == 0x2050) && (phy->rf_rev == 8))
				cmp_val = 0x3c;
			tmp = lb_gain;
			if ((10 - lb_gain) < cmp_val)
				tmp = (10 - lb_gain);
			if (tmp < 0)
				tmp += 6;
			else
				tmp += 3;
			cmp_val /= 4;
			tmp /= 4;
			if (tmp >= cmp_val)
				rf_pctl_reg = cmp_val;
			else
				rf_pctl_reg = tmp;
		}
	}
	BWN_RF_SETMASK(mac, 0x43, 0xfff0, rf_pctl_reg);
	bwn_phy_g_set_bbatt(mac, 2);

	reg = bwn_lo_txctl_regtable(mac, &mask, NULL);
	mask = ~mask;
	BWN_RF_MASK(mac, reg, mask);

	if (BWN_HAS_TXMAG(phy)) {
		int i, j;
		int feedthrough;
		int min_feedth = 0xffff;
		uint8_t tx_magn, tx_bias;

		for (i = 0; i < N(tx_magn_values); i++) {
			tx_magn = tx_magn_values[i];
			BWN_RF_SETMASK(mac, 0x52, 0xff0f, tx_magn);
			for (j = 0; j < N(tx_bias_values); j++) {
				tx_bias = tx_bias_values[j];
				BWN_RF_SETMASK(mac, 0x52, 0xfff0, tx_bias);
				feedthrough = bwn_lo_calcfeed(mac, 0, pga,
				    trsw_rx);
				if (feedthrough < min_feedth) {
					lo->tx_bias = tx_bias;
					lo->tx_magn = tx_magn;
					min_feedth = feedthrough;
				}
				if (lo->tx_bias == 0)
					break;
			}
			BWN_RF_WRITE(mac, 0x52,
					  (BWN_RF_READ(mac, 0x52)
					   & 0xff00) | lo->tx_bias | lo->
					  tx_magn);
		}
	} else {
		lo->tx_magn = 0;
		lo->tx_bias = 0;
		BWN_RF_MASK(mac, 0x52, 0xfff0);
	}

	BWN_GETTIME(lo->txctl_measured_time);
}

static void
bwn_lo_get_powervector(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_txpwr_loctl *lo = &pg->pg_loctl;
	int i;
	uint64_t tmp;
	uint64_t power_vector = 0;

	for (i = 0; i < 8; i += 2) {
		tmp = bwn_shm_read_2(mac, BWN_SHARED, 0x310 + i);
		power_vector |= (tmp << (i * 8));
		bwn_shm_write_2(mac, BWN_SHARED, 0x310 + i, 0);
	}
	if (power_vector)
		lo->power_vector = power_vector;

	BWN_GETTIME(lo->pwr_vec_read_time);
}

static void
bwn_lo_measure_gain_values(struct bwn_mac *mac, int16_t max_rx_gain,
    int use_trsw_rx)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	uint16_t tmp;

	if (max_rx_gain < 0)
		max_rx_gain = 0;

	if (BWN_HAS_LOOPBACK(phy)) {
		int trsw_rx = 0;
		int trsw_rx_gain;

		if (use_trsw_rx) {
			trsw_rx_gain = pg->pg_trsw_rx_gain / 2;
			if (max_rx_gain >= trsw_rx_gain) {
				trsw_rx_gain = max_rx_gain - trsw_rx_gain;
				trsw_rx = 0x20;
			}
		} else
			trsw_rx_gain = max_rx_gain;
		if (trsw_rx_gain < 9) {
			pg->pg_lna_lod_gain = 0;
		} else {
			pg->pg_lna_lod_gain = 1;
			trsw_rx_gain -= 8;
		}
		trsw_rx_gain = MIN(MAX(trsw_rx_gain, 0), 0x2d);
		pg->pg_pga_gain = trsw_rx_gain / 3;
		if (pg->pg_pga_gain >= 5) {
			pg->pg_pga_gain -= 5;
			pg->pg_lna_gain = 2;
		} else
			pg->pg_lna_gain = 0;
	} else {
		pg->pg_lna_gain = 0;
		pg->pg_trsw_rx_gain = 0x20;
		if (max_rx_gain >= 0x14) {
			pg->pg_lna_lod_gain = 1;
			pg->pg_pga_gain = 2;
		} else if (max_rx_gain >= 0x12) {
			pg->pg_lna_lod_gain = 1;
			pg->pg_pga_gain = 1;
		} else if (max_rx_gain >= 0xf) {
			pg->pg_lna_lod_gain = 1;
			pg->pg_pga_gain = 0;
		} else {
			pg->pg_lna_lod_gain = 0;
			pg->pg_pga_gain = 0;
		}
	}

	tmp = BWN_RF_READ(mac, 0x7a);
	if (pg->pg_lna_lod_gain == 0)
		tmp &= ~0x0008;
	else
		tmp |= 0x0008;
	BWN_RF_WRITE(mac, 0x7a, tmp);
}

static void
bwn_lo_save(struct bwn_mac *mac, struct bwn_lo_g_value *sav)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_txpwr_loctl *lo = &pg->pg_loctl;
	struct timespec ts;
	uint16_t tmp;

	if (bwn_has_hwpctl(mac)) {
		sav->phy_lomask = BWN_PHY_READ(mac, BWN_PHY_LO_MASK);
		sav->phy_extg = BWN_PHY_READ(mac, BWN_PHY_EXTG(0x01));
		sav->phy_dacctl_hwpctl = BWN_PHY_READ(mac, BWN_PHY_DACCTL);
		sav->phy_cck4 = BWN_PHY_READ(mac, BWN_PHY_CCK(0x14));
		sav->phy_hpwr_tssictl = BWN_PHY_READ(mac, BWN_PHY_HPWR_TSSICTL);

		BWN_PHY_SET(mac, BWN_PHY_HPWR_TSSICTL, 0x100);
		BWN_PHY_SET(mac, BWN_PHY_EXTG(0x01), 0x40);
		BWN_PHY_SET(mac, BWN_PHY_DACCTL, 0x40);
		BWN_PHY_SET(mac, BWN_PHY_CCK(0x14), 0x200);
	}
	if (phy->type == BWN_PHYTYPE_B &&
	    phy->rf_ver == 0x2050 && phy->rf_rev < 6) {
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x16), 0x410);
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x17), 0x820);
	}
	if (phy->rev >= 2) {
		sav->phy_analogover = BWN_PHY_READ(mac, BWN_PHY_ANALOGOVER);
		sav->phy_analogoverval =
		    BWN_PHY_READ(mac, BWN_PHY_ANALOGOVERVAL);
		sav->phy_rfover = BWN_PHY_READ(mac, BWN_PHY_RFOVER);
		sav->phy_rfoverval = BWN_PHY_READ(mac, BWN_PHY_RFOVERVAL);
		sav->phy_classctl = BWN_PHY_READ(mac, BWN_PHY_CLASSCTL);
		sav->phy_cck3 = BWN_PHY_READ(mac, BWN_PHY_CCK(0x3e));
		sav->phy_crs0 = BWN_PHY_READ(mac, BWN_PHY_CRS0);

		BWN_PHY_MASK(mac, BWN_PHY_CLASSCTL, 0xfffc);
		BWN_PHY_MASK(mac, BWN_PHY_CRS0, 0x7fff);
		BWN_PHY_SET(mac, BWN_PHY_ANALOGOVER, 0x0003);
		BWN_PHY_MASK(mac, BWN_PHY_ANALOGOVERVAL, 0xfffc);
		if (phy->type == BWN_PHYTYPE_G) {
			if ((phy->rev >= 7) &&
			    (sc->sc_board_info.board_flags &
			     BHND_BFL_EXTLNA)) {
				BWN_PHY_WRITE(mac, BWN_PHY_RFOVER, 0x933);
			} else {
				BWN_PHY_WRITE(mac, BWN_PHY_RFOVER, 0x133);
			}
		} else {
			BWN_PHY_WRITE(mac, BWN_PHY_RFOVER, 0);
		}
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x3e), 0);
	}
	sav->reg0 = BWN_READ_2(mac, 0x3f4);
	sav->reg1 = BWN_READ_2(mac, 0x3e2);
	sav->rf0 = BWN_RF_READ(mac, 0x43);
	sav->rf1 = BWN_RF_READ(mac, 0x7a);
	sav->phy_pgactl = BWN_PHY_READ(mac, BWN_PHY_PGACTL);
	sav->phy_cck2 = BWN_PHY_READ(mac, BWN_PHY_CCK(0x2a));
	sav->phy_syncctl = BWN_PHY_READ(mac, BWN_PHY_SYNCCTL);
	sav->phy_dacctl = BWN_PHY_READ(mac, BWN_PHY_DACCTL);

	if (!BWN_HAS_TXMAG(phy)) {
		sav->rf2 = BWN_RF_READ(mac, 0x52);
		sav->rf2 &= 0x00f0;
	}
	if (phy->type == BWN_PHYTYPE_B) {
		sav->phy_cck0 = BWN_PHY_READ(mac, BWN_PHY_CCK(0x30));
		sav->phy_cck1 = BWN_PHY_READ(mac, BWN_PHY_CCK(0x06));
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x30), 0x00ff);
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x06), 0x3f3f);
	} else {
		BWN_WRITE_2(mac, 0x3e2, BWN_READ_2(mac, 0x3e2)
			    | 0x8000);
	}
	BWN_WRITE_2(mac, 0x3f4, BWN_READ_2(mac, 0x3f4)
		    & 0xf000);

	tmp =
	    (phy->type == BWN_PHYTYPE_G) ? BWN_PHY_LO_MASK : BWN_PHY_CCK(0x2e);
	BWN_PHY_WRITE(mac, tmp, 0x007f);

	tmp = sav->phy_syncctl;
	BWN_PHY_WRITE(mac, BWN_PHY_SYNCCTL, tmp & 0xff7f);
	tmp = sav->rf1;
	BWN_RF_WRITE(mac, 0x007a, tmp & 0xfff0);

	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2a), 0x8a3);
	if (phy->type == BWN_PHYTYPE_G ||
	    (phy->type == BWN_PHYTYPE_B &&
	     phy->rf_ver == 0x2050 && phy->rf_rev >= 6)) {
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2b), 0x1003);
	} else
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2b), 0x0802);
	if (phy->rev >= 2)
		bwn_dummy_transmission(mac, 0, 1);
	bwn_phy_g_switch_chan(mac, 6, 0);
	BWN_RF_READ(mac, 0x51);
	if (phy->type == BWN_PHYTYPE_G)
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2f), 0);

	nanouptime(&ts);
	if (ieee80211_time_before(lo->txctl_measured_time,
	    (ts.tv_nsec / 1000000 + ts.tv_sec * 1000) - BWN_LO_TXCTL_EXPIRE))
		bwn_lo_measure_txctl_values(mac);

	if (phy->type == BWN_PHYTYPE_G && phy->rev >= 3)
		BWN_PHY_WRITE(mac, BWN_PHY_LO_MASK, 0xc078);
	else {
		if (phy->type == BWN_PHYTYPE_B)
			BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2e), 0x8078);
		else
			BWN_PHY_WRITE(mac, BWN_PHY_LO_MASK, 0x8078);
	}
}

static void
bwn_lo_restore(struct bwn_mac *mac, struct bwn_lo_g_value *sav)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	uint16_t tmp;

	if (phy->rev >= 2) {
		BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, 0xe300);
		tmp = (pg->pg_pga_gain << 8);
		BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL, tmp | 0xa0);
		DELAY(5);
		BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL, tmp | 0xa2);
		DELAY(2);
		BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL, tmp | 0xa3);
	} else {
		tmp = (pg->pg_pga_gain | 0xefa0);
		BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, tmp);
	}
	if (phy->type == BWN_PHYTYPE_G) {
		if (phy->rev >= 3)
			BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2e), 0xc078);
		else
			BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2e), 0x8078);
		if (phy->rev >= 2)
			BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2f), 0x0202);
		else
			BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2f), 0x0101);
	}
	BWN_WRITE_2(mac, 0x3f4, sav->reg0);
	BWN_PHY_WRITE(mac, BWN_PHY_PGACTL, sav->phy_pgactl);
	BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x2a), sav->phy_cck2);
	BWN_PHY_WRITE(mac, BWN_PHY_SYNCCTL, sav->phy_syncctl);
	BWN_PHY_WRITE(mac, BWN_PHY_DACCTL, sav->phy_dacctl);
	BWN_RF_WRITE(mac, 0x43, sav->rf0);
	BWN_RF_WRITE(mac, 0x7a, sav->rf1);
	if (!BWN_HAS_TXMAG(phy)) {
		tmp = sav->rf2;
		BWN_RF_SETMASK(mac, 0x52, 0xff0f, tmp);
	}
	BWN_WRITE_2(mac, 0x3e2, sav->reg1);
	if (phy->type == BWN_PHYTYPE_B &&
	    phy->rf_ver == 0x2050 && phy->rf_rev <= 5) {
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x30), sav->phy_cck0);
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x06), sav->phy_cck1);
	}
	if (phy->rev >= 2) {
		BWN_PHY_WRITE(mac, BWN_PHY_ANALOGOVER, sav->phy_analogover);
		BWN_PHY_WRITE(mac, BWN_PHY_ANALOGOVERVAL,
			      sav->phy_analogoverval);
		BWN_PHY_WRITE(mac, BWN_PHY_CLASSCTL, sav->phy_classctl);
		BWN_PHY_WRITE(mac, BWN_PHY_RFOVER, sav->phy_rfover);
		BWN_PHY_WRITE(mac, BWN_PHY_RFOVERVAL, sav->phy_rfoverval);
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x3e), sav->phy_cck3);
		BWN_PHY_WRITE(mac, BWN_PHY_CRS0, sav->phy_crs0);
	}
	if (bwn_has_hwpctl(mac)) {
		tmp = (sav->phy_lomask & 0xbfff);
		BWN_PHY_WRITE(mac, BWN_PHY_LO_MASK, tmp);
		BWN_PHY_WRITE(mac, BWN_PHY_EXTG(0x01), sav->phy_extg);
		BWN_PHY_WRITE(mac, BWN_PHY_DACCTL, sav->phy_dacctl_hwpctl);
		BWN_PHY_WRITE(mac, BWN_PHY_CCK(0x14), sav->phy_cck4);
		BWN_PHY_WRITE(mac, BWN_PHY_HPWR_TSSICTL, sav->phy_hpwr_tssictl);
	}
	bwn_phy_g_switch_chan(mac, sav->old_channel, 1);
}

static int
bwn_lo_probe_loctl(struct bwn_mac *mac,
    struct bwn_loctl *probe, struct bwn_lo_g_sm *d)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_loctl orig, test;
	struct bwn_loctl prev = { -100, -100 };
	static const struct bwn_loctl modifiers[] = {
		{  1,  1,}, {  1,  0,}, {  1, -1,}, {  0, -1,},
		{ -1, -1,}, { -1,  0,}, { -1,  1,}, {  0,  1,}
	};
	int begin, end, lower = 0, i;
	uint16_t feedth;

	if (d->curstate == 0) {
		begin = 1;
		end = 8;
	} else if (d->curstate % 2 == 0) {
		begin = d->curstate - 1;
		end = d->curstate + 1;
	} else {
		begin = d->curstate - 2;
		end = d->curstate + 2;
	}
	if (begin < 1)
		begin += 8;
	if (end > 8)
		end -= 8;

	memcpy(&orig, probe, sizeof(struct bwn_loctl));
	i = begin;
	d->curstate = i;
	while (1) {
		KASSERT(i >= 1 && i <= 8, ("%s:%d: fail", __func__, __LINE__));
		memcpy(&test, &orig, sizeof(struct bwn_loctl));
		test.i += modifiers[i - 1].i * d->multipler;
		test.q += modifiers[i - 1].q * d->multipler;
		if ((test.i != prev.i || test.q != prev.q) &&
		    (abs(test.i) <= 16 && abs(test.q) <= 16)) {
			bwn_lo_write(mac, &test);
			feedth = bwn_lo_calcfeed(mac, pg->pg_lna_gain,
			    pg->pg_pga_gain, pg->pg_trsw_rx_gain);
			if (feedth < d->feedth) {
				memcpy(probe, &test,
				    sizeof(struct bwn_loctl));
				lower = 1;
				d->feedth = feedth;
				if (d->nmeasure < 2 && !BWN_HAS_LOOPBACK(phy))
					break;
			}
		}
		memcpy(&prev, &test, sizeof(prev));
		if (i == end)
			break;
		if (i == 8)
			i = 1;
		else
			i++;
		d->curstate = i;
	}

	return (lower);
}

static void
bwn_lo_probe_sm(struct bwn_mac *mac, struct bwn_loctl *loctl, int *rxgain)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_lo_g_sm d;
	struct bwn_loctl probe;
	int lower, repeat, cnt = 0;
	uint16_t feedth;

	d.nmeasure = 0;
	d.multipler = 1;
	if (BWN_HAS_LOOPBACK(phy))
		d.multipler = 3;

	memcpy(&d.loctl, loctl, sizeof(struct bwn_loctl));
	repeat = (BWN_HAS_LOOPBACK(phy)) ? 4 : 1;

	do {
		bwn_lo_write(mac, &d.loctl);
		feedth = bwn_lo_calcfeed(mac, pg->pg_lna_gain,
		    pg->pg_pga_gain, pg->pg_trsw_rx_gain);
		if (feedth < 0x258) {
			if (feedth >= 0x12c)
				*rxgain += 6;
			else
				*rxgain += 3;
			feedth = bwn_lo_calcfeed(mac, pg->pg_lna_gain,
			    pg->pg_pga_gain, pg->pg_trsw_rx_gain);
		}
		d.feedth = feedth;
		d.curstate = 0;
		do {
			KASSERT(d.curstate >= 0 && d.curstate <= 8,
			    ("%s:%d: fail", __func__, __LINE__));
			memcpy(&probe, &d.loctl,
			       sizeof(struct bwn_loctl));
			lower = bwn_lo_probe_loctl(mac, &probe, &d);
			if (!lower)
				break;
			if ((probe.i == d.loctl.i) && (probe.q == d.loctl.q))
				break;
			memcpy(&d.loctl, &probe, sizeof(struct bwn_loctl));
			d.nmeasure++;
		} while (d.nmeasure < 24);
		memcpy(loctl, &d.loctl, sizeof(struct bwn_loctl));

		if (BWN_HAS_LOOPBACK(phy)) {
			if (d.feedth > 0x1194)
				*rxgain -= 6;
			else if (d.feedth < 0x5dc)
				*rxgain += 3;
			if (cnt == 0) {
				if (d.feedth <= 0x5dc) {
					d.multipler = 1;
					cnt++;
				} else
					d.multipler = 2;
			} else if (cnt == 2)
				d.multipler = 1;
		}
		bwn_lo_measure_gain_values(mac, *rxgain, BWN_HAS_LOOPBACK(phy));
	} while (++cnt < repeat);
}

static struct bwn_lo_calib *
bwn_lo_calibset(struct bwn_mac *mac,
    const struct bwn_bbatt *bbatt, const struct bwn_rfatt *rfatt)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_loctl loctl = { 0, 0 };
	struct bwn_lo_calib *cal;
	struct bwn_lo_g_value sval = { 0 };
	int rxgain;
	uint16_t pad, reg, value;

	sval.old_channel = phy->chan;
	bwn_mac_suspend(mac);
	bwn_lo_save(mac, &sval);

	reg = bwn_lo_txctl_regtable(mac, &value, &pad);
	BWN_RF_SETMASK(mac, 0x43, 0xfff0, rfatt->att);
	BWN_RF_SETMASK(mac, reg, ~value, (rfatt->padmix ? value :0));

	rxgain = (rfatt->att * 2) + (bbatt->att / 2);
	if (rfatt->padmix)
		rxgain -= pad;
	if (BWN_HAS_LOOPBACK(phy))
		rxgain += pg->pg_max_lb_gain;
	bwn_lo_measure_gain_values(mac, rxgain, BWN_HAS_LOOPBACK(phy));
	bwn_phy_g_set_bbatt(mac, bbatt->att);
	bwn_lo_probe_sm(mac, &loctl, &rxgain);

	bwn_lo_restore(mac, &sval);
	bwn_mac_enable(mac);

	cal = malloc(sizeof(*cal), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!cal) {
		device_printf(mac->mac_sc->sc_dev, "out of memory\n");
		return (NULL);
	}
	memcpy(&cal->bbatt, bbatt, sizeof(*bbatt));
	memcpy(&cal->rfatt, rfatt, sizeof(*rfatt));
	memcpy(&cal->ctl, &loctl, sizeof(loctl));

	BWN_GETTIME(cal->calib_time);

	return (cal);
}

static struct bwn_lo_calib *
bwn_lo_get_calib(struct bwn_mac *mac, const struct bwn_bbatt *bbatt,
    const struct bwn_rfatt *rfatt)
{
	struct bwn_txpwr_loctl *lo = &mac->mac_phy.phy_g.pg_loctl;
	struct bwn_lo_calib *c;

	TAILQ_FOREACH(c, &lo->calib_list, list) {
		if (!BWN_BBATTCMP(&c->bbatt, bbatt))
			continue;
		if (!BWN_RFATTCMP(&c->rfatt, rfatt))
			continue;
		return (c);
	}

	c = bwn_lo_calibset(mac, bbatt, rfatt);
	if (!c)
		return (NULL);
	TAILQ_INSERT_TAIL(&lo->calib_list, c, list);

	return (c);
}

static void
bwn_phy_g_dc_lookup_init(struct bwn_mac *mac, uint8_t update)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_softc *sc = mac->mac_sc;
	struct bwn_txpwr_loctl *lo = &pg->pg_loctl;
	const struct bwn_rfatt *rfatt;
	const struct bwn_bbatt *bbatt;
	uint64_t pvector;
	int i;
	int rf_offset, bb_offset;
	uint8_t changed = 0;

	KASSERT(BWN_DC_LT_SIZE == 32, ("%s:%d: fail", __func__, __LINE__));
	KASSERT(lo->rfatt.len * lo->bbatt.len <= 64,
	    ("%s:%d: fail", __func__, __LINE__));

	pvector = lo->power_vector;
	if (!update && !pvector)
		return;

	bwn_mac_suspend(mac);

	for (i = 0; i < BWN_DC_LT_SIZE * 2; i++) {
		struct bwn_lo_calib *cal;
		int idx;
		uint16_t val;

		if (!update && !(pvector & (((uint64_t)1ULL) << i)))
			continue;
		bb_offset = i / lo->rfatt.len;
		rf_offset = i % lo->rfatt.len;
		bbatt = &(lo->bbatt.array[bb_offset]);
		rfatt = &(lo->rfatt.array[rf_offset]);

		cal = bwn_lo_calibset(mac, bbatt, rfatt);
		if (!cal) {
			device_printf(sc->sc_dev, "LO: Could not "
			    "calibrate DC table entry\n");
			continue;
		}
		val = (uint8_t)(cal->ctl.q);
		val |= ((uint8_t)(cal->ctl.i)) << 4;
		free(cal, M_DEVBUF);

		idx = i / 2;
		if (i % 2)
			lo->dc_lt[idx] = (lo->dc_lt[idx] & 0x00ff)
			    | ((val & 0x00ff) << 8);
		else
			lo->dc_lt[idx] = (lo->dc_lt[idx] & 0xff00)
			    | (val & 0x00ff);
		changed = 1;
	}
	if (changed) {
		for (i = 0; i < BWN_DC_LT_SIZE; i++)
			BWN_PHY_WRITE(mac, 0x3a0 + i, lo->dc_lt[i]);
	}
	bwn_mac_enable(mac);
}

static void
bwn_lo_fixup_rfatt(struct bwn_rfatt *rf)
{

	if (!rf->padmix)
		return;
	if ((rf->att != 1) && (rf->att != 2) && (rf->att != 3))
		rf->att = 4;
}

static void
bwn_lo_g_adjust(struct bwn_mac *mac)
{
	struct bwn_phy_g *pg = &mac->mac_phy.phy_g;
	struct bwn_lo_calib *cal;
	struct bwn_rfatt rf;

	memcpy(&rf, &pg->pg_rfatt, sizeof(rf));
	bwn_lo_fixup_rfatt(&rf);

	cal = bwn_lo_get_calib(mac, &pg->pg_bbatt, &rf);
	if (!cal)
		return;
	bwn_lo_write(mac, &cal->ctl);
}

static void
bwn_lo_g_init(struct bwn_mac *mac)
{

	if (!bwn_has_hwpctl(mac))
		return;

	bwn_lo_get_powervector(mac);
	bwn_phy_g_dc_lookup_init(mac, 1);
}

static int16_t
bwn_nrssi_read(struct bwn_mac *mac, uint16_t offset)
{

	BWN_PHY_WRITE(mac, BWN_PHY_NRSSI_CTRL, offset);
	return ((int16_t)BWN_PHY_READ(mac, BWN_PHY_NRSSI_DATA));
}

static void
bwn_nrssi_threshold(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_softc *sc = mac->mac_sc;
	int32_t a, b;
	int16_t tmp16;
	uint16_t tmpu16;

	KASSERT(phy->type == BWN_PHYTYPE_G, ("%s: fail", __func__));

	if (phy->gmode && (sc->sc_board_info.board_flags & BHND_BFL_ADCDIV)) {
		if (!pg->pg_aci_wlan_automatic && pg->pg_aci_enable) {
			a = 0x13;
			b = 0x12;
		} else {
			a = 0xe;
			b = 0x11;
		}

		a = a * (pg->pg_nrssi[1] - pg->pg_nrssi[0]);
		a += (pg->pg_nrssi[0] << 6);
		a += (a < 32) ? 31 : 32;
		a = a >> 6;
		a = MIN(MAX(a, -31), 31);

		b = b * (pg->pg_nrssi[1] - pg->pg_nrssi[0]);
		b += (pg->pg_nrssi[0] << 6);
		if (b < 32)
			b += 31;
		else
			b += 32;
		b = b >> 6;
		b = MIN(MAX(b, -31), 31);

		tmpu16 = BWN_PHY_READ(mac, 0x048a) & 0xf000;
		tmpu16 |= ((uint32_t)b & 0x0000003f);
		tmpu16 |= (((uint32_t)a & 0x0000003f) << 6);
		BWN_PHY_WRITE(mac, 0x048a, tmpu16);
		return;
	}

	tmp16 = bwn_nrssi_read(mac, 0x20);
	if (tmp16 >= 0x20)
		tmp16 -= 0x40;
	BWN_PHY_SETMASK(mac, 0x048a, 0xf000, (tmp16 < 3) ? 0x09eb : 0x0aed);
}

static void
bwn_nrssi_slope_11g(struct bwn_mac *mac)
{
#define	SAVE_RF_MAX		3
#define	SAVE_PHY_COMM_MAX	4
#define	SAVE_PHY3_MAX		8
	static const uint16_t save_rf_regs[SAVE_RF_MAX] =
		{ 0x7a, 0x52, 0x43 };
	static const uint16_t save_phy_comm_regs[SAVE_PHY_COMM_MAX] =
		{ 0x15, 0x5a, 0x59, 0x58 };
	static const uint16_t save_phy3_regs[SAVE_PHY3_MAX] = {
		0x002e, 0x002f, 0x080f, BWN_PHY_G_LOCTL,
		0x0801, 0x0060, 0x0014, 0x0478
	};
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	int32_t i, tmp32, phy3_idx = 0;
	uint16_t delta, tmp;
	uint16_t save_rf[SAVE_RF_MAX];
	uint16_t save_phy_comm[SAVE_PHY_COMM_MAX];
	uint16_t save_phy3[SAVE_PHY3_MAX];
	uint16_t ant_div, phy0, chan_ex;
	int16_t nrssi0, nrssi1;

	KASSERT(phy->type == BWN_PHYTYPE_G,
	    ("%s:%d: fail", __func__, __LINE__));

	if (phy->rf_rev >= 9)
		return;
	if (phy->rf_rev == 8)
		bwn_nrssi_offset(mac);

	BWN_PHY_MASK(mac, BWN_PHY_G_CRS, 0x7fff);
	BWN_PHY_MASK(mac, 0x0802, 0xfffc);

	/*
	 * Save RF/PHY registers for later restoration
	 */
	ant_div = BWN_READ_2(mac, 0x03e2);
	BWN_WRITE_2(mac, 0x03e2, BWN_READ_2(mac, 0x03e2) | 0x8000);
	for (i = 0; i < SAVE_RF_MAX; ++i)
		save_rf[i] = BWN_RF_READ(mac, save_rf_regs[i]);
	for (i = 0; i < SAVE_PHY_COMM_MAX; ++i)
		save_phy_comm[i] = BWN_PHY_READ(mac, save_phy_comm_regs[i]);

	phy0 = BWN_READ_2(mac, BWN_PHY0);
	chan_ex = BWN_READ_2(mac, BWN_CHANNEL_EXT);
	if (phy->rev >= 3) {
		for (i = 0; i < SAVE_PHY3_MAX; ++i)
			save_phy3[i] = BWN_PHY_READ(mac, save_phy3_regs[i]);
		BWN_PHY_WRITE(mac, 0x002e, 0);
		BWN_PHY_WRITE(mac, BWN_PHY_G_LOCTL, 0);
		switch (phy->rev) {
		case 4:
		case 6:
		case 7:
			BWN_PHY_SET(mac, 0x0478, 0x0100);
			BWN_PHY_SET(mac, 0x0801, 0x0040);
			break;
		case 3:
		case 5:
			BWN_PHY_MASK(mac, 0x0801, 0xffbf);
			break;
		}
		BWN_PHY_SET(mac, 0x0060, 0x0040);
		BWN_PHY_SET(mac, 0x0014, 0x0200);
	}
	/*
	 * Calculate nrssi0
	 */
	BWN_RF_SET(mac, 0x007a, 0x0070);
	bwn_set_all_gains(mac, 0, 8, 0);
	BWN_RF_MASK(mac, 0x007a, 0x00f7);
	if (phy->rev >= 2) {
		BWN_PHY_SETMASK(mac, 0x0811, 0xffcf, 0x0030);
		BWN_PHY_SETMASK(mac, 0x0812, 0xffcf, 0x0010);
	}
	BWN_RF_SET(mac, 0x007a, 0x0080);
	DELAY(20);

	nrssi0 = (int16_t) ((BWN_PHY_READ(mac, 0x047f) >> 8) & 0x003f);
	if (nrssi0 >= 0x0020)
		nrssi0 -= 0x0040;

	/*
	 * Calculate nrssi1
	 */
	BWN_RF_MASK(mac, 0x007a, 0x007f);
	if (phy->rev >= 2)
		BWN_PHY_SETMASK(mac, 0x0003, 0xff9f, 0x0040);

	BWN_WRITE_2(mac, BWN_CHANNEL_EXT,
	    BWN_READ_2(mac, BWN_CHANNEL_EXT) | 0x2000);
	BWN_RF_SET(mac, 0x007a, 0x000f);
	BWN_PHY_WRITE(mac, 0x0015, 0xf330);
	if (phy->rev >= 2) {
		BWN_PHY_SETMASK(mac, 0x0812, 0xffcf, 0x0020);
		BWN_PHY_SETMASK(mac, 0x0811, 0xffcf, 0x0020);
	}

	bwn_set_all_gains(mac, 3, 0, 1);
	if (phy->rf_rev == 8) {
		BWN_RF_WRITE(mac, 0x0043, 0x001f);
	} else {
		tmp = BWN_RF_READ(mac, 0x0052) & 0xff0f;
		BWN_RF_WRITE(mac, 0x0052, tmp | 0x0060);
		tmp = BWN_RF_READ(mac, 0x0043) & 0xfff0;
		BWN_RF_WRITE(mac, 0x0043, tmp | 0x0009);
	}
	BWN_PHY_WRITE(mac, 0x005a, 0x0480);
	BWN_PHY_WRITE(mac, 0x0059, 0x0810);
	BWN_PHY_WRITE(mac, 0x0058, 0x000d);
	DELAY(20);
	nrssi1 = (int16_t) ((BWN_PHY_READ(mac, 0x047f) >> 8) & 0x003f);

	/*
	 * Install calculated narrow RSSI values
	 */
	if (nrssi1 >= 0x0020)
		nrssi1 -= 0x0040;
	if (nrssi0 == nrssi1)
		pg->pg_nrssi_slope = 0x00010000;
	else
		pg->pg_nrssi_slope = 0x00400000 / (nrssi0 - nrssi1);
	if (nrssi0 >= -4) {
		pg->pg_nrssi[0] = nrssi1;
		pg->pg_nrssi[1] = nrssi0;
	}

	/*
	 * Restore saved RF/PHY registers
	 */
	if (phy->rev >= 3) {
		for (phy3_idx = 0; phy3_idx < 4; ++phy3_idx) {
			BWN_PHY_WRITE(mac, save_phy3_regs[phy3_idx],
			    save_phy3[phy3_idx]);
		}
	}
	if (phy->rev >= 2) {
		BWN_PHY_MASK(mac, 0x0812, 0xffcf);
		BWN_PHY_MASK(mac, 0x0811, 0xffcf);
	}

	for (i = 0; i < SAVE_RF_MAX; ++i)
		BWN_RF_WRITE(mac, save_rf_regs[i], save_rf[i]);

	BWN_WRITE_2(mac, 0x03e2, ant_div);
	BWN_WRITE_2(mac, 0x03e6, phy0);
	BWN_WRITE_2(mac, BWN_CHANNEL_EXT, chan_ex);

	for (i = 0; i < SAVE_PHY_COMM_MAX; ++i)
		BWN_PHY_WRITE(mac, save_phy_comm_regs[i], save_phy_comm[i]);

	bwn_spu_workaround(mac, phy->chan);
	BWN_PHY_SET(mac, 0x0802, (0x0001 | 0x0002));
	bwn_set_original_gains(mac);
	BWN_PHY_SET(mac, BWN_PHY_G_CRS, 0x8000);
	if (phy->rev >= 3) {
		for (; phy3_idx < SAVE_PHY3_MAX; ++phy3_idx) {
			BWN_PHY_WRITE(mac, save_phy3_regs[phy3_idx],
			    save_phy3[phy3_idx]);
		}
	}

	delta = 0x1f - pg->pg_nrssi[0];
	for (i = 0; i < 64; i++) {
		tmp32 = (((i - delta) * pg->pg_nrssi_slope) / 0x10000) + 0x3a;
		tmp32 = MIN(MAX(tmp32, 0), 0x3f);
		pg->pg_nrssi_lt[i] = tmp32;
	}

	bwn_nrssi_threshold(mac);
#undef SAVE_RF_MAX
#undef SAVE_PHY_COMM_MAX
#undef SAVE_PHY3_MAX
}

static void
bwn_nrssi_offset(struct bwn_mac *mac)
{
#define	SAVE_RF_MAX		2
#define	SAVE_PHY_COMM_MAX	10
#define	SAVE_PHY6_MAX		8
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
	struct bwn_phy *phy = &mac->mac_phy;
	int i, phy6_idx = 0;
	uint16_t save_rf[SAVE_RF_MAX];
	uint16_t save_phy_comm[SAVE_PHY_COMM_MAX];
	uint16_t save_phy6[SAVE_PHY6_MAX];
	int16_t nrssi;
	uint16_t saved = 0xffff;

	for (i = 0; i < SAVE_PHY_COMM_MAX; ++i)
		save_phy_comm[i] = BWN_PHY_READ(mac, save_phy_comm_regs[i]);
	for (i = 0; i < SAVE_RF_MAX; ++i)
		save_rf[i] = BWN_RF_READ(mac, save_rf_regs[i]);

	BWN_PHY_MASK(mac, 0x0429, 0x7fff);
	BWN_PHY_SETMASK(mac, 0x0001, 0x3fff, 0x4000);
	BWN_PHY_SET(mac, 0x0811, 0x000c);
	BWN_PHY_SETMASK(mac, 0x0812, 0xfff3, 0x0004);
	BWN_PHY_MASK(mac, 0x0802, ~(0x1 | 0x2));
	if (phy->rev >= 6) {
		for (i = 0; i < SAVE_PHY6_MAX; ++i)
			save_phy6[i] = BWN_PHY_READ(mac, save_phy6_regs[i]);

		BWN_PHY_WRITE(mac, 0x002e, 0);
		BWN_PHY_WRITE(mac, 0x002f, 0);
		BWN_PHY_WRITE(mac, 0x080f, 0);
		BWN_PHY_WRITE(mac, 0x0810, 0);
		BWN_PHY_SET(mac, 0x0478, 0x0100);
		BWN_PHY_SET(mac, 0x0801, 0x0040);
		BWN_PHY_SET(mac, 0x0060, 0x0040);
		BWN_PHY_SET(mac, 0x0014, 0x0200);
	}
	BWN_RF_SET(mac, 0x007a, 0x0070);
	BWN_RF_SET(mac, 0x007a, 0x0080);
	DELAY(30);

	nrssi = (int16_t) ((BWN_PHY_READ(mac, 0x047f) >> 8) & 0x003f);
	if (nrssi >= 0x20)
		nrssi -= 0x40;
	if (nrssi == 31) {
		for (i = 7; i >= 4; i--) {
			BWN_RF_WRITE(mac, 0x007b, i);
			DELAY(20);
			nrssi = (int16_t) ((BWN_PHY_READ(mac, 0x047f) >> 8) &
			    0x003f);
			if (nrssi >= 0x20)
				nrssi -= 0x40;
			if (nrssi < 31 && saved == 0xffff)
				saved = i;
		}
		if (saved == 0xffff)
			saved = 4;
	} else {
		BWN_RF_MASK(mac, 0x007a, 0x007f);
		if (phy->rev != 1) {
			BWN_PHY_SET(mac, 0x0814, 0x0001);
			BWN_PHY_MASK(mac, 0x0815, 0xfffe);
		}
		BWN_PHY_SET(mac, 0x0811, 0x000c);
		BWN_PHY_SET(mac, 0x0812, 0x000c);
		BWN_PHY_SET(mac, 0x0811, 0x0030);
		BWN_PHY_SET(mac, 0x0812, 0x0030);
		BWN_PHY_WRITE(mac, 0x005a, 0x0480);
		BWN_PHY_WRITE(mac, 0x0059, 0x0810);
		BWN_PHY_WRITE(mac, 0x0058, 0x000d);
		if (phy->rev == 0)
			BWN_PHY_WRITE(mac, 0x0003, 0x0122);
		else
			BWN_PHY_SET(mac, 0x000a, 0x2000);
		if (phy->rev != 1) {
			BWN_PHY_SET(mac, 0x0814, 0x0004);
			BWN_PHY_MASK(mac, 0x0815, 0xfffb);
		}
		BWN_PHY_SETMASK(mac, 0x0003, 0xff9f, 0x0040);
		BWN_RF_SET(mac, 0x007a, 0x000f);
		bwn_set_all_gains(mac, 3, 0, 1);
		BWN_RF_SETMASK(mac, 0x0043, 0x00f0, 0x000f);
		DELAY(30);
		nrssi = (int16_t) ((BWN_PHY_READ(mac, 0x047f) >> 8) & 0x003f);
		if (nrssi >= 0x20)
			nrssi -= 0x40;
		if (nrssi == -32) {
			for (i = 0; i < 4; i++) {
				BWN_RF_WRITE(mac, 0x007b, i);
				DELAY(20);
				nrssi = (int16_t)((BWN_PHY_READ(mac,
				    0x047f) >> 8) & 0x003f);
				if (nrssi >= 0x20)
					nrssi -= 0x40;
				if (nrssi > -31 && saved == 0xffff)
					saved = i;
			}
			if (saved == 0xffff)
				saved = 3;
		} else
			saved = 0;
	}
	BWN_RF_WRITE(mac, 0x007b, saved);

	/*
	 * Restore saved RF/PHY registers
	 */
	if (phy->rev >= 6) {
		for (phy6_idx = 0; phy6_idx < 4; ++phy6_idx) {
			BWN_PHY_WRITE(mac, save_phy6_regs[phy6_idx],
			    save_phy6[phy6_idx]);
		}
	}
	if (phy->rev != 1) {
		for (i = 3; i < 5; i++)
			BWN_PHY_WRITE(mac, save_phy_comm_regs[i],
			    save_phy_comm[i]);
	}
	for (i = 5; i < SAVE_PHY_COMM_MAX; i++)
		BWN_PHY_WRITE(mac, save_phy_comm_regs[i], save_phy_comm[i]);

	for (i = SAVE_RF_MAX - 1; i >= 0; --i)
		BWN_RF_WRITE(mac, save_rf_regs[i], save_rf[i]);

	BWN_PHY_WRITE(mac, 0x0802, BWN_PHY_READ(mac, 0x0802) | 0x1 | 0x2);
	BWN_PHY_SET(mac, 0x0429, 0x8000);
	bwn_set_original_gains(mac);
	if (phy->rev >= 6) {
		for (; phy6_idx < SAVE_PHY6_MAX; ++phy6_idx) {
			BWN_PHY_WRITE(mac, save_phy6_regs[phy6_idx],
			    save_phy6[phy6_idx]);
		}
	}

	BWN_PHY_WRITE(mac, save_phy_comm_regs[0], save_phy_comm[0]);
	BWN_PHY_WRITE(mac, save_phy_comm_regs[2], save_phy_comm[2]);
	BWN_PHY_WRITE(mac, save_phy_comm_regs[1], save_phy_comm[1]);
}

static void
bwn_set_all_gains(struct bwn_mac *mac, int16_t first, int16_t second,
    int16_t third)
{
	struct bwn_phy *phy = &mac->mac_phy;
	uint16_t i;
	uint16_t start = 0x08, end = 0x18;
	uint16_t tmp;
	uint16_t table;

	if (phy->rev <= 1) {
		start = 0x10;
		end = 0x20;
	}

	table = BWN_OFDMTAB_GAINX;
	if (phy->rev <= 1)
		table = BWN_OFDMTAB_GAINX_R1;
	for (i = 0; i < 4; i++)
		bwn_ofdmtab_write_2(mac, table, i, first);

	for (i = start; i < end; i++)
		bwn_ofdmtab_write_2(mac, table, i, second);

	if (third != -1) {
		tmp = ((uint16_t) third << 14) | ((uint16_t) third << 6);
		BWN_PHY_SETMASK(mac, 0x04a0, 0xbfbf, tmp);
		BWN_PHY_SETMASK(mac, 0x04a1, 0xbfbf, tmp);
		BWN_PHY_SETMASK(mac, 0x04a2, 0xbfbf, tmp);
	}
	bwn_dummy_transmission(mac, 0, 1);
}

static void
bwn_set_original_gains(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	uint16_t i, tmp;
	uint16_t table;
	uint16_t start = 0x0008, end = 0x0018;

	if (phy->rev <= 1) {
		start = 0x0010;
		end = 0x0020;
	}

	table = BWN_OFDMTAB_GAINX;
	if (phy->rev <= 1)
		table = BWN_OFDMTAB_GAINX_R1;
	for (i = 0; i < 4; i++) {
		tmp = (i & 0xfffc);
		tmp |= (i & 0x0001) << 1;
		tmp |= (i & 0x0002) >> 1;

		bwn_ofdmtab_write_2(mac, table, i, tmp);
	}

	for (i = start; i < end; i++)
		bwn_ofdmtab_write_2(mac, table, i, i - start);

	BWN_PHY_SETMASK(mac, 0x04a0, 0xbfbf, 0x4040);
	BWN_PHY_SETMASK(mac, 0x04a1, 0xbfbf, 0x4040);
	BWN_PHY_SETMASK(mac, 0x04a2, 0xbfbf, 0x4000);
	bwn_dummy_transmission(mac, 0, 1);
}

static void
bwn_phy_hwpctl_init(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_rfatt old_rfatt, rfatt;
	struct bwn_bbatt old_bbatt, bbatt;
	struct bwn_softc *sc = mac->mac_sc;
	uint8_t old_txctl = 0;

	KASSERT(phy->type == BWN_PHYTYPE_G,
	    ("%s:%d: fail", __func__, __LINE__));

	if ((sc->sc_board_info.board_vendor == PCI_VENDOR_BROADCOM) &&
	    (sc->sc_board_info.board_type == BHND_BOARD_BU4306))
		return;

	BWN_PHY_WRITE(mac, 0x0028, 0x8018);

	BWN_WRITE_2(mac, BWN_PHY0, BWN_READ_2(mac, BWN_PHY0) & 0xffdf);

	if (!phy->gmode)
		return;
	bwn_hwpctl_early_init(mac);
	if (pg->pg_curtssi == 0) {
		if (phy->rf_ver == 0x2050 && phy->analog == 0) {
			BWN_RF_SETMASK(mac, 0x0076, 0x00f7, 0x0084);
		} else {
			memcpy(&old_rfatt, &pg->pg_rfatt, sizeof(old_rfatt));
			memcpy(&old_bbatt, &pg->pg_bbatt, sizeof(old_bbatt));
			old_txctl = pg->pg_txctl;

			bbatt.att = 11;
			if (phy->rf_rev == 8) {
				rfatt.att = 15;
				rfatt.padmix = 1;
			} else {
				rfatt.att = 9;
				rfatt.padmix = 0;
			}
			bwn_phy_g_set_txpwr_sub(mac, &bbatt, &rfatt, 0);
		}
		bwn_dummy_transmission(mac, 0, 1);
		pg->pg_curtssi = BWN_PHY_READ(mac, BWN_PHY_TSSI);
		if (phy->rf_ver == 0x2050 && phy->analog == 0)
			BWN_RF_MASK(mac, 0x0076, 0xff7b);
		else
			bwn_phy_g_set_txpwr_sub(mac, &old_bbatt,
			    &old_rfatt, old_txctl);
	}
	bwn_hwpctl_init_gphy(mac);

	/* clear TSSI */
	bwn_shm_write_2(mac, BWN_SHARED, 0x0058, 0x7f7f);
	bwn_shm_write_2(mac, BWN_SHARED, 0x005a, 0x7f7f);
	bwn_shm_write_2(mac, BWN_SHARED, 0x0070, 0x7f7f);
	bwn_shm_write_2(mac, BWN_SHARED, 0x0072, 0x7f7f);
}

static void
bwn_hwpctl_early_init(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;

	if (!bwn_has_hwpctl(mac)) {
		BWN_PHY_WRITE(mac, 0x047a, 0xc111);
		return;
	}

	BWN_PHY_MASK(mac, 0x0036, 0xfeff);
	BWN_PHY_WRITE(mac, 0x002f, 0x0202);
	BWN_PHY_SET(mac, 0x047c, 0x0002);
	BWN_PHY_SET(mac, 0x047a, 0xf000);
	if (phy->rf_ver == 0x2050 && phy->rf_rev == 8) {
		BWN_PHY_SETMASK(mac, 0x047a, 0xff0f, 0x0010);
		BWN_PHY_SET(mac, 0x005d, 0x8000);
		BWN_PHY_SETMASK(mac, 0x004e, 0xffc0, 0x0010);
		BWN_PHY_WRITE(mac, 0x002e, 0xc07f);
		BWN_PHY_SET(mac, 0x0036, 0x0400);
	} else {
		BWN_PHY_SET(mac, 0x0036, 0x0200);
		BWN_PHY_SET(mac, 0x0036, 0x0400);
		BWN_PHY_MASK(mac, 0x005d, 0x7fff);
		BWN_PHY_MASK(mac, 0x004f, 0xfffe);
		BWN_PHY_SETMASK(mac, 0x004e, 0xffc0, 0x0010);
		BWN_PHY_WRITE(mac, 0x002e, 0xc07f);
		BWN_PHY_SETMASK(mac, 0x047a, 0xff0f, 0x0010);
	}
}

static void
bwn_hwpctl_init_gphy(struct bwn_mac *mac)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_txpwr_loctl *lo = &pg->pg_loctl;
	int i;
	uint16_t nr_written = 0, tmp, value;
	uint8_t rf, bb;

	if (!bwn_has_hwpctl(mac)) {
		bwn_hf_write(mac, bwn_hf_read(mac) & ~BWN_HF_HW_POWERCTL);
		return;
	}

	BWN_PHY_SETMASK(mac, 0x0036, 0xffc0,
	    (pg->pg_idletssi - pg->pg_curtssi));
	BWN_PHY_SETMASK(mac, 0x0478, 0xff00,
	    (pg->pg_idletssi - pg->pg_curtssi));

	for (i = 0; i < 32; i++)
		bwn_ofdmtab_write_2(mac, 0x3c20, i, pg->pg_tssi2dbm[i]);
	for (i = 32; i < 64; i++)
		bwn_ofdmtab_write_2(mac, 0x3c00, i - 32, pg->pg_tssi2dbm[i]);
	for (i = 0; i < 64; i += 2) {
		value = (uint16_t) pg->pg_tssi2dbm[i];
		value |= ((uint16_t) pg->pg_tssi2dbm[i + 1]) << 8;
		BWN_PHY_WRITE(mac, 0x380 + (i / 2), value);
	}

	for (rf = 0; rf < lo->rfatt.len; rf++) {
		for (bb = 0; bb < lo->bbatt.len; bb++) {
			if (nr_written >= 0x40)
				return;
			tmp = lo->bbatt.array[bb].att;
			tmp <<= 8;
			if (phy->rf_rev == 8)
				tmp |= 0x50;
			else
				tmp |= 0x40;
			tmp |= lo->rfatt.array[rf].att;
			BWN_PHY_WRITE(mac, 0x3c0 + nr_written, tmp);
			nr_written++;
		}
	}

	BWN_PHY_MASK(mac, 0x0060, 0xffbf);
	BWN_PHY_WRITE(mac, 0x0014, 0x0000);

	KASSERT(phy->rev >= 6, ("%s:%d: fail", __func__, __LINE__));
	BWN_PHY_SET(mac, 0x0478, 0x0800);
	BWN_PHY_MASK(mac, 0x0478, 0xfeff);
	BWN_PHY_MASK(mac, 0x0801, 0xffbf);

	bwn_phy_g_dc_lookup_init(mac, 1);
	bwn_hf_write(mac, bwn_hf_read(mac) | BWN_HF_HW_POWERCTL);
}

static void
bwn_phy_g_switch_chan(struct bwn_mac *mac, int channel, uint8_t spu)
{
	struct bwn_softc	*sc = mac->mac_sc;
	int			 error;

	if (spu != 0)
		bwn_spu_workaround(mac, channel);

	BWN_WRITE_2(mac, BWN_CHANNEL, bwn_phy_g_chan2freq(channel));

	if (channel == 14) {
		uint8_t cc;

		error = bhnd_nvram_getvar_uint8(sc->sc_dev, BHND_NVAR_CC, &cc);
		if (error) {
			device_printf(sc->sc_dev, "error reading country code "
			    "from NVRAM, assuming channel 14 unavailable: %d\n",
			    error);
			cc = BWN_SPROM1_CC_WORLDWIDE;
		}

		if (cc == BWN_SPROM1_CC_JP)
			bwn_hf_write(mac,
			    bwn_hf_read(mac) & ~BWN_HF_JAPAN_CHAN14_OFF);
		else
			bwn_hf_write(mac,
			    bwn_hf_read(mac) | BWN_HF_JAPAN_CHAN14_OFF);
		BWN_WRITE_2(mac, BWN_CHANNEL_EXT,
		    BWN_READ_2(mac, BWN_CHANNEL_EXT) | (1 << 11));
		return;
	}

	BWN_WRITE_2(mac, BWN_CHANNEL_EXT,
	    BWN_READ_2(mac, BWN_CHANNEL_EXT) & 0xf7bf);
}

static uint16_t
bwn_phy_g_chan2freq(uint8_t channel)
{
	static const uint8_t bwn_phy_g_rf_channels[] = BWN_PHY_G_RF_CHANNELS;

	KASSERT(channel >= 1 && channel <= 14,
	    ("%s:%d: fail", __func__, __LINE__));

	return (bwn_phy_g_rf_channels[channel - 1]);
}

static void
bwn_phy_g_set_txpwr_sub(struct bwn_mac *mac, const struct bwn_bbatt *bbatt,
    const struct bwn_rfatt *rfatt, uint8_t txctl)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_txpwr_loctl *lo = &pg->pg_loctl;
	uint16_t bb, rf;
	uint16_t tx_bias, tx_magn;

	bb = bbatt->att;
	rf = rfatt->att;
	tx_bias = lo->tx_bias;
	tx_magn = lo->tx_magn;
	if (tx_bias == 0xff)
		tx_bias = 0;

	pg->pg_txctl = txctl;
	memmove(&pg->pg_rfatt, rfatt, sizeof(*rfatt));
	pg->pg_rfatt.padmix = (txctl & BWN_TXCTL_TXMIX) ? 1 : 0;
	memmove(&pg->pg_bbatt, bbatt, sizeof(*bbatt));
	bwn_phy_g_set_bbatt(mac, bb);
	bwn_shm_write_2(mac, BWN_SHARED, BWN_SHARED_RADIO_ATT, rf);
	if (phy->rf_ver == 0x2050 && phy->rf_rev == 8)
		BWN_RF_WRITE(mac, 0x43, (rf & 0x000f) | (txctl & 0x0070));
	else {
		BWN_RF_SETMASK(mac, 0x43, 0xfff0, (rf & 0x000f));
		BWN_RF_SETMASK(mac, 0x52, ~0x0070, (txctl & 0x0070));
	}
	if (BWN_HAS_TXMAG(phy))
		BWN_RF_WRITE(mac, 0x52, tx_magn | tx_bias);
	else
		BWN_RF_SETMASK(mac, 0x52, 0xfff0, (tx_bias & 0x000f));
	bwn_lo_g_adjust(mac);
}

static void
bwn_phy_g_set_bbatt(struct bwn_mac *mac,
    uint16_t bbatt)
{
	struct bwn_phy *phy = &mac->mac_phy;

	if (phy->analog == 0) {
		BWN_WRITE_2(mac, BWN_PHY0,
		    (BWN_READ_2(mac, BWN_PHY0) & 0xfff0) | bbatt);
		return;
	}
	if (phy->analog > 1) {
		BWN_PHY_SETMASK(mac, BWN_PHY_DACCTL, 0xffc3, bbatt << 2);
		return;
	}
	BWN_PHY_SETMASK(mac, BWN_PHY_DACCTL, 0xff87, bbatt << 3);
}

static uint16_t
bwn_rf_2050_rfoverval(struct bwn_mac *mac, uint16_t reg, uint32_t lpd)
{
	struct bwn_phy *phy = &mac->mac_phy;
	struct bwn_phy_g *pg = &phy->phy_g;
	struct bwn_softc *sc = mac->mac_sc;
	int max_lb_gain;
	uint16_t extlna;
	uint16_t i;

	if (phy->gmode == 0)
		return (0);

	if (BWN_HAS_LOOPBACK(phy)) {
		max_lb_gain = pg->pg_max_lb_gain;
		max_lb_gain += (phy->rf_rev == 8) ? 0x3e : 0x26;
		if (max_lb_gain >= 0x46) {
			extlna = 0x3000;
			max_lb_gain -= 0x46;
		} else if (max_lb_gain >= 0x3a) {
			extlna = 0x1000;
			max_lb_gain -= 0x3a;
		} else if (max_lb_gain >= 0x2e) {
			extlna = 0x2000;
			max_lb_gain -= 0x2e;
		} else {
			extlna = 0;
			max_lb_gain -= 0x10;
		}

		for (i = 0; i < 16; i++) {
			max_lb_gain -= (i * 6);
			if (max_lb_gain < 6)
				break;
		}

		if ((phy->rev < 7) ||
		    !(sc->sc_board_info.board_flags & BHND_BFL_EXTLNA)) {
			if (reg == BWN_PHY_RFOVER) {
				return (0x1b3);
			} else if (reg == BWN_PHY_RFOVERVAL) {
				extlna |= (i << 8);
				switch (lpd) {
				case BWN_LPD(0, 1, 1):
					return (0x0f92);
				case BWN_LPD(0, 0, 1):
				case BWN_LPD(1, 0, 1):
					return (0x0092 | extlna);
				case BWN_LPD(1, 0, 0):
					return (0x0093 | extlna);
				}
				KASSERT(0 == 1,
				    ("%s:%d: fail", __func__, __LINE__));
			}
			KASSERT(0 == 1, ("%s:%d: fail", __func__, __LINE__));
		} else {
			if (reg == BWN_PHY_RFOVER)
				return (0x9b3);
			if (reg == BWN_PHY_RFOVERVAL) {
				if (extlna)
					extlna |= 0x8000;
				extlna |= (i << 8);
				switch (lpd) {
				case BWN_LPD(0, 1, 1):
					return (0x8f92);
				case BWN_LPD(0, 0, 1):
					return (0x8092 | extlna);
				case BWN_LPD(1, 0, 1):
					return (0x2092 | extlna);
				case BWN_LPD(1, 0, 0):
					return (0x2093 | extlna);
				}
				KASSERT(0 == 1,
				    ("%s:%d: fail", __func__, __LINE__));
			}
			KASSERT(0 == 1, ("%s:%d: fail", __func__, __LINE__));
		}
		return (0);
	}

	if ((phy->rev < 7) ||
	    !(sc->sc_board_info.board_flags & BHND_BFL_EXTLNA)) {
		if (reg == BWN_PHY_RFOVER) {
			return (0x1b3);
		} else if (reg == BWN_PHY_RFOVERVAL) {
			switch (lpd) {
			case BWN_LPD(0, 1, 1):
				return (0x0fb2);
			case BWN_LPD(0, 0, 1):
				return (0x00b2);
			case BWN_LPD(1, 0, 1):
				return (0x30b2);
			case BWN_LPD(1, 0, 0):
				return (0x30b3);
			}
			KASSERT(0 == 1,
			    ("%s:%d: fail", __func__, __LINE__));
		}
		KASSERT(0 == 1, ("%s:%d: fail", __func__, __LINE__));
	} else {
		if (reg == BWN_PHY_RFOVER) {
			return (0x9b3);
		} else if (reg == BWN_PHY_RFOVERVAL) {
			switch (lpd) {
			case BWN_LPD(0, 1, 1):
				return (0x8fb2);
			case BWN_LPD(0, 0, 1):
				return (0x80b2);
			case BWN_LPD(1, 0, 1):
				return (0x20b2);
			case BWN_LPD(1, 0, 0):
				return (0x20b3);
			}
			KASSERT(0 == 1,
			    ("%s:%d: fail", __func__, __LINE__));
		}
		KASSERT(0 == 1, ("%s:%d: fail", __func__, __LINE__));
	}
	return (0);
}

static void
bwn_spu_workaround(struct bwn_mac *mac, uint8_t channel)
{

	if (mac->mac_phy.rf_ver != 0x2050 || mac->mac_phy.rf_rev >= 6)
		return;
	BWN_WRITE_2(mac, BWN_CHANNEL, (channel <= 10) ?
	    bwn_phy_g_chan2freq(channel + 4) : bwn_phy_g_chan2freq(1));
	DELAY(1000);
	BWN_WRITE_2(mac, BWN_CHANNEL, bwn_phy_g_chan2freq(channel));
}

static int
bwn_phy_shm_tssi_read(struct bwn_mac *mac, uint16_t shm_offset)
{
	const uint8_t ofdm = (shm_offset != BWN_SHARED_TSSI_CCK);
	unsigned int a, b, c, d;
	unsigned int avg;
	uint32_t tmp;

	tmp = bwn_shm_read_4(mac, BWN_SHARED, shm_offset);
	a = tmp & 0xff;
	b = (tmp >> 8) & 0xff;
	c = (tmp >> 16) & 0xff;
	d = (tmp >> 24) & 0xff;
	if (a == 0 || a == BWN_TSSI_MAX || b == 0 || b == BWN_TSSI_MAX ||
	    c == 0 || c == BWN_TSSI_MAX || d == 0 || d == BWN_TSSI_MAX)
		return (ENOENT);
	bwn_shm_write_4(mac, BWN_SHARED, shm_offset,
	    BWN_TSSI_MAX | (BWN_TSSI_MAX << 8) |
	    (BWN_TSSI_MAX << 16) | (BWN_TSSI_MAX << 24));

	if (ofdm) {
		a = (a + 32) & 0x3f;
		b = (b + 32) & 0x3f;
		c = (c + 32) & 0x3f;
		d = (d + 32) & 0x3f;
	}

	avg = (a + b + c + d + 2) / 4;
	if (ofdm) {
		if (bwn_shm_read_2(mac, BWN_SHARED, BWN_SHARED_HFLO)
		    & BWN_HF_4DB_CCK_POWERBOOST)
			avg = (avg >= 13) ? (avg - 13) : 0;
	}
	return (avg);
}

static void
bwn_phy_g_setatt(struct bwn_mac *mac, int *bbattp, int *rfattp)
{
	struct bwn_txpwr_loctl *lo = &mac->mac_phy.phy_g.pg_loctl;
	int rfatt = *rfattp;
	int bbatt = *bbattp;

	while (1) {
		if (rfatt > lo->rfatt.max && bbatt > lo->bbatt.max - 4)
			break;
		if (rfatt < lo->rfatt.min && bbatt < lo->bbatt.min + 4)
			break;
		if (bbatt > lo->bbatt.max && rfatt > lo->rfatt.max - 1)
			break;
		if (bbatt < lo->bbatt.min && rfatt < lo->rfatt.min + 1)
			break;
		if (bbatt > lo->bbatt.max) {
			bbatt -= 4;
			rfatt += 1;
			continue;
		}
		if (bbatt < lo->bbatt.min) {
			bbatt += 4;
			rfatt -= 1;
			continue;
		}
		if (rfatt > lo->rfatt.max) {
			rfatt -= 1;
			bbatt += 4;
			continue;
		}
		if (rfatt < lo->rfatt.min) {
			rfatt += 1;
			bbatt -= 4;
			continue;
		}
		break;
	}

	*rfattp = MIN(MAX(rfatt, lo->rfatt.min), lo->rfatt.max);
	*bbattp = MIN(MAX(bbatt, lo->bbatt.min), lo->bbatt.max);
}

static void
bwn_phy_lock(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;

	KASSERT(bhnd_get_hwrev(sc->sc_dev) >= 3,
	    ("%s: unsupported rev %d", __func__, bhnd_get_hwrev(sc->sc_dev)));

	if (ic->ic_opmode != IEEE80211_M_HOSTAP)
		bwn_psctl(mac, BWN_PS_AWAKE);
}

static void
bwn_phy_unlock(struct bwn_mac *mac)
{
	struct bwn_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;

	KASSERT(bhnd_get_hwrev(sc->sc_dev) >= 3,
	    ("%s: unsupported rev %d", __func__, bhnd_get_hwrev(sc->sc_dev)));

	if (ic->ic_opmode != IEEE80211_M_HOSTAP)
		bwn_psctl(mac, 0);
}

static void
bwn_rf_lock(struct bwn_mac *mac)
{

	BWN_WRITE_4(mac, BWN_MACCTL,
	    BWN_READ_4(mac, BWN_MACCTL) | BWN_MACCTL_RADIO_LOCK);
	BWN_READ_4(mac, BWN_MACCTL);
	DELAY(10);
}

static void
bwn_rf_unlock(struct bwn_mac *mac)
{

	BWN_READ_2(mac, BWN_PHYVER);
	BWN_WRITE_4(mac, BWN_MACCTL,
	    BWN_READ_4(mac, BWN_MACCTL) & ~BWN_MACCTL_RADIO_LOCK);
}
