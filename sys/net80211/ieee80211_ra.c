/*	$OpenBSD: ieee80211_ra.c,v 1.5 2022/03/19 10:28:44 stsp Exp $	*/

/*
 * Copyright (c) 2021 Christian Ehrhardt <ehrhardt@genua.de>
 * Copyright (c) 2016, 2021 Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2016 Theo Buehler <tb@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_ra.h>

int	ieee80211_ra_next_intra_rate(struct ieee80211_ra_node *,
	    struct ieee80211_node *);
const struct ieee80211_ht_rateset * ieee80211_ra_next_rateset(
		    struct ieee80211_ra_node *, struct ieee80211_node *);
int	ieee80211_ra_best_mcs_in_rateset(struct ieee80211_ra_node *,
	    const struct ieee80211_ht_rateset *);
void	ieee80211_ra_probe_next_rateset(struct ieee80211_ra_node *,
	    struct ieee80211_node *, const struct ieee80211_ht_rateset *);
int	ieee80211_ra_next_mcs(struct ieee80211_ra_node *,
	    struct ieee80211_node *);
void	ieee80211_ra_probe_done(struct ieee80211_ra_node *);
int	ieee80211_ra_intra_mode_ra_finished(
	    struct ieee80211_ra_node *, struct ieee80211_node *);
void	ieee80211_ra_trigger_next_rateset(struct ieee80211_ra_node *,
	    struct ieee80211_node *);
int	ieee80211_ra_inter_mode_ra_finished(
	    struct ieee80211_ra_node *, struct ieee80211_node *);
int	ieee80211_ra_best_rate(struct ieee80211_ra_node *,
	    struct ieee80211_node *);
void	ieee80211_ra_probe_next_rate(struct ieee80211_ra_node *,
	    struct ieee80211_node *);
int	ieee80211_ra_valid_tx_mcs(struct ieee80211com *, int);
uint32_t ieee80211_ra_valid_rates(struct ieee80211com *,
	    struct ieee80211_node *);
int	ieee80211_ra_probe_valid(struct ieee80211_ra_goodput_stats *);

/* We use fixed point arithmetic with 64 bit integers. */
#define RA_FP_SHIFT	21
#define RA_FP_INT(x)	(x ## ULL << RA_FP_SHIFT) /* the integer x */
#define RA_FP_1	RA_FP_INT(1)

/* Multiply two fixed point numbers. */
#define RA_FP_MUL(a, b) \
	(((a) * (b)) >> RA_FP_SHIFT)

/* Divide two fixed point numbers. */
#define RA_FP_DIV(a, b) \
	(b == 0 ? (uint64_t)-1 : (((a) << RA_FP_SHIFT) / (b)))

#ifdef RA_DEBUG
#define DPRINTF(x)	do { if (ra_debug > 0) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (ra_debug >= (n)) printf x; } while (0)
int ra_debug = 0;
#else
#define DPRINTF(x)	do { ; } while (0)
#define DPRINTFN(n, x)	do { ; } while (0)
#endif

#ifdef RA_DEBUG
void
ra_fixedp_split(uint32_t *i, uint32_t *f, uint64_t fp)
{
	uint64_t tmp;

	/* integer part */
	*i = (fp >> RA_FP_SHIFT);

 	/* fractional part */
	tmp = (fp & ((uint64_t)-1 >> (64 - RA_FP_SHIFT)));
	tmp *= 100;
	*f = (uint32_t)(tmp >> RA_FP_SHIFT);
}

char *
ra_fp_sprintf(uint64_t fp)
{
	uint32_t i, f;
	static char buf[64];
	int ret;

	ra_fixedp_split(&i, &f, fp);
	ret = snprintf(buf, sizeof(buf), "%u.%02u", i, f);
	if (ret == -1 || ret >= sizeof(buf))
		return "ERR";

	return buf;
}
#endif /* RA_DEBUG */

