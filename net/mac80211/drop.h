/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * mac80211 drop reason list
 *
 * Copyright (C) 2023 Intel Corporation
 */

#ifndef MAC80211_DROP_H
#define MAC80211_DROP_H
#include <net/dropreason.h>

typedef unsigned int __bitwise ieee80211_rx_result;

#define MAC80211_DROP_REASONS_MONITOR(R)	\
	R(RX_DROP_M_UNEXPECTED_4ADDR_FRAME)	\
	R(RX_DROP_M_BAD_BCN_KEYIDX)		\
	R(RX_DROP_M_BAD_MGMT_KEYIDX)		\
/* this line for the trailing \ - add before this */

#define MAC80211_DROP_REASONS_UNUSABLE(R)	\
	R(RX_DROP_U_MIC_FAIL)			\
	R(RX_DROP_U_REPLAY)			\
	R(RX_DROP_U_BAD_MMIE)			\
/* this line for the trailing \ - add before this */

/* having two enums allows for checking ieee80211_rx_result use with sparse */
enum ___mac80211_drop_reason {
/* if we get to the end of handlers with RX_CONTINUE this will be the reason */
	___RX_CONTINUE	= SKB_CONSUMED,

/* this never gets used as an argument to kfree_skb_reason() */
	___RX_QUEUED	= SKB_NOT_DROPPED_YET,

#define ENUM(x) ___ ## x,
	___RX_DROP_MONITOR = SKB_DROP_REASON_SUBSYS_MAC80211_MONITOR <<
		SKB_DROP_REASON_SUBSYS_SHIFT,
	MAC80211_DROP_REASONS_MONITOR(ENUM)

	___RX_DROP_UNUSABLE = SKB_DROP_REASON_SUBSYS_MAC80211_UNUSABLE <<
		SKB_DROP_REASON_SUBSYS_SHIFT,
	MAC80211_DROP_REASONS_UNUSABLE(ENUM)
#undef ENUM
};

enum mac80211_drop_reason {
	RX_CONTINUE	 = (__force ieee80211_rx_result)___RX_CONTINUE,
	RX_QUEUED	 = (__force ieee80211_rx_result)___RX_QUEUED,
	RX_DROP_MONITOR	 = (__force ieee80211_rx_result)___RX_DROP_MONITOR,
	RX_DROP_UNUSABLE = (__force ieee80211_rx_result)___RX_DROP_UNUSABLE,
#define DEF(x) x = (__force ieee80211_rx_result)___ ## x,
	MAC80211_DROP_REASONS_MONITOR(DEF)
	MAC80211_DROP_REASONS_UNUSABLE(DEF)
#undef DEF
};

#define RX_RES_IS_UNUSABLE(result)	\
	(((__force u32)(result) & SKB_DROP_REASON_SUBSYS_MASK) == ___RX_DROP_UNUSABLE)

#endif /* MAC80211_DROP_H */
