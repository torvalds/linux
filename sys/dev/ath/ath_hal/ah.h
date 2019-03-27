/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2009 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2008 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#ifndef _ATH_AH_H_
#define _ATH_AH_H_
/*
 * Atheros Hardware Access Layer
 *
 * Clients of the HAL call ath_hal_attach to obtain a reference to an ath_hal
 * structure for use with the device.  Hardware-related operations that
 * follow must call back into the HAL through interface, supplying the
 * reference as the first parameter.
 */

#include "ah_osdep.h"

/*
 * The maximum number of TX/RX chains supported.
 * This is intended to be used by various statistics gathering operations
 * (NF, RSSI, EVM).
 */
#define	AH_MAX_CHAINS			3
#define	AH_MIMO_MAX_EVM_PILOTS		6

/*
 * __ahdecl is analogous to _cdecl; it defines the calling
 * convention used within the HAL.  For most systems this
 * can just default to be empty and the compiler will (should)
 * use _cdecl.  For systems where _cdecl is not compatible this
 * must be defined.  See linux/ah_osdep.h for an example.
 */
#ifndef __ahdecl
#define __ahdecl
#endif

/*
 * Status codes that may be returned by the HAL.  Note that
 * interfaces that return a status code set it only when an
 * error occurs--i.e. you cannot check it for success.
 */
typedef enum {
	HAL_OK		= 0,	/* No error */
	HAL_ENXIO	= 1,	/* No hardware present */
	HAL_ENOMEM	= 2,	/* Memory allocation failed */
	HAL_EIO		= 3,	/* Hardware didn't respond as expected */
	HAL_EEMAGIC	= 4,	/* EEPROM magic number invalid */
	HAL_EEVERSION	= 5,	/* EEPROM version invalid */
	HAL_EELOCKED	= 6,	/* EEPROM unreadable */
	HAL_EEBADSUM	= 7,	/* EEPROM checksum invalid */
	HAL_EEREAD	= 8,	/* EEPROM read problem */
	HAL_EEBADMAC	= 9,	/* EEPROM mac address invalid */
	HAL_EESIZE	= 10,	/* EEPROM size not supported */
	HAL_EEWRITE	= 11,	/* Attempt to change write-locked EEPROM */
	HAL_EINVAL	= 12,	/* Invalid parameter to function */
	HAL_ENOTSUPP	= 13,	/* Hardware revision not supported */
	HAL_ESELFTEST	= 14,	/* Hardware self-test failed */
	HAL_EINPROGRESS	= 15,	/* Operation incomplete */
	HAL_EEBADREG	= 16,	/* EEPROM invalid regulatory contents */
	HAL_EEBADCC	= 17,	/* EEPROM invalid country code */
	HAL_INV_PMODE	= 18,	/* Couldn't bring out of sleep state */
} HAL_STATUS;

typedef enum {
	AH_FALSE = 0,		/* NB: lots of code assumes false is zero */
	AH_TRUE  = 1,
} HAL_BOOL;

typedef enum {
	HAL_CAP_REG_DMN		= 0,	/* current regulatory domain */
	HAL_CAP_CIPHER		= 1,	/* hardware supports cipher */
	HAL_CAP_TKIP_MIC	= 2,	/* handle TKIP MIC in hardware */
	HAL_CAP_TKIP_SPLIT	= 3,	/* hardware TKIP uses split keys */
	HAL_CAP_PHYCOUNTERS	= 4,	/* hardware PHY error counters */
	HAL_CAP_DIVERSITY	= 5,	/* hardware supports fast diversity */
	HAL_CAP_KEYCACHE_SIZE	= 6,	/* number of entries in key cache */
	HAL_CAP_NUM_TXQUEUES	= 7,	/* number of hardware xmit queues */
	HAL_CAP_VEOL		= 9,	/* hardware supports virtual EOL */
	HAL_CAP_PSPOLL		= 10,	/* hardware has working PS-Poll support */
	HAL_CAP_DIAG		= 11,	/* hardware diagnostic support */
	HAL_CAP_COMPRESSION	= 12,	/* hardware supports compression */
	HAL_CAP_BURST		= 13,	/* hardware supports packet bursting */
	HAL_CAP_FASTFRAME	= 14,	/* hardware supoprts fast frames */
	HAL_CAP_TXPOW		= 15,	/* global tx power limit  */
	HAL_CAP_TPC		= 16,	/* per-packet tx power control  */
	HAL_CAP_PHYDIAG		= 17,	/* hardware phy error diagnostic */
	HAL_CAP_BSSIDMASK	= 18,	/* hardware supports bssid mask */
	HAL_CAP_MCAST_KEYSRCH	= 19,	/* hardware has multicast key search */
	HAL_CAP_TSF_ADJUST	= 20,	/* hardware has beacon tsf adjust */
	/* 21 was HAL_CAP_XR */
	HAL_CAP_WME_TKIPMIC 	= 22,   /* hardware can support TKIP MIC when WMM is turned on */
	/* 23 was HAL_CAP_CHAN_HALFRATE */
	/* 24 was HAL_CAP_CHAN_QUARTERRATE */
	HAL_CAP_RFSILENT	= 25,	/* hardware has rfsilent support  */
	HAL_CAP_TPC_ACK		= 26,	/* ack txpower with per-packet tpc */
	HAL_CAP_TPC_CTS		= 27,	/* cts txpower with per-packet tpc */
	HAL_CAP_11D		= 28,   /* 11d beacon support for changing cc */
	HAL_CAP_PCIE_PS		= 29,
	HAL_CAP_HT		= 30,   /* hardware can support HT */
	HAL_CAP_GTXTO		= 31,	/* hardware supports global tx timeout */
	HAL_CAP_FAST_CC		= 32,	/* hardware supports fast channel change */
	HAL_CAP_TX_CHAINMASK	= 33,	/* mask of TX chains supported */
	HAL_CAP_RX_CHAINMASK	= 34,	/* mask of RX chains supported */
	HAL_CAP_NUM_GPIO_PINS	= 36,	/* number of GPIO pins */

	HAL_CAP_CST		= 38,	/* hardware supports carrier sense timeout */
	HAL_CAP_RIFS_RX		= 39,
	HAL_CAP_RIFS_TX		= 40,
	HAL_CAP_FORCE_PPM	= 41,
	HAL_CAP_RTS_AGGR_LIMIT	= 42,	/* aggregation limit with RTS */
	HAL_CAP_4ADDR_AGGR	= 43,	/* hardware is capable of 4addr aggregation */
	HAL_CAP_DFS_DMN		= 44,	/* current DFS domain */
	HAL_CAP_EXT_CHAN_DFS	= 45,	/* DFS support for extension channel */
	HAL_CAP_COMBINED_RADAR_RSSI	= 46,	/* Is combined RSSI for radar accurate */

	HAL_CAP_AUTO_SLEEP	= 48,	/* hardware can go to network sleep
					   automatically after waking up to receive TIM */
	HAL_CAP_MBSSID_AGGR_SUPPORT	= 49, /* Support for mBSSID Aggregation */
	HAL_CAP_SPLIT_4KB_TRANS	= 50,	/* hardware supports descriptors straddling a 4k page boundary */
	HAL_CAP_REG_FLAG	= 51,	/* Regulatory domain flags */
	HAL_CAP_BB_RIFS_HANG	= 52,
	HAL_CAP_RIFS_RX_ENABLED	= 53,
	HAL_CAP_BB_DFS_HANG	= 54,

	HAL_CAP_RX_STBC		= 58,
	HAL_CAP_TX_STBC		= 59,

	HAL_CAP_BT_COEX		= 60,	/* hardware is capable of bluetooth coexistence */
	HAL_CAP_DYNAMIC_SMPS	= 61,	/* Dynamic MIMO Power Save hardware support */

	HAL_CAP_DS		= 67,	/* 2 stream */
	HAL_CAP_BB_RX_CLEAR_STUCK_HANG	= 68,
	HAL_CAP_MAC_HANG	= 69,	/* can MAC hang */
	HAL_CAP_MFP		= 70,	/* Management Frame Protection in hardware */

	HAL_CAP_TS		= 72,	/* 3 stream */

	HAL_CAP_ENHANCED_DMA_SUPPORT	= 75,	/* DMA FIFO support */
	HAL_CAP_NUM_TXMAPS	= 76,	/* Number of buffers in a transmit descriptor */
	HAL_CAP_TXDESCLEN	= 77,	/* Length of transmit descriptor */
	HAL_CAP_TXSTATUSLEN	= 78,	/* Length of transmit status descriptor */
	HAL_CAP_RXSTATUSLEN	= 79,	/* Length of transmit status descriptor */
	HAL_CAP_RXFIFODEPTH	= 80,	/* Receive hardware FIFO depth */
	HAL_CAP_RXBUFSIZE	= 81,	/* Receive Buffer Length */
	HAL_CAP_NUM_MR_RETRIES	= 82,	/* limit on multirate retries */
	HAL_CAP_OL_PWRCTRL	= 84,	/* Open loop TX power control */
	HAL_CAP_SPECTRAL_SCAN	= 90,	/* Hardware supports spectral scan */

	HAL_CAP_BB_PANIC_WATCHDOG	= 92,

	HAL_CAP_HT20_SGI	= 96,	/* hardware supports HT20 short GI */

	HAL_CAP_LDPC		= 99,

	HAL_CAP_RXTSTAMP_PREC	= 100,	/* rx desc tstamp precision (bits) */

	HAL_CAP_ANT_DIV_COMB	= 105,	/* Enable antenna diversity/combining */
	HAL_CAP_PHYRESTART_CLR_WAR	= 106,	/* in some cases, clear phy restart to fix bb hang */
	HAL_CAP_ENTERPRISE_MODE	= 107,	/* Enterprise mode features */
	HAL_CAP_LDPCWAR		= 108,
	HAL_CAP_CHANNEL_SWITCH_TIME_USEC	= 109,	/* Channel change time, usec */
	HAL_CAP_ENABLE_APM	= 110,	/* APM enabled */
	HAL_CAP_PCIE_LCR_EXTSYNC_EN	= 111,
	HAL_CAP_PCIE_LCR_OFFSET	= 112,

	HAL_CAP_ENHANCED_DFS_SUPPORT	= 117,	/* hardware supports enhanced DFS */
	HAL_CAP_MCI		= 118,
	HAL_CAP_SMARTANTENNA	= 119,
	HAL_CAP_TRAFFIC_FAST_RECOVER	= 120,
	HAL_CAP_TX_DIVERSITY	= 121,
	HAL_CAP_CRDC		= 122,

	/* The following are private to the FreeBSD HAL (224 onward) */

	HAL_CAP_INTMIT		= 229,	/* interference mitigation */
	HAL_CAP_RXORN_FATAL	= 230,	/* HAL_INT_RXORN treated as fatal */
	HAL_CAP_BB_HANG		= 235,	/* can baseband hang */
	HAL_CAP_INTRMASK	= 237,	/* bitmask of supported interrupts */
	HAL_CAP_BSSIDMATCH	= 238,	/* hardware has disable bssid match */
	HAL_CAP_STREAMS		= 239,	/* how many 802.11n spatial streams are available */
	HAL_CAP_RXDESC_SELFLINK	= 242,	/* support a self-linked tail RX descriptor */
	HAL_CAP_BB_READ_WAR	= 244,	/* baseband read WAR */
	HAL_CAP_SERIALISE_WAR	= 245,	/* serialise register access on PCI */
	HAL_CAP_ENFORCE_TXOP	= 246,	/* Enforce TXOP if supported */
	HAL_CAP_RX_LNA_MIXING	= 247,	/* RX hardware uses LNA mixing */
	HAL_CAP_DO_MYBEACON	= 248,	/* Supports HAL_RX_FILTER_MYBEACON */
	HAL_CAP_TOA_LOCATIONING	= 249,	/* time of flight / arrival locationing */
	HAL_CAP_TXTSTAMP_PREC	= 250,	/* tx desc tstamp precision (bits) */
} HAL_CAPABILITY_TYPE;