const struct ieee80211_ht_rateset *
ieee80211_ra_get_ht_rateset(int mcs, int chan40, int sgi)
{
	const struct ieee80211_ht_rateset *rs;
	int i;

	for (i = 0; i < IEEE80211_HT_NUM_RATESETS; i++) {
		rs = &ieee80211_std_ratesets_11n[i];
		if (chan40 == rs->chan40 && sgi == rs->sgi &&
		    mcs >= rs->min_mcs && mcs <= rs->max_mcs)
			return rs;
	}

	panic("MCS %d is not part of any rateset", mcs);
}

int
ieee80211_ra_use_ht_sgi(struct ieee80211_node *ni)
{
	if ((ni->ni_chan->ic_flags & IEEE80211_CHAN_40MHZ) &&
	    ieee80211_node_supports_ht_chan40(ni)) {
		if (ni->ni_flags & IEEE80211_NODE_HT_SGI40)
			return 1;
	} else if (ni->ni_flags & IEEE80211_NODE_HT_SGI20)
		return 1;
	
	return 0;
}

/*
 * Update goodput statistics.
 */

uint64_t
ieee80211_ra_get_txrate(int mcs, int chan40, int sgi)
{
	const struct ieee80211_ht_rateset *rs;
	uint64_t txrate;

	rs = ieee80211_ra_get_ht_rateset(mcs, chan40, sgi);
	txrate = rs->rates[mcs - rs->min_mcs];
	txrate <<= RA_FP_SHIFT; /* convert to fixed-point */
	txrate *= 500; /* convert to kbit/s */
	txrate /= 1000; /* convert to mbit/s */

	return txrate;
}

/*
 * Rate selection.
 */

/* A rate's goodput has to be at least this much larger to be "better". */
#define IEEE80211_RA_RATE_THRESHOLD	(RA_FP_1 / 64) /* ~ 0.015 */

int
ieee80211_ra_next_lower_intra_rate(struct ieee80211_ra_node *rn,
    struct ieee80211_node *ni)
{
	const struct ieee80211_ht_rateset *rs;
	int i, next;

	rs = ieee80211_ra_get_ht_rateset(ni->ni_txmcs,
	    ieee80211_node_supports_ht_chan40(ni), ieee80211_ra_use_ht_sgi(ni));
	if (ni->ni_txmcs == rs->min_mcs)
		return rs->min_mcs;

	next = ni->ni_txmcs;
	for (i = rs->nrates - 1; i >= 0; i--) {
		if ((rn->valid_rates & (1 << (i + rs->min_mcs))) == 0)
			continue;
		if (i + rs->min_mcs < ni->ni_txmcs) {
			next = i + rs->min_mcs;
			break;
		}
	}

	return next;
}

int
ieee80211_ra_next_intra_rate(struct ieee80211_ra_node *rn,
    struct ieee80211_node *ni)
{
	const struct ieee80211_ht_rateset *rs;
	int i, next;

	rs = ieee80211_ra_get_ht_rateset(ni->ni_txmcs,
	    ieee80211_node_supports_ht_chan40(ni), ieee80211_ra_use_ht_sgi(ni));
	if (ni->ni_txmcs == rs->max_mcs)
		return rs->max_mcs;

	next = ni->ni_txmcs;
	for (i = 0; i < rs->nrates; i++) {
		if ((rn->valid_rates & (1 << (i + rs->min_mcs))) == 0)
			continue;
		if (i + rs->min_mcs > ni->ni_txmcs) {
			next = i + rs->min_mcs;
			break;
		}
	}

	return next;
}

const struct ieee80211_ht_rateset *
ieee80211_ra_next_rateset(struct ieee80211_ra_node *rn,
    struct ieee80211_node *ni)
{
	const struct ieee80211_ht_rateset *rs, *rsnext;
	int next;
	int chan40 = ieee80211_node_supports_ht_chan40(ni);
	int sgi = ieee80211_ra_use_ht_sgi(ni);
	int mcs = ni->ni_txmcs;

