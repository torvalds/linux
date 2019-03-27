/*	$OpenBSD: ieee80211_amrr.c,v 1.1 2006/06/17 19:07:19 damien Exp $	*/

/*-
 * Copyright (c) 2010 Rui Paulo <rpaulo@FreeBSD.org>
 * Copyright (c) 2006
 *	Damien Bergamini <damien.bergamini@free.fr>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*-
 * Naive implementation of the Adaptive Multi Rate Retry algorithm:
 *
 * "IEEE 802.11 Rate Adaptation: A Practical Approach"
 *  Mathieu Lacage, Hossein Manshaei, Thierry Turletti
 *  INRIA Sophia - Projet Planete
 *  http://www-sop.inria.fr/rapports/sophia/RR-5208.html
 */
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sbuf.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_ht.h>
#include <net80211/ieee80211_amrr.h>
#include <net80211/ieee80211_ratectl.h>

#define is_success(amn)	\
	((amn)->amn_retrycnt < (amn)->amn_txcnt / 10)
#define is_failure(amn)	\
	((amn)->amn_retrycnt > (amn)->amn_txcnt / 3)
#define is_enough(amn)		\
	((amn)->amn_txcnt > 10)

static void	amrr_setinterval(const struct ieee80211vap *, int);
static void	amrr_init(struct ieee80211vap *);
static void	amrr_deinit(struct ieee80211vap *);
static void	amrr_node_init(struct ieee80211_node *);
static void	amrr_node_deinit(struct ieee80211_node *);
static int	amrr_update(struct ieee80211_amrr *,
    			struct ieee80211_amrr_node *, struct ieee80211_node *);
static int	amrr_rate(struct ieee80211_node *, void *, uint32_t);
static void	amrr_tx_complete(const struct ieee80211_node *,
			const struct ieee80211_ratectl_tx_status *);
static void	amrr_tx_update_cb(void *, struct ieee80211_node *);
static void	amrr_tx_update(struct ieee80211vap *vap,
			struct ieee80211_ratectl_tx_stats *);
static void	amrr_sysctlattach(struct ieee80211vap *,
			struct sysctl_ctx_list *, struct sysctl_oid *);
static void	amrr_node_stats(struct ieee80211_node *ni, struct sbuf *s);

/* number of references from net80211 layer */
static	int nrefs = 0;

static const struct ieee80211_ratectl amrr = {
	.ir_name	= "amrr",
	.ir_attach	= NULL,
	.ir_detach	= NULL,
	.ir_init	= amrr_init,
	.ir_deinit	= amrr_deinit,
	.ir_node_init	= amrr_node_init,
	.ir_node_deinit	= amrr_node_deinit,
	.ir_rate	= amrr_rate,
	.ir_tx_complete	= amrr_tx_complete,
	.ir_tx_update	= amrr_tx_update,
	.ir_setinterval	= amrr_setinterval,
	.ir_node_stats	= amrr_node_stats,
};
IEEE80211_RATECTL_MODULE(amrr, 1);
IEEE80211_RATECTL_ALG(amrr, IEEE80211_RATECTL_AMRR, amrr);

static void
amrr_setinterval(const struct ieee80211vap *vap, int msecs)
{
	struct ieee80211_amrr *amrr = vap->iv_rs;

	if (!amrr)
		return;

	if (msecs < 100)
		msecs = 100;
	amrr->amrr_interval = msecs_to_ticks(msecs);
}

