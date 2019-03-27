/*	$FreeBSD$	*/
/* $NetBSD: ieee80211_rssadapt.c,v 1.9 2005/02/26 22:45:09 perry Exp $ */
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2010 Rui Paulo <rpaulo@FreeBSD.org>
 * Copyright (c) 2003, 2004 David Young.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 * 3. The name of David Young may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY David Young ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL David
 * Young BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_media.h>
#include <net/ethernet.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_rssadapt.h>
#include <net80211/ieee80211_ratectl.h>

struct rssadapt_expavgctl {
	/* RSS threshold decay. */
	u_int rc_decay_denom;
	u_int rc_decay_old;
	/* RSS threshold update. */
	u_int rc_thresh_denom;
	u_int rc_thresh_old;
	/* RSS average update. */
	u_int rc_avgrssi_denom;
	u_int rc_avgrssi_old;
};

static struct rssadapt_expavgctl master_expavgctl = {
	.rc_decay_denom = 16,
	.rc_decay_old = 15,
	.rc_thresh_denom = 8,
	.rc_thresh_old = 4,
	.rc_avgrssi_denom = 8,
	.rc_avgrssi_old = 4
};

#ifdef interpolate
#undef interpolate
#endif
#define interpolate(parm, old, new) ((parm##_old * (old) + \
                                     (parm##_denom - parm##_old) * (new)) / \
				    parm##_denom)

static void	rssadapt_setinterval(const struct ieee80211vap *, int);
static void	rssadapt_init(struct ieee80211vap *);
static void	rssadapt_deinit(struct ieee80211vap *);
static void	rssadapt_updatestats(struct ieee80211_rssadapt_node *);
static void	rssadapt_node_init(struct ieee80211_node *);
static void	rssadapt_node_deinit(struct ieee80211_node *);
static int	rssadapt_rate(struct ieee80211_node *, void *, uint32_t);
static void	rssadapt_lower_rate(struct ieee80211_rssadapt_node *, int, int);
static void	rssadapt_raise_rate(struct ieee80211_rssadapt_node *,
			int, int);
static void	rssadapt_tx_complete(const struct ieee80211_node *,
			const struct ieee80211_ratectl_tx_status *);
static void	rssadapt_sysctlattach(struct ieee80211vap *,
			struct sysctl_ctx_list *, struct sysctl_oid *);

/* number of references from net80211 layer */
static	int nrefs = 0;

static const struct ieee80211_ratectl rssadapt = {
	.ir_name	= "rssadapt",
	.ir_attach	= NULL,
	.ir_detach	= NULL,
	.ir_init	= rssadapt_init,
	.ir_deinit	= rssadapt_deinit,
	.ir_node_init	= rssadapt_node_init,
	.ir_node_deinit	= rssadapt_node_deinit,
	.ir_rate	= rssadapt_rate,
	.ir_tx_complete	= rssadapt_tx_complete,
	.ir_tx_update	= NULL,
	.ir_setinterval	= rssadapt_setinterval,
};
IEEE80211_RATECTL_MODULE(rssadapt, 1);
IEEE80211_RATECTL_ALG(rssadapt, IEEE80211_RATECTL_RSSADAPT, rssadapt);

static void
rssadapt_setinterval(const struct ieee80211vap *vap, int msecs)
{
	struct ieee80211_rssadapt *rs = vap->iv_rs;

	if (!rs)
		return;

	if (msecs < 100)
		msecs = 100;
	rs->interval = msecs_to_ticks(msecs);
}

static void
rssadapt_init(struct ieee80211vap *vap)
{
	struct ieee80211_rssadapt *rs;

	KASSERT(vap->iv_rs == NULL, ("%s: iv_rs already initialized",
	    __func__));

	nrefs++;		/* XXX locking */
	vap->iv_rs = rs = IEEE80211_MALLOC(sizeof(struct ieee80211_rssadapt),
	    M_80211_RATECTL, IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
	if (rs == NULL) {
		if_printf(vap->iv_ifp, "couldn't alloc ratectl structure\n");
		return;
	}
	rs->vap = vap;
	rssadapt_setinterval(vap, 500 /* msecs */);
	rssadapt_sysctlattach(vap, vap->iv_sysctl, vap->iv_oid);
}

static void
rssadapt_deinit(struct ieee80211vap *vap)
{
	IEEE80211_FREE(vap->iv_rs, M_80211_RATECTL);
	KASSERT(nrefs > 0, ("imbalanced attach/detach"));
	nrefs--;		/* XXX locking */
}

static void
rssadapt_updatestats(struct ieee80211_rssadapt_node *ra)
{
	long interval;

	ra->ra_pktrate = (ra->ra_pktrate + 10*(ra->ra_nfail + ra->ra_nok))/2;
	ra->ra_nfail = ra->ra_nok = 0;

	/*
	 * A node is eligible for its rate to be raised every 1/10 to 10
	 * seconds, more eligible in proportion to recent packet rates.
	 */
	interval = MAX(10*1000, 10*1000 / MAX(1, 10 * ra->ra_pktrate));
	ra->ra_raise_interval = msecs_to_ticks(interval);
}

static void
rssadapt_node_init(struct ieee80211_node *ni)
{
	struct ieee80211_rssadapt_node *ra;
	struct ieee80211vap *vap = ni->ni_vap;
	struct ieee80211_rssadapt *rsa = vap->iv_rs;
	const struct ieee80211_rateset *rs = &ni->ni_rates;

	if (!rsa) {
		if_printf(vap->iv_ifp, "ratectl structure was not allocated, "
		    "per-node structure allocation skipped\n");
		return;
	}

	if (ni->ni_rctls == NULL) {
		ni->ni_rctls = ra = 
		    IEEE80211_MALLOC(sizeof(struct ieee80211_rssadapt_node),
		        M_80211_RATECTL, IEEE80211_M_NOWAIT | IEEE80211_M_ZERO);
		if (ra == NULL) {
			if_printf(vap->iv_ifp, "couldn't alloc per-node ratectl "
			    "structure\n");
			return;
		}
	} else
		ra = ni->ni_rctls;
	ra->ra_rs = rsa;
	ra->ra_rates = *rs;
	rssadapt_updatestats(ra);

	/* pick initial rate */
	for (ra->ra_rix = rs->rs_nrates - 1;
	     ra->ra_rix > 0 && (rs->rs_rates[ra->ra_rix] & IEEE80211_RATE_VAL) > 72;
	     ra->ra_rix--)
		;
	ni->ni_txrate = rs->rs_rates[ra->ra_rix] & IEEE80211_RATE_VAL;
	ra->ra_ticks = ticks;

	IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_RATECTL, ni,
	    "RSSADAPT initial rate %d", ni->ni_txrate);
}

static void
rssadapt_node_deinit(struct ieee80211_node *ni)
{

	IEEE80211_FREE(ni->ni_rctls, M_80211_RATECTL);
}

static __inline int
bucket(int pktlen)
{
	int i, top, thridx;

	for (i = 0, top = IEEE80211_RSSADAPT_BKT0;
	     i < IEEE80211_RSSADAPT_BKTS;
	     i++, top <<= IEEE80211_RSSADAPT_BKTPOWER) {
		thridx = i;
		if (pktlen <= top)
			break;
	}
	return thridx;
}

static int
rssadapt_rate(struct ieee80211_node *ni, void *arg __unused, uint32_t iarg)
{
	struct ieee80211_rssadapt_node *ra = ni->ni_rctls;
	u_int pktlen = iarg;
	const struct ieee80211_rateset *rs;
	uint16_t (*thrs)[IEEE80211_RATE_SIZE];
	int rix, rssi;

	/* XXX should return -1 here, but drivers may not expect this... */
	if (!ra)
	{
		ni->ni_txrate = ni->ni_rates.rs_rates[0];
		return 0;
	}

	rs = &ra->ra_rates;
	if ((ticks - ra->ra_ticks) > ra->ra_rs->interval) {
		rssadapt_updatestats(ra);
		ra->ra_ticks = ticks;
	}

	thrs = &ra->ra_rate_thresh[bucket(pktlen)];

	/* XXX this is average rssi, should be using last value */
	rssi = ni->ni_ic->ic_node_getrssi(ni);
	for (rix = rs->rs_nrates-1; rix >= 0; rix--)
		if ((*thrs)[rix] < (rssi << 8))
			break;
	if (rix != ra->ra_rix) {
		/* update public rate */
		ni->ni_txrate = ni->ni_rates.rs_rates[rix] & IEEE80211_RATE_VAL;
		ra->ra_rix = rix;

		IEEE80211_NOTE(ni->ni_vap, IEEE80211_MSG_RATECTL, ni,
		    "RSSADAPT new rate %d (pktlen %d rssi %d)",
		    ni->ni_txrate, pktlen, rssi);
	}
	return rix;
}

/*
 * Adapt the data rate to suit the conditions.  When a transmitted
 * packet is dropped after RAL_RSSADAPT_RETRY_LIMIT retransmissions,
 * raise the RSS threshold for transmitting packets of similar length at
 * the same data rate.
 */
static void
rssadapt_lower_rate(struct ieee80211_rssadapt_node *ra, int pktlen, int rssi)
{
	uint16_t last_thr;
	uint16_t (*thrs)[IEEE80211_RATE_SIZE];
	u_int rix;

	thrs = &ra->ra_rate_thresh[bucket(pktlen)];

	rix = ra->ra_rix;
	last_thr = (*thrs)[rix];
	(*thrs)[rix] = interpolate(master_expavgctl.rc_thresh,
	    last_thr, (rssi << 8));

	IEEE80211_DPRINTF(ra->ra_rs->vap, IEEE80211_MSG_RATECTL,
	    "RSSADAPT lower threshold for rate %d (last_thr %d new thr %d rssi %d)\n",
	    ra->ra_rates.rs_rates[rix + 1] & IEEE80211_RATE_VAL,
	    last_thr, (*thrs)[rix], rssi);
}

static void
rssadapt_raise_rate(struct ieee80211_rssadapt_node *ra, int pktlen, int rssi)
{
	uint16_t (*thrs)[IEEE80211_RATE_SIZE];
	uint16_t newthr, oldthr;
	int rix;

	thrs = &ra->ra_rate_thresh[bucket(pktlen)];

	rix = ra->ra_rix;
	if ((*thrs)[rix + 1] > (*thrs)[rix]) {
		oldthr = (*thrs)[rix + 1];
		if ((*thrs)[rix] == 0)
			newthr = (rssi << 8);
		else
			newthr = (*thrs)[rix];
		(*thrs)[rix + 1] = interpolate(master_expavgctl.rc_decay,
		    oldthr, newthr);

		IEEE80211_DPRINTF(ra->ra_rs->vap, IEEE80211_MSG_RATECTL,
		    "RSSADAPT raise threshold for rate %d (oldthr %d newthr %d rssi %d)\n",
		    ra->ra_rates.rs_rates[rix + 1] & IEEE80211_RATE_VAL,
		    oldthr, newthr, rssi);

		ra->ra_last_raise = ticks;
	}
}

static void
rssadapt_tx_complete(const struct ieee80211_node *ni,
    const struct ieee80211_ratectl_tx_status *status)
{
	struct ieee80211_rssadapt_node *ra = ni->ni_rctls;
	int pktlen, rssi;

	if (!ra)
		return;

	if ((status->flags &
	    (IEEE80211_RATECTL_STATUS_PKTLEN|IEEE80211_RATECTL_STATUS_RSSI)) !=
	    (IEEE80211_RATECTL_STATUS_PKTLEN|IEEE80211_RATECTL_STATUS_RSSI))
		return;

	pktlen = status->pktlen;
	rssi = status->rssi;

	if (status->status == IEEE80211_RATECTL_TX_SUCCESS) {
		ra->ra_nok++;
		if ((ra->ra_rix + 1) < ra->ra_rates.rs_nrates &&
		    (ticks - ra->ra_last_raise) >= ra->ra_raise_interval)
			rssadapt_raise_rate(ra, pktlen, rssi);
	} else {
		ra->ra_nfail++;
		rssadapt_lower_rate(ra, pktlen, rssi);
	}
}

static int
rssadapt_sysctl_interval(SYSCTL_HANDLER_ARGS)
{
	struct ieee80211vap *vap = arg1;
	struct ieee80211_rssadapt *rs = vap->iv_rs;
	int msecs, error;

	if (!rs)
		return ENOMEM;

	msecs = ticks_to_msecs(rs->interval);
	error = sysctl_handle_int(oidp, &msecs, 0, req);
	if (error || !req->newptr)
		return error;
	rssadapt_setinterval(vap, msecs);
	return 0;
}

static void
rssadapt_sysctlattach(struct ieee80211vap *vap,
    struct sysctl_ctx_list *ctx, struct sysctl_oid *tree)
{

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "rssadapt_rate_interval", CTLTYPE_INT | CTLFLAG_RW, vap,
	    0, rssadapt_sysctl_interval, "I", "rssadapt operation interval (ms)");
}