	rs = ieee80211_ra_get_ht_rateset(mcs, chan40, sgi);
	if (rn->probing & IEEE80211_RA_PROBING_UP) {
		if (rs->max_mcs == 7) {	/* MCS 0-7 */
			if (chan40)
				next = sgi ? IEEE80211_HT_RATESET_MIMO2_SGI40 :
				    IEEE80211_HT_RATESET_MIMO2_40;
			else
				next = sgi ? IEEE80211_HT_RATESET_MIMO2_SGI :
				    IEEE80211_HT_RATESET_MIMO2;
		} else if (rs->max_mcs == 15) {	/* MCS 8-15 */
			if (chan40)
				next = sgi ? IEEE80211_HT_RATESET_MIMO3_SGI40 :
				    IEEE80211_HT_RATESET_MIMO3_40;
			else
				next = sgi ? IEEE80211_HT_RATESET_MIMO3_SGI :
				    IEEE80211_HT_RATESET_MIMO3;
		} else if (rs->max_mcs == 23) {	/* MCS 16-23 */
			if (chan40)
				next = sgi ? IEEE80211_HT_RATESET_MIMO4_SGI40 :
				    IEEE80211_HT_RATESET_MIMO4_40;
			else
				next = sgi ? IEEE80211_HT_RATESET_MIMO4_SGI :
				    IEEE80211_HT_RATESET_MIMO4;
		} else				/* MCS 24-31 */
			return NULL;
	} else if (rn->probing & IEEE80211_RA_PROBING_DOWN) {
		if (rs->min_mcs == 24) {	/* MCS 24-31 */
			if (chan40)
				next = sgi ? IEEE80211_HT_RATESET_MIMO3_SGI40 :
				    IEEE80211_HT_RATESET_MIMO3_40;
			else
				next = sgi ? IEEE80211_HT_RATESET_MIMO3_SGI :
				    IEEE80211_HT_RATESET_MIMO3;
		} else if (rs->min_mcs == 16) {	/* MCS 16-23 */
			if (chan40)
				next = sgi ? IEEE80211_HT_RATESET_MIMO2_SGI40 :
				    IEEE80211_HT_RATESET_MIMO2_40;
			else
				next = sgi ? IEEE80211_HT_RATESET_MIMO2_SGI :
				    IEEE80211_HT_RATESET_MIMO2;
		} else if (rs->min_mcs == 8) {	/* MCS 8-15 */
			if (chan40)
				next = sgi ? IEEE80211_HT_RATESET_SISO_SGI40 :
				    IEEE80211_HT_RATESET_SISO_40;
			else
				next = sgi ? IEEE80211_HT_RATESET_SISO_SGI :
				    IEEE80211_HT_RATESET_SISO;
		} else				/* MCS 0-7 */
			return NULL;
	} else
		panic("%s: invalid probing mode %d", __func__, rn->probing);

	rsnext = &ieee80211_std_ratesets_11n[next];
	if ((rsnext->mcs_mask & rn->valid_rates) == 0)
		return NULL;

	return rsnext;
}

int
ieee80211_ra_best_mcs_in_rateset(struct ieee80211_ra_node *rn,
    const struct ieee80211_ht_rateset *rs)
{
	uint64_t gmax = 0;
	int i, best_mcs = rs->min_mcs;

	for (i = 0; i < rs->nrates; i++) {
		int mcs = rs->min_mcs + i;
		struct ieee80211_ra_goodput_stats *g = &rn->g[mcs];
		if (((1 << mcs) & rn->valid_rates) == 0)
			continue;
		if (g->measured > gmax + IEEE80211_RA_RATE_THRESHOLD) {
			gmax = g->measured;
			best_mcs = mcs;
		}
	}

	return best_mcs;
}

void
ieee80211_ra_probe_next_rateset(struct ieee80211_ra_node *rn,
    struct ieee80211_node *ni, const struct ieee80211_ht_rateset *rsnext)
{
	const struct ieee80211_ht_rateset *rs;
	struct ieee80211_ra_goodput_stats *g;
	int best_mcs, i;

	/* Find most recently measured best MCS from the current rateset. */
	rs = ieee80211_ra_get_ht_rateset(ni->ni_txmcs,
	    ieee80211_node_supports_ht_chan40(ni), ieee80211_ra_use_ht_sgi(ni));
	best_mcs = ieee80211_ra_best_mcs_in_rateset(rn, rs);

	/* Switch to the next rateset. */
	ni->ni_txmcs = rsnext->min_mcs;
	if ((rn->valid_rates & (1 << rsnext->min_mcs)) == 0)
		ni->ni_txmcs = ieee80211_ra_next_intra_rate(rn, ni);