/* 
 * "States" for setting the LED.  These correspond to
 * the possible 802.11 operational states and there may
 * be a many-to-one mapping between these states and the
 * actual hardware state for the LED's (i.e. the hardware
 * may have fewer states).
 */
typedef enum {
	HAL_LED_INIT	= 0,
	HAL_LED_SCAN	= 1,
	HAL_LED_AUTH	= 2,
	HAL_LED_ASSOC	= 3,
	HAL_LED_RUN	= 4
} HAL_LED_STATE;

/*
 * Transmit queue types/numbers.  These are used to tag
 * each transmit queue in the hardware and to identify a set
 * of transmit queues for operations such as start/stop dma.
 */
typedef enum {
	HAL_TX_QUEUE_INACTIVE	= 0,		/* queue is inactive/unused */
	HAL_TX_QUEUE_DATA	= 1,		/* data xmit q's */
	HAL_TX_QUEUE_BEACON	= 2,		/* beacon xmit q */
	HAL_TX_QUEUE_CAB	= 3,		/* "crap after beacon" xmit q */
	HAL_TX_QUEUE_UAPSD	= 4,		/* u-apsd power save xmit q */
	HAL_TX_QUEUE_PSPOLL	= 5,		/* power save poll xmit q */
	HAL_TX_QUEUE_CFEND	= 6,
	HAL_TX_QUEUE_PAPRD	= 7,
} HAL_TX_QUEUE;

#define	HAL_NUM_TX_QUEUES	10		/* max possible # of queues */

/*
 * Receive queue types.  These are used to tag
 * each transmit queue in the hardware and to identify a set
 * of transmit queues for operations such as start/stop dma.
 */
typedef enum {
	HAL_RX_QUEUE_HP = 0,			/* high priority recv queue */
	HAL_RX_QUEUE_LP = 1,			/* low priority recv queue */
} HAL_RX_QUEUE;

#define	HAL_NUM_RX_QUEUES	2		/* max possible # of queues */

#define	HAL_TXFIFO_DEPTH	8		/* transmit fifo depth */

/*
 * Transmit queue subtype.  These map directly to
 * WME Access Categories (except for UPSD).  Refer
 * to Table 5 of the WME spec.
 */
typedef enum {
	HAL_WME_AC_BK	= 0,			/* background access category */
	HAL_WME_AC_BE	= 1, 			/* best effort access category*/
	HAL_WME_AC_VI	= 2,			/* video access category */
	HAL_WME_AC_VO	= 3,			/* voice access category */
	HAL_WME_UPSD	= 4,			/* uplink power save */
} HAL_TX_QUEUE_SUBTYPE;

/*
 * Transmit queue flags that control various
 * operational parameters.
 */
typedef enum {
	/*
	 * Per queue interrupt enables.  When set the associated
	 * interrupt may be delivered for packets sent through
	 * the queue.  Without these enabled no interrupts will
	 * be delivered for transmits through the queue.
	 */
	HAL_TXQ_TXOKINT_ENABLE	   = 0x0001,	/* enable TXOK interrupt */
	HAL_TXQ_TXERRINT_ENABLE	   = 0x0001,	/* enable TXERR interrupt */
	HAL_TXQ_TXDESCINT_ENABLE   = 0x0002,	/* enable TXDESC interrupt */
	HAL_TXQ_TXEOLINT_ENABLE	   = 0x0004,	/* enable TXEOL interrupt */
	HAL_TXQ_TXURNINT_ENABLE	   = 0x0008,	/* enable TXURN interrupt */
	/*
	 * Enable hardware compression for packets sent through
	 * the queue.  The compression buffer must be setup and
	 * packets must have a key entry marked in the tx descriptor.
	 */
	HAL_TXQ_COMPRESSION_ENABLE  = 0x0010,	/* enable h/w compression */
	/*
	 * Disable queue when veol is hit or ready time expires.
	 * By default the queue is disabled only on reaching the
	 * physical end of queue (i.e. a null link ptr in the
	 * descriptor chain).
	 */
	HAL_TXQ_RDYTIME_EXP_POLICY_ENABLE = 0x0020,
	/*
	 * Schedule frames on delivery of a DBA (DMA Beacon Alert)
	 * event.  Frames will be transmitted only when this timer
	 * fires, e.g to transmit a beacon in ap or adhoc modes.
	 */
	HAL_TXQ_DBA_GATED	    = 0x0040,	/* schedule based on DBA */
	/*
	 * Each transmit queue has a counter that is incremented
	 * each time the queue is enabled and decremented when
	 * the list of frames to transmit is traversed (or when
	 * the ready time for the queue expires).  This counter
	 * must be non-zero for frames to be scheduled for
	 * transmission.  The following controls disable bumping
	 * this counter under certain conditions.  Typically this
	 * is used to gate frames based on the contents of another
	 * queue (e.g. CAB traffic may only follow a beacon frame).
	 * These are meaningful only when frames are scheduled
	 * with a non-ASAP policy (e.g. DBA-gated).
	 */
	HAL_TXQ_CBR_DIS_QEMPTY	    = 0x0080,	/* disable on this q empty */
	HAL_TXQ_CBR_DIS_BEMPTY	    = 0x0100,	/* disable on beacon q empty */

	/*
	 * Fragment burst backoff policy.  Normally the no backoff
	 * is done after a successful transmission, the next fragment
	 * is sent at SIFS.  If this flag is set backoff is done
	 * after each fragment, regardless whether it was ack'd or
	 * not, after the backoff count reaches zero a normal channel
	 * access procedure is done before the next transmit (i.e.
	 * wait AIFS instead of SIFS).
	 */
	HAL_TXQ_FRAG_BURST_BACKOFF_ENABLE = 0x00800000,
	/*
	 * Disable post-tx backoff following each frame.
	 */
	HAL_TXQ_BACKOFF_DISABLE	    = 0x00010000, /* disable post backoff  */
	/*
	 * DCU arbiter lockout control.  This controls how
	 * lower priority tx queues are handled with respect to
	 * to a specific queue when multiple queues have frames
	 * to send.  No lockout means lower priority queues arbitrate
	 * concurrently with this queue.  Intra-frame lockout
	 * means lower priority queues are locked out until the
	 * current frame transmits (e.g. including backoffs and bursting).
	 * Global lockout means nothing lower can arbitrary so
	 * long as there is traffic activity on this queue (frames,
	 * backoff, etc).
	 */
	HAL_TXQ_ARB_LOCKOUT_INTRA   = 0x00020000, /* intra-frame lockout */
	HAL_TXQ_ARB_LOCKOUT_GLOBAL  = 0x00040000, /* full lockout s */

	HAL_TXQ_IGNORE_VIRTCOL	    = 0x00080000, /* ignore virt collisions */
	HAL_TXQ_SEQNUM_INC_DIS	    = 0x00100000, /* disable seqnum increment */
} HAL_TX_QUEUE_FLAGS;

typedef struct {
	uint32_t	tqi_ver;		/* hal TXQ version */
	HAL_TX_QUEUE_SUBTYPE tqi_subtype;	/* subtype if applicable */
	HAL_TX_QUEUE_FLAGS tqi_qflags;		/* flags (see above) */
	uint32_t	tqi_priority;		/* (not used) */
	uint32_t	tqi_aifs;		/* aifs */
	uint32_t	tqi_cwmin;		/* cwMin */
	uint32_t	tqi_cwmax;		/* cwMax */
	uint16_t	tqi_shretry;		/* rts retry limit */
	uint16_t	tqi_lgretry;		/* long retry limit (not used)*/
	uint32_t	tqi_cbrPeriod;		/* CBR period (us) */
	uint32_t	tqi_cbrOverflowLimit;	/* threshold for CBROVF int */
	uint32_t	tqi_burstTime;		/* max burst duration (us) */
	uint32_t	tqi_readyTime;		/* frame schedule time (us) */
	uint32_t	tqi_compBuf;		/* comp buffer phys addr */
} HAL_TXQ_INFO;

#define HAL_TQI_NONVAL 0xffff

/* token to use for aifs, cwmin, cwmax */
#define	HAL_TXQ_USEDEFAULT	((uint32_t) -1)

/* compression definitions */
#define HAL_COMP_BUF_MAX_SIZE           9216            /* 9K */
#define HAL_COMP_BUF_ALIGN_SIZE         512

/*
 * Transmit packet types.  This belongs in ah_desc.h, but
 * is here so we can give a proper type to various parameters
 * (and not require everyone include the file).
 *
 * NB: These values are intentionally assigned for
 *     direct use when setting up h/w descriptors.
 */
typedef enum {
	HAL_PKT_TYPE_NORMAL	= 0,
	HAL_PKT_TYPE_ATIM	= 1,
	HAL_PKT_TYPE_PSPOLL	= 2,
	HAL_PKT_TYPE_BEACON	= 3,
	HAL_PKT_TYPE_PROBE_RESP	= 4,
	HAL_PKT_TYPE_CHIRP	= 5,
	HAL_PKT_TYPE_GRP_POLL	= 6,
	HAL_PKT_TYPE_AMPDU	= 7,
} HAL_PKT_TYPE;

/* Rx Filter Frame Types */
typedef enum {
	/*
	 * These bits correspond to AR_RX_FILTER for all chips.
	 * Not all bits are supported by all chips.
	 */
	HAL_RX_FILTER_UCAST	= 0x00000001,	/* Allow unicast frames */
	HAL_RX_FILTER_MCAST	= 0x00000002,	/* Allow multicast frames */
	HAL_RX_FILTER_BCAST	= 0x00000004,	/* Allow broadcast frames */
	HAL_RX_FILTER_CONTROL	= 0x00000008,	/* Allow control frames */
	HAL_RX_FILTER_BEACON	= 0x00000010,	/* Allow beacon frames */
	HAL_RX_FILTER_PROM	= 0x00000020,	/* Promiscuous mode */
	HAL_RX_FILTER_PROBEREQ	= 0x00000080,	/* Allow probe request frames */
	HAL_RX_FILTER_PHYERR	= 0x00000100,	/* Allow phy errors */
	HAL_RX_FILTER_MYBEACON  = 0x00000200,   /* Filter beacons other than mine */
	HAL_RX_FILTER_COMPBAR	= 0x00000400,	/* Allow compressed BAR */
	HAL_RX_FILTER_COMP_BA	= 0x00000800,	/* Allow compressed blockack */
	HAL_RX_FILTER_PHYRADAR	= 0x00002000,	/* Allow phy radar errors */
	HAL_RX_FILTER_PSPOLL	= 0x00004000,	/* Allow PS-POLL frames */
	HAL_RX_FILTER_MCAST_BCAST_ALL	= 0x00008000,
						/* Allow all mcast/bcast frames */

	/*
	 * Magic RX filter flags that aren't targeting hardware bits
	 * but instead the HAL sets individual bits - eg PHYERR will result
	 * in OFDM/CCK timing error frames being received.
	 */
	HAL_RX_FILTER_BSSID	= 0x40000000,	/* Disable BSSID match */
} HAL_RX_FILTER;

