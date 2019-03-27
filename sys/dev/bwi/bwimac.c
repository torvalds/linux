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
 * $DragonFly: src/sys/dev/netif/bwi/bwimac.c,v 1.13 2008/02/15 11:15:38 sephe Exp $
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

#include <sys/linker.h>
#include <sys/firmware.h>
 
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
#include <net80211/ieee80211_phy.h>

#include <machine/bus.h>

#include <dev/bwi/bitops.h>
#include <dev/bwi/if_bwireg.h>
#include <dev/bwi/if_bwivar.h>
#include <dev/bwi/bwimac.h>
#include <dev/bwi/bwirf.h>
#include <dev/bwi/bwiphy.h>

struct bwi_retry_lim {
	uint16_t	shretry;
	uint16_t	shretry_fb;
	uint16_t	lgretry;
	uint16_t	lgretry_fb;
};

static int	bwi_mac_test(struct bwi_mac *);
static int	bwi_mac_get_property(struct bwi_mac *);

static void	bwi_mac_set_retry_lim(struct bwi_mac *,
			const struct bwi_retry_lim *);
static void	bwi_mac_set_ackrates(struct bwi_mac *,
			const struct ieee80211_rate_table *rt,
			const struct ieee80211_rateset *);

static int	bwi_mac_gpio_init(struct bwi_mac *);
static int	bwi_mac_gpio_fini(struct bwi_mac *);
static void	bwi_mac_opmode_init(struct bwi_mac *);
static void	bwi_mac_hostflags_init(struct bwi_mac *);
static void	bwi_mac_bss_param_init(struct bwi_mac *);

static void	bwi_mac_fw_free(struct bwi_mac *);
static int	bwi_mac_fw_load(struct bwi_mac *);
static int	bwi_mac_fw_init(struct bwi_mac *);
static int	bwi_mac_fw_load_iv(struct bwi_mac *, const struct firmware *);

static void	bwi_mac_setup_tpctl(struct bwi_mac *);
static void	bwi_mac_adjust_tpctl(struct bwi_mac *, int, int);

static void	bwi_mac_lock(struct bwi_mac *);
static void	bwi_mac_unlock(struct bwi_mac *);

static const uint8_t bwi_sup_macrev[] = { 2, 4, 5, 6, 7, 9, 10 };

void
bwi_tmplt_write_4(struct bwi_mac *mac, uint32_t ofs, uint32_t val)
{
	struct bwi_softc *sc = mac->mac_sc;

	if (mac->mac_flags & BWI_MAC_F_BSWAP)
		val = bswap32(val);

	CSR_WRITE_4(sc, BWI_MAC_TMPLT_CTRL, ofs);
	CSR_WRITE_4(sc, BWI_MAC_TMPLT_DATA, val);
}

void
bwi_hostflags_write(struct bwi_mac *mac, uint64_t flags)
{
	uint64_t val;

	val = flags & 0xffff;
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_HFLAGS_LO, val);

	val = (flags >> 16) & 0xffff;
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_HFLAGS_MI, val);

	/* HI has unclear meaning, so leave it as it is */
}

uint64_t
bwi_hostflags_read(struct bwi_mac *mac)
{
	uint64_t flags, val;

	/* HI has unclear meaning, so don't touch it */
	flags = 0;

	val = MOBJ_READ_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_HFLAGS_MI);
	flags |= val << 16;

	val = MOBJ_READ_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_HFLAGS_LO);
	flags |= val;

	return flags;
}

uint16_t
bwi_memobj_read_2(struct bwi_mac *mac, uint16_t obj_id, uint16_t ofs0)
{
	struct bwi_softc *sc = mac->mac_sc;
	uint32_t data_reg;
	int ofs;

	data_reg = BWI_MOBJ_DATA;
	ofs = ofs0 / 4;

	if (ofs0 % 4 != 0)
		data_reg = BWI_MOBJ_DATA_UNALIGN;

	CSR_WRITE_4(sc, BWI_MOBJ_CTRL, BWI_MOBJ_CTRL_VAL(obj_id, ofs));
	return CSR_READ_2(sc, data_reg);
}

uint32_t
bwi_memobj_read_4(struct bwi_mac *mac, uint16_t obj_id, uint16_t ofs0)
{
	struct bwi_softc *sc = mac->mac_sc;
	int ofs;

	ofs = ofs0 / 4;
	if (ofs0 % 4 != 0) {
		uint32_t ret;

		CSR_WRITE_4(sc, BWI_MOBJ_CTRL, BWI_MOBJ_CTRL_VAL(obj_id, ofs));
		ret = CSR_READ_2(sc, BWI_MOBJ_DATA_UNALIGN);
		ret <<= 16;

		CSR_WRITE_4(sc, BWI_MOBJ_CTRL,
			    BWI_MOBJ_CTRL_VAL(obj_id, ofs + 1));
		ret |= CSR_READ_2(sc, BWI_MOBJ_DATA);

		return ret;
	} else {
		CSR_WRITE_4(sc, BWI_MOBJ_CTRL, BWI_MOBJ_CTRL_VAL(obj_id, ofs));
		return CSR_READ_4(sc, BWI_MOBJ_DATA);
	}
}

void
bwi_memobj_write_2(struct bwi_mac *mac, uint16_t obj_id, uint16_t ofs0,
		   uint16_t v)
{
	struct bwi_softc *sc = mac->mac_sc;
	uint32_t data_reg;
	int ofs;

	data_reg = BWI_MOBJ_DATA;
	ofs = ofs0 / 4;

	if (ofs0 % 4 != 0)
		data_reg = BWI_MOBJ_DATA_UNALIGN;

	CSR_WRITE_4(sc, BWI_MOBJ_CTRL, BWI_MOBJ_CTRL_VAL(obj_id, ofs));
	CSR_WRITE_2(sc, data_reg, v);
}

void
bwi_memobj_write_4(struct bwi_mac *mac, uint16_t obj_id, uint16_t ofs0,
		   uint32_t v)
{
	struct bwi_softc *sc = mac->mac_sc;
	int ofs;

	ofs = ofs0 / 4;
	if (ofs0 % 4 != 0) {
		CSR_WRITE_4(sc, BWI_MOBJ_CTRL, BWI_MOBJ_CTRL_VAL(obj_id, ofs));
		CSR_WRITE_2(sc, BWI_MOBJ_DATA_UNALIGN, v >> 16);

		CSR_WRITE_4(sc, BWI_MOBJ_CTRL,
			    BWI_MOBJ_CTRL_VAL(obj_id, ofs + 1));
		CSR_WRITE_2(sc, BWI_MOBJ_DATA, v & 0xffff);
	} else {
		CSR_WRITE_4(sc, BWI_MOBJ_CTRL, BWI_MOBJ_CTRL_VAL(obj_id, ofs));
		CSR_WRITE_4(sc, BWI_MOBJ_DATA, v);
	}
}

int
bwi_mac_lateattach(struct bwi_mac *mac)
{
	int error;

	if (mac->mac_rev >= 5)
		CSR_READ_4(mac->mac_sc, BWI_STATE_HI); /* dummy read */

	bwi_mac_reset(mac, 1);

	error = bwi_phy_attach(mac);
	if (error)
		return error;

	error = bwi_rf_attach(mac);
	if (error)
		return error;

	/* Link 11B/G PHY, unlink 11A PHY */
	if (mac->mac_phy.phy_mode == IEEE80211_MODE_11A)
		bwi_mac_reset(mac, 0);
	else
		bwi_mac_reset(mac, 1);

	error = bwi_mac_test(mac);
	if (error)
		return error;

	error = bwi_mac_get_property(mac);
	if (error)
		return error;

	error = bwi_rf_map_txpower(mac);
	if (error)
		return error;

	bwi_rf_off(mac);
	CSR_WRITE_2(mac->mac_sc, BWI_BBP_ATTEN, BWI_BBP_ATTEN_MAGIC);
	bwi_regwin_disable(mac->mac_sc, &mac->mac_regwin, 0);

	return 0;
}

