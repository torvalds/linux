/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2005 John Bicket
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
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * John Bicket's SampleRate control algorithm.
 */
#include "opt_ath.h"
#include "opt_inet.h"
#include "opt_wlan.h"
#include "opt_ah.h"

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/sysctl.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/errno.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>

#include <sys/socket.h>
 
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/ethernet.h>		/* XXX for ether_sprintf */

#include <net80211/ieee80211_var.h>

#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h> 
#include <netinet/if_ether.h>
#endif

#include <dev/ath/if_athvar.h>
#include <dev/ath/ath_rate/sample/sample.h>
#include <dev/ath/ath_hal/ah_desc.h>
#include <dev/ath/ath_rate/sample/tx_schedules.h>

/*
 * This file is an implementation of the SampleRate algorithm
 * in "Bit-rate Selection in Wireless Networks"
 * (http://www.pdos.lcs.mit.edu/papers/jbicket-ms.ps)
 *
 * SampleRate chooses the bit-rate it predicts will provide the most
 * throughput based on estimates of the expected per-packet
 * transmission time for each bit-rate.  SampleRate periodically sends
 * packets at bit-rates other than the current one to estimate when
 * another bit-rate will provide better performance. SampleRate
 * switches to another bit-rate when its estimated per-packet
 * transmission time becomes smaller than the current bit-rate's.
 * SampleRate reduces the number of bit-rates it must sample by
 * eliminating those that could not perform better than the one
 * currently being used.  SampleRate also stops probing at a bit-rate
 * if it experiences several successive losses.
 *
 * The difference between the algorithm in the thesis and the one in this
 * file is that the one in this file uses a ewma instead of a window.
 *
 * Also, this implementation tracks the average transmission time for
 * a few different packet sizes independently for each link.
 */

static void	ath_rate_ctl_reset(struct ath_softc *, struct ieee80211_node *);

static __inline int
size_to_bin(int size) 
{
#if NUM_PACKET_SIZE_BINS > 1
	if (size <= packet_size_bins[0])
		return 0;
#endif
#if NUM_PACKET_SIZE_BINS > 2
	if (size <= packet_size_bins[1])
		return 1;
#endif
#if NUM_PACKET_SIZE_BINS > 3
	if (size <= packet_size_bins[2])
		return 2;
#endif
#if NUM_PACKET_SIZE_BINS > 4
#error "add support for more packet sizes"
#endif
	return NUM_PACKET_SIZE_BINS-1;
}

void
ath_rate_node_init(struct ath_softc *sc, struct ath_node *an)
{
	/* NB: assumed to be zero'd by caller */
}

void
ath_rate_node_cleanup(struct ath_softc *sc, struct ath_node *an)
{
}

static int
dot11rate(const HAL_RATE_TABLE *rt, int rix)
{
	if (rix < 0)
		return -1;
	return rt->info[rix].phy == IEEE80211_T_HT ?
	    rt->info[rix].dot11Rate : (rt->info[rix].dot11Rate & IEEE80211_RATE_VAL) / 2;
}

static const char *
dot11rate_label(const HAL_RATE_TABLE *rt, int rix)
{
	if (rix < 0)
		return "";
	return rt->info[rix].phy == IEEE80211_T_HT ? "MCS" : "Mb ";
}

/*
 * Return the rix with the lowest average_tx_time,
 * or -1 if all the average_tx_times are 0.
 */
static __inline int
pick_best_rate(struct ath_node *an, const HAL_RATE_TABLE *rt,
    int size_bin, int require_acked_before)
{
	struct sample_node *sn = ATH_NODE_SAMPLE(an);
        int best_rate_rix, best_rate_tt, best_rate_pct;
	uint64_t mask;
	int rix, tt, pct;

        best_rate_rix = 0;
        best_rate_tt = 0;
	best_rate_pct = 0;
	for (mask = sn->ratemask, rix = 0; mask != 0; mask >>= 1, rix++) {
		if ((mask & 1) == 0)		/* not a supported rate */
			continue;

		/* Don't pick a non-HT rate for a HT node */
		if ((an->an_node.ni_flags & IEEE80211_NODE_HT) &&
		    (rt->info[rix].phy != IEEE80211_T_HT)) {
			continue;
		}

		tt = sn->stats[size_bin][rix].average_tx_time;
		if (tt <= 0 ||
		    (require_acked_before &&
		     !sn->stats[size_bin][rix].packets_acked))
			continue;

		/* Calculate percentage if possible */
		if (sn->stats[size_bin][rix].total_packets > 0) {
			pct = sn->stats[size_bin][rix].ewma_pct;
		} else {
			/* XXX for now, assume 95% ok */
			pct = 95;
		}

		/* don't use a bit-rate that has been failing */
		if (sn->stats[size_bin][rix].successive_failures > 3)
			continue;

		/*
		 * For HT, Don't use a bit rate that is much more
		 * lossy than the best.
		 *
		 * XXX this isn't optimal; it's just designed to
		 * eliminate rates that are going to be obviously
		 * worse.
		 */
		if (an->an_node.ni_flags & IEEE80211_NODE_HT) {
			if (best_rate_pct > (pct + 50))
				continue;
		}

		/*
		 * For non-MCS rates, use the current average txtime for
		 * comparison.
		 */
		if (! (an->an_node.ni_flags & IEEE80211_NODE_HT)) {
			if (best_rate_tt == 0 || tt <= best_rate_tt) {
				best_rate_tt = tt;
				best_rate_rix = rix;
				best_rate_pct = pct;
			}
		}

		/*
		 * Since 2 stream rates have slightly higher TX times,
		 * allow a little bit of leeway. This should later
		 * be abstracted out and properly handled.
		 */
		if (an->an_node.ni_flags & IEEE80211_NODE_HT) {
			if (best_rate_tt == 0 || (tt * 8 <= best_rate_tt * 10)) {
				best_rate_tt = tt;
				best_rate_rix = rix;
				best_rate_pct = pct;
			}
		}
        }
        return (best_rate_tt ? best_rate_rix : -1);
}

/*
 * Pick a good "random" bit-rate to sample other than the current one.
 */
