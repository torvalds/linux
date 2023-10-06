// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2019 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2021-2022 Intel Corporation
 */

#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "sta_info.h"

#define AVG_PKT_SIZE	1024

/* Number of bits for an average sized packet */
#define MCS_NBITS (AVG_PKT_SIZE << 3)

/* Number of kilo-symbols (symbols * 1024) for a packet with (bps) bits per
 * symbol. We use k-symbols to avoid rounding in the _TIME macros below.
 */
#define MCS_N_KSYMS(bps) DIV_ROUND_UP(MCS_NBITS << 10, (bps))

/* Transmission time (in 1024 * usec) for a packet containing (ksyms) * 1024
 * symbols.
 */
#define MCS_SYMBOL_TIME(sgi, ksyms)					\
	(sgi ?								\
	  ((ksyms) * 4 * 18) / 20 :		/* 3.6 us per sym */	\
	  ((ksyms) * 4)			/* 4.0 us per sym */	\
	)

/* Transmit duration for the raw data part of an average sized packet */
#define MCS_DURATION(streams, sgi, bps) \
	((u32)MCS_SYMBOL_TIME(sgi, MCS_N_KSYMS((streams) * (bps))))

#define MCS_DURATION_S(shift, streams, sgi, bps)		\
	((u16)((MCS_DURATION(streams, sgi, bps) >> shift)))

/* These should match the values in enum nl80211_he_gi */
#define HE_GI_08 0
#define HE_GI_16 1
#define HE_GI_32 2

/* Transmission time (1024 usec) for a packet containing (ksyms) * k-symbols */
#define HE_SYMBOL_TIME(gi, ksyms)					\
	(gi == HE_GI_08 ?						\
	 ((ksyms) * 16 * 17) / 20 :		/* 13.6 us per sym */	\
	 (gi == HE_GI_16 ?						\
	  ((ksyms) * 16 * 18) / 20 :		/* 14.4 us per sym */	\
	  ((ksyms) * 16)			/* 16.0 us per sym */	\
	 ))

/* Transmit duration for the raw data part of an average sized packet */
#define HE_DURATION(streams, gi, bps) \
	((u32)HE_SYMBOL_TIME(gi, MCS_N_KSYMS((streams) * (bps))))

#define HE_DURATION_S(shift, streams, gi, bps)		\
	(HE_DURATION(streams, gi, bps) >> shift)

#define BW_20			0
#define BW_40			1
#define BW_80			2
#define BW_160			3

/*
 * Define group sort order: HT40 -> SGI -> #streams
 */
#define IEEE80211_MAX_STREAMS		4
#define IEEE80211_HT_STREAM_GROUPS	4 /* BW(=2) * SGI(=2) */
#define IEEE80211_VHT_STREAM_GROUPS	8 /* BW(=4) * SGI(=2) */

#define IEEE80211_HE_MAX_STREAMS	8

#define IEEE80211_HT_GROUPS_NB	(IEEE80211_MAX_STREAMS *	\
				 IEEE80211_HT_STREAM_GROUPS)
#define IEEE80211_VHT_GROUPS_NB	(IEEE80211_MAX_STREAMS *	\
					 IEEE80211_VHT_STREAM_GROUPS)

#define IEEE80211_HT_GROUP_0	0
#define IEEE80211_VHT_GROUP_0	(IEEE80211_HT_GROUP_0 + IEEE80211_HT_GROUPS_NB)
#define IEEE80211_HE_GROUP_0	(IEEE80211_VHT_GROUP_0 + IEEE80211_VHT_GROUPS_NB)

#define MCS_GROUP_RATES		12

#define HT_GROUP_IDX(_streams, _sgi, _ht40)	\
	IEEE80211_HT_GROUP_0 +			\
	IEEE80211_MAX_STREAMS * 2 * _ht40 +	\
	IEEE80211_MAX_STREAMS * _sgi +		\
	_streams - 1

#define _MAX(a, b) (((a)>(b))?(a):(b))

#define GROUP_SHIFT(duration)						\
	_MAX(0, 16 - __builtin_clz(duration))

