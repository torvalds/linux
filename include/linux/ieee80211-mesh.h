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

/* Mesh flags */
#define MESH_FLAGS_AE_A4 	0x1
#define MESH_FLAGS_AE_A5_A6	0x2
#define MESH_FLAGS_AE		0x3
#define MESH_FLAGS_PS_DEEP	0x4

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

#endif /* LINUX_IEEE80211_MESH_H */