static __inline int
pick_sample_rate(struct sample_softc *ssc , struct ath_node *an,
    const HAL_RATE_TABLE *rt, int size_bin)
{
#define	DOT11RATE(ix)	(rt->info[ix].dot11Rate & IEEE80211_RATE_VAL)
#define	MCS(ix)		(rt->info[ix].dot11Rate | IEEE80211_RATE_MCS)
	struct sample_node *sn = ATH_NODE_SAMPLE(an);
	int current_rix, rix;
	unsigned current_tt;
	uint64_t mask;
	
	current_rix = sn->current_rix[size_bin];
	if (current_rix < 0) {
		/* no successes yet, send at the lowest bit-rate */
		/* XXX should return MCS0 if HT */
		return 0;
	}

	current_tt = sn->stats[size_bin][current_rix].average_tx_time;

	rix = sn->last_sample_rix[size_bin]+1;	/* next sample rate */
	mask = sn->ratemask &~ ((uint64_t) 1<<current_rix);/* don't sample current rate */
	while (mask != 0) {
		if ((mask & ((uint64_t) 1<<rix)) == 0) {	/* not a supported rate */
	nextrate:
			if (++rix >= rt->rateCount)
				rix = 0;
			continue;
		}

		/*
		 * The following code stops trying to sample
		 * non-MCS rates when speaking to an MCS node.
		 * However, at least for CCK rates in 2.4GHz mode,
		 * the non-MCS rates MAY actually provide better
		 * PER at the very far edge of reception.
		 *
		 * However! Until ath_rate_form_aggr() grows
		 * some logic to not form aggregates if the
		 * selected rate is non-MCS, this won't work.
		 *
		 * So don't disable this code until you've taught
		 * ath_rate_form_aggr() to drop out if any of
		 * the selected rates are non-MCS.
		 */
#if 1
		/* if the node is HT and the rate isn't HT, don't bother sample */
		if ((an->an_node.ni_flags & IEEE80211_NODE_HT) &&
		    (rt->info[rix].phy != IEEE80211_T_HT)) {
			mask &= ~((uint64_t) 1<<rix);
			goto nextrate;
		}
#endif

		/* this bit-rate is always worse than the current one */
		if (sn->stats[size_bin][rix].perfect_tx_time > current_tt) {
			mask &= ~((uint64_t) 1<<rix);
			goto nextrate;
		}

		/* rarely sample bit-rates that fail a lot */
		if (sn->stats[size_bin][rix].successive_failures > ssc->max_successive_failures &&
		    ticks - sn->stats[size_bin][rix].last_tx < ssc->stale_failure_timeout) {
			mask &= ~((uint64_t) 1<<rix);
			goto nextrate;
		}

		/*
		 * For HT, only sample a few rates on either side of the
		 * current rix; there's quite likely a lot of them.
		 */
		if (an->an_node.ni_flags & IEEE80211_NODE_HT) {
			if (rix < (current_rix - 3) ||
			    rix > (current_rix + 3)) {
				mask &= ~((uint64_t) 1<<rix);
				goto nextrate;
			}
		}

		/* Don't sample more than 2 rates higher for rates > 11M for non-HT rates */
		if (! (an->an_node.ni_flags & IEEE80211_NODE_HT)) {
			if (DOT11RATE(rix) > 2*11 && rix > current_rix + 2) {
				mask &= ~((uint64_t) 1<<rix);
				goto nextrate;
			}
		}

		sn->last_sample_rix[size_bin] = rix;
		return rix;
	}
	return current_rix;
#undef DOT11RATE
#undef	MCS
}

static int
ath_rate_get_static_rix(struct ath_softc *sc, const struct ieee80211_node *ni)
{
#define	RATE(_ix)	(ni->ni_rates.rs_rates[(_ix)] & IEEE80211_RATE_VAL)
#define	DOT11RATE(_ix)	(rt->info[(_ix)].dot11Rate & IEEE80211_RATE_VAL)
#define	MCS(_ix)	(ni->ni_htrates.rs_rates[_ix] | IEEE80211_RATE_MCS)
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	int srate;

	/* Check MCS rates */
	for (srate = ni->ni_htrates.rs_nrates - 1; srate >= 0; srate--) {
		if (MCS(srate) == tp->ucastrate)
			return sc->sc_rixmap[tp->ucastrate];
	}

	/* Check legacy rates */
	for (srate = ni->ni_rates.rs_nrates - 1; srate >= 0; srate--) {
		if (RATE(srate) == tp->ucastrate)
			return sc->sc_rixmap[tp->ucastrate];
	}
	return -1;
#undef	RATE
#undef	DOT11RATE
#undef	MCS
}

static void
ath_rate_update_static_rix(struct ath_softc *sc, struct ieee80211_node *ni)
{
	struct ath_node *an = ATH_NODE(ni);
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	struct sample_node *sn = ATH_NODE_SAMPLE(an);

	if (tp != NULL && tp->ucastrate != IEEE80211_FIXED_RATE_NONE) {
		/*
		 * A fixed rate is to be used; ucastrate is the IEEE code
		 * for this rate (sans basic bit).  Check this against the
		 * negotiated rate set for the node.  Note the fixed rate
		 * may not be available for various reasons so we only
		 * setup the static rate index if the lookup is successful.
		 */
		sn->static_rix = ath_rate_get_static_rix(sc, ni);
	} else {
		sn->static_rix = -1;
	}
}

/*
 * Pick a non-HT rate to begin using.
 */
static int
ath_rate_pick_seed_rate_legacy(struct ath_softc *sc, struct ath_node *an,
    int frameLen)
{
#define	DOT11RATE(ix)	(rt->info[ix].dot11Rate & IEEE80211_RATE_VAL)
#define	MCS(ix)		(rt->info[ix].dot11Rate | IEEE80211_RATE_MCS)
#define	RATE(ix)	(DOT11RATE(ix) / 2)
	int rix = -1;
	const HAL_RATE_TABLE *rt = sc->sc_currates;
	struct sample_node *sn = ATH_NODE_SAMPLE(an);
	const int size_bin = size_to_bin(frameLen);

	/* no packet has been sent successfully yet */
	for (rix = rt->rateCount-1; rix > 0; rix--) {
		if ((sn->ratemask & ((uint64_t) 1<<rix)) == 0)
			continue;

		/* Skip HT rates */
		if (rt->info[rix].phy == IEEE80211_T_HT)
			continue;

		/*
		 * Pick the highest rate <= 36 Mbps
		 * that hasn't failed.
		 */
		if (DOT11RATE(rix) <= 72 &&
		    sn->stats[size_bin][rix].successive_failures == 0) {
			break;
		}
	}
	return rix;
#undef	RATE
#undef	MCS
#undef	DOT11RATE
}

/*
 * Pick a HT rate to begin using.
 *
 * Don't use any non-HT rates; only consider HT rates.
 */
