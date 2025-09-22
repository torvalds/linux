/*     $OpenBSD: ar5210.c,v 1.48 2022/01/09 05:42:38 jsg Exp $        */

/*
 * Copyright (c) 2004, 2005, 2006, 2007 Reyk Floeter <reyk@openbsd.org>
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

/*
 * HAL interface for the Atheros AR5000 Wireless LAN chipset
 * (AR5210 + AR5110).
 */

#include <dev/ic/ar5xxx.h>
#include <dev/ic/ar5210reg.h>
#include <dev/ic/ar5210var.h>

HAL_BOOL	 ar5k_ar5210_nic_reset(struct ath_hal *, u_int32_t);
HAL_BOOL	 ar5k_ar5210_nic_wakeup(struct ath_hal *, HAL_BOOL);
void		 ar5k_ar5210_init_tx_queue(struct ath_hal *, u_int);
void		 ar5k_ar5210_fill(struct ath_hal *);
HAL_BOOL	 ar5k_ar5210_do_calibrate(struct ath_hal *, HAL_CHANNEL *);
HAL_BOOL	 ar5k_ar5210_noise_floor(struct ath_hal *, HAL_CHANNEL *);

/*
 * Initial register setting for the AR5210
 */
static const struct ar5k_ini ar5210_ini[] =
    AR5K_AR5210_INI;

AR5K_HAL_FUNCTIONS(extern, ar5k_ar5210,);

void
ar5k_ar5210_fill(struct ath_hal *hal)
{
	hal->ah_magic = AR5K_AR5210_MAGIC;

	/*
	 * Init/Exit functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5210, get_rate_table);
	AR5K_HAL_FUNCTION(hal, ar5210, detach);

	/*
	 * Reset functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5210, reset);
	AR5K_HAL_FUNCTION(hal, ar5210, set_opmode);
	AR5K_HAL_FUNCTION(hal, ar5210, calibrate);

	/*
	 * TX functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5210, update_tx_triglevel);
	AR5K_HAL_FUNCTION(hal, ar5210, setup_tx_queue);
	AR5K_HAL_FUNCTION(hal, ar5210, setup_tx_queueprops);
	AR5K_HAL_FUNCTION(hal, ar5210, release_tx_queue);
	AR5K_HAL_FUNCTION(hal, ar5210, reset_tx_queue);
	AR5K_HAL_FUNCTION(hal, ar5210, get_tx_buf);
	AR5K_HAL_FUNCTION(hal, ar5210, put_tx_buf);
	AR5K_HAL_FUNCTION(hal, ar5210, tx_start);
	AR5K_HAL_FUNCTION(hal, ar5210, stop_tx_dma);
	AR5K_HAL_FUNCTION(hal, ar5210, setup_tx_desc);
	AR5K_HAL_FUNCTION(hal, ar5210, setup_xtx_desc);
	AR5K_HAL_FUNCTION(hal, ar5210, fill_tx_desc);
	AR5K_HAL_FUNCTION(hal, ar5210, proc_tx_desc);
	AR5K_HAL_FUNCTION(hal, ar5210, has_veol);

	/*
	 * RX functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5210, get_rx_buf);
	AR5K_HAL_FUNCTION(hal, ar5210, put_rx_buf);
	AR5K_HAL_FUNCTION(hal, ar5210, start_rx);
	AR5K_HAL_FUNCTION(hal, ar5210, stop_rx_dma);
	AR5K_HAL_FUNCTION(hal, ar5210, start_rx_pcu);
	AR5K_HAL_FUNCTION(hal, ar5210, stop_pcu_recv);
	AR5K_HAL_FUNCTION(hal, ar5210, set_mcast_filter);
	AR5K_HAL_FUNCTION(hal, ar5210, set_mcast_filterindex);
	AR5K_HAL_FUNCTION(hal, ar5210, clear_mcast_filter_idx);
	AR5K_HAL_FUNCTION(hal, ar5210, get_rx_filter);
	AR5K_HAL_FUNCTION(hal, ar5210, set_rx_filter);
	AR5K_HAL_FUNCTION(hal, ar5210, setup_rx_desc);
	AR5K_HAL_FUNCTION(hal, ar5210, proc_rx_desc);
	AR5K_HAL_FUNCTION(hal, ar5210, set_rx_signal);

	/*
	 * Misc functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5210, dump_state);
	AR5K_HAL_FUNCTION(hal, ar5210, get_diag_state);
	AR5K_HAL_FUNCTION(hal, ar5210, get_lladdr);
	AR5K_HAL_FUNCTION(hal, ar5210, set_lladdr);
	AR5K_HAL_FUNCTION(hal, ar5210, set_regdomain);
	AR5K_HAL_FUNCTION(hal, ar5210, set_ledstate);
	AR5K_HAL_FUNCTION(hal, ar5210, set_associd);
	AR5K_HAL_FUNCTION(hal, ar5210, set_gpio_input);
	AR5K_HAL_FUNCTION(hal, ar5210, set_gpio_output);
	AR5K_HAL_FUNCTION(hal, ar5210, get_gpio);
	AR5K_HAL_FUNCTION(hal, ar5210, set_gpio);
	AR5K_HAL_FUNCTION(hal, ar5210, set_gpio_intr);
	AR5K_HAL_FUNCTION(hal, ar5210, get_tsf32);
	AR5K_HAL_FUNCTION(hal, ar5210, get_tsf64);
	AR5K_HAL_FUNCTION(hal, ar5210, reset_tsf);
	AR5K_HAL_FUNCTION(hal, ar5210, get_regdomain);
	AR5K_HAL_FUNCTION(hal, ar5210, detect_card_present);
	AR5K_HAL_FUNCTION(hal, ar5210, update_mib_counters);
	AR5K_HAL_FUNCTION(hal, ar5210, get_rf_gain);
	AR5K_HAL_FUNCTION(hal, ar5210, set_slot_time);
	AR5K_HAL_FUNCTION(hal, ar5210, get_slot_time);
	AR5K_HAL_FUNCTION(hal, ar5210, set_ack_timeout);
	AR5K_HAL_FUNCTION(hal, ar5210, get_ack_timeout);
	AR5K_HAL_FUNCTION(hal, ar5210, set_cts_timeout);
	AR5K_HAL_FUNCTION(hal, ar5210, get_cts_timeout);

	/*
	 * Key table (WEP) functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5210, is_cipher_supported);
	AR5K_HAL_FUNCTION(hal, ar5210, get_keycache_size);
	AR5K_HAL_FUNCTION(hal, ar5210, reset_key);
	AR5K_HAL_FUNCTION(hal, ar5210, is_key_valid);
	AR5K_HAL_FUNCTION(hal, ar5210, set_key);
	AR5K_HAL_FUNCTION(hal, ar5210, set_key_lladdr);
	AR5K_HAL_FUNCTION(hal, ar5210, softcrypto);

	/*
	 * Power management functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5210, set_power);
	AR5K_HAL_FUNCTION(hal, ar5210, get_power_mode);
	AR5K_HAL_FUNCTION(hal, ar5210, query_pspoll_support);
	AR5K_HAL_FUNCTION(hal, ar5210, init_pspoll);
	AR5K_HAL_FUNCTION(hal, ar5210, enable_pspoll);
	AR5K_HAL_FUNCTION(hal, ar5210, disable_pspoll);

	/*
	 * Beacon functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5210, init_beacon);
	AR5K_HAL_FUNCTION(hal, ar5210, set_beacon_timers);
	AR5K_HAL_FUNCTION(hal, ar5210, reset_beacon);
	AR5K_HAL_FUNCTION(hal, ar5210, wait_for_beacon);

	/*
	 * Interrupt functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5210, is_intr_pending);
	AR5K_HAL_FUNCTION(hal, ar5210, get_isr);
	AR5K_HAL_FUNCTION(hal, ar5210, get_intr);
	AR5K_HAL_FUNCTION(hal, ar5210, set_intr);

	/*
	 * Chipset functions (ar5k-specific, non-HAL)
	 */
	AR5K_HAL_FUNCTION(hal, ar5210, get_capabilities);
	AR5K_HAL_FUNCTION(hal, ar5210, radar_alert);

	/*
	 * EEPROM access
	 */
	AR5K_HAL_FUNCTION(hal, ar5210, eeprom_is_busy);
	AR5K_HAL_FUNCTION(hal, ar5210, eeprom_read);
	AR5K_HAL_FUNCTION(hal, ar5210, eeprom_write);

	/*
	 * Unused functions or functions not implemented
	 */
	AR5K_HAL_FUNCTION(hal, ar5210, set_bssid_mask);
	AR5K_HAL_FUNCTION(hal, ar5210, get_tx_queueprops);
	AR5K_HAL_FUNCTION(hal, ar5210, num_tx_pending);
	AR5K_HAL_FUNCTION(hal, ar5210, phy_disable);
	AR5K_HAL_FUNCTION(hal, ar5210, set_txpower_limit);
	AR5K_HAL_FUNCTION(hal, ar5210, set_def_antenna);
	AR5K_HAL_FUNCTION(hal, ar5210, get_def_antenna);
#ifdef notyet
	AR5K_HAL_FUNCTION(hal, ar5210, set_capability);
	AR5K_HAL_FUNCTION(hal, ar5210, proc_mib_event);
	AR5K_HAL_FUNCTION(hal, ar5210, get_tx_inter_queue);
#endif
}

struct ath_hal *
ar5k_ar5210_attach(u_int16_t device, void *sc, bus_space_tag_t st,
    bus_space_handle_t sh, int *status)
{
	int i;
	struct ath_hal *hal = (struct ath_hal*) sc;
	u_int8_t mac[IEEE80211_ADDR_LEN];
	u_int32_t srev;

	ar5k_ar5210_fill(hal);

	/* Bring device out of sleep and reset its units */
	if (ar5k_ar5210_nic_wakeup(hal, AH_TRUE) != AH_TRUE)
		return (NULL);

	/* Get MAC, PHY and RADIO revisions */
	srev = AR5K_REG_READ(AR5K_AR5210_SREV);
	hal->ah_mac_srev = srev;
	hal->ah_mac_version = AR5K_REG_MS(srev, AR5K_AR5210_SREV_VER);
	hal->ah_mac_revision = AR5K_REG_MS(srev, AR5K_AR5210_SREV_REV);
	hal->ah_phy_revision = AR5K_REG_READ(AR5K_AR5210_PHY_CHIP_ID) &
	    0x00ffffffff;

	/* ...wait until PHY is ready and read RADIO revision */
	AR5K_REG_WRITE(AR5K_AR5210_PHY(0x34), 0x00001c16);
	for (i = 0; i < 4; i++)
		AR5K_REG_WRITE(AR5K_AR5210_PHY(0x20), 0x00010000);
	hal->ah_radio_5ghz_revision = (u_int16_t)
	    (ar5k_bitswap((AR5K_REG_READ(AR5K_AR5210_PHY(256) >> 28) & 0xf), 4)
		+ 1);
	hal->ah_radio_2ghz_revision = 0;

	/* Identify the chipset */
	hal->ah_version = AR5K_AR5210;
	hal->ah_radio = AR5K_AR5110;
	hal->ah_phy = AR5K_AR5210_PHY(0);

	bcopy(etherbroadcastaddr, mac, IEEE80211_ADDR_LEN);
	ar5k_ar5210_set_associd(hal, mac, 0, 0);
	ar5k_ar5210_get_lladdr(hal, mac);
	ar5k_ar5210_set_opmode(hal);

	return (hal);
}

