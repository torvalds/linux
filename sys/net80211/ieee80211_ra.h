/*	$OpenBSD: ieee80211_ra.h,v 1.2 2021/10/11 09:01:06 stsp Exp $	*/

/*
 * Copyright (c) 2021 Christian Ehrhardt <ehrhardt@genua.de>
 * Copyright (c) 2021 Stefan Sperling <stsp@openbsd.org>
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
 * Goodput statistics struct. Measures the effective data rate of an MCS.
 * All uint64_t numbers in this struct use fixed-point arithmetic.
 */
struct ieee80211_ra_goodput_stats {
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
 * Use ieee80211_ra_init() and ieee80211_ra_add_stats() only.
 */
struct ieee80211_ra_node {
	/* Bitmaps MCS 0-31. */
	uint32_t valid_probes;
	uint32_t valid_rates;
	uint32_t candidate_rates;
	uint32_t probed_rates;

	/* Probing state. */
	int probing;
#define IEEE80211_RA_NOT_PROBING	0x0
#define IEEE80211_RA_PROBING_DOWN	0x1
#define IEEE80211_RA_PROBING_UP		0x2
#define IEEE80211_RA_PROBING_INTER	0x4 /* combined with UP or DOWN */

	/* The current best MCS found by probing. */
	int best_mcs;

	/* Goodput statistics for each MCS. */
	struct ieee80211_ra_goodput_stats g[IEEE80211_HT_RATESET_NUM_MCS];
};

/* Initialize rate adaptation state. */
void	ieee80211_ra_node_init(struct ieee80211_ra_node *);

/*
 * Drivers report information about 802.11n/HT Tx attempts here.
 * mcs: The HT MCS used during this Tx attempt.
 * total: How many Tx attempts (initial attempt + any retries) were made?
 * fail: How many of these Tx attempts failed?
 */
void	ieee80211_ra_add_stats_ht(struct ieee80211_ra_node *,
	    struct ieee80211com *, struct ieee80211_node *,
	    int mcs, unsigned int total, unsigned int fail);

/* Drivers call this function to update ni->ni_txmcs. */
void	ieee80211_ra_choose(struct ieee80211_ra_node *,
	    struct ieee80211com *, struct ieee80211_node *);

/* Get the HT rateset for a particular HT MCS with 40MHz and/or SGI on/off. */
const struct ieee80211_ht_rateset * ieee80211_ra_get_ht_rateset(int mcs,
	    int chan40, int sgi);

/* Check whether SGI should be used. */
int ieee80211_ra_use_ht_sgi(struct ieee80211_node *);