static int
ath_rate_pick_seed_rate_ht(struct ath_softc *sc, struct ath_node *an,
    int frameLen)
{
#define	DOT11RATE(ix)	(rt->info[ix].dot11Rate & IEEE80211_RATE_VAL)
#define	MCS(ix)		(rt->info[ix].dot11Rate | IEEE80211_RATE_MCS)
#define	RATE(ix)	(DOT11RATE(ix) / 2)
	int rix = -1, ht_rix = -1;
	const HAL_RATE_TABLE *rt = sc->sc_currates;
	struct sample_node *sn = ATH_NODE_SAMPLE(an);
	const int size_bin = size_to_bin(frameLen);

	/* no packet has been sent successfully yet */
	for (rix = rt->rateCount-1; rix > 0; rix--) {
		/* Skip rates we can't use */
		if ((sn->ratemask & ((uint64_t) 1<<rix)) == 0)
			continue;

		/* Keep a copy of the last seen HT rate index */
		if (rt->info[rix].phy == IEEE80211_T_HT)
			ht_rix = rix;

		/* Skip non-HT rates */
		if (rt->info[rix].phy != IEEE80211_T_HT)
			continue;

		/*
		 * Pick a medium-speed rate regardless of stream count
		 * which has not seen any failures. Higher rates may fail;
		 * we'll try them later.
		 */
		if (((MCS(rix) & 0x7) <= 4) &&
		    sn->stats[size_bin][rix].successive_failures == 0) {
			break;
		}
	}

	/*
	 * If all the MCS rates have successive failures, rix should be
	 * > 0; otherwise use the lowest MCS rix (hopefully MCS 0.)
	 */
	return MAX(rix, ht_rix);
#undef	RATE
#undef	MCS
#undef	DOT11RATE
}


void
ath_rate_findrate(struct ath_softc *sc, struct ath_node *an,
		  int shortPreamble, size_t frameLen,
		  u_int8_t *rix0, int *try0, u_int8_t *txrate)
{
#define	DOT11RATE(ix)	(rt->info[ix].dot11Rate & IEEE80211_RATE_VAL)
#define	MCS(ix)		(rt->info[ix].dot11Rate | IEEE80211_RATE_MCS)
#define	RATE(ix)	(DOT11RATE(ix) / 2)
	struct sample_node *sn = ATH_NODE_SAMPLE(an);
	struct sample_softc *ssc = ATH_SOFTC_SAMPLE(sc);
	struct ieee80211com *ic = &sc->sc_ic;
	const HAL_RATE_TABLE *rt = sc->sc_currates;
	const int size_bin = size_to_bin(frameLen);
	int rix, mrr, best_rix, change_rates;
	unsigned average_tx_time;

	ath_rate_update_static_rix(sc, &an->an_node);

	if (sn->currates != sc->sc_currates) {
		device_printf(sc->sc_dev, "%s: currates != sc_currates!\n",
		    __func__);
		rix = 0;
		*try0 = ATH_TXMAXTRY;
		goto done;
	}

	if (sn->static_rix != -1) {
		rix = sn->static_rix;
		*try0 = ATH_TXMAXTRY;
		goto done;
	}

	mrr = sc->sc_mrretry;
	/* XXX check HT protmode too */
	if (mrr && (ic->ic_flags & IEEE80211_F_USEPROT && !sc->sc_mrrprot))
		mrr = 0;

	best_rix = pick_best_rate(an, rt, size_bin, !mrr);
	if (best_rix >= 0) {
		average_tx_time = sn->stats[size_bin][best_rix].average_tx_time;
	} else {
		average_tx_time = 0;
	}
	/*
	 * Limit the time measuring the performance of other tx
	 * rates to sample_rate% of the total transmission time.
	 */
	if (sn->sample_tt[size_bin] < average_tx_time * (sn->packets_since_sample[size_bin]*ssc->sample_rate/100)) {
		rix = pick_sample_rate(ssc, an, rt, size_bin);
		IEEE80211_NOTE(an->an_node.ni_vap, IEEE80211_MSG_RATECTL,
		     &an->an_node, "att %d sample_tt %d size %u sample rate %d %s current rate %d %s",
		     average_tx_time,
		     sn->sample_tt[size_bin],
		     bin_to_size(size_bin),
		     dot11rate(rt, rix),
		     dot11rate_label(rt, rix),
		     dot11rate(rt, sn->current_rix[size_bin]),
		     dot11rate_label(rt, sn->current_rix[size_bin]));
		if (rix != sn->current_rix[size_bin]) {
			sn->current_sample_rix[size_bin] = rix;
		} else {
			sn->current_sample_rix[size_bin] = -1;
		}
		sn->packets_since_sample[size_bin] = 0;
	} else {
		change_rates = 0;
		if (!sn->packets_sent[size_bin] || best_rix == -1) {
			/* no packet has been sent successfully yet */
			change_rates = 1;
			if (an->an_node.ni_flags & IEEE80211_NODE_HT)
				best_rix =
				    ath_rate_pick_seed_rate_ht(sc, an, frameLen);
			else
				best_rix =
				    ath_rate_pick_seed_rate_legacy(sc, an, frameLen);
		} else if (sn->packets_sent[size_bin] < 20) {
			/* let the bit-rate switch quickly during the first few packets */
			IEEE80211_NOTE(an->an_node.ni_vap,
			    IEEE80211_MSG_RATECTL, &an->an_node,
			    "%s: switching quickly..", __func__);
			change_rates = 1;
		} else if (ticks - ssc->min_switch > sn->ticks_since_switch[size_bin]) {
			/* min_switch seconds have gone by */
			IEEE80211_NOTE(an->an_node.ni_vap,
			    IEEE80211_MSG_RATECTL, &an->an_node,
			    "%s: min_switch %d > ticks_since_switch %d..",
			    __func__, ticks - ssc->min_switch, sn->ticks_since_switch[size_bin]);
			change_rates = 1;
		} else if ((! (an->an_node.ni_flags & IEEE80211_NODE_HT)) &&
		    (2*average_tx_time < sn->stats[size_bin][sn->current_rix[size_bin]].average_tx_time)) {
			/* the current bit-rate is twice as slow as the best one */
			IEEE80211_NOTE(an->an_node.ni_vap,
			    IEEE80211_MSG_RATECTL, &an->an_node,
			    "%s: 2x att (= %d) < cur_rix att %d",
			    __func__,
			    2 * average_tx_time, sn->stats[size_bin][sn->current_rix[size_bin]].average_tx_time);
			change_rates = 1;
		} else if ((an->an_node.ni_flags & IEEE80211_NODE_HT)) {
			int cur_rix = sn->current_rix[size_bin];
			int cur_att = sn->stats[size_bin][cur_rix].average_tx_time;
			/*
			 * If the node is HT, upgrade it if the MCS rate is
			 * higher and the average tx time is within 20% of
			 * the current rate. It can fail a little.
			 *
			 * This is likely not optimal!
			 */
#if 0
			printf("cur rix/att %x/%d, best rix/att %x/%d\n",
			    MCS(cur_rix), cur_att, MCS(best_rix), average_tx_time);
#endif
			if ((MCS(best_rix) > MCS(cur_rix)) &&
			    (average_tx_time * 8) <= (cur_att * 10)) {
				IEEE80211_NOTE(an->an_node.ni_vap,
				    IEEE80211_MSG_RATECTL, &an->an_node,
				    "%s: HT: best_rix 0x%d > cur_rix 0x%x, average_tx_time %d, cur_att %d",
				    __func__,
				    MCS(best_rix), MCS(cur_rix), average_tx_time, cur_att);
				change_rates = 1;
			}
		}

		sn->packets_since_sample[size_bin]++;
		
		if (change_rates) {
			if (best_rix != sn->current_rix[size_bin]) {
				IEEE80211_NOTE(an->an_node.ni_vap,
				    IEEE80211_MSG_RATECTL,
				    &an->an_node,
"%s: size %d switch rate %d (%d/%d) -> %d (%d/%d) after %d packets mrr %d",
				    __func__,
				    bin_to_size(size_bin),
				    RATE(sn->current_rix[size_bin]),
				    sn->stats[size_bin][sn->current_rix[size_bin]].average_tx_time,
				    sn->stats[size_bin][sn->current_rix[size_bin]].perfect_tx_time,
				    RATE(best_rix),
				    sn->stats[size_bin][best_rix].average_tx_time,
				    sn->stats[size_bin][best_rix].perfect_tx_time,
				    sn->packets_since_switch[size_bin],
				    mrr);
			}
			sn->packets_since_switch[size_bin] = 0;
			sn->current_rix[size_bin] = best_rix;
			sn->ticks_since_switch[size_bin] = ticks;
			/* 
			 * Set the visible txrate for this node.
			 */
			an->an_node.ni_txrate = (rt->info[best_rix].phy == IEEE80211_T_HT) ?  MCS(best_rix) : DOT11RATE(best_rix);
		}
		rix = sn->current_rix[size_bin];
		sn->packets_since_switch[size_bin]++;
	}
	*try0 = mrr ? sn->sched[rix].t0 : ATH_TXMAXTRY;
done:

	/*
	 * This bug totally sucks and should be fixed.
	 *
	 * For now though, let's not panic, so we can start to figure
	 * out how to better reproduce it.
	 */
	if (rix < 0 || rix >= rt->rateCount) {
		printf("%s: ERROR: rix %d out of bounds (rateCount=%d)\n",
		    __func__,
		    rix,
		    rt->rateCount);
		    rix = 0;	/* XXX just default for now */
	}
	KASSERT(rix >= 0 && rix < rt->rateCount, ("rix is %d", rix));

	*rix0 = rix;
	*txrate = rt->info[rix].rateCode
		| (shortPreamble ? rt->info[rix].shortPreamble : 0);
	sn->packets_sent[size_bin]++;
#undef DOT11RATE
#undef MCS
#undef RATE
}