typedef enum {
	HAL_PM_AWAKE		= 0,
	HAL_PM_FULL_SLEEP	= 1,
	HAL_PM_NETWORK_SLEEP	= 2,
	HAL_PM_UNDEFINED	= 3
} HAL_POWER_MODE;

/*
 * Enterprise mode flags
 */
#define	AH_ENT_DUAL_BAND_DISABLE	0x00000001
#define	AH_ENT_CHAIN2_DISABLE		0x00000002
#define	AH_ENT_5MHZ_DISABLE		0x00000004
#define	AH_ENT_10MHZ_DISABLE		0x00000008
#define	AH_ENT_49GHZ_DISABLE		0x00000010
#define	AH_ENT_LOOPBACK_DISABLE		0x00000020
#define	AH_ENT_TPC_PERF_DISABLE		0x00000040
#define	AH_ENT_MIN_PKT_SIZE_DISABLE	0x00000080
#define	AH_ENT_SPECTRAL_PRECISION	0x00000300
#define	AH_ENT_SPECTRAL_PRECISION_S	8
#define	AH_ENT_RTSCTS_DELIM_WAR		0x00010000

#define AH_FIRST_DESC_NDELIMS 60

/*
 * NOTE WELL:
 * These are mapped to take advantage of the common locations for many of
 * the bits on all of the currently supported MAC chips. This is to make
 * the ISR as efficient as possible, while still abstracting HW differences.
 * When new hardware breaks this commonality this enumerated type, as well
 * as the HAL functions using it, must be modified. All values are directly
 * mapped unless commented otherwise.
 */
typedef enum {
	HAL_INT_RX	= 0x00000001,	/* Non-common mapping */
	HAL_INT_RXDESC	= 0x00000002,	/* Legacy mapping */
	HAL_INT_RXERR	= 0x00000004,
	HAL_INT_RXHP	= 0x00000001,	/* EDMA */
	HAL_INT_RXLP	= 0x00000002,	/* EDMA */
	HAL_INT_RXNOFRM	= 0x00000008,
	HAL_INT_RXEOL	= 0x00000010,
	HAL_INT_RXORN	= 0x00000020,
	HAL_INT_TX	= 0x00000040,	/* Non-common mapping */
	HAL_INT_TXDESC	= 0x00000080,
	HAL_INT_TIM_TIMER= 0x00000100,
	HAL_INT_MCI	= 0x00000200,
	HAL_INT_BBPANIC	= 0x00000400,
	HAL_INT_TXURN	= 0x00000800,
	HAL_INT_MIB	= 0x00001000,
	HAL_INT_RXPHY	= 0x00004000,
	HAL_INT_RXKCM	= 0x00008000,
	HAL_INT_SWBA	= 0x00010000,
	HAL_INT_BRSSI	= 0x00020000,
	HAL_INT_BMISS	= 0x00040000,
	HAL_INT_BNR	= 0x00100000,
	HAL_INT_TIM	= 0x00200000,	/* Non-common mapping */
	HAL_INT_DTIM	= 0x00400000,	/* Non-common mapping */
	HAL_INT_DTIMSYNC= 0x00800000,	/* Non-common mapping */
	HAL_INT_GPIO	= 0x01000000,
	HAL_INT_CABEND	= 0x02000000,	/* Non-common mapping */
	HAL_INT_TSFOOR	= 0x04000000,	/* Non-common mapping */
	HAL_INT_TBTT	= 0x08000000,	/* Non-common mapping */
	/* Atheros ref driver has a generic timer interrupt now..*/
	HAL_INT_GENTIMER	= 0x08000000,	/* Non-common mapping */
	HAL_INT_CST	= 0x10000000,	/* Non-common mapping */
	HAL_INT_GTT	= 0x20000000,	/* Non-common mapping */
	HAL_INT_FATAL	= 0x40000000,	/* Non-common mapping */
#define	HAL_INT_GLOBAL	0x80000000	/* Set/clear IER */
	HAL_INT_BMISC	= HAL_INT_TIM
			| HAL_INT_DTIM
			| HAL_INT_DTIMSYNC
			| HAL_INT_CABEND
			| HAL_INT_TBTT,

	/* Interrupt bits that map directly to ISR/IMR bits */
	HAL_INT_COMMON  = HAL_INT_RXNOFRM
			| HAL_INT_RXDESC
			| HAL_INT_RXEOL
			| HAL_INT_RXORN
			| HAL_INT_TXDESC
			| HAL_INT_TXURN
			| HAL_INT_MIB
			| HAL_INT_RXPHY
			| HAL_INT_RXKCM
			| HAL_INT_SWBA
			| HAL_INT_BMISS
			| HAL_INT_BRSSI
			| HAL_INT_BNR
			| HAL_INT_GPIO,
} HAL_INT;

/*
 * MSI vector assignments
 */
typedef enum {
	HAL_MSIVEC_MISC = 0,
	HAL_MSIVEC_TX   = 1,
	HAL_MSIVEC_RXLP = 2,
	HAL_MSIVEC_RXHP = 3,
} HAL_MSIVEC;

typedef enum {
	HAL_INT_LINE = 0,
	HAL_INT_MSI  = 1,
} HAL_INT_TYPE;

/* For interrupt mitigation registers */
typedef enum {
	HAL_INT_RX_FIRSTPKT=0,
	HAL_INT_RX_LASTPKT,
	HAL_INT_TX_FIRSTPKT,
	HAL_INT_TX_LASTPKT,
	HAL_INT_THRESHOLD
} HAL_INT_MITIGATION;

/* XXX this is duplicate information! */
typedef struct {
	u_int32_t	cyclecnt_diff;		/* delta cycle count */
	u_int32_t	rxclr_cnt;		/* rx clear count */
	u_int32_t	extrxclr_cnt;		/* ext chan rx clear count */
	u_int32_t	txframecnt_diff;	/* delta tx frame count */
	u_int32_t	rxframecnt_diff;	/* delta rx frame count */
	u_int32_t	listen_time;		/* listen time in msec - time for which ch is free */
	u_int32_t	ofdmphyerr_cnt;		/* OFDM err count since last reset */
	u_int32_t	cckphyerr_cnt;		/* CCK err count since last reset */
	u_int32_t	ofdmphyerrcnt_diff;	/* delta OFDM Phy Error Count */
	HAL_BOOL	valid;			/* if the stats are valid*/
} HAL_ANISTATS;

typedef struct {
	u_int8_t	txctl_offset;
	u_int8_t	txctl_numwords;
	u_int8_t	txstatus_offset;
	u_int8_t	txstatus_numwords;

	u_int8_t	rxctl_offset;
	u_int8_t	rxctl_numwords;
	u_int8_t	rxstatus_offset;
	u_int8_t	rxstatus_numwords;

	u_int8_t	macRevision;
} HAL_DESC_INFO;

typedef enum {
	HAL_GPIO_OUTPUT_MUX_AS_OUTPUT		= 0,
	HAL_GPIO_OUTPUT_MUX_PCIE_ATTENTION_LED	= 1,
	HAL_GPIO_OUTPUT_MUX_PCIE_POWER_LED	= 2,
	HAL_GPIO_OUTPUT_MUX_MAC_NETWORK_LED	= 3,
	HAL_GPIO_OUTPUT_MUX_MAC_POWER_LED	= 4,
	HAL_GPIO_OUTPUT_MUX_AS_WLAN_ACTIVE	= 5,
	HAL_GPIO_OUTPUT_MUX_AS_TX_FRAME		= 6,

	HAL_GPIO_OUTPUT_MUX_AS_MCI_WLAN_DATA,
	HAL_GPIO_OUTPUT_MUX_AS_MCI_WLAN_CLK,
	HAL_GPIO_OUTPUT_MUX_AS_MCI_BT_DATA,
	HAL_GPIO_OUTPUT_MUX_AS_MCI_BT_CLK,
	HAL_GPIO_OUTPUT_MUX_AS_WL_IN_TX,
	HAL_GPIO_OUTPUT_MUX_AS_WL_IN_RX,
	HAL_GPIO_OUTPUT_MUX_AS_BT_IN_TX,
	HAL_GPIO_OUTPUT_MUX_AS_BT_IN_RX,
	HAL_GPIO_OUTPUT_MUX_AS_RUCKUS_STROBE,
	HAL_GPIO_OUTPUT_MUX_AS_RUCKUS_DATA,
	HAL_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL0,
	HAL_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL1,
	HAL_GPIO_OUTPUT_MUX_AS_SMARTANT_CTRL2,
	HAL_GPIO_OUTPUT_MUX_NUM_ENTRIES
} HAL_GPIO_MUX_TYPE;

typedef enum {
	HAL_GPIO_INTR_LOW		= 0,
	HAL_GPIO_INTR_HIGH		= 1,
	HAL_GPIO_INTR_DISABLE		= 2
} HAL_GPIO_INTR_TYPE;

typedef struct halCounters {
    u_int32_t   tx_frame_count;
    u_int32_t   rx_frame_count;
    u_int32_t   rx_clear_count;
    u_int32_t   cycle_count;
    u_int8_t    is_rx_active;     // true (1) or false (0)
    u_int8_t    is_tx_active;     // true (1) or false (0)
} HAL_COUNTERS;

typedef enum {
	HAL_RFGAIN_INACTIVE		= 0,
	HAL_RFGAIN_READ_REQUESTED	= 1,
	HAL_RFGAIN_NEED_CHANGE		= 2
} HAL_RFGAIN;

typedef uint16_t HAL_CTRY_CODE;		/* country code */
typedef uint16_t HAL_REG_DOMAIN;		/* regulatory domain code */

#define HAL_ANTENNA_MIN_MODE  0
#define HAL_ANTENNA_FIXED_A   1
#define HAL_ANTENNA_FIXED_B   2
#define HAL_ANTENNA_MAX_MODE  3

typedef struct {
	uint32_t	ackrcv_bad;
	uint32_t	rts_bad;
	uint32_t	rts_good;
	uint32_t	fcs_bad;
	uint32_t	beacons;
} HAL_MIB_STATS;

/*
 * These bits represent what's in ah_currentRDext.
 */
typedef enum {
	REG_EXT_FCC_MIDBAND		= 0,
	REG_EXT_JAPAN_MIDBAND		= 1,
	REG_EXT_FCC_DFS_HT40		= 2,
	REG_EXT_JAPAN_NONDFS_HT40	= 3,
	REG_EXT_JAPAN_DFS_HT40		= 4,
	REG_EXT_FCC_CH_144		= 5,
} REG_EXT_BITMAP;

