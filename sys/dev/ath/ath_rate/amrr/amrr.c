/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2004 INRIA
 * Copyright (c) 2002-2005 Sam Leffler, Errno Consulting
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
 * AMRR rate control. See:
 * http://www-sop.inria.fr/rapports/sophia/RR-5208.html
 * "IEEE 802.11 Rate Adaptation: A Practical Approach" by
 *    Mathieu Lacage, Hossein Manshaei, Thierry Turletti
 */
#include "opt_ath.h"
#include "opt_inet.h"
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/sysctl.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/errno.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>

#include <sys/socket.h>
 
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_arp.h>

#include <net80211/ieee80211_var.h>

#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h> 
#include <netinet/if_ether.h>
#endif

#include <dev/ath/if_athvar.h>
#include <dev/ath/ath_rate/amrr/amrr.h>
#include <dev/ath/ath_hal/ah_desc.h>

static	int ath_rateinterval = 1000;		/* rate ctl interval (ms)  */
static	int ath_rate_max_success_threshold = 10;
static	int ath_rate_min_success_threshold = 1;

static void	ath_rate_update(struct ath_softc *, struct ieee80211_node *,
			int rate);
static void	ath_rate_ctl_start(struct ath_softc *, struct ieee80211_node *);
static void	ath_rate_ctl(void *, struct ieee80211_node *);

void
ath_rate_node_init(struct ath_softc *sc, struct ath_node *an)
{
	/* NB: assumed to be zero'd by caller */
}

void
ath_rate_node_cleanup(struct ath_softc *sc, struct ath_node *an)
{
}

void
ath_rate_findrate(struct ath_softc *sc, struct ath_node *an,
	int shortPreamble, size_t frameLen,
	u_int8_t *rix, int *try0, u_int8_t *txrate)
{
	struct amrr_node *amn = ATH_NODE_AMRR(an);

	*rix = amn->amn_tx_rix0;
	*try0 = amn->amn_tx_try0;
	if (shortPreamble)
		*txrate = amn->amn_tx_rate0sp;
	else
		*txrate = amn->amn_tx_rate0;
}

/*
 * Get the TX rates.
 *
 * The short preamble bits aren't set here; the caller should augment
 * the returned rate with the relevant preamble rate flag.
 */
void
ath_rate_getxtxrates(struct ath_softc *sc, struct ath_node *an,
    uint8_t rix0, struct ath_rc_series *rc)
{
	struct amrr_node *amn = ATH_NODE_AMRR(an);

	rc[0].flags = rc[1].flags = rc[2].flags = rc[3].flags = 0;

	rc[0].rix = amn->amn_tx_rate0;
	rc[1].rix = amn->amn_tx_rate1;
	rc[2].rix = amn->amn_tx_rate2;
	rc[3].rix = amn->amn_tx_rate3;

	rc[0].tries = amn->amn_tx_try0;
	rc[1].tries = amn->amn_tx_try1;
	rc[2].tries = amn->amn_tx_try2;
	rc[3].tries = amn->amn_tx_try3;
}


void
ath_rate_setupxtxdesc(struct ath_softc *sc, struct ath_node *an,
	struct ath_desc *ds, int shortPreamble, u_int8_t rix)
{
	struct amrr_node *amn = ATH_NODE_AMRR(an);

	ath_hal_setupxtxdesc(sc->sc_ah, ds
		, amn->amn_tx_rate1sp, amn->amn_tx_try1	/* series 1 */
		, amn->amn_tx_rate2sp, amn->amn_tx_try2	/* series 2 */
		, amn->amn_tx_rate3sp, amn->amn_tx_try3	/* series 3 */
	);
}

