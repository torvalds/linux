/*	$OpenBSD: ieee80211_rssadapt.c,v 1.11 2014/12/23 03:24:08 tedu Exp $	*/
/*	$NetBSD: ieee80211_rssadapt.c,v 1.7 2004/05/25 04:33:59 dyoung Exp $	*/

/*-
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_rssadapt.h>

#ifdef interpolate
#undef interpolate
#endif
#define interpolate(parm, old, new)				\
	((parm##_old * (old) +					\
	(parm##_denom - parm##_old) * (new)) / parm##_denom)

#ifdef IEEE80211_DEBUG
static	struct timeval lastrateadapt;	/* time of last rate adaptation msg */
static	int currssadaptps = 0;		/* rate-adaptation msgs this second */
static	int ieee80211_adaptrate = 4;	/* rate-adaptation max msgs/sec */

#define RSSADAPT_DO_PRINT() \
	((ieee80211_rssadapt_debug > 0) && \
	 ppsratecheck(&lastrateadapt, &currssadaptps, ieee80211_adaptrate))
#define	RSSADAPT_PRINTF(X) \
	if (RSSADAPT_DO_PRINT()) \
		printf X

int ieee80211_rssadapt_debug = 0;

#else
#define	RSSADAPT_DO_PRINT() (0)
#define	RSSADAPT_PRINTF(X)
#endif

static const struct ieee80211_rssadapt_expavgctl master_expavgctl = {
	.rc_decay_denom		= 16,
	.rc_decay_old		= 15,
	.rc_thresh_denom	= 8,
	.rc_thresh_old		= 4,
	.rc_avgrssi_denom	= 8,
	.rc_avgrssi_old		= 4
};

int
ieee80211_rssadapt_choose(struct ieee80211_rssadapt *ra,
    const struct ieee80211_rateset *rs, const struct ieee80211_frame *wh,
    u_int len, int fixed_rate, const char *dvname, int do_not_adapt)
{
	u_int16_t (*thrs)[IEEE80211_RATE_SIZE];
	int flags = 0, i, rateidx = 0, thridx, top;

	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) == IEEE80211_FC0_TYPE_CTL)
		flags |= IEEE80211_RATE_BASIC;

	for (i = 0, top = IEEE80211_RSSADAPT_BKT0;
	     i < IEEE80211_RSSADAPT_BKTS;
	     i++, top <<= IEEE80211_RSSADAPT_BKTPOWER) {
		thridx = i;
		if (len <= top)
			break;
	}

	thrs = &ra->ra_rate_thresh[thridx];

	if (fixed_rate != -1) {
		if ((rs->rs_rates[fixed_rate] & flags) == flags) {
			rateidx = fixed_rate;
			goto out;
		}
		flags |= IEEE80211_RATE_BASIC;
		i = fixed_rate;
	} else
		i = rs->rs_nrates;

	while (--i >= 0) {
		rateidx = i;
		if ((rs->rs_rates[i] & flags) != flags)
			continue;
		if (do_not_adapt)
			break;
		if ((*thrs)[i] < ra->ra_avg_rssi)
			break;
	}

out:
#ifdef IEEE80211_DEBUG
	if (ieee80211_rssadapt_debug && dvname != NULL) {
		printf("%s: dst %s threshold[%d, %d.%d] %d < %d\n",
		    dvname, ether_sprintf((u_int8_t *)wh->i_addr1), len,
		    (rs->rs_rates[rateidx] & IEEE80211_RATE_VAL) / 2,
		    (rs->rs_rates[rateidx] & IEEE80211_RATE_VAL) * 5 % 10,
		    (*thrs)[rateidx], ra->ra_avg_rssi);
	}
#endif /* IEEE80211_DEBUG */
	return rateidx;
}

void
ieee80211_rssadapt_updatestats(struct ieee80211_rssadapt *ra)
{
	long interval;

	ra->ra_pktrate =
	    (ra->ra_pktrate + 10 * (ra->ra_nfail + ra->ra_nok)) / 2;
	ra->ra_nfail = ra->ra_nok = 0;

	/* a node is eligible for its rate to be raised every 1/10 to 10
	 * seconds, more eligible in proportion to recent packet rates.
	 */
	interval = MAX(100000, 10000000 / MAX(1, 10 * ra->ra_pktrate));
	ra->ra_raise_interval.tv_sec = interval / (1000 * 1000);
	ra->ra_raise_interval.tv_usec = interval % (1000 * 1000);
}

void
ieee80211_rssadapt_input(struct ieee80211com *ic,
    const struct ieee80211_node *ni, struct ieee80211_rssadapt *ra, int rssi)
{
#ifdef IEEE80211_DEBUG
	int last_avg_rssi = ra->ra_avg_rssi;
#endif

	ra->ra_avg_rssi = interpolate(master_expavgctl.rc_avgrssi,
	    ra->ra_avg_rssi, (rssi << 8));

	RSSADAPT_PRINTF(("%s: src %s rssi %d avg %d -> %d\n",
	    ic->ic_if.if_xname, ether_sprintf((u_int8_t *)ni->ni_macaddr),
	    rssi, last_avg_rssi, ra->ra_avg_rssi));
}

