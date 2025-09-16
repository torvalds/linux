/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * include/uapi/linux/ethtool_netlink.h - netlink interface for ethtool
 *
 * See Documentation/networking/ethtool-netlink.rst in kernel source tree for
 * doucumentation of the interface.
 */

#ifndef _UAPI_LINUX_ETHTOOL_NETLINK_H_
#define _UAPI_LINUX_ETHTOOL_NETLINK_H_

#include <linux/ethtool.h>
#include <linux/ethtool_netlink_generated.h>

#define ETHTOOL_FLAG_ALL (ETHTOOL_FLAG_COMPACT_BITSETS | \
			  ETHTOOL_FLAG_OMIT_REPLY | \
			  ETHTOOL_FLAG_STATS)

/* CABLE TEST NOTIFY */
enum {
	ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC,
	ETHTOOL_A_CABLE_RESULT_CODE_OK,
	ETHTOOL_A_CABLE_RESULT_CODE_OPEN,
	ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT,
	ETHTOOL_A_CABLE_RESULT_CODE_CROSS_SHORT,
	/* detected reflection caused by the impedance discontinuity between
	 * a regular 100 Ohm cable and a part with the abnormal impedance value
	 */
	ETHTOOL_A_CABLE_RESULT_CODE_IMPEDANCE_MISMATCH,
	/* TDR not possible due to high noise level */
	ETHTOOL_A_CABLE_RESULT_CODE_NOISE,
	/* TDR resolution not possible / out of distance */
	ETHTOOL_A_CABLE_RESULT_CODE_RESOLUTION_NOT_POSSIBLE,
};

enum {
	ETHTOOL_A_CABLE_PAIR_A,
	ETHTOOL_A_CABLE_PAIR_B,
	ETHTOOL_A_CABLE_PAIR_C,
	ETHTOOL_A_CABLE_PAIR_D,
};

/* Information source for specific results. */
enum {
	ETHTOOL_A_CABLE_INF_SRC_UNSPEC,
	/* Results provided by the Time Domain Reflectometry (TDR) */
	ETHTOOL_A_CABLE_INF_SRC_TDR,
	/* Results provided by the Active Link Cable Diagnostic (ALCD) */
	ETHTOOL_A_CABLE_INF_SRC_ALCD,
};

enum {
	ETHTOOL_A_CABLE_TEST_NTF_STATUS_UNSPEC,
	ETHTOOL_A_CABLE_TEST_NTF_STATUS_STARTED,
	ETHTOOL_A_CABLE_TEST_NTF_STATUS_COMPLETED
};

/* CABLE TEST TDR NOTIFY */

enum {
	ETHTOOL_A_CABLE_AMPLITUDE_UNSPEC,
	ETHTOOL_A_CABLE_AMPLITUDE_PAIR,         /* u8 */
	ETHTOOL_A_CABLE_AMPLITUDE_mV,           /* s16 */

	__ETHTOOL_A_CABLE_AMPLITUDE_CNT,
	ETHTOOL_A_CABLE_AMPLITUDE_MAX = (__ETHTOOL_A_CABLE_AMPLITUDE_CNT - 1)
};

enum {
	ETHTOOL_A_CABLE_PULSE_UNSPEC,
	ETHTOOL_A_CABLE_PULSE_mV,		/* s16 */

	__ETHTOOL_A_CABLE_PULSE_CNT,
	ETHTOOL_A_CABLE_PULSE_MAX = (__ETHTOOL_A_CABLE_PULSE_CNT - 1)
};

enum {
	ETHTOOL_A_CABLE_STEP_UNSPEC,
	ETHTOOL_A_CABLE_STEP_FIRST_DISTANCE,	/* u32 */
	ETHTOOL_A_CABLE_STEP_LAST_DISTANCE,	/* u32 */
	ETHTOOL_A_CABLE_STEP_STEP_DISTANCE,	/* u32 */

	__ETHTOOL_A_CABLE_STEP_CNT,
	ETHTOOL_A_CABLE_STEP_MAX = (__ETHTOOL_A_CABLE_STEP_CNT - 1)
};

enum {
	ETHTOOL_A_CABLE_TDR_NEST_UNSPEC,
	ETHTOOL_A_CABLE_TDR_NEST_STEP,		/* nest - ETHTTOOL_A_CABLE_STEP */
	ETHTOOL_A_CABLE_TDR_NEST_AMPLITUDE,	/* nest - ETHTOOL_A_CABLE_AMPLITUDE */
	ETHTOOL_A_CABLE_TDR_NEST_PULSE,		/* nest - ETHTOOL_A_CABLE_PULSE */

	__ETHTOOL_A_CABLE_TDR_NEST_CNT,
	ETHTOOL_A_CABLE_TDR_NEST_MAX = (__ETHTOOL_A_CABLE_TDR_NEST_CNT - 1)
};

enum {
	ETHTOOL_STATS_ETH_PHY,
	ETHTOOL_STATS_ETH_MAC,
	ETHTOOL_STATS_ETH_CTRL,
	ETHTOOL_STATS_RMON,
	ETHTOOL_STATS_PHY,

	/* add new constants above here */
	__ETHTOOL_STATS_CNT
};

enum {
	/* 30.3.2.1.5 aSymbolErrorDuringCarrier */
	ETHTOOL_A_STATS_ETH_PHY_5_SYM_ERR,

	/* add new constants above here */
	__ETHTOOL_A_STATS_ETH_PHY_CNT,
	ETHTOOL_A_STATS_ETH_PHY_MAX = (__ETHTOOL_A_STATS_ETH_PHY_CNT - 1)
};

