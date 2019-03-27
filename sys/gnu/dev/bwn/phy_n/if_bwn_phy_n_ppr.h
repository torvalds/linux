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

/*
 * $FreeBSD$
 */

#ifndef	__IF_BWN_PHY_PPR_H__
#define	__IF_BWN_PHY_PPR_H__

#define	BWN_PPR_CCK_RATES_NUM		4
#define	BWN_PPR_OFDM_RATES_NUM		8
#define	BWN_PPR_MCS_RATES_NUM		8

#define	BWN_PPR_RATES_NUM	(BWN_PPR_CCK_RATES_NUM +	\
				 BWN_PPR_OFDM_RATES_NUM * 2 +	\
				 BWN_PPR_MCS_RATES_NUM * 4)

struct bwn_ppr_rates {
	uint8_t cck[BWN_PPR_CCK_RATES_NUM];
	uint8_t ofdm[BWN_PPR_OFDM_RATES_NUM];
	uint8_t ofdm_20_cdd[BWN_PPR_OFDM_RATES_NUM];
	uint8_t mcs_20[BWN_PPR_MCS_RATES_NUM]; /* single stream rates */
	uint8_t mcs_20_cdd[BWN_PPR_MCS_RATES_NUM];
	uint8_t mcs_20_stbc[BWN_PPR_MCS_RATES_NUM];
	uint8_t mcs_20_sdm[BWN_PPR_MCS_RATES_NUM];
};

struct bwn_ppr {
	/* All powers are in 1/4 dBm (Q5.2) */
	union {
		uint8_t __all_rates[BWN_PPR_RATES_NUM];
		struct bwn_ppr_rates rates;
	};
};

extern	void bwn_ppr_clear(struct bwn_mac *mac, struct bwn_ppr *ppr);
extern	void bwn_ppr_add(struct bwn_mac *mac, struct bwn_ppr *ppr, int diff);
extern	void bwn_ppr_apply_max(struct bwn_mac *mac, struct bwn_ppr *ppr,
	    uint8_t max);
extern	void bwn_ppr_apply_min(struct bwn_mac *mac, struct bwn_ppr *ppr,
	    uint8_t min);
extern	uint8_t bwn_ppr_get_max(struct bwn_mac *mac, struct bwn_ppr *ppr);
extern	bool bwn_ppr_load_max_from_sprom(struct bwn_mac *mac,
	    struct bwn_ppr *ppr, bwn_phy_band_t band);

#endif