/*
 * Get the TX rates. Don't fiddle with short preamble flags for them;
 * the caller can do that.
 */
void
ath_rate_getxtxrates(struct ath_softc *sc, struct ath_node *an,
    uint8_t rix0, struct ath_rc_series *rc)
{
	struct sample_node *sn = ATH_NODE_SAMPLE(an);
	const struct txschedule *sched = &sn->sched[rix0];

	KASSERT(rix0 == sched->r0, ("rix0 (%x) != sched->r0 (%x)!\n",
	    rix0, sched->r0));

	rc[0].flags = rc[1].flags = rc[2].flags = rc[3].flags = 0;

	rc[0].rix = sched->r0;
	rc[1].rix = sched->r1;
	rc[2].rix = sched->r2;
	rc[3].rix = sched->r3;

	rc[0].tries = sched->t0;
	rc[1].tries = sched->t1;
	rc[2].tries = sched->t2;
	rc[3].tries = sched->t3;
}

void
ath_rate_setupxtxdesc(struct ath_softc *sc, struct ath_node *an,
		      struct ath_desc *ds, int shortPreamble, u_int8_t rix)
{
	struct sample_node *sn = ATH_NODE_SAMPLE(an);
	const struct txschedule *sched = &sn->sched[rix];
	const HAL_RATE_TABLE *rt = sc->sc_currates;
	uint8_t rix1, s1code, rix2, s2code, rix3, s3code;

	/* XXX precalculate short preamble tables */
	rix1 = sched->r1;
	s1code = rt->info[rix1].rateCode
	       | (shortPreamble ? rt->info[rix1].shortPreamble : 0);
	rix2 = sched->r2;
	s2code = rt->info[rix2].rateCode
	       | (shortPreamble ? rt->info[rix2].shortPreamble : 0);
	rix3 = sched->r3;
	s3code = rt->info[rix3].rateCode
	       | (shortPreamble ? rt->info[rix3].shortPreamble : 0);
	ath_hal_setupxtxdesc(sc->sc_ah, ds,
	    s1code, sched->t1,		/* series 1 */
	    s2code, sched->t2,		/* series 2 */
	    s3code, sched->t3);		/* series 3 */
}

