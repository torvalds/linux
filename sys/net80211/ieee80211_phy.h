/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007-2008 Sam Leffler, Errno Consulting
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

#ifndef _NET80211_IEEE80211_PHY_H_
#define _NET80211_IEEE80211_PHY_H_

#ifdef _KERNEL
/*
 * IEEE 802.11 PHY-related definitions.
 */

/*
 * Contention window (slots).
 */
#define IEEE80211_CW_MAX	1023	/* aCWmax */
#define IEEE80211_CW_MIN_0	31	/* DS/CCK aCWmin, ERP aCWmin(0) */
#define IEEE80211_CW_MIN_1	15	/* OFDM aCWmin, ERP aCWmin(1) */

/*
 * SIFS (microseconds).
 */
#define IEEE80211_DUR_SIFS	10	/* DS/CCK/ERP SIFS */
#define IEEE80211_DUR_OFDM_SIFS	16	/* OFDM SIFS */

/*
 * Slot time (microseconds).
 */
#define IEEE80211_DUR_SLOT	20	/* DS/CCK slottime, ERP long slottime */
#define IEEE80211_DUR_SHSLOT	9	/* ERP short slottime */
#define IEEE80211_DUR_OFDM_SLOT	9	/* OFDM slottime */

#define IEEE80211_GET_SLOTTIME(ic) \
	((ic->ic_flags & IEEE80211_F_SHSLOT) ? \
	    IEEE80211_DUR_SHSLOT : IEEE80211_DUR_SLOT)

/*
 * DIFS (microseconds).
 */
#define IEEE80211_DUR_DIFS(sifs, slot)	((sifs) + 2 * (slot))

struct ieee80211_channel;

#define	IEEE80211_RATE_TABLE_SIZE	128

struct ieee80211_rate_table {
	int		rateCount;		/* NB: for proper padding */
	uint8_t		rateCodeToIndex[256];	/* back mapping */
	struct {
		uint8_t		phy;		/* CCK/OFDM/TURBO */
		uint32_t	rateKbps;	/* transfer rate in kbs */
		uint8_t		shortPreamble;	/* mask for enabling short
						 * preamble in CCK rate code */
		uint8_t		dot11Rate;	/* value for supported rates
						 * info element of MLME */
		uint8_t		ctlRateIndex;	/* index of next lower basic
						 * rate; used for dur. calcs */
		uint16_t	lpAckDuration;	/* long preamble ACK dur. */
		uint16_t	spAckDuration;	/* short preamble ACK dur. */
	} info[IEEE80211_RATE_TABLE_SIZE];
};

const struct ieee80211_rate_table *ieee80211_get_ratetable(
			struct ieee80211_channel *);

static __inline__ uint8_t
ieee80211_ack_rate(const struct ieee80211_rate_table *rt, uint8_t rate)
{
	/*
	 * XXX Assert this is for a legacy rate; not for an MCS rate.
	 * If the caller wishes to use it for a basic rate, they should
	 * clear the high bit first.
	 */
	KASSERT(! (rate & 0x80), ("rate %d is basic/mcs?", rate));

	uint8_t cix = rt->info[rt->rateCodeToIndex[rate & IEEE80211_RATE_VAL]].ctlRateIndex;
	KASSERT(cix != (uint8_t)-1, ("rate %d has no info", rate));
	return rt->info[cix].dot11Rate;
}

static __inline__ uint8_t
ieee80211_ctl_rate(const struct ieee80211_rate_table *rt, uint8_t rate)
{
	/*
	 * XXX Assert this is for a legacy rate; not for an MCS rate.
	 * If the caller wishes to use it for a basic rate, they should
	 * clear the high bit first.
	 */
	KASSERT(! (rate & 0x80), ("rate %d is basic/mcs?", rate));

	uint8_t cix = rt->info[rt->rateCodeToIndex[rate & IEEE80211_RATE_VAL]].ctlRateIndex;
	KASSERT(cix != (uint8_t)-1, ("rate %d has no info", rate));
	return rt->info[cix].dot11Rate;
}

static __inline__ enum ieee80211_phytype
ieee80211_rate2phytype(const struct ieee80211_rate_table *rt, uint8_t rate)
{
	/*
	 * XXX Assert this is for a legacy rate; not for an MCS rate.
	 * If the caller wishes to use it for a basic rate, they should
	 * clear the high bit first.
	 */
	KASSERT(! (rate & 0x80), ("rate %d is basic/mcs?", rate));

	uint8_t rix = rt->rateCodeToIndex[rate & IEEE80211_RATE_VAL];
	KASSERT(rix != (uint8_t)-1, ("rate %d has no info", rate));
	return rt->info[rix].phy;
}

static __inline__ int
ieee80211_isratevalid(const struct ieee80211_rate_table *rt, uint8_t rate)
{
	/*
	 * XXX Assert this is for a legacy rate; not for an MCS rate.
	 * If the caller wishes to use it for a basic rate, they should
	 * clear the high bit first.
	 */
	KASSERT(! (rate & 0x80), ("rate %d is basic/mcs?", rate));

	return rt->rateCodeToIndex[rate] != (uint8_t)-1;
}

/*
 * Calculate ACK field for
 * o  non-fragment data frames
 * o  management frames
 * sent using rate, phy and short preamble setting.
 */
static __inline__ uint16_t
ieee80211_ack_duration(const struct ieee80211_rate_table *rt,
    uint8_t rate, int isShortPreamble)
{
	uint8_t rix = rt->rateCodeToIndex[rate];

	KASSERT(rix != (uint8_t)-1, ("rate %d has no info", rate));
	if (isShortPreamble) {
		KASSERT(rt->info[rix].spAckDuration != 0,
			("shpreamble ack dur is not computed!\n"));
		return rt->info[rix].spAckDuration;
	} else {
		KASSERT(rt->info[rix].lpAckDuration != 0,
			("lgpreamble ack dur is not computed!\n"));
		return rt->info[rix].lpAckDuration;
	}
}

static __inline__ uint8_t
ieee80211_legacy_rate_lookup(const struct ieee80211_rate_table *rt,
    uint8_t rate)
{

	return (rt->rateCodeToIndex[rate & IEEE80211_RATE_VAL]);
}

/*
 * Compute the time to transmit a frame of length frameLen bytes
 * using the specified 802.11 rate code, phy, and short preamble
 * setting.
 *
 * NB: SIFS is included.
 */
uint16_t	ieee80211_compute_duration(const struct ieee80211_rate_table *,
			uint32_t frameLen, uint16_t rate, int isShortPreamble);
/*
 * Convert PLCP signal/rate field to 802.11 rate code (.5Mbits/s)
 */
uint8_t		ieee80211_plcp2rate(uint8_t, enum ieee80211_phytype);
/*
 * Convert 802.11 rate code to PLCP signal.
 */
uint8_t		ieee80211_rate2plcp(int, enum ieee80211_phytype);

/*
 * 802.11n rate manipulation.
 */

#define	IEEE80211_HT_RC_2_MCS(_rc)	((_rc) & 0x1f)
#define	IEEE80211_HT_RC_2_STREAMS(_rc)	((((_rc) & 0x78) >> 3) + 1)
#define	IEEE80211_IS_HT_RATE(_rc)		( (_rc) & IEEE80211_RATE_MCS)

uint32_t	ieee80211_compute_duration_ht(uint32_t frameLen,
			uint16_t rate, int streams, int isht40,
			int isShortGI);

#endif	/* _KERNEL */
#endif	/* !_NET80211_IEEE80211_PHY_H_ */