	/* Select the lowest rate from the next rateset with loss-free
	 * goodput close to the current best measurement. */
	g = &rn->g[best_mcs];
	for (i = 0; i < rsnext->nrates; i++) {
		int mcs = rsnext->min_mcs + i;
		uint64_t txrate = rsnext->rates[i];

		if ((rn->valid_rates & (1 << mcs)) == 0)
			continue;

		txrate = txrate * 500; /* convert to kbit/s */
		txrate <<= RA_FP_SHIFT; /* convert to fixed-point */
		txrate /= 1000; /* convert to mbit/s */

		if (txrate > g->measured + IEEE80211_RA_RATE_THRESHOLD) {
			ni->ni_txmcs = mcs;
			break;
		}
	}
	/* If all rates are lower the maximum rate is the closest match. */
	if (i == rsnext->nrates)
		ni->ni_txmcs = rsnext->max_mcs;

	/* Add rates from the next rateset as candidates. */
	rn->candidate_rates |= (1 << ni->ni_txmcs);
	if (rn->probing & IEEE80211_RA_PROBING_UP) {
		rn->candidate_rates |=
		  (1 << ieee80211_ra_next_intra_rate(rn, ni));
	} else if (rn->probing & IEEE80211_RA_PROBING_DOWN) {
		rn->candidate_rates |=
		    (1 << ieee80211_ra_next_lower_intra_rate(rn, ni));
	} else
		panic("%s: invalid probing mode %d", __func__, rn->probing);
}

int
ieee80211_ra_next_mcs(struct ieee80211_ra_node *rn,
    struct ieee80211_node *ni)
{
	int next;

	if (rn->probing & IEEE80211_RA_PROBING_DOWN)
		next = ieee80211_ra_next_lower_intra_rate(rn, ni);
	else if (rn->probing & IEEE80211_RA_PROBING_UP)
		next = ieee80211_ra_next_intra_rate(rn, ni);
	else
		panic("%s: invalid probing mode %d", __func__, rn->probing);

	return next;
}

void
ieee80211_ra_probe_clear(struct ieee80211_ra_node *rn,
    struct ieee80211_node *ni)
{
	struct ieee80211_ra_goodput_stats *g = &rn->g[ni->ni_txmcs];

	g->nprobe_pkts = 0;
	g->nprobe_fail = 0;
}

void
ieee80211_ra_probe_done(struct ieee80211_ra_node *rn)
{
	rn->probing = IEEE80211_RA_NOT_PROBING;
	rn->probed_rates = 0;
	rn->valid_probes = 0;
	rn->candidate_rates = 0;
}

int
ieee80211_ra_intra_mode_ra_finished(struct ieee80211_ra_node *rn,
    struct ieee80211_node *ni)
{
	const struct ieee80211_ht_rateset *rs;
	struct ieee80211_ra_goodput_stats *g = &rn->g[ni->ni_txmcs];
	int next_mcs, best_mcs;
	uint64_t next_rate;
	int chan40 = ieee80211_node_supports_ht_chan40(ni);
	int sgi = ieee80211_ra_use_ht_sgi(ni);

	rn->probed_rates = (rn->probed_rates | (1 << ni->ni_txmcs));

	/* Check if the min/max MCS in this rateset has been probed. */
	rs = ieee80211_ra_get_ht_rateset(ni->ni_txmcs, chan40, sgi);
	if (rn->probing & IEEE80211_RA_PROBING_DOWN) {
		if (ni->ni_txmcs == rs->min_mcs ||
		    rn->probed_rates & (1 << rs->min_mcs)) {
			ieee80211_ra_trigger_next_rateset(rn, ni);
			return 1;
		}
	} else if (rn->probing & IEEE80211_RA_PROBING_UP) {
		if (ni->ni_txmcs == rs->max_mcs ||
		    rn->probed_rates & (1 << rs->max_mcs)) {
			ieee80211_ra_trigger_next_rateset(rn, ni);
			return 1;
		}
	}