static void
update_stats(struct ath_softc *sc, struct ath_node *an, 
		  int frame_size,
		  int rix0, int tries0,
		  int rix1, int tries1,
		  int rix2, int tries2,
		  int rix3, int tries3,
		  int short_tries, int tries, int status,
		  int nframes, int nbad)
{
	struct sample_node *sn = ATH_NODE_SAMPLE(an);
	struct sample_softc *ssc = ATH_SOFTC_SAMPLE(sc);
#ifdef IEEE80211_DEBUG
	const HAL_RATE_TABLE *rt = sc->sc_currates;
#endif
	const int size_bin = size_to_bin(frame_size);
	const int size = bin_to_size(size_bin);
	int tt, tries_so_far;
	int is_ht40 = (an->an_node.ni_chw == 40);
	int pct;

	if (!IS_RATE_DEFINED(sn, rix0))
		return;
	tt = calc_usecs_unicast_packet(sc, size, rix0, short_tries,
		MIN(tries0, tries) - 1, is_ht40);
	tries_so_far = tries0;

	if (tries1 && tries_so_far < tries) {
		if (!IS_RATE_DEFINED(sn, rix1))
			return;
		tt += calc_usecs_unicast_packet(sc, size, rix1, short_tries,
			MIN(tries1 + tries_so_far, tries) - tries_so_far - 1, is_ht40);
		tries_so_far += tries1;
	}

	if (tries2 && tries_so_far < tries) {
		if (!IS_RATE_DEFINED(sn, rix2))
			return;
		tt += calc_usecs_unicast_packet(sc, size, rix2, short_tries,
			MIN(tries2 + tries_so_far, tries) - tries_so_far - 1, is_ht40);
		tries_so_far += tries2;
	}

	if (tries3 && tries_so_far < tries) {
		if (!IS_RATE_DEFINED(sn, rix3))
			return;
		tt += calc_usecs_unicast_packet(sc, size, rix3, short_tries,
			MIN(tries3 + tries_so_far, tries) - tries_so_far - 1, is_ht40);
	}

	if (sn->stats[size_bin][rix0].total_packets < ssc->smoothing_minpackets) {
		/* just average the first few packets */
		int avg_tx = sn->stats[size_bin][rix0].average_tx_time;
		int packets = sn->stats[size_bin][rix0].total_packets;
		sn->stats[size_bin][rix0].average_tx_time = (tt+(avg_tx*packets))/(packets+nframes);
	} else {
		/* use a ewma */
		sn->stats[size_bin][rix0].average_tx_time = 
			((sn->stats[size_bin][rix0].average_tx_time * ssc->smoothing_rate) + 
			 (tt * (100 - ssc->smoothing_rate))) / 100;
	}
	
	/*
	 * XXX Don't mark the higher bit rates as also having failed; as this
	 * unfortunately stops those rates from being tasted when trying to
	 * TX. This happens with 11n aggregation.
	 *
	 * This is valid for higher CCK rates, higher OFDM rates, and higher
	 * HT rates within the current number of streams (eg MCS0..7, 8..15,
	 * etc.)
	 */
	if (nframes == nbad) {
#if 0
		int y;
#endif
		sn->stats[size_bin][rix0].successive_failures += nbad;
#if 0
		for (y = size_bin+1; y < NUM_PACKET_SIZE_BINS; y++) {
			/*
			 * Also say larger packets failed since we
			 * assume if a small packet fails at a
			 * bit-rate then a larger one will also.
			 */
			sn->stats[y][rix0].successive_failures += nbad;
			sn->stats[y][rix0].last_tx = ticks;
			sn->stats[y][rix0].tries += tries;
			sn->stats[y][rix0].total_packets += nframes;
		}
#endif
	} else {
		sn->stats[size_bin][rix0].packets_acked += (nframes - nbad);
		sn->stats[size_bin][rix0].successive_failures = 0;
	}
	sn->stats[size_bin][rix0].tries += tries;
	sn->stats[size_bin][rix0].last_tx = ticks;
	sn->stats[size_bin][rix0].total_packets += nframes;

	/* update EWMA for this rix */

	/* Calculate percentage based on current rate */
	if (nframes == 0)
		nframes = nbad = 1;
	pct = ((nframes - nbad) * 1000) / nframes;

	if (sn->stats[size_bin][rix0].total_packets <
	    ssc->smoothing_minpackets) {
		/* just average the first few packets */
		int a_pct = (sn->stats[size_bin][rix0].packets_acked * 1000) /
		    (sn->stats[size_bin][rix0].total_packets);
		sn->stats[size_bin][rix0].ewma_pct = a_pct;
	} else {
		/* use a ewma */
		sn->stats[size_bin][rix0].ewma_pct =
			((sn->stats[size_bin][rix0].ewma_pct * ssc->smoothing_rate) +
			 (pct * (100 - ssc->smoothing_rate))) / 100;
	}


	if (rix0 == sn->current_sample_rix[size_bin]) {
		IEEE80211_NOTE(an->an_node.ni_vap, IEEE80211_MSG_RATECTL,
		   &an->an_node,
"%s: size %d %s sample rate %d %s tries (%d/%d) tt %d avg_tt (%d/%d) nfrm %d nbad %d", 
		    __func__, 
		    size,
		    status ? "FAIL" : "OK",
		    dot11rate(rt, rix0),
		    dot11rate_label(rt, rix0),
		    short_tries, tries, tt, 
		    sn->stats[size_bin][rix0].average_tx_time,
		    sn->stats[size_bin][rix0].perfect_tx_time,
		    nframes, nbad);
		sn->sample_tt[size_bin] = tt;
		sn->current_sample_rix[size_bin] = -1;
	}
}

static void
badrate(struct ath_softc *sc, int series, int hwrate, int tries, int status)
{

	device_printf(sc->sc_dev,
	    "bad series%d hwrate 0x%x, tries %u ts_status 0x%x\n",
	    series, hwrate, tries, status);
}