int
bwi_mac_init(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	int error, i;

	/* Clear MAC/PHY/RF states */
	bwi_mac_setup_tpctl(mac);
	bwi_rf_clear_state(&mac->mac_rf);
	bwi_phy_clear_state(&mac->mac_phy);

	/* Enable MAC and linked it to PHY */
	if (!bwi_regwin_is_enabled(sc, &mac->mac_regwin))
		bwi_mac_reset(mac, 1);

	/* Initialize backplane */
	error = bwi_bus_init(sc, mac);
	if (error)
		return error;

	/* do timeout fixup */
	if (sc->sc_bus_regwin.rw_rev <= 5 &&
	    sc->sc_bus_regwin.rw_type != BWI_REGWIN_T_BUSPCIE) {
		CSR_SETBITS_4(sc, BWI_CONF_LO,
		__SHIFTIN(BWI_CONF_LO_SERVTO, BWI_CONF_LO_SERVTO_MASK) |
		__SHIFTIN(BWI_CONF_LO_REQTO, BWI_CONF_LO_REQTO_MASK));
	}

	/* Calibrate PHY */
	error = bwi_phy_calibrate(mac);
	if (error) {
		device_printf(sc->sc_dev, "PHY calibrate failed\n");
		return error;
	}

	/* Prepare to initialize firmware */
	CSR_WRITE_4(sc, BWI_MAC_STATUS,
		    BWI_MAC_STATUS_UCODE_JUMP0 |
		    BWI_MAC_STATUS_IHREN);

	/*
	 * Load and initialize firmwares
	 */
	error = bwi_mac_fw_load(mac);
	if (error)
		return error;

	error = bwi_mac_gpio_init(mac);
	if (error)
		return error;

	error = bwi_mac_fw_init(mac);
	if (error)
		return error;

	/*
	 * Turn on RF
	 */
	bwi_rf_on(mac);

	/* TODO: LED, hardware rf enabled is only related to LED setting */

	/*
	 * Initialize PHY
	 */
	CSR_WRITE_2(sc, BWI_BBP_ATTEN, 0);
	bwi_phy_init(mac);

	/* TODO: interference mitigation */

	/*
	 * Setup antenna mode
	 */
	bwi_rf_set_ant_mode(mac, mac->mac_rf.rf_ant_mode);

	/*
	 * Initialize operation mode (RX configuration)
	 */
	bwi_mac_opmode_init(mac);

	/* set up Beacon interval */
	if (mac->mac_rev < 3) {
		CSR_WRITE_2(sc, 0x60e, 0);
		CSR_WRITE_2(sc, 0x610, 0x8000);
		CSR_WRITE_2(sc, 0x604, 0);
		CSR_WRITE_2(sc, 0x606, 0x200);
	} else {
		CSR_WRITE_4(sc, 0x188, 0x80000000);
		CSR_WRITE_4(sc, 0x18c, 0x2000000);
	}

	/*
	 * Initialize TX/RX interrupts' mask
	 */
	CSR_WRITE_4(sc, BWI_MAC_INTR_STATUS, BWI_INTR_TIMER1);
	for (i = 0; i < BWI_TXRX_NRING; ++i) {
		uint32_t intrs;

		if (BWI_TXRX_IS_RX(i))
			intrs = BWI_TXRX_RX_INTRS;
		else
			intrs = BWI_TXRX_TX_INTRS;
		CSR_WRITE_4(sc, BWI_TXRX_INTR_MASK(i), intrs);
	}

	/* allow the MAC to control the PHY clock (dynamic on/off) */
	CSR_SETBITS_4(sc, BWI_STATE_LO, 0x100000);

	/* Setup MAC power up delay */
	CSR_WRITE_2(sc, BWI_MAC_POWERUP_DELAY, sc->sc_pwron_delay);

	/* Set MAC regwin revision */
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_MACREV, mac->mac_rev);

	/*
	 * Initialize host flags
	 */
	bwi_mac_hostflags_init(mac);

	/*
	 * Initialize BSS parameters
	 */
	bwi_mac_bss_param_init(mac);

	/*
	 * Initialize TX rings
	 */
	for (i = 0; i < BWI_TX_NRING; ++i) {
		error = sc->sc_init_tx_ring(sc, i);
		if (error) {
			device_printf(sc->sc_dev,
				  "can't initialize %dth TX ring\n", i);
			return error;
		}
	}

	/*
	 * Initialize RX ring
	 */
	error = sc->sc_init_rx_ring(sc);
	if (error) {
		device_printf(sc->sc_dev, "can't initialize RX ring\n");
		return error;
	}

	/*
	 * Initialize TX stats if the current MAC uses that
	 */
	if (mac->mac_flags & BWI_MAC_F_HAS_TXSTATS) {
		error = sc->sc_init_txstats(sc);
		if (error) {
			device_printf(sc->sc_dev,
				  "can't initialize TX stats ring\n");
			return error;
		}
	}

	/* update PRETBTT */
	CSR_WRITE_2(sc, 0x612, 0x50);	/* Force Pre-TBTT to 80? */
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, 0x416, 0x50);
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, 0x414, 0x1f4);

	mac->mac_flags |= BWI_MAC_F_INITED;
	return 0;
}

void
bwi_mac_reset(struct bwi_mac *mac, int link_phy)
{
	struct bwi_softc *sc = mac->mac_sc;
	uint32_t flags, state_lo, status;

	flags = BWI_STATE_LO_FLAG_PHYRST | BWI_STATE_LO_FLAG_PHYCLKEN;
	if (link_phy)
		flags |= BWI_STATE_LO_FLAG_PHYLNK;
	bwi_regwin_enable(sc, &mac->mac_regwin, flags);
	DELAY(2000);

	state_lo = CSR_READ_4(sc, BWI_STATE_LO);
	state_lo |= BWI_STATE_LO_GATED_CLOCK;
	state_lo &= ~__SHIFTIN(BWI_STATE_LO_FLAG_PHYRST,
			       BWI_STATE_LO_FLAGS_MASK);
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);
	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_STATE_LO);
	DELAY(1000);

	state_lo &= ~BWI_STATE_LO_GATED_CLOCK;
	CSR_WRITE_4(sc, BWI_STATE_LO, state_lo);
	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_STATE_LO);
	DELAY(1000);

	CSR_WRITE_2(sc, BWI_BBP_ATTEN, 0);

	status = CSR_READ_4(sc, BWI_MAC_STATUS);
	status |= BWI_MAC_STATUS_IHREN;
	if (link_phy)
		status |= BWI_MAC_STATUS_PHYLNK;
	else
		status &= ~BWI_MAC_STATUS_PHYLNK;
	CSR_WRITE_4(sc, BWI_MAC_STATUS, status);

	if (link_phy) {
		DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_ATTACH | BWI_DBG_INIT,
			"%s\n", "PHY is linked");
		mac->mac_phy.phy_flags |= BWI_PHY_F_LINKED;
	} else {
		DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_ATTACH | BWI_DBG_INIT,
			"%s\n", "PHY is unlinked");
		mac->mac_phy.phy_flags &= ~BWI_PHY_F_LINKED;
	}
}

void
bwi_mac_set_tpctl_11bg(struct bwi_mac *mac, const struct bwi_tpctl *new_tpctl)
{
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_tpctl *tpctl = &mac->mac_tpctl;

	if (new_tpctl != NULL) {
		KASSERT(new_tpctl->bbp_atten <= BWI_BBP_ATTEN_MAX,
		    ("bbp_atten %d", new_tpctl->bbp_atten));
		KASSERT(new_tpctl->rf_atten <=
			 (rf->rf_rev < 6 ? BWI_RF_ATTEN_MAX0
			 		 : BWI_RF_ATTEN_MAX1),
		    ("rf_atten %d", new_tpctl->rf_atten));
		KASSERT(new_tpctl->tp_ctrl1 <= BWI_TPCTL1_MAX,
		    ("tp_ctrl1 %d", new_tpctl->tp_ctrl1));

		tpctl->bbp_atten = new_tpctl->bbp_atten;
		tpctl->rf_atten = new_tpctl->rf_atten;
		tpctl->tp_ctrl1 = new_tpctl->tp_ctrl1;
	}

	/* Set BBP attenuation */
	bwi_phy_set_bbp_atten(mac, tpctl->bbp_atten);

	/* Set RF attenuation */
	RF_WRITE(mac, BWI_RFR_ATTEN, tpctl->rf_atten);
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_RF_ATTEN,
		     tpctl->rf_atten);

	/* Set TX power */
	if (rf->rf_type == BWI_RF_T_BCM2050) {
		RF_FILT_SETBITS(mac, BWI_RFR_TXPWR, ~BWI_RFR_TXPWR1_MASK,
			__SHIFTIN(tpctl->tp_ctrl1, BWI_RFR_TXPWR1_MASK));
	}

	/* Adjust RF Local Oscillator */
	if (mac->mac_phy.phy_mode == IEEE80211_MODE_11G)
		bwi_rf_lo_adjust(mac, tpctl);
}

