/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef IF_RTWN_RIDX_H
#define IF_RTWN_RIDX_H

/* HW rate indices. */
#define RTWN_RIDX_CCK1		0
#define RTWN_RIDX_CCK2		1
#define RTWN_RIDX_CCK55		2
#define RTWN_RIDX_CCK11		3
#define RTWN_RIDX_OFDM6		4
#define RTWN_RIDX_OFDM9		5
#define RTWN_RIDX_OFDM12	6
#define RTWN_RIDX_OFDM18	7
#define RTWN_RIDX_OFDM24	8
#define RTWN_RIDX_OFDM36	9
#define RTWN_RIDX_OFDM48	10
#define RTWN_RIDX_OFDM54	11

#define RTWN_RIDX_HT_MCS_SHIFT	12
#define RTWN_RIDX_HT_MCS(i)	(RTWN_RIDX_HT_MCS_SHIFT + (i))

#define RTWN_RIDX_COUNT		28
#define RTWN_RIDX_UNKNOWN	(uint8_t)-1

#define RTWN_RATE_IS_CCK(rate)  ((rate) <= RTWN_RIDX_CCK11)
#define RTWN_RATE_IS_OFDM(rate) \
	((rate) >= RTWN_RIDX_OFDM6 && (rate) != RTWN_RIDX_UNKNOWN)


static const uint8_t ridx2rate[] =
	{ 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 };

static __inline uint8_t
rate2ridx(uint8_t rate)
{
	if (rate & IEEE80211_RATE_MCS) {
		return ((rate & 0xf) + RTWN_RIDX_HT_MCS_SHIFT);
	}
	switch (rate) {
	/* 11g */
	case 12:	return 4;
	case 18:	return 5;
	case 24:	return 6;
	case 36:	return 7;
	case 48:	return 8;
	case 72:	return 9;
	case 96:	return 10;
	case 108:	return 11;
	/* 11b */
	case 2:		return 0;
	case 4:		return 1;
	case 11:	return 2;
	case 22:	return 3;
	default:	return RTWN_RIDX_UNKNOWN;
	}
}

/* XXX move to net80211 */
static __inline__ uint8_t
rtwn_ctl_mcsrate(const struct ieee80211_rate_table *rt, uint8_t ridx)
{
	uint8_t cix, rate;

	/* Check if we are using MCS rate. */
	KASSERT(ridx >= RTWN_RIDX_HT_MCS(0) && ridx != RTWN_RIDX_UNKNOWN,
	    ("bad mcs rate index %d", ridx));

	rate = (ridx - RTWN_RIDX_HT_MCS(0)) | IEEE80211_RATE_MCS;
	cix = rt->info[rt->rateCodeToIndex[rate]].ctlRateIndex;
	KASSERT(cix != (uint8_t)-1, ("rate %d (%d) has no info", rate, ridx));
	return rt->info[cix].dot11Rate;
}

#endif	/* IF_RTWN_RIDX_H */