/* MCS rate information for an MCS group */
#define __MCS_GROUP(_streams, _sgi, _ht40, _s)				\
	[HT_GROUP_IDX(_streams, _sgi, _ht40)] = {			\
	.shift = _s,							\
	.duration = {							\
		MCS_DURATION_S(_s, _streams, _sgi, _ht40 ? 54 : 26),	\
		MCS_DURATION_S(_s, _streams, _sgi, _ht40 ? 108 : 52),	\
		MCS_DURATION_S(_s, _streams, _sgi, _ht40 ? 162 : 78),	\
		MCS_DURATION_S(_s, _streams, _sgi, _ht40 ? 216 : 104),	\
		MCS_DURATION_S(_s, _streams, _sgi, _ht40 ? 324 : 156),	\
		MCS_DURATION_S(_s, _streams, _sgi, _ht40 ? 432 : 208),	\
		MCS_DURATION_S(_s, _streams, _sgi, _ht40 ? 486 : 234),	\
		MCS_DURATION_S(_s, _streams, _sgi, _ht40 ? 540 : 260)	\
	}								\
}

#define MCS_GROUP_SHIFT(_streams, _sgi, _ht40)				\
	GROUP_SHIFT(MCS_DURATION(_streams, _sgi, _ht40 ? 54 : 26))

#define MCS_GROUP(_streams, _sgi, _ht40)				\
	__MCS_GROUP(_streams, _sgi, _ht40,				\
		    MCS_GROUP_SHIFT(_streams, _sgi, _ht40))

#define VHT_GROUP_IDX(_streams, _sgi, _bw)				\
	(IEEE80211_VHT_GROUP_0 +					\
	 IEEE80211_MAX_STREAMS * 2 * (_bw) +				\
	 IEEE80211_MAX_STREAMS * (_sgi) +				\
	 (_streams) - 1)

#define BW2VBPS(_bw, r4, r3, r2, r1)					\
	(_bw == BW_160 ? r4 : _bw == BW_80 ? r3 : _bw == BW_40 ? r2 : r1)

#define __VHT_GROUP(_streams, _sgi, _bw, _s)				\
	[VHT_GROUP_IDX(_streams, _sgi, _bw)] = {			\
	.shift = _s,							\
	.duration = {							\
		MCS_DURATION_S(_s, _streams, _sgi,			\
			       BW2VBPS(_bw,  234,  117,  54,  26)),	\
		MCS_DURATION_S(_s, _streams, _sgi,			\
			       BW2VBPS(_bw,  468,  234, 108,  52)),	\
		MCS_DURATION_S(_s, _streams, _sgi,			\
			       BW2VBPS(_bw,  702,  351, 162,  78)),	\
		MCS_DURATION_S(_s, _streams, _sgi,			\
			       BW2VBPS(_bw,  936,  468, 216, 104)),	\
		MCS_DURATION_S(_s, _streams, _sgi,			\
			       BW2VBPS(_bw, 1404,  702, 324, 156)),	\
		MCS_DURATION_S(_s, _streams, _sgi,			\
			       BW2VBPS(_bw, 1872,  936, 432, 208)),	\
		MCS_DURATION_S(_s, _streams, _sgi,			\
			       BW2VBPS(_bw, 2106, 1053, 486, 234)),	\
		MCS_DURATION_S(_s, _streams, _sgi,			\
			       BW2VBPS(_bw, 2340, 1170, 540, 260)),	\
		MCS_DURATION_S(_s, _streams, _sgi,			\
			       BW2VBPS(_bw, 2808, 1404, 648, 312)),	\
		MCS_DURATION_S(_s, _streams, _sgi,			\
			       BW2VBPS(_bw, 3120, 1560, 720, 346))	\
        }								\
}

#define VHT_GROUP_SHIFT(_streams, _sgi, _bw)				\
	GROUP_SHIFT(MCS_DURATION(_streams, _sgi,			\
				 BW2VBPS(_bw, 243, 117,  54,  26)))

#define VHT_GROUP(_streams, _sgi, _bw)					\
	__VHT_GROUP(_streams, _sgi, _bw,				\
		    VHT_GROUP_SHIFT(_streams, _sgi, _bw))


#define HE_GROUP_IDX(_streams, _gi, _bw)				\
	(IEEE80211_HE_GROUP_0 +					\
	 IEEE80211_HE_MAX_STREAMS * 3 * (_bw) +			\
	 IEEE80211_HE_MAX_STREAMS * (_gi) +				\
	 (_streams) - 1)

