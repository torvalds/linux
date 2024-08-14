/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ethtool.h: Defines for Linux ethtool.
 *
 * Copyright (C) 1998 David S. Miller (davem@redhat.com)
 * Copyright 2001 Jeff Garzik <jgarzik@pobox.com>
 * Portions Copyright 2001 Sun Microsystems (thockin@sun.com)
 * Portions Copyright 2002 Intel (eli.kupermann@intel.com,
 *                                christopher.leech@intel.com,
 *                                scott.feldman@intel.com)
 * Portions Copyright (C) Sun Microsystems 2008
 */
#ifndef _LINUX_ETHTOOL_H
#define _LINUX_ETHTOOL_H

#include <linux/bitmap.h>
#include <linux/compat.h>
#include <linux/if_ether.h>
#include <linux/netlink.h>
#include <uapi/linux/ethtool.h>
#include <uapi/linux/net_tstamp.h>

struct compat_ethtool_rx_flow_spec {
	u32		flow_type;
	union ethtool_flow_union h_u;
	struct ethtool_flow_ext h_ext;
	union ethtool_flow_union m_u;
	struct ethtool_flow_ext m_ext;
	compat_u64	ring_cookie;
	u32		location;
};

struct compat_ethtool_rxnfc {
	u32				cmd;
	u32				flow_type;
	compat_u64			data;
	struct compat_ethtool_rx_flow_spec fs;
	u32				rule_cnt;
	u32				rule_locs[];
};

#include <linux/rculist.h>

/**
 * enum ethtool_phys_id_state - indicator state for physical identification
 * @ETHTOOL_ID_INACTIVE: Physical ID indicator should be deactivated
 * @ETHTOOL_ID_ACTIVE: Physical ID indicator should be activated
 * @ETHTOOL_ID_ON: LED should be turned on (used iff %ETHTOOL_ID_ACTIVE
 *	is not supported)
 * @ETHTOOL_ID_OFF: LED should be turned off (used iff %ETHTOOL_ID_ACTIVE
 *	is not supported)
 */
enum ethtool_phys_id_state {
	ETHTOOL_ID_INACTIVE,
	ETHTOOL_ID_ACTIVE,
	ETHTOOL_ID_ON,
	ETHTOOL_ID_OFF
};

enum {
	ETH_RSS_HASH_TOP_BIT, /* Configurable RSS hash function - Toeplitz */
	ETH_RSS_HASH_XOR_BIT, /* Configurable RSS hash function - Xor */
	ETH_RSS_HASH_CRC32_BIT, /* Configurable RSS hash function - Crc32 */

	/*
	 * Add your fresh new hash function bits above and remember to update
	 * rss_hash_func_strings[] in ethtool.c
	 */
	ETH_RSS_HASH_FUNCS_COUNT
};

/**
 * struct kernel_ethtool_ringparam - RX/TX ring configuration
 * @rx_buf_len: Current length of buffers on the rx ring.
 * @tcp_data_split: Scatter packet headers and data to separate buffers
 * @tx_push: The flag of tx push mode
 * @rx_push: The flag of rx push mode
 * @cqe_size: Size of TX/RX completion queue event
 * @tx_push_buf_len: Size of TX push buffer
 * @tx_push_buf_max_len: Maximum allowed size of TX push buffer
 */
struct kernel_ethtool_ringparam {
	u32	rx_buf_len;
	u8	tcp_data_split;
	u8	tx_push;
	u8	rx_push;
	u32	cqe_size;
	u32	tx_push_buf_len;
	u32	tx_push_buf_max_len;
};

/**
 * enum ethtool_supported_ring_param - indicator caps for setting ring params
 * @ETHTOOL_RING_USE_RX_BUF_LEN: capture for setting rx_buf_len
 * @ETHTOOL_RING_USE_CQE_SIZE: capture for setting cqe_size
 * @ETHTOOL_RING_USE_TX_PUSH: capture for setting tx_push
 * @ETHTOOL_RING_USE_RX_PUSH: capture for setting rx_push
 * @ETHTOOL_RING_USE_TX_PUSH_BUF_LEN: capture for setting tx_push_buf_len
 * @ETHTOOL_RING_USE_TCP_DATA_SPLIT: capture for setting tcp_data_split
 */
enum ethtool_supported_ring_param {
	ETHTOOL_RING_USE_RX_BUF_LEN		= BIT(0),
	ETHTOOL_RING_USE_CQE_SIZE		= BIT(1),
	ETHTOOL_RING_USE_TX_PUSH		= BIT(2),
	ETHTOOL_RING_USE_RX_PUSH		= BIT(3),
	ETHTOOL_RING_USE_TX_PUSH_BUF_LEN	= BIT(4),
	ETHTOOL_RING_USE_TCP_DATA_SPLIT		= BIT(5),
};

#define __ETH_RSS_HASH_BIT(bit)	((u32)1 << (bit))
#define __ETH_RSS_HASH(name)	__ETH_RSS_HASH_BIT(ETH_RSS_HASH_##name##_BIT)

#define ETH_RSS_HASH_TOP	__ETH_RSS_HASH(TOP)
#define ETH_RSS_HASH_XOR	__ETH_RSS_HASH(XOR)
#define ETH_RSS_HASH_CRC32	__ETH_RSS_HASH(CRC32)

#define ETH_RSS_HASH_UNKNOWN	0
#define ETH_RSS_HASH_NO_CHANGE	0

struct net_device;
struct netlink_ext_ack;

/* Link extended state and substate. */
struct ethtool_link_ext_state_info {
	enum ethtool_link_ext_state link_ext_state;
	union {
		enum ethtool_link_ext_substate_autoneg autoneg;
		enum ethtool_link_ext_substate_link_training link_training;
		enum ethtool_link_ext_substate_link_logical_mismatch link_logical_mismatch;
		enum ethtool_link_ext_substate_bad_signal_integrity bad_signal_integrity;
		enum ethtool_link_ext_substate_cable_issue cable_issue;
		enum ethtool_link_ext_substate_module module;
		u32 __link_ext_substate;
	};
};

struct ethtool_link_ext_stats {
	/* Custom Linux statistic for PHY level link down events.
	 * In a simpler world it should be equal to netdev->carrier_down_count
	 * unfortunately netdev also counts local reconfigurations which don't
	 * actually take the physical link down, not to mention NC-SI which,
	 * if present, keeps the link up regardless of host state.
	 * This statistic counts when PHY _actually_ went down, or lost link.
	 *
	 * Note that we need u64 for ethtool_stats_init() and comparisons
	 * to ETHTOOL_STAT_NOT_SET, but only u32 is exposed to the user.
	 */
	u64 link_down_events;
};

/**
 * ethtool_rxfh_indir_default - get default value for RX flow hash indirection
 * @index: Index in RX flow hash indirection table
 * @n_rx_rings: Number of RX rings to use
 *
 * This function provides the default policy for RX flow hash indirection.
 */
static inline u32 ethtool_rxfh_indir_default(u32 index, u32 n_rx_rings)
{
	return index % n_rx_rings;
}

/**
 * struct ethtool_rxfh_context - a custom RSS context configuration
 * @indir_size: Number of u32 entries in indirection table
 * @key_size: Size of hash key, in bytes
 * @priv_size: Size of driver private data, in bytes
 * @hfunc: RSS hash function identifier.  One of the %ETH_RSS_HASH_*
 * @input_xfrm: Defines how the input data is transformed. Valid values are one
 *	of %RXH_XFRM_*.
 * @indir_configured: indir has been specified (at create time or subsequently)
 * @key_configured: hkey has been specified (at create time or subsequently)
 */
struct ethtool_rxfh_context {
	u32 indir_size;
	u32 key_size;
	u16 priv_size;
	u8 hfunc;
	u8 input_xfrm;
	u8 indir_configured:1;
	u8 key_configured:1;
	/* private: driver private data, indirection table, and hash key are
	 * stored sequentially in @data area.  Use below helpers to access.
	 */
	u32 key_off;
	u8 data[] __aligned(sizeof(void *));
};

