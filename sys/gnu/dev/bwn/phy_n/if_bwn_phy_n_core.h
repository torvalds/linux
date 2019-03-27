/*

  Broadcom B43 wireless driver

  N-PHY core code.

  Copyright (c) 2008 Michael Buesch <m@bues.ch>
  Copyright (c) 2010 Rafał Miłecki <zajec5@gmail.com>
  Copyright (c) 2016 Adrian Chadd <adrian@FreeBSD.org>

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

/*
 * $FreeBSD$
 */

#ifndef	__IF_BWN_PHY_N_CORE_H__
#define	__IF_BWN_PHY_N_CORE_H__

struct bwn_mac;

enum b43_nphy_spur_avoid {
	BWN_SPUR_AVOID_DISABLE,
	BWN_SPUR_AVOID_AUTO,
	BWN_SPUR_AVOID_FORCE,
};

/*
 * TODO: determine whether center_freq is the primary
 * channel centre frequency or the actual centre centre
 * frequency (eg radio tuning.)  It /looks/ like it's
 * actual channel centre.
 */
struct bwn_chanspec {
	uint16_t center_freq;
	/* This is HT40U, HT40D, HT20, no-HT 20, etc */
	bwn_chan_type_t channel_type;
};

struct bwn_phy_n_iq_comp {
	int16_t a0;
	int16_t b0;
	int16_t a1;
	int16_t b1;
};

struct bwn_phy_n_rssical_cache {
	uint16_t rssical_radio_regs_2G[2];
	uint16_t rssical_phy_regs_2G[12];

	uint16_t rssical_radio_regs_5G[2];
	uint16_t rssical_phy_regs_5G[12];
};

struct bwn_phy_n_cal_cache {
	uint16_t txcal_radio_regs_2G[8];
	uint16_t txcal_coeffs_2G[8];
	struct bwn_phy_n_iq_comp rxcal_coeffs_2G;

	uint16_t txcal_radio_regs_5G[8];
	uint16_t txcal_coeffs_5G[8];
	struct bwn_phy_n_iq_comp rxcal_coeffs_5G;
};

struct bwn_phy_n_txpwrindex {
	int8_t index;
	int8_t index_internal;
	int8_t index_internal_save;
	uint16_t AfectrlOverride;
	uint16_t AfeCtrlDacGain;
	uint16_t rad_gain;
	uint8_t bbmult;
	uint16_t iqcomp_a;
	uint16_t iqcomp_b;
	uint16_t locomp;
};

struct bwn_phy_n_pwr_ctl_info {
	uint8_t idle_tssi_2g;
	uint8_t idle_tssi_5g;
};

struct bwn_phy_n {
	uint8_t antsel_type;
	uint8_t cal_orig_pwr_idx[2];
	uint8_t measure_hold;
	uint8_t phyrxchain;
	uint8_t hw_phyrxchain;
	uint8_t hw_phytxchain;
	uint8_t perical;
	uint32_t deaf_count;
	uint32_t rxcalparams;
	bool hang_avoid;
	bool mute;
	uint16_t papd_epsilon_offset[2];
	int32_t preamble_override;
	uint32_t bb_mult_save;

	bool gain_boost;
	bool elna_gain_config;
	bool band5g_pwrgain;
	bool use_int_tx_iq_lo_cal;
	bool lpf_bw_overrode_for_sample_play;

	uint8_t mphase_cal_phase_id;
	uint16_t mphase_txcal_cmdidx;
	uint16_t mphase_txcal_numcmds;
	uint16_t mphase_txcal_bestcoeffs[11];

	bool txpwrctrl;
	bool pwg_gain_5ghz;
	uint8_t tx_pwr_idx[2];
	int8_t tx_power_offset[101];
	uint16_t adj_pwr_tbl[84];
	uint16_t txcal_bbmult;
	uint16_t txiqlocal_bestc[11];
	bool txiqlocal_coeffsvalid;
	struct bwn_phy_n_txpwrindex txpwrindex[2];
	struct bwn_phy_n_pwr_ctl_info pwr_ctl_info[2];
	struct bwn_chanspec txiqlocal_chanspec;
	struct bwn_ppr tx_pwr_max_ppr;
	uint16_t tx_pwr_last_recalc_freq;
	int tx_pwr_last_recalc_limit;
	uint8_t tsspos_2g;

	uint8_t txrx_chain;
	uint16_t tx_rx_cal_phy_saveregs[11];
	uint16_t tx_rx_cal_radio_saveregs[22];

	uint16_t rfctrl_intc1_save;
	uint16_t rfctrl_intc2_save;

	uint16_t classifier_state;
	uint16_t clip_state[2];

	enum b43_nphy_spur_avoid spur_avoid;
	bool aband_spurwar_en;
	bool gband_spurwar_en;

	bool ipa2g_on;
	struct bwn_chanspec iqcal_chanspec_2G;
	struct bwn_chanspec rssical_chanspec_2G;

	bool ipa5g_on;
	struct bwn_chanspec iqcal_chanspec_5G;
	struct bwn_chanspec rssical_chanspec_5G;

	struct bwn_phy_n_rssical_cache rssical_cache;
	struct bwn_phy_n_cal_cache cal_cache;
	bool crsminpwr_adjusted;
	bool noisevars_adjusted;
};

extern	bwn_txpwr_result_t bwn_nphy_op_recalc_txpower(struct bwn_mac *mac, bool ignore_tssi);
extern	int bwn_nphy_op_allocate(struct bwn_mac *mac);
extern	int bwn_nphy_op_prepare_structs(struct bwn_mac *mac);
extern	void bwn_nphy_op_free(struct bwn_mac *mac);
extern	int bwn_nphy_op_init(struct bwn_mac *mac);
extern	void bwn_nphy_op_maskset(struct bwn_mac *mac, uint16_t reg, uint16_t mask, uint16_t set);
extern	uint16_t bwn_nphy_op_radio_read(struct bwn_mac *mac, uint16_t reg);
extern	void bwn_nphy_op_radio_write(struct bwn_mac *mac, uint16_t reg, uint16_t value);
extern	void bwn_nphy_op_software_rfkill(struct bwn_mac *mac, bool blocked);
extern	void bwn_nphy_op_switch_analog(struct bwn_mac *mac, bool on);
extern	int bwn_nphy_op_switch_channel(struct bwn_mac *mac, unsigned int new_channel);
extern	unsigned int bwn_nphy_op_get_default_chan(struct bwn_mac *mac);

#endif	/* __IF_BWN_PHY_N_CORE_H__ */