static int
bwi_mac_test(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	uint32_t orig_val, val;

#define TEST_VAL1	0xaa5555aa
#define TEST_VAL2	0x55aaaa55

	/* Save it for later restoring */
	orig_val = MOBJ_READ_4(mac, BWI_COMM_MOBJ, 0);

	/* Test 1 */
	MOBJ_WRITE_4(mac, BWI_COMM_MOBJ, 0, TEST_VAL1);
	val = MOBJ_READ_4(mac, BWI_COMM_MOBJ, 0);
	if (val != TEST_VAL1) {
		device_printf(sc->sc_dev, "TEST1 failed\n");
		return ENXIO;
	}

	/* Test 2 */
	MOBJ_WRITE_4(mac, BWI_COMM_MOBJ, 0, TEST_VAL2);
	val = MOBJ_READ_4(mac, BWI_COMM_MOBJ, 0);
	if (val != TEST_VAL2) {
		device_printf(sc->sc_dev, "TEST2 failed\n");
		return ENXIO;
	}

	/* Restore to the original value */
	MOBJ_WRITE_4(mac, BWI_COMM_MOBJ, 0, orig_val);

	val = CSR_READ_4(sc, BWI_MAC_STATUS);
	if ((val & ~BWI_MAC_STATUS_PHYLNK) != BWI_MAC_STATUS_IHREN) {
		device_printf(sc->sc_dev, "%s failed, MAC status 0x%08x\n",
			      __func__, val);
		return ENXIO;
	}

	val = CSR_READ_4(sc, BWI_MAC_INTR_STATUS);
	if (val != 0) {
		device_printf(sc->sc_dev, "%s failed, intr status %08x\n",
			      __func__, val);
		return ENXIO;
	}

#undef TEST_VAL2
#undef TEST_VAL1

	return 0;
}

static void
bwi_mac_setup_tpctl(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_tpctl *tpctl = &mac->mac_tpctl;

	/* Calc BBP attenuation */
	if (rf->rf_type == BWI_RF_T_BCM2050 && rf->rf_rev < 6)
		tpctl->bbp_atten = 0;
	else
		tpctl->bbp_atten = 2;

	/* Calc TX power CTRL1?? */
	tpctl->tp_ctrl1 = 0;
	if (rf->rf_type == BWI_RF_T_BCM2050) {
		if (rf->rf_rev == 1)
			tpctl->tp_ctrl1 = 3;
		else if (rf->rf_rev < 6)
			tpctl->tp_ctrl1 = 2;
		else if (rf->rf_rev == 8)
			tpctl->tp_ctrl1 = 1;
	}

	/* Empty TX power CTRL2?? */
	tpctl->tp_ctrl2 = 0xffff;

	/*
	 * Calc RF attenuation
	 */
	if (phy->phy_mode == IEEE80211_MODE_11A) {
		tpctl->rf_atten = 0x60;
		goto back;
	}

	if (BWI_IS_BRCM_BCM4309G(sc) && sc->sc_pci_revid < 0x51) {
		tpctl->rf_atten = sc->sc_pci_revid < 0x43 ? 2 : 3;
		goto back;
	}

	tpctl->rf_atten = 5;

	if (rf->rf_type != BWI_RF_T_BCM2050) {
		if (rf->rf_type == BWI_RF_T_BCM2053 && rf->rf_rev == 1)
			tpctl->rf_atten = 6;
		goto back;
	}

	/*
	 * NB: If we reaches here and the card is BRCM_BCM4309G,
	 *     then the card's PCI revision must >= 0x51
	 */

	/* BCM2050 RF */
	switch (rf->rf_rev) {
	case 1:
		if (phy->phy_mode == IEEE80211_MODE_11G) {
			if (BWI_IS_BRCM_BCM4309G(sc) || BWI_IS_BRCM_BU4306(sc))
				tpctl->rf_atten = 3;
			else
				tpctl->rf_atten = 1;
		} else {
			if (BWI_IS_BRCM_BCM4309G(sc))
				tpctl->rf_atten = 7;
			else
				tpctl->rf_atten = 6;
		}
		break;
	case 2:
		if (phy->phy_mode == IEEE80211_MODE_11G) {
			/*
			 * NOTE: Order of following conditions is critical
			 */
			if (BWI_IS_BRCM_BCM4309G(sc))
				tpctl->rf_atten = 3;
			else if (BWI_IS_BRCM_BU4306(sc))
				tpctl->rf_atten = 5;
			else if (sc->sc_bbp_id == BWI_BBPID_BCM4320)
				tpctl->rf_atten = 4;
			else
				tpctl->rf_atten = 3;
		} else {
			tpctl->rf_atten = 6;
		}
		break;
	case 4:
	case 5:
		tpctl->rf_atten = 1;
		break;
	case 8:
		tpctl->rf_atten = 0x1a;
		break;
	}
back:
	DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_INIT | BWI_DBG_TXPOWER,
		"bbp atten: %u, rf atten: %u, ctrl1: %u, ctrl2: %u\n",
		tpctl->bbp_atten, tpctl->rf_atten,
		tpctl->tp_ctrl1, tpctl->tp_ctrl2);
}

void
bwi_mac_dummy_xmit(struct bwi_mac *mac)
{
#define PACKET_LEN	5
	static const uint32_t	packet_11a[PACKET_LEN] =
	{ 0x000201cc, 0x00d40000, 0x00000000, 0x01000000, 0x00000000 };
	static const uint32_t	packet_11bg[PACKET_LEN] =
	{ 0x000b846e, 0x00d40000, 0x00000000, 0x01000000, 0x00000000 };

	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	const uint32_t *packet;
	uint16_t val_50c;
	int wait_max, i;

	if (mac->mac_phy.phy_mode == IEEE80211_MODE_11A) {
		wait_max = 30;
		packet = packet_11a;
		val_50c = 1;
	} else {
		wait_max = 250;
		packet = packet_11bg;
		val_50c = 0;
	}

	for (i = 0; i < PACKET_LEN; ++i)
		TMPLT_WRITE_4(mac, i * 4, packet[i]);

	CSR_READ_4(sc, BWI_MAC_STATUS);	/* dummy read */

	CSR_WRITE_2(sc, 0x568, 0);
	CSR_WRITE_2(sc, 0x7c0, 0);
	CSR_WRITE_2(sc, 0x50c, val_50c);
	CSR_WRITE_2(sc, 0x508, 0);
	CSR_WRITE_2(sc, 0x50a, 0);
	CSR_WRITE_2(sc, 0x54c, 0);
	CSR_WRITE_2(sc, 0x56a, 0x14);
	CSR_WRITE_2(sc, 0x568, 0x826);
	CSR_WRITE_2(sc, 0x500, 0);
	CSR_WRITE_2(sc, 0x502, 0x30);

	if (rf->rf_type == BWI_RF_T_BCM2050 && rf->rf_rev <= 5)
		RF_WRITE(mac, 0x51, 0x17);

	for (i = 0; i < wait_max; ++i) {
		if (CSR_READ_2(sc, 0x50e) & 0x80)
			break;
		DELAY(10);
	}
	for (i = 0; i < 10; ++i) {
		if (CSR_READ_2(sc, 0x50e) & 0x400)
			break;
		DELAY(10);
	}
	for (i = 0; i < 10; ++i) {
		if ((CSR_READ_2(sc, 0x690) & 0x100) == 0)
			break;
		DELAY(10);
	}

	if (rf->rf_type == BWI_RF_T_BCM2050 && rf->rf_rev <= 5)
		RF_WRITE(mac, 0x51, 0x37);
#undef PACKET_LEN
}