void
ath_rate_tx_complete(struct ath_softc *sc, struct ath_node *an,
	const struct ath_rc_series *rc, const struct ath_tx_status *ts,
	int frame_size, int nframes, int nbad)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct sample_node *sn = ATH_NODE_SAMPLE(an);
	int final_rix, short_tries, long_tries;
	const HAL_RATE_TABLE *rt = sc->sc_currates;
	int status = ts->ts_status;
	int mrr;

	final_rix = rt->rateCodeToIndex[ts->ts_rate];
	short_tries = ts->ts_shortretry;
	long_tries = ts->ts_longretry + 1;

	if (nframes == 0) {
		device_printf(sc->sc_dev, "%s: nframes=0?\n", __func__);
		return;
	}

	if (frame_size == 0)		    /* NB: should not happen */
		frame_size = 1500;

	if (sn->ratemask == 0) {
		IEEE80211_NOTE(an->an_node.ni_vap, IEEE80211_MSG_RATECTL,
		    &an->an_node,
		    "%s: size %d %s rate/try %d/%d no rates yet", 
		    __func__,
		    bin_to_size(size_to_bin(frame_size)),
		    status ? "FAIL" : "OK",
		    short_tries, long_tries);
		return;
	}
	mrr = sc->sc_mrretry;
	/* XXX check HT protmode too */
	if (mrr && (ic->ic_flags & IEEE80211_F_USEPROT && !sc->sc_mrrprot))
		mrr = 0;

	if (!mrr || ts->ts_finaltsi == 0) {
		if (!IS_RATE_DEFINED(sn, final_rix)) {
			device_printf(sc->sc_dev,
			    "%s: ts_rate=%d ts_finaltsi=%d, final_rix=%d\n",
			    __func__, ts->ts_rate, ts->ts_finaltsi, final_rix);
			badrate(sc, 0, ts->ts_rate, long_tries, status);
			return;
		}
		/*
		 * Only one rate was used; optimize work.
		 */
		IEEE80211_NOTE(an->an_node.ni_vap, IEEE80211_MSG_RATECTL,
		     &an->an_node, "%s: size %d (%d bytes) %s rate/short/long %d %s/%d/%d nframes/nbad [%d/%d]",
		     __func__,
		     bin_to_size(size_to_bin(frame_size)),
		     frame_size,
		     status ? "FAIL" : "OK",
		     dot11rate(rt, final_rix), dot11rate_label(rt, final_rix),
		     short_tries, long_tries, nframes, nbad);
		update_stats(sc, an, frame_size, 
			     final_rix, long_tries,
			     0, 0,
			     0, 0,
			     0, 0,
			     short_tries, long_tries, status,
			     nframes, nbad);

	} else {
		int finalTSIdx = ts->ts_finaltsi;
		int i;

		/*
		 * Process intermediate rates that failed.
		 */

		IEEE80211_NOTE(an->an_node.ni_vap, IEEE80211_MSG_RATECTL,
		    &an->an_node,
"%s: size %d (%d bytes) finaltsidx %d short %d long %d %s rate/try [%d %s/%d %d %s/%d %d %s/%d %d %s/%d] nframes/nbad [%d/%d]", 
		     __func__,
		     bin_to_size(size_to_bin(frame_size)),
		     frame_size,
		     finalTSIdx,
		     short_tries,
		     long_tries,
		     status ? "FAIL" : "OK",
		     dot11rate(rt, rc[0].rix),
		      dot11rate_label(rt, rc[0].rix), rc[0].tries,
		     dot11rate(rt, rc[1].rix),
		      dot11rate_label(rt, rc[1].rix), rc[1].tries,
		     dot11rate(rt, rc[2].rix),
		      dot11rate_label(rt, rc[2].rix), rc[2].tries,
		     dot11rate(rt, rc[3].rix),
		      dot11rate_label(rt, rc[3].rix), rc[3].tries,
		     nframes, nbad);

		for (i = 0; i < 4; i++) {
			if (rc[i].tries && !IS_RATE_DEFINED(sn, rc[i].rix))
				badrate(sc, 0, rc[i].ratecode, rc[i].tries,
				    status);
		}

		/*
		 * NB: series > 0 are not penalized for failure
		 * based on the try counts under the assumption
		 * that losses are often bursty and since we
		 * sample higher rates 1 try at a time doing so
		 * may unfairly penalize them.
		 */
		if (rc[0].tries) {
			update_stats(sc, an, frame_size,
				     rc[0].rix, rc[0].tries,
				     rc[1].rix, rc[1].tries,
				     rc[2].rix, rc[2].tries,
				     rc[3].rix, rc[3].tries,
				     short_tries, long_tries,
				     long_tries > rc[0].tries,
				     nframes, nbad);
			long_tries -= rc[0].tries;
		}
		
		if (rc[1].tries && finalTSIdx > 0) {
			update_stats(sc, an, frame_size,
				     rc[1].rix, rc[1].tries,
				     rc[2].rix, rc[2].tries,
				     rc[3].rix, rc[3].tries,
				     0, 0,
				     short_tries, long_tries,
				     status,
				     nframes, nbad);
			long_tries -= rc[1].tries;
		}

		if (rc[2].tries && finalTSIdx > 1) {
			update_stats(sc, an, frame_size,
				     rc[2].rix, rc[2].tries,
				     rc[3].rix, rc[3].tries,
				     0, 0,
				     0, 0,
				     short_tries, long_tries,
				     status,
				     nframes, nbad);
			long_tries -= rc[2].tries;
		}

		if (rc[3].tries && finalTSIdx > 2) {
			update_stats(sc, an, frame_size,
				     rc[3].rix, rc[3].tries,
				     0, 0,
				     0, 0,
				     0, 0,
				     short_tries, long_tries,
				     status,
				     nframes, nbad);
		}
	}
}

void
ath_rate_newassoc(struct ath_softc *sc, struct ath_node *an, int isnew)
{
	if (isnew)
		ath_rate_ctl_reset(sc, &an->an_node);
}

static const struct txschedule *mrr_schedules[IEEE80211_MODE_MAX+2] = {
	NULL,		/* IEEE80211_MODE_AUTO */
	series_11a,	/* IEEE80211_MODE_11A */
	series_11g,	/* IEEE80211_MODE_11B */
	series_11g,	/* IEEE80211_MODE_11G */
	NULL,		/* IEEE80211_MODE_FH */
	series_11a,	/* IEEE80211_MODE_TURBO_A */
	series_11g,	/* IEEE80211_MODE_TURBO_G */
	series_11a,	/* IEEE80211_MODE_STURBO_A */
	series_11na,	/* IEEE80211_MODE_11NA */
	series_11ng,	/* IEEE80211_MODE_11NG */
	series_half,	/* IEEE80211_MODE_HALF */
	series_quarter,	/* IEEE80211_MODE_QUARTER */
};

/*
 * Initialize the tables for a node.
 */