static void
amrr_init(struct ieee80211vap *vap)
{
	struct ieee80211_amrr *amrr;

	KASSERT(vap->iv_rs == NULL, ("%s called multiple times", __func__));

	nrefs++;		/* XXX locking */
	amrr = vap->iv_rs = IEEE80211_MALLOC(sizeof(struct ieee80211_amrr),
	    M_80211_RATECTL, IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (amrr == NULL) {
		if_printf(vap->iv_ifp, "couldn't alloc ratectl structure\n");
		return;
	}
	amrr->amrr_min_success_threshold = IEEE80211_AMRR_MIN_SUCCESS_THRESHOLD;
	amrr->amrr_max_success_threshold = IEEE80211_AMRR_MAX_SUCCESS_THRESHOLD;
	amrr_setinterval(vap, 500 /* ms */);
	amrr_sysctlattach(vap, vap->iv_sysctl, vap->iv_oid);
}

static void
amrr_deinit(struct ieee80211vap *vap)
{
	IEEE80211_FREE(vap->iv_rs, M_80211_RATECTL);
	KASSERT(nrefs > 0, ("imbalanced attach/detach"));
	nrefs--;		/* XXX locking */
}

/*
 * Return whether 11n rates are possible.
 *
 * Some 11n devices may return HT information but no HT rates.
 * Thus, we shouldn't treat them as an 11n node.
 */
static int
amrr_node_is_11n(struct ieee80211_node *ni)
{

	if (ni->ni_chan == NULL)
		return (0);
	if (ni->ni_chan == IEEE80211_CHAN_ANYC)
		return (0);
	if (IEEE80211_IS_CHAN_HT(ni->ni_chan) && ni->ni_htrates.rs_nrates == 0)
		return (0);
	return (IEEE80211_IS_CHAN_HT(ni->ni_chan));
}

static void
amrr_node_init(struct ieee80211_node *ni)
{
	const struct ieee80211_rateset *rs = NULL;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_amrr *amrr = vap->iv_rs;
	struct ieee80211_amrr_node *amn;
	uint8_t rate;

	if (!amrr) {
		if_printf(vap->iv_ifp, "ratectl structure was not allocated, "
		    "per-node structure allocation skipped\n");
		return;
	}

	if (ni->ni_rctls == NULL) {
		ni->ni_rctls = amn = IEEE80211_MALLOC(sizeof(struct ieee80211_amrr_node),
		    M_80211_RATECTL, IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
		if (amn == NULL) {
			if_printf(vap->iv_ifp, "couldn't alloc per-node ratectl "
			    "structure\n");
			return;
		}
	} else
		amn = ni->ni_rctls;
	amn->amn_amrr = amrr;
	amn->amn_success = 0;
	amn->amn_recovery = 0;
	amn->amn_txcnt = amn->amn_retrycnt = 0;
	amn->amn_success_threshold = amrr->amrr_min_success_threshold;

	/* 11n or not? Pick the right rateset */
	if (amrr_node_is_11n(ni)) {
		/* XXX ew */
		IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_RATECTL, ni,
		    "%s: 11n node", __func__);
		rs = (struct ieee80211_rateset *) &ni->ni_htrates;
	} else {
		IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_RATECTL, ni,
		    "%s: non-11n node", __func__);
		rs = &ni->ni_rates;
	}

	/* Initial rate - lowest */
	rate = rs->rs_rates[0];

	/* XXX clear the basic rate flag if it's not 11n */
	if (! amrr_node_is_11n(ni))
		rate &= IEEE80211_RATE_VAL;

	/* pick initial rate from the rateset - HT or otherwise */
	/* Pick something low that's likely to succeed */
	for (amn->amn_rix = rs->rs_nrates - 1; amn->amn_rix > 0;
	    amn->amn_rix--) {
		/* legacy - anything < 36mbit, stop searching */
		/* 11n - stop at MCS4 */
		if (amrr_node_is_11n(ni)) {
			if ((rs->rs_rates[amn->amn_rix] & 0x1f) < 4)
				break;
		} else if ((rs->rs_rates[amn->amn_rix] & IEEE80211_RATE_VAL) <= 72)
			break;
	}
	rate = rs->rs_rates[amn->amn_rix] & IEEE80211_RATE_VAL;

	/* if the rate is an 11n rate, ensure the MCS bit is set */
	if (amrr_node_is_11n(ni))
		rate |= IEEE80211_RATE_MCS;

	/* Assign initial rate from the rateset */
	ni->ni_txrate = rate;
	amn->amn_ticks = ticks;

	/* XXX TODO: we really need a rate-to-string method */
	/* XXX TODO: non-11n rate should be divided by two.. */
	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_RATECTL, ni,
	    "AMRR: nrates=%d, initial rate %s%d",
	    rs->rs_nrates,
	    amrr_node_is_11n(ni) ? "MCS " : "",
	    rate & IEEE80211_RATE_VAL);
}

static void
amrr_node_deinit(struct ieee80211_node *ni)
{
	IEEE80211_FREE(ni->ni_rctls, M_80211_RATECTL);
}

static int
amrr_update(struct ieee80211_amrr *amrr, struct ieee80211_amrr_node *amn,
    struct ieee80211_node *ni)
{
	int rix = amn->amn_rix;
	const struct ieee80211_rateset *rs = NULL;

	KASSERT(is_enough(amn), ("txcnt %d", amn->amn_txcnt));