void
bwi_mac_init_tpctl_11bg(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_tpctl tpctl_orig;
	int restore_tpctl = 0;

	KASSERT(phy->phy_mode != IEEE80211_MODE_11A,
	    ("phy_mode %d", phy->phy_mode));

	if (BWI_IS_BRCM_BU4306(sc))
		return;

	PHY_WRITE(mac, 0x28, 0x8018);
	CSR_CLRBITS_2(sc, BWI_BBP_ATTEN, 0x20);

	if (phy->phy_mode == IEEE80211_MODE_11G) {
		if ((phy->phy_flags & BWI_PHY_F_LINKED) == 0)
			return;
		PHY_WRITE(mac, 0x47a, 0xc111);
	}
	if (mac->mac_flags & BWI_MAC_F_TPCTL_INITED)
		return;

	if (phy->phy_mode == IEEE80211_MODE_11B && phy->phy_rev >= 2 &&
	    rf->rf_type == BWI_RF_T_BCM2050) {
		RF_SETBITS(mac, 0x76, 0x84);
	} else {
		struct bwi_tpctl tpctl;

		/* Backup original TX power control variables */
		bcopy(&mac->mac_tpctl, &tpctl_orig, sizeof(tpctl_orig));
		restore_tpctl = 1;

		bcopy(&mac->mac_tpctl, &tpctl, sizeof(tpctl));
		tpctl.bbp_atten = 11;
		tpctl.tp_ctrl1 = 0;
#ifdef notyet
		if (rf->rf_rev >= 6 && rf->rf_rev <= 8)
			tpctl.rf_atten = 31;
		else
#endif
			tpctl.rf_atten = 9;

		bwi_mac_set_tpctl_11bg(mac, &tpctl);
	}

	bwi_mac_dummy_xmit(mac);

	mac->mac_flags |= BWI_MAC_F_TPCTL_INITED;
	rf->rf_base_tssi = PHY_READ(mac, 0x29);
	DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_INIT | BWI_DBG_TXPOWER,
		"base tssi %d\n", rf->rf_base_tssi);

	if (abs(rf->rf_base_tssi - rf->rf_idle_tssi) >= 20) {
		device_printf(sc->sc_dev, "base tssi measure failed\n");
		mac->mac_flags |= BWI_MAC_F_TPCTL_ERROR;
	}

	if (restore_tpctl)
		bwi_mac_set_tpctl_11bg(mac, &tpctl_orig);
	else
		RF_CLRBITS(mac, 0x76, 0x84);

	bwi_rf_clear_tssi(mac);
}

void
bwi_mac_detach(struct bwi_mac *mac)
{
	bwi_mac_fw_free(mac);
}

static __inline int
bwi_fwimage_is_valid(struct bwi_softc *sc, const struct firmware *fw,
		     uint8_t fw_type)
{
	const struct bwi_fwhdr *hdr;

	if (fw->datasize < sizeof(*hdr)) {
		device_printf(sc->sc_dev,
		    "invalid firmware (%s): invalid size %zu\n",
		    fw->name, fw->datasize);
		return 0;
	}

	hdr = (const struct bwi_fwhdr *)fw->data;

	if (fw_type != BWI_FW_T_IV) {
		/*
		 * Don't verify IV's size, it has different meaning
		 */
		if (be32toh(hdr->fw_size) != fw->datasize - sizeof(*hdr)) {
			device_printf(sc->sc_dev,
			    "invalid firmware (%s): size mismatch, "
			    "fw %u, real %zu\n", fw->name,
			    be32toh(hdr->fw_size), fw->datasize - sizeof(*hdr));
			return 0;
		}
	}

	if (hdr->fw_type != fw_type) {
		device_printf(sc->sc_dev,
		    "invalid firmware (%s): type mismatch, "
		    "fw \'%c\', target \'%c\'\n", fw->name,
		    hdr->fw_type, fw_type);
		return 0;
	}

	if (hdr->fw_gen != BWI_FW_GEN_1) {
		device_printf(sc->sc_dev,
		    "invalid firmware (%s): wrong generation, "
		    "fw %d, target %d\n", fw->name, hdr->fw_gen, BWI_FW_GEN_1);
		return 0;
	}
	return 1;
}

/*
 * XXX Error cleanup
 */
int
bwi_mac_fw_alloc(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	char fwname[64];
	int idx;

	/*
	 * Try getting the firmware stub so firmware
	 * module would be loaded automatically
	 */
	if (mac->mac_stub == NULL) {
		snprintf(fwname, sizeof(fwname), BWI_FW_STUB_PATH,
			 sc->sc_fw_version);
		mac->mac_stub = firmware_get(fwname);
		if (mac->mac_stub == NULL)
			goto no_firmware;
	}

	if (mac->mac_ucode == NULL) {
		snprintf(fwname, sizeof(fwname), BWI_FW_UCODE_PATH,
			  sc->sc_fw_version,
			  mac->mac_rev >= 5 ? 5 : mac->mac_rev);

		mac->mac_ucode = firmware_get(fwname);
		if (mac->mac_ucode == NULL)
			goto no_firmware;
		if (!bwi_fwimage_is_valid(sc, mac->mac_ucode, BWI_FW_T_UCODE))
			return EINVAL;
	}

	if (mac->mac_pcm == NULL) {
		snprintf(fwname, sizeof(fwname), BWI_FW_PCM_PATH,
			  sc->sc_fw_version,
			  mac->mac_rev < 5 ? 4 : 5);

		mac->mac_pcm = firmware_get(fwname);
		if (mac->mac_pcm == NULL)
			goto no_firmware;
		if (!bwi_fwimage_is_valid(sc, mac->mac_pcm, BWI_FW_T_PCM))
			return EINVAL;
	}

	if (mac->mac_iv == NULL) {
		/* TODO: 11A */
		if (mac->mac_rev == 2 || mac->mac_rev == 4) {
			idx = 2;
		} else if (mac->mac_rev >= 5 && mac->mac_rev <= 10) {
			idx = 5;
		} else {
			device_printf(sc->sc_dev,
			    "no suitible IV for MAC rev %d\n", mac->mac_rev);
			return ENODEV;
		}

		snprintf(fwname, sizeof(fwname), BWI_FW_IV_PATH,
			  sc->sc_fw_version, idx);

		mac->mac_iv = firmware_get(fwname);
		if (mac->mac_iv == NULL)
			goto no_firmware;
		if (!bwi_fwimage_is_valid(sc, mac->mac_iv, BWI_FW_T_IV))
			return EINVAL;
	}

	if (mac->mac_iv_ext == NULL) {
		/* TODO: 11A */
		if (mac->mac_rev == 2 || mac->mac_rev == 4 ||
		    mac->mac_rev >= 11) {
			/* No extended IV */
			return (0);
		} else if (mac->mac_rev >= 5 && mac->mac_rev <= 10) {
			idx = 5;
		} else {
			device_printf(sc->sc_dev,
			    "no suitible ExtIV for MAC rev %d\n", mac->mac_rev);
			return ENODEV;
		}

		snprintf(fwname, sizeof(fwname), BWI_FW_IV_EXT_PATH,
			  sc->sc_fw_version, idx);

		mac->mac_iv_ext = firmware_get(fwname);
		if (mac->mac_iv_ext == NULL)
			goto no_firmware;
		if (!bwi_fwimage_is_valid(sc, mac->mac_iv_ext, BWI_FW_T_IV))
			return EINVAL;
	}
	return (0);

no_firmware:
	device_printf(sc->sc_dev, "request firmware %s failed\n", fwname);
	return (ENOENT);
}

static void
bwi_mac_fw_free(struct bwi_mac *mac)
{
	if (mac->mac_ucode != NULL) {
		firmware_put(mac->mac_ucode, FIRMWARE_UNLOAD);
		mac->mac_ucode = NULL;
	}

	if (mac->mac_pcm != NULL) {
		firmware_put(mac->mac_pcm, FIRMWARE_UNLOAD);
		mac->mac_pcm = NULL;
	}

	if (mac->mac_iv != NULL) {
		firmware_put(mac->mac_iv, FIRMWARE_UNLOAD);
		mac->mac_iv = NULL;
	}

	if (mac->mac_iv_ext != NULL) {
		firmware_put(mac->mac_iv_ext, FIRMWARE_UNLOAD);
		mac->mac_iv_ext = NULL;
	}

	if (mac->mac_stub != NULL) {
		firmware_put(mac->mac_stub, FIRMWARE_UNLOAD);
		mac->mac_stub = NULL;
	}
}