#define __HE_GROUP(_streams, _gi, _bw, _s)				\
	[HE_GROUP_IDX(_streams, _gi, _bw)] = {			\
	.shift = _s,							\
	.duration = {							\
		HE_DURATION_S(_s, _streams, _gi,			\
			      BW2VBPS(_bw,   979,  489,  230,  115)),	\
		HE_DURATION_S(_s, _streams, _gi,			\
			      BW2VBPS(_bw,  1958,  979,  475,  230)),	\
		HE_DURATION_S(_s, _streams, _gi,			\
			      BW2VBPS(_bw,  2937, 1468,  705,  345)),	\
		HE_DURATION_S(_s, _streams, _gi,			\
			      BW2VBPS(_bw,  3916, 1958,  936,  475)),	\
		HE_DURATION_S(_s, _streams, _gi,			\
			      BW2VBPS(_bw,  5875, 2937, 1411,  705)),	\
		HE_DURATION_S(_s, _streams, _gi,			\
			      BW2VBPS(_bw,  7833, 3916, 1872,  936)),	\
		HE_DURATION_S(_s, _streams, _gi,			\
			      BW2VBPS(_bw,  8827, 4406, 2102, 1051)),	\
		HE_DURATION_S(_s, _streams, _gi,			\
			      BW2VBPS(_bw,  9806, 4896, 2347, 1166)),	\
		HE_DURATION_S(_s, _streams, _gi,			\
			      BW2VBPS(_bw, 11764, 5875, 2808, 1411)),	\
		HE_DURATION_S(_s, _streams, _gi,			\
			      BW2VBPS(_bw, 13060, 6523, 3124, 1555)),	\
		HE_DURATION_S(_s, _streams, _gi,			\
			      BW2VBPS(_bw, 14702, 7344, 3513, 1756)),	\
		HE_DURATION_S(_s, _streams, _gi,			\
			      BW2VBPS(_bw, 16329, 8164, 3902, 1944))	\
        }								\
}

#define HE_GROUP_SHIFT(_streams, _gi, _bw)				\
	GROUP_SHIFT(HE_DURATION(_streams, _gi,			\
				BW2VBPS(_bw,   979,  489,  230,  115)))

#define HE_GROUP(_streams, _gi, _bw)					\
	__HE_GROUP(_streams, _gi, _bw,				\
		   HE_GROUP_SHIFT(_streams, _gi, _bw))
struct mcs_group {
	u8 shift;
	u16 duration[MCS_GROUP_RATES];
};