HAL_BOOL
ar5k_ar5210_nic_reset(struct ath_hal *hal, u_int32_t val)
{
	HAL_BOOL ret = AH_FALSE;
	u_int32_t mask = val ? val : ~0;

	/*
	 * Reset the device and wait until success
	 */
	AR5K_REG_WRITE(AR5K_AR5210_RC, val);

	/* Wait at least 128 PCI clocks */
	AR5K_DELAY(15);

	val &=
	    AR5K_AR5210_RC_PCU | AR5K_AR5210_RC_MAC |
	    AR5K_AR5210_RC_PHY | AR5K_AR5210_RC_DMA;

	mask &=
	    AR5K_AR5210_RC_PCU | AR5K_AR5210_RC_MAC |
	    AR5K_AR5210_RC_PHY | AR5K_AR5210_RC_DMA;

	ret = ar5k_register_timeout(hal, AR5K_AR5210_RC, mask, val, AH_FALSE);

	/*
	 * Reset configuration register
	 */
	if ((val & AR5K_AR5210_RC_MAC) == 0) {
		AR5K_REG_WRITE(AR5K_AR5210_CFG, AR5K_AR5210_INIT_CFG);
	}

	return (ret);
}

HAL_BOOL
ar5k_ar5210_nic_wakeup(struct ath_hal *hal, HAL_BOOL initial)
{
	/*
	 * Reset and wakeup the device
	 */

	if (initial == AH_TRUE) {
		/* ...reset hardware */
		if (ar5k_ar5210_nic_reset(hal,
			AR5K_AR5210_RC_PCI) == AH_FALSE) {
			AR5K_PRINT("failed to reset the PCI chipset\n");
			return (AH_FALSE);
		}

		AR5K_DELAY(1000);
	}

	/* ...wakeup the device */
	if (ar5k_ar5210_set_power(hal,
		HAL_PM_AWAKE, AH_TRUE, 0) == AH_FALSE) {
		AR5K_PRINT("failed to resume the AR5210 chipset\n");
		return (AH_FALSE);
	}

	/* ...do not enable Atheros turbo mode */
	AR5K_REG_WRITE(AR5K_AR5210_PHY_FC, 0);

	/* ...reset chipset */
	if (ar5k_ar5210_nic_reset(hal, AR5K_AR5210_RC_CHIP) == AH_FALSE) {
		AR5K_PRINT("failed to reset the AR5210 chipset\n");
		return (AH_FALSE);
	}

	AR5K_DELAY(1000);

	/* ...reset chipset and PCI device */
	if (ar5k_ar5210_nic_reset(hal,
		AR5K_AR5210_RC_CHIP | AR5K_AR5210_RC_PCI) == AH_FALSE) {
		AR5K_PRINT("failed to reset the AR5210 + PCI chipset\n");
		return (AH_FALSE);
	}

	AR5K_DELAY(2300);

	/* ...wakeup (again) */
	if (ar5k_ar5210_set_power(hal,
		HAL_PM_AWAKE, AH_TRUE, 0) == AH_FALSE) {
		AR5K_PRINT("failed to resume the AR5210 (again)\n");
		return (AH_FALSE);
	}

	/* ...final warm reset */
	if (ar5k_ar5210_nic_reset(hal, 0) == AH_FALSE) {
		AR5K_PRINT("failed to warm reset the AR5210\n");
		return (AH_FALSE);
	}

	return (AH_TRUE);
}

const HAL_RATE_TABLE *
ar5k_ar5210_get_rate_table(struct ath_hal *hal, u_int mode)
{
	switch (mode) {
	case HAL_MODE_11A:
		return (&hal->ah_rt_11a);
	case HAL_MODE_11B:
	case HAL_MODE_11G:
	default:
		return (NULL);
	}

	return (NULL);
}

void
ar5k_ar5210_detach(struct ath_hal *hal)
{
	/*
	 * Free HAL structure, assume interrupts are down
	 */
	free(hal, M_DEVBUF, 0);
}

HAL_BOOL
ar5k_ar5210_phy_disable(struct ath_hal *hal)
{
	AR5K_REG_WRITE(AR5K_AR5210_PHY_ACTIVE, AR5K_AR5210_PHY_DISABLE);
	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_reset(struct ath_hal *hal, HAL_OPMODE op_mode, HAL_CHANNEL *channel,
    HAL_BOOL change_channel, HAL_STATUS *status)
{
	int i;

	/* Not used, keep for HAL compatibility */
	*status = HAL_OK;

	if (ar5k_ar5210_nic_wakeup(hal, AH_FALSE) == AH_FALSE)
		return (AH_FALSE);

	/*
	 * Initialize operating mode
	 */
	hal->ah_op_mode = op_mode;
	ar5k_ar5210_set_opmode(hal);

	/*
	 * Write initial mode register settings
	 */
	for (i = 0; i < nitems(ar5210_ini); i++) {
		if (change_channel == AH_TRUE &&
		    ar5210_ini[i].ini_register >= AR5K_AR5210_PCU_MIN &&
		    ar5210_ini[i].ini_register <= AR5K_AR5210_PCU_MAX)
			continue;

		switch (ar5210_ini[i].ini_mode) {
		case AR5K_INI_READ:
			/* Cleared on read */
			AR5K_REG_READ(ar5210_ini[i].ini_register);
			break;

		case AR5K_INI_WRITE:
		default:
			AR5K_REG_WRITE(ar5210_ini[i].ini_register,
			    ar5210_ini[i].ini_value);
		}
	}

	AR5K_DELAY(1000);

	/*
	 * Set channel and calibrate the PHY
	 */

	/* Disable phy and wait */
	AR5K_REG_WRITE(AR5K_AR5210_PHY_ACTIVE, AR5K_AR5210_PHY_DISABLE);
	AR5K_DELAY(1000);

	if (ar5k_channel(hal, channel) == AH_FALSE)
		return (AH_FALSE);

	/*
	 * Activate phy and wait
	 */
	AR5K_REG_WRITE(AR5K_AR5210_PHY_ACTIVE, AR5K_AR5210_PHY_ENABLE);
	AR5K_DELAY(1000);

	ar5k_ar5210_do_calibrate(hal, channel);
	if (ar5k_ar5210_noise_floor(hal, channel) == AH_FALSE)
		return (AH_FALSE);

	/*
	 * Set RF kill flags if supported by the device (read from the EEPROM)
	 */
	if (AR5K_EEPROM_HDR_RFKILL(hal->ah_capabilities.cap_eeprom.ee_header)) {
		ar5k_ar5210_set_gpio_input(hal, 0);
		if ((hal->ah_gpio[0] = ar5k_ar5210_get_gpio(hal, 0)) == 0) {
			ar5k_ar5210_set_gpio_intr(hal, 0, 1);
		} else {
			ar5k_ar5210_set_gpio_intr(hal, 0, 0);
		}
	}

	/*
	 * Reset queues and start beacon timers at the end of the reset routine
	 */
	for (i = 0; i < hal->ah_capabilities.cap_queues.q_tx_num; i++) {
		if (ar5k_ar5210_reset_tx_queue(hal, i) == AH_FALSE) {
			AR5K_PRINTF("failed to reset TX queue #%d\n", i);
			return (AH_FALSE);
		}
	}

	AR5K_REG_DISABLE_BITS(AR5K_AR5210_BEACON,
	    AR5K_AR5210_BEACON_EN | AR5K_AR5210_BEACON_RESET_TSF);

	return (AH_TRUE);
}

void
ar5k_ar5210_set_def_antenna(struct ath_hal *hal, u_int ant)
{
	/* Not available */
	return;
}

u_int
ar5k_ar5210_get_def_antenna(struct ath_hal *hal)
{
	return (0);
}

void
ar5k_ar5210_set_opmode(struct ath_hal *hal)
{
	u_int32_t pcu_reg, beacon_reg, low_id, high_id;

	beacon_reg = 0;
	pcu_reg = 0;

	switch (hal->ah_op_mode) {
	case IEEE80211_M_STA:
		pcu_reg |= AR5K_AR5210_STA_ID1_NO_PSPOLL |
		    AR5K_AR5210_STA_ID1_DESC_ANTENNA |
		    AR5K_AR5210_STA_ID1_PWR_SV;
		break;

#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_IBSS:
		pcu_reg |= AR5K_AR5210_STA_ID1_ADHOC |
		    AR5K_AR5210_STA_ID1_NO_PSPOLL |
		    AR5K_AR5210_STA_ID1_DESC_ANTENNA;
		beacon_reg |= AR5K_AR5210_BCR_ADHOC;
		break;

	case IEEE80211_M_HOSTAP:
		pcu_reg |= AR5K_AR5210_STA_ID1_AP |
		    AR5K_AR5210_STA_ID1_NO_PSPOLL |
		    AR5K_AR5210_STA_ID1_DESC_ANTENNA;
		beacon_reg |= AR5K_AR5210_BCR_AP;
		break;
#endif

	case IEEE80211_M_MONITOR:
		pcu_reg |= AR5K_AR5210_STA_ID1_NO_PSPOLL;
		break;

	default:
		return;
	}

	/*
	 * Set PCU and BCR registers
	 */
	low_id = AR5K_LOW_ID(hal->ah_sta_id);
	high_id = AR5K_HIGH_ID(hal->ah_sta_id);
	AR5K_REG_WRITE(AR5K_AR5210_STA_ID0, low_id);
	AR5K_REG_WRITE(AR5K_AR5210_STA_ID1, pcu_reg | high_id);
	AR5K_REG_WRITE(AR5K_AR5210_BCR, beacon_reg);

	return;
}

HAL_BOOL
ar5k_ar5210_calibrate(struct ath_hal *hal, HAL_CHANNEL *channel)
{
	HAL_BOOL ret = AH_TRUE;
	u_int32_t phy_sig, phy_agc, phy_sat, beacon;

#define AGC_DISABLE	{						\
	AR5K_REG_ENABLE_BITS(AR5K_AR5210_PHY_AGC,			\
	    AR5K_AR5210_PHY_AGC_DISABLE);				\
	AR5K_DELAY(10);							\
}

#define AGC_ENABLE	{						\
	AR5K_REG_DISABLE_BITS(AR5K_AR5210_PHY_AGC,			\
	    AR5K_AR5210_PHY_AGC_DISABLE);				\
}

	/*
	 * Disable beacons and RX/TX queues, wait
	 */
	AR5K_REG_ENABLE_BITS(AR5K_AR5210_DIAG_SW,
	    AR5K_AR5210_DIAG_SW_DIS_TX | AR5K_AR5210_DIAG_SW_DIS_RX);
	beacon = AR5K_REG_READ(AR5K_AR5210_BEACON);
	AR5K_REG_WRITE(AR5K_AR5210_BEACON, beacon & ~AR5K_AR5210_BEACON_EN);

	AR5K_DELAY(2300);

	/*
	 * Set the channel (with AGC turned off)
	 */
	AGC_DISABLE;
	ret = ar5k_channel(hal, channel);

	/*
	 * Activate PHY and wait
	 */
	AR5K_REG_WRITE(AR5K_AR5210_PHY_ACTIVE, AR5K_AR5210_PHY_ENABLE);
	AR5K_DELAY(1000);

	AGC_ENABLE;

	if (ret == AH_FALSE)
		return (ret);

	/*
	 * Calibrate the radio chip
	 */

	/* Remember normal state */
	phy_sig = AR5K_REG_READ(AR5K_AR5210_PHY_SIG);
	phy_agc = AR5K_REG_READ(AR5K_AR5210_PHY_AGCCOARSE);
	phy_sat = AR5K_REG_READ(AR5K_AR5210_PHY_ADCSAT);

	/* Update radio registers */
	AR5K_REG_WRITE(AR5K_AR5210_PHY_SIG,
	    (phy_sig & ~(AR5K_AR5210_PHY_SIG_FIRPWR)) |
	    AR5K_REG_SM(-1, AR5K_AR5210_PHY_SIG_FIRPWR));

	AR5K_REG_WRITE(AR5K_AR5210_PHY_AGCCOARSE,
	    (phy_agc & ~(AR5K_AR5210_PHY_AGCCOARSE_HI |
		AR5K_AR5210_PHY_AGCCOARSE_LO)) |
	    AR5K_REG_SM(-1, AR5K_AR5210_PHY_AGCCOARSE_HI) |
	    AR5K_REG_SM(-127, AR5K_AR5210_PHY_AGCCOARSE_LO));

	AR5K_REG_WRITE(AR5K_AR5210_PHY_ADCSAT,
	    (phy_sat & ~(AR5K_AR5210_PHY_ADCSAT_ICNT |
		AR5K_AR5210_PHY_ADCSAT_THR)) |
	    AR5K_REG_SM(2, AR5K_AR5210_PHY_ADCSAT_ICNT) |
	    AR5K_REG_SM(12, AR5K_AR5210_PHY_ADCSAT_THR));

	AR5K_DELAY(20);

	AGC_DISABLE;
	AR5K_REG_WRITE(AR5K_AR5210_PHY_RFSTG, AR5K_AR5210_PHY_RFSTG_DISABLE);
	AGC_ENABLE;

	AR5K_DELAY(1000);

	ret = ar5k_ar5210_do_calibrate(hal, channel);

	/* Reset to normal state */
	AR5K_REG_WRITE(AR5K_AR5210_PHY_SIG, phy_sig);
	AR5K_REG_WRITE(AR5K_AR5210_PHY_AGCCOARSE, phy_agc);
	AR5K_REG_WRITE(AR5K_AR5210_PHY_ADCSAT, phy_sat);

	if (ret == AH_FALSE)
		return (AH_FALSE);

	if (ar5k_ar5210_noise_floor(hal, channel) == AH_FALSE)
		return (AH_FALSE);

	/*
	 * Re-enable RX/TX and beacons
	 */
	AR5K_REG_DISABLE_BITS(AR5K_AR5210_DIAG_SW,
	    AR5K_AR5210_DIAG_SW_DIS_TX | AR5K_AR5210_DIAG_SW_DIS_RX);
	AR5K_REG_WRITE(AR5K_AR5210_BEACON, beacon);