static int
bwi_mac_fw_load(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	const uint32_t *fw;
	uint16_t fw_rev;
	int fw_len, i;

	/*
	 * Load ucode image
	 */
	fw = (const uint32_t *)
	     ((const uint8_t *)mac->mac_ucode->data + BWI_FWHDR_SZ);
	fw_len = (mac->mac_ucode->datasize - BWI_FWHDR_SZ) / sizeof(uint32_t);

	CSR_WRITE_4(sc, BWI_MOBJ_CTRL,
		    BWI_MOBJ_CTRL_VAL(
		    BWI_FW_UCODE_MOBJ | BWI_WR_MOBJ_AUTOINC, 0));
	for (i = 0; i < fw_len; ++i) {
		CSR_WRITE_4(sc, BWI_MOBJ_DATA, be32toh(fw[i]));
		DELAY(10);
	}

	/*
	 * Load PCM image
	 */
	fw = (const uint32_t *)
	     ((const uint8_t *)mac->mac_pcm->data + BWI_FWHDR_SZ);
	fw_len = (mac->mac_pcm->datasize - BWI_FWHDR_SZ) / sizeof(uint32_t);

	CSR_WRITE_4(sc, BWI_MOBJ_CTRL,
		    BWI_MOBJ_CTRL_VAL(BWI_FW_PCM_MOBJ, 0x01ea));
	CSR_WRITE_4(sc, BWI_MOBJ_DATA, 0x4000);

	CSR_WRITE_4(sc, BWI_MOBJ_CTRL,
		    BWI_MOBJ_CTRL_VAL(BWI_FW_PCM_MOBJ, 0x01eb));
	for (i = 0; i < fw_len; ++i) {
		CSR_WRITE_4(sc, BWI_MOBJ_DATA, be32toh(fw[i]));
		DELAY(10);
	}

	CSR_WRITE_4(sc, BWI_MAC_INTR_STATUS, BWI_ALL_INTRS);
	CSR_WRITE_4(sc, BWI_MAC_STATUS,
		    BWI_MAC_STATUS_UCODE_START |
		    BWI_MAC_STATUS_IHREN |
		    BWI_MAC_STATUS_INFRA);

#define NRETRY	200

	for (i = 0; i < NRETRY; ++i) {
		uint32_t intr_status;

		intr_status = CSR_READ_4(sc, BWI_MAC_INTR_STATUS);
		if (intr_status == BWI_INTR_READY)
			break;
		DELAY(10);
	}
	if (i == NRETRY) {
		device_printf(sc->sc_dev,
		    "firmware (ucode&pcm) loading timed out\n");
		return ETIMEDOUT;
	}

#undef NRETRY

	CSR_READ_4(sc, BWI_MAC_INTR_STATUS);	/* dummy read */

	fw_rev = MOBJ_READ_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_FWREV);
	if (fw_rev > BWI_FW_VERSION3_REVMAX) {
		device_printf(sc->sc_dev,
		    "firmware version 4 is not supported yet\n");
		return ENODEV;
	}

	device_printf(sc->sc_dev,
	    "firmware rev 0x%04x, patch level 0x%04x\n", fw_rev,
	    MOBJ_READ_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_FWPATCHLV));
	return 0;
}

static int
bwi_mac_gpio_init(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_regwin *old, *gpio_rw;
	uint32_t filt, bits;
	int error;

	CSR_CLRBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_GPOSEL_MASK);
	/* TODO:LED */

	CSR_SETBITS_2(sc, BWI_MAC_GPIO_MASK, 0xf);

	filt = 0x1f;
	bits = 0xf;
	if (sc->sc_bbp_id == BWI_BBPID_BCM4301) {
		filt |= 0x60;
		bits |= 0x60;
	}
	if (sc->sc_card_flags & BWI_CARD_F_PA_GPIO9) {
		CSR_SETBITS_2(sc, BWI_MAC_GPIO_MASK, 0x200);
		filt |= 0x200;
		bits |= 0x200;
	}

	gpio_rw = BWI_GPIO_REGWIN(sc);
	error = bwi_regwin_switch(sc, gpio_rw, &old);
	if (error)
		return error;

	CSR_FILT_SETBITS_4(sc, BWI_GPIO_CTRL, filt, bits);

	return bwi_regwin_switch(sc, old, NULL);
}

static int
bwi_mac_gpio_fini(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_regwin *old, *gpio_rw;
	int error;

	gpio_rw = BWI_GPIO_REGWIN(sc);
	error = bwi_regwin_switch(sc, gpio_rw, &old);
	if (error)
		return error;

	CSR_WRITE_4(sc, BWI_GPIO_CTRL, 0);

	return bwi_regwin_switch(sc, old, NULL);
}

static int
bwi_mac_fw_load_iv(struct bwi_mac *mac, const struct firmware *fw)
{
	struct bwi_softc *sc = mac->mac_sc;
	const struct bwi_fwhdr *hdr;
	const struct bwi_fw_iv *iv;
	int n, i, iv_img_size;

	/* Get the number of IVs in the IV image */
	hdr = (const struct bwi_fwhdr *)fw->data;
	n = be32toh(hdr->fw_iv_cnt);
	DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_INIT | BWI_DBG_FIRMWARE,
		"IV count %d\n", n);

	/* Calculate the IV image size, for later sanity check */
	iv_img_size = fw->datasize - sizeof(*hdr);

	/* Locate the first IV */
	iv = (const struct bwi_fw_iv *)
	     ((const uint8_t *)fw->data + sizeof(*hdr));

	for (i = 0; i < n; ++i) {
		uint16_t iv_ofs, ofs;
		int sz = 0;

		if (iv_img_size < sizeof(iv->iv_ofs)) {
			device_printf(sc->sc_dev, "invalid IV image, ofs\n");
			return EINVAL;
		}
		iv_img_size -= sizeof(iv->iv_ofs);
		sz += sizeof(iv->iv_ofs);

		iv_ofs = be16toh(iv->iv_ofs);

		ofs = __SHIFTOUT(iv_ofs, BWI_FW_IV_OFS_MASK);
		if (ofs >= 0x1000) {
			device_printf(sc->sc_dev, "invalid ofs (0x%04x) "
				  "for %dth iv\n", ofs, i);
			return EINVAL;
		}

		if (iv_ofs & BWI_FW_IV_IS_32BIT) {
			uint32_t val32;

			if (iv_img_size < sizeof(iv->iv_val.val32)) {
				device_printf(sc->sc_dev,
				    "invalid IV image, val32\n");
				return EINVAL;
			}
			iv_img_size -= sizeof(iv->iv_val.val32);
			sz += sizeof(iv->iv_val.val32);

			val32 = be32toh(iv->iv_val.val32);
			CSR_WRITE_4(sc, ofs, val32);
		} else {
			uint16_t val16;

			if (iv_img_size < sizeof(iv->iv_val.val16)) {
				device_printf(sc->sc_dev,
				    "invalid IV image, val16\n");
				return EINVAL;
			}
			iv_img_size -= sizeof(iv->iv_val.val16);
			sz += sizeof(iv->iv_val.val16);

			val16 = be16toh(iv->iv_val.val16);
			CSR_WRITE_2(sc, ofs, val16);
		}

		iv = (const struct bwi_fw_iv *)((const uint8_t *)iv + sz);
	}

	if (iv_img_size != 0) {
		device_printf(sc->sc_dev, "invalid IV image, size left %d\n",
		    iv_img_size);
		return EINVAL;
	}
	return 0;
}

static int
bwi_mac_fw_init(struct bwi_mac *mac)
{
	device_t dev = mac->mac_sc->sc_dev;
	int error;

	error = bwi_mac_fw_load_iv(mac, mac->mac_iv);
	if (error) {
		device_printf(dev, "load IV failed\n");
		return error;
	}

	if (mac->mac_iv_ext != NULL) {
		error = bwi_mac_fw_load_iv(mac, mac->mac_iv_ext);
		if (error)
			device_printf(dev, "load ExtIV failed\n");
	}
	return error;
}

