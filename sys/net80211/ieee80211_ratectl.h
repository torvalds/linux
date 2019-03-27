/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Rui Paulo <rpaulo@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

enum ieee80211_ratealgs {
	IEEE80211_RATECTL_AMRR		= 0,
	IEEE80211_RATECTL_RSSADAPT	= 1,
	IEEE80211_RATECTL_ONOE		= 2,
	IEEE80211_RATECTL_SAMPLE	= 3,
	IEEE80211_RATECTL_NONE		= 4,
	IEEE80211_RATECTL_MAX
};

/* used fields for tx_complete() events */
#define IEEE80211_RATECTL_STATUS_PKTLEN		0x00000001
#define IEEE80211_RATECTL_STATUS_FINAL_RATE	0x00000002
#define IEEE80211_RATECTL_STATUS_SHORT_RETRY	0x00000004
#define IEEE80211_RATECTL_STATUS_LONG_RETRY	0x00000008
#define IEEE80211_RATECTL_STATUS_RSSI		0x00000010

/* failure reason */
enum ieee80211_ratectl_tx_fail_reason {
	IEEE80211_RATECTL_TX_SUCCESS		= 0,
	IEEE80211_RATECTL_TX_FAIL_SHORT		= 1,	/* too many RTS retries */
	IEEE80211_RATECTL_TX_FAIL_LONG		= 2,	/* too many retries */
	IEEE80211_RATECTL_TX_FAIL_EXPIRED	= 3,	/* lifetime expired */
	IEEE80211_RATECTL_TX_FAIL_UNSPECIFIED	= 4,	/* another reason */
};
#define IEEE80211_RATECTL_TX_FAIL_MAX	\
	(IEEE80211_RATECTL_TX_FAIL_UNSPECIFIED + 1)

struct ieee80211_ratectl_tx_status {
	uint32_t	flags;		/* mark used fields */
	enum ieee80211_ratectl_tx_fail_reason status;	/* Tx status */

	int		pktlen;		/* frame length */
	int		final_rate;	/* transmission rate */
	uint_fast8_t	short_retries;	/* RTS/CTS retries */
	uint_fast8_t	long_retries;	/* ACK retries */
	int8_t		rssi;		/* ACK RSSI */

	uint8_t		spare[15];	/* for future use */
};

/* used fields for tx_update() events */
#define IEEE80211_RATECTL_TX_STATS_NODE		0x00000001
#define IEEE80211_RATECTL_TX_STATS_RETRIES	0x00000002

struct ieee80211_ratectl_tx_stats {
	uint32_t	flags;		/* mark used fields */

	struct ieee80211_node *ni;	/* receiver */
	int		nframes;	/* transmitted frames */
	int		nsuccess;	/* ACKed frames */
	int		nretries;	/* number of retries */
};

struct ieee80211_ratectl {
	const char *ir_name;
	int	(*ir_attach)(const struct ieee80211vap *);
	void	(*ir_detach)(const struct ieee80211vap *);
	void	(*ir_init)(struct ieee80211vap *);
	void	(*ir_deinit)(struct ieee80211vap *);
	void	(*ir_node_init)(struct ieee80211_node *);
	void	(*ir_node_deinit)(struct ieee80211_node *);
	int	(*ir_rate)(struct ieee80211_node *, void *, uint32_t);
	void	(*ir_tx_complete)(const struct ieee80211_node *,
	    			  const struct ieee80211_ratectl_tx_status *);
	void	(*ir_tx_update)(struct ieee80211vap *,
				struct ieee80211_ratectl_tx_stats *);
	void	(*ir_setinterval)(const struct ieee80211vap *, int);
	void	(*ir_node_stats)(struct ieee80211_node *ni, struct sbuf *s);
};

void	ieee80211_ratectl_register(int, const struct ieee80211_ratectl *);
void	ieee80211_ratectl_unregister(int);
void	ieee80211_ratectl_init(struct ieee80211vap *);
void	ieee80211_ratectl_set(struct ieee80211vap *, int);

MALLOC_DECLARE(M_80211_RATECTL);

static __inline void
ieee80211_ratectl_deinit(struct ieee80211vap *vap)
{
	vap->iv_rate->ir_deinit(vap);
}

static __inline void
ieee80211_ratectl_node_init(struct ieee80211_node *ni)
{
	const struct ieee80211vap *vap = ni->ni_vap;

	vap->iv_rate->ir_node_init(ni);
}

static __inline void
ieee80211_ratectl_node_deinit(struct ieee80211_node *ni)
{
	const struct ieee80211vap *vap = ni->ni_vap;

	vap->iv_rate->ir_node_deinit(ni);
}

static int __inline
ieee80211_ratectl_rate(struct ieee80211_node *ni, void *arg, uint32_t iarg)
{
	const struct ieee80211vap *vap = ni->ni_vap;

	return vap->iv_rate->ir_rate(ni, arg, iarg);
}

static __inline void
ieee80211_ratectl_tx_complete(const struct ieee80211_node *ni,
    const struct ieee80211_ratectl_tx_status *status)
{
	const struct ieee80211vap *vap = ni->ni_vap;

	vap->iv_rate->ir_tx_complete(ni, status);
}

static __inline void
ieee80211_ratectl_tx_update(struct ieee80211vap *vap,
    struct ieee80211_ratectl_tx_stats *stats)
{
	if (vap->iv_rate->ir_tx_update == NULL)
		return;
	vap->iv_rate->ir_tx_update(vap, stats);
}

static __inline void
ieee80211_ratectl_setinterval(const struct ieee80211vap *vap, int msecs)
{
	if (vap->iv_rate->ir_setinterval == NULL)
		return;
	vap->iv_rate->ir_setinterval(vap, msecs);
}

static __inline void
ieee80211_ratectl_node_stats(struct ieee80211_node *ni, struct sbuf *s)
{
	const struct ieee80211vap *vap = ni->ni_vap;

	if (vap->iv_rate->ir_node_stats == NULL)
		return;
	vap->iv_rate->ir_node_stats(ni, s);
}