static inline void *ethtool_rxfh_context_priv(struct ethtool_rxfh_context *ctx)
{
	return ctx->data;
}

static inline u32 *ethtool_rxfh_context_indir(struct ethtool_rxfh_context *ctx)
{
	return (u32 *)(ctx->data + ALIGN(ctx->priv_size, sizeof(u32)));
}

static inline u8 *ethtool_rxfh_context_key(struct ethtool_rxfh_context *ctx)
{
	return &ctx->data[ctx->key_off];
}

void ethtool_rxfh_context_lost(struct net_device *dev, u32 context_id);

/* declare a link mode bitmap */
#define __ETHTOOL_DECLARE_LINK_MODE_MASK(name)		\
	DECLARE_BITMAP(name, __ETHTOOL_LINK_MODE_MASK_NBITS)

/* drivers must ignore base.cmd and base.link_mode_masks_nwords
 * fields, but they are allowed to overwrite them (will be ignored).
 */
struct ethtool_link_ksettings {
	struct ethtool_link_settings base;
	struct {
		__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
		__ETHTOOL_DECLARE_LINK_MODE_MASK(advertising);
		__ETHTOOL_DECLARE_LINK_MODE_MASK(lp_advertising);
	} link_modes;
	u32	lanes;
};

/**
 * ethtool_link_ksettings_zero_link_mode - clear link_ksettings link mode mask
 *   @ptr : pointer to struct ethtool_link_ksettings
 *   @name : one of supported/advertising/lp_advertising
 */
#define ethtool_link_ksettings_zero_link_mode(ptr, name)		\
	bitmap_zero((ptr)->link_modes.name, __ETHTOOL_LINK_MODE_MASK_NBITS)

/**
 * ethtool_link_ksettings_add_link_mode - set bit in link_ksettings
 * link mode mask
 *   @ptr : pointer to struct ethtool_link_ksettings
 *   @name : one of supported/advertising/lp_advertising
 *   @mode : one of the ETHTOOL_LINK_MODE_*_BIT
 * (not atomic, no bound checking)
 */