enum {
	/* 30.3.1.1.2 aFramesTransmittedOK */
	ETHTOOL_A_STATS_ETH_MAC_2_TX_PKT,
	/* 30.3.1.1.3 aSingleCollisionFrames */
	ETHTOOL_A_STATS_ETH_MAC_3_SINGLE_COL,
	/* 30.3.1.1.4 aMultipleCollisionFrames */
	ETHTOOL_A_STATS_ETH_MAC_4_MULTI_COL,
	/* 30.3.1.1.5 aFramesReceivedOK */
	ETHTOOL_A_STATS_ETH_MAC_5_RX_PKT,
	/* 30.3.1.1.6 aFrameCheckSequenceErrors */
	ETHTOOL_A_STATS_ETH_MAC_6_FCS_ERR,
	/* 30.3.1.1.7 aAlignmentErrors */
	ETHTOOL_A_STATS_ETH_MAC_7_ALIGN_ERR,
	/* 30.3.1.1.8 aOctetsTransmittedOK */
	ETHTOOL_A_STATS_ETH_MAC_8_TX_BYTES,
	/* 30.3.1.1.9 aFramesWithDeferredXmissions */
	ETHTOOL_A_STATS_ETH_MAC_9_TX_DEFER,
	/* 30.3.1.1.10 aLateCollisions */
	ETHTOOL_A_STATS_ETH_MAC_10_LATE_COL,
	/* 30.3.1.1.11 aFramesAbortedDueToXSColls */
	ETHTOOL_A_STATS_ETH_MAC_11_XS_COL,
	/* 30.3.1.1.12 aFramesLostDueToIntMACXmitError */
	ETHTOOL_A_STATS_ETH_MAC_12_TX_INT_ERR,
	/* 30.3.1.1.13 aCarrierSenseErrors */
	ETHTOOL_A_STATS_ETH_MAC_13_CS_ERR,
	/* 30.3.1.1.14 aOctetsReceivedOK */
	ETHTOOL_A_STATS_ETH_MAC_14_RX_BYTES,
	/* 30.3.1.1.15 aFramesLostDueToIntMACRcvError */
	ETHTOOL_A_STATS_ETH_MAC_15_RX_INT_ERR,

	/* 30.3.1.1.18 aMulticastFramesXmittedOK */
	ETHTOOL_A_STATS_ETH_MAC_18_TX_MCAST,
	/* 30.3.1.1.19 aBroadcastFramesXmittedOK */
	ETHTOOL_A_STATS_ETH_MAC_19_TX_BCAST,
	/* 30.3.1.1.20 aFramesWithExcessiveDeferral */
	ETHTOOL_A_STATS_ETH_MAC_20_XS_DEFER,
	/* 30.3.1.1.21 aMulticastFramesReceivedOK */
	ETHTOOL_A_STATS_ETH_MAC_21_RX_MCAST,
	/* 30.3.1.1.22 aBroadcastFramesReceivedOK */
	ETHTOOL_A_STATS_ETH_MAC_22_RX_BCAST,
	/* 30.3.1.1.23 aInRangeLengthErrors */
	ETHTOOL_A_STATS_ETH_MAC_23_IR_LEN_ERR,
	/* 30.3.1.1.24 aOutOfRangeLengthField */
	ETHTOOL_A_STATS_ETH_MAC_24_OOR_LEN,
	/* 30.3.1.1.25 aFrameTooLongErrors */
	ETHTOOL_A_STATS_ETH_MAC_25_TOO_LONG_ERR,

	/* add new constants above here */
	__ETHTOOL_A_STATS_ETH_MAC_CNT,
	ETHTOOL_A_STATS_ETH_MAC_MAX = (__ETHTOOL_A_STATS_ETH_MAC_CNT - 1)
};

enum {
	/* 30.3.3.3 aMACControlFramesTransmitted */
	ETHTOOL_A_STATS_ETH_CTRL_3_TX,
	/* 30.3.3.4 aMACControlFramesReceived */
	ETHTOOL_A_STATS_ETH_CTRL_4_RX,
	/* 30.3.3.5 aUnsupportedOpcodesReceived */
	ETHTOOL_A_STATS_ETH_CTRL_5_RX_UNSUP,

	/* add new constants above here */
	__ETHTOOL_A_STATS_ETH_CTRL_CNT,
	ETHTOOL_A_STATS_ETH_CTRL_MAX = (__ETHTOOL_A_STATS_ETH_CTRL_CNT - 1)
};

enum {
	/* etherStatsUndersizePkts */
	ETHTOOL_A_STATS_RMON_UNDERSIZE,
	/* etherStatsOversizePkts */
	ETHTOOL_A_STATS_RMON_OVERSIZE,
	/* etherStatsFragments */
	ETHTOOL_A_STATS_RMON_FRAG,
	/* etherStatsJabbers */
	ETHTOOL_A_STATS_RMON_JABBER,

	/* add new constants above here */
	__ETHTOOL_A_STATS_RMON_CNT,
	ETHTOOL_A_STATS_RMON_MAX = (__ETHTOOL_A_STATS_RMON_CNT - 1)
};

enum {
	/* Basic packet counters if PHY has separate counters from the MAC */
	ETHTOOL_A_STATS_PHY_RX_PKTS,
	ETHTOOL_A_STATS_PHY_RX_BYTES,
	ETHTOOL_A_STATS_PHY_RX_ERRORS,
	ETHTOOL_A_STATS_PHY_TX_PKTS,
	ETHTOOL_A_STATS_PHY_TX_BYTES,
	ETHTOOL_A_STATS_PHY_TX_ERRORS,

	/* add new constants above here */
	__ETHTOOL_A_STATS_PHY_CNT,
	ETHTOOL_A_STATS_PHY_MAX = (__ETHTOOL_A_STATS_PHY_CNT - 1)
};

#endif /* _UAPI_LINUX_ETHTOOL_NETLINK_H_ */
