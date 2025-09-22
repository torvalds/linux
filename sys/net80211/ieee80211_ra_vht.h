/*	$OpenBSD: ieee80211_ra_vht.h,v 1.1 2022/03/19 10:25:09 stsp Exp $	*/

/*
 * Copyright (c) 2021 Christian Ehrhardt <ehrhardt@genua.de>
 * Copyright (c) 2021, 2022 Stefan Sperling <stsp@openbsd.org>
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
 * Goodput statistics struct. Measures the effective data rate of a rate.
 * All uint64_t numbers in this struct use fixed-point arithmetic.
 */
struct ieee80211_ra_vht_goodput_stats {
	uint64_t measured;	/* Most recently measured goodput. */
	uint64_t average;	/* Average measured goodput. */
	uint64_t stddeviation;	/* Goodput standard deviation. */
 	uint64_t loss;		/* This rate's loss percentage SFER. */
	uint32_t nprobe_pkts;	/* Number of packets in current probe. */
	uint32_t nprobe_fail;	/* Number of failed packets. */
};

/*
 * Rate adaptation state.
 *
 * Drivers should not modify any fields of this structure directly.
 * Use ieee80211_ra_vht_init() and ieee80211_ra_vht_add_stats() only.
 */
struct ieee80211_ra_vht_node {
	/* Bitmaps for MCS 0-9 for a given number of spatial streams. */
	uint16_t valid_probes[IEEE80211_VHT_NUM_SS];
	uint16_t valid_rates[IEEE80211_VHT_NUM_SS];
	uint16_t candidate_rates[IEEE80211_VHT_NUM_SS];
	uint16_t probed_rates[IEEE80211_VHT_NUM_SS];

	/* Maximum usable MCS per given number of spatial streams. */
	int max_mcs[IEEE80211_VHT_NUM_SS];

	/* Probing state. */
	int probing;
#define IEEE80211_RA_NOT_PROBING	0x0
#define IEEE80211_RA_PROBING_DOWN	0x1
#define IEEE80211_RA_PROBING_UP		0x2
#define IEEE80211_RA_PROBING_INTER	0x4 /* combined with UP or DOWN */

	/* The current best MCS,NSS found by probing. */
	int best_mcs;
	int best_nss;

	/* Goodput statistics for each rate. */
	struct ieee80211_ra_vht_goodput_stats
	    g[IEEE80211_VHT_NUM_RATESETS][IEEE80211_VHT_RATESET_MAX_NRATES];
};

/* Initialize rate adaptation state. */
void	ieee80211_ra_vht_node_init(struct ieee80211_ra_vht_node *);

/*
 * Drivers report information about 802.11ac/VHT Tx attempts here.
 * mcs: The VHT MCS used during this Tx attempt.
 * nss: The number of spatial streams used during this Tx attempt.
 * total: How many Tx attempts (initial attempt + any retries) were made?
 * fail: How many of these Tx attempts failed?
 */
void	ieee80211_ra_vht_add_stats(struct ieee80211_ra_vht_node *,
	    struct ieee80211com *, struct ieee80211_node *,
	    int mcs, int nss, unsigned int total, unsigned int fail);

/* Drivers call this function to update ni->ni_txmcs and ni->ni_vht_ss. */
void	ieee80211_ra_vht_choose(struct ieee80211_ra_vht_node *,
	    struct ieee80211com *, struct ieee80211_node *);

/*
 * Get the VHT rateset for a particular VHT MCS, NSS, with 40MHz, 80MHz,
 * and/or SGI on/off.
 */
const struct ieee80211_vht_rateset * ieee80211_ra_vht_get_rateset(int mcs,
	    int nss, int chan40, int chan80, int sgi);

/* Check whether SGI should be used. */
int ieee80211_ra_vht_use_sgi(struct ieee80211_node *);