	/*
	 * Check if the measured goodput is loss-free and better than the
	 * loss-free goodput of the candidate rate.
	 */
	next_mcs = ieee80211_ra_next_mcs(rn, ni);
	if (next_mcs == ni->ni_txmcs) {
		ieee80211_ra_trigger_next_rateset(rn, ni);
		return 1;
	}
	next_rate = ieee80211_ra_get_txrate(next_mcs, chan40, sgi);
	if (g->loss == 0 &&
	    g->measured >= next_rate + IEEE80211_RA_RATE_THRESHOLD) {
		ieee80211_ra_trigger_next_rateset(rn, ni);
		return 1;
	}

	/* Check if we had a better measurement at a previously probed MCS. */
	best_mcs = ieee80211_ra_best_mcs_in_rateset(rn, rs);
	if (best_mcs != ni->ni_txmcs && (rn->probed_rates & (1 << best_mcs))) {
		if ((rn->probing & IEEE80211_RA_PROBING_UP) &&
		    best_mcs < ni->ni_txmcs) {
			ieee80211_ra_trigger_next_rateset(rn, ni);
			return 1;
		}
		if ((rn->probing & IEEE80211_RA_PROBING_DOWN) &&
		    best_mcs > ni->ni_txmcs) {
			ieee80211_ra_trigger_next_rateset(rn, ni);
			return 1;
		}
	}

	/* Check if all rates in the set of candidate rates have been probed. */
	if ((rn->candidate_rates & rn->probed_rates) == rn->candidate_rates) {
		/* Remain in the current rateset until above checks trigger. */
		rn->probing &= ~IEEE80211_RA_PROBING_INTER;
		return 1;
	}

	return 0;
}

void
ieee80211_ra_trigger_next_rateset(struct ieee80211_ra_node *rn,
    struct ieee80211_node *ni)
{
	const struct ieee80211_ht_rateset *rsnext;

	rsnext = ieee80211_ra_next_rateset(rn, ni);
	if (rsnext) {
		ieee80211_ra_probe_next_rateset(rn, ni, rsnext);
		rn->probing |= IEEE80211_RA_PROBING_INTER;
	} else
		rn->probing &= ~IEEE80211_RA_PROBING_INTER;
}

int
ieee80211_ra_inter_mode_ra_finished(struct ieee80211_ra_node *rn,
    struct ieee80211_node *ni)
{
	return ((rn->probing & IEEE80211_RA_PROBING_INTER) == 0);
}

int
ieee80211_ra_best_rate(struct ieee80211_ra_node *rn,
    struct ieee80211_node *ni)
{
	int i, best = rn->best_mcs;
	uint64_t gmax = rn->g[rn->best_mcs].measured;

	for (i = 0; i < nitems(rn->g); i++) {
		struct ieee80211_ra_goodput_stats *g = &rn->g[i];
		if (((1 << i) & rn->valid_rates) == 0)
			continue;
		if (g->measured > gmax + IEEE80211_RA_RATE_THRESHOLD) {
			gmax = g->measured;
			best = i;
		}
	}

#ifdef RA_DEBUG
	if (rn->best_mcs != best) {
		DPRINTF(("MCS %d is best; MCS{cur|avg|loss}:", best));
		for (i = 0; i < IEEE80211_HT_RATESET_NUM_MCS; i++) {
			struct ieee80211_ra_goodput_stats *g = &rn->g[i];
			if ((rn->valid_rates & (1 << i)) == 0)
				continue;
			DPRINTF((" %d{%s|", i, ra_fp_sprintf(g->measured)));
			DPRINTF(("%s|", ra_fp_sprintf(g->average)));
			DPRINTF(("%s%%}", ra_fp_sprintf(g->loss)));
		}
		DPRINTF(("\n"));
	}
#endif
	return best;
}

void
ieee80211_ra_probe_next_rate(struct ieee80211_ra_node *rn,
    struct ieee80211_node *ni)
{
	/* Select the next rate to probe. */
	rn->probed_rates |= (1 << ni->ni_txmcs);
	ni->ni_txmcs = ieee80211_ra_next_mcs(rn, ni);
}