	/* 11n or not? Pick the right rateset */
	if (amrr_node_is_11n(ni)) {
		/* XXX ew */
		rs = (struct ieee80211_rateset *) &ni->ni_htrates;
	} else {
		rs = &ni->ni_rates;
	}

	/* XXX TODO: we really need a rate-to-string method */
	/* XXX TODO: non-11n rate should be divided by two.. */
	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_RATECTL, ni,
	    "AMRR: current rate %d, txcnt=%d, retrycnt=%d",
	    rs->rs_rates[rix] & IEEE80211_RATE_VAL,
	    amn->amn_txcnt,
	    amn->amn_retrycnt);

	/*
	 * XXX This is totally bogus for 11n, as although high MCS
	 * rates for each stream may be failing, the next stream
	 * should be checked.
	 *
	 * Eg, if MCS5 is ok but MCS6/7 isn't, and we can go up to
	 * MCS23, we should skip 6/7 and try 8 onwards.
	 */
	if (is_success(amn)) {
		amn->amn_success++;
		if (amn->amn_success >= amn->amn_success_threshold &&
		    rix + 1 < rs->rs_nrates) {
			amn->amn_recovery = 1;
			amn->amn_success = 0;
			rix++;
			/* XXX TODO: we really need a rate-to-string method */
			/* XXX TODO: non-11n rate should be divided by two.. */
			IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_RATECTL, ni,
			    "AMRR increasing rate %d (txcnt=%d retrycnt=%d)",
			    rs->rs_rates[rix] & IEEE80211_RATE_VAL,
			    amn->amn_txcnt, amn->amn_retrycnt);
		} else {
			amn->amn_recovery = 0;
		}
	} else if (is_failure(amn)) {
		amn->amn_success = 0;
		if (rix > 0) {
			if (amn->amn_recovery) {
				amn->amn_success_threshold *= 2;
				if (amn->amn_success_threshold >
				    amrr->amrr_max_success_threshold)
					amn->amn_success_threshold =
					    amrr->amrr_max_success_threshold;
			} else {
				amn->amn_success_threshold =
				    amrr->amrr_min_success_threshold;
			}
			rix--;
			/* XXX TODO: we really need a rate-to-string method */
			/* XXX TODO: non-11n rate should be divided by two.. */
			IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_RATECTL, ni,
			    "AMRR decreasing rate %d (txcnt=%d retrycnt=%d)",
			    rs->rs_rates[rix] & IEEE80211_RATE_VAL,
			    amn->amn_txcnt, amn->amn_retrycnt);
		}
		amn->amn_recovery = 0;
	}

	/* reset counters */
	amn->amn_txcnt = 0;
	amn->amn_retrycnt = 0;

	return rix;
}

/*
 * Return the rate index to use in sending a data frame.
 * Update our internal state if it's been long enough.
 * If the rate changes we also update ni_txrate to match.
 */
static int
amrr_rate(struct ieee80211_node *ni, void *arg __unused, uint32_t iarg __unused)
{
	struct ieee80211_amrr_node *amn = ni->ni_rctls;
	struct ieee80211_amrr *amrr;
	const struct ieee80211_rateset *rs = NULL;
	int rix;

	/* XXX should return -1 here, but drivers may not expect this... */
	if (!amn)
	{
		ni->ni_txrate = ni->ni_rates.rs_rates[0];
		return 0;
	}

	amrr = amn->amn_amrr;

	/* 11n or not? Pick the right rateset */
	if (amrr_node_is_11n(ni)) {
		/* XXX ew */
		rs = (struct ieee80211_rateset *) &ni->ni_htrates;
	} else {
		rs = &ni->ni_rates;
	}

	if (is_enough(amn) && (ticks - amn->amn_ticks) > amrr->amrr_interval) {
		rix = amrr_update(amrr, amn, ni);
		if (rix != amn->amn_rix) {
			/* update public rate */
			ni->ni_txrate = rs->rs_rates[rix];
			/* XXX strip basic rate flag from txrate, if non-11n */
			if (amrr_node_is_11n(ni))
				ni->ni_txrate |= IEEE80211_RATE_MCS;
			else
				ni->ni_txrate &= IEEE80211_RATE_VAL;
			amn->amn_rix = rix;
		}
		amn->amn_ticks = ticks;
	} else
		rix = amn->amn_rix;
	return rix;
}

/*
 * Update statistics with tx complete status.  Ok is non-zero
 * if the packet is known to be ACK'd.  Retries has the number
 * retransmissions (i.e. xmit attempts - 1).
 */