static const struct mcs_group airtime_mcs_groups[] = {
	MCS_GROUP(1, 0, BW_20),
	MCS_GROUP(2, 0, BW_20),
	MCS_GROUP(3, 0, BW_20),
	MCS_GROUP(4, 0, BW_20),

	MCS_GROUP(1, 1, BW_20),
	MCS_GROUP(2, 1, BW_20),
	MCS_GROUP(3, 1, BW_20),
	MCS_GROUP(4, 1, BW_20),

	MCS_GROUP(1, 0, BW_40),
	MCS_GROUP(2, 0, BW_40),
	MCS_GROUP(3, 0, BW_40),
	MCS_GROUP(4, 0, BW_40),

	MCS_GROUP(1, 1, BW_40),
	MCS_GROUP(2, 1, BW_40),
	MCS_GROUP(3, 1, BW_40),
	MCS_GROUP(4, 1, BW_40),

	VHT_GROUP(1, 0, BW_20),
	VHT_GROUP(2, 0, BW_20),
	VHT_GROUP(3, 0, BW_20),
	VHT_GROUP(4, 0, BW_20),

	VHT_GROUP(1, 1, BW_20),
	VHT_GROUP(2, 1, BW_20),
	VHT_GROUP(3, 1, BW_20),
	VHT_GROUP(4, 1, BW_20),

	VHT_GROUP(1, 0, BW_40),
	VHT_GROUP(2, 0, BW_40),
	VHT_GROUP(3, 0, BW_40),
	VHT_GROUP(4, 0, BW_40),

	VHT_GROUP(1, 1, BW_40),
	VHT_GROUP(2, 1, BW_40),
	VHT_GROUP(3, 1, BW_40),
	VHT_GROUP(4, 1, BW_40),

	VHT_GROUP(1, 0, BW_80),
	VHT_GROUP(2, 0, BW_80),
	VHT_GROUP(3, 0, BW_80),
	VHT_GROUP(4, 0, BW_80),

	VHT_GROUP(1, 1, BW_80),
	VHT_GROUP(2, 1, BW_80),
	VHT_GROUP(3, 1, BW_80),
	VHT_GROUP(4, 1, BW_80),

	VHT_GROUP(1, 0, BW_160),
	VHT_GROUP(2, 0, BW_160),
	VHT_GROUP(3, 0, BW_160),
	VHT_GROUP(4, 0, BW_160),

	VHT_GROUP(1, 1, BW_160),
	VHT_GROUP(2, 1, BW_160),
	VHT_GROUP(3, 1, BW_160),
	VHT_GROUP(4, 1, BW_160),

	HE_GROUP(1, HE_GI_08, BW_20),
	HE_GROUP(2, HE_GI_08, BW_20),
	HE_GROUP(3, HE_GI_08, BW_20),
	HE_GROUP(4, HE_GI_08, BW_20),
	HE_GROUP(5, HE_GI_08, BW_20),
	HE_GROUP(6, HE_GI_08, BW_20),
	HE_GROUP(7, HE_GI_08, BW_20),
	HE_GROUP(8, HE_GI_08, BW_20),

	HE_GROUP(1, HE_GI_16, BW_20),
	HE_GROUP(2, HE_GI_16, BW_20),
	HE_GROUP(3, HE_GI_16, BW_20),
	HE_GROUP(4, HE_GI_16, BW_20),
	HE_GROUP(5, HE_GI_16, BW_20),
	HE_GROUP(6, HE_GI_16, BW_20),
	HE_GROUP(7, HE_GI_16, BW_20),
	HE_GROUP(8, HE_GI_16, BW_20),

	HE_GROUP(1, HE_GI_32, BW_20),
	HE_GROUP(2, HE_GI_32, BW_20),
	HE_GROUP(3, HE_GI_32, BW_20),
	HE_GROUP(4, HE_GI_32, BW_20),
	HE_GROUP(5, HE_GI_32, BW_20),
	HE_GROUP(6, HE_GI_32, BW_20),
	HE_GROUP(7, HE_GI_32, BW_20),
	HE_GROUP(8, HE_GI_32, BW_20),

	HE_GROUP(1, HE_GI_08, BW_40),
	HE_GROUP(2, HE_GI_08, BW_40),
	HE_GROUP(3, HE_GI_08, BW_40),
	HE_GROUP(4, HE_GI_08, BW_40),
	HE_GROUP(5, HE_GI_08, BW_40),
	HE_GROUP(6, HE_GI_08, BW_40),
	HE_GROUP(7, HE_GI_08, BW_40),
	HE_GROUP(8, HE_GI_08, BW_40),

	HE_GROUP(1, HE_GI_16, BW_40),
	HE_GROUP(2, HE_GI_16, BW_40),
	HE_GROUP(3, HE_GI_16, BW_40),
	HE_GROUP(4, HE_GI_16, BW_40),
	HE_GROUP(5, HE_GI_16, BW_40),
	HE_GROUP(6, HE_GI_16, BW_40),
	HE_GROUP(7, HE_GI_16, BW_40),
	HE_GROUP(8, HE_GI_16, BW_40),

	HE_GROUP(1, HE_GI_32, BW_40),
	HE_GROUP(2, HE_GI_32, BW_40),
	HE_GROUP(3, HE_GI_32, BW_40),
	HE_GROUP(4, HE_GI_32, BW_40),
	HE_GROUP(5, HE_GI_32, BW_40),
	HE_GROUP(6, HE_GI_32, BW_40),
	HE_GROUP(7, HE_GI_32, BW_40),
	HE_GROUP(8, HE_GI_32, BW_40),

	HE_GROUP(1, HE_GI_08, BW_80),
	HE_GROUP(2, HE_GI_08, BW_80),
	HE_GROUP(3, HE_GI_08, BW_80),
	HE_GROUP(4, HE_GI_08, BW_80),
	HE_GROUP(5, HE_GI_08, BW_80),
	HE_GROUP(6, HE_GI_08, BW_80),
	HE_GROUP(7, HE_GI_08, BW_80),
	HE_GROUP(8, HE_GI_08, BW_80),

	HE_GROUP(1, HE_GI_16, BW_80),
	HE_GROUP(2, HE_GI_16, BW_80),
	HE_GROUP(3, HE_GI_16, BW_80),
	HE_GROUP(4, HE_GI_16, BW_80),
	HE_GROUP(5, HE_GI_16, BW_80),
	HE_GROUP(6, HE_GI_16, BW_80),
	HE_GROUP(7, HE_GI_16, BW_80),
	HE_GROUP(8, HE_GI_16, BW_80),

	HE_GROUP(1, HE_GI_32, BW_80),
	HE_GROUP(2, HE_GI_32, BW_80),
	HE_GROUP(3, HE_GI_32, BW_80),
	HE_GROUP(4, HE_GI_32, BW_80),
	HE_GROUP(5, HE_GI_32, BW_80),
	HE_GROUP(6, HE_GI_32, BW_80),
	HE_GROUP(7, HE_GI_32, BW_80),
	HE_GROUP(8, HE_GI_32, BW_80),

	HE_GROUP(1, HE_GI_08, BW_160),
	HE_GROUP(2, HE_GI_08, BW_160),
	HE_GROUP(3, HE_GI_08, BW_160),
	HE_GROUP(4, HE_GI_08, BW_160),
	HE_GROUP(5, HE_GI_08, BW_160),
	HE_GROUP(6, HE_GI_08, BW_160),
	HE_GROUP(7, HE_GI_08, BW_160),
	HE_GROUP(8, HE_GI_08, BW_160),

	HE_GROUP(1, HE_GI_16, BW_160),
	HE_GROUP(2, HE_GI_16, BW_160),
	HE_GROUP(3, HE_GI_16, BW_160),
	HE_GROUP(4, HE_GI_16, BW_160),
	HE_GROUP(5, HE_GI_16, BW_160),
	HE_GROUP(6, HE_GI_16, BW_160),
	HE_GROUP(7, HE_GI_16, BW_160),
	HE_GROUP(8, HE_GI_16, BW_160),

	HE_GROUP(1, HE_GI_32, BW_160),
	HE_GROUP(2, HE_GI_32, BW_160),
	HE_GROUP(3, HE_GI_32, BW_160),
	HE_GROUP(4, HE_GI_32, BW_160),
	HE_GROUP(5, HE_GI_32, BW_160),
	HE_GROUP(6, HE_GI_32, BW_160),
	HE_GROUP(7, HE_GI_32, BW_160),
	HE_GROUP(8, HE_GI_32, BW_160),
};