int
ieee80211_ra_valid_tx_mcs(struct ieee80211com *ic, int mcs)
{
	uint32_t ntxstreams = 1;
	static const int max_mcs[] = { 7, 15, 23, 31 };

	if ((ic->ic_tx_mcs_set & IEEE80211_TX_RX_MCS_NOT_EQUAL) == 0)
		return isset(ic->ic_sup_mcs, mcs);

	ntxstreams += ((ic->ic_tx_mcs_set & IEEE80211_TX_SPATIAL_STREAMS) >> 2);
	if (ntxstreams < 1 || ntxstreams > 4)
		panic("invalid number of Tx streams: %u", ntxstreams);
	return (mcs <= max_mcs[ntxstreams - 1] && isset(ic->ic_sup_mcs, mcs));
}

uint32_t
ieee80211_ra_valid_rates(struct ieee80211com *ic, struct ieee80211_node *ni)
{
	uint32_t valid_mcs = 0;
	int i;

	for (i = 0; i < IEEE80211_HT_RATESET_NUM_MCS; i++) {
		if (!isset(ni->ni_rxmcs, i))
			continue;
		if (!ieee80211_ra_valid_tx_mcs(ic, i))
			continue;
		valid_mcs |= (1 << i);
	}

	return valid_mcs;
}

int
ieee80211_ra_probe_valid(struct ieee80211_ra_goodput_stats *g)
{
	/* 128 packets make up a valid probe in any case. */
	if (g->nprobe_pkts >= 128)
		return 1;

	/* 8 packets with > 75% loss make a valid probe, too. */
	if (g->nprobe_pkts >= 8 &&
	    g->nprobe_pkts - g->nprobe_fail < g->nprobe_pkts / 4)
		return 1;

	return 0;
}

void
ieee80211_ra_add_stats_ht(struct ieee80211_ra_node *rn,
    struct ieee80211com *ic, struct ieee80211_node *ni,
    int mcs, uint32_t total, uint32_t fail)
{
	static const uint64_t alpha = RA_FP_1 / 8; /* 1/8 = 0.125 */
	static const uint64_t beta =  RA_FP_1 / 4; /* 1/4 = 0.25 */
	int s;
	struct ieee80211_ra_goodput_stats *g;
	uint64_t sfer, rate, delta;

	/*
	 * Ignore invalid values. These values may come from hardware
	 * so asserting valid values via panic is not appropriate.
	 */
	if (mcs < 0 || mcs >= IEEE80211_HT_RATESET_NUM_MCS)
		return;
	if (total == 0)
		return;

	s = splnet();

	g = &rn->g[mcs];
	g->nprobe_pkts += total;
	g->nprobe_fail += fail;

	if (!ieee80211_ra_probe_valid(g)) {
		splx(s);
		return;
	}
	rn->valid_probes |= 1U << mcs;

	if (g->nprobe_fail > g->nprobe_pkts) {
		DPRINTF(("%s fail %u > pkts %u\n",
		    ether_sprintf(ni->ni_macaddr),
		    g->nprobe_fail, g->nprobe_pkts));
		g->nprobe_fail = g->nprobe_pkts;
	}

	sfer = g->nprobe_fail << RA_FP_SHIFT;
	sfer /= g->nprobe_pkts;
	g->nprobe_fail = 0;
	g->nprobe_pkts = 0;

	rate = ieee80211_ra_get_txrate(mcs,
	    ieee80211_node_supports_ht_chan40(ni),
	    ieee80211_ra_use_ht_sgi(ni));

	g->loss = sfer * 100;
	g->measured = RA_FP_MUL(RA_FP_1 - sfer, rate);
	g->average = RA_FP_MUL(RA_FP_1 - alpha, g->average);
	g->average += RA_FP_MUL(alpha, g->measured);

	g->stddeviation = RA_FP_MUL(RA_FP_1 - beta, g->stddeviation);
	if (g->average > g->measured)
		delta = g->average - g->measured;
	else
		delta = g->measured - g->average;
	g->stddeviation += RA_FP_MUL(beta, delta);

	splx(s);
}