static void
amrr_tx_complete(const struct ieee80211_node *ni,
    const struct ieee80211_ratectl_tx_status *status)
{
	struct ieee80211_amrr_node *amn = ni->ni_rctls;
	int retries;

	if (!amn)
		return;

	retries = 0;
	if (status->flags & IEEE80211_RATECTL_STATUS_LONG_RETRY)
		retries = status->long_retries;

	amn->amn_txcnt++;
	if (status->status == IEEE80211_RATECTL_TX_SUCCESS)
		amn->amn_success++;
	amn->amn_retrycnt += retries;
}

static void
amrr_tx_update_cb(void *arg, struct ieee80211_node *ni)
{
	struct ieee80211_ratectl_tx_stats *stats = arg;
	struct ieee80211_amrr_node *amn = ni->ni_rctls;
	int txcnt, success, retrycnt;

	if (!amn)
		return;

	txcnt = stats->nframes;
	success = stats->nsuccess;
	retrycnt = 0;
	if (stats->flags & IEEE80211_RATECTL_TX_STATS_RETRIES)
		retrycnt = stats->nretries;

	amn->amn_txcnt += txcnt;
	amn->amn_success += success;
	amn->amn_retrycnt += retrycnt;
}

/*
 * Set tx count/retry statistics explicitly.  Intended for
 * drivers that poll the device for statistics maintained
 * in the device.
 */
static void
amrr_tx_update(struct ieee80211vap *vap,
    struct ieee80211_ratectl_tx_stats *stats)
{

	if (stats->flags & IEEE80211_RATECTL_TX_STATS_NODE)
		amrr_tx_update_cb(stats, stats->ni);
	else {
		ieee80211_iterate_nodes_vap(&vap->iv_ic->ic_sta, vap,
		    amrr_tx_update_cb, stats);
	}
}

static int
amrr_sysctl_interval(SYSCTL_HANDLER_ARGS)
{
	struct ieee80211vap *vap = arg1;
	struct ieee80211_amrr *amrr = vap->iv_rs;
	int msecs, error;

	if (!amrr)
		return ENOMEM;

	msecs = ticks_to_msecs(amrr->amrr_interval);
	error = sysctl_handle_int(oidp, &msecs, 0, req);
	if (error || !req->newptr)
		return error;
	amrr_setinterval(vap, msecs);
	return 0;
}

static void
amrr_sysctlattach(struct ieee80211vap *vap,
    struct sysctl_ctx_list *ctx, struct sysctl_oid *tree)
{
	struct ieee80211_amrr *amrr = vap->iv_rs;

	if (!amrr)
		return;

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "amrr_rate_interval", CTLTYPE_INT | CTLFLAG_RW, vap,
	    0, amrr_sysctl_interval, "I", "amrr operation interval (ms)");
	/* XXX bounds check values */
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "amrr_max_sucess_threshold", CTLFLAG_RW,
	    &amrr->amrr_max_success_threshold, 0, "");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "amrr_min_sucess_threshold", CTLFLAG_RW,
	    &amrr->amrr_min_success_threshold, 0, "");
}

static void
amrr_node_stats(struct ieee80211_node *ni, struct sbuf *s)
{
	int rate;
	struct ieee80211_amrr_node *amn = ni->ni_rctls;
	struct ieee80211_rateset *rs;

	/* XXX TODO: check locking? */

	if (!amn)
		return;

	/* XXX TODO: this should be a method */
	if (amrr_node_is_11n(ni)) {
		rs = (struct ieee80211_rateset *) &ni->ni_htrates;
		rate = rs->rs_rates[amn->amn_rix] & IEEE80211_RATE_VAL;
		sbuf_printf(s, "rate: MCS %d\n", rate);
	} else {
		rs = &ni->ni_rates;
		rate = rs->rs_rates[amn->amn_rix] & IEEE80211_RATE_VAL;
		sbuf_printf(s, "rate: %d Mbit\n", rate / 2);
	}

	sbuf_printf(s, "ticks: %d\n", amn->amn_ticks);
	sbuf_printf(s, "txcnt: %u\n", amn->amn_txcnt);
	sbuf_printf(s, "success: %u\n", amn->amn_success);
	sbuf_printf(s, "success_threshold: %u\n", amn->amn_success_threshold);
	sbuf_printf(s, "recovery: %u\n", amn->amn_recovery);
	sbuf_printf(s, "retry_cnt: %u\n", amn->amn_retrycnt);
}