void
ath_rate_tx_complete(struct ath_softc *sc, struct ath_node *an,
	const struct ath_rc_series *rc, const struct ath_tx_status *ts,
	int frame_size, int nframes, int nbad)
{
	struct amrr_node *amn = ATH_NODE_AMRR(an);
	int sr = ts->ts_shortretry;
	int lr = ts->ts_longretry;
	int retry_count = sr + lr;

	amn->amn_tx_try0_cnt++;
	if (retry_count == 1) {
		amn->amn_tx_try1_cnt++;
	} else if (retry_count == 2) {
		amn->amn_tx_try1_cnt++;
		amn->amn_tx_try2_cnt++;
	} else if (retry_count == 3) {
		amn->amn_tx_try1_cnt++;
		amn->amn_tx_try2_cnt++;
		amn->amn_tx_try3_cnt++;
	} else if (retry_count > 3) {
		amn->amn_tx_try1_cnt++;
		amn->amn_tx_try2_cnt++;
		amn->amn_tx_try3_cnt++;
		amn->amn_tx_failure_cnt++;
	}
	if (amn->amn_interval != 0 &&
	    ticks - amn->amn_ticks > amn->amn_interval) {
		ath_rate_ctl(sc, &an->an_node);
		amn->amn_ticks = ticks;
	}
}

void
ath_rate_newassoc(struct ath_softc *sc, struct ath_node *an, int isnew)
{
	if (isnew)
		ath_rate_ctl_start(sc, &an->an_node);
}

static void 
node_reset(struct amrr_node *amn)
{
	amn->amn_tx_try0_cnt = 0;
	amn->amn_tx_try1_cnt = 0;
	amn->amn_tx_try2_cnt = 0;
	amn->amn_tx_try3_cnt = 0;
	amn->amn_tx_failure_cnt = 0;
  	amn->amn_success = 0;
  	amn->amn_recovery = 0;
  	amn->amn_success_threshold = ath_rate_min_success_threshold;
}


/**
 * The code below assumes that we are dealing with hardware multi rate retry
 * I have no idea what will happen if you try to use this module with another
 * type of hardware. Your machine might catch fire or it might work with
 * horrible performance...
 */
static void
ath_rate_update(struct ath_softc *sc, struct ieee80211_node *ni, int rate)
{
	struct ath_node *an = ATH_NODE(ni);
	struct amrr_node *amn = ATH_NODE_AMRR(an);
	struct ieee80211vap *vap = ni->ni_vap;
	const HAL_RATE_TABLE *rt = sc->sc_currates;
	u_int8_t rix;

	KASSERT(rt != NULL, ("no rate table, mode %u", sc->sc_curmode));

	IEEE80211_NOTE(vap, IEEE80211_MSG_RATECTL, ni,
	    "%s: set xmit rate to %dM", __func__, 
	    ni->ni_rates.rs_nrates > 0 ?
		(ni->ni_rates.rs_rates[rate] & IEEE80211_RATE_VAL) / 2 : 0);

	amn->amn_rix = rate;
	/*
	 * Before associating a node has no rate set setup
	 * so we can't calculate any transmit codes to use.
	 * This is ok since we should never be sending anything
	 * but management frames and those always go at the
	 * lowest hardware rate.
	 */
	if (ni->ni_rates.rs_nrates > 0) {
		ni->ni_txrate = ni->ni_rates.rs_rates[rate] & IEEE80211_RATE_VAL;
		amn->amn_tx_rix0 = sc->sc_rixmap[ni->ni_txrate];
		amn->amn_tx_rate0 = rt->info[amn->amn_tx_rix0].rateCode;
		amn->amn_tx_rate0sp = amn->amn_tx_rate0 |
			rt->info[amn->amn_tx_rix0].shortPreamble;
		if (sc->sc_mrretry) {
			amn->amn_tx_try0 = 1;
			amn->amn_tx_try1 = 1;
			amn->amn_tx_try2 = 1;
			amn->amn_tx_try3 = 1;
			if (--rate >= 0) {
				rix = sc->sc_rixmap[
						    ni->ni_rates.rs_rates[rate]&IEEE80211_RATE_VAL];
				amn->amn_tx_rate1 = rt->info[rix].rateCode;
				amn->amn_tx_rate1sp = amn->amn_tx_rate1 |
					rt->info[rix].shortPreamble;
			} else {
				amn->amn_tx_rate1 = amn->amn_tx_rate1sp = 0;
			}
			if (--rate >= 0) {
				rix = sc->sc_rixmap[
						    ni->ni_rates.rs_rates[rate]&IEEE80211_RATE_VAL];
				amn->amn_tx_rate2 = rt->info[rix].rateCode;
				amn->amn_tx_rate2sp = amn->amn_tx_rate2 |
					rt->info[rix].shortPreamble;
			} else {
				amn->amn_tx_rate2 = amn->amn_tx_rate2sp = 0;
			}
			if (rate > 0) {
				/* NB: only do this if we didn't already do it above */
				amn->amn_tx_rate3 = rt->info[0].rateCode;
				amn->amn_tx_rate3sp =
					amn->amn_tx_rate3 | rt->info[0].shortPreamble;
			} else {
				amn->amn_tx_rate3 = amn->amn_tx_rate3sp = 0;
			}
		} else {
			amn->amn_tx_try0 = ATH_TXMAXTRY;
			/* theorically, these statements are useless because
			 *  the code which uses them tests for an_tx_try0 == ATH_TXMAXTRY
			 */
			amn->amn_tx_try1 = 0;
			amn->amn_tx_try2 = 0;
			amn->amn_tx_try3 = 0;
			amn->amn_tx_rate1 = amn->amn_tx_rate1sp = 0;
			amn->amn_tx_rate2 = amn->amn_tx_rate2sp = 0;
			amn->amn_tx_rate3 = amn->amn_tx_rate3sp = 0;
		}
	}
	node_reset(amn);

	amn->amn_interval = ath_rateinterval;
	if (vap->iv_opmode == IEEE80211_M_STA)
		amn->amn_interval /= 2;
	amn->amn_interval = (amn->amn_interval * hz) / 1000;
}