enum {
	HAL_MODE_11A	= 0x001,		/* 11a channels */
	HAL_MODE_TURBO	= 0x002,		/* 11a turbo-only channels */
	HAL_MODE_11B	= 0x004,		/* 11b channels */
	HAL_MODE_PUREG	= 0x008,		/* 11g channels (OFDM only) */
#ifdef notdef
	HAL_MODE_11G	= 0x010,		/* 11g channels (OFDM/CCK) */
#else
	HAL_MODE_11G	= 0x008,		/* XXX historical */
#endif
	HAL_MODE_108G	= 0x020,		/* 11g+Turbo channels */
	HAL_MODE_108A	= 0x040,		/* 11a+Turbo channels */
	HAL_MODE_11A_HALF_RATE = 0x200,		/* 11a half width channels */
	HAL_MODE_11A_QUARTER_RATE = 0x400,	/* 11a quarter width channels */
	HAL_MODE_11G_HALF_RATE = 0x800,		/* 11g half width channels */
	HAL_MODE_11G_QUARTER_RATE = 0x1000,	/* 11g quarter width channels */
	HAL_MODE_11NG_HT20	= 0x008000,
	HAL_MODE_11NA_HT20  	= 0x010000,
	HAL_MODE_11NG_HT40PLUS	= 0x020000,
	HAL_MODE_11NG_HT40MINUS	= 0x040000,
	HAL_MODE_11NA_HT40PLUS	= 0x080000,
	HAL_MODE_11NA_HT40MINUS	= 0x100000,
	HAL_MODE_ALL	= 0xffffff
};

typedef struct {
	int		rateCount;		/* NB: for proper padding */
	uint8_t		rateCodeToIndex[256];	/* back mapping */
	struct {
		uint8_t		valid;		/* valid for rate control use */
		uint8_t		phy;		/* CCK/OFDM/XR */
		uint32_t	rateKbps;	/* transfer rate in kbs */
		uint8_t		rateCode;	/* rate for h/w descriptors */
		uint8_t		shortPreamble;	/* mask for enabling short
						 * preamble in CCK rate code */
		uint8_t		dot11Rate;	/* value for supported rates
						 * info element of MLME */
		uint8_t		controlRate;	/* index of next lower basic
						 * rate; used for dur. calcs */
		uint16_t	lpAckDuration;	/* long preamble ACK duration */
		uint16_t	spAckDuration;	/* short preamble ACK duration*/
	} info[64];
} HAL_RATE_TABLE;

typedef struct {
	u_int		rs_count;		/* number of valid entries */
	uint8_t	rs_rates[64];		/* rates */
} HAL_RATE_SET;

/*
 * 802.11n specific structures and enums
 */
typedef enum {
	HAL_CHAINTYPE_TX	= 1,	/* Tx chain type */
	HAL_CHAINTYPE_RX	= 2,	/* RX chain type */
} HAL_CHAIN_TYPE;

typedef struct {
	u_int	Tries;
	u_int	Rate;		/* hardware rate code */
	u_int	RateIndex;	/* rate series table index */
	u_int	PktDuration;
	u_int	ChSel;
	u_int	RateFlags;
#define	HAL_RATESERIES_RTS_CTS		0x0001	/* use rts/cts w/this series */
#define	HAL_RATESERIES_2040		0x0002	/* use ext channel for series */
#define	HAL_RATESERIES_HALFGI		0x0004	/* use half-gi for series */
#define	HAL_RATESERIES_STBC		0x0008	/* use STBC for series */
	u_int	tx_power_cap;		/* in 1/2 dBm units XXX TODO */
} HAL_11N_RATE_SERIES;

typedef enum {
	HAL_HT_MACMODE_20	= 0,	/* 20 MHz operation */
	HAL_HT_MACMODE_2040	= 1,	/* 20/40 MHz operation */
} HAL_HT_MACMODE;

typedef enum {
	HAL_HT_PHYMODE_20	= 0,	/* 20 MHz operation */
	HAL_HT_PHYMODE_2040	= 1,	/* 20/40 MHz operation */
} HAL_HT_PHYMODE;

typedef enum {
	HAL_HT_EXTPROTSPACING_20 = 0,	/* 20 MHz spacing */
	HAL_HT_EXTPROTSPACING_25 = 1,	/* 25 MHz spacing */
} HAL_HT_EXTPROTSPACING;


typedef enum {
	HAL_RX_CLEAR_CTL_LOW	= 0x1,	/* force control channel to appear busy */
	HAL_RX_CLEAR_EXT_LOW	= 0x2,	/* force extension channel to appear busy */
} HAL_HT_RXCLEAR;

typedef enum {
	HAL_FREQ_BAND_5GHZ	= 0,
	HAL_FREQ_BAND_2GHZ	= 1,
} HAL_FREQ_BAND;

/*
 * Antenna switch control.  By default antenna selection
 * enables multiple (2) antenna use.  To force use of the
 * A or B antenna only specify a fixed setting.  Fixing
 * the antenna will also disable any diversity support.
 */
typedef enum {
	HAL_ANT_VARIABLE = 0,			/* variable by programming */
	HAL_ANT_FIXED_A	 = 1,			/* fixed antenna A */
	HAL_ANT_FIXED_B	 = 2,			/* fixed antenna B */
} HAL_ANT_SETTING;

typedef enum {
	HAL_M_STA	= 1,			/* infrastructure station */
	HAL_M_IBSS	= 0,			/* IBSS (adhoc) station */
	HAL_M_HOSTAP	= 6,			/* Software Access Point */
	HAL_M_MONITOR	= 8			/* Monitor mode */
} HAL_OPMODE;

typedef enum {
	HAL_RESET_NORMAL	= 0,		/* Do normal reset */
	HAL_RESET_BBPANIC	= 1,		/* Reset because of BB panic */
	HAL_RESET_FORCE_COLD	= 2,		/* Force full reset */
} HAL_RESET_TYPE;

enum {
	HAL_RESET_POWER_ON,
	HAL_RESET_WARM,
	HAL_RESET_COLD
};

typedef struct {
	uint8_t		kv_type;		/* one of HAL_CIPHER */
	uint8_t		kv_apsd;		/* Mask for APSD enabled ACs */
	uint16_t	kv_len;			/* length in bits */
	uint8_t		kv_val[16];		/* enough for 128-bit keys */
	uint8_t		kv_mic[8];		/* TKIP MIC key */
	uint8_t		kv_txmic[8];		/* TKIP TX MIC key (optional) */
} HAL_KEYVAL;

/*
 * This is the TX descriptor field which marks the key padding requirement.
 * The naming is unfortunately unclear.
 */
#define AH_KEYTYPE_MASK     0x0F
typedef enum {
    HAL_KEY_TYPE_CLEAR,
    HAL_KEY_TYPE_WEP,
    HAL_KEY_TYPE_AES,
    HAL_KEY_TYPE_TKIP,
} HAL_KEY_TYPE;

typedef enum {
	HAL_CIPHER_WEP		= 0,
	HAL_CIPHER_AES_OCB	= 1,
	HAL_CIPHER_AES_CCM	= 2,
	HAL_CIPHER_CKIP		= 3,
	HAL_CIPHER_TKIP		= 4,
	HAL_CIPHER_CLR		= 5,		/* no encryption */

	HAL_CIPHER_MIC		= 127		/* TKIP-MIC, not a cipher */
} HAL_CIPHER;

enum {
	HAL_SLOT_TIME_6	 = 6,			/* NB: for turbo mode */
	HAL_SLOT_TIME_9	 = 9,
	HAL_SLOT_TIME_20 = 20,
};

/*
 * Per-station beacon timer state.  Note that the specified
 * beacon interval (given in TU's) can also include flags
 * to force a TSF reset and to enable the beacon xmit logic.
 * If bs_cfpmaxduration is non-zero the hardware is setup to
 * coexist with a PCF-capable AP.
 */
typedef struct {
	uint32_t	bs_nexttbtt;		/* next beacon in TU */
	uint32_t	bs_nextdtim;		/* next DTIM in TU */
	uint32_t	bs_intval;		/* beacon interval+flags */
/*
 * HAL_BEACON_PERIOD, HAL_BEACON_ENA and HAL_BEACON_RESET_TSF
 * are all 1:1 correspondances with the pre-11n chip AR_BEACON
 * register.
 */
#define	HAL_BEACON_PERIOD	0x0000ffff	/* beacon interval period */
#define	HAL_BEACON_PERIOD_TU8	0x0007ffff	/* beacon interval, tu/8 */
#define	HAL_BEACON_ENA		0x00800000	/* beacon xmit enable */
#define	HAL_BEACON_RESET_TSF	0x01000000	/* clear TSF */
#define	HAL_TSFOOR_THRESHOLD	0x00004240	/* TSF OOR thresh (16k uS) */
	uint32_t	bs_dtimperiod;
	uint16_t	bs_cfpperiod;		/* CFP period in TU */
	uint16_t	bs_cfpmaxduration;	/* max CFP duration in TU */
	uint32_t	bs_cfpnext;		/* next CFP in TU */
	uint16_t	bs_timoffset;		/* byte offset to TIM bitmap */
	uint16_t	bs_bmissthreshold;	/* beacon miss threshold */
	uint32_t	bs_sleepduration;	/* max sleep duration */
	uint32_t	bs_tsfoor_threshold;	/* TSF out of range threshold */
} HAL_BEACON_STATE;

/*
 * Like HAL_BEACON_STATE but for non-station mode setup.
 * NB: see above flag definitions for bt_intval. 
 */
typedef struct {
	uint32_t	bt_intval;		/* beacon interval+flags */
	uint32_t	bt_nexttbtt;		/* next beacon in TU */
	uint32_t	bt_nextatim;		/* next ATIM in TU */
	uint32_t	bt_nextdba;		/* next DBA in 1/8th TU */
	uint32_t	bt_nextswba;		/* next SWBA in 1/8th TU */
	uint32_t	bt_flags;		/* timer enables */
#define HAL_BEACON_TBTT_EN	0x00000001
#define HAL_BEACON_DBA_EN	0x00000002
#define HAL_BEACON_SWBA_EN	0x00000004
} HAL_BEACON_TIMERS;

/*
 * Per-node statistics maintained by the driver for use in
 * optimizing signal quality and other operational aspects.
 */
typedef struct {
	uint32_t	ns_avgbrssi;	/* average beacon rssi */
	uint32_t	ns_avgrssi;	/* average data rssi */
	uint32_t	ns_avgtxrssi;	/* average tx rssi */
} HAL_NODE_STATS;

#define	HAL_RSSI_EP_MULTIPLIER	(1<<7)	/* pow2 to optimize out * and / */

/*
 * This is the ANI state and MIB stats.
 *
 * It's used by the HAL modules to keep state /and/ by the debug ioctl
 * to fetch ANI information.
 */
