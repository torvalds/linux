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
	/* 0x00 == ___RX_DROP_UNUSABLE */	\
	R(RX_DROP_U_MIC_FAIL)			\
	R(RX_DROP_U_REPLAY)			\
	R(RX_DROP_U_BAD_MMIE)			\
	R(RX_DROP_U_DUP)			\
	R(RX_DROP_U_SPURIOUS)			\
	R(RX_DROP_U_DECRYPT_FAIL)		\
	R(RX_DROP_U_NO_KEY_ID)			\
	R(RX_DROP_U_BAD_CIPHER)			\
	R(RX_DROP_U_OOM)			\
	R(RX_DROP_U_NONSEQ_PN)			\
	R(RX_DROP_U_BAD_KEY_COLOR)		\
	R(RX_DROP_U_BAD_4ADDR)			\
	R(RX_DROP_U_BAD_AMSDU)			\
	R(RX_DROP_U_BAD_AMSDU_CIPHER)		\
	R(RX_DROP_U_INVALID_8023)		\
	/* 0x10 */				\
	R(RX_DROP_U_RUNT_ACTION)		\
	R(RX_DROP_U_UNPROT_ACTION)		\
	R(RX_DROP_U_UNPROT_DUAL)		\
	R(RX_DROP_U_UNPROT_UCAST_MGMT)		\
	R(RX_DROP_U_UNPROT_MCAST_MGMT)		\
	R(RX_DROP_U_UNPROT_BEACON)		\
	R(RX_DROP_U_UNPROT_UNICAST_PUB_ACTION)	\
	R(RX_DROP_U_UNPROT_ROBUST_ACTION)	\
	R(RX_DROP_U_ACTION_UNKNOWN_SRC)		\
	R(RX_DROP_U_REJECTED_ACTION_RESPONSE)	\
	R(RX_DROP_U_EXPECT_DEFRAG_PROT)		\
	R(RX_DROP_U_WEP_DEC_FAIL)		\
	R(RX_DROP_U_NO_IV)			\
	R(RX_DROP_U_NO_ICV)			\
	R(RX_DROP_U_AP_RX_GROUPCAST)		\
	R(RX_DROP_U_SHORT_MMIC)			\
	/* 0x20 */				\
	R(RX_DROP_U_MMIC_FAIL)			\
	R(RX_DROP_U_SHORT_TKIP)			\
	R(RX_DROP_U_TKIP_FAIL)			\
	R(RX_DROP_U_SHORT_CCMP)			\
	R(RX_DROP_U_SHORT_CCMP_MIC)		\
	R(RX_DROP_U_SHORT_GCMP)			\
	R(RX_DROP_U_SHORT_GCMP_MIC)		\
	R(RX_DROP_U_SHORT_CMAC)			\
	R(RX_DROP_U_SHORT_CMAC256)		\
	R(RX_DROP_U_SHORT_GMAC)			\
	R(RX_DROP_U_UNEXPECTED_VLAN_4ADDR)	\
	R(RX_DROP_U_UNEXPECTED_STA_4ADDR)	\
	R(RX_DROP_U_UNEXPECTED_VLAN_MCAST)	\
	R(RX_DROP_U_NOT_PORT_CONTROL)		\
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
#define DEF(x) x = (__force ieee80211_rx_result)___ ## x,
	MAC80211_DROP_REASONS_MONITOR(DEF)
	MAC80211_DROP_REASONS_UNUSABLE(DEF)
#undef DEF
};

#define RX_RES_IS_UNUSABLE(result)	\
	(((__force u32)(result) & SKB_DROP_REASON_SUBSYS_MASK) == ___RX_DROP_UNUSABLE)

#endif /* MAC80211_DROP_H */