static void
ath_rate_ctl_reset(struct ath_softc *sc, struct ieee80211_node *ni)
{
#define	RATE(_ix)	(ni->ni_rates.rs_rates[(_ix)] & IEEE80211_RATE_VAL)
#define	DOT11RATE(_ix)	(rt->info[(_ix)].dot11Rate & IEEE80211_RATE_VAL)
#define	MCS(_ix)	(ni->ni_htrates.rs_rates[_ix] | IEEE80211_RATE_MCS)
	struct ath_node *an = ATH_NODE(ni);
	struct sample_node *sn = ATH_NODE_SAMPLE(an);
	const HAL_RATE_TABLE *rt = sc->sc_currates;
	int x, y, rix;

	KASSERT(rt != NULL, ("no rate table, mode %u", sc->sc_curmode));

	KASSERT(sc->sc_curmode < IEEE80211_MODE_MAX+2,
	    ("curmode %u", sc->sc_curmode));

	sn->sched = mrr_schedules[sc->sc_curmode];
	KASSERT(sn->sched != NULL,
	    ("no mrr schedule for mode %u", sc->sc_curmode));

        sn->static_rix = -1;
	ath_rate_update_static_rix(sc, ni);

	sn->currates = sc->sc_currates;

	/*
	 * Construct a bitmask of usable rates.  This has all
	 * negotiated rates minus those marked by the hal as
	 * to be ignored for doing rate control.
	 */
	sn->ratemask = 0;
	/* MCS rates */
	if (ni->ni_flags & IEEE80211_NODE_HT) {
		for (x = 0; x < ni->ni_htrates.rs_nrates; x++) {
			rix = sc->sc_rixmap[MCS(x)];
			if (rix == 0xff)
				continue;
			/* skip rates marked broken by hal */
			if (!rt->info[rix].valid)
				continue;
			KASSERT(rix < SAMPLE_MAXRATES,
			    ("mcs %u has rix %d", MCS(x), rix));
			sn->ratemask |= (uint64_t) 1<<rix;
		}
	}

	/* Legacy rates */
	for (x = 0; x < ni->ni_rates.rs_nrates; x++) {
		rix = sc->sc_rixmap[RATE(x)];
		if (rix == 0xff)
			continue;
		/* skip rates marked broken by hal */
		if (!rt->info[rix].valid)
			continue;
		KASSERT(rix < SAMPLE_MAXRATES,
		    ("rate %u has rix %d", RATE(x), rix));
		sn->ratemask |= (uint64_t) 1<<rix;
	}
#ifdef IEEE80211_DEBUG
	if (ieee80211_msg(ni->ni_vap, IEEE80211_MSG_RATECTL)) {
		uint64_t mask;

		ieee80211_note(ni->ni_vap, "[%6D] %s: size 1600 rate/tt",
		    ni->ni_macaddr, ":", __func__);
		for (mask = sn->ratemask, rix = 0; mask != 0; mask >>= 1, rix++) {
			if ((mask & 1) == 0)
				continue;
			printf(" %d %s/%d", dot11rate(rt, rix), dot11rate_label(rt, rix),
			    calc_usecs_unicast_packet(sc, 1600, rix, 0,0,
			        (ni->ni_chw == 40)));
		}
		printf("\n");
	}
#endif
	for (y = 0; y < NUM_PACKET_SIZE_BINS; y++) {
		int size = bin_to_size(y);
		uint64_t mask;

		sn->packets_sent[y] = 0;
		sn->current_sample_rix[y] = -1;
		sn->last_sample_rix[y] = 0;
		/* XXX start with first valid rate */
		sn->current_rix[y] = ffs(sn->ratemask)-1;
		
		/*
		 * Initialize the statistics buckets; these are
		 * indexed by the rate code index.
		 */
		for (rix = 0, mask = sn->ratemask; mask != 0; rix++, mask >>= 1) {
			if ((mask & 1) == 0)		/* not a valid rate */
				continue;
			sn->stats[y][rix].successive_failures = 0;
			sn->stats[y][rix].tries = 0;
			sn->stats[y][rix].total_packets = 0;
			sn->stats[y][rix].packets_acked = 0;
			sn->stats[y][rix].last_tx = 0;
			sn->stats[y][rix].ewma_pct = 0;
			
			sn->stats[y][rix].perfect_tx_time =
			    calc_usecs_unicast_packet(sc, size, rix, 0, 0,
			    (ni->ni_chw == 40));
			sn->stats[y][rix].average_tx_time =
			    sn->stats[y][rix].perfect_tx_time;
		}
	}
#if 0
	/* XXX 0, num_rates-1 are wrong */
	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_RATECTL, ni,
	    "%s: %d rates %d%sMbps (%dus)- %d%sMbps (%dus)", __func__, 
	    sn->num_rates,
	    DOT11RATE(0)/2, DOT11RATE(0) % 1 ? ".5" : "",
	    sn->stats[1][0].perfect_tx_time,
	    DOT11RATE(sn->num_rates-1)/2, DOT11RATE(sn->num_rates-1) % 1 ? ".5" : "",
	    sn->stats[1][sn->num_rates-1].perfect_tx_time
	);
#endif
	/* set the visible bit-rate */
	if (sn->static_rix != -1)
		ni->ni_txrate = DOT11RATE(sn->static_rix);
	else
		ni->ni_txrate = RATE(0);
#undef RATE
#undef DOT11RATE
}

/*
 * Fetch the statistics for the given node.
 *
 * The ieee80211 node must be referenced and unlocked, however the ath_node
 * must be locked.
 *
 * The main difference here is that we convert the rate indexes
 * to 802.11 rates, or the userland output won't make much sense
 * as it has no access to the rix table.
 */
int
ath_rate_fetch_node_stats(struct ath_softc *sc, struct ath_node *an,
    struct ath_rateioctl *rs)
{
	struct sample_node *sn = ATH_NODE_SAMPLE(an);
	const HAL_RATE_TABLE *rt = sc->sc_currates;
	struct ath_rateioctl_tlv av;
	struct ath_rateioctl_rt *tv;
	int y;
	int o = 0;

	ATH_NODE_LOCK_ASSERT(an);

	/*
	 * Ensure there's enough space for the statistics.
	 */
	if (rs->len <
	    sizeof(struct ath_rateioctl_tlv) +
	    sizeof(struct ath_rateioctl_rt) +
	    sizeof(struct ath_rateioctl_tlv) +
	    sizeof(struct sample_node)) {
		device_printf(sc->sc_dev, "%s: len=%d, too short\n",
		    __func__,
		    rs->len);
		return (EINVAL);
	}

	/*
	 * Take a temporary copy of the sample node state so we can
	 * modify it before we copy it.
	 */
	tv = malloc(sizeof(struct ath_rateioctl_rt), M_TEMP,
	    M_NOWAIT | M_ZERO);
	if (tv == NULL) {
		return (ENOMEM);
	}

	/*
	 * Populate the rate table mapping TLV.
	 */
	tv->nentries = rt->rateCount;
	for (y = 0; y < rt->rateCount; y++) {
		tv->ratecode[y] = rt->info[y].dot11Rate & IEEE80211_RATE_VAL;
		if (rt->info[y].phy == IEEE80211_T_HT)
			tv->ratecode[y] |= IEEE80211_RATE_MCS;
	}

	o = 0;
	/*
	 * First TLV - rate code mapping
	 */
	av.tlv_id = ATH_RATE_TLV_RATETABLE;
	av.tlv_len = sizeof(struct ath_rateioctl_rt);
	copyout(&av, rs->buf + o, sizeof(struct ath_rateioctl_tlv));
	o += sizeof(struct ath_rateioctl_tlv);
	copyout(tv, rs->buf + o, sizeof(struct ath_rateioctl_rt));
	o += sizeof(struct ath_rateioctl_rt);