#undef AGC_ENABLE
#undef AGC_DISABLE

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_do_calibrate(struct ath_hal *hal, HAL_CHANNEL *channel)
{
	/*
	 * Enable calibration and wait until completion
	 */
	AR5K_REG_ENABLE_BITS(AR5K_AR5210_PHY_AGCCTL,
	    AR5K_AR5210_PHY_AGCCTL_CAL);

	if (ar5k_register_timeout(hal, AR5K_AR5210_PHY_AGCCTL,
		AR5K_AR5210_PHY_AGCCTL_CAL, 0, AH_FALSE) == AH_FALSE) {
		AR5K_PRINTF("calibration timeout (%uMHz)\n",
		    channel->c_channel);
		return (AH_FALSE);
	}

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_noise_floor(struct ath_hal *hal, HAL_CHANNEL *channel)
{
	int i;
	u_int32_t noise_floor;

	/*
	 * Enable noise floor calibration and wait until completion
	 */
	AR5K_REG_ENABLE_BITS(AR5K_AR5210_PHY_AGCCTL,
	    AR5K_AR5210_PHY_AGCCTL_NF);

	if (ar5k_register_timeout(hal, AR5K_AR5210_PHY_AGCCTL,
		AR5K_AR5210_PHY_AGCCTL_NF, 0, AH_FALSE) == AH_FALSE) {
		AR5K_PRINTF("noise floor calibration timeout (%uMHz)\n",
		    channel->c_channel);
		return (AH_FALSE);
	}

	/* wait until the noise floor is calibrated */
	for (i = 20; i > 0; i--) {
		AR5K_DELAY(1000);
		noise_floor = AR5K_REG_READ(AR5K_AR5210_PHY_NF);
		if (AR5K_AR5210_PHY_NF_RVAL(noise_floor) &
		    AR5K_AR5210_PHY_NF_ACTIVE)
			noise_floor = AR5K_AR5210_PHY_NF_AVAL(noise_floor);
		if (noise_floor <= AR5K_TUNE_NOISE_FLOOR)
			break;
	}

	if (noise_floor > AR5K_TUNE_NOISE_FLOOR) {
		AR5K_PRINTF("noise floor calibration failed (%uMHz)\n",
		    channel->c_channel);
		return (AH_FALSE);
	}

	return (AH_TRUE);
}

/*
 * Transmit functions
 */

HAL_BOOL
ar5k_ar5210_update_tx_triglevel(struct ath_hal *hal, HAL_BOOL increase)
{
	u_int32_t trigger_level;
	HAL_BOOL status = AH_FALSE;

	/*
	 * Disable interrupts by setting the mask
	 */
	AR5K_REG_DISABLE_BITS(AR5K_AR5210_IMR, HAL_INT_GLOBAL);

	trigger_level = AR5K_REG_READ(AR5K_AR5210_TRIG_LVL);

	if (increase == AH_FALSE) {
		if (--trigger_level < AR5K_TUNE_MIN_TX_FIFO_THRES)
			goto done;
	} else {
		trigger_level +=
		    ((AR5K_TUNE_MAX_TX_FIFO_THRES - trigger_level) / 2);
	}

	/*
	 * Update trigger level on success
	 */
	AR5K_REG_WRITE(AR5K_AR5210_TRIG_LVL, trigger_level);
	status = AH_TRUE;

 done:
	/*
	 * Restore interrupt mask
	 */
	AR5K_REG_ENABLE_BITS(AR5K_AR5210_IMR, HAL_INT_GLOBAL);

	return (status);
}

int
ar5k_ar5210_setup_tx_queue(struct ath_hal *hal, HAL_TX_QUEUE queue_type,
    const HAL_TXQ_INFO *queue_info)
{
	u_int queue;

	/*
	 * Get queue by type
	 */
	switch (queue_type) {
	case HAL_TX_QUEUE_DATA:
		queue = 0;
		break;
	case HAL_TX_QUEUE_BEACON:
	case HAL_TX_QUEUE_CAB:
		queue = 1;
		break;
	default:
		return (-1);
	}

	/*
	 * Setup internal queue structure
	 */
	bzero(&hal->ah_txq[queue], sizeof(HAL_TXQ_INFO));
	hal->ah_txq[queue].tqi_type = queue_type;

	if (queue_info != NULL) {
		if (ar5k_ar5210_setup_tx_queueprops(hal,
			queue, queue_info) != AH_TRUE)
			return (-1);
	}

	return (queue);
}

HAL_BOOL
ar5k_ar5210_setup_tx_queueprops(struct ath_hal *hal, int queue,
    const HAL_TXQ_INFO *queue_info)
{
	AR5K_ASSERT_ENTRY(queue, hal->ah_capabilities.cap_queues.q_tx_num);

	if (hal->ah_txq[queue].tqi_type == HAL_TX_QUEUE_INACTIVE)
		return (AH_FALSE);

	hal->ah_txq[queue].tqi_aifs = queue_info->tqi_aifs;
	hal->ah_txq[queue].tqi_cw_max = queue_info->tqi_cw_max;
	hal->ah_txq[queue].tqi_cw_min = queue_info->tqi_cw_min;
	hal->ah_txq[queue].tqi_flags = queue_info->tqi_flags;

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_get_tx_queueprops(struct ath_hal *hal, int queue,
    HAL_TXQ_INFO *queue_info)
{
	AR5K_ASSERT_ENTRY(queue, hal->ah_capabilities.cap_queues.q_tx_num);
	bcopy(&hal->ah_txq[queue], queue_info, sizeof(HAL_TXQ_INFO));
	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_release_tx_queue(struct ath_hal *hal, u_int queue)
{
	AR5K_ASSERT_ENTRY(queue, hal->ah_capabilities.cap_queues.q_tx_num);

	/* This queue will be skipped in further operations */
	hal->ah_txq[queue].tqi_type = HAL_TX_QUEUE_INACTIVE;

	return (AH_FALSE);
}

void
ar5k_ar5210_init_tx_queue(struct ath_hal *hal, u_int aifs)
{
	int i;
	struct {
		u_int16_t mode_register;
		u_int32_t mode_base;
	} initial[] = AR5K_AR5210_INI_MODE(aifs);

	/*
	 * Write initial mode register settings
	 */
	for (i = 0; i < nitems(initial); i++)
		AR5K_REG_WRITE((u_int32_t)initial[i].mode_register,
		    initial[i].mode_base);
}

HAL_BOOL
ar5k_ar5210_reset_tx_queue(struct ath_hal *hal, u_int queue)
{
	u_int32_t cw_min, retry_lg, retry_sh;
	HAL_TXQ_INFO *tq;

	AR5K_ASSERT_ENTRY(queue, hal->ah_capabilities.cap_queues.q_tx_num);

	tq = &hal->ah_txq[queue];

	/* Only handle data queues, others will be ignored */
	if (tq->tqi_type != HAL_TX_QUEUE_DATA)
		return (AH_TRUE);

	/* Set turbo/base mode parameters */
	ar5k_ar5210_init_tx_queue(hal, hal->ah_aifs + tq->tqi_aifs);

	/*
	 * Set retry limits
	 */
	if (hal->ah_software_retry == AH_TRUE) {
		/* XXX Need to test this */
		retry_lg = hal->ah_limit_tx_retries;
		retry_sh = retry_lg =
		    retry_lg > AR5K_AR5210_RETRY_LMT_SH_RETRY ?
		    AR5K_AR5210_RETRY_LMT_SH_RETRY : retry_lg;
	} else {
		retry_lg = AR5K_INIT_LG_RETRY;
		retry_sh = AR5K_INIT_SH_RETRY;
	}

	/*
	 * Set initial content window (cw_min/cw_max)
	 */
	cw_min = 1;
	while (cw_min < hal->ah_cw_min)
		cw_min = (cw_min << 1) | 1;

	cw_min = tq->tqi_cw_min < 0 ?
	    (cw_min >> (-tq->tqi_cw_min)) :
	    ((cw_min << tq->tqi_cw_min) + (1 << tq->tqi_cw_min) - 1);

	/* Commit values */
	AR5K_REG_WRITE(AR5K_AR5210_RETRY_LMT,
	    (cw_min << AR5K_AR5210_RETRY_LMT_CW_MIN_S)
	    | AR5K_REG_SM(AR5K_INIT_SLG_RETRY, AR5K_AR5210_RETRY_LMT_SLG_RETRY)
	    | AR5K_REG_SM(AR5K_INIT_SSH_RETRY, AR5K_AR5210_RETRY_LMT_SSH_RETRY)
	    | AR5K_REG_SM(retry_lg, AR5K_AR5210_RETRY_LMT_LG_RETRY)
	    | AR5K_REG_SM(retry_sh, AR5K_AR5210_RETRY_LMT_SH_RETRY));

	return (AH_TRUE);
}

u_int32_t
ar5k_ar5210_get_tx_buf(struct ath_hal *hal, u_int queue)
{
	u_int16_t tx_reg;

	AR5K_ASSERT_ENTRY(queue, hal->ah_capabilities.cap_queues.q_tx_num);

	/*
	 * Get the transmit queue descriptor pointer register by type
	 */
	switch (hal->ah_txq[queue].tqi_type) {
	case HAL_TX_QUEUE_DATA:
		tx_reg = AR5K_AR5210_TXDP0;
		break;
	case HAL_TX_QUEUE_BEACON:
	case HAL_TX_QUEUE_CAB:
		tx_reg = AR5K_AR5210_TXDP1;
		break;
	default:
		return (0xffffffff);
	}

	return (AR5K_REG_READ(tx_reg));
}

HAL_BOOL
ar5k_ar5210_put_tx_buf(struct ath_hal *hal, u_int queue, u_int32_t phys_addr)
{
	u_int16_t tx_reg;

	AR5K_ASSERT_ENTRY(queue, hal->ah_capabilities.cap_queues.q_tx_num);

	/*
	 * Get the transmit queue descriptor pointer register by type
	 */
	switch (hal->ah_txq[queue].tqi_type) {
	case HAL_TX_QUEUE_DATA:
		tx_reg = AR5K_AR5210_TXDP0;
		break;
	case HAL_TX_QUEUE_BEACON:
	case HAL_TX_QUEUE_CAB:
		tx_reg = AR5K_AR5210_TXDP1;
		break;
	default:
		return (AH_FALSE);
	}

	/* Set descriptor pointer */
	AR5K_REG_WRITE(tx_reg, phys_addr);

	return (AH_TRUE);
}

u_int32_t
ar5k_ar5210_num_tx_pending(struct ath_hal *hal, u_int queue)
{
	return (AH_FALSE);
}

HAL_BOOL
ar5k_ar5210_tx_start(struct ath_hal *hal, u_int queue)
{
	u_int32_t tx_queue;

	AR5K_ASSERT_ENTRY(queue, hal->ah_capabilities.cap_queues.q_tx_num);

	tx_queue = AR5K_REG_READ(AR5K_AR5210_CR);

	/*
	 * Set the queue type
	 */
	switch (hal->ah_txq[queue].tqi_type) {
	case HAL_TX_QUEUE_DATA:
		tx_queue |= AR5K_AR5210_CR_TXE0 & ~AR5K_AR5210_CR_TXD0;
		break;

	case HAL_TX_QUEUE_BEACON:
		tx_queue |= AR5K_AR5210_CR_TXE1 & ~AR5K_AR5210_CR_TXD1;
		AR5K_REG_WRITE(AR5K_AR5210_BSR,
		    AR5K_AR5210_BCR_TQ1V | AR5K_AR5210_BCR_BDMAE);
		break;

	case HAL_TX_QUEUE_CAB:
		tx_queue |= AR5K_AR5210_CR_TXE1 & ~AR5K_AR5210_CR_TXD1;
		AR5K_REG_WRITE(AR5K_AR5210_BSR,
		    AR5K_AR5210_BCR_TQ1FV | AR5K_AR5210_BCR_TQ1V |
		    AR5K_AR5210_BCR_BDMAE);
		break;

	default:
		return (AH_FALSE);
	}

	/* Start queue */
	AR5K_REG_WRITE(AR5K_AR5210_CR, tx_queue);

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_stop_tx_dma(struct ath_hal *hal, u_int queue)
{
	u_int32_t tx_queue;

	AR5K_ASSERT_ENTRY(queue, hal->ah_capabilities.cap_queues.q_tx_num);

	tx_queue = AR5K_REG_READ(AR5K_AR5210_CR);

	/*
	 * Set by queue type
	 */
	switch (hal->ah_txq[queue].tqi_type) {
	case HAL_TX_QUEUE_DATA:
		tx_queue |= AR5K_AR5210_CR_TXD0 & ~AR5K_AR5210_CR_TXE0;
		break;

	case HAL_TX_QUEUE_BEACON:
	case HAL_TX_QUEUE_CAB:
		/* XXX Fix me... */
		tx_queue |= AR5K_AR5210_CR_TXD1 & ~AR5K_AR5210_CR_TXD1;
		AR5K_REG_WRITE(AR5K_AR5210_BSR, 0);
		break;

	default:
		return (AH_FALSE);
	}

	/* Stop queue */
	AR5K_REG_WRITE(AR5K_AR5210_CR, tx_queue);

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_setup_tx_desc(struct ath_hal *hal, struct ath_desc *desc,
    u_int packet_length, u_int header_length, HAL_PKT_TYPE type, u_int tx_power,
    u_int tx_rate0, u_int tx_tries0, u_int key_index, u_int antenna_mode,
    u_int flags, u_int rtscts_rate, u_int rtscts_duration)
{
	u_int32_t frame_type;
	struct ar5k_ar5210_tx_desc *tx_desc;

	tx_desc = (struct ar5k_ar5210_tx_desc*)&desc->ds_ctl0;

	/*
	 * Validate input
	 */
	if (tx_tries0 == 0)
		return (AH_FALSE);

	if ((tx_desc->tx_control_0 = (packet_length &
	    AR5K_AR5210_DESC_TX_CTL0_FRAME_LEN)) != packet_length)
		return (AH_FALSE);

	if ((tx_desc->tx_control_0 = (header_length &
	    AR5K_AR5210_DESC_TX_CTL0_HEADER_LEN)) != header_length)
		return (AH_FALSE);

	if (type == HAL_PKT_TYPE_BEACON || type == HAL_PKT_TYPE_PROBE_RESP)
		frame_type = AR5K_AR5210_DESC_TX_FRAME_TYPE_NO_DELAY;
	else if (type == HAL_PKT_TYPE_PIFS)
		frame_type = AR5K_AR5210_DESC_TX_FRAME_TYPE_PIFS;
	else
		frame_type = type;

	tx_desc->tx_control_0 =
	    AR5K_REG_SM(frame_type, AR5K_AR5210_DESC_TX_CTL0_FRAME_TYPE);
	tx_desc->tx_control_0 |=
	    AR5K_REG_SM(tx_rate0, AR5K_AR5210_DESC_TX_CTL0_XMIT_RATE);

#define _TX_FLAGS(_c, _flag)						\
	if (flags & HAL_TXDESC_##_flag)					\
		tx_desc->tx_control_##_c |=				\
			AR5K_AR5210_DESC_TX_CTL##_c##_##_flag

	_TX_FLAGS(0, CLRDMASK);
	_TX_FLAGS(0, INTREQ);
	_TX_FLAGS(0, RTSENA);

#undef _TX_FLAGS

	/*
	 * WEP crap
	 */
	if (key_index != HAL_TXKEYIX_INVALID) {
		tx_desc->tx_control_0 |=
		    AR5K_AR5210_DESC_TX_CTL0_ENCRYPT_KEY_VALID;
		tx_desc->tx_control_1 |=
		    AR5K_REG_SM(key_index,
		    AR5K_AR5210_DESC_TX_CTL1_ENCRYPT_KEY_INDEX);
	}

	/*
	 * RTS/CTS
	 */
	if (flags & (HAL_TXDESC_RTSENA | HAL_TXDESC_CTSENA)) {
		tx_desc->tx_control_1 |=
		    rtscts_duration & AR5K_AR5210_DESC_TX_CTL1_RTS_DURATION;
	}

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_fill_tx_desc(struct ath_hal *hal, struct ath_desc *desc,
    u_int segment_length, HAL_BOOL first_segment, HAL_BOOL last_segment)
{
	struct ar5k_ar5210_tx_desc *tx_desc;

	tx_desc = (struct ar5k_ar5210_tx_desc*)&desc->ds_ctl0;

	/* Clear status descriptor */
	bzero(desc->ds_hw, sizeof(desc->ds_hw));

	/* Validate segment length and initialize the descriptor */
	if (segment_length & ~AR5K_AR5210_DESC_TX_CTL1_BUF_LEN)
		return (AH_FALSE);
	tx_desc->tx_control_1 =
#if 0
	    (tx_desc->tx_control_1 & ~AR5K_AR5210_DESC_TX_CTL1_BUF_LEN) |
#endif
	    segment_length;

	if (first_segment != AH_TRUE)
		tx_desc->tx_control_0 &= ~AR5K_AR5210_DESC_TX_CTL0_FRAME_LEN;

	if (last_segment != AH_TRUE)
		tx_desc->tx_control_1 |= AR5K_AR5210_DESC_TX_CTL1_MORE;

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_setup_xtx_desc(struct ath_hal *hal, struct ath_desc *desc,
    u_int tx_rate1, u_int tx_tries1, u_int tx_rate2, u_int tx_tries2,
    u_int tx_rate3, u_int tx_tries3)
{
	/*
	 * Does this function is for setting up XR? Not sure...
	 * Nevertheless, I didn't find any information about XR support
	 * by the AR5210. This seems to be a slightly new feature.
	 */
	return (AH_FALSE);
}

HAL_STATUS
ar5k_ar5210_proc_tx_desc(struct ath_hal *hal, struct ath_desc *desc)
{
	struct ar5k_ar5210_tx_status *tx_status;
	struct ar5k_ar5210_tx_desc *tx_desc;

	tx_desc = (struct ar5k_ar5210_tx_desc*)&desc->ds_ctl0;
	tx_status = (struct ar5k_ar5210_tx_status*)&desc->ds_hw[0];

	/* No frame has been send or error */
	if ((tx_status->tx_status_1 & AR5K_AR5210_DESC_TX_STATUS1_DONE) == 0)
		return (HAL_EINPROGRESS);

	/*
	 * Get descriptor status
	 */
	desc->ds_us.tx.ts_tstamp =
	    AR5K_REG_MS(tx_status->tx_status_0,
	    AR5K_AR5210_DESC_TX_STATUS0_SEND_TIMESTAMP);
	desc->ds_us.tx.ts_shortretry =
	    AR5K_REG_MS(tx_status->tx_status_0,
	    AR5K_AR5210_DESC_TX_STATUS0_SHORT_RETRY_COUNT);
	desc->ds_us.tx.ts_longretry =
	    AR5K_REG_MS(tx_status->tx_status_0,
	    AR5K_AR5210_DESC_TX_STATUS0_LONG_RETRY_COUNT);
	desc->ds_us.tx.ts_seqnum =
	    AR5K_REG_MS(tx_status->tx_status_1,
	    AR5K_AR5210_DESC_TX_STATUS1_SEQ_NUM);
	desc->ds_us.tx.ts_rssi =
	    AR5K_REG_MS(tx_status->tx_status_1,
	    AR5K_AR5210_DESC_TX_STATUS1_ACK_SIG_STRENGTH);
	desc->ds_us.tx.ts_antenna = 1;
	desc->ds_us.tx.ts_status = 0;
	desc->ds_us.tx.ts_rate =
	    AR5K_REG_MS(tx_desc->tx_control_0,
	    AR5K_AR5210_DESC_TX_CTL0_XMIT_RATE);

	if ((tx_status->tx_status_0 &
	    AR5K_AR5210_DESC_TX_STATUS0_FRAME_XMIT_OK) == 0) {
		if (tx_status->tx_status_0 &
		    AR5K_AR5210_DESC_TX_STATUS0_EXCESSIVE_RETRIES)
			desc->ds_us.tx.ts_status |= HAL_TXERR_XRETRY;

		if (tx_status->tx_status_0 &
		    AR5K_AR5210_DESC_TX_STATUS0_FIFO_UNDERRUN)
			desc->ds_us.tx.ts_status |= HAL_TXERR_FIFO;

		if (tx_status->tx_status_0 &
		    AR5K_AR5210_DESC_TX_STATUS0_FILTERED)
			desc->ds_us.tx.ts_status |= HAL_TXERR_FILT;
	}

	return (HAL_OK);
}

HAL_BOOL
ar5k_ar5210_has_veol(struct ath_hal *hal)
{
	return (AH_FALSE);
}

/*
 * Receive functions
 */

u_int32_t
ar5k_ar5210_get_rx_buf(struct ath_hal *hal)
{
	return (AR5K_REG_READ(AR5K_AR5210_RXDP));
}

void
ar5k_ar5210_put_rx_buf(struct ath_hal *hal, u_int32_t phys_addr)
{
	AR5K_REG_WRITE(AR5K_AR5210_RXDP, phys_addr);
}

void
ar5k_ar5210_start_rx(struct ath_hal *hal)
{
	AR5K_REG_WRITE(AR5K_AR5210_CR, AR5K_AR5210_CR_RXE);
}

HAL_BOOL
ar5k_ar5210_stop_rx_dma(struct ath_hal *hal)
{
	int i;

	AR5K_REG_WRITE(AR5K_AR5210_CR, AR5K_AR5210_CR_RXD);

	/*
	 * It may take some time to disable the DMA receive unit
	 */
	for (i = 2000;
	     i > 0 && (AR5K_REG_READ(AR5K_AR5210_CR) & AR5K_AR5210_CR_RXE) != 0;
	     i--)
		AR5K_DELAY(10);

	return (i > 0 ? AH_TRUE : AH_FALSE);
}

void
ar5k_ar5210_start_rx_pcu(struct ath_hal *hal)
{
	AR5K_REG_DISABLE_BITS(AR5K_AR5210_DIAG_SW, AR5K_AR5210_DIAG_SW_DIS_RX);
}

void
ar5k_ar5210_stop_pcu_recv(struct ath_hal *hal)
{
	AR5K_REG_ENABLE_BITS(AR5K_AR5210_DIAG_SW, AR5K_AR5210_DIAG_SW_DIS_RX);
}

void
ar5k_ar5210_set_mcast_filter(struct ath_hal *hal, u_int32_t filter0,
    u_int32_t filter1)
{
	/* Set the multicast filter */
	AR5K_REG_WRITE(AR5K_AR5210_MCAST_FIL0, filter0);
	AR5K_REG_WRITE(AR5K_AR5210_MCAST_FIL1, filter1);
}

HAL_BOOL
ar5k_ar5210_set_mcast_filterindex(struct ath_hal *hal, u_int32_t index)
{
	if (index >= 64) {
		return (AH_FALSE);
	} else if (index >= 32) {
		AR5K_REG_ENABLE_BITS(AR5K_AR5210_MCAST_FIL1,
		    (1 << (index - 32)));
	} else {
		AR5K_REG_ENABLE_BITS(AR5K_AR5210_MCAST_FIL0,
		    (1 << index));
	}

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_clear_mcast_filter_idx(struct ath_hal *hal, u_int32_t index)
{
	if (index >= 64) {
		return (AH_FALSE);
	} else if (index >= 32) {
		AR5K_REG_DISABLE_BITS(AR5K_AR5210_MCAST_FIL1,
		    (1 << (index - 32)));
	} else {
		AR5K_REG_DISABLE_BITS(AR5K_AR5210_MCAST_FIL0,
		    (1 << index));
	}

	return (AH_TRUE);
}

u_int32_t
ar5k_ar5210_get_rx_filter(struct ath_hal *hal)
{
	return (AR5K_REG_READ(AR5K_AR5210_RX_FILTER));
}

void
ar5k_ar5210_set_rx_filter(struct ath_hal *hal, u_int32_t filter)
{
	/*
	 * The AR5210 uses promiscuous mode to detect radar activity
	 */
	if (filter & HAL_RX_FILTER_PHYRADAR) {
		filter &= ~HAL_RX_FILTER_PHYRADAR;
		filter |= AR5K_AR5210_RX_FILTER_PROMISC;
	}

	AR5K_REG_WRITE(AR5K_AR5210_RX_FILTER, filter);
}

HAL_BOOL
ar5k_ar5210_setup_rx_desc(struct ath_hal *hal, struct ath_desc *desc,
    u_int32_t size, u_int flags)
{
	struct ar5k_ar5210_rx_desc *rx_desc;

	rx_desc = (struct ar5k_ar5210_rx_desc*)&desc->ds_ctl0;

	if ((rx_desc->rx_control_1 = (size &
	    AR5K_AR5210_DESC_RX_CTL1_BUF_LEN)) != size)
		return (AH_FALSE);

	if (flags & HAL_RXDESC_INTREQ)
		rx_desc->rx_control_1 |= AR5K_AR5210_DESC_RX_CTL1_INTREQ;

	return (AH_TRUE);
}

HAL_STATUS
ar5k_ar5210_proc_rx_desc(struct ath_hal *hal, struct ath_desc *desc,
    u_int32_t phys_addr, struct ath_desc *next)
{
	struct ar5k_ar5210_rx_status *rx_status;

	rx_status = (struct ar5k_ar5210_rx_status*)&desc->ds_hw[0];

	/* No frame received / not ready */
	if ((rx_status->rx_status_1 & AR5K_AR5210_DESC_RX_STATUS1_DONE) == 0)
		return (HAL_EINPROGRESS);

	/*
	 * Frame receive status
	 */
	desc->ds_us.rx.rs_datalen = rx_status->rx_status_0 &
	    AR5K_AR5210_DESC_RX_STATUS0_DATA_LEN;
	desc->ds_us.rx.rs_rssi =
	    AR5K_REG_MS(rx_status->rx_status_0,
	    AR5K_AR5210_DESC_RX_STATUS0_RECEIVE_SIGNAL);
	desc->ds_us.rx.rs_rate =
	    AR5K_REG_MS(rx_status->rx_status_0,
	    AR5K_AR5210_DESC_RX_STATUS0_RECEIVE_RATE);
	desc->ds_us.rx.rs_antenna = rx_status->rx_status_0 &
	    AR5K_AR5210_DESC_RX_STATUS0_RECEIVE_ANTENNA;
	desc->ds_us.rx.rs_more = rx_status->rx_status_0 &
	    AR5K_AR5210_DESC_RX_STATUS0_MORE;
	desc->ds_us.rx.rs_tstamp =
	    AR5K_REG_MS(rx_status->rx_status_1,
	    AR5K_AR5210_DESC_RX_STATUS1_RECEIVE_TIMESTAMP);
	desc->ds_us.rx.rs_status = 0;

	/*
	 * Key table status
	 */
	if (rx_status->rx_status_1 &
	    AR5K_AR5210_DESC_RX_STATUS1_KEY_INDEX_VALID) {
		desc->ds_us.rx.rs_keyix =
		    AR5K_REG_MS(rx_status->rx_status_1,
		    AR5K_AR5210_DESC_RX_STATUS1_KEY_INDEX);
	} else {
		desc->ds_us.rx.rs_keyix = HAL_RXKEYIX_INVALID;
	}

	/*
	 * Receive/descriptor errors
	 */
	if ((rx_status->rx_status_1 &
	    AR5K_AR5210_DESC_RX_STATUS1_FRAME_RECEIVE_OK) == 0) {
		if (rx_status->rx_status_1 &
		    AR5K_AR5210_DESC_RX_STATUS1_CRC_ERROR)
			desc->ds_us.rx.rs_status |= HAL_RXERR_CRC;

		if (rx_status->rx_status_1 &
		    AR5K_AR5210_DESC_RX_STATUS1_FIFO_OVERRUN)
			desc->ds_us.rx.rs_status |= HAL_RXERR_FIFO;

		if (rx_status->rx_status_1 &
		    AR5K_AR5210_DESC_RX_STATUS1_PHY_ERROR) {
			desc->ds_us.rx.rs_status |= HAL_RXERR_PHY;
			desc->ds_us.rx.rs_phyerr =
			    AR5K_REG_MS(rx_status->rx_status_1,
			    AR5K_AR5210_DESC_RX_STATUS1_PHY_ERROR);
		}

		if (rx_status->rx_status_1 &
		    AR5K_AR5210_DESC_RX_STATUS1_DECRYPT_CRC_ERROR)
			desc->ds_us.rx.rs_status |= HAL_RXERR_DECRYPT;
	}

	return (HAL_OK);
}

void
ar5k_ar5210_set_rx_signal(struct ath_hal *hal)
{
	/* Signal state monitoring is not yet supported */
}

/*
 * Misc functions
 */

void
ar5k_ar5210_dump_state(struct ath_hal *hal)
{
#ifdef AR5K_DEBUG
#define AR5K_PRINT_REGISTER(_x)						\
	printf("(%s: %08x) ", #_x, AR5K_REG_READ(AR5K_AR5210_##_x));

	printf("DMA registers:\n");
	AR5K_PRINT_REGISTER(TXDP0);
	AR5K_PRINT_REGISTER(TXDP1);
	AR5K_PRINT_REGISTER(CR);
	AR5K_PRINT_REGISTER(RXDP);
	AR5K_PRINT_REGISTER(CFG);
	AR5K_PRINT_REGISTER(ISR);
	AR5K_PRINT_REGISTER(IMR);
	AR5K_PRINT_REGISTER(IER);
	AR5K_PRINT_REGISTER(BCR);
	AR5K_PRINT_REGISTER(BSR);
	AR5K_PRINT_REGISTER(TXCFG);
	AR5K_PRINT_REGISTER(RXCFG);
	AR5K_PRINT_REGISTER(MIBC);
	AR5K_PRINT_REGISTER(TOPS);
	AR5K_PRINT_REGISTER(RXNOFRM);
	AR5K_PRINT_REGISTER(TXNOFRM);
	AR5K_PRINT_REGISTER(RPGTO);
	AR5K_PRINT_REGISTER(RFCNT);
	AR5K_PRINT_REGISTER(MISC);
	AR5K_PRINT_REGISTER(RC);
	AR5K_PRINT_REGISTER(SCR);
	AR5K_PRINT_REGISTER(INTPEND);
	AR5K_PRINT_REGISTER(SFR);
	AR5K_PRINT_REGISTER(PCICFG);
	AR5K_PRINT_REGISTER(GPIOCR);
	AR5K_PRINT_REGISTER(GPIODO);
	AR5K_PRINT_REGISTER(GPIODI);
	AR5K_PRINT_REGISTER(SREV);
	printf("\n");

	printf("PCU registers:\n");
	AR5K_PRINT_REGISTER(STA_ID0);
	AR5K_PRINT_REGISTER(STA_ID1);
	AR5K_PRINT_REGISTER(BSS_ID0);
	AR5K_PRINT_REGISTER(BSS_ID1);
	AR5K_PRINT_REGISTER(SLOT_TIME);
	AR5K_PRINT_REGISTER(TIME_OUT);
	AR5K_PRINT_REGISTER(RSSI_THR);
	AR5K_PRINT_REGISTER(RETRY_LMT);
	AR5K_PRINT_REGISTER(USEC);
	AR5K_PRINT_REGISTER(BEACON);
	AR5K_PRINT_REGISTER(CFP_PERIOD);
	AR5K_PRINT_REGISTER(TIMER0);
	AR5K_PRINT_REGISTER(TIMER1);
	AR5K_PRINT_REGISTER(TIMER2);
	AR5K_PRINT_REGISTER(TIMER3);
	AR5K_PRINT_REGISTER(IFS0);
	AR5K_PRINT_REGISTER(IFS1);
	AR5K_PRINT_REGISTER(CFP_DUR);
	AR5K_PRINT_REGISTER(RX_FILTER);
	AR5K_PRINT_REGISTER(MCAST_FIL0);
	AR5K_PRINT_REGISTER(MCAST_FIL1);
	AR5K_PRINT_REGISTER(TX_MASK0);
	AR5K_PRINT_REGISTER(TX_MASK1);
	AR5K_PRINT_REGISTER(CLR_TMASK);
	AR5K_PRINT_REGISTER(TRIG_LVL);
	AR5K_PRINT_REGISTER(DIAG_SW);
	AR5K_PRINT_REGISTER(TSF_L32);
	AR5K_PRINT_REGISTER(TSF_U32);
	AR5K_PRINT_REGISTER(LAST_TSTP);
	AR5K_PRINT_REGISTER(RETRY_CNT);
	AR5K_PRINT_REGISTER(BACKOFF);
	AR5K_PRINT_REGISTER(NAV);
	AR5K_PRINT_REGISTER(RTS_OK);
	AR5K_PRINT_REGISTER(RTS_FAIL);
	AR5K_PRINT_REGISTER(ACK_FAIL);
	AR5K_PRINT_REGISTER(FCS_FAIL);
	AR5K_PRINT_REGISTER(BEACON_CNT);
	AR5K_PRINT_REGISTER(KEYTABLE_0);
	printf("\n");

	printf("PHY registers:\n");
	AR5K_PRINT_REGISTER(PHY(0));
	AR5K_PRINT_REGISTER(PHY_FC);
	AR5K_PRINT_REGISTER(PHY_AGC);
	AR5K_PRINT_REGISTER(PHY_CHIP_ID);
	AR5K_PRINT_REGISTER(PHY_ACTIVE);
	AR5K_PRINT_REGISTER(PHY_AGCCTL);
	printf("\n");
#endif
}

HAL_BOOL
ar5k_ar5210_get_diag_state(struct ath_hal *hal, int id, void **device,
    u_int *size)
{
	/*
	 * We'll ignore this right now. This seems to be some kind of an obscure
         * debugging interface for the binary-only HAL.
	 */
	return (AH_FALSE);
}

void
ar5k_ar5210_get_lladdr(struct ath_hal *hal, u_int8_t *mac)
{
	bcopy(hal->ah_sta_id, mac, IEEE80211_ADDR_LEN);
}

HAL_BOOL
ar5k_ar5210_set_lladdr(struct ath_hal *hal, const u_int8_t *mac)
{
	u_int32_t low_id, high_id;

	/* Set new station ID */
	bcopy(mac, hal->ah_sta_id, IEEE80211_ADDR_LEN);

	low_id = AR5K_LOW_ID(mac);
	high_id = 0x0000ffff & AR5K_HIGH_ID(mac);

	AR5K_REG_WRITE(AR5K_AR5210_STA_ID0, low_id);
	AR5K_REG_WRITE(AR5K_AR5210_STA_ID1, high_id);

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_set_regdomain(struct ath_hal *hal, u_int16_t regdomain,
    HAL_STATUS *status)
{
	ieee80211_regdomain_t ieee_regdomain;

	ieee_regdomain = ar5k_regdomain_to_ieee(regdomain);

	if (ar5k_eeprom_regulation_domain(hal, AH_TRUE,
		&ieee_regdomain) == AH_TRUE) {
		*status = HAL_OK;
		return (AH_TRUE);
	}

	*status = EIO;

	return (AH_FALSE);
}

void
ar5k_ar5210_set_ledstate(struct ath_hal *hal, HAL_LED_STATE state)
{
	u_int32_t led;

	led = AR5K_REG_READ(AR5K_AR5210_PCICFG);

	/*
	 * Some blinking values, define at your wish
	 */
	switch (state) {
	case IEEE80211_S_SCAN:
	case IEEE80211_S_INIT:
		led |=
		    AR5K_AR5210_PCICFG_LED_PEND |
		    AR5K_AR5210_PCICFG_LED_BCTL;
		break;
	case IEEE80211_S_RUN:
		led |=
		    AR5K_AR5210_PCICFG_LED_ACT;
		break;
	default:
		led |=
		    AR5K_AR5210_PCICFG_LED_ACT |
		    AR5K_AR5210_PCICFG_LED_BCTL;
		break;
	}

	AR5K_REG_WRITE(AR5K_AR5210_PCICFG, led);
}

void
ar5k_ar5210_set_associd(struct ath_hal *hal, const u_int8_t *bssid,
    u_int16_t assoc_id, u_int16_t tim_offset)
{
	u_int32_t low_id, high_id;

	/*
	 * Set BSSID which triggers the "SME Join" operation
	 */
	low_id = AR5K_LOW_ID(bssid);
	high_id = AR5K_HIGH_ID(bssid);
	AR5K_REG_WRITE(AR5K_AR5210_BSS_ID0, low_id);
	AR5K_REG_WRITE(AR5K_AR5210_BSS_ID1, high_id |
	    ((assoc_id & 0x3fff) << AR5K_AR5210_BSS_ID1_AID_S));
	bcopy(bssid, &hal->ah_bssid, IEEE80211_ADDR_LEN);

	if (assoc_id == 0) {
		ar5k_ar5210_disable_pspoll(hal);
		return;
	}

	AR5K_REG_WRITE_BITS(AR5K_AR5210_BEACON, AR5K_AR5210_BEACON_TIM,
	    tim_offset ? tim_offset + 4 : 0);

	ar5k_ar5210_enable_pspoll(hal, NULL, 0);
}

HAL_BOOL
ar5k_ar5210_set_bssid_mask(struct ath_hal *hal, const u_int8_t* mask)
{
	/* Not supported in 5210 */
	return (AH_FALSE);
}

HAL_BOOL
ar5k_ar5210_set_gpio_output(struct ath_hal *hal, u_int32_t gpio)
{
	if (gpio > AR5K_AR5210_NUM_GPIO)
		return (AH_FALSE);

	AR5K_REG_WRITE(AR5K_AR5210_GPIOCR,
	    (AR5K_REG_READ(AR5K_AR5210_GPIOCR) &~ AR5K_AR5210_GPIOCR_ALL(gpio))
	    | AR5K_AR5210_GPIOCR_OUT1(gpio));

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_set_gpio_input(struct ath_hal *hal, u_int32_t gpio)
{
	if (gpio > AR5K_AR5210_NUM_GPIO)
		return (AH_FALSE);

	AR5K_REG_WRITE(AR5K_AR5210_GPIOCR,
	    (AR5K_REG_READ(AR5K_AR5210_GPIOCR) &~ AR5K_AR5210_GPIOCR_ALL(gpio))
	    | AR5K_AR5210_GPIOCR_IN(gpio));

	return (AH_TRUE);
}

u_int32_t
ar5k_ar5210_get_gpio(struct ath_hal *hal, u_int32_t gpio)
{
	if (gpio > AR5K_AR5210_NUM_GPIO)
		return (0xffffffff);

	/* GPIO input magic */
	return (((AR5K_REG_READ(AR5K_AR5210_GPIODI) &
		     AR5K_AR5210_GPIOD_MASK) >> gpio) & 0x1);
}

HAL_BOOL
ar5k_ar5210_set_gpio(struct ath_hal *hal, u_int32_t gpio, u_int32_t val)
{
	u_int32_t data;

	if (gpio > AR5K_AR5210_NUM_GPIO)
		return (0xffffffff);

	/* GPIO output magic */
	data =  AR5K_REG_READ(AR5K_AR5210_GPIODO);

	data &= ~(1 << gpio);
	data |= (val&1) << gpio;

	AR5K_REG_WRITE(AR5K_AR5210_GPIODO, data);

	return (AH_TRUE);
}

void
ar5k_ar5210_set_gpio_intr(struct ath_hal *hal, u_int gpio,
    u_int32_t interrupt_level)
{
	u_int32_t data;

	if (gpio > AR5K_AR5210_NUM_GPIO)
		return;

	/*
	 * Set the GPIO interrupt
	 */
	data = (AR5K_REG_READ(AR5K_AR5210_GPIOCR) &
	    ~(AR5K_AR5210_GPIOCR_INT_SEL(gpio) | AR5K_AR5210_GPIOCR_INT_SELH |
		AR5K_AR5210_GPIOCR_INT_ENA | AR5K_AR5210_GPIOCR_ALL(gpio))) |
	    (AR5K_AR5210_GPIOCR_INT_SEL(gpio) | AR5K_AR5210_GPIOCR_INT_ENA);

	AR5K_REG_WRITE(AR5K_AR5210_GPIOCR,
	    interrupt_level ? data : (data | AR5K_AR5210_GPIOCR_INT_SELH));

	hal->ah_imr |= AR5K_AR5210_IMR_GPIO;

	/* Enable GPIO interrupts */
	AR5K_REG_ENABLE_BITS(AR5K_AR5210_IMR, AR5K_AR5210_IMR_GPIO);
}

u_int32_t
ar5k_ar5210_get_tsf32(struct ath_hal *hal)
{
	return (AR5K_REG_READ(AR5K_AR5210_TSF_L32));
}

u_int64_t
ar5k_ar5210_get_tsf64(struct ath_hal *hal)
{
	u_int64_t tsf = AR5K_REG_READ(AR5K_AR5210_TSF_U32);
	return (AR5K_REG_READ(AR5K_AR5210_TSF_L32) | (tsf << 32));
}

void
ar5k_ar5210_reset_tsf(struct ath_hal *hal)
{
	AR5K_REG_ENABLE_BITS(AR5K_AR5210_BEACON,
	    AR5K_AR5210_BEACON_RESET_TSF);
}

u_int16_t
ar5k_ar5210_get_regdomain(struct ath_hal *hal)
{
	return (ar5k_get_regdomain(hal));
}

HAL_BOOL
ar5k_ar5210_detect_card_present(struct ath_hal *hal)
{
	u_int16_t magic;

	/*
	 * Checking the EEPROM's magic value could be an indication
	 * if the card is still present. I didn't find another suitable
	 * way to do this.
	 */
	if (ar5k_ar5210_eeprom_read(hal, AR5K_EEPROM_MAGIC, &magic) != 0)
		return (AH_FALSE);

	return (magic == AR5K_EEPROM_MAGIC_VALUE ? AH_TRUE : AH_FALSE);
}

void
ar5k_ar5210_update_mib_counters(struct ath_hal *hal, HAL_MIB_STATS *statistics)
{
	statistics->ackrcv_bad += AR5K_REG_READ(AR5K_AR5210_ACK_FAIL);
	statistics->rts_bad += AR5K_REG_READ(AR5K_AR5210_RTS_FAIL);
	statistics->rts_good += AR5K_REG_READ(AR5K_AR5210_RTS_OK);
	statistics->fcs_bad += AR5K_REG_READ(AR5K_AR5210_FCS_FAIL);
	statistics->beacons += AR5K_REG_READ(AR5K_AR5210_BEACON_CNT);
}

HAL_RFGAIN
ar5k_ar5210_get_rf_gain(struct ath_hal *hal)
{
	return (HAL_RFGAIN_INACTIVE);
}

HAL_BOOL
ar5k_ar5210_set_slot_time(struct ath_hal *hal, u_int slot_time)
{
	if (slot_time < HAL_SLOT_TIME_9 || slot_time > HAL_SLOT_TIME_MAX)
		return (AH_FALSE);

	AR5K_REG_WRITE(AR5K_AR5210_SLOT_TIME, ar5k_htoclock(slot_time));

	return (AH_TRUE);
}

u_int
ar5k_ar5210_get_slot_time(struct ath_hal *hal)
{
	return (ar5k_clocktoh(AR5K_REG_READ(AR5K_AR5210_SLOT_TIME) & 0xffff));
}

HAL_BOOL
ar5k_ar5210_set_ack_timeout(struct ath_hal *hal, u_int timeout)
{
	if (ar5k_clocktoh(AR5K_REG_MS(0xffffffff, AR5K_AR5210_TIME_OUT_ACK))
		<= timeout)
		return (AH_FALSE);

	AR5K_REG_WRITE_BITS(AR5K_AR5210_TIME_OUT, AR5K_AR5210_TIME_OUT_ACK,
	    ar5k_htoclock(timeout));

	return (AH_TRUE);
}

u_int
ar5k_ar5210_get_ack_timeout(struct ath_hal *hal)
{
	return (ar5k_clocktoh(AR5K_REG_MS(AR5K_REG_READ(AR5K_AR5210_TIME_OUT),
	    AR5K_AR5210_TIME_OUT_ACK)));
}

HAL_BOOL
ar5k_ar5210_set_cts_timeout(struct ath_hal *hal, u_int timeout)
{
	if (ar5k_clocktoh(AR5K_REG_MS(0xffffffff, AR5K_AR5210_TIME_OUT_CTS))
	    <= timeout)
		return (AH_FALSE);

	AR5K_REG_WRITE_BITS(AR5K_AR5210_TIME_OUT, AR5K_AR5210_TIME_OUT_CTS,
	    ar5k_htoclock(timeout));

	return (AH_TRUE);
}

u_int
ar5k_ar5210_get_cts_timeout(struct ath_hal *hal)
{
	return (ar5k_clocktoh(AR5K_REG_MS(AR5K_REG_READ(AR5K_AR5210_TIME_OUT),
	    AR5K_AR5210_TIME_OUT_CTS)));
}

/*
 * Key table (WEP) functions
 */

HAL_BOOL
ar5k_ar5210_is_cipher_supported(struct ath_hal *hal, HAL_CIPHER cipher)
{
	/*
	 * The AR5210 only supports WEP
	 */
	if (cipher == HAL_CIPHER_WEP)
		return (AH_TRUE);

	return (AH_FALSE);
}

u_int32_t
ar5k_ar5210_get_keycache_size(struct ath_hal *hal)
{
	return (AR5K_AR5210_KEYCACHE_SIZE);
}

HAL_BOOL
ar5k_ar5210_reset_key(struct ath_hal *hal, u_int16_t entry)
{
	int i;

	AR5K_ASSERT_ENTRY(entry, AR5K_AR5210_KEYTABLE_SIZE);

	for (i = 0; i < AR5K_AR5210_KEYCACHE_SIZE; i++)
		AR5K_REG_WRITE(AR5K_AR5210_KEYTABLE_OFF(entry, i), 0);

	return (AH_FALSE);
}

HAL_BOOL
ar5k_ar5210_is_key_valid(struct ath_hal *hal, u_int16_t entry)
{
	AR5K_ASSERT_ENTRY(entry, AR5K_AR5210_KEYTABLE_SIZE);

	/*
	 * Check the validation flag at the end of the entry
	 */
	if (AR5K_REG_READ(AR5K_AR5210_KEYTABLE_MAC1(entry)) &
	    AR5K_AR5210_KEYTABLE_VALID)
		return (AH_TRUE);

	return (AH_FALSE);
}

HAL_BOOL
ar5k_ar5210_set_key(struct ath_hal *hal, u_int16_t entry,
    const HAL_KEYVAL *keyval, const u_int8_t *mac, int xor_notused)
{
	int i;
	u_int32_t key_v[AR5K_AR5210_KEYCACHE_SIZE - 2];

	AR5K_ASSERT_ENTRY(entry, AR5K_AR5210_KEYTABLE_SIZE);

	bzero(&key_v, sizeof(key_v));

	switch (keyval->wk_len) {
	case AR5K_KEYVAL_LENGTH_40:
		bcopy(keyval->wk_key, &key_v[0], 4);
		bcopy(keyval->wk_key + 4, &key_v[1], 1);
		key_v[5] = AR5K_AR5210_KEYTABLE_TYPE_40;
		break;

	case AR5K_KEYVAL_LENGTH_104:
		bcopy(keyval->wk_key, &key_v[0], 4);
		bcopy(keyval->wk_key + 4, &key_v[1], 2);
		bcopy(keyval->wk_key + 6, &key_v[2], 4);
		bcopy(keyval->wk_key + 10, &key_v[3], 2);
		bcopy(keyval->wk_key + 12, &key_v[4], 1);
		key_v[5] = AR5K_AR5210_KEYTABLE_TYPE_104;
		break;

	case AR5K_KEYVAL_LENGTH_128:
		bcopy(keyval->wk_key, &key_v[0], 4);
		bcopy(keyval->wk_key + 4, &key_v[1], 2);
		bcopy(keyval->wk_key + 6, &key_v[2], 4);
		bcopy(keyval->wk_key + 10, &key_v[3], 2);
		bcopy(keyval->wk_key + 12, &key_v[4], 4);
		key_v[5] = AR5K_AR5210_KEYTABLE_TYPE_128;
		break;

	default:
		/* Unsupported key length (not WEP40/104/128) */
		return (AH_FALSE);
	}

	for (i = 0; i < nitems(key_v); i++)
		AR5K_REG_WRITE(AR5K_AR5210_KEYTABLE_OFF(entry, i), key_v[i]);

	return (ar5k_ar5210_set_key_lladdr(hal, entry, mac));
}

HAL_BOOL
ar5k_ar5210_set_key_lladdr(struct ath_hal *hal, u_int16_t entry,
    const u_int8_t *mac)
{
	u_int32_t low_id, high_id;
	const u_int8_t *mac_v;

	/*
	 * Invalid entry (key table overflow)
	 */
	AR5K_ASSERT_ENTRY(entry, AR5K_AR5210_KEYTABLE_SIZE);

	/* MAC may be NULL if it's a broadcast key */
	mac_v = mac == NULL ? etherbroadcastaddr : mac;

	low_id = AR5K_LOW_ID(mac_v);
	high_id = AR5K_HIGH_ID(mac_v) | AR5K_AR5210_KEYTABLE_VALID;

	AR5K_REG_WRITE(AR5K_AR5210_KEYTABLE_MAC0(entry), low_id);
	AR5K_REG_WRITE(AR5K_AR5210_KEYTABLE_MAC1(entry), high_id);

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_softcrypto(struct ath_hal *hal, HAL_BOOL enable)
{
	u_int32_t bits;
	int i;

	bits = AR5K_AR5210_DIAG_SW_DIS_ENC | AR5K_AR5210_DIAG_SW_DIS_DEC;
	if (enable == AH_TRUE) {
		/* Disable the hardware crypto engine */
		AR5K_REG_ENABLE_BITS(AR5K_AR5210_DIAG_SW, bits);
	} else {
		/* Enable the hardware crypto engine */
		AR5K_REG_DISABLE_BITS(AR5K_AR5210_DIAG_SW, bits);
	}

	/* Reset the key cache */
	for (i = 0; i < AR5K_AR5210_KEYTABLE_SIZE; i++)
		ar5k_ar5210_reset_key(hal, i);

	return (AH_TRUE);
}

/*
 * Power management functions
 */

HAL_BOOL
ar5k_ar5210_set_power(struct ath_hal *hal, HAL_POWER_MODE mode,
    HAL_BOOL set_chip, u_int16_t sleep_duration)
{
	u_int32_t staid;
	int i;

	staid = AR5K_REG_READ(AR5K_AR5210_STA_ID1);

	switch (mode) {
	case HAL_PM_AUTO:
		staid &= ~AR5K_AR5210_STA_ID1_DEFAULT_ANTENNA;
		/* FALLTHROUGH */
	case HAL_PM_NETWORK_SLEEP:
		if (set_chip == AH_TRUE) {
			AR5K_REG_WRITE(AR5K_AR5210_SCR,
			    AR5K_AR5210_SCR_SLE | sleep_duration);
		}
		staid |= AR5K_AR5210_STA_ID1_PWR_SV;
		break;

	case HAL_PM_FULL_SLEEP:
		if (set_chip == AH_TRUE) {
			AR5K_REG_WRITE(AR5K_AR5210_SCR,
			    AR5K_AR5210_SCR_SLE_SLP);
		}
		staid |= AR5K_AR5210_STA_ID1_PWR_SV;
		break;

	case HAL_PM_AWAKE:
		if (set_chip == AH_FALSE)
			goto commit;

		AR5K_REG_WRITE(AR5K_AR5210_SCR, AR5K_AR5210_SCR_SLE_WAKE);

		for (i = 5000; i > 0; i--) {
			/* Check if the AR5210 did wake up */
			if ((AR5K_REG_READ(AR5K_AR5210_PCICFG) &
			    AR5K_AR5210_PCICFG_SPWR_DN) == 0)
				break;

			/* Wait a bit and retry */
			AR5K_DELAY(200);
			AR5K_REG_WRITE(AR5K_AR5210_SCR,
			    AR5K_AR5210_SCR_SLE_WAKE);
		}

		/* Fail if the AR5210 didn't wake up */
		if (i <= 0)
			return (AH_FALSE);

		staid &= ~AR5K_AR5210_STA_ID1_PWR_SV;
		break;

	default:
		return (AH_FALSE);
	}

 commit:
	hal->ah_power_mode = mode;

	AR5K_REG_WRITE(AR5K_AR5210_STA_ID1, staid);

	return (AH_TRUE);
}

HAL_POWER_MODE
ar5k_ar5210_get_power_mode(struct ath_hal *hal)
{
	return (hal->ah_power_mode);
}

HAL_BOOL
ar5k_ar5210_query_pspoll_support(struct ath_hal *hal)
{
	/* I think so, why not? */
	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_init_pspoll(struct ath_hal *hal)
{
	/*
	 * Not used on the AR5210
	 */
	return (AH_FALSE);
}

HAL_BOOL
ar5k_ar5210_enable_pspoll(struct ath_hal *hal, u_int8_t *bssid,
    u_int16_t assoc_id)
{
	AR5K_REG_DISABLE_BITS(AR5K_AR5210_STA_ID1,
	    AR5K_AR5210_STA_ID1_NO_PSPOLL |
	    AR5K_AR5210_STA_ID1_DEFAULT_ANTENNA);

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_disable_pspoll(struct ath_hal *hal)
{
	AR5K_REG_ENABLE_BITS(AR5K_AR5210_STA_ID1,
	    AR5K_AR5210_STA_ID1_NO_PSPOLL |
	    AR5K_AR5210_STA_ID1_DEFAULT_ANTENNA);

	return (AH_TRUE);
}

/*
 * Beacon functions
 */

void
ar5k_ar5210_init_beacon(struct ath_hal *hal, u_int32_t next_beacon,
    u_int32_t interval)
{
	u_int32_t timer1, timer2, timer3;

	/*
	 * Set the additional timers by mode
	 */
	switch (hal->ah_op_mode) {
	case HAL_M_STA:
		timer1 = 0xffffffff;
		timer2 = 0xffffffff;
		timer3 = 1;
		break;

	default:
		timer1 = (next_beacon - AR5K_TUNE_DMA_BEACON_RESP) << 3;
		timer2 = (next_beacon - AR5K_TUNE_SW_BEACON_RESP) << 3;
		timer3 = next_beacon + hal->ah_atim_window;
		break;
	}

	/*
	 * Enable all timers and set the beacon register
	 * (next beacon, DMA beacon, software beacon, ATIM window time)
	 */
	AR5K_REG_WRITE(AR5K_AR5210_TIMER0, next_beacon);
	AR5K_REG_WRITE(AR5K_AR5210_TIMER1, timer1);
	AR5K_REG_WRITE(AR5K_AR5210_TIMER2, timer2);
	AR5K_REG_WRITE(AR5K_AR5210_TIMER3, timer3);

	AR5K_REG_WRITE(AR5K_AR5210_BEACON, interval &
	    (AR5K_AR5210_BEACON_PERIOD | AR5K_AR5210_BEACON_RESET_TSF |
		AR5K_AR5210_BEACON_EN));
}

void
ar5k_ar5210_set_beacon_timers(struct ath_hal *hal,
    const HAL_BEACON_STATE *state, u_int32_t tsf, u_int32_t dtim_count,
    u_int32_t cfp_count)
{
	u_int32_t cfp_period, next_cfp;

	/* Return on an invalid beacon state */
	if (state->bs_interval < 1)
		return;

	/*
	 * PCF support?
	 */
	if (state->bs_cfp_period > 0) {
		/* Enable CFP mode and set the CFP and timer registers */
		cfp_period = state->bs_cfp_period * state->bs_dtim_period *
		    state->bs_interval;
		next_cfp = (cfp_count * state->bs_dtim_period + dtim_count) *
		    state->bs_interval;

		AR5K_REG_DISABLE_BITS(AR5K_AR5210_STA_ID1,
		    AR5K_AR5210_STA_ID1_DEFAULT_ANTENNA |
		    AR5K_AR5210_STA_ID1_PCF);
		AR5K_REG_WRITE(AR5K_AR5210_CFP_PERIOD, cfp_period);
		AR5K_REG_WRITE(AR5K_AR5210_CFP_DUR, state->bs_cfp_max_duration);
		AR5K_REG_WRITE(AR5K_AR5210_TIMER2,
		    (tsf + (next_cfp == 0 ? cfp_period : next_cfp)) << 3);
	} else {
		/* Disable PCF mode */
		AR5K_REG_DISABLE_BITS(AR5K_AR5210_STA_ID1,
		    AR5K_AR5210_STA_ID1_DEFAULT_ANTENNA |
		    AR5K_AR5210_STA_ID1_PCF);
	}

	/*
	 * Enable the beacon timer register
	 */
	AR5K_REG_WRITE(AR5K_AR5210_TIMER0, state->bs_next_beacon);

	/*
	 * Start the beacon timers
	 */
	AR5K_REG_WRITE(AR5K_AR5210_BEACON,
	    (AR5K_REG_READ(AR5K_AR5210_BEACON) &~
		(AR5K_AR5210_BEACON_PERIOD | AR5K_AR5210_BEACON_TIM)) |
	    AR5K_REG_SM(state->bs_tim_offset ? state->bs_tim_offset + 4 : 0,
		AR5K_AR5210_BEACON_TIM) |
	    AR5K_REG_SM(state->bs_interval, AR5K_AR5210_BEACON_PERIOD));

	/*
	 * Write new beacon miss threshold, if it appears to be valid
	 */
	if (state->bs_bmiss_threshold <=
	    (AR5K_AR5210_RSSI_THR_BM_THR >> AR5K_AR5210_RSSI_THR_BM_THR_S)) {
		AR5K_REG_WRITE_BITS(AR5K_AR5210_RSSI_THR,
		    AR5K_AR5210_RSSI_THR_BM_THR, state->bs_bmiss_threshold);
	}
}

void
ar5k_ar5210_reset_beacon(struct ath_hal *hal)
{
	/*
	 * Disable beacon timer
	 */
	AR5K_REG_WRITE(AR5K_AR5210_TIMER0, 0);

	/*
	 * Disable some beacon register values
	 */
	AR5K_REG_DISABLE_BITS(AR5K_AR5210_STA_ID1,
	    AR5K_AR5210_STA_ID1_DEFAULT_ANTENNA | AR5K_AR5210_STA_ID1_PCF);
	AR5K_REG_WRITE(AR5K_AR5210_BEACON, AR5K_AR5210_BEACON_PERIOD);
}

HAL_BOOL
ar5k_ar5210_wait_for_beacon(struct ath_hal *hal, bus_addr_t phys_addr)
{
	int i;

	/*
	 * Wait for beacon queue to be done
	 */
	for (i = (AR5K_TUNE_BEACON_INTERVAL / 2); i > 0 &&
		 (AR5K_REG_READ(AR5K_AR5210_BSR) &
		     AR5K_AR5210_BSR_TXQ1F) != 0 &&
		 (AR5K_REG_READ(AR5K_AR5210_CR) &
		     AR5K_AR5210_CR_TXE1) != 0; i--);

	/* Timeout... */
	if (i <= 0) {
		/*
		 * Re-schedule the beacon queue
		 */
		AR5K_REG_WRITE(AR5K_AR5210_TXDP1, (u_int32_t)phys_addr);
		AR5K_REG_WRITE(AR5K_AR5210_BCR,
		    AR5K_AR5210_BCR_TQ1V | AR5K_AR5210_BCR_BDMAE);

		return (AH_FALSE);
	}

	return (AH_TRUE);
}

/*
 * Interrupt handling
 */

HAL_BOOL
ar5k_ar5210_is_intr_pending(struct ath_hal *hal)
{
	return (AR5K_REG_READ(AR5K_AR5210_INTPEND) == 0 ? AH_FALSE : AH_TRUE);
}

HAL_BOOL
ar5k_ar5210_get_isr(struct ath_hal *hal, u_int32_t *interrupt_mask)
{
	u_int32_t data;

	if ((data = AR5K_REG_READ(AR5K_AR5210_ISR)) == HAL_INT_NOCARD) {
		*interrupt_mask = data;
		return (AH_FALSE);
	}

	/*
	 * Get abstract interrupt mask (HAL-compatible)
	 */
	*interrupt_mask = (data & HAL_INT_COMMON) & hal->ah_imr;

	if (data & (AR5K_AR5210_ISR_RXOK | AR5K_AR5210_ISR_RXERR))
		*interrupt_mask |= HAL_INT_RX;
	if (data & (AR5K_AR5210_ISR_TXOK | AR5K_AR5210_ISR_TXERR))
		*interrupt_mask |= HAL_INT_TX;
	if (data & AR5K_AR5210_ISR_FATAL)
		*interrupt_mask |= HAL_INT_FATAL;

	/*
	 * Special interrupt handling (not caught by the driver)
	 */
	if (((*interrupt_mask) & AR5K_AR5210_ISR_RXPHY) &&
	    hal->ah_radar.r_enabled == AH_TRUE)
		ar5k_radar_alert(hal);

	/* XXX BMISS interrupts may occur after association */
	*interrupt_mask &= ~HAL_INT_BMISS;

	return (AH_TRUE);
}

u_int32_t
ar5k_ar5210_get_intr(struct ath_hal *hal)
{
	/* Return the interrupt mask stored previously */
	return (hal->ah_imr);
}

HAL_INT
ar5k_ar5210_set_intr(struct ath_hal *hal, HAL_INT new_mask)
{
	HAL_INT old_mask, int_mask;

	/*
	 * Disable card interrupts to prevent any race conditions
	 * (they will be re-enabled afterwards).
	 */
	AR5K_REG_WRITE(AR5K_AR5210_IER, AR5K_AR5210_IER_DISABLE);

	old_mask = hal->ah_imr;

	/*
	 * Add additional, chipset-dependent interrupt mask flags
	 * and write them to the IMR (interrupt mask register).
	 */
	int_mask = new_mask & HAL_INT_COMMON;

	if (new_mask & HAL_INT_RX)
		int_mask |=
		    AR5K_AR5210_IMR_RXOK |
		    AR5K_AR5210_IMR_RXERR |
		    AR5K_AR5210_IMR_RXORN;

	if (new_mask & HAL_INT_TX)
		int_mask |=
		    AR5K_AR5210_IMR_TXOK |
		    AR5K_AR5210_IMR_TXERR |
		    AR5K_AR5210_IMR_TXURN;

	AR5K_REG_WRITE(AR5K_AR5210_IMR, int_mask);

	/* Store new interrupt mask */
	hal->ah_imr = new_mask;

	/* ..re-enable interrupts */
	if (int_mask) {
		AR5K_REG_WRITE(AR5K_AR5210_IER, AR5K_AR5210_IER_ENABLE);
	}

	return (old_mask);
}

/*
 * Misc internal functions
 */

HAL_BOOL
ar5k_ar5210_get_capabilities(struct ath_hal *hal)
{
	/* Set number of supported TX queues */
	hal->ah_capabilities.cap_queues.q_tx_num = AR5K_AR5210_TX_NUM_QUEUES;

	/*
	 * Set radio capabilities
	 * (The AR5210 only supports the middle 5GHz band)
	 */
	hal->ah_capabilities.cap_range.range_5ghz_min = 5120;
	hal->ah_capabilities.cap_range.range_5ghz_max = 5430;
	hal->ah_capabilities.cap_range.range_2ghz_min = 0;
	hal->ah_capabilities.cap_range.range_2ghz_max = 0;

	/* Set supported modes */
	hal->ah_capabilities.cap_mode = HAL_MODE_11A;

	/* Set number of GPIO pins */
	hal->ah_gpio_npins = AR5K_AR5210_NUM_GPIO;

	return (AH_TRUE);
}

void
ar5k_ar5210_radar_alert(struct ath_hal *hal, HAL_BOOL enable)
{
	/*
	 * Set the RXPHY interrupt to be able to detect
	 * possible radar activity.
	 */
	AR5K_REG_WRITE(AR5K_AR5210_IER, AR5K_AR5210_IER_DISABLE);

	if (enable == AH_TRUE) {
		AR5K_REG_ENABLE_BITS(AR5K_AR5210_IMR,
		    AR5K_AR5210_IMR_RXPHY);
	} else {
		AR5K_REG_DISABLE_BITS(AR5K_AR5210_IMR,
		    AR5K_AR5210_IMR_RXPHY);
	}

	AR5K_REG_WRITE(AR5K_AR5210_IER, AR5K_AR5210_IER_ENABLE);
}

/*
 * EEPROM access functions
 */

HAL_BOOL
ar5k_ar5210_eeprom_is_busy(struct ath_hal *hal)
{
	return (AR5K_REG_READ(AR5K_AR5210_CFG) & AR5K_AR5210_CFG_EEBS ?
	    AH_TRUE : AH_FALSE);
}

int
ar5k_ar5210_eeprom_read(struct ath_hal *hal, u_int32_t offset, u_int16_t *data)
{
	u_int32_t status, timeout;

	/* Enable eeprom access */
	AR5K_REG_ENABLE_BITS(AR5K_AR5210_PCICFG, AR5K_AR5210_PCICFG_EEAE);

	/*
	 * Prime read pump
	 */
	(void)AR5K_REG_READ(AR5K_AR5210_EEPROM_BASE + (4 * offset));

	for (timeout = 10000; timeout > 0; timeout--) {
		AR5K_DELAY(1);
		status = AR5K_REG_READ(AR5K_AR5210_EEPROM_STATUS);
		if (status & AR5K_AR5210_EEPROM_STAT_RDDONE) {
			if (status & AR5K_AR5210_EEPROM_STAT_RDERR)
				return (EIO);
			*data = (u_int16_t)
			    (AR5K_REG_READ(AR5K_AR5210_EEPROM_RDATA) & 0xffff);
			return (0);
		}
	}

	return (ETIMEDOUT);
}

int
ar5k_ar5210_eeprom_write(struct ath_hal *hal, u_int32_t offset, u_int16_t data)
{
	u_int32_t status, timeout;

	/* Enable eeprom access */
	AR5K_REG_ENABLE_BITS(AR5K_AR5210_PCICFG, AR5K_AR5210_PCICFG_EEAE);

	/*
	 * Prime write pump
	 */
	AR5K_REG_WRITE(AR5K_AR5210_EEPROM_BASE + (4 * offset), data);

	for (timeout = 10000; timeout > 0; timeout--) {
		AR5K_DELAY(1);
		status = AR5K_REG_READ(AR5K_AR5210_EEPROM_STATUS);
		if (status & AR5K_AR5210_EEPROM_STAT_WRDONE) {
			if (status & AR5K_AR5210_EEPROM_STAT_WRERR)
				return (EIO);
			return (0);
		}
	}

	return (ETIMEDOUT);
}

HAL_BOOL
ar5k_ar5210_set_txpower_limit(struct ath_hal *hal, u_int power)
{
	/* Not implemented */
	return (AH_FALSE);
}