#define ethtool_link_ksettings_add_link_mode(ptr, name, mode)		\
	__set_bit(ETHTOOL_LINK_MODE_ ## mode ## _BIT, (ptr)->link_modes.name)

/**
 * ethtool_link_ksettings_del_link_mode - clear bit in link_ksettings
 * link mode mask
 *   @ptr : pointer to struct ethtool_link_ksettings
 *   @name : one of supported/advertising/lp_advertising
 *   @mode : one of the ETHTOOL_LINK_MODE_*_BIT
 * (not atomic, no bound checking)
 */
#define ethtool_link_ksettings_del_link_mode(ptr, name, mode)		\
	__clear_bit(ETHTOOL_LINK_MODE_ ## mode ## _BIT, (ptr)->link_modes.name)

/**
 * ethtool_link_ksettings_test_link_mode - test bit in ksettings link mode mask
 *   @ptr : pointer to struct ethtool_link_ksettings
 *   @name : one of supported/advertising/lp_advertising
 *   @mode : one of the ETHTOOL_LINK_MODE_*_BIT
 * (not atomic, no bound checking)
 *
 * Returns true/false.
 */
#define ethtool_link_ksettings_test_link_mode(ptr, name, mode)		\
	test_bit(ETHTOOL_LINK_MODE_ ## mode ## _BIT, (ptr)->link_modes.name)

extern int
__ethtool_get_link_ksettings(struct net_device *dev,
			     struct ethtool_link_ksettings *link_ksettings);

struct ethtool_keee {
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(advertised);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(lp_advertised);
	u32	tx_lpi_timer;
	bool	tx_lpi_enabled;
	bool	eee_active;
	bool	eee_enabled;
};

struct kernel_ethtool_coalesce {
	u8 use_cqe_mode_tx;
	u8 use_cqe_mode_rx;
	u32 tx_aggr_max_bytes;
	u32 tx_aggr_max_frames;
	u32 tx_aggr_time_usecs;
};

/**
 * ethtool_intersect_link_masks - Given two link masks, AND them together
 * @dst: first mask and where result is stored
 * @src: second mask to intersect with
 *
 * Given two link mode masks, AND them together and save the result in dst.
 */
void ethtool_intersect_link_masks(struct ethtool_link_ksettings *dst,
				  struct ethtool_link_ksettings *src);

void ethtool_convert_legacy_u32_to_link_mode(unsigned long *dst,
					     u32 legacy_u32);

/* return false if src had higher bits set. lower bits always updated. */
bool ethtool_convert_link_mode_to_legacy_u32(u32 *legacy_u32,
				     const unsigned long *src);

#define ETHTOOL_COALESCE_RX_USECS		BIT(0)
#define ETHTOOL_COALESCE_RX_MAX_FRAMES		BIT(1)
#define ETHTOOL_COALESCE_RX_USECS_IRQ		BIT(2)
#define ETHTOOL_COALESCE_RX_MAX_FRAMES_IRQ	BIT(3)
#define ETHTOOL_COALESCE_TX_USECS		BIT(4)
#define ETHTOOL_COALESCE_TX_MAX_FRAMES		BIT(5)
#define ETHTOOL_COALESCE_TX_USECS_IRQ		BIT(6)
#define ETHTOOL_COALESCE_TX_MAX_FRAMES_IRQ	BIT(7)
#define ETHTOOL_COALESCE_STATS_BLOCK_USECS	BIT(8)
#define ETHTOOL_COALESCE_USE_ADAPTIVE_RX	BIT(9)
#define ETHTOOL_COALESCE_USE_ADAPTIVE_TX	BIT(10)
#define ETHTOOL_COALESCE_PKT_RATE_LOW		BIT(11)
#define ETHTOOL_COALESCE_RX_USECS_LOW		BIT(12)
#define ETHTOOL_COALESCE_RX_MAX_FRAMES_LOW	BIT(13)
#define ETHTOOL_COALESCE_TX_USECS_LOW		BIT(14)
#define ETHTOOL_COALESCE_TX_MAX_FRAMES_LOW	BIT(15)
#define ETHTOOL_COALESCE_PKT_RATE_HIGH		BIT(16)
#define ETHTOOL_COALESCE_RX_USECS_HIGH		BIT(17)
#define ETHTOOL_COALESCE_RX_MAX_FRAMES_HIGH	BIT(18)
#define ETHTOOL_COALESCE_TX_USECS_HIGH		BIT(19)
#define ETHTOOL_COALESCE_TX_MAX_FRAMES_HIGH	BIT(20)
#define ETHTOOL_COALESCE_RATE_SAMPLE_INTERVAL	BIT(21)
#define ETHTOOL_COALESCE_USE_CQE_RX		BIT(22)
#define ETHTOOL_COALESCE_USE_CQE_TX		BIT(23)
#define ETHTOOL_COALESCE_TX_AGGR_MAX_BYTES	BIT(24)
#define ETHTOOL_COALESCE_TX_AGGR_MAX_FRAMES	BIT(25)
#define ETHTOOL_COALESCE_TX_AGGR_TIME_USECS	BIT(26)
#define ETHTOOL_COALESCE_RX_PROFILE		BIT(27)
#define ETHTOOL_COALESCE_TX_PROFILE		BIT(28)
#define ETHTOOL_COALESCE_ALL_PARAMS		GENMASK(28, 0)

#define ETHTOOL_COALESCE_USECS						\
	(ETHTOOL_COALESCE_RX_USECS | ETHTOOL_COALESCE_TX_USECS)
#define ETHTOOL_COALESCE_MAX_FRAMES					\
	(ETHTOOL_COALESCE_RX_MAX_FRAMES | ETHTOOL_COALESCE_TX_MAX_FRAMES)
#define ETHTOOL_COALESCE_USECS_IRQ					\
	(ETHTOOL_COALESCE_RX_USECS_IRQ | ETHTOOL_COALESCE_TX_USECS_IRQ)
#define ETHTOOL_COALESCE_MAX_FRAMES_IRQ		\
	(ETHTOOL_COALESCE_RX_MAX_FRAMES_IRQ |	\
	 ETHTOOL_COALESCE_TX_MAX_FRAMES_IRQ)
#define ETHTOOL_COALESCE_USE_ADAPTIVE					\
	(ETHTOOL_COALESCE_USE_ADAPTIVE_RX | ETHTOOL_COALESCE_USE_ADAPTIVE_TX)
#define ETHTOOL_COALESCE_USECS_LOW_HIGH					\
	(ETHTOOL_COALESCE_RX_USECS_LOW | ETHTOOL_COALESCE_TX_USECS_LOW | \
	 ETHTOOL_COALESCE_RX_USECS_HIGH | ETHTOOL_COALESCE_TX_USECS_HIGH)
#define ETHTOOL_COALESCE_MAX_FRAMES_LOW_HIGH	\
	(ETHTOOL_COALESCE_RX_MAX_FRAMES_LOW |	\
	 ETHTOOL_COALESCE_TX_MAX_FRAMES_LOW |	\
	 ETHTOOL_COALESCE_RX_MAX_FRAMES_HIGH |	\
	 ETHTOOL_COALESCE_TX_MAX_FRAMES_HIGH)
#define ETHTOOL_COALESCE_PKT_RATE_RX_USECS				\
	(ETHTOOL_COALESCE_USE_ADAPTIVE_RX |				\
	 ETHTOOL_COALESCE_RX_USECS_LOW | ETHTOOL_COALESCE_RX_USECS_HIGH | \
	 ETHTOOL_COALESCE_PKT_RATE_LOW | ETHTOOL_COALESCE_PKT_RATE_HIGH | \
	 ETHTOOL_COALESCE_RATE_SAMPLE_INTERVAL)
#define ETHTOOL_COALESCE_USE_CQE					\
	(ETHTOOL_COALESCE_USE_CQE_RX | ETHTOOL_COALESCE_USE_CQE_TX)
#define ETHTOOL_COALESCE_TX_AGGR		\
	(ETHTOOL_COALESCE_TX_AGGR_MAX_BYTES |	\
	 ETHTOOL_COALESCE_TX_AGGR_MAX_FRAMES |	\
	 ETHTOOL_COALESCE_TX_AGGR_TIME_USECS)

#define ETHTOOL_STAT_NOT_SET	(~0ULL)

static inline void ethtool_stats_init(u64 *stats, unsigned int n)
{
	while (n--)
		stats[n] = ETHTOOL_STAT_NOT_SET;
}

/* Basic IEEE 802.3 MAC statistics (30.3.1.1.*), not otherwise exposed
 * via a more targeted API.
 */
struct ethtool_eth_mac_stats {
	enum ethtool_mac_stats_src src;
	struct_group(stats,
		u64 FramesTransmittedOK;
		u64 SingleCollisionFrames;
		u64 MultipleCollisionFrames;
		u64 FramesReceivedOK;
		u64 FrameCheckSequenceErrors;
		u64 AlignmentErrors;
		u64 OctetsTransmittedOK;
		u64 FramesWithDeferredXmissions;
		u64 LateCollisions;
		u64 FramesAbortedDueToXSColls;
		u64 FramesLostDueToIntMACXmitError;
		u64 CarrierSenseErrors;
		u64 OctetsReceivedOK;
		u64 FramesLostDueToIntMACRcvError;
		u64 MulticastFramesXmittedOK;
		u64 BroadcastFramesXmittedOK;
		u64 FramesWithExcessiveDeferral;
		u64 MulticastFramesReceivedOK;
		u64 BroadcastFramesReceivedOK;
		u64 InRangeLengthErrors;
		u64 OutOfRangeLengthField;
		u64 FrameTooLongErrors;
	);
};

/* Basic IEEE 802.3 PHY statistics (30.3.2.1.*), not otherwise exposed
 * via a more targeted API.
 */
struct ethtool_eth_phy_stats {
	enum ethtool_mac_stats_src src;
	struct_group(stats,
		u64 SymbolErrorDuringCarrier;
	);
};

/* Basic IEEE 802.3 MAC Ctrl statistics (30.3.3.*), not otherwise exposed
 * via a more targeted API.
 */
struct ethtool_eth_ctrl_stats {
	enum ethtool_mac_stats_src src;
	struct_group(stats,
		u64 MACControlFramesTransmitted;
		u64 MACControlFramesReceived;
		u64 UnsupportedOpcodesReceived;
	);
};

/**
 * struct ethtool_pause_stats - statistics for IEEE 802.3x pause frames
 * @src: input field denoting whether stats should be queried from the eMAC or
 *	pMAC (if the MM layer is supported). To be ignored otherwise.
 * @tx_pause_frames: transmitted pause frame count. Reported to user space
 *	as %ETHTOOL_A_PAUSE_STAT_TX_FRAMES.
 *
 *	Equivalent to `30.3.4.2 aPAUSEMACCtrlFramesTransmitted`
 *	from the standard.
 *
 * @rx_pause_frames: received pause frame count. Reported to user space
 *	as %ETHTOOL_A_PAUSE_STAT_RX_FRAMES. Equivalent to:
 *
 *	Equivalent to `30.3.4.3 aPAUSEMACCtrlFramesReceived`
 *	from the standard.
 */
struct ethtool_pause_stats {
	enum ethtool_mac_stats_src src;
	struct_group(stats,
		u64 tx_pause_frames;
		u64 rx_pause_frames;
	);
};

#define ETHTOOL_MAX_LANES	8

/**
 * struct ethtool_fec_stats - statistics for IEEE 802.3 FEC
 * @corrected_blocks: number of received blocks corrected by FEC
 *	Reported to user space as %ETHTOOL_A_FEC_STAT_CORRECTED.
 *
 *	Equivalent to `30.5.1.1.17 aFECCorrectedBlocks` from the standard.
 *
 * @uncorrectable_blocks: number of received blocks FEC was not able to correct
 *	Reported to user space as %ETHTOOL_A_FEC_STAT_UNCORR.
 *
 *	Equivalent to `30.5.1.1.18 aFECUncorrectableBlocks` from the standard.
 *
 * @corrected_bits: number of bits corrected by FEC
 *	Similar to @corrected_blocks but counts individual bit changes,
 *	not entire FEC data blocks. This is a non-standard statistic.
 *	Reported to user space as %ETHTOOL_A_FEC_STAT_CORR_BITS.
 *
 * For each of the above fields, the two substructure members are:
 *
 * - @lanes: per-lane/PCS-instance counts as defined by the standard
 * - @total: error counts for the entire port, for drivers incapable of reporting
 *	per-lane stats
 *
 * Drivers should fill in either only total or per-lane statistics, core
 * will take care of adding lane values up to produce the total.
 */
struct ethtool_fec_stats {
	struct ethtool_fec_stat {
		u64 total;
		u64 lanes[ETHTOOL_MAX_LANES];
	} corrected_blocks, uncorrectable_blocks, corrected_bits;
};

/**
 * struct ethtool_rmon_hist_range - byte range for histogram statistics
 * @low: low bound of the bucket (inclusive)
 * @high: high bound of the bucket (inclusive)
 */
struct ethtool_rmon_hist_range {
	u16 low;
	u16 high;
};

#define ETHTOOL_RMON_HIST_MAX	10

/**
 * struct ethtool_rmon_stats - selected RMON (RFC 2819) statistics
 * @src: input field denoting whether stats should be queried from the eMAC or
 *	pMAC (if the MM layer is supported). To be ignored otherwise.
 * @undersize_pkts: Equivalent to `etherStatsUndersizePkts` from the RFC.
 * @oversize_pkts: Equivalent to `etherStatsOversizePkts` from the RFC.
 * @fragments: Equivalent to `etherStatsFragments` from the RFC.
 * @jabbers: Equivalent to `etherStatsJabbers` from the RFC.
 * @hist: Packet counter for packet length buckets (e.g.
 *	`etherStatsPkts128to255Octets` from the RFC).
 * @hist_tx: Tx counters in similar form to @hist, not defined in the RFC.
 *
 * Selection of RMON (RFC 2819) statistics which are not exposed via different
 * APIs, primarily the packet-length-based counters.
 * Unfortunately different designs choose different buckets beyond
 * the 1024B mark (jumbo frame teritory), so the definition of the bucket
 * ranges is left to the driver.
 */
struct ethtool_rmon_stats {
	enum ethtool_mac_stats_src src;
	struct_group(stats,
		u64 undersize_pkts;
		u64 oversize_pkts;
		u64 fragments;
		u64 jabbers;

		u64 hist[ETHTOOL_RMON_HIST_MAX];
		u64 hist_tx[ETHTOOL_RMON_HIST_MAX];
	);
};

/**
 * struct ethtool_ts_stats - HW timestamping statistics
 * @pkts: Number of packets successfully timestamped by the hardware.
 * @lost: Number of hardware timestamping requests where the timestamping
 *	information from the hardware never arrived for submission with
 *	the skb.
 * @err: Number of arbitrary timestamp generation error events that the
 *	hardware encountered, exclusive of @lost statistics. Cases such
 *	as resource exhaustion, unavailability, firmware errors, and
 *	detected illogical timestamp values not submitted with the skb
 *	are inclusive to this counter.
 */
struct ethtool_ts_stats {
	struct_group(tx_stats,
		u64 pkts;
		u64 lost;
		u64 err;
	);
};

#define ETH_MODULE_EEPROM_PAGE_LEN	128
#define ETH_MODULE_MAX_I2C_ADDRESS	0x7f

/**
 * struct ethtool_module_eeprom - plug-in module EEPROM read / write parameters
 * @offset: When @offset is 0-127, it is used as an address to the Lower Memory
 *	(@page must be 0). Otherwise, it is used as an address to the
 *	Upper Memory.
 * @length: Number of bytes to read / write.
 * @page: Page number.
 * @bank: Bank number, if supported by EEPROM spec.
 * @i2c_address: I2C address of a page. Value less than 0x7f expected. Most
 *	EEPROMs use 0x50 or 0x51.
 * @data: Pointer to buffer with EEPROM data of @length size.
 */
struct ethtool_module_eeprom {
	u32	offset;
	u32	length;
	u8	page;
	u8	bank;
	u8	i2c_address;
	u8	*data;
};

/**
 * struct ethtool_module_power_mode_params - module power mode parameters
 * @policy: The power mode policy enforced by the host for the plug-in module.
 * @mode: The operational power mode of the plug-in module. Should be filled by
 *	device drivers on get operations.
 */
struct ethtool_module_power_mode_params {
	enum ethtool_module_power_mode_policy policy;
	enum ethtool_module_power_mode mode;
};

/**
 * struct ethtool_mm_state - 802.3 MAC merge layer state
 * @verify_time:
 *	wait time between verification attempts in ms (according to clause
 *	30.14.1.6 aMACMergeVerifyTime)
 * @max_verify_time:
 *	maximum accepted value for the @verify_time variable in set requests
 * @verify_status:
 *	state of the verification state machine of the MM layer (according to
 *	clause 30.14.1.2 aMACMergeStatusVerify)
 * @tx_enabled:
 *	set if the MM layer is administratively enabled in the TX direction
 *	(according to clause 30.14.1.3 aMACMergeEnableTx)
 * @tx_active:
 *	set if the MM layer is enabled in the TX direction, which makes FP
 *	possible (according to 30.14.1.5 aMACMergeStatusTx). This should be
 *	true if MM is enabled, and the verification status is either verified,
 *	or disabled.
 * @pmac_enabled:
 *	set if the preemptible MAC is powered on and is able to receive
 *	preemptible packets and respond to verification frames.
 * @verify_enabled:
 *	set if the Verify function of the MM layer (which sends SMD-V
 *	verification requests) is administratively enabled (regardless of
 *	whether it is currently in the ETHTOOL_MM_VERIFY_STATUS_DISABLED state
 *	or not), according to clause 30.14.1.4 aMACMergeVerifyDisableTx (but
 *	using positive rather than negative logic). The device should always
 *	respond to received SMD-V requests as long as @pmac_enabled is set.
 * @tx_min_frag_size:
 *	the minimum size of non-final mPacket fragments that the link partner
 *	supports receiving, expressed in octets. Compared to the definition
 *	from clause 30.14.1.7 aMACMergeAddFragSize which is expressed in the
 *	range 0 to 3 (requiring a translation to the size in octets according
 *	to the formula 64 * (1 + addFragSize) - 4), a value in a continuous and
 *	unbounded range can be specified here.
 * @rx_min_frag_size:
 *	the minimum size of non-final mPacket fragments that this device
 *	supports receiving, expressed in octets.
 */
struct ethtool_mm_state {
	u32 verify_time;
	u32 max_verify_time;
	enum ethtool_mm_verify_status verify_status;
	bool tx_enabled;
	bool tx_active;
	bool pmac_enabled;
	bool verify_enabled;
	u32 tx_min_frag_size;
	u32 rx_min_frag_size;
};

/**
 * struct ethtool_mm_cfg - 802.3 MAC merge layer configuration
 * @verify_time: see struct ethtool_mm_state
 * @verify_enabled: see struct ethtool_mm_state
 * @tx_enabled: see struct ethtool_mm_state
 * @pmac_enabled: see struct ethtool_mm_state
 * @tx_min_frag_size: see struct ethtool_mm_state
 */
struct ethtool_mm_cfg {
	u32 verify_time;
	bool verify_enabled;
	bool tx_enabled;
	bool pmac_enabled;
	u32 tx_min_frag_size;
};

/**
 * struct ethtool_mm_stats - 802.3 MAC merge layer statistics
 * @MACMergeFrameAssErrorCount:
 *	received MAC frames with reassembly errors
 * @MACMergeFrameSmdErrorCount:
 *	received MAC frames/fragments rejected due to unknown or incorrect SMD
 * @MACMergeFrameAssOkCount:
 *	received MAC frames that were successfully reassembled and passed up
 * @MACMergeFragCountRx:
 *	number of additional correct SMD-C mPackets received due to preemption
 * @MACMergeFragCountTx:
 *	number of additional mPackets sent due to preemption
 * @MACMergeHoldCount:
 *	number of times the MM layer entered the HOLD state, which blocks
 *	transmission of preemptible traffic
 */
struct ethtool_mm_stats {
	u64 MACMergeFrameAssErrorCount;
	u64 MACMergeFrameSmdErrorCount;
	u64 MACMergeFrameAssOkCount;
	u64 MACMergeFragCountRx;
	u64 MACMergeFragCountTx;
	u64 MACMergeHoldCount;
};

/**
 * struct ethtool_rxfh_param - RXFH (RSS) parameters
 * @hfunc: Defines the current RSS hash function used by HW (or to be set to).
 *	Valid values are one of the %ETH_RSS_HASH_*.
 * @indir_size: On SET, the array size of the user buffer for the
 *	indirection table, which may be zero, or
 *	%ETH_RXFH_INDIR_NO_CHANGE.  On GET (read from the driver),
 *	the array size of the hardware indirection table.
 * @indir: The indirection table of size @indir_size entries.
 * @key_size: On SET, the array size of the user buffer for the hash key,
 *	which may be zero.  On GET (read from the driver), the size of the
 *	hardware hash key.
 * @key: The hash key of size @key_size bytes.
 * @rss_context: RSS context identifier.  Context 0 is the default for normal
 *	traffic; other contexts can be referenced as the destination for RX flow
 *	classification rules.  On SET, %ETH_RXFH_CONTEXT_ALLOC is used
 *	to allocate a new RSS context; on return this field will
 *	contain the ID of the newly allocated context.
 * @rss_delete: Set to non-ZERO to remove the @rss_context context.
 * @input_xfrm: Defines how the input data is transformed. Valid values are one
 *	of %RXH_XFRM_*.
 */
struct ethtool_rxfh_param {
	u8	hfunc;
	u32	indir_size;
	u32	*indir;
	u32	key_size;
	u8	*key;
	u32	rss_context;
	u8	rss_delete;
	u8	input_xfrm;
};

/**
 * struct kernel_ethtool_ts_info - kernel copy of struct ethtool_ts_info
 * @cmd: command number = %ETHTOOL_GET_TS_INFO
 * @so_timestamping: bit mask of the sum of the supported SO_TIMESTAMPING flags
 * @phc_index: device index of the associated PHC, or -1 if there is none
 * @tx_types: bit mask of the supported hwtstamp_tx_types enumeration values
 * @rx_filters: bit mask of the supported hwtstamp_rx_filters enumeration values
 */
struct kernel_ethtool_ts_info {
	u32 cmd;
	u32 so_timestamping;
	int phc_index;
	enum hwtstamp_tx_types tx_types;
	enum hwtstamp_rx_filters rx_filters;
};

/**
 * struct ethtool_ops - optional netdev operations
 * @cap_link_lanes_supported: indicates if the driver supports lanes
 *	parameter.
 * @cap_rss_ctx_supported: indicates if the driver supports RSS
 *	contexts.
 * @cap_rss_sym_xor_supported: indicates if the driver supports symmetric-xor
 *	RSS.
 * @rxfh_indir_space: max size of RSS indirection tables, if indirection table
 *	size as returned by @get_rxfh_indir_size may change during lifetime
 *	of the device. Leave as 0 if the table size is constant.
 * @rxfh_key_space: same as @rxfh_indir_space, but for the key.
 * @rxfh_priv_size: size of the driver private data area the core should
 *	allocate for an RSS context (in &struct ethtool_rxfh_context).
 * @rxfh_max_num_contexts: maximum (exclusive) supported RSS context ID.
 *	If this is zero then the core may choose any (nonzero) ID, otherwise
 *	the core will only use IDs strictly less than this value, as the
 *	@rss_context argument to @create_rxfh_context and friends.
 * @supported_coalesce_params: supported types of interrupt coalescing.
 * @supported_ring_params: supported ring params.
 * @get_drvinfo: Report driver/device information. Modern drivers no
 *	longer have to implement this callback. Most fields are
 *	correctly filled in by the core using system information, or
 *	populated using other driver operations.
 * @get_regs_len: Get buffer length required for @get_regs
 * @get_regs: Get device registers
 * @get_wol: Report whether Wake-on-Lan is enabled
 * @set_wol: Turn Wake-on-Lan on or off.  Returns a negative error code
 *	or zero.
 * @get_msglevel: Report driver message level.  This should be the value
 *	of the @msg_enable field used by netif logging functions.
 * @set_msglevel: Set driver message level
 * @nway_reset: Restart autonegotiation.  Returns a negative error code
 *	or zero.
 * @get_link: Report whether physical link is up.  Will only be called if
 *	the netdev is up.  Should usually be set to ethtool_op_get_link(),
 *	which uses netif_carrier_ok().
 * @get_link_ext_state: Report link extended state. Should set link_ext_state and
 *	link_ext_substate (link_ext_substate of 0 means link_ext_substate is unknown,
 *	do not attach ext_substate attribute to netlink message). If link_ext_state
 *	and link_ext_substate are unknown, return -ENODATA. If not implemented,
 *	link_ext_state and link_ext_substate will not be sent to userspace.
 * @get_link_ext_stats: Read extra link-related counters.
 * @get_eeprom_len: Read range of EEPROM addresses for validation of
 *	@get_eeprom and @set_eeprom requests.
 *	Returns 0 if device does not support EEPROM access.
 * @get_eeprom: Read data from the device EEPROM.
 *	Should fill in the magic field.  Don't need to check len for zero
 *	or wraparound.  Fill in the data argument with the eeprom values
 *	from offset to offset + len.  Update len to the amount read.
 *	Returns an error or zero.
 * @set_eeprom: Write data to the device EEPROM.
 *	Should validate the magic field.  Don't need to check len for zero
 *	or wraparound.  Update len to the amount written.  Returns an error
 *	or zero.
 * @get_coalesce: Get interrupt coalescing parameters.  Returns a negative
 *	error code or zero.
 * @set_coalesce: Set interrupt coalescing parameters.  Supported coalescing
 *	types should be set in @supported_coalesce_params.
 *	Returns a negative error code or zero.
 * @get_ringparam: Report ring sizes
 * @set_ringparam: Set ring sizes.  Returns a negative error code or zero.
 * @get_pause_stats: Report pause frame statistics. Drivers must not zero
 *	statistics which they don't report. The stats structure is initialized
 *	to ETHTOOL_STAT_NOT_SET indicating driver does not report statistics.
 * @get_pauseparam: Report pause parameters
 * @set_pauseparam: Set pause parameters.  Returns a negative error code
 *	or zero.
 * @self_test: Run specified self-tests
 * @get_strings: Return a set of strings that describe the requested objects
 * @set_phys_id: Identify the physical devices, e.g. by flashing an LED
 *	attached to it.  The implementation may update the indicator
 *	asynchronously or synchronously, but in either case it must return
 *	quickly.  It is initially called with the argument %ETHTOOL_ID_ACTIVE,
 *	and must either activate asynchronous updates and return zero, return
 *	a negative error or return a positive frequency for synchronous
 *	indication (e.g. 1 for one on/off cycle per second).  If it returns
 *	a frequency then it will be called again at intervals with the
 *	argument %ETHTOOL_ID_ON or %ETHTOOL_ID_OFF and should set the state of
 *	the indicator accordingly.  Finally, it is called with the argument
 *	%ETHTOOL_ID_INACTIVE and must deactivate the indicator.  Returns a
 *	negative error code or zero.
 * @get_ethtool_stats: Return extended statistics about the device.
 *	This is only useful if the device maintains statistics not
 *	included in &struct rtnl_link_stats64.
 * @begin: Function to be called before any other operation.  Returns a
 *	negative error code or zero.
 * @complete: Function to be called after any other operation except
 *	@begin.  Will be called even if the other operation failed.
 * @get_priv_flags: Report driver-specific feature flags.
 * @set_priv_flags: Set driver-specific feature flags.  Returns a negative
 *	error code or zero.
 * @get_sset_count: Get number of strings that @get_strings will write.
 * @get_rxnfc: Get RX flow classification rules.  Returns a negative
 *	error code or zero.
 * @set_rxnfc: Set RX flow classification rules.  Returns a negative
 *	error code or zero.
 * @flash_device: Write a firmware image to device's flash memory.
 *	Returns a negative error code or zero.
 * @reset: Reset (part of) the device, as specified by a bitmask of
 *	flags from &enum ethtool_reset_flags.  Returns a negative
 *	error code or zero.
 * @get_rxfh_key_size: Get the size of the RX flow hash key.
 *	Returns zero if not supported for this specific device.
 * @get_rxfh_indir_size: Get the size of the RX flow hash indirection table.
 *	Returns zero if not supported for this specific device.
 * @get_rxfh: Get the contents of the RX flow hash indirection table, hash key
 *	and/or hash function.
 *	Returns a negative error code or zero.
 * @set_rxfh: Set the contents of the RX flow hash indirection table, hash
 *	key, and/or hash function.  Arguments which are set to %NULL or zero
 *	will remain unchanged.
 *	Returns a negative error code or zero. An error code must be returned
 *	if at least one unsupported change was requested.
 * @create_rxfh_context: Create a new RSS context with the specified RX flow
 *	hash indirection table, hash key, and hash function.
 *	The &struct ethtool_rxfh_context for this context is passed in @ctx;
 *	note that the indir table, hkey and hfunc are not yet populated as
 *	of this call.  The driver does not need to update these; the core
 *	will do so if this op succeeds.
 *	However, if @rxfh.indir is set to %NULL, the driver must update the
 *	indir table in @ctx with the (default or inherited) table actually in
 *	use; similarly, if @rxfh.key is %NULL, @rxfh.hfunc is
 *	%ETH_RSS_HASH_NO_CHANGE, or @rxfh.input_xfrm is %RXH_XFRM_NO_CHANGE,
 *	the driver should update the corresponding information in @ctx.
 *	If the driver provides this method, it must also provide
 *	@modify_rxfh_context and @remove_rxfh_context.
 *	Returns a negative error code or zero.
 * @modify_rxfh_context: Reconfigure the specified RSS context.  Allows setting
 *	the contents of the RX flow hash indirection table, hash key, and/or
 *	hash function associated with the given context.
 *	Parameters which are set to %NULL or zero will remain unchanged.
 *	The &struct ethtool_rxfh_context for this context is passed in @ctx;
 *	note that it will still contain the *old* settings.  The driver does
 *	not need to update these; the core will do so if this op succeeds.
 *	Returns a negative error code or zero. An error code must be returned
 *	if at least one unsupported change was requested.
 * @remove_rxfh_context: Remove the specified RSS context.
 *	The &struct ethtool_rxfh_context for this context is passed in @ctx.
 *	Returns a negative error code or zero.
 * @get_channels: Get number of channels.
 * @set_channels: Set number of channels.  Returns a negative error code or
 *	zero.
 * @get_dump_flag: Get dump flag indicating current dump length, version,
 * 		   and flag of the device.
 * @get_dump_data: Get dump data.
 * @set_dump: Set dump specific flags to the device.
 * @get_ts_info: Get the time stamping and PTP hardware clock capabilities.
 *	It may be called with RCU, or rtnl or reference on the device.
 *	Drivers supporting transmit time stamps in software should set this to
 *	ethtool_op_get_ts_info(). Drivers must not zero statistics which they
 *	don't report. The stats	structure is initialized to ETHTOOL_STAT_NOT_SET
 *	indicating driver does not report statistics.
 * @get_ts_stats: Query the device hardware timestamping statistics.
 * @get_module_info: Get the size and type of the eeprom contained within
 *	a plug-in module.
 * @get_module_eeprom: Get the eeprom information from the plug-in module
 * @get_eee: Get Energy-Efficient (EEE) supported and status.
 * @set_eee: Set EEE status (enable/disable) as well as LPI timers.
 * @get_tunable: Read the value of a driver / device tunable.
 * @set_tunable: Set the value of a driver / device tunable.
 * @get_per_queue_coalesce: Get interrupt coalescing parameters per queue.
 *	It must check that the given queue number is valid. If neither a RX nor
 *	a TX queue has this number, return -EINVAL. If only a RX queue or a TX
 *	queue has this number, set the inapplicable fields to ~0 and return 0.
 *	Returns a negative error code or zero.
 * @set_per_queue_coalesce: Set interrupt coalescing parameters per queue.
 *	It must check that the given queue number is valid. If neither a RX nor
 *	a TX queue has this number, return -EINVAL. If only a RX queue or a TX
 *	queue has this number, ignore the inapplicable fields. Supported
 *	coalescing types should be set in @supported_coalesce_params.
 *	Returns a negative error code or zero.
 * @get_link_ksettings: Get various device settings including Ethernet link
 *	settings. The %cmd and %link_mode_masks_nwords fields should be
 *	ignored (use %__ETHTOOL_LINK_MODE_MASK_NBITS instead of the latter),
 *	any change to them will be overwritten by kernel. Returns a negative
 *	error code or zero.
 * @set_link_ksettings: Set various device settings including Ethernet link
 *	settings. The %cmd and %link_mode_masks_nwords fields should be
 *	ignored (use %__ETHTOOL_LINK_MODE_MASK_NBITS instead of the latter),
 *	any change to them will be overwritten by kernel. Returns a negative
 *	error code or zero.
 * @get_fec_stats: Report FEC statistics.
 *	Core will sum up per-lane stats to get the total.
 *	Drivers must not zero statistics which they don't report. The stats
 *	structure is initialized to ETHTOOL_STAT_NOT_SET indicating driver does
 *	not report statistics.
 * @get_fecparam: Get the network device Forward Error Correction parameters.
 * @set_fecparam: Set the network device Forward Error Correction parameters.
 * @get_ethtool_phy_stats: Return extended statistics about the PHY device.
 *	This is only useful if the device maintains PHY statistics and
 *	cannot use the standard PHY library helpers.
 * @get_phy_tunable: Read the value of a PHY tunable.
 * @set_phy_tunable: Set the value of a PHY tunable.
 * @get_module_eeprom_by_page: Get a region of plug-in module EEPROM data from
 *	specified page. Returns a negative error code or the amount of bytes
 *	read.
 * @set_module_eeprom_by_page: Write to a region of plug-in module EEPROM,
 *	from kernel space only. Returns a negative error code or zero.
 * @get_eth_phy_stats: Query some of the IEEE 802.3 PHY statistics.
 * @get_eth_mac_stats: Query some of the IEEE 802.3 MAC statistics.
 * @get_eth_ctrl_stats: Query some of the IEEE 802.3 MAC Ctrl statistics.
 * @get_rmon_stats: Query some of the RMON (RFC 2819) statistics.
 *	Set %ranges to a pointer to zero-terminated array of byte ranges.
 * @get_module_power_mode: Get the power mode policy for the plug-in module
 *	used by the network device and its operational power mode, if
 *	plugged-in.
 * @set_module_power_mode: Set the power mode policy for the plug-in module
 *	used by the network device.
 * @get_mm: Query the 802.3 MAC Merge layer state.
 * @set_mm: Set the 802.3 MAC Merge layer parameters.
 * @get_mm_stats: Query the 802.3 MAC Merge layer statistics.
 *
 * All operations are optional (i.e. the function pointer may be set
 * to %NULL) and callers must take this into account.  Callers must
 * hold the RTNL lock.
 *
 * See the structures used by these operations for further documentation.
 * Note that for all operations using a structure ending with a zero-
 * length array, the array is allocated separately in the kernel and
 * is passed to the driver as an additional parameter.
 *
 * See &struct net_device and &struct net_device_ops for documentation
 * of the generic netdev features interface.
 */
struct ethtool_ops {
	u32     cap_link_lanes_supported:1;
	u32     cap_rss_ctx_supported:1;
	u32	cap_rss_sym_xor_supported:1;
	u32	rxfh_indir_space;
	u16	rxfh_key_space;
	u16	rxfh_priv_size;
	u32	rxfh_max_num_contexts;
	u32	supported_coalesce_params;
	u32	supported_ring_params;
	void	(*get_drvinfo)(struct net_device *, struct ethtool_drvinfo *);
	int	(*get_regs_len)(struct net_device *);
	void	(*get_regs)(struct net_device *, struct ethtool_regs *, void *);
	void	(*get_wol)(struct net_device *, struct ethtool_wolinfo *);
	int	(*set_wol)(struct net_device *, struct ethtool_wolinfo *);
	u32	(*get_msglevel)(struct net_device *);
	void	(*set_msglevel)(struct net_device *, u32);
	int	(*nway_reset)(struct net_device *);
	u32	(*get_link)(struct net_device *);
	int	(*get_link_ext_state)(struct net_device *,
				      struct ethtool_link_ext_state_info *);
	void	(*get_link_ext_stats)(struct net_device *dev,
				      struct ethtool_link_ext_stats *stats);
	int	(*get_eeprom_len)(struct net_device *);
	int	(*get_eeprom)(struct net_device *,
			      struct ethtool_eeprom *, u8 *);
	int	(*set_eeprom)(struct net_device *,
			      struct ethtool_eeprom *, u8 *);
	int	(*get_coalesce)(struct net_device *,
				struct ethtool_coalesce *,
				struct kernel_ethtool_coalesce *,
				struct netlink_ext_ack *);
	int	(*set_coalesce)(struct net_device *,
				struct ethtool_coalesce *,
				struct kernel_ethtool_coalesce *,
				struct netlink_ext_ack *);
	void	(*get_ringparam)(struct net_device *,
				 struct ethtool_ringparam *,
				 struct kernel_ethtool_ringparam *,
				 struct netlink_ext_ack *);
	int	(*set_ringparam)(struct net_device *,
				 struct ethtool_ringparam *,
				 struct kernel_ethtool_ringparam *,
				 struct netlink_ext_ack *);
	void	(*get_pause_stats)(struct net_device *dev,
				   struct ethtool_pause_stats *pause_stats);
	void	(*get_pauseparam)(struct net_device *,
				  struct ethtool_pauseparam*);
	int	(*set_pauseparam)(struct net_device *,
				  struct ethtool_pauseparam*);
	void	(*self_test)(struct net_device *, struct ethtool_test *, u64 *);
	void	(*get_strings)(struct net_device *, u32 stringset, u8 *);
	int	(*set_phys_id)(struct net_device *, enum ethtool_phys_id_state);
	void	(*get_ethtool_stats)(struct net_device *,
				     struct ethtool_stats *, u64 *);
	int	(*begin)(struct net_device *);
	void	(*complete)(struct net_device *);
	u32	(*get_priv_flags)(struct net_device *);
	int	(*set_priv_flags)(struct net_device *, u32);
	int	(*get_sset_count)(struct net_device *, int);
	int	(*get_rxnfc)(struct net_device *,
			     struct ethtool_rxnfc *, u32 *rule_locs);
	int	(*set_rxnfc)(struct net_device *, struct ethtool_rxnfc *);
	int	(*flash_device)(struct net_device *, struct ethtool_flash *);
	int	(*reset)(struct net_device *, u32 *);
	u32	(*get_rxfh_key_size)(struct net_device *);
	u32	(*get_rxfh_indir_size)(struct net_device *);
	int	(*get_rxfh)(struct net_device *, struct ethtool_rxfh_param *);
	int	(*set_rxfh)(struct net_device *, struct ethtool_rxfh_param *,
			    struct netlink_ext_ack *extack);
	int	(*create_rxfh_context)(struct net_device *,
				       struct ethtool_rxfh_context *ctx,
				       const struct ethtool_rxfh_param *rxfh,
				       struct netlink_ext_ack *extack);
	int	(*modify_rxfh_context)(struct net_device *,
				       struct ethtool_rxfh_context *ctx,
				       const struct ethtool_rxfh_param *rxfh,
				       struct netlink_ext_ack *extack);
	int	(*remove_rxfh_context)(struct net_device *,
				       struct ethtool_rxfh_context *ctx,
				       u32 rss_context,
				       struct netlink_ext_ack *extack);
	void	(*get_channels)(struct net_device *, struct ethtool_channels *);
	int	(*set_channels)(struct net_device *, struct ethtool_channels *);
	int	(*get_dump_flag)(struct net_device *, struct ethtool_dump *);
	int	(*get_dump_data)(struct net_device *,
				 struct ethtool_dump *, void *);
	int	(*set_dump)(struct net_device *, struct ethtool_dump *);
	int	(*get_ts_info)(struct net_device *, struct kernel_ethtool_ts_info *);
	void	(*get_ts_stats)(struct net_device *dev,
				struct ethtool_ts_stats *ts_stats);
	int     (*get_module_info)(struct net_device *,
				   struct ethtool_modinfo *);
	int     (*get_module_eeprom)(struct net_device *,
				     struct ethtool_eeprom *, u8 *);
	int	(*get_eee)(struct net_device *dev, struct ethtool_keee *eee);
	int	(*set_eee)(struct net_device *dev, struct ethtool_keee *eee);
	int	(*get_tunable)(struct net_device *,
			       const struct ethtool_tunable *, void *);
	int	(*set_tunable)(struct net_device *,
			       const struct ethtool_tunable *, const void *);
	int	(*get_per_queue_coalesce)(struct net_device *, u32,
					  struct ethtool_coalesce *);
	int	(*set_per_queue_coalesce)(struct net_device *, u32,
					  struct ethtool_coalesce *);
	int	(*get_link_ksettings)(struct net_device *,
				      struct ethtool_link_ksettings *);
	int	(*set_link_ksettings)(struct net_device *,
				      const struct ethtool_link_ksettings *);
	void	(*get_fec_stats)(struct net_device *dev,
				 struct ethtool_fec_stats *fec_stats);
	int	(*get_fecparam)(struct net_device *,
				      struct ethtool_fecparam *);
	int	(*set_fecparam)(struct net_device *,
				      struct ethtool_fecparam *);
	void	(*get_ethtool_phy_stats)(struct net_device *,
					 struct ethtool_stats *, u64 *);
	int	(*get_phy_tunable)(struct net_device *,
				   const struct ethtool_tunable *, void *);
	int	(*set_phy_tunable)(struct net_device *,
				   const struct ethtool_tunable *, const void *);
	int	(*get_module_eeprom_by_page)(struct net_device *dev,
					     const struct ethtool_module_eeprom *page,
					     struct netlink_ext_ack *extack);
	int	(*set_module_eeprom_by_page)(struct net_device *dev,
					     const struct ethtool_module_eeprom *page,
					     struct netlink_ext_ack *extack);
	void	(*get_eth_phy_stats)(struct net_device *dev,
				     struct ethtool_eth_phy_stats *phy_stats);
	void	(*get_eth_mac_stats)(struct net_device *dev,
				     struct ethtool_eth_mac_stats *mac_stats);
	void	(*get_eth_ctrl_stats)(struct net_device *dev,
				      struct ethtool_eth_ctrl_stats *ctrl_stats);
	void	(*get_rmon_stats)(struct net_device *dev,
				  struct ethtool_rmon_stats *rmon_stats,
				  const struct ethtool_rmon_hist_range **ranges);
	int	(*get_module_power_mode)(struct net_device *dev,
					 struct ethtool_module_power_mode_params *params,
					 struct netlink_ext_ack *extack);
	int	(*set_module_power_mode)(struct net_device *dev,
					 const struct ethtool_module_power_mode_params *params,
					 struct netlink_ext_ack *extack);
	int	(*get_mm)(struct net_device *dev, struct ethtool_mm_state *state);
	int	(*set_mm)(struct net_device *dev, struct ethtool_mm_cfg *cfg,
			  struct netlink_ext_ack *extack);
	void	(*get_mm_stats)(struct net_device *dev, struct ethtool_mm_stats *stats);
};

int ethtool_check_ops(const struct ethtool_ops *ops);

struct ethtool_rx_flow_rule {
	struct flow_rule	*rule;
	unsigned long		priv[];
};

struct ethtool_rx_flow_spec_input {
	const struct ethtool_rx_flow_spec	*fs;
	u32					rss_ctx;
};

struct ethtool_rx_flow_rule *
ethtool_rx_flow_rule_create(const struct ethtool_rx_flow_spec_input *input);
void ethtool_rx_flow_rule_destroy(struct ethtool_rx_flow_rule *rule);

bool ethtool_virtdev_validate_cmd(const struct ethtool_link_ksettings *cmd);
int ethtool_virtdev_set_link_ksettings(struct net_device *dev,
				       const struct ethtool_link_ksettings *cmd,
				       u32 *dev_speed, u8 *dev_duplex);

/**
 * struct ethtool_netdev_state - per-netdevice state for ethtool features
 * @rss_ctx:		XArray of custom RSS contexts
 * @rss_lock:		Protects entries in @rss_ctx.  May be taken from
 *			within RTNL.
 * @wol_enabled:	Wake-on-LAN is enabled
 * @module_fw_flash_in_progress: Module firmware flashing is in progress.
 */
struct ethtool_netdev_state {
	struct xarray		rss_ctx;
	struct mutex		rss_lock;
	unsigned		wol_enabled:1;
	unsigned		module_fw_flash_in_progress:1;
};

struct phy_device;
struct phy_tdr_config;
struct phy_plca_cfg;
struct phy_plca_status;

/**
 * struct ethtool_phy_ops - Optional PHY device options
 * @get_sset_count: Get number of strings that @get_strings will write.
 * @get_strings: Return a set of strings that describe the requested objects
 * @get_stats: Return extended statistics about the PHY device.
 * @get_plca_cfg: Return PLCA configuration.
 * @set_plca_cfg: Set PLCA configuration.
 * @get_plca_status: Get PLCA configuration.
 * @start_cable_test: Start a cable test
 * @start_cable_test_tdr: Start a Time Domain Reflectometry cable test
 *
 * All operations are optional (i.e. the function pointer may be set to %NULL)
 * and callers must take this into account. Callers must hold the RTNL lock.
 */
struct ethtool_phy_ops {
	int (*get_sset_count)(struct phy_device *dev);
	int (*get_strings)(struct phy_device *dev, u8 *data);
	int (*get_stats)(struct phy_device *dev,
			 struct ethtool_stats *stats, u64 *data);
	int (*get_plca_cfg)(struct phy_device *dev,
			    struct phy_plca_cfg *plca_cfg);
	int (*set_plca_cfg)(struct phy_device *dev,
			    const struct phy_plca_cfg *plca_cfg,
			    struct netlink_ext_ack *extack);
	int (*get_plca_status)(struct phy_device *dev,
			       struct phy_plca_status *plca_st);
	int (*start_cable_test)(struct phy_device *phydev,
				struct netlink_ext_ack *extack);
	int (*start_cable_test_tdr)(struct phy_device *phydev,
				    struct netlink_ext_ack *extack,
				    const struct phy_tdr_config *config);
};

/**
 * ethtool_set_ethtool_phy_ops - Set the ethtool_phy_ops singleton
 * @ops: Ethtool PHY operations to set
 */
void ethtool_set_ethtool_phy_ops(const struct ethtool_phy_ops *ops);

/**
 * ethtool_params_from_link_mode - Derive link parameters from a given link mode
 * @link_ksettings: Link parameters to be derived from the link mode
 * @link_mode: Link mode
 */
void
ethtool_params_from_link_mode(struct ethtool_link_ksettings *link_ksettings,
			      enum ethtool_link_mode_bit_indices link_mode);

/**
 * ethtool_get_phc_vclocks - Derive phc vclocks information, and caller
 *                           is responsible to free memory of vclock_index
 * @dev: pointer to net_device structure
 * @vclock_index: pointer to pointer of vclock index
 *
 * Return number of phc vclocks
 */
int ethtool_get_phc_vclocks(struct net_device *dev, int **vclock_index);

/* Some generic methods drivers may use in their ethtool_ops */
u32 ethtool_op_get_link(struct net_device *dev);
int ethtool_op_get_ts_info(struct net_device *dev,
			   struct kernel_ethtool_ts_info *eti);

/**
 * ethtool_mm_frag_size_add_to_min - Translate (standard) additional fragment
 *	size expressed as multiplier into (absolute) minimum fragment size
 *	value expressed in octets
 * @val_add: Value of addFragSize multiplier
 */
static inline u32 ethtool_mm_frag_size_add_to_min(u32 val_add)
{
	return (ETH_ZLEN + ETH_FCS_LEN) * (1 + val_add) - ETH_FCS_LEN;
}

/**
 * ethtool_mm_frag_size_min_to_add - Translate (absolute) minimum fragment size
 *	expressed in octets into (standard) additional fragment size expressed
 *	as multiplier
 * @val_min: Value of addFragSize variable in octets
 * @val_add: Pointer where the standard addFragSize value is to be returned
 * @extack: Netlink extended ack
 *
 * Translate a value in octets to one of 0, 1, 2, 3 according to the reverse
 * application of the 802.3 formula 64 * (1 + addFragSize) - 4. To be called
 * by drivers which do not support programming the minimum fragment size to a
 * continuous range. Returns error on other fragment length values.
 */
static inline int ethtool_mm_frag_size_min_to_add(u32 val_min, u32 *val_add,
						  struct netlink_ext_ack *extack)
{
	u32 add_frag_size;

	for (add_frag_size = 0; add_frag_size < 4; add_frag_size++) {
		if (ethtool_mm_frag_size_add_to_min(add_frag_size) == val_min) {
			*val_add = add_frag_size;
			return 0;
		}
	}

	NL_SET_ERR_MSG_MOD(extack,
			   "minFragSize required to be one of 60, 124, 188 or 252");
	return -EINVAL;
}

/**
 * ethtool_get_ts_info_by_layer - Obtains time stamping capabilities from the MAC or PHY layer.
 * @dev: pointer to net_device structure
 * @info: buffer to hold the result
 * Returns zero on success, non-zero otherwise.
 */
int ethtool_get_ts_info_by_layer(struct net_device *dev,
				 struct kernel_ethtool_ts_info *info);

/**
 * ethtool_sprintf - Write formatted string to ethtool string data
 * @data: Pointer to a pointer to the start of string to update
 * @fmt: Format of string to write
 *
 * Write formatted string to *data. Update *data to point at start of
 * next string.
 */
extern __printf(2, 3) void ethtool_sprintf(u8 **data, const char *fmt, ...);

/**
 * ethtool_puts - Write string to ethtool string data
 * @data: Pointer to a pointer to the start of string to update
 * @str: String to write
 *
 * Write string to *data without a trailing newline. Update *data
 * to point at start of next string.
 *
 * Prefer this function to ethtool_sprintf() when given only
 * two arguments or if @fmt is just "%s".
 */
extern void ethtool_puts(u8 **data, const char *str);

/* Link mode to forced speed capabilities maps */
struct ethtool_forced_speed_map {
	u32		speed;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(caps);

	const u32	*cap_arr;
	u32		arr_size;
};

#define ETHTOOL_FORCED_SPEED_MAP(prefix, value)				\
{									\
	.speed		= SPEED_##value,				\
	.cap_arr	= prefix##_##value,				\
	.arr_size	= ARRAY_SIZE(prefix##_##value),			\
}

void
ethtool_forced_speed_maps_init(struct ethtool_forced_speed_map *maps, u32 size);

/* C33 PSE extended state and substate. */
struct ethtool_c33_pse_ext_state_info {
	enum ethtool_c33_pse_ext_state c33_pse_ext_state;
	union {
		enum ethtool_c33_pse_ext_substate_error_condition error_condition;
		enum ethtool_c33_pse_ext_substate_mr_pse_enable mr_pse_enable;
		enum ethtool_c33_pse_ext_substate_option_detect_ted option_detect_ted;
		enum ethtool_c33_pse_ext_substate_option_vport_lim option_vport_lim;
		enum ethtool_c33_pse_ext_substate_ovld_detected ovld_detected;
		enum ethtool_c33_pse_ext_substate_power_not_available power_not_available;
		enum ethtool_c33_pse_ext_substate_short_detected short_detected;
		u32 __c33_pse_ext_substate;
	};
};

struct ethtool_c33_pse_pw_limit_range {
	u32 min;
	u32 max;
};
#endif /* _LINUX_ETHTOOL_H */