/*
 * Set the starting transmit rate for a node.
 */
static void
ath_rate_ctl_start(struct ath_softc *sc, struct ieee80211_node *ni)
{
#define	RATE(_ix)	(ni->ni_rates.rs_rates[(_ix)] & IEEE80211_RATE_VAL)
	const struct ieee80211_txparam *tp = ni->ni_txparms;
	int srate;

	KASSERT(ni->ni_rates.rs_nrates > 0, ("no rates"));
	if (tp == NULL || tp->ucastrate == IEEE80211_FIXED_RATE_NONE) {
		/*
		 * No fixed rate is requested. For 11b start with
		 * the highest negotiated rate; otherwise, for 11g
		 * and 11a, we start "in the middle" at 24Mb or 36Mb.
		 */
		srate = ni->ni_rates.rs_nrates - 1;
		if (sc->sc_curmode != IEEE80211_MODE_11B) {
			/*
			 * Scan the negotiated rate set to find the
			 * closest rate.
			 */
			/* NB: the rate set is assumed sorted */
			for (; srate >= 0 && RATE(srate) > 72; srate--)
				;
		}
	} else {
		/*
		 * A fixed rate is to be used; ic_fixed_rate is the
		 * IEEE code for this rate (sans basic bit).  Convert this
		 * to the index into the negotiated rate set for
		 * the node.  We know the rate is there because the
		 * rate set is checked when the station associates.
		 */
		/* NB: the rate set is assumed sorted */
		srate = ni->ni_rates.rs_nrates - 1;
		for (; srate >= 0 && RATE(srate) != tp->ucastrate; srate--)
			;
	}
	/*
	 * The selected rate may not be available due to races
	 * and mode settings.  Also orphaned nodes created in
	 * adhoc mode may not have any rate set so this lookup
	 * can fail.  This is not fatal.
	 */
	ath_rate_update(sc, ni, srate < 0 ? 0 : srate);
#undef RATE
}

/* 
 * Examine and potentially adjust the transmit rate.
 */