static void
bwi_mac_opmode_init(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint32_t mac_status;
	uint16_t pre_tbtt;

	CSR_CLRBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_INFRA);
	CSR_SETBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_INFRA);

	/* Set probe resp timeout to infinite */
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_PROBE_RESP_TO, 0);

	/*
	 * TODO: factor out following part
	 */

	mac_status = CSR_READ_4(sc, BWI_MAC_STATUS);
	mac_status &= ~(BWI_MAC_STATUS_OPMODE_HOSTAP |
			BWI_MAC_STATUS_PASS_CTL |
			BWI_MAC_STATUS_PASS_BCN |
			BWI_MAC_STATUS_PASS_BADPLCP |
			BWI_MAC_STATUS_PASS_BADFCS |
			BWI_MAC_STATUS_PROMISC);
	mac_status |= BWI_MAC_STATUS_INFRA;

	/* Always turn on PROMISC on old hardware */
	if (mac->mac_rev < 5)
		mac_status |= BWI_MAC_STATUS_PROMISC;

	switch (ic->ic_opmode) {
	case IEEE80211_M_IBSS:
		mac_status &= ~BWI_MAC_STATUS_INFRA;
		break;
	case IEEE80211_M_HOSTAP:
		mac_status |= BWI_MAC_STATUS_OPMODE_HOSTAP;
		break;
	case IEEE80211_M_MONITOR:
#if 0
		/* Do you want data from your microwave oven? */
		mac_status |= BWI_MAC_STATUS_PASS_CTL |
			      BWI_MAC_STATUS_PASS_BADPLCP |
			      BWI_MAC_STATUS_PASS_BADFCS;
#else
		mac_status |= BWI_MAC_STATUS_PASS_CTL;
#endif
		/* Promisc? */
		break;
	default:
		break;
	}

	if (ic->ic_promisc > 0)
		mac_status |= BWI_MAC_STATUS_PROMISC;

	CSR_WRITE_4(sc, BWI_MAC_STATUS, mac_status);

	if (ic->ic_opmode != IEEE80211_M_IBSS &&
	    ic->ic_opmode != IEEE80211_M_HOSTAP) {
		if (sc->sc_bbp_id == BWI_BBPID_BCM4306 && sc->sc_bbp_rev == 3)
			pre_tbtt = 100;
		else
			pre_tbtt = 50;
	} else {
		pre_tbtt = 2;
	}
	CSR_WRITE_2(sc, BWI_MAC_PRE_TBTT, pre_tbtt);
}

static void
bwi_mac_hostflags_init(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct bwi_rf *rf = &mac->mac_rf;
	uint64_t host_flags;

	if (phy->phy_mode == IEEE80211_MODE_11A)
		return;

	host_flags = HFLAGS_READ(mac);
	host_flags |= BWI_HFLAG_SYM_WA;

	if (phy->phy_mode == IEEE80211_MODE_11G) {
		if (phy->phy_rev == 1)
			host_flags |= BWI_HFLAG_GDC_WA;
		if (sc->sc_card_flags & BWI_CARD_F_PA_GPIO9)
			host_flags |= BWI_HFLAG_OFDM_PA;
	} else if (phy->phy_mode == IEEE80211_MODE_11B) {
		if (phy->phy_rev >= 2 && rf->rf_type == BWI_RF_T_BCM2050)
			host_flags &= ~BWI_HFLAG_GDC_WA;
	} else {
		panic("unknown PHY mode %u\n", phy->phy_mode);
	}

	HFLAGS_WRITE(mac, host_flags);
}

static void
bwi_mac_bss_param_init(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_phy *phy = &mac->mac_phy;
	struct ieee80211com *ic = &sc->sc_ic;
	const struct ieee80211_rate_table *rt;
	struct bwi_retry_lim lim;
	uint16_t cw_min;

	/*
	 * Set short/long retry limits
	 */
	bzero(&lim, sizeof(lim));
	lim.shretry = BWI_SHRETRY;
	lim.shretry_fb = BWI_SHRETRY_FB;
	lim.lgretry = BWI_LGRETRY;
	lim.lgretry_fb = BWI_LGRETRY_FB;
	bwi_mac_set_retry_lim(mac, &lim);

	/*
	 * Implicitly prevent firmware from sending probe response
	 * by setting its "probe response timeout" to 1us.
	 */
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_PROBE_RESP_TO, 1);

	/*
	 * XXX MAC level acknowledge and CW min/max should depend
	 * on the char rateset of the IBSS/BSS to join.
	 * XXX this is all wrong; should be done on channel change
	 */
	if (phy->phy_mode == IEEE80211_MODE_11B) {
		rt = ieee80211_get_ratetable(
		    ieee80211_find_channel(ic, 2412, IEEE80211_CHAN_B));
		bwi_mac_set_ackrates(mac, rt,
		    &ic->ic_sup_rates[IEEE80211_MODE_11B]);
	} else {
		rt = ieee80211_get_ratetable(
		    ieee80211_find_channel(ic, 2412, IEEE80211_CHAN_G));
		bwi_mac_set_ackrates(mac, rt,
		    &ic->ic_sup_rates[IEEE80211_MODE_11G]);
	}

	/*
	 * Set CW min
	 */
	if (phy->phy_mode == IEEE80211_MODE_11B)
		cw_min = IEEE80211_CW_MIN_0;
	else
		cw_min = IEEE80211_CW_MIN_1;
	MOBJ_WRITE_2(mac, BWI_80211_MOBJ, BWI_80211_MOBJ_CWMIN, cw_min);

	/*
	 * Set CW max
	 */
	MOBJ_WRITE_2(mac, BWI_80211_MOBJ, BWI_80211_MOBJ_CWMAX,
		     IEEE80211_CW_MAX);
}

static void
bwi_mac_set_retry_lim(struct bwi_mac *mac, const struct bwi_retry_lim *lim)
{
	/* Short/Long retry limit */
	MOBJ_WRITE_2(mac, BWI_80211_MOBJ, BWI_80211_MOBJ_SHRETRY,
		     lim->shretry);
	MOBJ_WRITE_2(mac, BWI_80211_MOBJ, BWI_80211_MOBJ_LGRETRY,
		     lim->lgretry);

	/* Short/Long retry fallback limit */
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_SHRETRY_FB,
		     lim->shretry_fb);
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_LGRETEY_FB,
		     lim->lgretry_fb);
}

static void
bwi_mac_set_ackrates(struct bwi_mac *mac, const struct ieee80211_rate_table *rt,
	const struct ieee80211_rateset *rs)
{
	int i;

	/* XXX not standard conforming */
	for (i = 0; i < rs->rs_nrates; ++i) {
		enum ieee80211_phytype modtype;
		uint16_t ofs;

		modtype = ieee80211_rate2phytype(rt,
		    rs->rs_rates[i] & IEEE80211_RATE_VAL);
		switch (modtype) {
		case IEEE80211_T_DS:
			ofs = 0x4c0;
			break;
		case IEEE80211_T_OFDM:
			ofs = 0x480;
			break;
		default:
			panic("unsupported modtype %u\n", modtype);
		}
		ofs += 2*(ieee80211_rate2plcp(
		    rs->rs_rates[i] & IEEE80211_RATE_VAL,
		    modtype) & 0xf);

		MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, ofs + 0x20,
			     MOBJ_READ_2(mac, BWI_COMM_MOBJ, ofs));
	}
}

int
bwi_mac_start(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;

	CSR_SETBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_ENABLE);
	CSR_WRITE_4(sc, BWI_MAC_INTR_STATUS, BWI_INTR_READY);

	/* Flush pending bus writes */
	CSR_READ_4(sc, BWI_MAC_STATUS);
	CSR_READ_4(sc, BWI_MAC_INTR_STATUS);

	return bwi_mac_config_ps(mac);
}

int
bwi_mac_stop(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	int error, i;

	error = bwi_mac_config_ps(mac);
	if (error)
		return error;

	CSR_CLRBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_ENABLE);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_MAC_STATUS);

#define NRETRY	10000
	for (i = 0; i < NRETRY; ++i) {
		if (CSR_READ_4(sc, BWI_MAC_INTR_STATUS) & BWI_INTR_READY)
			break;
		DELAY(1);
	}
	if (i == NRETRY) {
		device_printf(sc->sc_dev, "can't stop MAC\n");
		return ETIMEDOUT;
	}