void
ieee80211_ra_choose(struct ieee80211_ra_node *rn, struct ieee80211com *ic,
    struct ieee80211_node *ni)
{
	struct ieee80211_ra_goodput_stats *g = &rn->g[ni->ni_txmcs];
	int s;
	int chan40 = ieee80211_node_supports_ht_chan40(ni);
	int sgi = ieee80211_ra_use_ht_sgi(ni);
	const struct ieee80211_ht_rateset *rs, *rsnext;

	s = splnet();

	if (rn->valid_rates == 0)
		rn->valid_rates = ieee80211_ra_valid_rates(ic, ni);

	if (rn->probing) {
		/* Probe another rate or settle at the best rate. */
		if (!(rn->valid_probes & (1UL << ni->ni_txmcs))) {
			splx(s);
			return;
		}
		ieee80211_ra_probe_clear(rn, ni);
		if (!ieee80211_ra_intra_mode_ra_finished(rn, ni)) {
			ieee80211_ra_probe_next_rate(rn, ni);
			DPRINTFN(3, ("probing MCS %d\n", ni->ni_txmcs));
		} else if (ieee80211_ra_inter_mode_ra_finished(rn, ni)) {
			rn->best_mcs = ieee80211_ra_best_rate(rn, ni);
			ni->ni_txmcs = rn->best_mcs;
			ieee80211_ra_probe_done(rn);
		}

		splx(s);
		return;
	} else {
		rn->valid_probes = 0;
	}

	rs = ieee80211_ra_get_ht_rateset(ni->ni_txmcs, chan40, sgi);
	if ((g->measured >> RA_FP_SHIFT) == 0LL ||
	    (g->average >= 3 * g->stddeviation &&
	    g->measured < g->average - 3 * g->stddeviation)) {
		/* Channel becomes bad. Probe downwards. */
		rn->probing = IEEE80211_RA_PROBING_DOWN;
		rn->probed_rates = 0;
		if (ni->ni_txmcs == rs->min_mcs) {
			rsnext = ieee80211_ra_next_rateset(rn, ni);
			if (rsnext) {
				ieee80211_ra_probe_next_rateset(rn, ni,
				    rsnext);
			} else {
				/* Cannot probe further down. */
				rn->probing = IEEE80211_RA_NOT_PROBING;
			}
		} else {
			ni->ni_txmcs = ieee80211_ra_next_mcs(rn, ni);
			rn->candidate_rates = (1 << ni->ni_txmcs);
		}
	} else if (g->loss < 2 * RA_FP_1 ||
	    g->measured > g->average + 3 * g->stddeviation) {
		/* Channel becomes good. */
		rn->probing = IEEE80211_RA_PROBING_UP;
		rn->probed_rates = 0;
		if (ni->ni_txmcs == rs->max_mcs) {
			rsnext = ieee80211_ra_next_rateset(rn, ni);
			if (rsnext) {
				ieee80211_ra_probe_next_rateset(rn, ni,
				    rsnext);
			} else {
				/* Cannot probe further up. */
				rn->probing = IEEE80211_RA_NOT_PROBING;
			}
		} else {
			ni->ni_txmcs = ieee80211_ra_next_mcs(rn, ni);
			rn->candidate_rates = (1 << ni->ni_txmcs);
		}
	} else {
		/* Remain at current rate. */
		rn->probing = IEEE80211_RA_NOT_PROBING;
		rn->probed_rates = 0;
		rn->candidate_rates = 0;
	}

	splx(s);

	if (rn->probing) {
		if (rn->probing & IEEE80211_RA_PROBING_UP)
			DPRINTFN(2, ("channel becomes good; probe up\n"));
		else
			DPRINTFN(2, ("channel becomes bad; probe down\n"));

		DPRINTFN(3, ("measured: %s Mbit/s\n",
		    ra_fp_sprintf(g->measured)));
		DPRINTFN(3, ("average: %s Mbit/s\n",
		    ra_fp_sprintf(g->average)));
		DPRINTFN(3, ("stddeviation: %s\n",
		    ra_fp_sprintf(g->stddeviation)));
		DPRINTFN(3, ("loss: %s%%\n", ra_fp_sprintf(g->loss)));
	}
}

void
ieee80211_ra_node_init(struct ieee80211_ra_node *rn)
{
	memset(rn, 0, sizeof(*rn));
}
