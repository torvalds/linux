/*	$OpenBSD: ieee80211_ra_vht.c,v 1.3 2022/03/23 09:21:47 stsp Exp $	*/

/*
 * Copyright (c) 2021 Christian Ehrhardt <ehrhardt@genua.de>
 * Copyright (c) 2016, 2021, 2022 Stefan Sperling <stsp@openbsd.org>
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
#include <net80211/ieee80211_ra_vht.h>

int	ieee80211_ra_vht_next_intra_rate(struct ieee80211_ra_vht_node *,
	    struct ieee80211_node *);
const struct ieee80211_vht_rateset * ieee80211_ra_vht_next_rateset(
		    struct ieee80211_ra_vht_node *, struct ieee80211_node *);
int	ieee80211_ra_vht_best_mcs_in_rateset(struct ieee80211_ra_vht_node *,
	    const struct ieee80211_vht_rateset *);
void	ieee80211_ra_vht_probe_next_rateset(struct ieee80211_ra_vht_node *,
	    struct ieee80211_node *, const struct ieee80211_vht_rateset *);
int	ieee80211_ra_vht_next_mcs(struct ieee80211_ra_vht_node *,
	    struct ieee80211_node *);
void	ieee80211_ra_vht_probe_done(struct ieee80211_ra_vht_node *, int);
int	ieee80211_ra_vht_intra_mode_ra_finished(
	    struct ieee80211_ra_vht_node *, struct ieee80211_node *);
void	ieee80211_ra_vht_trigger_next_rateset(struct ieee80211_ra_vht_node *,
	    struct ieee80211_node *);
int	ieee80211_ra_vht_inter_mode_ra_finished(
	    struct ieee80211_ra_vht_node *, struct ieee80211_node *);
void	ieee80211_ra_vht_best_rate(struct ieee80211_ra_vht_node *,
	    struct ieee80211_node *);
void	ieee80211_ra_vht_probe_next_rate(struct ieee80211_ra_vht_node *,
	    struct ieee80211_node *);
void	ieee80211_ra_vht_init_valid_rates(struct ieee80211com *,
	    struct ieee80211_node *, struct ieee80211_ra_vht_node *);
int	ieee80211_ra_vht_probe_valid(struct ieee80211_ra_vht_goodput_stats *);

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
#define DPRINTF(x)	do { if (ra_vht_debug > 0) printf x; } while (0)
#define DPRINTFN(n, x)	do { if (ra_vht_debug >= (n)) printf x; } while (0)
int ra_vht_debug = 0;
#else
#define DPRINTF(x)	do { ; } while (0)
#define DPRINTFN(n, x)	do { ; } while (0)
#endif

#ifdef RA_DEBUG
void
ra_vht_fixedp_split(uint32_t *i, uint32_t *f, uint64_t fp)
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
ra_vht_fp_sprintf(uint64_t fp)
{
	uint32_t i, f;
	static char buf[64];
	int ret;

	ra_vht_fixedp_split(&i, &f, fp);
	ret = snprintf(buf, sizeof(buf), "%u.%02u", i, f);
	if (ret == -1 || ret >= sizeof(buf))
		return "ERR";

	return buf;
}
#endif /* RA_DEBUG */

const struct ieee80211_vht_rateset *
ieee80211_ra_vht_get_rateset(int mcs, int nss, int chan40, int chan80, int sgi)
{
	const struct ieee80211_vht_rateset *rs;
	int i;

	for (i = 0; i < IEEE80211_VHT_NUM_RATESETS; i++) {
		rs = &ieee80211_std_ratesets_11ac[i];
		if (mcs < rs->nrates && rs->num_ss == nss &&
		    chan40 == rs->chan40 && chan80 == rs->chan80 &&
		    sgi == rs->sgi)
			return rs;
	}

	panic("MCS %d NSS %d is not part of any rateset", mcs, nss);
}