	/*
	 * Second TLV - sample node statistics
	 */
	av.tlv_id = ATH_RATE_TLV_SAMPLENODE;
	av.tlv_len = sizeof(struct sample_node);
	copyout(&av, rs->buf + o, sizeof(struct ath_rateioctl_tlv));
	o += sizeof(struct ath_rateioctl_tlv);

	/*
	 * Copy the statistics over to the provided buffer.
	 */
	copyout(sn, rs->buf + o, sizeof(struct sample_node));
	o += sizeof(struct sample_node);

	free(tv, M_TEMP);

	return (0);
}

static void
sample_stats(void *arg, struct ieee80211_node *ni)
{
	struct ath_softc *sc = arg;
	const HAL_RATE_TABLE *rt = sc->sc_currates;
	struct sample_node *sn = ATH_NODE_SAMPLE(ATH_NODE(ni));
	uint64_t mask;
	int rix, y;

	printf("\n[%s] refcnt %d static_rix (%d %s) ratemask 0x%jx\n",
	    ether_sprintf(ni->ni_macaddr), ieee80211_node_refcnt(ni),
	    dot11rate(rt, sn->static_rix),
	    dot11rate_label(rt, sn->static_rix),
	    (uintmax_t)sn->ratemask);
	for (y = 0; y < NUM_PACKET_SIZE_BINS; y++) {
		printf("[%4u] cur rix %d (%d %s) since switch: packets %d ticks %u\n",
		    bin_to_size(y), sn->current_rix[y],
		    dot11rate(rt, sn->current_rix[y]),
		    dot11rate_label(rt, sn->current_rix[y]),
		    sn->packets_since_switch[y], sn->ticks_since_switch[y]);
		printf("[%4u] last sample (%d %s) cur sample (%d %s) packets sent %d\n",
		    bin_to_size(y),
		    dot11rate(rt, sn->last_sample_rix[y]),
		    dot11rate_label(rt, sn->last_sample_rix[y]),
		    dot11rate(rt, sn->current_sample_rix[y]),
		    dot11rate_label(rt, sn->current_sample_rix[y]),
		    sn->packets_sent[y]);
		printf("[%4u] packets since sample %d sample tt %u\n",
		    bin_to_size(y), sn->packets_since_sample[y],
		    sn->sample_tt[y]);
	}
	for (mask = sn->ratemask, rix = 0; mask != 0; mask >>= 1, rix++) {
		if ((mask & 1) == 0)
				continue;
		for (y = 0; y < NUM_PACKET_SIZE_BINS; y++) {
			if (sn->stats[y][rix].total_packets == 0)
				continue;
			printf("[%2u %s:%4u] %8ju:%-8ju (%3d%%) (EWMA %3d.%1d%%) T %8ju F %4d avg %5u last %u\n",
			    dot11rate(rt, rix), dot11rate_label(rt, rix),
			    bin_to_size(y),
			    (uintmax_t) sn->stats[y][rix].total_packets,
			    (uintmax_t) sn->stats[y][rix].packets_acked,
			    (int) ((sn->stats[y][rix].packets_acked * 100ULL) /
			     sn->stats[y][rix].total_packets),
			    sn->stats[y][rix].ewma_pct / 10,
			    sn->stats[y][rix].ewma_pct % 10,
			    (uintmax_t) sn->stats[y][rix].tries,
			    sn->stats[y][rix].successive_failures,
			    sn->stats[y][rix].average_tx_time,
			    ticks - sn->stats[y][rix].last_tx);
		}
	}
}

static int
ath_rate_sysctl_stats(SYSCTL_HANDLER_ARGS)
{
	struct ath_softc *sc = arg1;
	struct ieee80211com *ic = &sc->sc_ic;
	int error, v;

	v = 0;
	error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || !req->newptr)
		return error;
	ieee80211_iterate_nodes(&ic->ic_sta, sample_stats, sc);
	return 0;
}

static int
ath_rate_sysctl_smoothing_rate(SYSCTL_HANDLER_ARGS)
{
	struct sample_softc *ssc = arg1;
	int rate, error;

	rate = ssc->smoothing_rate;
	error = sysctl_handle_int(oidp, &rate, 0, req);
	if (error || !req->newptr)
		return error;
	if (!(0 <= rate && rate < 100))
		return EINVAL;
	ssc->smoothing_rate = rate;
	ssc->smoothing_minpackets = 100 / (100 - rate);
	return 0;
}

static int
ath_rate_sysctl_sample_rate(SYSCTL_HANDLER_ARGS)
{
	struct sample_softc *ssc = arg1;
	int rate, error;

	rate = ssc->sample_rate;
	error = sysctl_handle_int(oidp, &rate, 0, req);
	if (error || !req->newptr)
		return error;
	if (!(2 <= rate && rate <= 100))
		return EINVAL;
	ssc->sample_rate = rate;
	return 0;
}

static void
ath_rate_sysctlattach(struct ath_softc *sc, struct sample_softc *ssc)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "smoothing_rate", CTLTYPE_INT | CTLFLAG_RW, ssc, 0,
	    ath_rate_sysctl_smoothing_rate, "I",
	    "sample: smoothing rate for avg tx time (%%)");
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "sample_rate", CTLTYPE_INT | CTLFLAG_RW, ssc, 0,
	    ath_rate_sysctl_sample_rate, "I",
	    "sample: percent air time devoted to sampling new rates (%%)");
	/* XXX max_successive_failures, stale_failure_timeout, min_switch */
	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "sample_stats", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
	    ath_rate_sysctl_stats, "I", "sample: print statistics");
}

struct ath_ratectrl *
ath_rate_attach(struct ath_softc *sc)
{
	struct sample_softc *ssc;
	
	ssc = malloc(sizeof(struct sample_softc), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (ssc == NULL)
		return NULL;
	ssc->arc.arc_space = sizeof(struct sample_node);
	ssc->smoothing_rate = 75;		/* ewma percentage ([0..99]) */
	ssc->smoothing_minpackets = 100 / (100 - ssc->smoothing_rate);
	ssc->sample_rate = 10;			/* %time to try diff tx rates */
	ssc->max_successive_failures = 3;	/* threshold for rate sampling*/
	ssc->stale_failure_timeout = 10 * hz;	/* 10 seconds */
	ssc->min_switch = hz;			/* 1 second */
	ath_rate_sysctlattach(sc, ssc);
	return &ssc->arc;
}

void
ath_rate_detach(struct ath_ratectrl *arc)
{
	struct sample_softc *ssc = (struct sample_softc *) arc;
	
	free(ssc, M_DEVBUF);
}