typedef struct {
	uint32_t	ast_ani_niup;   /* ANI increased noise immunity */
	uint32_t	ast_ani_nidown; /* ANI decreased noise immunity */
	uint32_t	ast_ani_spurup; /* ANI increased spur immunity */
	uint32_t	ast_ani_spurdown;/* ANI descreased spur immunity */
	uint32_t	ast_ani_ofdmon; /* ANI OFDM weak signal detect on */
	uint32_t	ast_ani_ofdmoff;/* ANI OFDM weak signal detect off */
	uint32_t	ast_ani_cckhigh;/* ANI CCK weak signal threshold high */
	uint32_t	ast_ani_ccklow; /* ANI CCK weak signal threshold low */
	uint32_t	ast_ani_stepup; /* ANI increased first step level */
	uint32_t	ast_ani_stepdown;/* ANI decreased first step level */
	uint32_t	ast_ani_ofdmerrs;/* ANI cumulative ofdm phy err count */
	uint32_t	ast_ani_cckerrs;/* ANI cumulative cck phy err count */
	uint32_t	ast_ani_reset;  /* ANI parameters zero'd for non-STA */
	uint32_t	ast_ani_lzero;  /* ANI listen time forced to zero */
	uint32_t	ast_ani_lneg;   /* ANI listen time calculated < 0 */
	HAL_MIB_STATS	ast_mibstats;   /* MIB counter stats */
	HAL_NODE_STATS	ast_nodestats;  /* Latest rssi stats from driver */
} HAL_ANI_STATS;

typedef struct {
	uint8_t		noiseImmunityLevel; /* Global for pre-AR9380; OFDM later*/
	uint8_t		cckNoiseImmunityLevel; /* AR9380: CCK specific NI */
	uint8_t		spurImmunityLevel;
	uint8_t		firstepLevel;
	uint8_t		ofdmWeakSigDetectOff;
	uint8_t		cckWeakSigThreshold;
	uint8_t		mrcCck;		/* MRC CCK is enabled */
	uint32_t	listenTime;

	/* NB: intentionally ordered so data exported to user space is first */
	uint32_t	txFrameCount;   /* Last txFrameCount */
	uint32_t	rxFrameCount;   /* Last rx Frame count */
	uint32_t	cycleCount;     /* Last cycleCount
					   (to detect wrap-around) */
	uint32_t	ofdmPhyErrCount;/* OFDM err count since last reset */
	uint32_t	cckPhyErrCount; /* CCK err count since last reset */
} HAL_ANI_STATE;

struct ath_desc;
struct ath_tx_status;
struct ath_rx_status;
struct ieee80211_channel;

/*
 * This is a channel survey sample entry.
 *
 * The AR5212 ANI routines fill these samples. The ANI code then uses it
 * when calculating listen time; it is also exported via a diagnostic
 * API.
 */
typedef struct {
	uint32_t        seq_num;
	uint32_t        tx_busy;
	uint32_t        rx_busy;
	uint32_t        chan_busy;
	uint32_t        ext_chan_busy;
	uint32_t        cycle_count;
	/* XXX TODO */
	uint32_t        ofdm_phyerr_count;
	uint32_t        cck_phyerr_count;
} HAL_SURVEY_SAMPLE;

/*
 * This provides 3.2 seconds of sample space given an
 * ANI time of 1/10th of a second. This may not be enough!
 */
#define	CHANNEL_SURVEY_SAMPLE_COUNT	32

typedef struct {
	HAL_SURVEY_SAMPLE samples[CHANNEL_SURVEY_SAMPLE_COUNT];
	uint32_t cur_sample;	/* current sample in sequence */
	uint32_t cur_seq;	/* current sequence number */
} HAL_CHANNEL_SURVEY;

/*
 * ANI commands.
 *
 * These are used both internally and externally via the diagnostic
 * API.
 *
 * Note that this is NOT the ANI commands being used via the INTMIT
 * capability - that has a different mapping for some reason.
 */
typedef enum {
	HAL_ANI_PRESENT = 0,			/* is ANI support present */
	HAL_ANI_NOISE_IMMUNITY_LEVEL = 1,	/* set level (global or ofdm) */
	HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION = 2,	/* enable/disable */
	HAL_ANI_CCK_WEAK_SIGNAL_THR = 3,	/* enable/disable */
	HAL_ANI_FIRSTEP_LEVEL = 4,		/* set level */
	HAL_ANI_SPUR_IMMUNITY_LEVEL = 5,	/* set level */
	HAL_ANI_MODE = 6,			/* 0 => manual, 1 => auto (XXX do not change) */
	HAL_ANI_PHYERR_RESET = 7,		/* reset phy error stats */
	HAL_ANI_MRC_CCK = 8,
	HAL_ANI_CCK_NOISE_IMMUNITY_LEVEL = 9,	/* set level (cck) */
} HAL_ANI_CMD;

#define	HAL_ANI_ALL		0xffffffff

/*
 * This is the layout of the ANI INTMIT capability.
 *
 * Notice that the command values differ to HAL_ANI_CMD.
 */
typedef enum {
	HAL_CAP_INTMIT_PRESENT = 0,
	HAL_CAP_INTMIT_ENABLE = 1,
	HAL_CAP_INTMIT_NOISE_IMMUNITY_LEVEL = 2,
	HAL_CAP_INTMIT_OFDM_WEAK_SIGNAL_LEVEL = 3,
	HAL_CAP_INTMIT_CCK_WEAK_SIGNAL_THR = 4,
	HAL_CAP_INTMIT_FIRSTEP_LEVEL = 5,
	HAL_CAP_INTMIT_SPUR_IMMUNITY_LEVEL = 6
} HAL_CAP_INTMIT_CMD;

typedef struct {
	int32_t		pe_firpwr;	/* FIR pwr out threshold */
	int32_t		pe_rrssi;	/* Radar rssi thresh */
	int32_t		pe_height;	/* Pulse height thresh */
	int32_t		pe_prssi;	/* Pulse rssi thresh */
	int32_t		pe_inband;	/* Inband thresh */

	/* The following params are only for AR5413 and later */
	u_int32_t	pe_relpwr;	/* Relative power threshold in 0.5dB steps */
	u_int32_t	pe_relstep;	/* Pulse Relative step threshold in 0.5dB steps */
	u_int32_t	pe_maxlen;	/* Max length of radar sign in 0.8us units */
	int32_t		pe_usefir128;	/* Use the average in-band power measured over 128 cycles */
	int32_t		pe_blockradar;	/*
					 * Enable to block radar check if pkt detect is done via OFDM
					 * weak signal detect or pkt is detected immediately after tx
					 * to rx transition
					 */
	int32_t		pe_enmaxrssi;	/*
					 * Enable to use the max rssi instead of the last rssi during
					 * fine gain changes for radar detection
					 */
	int32_t		pe_extchannel;	/* Enable DFS on ext channel */
	int32_t		pe_enabled;	/* Whether radar detection is enabled */
	int32_t		pe_enrelpwr;
	int32_t		pe_en_relstep_check;
} HAL_PHYERR_PARAM;

#define	HAL_PHYERR_PARAM_NOVAL	65535

typedef struct {
	u_int16_t	ss_fft_period;	/* Skip interval for FFT reports */
	u_int16_t	ss_period;	/* Spectral scan period */
	u_int16_t	ss_count;	/* # of reports to return from ss_active */
	u_int16_t	ss_short_report;/* Set to report ony 1 set of FFT results */
	u_int8_t	radar_bin_thresh_sel;	/* strong signal radar FFT threshold configuration */
	u_int16_t	ss_spectral_pri;		/* are we doing a noise power cal ? */
	int8_t		ss_nf_cal[AH_MAX_CHAINS*2];     /* nf calibrated values for ctl+ext from eeprom */
	int8_t		ss_nf_pwr[AH_MAX_CHAINS*2];     /* nf pwr values for ctl+ext from eeprom */
	int32_t		ss_nf_temp_data;	/* temperature data taken during nf scan */
	int		ss_enabled;
	int		ss_active;
} HAL_SPECTRAL_PARAM;
#define	HAL_SPECTRAL_PARAM_NOVAL	0xFFFF
#define	HAL_SPECTRAL_PARAM_ENABLE	0x8000	/* Enable/Disable if applicable */

/*
 * DFS operating mode flags.
 */
typedef enum {
	HAL_DFS_UNINIT_DOMAIN	= 0,	/* Uninitialized dfs domain */
	HAL_DFS_FCC_DOMAIN	= 1,	/* FCC3 dfs domain */
	HAL_DFS_ETSI_DOMAIN	= 2,	/* ETSI dfs domain */
	HAL_DFS_MKK4_DOMAIN	= 3,	/* Japan dfs domain */
} HAL_DFS_DOMAIN;


/*
 * MFP decryption options for initializing the MAC.
 */
typedef enum {
	HAL_MFP_QOSDATA = 0,	/* Decrypt MFP frames like QoS data frames. All chips before Merlin. */
	HAL_MFP_PASSTHRU,	/* Don't decrypt MFP frames at all. Passthrough */
	HAL_MFP_HW_CRYPTO	/* hardware decryption enabled. Merlin can do it. */
} HAL_MFP_OPT_T;

/* LNA config supported */
typedef enum {
	HAL_ANT_DIV_COMB_LNA1_MINUS_LNA2	= 0,
	HAL_ANT_DIV_COMB_LNA2			= 1,
	HAL_ANT_DIV_COMB_LNA1			= 2,
	HAL_ANT_DIV_COMB_LNA1_PLUS_LNA2		= 3,
} HAL_ANT_DIV_COMB_LNA_CONF;

typedef struct {
	u_int8_t	main_lna_conf;
	u_int8_t	alt_lna_conf;
	u_int8_t	fast_div_bias;
	u_int8_t	main_gaintb;
	u_int8_t	alt_gaintb;
	u_int8_t	antdiv_configgroup;
	int8_t		lna1_lna2_delta;
} HAL_ANT_COMB_CONFIG;

#define	DEFAULT_ANTDIV_CONFIG_GROUP	0x00
#define	HAL_ANTDIV_CONFIG_GROUP_1	0x01
#define	HAL_ANTDIV_CONFIG_GROUP_2	0x02
#define	HAL_ANTDIV_CONFIG_GROUP_3	0x03

/*
 * Flag for setting QUIET period
 */
typedef enum {
	HAL_QUIET_DISABLE		= 0x0,
	HAL_QUIET_ENABLE		= 0x1,
	HAL_QUIET_ADD_CURRENT_TSF	= 0x2,	/* add current TSF to next_start offset */
	HAL_QUIET_ADD_SWBA_RESP_TIME	= 0x4,	/* add beacon response time to next_start offset */
} HAL_QUIET_FLAG;

#define	HAL_DFS_EVENT_PRICH		0x0000001
#define	HAL_DFS_EVENT_EXTCH		0x0000002
#define	HAL_DFS_EVENT_EXTEARLY		0x0000004
#define	HAL_DFS_EVENT_ISDC		0x0000008

struct hal_dfs_event {
	uint64_t	re_full_ts;	/* 64-bit full timestamp from interrupt time */
	uint32_t	re_ts;		/* Original 15 bit recv timestamp */
	uint8_t		re_rssi;	/* rssi of radar event */
	uint8_t		re_dur;		/* duration of radar pulse */
	uint32_t	re_flags;	/* Flags (see above) */
};
typedef struct hal_dfs_event HAL_DFS_EVENT;