#undef NRETRY

	return 0;
}

int
bwi_mac_config_ps(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	uint32_t status;

	status = CSR_READ_4(sc, BWI_MAC_STATUS);

	status &= ~BWI_MAC_STATUS_HW_PS;
	status |= BWI_MAC_STATUS_WAKEUP;
	CSR_WRITE_4(sc, BWI_MAC_STATUS, status);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_MAC_STATUS);

	if (mac->mac_rev >= 5) {
		int i;

#define NRETRY	100
		for (i = 0; i < NRETRY; ++i) {
			if (MOBJ_READ_2(mac, BWI_COMM_MOBJ,
			    BWI_COMM_MOBJ_UCODE_STATE) != BWI_UCODE_STATE_PS)
				break;
			DELAY(10);
		}
		if (i == NRETRY) {
			device_printf(sc->sc_dev, "config PS failed\n");
			return ETIMEDOUT;
		}
#undef NRETRY
	}
	return 0;
}

void
bwi_mac_reset_hwkeys(struct bwi_mac *mac)
{
	/* TODO: firmware crypto */
	MOBJ_READ_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_KEYTABLE_OFS);
}

void
bwi_mac_shutdown(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	int i;

	if (mac->mac_flags & BWI_MAC_F_HAS_TXSTATS)
		sc->sc_free_txstats(sc);

	sc->sc_free_rx_ring(sc);

	for (i = 0; i < BWI_TX_NRING; ++i)
		sc->sc_free_tx_ring(sc, i);

	bwi_rf_off(mac);

	/* TODO:LED */

	bwi_mac_gpio_fini(mac);

	bwi_rf_off(mac); /* XXX again */
	CSR_WRITE_2(sc, BWI_BBP_ATTEN, BWI_BBP_ATTEN_MAGIC);
	bwi_regwin_disable(sc, &mac->mac_regwin, 0);

	mac->mac_flags &= ~BWI_MAC_F_INITED;
}

static int
bwi_mac_get_property(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	enum bwi_bus_space old_bus_space;
	uint32_t val;

	/*
	 * Byte swap
	 */
	val = CSR_READ_4(sc, BWI_MAC_STATUS);
	if (val & BWI_MAC_STATUS_BSWAP) {
		DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_ATTACH, "%s\n",
			"need byte swap");
		mac->mac_flags |= BWI_MAC_F_BSWAP;
	}

	/*
	 * DMA address space
	 */
	old_bus_space = sc->sc_bus_space;

	val = CSR_READ_4(sc, BWI_STATE_HI);
	if (__SHIFTOUT(val, BWI_STATE_HI_FLAGS_MASK) &
	    BWI_STATE_HI_FLAG_64BIT) {
		/* 64bit address */
		sc->sc_bus_space = BWI_BUS_SPACE_64BIT;
		DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_ATTACH, "%s\n",
			"64bit bus space");
	} else {
		uint32_t txrx_reg = BWI_TXRX_CTRL_BASE + BWI_TX32_CTRL;

		CSR_WRITE_4(sc, txrx_reg, BWI_TXRX32_CTRL_ADDRHI_MASK);
		if (CSR_READ_4(sc, txrx_reg) & BWI_TXRX32_CTRL_ADDRHI_MASK) {
			/* 32bit address */
			sc->sc_bus_space = BWI_BUS_SPACE_32BIT;
			DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_ATTACH, "%s\n",
				"32bit bus space");
		} else {
			/* 30bit address */
			sc->sc_bus_space = BWI_BUS_SPACE_30BIT;
			DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_ATTACH, "%s\n",
				"30bit bus space");
		}
	}

	if (old_bus_space != 0 && old_bus_space != sc->sc_bus_space) {
		device_printf(sc->sc_dev, "MACs bus space mismatch!\n");
		return ENXIO;
	}
	return 0;
}

void
bwi_mac_updateslot(struct bwi_mac *mac, int shslot)
{
	uint16_t slot_time;

	if (mac->mac_phy.phy_mode == IEEE80211_MODE_11B)
		return;

	if (shslot)
		slot_time = IEEE80211_DUR_SHSLOT;
	else
		slot_time = IEEE80211_DUR_SLOT;

	CSR_WRITE_2(mac->mac_sc, BWI_MAC_SLOTTIME,
		    slot_time + BWI_MAC_SLOTTIME_ADJUST);
	MOBJ_WRITE_2(mac, BWI_COMM_MOBJ, BWI_COMM_MOBJ_SLOTTIME, slot_time);
}

int
bwi_mac_attach(struct bwi_softc *sc, int id, uint8_t rev)
{
	struct bwi_mac *mac;
	int i;

	KASSERT(sc->sc_nmac <= BWI_MAC_MAX && sc->sc_nmac >= 0,
	    ("sc_nmac %d", sc->sc_nmac));

	if (sc->sc_nmac == BWI_MAC_MAX) {
		device_printf(sc->sc_dev, "too many MACs\n");
		return 0;
	}

	/*
	 * More than one MAC is only supported by BCM4309
	 */
	if (sc->sc_nmac != 0 &&
	    sc->sc_pci_did != PCI_PRODUCT_BROADCOM_BCM4309) {
		DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_ATTACH, "%s\n",
			"ignore second MAC");
		return 0;
	}

	mac = &sc->sc_mac[sc->sc_nmac];

	/* XXX will this happen? */
	if (BWI_REGWIN_EXIST(&mac->mac_regwin)) {
		device_printf(sc->sc_dev, "%dth MAC already attached\n",
			      sc->sc_nmac);
		return 0;
	}

	/*
	 * Test whether the revision of this MAC is supported
	 */
	for (i = 0; i < nitems(bwi_sup_macrev); ++i) {
		if (bwi_sup_macrev[i] == rev)
			break;
	}
	if (i == nitems(bwi_sup_macrev)) {
		device_printf(sc->sc_dev, "MAC rev %u is "
			      "not supported\n", rev);
		return ENXIO;
	}

	BWI_CREATE_MAC(mac, sc, id, rev);
	sc->sc_nmac++;

	if (mac->mac_rev < 5) {
		mac->mac_flags |= BWI_MAC_F_HAS_TXSTATS;
		DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_ATTACH, "%s\n",
			"has TX stats");
	} else {
		mac->mac_flags |= BWI_MAC_F_PHYE_RESET;
	}

	device_printf(sc->sc_dev, "MAC: rev %u\n", rev);
	return 0;
}

static __inline void
bwi_mac_balance_atten(int *bbp_atten0, int *rf_atten0)
{
	int bbp_atten, rf_atten, rf_atten_lim = -1;

	bbp_atten = *bbp_atten0;
	rf_atten = *rf_atten0;

	/*
	 * RF attenuation affects TX power BWI_RF_ATTEN_FACTOR times
	 * as much as BBP attenuation, so we try our best to keep RF
	 * attenuation within range.  BBP attenuation will be clamped
	 * later if it is out of range during balancing.
	 *
	 * BWI_RF_ATTEN_MAX0 is used as RF attenuation upper limit.
	 */

	/*
	 * Use BBP attenuation to balance RF attenuation
	 */
	if (rf_atten < 0)
		rf_atten_lim = 0;
	else if (rf_atten > BWI_RF_ATTEN_MAX0)
		rf_atten_lim = BWI_RF_ATTEN_MAX0;

	if (rf_atten_lim >= 0) {
		bbp_atten += (BWI_RF_ATTEN_FACTOR * (rf_atten - rf_atten_lim));
		rf_atten = rf_atten_lim;
	}

	/*
	 * If possible, use RF attenuation to balance BBP attenuation
	 * NOTE: RF attenuation is still kept within range.
	 */
	while (rf_atten < BWI_RF_ATTEN_MAX0 && bbp_atten > BWI_BBP_ATTEN_MAX) {
		bbp_atten -= BWI_RF_ATTEN_FACTOR;
		++rf_atten;
	}
	while (rf_atten > 0 && bbp_atten < 0) {
		bbp_atten += BWI_RF_ATTEN_FACTOR;
		--rf_atten;
	}

	/* RF attenuation MUST be within range */
	KASSERT(rf_atten >= 0 && rf_atten <= BWI_RF_ATTEN_MAX0,
	    ("rf_atten %d", rf_atten));

	/*
	 * Clamp BBP attenuation
	 */
	if (bbp_atten < 0)
		bbp_atten = 0;
	else if (bbp_atten > BWI_BBP_ATTEN_MAX)
		bbp_atten = BWI_BBP_ATTEN_MAX;

	*rf_atten0 = rf_atten;
	*bbp_atten0 = bbp_atten;
}

