/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IEEE 802.11 mesh definitions
 *
 * Copyright (c) 2001-2002, SSH Communications Security Corp and Jouni Malinen
 * <jkmaline@cc.hut.fi>
 * Copyright (c) 2002-2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright (c) 2005, Devicescape Software, Inc.
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright (c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright (c) 2018 - 2025 Intel Corporation
 */

#ifndef LINUX_IEEE80211_MESH_H
#define LINUX_IEEE80211_MESH_H

#include <linux/types.h>
#include <linux/if_ether.h>

#define IEEE80211_MAX_MESH_ID_LEN	32

struct ieee80211s_hdr {
	u8 flags;
	u8 ttl;
	__le32 seqnum;
	u8 eaddr1[ETH_ALEN];
	u8 eaddr2[ETH_ALEN];
} __packed __aligned(2);

struct ieee80211_mesh_hwmp_preq_target {
	u8 flags;
	u8 addr[ETH_ALEN];
	__le32 sn;
} __packed;

struct ieee80211_mesh_hwmp_preq_top {
	u8 flags;
	u8 hopcount;
	u8 ttl;
	__le32 preq_id;
	u8 orig_addr[ETH_ALEN];
	__le32 orig_sn;

	/* optional AE, lifetime, metric, target */
	u8 variable[];
} __packed;

struct ieee80211_mesh_hwmp_preq_bottom {
	__le32 lifetime;
	__le32 metric;
	u8 target_count;
	struct ieee80211_mesh_hwmp_preq_target targets[];
} __packed;

struct ieee80211_mesh_hwmp_prep_top {
	u8 flags;
	u8 hopcount;
	u8 ttl;
	u8 target_addr[ETH_ALEN];
	__le32 target_sn;

	/* optional Target External Address */
	u8 variable[];
} __packed;

struct ieee80211_mesh_hwmp_prep_bottom {
	__le32 lifetime;
	__le32 metric;
	u8 orig_addr[ETH_ALEN];
	__le32 orig_sn;
} __packed;

struct ieee80211_mesh_hwmp_perr_dst {
	u8 flags;
	u8 addr[ETH_ALEN];
	__le32 sn;
	/* optional Destination External Address */
	u8 variable[];
} __packed;

struct ieee80211_mesh_hwmp_perr {
	u8 ttl;
	u8 number_of_dst;
	/* Destinations */
	u8 variable[];
} __packed;

/* Mesh flags */
#define MESH_FLAGS_AE_A4 	0x1
#define MESH_FLAGS_AE_A5_A6	0x2
#define MESH_FLAGS_AE		0x3
#define MESH_FLAGS_PS_DEEP	0x4

/* HWMP IE processing macros */
#define AE_F			(1<<6)

/**
 * enum ieee80211_preq_flags - mesh PREQ element flags
 *
 * @IEEE80211_PREQ_PROACTIVE_PREP_FLAG: proactive PREP subfield
 */
enum ieee80211_preq_flags {
	IEEE80211_PREQ_PROACTIVE_PREP_FLAG	= 1<<2,
};

/**
 * enum ieee80211_preq_target_flags - mesh PREQ element per target flags
 *
 * @IEEE80211_PREQ_TO_FLAG: target only subfield
 * @IEEE80211_PREQ_USN_FLAG: unknown target HWMP sequence number subfield
 */
enum ieee80211_preq_target_flags {
	IEEE80211_PREQ_TO_FLAG	= 1<<0,
	IEEE80211_PREQ_USN_FLAG	= 1<<2,
};

/**
 * struct ieee80211_mesh_chansw_params_ie - mesh channel switch parameters IE
 * @mesh_ttl: Time To Live
 * @mesh_flags: Flags
 * @mesh_reason: Reason Code
 * @mesh_pre_value: Precedence Value
 *
 * This structure represents the payload of the "Mesh Channel Switch
 * Parameters element" as described in IEEE Std 802.11-2020 section
 * 9.4.2.102.
 */
struct ieee80211_mesh_chansw_params_ie {
	u8 mesh_ttl;
	u8 mesh_flags;
	__le16 mesh_reason;
	__le16 mesh_pre_value;
} __packed;

/**
 * struct ieee80211_meshconf_ie - Mesh Configuration element
 * @meshconf_psel: Active Path Selection Protocol Identifier
 * @meshconf_pmetric: Active Path Selection Metric Identifier
 * @meshconf_congest: Congestion Control Mode Identifier
 * @meshconf_synch: Synchronization Method Identifier
 * @meshconf_auth: Authentication Protocol Identifier
 * @meshconf_form: Mesh Formation Info
 * @meshconf_cap: Mesh Capability (see &enum mesh_config_capab_flags)
 *
 * This structure represents the payload of the "Mesh Configuration
 * element" as described in IEEE Std 802.11-2020 section 9.4.2.97.
 */
struct ieee80211_meshconf_ie {
	u8 meshconf_psel;
	u8 meshconf_pmetric;
	u8 meshconf_congest;
	u8 meshconf_synch;
	u8 meshconf_auth;
	u8 meshconf_form;
	u8 meshconf_cap;
} __packed;

/**
 * enum mesh_config_capab_flags - Mesh Configuration IE capability field flags
 *
 * @IEEE80211_MESHCONF_CAPAB_ACCEPT_PLINKS: STA is willing to establish
 *	additional mesh peerings with other mesh STAs
 * @IEEE80211_MESHCONF_CAPAB_FORWARDING: the STA forwards MSDUs
 * @IEEE80211_MESHCONF_CAPAB_TBTT_ADJUSTING: TBTT adjustment procedure
 *	is ongoing
 * @IEEE80211_MESHCONF_CAPAB_POWER_SAVE_LEVEL: STA is in deep sleep mode or has
 *	neighbors in deep sleep mode
 *
 * Enumerates the "Mesh Capability" as described in IEEE Std
 * 802.11-2020 section 9.4.2.97.7.
 */
enum mesh_config_capab_flags {
	IEEE80211_MESHCONF_CAPAB_ACCEPT_PLINKS		= 0x01,
	IEEE80211_MESHCONF_CAPAB_FORWARDING		= 0x08,
	IEEE80211_MESHCONF_CAPAB_TBTT_ADJUSTING		= 0x20,
	IEEE80211_MESHCONF_CAPAB_POWER_SAVE_LEVEL	= 0x40,
};

#define IEEE80211_MESHCONF_FORM_CONNECTED_TO_GATE 0x1

/*
 * mesh channel switch parameters element's flag indicator
 *
 */
#define WLAN_EID_CHAN_SWITCH_PARAM_TX_RESTRICT BIT(0)
#define WLAN_EID_CHAN_SWITCH_PARAM_INITIATOR BIT(1)
#define WLAN_EID_CHAN_SWITCH_PARAM_REASON BIT(2)

/**
 * struct ieee80211_rann_ie - RANN (root announcement) element
 * @rann_flags: Flags
 * @rann_hopcount: Hop Count
 * @rann_ttl: Element TTL
 * @rann_addr: Root Mesh STA Address
 * @rann_seq: HWMP Sequence Number
 * @rann_interval: Interval
 * @rann_metric: Metric
 *
 * This structure represents the payload of the "RANN element" as
 * described in IEEE Std 802.11-2020 section 9.4.2.111.
 */
struct ieee80211_rann_ie {
	u8 rann_flags;
	u8 rann_hopcount;
	u8 rann_ttl;
	u8 rann_addr[ETH_ALEN];
	__le32 rann_seq;
	__le32 rann_interval;
	__le32 rann_metric;
} __packed;

enum ieee80211_rann_flags {
	RANN_FLAG_IS_GATE = 1 << 0,
};

/* Mesh action codes */
enum ieee80211_mesh_actioncode {
	WLAN_MESH_ACTION_LINK_METRIC_REPORT,
	WLAN_MESH_ACTION_HWMP_PATH_SELECTION,
	WLAN_MESH_ACTION_GATE_ANNOUNCEMENT,
	WLAN_MESH_ACTION_CONGESTION_CONTROL_NOTIFICATION,
	WLAN_MESH_ACTION_MCCA_SETUP_REQUEST,
	WLAN_MESH_ACTION_MCCA_SETUP_REPLY,
	WLAN_MESH_ACTION_MCCA_ADVERTISEMENT_REQUEST,
	WLAN_MESH_ACTION_MCCA_ADVERTISEMENT,
	WLAN_MESH_ACTION_MCCA_TEARDOWN,
	WLAN_MESH_ACTION_TBTT_ADJUSTMENT_REQUEST,
	WLAN_MESH_ACTION_TBTT_ADJUSTMENT_RESPONSE,
};

/**
 * enum ieee80211_mesh_sync_method - mesh synchronization method identifier
 *
 * @IEEE80211_SYNC_METHOD_NEIGHBOR_OFFSET: the default synchronization method
 * @IEEE80211_SYNC_METHOD_VENDOR: a vendor specific synchronization method
 *	that will be specified in a vendor specific information element
 */
enum ieee80211_mesh_sync_method {
	IEEE80211_SYNC_METHOD_NEIGHBOR_OFFSET = 1,
	IEEE80211_SYNC_METHOD_VENDOR = 255,
};

/**
 * enum ieee80211_mesh_path_protocol - mesh path selection protocol identifier
 *
 * @IEEE80211_PATH_PROTOCOL_HWMP: the default path selection protocol
 * @IEEE80211_PATH_PROTOCOL_VENDOR: a vendor specific protocol that will
 *	be specified in a vendor specific information element
 */
enum ieee80211_mesh_path_protocol {
	IEEE80211_PATH_PROTOCOL_HWMP = 1,
	IEEE80211_PATH_PROTOCOL_VENDOR = 255,
};

/**
 * enum ieee80211_mesh_path_metric - mesh path selection metric identifier
 *
 * @IEEE80211_PATH_METRIC_AIRTIME: the default path selection metric
 * @IEEE80211_PATH_METRIC_VENDOR: a vendor specific metric that will be
 *	specified in a vendor specific information element
 */
enum ieee80211_mesh_path_metric {
	IEEE80211_PATH_METRIC_AIRTIME = 1,
	IEEE80211_PATH_METRIC_VENDOR = 255,
};

/**
 * enum ieee80211_root_mode_identifier - root mesh STA mode identifier
 *
 * These attribute are used by dot11MeshHWMPRootMode to set root mesh STA mode
 *
 * @IEEE80211_ROOTMODE_NO_ROOT: the mesh STA is not a root mesh STA (default)
 * @IEEE80211_ROOTMODE_ROOT: the mesh STA is a root mesh STA if greater than
 *	this value
 * @IEEE80211_PROACTIVE_PREQ_NO_PREP: the mesh STA is a root mesh STA supports
 *	the proactive PREQ with proactive PREP subfield set to 0
 * @IEEE80211_PROACTIVE_PREQ_WITH_PREP: the mesh STA is a root mesh STA
 *	supports the proactive PREQ with proactive PREP subfield set to 1
 * @IEEE80211_PROACTIVE_RANN: the mesh STA is a root mesh STA supports
 *	the proactive RANN
 */
enum ieee80211_root_mode_identifier {
	IEEE80211_ROOTMODE_NO_ROOT = 0,
	IEEE80211_ROOTMODE_ROOT = 1,
	IEEE80211_PROACTIVE_PREQ_NO_PREP = 2,
	IEEE80211_PROACTIVE_PREQ_WITH_PREP = 3,
	IEEE80211_PROACTIVE_RANN = 4,
};

static inline bool ieee80211_mesh_preq_prep_ae_enabled(const u8 *ie)
{
	return ie[0] & AE_F;
}

static inline struct ieee80211_mesh_hwmp_preq_bottom *
ieee80211_mesh_hwmp_preq_get_bottom(const u8 *ie)
{
	struct ieee80211_mesh_hwmp_preq_top *top = (void *)ie;

	return (void *)&top->variable[
		ieee80211_mesh_preq_prep_ae_enabled(ie) ? ETH_ALEN : 0];
}

static inline struct ieee80211_mesh_hwmp_prep_bottom *
ieee80211_mesh_hwmp_prep_get_bottom(const u8 *ie)
{
	struct ieee80211_mesh_hwmp_prep_top *top = (void *)ie;

	return (void *)&top->variable[
		ieee80211_mesh_preq_prep_ae_enabled(ie) ? ETH_ALEN : 0];
}

static inline struct ieee80211_mesh_hwmp_perr_dst *
ieee80211_mesh_hwmp_perr_get_dst(const u8 *ie, u8 dst_idx)
{
	struct ieee80211_mesh_hwmp_perr *perr_ie = (void *)ie;
	struct ieee80211_mesh_hwmp_perr_dst *dst;
	u8 *pos = perr_ie->variable;
	int i;

	for (i = 0; i < dst_idx + 1; i++) {
		dst = (void *)pos;
		pos += sizeof(struct ieee80211_mesh_hwmp_perr_dst) +
			  ((dst->flags & AE_F) ? ETH_ALEN : 0)
			  /* Destination External Address */ +
			  2 /* Reason Code */;
	}

	return dst;
}

static inline u8 *
ieee80211_mesh_hwmp_perr_get_addr(const u8 *ie, u8 dst_idx)
{
	struct ieee80211_mesh_hwmp_perr_dst *dst =
		ieee80211_mesh_hwmp_perr_get_dst(ie, dst_idx);

	return dst->addr;
}

static inline u32
ieee80211_mesh_hwmp_perr_get_sn(const u8 *ie, u8 dst_idx)
{
	struct ieee80211_mesh_hwmp_perr_dst *dst =
		ieee80211_mesh_hwmp_perr_get_dst(ie, dst_idx);

	return le32_to_cpu(dst->sn);
}

static inline u16
ieee80211_mesh_hwmp_perr_get_rcode(const u8 *ie, u8 dst_idx)
{
	struct ieee80211_mesh_hwmp_perr_dst *dst =
		ieee80211_mesh_hwmp_perr_get_dst(ie, dst_idx);

	return get_unaligned_le16(&dst->variable[
		(dst->flags & AE_F) ? ETH_ALEN : 0]);
}

/* IEEE Std 802.11-2016 9.4.2.113 PREQ element */
static inline bool ieee80211_mesh_preq_size_ok(const u8 *pos, u8 elen)
{
	struct ieee80211_mesh_hwmp_preq_bottom *preq_elem_bottom =
		ieee80211_mesh_hwmp_preq_get_bottom(pos);
	u8 target_count;
	int needed;

	/* Check if the element contains flags */
	needed = sizeof(struct ieee80211_mesh_hwmp_preq_top);
	if (elen < needed)
		return false;

	/* Check if the element contains target_count */
	needed += (ieee80211_mesh_preq_prep_ae_enabled(pos) ? ETH_ALEN : 0)
		 /* Originator External Address */ +
		 sizeof(struct ieee80211_mesh_hwmp_preq_bottom);
	if (elen < needed)
		return false;

	target_count = preq_elem_bottom->target_count;
	/* IEEE Std 802.11-2016 Table 14-10 to 14-16 */
	if (target_count < 1)
		return false;

	needed += target_count * sizeof(struct ieee80211_mesh_hwmp_preq_target);
	return elen == needed;
}

/* IEEE Std 802.11-2016 9.4.2.114 PREP element */
static inline bool ieee80211_mesh_prep_size_ok(const u8 *pos, u8 elen)
{
	u8 needed;

	/* Check if the element contains flags */
	needed = sizeof(struct ieee80211_mesh_hwmp_prep_top);
	if (elen < needed)
		return false;

	needed += (ieee80211_mesh_preq_prep_ae_enabled(pos) ? ETH_ALEN : 0)
		 /* Target External Address */ +
		 sizeof(struct ieee80211_mesh_hwmp_prep_bottom);
	return elen == needed;
}

/* IEEE Std 802.11-2016 9.4.2.115 PERR element */
static inline bool ieee80211_mesh_perr_size_ok(const u8 *pos, u8 elen)
{
	struct ieee80211_mesh_hwmp_perr *perr_elem = (void *)pos;
	const u8 *start = pos;
	u8 number_of_dst;
	int needed;
	int i;

	needed = sizeof(struct ieee80211_mesh_hwmp_perr);

	/* Check if the element contains number of dst */
	if (elen < needed)
		return false;

	pos += sizeof(struct ieee80211_mesh_hwmp_perr);
	number_of_dst = perr_elem->number_of_dst;

	for (i = 0; i < number_of_dst; i++) {
		struct ieee80211_mesh_hwmp_perr_dst *dst = (void *)pos;
		u8 dst_len = sizeof(struct ieee80211_mesh_hwmp_perr_dst);

		/* Check if the element contains flags */
		if (elen < pos - start + dst_len)
			return false;

		dst_len += ((dst->flags & AE_F) ? ETH_ALEN : 0)
			  /* Destination External Address */ +
			  2 /* Reason Code */;
		needed += dst_len;
		pos += dst_len;
	}

	return elen == needed;
}

#endif /* LINUX_IEEE80211_MESH_H */