static void
ath_rate_ctl(void *arg, struct ieee80211_node *ni)
{
	struct ath_softc *sc = arg;
	struct amrr_node *amn = ATH_NODE_AMRR(ATH_NODE (ni));
	int rix;

#define is_success(amn) \
(amn->amn_tx_try1_cnt  < (amn->amn_tx_try0_cnt/10))
#define is_enough(amn) \
(amn->amn_tx_try0_cnt > 10)
#define is_failure(amn) \
(amn->amn_tx_try1_cnt > (amn->amn_tx_try0_cnt/3))

	rix = amn->amn_rix;
  
  	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_RATECTL, ni,
	    "cnt0: %d cnt1: %d cnt2: %d cnt3: %d -- threshold: %d",
	    amn->amn_tx_try0_cnt, amn->amn_tx_try1_cnt, amn->amn_tx_try2_cnt,
	    amn->amn_tx_try3_cnt, amn->amn_success_threshold);
  	if (is_success (amn) && is_enough (amn)) {
		amn->amn_success++;
		if (amn->amn_success == amn->amn_success_threshold &&
		    rix + 1 < ni->ni_rates.rs_nrates) {
  			amn->amn_recovery = 1;
  			amn->amn_success = 0;
  			rix++;
			IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_RATECTL, ni,
			    "increase rate to %d", rix);
  		} else {
			amn->amn_recovery = 0;
		}
  	} else if (is_failure (amn)) {
  		amn->amn_success = 0;
		if (rix > 0) {
  			if (amn->amn_recovery) {
  				/* recovery failure. */
  				amn->amn_success_threshold *= 2;
  				amn->amn_success_threshold = min (amn->amn_success_threshold,
								  (u_int)ath_rate_max_success_threshold);
				IEEE80211_NOTE(ni->ni_vap,
				    IEEE80211_MSG_RATECTL, ni,
				    "decrease rate recovery thr: %d",
				    amn->amn_success_threshold);
  			} else {
  				/* simple failure. */
 				amn->amn_success_threshold = ath_rate_min_success_threshold;
				IEEE80211_NOTE(ni->ni_vap,
				    IEEE80211_MSG_RATECTL, ni,
				    "decrease rate normal thr: %d",
				    amn->amn_success_threshold);
  			}
			amn->amn_recovery = 0;
  			rix--;
   		} else {
			amn->amn_recovery = 0;
		}

   	}
	if (is_enough (amn) || rix != amn->amn_rix) {
		/* reset counters. */
		amn->amn_tx_try0_cnt = 0;
		amn->amn_tx_try1_cnt = 0;
		amn->amn_tx_try2_cnt = 0;
		amn->amn_tx_try3_cnt = 0;
		amn->amn_tx_failure_cnt = 0;
	}
	if (rix != amn->amn_rix) {
		ath_rate_update(sc, ni, rix);
	}
}

static int
ath_rate_fetch_node_stats(struct ath_softc *sc, struct ath_node *an,
    struct ath_rateioctl *re)
{

	return (EINVAL);
}

static void
ath_rate_sysctlattach(struct ath_softc *sc)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(sc->sc_dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(sc->sc_dev);

	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"rate_interval", CTLFLAG_RW, &ath_rateinterval, 0,
		"rate control: operation interval (ms)");
	/* XXX bounds check values */
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"max_sucess_threshold", CTLFLAG_RW,
		&ath_rate_max_success_threshold, 0, "");
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		"min_sucess_threshold", CTLFLAG_RW,
		&ath_rate_min_success_threshold, 0, "");
}

struct ath_ratectrl *
ath_rate_attach(struct ath_softc *sc)
{
	struct amrr_softc *asc;

	asc = malloc(sizeof(struct amrr_softc), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (asc == NULL)
		return NULL;
	asc->arc.arc_space = sizeof(struct amrr_node);
	ath_rate_sysctlattach(sc);

	return &asc->arc;
}

void
ath_rate_detach(struct ath_ratectrl *arc)
{
	struct amrr_softc *asc = (struct amrr_softc *) arc;

	free(asc, M_DEVBUF);
}