static u32
ieee80211_calc_legacy_rate_duration(u16 bitrate, bool short_pre,
				    bool cck, int len)
{
	u32 duration;

	if (cck) {
		duration = 144 + 48; /* preamble + PLCP */
		if (short_pre)
			duration >>= 1;

		duration += 10; /* SIFS */
	} else {
		duration = 20 + 16; /* premable + SIFS */
	}

	len <<= 3;
	duration += (len * 10) / bitrate;

	return duration;
}

static u32 ieee80211_get_rate_duration(struct ieee80211_hw *hw,
				       struct ieee80211_rx_status *status,
				       u32 *overhead)
{
	bool sgi = status->enc_flags & RX_ENC_FLAG_SHORT_GI;
	int bw, streams;
	int group, idx;
	u32 duration;

	switch (status->bw) {
	case RATE_INFO_BW_20:
		bw = BW_20;
		break;
	case RATE_INFO_BW_40:
		bw = BW_40;
		break;
	case RATE_INFO_BW_80:
		bw = BW_80;
		break;
	case RATE_INFO_BW_160:
		bw = BW_160;
		break;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}

	switch (status->encoding) {
	case RX_ENC_VHT:
		streams = status->nss;
		idx = status->rate_idx;
		group = VHT_GROUP_IDX(streams, sgi, bw);
		break;
	case RX_ENC_HT:
		streams = ((status->rate_idx >> 3) & 3) + 1;
		idx = status->rate_idx & 7;
		group = HT_GROUP_IDX(streams, sgi, bw);
		break;
	case RX_ENC_HE:
		streams = status->nss;
		idx = status->rate_idx;
		group = HE_GROUP_IDX(streams, status->he_gi, bw);
		break;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}

	if (WARN_ON_ONCE((status->encoding != RX_ENC_HE && streams > 4) ||
			 (status->encoding == RX_ENC_HE && streams > 8)))
		return 0;

	if (idx >= MCS_GROUP_RATES)
		return 0;

	duration = airtime_mcs_groups[group].duration[idx];
	duration <<= airtime_mcs_groups[group].shift;
	*overhead = 36 + (streams << 2);

	return duration;
}