/*
 * Adapt the data rate to suit the conditions.  When a transmitted
 * packet is dropped after IEEE80211_RSSADAPT_RETRY_LIMIT retransmissions,
 * raise the RSS threshold for transmitting packets of similar length at
 * the same data rate.
 */
void
ieee80211_rssadapt_lower_rate(struct ieee80211com *ic,
    const struct ieee80211_node *ni, struct ieee80211_rssadapt *ra,
    const struct ieee80211_rssdesc *id)
{
	const struct ieee80211_rateset *rs = &ni->ni_rates;
	u_int16_t last_thr;
	u_int i, thridx, top;

	ra->ra_nfail++;

	if (id->id_rateidx >= rs->rs_nrates) {
		RSSADAPT_PRINTF(("ieee80211_rssadapt_lower_rate: "
		    "%s rate #%d > #%d out of bounds\n",
		    ether_sprintf((u_int8_t *)ni->ni_macaddr), id->id_rateidx,
		    rs->rs_nrates - 1));
		return;
	}

	for (i = 0, top = IEEE80211_RSSADAPT_BKT0;
	     i < IEEE80211_RSSADAPT_BKTS;
	     i++, top <<= IEEE80211_RSSADAPT_BKTPOWER) {
		thridx = i;
		if (id->id_len <= top)
			break;
	}

	last_thr = ra->ra_rate_thresh[thridx][id->id_rateidx];
	ra->ra_rate_thresh[thridx][id->id_rateidx] =
	    interpolate(master_expavgctl.rc_thresh, last_thr,
	    (id->id_rssi << 8));

	RSSADAPT_PRINTF(("%s: dst %s rssi %d threshold[%d, %d.%d] %d -> %d\n",
	    ic->ic_if.if_xname, ether_sprintf((u_int8_t *)ni->ni_macaddr),
	    id->id_rssi, id->id_len,
	    (rs->rs_rates[id->id_rateidx] & IEEE80211_RATE_VAL) / 2,
	    (rs->rs_rates[id->id_rateidx] & IEEE80211_RATE_VAL) * 5 % 10,
	    last_thr, ra->ra_rate_thresh[thridx][id->id_rateidx]));
}

void
ieee80211_rssadapt_raise_rate(struct ieee80211com *ic,
    struct ieee80211_rssadapt *ra, const struct ieee80211_rssdesc *id)
{
	u_int16_t (*thrs)[IEEE80211_RATE_SIZE], newthr, oldthr;
	const struct ieee80211_node *ni = id->id_node;
	const struct ieee80211_rateset *rs = &ni->ni_rates;
	int i, rate, top;
#ifdef IEEE80211_DEBUG
	int j;
#endif

	ra->ra_nok++;

	if (!ratecheck(&ra->ra_last_raise, &ra->ra_raise_interval))
		return;

	for (i = 0, top = IEEE80211_RSSADAPT_BKT0;
	     i < IEEE80211_RSSADAPT_BKTS;
	     i++, top <<= IEEE80211_RSSADAPT_BKTPOWER) {
		thrs = &ra->ra_rate_thresh[i];
		if (id->id_len <= top)
			break;
	}

	if (id->id_rateidx + 1 < rs->rs_nrates &&
	    (*thrs)[id->id_rateidx + 1] > (*thrs)[id->id_rateidx]) {
		rate = (rs->rs_rates[id->id_rateidx + 1] & IEEE80211_RATE_VAL);

		RSSADAPT_PRINTF(("%s: threshold[%d, %d.%d] decay %d ",
		    ic->ic_if.if_xname, IEEE80211_RSSADAPT_BKT0 <<
			(IEEE80211_RSSADAPT_BKTPOWER * i),
		    rate / 2, rate * 5 % 10, (*thrs)[id->id_rateidx + 1]));
		oldthr = (*thrs)[id->id_rateidx + 1];
		if ((*thrs)[id->id_rateidx] == 0)
			newthr = ra->ra_avg_rssi;
		else
			newthr = (*thrs)[id->id_rateidx];
		(*thrs)[id->id_rateidx + 1] =
		    interpolate(master_expavgctl.rc_decay, oldthr, newthr);

		RSSADAPT_PRINTF(("-> %d\n", (*thrs)[id->id_rateidx + 1]));
	}

#ifdef IEEE80211_DEBUG
	if (RSSADAPT_DO_PRINT()) {
		printf("%s: dst %s thresholds\n", ic->ic_if.if_xname,
		    ether_sprintf((u_int8_t *)ni->ni_macaddr));
		for (i = 0; i < IEEE80211_RSSADAPT_BKTS; i++) {
			printf("%d-byte", IEEE80211_RSSADAPT_BKT0 <<
			    (IEEE80211_RSSADAPT_BKTPOWER * i));
			for (j = 0; j < rs->rs_nrates; j++) {
				rate = (rs->rs_rates[j] & IEEE80211_RATE_VAL);
				printf(", T[%d.%d] = %d", rate / 2,
				    rate * 5 % 10, ra->ra_rate_thresh[i][j]);
			}
			printf("\n");
		}
	}
#endif /* IEEE80211_DEBUG */
}
