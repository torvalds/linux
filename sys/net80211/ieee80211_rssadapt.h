/* $OpenBSD: ieee80211_rssadapt.h,v 1.5 2010/07/17 16:25:09 damien Exp $ */
/* $NetBSD: ieee80211_rssadapt.h,v 1.3 2004/05/06 03:03:20 dyoung Exp $ */

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
#ifndef _NET80211_IEEE80211_RSSADAPT_H_
#define _NET80211_IEEE80211_RSSADAPT_H_

/* Data-rate adaptation loosely based on "Link Adaptation Strategy
 * for IEEE 802.11 WLAN via Received Signal Strength Measurement"
 * by Javier del Prado Pavon and Sunghyun Choi.
 */

/* Buckets for frames 0-128 bytes long, 129-1024, 1025-maximum. */
#define	IEEE80211_RSSADAPT_BKTS		3
#define IEEE80211_RSSADAPT_BKT0		128
#define	IEEE80211_RSSADAPT_BKTPOWER	3	/* 2**_BKTPOWER */

#define	ieee80211_rssadapt_thresh_new \
    (ieee80211_rssadapt_thresh_denom - ieee80211_rssadapt_thresh_old)
#define	ieee80211_rssadapt_decay_new \
    (ieee80211_rssadapt_decay_denom - ieee80211_rssadapt_decay_old)
#define	ieee80211_rssadapt_avgrssi_new \
    (ieee80211_rssadapt_avgrssi_denom - ieee80211_rssadapt_avgrssi_old)

struct ieee80211_rssadapt_expavgctl {
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

struct ieee80211_rssadapt {
	/* exponential average RSSI << 8 */
	u_int16_t		ra_avg_rssi;
	/* Tx failures in this update interval */
	u_int32_t		ra_nfail;
	/* Tx successes in this update interval */
	u_int32_t		ra_nok;
	/* exponential average packets/second */
	u_int32_t		ra_pktrate;
	/* RSSI threshold for each Tx rate */
	u_int16_t		ra_rate_thresh[IEEE80211_RSSADAPT_BKTS]
				    [IEEE80211_RATE_SIZE];
	struct timeval		ra_last_raise;
	struct timeval		ra_raise_interval;
};

/* Properties of a Tx packet, for link adaptation. */
struct ieee80211_rssdesc {
	u_int			 id_len;	/* Tx packet length */
	u_int			 id_rateidx;	/* index into ni->ni_rates */
	struct ieee80211_node	*id_node;	/* destination STA MAC */
	u_int8_t		 id_rssi;	/* destination STA avg RSS @
						 * Tx time
						 */
};

void	ieee80211_rssadapt_updatestats(struct ieee80211_rssadapt *);
void	ieee80211_rssadapt_input(struct ieee80211com *,
	    const struct ieee80211_node *, struct ieee80211_rssadapt *, int);
void	ieee80211_rssadapt_lower_rate(struct ieee80211com *,
	    const struct ieee80211_node *, struct ieee80211_rssadapt *,
	    const struct ieee80211_rssdesc *);
void	ieee80211_rssadapt_raise_rate(struct ieee80211com *,
	    struct ieee80211_rssadapt *, const struct ieee80211_rssdesc *);
int	ieee80211_rssadapt_choose(struct ieee80211_rssadapt *,
	    const struct ieee80211_rateset *, const struct ieee80211_frame *,
	    u_int, int, const char *, int);
#ifdef IEEE80211_DEBUG
extern int ieee80211_rssadapt_debug;
#endif /* IEEE80211_DEBUG */

#endif /* _NET80211_IEEE80211_RSSADAPT_H_ */