/*
 * Generic Timer domain
 */
typedef enum {
	HAL_GEN_TIMER_TSF = 0,
	HAL_GEN_TIMER_TSF2,
	HAL_GEN_TIMER_TSF_ANY
} HAL_GEN_TIMER_DOMAIN;

/*
 * BT Co-existence definitions
 */
#include "ath_hal/ah_btcoex.h"

struct hal_bb_panic_info {
	u_int32_t	status;
	u_int32_t	tsf;
	u_int32_t	phy_panic_wd_ctl1;
	u_int32_t	phy_panic_wd_ctl2;
	u_int32_t	phy_gen_ctrl;
	u_int32_t	rxc_pcnt;
	u_int32_t	rxf_pcnt;
	u_int32_t	txf_pcnt;
	u_int32_t	cycles;
	u_int32_t	wd;
	u_int32_t	det;
	u_int32_t	rdar;
	u_int32_t	r_odfm;
	u_int32_t	r_cck;
	u_int32_t	t_odfm;
	u_int32_t	t_cck;
	u_int32_t	agc;
	u_int32_t	src;
};

/* Serialize Register Access Mode */
typedef enum {
	SER_REG_MODE_OFF	= 0,
	SER_REG_MODE_ON		= 1,
	SER_REG_MODE_AUTO	= 2,
} SER_REG_MODE;

typedef struct
{
	int ah_debug;			/* only used if AH_DEBUG is defined */
	int ah_ar5416_biasadj;		/* enable AR2133 radio specific bias fiddling */

	/* NB: these are deprecated; they exist for now for compatibility */
	int ah_dma_beacon_response_time;/* in TU's */
	int ah_sw_beacon_response_time;	/* in TU's */
	int ah_additional_swba_backoff;	/* in TU's */
	int ah_force_full_reset;	/* force full chip reset rather then warm reset */
	int ah_serialise_reg_war;	/* force serialisation of register IO */

	/* XXX these don't belong here, they're just for the ar9300  HAL port effort */
	int ath_hal_desc_tpc;		/* Per-packet TPC */
	int ath_hal_sta_update_tx_pwr_enable;	/* GreenTX */
	int ath_hal_sta_update_tx_pwr_enable_S1;	/* GreenTX */
	int ath_hal_sta_update_tx_pwr_enable_S2;	/* GreenTX */
	int ath_hal_sta_update_tx_pwr_enable_S3;	/* GreenTX */

	/* I'm not sure what the default values for these should be */
	int ath_hal_pll_pwr_save;
	int ath_hal_pcie_power_save_enable;
	int ath_hal_intr_mitigation_rx;
	int ath_hal_intr_mitigation_tx;

	int ath_hal_pcie_clock_req;
#define	AR_PCIE_PLL_PWRSAVE_CONTROL	(1<<0)
#define	AR_PCIE_PLL_PWRSAVE_ON_D3	(1<<1)
#define	AR_PCIE_PLL_PWRSAVE_ON_D0	(1<<2)

	int ath_hal_pcie_waen;
	int ath_hal_pcie_ser_des_write;

	/* these are important for correct AR9300 behaviour */
	int ath_hal_ht_enable;		/* needs to be enabled for AR9300 HT */
	int ath_hal_diversity_control;
	int ath_hal_antenna_switch_swap;
	int ath_hal_ext_lna_ctl_gpio;
	int ath_hal_spur_mode;
	int ath_hal_6mb_ack;		/* should set this to 1 for 11a/11na? */
	int ath_hal_enable_msi;		/* enable MSI interrupts (needed?) */
	int ath_hal_beacon_filter_interval;	/* ok to be 0 for now? */

	/* For now, set this to 0 - net80211 needs to know about hardware MFP support */
	int ath_hal_mfp_support;

	int ath_hal_enable_ani;	/* should set this.. */
	int ath_hal_cwm_ignore_ext_cca;
	int ath_hal_show_bb_panic;
	int ath_hal_ant_ctrl_comm2g_switch_enable;
	int ath_hal_ext_atten_margin_cfg;
	int ath_hal_min_gainidx;
	int ath_hal_war70c;
	uint32_t ath_hal_mci_config;
} HAL_OPS_CONFIG;

/*
 * Hardware Access Layer (HAL) API.
 *
 * Clients of the HAL call ath_hal_attach to obtain a reference to an
 * ath_hal structure for use with the device.  Hardware-related operations
 * that follow must call back into the HAL through interface, supplying
 * the reference as the first parameter.  Note that before using the
 * reference returned by ath_hal_attach the caller should verify the
 * ABI version number.
 */
struct ath_hal {
	uint32_t	ah_magic;	/* consistency check magic number */
	uint16_t	ah_devid;	/* PCI device ID */
	uint16_t	ah_subvendorid;	/* PCI subvendor ID */
	HAL_SOFTC	ah_sc;		/* back pointer to driver/os state */
	HAL_BUS_TAG	ah_st;		/* params for register r+w */
	HAL_BUS_HANDLE	ah_sh;
	HAL_CTRY_CODE	ah_countryCode;

	uint32_t	ah_macVersion;	/* MAC version id */
	uint16_t	ah_macRev;	/* MAC revision */
	uint16_t	ah_phyRev;	/* PHY revision */
	/* NB: when only one radio is present the rev is in 5Ghz */
	uint16_t	ah_analog5GhzRev;/* 5GHz radio revision */
	uint16_t	ah_analog2GhzRev;/* 2GHz radio revision */

	uint16_t	*ah_eepromdata;	/* eeprom buffer, if needed */

	uint32_t	ah_intrstate[8];	/* last int state */
	uint32_t	ah_syncstate;		/* last sync intr state */

	/* Current powerstate from HAL calls */
	HAL_POWER_MODE	ah_powerMode;

	HAL_OPS_CONFIG ah_config;
	const HAL_RATE_TABLE *__ahdecl(*ah_getRateTable)(struct ath_hal *,
				u_int mode);
	void	  __ahdecl(*ah_detach)(struct ath_hal*);

	/* Reset functions */
	HAL_BOOL  __ahdecl(*ah_reset)(struct ath_hal *, HAL_OPMODE,
				struct ieee80211_channel *,
				HAL_BOOL bChannelChange,
				HAL_RESET_TYPE resetType,
				HAL_STATUS *status);
	HAL_BOOL  __ahdecl(*ah_phyDisable)(struct ath_hal *);
	HAL_BOOL  __ahdecl(*ah_disable)(struct ath_hal *);
	void	  __ahdecl(*ah_configPCIE)(struct ath_hal *, HAL_BOOL restore,
				HAL_BOOL power_off);
	void	  __ahdecl(*ah_disablePCIE)(struct ath_hal *);
	void	  __ahdecl(*ah_setPCUConfig)(struct ath_hal *);
	HAL_BOOL  __ahdecl(*ah_perCalibration)(struct ath_hal*,
			struct ieee80211_channel *, HAL_BOOL *);
	HAL_BOOL  __ahdecl(*ah_perCalibrationN)(struct ath_hal *,
			struct ieee80211_channel *, u_int chainMask,
			HAL_BOOL longCal, HAL_BOOL *isCalDone);
	HAL_BOOL  __ahdecl(*ah_resetCalValid)(struct ath_hal *,
			const struct ieee80211_channel *);
	HAL_BOOL  __ahdecl(*ah_setTxPower)(struct ath_hal *,
	    		const struct ieee80211_channel *, uint16_t *);
	HAL_BOOL  __ahdecl(*ah_setTxPowerLimit)(struct ath_hal *, uint32_t);
	HAL_BOOL  __ahdecl(*ah_setBoardValues)(struct ath_hal *,
	    		const struct ieee80211_channel *);

	/* Transmit functions */
	HAL_BOOL  __ahdecl(*ah_updateTxTrigLevel)(struct ath_hal*,
				HAL_BOOL incTrigLevel);
	int	  __ahdecl(*ah_setupTxQueue)(struct ath_hal *, HAL_TX_QUEUE,
				const HAL_TXQ_INFO *qInfo);
	HAL_BOOL  __ahdecl(*ah_setTxQueueProps)(struct ath_hal *, int q, 
				const HAL_TXQ_INFO *qInfo);
	HAL_BOOL  __ahdecl(*ah_getTxQueueProps)(struct ath_hal *, int q, 
				HAL_TXQ_INFO *qInfo);
	HAL_BOOL  __ahdecl(*ah_releaseTxQueue)(struct ath_hal *ah, u_int q);
	HAL_BOOL  __ahdecl(*ah_resetTxQueue)(struct ath_hal *ah, u_int q);
	uint32_t __ahdecl(*ah_getTxDP)(struct ath_hal*, u_int);
	HAL_BOOL  __ahdecl(*ah_setTxDP)(struct ath_hal*, u_int, uint32_t txdp);
	uint32_t __ahdecl(*ah_numTxPending)(struct ath_hal *, u_int q);
	HAL_BOOL  __ahdecl(*ah_startTxDma)(struct ath_hal*, u_int);
	HAL_BOOL  __ahdecl(*ah_stopTxDma)(struct ath_hal*, u_int);
	HAL_BOOL  __ahdecl(*ah_setupTxDesc)(struct ath_hal *, struct ath_desc *,
				u_int pktLen, u_int hdrLen,
				HAL_PKT_TYPE type, u_int txPower,
				u_int txRate0, u_int txTries0,
				u_int keyIx, u_int antMode, u_int flags,
				u_int rtsctsRate, u_int rtsctsDuration,
				u_int compicvLen, u_int compivLen,
				u_int comp);
	HAL_BOOL  __ahdecl(*ah_setupXTxDesc)(struct ath_hal *, struct ath_desc*,
				u_int txRate1, u_int txTries1,
				u_int txRate2, u_int txTries2,
				u_int txRate3, u_int txTries3);
	HAL_BOOL  __ahdecl(*ah_fillTxDesc)(struct ath_hal *, struct ath_desc *,
				HAL_DMA_ADDR *bufAddrList, uint32_t *segLenList,
				u_int descId, u_int qcuId, HAL_BOOL firstSeg,
				HAL_BOOL lastSeg, const struct ath_desc *);
	HAL_STATUS __ahdecl(*ah_procTxDesc)(struct ath_hal *,
				struct ath_desc *, struct ath_tx_status *);
	void	   __ahdecl(*ah_getTxIntrQueue)(struct ath_hal *, uint32_t *);
	void	   __ahdecl(*ah_reqTxIntrDesc)(struct ath_hal *, struct ath_desc*);
	HAL_BOOL	__ahdecl(*ah_getTxCompletionRates)(struct ath_hal *,
				const struct ath_desc *ds, int *rates, int *tries);
	void	  __ahdecl(*ah_setTxDescLink)(struct ath_hal *ah, void *ds,
				uint32_t link);
	void	  __ahdecl(*ah_getTxDescLink)(struct ath_hal *ah, void *ds,
				uint32_t *link);
	void	  __ahdecl(*ah_getTxDescLinkPtr)(struct ath_hal *ah, void *ds,
				uint32_t **linkptr);
	void	  __ahdecl(*ah_setupTxStatusRing)(struct ath_hal *,
				void *ts_start, uint32_t ts_paddr_start,
				uint16_t size);
	void	  __ahdecl(*ah_getTxRawTxDesc)(struct ath_hal *, u_int32_t *);