u32 ieee80211_calc_rx_airtime(struct ieee80211_hw *hw,
			      struct ieee80211_rx_status *status,
			      int len)
{
	struct ieee80211_supported_band *sband;
	u32 duration, overhead = 0;

	if (status->encoding == RX_ENC_LEGACY) {
		const struct ieee80211_rate *rate;
		bool sp = status->enc_flags & RX_ENC_FLAG_SHORTPRE;
		bool cck;

		/* on 60GHz or sub-1GHz band, there are no legacy rates */
		if (WARN_ON_ONCE(status->band == NL80211_BAND_60GHZ ||
				 status->band == NL80211_BAND_S1GHZ))
			return 0;

		sband = hw->wiphy->bands[status->band];
		if (!sband || status->rate_idx >= sband->n_bitrates)
			return 0;

		rate = &sband->bitrates[status->rate_idx];
		cck = rate->flags & IEEE80211_RATE_MANDATORY_B;

		return ieee80211_calc_legacy_rate_duration(rate->bitrate, sp,
							   cck, len);
	}

	duration = ieee80211_get_rate_duration(hw, status, &overhead);
	if (!duration)
		return 0;

	duration *= len;
	duration /= AVG_PKT_SIZE;
	duration /= 1024;

	return duration + overhead;
}
EXPORT_SYMBOL_GPL(ieee80211_calc_rx_airtime);

static bool ieee80211_fill_rate_info(struct ieee80211_hw *hw,
				     struct ieee80211_rx_status *stat, u8 band,
				     struct rate_info *ri)
{
	struct ieee80211_supported_band *sband = hw->wiphy->bands[band];
	int i;

	if (!ri || !sband)
	    return false;

	stat->bw = ri->bw;
	stat->nss = ri->nss;
	stat->rate_idx = ri->mcs;

	if (ri->flags & RATE_INFO_FLAGS_HE_MCS)
		stat->encoding = RX_ENC_HE;
	else if (ri->flags & RATE_INFO_FLAGS_VHT_MCS)
		stat->encoding = RX_ENC_VHT;
	else if (ri->flags & RATE_INFO_FLAGS_MCS)
		stat->encoding = RX_ENC_HT;
	else
		stat->encoding = RX_ENC_LEGACY;

	if (ri->flags & RATE_INFO_FLAGS_SHORT_GI)
		stat->enc_flags |= RX_ENC_FLAG_SHORT_GI;

	stat->he_gi = ri->he_gi;

	if (stat->encoding != RX_ENC_LEGACY)
		return true;

	stat->rate_idx = 0;
	for (i = 0; i < sband->n_bitrates; i++) {
		if (ri->legacy != sband->bitrates[i].bitrate)
			continue;

		stat->rate_idx = i;
		return true;
	}

	return false;
}

static int ieee80211_fill_rx_status(struct ieee80211_rx_status *stat,
				    struct ieee80211_hw *hw,
				    struct ieee80211_tx_rate *rate,
				    struct rate_info *ri, u8 band, int len)
{
	memset(stat, 0, sizeof(*stat));
	stat->band = band;

	if (ieee80211_fill_rate_info(hw, stat, band, ri))
		return 0;

	if (!ieee80211_rate_valid(rate))
		return -1;