static void
bwi_mac_adjust_tpctl(struct bwi_mac *mac, int rf_atten_adj, int bbp_atten_adj)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	struct bwi_tpctl tpctl;
	int bbp_atten, rf_atten, tp_ctrl1;

	bcopy(&mac->mac_tpctl, &tpctl, sizeof(tpctl));

	/* NOTE: Use signed value to do calulation */
	bbp_atten = tpctl.bbp_atten;
	rf_atten = tpctl.rf_atten;
	tp_ctrl1 = tpctl.tp_ctrl1;

	bbp_atten += bbp_atten_adj;
	rf_atten += rf_atten_adj;

	bwi_mac_balance_atten(&bbp_atten, &rf_atten);

	if (rf->rf_type == BWI_RF_T_BCM2050 && rf->rf_rev == 2) {
		if (rf_atten <= 1) {
			if (tp_ctrl1 == 0) {
				tp_ctrl1 = 3;
				bbp_atten += 2;
				rf_atten += 2;
			} else if (sc->sc_card_flags & BWI_CARD_F_PA_GPIO9) {
				bbp_atten +=
				(BWI_RF_ATTEN_FACTOR * (rf_atten - 2));
				rf_atten = 2;
			}
		} else if (rf_atten > 4 && tp_ctrl1 != 0) {
			tp_ctrl1 = 0;
			if (bbp_atten < 3) {
				bbp_atten += 2;
				rf_atten -= 3;
			} else {
				bbp_atten -= 2;
				rf_atten -= 2;
			}
		}
		bwi_mac_balance_atten(&bbp_atten, &rf_atten);
	}

	tpctl.bbp_atten = bbp_atten;
	tpctl.rf_atten = rf_atten;
	tpctl.tp_ctrl1 = tp_ctrl1;

	bwi_mac_lock(mac);
	bwi_mac_set_tpctl_11bg(mac, &tpctl);
	bwi_mac_unlock(mac);
}

/*
 * http://bcm-specs.sipsolutions.net/RecalculateTransmissionPower
 */
void
bwi_mac_calibrate_txpower(struct bwi_mac *mac, enum bwi_txpwrcb_type type)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct bwi_rf *rf = &mac->mac_rf;
	int8_t tssi[4], tssi_avg, cur_txpwr;
	int error, i, ofdm_tssi;
	int txpwr_diff, rf_atten_adj, bbp_atten_adj;

	if (!sc->sc_txpwr_calib)
		return;

	if (mac->mac_flags & BWI_MAC_F_TPCTL_ERROR) {
		DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_TXPOWER, "%s\n",
			"tpctl error happened, can't set txpower");
		return;
	}

	if (BWI_IS_BRCM_BU4306(sc)) {
		DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_TXPOWER, "%s\n",
			"BU4306, can't set txpower");
		return;
	}

	/*
	 * Save latest TSSI and reset the related memory objects
	 */
	ofdm_tssi = 0;
	error = bwi_rf_get_latest_tssi(mac, tssi, BWI_COMM_MOBJ_TSSI_DS);
	if (error) {
		DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_TXPOWER, "%s\n",
			"no DS tssi");

		if (mac->mac_phy.phy_mode == IEEE80211_MODE_11B) {
			if (type == BWI_TXPWR_FORCE) {
				rf_atten_adj = 0;
				bbp_atten_adj = 1;
				goto calib;
			} else {
				return;
			}
		}

		error = bwi_rf_get_latest_tssi(mac, tssi,
				BWI_COMM_MOBJ_TSSI_OFDM);
		if (error) {
			DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_TXPOWER, "%s\n",
				"no OFDM tssi");
			if (type == BWI_TXPWR_FORCE) {
				rf_atten_adj = 0;
				bbp_atten_adj = 1;
				goto calib;
			} else {
				return;
			}
		}

		for (i = 0; i < 4; ++i) {
			tssi[i] += 0x20;
			tssi[i] &= 0x3f;
		}
		ofdm_tssi = 1;
	}
	bwi_rf_clear_tssi(mac);

	DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_TXPOWER,
		"tssi0 %d, tssi1 %d, tssi2 %d, tssi3 %d\n",
		tssi[0], tssi[1], tssi[2], tssi[3]);

	/*
	 * Calculate RF/BBP attenuation adjustment based on
	 * the difference between desired TX power and sampled
	 * TX power.
	 */
	/* +8 == "each incremented by 1/2" */
	tssi_avg = (tssi[0] + tssi[1] + tssi[2] + tssi[3] + 8) / 4;
	if (ofdm_tssi && (HFLAGS_READ(mac) & BWI_HFLAG_PWR_BOOST_DS))
		tssi_avg -= 13;

	DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_TXPOWER, "tssi avg %d\n", tssi_avg);

	error = bwi_rf_tssi2dbm(mac, tssi_avg, &cur_txpwr);
	if (error)
		return;
	DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_TXPOWER, "current txpower %d\n",
		cur_txpwr);

	txpwr_diff = rf->rf_txpower_max - cur_txpwr; /* XXX ni_txpower */

	rf_atten_adj = -howmany(txpwr_diff, 8);
	if (type == BWI_TXPWR_INIT) {
		/*
		 * Move toward EEPROM max TX power as fast as we can
		 */
		bbp_atten_adj = -txpwr_diff;
	} else {
		bbp_atten_adj = -(txpwr_diff / 2);
	}
	bbp_atten_adj -= (BWI_RF_ATTEN_FACTOR * rf_atten_adj);

	if (rf_atten_adj == 0 && bbp_atten_adj == 0) {
		DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_TXPOWER, "%s\n",
			"no need to adjust RF/BBP attenuation");
		/* TODO: LO */
		return;
	}

calib:
	DPRINTF(sc, BWI_DBG_MAC | BWI_DBG_TXPOWER,
		"rf atten adjust %d, bbp atten adjust %d\n",
		rf_atten_adj, bbp_atten_adj);
	bwi_mac_adjust_tpctl(mac, rf_atten_adj, bbp_atten_adj);
	/* TODO: LO */
}

static void
bwi_mac_lock(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;

	KASSERT((mac->mac_flags & BWI_MAC_F_LOCKED) == 0,
	    ("mac_flags 0x%x", mac->mac_flags));

	if (mac->mac_rev < 3)
		bwi_mac_stop(mac);
	else if (ic->ic_opmode != IEEE80211_M_HOSTAP)
		bwi_mac_config_ps(mac);

	CSR_SETBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_RFLOCK);

	/* Flush pending bus write */
	CSR_READ_4(sc, BWI_MAC_STATUS);
	DELAY(10);

	mac->mac_flags |= BWI_MAC_F_LOCKED;
}

static void
bwi_mac_unlock(struct bwi_mac *mac)
{
	struct bwi_softc *sc = mac->mac_sc;
	struct ieee80211com *ic = &sc->sc_ic;

	KASSERT(mac->mac_flags & BWI_MAC_F_LOCKED,
	    ("mac_flags 0x%x", mac->mac_flags));

	CSR_READ_2(sc, BWI_PHYINFO); /* dummy read */

	CSR_CLRBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_RFLOCK);

	if (mac->mac_rev < 3)
		bwi_mac_start(mac);
	else if (ic->ic_opmode != IEEE80211_M_HOSTAP)
		bwi_mac_config_ps(mac);

	mac->mac_flags &= ~BWI_MAC_F_LOCKED;
}

void
bwi_mac_set_promisc(struct bwi_mac *mac, int promisc)
{
	struct bwi_softc *sc = mac->mac_sc;

	if (mac->mac_rev < 5) /* Promisc is always on */
		return;

	if (promisc)
		CSR_SETBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_PROMISC);
	else
		CSR_CLRBITS_4(sc, BWI_MAC_STATUS, BWI_MAC_STATUS_PROMISC);
}