	/* Receive Functions */
	uint32_t __ahdecl(*ah_getRxDP)(struct ath_hal*, HAL_RX_QUEUE);
	void	  __ahdecl(*ah_setRxDP)(struct ath_hal*, uint32_t rxdp, HAL_RX_QUEUE);
	void	  __ahdecl(*ah_enableReceive)(struct ath_hal*);
	HAL_BOOL  __ahdecl(*ah_stopDmaReceive)(struct ath_hal*);
	void	  __ahdecl(*ah_startPcuReceive)(struct ath_hal*);
	void	  __ahdecl(*ah_stopPcuReceive)(struct ath_hal*);
	void	  __ahdecl(*ah_setMulticastFilter)(struct ath_hal*,
				uint32_t filter0, uint32_t filter1);
	HAL_BOOL  __ahdecl(*ah_setMulticastFilterIndex)(struct ath_hal*,
				uint32_t index);
	HAL_BOOL  __ahdecl(*ah_clrMulticastFilterIndex)(struct ath_hal*,
				uint32_t index);
	uint32_t __ahdecl(*ah_getRxFilter)(struct ath_hal*);
	void	  __ahdecl(*ah_setRxFilter)(struct ath_hal*, uint32_t);
	HAL_BOOL  __ahdecl(*ah_setupRxDesc)(struct ath_hal *, struct ath_desc *,
				uint32_t size, u_int flags);
	HAL_STATUS __ahdecl(*ah_procRxDesc)(struct ath_hal *,
				struct ath_desc *, uint32_t phyAddr,
				struct ath_desc *next, uint64_t tsf,
				struct ath_rx_status *);
	void	  __ahdecl(*ah_rxMonitor)(struct ath_hal *,
				const HAL_NODE_STATS *,
				const struct ieee80211_channel *);
	void      __ahdecl(*ah_aniPoll)(struct ath_hal *,
				const struct ieee80211_channel *);
	void	  __ahdecl(*ah_procMibEvent)(struct ath_hal *,
				const HAL_NODE_STATS *);

	/* Misc Functions */
	HAL_STATUS __ahdecl(*ah_getCapability)(struct ath_hal *,
				HAL_CAPABILITY_TYPE, uint32_t capability,
				uint32_t *result);
	HAL_BOOL   __ahdecl(*ah_setCapability)(struct ath_hal *,
				HAL_CAPABILITY_TYPE, uint32_t capability,
				uint32_t setting, HAL_STATUS *);
	HAL_BOOL   __ahdecl(*ah_getDiagState)(struct ath_hal *, int request,
				const void *args, uint32_t argsize,
				void **result, uint32_t *resultsize);
	void	  __ahdecl(*ah_getMacAddress)(struct ath_hal *, uint8_t *);
	HAL_BOOL  __ahdecl(*ah_setMacAddress)(struct ath_hal *, const uint8_t*);
	void	  __ahdecl(*ah_getBssIdMask)(struct ath_hal *, uint8_t *);
	HAL_BOOL  __ahdecl(*ah_setBssIdMask)(struct ath_hal *, const uint8_t*);
	HAL_BOOL  __ahdecl(*ah_setRegulatoryDomain)(struct ath_hal*,
				uint16_t, HAL_STATUS *);
	void	  __ahdecl(*ah_setLedState)(struct ath_hal*, HAL_LED_STATE);
	void	  __ahdecl(*ah_writeAssocid)(struct ath_hal*,
				const uint8_t *bssid, uint16_t assocId);
	HAL_BOOL  __ahdecl(*ah_gpioCfgOutput)(struct ath_hal *,
				uint32_t gpio, HAL_GPIO_MUX_TYPE);
	HAL_BOOL  __ahdecl(*ah_gpioCfgInput)(struct ath_hal *, uint32_t gpio);
	uint32_t __ahdecl(*ah_gpioGet)(struct ath_hal *, uint32_t gpio);
	HAL_BOOL  __ahdecl(*ah_gpioSet)(struct ath_hal *,
				uint32_t gpio, uint32_t val);
	void	  __ahdecl(*ah_gpioSetIntr)(struct ath_hal*, u_int, uint32_t);
	uint32_t __ahdecl(*ah_getTsf32)(struct ath_hal*);
	uint64_t __ahdecl(*ah_getTsf64)(struct ath_hal*);
	void     __ahdecl(*ah_setTsf64)(struct ath_hal *, uint64_t);
	void	  __ahdecl(*ah_resetTsf)(struct ath_hal*);
	HAL_BOOL  __ahdecl(*ah_detectCardPresent)(struct ath_hal*);
	void	  __ahdecl(*ah_updateMibCounters)(struct ath_hal*,
				HAL_MIB_STATS*);
	HAL_RFGAIN __ahdecl(*ah_getRfGain)(struct ath_hal*);
	u_int	  __ahdecl(*ah_getDefAntenna)(struct ath_hal*);
	void	  __ahdecl(*ah_setDefAntenna)(struct ath_hal*, u_int);
	HAL_ANT_SETTING	 __ahdecl(*ah_getAntennaSwitch)(struct ath_hal*);
	HAL_BOOL  __ahdecl(*ah_setAntennaSwitch)(struct ath_hal*,
				HAL_ANT_SETTING);
	HAL_BOOL  __ahdecl(*ah_setSifsTime)(struct ath_hal*, u_int);
	u_int	  __ahdecl(*ah_getSifsTime)(struct ath_hal*);
	HAL_BOOL  __ahdecl(*ah_setSlotTime)(struct ath_hal*, u_int);
	u_int	  __ahdecl(*ah_getSlotTime)(struct ath_hal*);
	HAL_BOOL  __ahdecl(*ah_setAckTimeout)(struct ath_hal*, u_int);
	u_int	  __ahdecl(*ah_getAckTimeout)(struct ath_hal*);
	HAL_BOOL  __ahdecl(*ah_setAckCTSRate)(struct ath_hal*, u_int);
	u_int	  __ahdecl(*ah_getAckCTSRate)(struct ath_hal*);
	HAL_BOOL  __ahdecl(*ah_setCTSTimeout)(struct ath_hal*, u_int);
	u_int	  __ahdecl(*ah_getCTSTimeout)(struct ath_hal*);
	HAL_BOOL  __ahdecl(*ah_setDecompMask)(struct ath_hal*, uint16_t, int);
	void	  __ahdecl(*ah_setCoverageClass)(struct ath_hal*, uint8_t, int);
	HAL_STATUS	__ahdecl(*ah_setQuiet)(struct ath_hal *ah, uint32_t period,
				uint32_t duration, uint32_t nextStart,
				HAL_QUIET_FLAG flag);
	void	  __ahdecl(*ah_setChainMasks)(struct ath_hal *,
				uint32_t, uint32_t);

	/* DFS functions */
	void	  __ahdecl(*ah_enableDfs)(struct ath_hal *ah,
				HAL_PHYERR_PARAM *pe);
	void	  __ahdecl(*ah_getDfsThresh)(struct ath_hal *ah,
				HAL_PHYERR_PARAM *pe);
	HAL_BOOL  __ahdecl(*ah_getDfsDefaultThresh)(struct ath_hal *ah,
				HAL_PHYERR_PARAM *pe);
	HAL_BOOL  __ahdecl(*ah_procRadarEvent)(struct ath_hal *ah,
				struct ath_rx_status *rxs, uint64_t fulltsf,
				const char *buf, HAL_DFS_EVENT *event);
	HAL_BOOL  __ahdecl(*ah_isFastClockEnabled)(struct ath_hal *ah);
	void	  __ahdecl(*ah_setDfsCacTxQuiet)(struct ath_hal *, HAL_BOOL);

	/* Spectral Scan functions */
	void	__ahdecl(*ah_spectralConfigure)(struct ath_hal *ah,
				HAL_SPECTRAL_PARAM *sp);
	void	__ahdecl(*ah_spectralGetConfig)(struct ath_hal *ah,
				HAL_SPECTRAL_PARAM *sp);
	void	__ahdecl(*ah_spectralStart)(struct ath_hal *);
	void	__ahdecl(*ah_spectralStop)(struct ath_hal *);
	HAL_BOOL	__ahdecl(*ah_spectralIsEnabled)(struct ath_hal *);
	HAL_BOOL	__ahdecl(*ah_spectralIsActive)(struct ath_hal *);
	/* XXX getNfPri() and getNfExt() */

	/* Key Cache Functions */
	uint32_t __ahdecl(*ah_getKeyCacheSize)(struct ath_hal*);
	HAL_BOOL  __ahdecl(*ah_resetKeyCacheEntry)(struct ath_hal*, uint16_t);
	HAL_BOOL  __ahdecl(*ah_isKeyCacheEntryValid)(struct ath_hal *,
				uint16_t);
	HAL_BOOL  __ahdecl(*ah_setKeyCacheEntry)(struct ath_hal*,
				uint16_t, const HAL_KEYVAL *,
				const uint8_t *, int);
	HAL_BOOL  __ahdecl(*ah_setKeyCacheEntryMac)(struct ath_hal*,
				uint16_t, const uint8_t *);

	/* Power Management Functions */
	HAL_BOOL  __ahdecl(*ah_setPowerMode)(struct ath_hal*,
				HAL_POWER_MODE mode, int setChip);
	HAL_POWER_MODE __ahdecl(*ah_getPowerMode)(struct ath_hal*);
	int16_t   __ahdecl(*ah_getChanNoise)(struct ath_hal *,
				const struct ieee80211_channel *);

	/* Beacon Management Functions */
	void	  __ahdecl(*ah_setBeaconTimers)(struct ath_hal*,
				const HAL_BEACON_TIMERS *);
	/* NB: deprecated, use ah_setBeaconTimers instead */
	void	  __ahdecl(*ah_beaconInit)(struct ath_hal *,
				uint32_t nexttbtt, uint32_t intval);
	void	  __ahdecl(*ah_setStationBeaconTimers)(struct ath_hal*,
				const HAL_BEACON_STATE *);
	void	  __ahdecl(*ah_resetStationBeaconTimers)(struct ath_hal*);
	uint64_t  __ahdecl(*ah_getNextTBTT)(struct ath_hal *);

	/* 802.11n Functions */
	HAL_BOOL  __ahdecl(*ah_chainTxDesc)(struct ath_hal *,
				struct ath_desc *,
				HAL_DMA_ADDR *bufAddrList,
				uint32_t *segLenList,
				u_int, u_int, HAL_PKT_TYPE,
				u_int, HAL_CIPHER, uint8_t, HAL_BOOL,
				HAL_BOOL, HAL_BOOL);
	HAL_BOOL  __ahdecl(*ah_setupFirstTxDesc)(struct ath_hal *,
				struct ath_desc *, u_int, u_int, u_int,
				u_int, u_int, u_int, u_int, u_int);
	HAL_BOOL  __ahdecl(*ah_setupLastTxDesc)(struct ath_hal *,
				struct ath_desc *, const struct ath_desc *);
	void	  __ahdecl(*ah_set11nRateScenario)(struct ath_hal *,
	    			struct ath_desc *, u_int, u_int,
				HAL_11N_RATE_SERIES [], u_int, u_int);