	if (rate->flags & IEEE80211_TX_RC_160_MHZ_WIDTH)
		stat->bw = RATE_INFO_BW_160;
	else if (rate->flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
		stat->bw = RATE_INFO_BW_80;
	else if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
		stat->bw = RATE_INFO_BW_40;
	else
		stat->bw = RATE_INFO_BW_20;

	stat->enc_flags = 0;
	if (rate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
		stat->enc_flags |= RX_ENC_FLAG_SHORTPRE;
	if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
		stat->enc_flags |= RX_ENC_FLAG_SHORT_GI;

	stat->rate_idx = rate->idx;
	if (rate->flags & IEEE80211_TX_RC_VHT_MCS) {
		stat->encoding = RX_ENC_VHT;
		stat->rate_idx = ieee80211_rate_get_vht_mcs(rate);
		stat->nss = ieee80211_rate_get_vht_nss(rate);
	} else if (rate->flags & IEEE80211_TX_RC_MCS) {
		stat->encoding = RX_ENC_HT;
	} else {
		stat->encoding = RX_ENC_LEGACY;
	}

	return 0;
}

static u32 ieee80211_calc_tx_airtime_rate(struct ieee80211_hw *hw,
					  struct ieee80211_tx_rate *rate,
					  struct rate_info *ri,
					  u8 band, int len)
{
	struct ieee80211_rx_status stat;

	if (ieee80211_fill_rx_status(&stat, hw, rate, ri, band, len))
		return 0;

	return ieee80211_calc_rx_airtime(hw, &stat, len);
}

u32 ieee80211_calc_tx_airtime(struct ieee80211_hw *hw,
			      struct ieee80211_tx_info *info,
			      int len)
{
	u32 duration = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(info->status.rates); i++) {
		struct ieee80211_tx_rate *rate = &info->status.rates[i];
		u32 cur_duration;

		cur_duration = ieee80211_calc_tx_airtime_rate(hw, rate, NULL,
							      info->band, len);
		if (!cur_duration)
			break;

		duration += cur_duration * rate->count;
	}

	return duration;
}
EXPORT_SYMBOL_GPL(ieee80211_calc_tx_airtime);

u32 ieee80211_calc_expected_tx_airtime(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_sta *pubsta,
				       int len, bool ampdu)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_chanctx_conf *conf;
	int rateidx;
	bool cck, short_pream;
	u32 basic_rates;
	u8 band = 0;
	u16 rate;

	len += 38; /* Ethernet header length */

	conf = rcu_dereference(vif->bss_conf.chanctx_conf);
	if (conf)
		band = conf->def.chan->band;

	if (pubsta) {
		struct sta_info *sta = container_of(pubsta, struct sta_info,
						    sta);
		struct ieee80211_rx_status stat;
		struct ieee80211_tx_rate *tx_rate = &sta->deflink.tx_stats.last_rate;
		struct rate_info *ri = &sta->deflink.tx_stats.last_rate_info;
		u32 duration, overhead;
		u8 agg_shift;

		if (ieee80211_fill_rx_status(&stat, hw, tx_rate, ri, band, len))
			return 0;

		if (stat.encoding == RX_ENC_LEGACY || !ampdu)
			return ieee80211_calc_rx_airtime(hw, &stat, len);

		duration = ieee80211_get_rate_duration(hw, &stat, &overhead);
		/*
		 * Assume that HT/VHT transmission on any AC except VO will
		 * use aggregation. Since we don't have reliable reporting
		 * of aggregation length, assume an average size based on the
		 * tx rate.
		 * This will not be very accurate, but much better than simply
		 * assuming un-aggregated tx in all cases.
		 */
		if (duration > 400 * 1024) /* <= VHT20 MCS2 1S */
			agg_shift = 1;
		else if (duration > 250 * 1024) /* <= VHT20 MCS3 1S or MCS1 2S */
			agg_shift = 2;
		else if (duration > 150 * 1024) /* <= VHT20 MCS5 1S or MCS2 2S */
			agg_shift = 3;
		else if (duration > 70 * 1024) /* <= VHT20 MCS5 2S */
			agg_shift = 4;
		else if (stat.encoding != RX_ENC_HE ||
			 duration > 20 * 1024) /* <= HE40 MCS6 2S */
			agg_shift = 5;
		else
			agg_shift = 6;

		duration *= len;
		duration /= AVG_PKT_SIZE;
		duration /= 1024;
		duration += (overhead >> agg_shift);

		return max_t(u32, duration, 4);
	}

	if (!conf)
		return 0;

	/* No station to get latest rate from, so calculate the worst-case
	 * duration using the lowest configured basic rate.
	 */
	sband = hw->wiphy->bands[band];

	basic_rates = vif->bss_conf.basic_rates;
	short_pream = vif->bss_conf.use_short_preamble;

	rateidx = basic_rates ? ffs(basic_rates) - 1 : 0;
	rate = sband->bitrates[rateidx].bitrate;
	cck = sband->bitrates[rateidx].flags & IEEE80211_RATE_MANDATORY_B;

	return ieee80211_calc_legacy_rate_duration(rate, short_pream, cck, len);
}