int
ieee80211_ra_vht_use_sgi(struct ieee80211_node *ni)
{
	if ((ni->ni_chan->ic_xflags & IEEE80211_CHANX_160MHZ) &&
	    ieee80211_node_supports_vht_chan160(ni)) {
		if (ni->ni_flags & IEEE80211_NODE_VHT_SGI160)
			return 1;
	}

	if ((ni->ni_chan->ic_xflags & IEEE80211_CHANX_80MHZ) &&
	    ieee80211_node_supports_vht_chan80(ni)) {
		if (ni->ni_flags & IEEE80211_NODE_VHT_SGI80)
			return 1;
	}
	
	return 0;
}

/*
 * Update goodput statistics.
 */

uint64_t
ieee80211_ra_vht_get_txrate(int mcs, int nss, int chan40, int chan80, int sgi)
{
	const struct ieee80211_vht_rateset *rs;
	uint64_t txrate;

	rs = ieee80211_ra_vht_get_rateset(mcs, nss, chan40, chan80, sgi);
	txrate = rs->rates[mcs];
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
ieee80211_ra_vht_next_lower_intra_rate(struct ieee80211_ra_vht_node *rn,
    struct ieee80211_node *ni)
{
	if (ni->ni_txmcs <= 0)
		return 0;

	return ni->ni_txmcs - 1;
}

int
ieee80211_ra_vht_get_max_mcs(int vht_mcs, int nss, int chan40)
{
	int supp_mcs = (vht_mcs & IEEE80211_VHT_MCS_FOR_SS_MASK(nss)) >>
	    IEEE80211_VHT_MCS_FOR_SS_SHIFT(nss);
	int max_mcs = -1;

	switch (supp_mcs) {
	case IEEE80211_VHT_MCS_SS_NOT_SUPP:
		break;
	case IEEE80211_VHT_MCS_0_7:
		max_mcs = 7;
		break;
	case IEEE80211_VHT_MCS_0_8:
		max_mcs = 8;
		break;
	case IEEE80211_VHT_MCS_0_9:
		/* Disable VHT MCS 9 for 20MHz-only stations. */
		if (!chan40)
			max_mcs = 8;
		else
			max_mcs = 9;
		break;
	default:
		/* Should not happen; Values above cover the possible range. */
		panic("invalid VHT Rx MCS value %u", supp_mcs);
	}

	return max_mcs;
}

int
ieee80211_ra_vht_next_intra_rate(struct ieee80211_ra_vht_node *rn,
    struct ieee80211_node *ni)
{
	int max_mcs;

	max_mcs = ieee80211_ra_vht_get_max_mcs(ni->ni_vht_rxmcs,
	    ni->ni_vht_ss, ieee80211_node_supports_ht_chan40(ni));
	if (max_mcs != 7 && max_mcs != 8 && max_mcs != 9)
		panic("ni->ni_vht_ss invalid: %u", ni->ni_vht_ss);

	if (ni->ni_txmcs >= max_mcs)
		return max_mcs;

	return ni->ni_txmcs + 1;
}

const struct ieee80211_vht_rateset *
ieee80211_ra_vht_next_rateset(struct ieee80211_ra_vht_node *rn,
    struct ieee80211_node *ni)
{
	const struct ieee80211_vht_rateset *rs, *rsnext;
	int next;
	int sgi = ieee80211_ra_vht_use_sgi(ni);
	int mcs = ni->ni_txmcs;
	int nss = ni->ni_vht_ss;

	/*
	 * We only probe 80MHz ratesets.
	 * Drivers handle retries on slower rates if needed.
	 */
	rs = ieee80211_ra_vht_get_rateset(mcs, nss, 0, 1, sgi);
	if (rn->probing & IEEE80211_RA_PROBING_UP) {
		switch (rs->idx) {
		case IEEE80211_VHT_RATESET_SISO_80:
			next = IEEE80211_VHT_RATESET_MIMO2_80;
			break;
		case IEEE80211_VHT_RATESET_SISO_80_SGI:
			next = IEEE80211_VHT_RATESET_MIMO2_80_SGI;
			break;
		default:
			return NULL;
		}
	} else if (rn->probing & IEEE80211_RA_PROBING_DOWN) {
		switch (rs->idx) {
		case IEEE80211_VHT_RATESET_MIMO2_80:
			next = IEEE80211_VHT_RATESET_SISO_80;
			break;
		case IEEE80211_VHT_RATESET_MIMO2_80_SGI:
			next = IEEE80211_VHT_RATESET_SISO_80_SGI;
			break;
		default:
			return NULL;
		}
	} else
		panic("%s: invalid probing mode %d", __func__, rn->probing);

	rsnext = &ieee80211_std_ratesets_11ac[next];
	if (rn->valid_rates[rsnext->num_ss - 1] == 0)
		return NULL;

	return rsnext;
}

int
ieee80211_ra_vht_best_mcs_in_rateset(struct ieee80211_ra_vht_node *rn,
    const struct ieee80211_vht_rateset *rs)
{
	uint64_t gmax = 0;
	int mcs, best_mcs = 0;

	for (mcs = 0; mcs < rs->nrates; mcs++) {
		struct ieee80211_ra_vht_goodput_stats *g = &rn->g[rs->idx][mcs];
		if (((1 << mcs) & rn->valid_rates[rs->num_ss - 1]) == 0)
			continue;
		if (g->measured > gmax + IEEE80211_RA_RATE_THRESHOLD) {
			gmax = g->measured;
			best_mcs = mcs;
		}
	}

	return best_mcs;
}

void
ieee80211_ra_vht_probe_next_rateset(struct ieee80211_ra_vht_node *rn,
    struct ieee80211_node *ni, const struct ieee80211_vht_rateset *rsnext)
{
	const struct ieee80211_vht_rateset *rs;
	struct ieee80211_ra_vht_goodput_stats *g;
	int best_mcs, mcs;

	/* Find most recently measured best MCS from the current rateset. */
	rs = ieee80211_ra_vht_get_rateset(ni->ni_txmcs, ni->ni_vht_ss, 0, 1,
	    ieee80211_ra_vht_use_sgi(ni));
	best_mcs = ieee80211_ra_vht_best_mcs_in_rateset(rn, rs);

	/* Switch to the next rateset. */
	ni->ni_txmcs = 0;
	ni->ni_vht_ss = rsnext->num_ss;

	/* Select the lowest rate from the next rateset with loss-free
	 * goodput close to the current best measurement. */
	g = &rn->g[rs->idx][best_mcs];
	for (mcs = 0; mcs < rsnext->nrates; mcs++) {
		uint64_t txrate = rsnext->rates[mcs];

		if ((rn->valid_rates[rsnext->num_ss - 1] & (1 << mcs)) == 0)
			continue;

		txrate = txrate * 500; /* convert to kbit/s */
		txrate <<= RA_FP_SHIFT; /* convert to fixed-point */
		txrate /= 1000; /* convert to mbit/s */

		if (txrate > g->measured + IEEE80211_RA_RATE_THRESHOLD) {
			ni->ni_txmcs = mcs;
			break;
		}
	}
	/* If all rates are lower then the best rate is the closest match. */
	if (mcs == rsnext->nrates)
		ni->ni_txmcs = ieee80211_ra_vht_best_mcs_in_rateset(rn, rsnext);

	/* Add rates from the next rateset as candidates. */
	rn->candidate_rates[rsnext->num_ss - 1] |= (1 << ni->ni_txmcs);
	if (rn->probing & IEEE80211_RA_PROBING_UP) {
		rn->candidate_rates[rsnext->num_ss - 1] |=
		  (1 << ieee80211_ra_vht_next_intra_rate(rn, ni));
	} else if (rn->probing & IEEE80211_RA_PROBING_DOWN) {
		rn->candidate_rates[rsnext->num_ss - 1] |=
		    (1 << ieee80211_ra_vht_next_lower_intra_rate(rn, ni));
	} else
		panic("%s: invalid probing mode %d", __func__, rn->probing);
}

int
ieee80211_ra_vht_next_mcs(struct ieee80211_ra_vht_node *rn,
    struct ieee80211_node *ni)
{
	int next;

	if (rn->probing & IEEE80211_RA_PROBING_DOWN)
		next = ieee80211_ra_vht_next_lower_intra_rate(rn, ni);
	else if (rn->probing & IEEE80211_RA_PROBING_UP)
		next = ieee80211_ra_vht_next_intra_rate(rn, ni);
	else
		panic("%s: invalid probing mode %d", __func__, rn->probing);

	return next;
}

void
ieee80211_ra_vht_probe_clear(struct ieee80211_ra_vht_goodput_stats *g)
{
	g->nprobe_pkts = 0;
	g->nprobe_fail = 0;
}

void
ieee80211_ra_vht_probe_done(struct ieee80211_ra_vht_node *rn, int nss)
{
	rn->probing = IEEE80211_RA_NOT_PROBING;
	rn->probed_rates[nss - 1] = 0;
	rn->valid_probes[nss - 1] = 0;
	rn->candidate_rates[nss - 1] = 0;
}

int
ieee80211_ra_vht_intra_mode_ra_finished(struct ieee80211_ra_vht_node *rn,
    struct ieee80211_node *ni)
{
	const struct ieee80211_vht_rateset *rs;
	struct ieee80211_ra_vht_goodput_stats *g;
	int next_mcs, best_mcs;
	uint64_t next_rate;
	int nss = ni->ni_vht_ss;
	int sgi = ieee80211_ra_vht_use_sgi(ni);

	rn->probed_rates[nss - 1] = (rn->probed_rates[nss - 1] |
	    (1 << ni->ni_txmcs));

	/* Check if the min/max MCS in this rateset has been probed. */
	rs = ieee80211_ra_vht_get_rateset(ni->ni_txmcs, nss, 0, 1, sgi);
	if (rn->probing & IEEE80211_RA_PROBING_DOWN) {
		if (ni->ni_txmcs == 0 ||
		    rn->probed_rates[nss - 1] & (1 << 0)) {
			ieee80211_ra_vht_trigger_next_rateset(rn, ni);
			return 1;
		}
	} else if (rn->probing & IEEE80211_RA_PROBING_UP) {
		if (ni->ni_txmcs == rn->max_mcs[nss - 1] ||
		    rn->probed_rates[nss - 1] & (1 << rn->max_mcs[nss - 1])) {
			ieee80211_ra_vht_trigger_next_rateset(rn, ni);
			return 1;
		}
	}

	/*
	 * Check if the measured goodput is loss-free and better than the
	 * loss-free goodput of the candidate rate.
	 */
	next_mcs = ieee80211_ra_vht_next_mcs(rn, ni);
	if (next_mcs == ni->ni_txmcs) {
		ieee80211_ra_vht_trigger_next_rateset(rn, ni);
		return 1;
	}
	next_rate = ieee80211_ra_vht_get_txrate(next_mcs, nss, 0, 1, sgi);
	g = &rn->g[rs->idx][ni->ni_txmcs];
	if (g->loss == 0 &&
	    g->measured >= next_rate + IEEE80211_RA_RATE_THRESHOLD) {
		ieee80211_ra_vht_trigger_next_rateset(rn, ni);
		return 1;
	}

	/* Check if we had a better measurement at a previously probed MCS. */
	best_mcs = ieee80211_ra_vht_best_mcs_in_rateset(rn, rs);
	if (best_mcs != ni->ni_txmcs) {
		if ((rn->probing & IEEE80211_RA_PROBING_UP) &&
		    best_mcs < ni->ni_txmcs) {
			ieee80211_ra_vht_trigger_next_rateset(rn, ni);
			return 1;
		}
		if ((rn->probing & IEEE80211_RA_PROBING_DOWN) &&
		    best_mcs > ni->ni_txmcs) {
			ieee80211_ra_vht_trigger_next_rateset(rn, ni);
			return 1;
		}
	}

	/* Check if all rates in the set of candidate rates have been probed. */
	if ((rn->candidate_rates[nss - 1] & rn->probed_rates[nss - 1]) ==
	    rn->candidate_rates[nss - 1]) {
		/* Remain in the current rateset until above checks trigger. */
		rn->probing &= ~IEEE80211_RA_PROBING_INTER;
		return 1;
	}

	return 0;
}

void
ieee80211_ra_vht_trigger_next_rateset(struct ieee80211_ra_vht_node *rn,
    struct ieee80211_node *ni)
{
	const struct ieee80211_vht_rateset *rsnext;

	rsnext = ieee80211_ra_vht_next_rateset(rn, ni);
	if (rsnext) {
		ieee80211_ra_vht_probe_next_rateset(rn, ni, rsnext);
		rn->probing |= IEEE80211_RA_PROBING_INTER;
	} else
		rn->probing &= ~IEEE80211_RA_PROBING_INTER;
}

int
ieee80211_ra_vht_inter_mode_ra_finished(struct ieee80211_ra_vht_node *rn,
    struct ieee80211_node *ni)
{
	return ((rn->probing & IEEE80211_RA_PROBING_INTER) == 0);
}

void
ieee80211_ra_vht_best_rate(struct ieee80211_ra_vht_node *rn,
    struct ieee80211_node *ni)
{
	const struct ieee80211_vht_rateset *rs;
	int i, j, best_mcs = rn->best_mcs, best_nss = rn->best_nss;
	uint64_t gmax;

	rs = ieee80211_ra_vht_get_rateset(best_mcs, best_nss, 0, 1,
	    ieee80211_ra_vht_use_sgi(ni));
	gmax = rn->g[rs->idx][best_mcs].measured;

	for (i = 0; i < IEEE80211_VHT_NUM_RATESETS; i++) {
		rs = &ieee80211_std_ratesets_11ac[i];
		for (j = 0; j < IEEE80211_VHT_RATESET_MAX_NRATES; j++) {
			struct ieee80211_ra_vht_goodput_stats *g = &rn->g[i][j];
			if (((1 << i) & rn->valid_rates[rs->num_ss - 1]) == 0)
				continue;
			if (g->measured > gmax + IEEE80211_RA_RATE_THRESHOLD) {
				gmax = g->measured;
				best_mcs = j;
				best_nss = rs->num_ss;
			}
		}
	}

#ifdef RA_DEBUG
	if (rn->best_mcs != best_mcs || rn->best_nss != best_nss) {
		DPRINTF(("MCS,NSS %d,%d is best; MCS,NSS{cur|avg|loss}:",
		    best_mcs, best_nss));
		for (i = 0; i < IEEE80211_VHT_NUM_RATESETS; i++) {
			rs = &ieee80211_std_ratesets_11ac[i];
			if (rs->chan80 == 0 ||
			    rs->sgi != ieee80211_ra_vht_use_sgi(ni))
				continue;
			for (j = 0; j < IEEE80211_VHT_RATESET_MAX_NRATES; j++) {
				struct ieee80211_ra_vht_goodput_stats *g;
				g = &rn->g[i][j];
				if ((rn->valid_rates[rs->num_ss - 1] &
				    (1 << j)) == 0)
					continue;
				DPRINTF((" %d,%d{%s|", j, rs->num_ss,
				    ra_vht_fp_sprintf(g->measured)));
				DPRINTF(("%s|", ra_vht_fp_sprintf(g->average)));
				DPRINTF(("%s%%}", ra_vht_fp_sprintf(g->loss)));
			}
		}
		DPRINTF(("\n"));
	}
#endif
	rn->best_mcs = best_mcs;
	rn->best_nss = best_nss;
}

void
ieee80211_ra_vht_probe_next_rate(struct ieee80211_ra_vht_node *rn,
    struct ieee80211_node *ni)
{
	/* Select the next rate to probe. */
	rn->probed_rates[ni->ni_vht_ss - 1] |= (1 << ni->ni_txmcs);
	ni->ni_txmcs = ieee80211_ra_vht_next_mcs(rn, ni);
}

void
ieee80211_ra_vht_init_valid_rates(struct ieee80211com *ic,
    struct ieee80211_node *ni, struct ieee80211_ra_vht_node *rn)
{
	int nss, ic_max_mcs, ni_max_mcs, max_mcs;

	memset(rn->max_mcs, 0, sizeof(rn->max_mcs));
	memset(rn->valid_rates, 0, sizeof(rn->valid_rates));

	for (nss = 1; nss <= IEEE80211_VHT_NUM_SS; nss++) {
		ic_max_mcs = ieee80211_ra_vht_get_max_mcs(ic->ic_vht_txmcs,
		    nss, IEEE80211_CHAN_40MHZ_ALLOWED(ic->ic_bss->ni_chan));
		ni_max_mcs = ieee80211_ra_vht_get_max_mcs(ni->ni_vht_rxmcs,
		    nss, ieee80211_node_supports_ht_chan40(ni));
		if ((ic_max_mcs != 7 && ic_max_mcs != 8 && ic_max_mcs != 9) ||
		    (ni_max_mcs != 7 && ni_max_mcs != 8 && ni_max_mcs != 9))
			continue;

		max_mcs = MIN(ic_max_mcs, ni_max_mcs);
		rn->max_mcs[nss - 1] = max_mcs;
		rn->valid_rates[nss - 1] = ((1 << (max_mcs + 1)) - 1);
	}
}

int
ieee80211_ra_vht_probe_valid(struct ieee80211_ra_vht_goodput_stats *g)
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
ieee80211_ra_vht_add_stats(struct ieee80211_ra_vht_node *rn,
    struct ieee80211com *ic, struct ieee80211_node *ni,
    int mcs, int nss, uint32_t total, uint32_t fail)
{
	static const uint64_t alpha = RA_FP_1 / 8; /* 1/8 = 0.125 */
	static const uint64_t beta =  RA_FP_1 / 4; /* 1/4 = 0.25 */
	int s;
	const struct ieee80211_vht_rateset *rs;
	struct ieee80211_ra_vht_goodput_stats *g;
	uint64_t sfer, rate, delta;

	/*
	 * Ignore invalid values. These values may come from hardware
	 * so asserting valid values via panic is not appropriate.
	 */
	if (mcs < 0 || mcs >= IEEE80211_VHT_RATESET_MAX_NRATES)
		return;
	if (nss <= 0 || nss > IEEE80211_VHT_NUM_SS)
		return;
	if (total == 0)
		return;

	s = splnet();

	rs = ieee80211_ra_vht_get_rateset(mcs, nss, 0, 1,
	    ieee80211_ra_vht_use_sgi(ni));
	g = &rn->g[rs->idx][mcs];
	g->nprobe_pkts += total;
	g->nprobe_fail += fail;

	if (!ieee80211_ra_vht_probe_valid(g)) {
		splx(s);
		return;
	}
	rn->valid_probes[nss - 1] |= 1U << mcs;

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

	rate = ieee80211_ra_vht_get_txrate(mcs, nss, 0, 1,
	    ieee80211_ra_vht_use_sgi(ni));

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
ieee80211_ra_vht_choose(struct ieee80211_ra_vht_node *rn,
    struct ieee80211com *ic, struct ieee80211_node *ni)
{
	struct ieee80211_ra_vht_goodput_stats *g;
	int s;
	int sgi = ieee80211_ra_vht_use_sgi(ni);
	const struct ieee80211_vht_rateset *rs, *rsnext;
	int nss = ni->ni_vht_ss;

	s = splnet();

	if (rn->valid_rates[0] == 0) {
		ieee80211_ra_vht_init_valid_rates(ic, ni, rn);
		if (rn->valid_rates[0] == 0)
			panic("VHT not supported");
	}

	rs = ieee80211_ra_vht_get_rateset(ni->ni_txmcs, nss, 0, 1, sgi);
	g = &rn->g[rs->idx][ni->ni_txmcs];

	if (rn->probing) {
		/* Probe another rate or settle at the best rate. */
		if (!(rn->valid_probes[nss - 1] & (1UL << ni->ni_txmcs))) {
			splx(s);
			return;
		}
		ieee80211_ra_vht_probe_clear(g);
		if (!ieee80211_ra_vht_intra_mode_ra_finished(rn, ni)) {
			ieee80211_ra_vht_probe_next_rate(rn, ni);
			DPRINTFN(3, ("probing MCS,NSS %d,%d\n",
			    ni->ni_txmcs, ni->ni_vht_ss));
		} else if (ieee80211_ra_vht_inter_mode_ra_finished(rn, ni)) {
			ieee80211_ra_vht_best_rate(rn, ni);
			ni->ni_txmcs = rn->best_mcs;
			ni->ni_vht_ss = rn->best_nss;
			ieee80211_ra_vht_probe_done(rn, nss);
		}

		splx(s);
		return;
	} else {
		rn->valid_probes[nss - 1] = 0;
	}


	rs = ieee80211_ra_vht_get_rateset(ni->ni_txmcs, nss, 0, 1, sgi);
	if ((g->measured >> RA_FP_SHIFT) == 0LL ||
	    (g->average >= 3 * g->stddeviation &&
	    g->measured < g->average - 3 * g->stddeviation)) {
		/* Channel becomes bad. Probe downwards. */
		rn->probing = IEEE80211_RA_PROBING_DOWN;
		rn->probed_rates[nss - 1] = 0;
		if (ni->ni_txmcs == 0) {
			rsnext = ieee80211_ra_vht_next_rateset(rn, ni);
			if (rsnext) {
				ieee80211_ra_vht_probe_next_rateset(rn, ni,
				    rsnext);
			} else {
				/* Cannot probe further down. */
				rn->probing = IEEE80211_RA_NOT_PROBING;
			}
		} else {
			ni->ni_txmcs = ieee80211_ra_vht_next_mcs(rn, ni);
			rn->candidate_rates[nss - 1] = (1 << ni->ni_txmcs);
		}
	} else if (g->loss < 2 * RA_FP_1 ||
	    g->measured > g->average + 3 * g->stddeviation) {
		/* Channel becomes good. */
		rn->probing = IEEE80211_RA_PROBING_UP;
		rn->probed_rates[nss - 1] = 0;
		if (ni->ni_txmcs == rn->max_mcs[nss - 1]) {
			rsnext = ieee80211_ra_vht_next_rateset(rn, ni);
			if (rsnext) {
				ieee80211_ra_vht_probe_next_rateset(rn, ni,
				    rsnext);
			} else {
				/* Cannot probe further up. */
				rn->probing = IEEE80211_RA_NOT_PROBING;
			}
		} else {
			ni->ni_txmcs = ieee80211_ra_vht_next_mcs(rn, ni);
			rn->candidate_rates[nss - 1] = (1 << ni->ni_txmcs);
		}
	} else {
		/* Remain at current rate. */
		rn->probing = IEEE80211_RA_NOT_PROBING;
		rn->probed_rates[nss - 1] = 0;
		rn->candidate_rates[nss - 1] = 0;
	}

	splx(s);

	if (rn->probing) {
		if (rn->probing & IEEE80211_RA_PROBING_UP)
			DPRINTFN(2, ("channel becomes good; probe up\n"));
		else
			DPRINTFN(2, ("channel becomes bad; probe down\n"));

		DPRINTFN(3, ("measured: %s Mbit/s\n",
		    ra_vht_fp_sprintf(g->measured)));
		DPRINTFN(3, ("average: %s Mbit/s\n",
		    ra_vht_fp_sprintf(g->average)));
		DPRINTFN(3, ("stddeviation: %s\n",
		    ra_vht_fp_sprintf(g->stddeviation)));
		DPRINTFN(3, ("loss: %s%%\n", ra_vht_fp_sprintf(g->loss)));
	}
}

void
ieee80211_ra_vht_node_init(struct ieee80211_ra_vht_node *rn)
{
	memset(rn, 0, sizeof(*rn));
	rn->best_nss = 1;
}