	/*
	 * The next 4 (set11ntxdesc -> set11naggrlast) are specific
	 * to the EDMA HAL.  Descriptors are chained together by
	 * using filltxdesc (not ChainTxDesc) and then setting the
	 * aggregate flags appropriately using first/middle/last.
	 */
	void	  __ahdecl(*ah_set11nTxDesc)(struct ath_hal *,
				void *, u_int, HAL_PKT_TYPE, u_int, u_int,
				u_int);
	void	  __ahdecl(*ah_set11nAggrFirst)(struct ath_hal *,
				struct ath_desc *, u_int, u_int);
	void	  __ahdecl(*ah_set11nAggrMiddle)(struct ath_hal *,
	    			struct ath_desc *, u_int);
	void	  __ahdecl(*ah_set11nAggrLast)(struct ath_hal *,
				struct ath_desc *);
	void	  __ahdecl(*ah_clr11nAggr)(struct ath_hal *,
	    			struct ath_desc *);
	void	  __ahdecl(*ah_set11nBurstDuration)(struct ath_hal *,
	    			struct ath_desc *, u_int);
	void	  __ahdecl(*ah_set11nVirtMoreFrag)(struct ath_hal *,
				struct ath_desc *, u_int);

	HAL_BOOL  __ahdecl(*ah_getMibCycleCounts) (struct ath_hal *,
				HAL_SURVEY_SAMPLE *);

	uint32_t  __ahdecl(*ah_get11nExtBusy)(struct ath_hal *);
	void      __ahdecl(*ah_set11nMac2040)(struct ath_hal *,
				HAL_HT_MACMODE);
	HAL_HT_RXCLEAR __ahdecl(*ah_get11nRxClear)(struct ath_hal *ah);
	void	  __ahdecl(*ah_set11nRxClear)(struct ath_hal *,
	    			HAL_HT_RXCLEAR);

	/* Interrupt functions */
	HAL_BOOL  __ahdecl(*ah_isInterruptPending)(struct ath_hal*);
	HAL_BOOL  __ahdecl(*ah_getPendingInterrupts)(struct ath_hal*, HAL_INT*);
	HAL_INT	  __ahdecl(*ah_getInterrupts)(struct ath_hal*);
	HAL_INT	  __ahdecl(*ah_setInterrupts)(struct ath_hal*, HAL_INT);

	/* Bluetooth Coexistence functions */
	void	    __ahdecl(*ah_btCoexSetInfo)(struct ath_hal *,
				HAL_BT_COEX_INFO *);
	void	    __ahdecl(*ah_btCoexSetConfig)(struct ath_hal *,
				HAL_BT_COEX_CONFIG *);
	void	    __ahdecl(*ah_btCoexSetQcuThresh)(struct ath_hal *,
				int);
	void	    __ahdecl(*ah_btCoexSetWeights)(struct ath_hal *,
				uint32_t);
	void	    __ahdecl(*ah_btCoexSetBmissThresh)(struct ath_hal *,
				uint32_t);
	void	    __ahdecl(*ah_btCoexSetParameter)(struct ath_hal *,
				uint32_t, uint32_t);
	void	    __ahdecl(*ah_btCoexDisable)(struct ath_hal *);
	int	    __ahdecl(*ah_btCoexEnable)(struct ath_hal *);

	/* Bluetooth MCI methods */
	void	    __ahdecl(*ah_btMciSetup)(struct ath_hal *,
				uint32_t, void *, uint16_t, uint32_t);
	HAL_BOOL    __ahdecl(*ah_btMciSendMessage)(struct ath_hal *,
				uint8_t, uint32_t, uint32_t *, uint8_t,
				HAL_BOOL, HAL_BOOL);
	uint32_t    __ahdecl(*ah_btMciGetInterrupt)(struct ath_hal *,
				uint32_t *, uint32_t *);
	uint32_t    __ahdecl(*ah_btMciState)(struct ath_hal *,
				uint32_t, uint32_t *);
	void	    __ahdecl(*ah_btMciDetach)(struct ath_hal *);

	/* LNA diversity configuration */
	void	    __ahdecl(*ah_divLnaConfGet)(struct ath_hal *,
				HAL_ANT_COMB_CONFIG *);
	void	    __ahdecl(*ah_divLnaConfSet)(struct ath_hal *,
				HAL_ANT_COMB_CONFIG *);
};

/* 
 * Check the PCI vendor ID and device ID against Atheros' values
 * and return a printable description for any Atheros hardware.
 * AH_NULL is returned if the ID's do not describe Atheros hardware.
 */
extern	const char *__ahdecl ath_hal_probe(uint16_t vendorid, uint16_t devid);

/*
 * Attach the HAL for use with the specified device.  The device is
 * defined by the PCI device ID.  The caller provides an opaque pointer
 * to an upper-layer data structure (HAL_SOFTC) that is stored in the
 * HAL state block for later use.  Hardware register accesses are done
 * using the specified bus tag and handle.  On successful return a
 * reference to a state block is returned that must be supplied in all
 * subsequent HAL calls.  Storage associated with this reference is
 * dynamically allocated and must be freed by calling the ah_detach
 * method when the client is done.  If the attach operation fails a
 * null (AH_NULL) reference will be returned and a status code will
 * be returned if the status parameter is non-zero.
 */
extern	struct ath_hal * __ahdecl ath_hal_attach(uint16_t devid, HAL_SOFTC,
		HAL_BUS_TAG, HAL_BUS_HANDLE, uint16_t *eepromdata,
		HAL_OPS_CONFIG *ah_config, HAL_STATUS* status);

extern	const char *ath_hal_mac_name(struct ath_hal *);
extern	const char *ath_hal_rf_name(struct ath_hal *);

/*
 * Regulatory interfaces.  Drivers should use ath_hal_init_channels to
 * request a set of channels for a particular country code and/or
 * regulatory domain.  If CTRY_DEFAULT and SKU_NONE are specified then
 * this list is constructed according to the contents of the EEPROM.
 * ath_hal_getchannels acts similarly but does not alter the operating
 * state; this can be used to collect information for a particular
 * regulatory configuration.  Finally ath_hal_set_channels installs a
 * channel list constructed outside the driver.  The HAL will adopt the
 * channel list and setup internal state according to the specified
 * regulatory configuration (e.g. conformance test limits).
 *
 * For all interfaces the channel list is returned in the supplied array.
 * maxchans defines the maximum size of this array.  nchans contains the
 * actual number of channels returned.  If a problem occurred then a
 * status code != HAL_OK is returned.
 */
struct ieee80211_channel;

/*
 * Return a list of channels according to the specified regulatory.
 */
extern	HAL_STATUS __ahdecl ath_hal_getchannels(struct ath_hal *,
    struct ieee80211_channel *chans, u_int maxchans, int *nchans,
    u_int modeSelect, HAL_CTRY_CODE cc, HAL_REG_DOMAIN regDmn,
    HAL_BOOL enableExtendedChannels);

/*
 * Return a list of channels and install it as the current operating
 * regulatory list.
 */
extern	HAL_STATUS __ahdecl ath_hal_init_channels(struct ath_hal *,
    struct ieee80211_channel *chans, u_int maxchans, int *nchans,
    u_int modeSelect, HAL_CTRY_CODE cc, HAL_REG_DOMAIN rd,
    HAL_BOOL enableExtendedChannels);

/*
 * Install the list of channels as the current operating regulatory
 * and setup related state according to the country code and sku.
 */
extern	HAL_STATUS __ahdecl ath_hal_set_channels(struct ath_hal *,
    struct ieee80211_channel *chans, int nchans,
    HAL_CTRY_CODE cc, HAL_REG_DOMAIN regDmn);

/*
 * Fetch the ctl/ext noise floor values reported by a MIMO
 * radio. Returns 1 for valid results, 0 for invalid channel.
 */
extern int __ahdecl ath_hal_get_mimo_chan_noise(struct ath_hal *ah,
    const struct ieee80211_channel *chan, int16_t *nf_ctl,
    int16_t *nf_ext);

/*
 * Calibrate noise floor data following a channel scan or similar.
 * This must be called prior retrieving noise floor data.
 */
extern	void __ahdecl ath_hal_process_noisefloor(struct ath_hal *ah);

/*
 * Return bit mask of wireless modes supported by the hardware.
 */
extern	u_int __ahdecl ath_hal_getwirelessmodes(struct ath_hal*);

/*
 * Get the HAL wireless mode for the given channel.
 */
extern	int ath_hal_get_curmode(struct ath_hal *ah,
    const struct ieee80211_channel *chan);

/*
 * Calculate the packet TX time for a legacy or 11n frame
 */
extern uint32_t __ahdecl ath_hal_pkt_txtime(struct ath_hal *ah,
    const HAL_RATE_TABLE *rates, uint32_t frameLen,
    uint16_t rateix, HAL_BOOL isht40, HAL_BOOL shortPreamble,
    HAL_BOOL includeSifs);

/*
 * Calculate the duration of an 11n frame.
 */
extern uint32_t __ahdecl ath_computedur_ht(uint32_t frameLen, uint16_t rate,
    int streams, HAL_BOOL isht40, HAL_BOOL isShortGI);

/*
 * Calculate the transmit duration of a legacy frame.
 */
extern uint16_t __ahdecl ath_hal_computetxtime(struct ath_hal *,
		const HAL_RATE_TABLE *rates, uint32_t frameLen,
		uint16_t rateix, HAL_BOOL shortPreamble,
		HAL_BOOL includeSifs);

/*
 * Adjust the TSF.
 */
extern void __ahdecl ath_hal_adjusttsf(struct ath_hal *ah, int32_t tsfdelta);

/*
 * Enable or disable CCA.
 */
void __ahdecl ath_hal_setcca(struct ath_hal *ah, int ena);

/*
 * Get CCA setting.
 */
int __ahdecl ath_hal_getcca(struct ath_hal *ah);

/*
 * Enable/disable and get self-gen frame (ACK, CTS) for CAC.
 */
void __ahdecl ath_hal_set_dfs_cac_tx_quiet(struct ath_hal *ah, HAL_BOOL ena);

/*
 * Read EEPROM data from ah_eepromdata
 */
HAL_BOOL __ahdecl ath_hal_EepromDataRead(struct ath_hal *ah,
		u_int off, uint16_t *data);

/*
 * For now, simply pass through MFP frames.
 */
static inline u_int32_t
ath_hal_get_mfp_qos(struct ath_hal *ah)
{
	//return AH_PRIVATE(ah)->ah_mfp_qos;
	return HAL_MFP_QOSDATA;
}

/*
 * Convert between microseconds and core system clocks.
 */
extern u_int ath_hal_mac_clks(struct ath_hal *ah, u_int usecs);
extern u_int ath_hal_mac_usec(struct ath_hal *ah, u_int clks);
extern uint64_t ath_hal_mac_psec(struct ath_hal *ah, u_int clks);

#endif /* _ATH_AH_H_ */
