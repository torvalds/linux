// SPDX-License-Identifier: GPL-2.0-only
/*
 * This is the new netlink-based wireless configuration interface.
 *
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2013-2014  Intel Mobile Communications GmbH
 * Copyright 2015-2017	Intel Deutschland GmbH
 * Copyright (C) 2018-2019 Intel Corporation
 */

#include <linux/if.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/if_ether.h>
#include <linux/ieee80211.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <linux/netlink.h>
#include <linux/nospec.h>
#include <linux/etherdevice.h>
#include <net/net_namespace.h>
#include <net/genetlink.h>
#include <net/cfg80211.h>
#include <net/sock.h>
#include <net/inet_connection_sock.h>
#include "core.h"
#include "nl80211.h"
#include "reg.h"
#include "rdev-ops.h"

static int nl80211_crypto_settings(struct cfg80211_registered_device *rdev,
				   struct genl_info *info,
				   struct cfg80211_crypto_settings *settings,
				   int cipher_limit);

/* the netlink family */
static struct genl_family nl80211_fam;

/* multicast groups */
enum nl80211_multicast_groups {
	NL80211_MCGRP_CONFIG,
	NL80211_MCGRP_SCAN,
	NL80211_MCGRP_REGULATORY,
	NL80211_MCGRP_MLME,
	NL80211_MCGRP_VENDOR,
	NL80211_MCGRP_NAN,
	NL80211_MCGRP_TESTMODE /* keep last - ifdef! */
};

static const struct genl_multicast_group nl80211_mcgrps[] = {
	[NL80211_MCGRP_CONFIG] = { .name = NL80211_MULTICAST_GROUP_CONFIG },
	[NL80211_MCGRP_SCAN] = { .name = NL80211_MULTICAST_GROUP_SCAN },
	[NL80211_MCGRP_REGULATORY] = { .name = NL80211_MULTICAST_GROUP_REG },
	[NL80211_MCGRP_MLME] = { .name = NL80211_MULTICAST_GROUP_MLME },
	[NL80211_MCGRP_VENDOR] = { .name = NL80211_MULTICAST_GROUP_VENDOR },
	[NL80211_MCGRP_NAN] = { .name = NL80211_MULTICAST_GROUP_NAN },
#ifdef CONFIG_NL80211_TESTMODE
	[NL80211_MCGRP_TESTMODE] = { .name = NL80211_MULTICAST_GROUP_TESTMODE }
#endif
};

/* returns ERR_PTR values */
static struct wireless_dev *
__cfg80211_wdev_from_attrs(struct net *netns, struct nlattr **attrs)
{
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *result = NULL;
	bool have_ifidx = attrs[NL80211_ATTR_IFINDEX];
	bool have_wdev_id = attrs[NL80211_ATTR_WDEV];
	u64 wdev_id;
	int wiphy_idx = -1;
	int ifidx = -1;

	ASSERT_RTNL();

	if (!have_ifidx && !have_wdev_id)
		return ERR_PTR(-EINVAL);

	if (have_ifidx)
		ifidx = nla_get_u32(attrs[NL80211_ATTR_IFINDEX]);
	if (have_wdev_id) {
		wdev_id = nla_get_u64(attrs[NL80211_ATTR_WDEV]);
		wiphy_idx = wdev_id >> 32;
	}

	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		struct wireless_dev *wdev;

		if (wiphy_net(&rdev->wiphy) != netns)
			continue;

		if (have_wdev_id && rdev->wiphy_idx != wiphy_idx)
			continue;

		list_for_each_entry(wdev, &rdev->wiphy.wdev_list, list) {
			if (have_ifidx && wdev->netdev &&
			    wdev->netdev->ifindex == ifidx) {
				result = wdev;
				break;
			}
			if (have_wdev_id && wdev->identifier == (u32)wdev_id) {
				result = wdev;
				break;
			}
		}

		if (result)
			break;
	}

	if (result)
		return result;
	return ERR_PTR(-ENODEV);
}

static struct cfg80211_registered_device *
__cfg80211_rdev_from_attrs(struct net *netns, struct nlattr **attrs)
{
	struct cfg80211_registered_device *rdev = NULL, *tmp;
	struct net_device *netdev;

	ASSERT_RTNL();

	if (!attrs[NL80211_ATTR_WIPHY] &&
	    !attrs[NL80211_ATTR_IFINDEX] &&
	    !attrs[NL80211_ATTR_WDEV])
		return ERR_PTR(-EINVAL);

	if (attrs[NL80211_ATTR_WIPHY])
		rdev = cfg80211_rdev_by_wiphy_idx(
				nla_get_u32(attrs[NL80211_ATTR_WIPHY]));

	if (attrs[NL80211_ATTR_WDEV]) {
		u64 wdev_id = nla_get_u64(attrs[NL80211_ATTR_WDEV]);
		struct wireless_dev *wdev;
		bool found = false;

		tmp = cfg80211_rdev_by_wiphy_idx(wdev_id >> 32);
		if (tmp) {
			/* make sure wdev exists */
			list_for_each_entry(wdev, &tmp->wiphy.wdev_list, list) {
				if (wdev->identifier != (u32)wdev_id)
					continue;
				found = true;
				break;
			}

			if (!found)
				tmp = NULL;

			if (rdev && tmp != rdev)
				return ERR_PTR(-EINVAL);
			rdev = tmp;
		}
	}

	if (attrs[NL80211_ATTR_IFINDEX]) {
		int ifindex = nla_get_u32(attrs[NL80211_ATTR_IFINDEX]);

		netdev = __dev_get_by_index(netns, ifindex);
		if (netdev) {
			if (netdev->ieee80211_ptr)
				tmp = wiphy_to_rdev(
					netdev->ieee80211_ptr->wiphy);
			else
				tmp = NULL;

			/* not wireless device -- return error */
			if (!tmp)
				return ERR_PTR(-EINVAL);

			/* mismatch -- return error */
			if (rdev && tmp != rdev)
				return ERR_PTR(-EINVAL);

			rdev = tmp;
		}
	}

	if (!rdev)
		return ERR_PTR(-ENODEV);

	if (netns != wiphy_net(&rdev->wiphy))
		return ERR_PTR(-ENODEV);

	return rdev;
}

/*
 * This function returns a pointer to the driver
 * that the genl_info item that is passed refers to.
 *
 * The result of this can be a PTR_ERR and hence must
 * be checked with IS_ERR() for errors.
 */
static struct cfg80211_registered_device *
cfg80211_get_dev_from_info(struct net *netns, struct genl_info *info)
{
	return __cfg80211_rdev_from_attrs(netns, info->attrs);
}

static int validate_ie_attr(const struct nlattr *attr,
			    struct netlink_ext_ack *extack)
{
	const u8 *data = nla_data(attr);
	unsigned int len = nla_len(attr);
	const struct element *elem;

	for_each_element(elem, data, len) {
		/* nothing */
	}

	if (for_each_element_completed(elem, data, len))
		return 0;

	NL_SET_ERR_MSG_ATTR(extack, attr, "malformed information elements");
	return -EINVAL;
}

/* policy for the attributes */
static const struct nla_policy
nl80211_ftm_responder_policy[NL80211_FTM_RESP_ATTR_MAX + 1] = {
	[NL80211_FTM_RESP_ATTR_ENABLED] = { .type = NLA_FLAG, },
	[NL80211_FTM_RESP_ATTR_LCI] = { .type = NLA_BINARY,
					.len = U8_MAX },
	[NL80211_FTM_RESP_ATTR_CIVICLOC] = { .type = NLA_BINARY,
					     .len = U8_MAX },
};

static const struct nla_policy
nl80211_pmsr_ftm_req_attr_policy[NL80211_PMSR_FTM_REQ_ATTR_MAX + 1] = {
	[NL80211_PMSR_FTM_REQ_ATTR_ASAP] = { .type = NLA_FLAG },
	[NL80211_PMSR_FTM_REQ_ATTR_PREAMBLE] = { .type = NLA_U32 },
	[NL80211_PMSR_FTM_REQ_ATTR_NUM_BURSTS_EXP] =
		NLA_POLICY_MAX(NLA_U8, 15),
	[NL80211_PMSR_FTM_REQ_ATTR_BURST_PERIOD] = { .type = NLA_U16 },
	[NL80211_PMSR_FTM_REQ_ATTR_BURST_DURATION] =
		NLA_POLICY_MAX(NLA_U8, 15),
	[NL80211_PMSR_FTM_REQ_ATTR_FTMS_PER_BURST] =
		NLA_POLICY_MAX(NLA_U8, 31),
	[NL80211_PMSR_FTM_REQ_ATTR_NUM_FTMR_RETRIES] = { .type = NLA_U8 },
	[NL80211_PMSR_FTM_REQ_ATTR_REQUEST_LCI] = { .type = NLA_FLAG },
	[NL80211_PMSR_FTM_REQ_ATTR_REQUEST_CIVICLOC] = { .type = NLA_FLAG },
};

static const struct nla_policy
nl80211_pmsr_req_data_policy[NL80211_PMSR_TYPE_MAX + 1] = {
	[NL80211_PMSR_TYPE_FTM] =
		NLA_POLICY_NESTED(nl80211_pmsr_ftm_req_attr_policy),
};

static const struct nla_policy
nl80211_pmsr_req_attr_policy[NL80211_PMSR_REQ_ATTR_MAX + 1] = {
	[NL80211_PMSR_REQ_ATTR_DATA] =
		NLA_POLICY_NESTED(nl80211_pmsr_req_data_policy),
	[NL80211_PMSR_REQ_ATTR_GET_AP_TSF] = { .type = NLA_FLAG },
};

static const struct nla_policy
nl80211_psmr_peer_attr_policy[NL80211_PMSR_PEER_ATTR_MAX + 1] = {
	[NL80211_PMSR_PEER_ATTR_ADDR] = NLA_POLICY_ETH_ADDR,
	/*
	 * we could specify this again to be the top-level policy,
	 * but that would open us up to recursion problems ...
	 */
	[NL80211_PMSR_PEER_ATTR_CHAN] = { .type = NLA_NESTED },
	[NL80211_PMSR_PEER_ATTR_REQ] =
		NLA_POLICY_NESTED(nl80211_pmsr_req_attr_policy),
	[NL80211_PMSR_PEER_ATTR_RESP] = { .type = NLA_REJECT },
};

static const struct nla_policy
nl80211_pmsr_attr_policy[NL80211_PMSR_ATTR_MAX + 1] = {
	[NL80211_PMSR_ATTR_MAX_PEERS] = { .type = NLA_REJECT },
	[NL80211_PMSR_ATTR_REPORT_AP_TSF] = { .type = NLA_REJECT },
	[NL80211_PMSR_ATTR_RANDOMIZE_MAC_ADDR] = { .type = NLA_REJECT },
	[NL80211_PMSR_ATTR_TYPE_CAPA] = { .type = NLA_REJECT },
	[NL80211_PMSR_ATTR_PEERS] =
		NLA_POLICY_NESTED_ARRAY(nl80211_psmr_peer_attr_policy),
};

const struct nla_policy nl80211_policy[NUM_NL80211_ATTR] = {
	[NL80211_ATTR_WIPHY] = { .type = NLA_U32 },
	[NL80211_ATTR_WIPHY_NAME] = { .type = NLA_NUL_STRING,
				      .len = 20-1 },
	[NL80211_ATTR_WIPHY_TXQ_PARAMS] = { .type = NLA_NESTED },

	[NL80211_ATTR_WIPHY_FREQ] = { .type = NLA_U32 },
	[NL80211_ATTR_WIPHY_CHANNEL_TYPE] = { .type = NLA_U32 },
	[NL80211_ATTR_CHANNEL_WIDTH] = { .type = NLA_U32 },
	[NL80211_ATTR_CENTER_FREQ1] = { .type = NLA_U32 },
	[NL80211_ATTR_CENTER_FREQ2] = { .type = NLA_U32 },

	[NL80211_ATTR_WIPHY_RETRY_SHORT] = NLA_POLICY_MIN(NLA_U8, 1),
	[NL80211_ATTR_WIPHY_RETRY_LONG] = NLA_POLICY_MIN(NLA_U8, 1),
	[NL80211_ATTR_WIPHY_FRAG_THRESHOLD] = { .type = NLA_U32 },
	[NL80211_ATTR_WIPHY_RTS_THRESHOLD] = { .type = NLA_U32 },
	[NL80211_ATTR_WIPHY_COVERAGE_CLASS] = { .type = NLA_U8 },
	[NL80211_ATTR_WIPHY_DYN_ACK] = { .type = NLA_FLAG },

	[NL80211_ATTR_IFTYPE] = NLA_POLICY_MAX(NLA_U32, NL80211_IFTYPE_MAX),
	[NL80211_ATTR_IFINDEX] = { .type = NLA_U32 },
	[NL80211_ATTR_IFNAME] = { .type = NLA_NUL_STRING, .len = IFNAMSIZ-1 },

	[NL80211_ATTR_MAC] = { .type = NLA_EXACT_LEN_WARN, .len = ETH_ALEN },
	[NL80211_ATTR_PREV_BSSID] = {
		.type = NLA_EXACT_LEN_WARN,
		.len = ETH_ALEN
	},

	[NL80211_ATTR_KEY] = { .type = NLA_NESTED, },
	[NL80211_ATTR_KEY_DATA] = { .type = NLA_BINARY,
				    .len = WLAN_MAX_KEY_LEN },
	[NL80211_ATTR_KEY_IDX] = NLA_POLICY_MAX(NLA_U8, 5),
	[NL80211_ATTR_KEY_CIPHER] = { .type = NLA_U32 },
	[NL80211_ATTR_KEY_DEFAULT] = { .type = NLA_FLAG },
	[NL80211_ATTR_KEY_SEQ] = { .type = NLA_BINARY, .len = 16 },
	[NL80211_ATTR_KEY_TYPE] =
		NLA_POLICY_MAX(NLA_U32, NUM_NL80211_KEYTYPES),

	[NL80211_ATTR_BEACON_INTERVAL] = { .type = NLA_U32 },
	[NL80211_ATTR_DTIM_PERIOD] = { .type = NLA_U32 },
	[NL80211_ATTR_BEACON_HEAD] = { .type = NLA_BINARY,
				       .len = IEEE80211_MAX_DATA_LEN },
	[NL80211_ATTR_BEACON_TAIL] =
		NLA_POLICY_VALIDATE_FN(NLA_BINARY, validate_ie_attr,
				       IEEE80211_MAX_DATA_LEN),
	[NL80211_ATTR_STA_AID] =
		NLA_POLICY_RANGE(NLA_U16, 1, IEEE80211_MAX_AID),
	[NL80211_ATTR_STA_FLAGS] = { .type = NLA_NESTED },
	[NL80211_ATTR_STA_LISTEN_INTERVAL] = { .type = NLA_U16 },
	[NL80211_ATTR_STA_SUPPORTED_RATES] = { .type = NLA_BINARY,
					       .len = NL80211_MAX_SUPP_RATES },
	[NL80211_ATTR_STA_PLINK_ACTION] =
		NLA_POLICY_MAX(NLA_U8, NUM_NL80211_PLINK_ACTIONS - 1),
	[NL80211_ATTR_STA_TX_POWER_SETTING] =
		NLA_POLICY_RANGE(NLA_U8,
				 NL80211_TX_POWER_AUTOMATIC,
				 NL80211_TX_POWER_FIXED),
	[NL80211_ATTR_STA_TX_POWER] = { .type = NLA_S16 },
	[NL80211_ATTR_STA_VLAN] = { .type = NLA_U32 },
	[NL80211_ATTR_MNTR_FLAGS] = { /* NLA_NESTED can't be empty */ },
	[NL80211_ATTR_MESH_ID] = { .type = NLA_BINARY,
				   .len = IEEE80211_MAX_MESH_ID_LEN },
	[NL80211_ATTR_MPATH_NEXT_HOP] = { .type = NLA_U32 },

	[NL80211_ATTR_REG_ALPHA2] = { .type = NLA_STRING, .len = 2 },
	[NL80211_ATTR_REG_RULES] = { .type = NLA_NESTED },

	[NL80211_ATTR_BSS_CTS_PROT] = { .type = NLA_U8 },
	[NL80211_ATTR_BSS_SHORT_PREAMBLE] = { .type = NLA_U8 },
	[NL80211_ATTR_BSS_SHORT_SLOT_TIME] = { .type = NLA_U8 },
	[NL80211_ATTR_BSS_BASIC_RATES] = { .type = NLA_BINARY,
					   .len = NL80211_MAX_SUPP_RATES },
	[NL80211_ATTR_BSS_HT_OPMODE] = { .type = NLA_U16 },

	[NL80211_ATTR_MESH_CONFIG] = { .type = NLA_NESTED },
	[NL80211_ATTR_SUPPORT_MESH_AUTH] = { .type = NLA_FLAG },

	[NL80211_ATTR_HT_CAPABILITY] = {
		.type = NLA_EXACT_LEN_WARN,
		.len = NL80211_HT_CAPABILITY_LEN
	},

	[NL80211_ATTR_MGMT_SUBTYPE] = { .type = NLA_U8 },
	[NL80211_ATTR_IE] = NLA_POLICY_VALIDATE_FN(NLA_BINARY,
						   validate_ie_attr,
						   IEEE80211_MAX_DATA_LEN),
	[NL80211_ATTR_SCAN_FREQUENCIES] = { .type = NLA_NESTED },
	[NL80211_ATTR_SCAN_SSIDS] = { .type = NLA_NESTED },

	[NL80211_ATTR_SSID] = { .type = NLA_BINARY,
				.len = IEEE80211_MAX_SSID_LEN },
	[NL80211_ATTR_AUTH_TYPE] = { .type = NLA_U32 },
	[NL80211_ATTR_REASON_CODE] = { .type = NLA_U16 },
	[NL80211_ATTR_FREQ_FIXED] = { .type = NLA_FLAG },
	[NL80211_ATTR_TIMED_OUT] = { .type = NLA_FLAG },
	[NL80211_ATTR_USE_MFP] = NLA_POLICY_RANGE(NLA_U32,
						  NL80211_MFP_NO,
						  NL80211_MFP_OPTIONAL),
	[NL80211_ATTR_STA_FLAGS2] = {
		.len = sizeof(struct nl80211_sta_flag_update),
	},
	[NL80211_ATTR_CONTROL_PORT] = { .type = NLA_FLAG },
	[NL80211_ATTR_CONTROL_PORT_ETHERTYPE] = { .type = NLA_U16 },
	[NL80211_ATTR_CONTROL_PORT_NO_ENCRYPT] = { .type = NLA_FLAG },
	[NL80211_ATTR_CONTROL_PORT_OVER_NL80211] = { .type = NLA_FLAG },
	[NL80211_ATTR_PRIVACY] = { .type = NLA_FLAG },
	[NL80211_ATTR_CIPHER_SUITE_GROUP] = { .type = NLA_U32 },
	[NL80211_ATTR_WPA_VERSIONS] = { .type = NLA_U32 },
	[NL80211_ATTR_PID] = { .type = NLA_U32 },
	[NL80211_ATTR_4ADDR] = { .type = NLA_U8 },
	[NL80211_ATTR_PMKID] = {
		.type = NLA_EXACT_LEN_WARN,
		.len = WLAN_PMKID_LEN
	},
	[NL80211_ATTR_DURATION] = { .type = NLA_U32 },
	[NL80211_ATTR_COOKIE] = { .type = NLA_U64 },
	[NL80211_ATTR_TX_RATES] = { .type = NLA_NESTED },
	[NL80211_ATTR_FRAME] = { .type = NLA_BINARY,
				 .len = IEEE80211_MAX_DATA_LEN },
	[NL80211_ATTR_FRAME_MATCH] = { .type = NLA_BINARY, },
	[NL80211_ATTR_PS_STATE] = NLA_POLICY_RANGE(NLA_U32,
						   NL80211_PS_DISABLED,
						   NL80211_PS_ENABLED),
	[NL80211_ATTR_CQM] = { .type = NLA_NESTED, },
	[NL80211_ATTR_LOCAL_STATE_CHANGE] = { .type = NLA_FLAG },
	[NL80211_ATTR_AP_ISOLATE] = { .type = NLA_U8 },
	[NL80211_ATTR_WIPHY_TX_POWER_SETTING] = { .type = NLA_U32 },
	[NL80211_ATTR_WIPHY_TX_POWER_LEVEL] = { .type = NLA_U32 },
	[NL80211_ATTR_FRAME_TYPE] = { .type = NLA_U16 },
	[NL80211_ATTR_WIPHY_ANTENNA_TX] = { .type = NLA_U32 },
	[NL80211_ATTR_WIPHY_ANTENNA_RX] = { .type = NLA_U32 },
	[NL80211_ATTR_MCAST_RATE] = { .type = NLA_U32 },
	[NL80211_ATTR_OFFCHANNEL_TX_OK] = { .type = NLA_FLAG },
	[NL80211_ATTR_KEY_DEFAULT_TYPES] = { .type = NLA_NESTED },
	[NL80211_ATTR_WOWLAN_TRIGGERS] = { .type = NLA_NESTED },
	[NL80211_ATTR_STA_PLINK_STATE] =
		NLA_POLICY_MAX(NLA_U8, NUM_NL80211_PLINK_STATES - 1),
	[NL80211_ATTR_MESH_PEER_AID] =
		NLA_POLICY_RANGE(NLA_U16, 1, IEEE80211_MAX_AID),
	[NL80211_ATTR_SCHED_SCAN_INTERVAL] = { .type = NLA_U32 },
	[NL80211_ATTR_REKEY_DATA] = { .type = NLA_NESTED },
	[NL80211_ATTR_SCAN_SUPP_RATES] = { .type = NLA_NESTED },
	[NL80211_ATTR_HIDDEN_SSID] =
		NLA_POLICY_RANGE(NLA_U32,
				 NL80211_HIDDEN_SSID_NOT_IN_USE,
				 NL80211_HIDDEN_SSID_ZERO_CONTENTS),
	[NL80211_ATTR_IE_PROBE_RESP] =
		NLA_POLICY_VALIDATE_FN(NLA_BINARY, validate_ie_attr,
				       IEEE80211_MAX_DATA_LEN),
	[NL80211_ATTR_IE_ASSOC_RESP] =
		NLA_POLICY_VALIDATE_FN(NLA_BINARY, validate_ie_attr,
				       IEEE80211_MAX_DATA_LEN),
	[NL80211_ATTR_ROAM_SUPPORT] = { .type = NLA_FLAG },
	[NL80211_ATTR_SCHED_SCAN_MATCH] = { .type = NLA_NESTED },
	[NL80211_ATTR_TX_NO_CCK_RATE] = { .type = NLA_FLAG },
	[NL80211_ATTR_TDLS_ACTION] = { .type = NLA_U8 },
	[NL80211_ATTR_TDLS_DIALOG_TOKEN] = { .type = NLA_U8 },
	[NL80211_ATTR_TDLS_OPERATION] = { .type = NLA_U8 },
	[NL80211_ATTR_TDLS_SUPPORT] = { .type = NLA_FLAG },
	[NL80211_ATTR_TDLS_EXTERNAL_SETUP] = { .type = NLA_FLAG },
	[NL80211_ATTR_TDLS_INITIATOR] = { .type = NLA_FLAG },
	[NL80211_ATTR_DONT_WAIT_FOR_ACK] = { .type = NLA_FLAG },
	[NL80211_ATTR_PROBE_RESP] = { .type = NLA_BINARY,
				      .len = IEEE80211_MAX_DATA_LEN },
	[NL80211_ATTR_DFS_REGION] = { .type = NLA_U8 },
	[NL80211_ATTR_DISABLE_HT] = { .type = NLA_FLAG },
	[NL80211_ATTR_HT_CAPABILITY_MASK] = {
		.len = NL80211_HT_CAPABILITY_LEN
	},
	[NL80211_ATTR_NOACK_MAP] = { .type = NLA_U16 },
	[NL80211_ATTR_INACTIVITY_TIMEOUT] = { .type = NLA_U16 },
	[NL80211_ATTR_BG_SCAN_PERIOD] = { .type = NLA_U16 },
	[NL80211_ATTR_WDEV] = { .type = NLA_U64 },
	[NL80211_ATTR_USER_REG_HINT_TYPE] = { .type = NLA_U32 },
	[NL80211_ATTR_AUTH_DATA] = { .type = NLA_BINARY, },
	[NL80211_ATTR_VHT_CAPABILITY] = {
		.type = NLA_EXACT_LEN_WARN,
		.len = NL80211_VHT_CAPABILITY_LEN
	},
	[NL80211_ATTR_SCAN_FLAGS] = { .type = NLA_U32 },
	[NL80211_ATTR_P2P_CTWINDOW] = NLA_POLICY_MAX(NLA_U8, 127),
	[NL80211_ATTR_P2P_OPPPS] = NLA_POLICY_MAX(NLA_U8, 1),
	[NL80211_ATTR_LOCAL_MESH_POWER_MODE] =
		NLA_POLICY_RANGE(NLA_U32,
				 NL80211_MESH_POWER_UNKNOWN + 1,
				 NL80211_MESH_POWER_MAX),
	[NL80211_ATTR_ACL_POLICY] = {. type = NLA_U32 },
	[NL80211_ATTR_MAC_ADDRS] = { .type = NLA_NESTED },
	[NL80211_ATTR_STA_CAPABILITY] = { .type = NLA_U16 },
	[NL80211_ATTR_STA_EXT_CAPABILITY] = { .type = NLA_BINARY, },
	[NL80211_ATTR_SPLIT_WIPHY_DUMP] = { .type = NLA_FLAG, },
	[NL80211_ATTR_DISABLE_VHT] = { .type = NLA_FLAG },
	[NL80211_ATTR_VHT_CAPABILITY_MASK] = {
		.len = NL80211_VHT_CAPABILITY_LEN,
	},
	[NL80211_ATTR_MDID] = { .type = NLA_U16 },
	[NL80211_ATTR_IE_RIC] = { .type = NLA_BINARY,
				  .len = IEEE80211_MAX_DATA_LEN },
	[NL80211_ATTR_PEER_AID] =
		NLA_POLICY_RANGE(NLA_U16, 1, IEEE80211_MAX_AID),
	[NL80211_ATTR_CH_SWITCH_COUNT] = { .type = NLA_U32 },
	[NL80211_ATTR_CH_SWITCH_BLOCK_TX] = { .type = NLA_FLAG },
	[NL80211_ATTR_CSA_IES] = { .type = NLA_NESTED },
	[NL80211_ATTR_CSA_C_OFF_BEACON] = { .type = NLA_BINARY },
	[NL80211_ATTR_CSA_C_OFF_PRESP] = { .type = NLA_BINARY },
	[NL80211_ATTR_STA_SUPPORTED_CHANNELS] = { .type = NLA_BINARY },
	[NL80211_ATTR_STA_SUPPORTED_OPER_CLASSES] = { .type = NLA_BINARY },
	[NL80211_ATTR_HANDLE_DFS] = { .type = NLA_FLAG },
	[NL80211_ATTR_OPMODE_NOTIF] = { .type = NLA_U8 },
	[NL80211_ATTR_VENDOR_ID] = { .type = NLA_U32 },
	[NL80211_ATTR_VENDOR_SUBCMD] = { .type = NLA_U32 },
	[NL80211_ATTR_VENDOR_DATA] = { .type = NLA_BINARY },
	[NL80211_ATTR_QOS_MAP] = { .type = NLA_BINARY,
				   .len = IEEE80211_QOS_MAP_LEN_MAX },
	[NL80211_ATTR_MAC_HINT] = {
		.type = NLA_EXACT_LEN_WARN,
		.len = ETH_ALEN
	},
	[NL80211_ATTR_WIPHY_FREQ_HINT] = { .type = NLA_U32 },
	[NL80211_ATTR_TDLS_PEER_CAPABILITY] = { .type = NLA_U32 },
	[NL80211_ATTR_SOCKET_OWNER] = { .type = NLA_FLAG },
	[NL80211_ATTR_CSA_C_OFFSETS_TX] = { .type = NLA_BINARY },
	[NL80211_ATTR_USE_RRM] = { .type = NLA_FLAG },
	[NL80211_ATTR_TSID] = NLA_POLICY_MAX(NLA_U8, IEEE80211_NUM_TIDS - 1),
	[NL80211_ATTR_USER_PRIO] =
		NLA_POLICY_MAX(NLA_U8, IEEE80211_NUM_UPS - 1),
	[NL80211_ATTR_ADMITTED_TIME] = { .type = NLA_U16 },
	[NL80211_ATTR_SMPS_MODE] = { .type = NLA_U8 },
	[NL80211_ATTR_MAC_MASK] = {
		.type = NLA_EXACT_LEN_WARN,
		.len = ETH_ALEN
	},
	[NL80211_ATTR_WIPHY_SELF_MANAGED_REG] = { .type = NLA_FLAG },
	[NL80211_ATTR_NETNS_FD] = { .type = NLA_U32 },
	[NL80211_ATTR_SCHED_SCAN_DELAY] = { .type = NLA_U32 },
	[NL80211_ATTR_REG_INDOOR] = { .type = NLA_FLAG },
	[NL80211_ATTR_PBSS] = { .type = NLA_FLAG },
	[NL80211_ATTR_BSS_SELECT] = { .type = NLA_NESTED },
	[NL80211_ATTR_STA_SUPPORT_P2P_PS] =
		NLA_POLICY_MAX(NLA_U8, NUM_NL80211_P2P_PS_STATUS - 1),
	[NL80211_ATTR_MU_MIMO_GROUP_DATA] = {
		.len = VHT_MUMIMO_GROUPS_DATA_LEN
	},
	[NL80211_ATTR_MU_MIMO_FOLLOW_MAC_ADDR] = {
		.type = NLA_EXACT_LEN_WARN,
		.len = ETH_ALEN
	},
	[NL80211_ATTR_NAN_MASTER_PREF] = NLA_POLICY_MIN(NLA_U8, 1),
	[NL80211_ATTR_BANDS] = { .type = NLA_U32 },
	[NL80211_ATTR_NAN_FUNC] = { .type = NLA_NESTED },
	[NL80211_ATTR_FILS_KEK] = { .type = NLA_BINARY,
				    .len = FILS_MAX_KEK_LEN },
	[NL80211_ATTR_FILS_NONCES] = {
		.type = NLA_EXACT_LEN_WARN,
		.len = 2 * FILS_NONCE_LEN
	},
	[NL80211_ATTR_MULTICAST_TO_UNICAST_ENABLED] = { .type = NLA_FLAG, },
	[NL80211_ATTR_BSSID] = { .type = NLA_EXACT_LEN_WARN, .len = ETH_ALEN },
	[NL80211_ATTR_SCHED_SCAN_RELATIVE_RSSI] = { .type = NLA_S8 },
	[NL80211_ATTR_SCHED_SCAN_RSSI_ADJUST] = {
		.len = sizeof(struct nl80211_bss_select_rssi_adjust)
	},
	[NL80211_ATTR_TIMEOUT_REASON] = { .type = NLA_U32 },
	[NL80211_ATTR_FILS_ERP_USERNAME] = { .type = NLA_BINARY,
					     .len = FILS_ERP_MAX_USERNAME_LEN },
	[NL80211_ATTR_FILS_ERP_REALM] = { .type = NLA_BINARY,
					  .len = FILS_ERP_MAX_REALM_LEN },
	[NL80211_ATTR_FILS_ERP_NEXT_SEQ_NUM] = { .type = NLA_U16 },
	[NL80211_ATTR_FILS_ERP_RRK] = { .type = NLA_BINARY,
					.len = FILS_ERP_MAX_RRK_LEN },
	[NL80211_ATTR_FILS_CACHE_ID] = { .type = NLA_EXACT_LEN_WARN, .len = 2 },
	[NL80211_ATTR_PMK] = { .type = NLA_BINARY, .len = PMK_MAX_LEN },
	[NL80211_ATTR_SCHED_SCAN_MULTI] = { .type = NLA_FLAG },
	[NL80211_ATTR_EXTERNAL_AUTH_SUPPORT] = { .type = NLA_FLAG },

	[NL80211_ATTR_TXQ_LIMIT] = { .type = NLA_U32 },
	[NL80211_ATTR_TXQ_MEMORY_LIMIT] = { .type = NLA_U32 },
	[NL80211_ATTR_TXQ_QUANTUM] = { .type = NLA_U32 },
	[NL80211_ATTR_HE_CAPABILITY] = { .type = NLA_BINARY,
					 .len = NL80211_HE_MAX_CAPABILITY_LEN },

	[NL80211_ATTR_FTM_RESPONDER] = {
		.type = NLA_NESTED,
		.validation_data = nl80211_ftm_responder_policy,
	},
	[NL80211_ATTR_TIMEOUT] = NLA_POLICY_MIN(NLA_U32, 1),
	[NL80211_ATTR_PEER_MEASUREMENTS] =
		NLA_POLICY_NESTED(nl80211_pmsr_attr_policy),
	[NL80211_ATTR_AIRTIME_WEIGHT] = NLA_POLICY_MIN(NLA_U16, 1),
};

/* policy for the key attributes */
static const struct nla_policy nl80211_key_policy[NL80211_KEY_MAX + 1] = {
	[NL80211_KEY_DATA] = { .type = NLA_BINARY, .len = WLAN_MAX_KEY_LEN },
	[NL80211_KEY_IDX] = { .type = NLA_U8 },
	[NL80211_KEY_CIPHER] = { .type = NLA_U32 },
	[NL80211_KEY_SEQ] = { .type = NLA_BINARY, .len = 16 },
	[NL80211_KEY_DEFAULT] = { .type = NLA_FLAG },
	[NL80211_KEY_DEFAULT_MGMT] = { .type = NLA_FLAG },
	[NL80211_KEY_TYPE] = NLA_POLICY_MAX(NLA_U32, NUM_NL80211_KEYTYPES - 1),
	[NL80211_KEY_DEFAULT_TYPES] = { .type = NLA_NESTED },
	[NL80211_KEY_MODE] = NLA_POLICY_RANGE(NLA_U8, 0, NL80211_KEY_SET_TX),
};

/* policy for the key default flags */
static const struct nla_policy
nl80211_key_default_policy[NUM_NL80211_KEY_DEFAULT_TYPES] = {
	[NL80211_KEY_DEFAULT_TYPE_UNICAST] = { .type = NLA_FLAG },
	[NL80211_KEY_DEFAULT_TYPE_MULTICAST] = { .type = NLA_FLAG },
};

#ifdef CONFIG_PM
/* policy for WoWLAN attributes */
static const struct nla_policy
nl80211_wowlan_policy[NUM_NL80211_WOWLAN_TRIG] = {
	[NL80211_WOWLAN_TRIG_ANY] = { .type = NLA_FLAG },
	[NL80211_WOWLAN_TRIG_DISCONNECT] = { .type = NLA_FLAG },
	[NL80211_WOWLAN_TRIG_MAGIC_PKT] = { .type = NLA_FLAG },
	[NL80211_WOWLAN_TRIG_PKT_PATTERN] = { .type = NLA_NESTED },
	[NL80211_WOWLAN_TRIG_GTK_REKEY_FAILURE] = { .type = NLA_FLAG },
	[NL80211_WOWLAN_TRIG_EAP_IDENT_REQUEST] = { .type = NLA_FLAG },
	[NL80211_WOWLAN_TRIG_4WAY_HANDSHAKE] = { .type = NLA_FLAG },
	[NL80211_WOWLAN_TRIG_RFKILL_RELEASE] = { .type = NLA_FLAG },
	[NL80211_WOWLAN_TRIG_TCP_CONNECTION] = { .type = NLA_NESTED },
	[NL80211_WOWLAN_TRIG_NET_DETECT] = { .type = NLA_NESTED },
};

static const struct nla_policy
nl80211_wowlan_tcp_policy[NUM_NL80211_WOWLAN_TCP] = {
	[NL80211_WOWLAN_TCP_SRC_IPV4] = { .type = NLA_U32 },
	[NL80211_WOWLAN_TCP_DST_IPV4] = { .type = NLA_U32 },
	[NL80211_WOWLAN_TCP_DST_MAC] = {
		.type = NLA_EXACT_LEN_WARN,
		.len = ETH_ALEN
	},
	[NL80211_WOWLAN_TCP_SRC_PORT] = { .type = NLA_U16 },
	[NL80211_WOWLAN_TCP_DST_PORT] = { .type = NLA_U16 },
	[NL80211_WOWLAN_TCP_DATA_PAYLOAD] = { .type = NLA_MIN_LEN, .len = 1 },
	[NL80211_WOWLAN_TCP_DATA_PAYLOAD_SEQ] = {
		.len = sizeof(struct nl80211_wowlan_tcp_data_seq)
	},
	[NL80211_WOWLAN_TCP_DATA_PAYLOAD_TOKEN] = {
		.len = sizeof(struct nl80211_wowlan_tcp_data_token)
	},
	[NL80211_WOWLAN_TCP_DATA_INTERVAL] = { .type = NLA_U32 },
	[NL80211_WOWLAN_TCP_WAKE_PAYLOAD] = { .type = NLA_MIN_LEN, .len = 1 },
	[NL80211_WOWLAN_TCP_WAKE_MASK] = { .type = NLA_MIN_LEN, .len = 1 },
};
#endif /* CONFIG_PM */

/* policy for coalesce rule attributes */
static const struct nla_policy
nl80211_coalesce_policy[NUM_NL80211_ATTR_COALESCE_RULE] = {
	[NL80211_ATTR_COALESCE_RULE_DELAY] = { .type = NLA_U32 },
	[NL80211_ATTR_COALESCE_RULE_CONDITION] =
		NLA_POLICY_RANGE(NLA_U32,
				 NL80211_COALESCE_CONDITION_MATCH,
				 NL80211_COALESCE_CONDITION_NO_MATCH),
	[NL80211_ATTR_COALESCE_RULE_PKT_PATTERN] = { .type = NLA_NESTED },
};

/* policy for GTK rekey offload attributes */
static const struct nla_policy
nl80211_rekey_policy[NUM_NL80211_REKEY_DATA] = {
	[NL80211_REKEY_DATA_KEK] = {
		.type = NLA_EXACT_LEN_WARN,
		.len = NL80211_KEK_LEN,
	},
	[NL80211_REKEY_DATA_KCK] = {
		.type = NLA_EXACT_LEN_WARN,
		.len = NL80211_KCK_LEN,
	},
	[NL80211_REKEY_DATA_REPLAY_CTR] = {
		.type = NLA_EXACT_LEN_WARN,
		.len = NL80211_REPLAY_CTR_LEN
	},
};

static const struct nla_policy
nl80211_match_band_rssi_policy[NUM_NL80211_BANDS] = {
	[NL80211_BAND_2GHZ] = { .type = NLA_S32 },
	[NL80211_BAND_5GHZ] = { .type = NLA_S32 },
	[NL80211_BAND_60GHZ] = { .type = NLA_S32 },
};

static const struct nla_policy
nl80211_match_policy[NL80211_SCHED_SCAN_MATCH_ATTR_MAX + 1] = {
	[NL80211_SCHED_SCAN_MATCH_ATTR_SSID] = { .type = NLA_BINARY,
						 .len = IEEE80211_MAX_SSID_LEN },
	[NL80211_SCHED_SCAN_MATCH_ATTR_BSSID] = {
		.type = NLA_EXACT_LEN_WARN,
		.len = ETH_ALEN
	},
	[NL80211_SCHED_SCAN_MATCH_ATTR_RSSI] = { .type = NLA_U32 },
	[NL80211_SCHED_SCAN_MATCH_PER_BAND_RSSI] =
		NLA_POLICY_NESTED(nl80211_match_band_rssi_policy),
};

static const struct nla_policy
nl80211_plan_policy[NL80211_SCHED_SCAN_PLAN_MAX + 1] = {
	[NL80211_SCHED_SCAN_PLAN_INTERVAL] = { .type = NLA_U32 },
	[NL80211_SCHED_SCAN_PLAN_ITERATIONS] = { .type = NLA_U32 },
};

static const struct nla_policy
nl80211_bss_select_policy[NL80211_BSS_SELECT_ATTR_MAX + 1] = {
	[NL80211_BSS_SELECT_ATTR_RSSI] = { .type = NLA_FLAG },
	[NL80211_BSS_SELECT_ATTR_BAND_PREF] = { .type = NLA_U32 },
	[NL80211_BSS_SELECT_ATTR_RSSI_ADJUST] = {
		.len = sizeof(struct nl80211_bss_select_rssi_adjust)
	},
};

/* policy for NAN function attributes */
static const struct nla_policy
nl80211_nan_func_policy[NL80211_NAN_FUNC_ATTR_MAX + 1] = {
	[NL80211_NAN_FUNC_TYPE] = { .type = NLA_U8 },
	[NL80211_NAN_FUNC_SERVICE_ID] = {
				    .len = NL80211_NAN_FUNC_SERVICE_ID_LEN },
	[NL80211_NAN_FUNC_PUBLISH_TYPE] = { .type = NLA_U8 },
	[NL80211_NAN_FUNC_PUBLISH_BCAST] = { .type = NLA_FLAG },
	[NL80211_NAN_FUNC_SUBSCRIBE_ACTIVE] = { .type = NLA_FLAG },
	[NL80211_NAN_FUNC_FOLLOW_UP_ID] = { .type = NLA_U8 },
	[NL80211_NAN_FUNC_FOLLOW_UP_REQ_ID] = { .type = NLA_U8 },
	[NL80211_NAN_FUNC_FOLLOW_UP_DEST] = {
		.type = NLA_EXACT_LEN_WARN,
		.len = ETH_ALEN
	},
	[NL80211_NAN_FUNC_CLOSE_RANGE] = { .type = NLA_FLAG },
	[NL80211_NAN_FUNC_TTL] = { .type = NLA_U32 },
	[NL80211_NAN_FUNC_SERVICE_INFO] = { .type = NLA_BINARY,
			.len = NL80211_NAN_FUNC_SERVICE_SPEC_INFO_MAX_LEN },
	[NL80211_NAN_FUNC_SRF] = { .type = NLA_NESTED },
	[NL80211_NAN_FUNC_RX_MATCH_FILTER] = { .type = NLA_NESTED },
	[NL80211_NAN_FUNC_TX_MATCH_FILTER] = { .type = NLA_NESTED },
	[NL80211_NAN_FUNC_INSTANCE_ID] = { .type = NLA_U8 },
	[NL80211_NAN_FUNC_TERM_REASON] = { .type = NLA_U8 },
};

/* policy for Service Response Filter attributes */
static const struct nla_policy
nl80211_nan_srf_policy[NL80211_NAN_SRF_ATTR_MAX + 1] = {
	[NL80211_NAN_SRF_INCLUDE] = { .type = NLA_FLAG },
	[NL80211_NAN_SRF_BF] = { .type = NLA_BINARY,
				 .len =  NL80211_NAN_FUNC_SRF_MAX_LEN },
	[NL80211_NAN_SRF_BF_IDX] = { .type = NLA_U8 },
	[NL80211_NAN_SRF_MAC_ADDRS] = { .type = NLA_NESTED },
};

/* policy for packet pattern attributes */
static const struct nla_policy
nl80211_packet_pattern_policy[MAX_NL80211_PKTPAT + 1] = {
	[NL80211_PKTPAT_MASK] = { .type = NLA_BINARY, },
	[NL80211_PKTPAT_PATTERN] = { .type = NLA_BINARY, },
	[NL80211_PKTPAT_OFFSET] = { .type = NLA_U32 },
};

int nl80211_prepare_wdev_dump(struct netlink_callback *cb,
			      struct cfg80211_registered_device **rdev,
			      struct wireless_dev **wdev)
{
	int err;

	if (!cb->args[0]) {
		err = nlmsg_parse_deprecated(cb->nlh,
					     GENL_HDRLEN + nl80211_fam.hdrsize,
					     genl_family_attrbuf(&nl80211_fam),
					     nl80211_fam.maxattr,
					     nl80211_policy, NULL);
		if (err)
			return err;

		*wdev = __cfg80211_wdev_from_attrs(
					sock_net(cb->skb->sk),
					genl_family_attrbuf(&nl80211_fam));
		if (IS_ERR(*wdev))
			return PTR_ERR(*wdev);
		*rdev = wiphy_to_rdev((*wdev)->wiphy);
		/* 0 is the first index - add 1 to parse only once */
		cb->args[0] = (*rdev)->wiphy_idx + 1;
		cb->args[1] = (*wdev)->identifier;
	} else {
		/* subtract the 1 again here */
		struct wiphy *wiphy = wiphy_idx_to_wiphy(cb->args[0] - 1);
		struct wireless_dev *tmp;

		if (!wiphy)
			return -ENODEV;
		*rdev = wiphy_to_rdev(wiphy);
		*wdev = NULL;

		list_for_each_entry(tmp, &(*rdev)->wiphy.wdev_list, list) {
			if (tmp->identifier == cb->args[1]) {
				*wdev = tmp;
				break;
			}
		}

		if (!*wdev)
			return -ENODEV;
	}

	return 0;
}

/* message building helper */
void *nl80211hdr_put(struct sk_buff *skb, u32 portid, u32 seq,
		     int flags, u8 cmd)
{
	/* since there is no private header just add the generic one */
	return genlmsg_put(skb, portid, seq, &nl80211_fam, flags, cmd);
}

static int nl80211_msg_put_wmm_rules(struct sk_buff *msg,
				     const struct ieee80211_reg_rule *rule)
{
	int j;
	struct nlattr *nl_wmm_rules =
		nla_nest_start_noflag(msg, NL80211_FREQUENCY_ATTR_WMM);

	if (!nl_wmm_rules)
		goto nla_put_failure;

	for (j = 0; j < IEEE80211_NUM_ACS; j++) {
		struct nlattr *nl_wmm_rule = nla_nest_start_noflag(msg, j);

		if (!nl_wmm_rule)
			goto nla_put_failure;

		if (nla_put_u16(msg, NL80211_WMMR_CW_MIN,
				rule->wmm_rule.client[j].cw_min) ||
		    nla_put_u16(msg, NL80211_WMMR_CW_MAX,
				rule->wmm_rule.client[j].cw_max) ||
		    nla_put_u8(msg, NL80211_WMMR_AIFSN,
			       rule->wmm_rule.client[j].aifsn) ||
		    nla_put_u16(msg, NL80211_WMMR_TXOP,
			        rule->wmm_rule.client[j].cot))
			goto nla_put_failure;

		nla_nest_end(msg, nl_wmm_rule);
	}
	nla_nest_end(msg, nl_wmm_rules);

	return 0;

nla_put_failure:
	return -ENOBUFS;
}

static int nl80211_msg_put_channel(struct sk_buff *msg, struct wiphy *wiphy,
				   struct ieee80211_channel *chan,
				   bool large)
{
	/* Some channels must be completely excluded from the
	 * list to protect old user-space tools from breaking
	 */
	if (!large && chan->flags &
	    (IEEE80211_CHAN_NO_10MHZ | IEEE80211_CHAN_NO_20MHZ))
		return 0;

	if (nla_put_u32(msg, NL80211_FREQUENCY_ATTR_FREQ,
			chan->center_freq))
		goto nla_put_failure;

	if ((chan->flags & IEEE80211_CHAN_DISABLED) &&
	    nla_put_flag(msg, NL80211_FREQUENCY_ATTR_DISABLED))
		goto nla_put_failure;
	if (chan->flags & IEEE80211_CHAN_NO_IR) {
		if (nla_put_flag(msg, NL80211_FREQUENCY_ATTR_NO_IR))
			goto nla_put_failure;
		if (nla_put_flag(msg, __NL80211_FREQUENCY_ATTR_NO_IBSS))
			goto nla_put_failure;
	}
	if (chan->flags & IEEE80211_CHAN_RADAR) {
		if (nla_put_flag(msg, NL80211_FREQUENCY_ATTR_RADAR))
			goto nla_put_failure;
		if (large) {
			u32 time;

			time = elapsed_jiffies_msecs(chan->dfs_state_entered);

			if (nla_put_u32(msg, NL80211_FREQUENCY_ATTR_DFS_STATE,
					chan->dfs_state))
				goto nla_put_failure;
			if (nla_put_u32(msg, NL80211_FREQUENCY_ATTR_DFS_TIME,
					time))
				goto nla_put_failure;
			if (nla_put_u32(msg,
					NL80211_FREQUENCY_ATTR_DFS_CAC_TIME,
					chan->dfs_cac_ms))
				goto nla_put_failure;
		}
	}

	if (large) {
		if ((chan->flags & IEEE80211_CHAN_NO_HT40MINUS) &&
		    nla_put_flag(msg, NL80211_FREQUENCY_ATTR_NO_HT40_MINUS))
			goto nla_put_failure;
		if ((chan->flags & IEEE80211_CHAN_NO_HT40PLUS) &&
		    nla_put_flag(msg, NL80211_FREQUENCY_ATTR_NO_HT40_PLUS))
			goto nla_put_failure;
		if ((chan->flags & IEEE80211_CHAN_NO_80MHZ) &&
		    nla_put_flag(msg, NL80211_FREQUENCY_ATTR_NO_80MHZ))
			goto nla_put_failure;
		if ((chan->flags & IEEE80211_CHAN_NO_160MHZ) &&
		    nla_put_flag(msg, NL80211_FREQUENCY_ATTR_NO_160MHZ))
			goto nla_put_failure;
		if ((chan->flags & IEEE80211_CHAN_INDOOR_ONLY) &&
		    nla_put_flag(msg, NL80211_FREQUENCY_ATTR_INDOOR_ONLY))
			goto nla_put_failure;
		if ((chan->flags & IEEE80211_CHAN_IR_CONCURRENT) &&
		    nla_put_flag(msg, NL80211_FREQUENCY_ATTR_IR_CONCURRENT))
			goto nla_put_failure;
		if ((chan->flags & IEEE80211_CHAN_NO_20MHZ) &&
		    nla_put_flag(msg, NL80211_FREQUENCY_ATTR_NO_20MHZ))
			goto nla_put_failure;
		if ((chan->flags & IEEE80211_CHAN_NO_10MHZ) &&
		    nla_put_flag(msg, NL80211_FREQUENCY_ATTR_NO_10MHZ))
			goto nla_put_failure;
	}

	if (nla_put_u32(msg, NL80211_FREQUENCY_ATTR_MAX_TX_POWER,
			DBM_TO_MBM(chan->max_power)))
		goto nla_put_failure;

	if (large) {
		const struct ieee80211_reg_rule *rule =
			freq_reg_info(wiphy, MHZ_TO_KHZ(chan->center_freq));

		if (!IS_ERR_OR_NULL(rule) && rule->has_wmm) {
			if (nl80211_msg_put_wmm_rules(msg, rule))
				goto nla_put_failure;
		}
	}

	return 0;

 nla_put_failure:
	return -ENOBUFS;
}

static bool nl80211_put_txq_stats(struct sk_buff *msg,
				  struct cfg80211_txq_stats *txqstats,
				  int attrtype)
{
	struct nlattr *txqattr;

#define PUT_TXQVAL_U32(attr, memb) do {					  \
	if (txqstats->filled & BIT(NL80211_TXQ_STATS_ ## attr) &&	  \
	    nla_put_u32(msg, NL80211_TXQ_STATS_ ## attr, txqstats->memb)) \
		return false;						  \
	} while (0)

	txqattr = nla_nest_start_noflag(msg, attrtype);
	if (!txqattr)
		return false;

	PUT_TXQVAL_U32(BACKLOG_BYTES, backlog_bytes);
	PUT_TXQVAL_U32(BACKLOG_PACKETS, backlog_packets);
	PUT_TXQVAL_U32(FLOWS, flows);
	PUT_TXQVAL_U32(DROPS, drops);
	PUT_TXQVAL_U32(ECN_MARKS, ecn_marks);
	PUT_TXQVAL_U32(OVERLIMIT, overlimit);
	PUT_TXQVAL_U32(OVERMEMORY, overmemory);
	PUT_TXQVAL_U32(COLLISIONS, collisions);
	PUT_TXQVAL_U32(TX_BYTES, tx_bytes);
	PUT_TXQVAL_U32(TX_PACKETS, tx_packets);
	PUT_TXQVAL_U32(MAX_FLOWS, max_flows);
	nla_nest_end(msg, txqattr);

#undef PUT_TXQVAL_U32
	return true;
}

/* netlink command implementations */

struct key_parse {
	struct key_params p;
	int idx;
	int type;
	bool def, defmgmt;
	bool def_uni, def_multi;
};

static int nl80211_parse_key_new(struct genl_info *info, struct nlattr *key,
				 struct key_parse *k)
{
	struct nlattr *tb[NL80211_KEY_MAX + 1];
	int err = nla_parse_nested_deprecated(tb, NL80211_KEY_MAX, key,
					      nl80211_key_policy,
					      info->extack);
	if (err)
		return err;

	k->def = !!tb[NL80211_KEY_DEFAULT];
	k->defmgmt = !!tb[NL80211_KEY_DEFAULT_MGMT];

	if (k->def) {
		k->def_uni = true;
		k->def_multi = true;
	}
	if (k->defmgmt)
		k->def_multi = true;

	if (tb[NL80211_KEY_IDX])
		k->idx = nla_get_u8(tb[NL80211_KEY_IDX]);

	if (tb[NL80211_KEY_DATA]) {
		k->p.key = nla_data(tb[NL80211_KEY_DATA]);
		k->p.key_len = nla_len(tb[NL80211_KEY_DATA]);
	}

	if (tb[NL80211_KEY_SEQ]) {
		k->p.seq = nla_data(tb[NL80211_KEY_SEQ]);
		k->p.seq_len = nla_len(tb[NL80211_KEY_SEQ]);
	}

	if (tb[NL80211_KEY_CIPHER])
		k->p.cipher = nla_get_u32(tb[NL80211_KEY_CIPHER]);

	if (tb[NL80211_KEY_TYPE])
		k->type = nla_get_u32(tb[NL80211_KEY_TYPE]);

	if (tb[NL80211_KEY_DEFAULT_TYPES]) {
		struct nlattr *kdt[NUM_NL80211_KEY_DEFAULT_TYPES];

		err = nla_parse_nested_deprecated(kdt,
						  NUM_NL80211_KEY_DEFAULT_TYPES - 1,
						  tb[NL80211_KEY_DEFAULT_TYPES],
						  nl80211_key_default_policy,
						  info->extack);
		if (err)
			return err;

		k->def_uni = kdt[NL80211_KEY_DEFAULT_TYPE_UNICAST];
		k->def_multi = kdt[NL80211_KEY_DEFAULT_TYPE_MULTICAST];
	}

	if (tb[NL80211_KEY_MODE])
		k->p.mode = nla_get_u8(tb[NL80211_KEY_MODE]);

	return 0;
}

static int nl80211_parse_key_old(struct genl_info *info, struct key_parse *k)
{
	if (info->attrs[NL80211_ATTR_KEY_DATA]) {
		k->p.key = nla_data(info->attrs[NL80211_ATTR_KEY_DATA]);
		k->p.key_len = nla_len(info->attrs[NL80211_ATTR_KEY_DATA]);
	}

	if (info->attrs[NL80211_ATTR_KEY_SEQ]) {
		k->p.seq = nla_data(info->attrs[NL80211_ATTR_KEY_SEQ]);
		k->p.seq_len = nla_len(info->attrs[NL80211_ATTR_KEY_SEQ]);
	}

	if (info->attrs[NL80211_ATTR_KEY_IDX])
		k->idx = nla_get_u8(info->attrs[NL80211_ATTR_KEY_IDX]);

	if (info->attrs[NL80211_ATTR_KEY_CIPHER])
		k->p.cipher = nla_get_u32(info->attrs[NL80211_ATTR_KEY_CIPHER]);

	k->def = !!info->attrs[NL80211_ATTR_KEY_DEFAULT];
	k->defmgmt = !!info->attrs[NL80211_ATTR_KEY_DEFAULT_MGMT];

	if (k->def) {
		k->def_uni = true;
		k->def_multi = true;
	}
	if (k->defmgmt)
		k->def_multi = true;

	if (info->attrs[NL80211_ATTR_KEY_TYPE])
		k->type = nla_get_u32(info->attrs[NL80211_ATTR_KEY_TYPE]);

	if (info->attrs[NL80211_ATTR_KEY_DEFAULT_TYPES]) {
		struct nlattr *kdt[NUM_NL80211_KEY_DEFAULT_TYPES];
		int err = nla_parse_nested_deprecated(kdt,
						      NUM_NL80211_KEY_DEFAULT_TYPES - 1,
						      info->attrs[NL80211_ATTR_KEY_DEFAULT_TYPES],
						      nl80211_key_default_policy,
						      info->extack);
		if (err)
			return err;

		k->def_uni = kdt[NL80211_KEY_DEFAULT_TYPE_UNICAST];
		k->def_multi = kdt[NL80211_KEY_DEFAULT_TYPE_MULTICAST];
	}

	return 0;
}

static int nl80211_parse_key(struct genl_info *info, struct key_parse *k)
{
	int err;

	memset(k, 0, sizeof(*k));
	k->idx = -1;
	k->type = -1;

	if (info->attrs[NL80211_ATTR_KEY])
		err = nl80211_parse_key_new(info, info->attrs[NL80211_ATTR_KEY], k);
	else
		err = nl80211_parse_key_old(info, k);

	if (err)
		return err;

	if (k->def && k->defmgmt) {
		GENL_SET_ERR_MSG(info, "key with def && defmgmt is invalid");
		return -EINVAL;
	}

	if (k->defmgmt) {
		if (k->def_uni || !k->def_multi) {
			GENL_SET_ERR_MSG(info, "defmgmt key must be mcast");
			return -EINVAL;
		}
	}

	if (k->idx != -1) {
		if (k->defmgmt) {
			if (k->idx < 4 || k->idx > 5) {
				GENL_SET_ERR_MSG(info,
						 "defmgmt key idx not 4 or 5");
				return -EINVAL;
			}
		} else if (k->def) {
			if (k->idx < 0 || k->idx > 3) {
				GENL_SET_ERR_MSG(info, "def key idx not 0-3");
				return -EINVAL;
			}
		} else {
			if (k->idx < 0 || k->idx > 5) {
				GENL_SET_ERR_MSG(info, "key idx not 0-5");
				return -EINVAL;
			}
		}
	}

	return 0;
}

static struct cfg80211_cached_keys *
nl80211_parse_connkeys(struct cfg80211_registered_device *rdev,
		       struct genl_info *info, bool *no_ht)
{
	struct nlattr *keys = info->attrs[NL80211_ATTR_KEYS];
	struct key_parse parse;
	struct nlattr *key;
	struct cfg80211_cached_keys *result;
	int rem, err, def = 0;
	bool have_key = false;

	nla_for_each_nested(key, keys, rem) {
		have_key = true;
		break;
	}

	if (!have_key)
		return NULL;

	result = kzalloc(sizeof(*result), GFP_KERNEL);
	if (!result)
		return ERR_PTR(-ENOMEM);

	result->def = -1;

	nla_for_each_nested(key, keys, rem) {
		memset(&parse, 0, sizeof(parse));
		parse.idx = -1;

		err = nl80211_parse_key_new(info, key, &parse);
		if (err)
			goto error;
		err = -EINVAL;
		if (!parse.p.key)
			goto error;
		if (parse.idx < 0 || parse.idx > 3) {
			GENL_SET_ERR_MSG(info, "key index out of range [0-3]");
			goto error;
		}
		if (parse.def) {
			if (def) {
				GENL_SET_ERR_MSG(info,
						 "only one key can be default");
				goto error;
			}
			def = 1;
			result->def = parse.idx;
			if (!parse.def_uni || !parse.def_multi)
				goto error;
		} else if (parse.defmgmt)
			goto error;
		err = cfg80211_validate_key_settings(rdev, &parse.p,
						     parse.idx, false, NULL);
		if (err)
			goto error;
		if (parse.p.cipher != WLAN_CIPHER_SUITE_WEP40 &&
		    parse.p.cipher != WLAN_CIPHER_SUITE_WEP104) {
			GENL_SET_ERR_MSG(info, "connect key must be WEP");
			err = -EINVAL;
			goto error;
		}
		result->params[parse.idx].cipher = parse.p.cipher;
		result->params[parse.idx].key_len = parse.p.key_len;
		result->params[parse.idx].key = result->data[parse.idx];
		memcpy(result->data[parse.idx], parse.p.key, parse.p.key_len);

		/* must be WEP key if we got here */
		if (no_ht)
			*no_ht = true;
	}

	if (result->def < 0) {
		err = -EINVAL;
		GENL_SET_ERR_MSG(info, "need a default/TX key");
		goto error;
	}

	return result;
 error:
	kfree(result);
	return ERR_PTR(err);
}

static int nl80211_key_allowed(struct wireless_dev *wdev)
{
	ASSERT_WDEV_LOCK(wdev);

	switch (wdev->iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_MESH_POINT:
		break;
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		if (!wdev->current_bss)
			return -ENOLINK;
		break;
	case NL80211_IFTYPE_UNSPECIFIED:
	case NL80211_IFTYPE_OCB:
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_NAN:
	case NL80211_IFTYPE_P2P_DEVICE:
	case NL80211_IFTYPE_WDS:
	case NUM_NL80211_IFTYPES:
		return -EINVAL;
	}

	return 0;
}

static struct ieee80211_channel *nl80211_get_valid_chan(struct wiphy *wiphy,
							struct nlattr *tb)
{
	struct ieee80211_channel *chan;

	if (tb == NULL)
		return NULL;
	chan = ieee80211_get_channel(wiphy, nla_get_u32(tb));
	if (!chan || chan->flags & IEEE80211_CHAN_DISABLED)
		return NULL;
	return chan;
}

static int nl80211_put_iftypes(struct sk_buff *msg, u32 attr, u16 ifmodes)
{
	struct nlattr *nl_modes = nla_nest_start_noflag(msg, attr);
	int i;

	if (!nl_modes)
		goto nla_put_failure;

	i = 0;
	while (ifmodes) {
		if ((ifmodes & 1) && nla_put_flag(msg, i))
			goto nla_put_failure;
		ifmodes >>= 1;
		i++;
	}

	nla_nest_end(msg, nl_modes);
	return 0;

nla_put_failure:
	return -ENOBUFS;
}

static int nl80211_put_iface_combinations(struct wiphy *wiphy,
					  struct sk_buff *msg,
					  bool large)
{
	struct nlattr *nl_combis;
	int i, j;

	nl_combis = nla_nest_start_noflag(msg,
					  NL80211_ATTR_INTERFACE_COMBINATIONS);
	if (!nl_combis)
		goto nla_put_failure;

	for (i = 0; i < wiphy->n_iface_combinations; i++) {
		const struct ieee80211_iface_combination *c;
		struct nlattr *nl_combi, *nl_limits;

		c = &wiphy->iface_combinations[i];

		nl_combi = nla_nest_start_noflag(msg, i + 1);
		if (!nl_combi)
			goto nla_put_failure;

		nl_limits = nla_nest_start_noflag(msg,
						  NL80211_IFACE_COMB_LIMITS);
		if (!nl_limits)
			goto nla_put_failure;

		for (j = 0; j < c->n_limits; j++) {
			struct nlattr *nl_limit;

			nl_limit = nla_nest_start_noflag(msg, j + 1);
			if (!nl_limit)
				goto nla_put_failure;
			if (nla_put_u32(msg, NL80211_IFACE_LIMIT_MAX,
					c->limits[j].max))
				goto nla_put_failure;
			if (nl80211_put_iftypes(msg, NL80211_IFACE_LIMIT_TYPES,
						c->limits[j].types))
				goto nla_put_failure;
			nla_nest_end(msg, nl_limit);
		}

		nla_nest_end(msg, nl_limits);

		if (c->beacon_int_infra_match &&
		    nla_put_flag(msg, NL80211_IFACE_COMB_STA_AP_BI_MATCH))
			goto nla_put_failure;
		if (nla_put_u32(msg, NL80211_IFACE_COMB_NUM_CHANNELS,
				c->num_different_channels) ||
		    nla_put_u32(msg, NL80211_IFACE_COMB_MAXNUM,
				c->max_interfaces))
			goto nla_put_failure;
		if (large &&
		    (nla_put_u32(msg, NL80211_IFACE_COMB_RADAR_DETECT_WIDTHS,
				c->radar_detect_widths) ||
		     nla_put_u32(msg, NL80211_IFACE_COMB_RADAR_DETECT_REGIONS,
				c->radar_detect_regions)))
			goto nla_put_failure;
		if (c->beacon_int_min_gcd &&
		    nla_put_u32(msg, NL80211_IFACE_COMB_BI_MIN_GCD,
				c->beacon_int_min_gcd))
			goto nla_put_failure;

		nla_nest_end(msg, nl_combi);
	}

	nla_nest_end(msg, nl_combis);

	return 0;
nla_put_failure:
	return -ENOBUFS;
}

#ifdef CONFIG_PM
static int nl80211_send_wowlan_tcp_caps(struct cfg80211_registered_device *rdev,
					struct sk_buff *msg)
{
	const struct wiphy_wowlan_tcp_support *tcp = rdev->wiphy.wowlan->tcp;
	struct nlattr *nl_tcp;

	if (!tcp)
		return 0;

	nl_tcp = nla_nest_start_noflag(msg,
				       NL80211_WOWLAN_TRIG_TCP_CONNECTION);
	if (!nl_tcp)
		return -ENOBUFS;

	if (nla_put_u32(msg, NL80211_WOWLAN_TCP_DATA_PAYLOAD,
			tcp->data_payload_max))
		return -ENOBUFS;

	if (nla_put_u32(msg, NL80211_WOWLAN_TCP_DATA_PAYLOAD,
			tcp->data_payload_max))
		return -ENOBUFS;

	if (tcp->seq && nla_put_flag(msg, NL80211_WOWLAN_TCP_DATA_PAYLOAD_SEQ))
		return -ENOBUFS;

	if (tcp->tok && nla_put(msg, NL80211_WOWLAN_TCP_DATA_PAYLOAD_TOKEN,
				sizeof(*tcp->tok), tcp->tok))
		return -ENOBUFS;

	if (nla_put_u32(msg, NL80211_WOWLAN_TCP_DATA_INTERVAL,
			tcp->data_interval_max))
		return -ENOBUFS;

	if (nla_put_u32(msg, NL80211_WOWLAN_TCP_WAKE_PAYLOAD,
			tcp->wake_payload_max))
		return -ENOBUFS;

	nla_nest_end(msg, nl_tcp);
	return 0;
}

static int nl80211_send_wowlan(struct sk_buff *msg,
			       struct cfg80211_registered_device *rdev,
			       bool large)
{
	struct nlattr *nl_wowlan;

	if (!rdev->wiphy.wowlan)
		return 0;

	nl_wowlan = nla_nest_start_noflag(msg,
					  NL80211_ATTR_WOWLAN_TRIGGERS_SUPPORTED);
	if (!nl_wowlan)
		return -ENOBUFS;

	if (((rdev->wiphy.wowlan->flags & WIPHY_WOWLAN_ANY) &&
	     nla_put_flag(msg, NL80211_WOWLAN_TRIG_ANY)) ||
	    ((rdev->wiphy.wowlan->flags & WIPHY_WOWLAN_DISCONNECT) &&
	     nla_put_flag(msg, NL80211_WOWLAN_TRIG_DISCONNECT)) ||
	    ((rdev->wiphy.wowlan->flags & WIPHY_WOWLAN_MAGIC_PKT) &&
	     nla_put_flag(msg, NL80211_WOWLAN_TRIG_MAGIC_PKT)) ||
	    ((rdev->wiphy.wowlan->flags & WIPHY_WOWLAN_SUPPORTS_GTK_REKEY) &&
	     nla_put_flag(msg, NL80211_WOWLAN_TRIG_GTK_REKEY_SUPPORTED)) ||
	    ((rdev->wiphy.wowlan->flags & WIPHY_WOWLAN_GTK_REKEY_FAILURE) &&
	     nla_put_flag(msg, NL80211_WOWLAN_TRIG_GTK_REKEY_FAILURE)) ||
	    ((rdev->wiphy.wowlan->flags & WIPHY_WOWLAN_EAP_IDENTITY_REQ) &&
	     nla_put_flag(msg, NL80211_WOWLAN_TRIG_EAP_IDENT_REQUEST)) ||
	    ((rdev->wiphy.wowlan->flags & WIPHY_WOWLAN_4WAY_HANDSHAKE) &&
	     nla_put_flag(msg, NL80211_WOWLAN_TRIG_4WAY_HANDSHAKE)) ||
	    ((rdev->wiphy.wowlan->flags & WIPHY_WOWLAN_RFKILL_RELEASE) &&
	     nla_put_flag(msg, NL80211_WOWLAN_TRIG_RFKILL_RELEASE)))
		return -ENOBUFS;

	if (rdev->wiphy.wowlan->n_patterns) {
		struct nl80211_pattern_support pat = {
			.max_patterns = rdev->wiphy.wowlan->n_patterns,
			.min_pattern_len = rdev->wiphy.wowlan->pattern_min_len,
			.max_pattern_len = rdev->wiphy.wowlan->pattern_max_len,
			.max_pkt_offset = rdev->wiphy.wowlan->max_pkt_offset,
		};

		if (nla_put(msg, NL80211_WOWLAN_TRIG_PKT_PATTERN,
			    sizeof(pat), &pat))
			return -ENOBUFS;
	}

	if ((rdev->wiphy.wowlan->flags & WIPHY_WOWLAN_NET_DETECT) &&
	    nla_put_u32(msg, NL80211_WOWLAN_TRIG_NET_DETECT,
			rdev->wiphy.wowlan->max_nd_match_sets))
		return -ENOBUFS;

	if (large && nl80211_send_wowlan_tcp_caps(rdev, msg))
		return -ENOBUFS;

	nla_nest_end(msg, nl_wowlan);

	return 0;
}
#endif

static int nl80211_send_coalesce(struct sk_buff *msg,
				 struct cfg80211_registered_device *rdev)
{
	struct nl80211_coalesce_rule_support rule;

	if (!rdev->wiphy.coalesce)
		return 0;

	rule.max_rules = rdev->wiphy.coalesce->n_rules;
	rule.max_delay = rdev->wiphy.coalesce->max_delay;
	rule.pat.max_patterns = rdev->wiphy.coalesce->n_patterns;
	rule.pat.min_pattern_len = rdev->wiphy.coalesce->pattern_min_len;
	rule.pat.max_pattern_len = rdev->wiphy.coalesce->pattern_max_len;
	rule.pat.max_pkt_offset = rdev->wiphy.coalesce->max_pkt_offset;

	if (nla_put(msg, NL80211_ATTR_COALESCE_RULE, sizeof(rule), &rule))
		return -ENOBUFS;

	return 0;
}

static int
nl80211_send_iftype_data(struct sk_buff *msg,
			 const struct ieee80211_sband_iftype_data *iftdata)
{
	const struct ieee80211_sta_he_cap *he_cap = &iftdata->he_cap;

	if (nl80211_put_iftypes(msg, NL80211_BAND_IFTYPE_ATTR_IFTYPES,
				iftdata->types_mask))
		return -ENOBUFS;

	if (he_cap->has_he) {
		if (nla_put(msg, NL80211_BAND_IFTYPE_ATTR_HE_CAP_MAC,
			    sizeof(he_cap->he_cap_elem.mac_cap_info),
			    he_cap->he_cap_elem.mac_cap_info) ||
		    nla_put(msg, NL80211_BAND_IFTYPE_ATTR_HE_CAP_PHY,
			    sizeof(he_cap->he_cap_elem.phy_cap_info),
			    he_cap->he_cap_elem.phy_cap_info) ||
		    nla_put(msg, NL80211_BAND_IFTYPE_ATTR_HE_CAP_MCS_SET,
			    sizeof(he_cap->he_mcs_nss_supp),
			    &he_cap->he_mcs_nss_supp) ||
		    nla_put(msg, NL80211_BAND_IFTYPE_ATTR_HE_CAP_PPE,
			    sizeof(he_cap->ppe_thres), he_cap->ppe_thres))
			return -ENOBUFS;
	}

	return 0;
}

static int nl80211_send_band_rateinfo(struct sk_buff *msg,
				      struct ieee80211_supported_band *sband)
{
	struct nlattr *nl_rates, *nl_rate;
	struct ieee80211_rate *rate;
	int i;

	/* add HT info */
	if (sband->ht_cap.ht_supported &&
	    (nla_put(msg, NL80211_BAND_ATTR_HT_MCS_SET,
		     sizeof(sband->ht_cap.mcs),
		     &sband->ht_cap.mcs) ||
	     nla_put_u16(msg, NL80211_BAND_ATTR_HT_CAPA,
			 sband->ht_cap.cap) ||
	     nla_put_u8(msg, NL80211_BAND_ATTR_HT_AMPDU_FACTOR,
			sband->ht_cap.ampdu_factor) ||
	     nla_put_u8(msg, NL80211_BAND_ATTR_HT_AMPDU_DENSITY,
			sband->ht_cap.ampdu_density)))
		return -ENOBUFS;

	/* add VHT info */
	if (sband->vht_cap.vht_supported &&
	    (nla_put(msg, NL80211_BAND_ATTR_VHT_MCS_SET,
		     sizeof(sband->vht_cap.vht_mcs),
		     &sband->vht_cap.vht_mcs) ||
	     nla_put_u32(msg, NL80211_BAND_ATTR_VHT_CAPA,
			 sband->vht_cap.cap)))
		return -ENOBUFS;

	if (sband->n_iftype_data) {
		struct nlattr *nl_iftype_data =
			nla_nest_start_noflag(msg,
					      NL80211_BAND_ATTR_IFTYPE_DATA);
		int err;

		if (!nl_iftype_data)
			return -ENOBUFS;

		for (i = 0; i < sband->n_iftype_data; i++) {
			struct nlattr *iftdata;

			iftdata = nla_nest_start_noflag(msg, i + 1);
			if (!iftdata)
				return -ENOBUFS;

			err = nl80211_send_iftype_data(msg,
						       &sband->iftype_data[i]);
			if (err)
				return err;

			nla_nest_end(msg, iftdata);
		}

		nla_nest_end(msg, nl_iftype_data);
	}

	/* add bitrates */
	nl_rates = nla_nest_start_noflag(msg, NL80211_BAND_ATTR_RATES);
	if (!nl_rates)
		return -ENOBUFS;

	for (i = 0; i < sband->n_bitrates; i++) {
		nl_rate = nla_nest_start_noflag(msg, i);
		if (!nl_rate)
			return -ENOBUFS;

		rate = &sband->bitrates[i];
		if (nla_put_u32(msg, NL80211_BITRATE_ATTR_RATE,
				rate->bitrate))
			return -ENOBUFS;
		if ((rate->flags & IEEE80211_RATE_SHORT_PREAMBLE) &&
		    nla_put_flag(msg,
				 NL80211_BITRATE_ATTR_2GHZ_SHORTPREAMBLE))
			return -ENOBUFS;

		nla_nest_end(msg, nl_rate);
	}

	nla_nest_end(msg, nl_rates);

	return 0;
}

static int
nl80211_send_mgmt_stypes(struct sk_buff *msg,
			 const struct ieee80211_txrx_stypes *mgmt_stypes)
{
	u16 stypes;
	struct nlattr *nl_ftypes, *nl_ifs;
	enum nl80211_iftype ift;
	int i;

	if (!mgmt_stypes)
		return 0;

	nl_ifs = nla_nest_start_noflag(msg, NL80211_ATTR_TX_FRAME_TYPES);
	if (!nl_ifs)
		return -ENOBUFS;

	for (ift = 0; ift < NUM_NL80211_IFTYPES; ift++) {
		nl_ftypes = nla_nest_start_noflag(msg, ift);
		if (!nl_ftypes)
			return -ENOBUFS;
		i = 0;
		stypes = mgmt_stypes[ift].tx;
		while (stypes) {
			if ((stypes & 1) &&
			    nla_put_u16(msg, NL80211_ATTR_FRAME_TYPE,
					(i << 4) | IEEE80211_FTYPE_MGMT))
				return -ENOBUFS;
			stypes >>= 1;
			i++;
		}
		nla_nest_end(msg, nl_ftypes);
	}

	nla_nest_end(msg, nl_ifs);

	nl_ifs = nla_nest_start_noflag(msg, NL80211_ATTR_RX_FRAME_TYPES);
	if (!nl_ifs)
		return -ENOBUFS;

	for (ift = 0; ift < NUM_NL80211_IFTYPES; ift++) {
		nl_ftypes = nla_nest_start_noflag(msg, ift);
		if (!nl_ftypes)
			return -ENOBUFS;
		i = 0;
		stypes = mgmt_stypes[ift].rx;
		while (stypes) {
			if ((stypes & 1) &&
			    nla_put_u16(msg, NL80211_ATTR_FRAME_TYPE,
					(i << 4) | IEEE80211_FTYPE_MGMT))
				return -ENOBUFS;
			stypes >>= 1;
			i++;
		}
		nla_nest_end(msg, nl_ftypes);
	}
	nla_nest_end(msg, nl_ifs);

	return 0;
}

#define CMD(op, n)							\
	 do {								\
		if (rdev->ops->op) {					\
			i++;						\
			if (nla_put_u32(msg, i, NL80211_CMD_ ## n)) 	\
				goto nla_put_failure;			\
		}							\
	} while (0)

static int nl80211_add_commands_unsplit(struct cfg80211_registered_device *rdev,
					struct sk_buff *msg)
{
	int i = 0;

	/*
	 * do *NOT* add anything into this function, new things need to be
	 * advertised only to new versions of userspace that can deal with
	 * the split (and they can't possibly care about new features...
	 */
	CMD(add_virtual_intf, NEW_INTERFACE);
	CMD(change_virtual_intf, SET_INTERFACE);
	CMD(add_key, NEW_KEY);
	CMD(start_ap, START_AP);
	CMD(add_station, NEW_STATION);
	CMD(add_mpath, NEW_MPATH);
	CMD(update_mesh_config, SET_MESH_CONFIG);
	CMD(change_bss, SET_BSS);
	CMD(auth, AUTHENTICATE);
	CMD(assoc, ASSOCIATE);
	CMD(deauth, DEAUTHENTICATE);
	CMD(disassoc, DISASSOCIATE);
	CMD(join_ibss, JOIN_IBSS);
	CMD(join_mesh, JOIN_MESH);
	CMD(set_pmksa, SET_PMKSA);
	CMD(del_pmksa, DEL_PMKSA);
	CMD(flush_pmksa, FLUSH_PMKSA);
	if (rdev->wiphy.flags & WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL)
		CMD(remain_on_channel, REMAIN_ON_CHANNEL);
	CMD(set_bitrate_mask, SET_TX_BITRATE_MASK);
	CMD(mgmt_tx, FRAME);
	CMD(mgmt_tx_cancel_wait, FRAME_WAIT_CANCEL);
	if (rdev->wiphy.flags & WIPHY_FLAG_NETNS_OK) {
		i++;
		if (nla_put_u32(msg, i, NL80211_CMD_SET_WIPHY_NETNS))
			goto nla_put_failure;
	}
	if (rdev->ops->set_monitor_channel || rdev->ops->start_ap ||
	    rdev->ops->join_mesh) {
		i++;
		if (nla_put_u32(msg, i, NL80211_CMD_SET_CHANNEL))
			goto nla_put_failure;
	}
	CMD(set_wds_peer, SET_WDS_PEER);
	if (rdev->wiphy.flags & WIPHY_FLAG_SUPPORTS_TDLS) {
		CMD(tdls_mgmt, TDLS_MGMT);
		CMD(tdls_oper, TDLS_OPER);
	}
	if (rdev->wiphy.max_sched_scan_reqs)
		CMD(sched_scan_start, START_SCHED_SCAN);
	CMD(probe_client, PROBE_CLIENT);
	CMD(set_noack_map, SET_NOACK_MAP);
	if (rdev->wiphy.flags & WIPHY_FLAG_REPORTS_OBSS) {
		i++;
		if (nla_put_u32(msg, i, NL80211_CMD_REGISTER_BEACONS))
			goto nla_put_failure;
	}
	CMD(start_p2p_device, START_P2P_DEVICE);
	CMD(set_mcast_rate, SET_MCAST_RATE);
#ifdef CONFIG_NL80211_TESTMODE
	CMD(testmode_cmd, TESTMODE);
#endif

	if (rdev->ops->connect || rdev->ops->auth) {
		i++;
		if (nla_put_u32(msg, i, NL80211_CMD_CONNECT))
			goto nla_put_failure;
	}

	if (rdev->ops->disconnect || rdev->ops->deauth) {
		i++;
		if (nla_put_u32(msg, i, NL80211_CMD_DISCONNECT))
			goto nla_put_failure;
	}

	return i;
 nla_put_failure:
	return -ENOBUFS;
}

static int
nl80211_send_pmsr_ftm_capa(const struct cfg80211_pmsr_capabilities *cap,
			   struct sk_buff *msg)
{
	struct nlattr *ftm;

	if (!cap->ftm.supported)
		return 0;

	ftm = nla_nest_start_noflag(msg, NL80211_PMSR_TYPE_FTM);
	if (!ftm)
		return -ENOBUFS;

	if (cap->ftm.asap && nla_put_flag(msg, NL80211_PMSR_FTM_CAPA_ATTR_ASAP))
		return -ENOBUFS;
	if (cap->ftm.non_asap &&
	    nla_put_flag(msg, NL80211_PMSR_FTM_CAPA_ATTR_NON_ASAP))
		return -ENOBUFS;
	if (cap->ftm.request_lci &&
	    nla_put_flag(msg, NL80211_PMSR_FTM_CAPA_ATTR_REQ_LCI))
		return -ENOBUFS;
	if (cap->ftm.request_civicloc &&
	    nla_put_flag(msg, NL80211_PMSR_FTM_CAPA_ATTR_REQ_CIVICLOC))
		return -ENOBUFS;
	if (nla_put_u32(msg, NL80211_PMSR_FTM_CAPA_ATTR_PREAMBLES,
			cap->ftm.preambles))
		return -ENOBUFS;
	if (nla_put_u32(msg, NL80211_PMSR_FTM_CAPA_ATTR_BANDWIDTHS,
			cap->ftm.bandwidths))
		return -ENOBUFS;
	if (cap->ftm.max_bursts_exponent >= 0 &&
	    nla_put_u32(msg, NL80211_PMSR_FTM_CAPA_ATTR_MAX_BURSTS_EXPONENT,
			cap->ftm.max_bursts_exponent))
		return -ENOBUFS;
	if (cap->ftm.max_ftms_per_burst &&
	    nla_put_u32(msg, NL80211_PMSR_FTM_CAPA_ATTR_MAX_FTMS_PER_BURST,
			cap->ftm.max_ftms_per_burst))
		return -ENOBUFS;

	nla_nest_end(msg, ftm);
	return 0;
}

static int nl80211_send_pmsr_capa(struct cfg80211_registered_device *rdev,
				  struct sk_buff *msg)
{
	const struct cfg80211_pmsr_capabilities *cap = rdev->wiphy.pmsr_capa;
	struct nlattr *pmsr, *caps;

	if (!cap)
		return 0;

	/*
	 * we don't need to clean up anything here since the caller
	 * will genlmsg_cancel() if we fail
	 */

	pmsr = nla_nest_start_noflag(msg, NL80211_ATTR_PEER_MEASUREMENTS);
	if (!pmsr)
		return -ENOBUFS;

	if (nla_put_u32(msg, NL80211_PMSR_ATTR_MAX_PEERS, cap->max_peers))
		return -ENOBUFS;

	if (cap->report_ap_tsf &&
	    nla_put_flag(msg, NL80211_PMSR_ATTR_REPORT_AP_TSF))
		return -ENOBUFS;

	if (cap->randomize_mac_addr &&
	    nla_put_flag(msg, NL80211_PMSR_ATTR_RANDOMIZE_MAC_ADDR))
		return -ENOBUFS;

	caps = nla_nest_start_noflag(msg, NL80211_PMSR_ATTR_TYPE_CAPA);
	if (!caps)
		return -ENOBUFS;

	if (nl80211_send_pmsr_ftm_capa(cap, msg))
		return -ENOBUFS;

	nla_nest_end(msg, caps);
	nla_nest_end(msg, pmsr);

	return 0;
}

struct nl80211_dump_wiphy_state {
	s64 filter_wiphy;
	long start;
	long split_start, band_start, chan_start, capa_start;
	bool split;
};

static int nl80211_send_wiphy(struct cfg80211_registered_device *rdev,
			      enum nl80211_commands cmd,
			      struct sk_buff *msg, u32 portid, u32 seq,
			      int flags, struct nl80211_dump_wiphy_state *state)
{
	void *hdr;
	struct nlattr *nl_bands, *nl_band;
	struct nlattr *nl_freqs, *nl_freq;
	struct nlattr *nl_cmds;
	enum nl80211_band band;
	struct ieee80211_channel *chan;
	int i;
	const struct ieee80211_txrx_stypes *mgmt_stypes =
				rdev->wiphy.mgmt_stypes;
	u32 features;

	hdr = nl80211hdr_put(msg, portid, seq, flags, cmd);
	if (!hdr)
		return -ENOBUFS;

	if (WARN_ON(!state))
		return -EINVAL;

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_string(msg, NL80211_ATTR_WIPHY_NAME,
			   wiphy_name(&rdev->wiphy)) ||
	    nla_put_u32(msg, NL80211_ATTR_GENERATION,
			cfg80211_rdev_list_generation))
		goto nla_put_failure;

	if (cmd != NL80211_CMD_NEW_WIPHY)
		goto finish;

	switch (state->split_start) {
	case 0:
		if (nla_put_u8(msg, NL80211_ATTR_WIPHY_RETRY_SHORT,
			       rdev->wiphy.retry_short) ||
		    nla_put_u8(msg, NL80211_ATTR_WIPHY_RETRY_LONG,
			       rdev->wiphy.retry_long) ||
		    nla_put_u32(msg, NL80211_ATTR_WIPHY_FRAG_THRESHOLD,
				rdev->wiphy.frag_threshold) ||
		    nla_put_u32(msg, NL80211_ATTR_WIPHY_RTS_THRESHOLD,
				rdev->wiphy.rts_threshold) ||
		    nla_put_u8(msg, NL80211_ATTR_WIPHY_COVERAGE_CLASS,
			       rdev->wiphy.coverage_class) ||
		    nla_put_u8(msg, NL80211_ATTR_MAX_NUM_SCAN_SSIDS,
			       rdev->wiphy.max_scan_ssids) ||
		    nla_put_u8(msg, NL80211_ATTR_MAX_NUM_SCHED_SCAN_SSIDS,
			       rdev->wiphy.max_sched_scan_ssids) ||
		    nla_put_u16(msg, NL80211_ATTR_MAX_SCAN_IE_LEN,
				rdev->wiphy.max_scan_ie_len) ||
		    nla_put_u16(msg, NL80211_ATTR_MAX_SCHED_SCAN_IE_LEN,
				rdev->wiphy.max_sched_scan_ie_len) ||
		    nla_put_u8(msg, NL80211_ATTR_MAX_MATCH_SETS,
			       rdev->wiphy.max_match_sets) ||
		    nla_put_u32(msg, NL80211_ATTR_MAX_NUM_SCHED_SCAN_PLANS,
				rdev->wiphy.max_sched_scan_plans) ||
		    nla_put_u32(msg, NL80211_ATTR_MAX_SCAN_PLAN_INTERVAL,
				rdev->wiphy.max_sched_scan_plan_interval) ||
		    nla_put_u32(msg, NL80211_ATTR_MAX_SCAN_PLAN_ITERATIONS,
				rdev->wiphy.max_sched_scan_plan_iterations))
			goto nla_put_failure;

		if ((rdev->wiphy.flags & WIPHY_FLAG_IBSS_RSN) &&
		    nla_put_flag(msg, NL80211_ATTR_SUPPORT_IBSS_RSN))
			goto nla_put_failure;
		if ((rdev->wiphy.flags & WIPHY_FLAG_MESH_AUTH) &&
		    nla_put_flag(msg, NL80211_ATTR_SUPPORT_MESH_AUTH))
			goto nla_put_failure;
		if ((rdev->wiphy.flags & WIPHY_FLAG_AP_UAPSD) &&
		    nla_put_flag(msg, NL80211_ATTR_SUPPORT_AP_UAPSD))
			goto nla_put_failure;
		if ((rdev->wiphy.flags & WIPHY_FLAG_SUPPORTS_FW_ROAM) &&
		    nla_put_flag(msg, NL80211_ATTR_ROAM_SUPPORT))
			goto nla_put_failure;
		if ((rdev->wiphy.flags & WIPHY_FLAG_SUPPORTS_TDLS) &&
		    nla_put_flag(msg, NL80211_ATTR_TDLS_SUPPORT))
			goto nla_put_failure;
		if ((rdev->wiphy.flags & WIPHY_FLAG_TDLS_EXTERNAL_SETUP) &&
		    nla_put_flag(msg, NL80211_ATTR_TDLS_EXTERNAL_SETUP))
			goto nla_put_failure;
		state->split_start++;
		if (state->split)
			break;
		/* fall through */
	case 1:
		if (nla_put(msg, NL80211_ATTR_CIPHER_SUITES,
			    sizeof(u32) * rdev->wiphy.n_cipher_suites,
			    rdev->wiphy.cipher_suites))
			goto nla_put_failure;

		if (nla_put_u8(msg, NL80211_ATTR_MAX_NUM_PMKIDS,
			       rdev->wiphy.max_num_pmkids))
			goto nla_put_failure;

		if ((rdev->wiphy.flags & WIPHY_FLAG_CONTROL_PORT_PROTOCOL) &&
		    nla_put_flag(msg, NL80211_ATTR_CONTROL_PORT_ETHERTYPE))
			goto nla_put_failure;

		if (nla_put_u32(msg, NL80211_ATTR_WIPHY_ANTENNA_AVAIL_TX,
				rdev->wiphy.available_antennas_tx) ||
		    nla_put_u32(msg, NL80211_ATTR_WIPHY_ANTENNA_AVAIL_RX,
				rdev->wiphy.available_antennas_rx))
			goto nla_put_failure;

		if ((rdev->wiphy.flags & WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD) &&
		    nla_put_u32(msg, NL80211_ATTR_PROBE_RESP_OFFLOAD,
				rdev->wiphy.probe_resp_offload))
			goto nla_put_failure;

		if ((rdev->wiphy.available_antennas_tx ||
		     rdev->wiphy.available_antennas_rx) &&
		    rdev->ops->get_antenna) {
			u32 tx_ant = 0, rx_ant = 0;
			int res;

			res = rdev_get_antenna(rdev, &tx_ant, &rx_ant);
			if (!res) {
				if (nla_put_u32(msg,
						NL80211_ATTR_WIPHY_ANTENNA_TX,
						tx_ant) ||
				    nla_put_u32(msg,
						NL80211_ATTR_WIPHY_ANTENNA_RX,
						rx_ant))
					goto nla_put_failure;
			}
		}

		state->split_start++;
		if (state->split)
			break;
		/* fall through */
	case 2:
		if (nl80211_put_iftypes(msg, NL80211_ATTR_SUPPORTED_IFTYPES,
					rdev->wiphy.interface_modes))
				goto nla_put_failure;
		state->split_start++;
		if (state->split)
			break;
		/* fall through */
	case 3:
		nl_bands = nla_nest_start_noflag(msg,
						 NL80211_ATTR_WIPHY_BANDS);
		if (!nl_bands)
			goto nla_put_failure;

		for (band = state->band_start;
		     band < NUM_NL80211_BANDS; band++) {
			struct ieee80211_supported_band *sband;

			sband = rdev->wiphy.bands[band];

			if (!sband)
				continue;

			nl_band = nla_nest_start_noflag(msg, band);
			if (!nl_band)
				goto nla_put_failure;

			switch (state->chan_start) {
			case 0:
				if (nl80211_send_band_rateinfo(msg, sband))
					goto nla_put_failure;
				state->chan_start++;
				if (state->split)
					break;
				/* fall through */
			default:
				/* add frequencies */
				nl_freqs = nla_nest_start_noflag(msg,
								 NL80211_BAND_ATTR_FREQS);
				if (!nl_freqs)
					goto nla_put_failure;

				for (i = state->chan_start - 1;
				     i < sband->n_channels;
				     i++) {
					nl_freq = nla_nest_start_noflag(msg,
									i);
					if (!nl_freq)
						goto nla_put_failure;

					chan = &sband->channels[i];

					if (nl80211_msg_put_channel(
							msg, &rdev->wiphy, chan,
							state->split))
						goto nla_put_failure;

					nla_nest_end(msg, nl_freq);
					if (state->split)
						break;
				}
				if (i < sband->n_channels)
					state->chan_start = i + 2;
				else
					state->chan_start = 0;
				nla_nest_end(msg, nl_freqs);
			}

			nla_nest_end(msg, nl_band);

			if (state->split) {
				/* start again here */
				if (state->chan_start)
					band--;
				break;
			}
		}
		nla_nest_end(msg, nl_bands);

		if (band < NUM_NL80211_BANDS)
			state->band_start = band + 1;
		else
			state->band_start = 0;

		/* if bands & channels are done, continue outside */
		if (state->band_start == 0 && state->chan_start == 0)
			state->split_start++;
		if (state->split)
			break;
		/* fall through */
	case 4:
		nl_cmds = nla_nest_start_noflag(msg,
						NL80211_ATTR_SUPPORTED_COMMANDS);
		if (!nl_cmds)
			goto nla_put_failure;

		i = nl80211_add_commands_unsplit(rdev, msg);
		if (i < 0)
			goto nla_put_failure;
		if (state->split) {
			CMD(crit_proto_start, CRIT_PROTOCOL_START);
			CMD(crit_proto_stop, CRIT_PROTOCOL_STOP);
			if (rdev->wiphy.flags & WIPHY_FLAG_HAS_CHANNEL_SWITCH)
				CMD(channel_switch, CHANNEL_SWITCH);
			CMD(set_qos_map, SET_QOS_MAP);
			if (rdev->wiphy.features &
					NL80211_FEATURE_SUPPORTS_WMM_ADMISSION)
				CMD(add_tx_ts, ADD_TX_TS);
			CMD(set_multicast_to_unicast, SET_MULTICAST_TO_UNICAST);
			CMD(update_connect_params, UPDATE_CONNECT_PARAMS);
		}
#undef CMD

		nla_nest_end(msg, nl_cmds);
		state->split_start++;
		if (state->split)
			break;
		/* fall through */
	case 5:
		if (rdev->ops->remain_on_channel &&
		    (rdev->wiphy.flags & WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL) &&
		    nla_put_u32(msg,
				NL80211_ATTR_MAX_REMAIN_ON_CHANNEL_DURATION,
				rdev->wiphy.max_remain_on_channel_duration))
			goto nla_put_failure;

		if ((rdev->wiphy.flags & WIPHY_FLAG_OFFCHAN_TX) &&
		    nla_put_flag(msg, NL80211_ATTR_OFFCHANNEL_TX_OK))
			goto nla_put_failure;

		if (nl80211_send_mgmt_stypes(msg, mgmt_stypes))
			goto nla_put_failure;
		state->split_start++;
		if (state->split)
			break;
		/* fall through */
	case 6:
#ifdef CONFIG_PM
		if (nl80211_send_wowlan(msg, rdev, state->split))
			goto nla_put_failure;
		state->split_start++;
		if (state->split)
			break;
#else
		state->split_start++;
#endif
		/* fall through */
	case 7:
		if (nl80211_put_iftypes(msg, NL80211_ATTR_SOFTWARE_IFTYPES,
					rdev->wiphy.software_iftypes))
			goto nla_put_failure;

		if (nl80211_put_iface_combinations(&rdev->wiphy, msg,
						   state->split))
			goto nla_put_failure;

		state->split_start++;
		if (state->split)
			break;
		/* fall through */
	case 8:
		if ((rdev->wiphy.flags & WIPHY_FLAG_HAVE_AP_SME) &&
		    nla_put_u32(msg, NL80211_ATTR_DEVICE_AP_SME,
				rdev->wiphy.ap_sme_capa))
			goto nla_put_failure;

		features = rdev->wiphy.features;
		/*
		 * We can only add the per-channel limit information if the
		 * dump is split, otherwise it makes it too big. Therefore
		 * only advertise it in that case.
		 */
		if (state->split)
			features |= NL80211_FEATURE_ADVERTISE_CHAN_LIMITS;
		if (nla_put_u32(msg, NL80211_ATTR_FEATURE_FLAGS, features))
			goto nla_put_failure;

		if (rdev->wiphy.ht_capa_mod_mask &&
		    nla_put(msg, NL80211_ATTR_HT_CAPABILITY_MASK,
			    sizeof(*rdev->wiphy.ht_capa_mod_mask),
			    rdev->wiphy.ht_capa_mod_mask))
			goto nla_put_failure;

		if (rdev->wiphy.flags & WIPHY_FLAG_HAVE_AP_SME &&
		    rdev->wiphy.max_acl_mac_addrs &&
		    nla_put_u32(msg, NL80211_ATTR_MAC_ACL_MAX,
				rdev->wiphy.max_acl_mac_addrs))
			goto nla_put_failure;

		/*
		 * Any information below this point is only available to
		 * applications that can deal with it being split. This
		 * helps ensure that newly added capabilities don't break
		 * older tools by overrunning their buffers.
		 *
		 * We still increment split_start so that in the split
		 * case we'll continue with more data in the next round,
		 * but break unconditionally so unsplit data stops here.
		 */
		state->split_start++;
		break;
	case 9:
		if (rdev->wiphy.extended_capabilities &&
		    (nla_put(msg, NL80211_ATTR_EXT_CAPA,
			     rdev->wiphy.extended_capabilities_len,
			     rdev->wiphy.extended_capabilities) ||
		     nla_put(msg, NL80211_ATTR_EXT_CAPA_MASK,
			     rdev->wiphy.extended_capabilities_len,
			     rdev->wiphy.extended_capabilities_mask)))
			goto nla_put_failure;

		if (rdev->wiphy.vht_capa_mod_mask &&
		    nla_put(msg, NL80211_ATTR_VHT_CAPABILITY_MASK,
			    sizeof(*rdev->wiphy.vht_capa_mod_mask),
			    rdev->wiphy.vht_capa_mod_mask))
			goto nla_put_failure;

		state->split_start++;
		break;
	case 10:
		if (nl80211_send_coalesce(msg, rdev))
			goto nla_put_failure;

		if ((rdev->wiphy.flags & WIPHY_FLAG_SUPPORTS_5_10_MHZ) &&
		    (nla_put_flag(msg, NL80211_ATTR_SUPPORT_5_MHZ) ||
		     nla_put_flag(msg, NL80211_ATTR_SUPPORT_10_MHZ)))
			goto nla_put_failure;

		if (rdev->wiphy.max_ap_assoc_sta &&
		    nla_put_u32(msg, NL80211_ATTR_MAX_AP_ASSOC_STA,
				rdev->wiphy.max_ap_assoc_sta))
			goto nla_put_failure;

		state->split_start++;
		break;
	case 11:
		if (rdev->wiphy.n_vendor_commands) {
			const struct nl80211_vendor_cmd_info *info;
			struct nlattr *nested;

			nested = nla_nest_start_noflag(msg,
						       NL80211_ATTR_VENDOR_DATA);
			if (!nested)
				goto nla_put_failure;

			for (i = 0; i < rdev->wiphy.n_vendor_commands; i++) {
				info = &rdev->wiphy.vendor_commands[i].info;
				if (nla_put(msg, i + 1, sizeof(*info), info))
					goto nla_put_failure;
			}
			nla_nest_end(msg, nested);
		}

		if (rdev->wiphy.n_vendor_events) {
			const struct nl80211_vendor_cmd_info *info;
			struct nlattr *nested;

			nested = nla_nest_start_noflag(msg,
						       NL80211_ATTR_VENDOR_EVENTS);
			if (!nested)
				goto nla_put_failure;

			for (i = 0; i < rdev->wiphy.n_vendor_events; i++) {
				info = &rdev->wiphy.vendor_events[i];
				if (nla_put(msg, i + 1, sizeof(*info), info))
					goto nla_put_failure;
			}
			nla_nest_end(msg, nested);
		}
		state->split_start++;
		break;
	case 12:
		if (rdev->wiphy.flags & WIPHY_FLAG_HAS_CHANNEL_SWITCH &&
		    nla_put_u8(msg, NL80211_ATTR_MAX_CSA_COUNTERS,
			       rdev->wiphy.max_num_csa_counters))
			goto nla_put_failure;

		if (rdev->wiphy.regulatory_flags & REGULATORY_WIPHY_SELF_MANAGED &&
		    nla_put_flag(msg, NL80211_ATTR_WIPHY_SELF_MANAGED_REG))
			goto nla_put_failure;

		if (rdev->wiphy.max_sched_scan_reqs &&
		    nla_put_u32(msg, NL80211_ATTR_SCHED_SCAN_MAX_REQS,
				rdev->wiphy.max_sched_scan_reqs))
			goto nla_put_failure;

		if (nla_put(msg, NL80211_ATTR_EXT_FEATURES,
			    sizeof(rdev->wiphy.ext_features),
			    rdev->wiphy.ext_features))
			goto nla_put_failure;

		if (rdev->wiphy.bss_select_support) {
			struct nlattr *nested;
			u32 bss_select_support = rdev->wiphy.bss_select_support;

			nested = nla_nest_start_noflag(msg,
						       NL80211_ATTR_BSS_SELECT);
			if (!nested)
				goto nla_put_failure;

			i = 0;
			while (bss_select_support) {
				if ((bss_select_support & 1) &&
				    nla_put_flag(msg, i))
					goto nla_put_failure;
				i++;
				bss_select_support >>= 1;
			}
			nla_nest_end(msg, nested);
		}

		state->split_start++;
		break;
	case 13:
		if (rdev->wiphy.num_iftype_ext_capab &&
		    rdev->wiphy.iftype_ext_capab) {
			struct nlattr *nested_ext_capab, *nested;

			nested = nla_nest_start_noflag(msg,
						       NL80211_ATTR_IFTYPE_EXT_CAPA);
			if (!nested)
				goto nla_put_failure;

			for (i = state->capa_start;
			     i < rdev->wiphy.num_iftype_ext_capab; i++) {
				const struct wiphy_iftype_ext_capab *capab;

				capab = &rdev->wiphy.iftype_ext_capab[i];

				nested_ext_capab = nla_nest_start_noflag(msg,
									 i);
				if (!nested_ext_capab ||
				    nla_put_u32(msg, NL80211_ATTR_IFTYPE,
						capab->iftype) ||
				    nla_put(msg, NL80211_ATTR_EXT_CAPA,
					    capab->extended_capabilities_len,
					    capab->extended_capabilities) ||
				    nla_put(msg, NL80211_ATTR_EXT_CAPA_MASK,
					    capab->extended_capabilities_len,
					    capab->extended_capabilities_mask))
					goto nla_put_failure;

				nla_nest_end(msg, nested_ext_capab);
				if (state->split)
					break;
			}
			nla_nest_end(msg, nested);
			if (i < rdev->wiphy.num_iftype_ext_capab) {
				state->capa_start = i + 1;
				break;
			}
		}

		if (nla_put_u32(msg, NL80211_ATTR_BANDS,
				rdev->wiphy.nan_supported_bands))
			goto nla_put_failure;

		if (wiphy_ext_feature_isset(&rdev->wiphy,
					    NL80211_EXT_FEATURE_TXQS)) {
			struct cfg80211_txq_stats txqstats = {};
			int res;

			res = rdev_get_txq_stats(rdev, NULL, &txqstats);
			if (!res &&
			    !nl80211_put_txq_stats(msg, &txqstats,
						   NL80211_ATTR_TXQ_STATS))
				goto nla_put_failure;

			if (nla_put_u32(msg, NL80211_ATTR_TXQ_LIMIT,
					rdev->wiphy.txq_limit))
				goto nla_put_failure;
			if (nla_put_u32(msg, NL80211_ATTR_TXQ_MEMORY_LIMIT,
					rdev->wiphy.txq_memory_limit))
				goto nla_put_failure;
			if (nla_put_u32(msg, NL80211_ATTR_TXQ_QUANTUM,
					rdev->wiphy.txq_quantum))
				goto nla_put_failure;
		}

		state->split_start++;
		break;
	case 14:
		if (nl80211_send_pmsr_capa(rdev, msg))
			goto nla_put_failure;

		state->split_start++;
		break;
	case 15:
		if (rdev->wiphy.akm_suites &&
		    nla_put(msg, NL80211_ATTR_AKM_SUITES,
			    sizeof(u32) * rdev->wiphy.n_akm_suites,
			    rdev->wiphy.akm_suites))
			goto nla_put_failure;

		/* done */
		state->split_start = 0;
		break;
	}
 finish:
	genlmsg_end(msg, hdr);
	return 0;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nl80211_dump_wiphy_parse(struct sk_buff *skb,
				    struct netlink_callback *cb,
				    struct nl80211_dump_wiphy_state *state)
{
	struct nlattr **tb = genl_family_attrbuf(&nl80211_fam);
	int ret = nlmsg_parse_deprecated(cb->nlh,
					 GENL_HDRLEN + nl80211_fam.hdrsize,
					 tb, nl80211_fam.maxattr,
					 nl80211_policy, NULL);
	/* ignore parse errors for backward compatibility */
	if (ret)
		return 0;

	state->split = tb[NL80211_ATTR_SPLIT_WIPHY_DUMP];
	if (tb[NL80211_ATTR_WIPHY])
		state->filter_wiphy = nla_get_u32(tb[NL80211_ATTR_WIPHY]);
	if (tb[NL80211_ATTR_WDEV])
		state->filter_wiphy = nla_get_u64(tb[NL80211_ATTR_WDEV]) >> 32;
	if (tb[NL80211_ATTR_IFINDEX]) {
		struct net_device *netdev;
		struct cfg80211_registered_device *rdev;
		int ifidx = nla_get_u32(tb[NL80211_ATTR_IFINDEX]);

		netdev = __dev_get_by_index(sock_net(skb->sk), ifidx);
		if (!netdev)
			return -ENODEV;
		if (netdev->ieee80211_ptr) {
			rdev = wiphy_to_rdev(
				netdev->ieee80211_ptr->wiphy);
			state->filter_wiphy = rdev->wiphy_idx;
		}
	}

	return 0;
}

static int nl80211_dump_wiphy(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx = 0, ret;
	struct nl80211_dump_wiphy_state *state = (void *)cb->args[0];
	struct cfg80211_registered_device *rdev;

	rtnl_lock();
	if (!state) {
		state = kzalloc(sizeof(*state), GFP_KERNEL);
		if (!state) {
			rtnl_unlock();
			return -ENOMEM;
		}
		state->filter_wiphy = -1;
		ret = nl80211_dump_wiphy_parse(skb, cb, state);
		if (ret) {
			kfree(state);
			rtnl_unlock();
			return ret;
		}
		cb->args[0] = (long)state;
	}

	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		if (!net_eq(wiphy_net(&rdev->wiphy), sock_net(skb->sk)))
			continue;
		if (++idx <= state->start)
			continue;
		if (state->filter_wiphy != -1 &&
		    state->filter_wiphy != rdev->wiphy_idx)
			continue;
		/* attempt to fit multiple wiphy data chunks into the skb */
		do {
			ret = nl80211_send_wiphy(rdev, NL80211_CMD_NEW_WIPHY,
						 skb,
						 NETLINK_CB(cb->skb).portid,
						 cb->nlh->nlmsg_seq,
						 NLM_F_MULTI, state);
			if (ret < 0) {
				/*
				 * If sending the wiphy data didn't fit (ENOBUFS
				 * or EMSGSIZE returned), this SKB is still
				 * empty (so it's not too big because another
				 * wiphy dataset is already in the skb) and
				 * we've not tried to adjust the dump allocation
				 * yet ... then adjust the alloc size to be
				 * bigger, and return 1 but with the empty skb.
				 * This results in an empty message being RX'ed
				 * in userspace, but that is ignored.
				 *
				 * We can then retry with the larger buffer.
				 */
				if ((ret == -ENOBUFS || ret == -EMSGSIZE) &&
				    !skb->len && !state->split &&
				    cb->min_dump_alloc < 4096) {
					cb->min_dump_alloc = 4096;
					state->split_start = 0;
					rtnl_unlock();
					return 1;
				}
				idx--;
				break;
			}
		} while (state->split_start > 0);
		break;
	}
	rtnl_unlock();

	state->start = idx;

	return skb->len;
}

static int nl80211_dump_wiphy_done(struct netlink_callback *cb)
{
	kfree((void *)cb->args[0]);
	return 0;
}

static int nl80211_get_wiphy(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct nl80211_dump_wiphy_state state = {};

	msg = nlmsg_new(4096, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	if (nl80211_send_wiphy(rdev, NL80211_CMD_NEW_WIPHY, msg,
			       info->snd_portid, info->snd_seq, 0,
			       &state) < 0) {
		nlmsg_free(msg);
		return -ENOBUFS;
	}

	return genlmsg_reply(msg, info);
}

static const struct nla_policy txq_params_policy[NL80211_TXQ_ATTR_MAX + 1] = {
	[NL80211_TXQ_ATTR_QUEUE]		= { .type = NLA_U8 },
	[NL80211_TXQ_ATTR_TXOP]			= { .type = NLA_U16 },
	[NL80211_TXQ_ATTR_CWMIN]		= { .type = NLA_U16 },
	[NL80211_TXQ_ATTR_CWMAX]		= { .type = NLA_U16 },
	[NL80211_TXQ_ATTR_AIFS]			= { .type = NLA_U8 },
};

static int parse_txq_params(struct nlattr *tb[],
			    struct ieee80211_txq_params *txq_params)
{
	u8 ac;

	if (!tb[NL80211_TXQ_ATTR_AC] || !tb[NL80211_TXQ_ATTR_TXOP] ||
	    !tb[NL80211_TXQ_ATTR_CWMIN] || !tb[NL80211_TXQ_ATTR_CWMAX] ||
	    !tb[NL80211_TXQ_ATTR_AIFS])
		return -EINVAL;

	ac = nla_get_u8(tb[NL80211_TXQ_ATTR_AC]);
	txq_params->txop = nla_get_u16(tb[NL80211_TXQ_ATTR_TXOP]);
	txq_params->cwmin = nla_get_u16(tb[NL80211_TXQ_ATTR_CWMIN]);
	txq_params->cwmax = nla_get_u16(tb[NL80211_TXQ_ATTR_CWMAX]);
	txq_params->aifs = nla_get_u8(tb[NL80211_TXQ_ATTR_AIFS]);

	if (ac >= NL80211_NUM_ACS)
		return -EINVAL;
	txq_params->ac = array_index_nospec(ac, NL80211_NUM_ACS);
	return 0;
}

static bool nl80211_can_set_dev_channel(struct wireless_dev *wdev)
{
	/*
	 * You can only set the channel explicitly for WDS interfaces,
	 * all others have their channel managed via their respective
	 * "establish a connection" command (connect, join, ...)
	 *
	 * For AP/GO and mesh mode, the channel can be set with the
	 * channel userspace API, but is only stored and passed to the
	 * low-level driver when the AP starts or the mesh is joined.
	 * This is for backward compatibility, userspace can also give
	 * the channel in the start-ap or join-mesh commands instead.
	 *
	 * Monitors are special as they are normally slaved to
	 * whatever else is going on, so they have their own special
	 * operation to set the monitor channel if possible.
	 */
	return !wdev ||
		wdev->iftype == NL80211_IFTYPE_AP ||
		wdev->iftype == NL80211_IFTYPE_MESH_POINT ||
		wdev->iftype == NL80211_IFTYPE_MONITOR ||
		wdev->iftype == NL80211_IFTYPE_P2P_GO;
}

int nl80211_parse_chandef(struct cfg80211_registered_device *rdev,
			  struct genl_info *info,
			  struct cfg80211_chan_def *chandef)
{
	struct netlink_ext_ack *extack = info->extack;
	struct nlattr **attrs = info->attrs;
	u32 control_freq;

	if (!attrs[NL80211_ATTR_WIPHY_FREQ])
		return -EINVAL;

	control_freq = nla_get_u32(attrs[NL80211_ATTR_WIPHY_FREQ]);

	chandef->chan = ieee80211_get_channel(&rdev->wiphy, control_freq);
	chandef->width = NL80211_CHAN_WIDTH_20_NOHT;
	chandef->center_freq1 = control_freq;
	chandef->center_freq2 = 0;

	/* Primary channel not allowed */
	if (!chandef->chan || chandef->chan->flags & IEEE80211_CHAN_DISABLED) {
		NL_SET_ERR_MSG_ATTR(extack, attrs[NL80211_ATTR_WIPHY_FREQ],
				    "Channel is disabled");
		return -EINVAL;
	}

	if (attrs[NL80211_ATTR_WIPHY_CHANNEL_TYPE]) {
		enum nl80211_channel_type chantype;

		chantype = nla_get_u32(attrs[NL80211_ATTR_WIPHY_CHANNEL_TYPE]);

		switch (chantype) {
		case NL80211_CHAN_NO_HT:
		case NL80211_CHAN_HT20:
		case NL80211_CHAN_HT40PLUS:
		case NL80211_CHAN_HT40MINUS:
			cfg80211_chandef_create(chandef, chandef->chan,
						chantype);
			/* user input for center_freq is incorrect */
			if (attrs[NL80211_ATTR_CENTER_FREQ1] &&
			    chandef->center_freq1 != nla_get_u32(attrs[NL80211_ATTR_CENTER_FREQ1])) {
				NL_SET_ERR_MSG_ATTR(extack,
						    attrs[NL80211_ATTR_CENTER_FREQ1],
						    "bad center frequency 1");
				return -EINVAL;
			}
			/* center_freq2 must be zero */
			if (attrs[NL80211_ATTR_CENTER_FREQ2] &&
			    nla_get_u32(attrs[NL80211_ATTR_CENTER_FREQ2])) {
				NL_SET_ERR_MSG_ATTR(extack,
						    attrs[NL80211_ATTR_CENTER_FREQ2],
						    "center frequency 2 can't be used");
				return -EINVAL;
			}
			break;
		default:
			NL_SET_ERR_MSG_ATTR(extack,
					    attrs[NL80211_ATTR_WIPHY_CHANNEL_TYPE],
					    "invalid channel type");
			return -EINVAL;
		}
	} else if (attrs[NL80211_ATTR_CHANNEL_WIDTH]) {
		chandef->width =
			nla_get_u32(attrs[NL80211_ATTR_CHANNEL_WIDTH]);
		if (attrs[NL80211_ATTR_CENTER_FREQ1])
			chandef->center_freq1 =
				nla_get_u32(attrs[NL80211_ATTR_CENTER_FREQ1]);
		if (attrs[NL80211_ATTR_CENTER_FREQ2])
			chandef->center_freq2 =
				nla_get_u32(attrs[NL80211_ATTR_CENTER_FREQ2]);
	}

	if (!cfg80211_chandef_valid(chandef)) {
		NL_SET_ERR_MSG(extack, "invalid channel definition");
		return -EINVAL;
	}

	if (!cfg80211_chandef_usable(&rdev->wiphy, chandef,
				     IEEE80211_CHAN_DISABLED)) {
		NL_SET_ERR_MSG(extack, "(extension) channel is disabled");
		return -EINVAL;
	}

	if ((chandef->width == NL80211_CHAN_WIDTH_5 ||
	     chandef->width == NL80211_CHAN_WIDTH_10) &&
	    !(rdev->wiphy.flags & WIPHY_FLAG_SUPPORTS_5_10_MHZ)) {
		NL_SET_ERR_MSG(extack, "5/10 MHz not supported");
		return -EINVAL;
	}

	return 0;
}

static int __nl80211_set_channel(struct cfg80211_registered_device *rdev,
				 struct net_device *dev,
				 struct genl_info *info)
{
	struct cfg80211_chan_def chandef;
	int result;
	enum nl80211_iftype iftype = NL80211_IFTYPE_MONITOR;
	struct wireless_dev *wdev = NULL;

	if (dev)
		wdev = dev->ieee80211_ptr;
	if (!nl80211_can_set_dev_channel(wdev))
		return -EOPNOTSUPP;
	if (wdev)
		iftype = wdev->iftype;

	result = nl80211_parse_chandef(rdev, info, &chandef);
	if (result)
		return result;

	switch (iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		if (!cfg80211_reg_can_beacon_relax(&rdev->wiphy, &chandef,
						   iftype)) {
			result = -EINVAL;
			break;
		}
		if (wdev->beacon_interval) {
			if (!dev || !rdev->ops->set_ap_chanwidth ||
			    !(rdev->wiphy.features &
			      NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE)) {
				result = -EBUSY;
				break;
			}

			/* Only allow dynamic channel width changes */
			if (chandef.chan != wdev->preset_chandef.chan) {
				result = -EBUSY;
				break;
			}
			result = rdev_set_ap_chanwidth(rdev, dev, &chandef);
			if (result)
				break;
		}
		wdev->preset_chandef = chandef;
		result = 0;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		result = cfg80211_set_mesh_channel(rdev, wdev, &chandef);
		break;
	case NL80211_IFTYPE_MONITOR:
		result = cfg80211_set_monitor_channel(rdev, &chandef);
		break;
	default:
		result = -EINVAL;
	}

	return result;
}

static int nl80211_set_channel(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *netdev = info->user_ptr[1];

	return __nl80211_set_channel(rdev, netdev, info);
}

static int nl80211_set_wds_peer(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	const u8 *bssid;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (netif_running(dev))
		return -EBUSY;

	if (!rdev->ops->set_wds_peer)
		return -EOPNOTSUPP;

	if (wdev->iftype != NL80211_IFTYPE_WDS)
		return -EOPNOTSUPP;

	bssid = nla_data(info->attrs[NL80211_ATTR_MAC]);
	return rdev_set_wds_peer(rdev, dev, bssid);
}

static int nl80211_set_wiphy(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct net_device *netdev = NULL;
	struct wireless_dev *wdev;
	int result = 0, rem_txq_params = 0;
	struct nlattr *nl_txq_params;
	u32 changed;
	u8 retry_short = 0, retry_long = 0;
	u32 frag_threshold = 0, rts_threshold = 0;
	u8 coverage_class = 0;
	u32 txq_limit = 0, txq_memory_limit = 0, txq_quantum = 0;

	ASSERT_RTNL();

	/*
	 * Try to find the wiphy and netdev. Normally this
	 * function shouldn't need the netdev, but this is
	 * done for backward compatibility -- previously
	 * setting the channel was done per wiphy, but now
	 * it is per netdev. Previous userland like hostapd
	 * also passed a netdev to set_wiphy, so that it is
	 * possible to let that go to the right netdev!
	 */

	if (info->attrs[NL80211_ATTR_IFINDEX]) {
		int ifindex = nla_get_u32(info->attrs[NL80211_ATTR_IFINDEX]);

		netdev = __dev_get_by_index(genl_info_net(info), ifindex);
		if (netdev && netdev->ieee80211_ptr)
			rdev = wiphy_to_rdev(netdev->ieee80211_ptr->wiphy);
		else
			netdev = NULL;
	}

	if (!netdev) {
		rdev = __cfg80211_rdev_from_attrs(genl_info_net(info),
						  info->attrs);
		if (IS_ERR(rdev))
			return PTR_ERR(rdev);
		wdev = NULL;
		netdev = NULL;
		result = 0;
	} else
		wdev = netdev->ieee80211_ptr;

	/*
	 * end workaround code, by now the rdev is available
	 * and locked, and wdev may or may not be NULL.
	 */

	if (info->attrs[NL80211_ATTR_WIPHY_NAME])
		result = cfg80211_dev_rename(
			rdev, nla_data(info->attrs[NL80211_ATTR_WIPHY_NAME]));

	if (result)
		return result;

	if (info->attrs[NL80211_ATTR_WIPHY_TXQ_PARAMS]) {
		struct ieee80211_txq_params txq_params;
		struct nlattr *tb[NL80211_TXQ_ATTR_MAX + 1];

		if (!rdev->ops->set_txq_params)
			return -EOPNOTSUPP;

		if (!netdev)
			return -EINVAL;

		if (netdev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
		    netdev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
			return -EINVAL;

		if (!netif_running(netdev))
			return -ENETDOWN;

		nla_for_each_nested(nl_txq_params,
				    info->attrs[NL80211_ATTR_WIPHY_TXQ_PARAMS],
				    rem_txq_params) {
			result = nla_parse_nested_deprecated(tb,
							     NL80211_TXQ_ATTR_MAX,
							     nl_txq_params,
							     txq_params_policy,
							     info->extack);
			if (result)
				return result;
			result = parse_txq_params(tb, &txq_params);
			if (result)
				return result;

			result = rdev_set_txq_params(rdev, netdev,
						     &txq_params);
			if (result)
				return result;
		}
	}

	if (info->attrs[NL80211_ATTR_WIPHY_FREQ]) {
		result = __nl80211_set_channel(
			rdev,
			nl80211_can_set_dev_channel(wdev) ? netdev : NULL,
			info);
		if (result)
			return result;
	}

	if (info->attrs[NL80211_ATTR_WIPHY_TX_POWER_SETTING]) {
		struct wireless_dev *txp_wdev = wdev;
		enum nl80211_tx_power_setting type;
		int idx, mbm = 0;

		if (!(rdev->wiphy.features & NL80211_FEATURE_VIF_TXPOWER))
			txp_wdev = NULL;

		if (!rdev->ops->set_tx_power)
			return -EOPNOTSUPP;

		idx = NL80211_ATTR_WIPHY_TX_POWER_SETTING;
		type = nla_get_u32(info->attrs[idx]);

		if (!info->attrs[NL80211_ATTR_WIPHY_TX_POWER_LEVEL] &&
		    (type != NL80211_TX_POWER_AUTOMATIC))
			return -EINVAL;

		if (type != NL80211_TX_POWER_AUTOMATIC) {
			idx = NL80211_ATTR_WIPHY_TX_POWER_LEVEL;
			mbm = nla_get_u32(info->attrs[idx]);
		}

		result = rdev_set_tx_power(rdev, txp_wdev, type, mbm);
		if (result)
			return result;
	}

	if (info->attrs[NL80211_ATTR_WIPHY_ANTENNA_TX] &&
	    info->attrs[NL80211_ATTR_WIPHY_ANTENNA_RX]) {
		u32 tx_ant, rx_ant;

		if ((!rdev->wiphy.available_antennas_tx &&
		     !rdev->wiphy.available_antennas_rx) ||
		    !rdev->ops->set_antenna)
			return -EOPNOTSUPP;

		tx_ant = nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_ANTENNA_TX]);
		rx_ant = nla_get_u32(info->attrs[NL80211_ATTR_WIPHY_ANTENNA_RX]);

		/* reject antenna configurations which don't match the
		 * available antenna masks, except for the "all" mask */
		if ((~tx_ant && (tx_ant & ~rdev->wiphy.available_antennas_tx)) ||
		    (~rx_ant && (rx_ant & ~rdev->wiphy.available_antennas_rx)))
			return -EINVAL;

		tx_ant = tx_ant & rdev->wiphy.available_antennas_tx;
		rx_ant = rx_ant & rdev->wiphy.available_antennas_rx;

		result = rdev_set_antenna(rdev, tx_ant, rx_ant);
		if (result)
			return result;
	}

	changed = 0;

	if (info->attrs[NL80211_ATTR_WIPHY_RETRY_SHORT]) {
		retry_short = nla_get_u8(
			info->attrs[NL80211_ATTR_WIPHY_RETRY_SHORT]);

		changed |= WIPHY_PARAM_RETRY_SHORT;
	}

	if (info->attrs[NL80211_ATTR_WIPHY_RETRY_LONG]) {
		retry_long = nla_get_u8(
			info->attrs[NL80211_ATTR_WIPHY_RETRY_LONG]);

		changed |= WIPHY_PARAM_RETRY_LONG;
	}

	if (info->attrs[NL80211_ATTR_WIPHY_FRAG_THRESHOLD]) {
		frag_threshold = nla_get_u32(
			info->attrs[NL80211_ATTR_WIPHY_FRAG_THRESHOLD]);
		if (frag_threshold < 256)
			return -EINVAL;

		if (frag_threshold != (u32) -1) {
			/*
			 * Fragments (apart from the last one) are required to
			 * have even length. Make the fragmentation code
			 * simpler by stripping LSB should someone try to use
			 * odd threshold value.
			 */
			frag_threshold &= ~0x1;
		}
		changed |= WIPHY_PARAM_FRAG_THRESHOLD;
	}

	if (info->attrs[NL80211_ATTR_WIPHY_RTS_THRESHOLD]) {
		rts_threshold = nla_get_u32(
			info->attrs[NL80211_ATTR_WIPHY_RTS_THRESHOLD]);
		changed |= WIPHY_PARAM_RTS_THRESHOLD;
	}

	if (info->attrs[NL80211_ATTR_WIPHY_COVERAGE_CLASS]) {
		if (info->attrs[NL80211_ATTR_WIPHY_DYN_ACK])
			return -EINVAL;

		coverage_class = nla_get_u8(
			info->attrs[NL80211_ATTR_WIPHY_COVERAGE_CLASS]);
		changed |= WIPHY_PARAM_COVERAGE_CLASS;
	}

	if (info->attrs[NL80211_ATTR_WIPHY_DYN_ACK]) {
		if (!(rdev->wiphy.features & NL80211_FEATURE_ACKTO_ESTIMATION))
			return -EOPNOTSUPP;

		changed |= WIPHY_PARAM_DYN_ACK;
	}

	if (info->attrs[NL80211_ATTR_TXQ_LIMIT]) {
		if (!wiphy_ext_feature_isset(&rdev->wiphy,
					     NL80211_EXT_FEATURE_TXQS))
			return -EOPNOTSUPP;
		txq_limit = nla_get_u32(
			info->attrs[NL80211_ATTR_TXQ_LIMIT]);
		changed |= WIPHY_PARAM_TXQ_LIMIT;
	}

	if (info->attrs[NL80211_ATTR_TXQ_MEMORY_LIMIT]) {
		if (!wiphy_ext_feature_isset(&rdev->wiphy,
					     NL80211_EXT_FEATURE_TXQS))
			return -EOPNOTSUPP;
		txq_memory_limit = nla_get_u32(
			info->attrs[NL80211_ATTR_TXQ_MEMORY_LIMIT]);
		changed |= WIPHY_PARAM_TXQ_MEMORY_LIMIT;
	}

	if (info->attrs[NL80211_ATTR_TXQ_QUANTUM]) {
		if (!wiphy_ext_feature_isset(&rdev->wiphy,
					     NL80211_EXT_FEATURE_TXQS))
			return -EOPNOTSUPP;
		txq_quantum = nla_get_u32(
			info->attrs[NL80211_ATTR_TXQ_QUANTUM]);
		changed |= WIPHY_PARAM_TXQ_QUANTUM;
	}

	if (changed) {
		u8 old_retry_short, old_retry_long;
		u32 old_frag_threshold, old_rts_threshold;
		u8 old_coverage_class;
		u32 old_txq_limit, old_txq_memory_limit, old_txq_quantum;

		if (!rdev->ops->set_wiphy_params)
			return -EOPNOTSUPP;

		old_retry_short = rdev->wiphy.retry_short;
		old_retry_long = rdev->wiphy.retry_long;
		old_frag_threshold = rdev->wiphy.frag_threshold;
		old_rts_threshold = rdev->wiphy.rts_threshold;
		old_coverage_class = rdev->wiphy.coverage_class;
		old_txq_limit = rdev->wiphy.txq_limit;
		old_txq_memory_limit = rdev->wiphy.txq_memory_limit;
		old_txq_quantum = rdev->wiphy.txq_quantum;

		if (changed & WIPHY_PARAM_RETRY_SHORT)
			rdev->wiphy.retry_short = retry_short;
		if (changed & WIPHY_PARAM_RETRY_LONG)
			rdev->wiphy.retry_long = retry_long;
		if (changed & WIPHY_PARAM_FRAG_THRESHOLD)
			rdev->wiphy.frag_threshold = frag_threshold;
		if (changed & WIPHY_PARAM_RTS_THRESHOLD)
			rdev->wiphy.rts_threshold = rts_threshold;
		if (changed & WIPHY_PARAM_COVERAGE_CLASS)
			rdev->wiphy.coverage_class = coverage_class;
		if (changed & WIPHY_PARAM_TXQ_LIMIT)
			rdev->wiphy.txq_limit = txq_limit;
		if (changed & WIPHY_PARAM_TXQ_MEMORY_LIMIT)
			rdev->wiphy.txq_memory_limit = txq_memory_limit;
		if (changed & WIPHY_PARAM_TXQ_QUANTUM)
			rdev->wiphy.txq_quantum = txq_quantum;

		result = rdev_set_wiphy_params(rdev, changed);
		if (result) {
			rdev->wiphy.retry_short = old_retry_short;
			rdev->wiphy.retry_long = old_retry_long;
			rdev->wiphy.frag_threshold = old_frag_threshold;
			rdev->wiphy.rts_threshold = old_rts_threshold;
			rdev->wiphy.coverage_class = old_coverage_class;
			rdev->wiphy.txq_limit = old_txq_limit;
			rdev->wiphy.txq_memory_limit = old_txq_memory_limit;
			rdev->wiphy.txq_quantum = old_txq_quantum;
			return result;
		}
	}
	return 0;
}

static int nl80211_send_chandef(struct sk_buff *msg,
				const struct cfg80211_chan_def *chandef)
{
	if (WARN_ON(!cfg80211_chandef_valid(chandef)))
		return -EINVAL;

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY_FREQ,
			chandef->chan->center_freq))
		return -ENOBUFS;
	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
	case NL80211_CHAN_WIDTH_40:
		if (nla_put_u32(msg, NL80211_ATTR_WIPHY_CHANNEL_TYPE,
				cfg80211_get_chandef_type(chandef)))
			return -ENOBUFS;
		break;
	default:
		break;
	}
	if (nla_put_u32(msg, NL80211_ATTR_CHANNEL_WIDTH, chandef->width))
		return -ENOBUFS;
	if (nla_put_u32(msg, NL80211_ATTR_CENTER_FREQ1, chandef->center_freq1))
		return -ENOBUFS;
	if (chandef->center_freq2 &&
	    nla_put_u32(msg, NL80211_ATTR_CENTER_FREQ2, chandef->center_freq2))
		return -ENOBUFS;
	return 0;
}

static int nl80211_send_iface(struct sk_buff *msg, u32 portid, u32 seq, int flags,
			      struct cfg80211_registered_device *rdev,
			      struct wireless_dev *wdev,
			      enum nl80211_commands cmd)
{
	struct net_device *dev = wdev->netdev;
	void *hdr;

	WARN_ON(cmd != NL80211_CMD_NEW_INTERFACE &&
		cmd != NL80211_CMD_DEL_INTERFACE &&
		cmd != NL80211_CMD_SET_INTERFACE);

	hdr = nl80211hdr_put(msg, portid, seq, flags, cmd);
	if (!hdr)
		return -1;

	if (dev &&
	    (nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex) ||
	     nla_put_string(msg, NL80211_ATTR_IFNAME, dev->name)))
		goto nla_put_failure;

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFTYPE, wdev->iftype) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_WDEV, wdev_id(wdev),
			      NL80211_ATTR_PAD) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, wdev_address(wdev)) ||
	    nla_put_u32(msg, NL80211_ATTR_GENERATION,
			rdev->devlist_generation ^
			(cfg80211_rdev_list_generation << 2)) ||
	    nla_put_u8(msg, NL80211_ATTR_4ADDR, wdev->use_4addr))
		goto nla_put_failure;

	if (rdev->ops->get_channel) {
		int ret;
		struct cfg80211_chan_def chandef;

		ret = rdev_get_channel(rdev, wdev, &chandef);
		if (ret == 0) {
			if (nl80211_send_chandef(msg, &chandef))
				goto nla_put_failure;
		}
	}

	if (rdev->ops->get_tx_power) {
		int dbm, ret;

		ret = rdev_get_tx_power(rdev, wdev, &dbm);
		if (ret == 0 &&
		    nla_put_u32(msg, NL80211_ATTR_WIPHY_TX_POWER_LEVEL,
				DBM_TO_MBM(dbm)))
			goto nla_put_failure;
	}

	wdev_lock(wdev);
	switch (wdev->iftype) {
	case NL80211_IFTYPE_AP:
		if (wdev->ssid_len &&
		    nla_put(msg, NL80211_ATTR_SSID, wdev->ssid_len, wdev->ssid))
			goto nla_put_failure_locked;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_ADHOC: {
		const u8 *ssid_ie;
		if (!wdev->current_bss)
			break;
		rcu_read_lock();
		ssid_ie = ieee80211_bss_get_ie(&wdev->current_bss->pub,
					       WLAN_EID_SSID);
		if (ssid_ie &&
		    nla_put(msg, NL80211_ATTR_SSID, ssid_ie[1], ssid_ie + 2))
			goto nla_put_failure_rcu_locked;
		rcu_read_unlock();
		break;
		}
	default:
		/* nothing */
		break;
	}
	wdev_unlock(wdev);

	if (rdev->ops->get_txq_stats) {
		struct cfg80211_txq_stats txqstats = {};
		int ret = rdev_get_txq_stats(rdev, wdev, &txqstats);

		if (ret == 0 &&
		    !nl80211_put_txq_stats(msg, &txqstats,
					   NL80211_ATTR_TXQ_STATS))
			goto nla_put_failure;
	}

	genlmsg_end(msg, hdr);
	return 0;

 nla_put_failure_rcu_locked:
	rcu_read_unlock();
 nla_put_failure_locked:
	wdev_unlock(wdev);
 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nl80211_dump_interface(struct sk_buff *skb, struct netlink_callback *cb)
{
	int wp_idx = 0;
	int if_idx = 0;
	int wp_start = cb->args[0];
	int if_start = cb->args[1];
	int filter_wiphy = -1;
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *wdev;
	int ret;

	rtnl_lock();
	if (!cb->args[2]) {
		struct nl80211_dump_wiphy_state state = {
			.filter_wiphy = -1,
		};

		ret = nl80211_dump_wiphy_parse(skb, cb, &state);
		if (ret)
			goto out_unlock;

		filter_wiphy = state.filter_wiphy;

		/*
		 * if filtering, set cb->args[2] to +1 since 0 is the default
		 * value needed to determine that parsing is necessary.
		 */
		if (filter_wiphy >= 0)
			cb->args[2] = filter_wiphy + 1;
		else
			cb->args[2] = -1;
	} else if (cb->args[2] > 0) {
		filter_wiphy = cb->args[2] - 1;
	}

	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		if (!net_eq(wiphy_net(&rdev->wiphy), sock_net(skb->sk)))
			continue;
		if (wp_idx < wp_start) {
			wp_idx++;
			continue;
		}

		if (filter_wiphy >= 0 && filter_wiphy != rdev->wiphy_idx)
			continue;

		if_idx = 0;

		list_for_each_entry(wdev, &rdev->wiphy.wdev_list, list) {
			if (if_idx < if_start) {
				if_idx++;
				continue;
			}
			if (nl80211_send_iface(skb, NETLINK_CB(cb->skb).portid,
					       cb->nlh->nlmsg_seq, NLM_F_MULTI,
					       rdev, wdev,
					       NL80211_CMD_NEW_INTERFACE) < 0) {
				goto out;
			}
			if_idx++;
		}

		wp_idx++;
	}
 out:
	cb->args[0] = wp_idx;
	cb->args[1] = if_idx;

	ret = skb->len;
 out_unlock:
	rtnl_unlock();

	return ret;
}

static int nl80211_get_interface(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	if (nl80211_send_iface(msg, info->snd_portid, info->snd_seq, 0,
			       rdev, wdev, NL80211_CMD_NEW_INTERFACE) < 0) {
		nlmsg_free(msg);
		return -ENOBUFS;
	}

	return genlmsg_reply(msg, info);
}

static const struct nla_policy mntr_flags_policy[NL80211_MNTR_FLAG_MAX + 1] = {
	[NL80211_MNTR_FLAG_FCSFAIL] = { .type = NLA_FLAG },
	[NL80211_MNTR_FLAG_PLCPFAIL] = { .type = NLA_FLAG },
	[NL80211_MNTR_FLAG_CONTROL] = { .type = NLA_FLAG },
	[NL80211_MNTR_FLAG_OTHER_BSS] = { .type = NLA_FLAG },
	[NL80211_MNTR_FLAG_COOK_FRAMES] = { .type = NLA_FLAG },
	[NL80211_MNTR_FLAG_ACTIVE] = { .type = NLA_FLAG },
};

static int parse_monitor_flags(struct nlattr *nla, u32 *mntrflags)
{
	struct nlattr *flags[NL80211_MNTR_FLAG_MAX + 1];
	int flag;

	*mntrflags = 0;

	if (!nla)
		return -EINVAL;

	if (nla_parse_nested_deprecated(flags, NL80211_MNTR_FLAG_MAX, nla, mntr_flags_policy, NULL))
		return -EINVAL;

	for (flag = 1; flag <= NL80211_MNTR_FLAG_MAX; flag++)
		if (flags[flag])
			*mntrflags |= (1<<flag);

	*mntrflags |= MONITOR_FLAG_CHANGED;

	return 0;
}

static int nl80211_parse_mon_options(struct cfg80211_registered_device *rdev,
				     enum nl80211_iftype type,
				     struct genl_info *info,
				     struct vif_params *params)
{
	bool change = false;
	int err;

	if (info->attrs[NL80211_ATTR_MNTR_FLAGS]) {
		if (type != NL80211_IFTYPE_MONITOR)
			return -EINVAL;

		err = parse_monitor_flags(info->attrs[NL80211_ATTR_MNTR_FLAGS],
					  &params->flags);
		if (err)
			return err;

		change = true;
	}

	if (params->flags & MONITOR_FLAG_ACTIVE &&
	    !(rdev->wiphy.features & NL80211_FEATURE_ACTIVE_MONITOR))
		return -EOPNOTSUPP;

	if (info->attrs[NL80211_ATTR_MU_MIMO_GROUP_DATA]) {
		const u8 *mumimo_groups;
		u32 cap_flag = NL80211_EXT_FEATURE_MU_MIMO_AIR_SNIFFER;

		if (type != NL80211_IFTYPE_MONITOR)
			return -EINVAL;

		if (!wiphy_ext_feature_isset(&rdev->wiphy, cap_flag))
			return -EOPNOTSUPP;

		mumimo_groups =
			nla_data(info->attrs[NL80211_ATTR_MU_MIMO_GROUP_DATA]);

		/* bits 0 and 63 are reserved and must be zero */
		if ((mumimo_groups[0] & BIT(0)) ||
		    (mumimo_groups[VHT_MUMIMO_GROUPS_DATA_LEN - 1] & BIT(7)))
			return -EINVAL;

		params->vht_mumimo_groups = mumimo_groups;
		change = true;
	}

	if (info->attrs[NL80211_ATTR_MU_MIMO_FOLLOW_MAC_ADDR]) {
		u32 cap_flag = NL80211_EXT_FEATURE_MU_MIMO_AIR_SNIFFER;

		if (type != NL80211_IFTYPE_MONITOR)
			return -EINVAL;

		if (!wiphy_ext_feature_isset(&rdev->wiphy, cap_flag))
			return -EOPNOTSUPP;

		params->vht_mumimo_follow_addr =
			nla_data(info->attrs[NL80211_ATTR_MU_MIMO_FOLLOW_MAC_ADDR]);
		change = true;
	}

	return change ? 1 : 0;
}

static int nl80211_valid_4addr(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev, u8 use_4addr,
			       enum nl80211_iftype iftype)
{
	if (!use_4addr) {
		if (netdev && (netdev->priv_flags & IFF_BRIDGE_PORT))
			return -EBUSY;
		return 0;
	}

	switch (iftype) {
	case NL80211_IFTYPE_AP_VLAN:
		if (rdev->wiphy.flags & WIPHY_FLAG_4ADDR_AP)
			return 0;
		break;
	case NL80211_IFTYPE_STATION:
		if (rdev->wiphy.flags & WIPHY_FLAG_4ADDR_STATION)
			return 0;
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int nl80211_set_interface(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct vif_params params;
	int err;
	enum nl80211_iftype otype, ntype;
	struct net_device *dev = info->user_ptr[1];
	bool change = false;

	memset(&params, 0, sizeof(params));

	otype = ntype = dev->ieee80211_ptr->iftype;

	if (info->attrs[NL80211_ATTR_IFTYPE]) {
		ntype = nla_get_u32(info->attrs[NL80211_ATTR_IFTYPE]);
		if (otype != ntype)
			change = true;
	}

	if (info->attrs[NL80211_ATTR_MESH_ID]) {
		struct wireless_dev *wdev = dev->ieee80211_ptr;

		if (ntype != NL80211_IFTYPE_MESH_POINT)
			return -EINVAL;
		if (netif_running(dev))
			return -EBUSY;

		wdev_lock(wdev);
		BUILD_BUG_ON(IEEE80211_MAX_SSID_LEN !=
			     IEEE80211_MAX_MESH_ID_LEN);
		wdev->mesh_id_up_len =
			nla_len(info->attrs[NL80211_ATTR_MESH_ID]);
		memcpy(wdev->ssid, nla_data(info->attrs[NL80211_ATTR_MESH_ID]),
		       wdev->mesh_id_up_len);
		wdev_unlock(wdev);
	}

	if (info->attrs[NL80211_ATTR_4ADDR]) {
		params.use_4addr = !!nla_get_u8(info->attrs[NL80211_ATTR_4ADDR]);
		change = true;
		err = nl80211_valid_4addr(rdev, dev, params.use_4addr, ntype);
		if (err)
			return err;
	} else {
		params.use_4addr = -1;
	}

	err = nl80211_parse_mon_options(rdev, ntype, info, &params);
	if (err < 0)
		return err;
	if (err > 0)
		change = true;

	if (change)
		err = cfg80211_change_iface(rdev, dev, ntype, &params);
	else
		err = 0;

	if (!err && params.use_4addr != -1)
		dev->ieee80211_ptr->use_4addr = params.use_4addr;

	if (change && !err) {
		struct wireless_dev *wdev = dev->ieee80211_ptr;

		nl80211_notify_iface(rdev, wdev, NL80211_CMD_SET_INTERFACE);
	}

	return err;
}

static int nl80211_new_interface(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct vif_params params;
	struct wireless_dev *wdev;
	struct sk_buff *msg;
	int err;
	enum nl80211_iftype type = NL80211_IFTYPE_UNSPECIFIED;

	/* to avoid failing a new interface creation due to pending removal */
	cfg80211_destroy_ifaces(rdev);

	memset(&params, 0, sizeof(params));

	if (!info->attrs[NL80211_ATTR_IFNAME])
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_IFTYPE])
		type = nla_get_u32(info->attrs[NL80211_ATTR_IFTYPE]);

	if (!rdev->ops->add_virtual_intf)
		return -EOPNOTSUPP;

	if ((type == NL80211_IFTYPE_P2P_DEVICE || type == NL80211_IFTYPE_NAN ||
	     rdev->wiphy.features & NL80211_FEATURE_MAC_ON_CREATE) &&
	    info->attrs[NL80211_ATTR_MAC]) {
		nla_memcpy(params.macaddr, info->attrs[NL80211_ATTR_MAC],
			   ETH_ALEN);
		if (!is_valid_ether_addr(params.macaddr))
			return -EADDRNOTAVAIL;
	}

	if (info->attrs[NL80211_ATTR_4ADDR]) {
		params.use_4addr = !!nla_get_u8(info->attrs[NL80211_ATTR_4ADDR]);
		err = nl80211_valid_4addr(rdev, NULL, params.use_4addr, type);
		if (err)
			return err;
	}

	if (!cfg80211_iftype_allowed(&rdev->wiphy, type, params.use_4addr, 0))
		return -EOPNOTSUPP;

	err = nl80211_parse_mon_options(rdev, type, info, &params);
	if (err < 0)
		return err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	wdev = rdev_add_virtual_intf(rdev,
				nla_data(info->attrs[NL80211_ATTR_IFNAME]),
				NET_NAME_USER, type, &params);
	if (WARN_ON(!wdev)) {
		nlmsg_free(msg);
		return -EPROTO;
	} else if (IS_ERR(wdev)) {
		nlmsg_free(msg);
		return PTR_ERR(wdev);
	}

	if (info->attrs[NL80211_ATTR_SOCKET_OWNER])
		wdev->owner_nlportid = info->snd_portid;

	switch (type) {
	case NL80211_IFTYPE_MESH_POINT:
		if (!info->attrs[NL80211_ATTR_MESH_ID])
			break;
		wdev_lock(wdev);
		BUILD_BUG_ON(IEEE80211_MAX_SSID_LEN !=
			     IEEE80211_MAX_MESH_ID_LEN);
		wdev->mesh_id_up_len =
			nla_len(info->attrs[NL80211_ATTR_MESH_ID]);
		memcpy(wdev->ssid, nla_data(info->attrs[NL80211_ATTR_MESH_ID]),
		       wdev->mesh_id_up_len);
		wdev_unlock(wdev);
		break;
	case NL80211_IFTYPE_NAN:
	case NL80211_IFTYPE_P2P_DEVICE:
		/*
		 * P2P Device and NAN do not have a netdev, so don't go
		 * through the netdev notifier and must be added here
		 */
		cfg80211_init_wdev(rdev, wdev);
		break;
	default:
		break;
	}

	if (nl80211_send_iface(msg, info->snd_portid, info->snd_seq, 0,
			       rdev, wdev, NL80211_CMD_NEW_INTERFACE) < 0) {
		nlmsg_free(msg);
		return -ENOBUFS;
	}

	return genlmsg_reply(msg, info);
}

static int nl80211_del_interface(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];

	if (!rdev->ops->del_virtual_intf)
		return -EOPNOTSUPP;

	/*
	 * If we remove a wireless device without a netdev then clear
	 * user_ptr[1] so that nl80211_post_doit won't dereference it
	 * to check if it needs to do dev_put(). Otherwise it crashes
	 * since the wdev has been freed, unlike with a netdev where
	 * we need the dev_put() for the netdev to really be freed.
	 */
	if (!wdev->netdev)
		info->user_ptr[1] = NULL;

	return rdev_del_virtual_intf(rdev, wdev);
}

static int nl80211_set_noack_map(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	u16 noack_map;

	if (!info->attrs[NL80211_ATTR_NOACK_MAP])
		return -EINVAL;

	if (!rdev->ops->set_noack_map)
		return -EOPNOTSUPP;

	noack_map = nla_get_u16(info->attrs[NL80211_ATTR_NOACK_MAP]);

	return rdev_set_noack_map(rdev, dev, noack_map);
}

struct get_key_cookie {
	struct sk_buff *msg;
	int error;
	int idx;
};

static void get_key_callback(void *c, struct key_params *params)
{
	struct nlattr *key;
	struct get_key_cookie *cookie = c;

	if ((params->key &&
	     nla_put(cookie->msg, NL80211_ATTR_KEY_DATA,
		     params->key_len, params->key)) ||
	    (params->seq &&
	     nla_put(cookie->msg, NL80211_ATTR_KEY_SEQ,
		     params->seq_len, params->seq)) ||
	    (params->cipher &&
	     nla_put_u32(cookie->msg, NL80211_ATTR_KEY_CIPHER,
			 params->cipher)))
		goto nla_put_failure;

	key = nla_nest_start_noflag(cookie->msg, NL80211_ATTR_KEY);
	if (!key)
		goto nla_put_failure;

	if ((params->key &&
	     nla_put(cookie->msg, NL80211_KEY_DATA,
		     params->key_len, params->key)) ||
	    (params->seq &&
	     nla_put(cookie->msg, NL80211_KEY_SEQ,
		     params->seq_len, params->seq)) ||
	    (params->cipher &&
	     nla_put_u32(cookie->msg, NL80211_KEY_CIPHER,
			 params->cipher)))
		goto nla_put_failure;

	if (nla_put_u8(cookie->msg, NL80211_KEY_IDX, cookie->idx))
		goto nla_put_failure;

	nla_nest_end(cookie->msg, key);

	return;
 nla_put_failure:
	cookie->error = 1;
}

static int nl80211_get_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	int err;
	struct net_device *dev = info->user_ptr[1];
	u8 key_idx = 0;
	const u8 *mac_addr = NULL;
	bool pairwise;
	struct get_key_cookie cookie = {
		.error = 0,
	};
	void *hdr;
	struct sk_buff *msg;

	if (info->attrs[NL80211_ATTR_KEY_IDX])
		key_idx = nla_get_u8(info->attrs[NL80211_ATTR_KEY_IDX]);

	if (info->attrs[NL80211_ATTR_MAC])
		mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	pairwise = !!mac_addr;
	if (info->attrs[NL80211_ATTR_KEY_TYPE]) {
		u32 kt = nla_get_u32(info->attrs[NL80211_ATTR_KEY_TYPE]);

		if (kt != NL80211_KEYTYPE_GROUP &&
		    kt != NL80211_KEYTYPE_PAIRWISE)
			return -EINVAL;
		pairwise = kt == NL80211_KEYTYPE_PAIRWISE;
	}

	if (!rdev->ops->get_key)
		return -EOPNOTSUPP;

	if (!pairwise && mac_addr && !(rdev->wiphy.flags & WIPHY_FLAG_IBSS_RSN))
		return -ENOENT;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = nl80211hdr_put(msg, info->snd_portid, info->snd_seq, 0,
			     NL80211_CMD_NEW_KEY);
	if (!hdr)
		goto nla_put_failure;

	cookie.msg = msg;
	cookie.idx = key_idx;

	if (nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex) ||
	    nla_put_u8(msg, NL80211_ATTR_KEY_IDX, key_idx))
		goto nla_put_failure;
	if (mac_addr &&
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, mac_addr))
		goto nla_put_failure;

	err = rdev_get_key(rdev, dev, key_idx, pairwise, mac_addr, &cookie,
			   get_key_callback);

	if (err)
		goto free_msg;

	if (cookie.error)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return genlmsg_reply(msg, info);

 nla_put_failure:
	err = -ENOBUFS;
 free_msg:
	nlmsg_free(msg);
	return err;
}

static int nl80211_set_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct key_parse key;
	int err;
	struct net_device *dev = info->user_ptr[1];

	err = nl80211_parse_key(info, &key);
	if (err)
		return err;

	if (key.idx < 0)
		return -EINVAL;

	/* Only support setting default key and
	 * Extended Key ID action NL80211_KEY_SET_TX.
	 */
	if (!key.def && !key.defmgmt &&
	    !(key.p.mode == NL80211_KEY_SET_TX))
		return -EINVAL;

	wdev_lock(dev->ieee80211_ptr);

	if (key.def) {
		if (!rdev->ops->set_default_key) {
			err = -EOPNOTSUPP;
			goto out;
		}

		err = nl80211_key_allowed(dev->ieee80211_ptr);
		if (err)
			goto out;

		err = rdev_set_default_key(rdev, dev, key.idx,
						 key.def_uni, key.def_multi);

		if (err)
			goto out;

#ifdef CONFIG_CFG80211_WEXT
		dev->ieee80211_ptr->wext.default_key = key.idx;
#endif
	} else if (key.defmgmt) {
		if (key.def_uni || !key.def_multi) {
			err = -EINVAL;
			goto out;
		}

		if (!rdev->ops->set_default_mgmt_key) {
			err = -EOPNOTSUPP;
			goto out;
		}

		err = nl80211_key_allowed(dev->ieee80211_ptr);
		if (err)
			goto out;

		err = rdev_set_default_mgmt_key(rdev, dev, key.idx);
		if (err)
			goto out;

#ifdef CONFIG_CFG80211_WEXT
		dev->ieee80211_ptr->wext.default_mgmt_key = key.idx;
#endif
	} else if (key.p.mode == NL80211_KEY_SET_TX &&
		   wiphy_ext_feature_isset(&rdev->wiphy,
					   NL80211_EXT_FEATURE_EXT_KEY_ID)) {
		u8 *mac_addr = NULL;

		if (info->attrs[NL80211_ATTR_MAC])
			mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

		if (!mac_addr || key.idx < 0 || key.idx > 1) {
			err = -EINVAL;
			goto out;
		}

		err = rdev_add_key(rdev, dev, key.idx,
				   NL80211_KEYTYPE_PAIRWISE,
				   mac_addr, &key.p);
	} else {
		err = -EINVAL;
	}
 out:
	wdev_unlock(dev->ieee80211_ptr);

	return err;
}

static int nl80211_new_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	int err;
	struct net_device *dev = info->user_ptr[1];
	struct key_parse key;
	const u8 *mac_addr = NULL;

	err = nl80211_parse_key(info, &key);
	if (err)
		return err;

	if (!key.p.key)
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_MAC])
		mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (key.type == -1) {
		if (mac_addr)
			key.type = NL80211_KEYTYPE_PAIRWISE;
		else
			key.type = NL80211_KEYTYPE_GROUP;
	}

	/* for now */
	if (key.type != NL80211_KEYTYPE_PAIRWISE &&
	    key.type != NL80211_KEYTYPE_GROUP)
		return -EINVAL;

	if (!rdev->ops->add_key)
		return -EOPNOTSUPP;

	if (cfg80211_validate_key_settings(rdev, &key.p, key.idx,
					   key.type == NL80211_KEYTYPE_PAIRWISE,
					   mac_addr))
		return -EINVAL;

	wdev_lock(dev->ieee80211_ptr);
	err = nl80211_key_allowed(dev->ieee80211_ptr);
	if (!err)
		err = rdev_add_key(rdev, dev, key.idx,
				   key.type == NL80211_KEYTYPE_PAIRWISE,
				    mac_addr, &key.p);
	wdev_unlock(dev->ieee80211_ptr);

	return err;
}

static int nl80211_del_key(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	int err;
	struct net_device *dev = info->user_ptr[1];
	u8 *mac_addr = NULL;
	struct key_parse key;

	err = nl80211_parse_key(info, &key);
	if (err)
		return err;

	if (info->attrs[NL80211_ATTR_MAC])
		mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (key.type == -1) {
		if (mac_addr)
			key.type = NL80211_KEYTYPE_PAIRWISE;
		else
			key.type = NL80211_KEYTYPE_GROUP;
	}

	/* for now */
	if (key.type != NL80211_KEYTYPE_PAIRWISE &&
	    key.type != NL80211_KEYTYPE_GROUP)
		return -EINVAL;

	if (!rdev->ops->del_key)
		return -EOPNOTSUPP;

	wdev_lock(dev->ieee80211_ptr);
	err = nl80211_key_allowed(dev->ieee80211_ptr);

	if (key.type == NL80211_KEYTYPE_GROUP && mac_addr &&
	    !(rdev->wiphy.flags & WIPHY_FLAG_IBSS_RSN))
		err = -ENOENT;

	if (!err)
		err = rdev_del_key(rdev, dev, key.idx,
				   key.type == NL80211_KEYTYPE_PAIRWISE,
				   mac_addr);

#ifdef CONFIG_CFG80211_WEXT
	if (!err) {
		if (key.idx == dev->ieee80211_ptr->wext.default_key)
			dev->ieee80211_ptr->wext.default_key = -1;
		else if (key.idx == dev->ieee80211_ptr->wext.default_mgmt_key)
			dev->ieee80211_ptr->wext.default_mgmt_key = -1;
	}
#endif
	wdev_unlock(dev->ieee80211_ptr);

	return err;
}

/* This function returns an error or the number of nested attributes */
static int validate_acl_mac_addrs(struct nlattr *nl_attr)
{
	struct nlattr *attr;
	int n_entries = 0, tmp;

	nla_for_each_nested(attr, nl_attr, tmp) {
		if (nla_len(attr) != ETH_ALEN)
			return -EINVAL;

		n_entries++;
	}

	return n_entries;
}

/*
 * This function parses ACL information and allocates memory for ACL data.
 * On successful return, the calling function is responsible to free the
 * ACL buffer returned by this function.
 */
static struct cfg80211_acl_data *parse_acl_data(struct wiphy *wiphy,
						struct genl_info *info)
{
	enum nl80211_acl_policy acl_policy;
	struct nlattr *attr;
	struct cfg80211_acl_data *acl;
	int i = 0, n_entries, tmp;

	if (!wiphy->max_acl_mac_addrs)
		return ERR_PTR(-EOPNOTSUPP);

	if (!info->attrs[NL80211_ATTR_ACL_POLICY])
		return ERR_PTR(-EINVAL);

	acl_policy = nla_get_u32(info->attrs[NL80211_ATTR_ACL_POLICY]);
	if (acl_policy != NL80211_ACL_POLICY_ACCEPT_UNLESS_LISTED &&
	    acl_policy != NL80211_ACL_POLICY_DENY_UNLESS_LISTED)
		return ERR_PTR(-EINVAL);

	if (!info->attrs[NL80211_ATTR_MAC_ADDRS])
		return ERR_PTR(-EINVAL);

	n_entries = validate_acl_mac_addrs(info->attrs[NL80211_ATTR_MAC_ADDRS]);
	if (n_entries < 0)
		return ERR_PTR(n_entries);

	if (n_entries > wiphy->max_acl_mac_addrs)
		return ERR_PTR(-ENOTSUPP);

	acl = kzalloc(struct_size(acl, mac_addrs, n_entries), GFP_KERNEL);
	if (!acl)
		return ERR_PTR(-ENOMEM);

	nla_for_each_nested(attr, info->attrs[NL80211_ATTR_MAC_ADDRS], tmp) {
		memcpy(acl->mac_addrs[i].addr, nla_data(attr), ETH_ALEN);
		i++;
	}

	acl->n_acl_entries = n_entries;
	acl->acl_policy = acl_policy;

	return acl;
}

static int nl80211_set_mac_acl(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct cfg80211_acl_data *acl;
	int err;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
		return -EOPNOTSUPP;

	if (!dev->ieee80211_ptr->beacon_interval)
		return -EINVAL;

	acl = parse_acl_data(&rdev->wiphy, info);
	if (IS_ERR(acl))
		return PTR_ERR(acl);

	err = rdev_set_mac_acl(rdev, dev, acl);

	kfree(acl);

	return err;
}

static u32 rateset_to_mask(struct ieee80211_supported_band *sband,
			   u8 *rates, u8 rates_len)
{
	u8 i;
	u32 mask = 0;

	for (i = 0; i < rates_len; i++) {
		int rate = (rates[i] & 0x7f) * 5;
		int ridx;

		for (ridx = 0; ridx < sband->n_bitrates; ridx++) {
			struct ieee80211_rate *srate =
				&sband->bitrates[ridx];
			if (rate == srate->bitrate) {
				mask |= 1 << ridx;
				break;
			}
		}
		if (ridx == sband->n_bitrates)
			return 0; /* rate not found */
	}

	return mask;
}

static bool ht_rateset_to_mask(struct ieee80211_supported_band *sband,
			       u8 *rates, u8 rates_len,
			       u8 mcs[IEEE80211_HT_MCS_MASK_LEN])
{
	u8 i;

	memset(mcs, 0, IEEE80211_HT_MCS_MASK_LEN);

	for (i = 0; i < rates_len; i++) {
		int ridx, rbit;

		ridx = rates[i] / 8;
		rbit = BIT(rates[i] % 8);

		/* check validity */
		if ((ridx < 0) || (ridx >= IEEE80211_HT_MCS_MASK_LEN))
			return false;

		/* check availability */
		ridx = array_index_nospec(ridx, IEEE80211_HT_MCS_MASK_LEN);
		if (sband->ht_cap.mcs.rx_mask[ridx] & rbit)
			mcs[ridx] |= rbit;
		else
			return false;
	}

	return true;
}

static u16 vht_mcs_map_to_mcs_mask(u8 vht_mcs_map)
{
	u16 mcs_mask = 0;

	switch (vht_mcs_map) {
	case IEEE80211_VHT_MCS_NOT_SUPPORTED:
		break;
	case IEEE80211_VHT_MCS_SUPPORT_0_7:
		mcs_mask = 0x00FF;
		break;
	case IEEE80211_VHT_MCS_SUPPORT_0_8:
		mcs_mask = 0x01FF;
		break;
	case IEEE80211_VHT_MCS_SUPPORT_0_9:
		mcs_mask = 0x03FF;
		break;
	default:
		break;
	}

	return mcs_mask;
}

static void vht_build_mcs_mask(u16 vht_mcs_map,
			       u16 vht_mcs_mask[NL80211_VHT_NSS_MAX])
{
	u8 nss;

	for (nss = 0; nss < NL80211_VHT_NSS_MAX; nss++) {
		vht_mcs_mask[nss] = vht_mcs_map_to_mcs_mask(vht_mcs_map & 0x03);
		vht_mcs_map >>= 2;
	}
}

static bool vht_set_mcs_mask(struct ieee80211_supported_band *sband,
			     struct nl80211_txrate_vht *txrate,
			     u16 mcs[NL80211_VHT_NSS_MAX])
{
	u16 tx_mcs_map = le16_to_cpu(sband->vht_cap.vht_mcs.tx_mcs_map);
	u16 tx_mcs_mask[NL80211_VHT_NSS_MAX] = {};
	u8 i;

	if (!sband->vht_cap.vht_supported)
		return false;

	memset(mcs, 0, sizeof(u16) * NL80211_VHT_NSS_MAX);

	/* Build vht_mcs_mask from VHT capabilities */
	vht_build_mcs_mask(tx_mcs_map, tx_mcs_mask);

	for (i = 0; i < NL80211_VHT_NSS_MAX; i++) {
		if ((tx_mcs_mask[i] & txrate->mcs[i]) == txrate->mcs[i])
			mcs[i] = txrate->mcs[i];
		else
			return false;
	}

	return true;
}

static const struct nla_policy nl80211_txattr_policy[NL80211_TXRATE_MAX + 1] = {
	[NL80211_TXRATE_LEGACY] = { .type = NLA_BINARY,
				    .len = NL80211_MAX_SUPP_RATES },
	[NL80211_TXRATE_HT] = { .type = NLA_BINARY,
				.len = NL80211_MAX_SUPP_HT_RATES },
	[NL80211_TXRATE_VHT] = {
		.type = NLA_EXACT_LEN_WARN,
		.len = sizeof(struct nl80211_txrate_vht),
	},
	[NL80211_TXRATE_GI] = { .type = NLA_U8 },
};

static int nl80211_parse_tx_bitrate_mask(struct genl_info *info,
					 struct cfg80211_bitrate_mask *mask)
{
	struct nlattr *tb[NL80211_TXRATE_MAX + 1];
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	int rem, i;
	struct nlattr *tx_rates;
	struct ieee80211_supported_band *sband;
	u16 vht_tx_mcs_map;

	memset(mask, 0, sizeof(*mask));
	/* Default to all rates enabled */
	for (i = 0; i < NUM_NL80211_BANDS; i++) {
		sband = rdev->wiphy.bands[i];

		if (!sband)
			continue;

		mask->control[i].legacy = (1 << sband->n_bitrates) - 1;
		memcpy(mask->control[i].ht_mcs,
		       sband->ht_cap.mcs.rx_mask,
		       sizeof(mask->control[i].ht_mcs));

		if (!sband->vht_cap.vht_supported)
			continue;

		vht_tx_mcs_map = le16_to_cpu(sband->vht_cap.vht_mcs.tx_mcs_map);
		vht_build_mcs_mask(vht_tx_mcs_map, mask->control[i].vht_mcs);
	}

	/* if no rates are given set it back to the defaults */
	if (!info->attrs[NL80211_ATTR_TX_RATES])
		goto out;

	/* The nested attribute uses enum nl80211_band as the index. This maps
	 * directly to the enum nl80211_band values used in cfg80211.
	 */
	BUILD_BUG_ON(NL80211_MAX_SUPP_HT_RATES > IEEE80211_HT_MCS_MASK_LEN * 8);
	nla_for_each_nested(tx_rates, info->attrs[NL80211_ATTR_TX_RATES], rem) {
		enum nl80211_band band = nla_type(tx_rates);
		int err;

		if (band < 0 || band >= NUM_NL80211_BANDS)
			return -EINVAL;
		sband = rdev->wiphy.bands[band];
		if (sband == NULL)
			return -EINVAL;
		err = nla_parse_nested_deprecated(tb, NL80211_TXRATE_MAX,
						  tx_rates,
						  nl80211_txattr_policy,
						  info->extack);
		if (err)
			return err;
		if (tb[NL80211_TXRATE_LEGACY]) {
			mask->control[band].legacy = rateset_to_mask(
				sband,
				nla_data(tb[NL80211_TXRATE_LEGACY]),
				nla_len(tb[NL80211_TXRATE_LEGACY]));
			if ((mask->control[band].legacy == 0) &&
			    nla_len(tb[NL80211_TXRATE_LEGACY]))
				return -EINVAL;
		}
		if (tb[NL80211_TXRATE_HT]) {
			if (!ht_rateset_to_mask(
					sband,
					nla_data(tb[NL80211_TXRATE_HT]),
					nla_len(tb[NL80211_TXRATE_HT]),
					mask->control[band].ht_mcs))
				return -EINVAL;
		}
		if (tb[NL80211_TXRATE_VHT]) {
			if (!vht_set_mcs_mask(
					sband,
					nla_data(tb[NL80211_TXRATE_VHT]),
					mask->control[band].vht_mcs))
				return -EINVAL;
		}
		if (tb[NL80211_TXRATE_GI]) {
			mask->control[band].gi =
				nla_get_u8(tb[NL80211_TXRATE_GI]);
			if (mask->control[band].gi > NL80211_TXRATE_FORCE_LGI)
				return -EINVAL;
		}

		if (mask->control[band].legacy == 0) {
			/* don't allow empty legacy rates if HT or VHT
			 * are not even supported.
			 */
			if (!(rdev->wiphy.bands[band]->ht_cap.ht_supported ||
			      rdev->wiphy.bands[band]->vht_cap.vht_supported))
				return -EINVAL;

			for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++)
				if (mask->control[band].ht_mcs[i])
					goto out;

			for (i = 0; i < NL80211_VHT_NSS_MAX; i++)
				if (mask->control[band].vht_mcs[i])
					goto out;

			/* legacy and mcs rates may not be both empty */
			return -EINVAL;
		}
	}

out:
	return 0;
}

static int validate_beacon_tx_rate(struct cfg80211_registered_device *rdev,
				   enum nl80211_band band,
				   struct cfg80211_bitrate_mask *beacon_rate)
{
	u32 count_ht, count_vht, i;
	u32 rate = beacon_rate->control[band].legacy;

	/* Allow only one rate */
	if (hweight32(rate) > 1)
		return -EINVAL;

	count_ht = 0;
	for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++) {
		if (hweight8(beacon_rate->control[band].ht_mcs[i]) > 1) {
			return -EINVAL;
		} else if (beacon_rate->control[band].ht_mcs[i]) {
			count_ht++;
			if (count_ht > 1)
				return -EINVAL;
		}
		if (count_ht && rate)
			return -EINVAL;
	}

	count_vht = 0;
	for (i = 0; i < NL80211_VHT_NSS_MAX; i++) {
		if (hweight16(beacon_rate->control[band].vht_mcs[i]) > 1) {
			return -EINVAL;
		} else if (beacon_rate->control[band].vht_mcs[i]) {
			count_vht++;
			if (count_vht > 1)
				return -EINVAL;
		}
		if (count_vht && rate)
			return -EINVAL;
	}

	if ((count_ht && count_vht) || (!rate && !count_ht && !count_vht))
		return -EINVAL;

	if (rate &&
	    !wiphy_ext_feature_isset(&rdev->wiphy,
				     NL80211_EXT_FEATURE_BEACON_RATE_LEGACY))
		return -EINVAL;
	if (count_ht &&
	    !wiphy_ext_feature_isset(&rdev->wiphy,
				     NL80211_EXT_FEATURE_BEACON_RATE_HT))
		return -EINVAL;
	if (count_vht &&
	    !wiphy_ext_feature_isset(&rdev->wiphy,
				     NL80211_EXT_FEATURE_BEACON_RATE_VHT))
		return -EINVAL;

	return 0;
}

static int nl80211_parse_beacon(struct cfg80211_registered_device *rdev,
				struct nlattr *attrs[],
				struct cfg80211_beacon_data *bcn)
{
	bool haveinfo = false;
	int err;

	memset(bcn, 0, sizeof(*bcn));

	if (attrs[NL80211_ATTR_BEACON_HEAD]) {
		bcn->head = nla_data(attrs[NL80211_ATTR_BEACON_HEAD]);
		bcn->head_len = nla_len(attrs[NL80211_ATTR_BEACON_HEAD]);
		if (!bcn->head_len)
			return -EINVAL;
		haveinfo = true;
	}

	if (attrs[NL80211_ATTR_BEACON_TAIL]) {
		bcn->tail = nla_data(attrs[NL80211_ATTR_BEACON_TAIL]);
		bcn->tail_len = nla_len(attrs[NL80211_ATTR_BEACON_TAIL]);
		haveinfo = true;
	}

	if (!haveinfo)
		return -EINVAL;

	if (attrs[NL80211_ATTR_IE]) {
		bcn->beacon_ies = nla_data(attrs[NL80211_ATTR_IE]);
		bcn->beacon_ies_len = nla_len(attrs[NL80211_ATTR_IE]);
	}

	if (attrs[NL80211_ATTR_IE_PROBE_RESP]) {
		bcn->proberesp_ies =
			nla_data(attrs[NL80211_ATTR_IE_PROBE_RESP]);
		bcn->proberesp_ies_len =
			nla_len(attrs[NL80211_ATTR_IE_PROBE_RESP]);
	}

	if (attrs[NL80211_ATTR_IE_ASSOC_RESP]) {
		bcn->assocresp_ies =
			nla_data(attrs[NL80211_ATTR_IE_ASSOC_RESP]);
		bcn->assocresp_ies_len =
			nla_len(attrs[NL80211_ATTR_IE_ASSOC_RESP]);
	}

	if (attrs[NL80211_ATTR_PROBE_RESP]) {
		bcn->probe_resp = nla_data(attrs[NL80211_ATTR_PROBE_RESP]);
		bcn->probe_resp_len = nla_len(attrs[NL80211_ATTR_PROBE_RESP]);
	}

	if (attrs[NL80211_ATTR_FTM_RESPONDER]) {
		struct nlattr *tb[NL80211_FTM_RESP_ATTR_MAX + 1];

		err = nla_parse_nested_deprecated(tb,
						  NL80211_FTM_RESP_ATTR_MAX,
						  attrs[NL80211_ATTR_FTM_RESPONDER],
						  NULL, NULL);
		if (err)
			return err;

		if (tb[NL80211_FTM_RESP_ATTR_ENABLED] &&
		    wiphy_ext_feature_isset(&rdev->wiphy,
					    NL80211_EXT_FEATURE_ENABLE_FTM_RESPONDER))
			bcn->ftm_responder = 1;
		else
			return -EOPNOTSUPP;

		if (tb[NL80211_FTM_RESP_ATTR_LCI]) {
			bcn->lci = nla_data(tb[NL80211_FTM_RESP_ATTR_LCI]);
			bcn->lci_len = nla_len(tb[NL80211_FTM_RESP_ATTR_LCI]);
		}

		if (tb[NL80211_FTM_RESP_ATTR_CIVICLOC]) {
			bcn->civicloc = nla_data(tb[NL80211_FTM_RESP_ATTR_CIVICLOC]);
			bcn->civicloc_len = nla_len(tb[NL80211_FTM_RESP_ATTR_CIVICLOC]);
		}
	} else {
		bcn->ftm_responder = -1;
	}

	return 0;
}

static void nl80211_check_ap_rate_selectors(struct cfg80211_ap_settings *params,
					    const u8 *rates)
{
	int i;

	if (!rates)
		return;

	for (i = 0; i < rates[1]; i++) {
		if (rates[2 + i] == BSS_MEMBERSHIP_SELECTOR_HT_PHY)
			params->ht_required = true;
		if (rates[2 + i] == BSS_MEMBERSHIP_SELECTOR_VHT_PHY)
			params->vht_required = true;
	}
}

/*
 * Since the nl80211 API didn't include, from the beginning, attributes about
 * HT/VHT requirements/capabilities, we parse them out of the IEs for the
 * benefit of drivers that rebuild IEs in the firmware.
 */
static void nl80211_calculate_ap_params(struct cfg80211_ap_settings *params)
{
	const struct cfg80211_beacon_data *bcn = &params->beacon;
	size_t ies_len = bcn->tail_len;
	const u8 *ies = bcn->tail;
	const u8 *rates;
	const u8 *cap;

	rates = cfg80211_find_ie(WLAN_EID_SUPP_RATES, ies, ies_len);
	nl80211_check_ap_rate_selectors(params, rates);

	rates = cfg80211_find_ie(WLAN_EID_EXT_SUPP_RATES, ies, ies_len);
	nl80211_check_ap_rate_selectors(params, rates);

	cap = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, ies, ies_len);
	if (cap && cap[1] >= sizeof(*params->ht_cap))
		params->ht_cap = (void *)(cap + 2);
	cap = cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, ies, ies_len);
	if (cap && cap[1] >= sizeof(*params->vht_cap))
		params->vht_cap = (void *)(cap + 2);
	cap = cfg80211_find_ext_ie(WLAN_EID_EXT_HE_CAPABILITY, ies, ies_len);
	if (cap && cap[1] >= sizeof(*params->he_cap) + 1)
		params->he_cap = (void *)(cap + 3);
}

static bool nl80211_get_ap_channel(struct cfg80211_registered_device *rdev,
				   struct cfg80211_ap_settings *params)
{
	struct wireless_dev *wdev;
	bool ret = false;

	list_for_each_entry(wdev, &rdev->wiphy.wdev_list, list) {
		if (wdev->iftype != NL80211_IFTYPE_AP &&
		    wdev->iftype != NL80211_IFTYPE_P2P_GO)
			continue;

		if (!wdev->preset_chandef.chan)
			continue;

		params->chandef = wdev->preset_chandef;
		ret = true;
		break;
	}

	return ret;
}

static bool nl80211_valid_auth_type(struct cfg80211_registered_device *rdev,
				    enum nl80211_auth_type auth_type,
				    enum nl80211_commands cmd)
{
	if (auth_type > NL80211_AUTHTYPE_MAX)
		return false;

	switch (cmd) {
	case NL80211_CMD_AUTHENTICATE:
		if (!(rdev->wiphy.features & NL80211_FEATURE_SAE) &&
		    auth_type == NL80211_AUTHTYPE_SAE)
			return false;
		if (!wiphy_ext_feature_isset(&rdev->wiphy,
					     NL80211_EXT_FEATURE_FILS_STA) &&
		    (auth_type == NL80211_AUTHTYPE_FILS_SK ||
		     auth_type == NL80211_AUTHTYPE_FILS_SK_PFS ||
		     auth_type == NL80211_AUTHTYPE_FILS_PK))
			return false;
		return true;
	case NL80211_CMD_CONNECT:
		if (!(rdev->wiphy.features & NL80211_FEATURE_SAE) &&
		    auth_type == NL80211_AUTHTYPE_SAE)
			return false;

		/* FILS with SK PFS or PK not supported yet */
		if (auth_type == NL80211_AUTHTYPE_FILS_SK_PFS ||
		    auth_type == NL80211_AUTHTYPE_FILS_PK)
			return false;
		if (!wiphy_ext_feature_isset(
			    &rdev->wiphy,
			    NL80211_EXT_FEATURE_FILS_SK_OFFLOAD) &&
		    auth_type == NL80211_AUTHTYPE_FILS_SK)
			return false;
		return true;
	case NL80211_CMD_START_AP:
		/* SAE not supported yet */
		if (auth_type == NL80211_AUTHTYPE_SAE)
			return false;
		/* FILS not supported yet */
		if (auth_type == NL80211_AUTHTYPE_FILS_SK ||
		    auth_type == NL80211_AUTHTYPE_FILS_SK_PFS ||
		    auth_type == NL80211_AUTHTYPE_FILS_PK)
			return false;
		return true;
	default:
		return false;
	}
}

static int nl80211_start_ap(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_ap_settings params;
	int err;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
		return -EOPNOTSUPP;

	if (!rdev->ops->start_ap)
		return -EOPNOTSUPP;

	if (wdev->beacon_interval)
		return -EALREADY;

	memset(&params, 0, sizeof(params));

	/* these are required for START_AP */
	if (!info->attrs[NL80211_ATTR_BEACON_INTERVAL] ||
	    !info->attrs[NL80211_ATTR_DTIM_PERIOD] ||
	    !info->attrs[NL80211_ATTR_BEACON_HEAD])
		return -EINVAL;

	err = nl80211_parse_beacon(rdev, info->attrs, &params.beacon);
	if (err)
		return err;

	params.beacon_interval =
		nla_get_u32(info->attrs[NL80211_ATTR_BEACON_INTERVAL]);
	params.dtim_period =
		nla_get_u32(info->attrs[NL80211_ATTR_DTIM_PERIOD]);

	err = cfg80211_validate_beacon_int(rdev, dev->ieee80211_ptr->iftype,
					   params.beacon_interval);
	if (err)
		return err;

	/*
	 * In theory, some of these attributes should be required here
	 * but since they were not used when the command was originally
	 * added, keep them optional for old user space programs to let
	 * them continue to work with drivers that do not need the
	 * additional information -- drivers must check!
	 */
	if (info->attrs[NL80211_ATTR_SSID]) {
		params.ssid = nla_data(info->attrs[NL80211_ATTR_SSID]);
		params.ssid_len =
			nla_len(info->attrs[NL80211_ATTR_SSID]);
		if (params.ssid_len == 0 ||
		    params.ssid_len > IEEE80211_MAX_SSID_LEN)
			return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_HIDDEN_SSID])
		params.hidden_ssid = nla_get_u32(
			info->attrs[NL80211_ATTR_HIDDEN_SSID]);

	params.privacy = !!info->attrs[NL80211_ATTR_PRIVACY];

	if (info->attrs[NL80211_ATTR_AUTH_TYPE]) {
		params.auth_type = nla_get_u32(
			info->attrs[NL80211_ATTR_AUTH_TYPE]);
		if (!nl80211_valid_auth_type(rdev, params.auth_type,
					     NL80211_CMD_START_AP))
			return -EINVAL;
	} else
		params.auth_type = NL80211_AUTHTYPE_AUTOMATIC;

	err = nl80211_crypto_settings(rdev, info, &params.crypto,
				      NL80211_MAX_NR_CIPHER_SUITES);
	if (err)
		return err;

	if (info->attrs[NL80211_ATTR_INACTIVITY_TIMEOUT]) {
		if (!(rdev->wiphy.features & NL80211_FEATURE_INACTIVITY_TIMER))
			return -EOPNOTSUPP;
		params.inactivity_timeout = nla_get_u16(
			info->attrs[NL80211_ATTR_INACTIVITY_TIMEOUT]);
	}

	if (info->attrs[NL80211_ATTR_P2P_CTWINDOW]) {
		if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
			return -EINVAL;
		params.p2p_ctwindow =
			nla_get_u8(info->attrs[NL80211_ATTR_P2P_CTWINDOW]);
		if (params.p2p_ctwindow != 0 &&
		    !(rdev->wiphy.features & NL80211_FEATURE_P2P_GO_CTWIN))
			return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_P2P_OPPPS]) {
		u8 tmp;

		if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
			return -EINVAL;
		tmp = nla_get_u8(info->attrs[NL80211_ATTR_P2P_OPPPS]);
		params.p2p_opp_ps = tmp;
		if (params.p2p_opp_ps != 0 &&
		    !(rdev->wiphy.features & NL80211_FEATURE_P2P_GO_OPPPS))
			return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_WIPHY_FREQ]) {
		err = nl80211_parse_chandef(rdev, info, &params.chandef);
		if (err)
			return err;
	} else if (wdev->preset_chandef.chan) {
		params.chandef = wdev->preset_chandef;
	} else if (!nl80211_get_ap_channel(rdev, &params))
		return -EINVAL;

	if (!cfg80211_reg_can_beacon_relax(&rdev->wiphy, &params.chandef,
					   wdev->iftype))
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_TX_RATES]) {
		err = nl80211_parse_tx_bitrate_mask(info, &params.beacon_rate);
		if (err)
			return err;

		err = validate_beacon_tx_rate(rdev, params.chandef.chan->band,
					      &params.beacon_rate);
		if (err)
			return err;
	}

	if (info->attrs[NL80211_ATTR_SMPS_MODE]) {
		params.smps_mode =
			nla_get_u8(info->attrs[NL80211_ATTR_SMPS_MODE]);
		switch (params.smps_mode) {
		case NL80211_SMPS_OFF:
			break;
		case NL80211_SMPS_STATIC:
			if (!(rdev->wiphy.features &
			      NL80211_FEATURE_STATIC_SMPS))
				return -EINVAL;
			break;
		case NL80211_SMPS_DYNAMIC:
			if (!(rdev->wiphy.features &
			      NL80211_FEATURE_DYNAMIC_SMPS))
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
	} else {
		params.smps_mode = NL80211_SMPS_OFF;
	}

	params.pbss = nla_get_flag(info->attrs[NL80211_ATTR_PBSS]);
	if (params.pbss && !rdev->wiphy.bands[NL80211_BAND_60GHZ])
		return -EOPNOTSUPP;

	if (info->attrs[NL80211_ATTR_ACL_POLICY]) {
		params.acl = parse_acl_data(&rdev->wiphy, info);
		if (IS_ERR(params.acl))
			return PTR_ERR(params.acl);
	}

	nl80211_calculate_ap_params(&params);

	if (info->attrs[NL80211_ATTR_EXTERNAL_AUTH_SUPPORT])
		params.flags |= AP_SETTINGS_EXTERNAL_AUTH_SUPPORT;

	wdev_lock(wdev);
	err = rdev_start_ap(rdev, dev, &params);
	if (!err) {
		wdev->preset_chandef = params.chandef;
		wdev->beacon_interval = params.beacon_interval;
		wdev->chandef = params.chandef;
		wdev->ssid_len = params.ssid_len;
		memcpy(wdev->ssid, params.ssid, wdev->ssid_len);

		if (info->attrs[NL80211_ATTR_SOCKET_OWNER])
			wdev->conn_owner_nlportid = info->snd_portid;
	}
	wdev_unlock(wdev);

	kfree(params.acl);

	return err;
}

static int nl80211_set_beacon(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_beacon_data params;
	int err;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
		return -EOPNOTSUPP;

	if (!rdev->ops->change_beacon)
		return -EOPNOTSUPP;

	if (!wdev->beacon_interval)
		return -EINVAL;

	err = nl80211_parse_beacon(rdev, info->attrs, &params);
	if (err)
		return err;

	wdev_lock(wdev);
	err = rdev_change_beacon(rdev, dev, &params);
	wdev_unlock(wdev);

	return err;
}

static int nl80211_stop_ap(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];

	return cfg80211_stop_ap(rdev, dev, false);
}

static const struct nla_policy sta_flags_policy[NL80211_STA_FLAG_MAX + 1] = {
	[NL80211_STA_FLAG_AUTHORIZED] = { .type = NLA_FLAG },
	[NL80211_STA_FLAG_SHORT_PREAMBLE] = { .type = NLA_FLAG },
	[NL80211_STA_FLAG_WME] = { .type = NLA_FLAG },
	[NL80211_STA_FLAG_MFP] = { .type = NLA_FLAG },
	[NL80211_STA_FLAG_AUTHENTICATED] = { .type = NLA_FLAG },
	[NL80211_STA_FLAG_TDLS_PEER] = { .type = NLA_FLAG },
};

static int parse_station_flags(struct genl_info *info,
			       enum nl80211_iftype iftype,
			       struct station_parameters *params)
{
	struct nlattr *flags[NL80211_STA_FLAG_MAX + 1];
	struct nlattr *nla;
	int flag;

	/*
	 * Try parsing the new attribute first so userspace
	 * can specify both for older kernels.
	 */
	nla = info->attrs[NL80211_ATTR_STA_FLAGS2];
	if (nla) {
		struct nl80211_sta_flag_update *sta_flags;

		sta_flags = nla_data(nla);
		params->sta_flags_mask = sta_flags->mask;
		params->sta_flags_set = sta_flags->set;
		params->sta_flags_set &= params->sta_flags_mask;
		if ((params->sta_flags_mask |
		     params->sta_flags_set) & BIT(__NL80211_STA_FLAG_INVALID))
			return -EINVAL;
		return 0;
	}

	/* if present, parse the old attribute */

	nla = info->attrs[NL80211_ATTR_STA_FLAGS];
	if (!nla)
		return 0;

	if (nla_parse_nested_deprecated(flags, NL80211_STA_FLAG_MAX, nla, sta_flags_policy, info->extack))
		return -EINVAL;

	/*
	 * Only allow certain flags for interface types so that
	 * other attributes are silently ignored. Remember that
	 * this is backward compatibility code with old userspace
	 * and shouldn't be hit in other cases anyway.
	 */
	switch (iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_GO:
		params->sta_flags_mask = BIT(NL80211_STA_FLAG_AUTHORIZED) |
					 BIT(NL80211_STA_FLAG_SHORT_PREAMBLE) |
					 BIT(NL80211_STA_FLAG_WME) |
					 BIT(NL80211_STA_FLAG_MFP);
		break;
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_STATION:
		params->sta_flags_mask = BIT(NL80211_STA_FLAG_AUTHORIZED) |
					 BIT(NL80211_STA_FLAG_TDLS_PEER);
		break;
	case NL80211_IFTYPE_MESH_POINT:
		params->sta_flags_mask = BIT(NL80211_STA_FLAG_AUTHENTICATED) |
					 BIT(NL80211_STA_FLAG_MFP) |
					 BIT(NL80211_STA_FLAG_AUTHORIZED);
		break;
	default:
		return -EINVAL;
	}

	for (flag = 1; flag <= NL80211_STA_FLAG_MAX; flag++) {
		if (flags[flag]) {
			params->sta_flags_set |= (1<<flag);

			/* no longer support new API additions in old API */
			if (flag > NL80211_STA_FLAG_MAX_OLD_API)
				return -EINVAL;
		}
	}

	return 0;
}

bool nl80211_put_sta_rate(struct sk_buff *msg, struct rate_info *info, int attr)
{
	struct nlattr *rate;
	u32 bitrate;
	u16 bitrate_compat;
	enum nl80211_rate_info rate_flg;

	rate = nla_nest_start_noflag(msg, attr);
	if (!rate)
		return false;

	/* cfg80211_calculate_bitrate will return 0 for mcs >= 32 */
	bitrate = cfg80211_calculate_bitrate(info);
	/* report 16-bit bitrate only if we can */
	bitrate_compat = bitrate < (1UL << 16) ? bitrate : 0;
	if (bitrate > 0 &&
	    nla_put_u32(msg, NL80211_RATE_INFO_BITRATE32, bitrate))
		return false;
	if (bitrate_compat > 0 &&
	    nla_put_u16(msg, NL80211_RATE_INFO_BITRATE, bitrate_compat))
		return false;

	switch (info->bw) {
	case RATE_INFO_BW_5:
		rate_flg = NL80211_RATE_INFO_5_MHZ_WIDTH;
		break;
	case RATE_INFO_BW_10:
		rate_flg = NL80211_RATE_INFO_10_MHZ_WIDTH;
		break;
	default:
		WARN_ON(1);
		/* fall through */
	case RATE_INFO_BW_20:
		rate_flg = 0;
		break;
	case RATE_INFO_BW_40:
		rate_flg = NL80211_RATE_INFO_40_MHZ_WIDTH;
		break;
	case RATE_INFO_BW_80:
		rate_flg = NL80211_RATE_INFO_80_MHZ_WIDTH;
		break;
	case RATE_INFO_BW_160:
		rate_flg = NL80211_RATE_INFO_160_MHZ_WIDTH;
		break;
	case RATE_INFO_BW_HE_RU:
		rate_flg = 0;
		WARN_ON(!(info->flags & RATE_INFO_FLAGS_HE_MCS));
	}

	if (rate_flg && nla_put_flag(msg, rate_flg))
		return false;

	if (info->flags & RATE_INFO_FLAGS_MCS) {
		if (nla_put_u8(msg, NL80211_RATE_INFO_MCS, info->mcs))
			return false;
		if (info->flags & RATE_INFO_FLAGS_SHORT_GI &&
		    nla_put_flag(msg, NL80211_RATE_INFO_SHORT_GI))
			return false;
	} else if (info->flags & RATE_INFO_FLAGS_VHT_MCS) {
		if (nla_put_u8(msg, NL80211_RATE_INFO_VHT_MCS, info->mcs))
			return false;
		if (nla_put_u8(msg, NL80211_RATE_INFO_VHT_NSS, info->nss))
			return false;
		if (info->flags & RATE_INFO_FLAGS_SHORT_GI &&
		    nla_put_flag(msg, NL80211_RATE_INFO_SHORT_GI))
			return false;
	} else if (info->flags & RATE_INFO_FLAGS_HE_MCS) {
		if (nla_put_u8(msg, NL80211_RATE_INFO_HE_MCS, info->mcs))
			return false;
		if (nla_put_u8(msg, NL80211_RATE_INFO_HE_NSS, info->nss))
			return false;
		if (nla_put_u8(msg, NL80211_RATE_INFO_HE_GI, info->he_gi))
			return false;
		if (nla_put_u8(msg, NL80211_RATE_INFO_HE_DCM, info->he_dcm))
			return false;
		if (info->bw == RATE_INFO_BW_HE_RU &&
		    nla_put_u8(msg, NL80211_RATE_INFO_HE_RU_ALLOC,
			       info->he_ru_alloc))
			return false;
	}

	nla_nest_end(msg, rate);
	return true;
}

static bool nl80211_put_signal(struct sk_buff *msg, u8 mask, s8 *signal,
			       int id)
{
	void *attr;
	int i = 0;

	if (!mask)
		return true;

	attr = nla_nest_start_noflag(msg, id);
	if (!attr)
		return false;

	for (i = 0; i < IEEE80211_MAX_CHAINS; i++) {
		if (!(mask & BIT(i)))
			continue;

		if (nla_put_u8(msg, i, signal[i]))
			return false;
	}

	nla_nest_end(msg, attr);

	return true;
}

static int nl80211_send_station(struct sk_buff *msg, u32 cmd, u32 portid,
				u32 seq, int flags,
				struct cfg80211_registered_device *rdev,
				struct net_device *dev,
				const u8 *mac_addr, struct station_info *sinfo)
{
	void *hdr;
	struct nlattr *sinfoattr, *bss_param;

	hdr = nl80211hdr_put(msg, portid, seq, flags, cmd);
	if (!hdr) {
		cfg80211_sinfo_release_content(sinfo);
		return -1;
	}

	if (nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, mac_addr) ||
	    nla_put_u32(msg, NL80211_ATTR_GENERATION, sinfo->generation))
		goto nla_put_failure;

	sinfoattr = nla_nest_start_noflag(msg, NL80211_ATTR_STA_INFO);
	if (!sinfoattr)
		goto nla_put_failure;

#define PUT_SINFO(attr, memb, type) do {				\
	BUILD_BUG_ON(sizeof(type) == sizeof(u64));			\
	if (sinfo->filled & BIT_ULL(NL80211_STA_INFO_ ## attr) &&	\
	    nla_put_ ## type(msg, NL80211_STA_INFO_ ## attr,		\
			     sinfo->memb))				\
		goto nla_put_failure;					\
	} while (0)
#define PUT_SINFO_U64(attr, memb) do {					\
	if (sinfo->filled & BIT_ULL(NL80211_STA_INFO_ ## attr) &&	\
	    nla_put_u64_64bit(msg, NL80211_STA_INFO_ ## attr,		\
			      sinfo->memb, NL80211_STA_INFO_PAD))	\
		goto nla_put_failure;					\
	} while (0)

	PUT_SINFO(CONNECTED_TIME, connected_time, u32);
	PUT_SINFO(INACTIVE_TIME, inactive_time, u32);

	if (sinfo->filled & (BIT_ULL(NL80211_STA_INFO_RX_BYTES) |
			     BIT_ULL(NL80211_STA_INFO_RX_BYTES64)) &&
	    nla_put_u32(msg, NL80211_STA_INFO_RX_BYTES,
			(u32)sinfo->rx_bytes))
		goto nla_put_failure;

	if (sinfo->filled & (BIT_ULL(NL80211_STA_INFO_TX_BYTES) |
			     BIT_ULL(NL80211_STA_INFO_TX_BYTES64)) &&
	    nla_put_u32(msg, NL80211_STA_INFO_TX_BYTES,
			(u32)sinfo->tx_bytes))
		goto nla_put_failure;

	PUT_SINFO_U64(RX_BYTES64, rx_bytes);
	PUT_SINFO_U64(TX_BYTES64, tx_bytes);
	PUT_SINFO(LLID, llid, u16);
	PUT_SINFO(PLID, plid, u16);
	PUT_SINFO(PLINK_STATE, plink_state, u8);
	PUT_SINFO_U64(RX_DURATION, rx_duration);
	PUT_SINFO_U64(TX_DURATION, tx_duration);

	if (wiphy_ext_feature_isset(&rdev->wiphy,
				    NL80211_EXT_FEATURE_AIRTIME_FAIRNESS))
		PUT_SINFO(AIRTIME_WEIGHT, airtime_weight, u16);

	switch (rdev->wiphy.signal_type) {
	case CFG80211_SIGNAL_TYPE_MBM:
		PUT_SINFO(SIGNAL, signal, u8);
		PUT_SINFO(SIGNAL_AVG, signal_avg, u8);
		break;
	default:
		break;
	}
	if (sinfo->filled & BIT_ULL(NL80211_STA_INFO_CHAIN_SIGNAL)) {
		if (!nl80211_put_signal(msg, sinfo->chains,
					sinfo->chain_signal,
					NL80211_STA_INFO_CHAIN_SIGNAL))
			goto nla_put_failure;
	}
	if (sinfo->filled & BIT_ULL(NL80211_STA_INFO_CHAIN_SIGNAL_AVG)) {
		if (!nl80211_put_signal(msg, sinfo->chains,
					sinfo->chain_signal_avg,
					NL80211_STA_INFO_CHAIN_SIGNAL_AVG))
			goto nla_put_failure;
	}
	if (sinfo->filled & BIT_ULL(NL80211_STA_INFO_TX_BITRATE)) {
		if (!nl80211_put_sta_rate(msg, &sinfo->txrate,
					  NL80211_STA_INFO_TX_BITRATE))
			goto nla_put_failure;
	}
	if (sinfo->filled & BIT_ULL(NL80211_STA_INFO_RX_BITRATE)) {
		if (!nl80211_put_sta_rate(msg, &sinfo->rxrate,
					  NL80211_STA_INFO_RX_BITRATE))
			goto nla_put_failure;
	}

	PUT_SINFO(RX_PACKETS, rx_packets, u32);
	PUT_SINFO(TX_PACKETS, tx_packets, u32);
	PUT_SINFO(TX_RETRIES, tx_retries, u32);
	PUT_SINFO(TX_FAILED, tx_failed, u32);
	PUT_SINFO(EXPECTED_THROUGHPUT, expected_throughput, u32);
	PUT_SINFO(AIRTIME_LINK_METRIC, airtime_link_metric, u32);
	PUT_SINFO(BEACON_LOSS, beacon_loss_count, u32);
	PUT_SINFO(LOCAL_PM, local_pm, u32);
	PUT_SINFO(PEER_PM, peer_pm, u32);
	PUT_SINFO(NONPEER_PM, nonpeer_pm, u32);
	PUT_SINFO(CONNECTED_TO_GATE, connected_to_gate, u8);

	if (sinfo->filled & BIT_ULL(NL80211_STA_INFO_BSS_PARAM)) {
		bss_param = nla_nest_start_noflag(msg,
						  NL80211_STA_INFO_BSS_PARAM);
		if (!bss_param)
			goto nla_put_failure;

		if (((sinfo->bss_param.flags & BSS_PARAM_FLAGS_CTS_PROT) &&
		     nla_put_flag(msg, NL80211_STA_BSS_PARAM_CTS_PROT)) ||
		    ((sinfo->bss_param.flags & BSS_PARAM_FLAGS_SHORT_PREAMBLE) &&
		     nla_put_flag(msg, NL80211_STA_BSS_PARAM_SHORT_PREAMBLE)) ||
		    ((sinfo->bss_param.flags & BSS_PARAM_FLAGS_SHORT_SLOT_TIME) &&
		     nla_put_flag(msg, NL80211_STA_BSS_PARAM_SHORT_SLOT_TIME)) ||
		    nla_put_u8(msg, NL80211_STA_BSS_PARAM_DTIM_PERIOD,
			       sinfo->bss_param.dtim_period) ||
		    nla_put_u16(msg, NL80211_STA_BSS_PARAM_BEACON_INTERVAL,
				sinfo->bss_param.beacon_interval))
			goto nla_put_failure;

		nla_nest_end(msg, bss_param);
	}
	if ((sinfo->filled & BIT_ULL(NL80211_STA_INFO_STA_FLAGS)) &&
	    nla_put(msg, NL80211_STA_INFO_STA_FLAGS,
		    sizeof(struct nl80211_sta_flag_update),
		    &sinfo->sta_flags))
		goto nla_put_failure;

	PUT_SINFO_U64(T_OFFSET, t_offset);
	PUT_SINFO_U64(RX_DROP_MISC, rx_dropped_misc);
	PUT_SINFO_U64(BEACON_RX, rx_beacon);
	PUT_SINFO(BEACON_SIGNAL_AVG, rx_beacon_signal_avg, u8);
	PUT_SINFO(RX_MPDUS, rx_mpdu_count, u32);
	PUT_SINFO(FCS_ERROR_COUNT, fcs_err_count, u32);
	if (wiphy_ext_feature_isset(&rdev->wiphy,
				    NL80211_EXT_FEATURE_ACK_SIGNAL_SUPPORT)) {
		PUT_SINFO(ACK_SIGNAL, ack_signal, u8);
		PUT_SINFO(ACK_SIGNAL_AVG, avg_ack_signal, s8);
	}

#undef PUT_SINFO
#undef PUT_SINFO_U64

	if (sinfo->pertid) {
		struct nlattr *tidsattr;
		int tid;

		tidsattr = nla_nest_start_noflag(msg,
						 NL80211_STA_INFO_TID_STATS);
		if (!tidsattr)
			goto nla_put_failure;

		for (tid = 0; tid < IEEE80211_NUM_TIDS + 1; tid++) {
			struct cfg80211_tid_stats *tidstats;
			struct nlattr *tidattr;

			tidstats = &sinfo->pertid[tid];

			if (!tidstats->filled)
				continue;

			tidattr = nla_nest_start_noflag(msg, tid + 1);
			if (!tidattr)
				goto nla_put_failure;

#define PUT_TIDVAL_U64(attr, memb) do {					\
	if (tidstats->filled & BIT(NL80211_TID_STATS_ ## attr) &&	\
	    nla_put_u64_64bit(msg, NL80211_TID_STATS_ ## attr,		\
			      tidstats->memb, NL80211_TID_STATS_PAD))	\
		goto nla_put_failure;					\
	} while (0)

			PUT_TIDVAL_U64(RX_MSDU, rx_msdu);
			PUT_TIDVAL_U64(TX_MSDU, tx_msdu);
			PUT_TIDVAL_U64(TX_MSDU_RETRIES, tx_msdu_retries);
			PUT_TIDVAL_U64(TX_MSDU_FAILED, tx_msdu_failed);

#undef PUT_TIDVAL_U64
			if ((tidstats->filled &
			     BIT(NL80211_TID_STATS_TXQ_STATS)) &&
			    !nl80211_put_txq_stats(msg, &tidstats->txq_stats,
						   NL80211_TID_STATS_TXQ_STATS))
				goto nla_put_failure;

			nla_nest_end(msg, tidattr);
		}

		nla_nest_end(msg, tidsattr);
	}

	nla_nest_end(msg, sinfoattr);

	if (sinfo->assoc_req_ies_len &&
	    nla_put(msg, NL80211_ATTR_IE, sinfo->assoc_req_ies_len,
		    sinfo->assoc_req_ies))
		goto nla_put_failure;

	cfg80211_sinfo_release_content(sinfo);
	genlmsg_end(msg, hdr);
	return 0;

 nla_put_failure:
	cfg80211_sinfo_release_content(sinfo);
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nl80211_dump_station(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	struct station_info sinfo;
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *wdev;
	u8 mac_addr[ETH_ALEN];
	int sta_idx = cb->args[2];
	int err;

	rtnl_lock();
	err = nl80211_prepare_wdev_dump(cb, &rdev, &wdev);
	if (err)
		goto out_err;

	if (!wdev->netdev) {
		err = -EINVAL;
		goto out_err;
	}

	if (!rdev->ops->dump_station) {
		err = -EOPNOTSUPP;
		goto out_err;
	}

	while (1) {
		memset(&sinfo, 0, sizeof(sinfo));
		err = rdev_dump_station(rdev, wdev->netdev, sta_idx,
					mac_addr, &sinfo);
		if (err == -ENOENT)
			break;
		if (err)
			goto out_err;

		if (nl80211_send_station(skb, NL80211_CMD_NEW_STATION,
				NETLINK_CB(cb->skb).portid,
				cb->nlh->nlmsg_seq, NLM_F_MULTI,
				rdev, wdev->netdev, mac_addr,
				&sinfo) < 0)
			goto out;

		sta_idx++;
	}

 out:
	cb->args[2] = sta_idx;
	err = skb->len;
 out_err:
	rtnl_unlock();

	return err;
}

static int nl80211_get_station(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct station_info sinfo;
	struct sk_buff *msg;
	u8 *mac_addr = NULL;
	int err;

	memset(&sinfo, 0, sizeof(sinfo));

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (!rdev->ops->get_station)
		return -EOPNOTSUPP;

	err = rdev_get_station(rdev, dev, mac_addr, &sinfo);
	if (err)
		return err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		cfg80211_sinfo_release_content(&sinfo);
		return -ENOMEM;
	}

	if (nl80211_send_station(msg, NL80211_CMD_NEW_STATION,
				 info->snd_portid, info->snd_seq, 0,
				 rdev, dev, mac_addr, &sinfo) < 0) {
		nlmsg_free(msg);
		return -ENOBUFS;
	}

	return genlmsg_reply(msg, info);
}

int cfg80211_check_station_change(struct wiphy *wiphy,
				  struct station_parameters *params,
				  enum cfg80211_station_type statype)
{
	if (params->listen_interval != -1 &&
	    statype != CFG80211_STA_AP_CLIENT_UNASSOC)
		return -EINVAL;

	if (params->support_p2p_ps != -1 &&
	    statype != CFG80211_STA_AP_CLIENT_UNASSOC)
		return -EINVAL;

	if (params->aid &&
	    !(params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER)) &&
	    statype != CFG80211_STA_AP_CLIENT_UNASSOC)
		return -EINVAL;

	/* When you run into this, adjust the code below for the new flag */
	BUILD_BUG_ON(NL80211_STA_FLAG_MAX != 7);

	switch (statype) {
	case CFG80211_STA_MESH_PEER_KERNEL:
	case CFG80211_STA_MESH_PEER_USER:
		/*
		 * No ignoring the TDLS flag here -- the userspace mesh
		 * code doesn't have the bug of including TDLS in the
		 * mask everywhere.
		 */
		if (params->sta_flags_mask &
				~(BIT(NL80211_STA_FLAG_AUTHENTICATED) |
				  BIT(NL80211_STA_FLAG_MFP) |
				  BIT(NL80211_STA_FLAG_AUTHORIZED)))
			return -EINVAL;
		break;
	case CFG80211_STA_TDLS_PEER_SETUP:
	case CFG80211_STA_TDLS_PEER_ACTIVE:
		if (!(params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER)))
			return -EINVAL;
		/* ignore since it can't change */
		params->sta_flags_mask &= ~BIT(NL80211_STA_FLAG_TDLS_PEER);
		break;
	default:
		/* disallow mesh-specific things */
		if (params->plink_action != NL80211_PLINK_ACTION_NO_ACTION)
			return -EINVAL;
		if (params->local_pm)
			return -EINVAL;
		if (params->sta_modify_mask & STATION_PARAM_APPLY_PLINK_STATE)
			return -EINVAL;
	}

	if (statype != CFG80211_STA_TDLS_PEER_SETUP &&
	    statype != CFG80211_STA_TDLS_PEER_ACTIVE) {
		/* TDLS can't be set, ... */
		if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER))
			return -EINVAL;
		/*
		 * ... but don't bother the driver with it. This works around
		 * a hostapd/wpa_supplicant issue -- it always includes the
		 * TLDS_PEER flag in the mask even for AP mode.
		 */
		params->sta_flags_mask &= ~BIT(NL80211_STA_FLAG_TDLS_PEER);
	}

	if (statype != CFG80211_STA_TDLS_PEER_SETUP &&
	    statype != CFG80211_STA_AP_CLIENT_UNASSOC) {
		/* reject other things that can't change */
		if (params->sta_modify_mask & STATION_PARAM_APPLY_UAPSD)
			return -EINVAL;
		if (params->sta_modify_mask & STATION_PARAM_APPLY_CAPABILITY)
			return -EINVAL;
		if (params->supported_rates)
			return -EINVAL;
		if (params->ext_capab || params->ht_capa || params->vht_capa ||
		    params->he_capa)
			return -EINVAL;
	}

	if (statype != CFG80211_STA_AP_CLIENT &&
	    statype != CFG80211_STA_AP_CLIENT_UNASSOC) {
		if (params->vlan)
			return -EINVAL;
	}

	switch (statype) {
	case CFG80211_STA_AP_MLME_CLIENT:
		/* Use this only for authorizing/unauthorizing a station */
		if (!(params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED)))
			return -EOPNOTSUPP;
		break;
	case CFG80211_STA_AP_CLIENT:
	case CFG80211_STA_AP_CLIENT_UNASSOC:
		/* accept only the listed bits */
		if (params->sta_flags_mask &
				~(BIT(NL80211_STA_FLAG_AUTHORIZED) |
				  BIT(NL80211_STA_FLAG_AUTHENTICATED) |
				  BIT(NL80211_STA_FLAG_ASSOCIATED) |
				  BIT(NL80211_STA_FLAG_SHORT_PREAMBLE) |
				  BIT(NL80211_STA_FLAG_WME) |
				  BIT(NL80211_STA_FLAG_MFP)))
			return -EINVAL;

		/* but authenticated/associated only if driver handles it */
		if (!(wiphy->features & NL80211_FEATURE_FULL_AP_CLIENT_STATE) &&
		    params->sta_flags_mask &
				(BIT(NL80211_STA_FLAG_AUTHENTICATED) |
				 BIT(NL80211_STA_FLAG_ASSOCIATED)))
			return -EINVAL;
		break;
	case CFG80211_STA_IBSS:
	case CFG80211_STA_AP_STA:
		/* reject any changes other than AUTHORIZED */
		if (params->sta_flags_mask & ~BIT(NL80211_STA_FLAG_AUTHORIZED))
			return -EINVAL;
		break;
	case CFG80211_STA_TDLS_PEER_SETUP:
		/* reject any changes other than AUTHORIZED or WME */
		if (params->sta_flags_mask & ~(BIT(NL80211_STA_FLAG_AUTHORIZED) |
					       BIT(NL80211_STA_FLAG_WME)))
			return -EINVAL;
		/* force (at least) rates when authorizing */
		if (params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED) &&
		    !params->supported_rates)
			return -EINVAL;
		break;
	case CFG80211_STA_TDLS_PEER_ACTIVE:
		/* reject any changes */
		return -EINVAL;
	case CFG80211_STA_MESH_PEER_KERNEL:
		if (params->sta_modify_mask & STATION_PARAM_APPLY_PLINK_STATE)
			return -EINVAL;
		break;
	case CFG80211_STA_MESH_PEER_USER:
		if (params->plink_action != NL80211_PLINK_ACTION_NO_ACTION &&
		    params->plink_action != NL80211_PLINK_ACTION_BLOCK)
			return -EINVAL;
		break;
	}

	/*
	 * Older kernel versions ignored this attribute entirely, so don't
	 * reject attempts to update it but mark it as unused instead so the
	 * driver won't look at the data.
	 */
	if (statype != CFG80211_STA_AP_CLIENT_UNASSOC &&
	    statype != CFG80211_STA_TDLS_PEER_SETUP)
		params->opmode_notif_used = false;

	return 0;
}
EXPORT_SYMBOL(cfg80211_check_station_change);

/*
 * Get vlan interface making sure it is running and on the right wiphy.
 */
static struct net_device *get_vlan(struct genl_info *info,
				   struct cfg80211_registered_device *rdev)
{
	struct nlattr *vlanattr = info->attrs[NL80211_ATTR_STA_VLAN];
	struct net_device *v;
	int ret;

	if (!vlanattr)
		return NULL;

	v = dev_get_by_index(genl_info_net(info), nla_get_u32(vlanattr));
	if (!v)
		return ERR_PTR(-ENODEV);

	if (!v->ieee80211_ptr || v->ieee80211_ptr->wiphy != &rdev->wiphy) {
		ret = -EINVAL;
		goto error;
	}

	if (v->ieee80211_ptr->iftype != NL80211_IFTYPE_AP_VLAN &&
	    v->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    v->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO) {
		ret = -EINVAL;
		goto error;
	}

	if (!netif_running(v)) {
		ret = -ENETDOWN;
		goto error;
	}

	return v;
 error:
	dev_put(v);
	return ERR_PTR(ret);
}

static const struct nla_policy
nl80211_sta_wme_policy[NL80211_STA_WME_MAX + 1] = {
	[NL80211_STA_WME_UAPSD_QUEUES] = { .type = NLA_U8 },
	[NL80211_STA_WME_MAX_SP] = { .type = NLA_U8 },
};

static int nl80211_parse_sta_wme(struct genl_info *info,
				 struct station_parameters *params)
{
	struct nlattr *tb[NL80211_STA_WME_MAX + 1];
	struct nlattr *nla;
	int err;

	/* parse WME attributes if present */
	if (!info->attrs[NL80211_ATTR_STA_WME])
		return 0;

	nla = info->attrs[NL80211_ATTR_STA_WME];
	err = nla_parse_nested_deprecated(tb, NL80211_STA_WME_MAX, nla,
					  nl80211_sta_wme_policy,
					  info->extack);
	if (err)
		return err;

	if (tb[NL80211_STA_WME_UAPSD_QUEUES])
		params->uapsd_queues = nla_get_u8(
			tb[NL80211_STA_WME_UAPSD_QUEUES]);
	if (params->uapsd_queues & ~IEEE80211_WMM_IE_STA_QOSINFO_AC_MASK)
		return -EINVAL;

	if (tb[NL80211_STA_WME_MAX_SP])
		params->max_sp = nla_get_u8(tb[NL80211_STA_WME_MAX_SP]);

	if (params->max_sp & ~IEEE80211_WMM_IE_STA_QOSINFO_SP_MASK)
		return -EINVAL;

	params->sta_modify_mask |= STATION_PARAM_APPLY_UAPSD;

	return 0;
}

static int nl80211_parse_sta_channel_info(struct genl_info *info,
				      struct station_parameters *params)
{
	if (info->attrs[NL80211_ATTR_STA_SUPPORTED_CHANNELS]) {
		params->supported_channels =
		     nla_data(info->attrs[NL80211_ATTR_STA_SUPPORTED_CHANNELS]);
		params->supported_channels_len =
		     nla_len(info->attrs[NL80211_ATTR_STA_SUPPORTED_CHANNELS]);
		/*
		 * Need to include at least one (first channel, number of
		 * channels) tuple for each subband, and must have proper
		 * tuples for the rest of the data as well.
		 */
		if (params->supported_channels_len < 2)
			return -EINVAL;
		if (params->supported_channels_len % 2)
			return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_STA_SUPPORTED_OPER_CLASSES]) {
		params->supported_oper_classes =
		 nla_data(info->attrs[NL80211_ATTR_STA_SUPPORTED_OPER_CLASSES]);
		params->supported_oper_classes_len =
		  nla_len(info->attrs[NL80211_ATTR_STA_SUPPORTED_OPER_CLASSES]);
		/*
		 * The value of the Length field of the Supported Operating
		 * Classes element is between 2 and 253.
		 */
		if (params->supported_oper_classes_len < 2 ||
		    params->supported_oper_classes_len > 253)
			return -EINVAL;
	}
	return 0;
}

static int nl80211_set_station_tdls(struct genl_info *info,
				    struct station_parameters *params)
{
	int err;
	/* Dummy STA entry gets updated once the peer capabilities are known */
	if (info->attrs[NL80211_ATTR_PEER_AID])
		params->aid = nla_get_u16(info->attrs[NL80211_ATTR_PEER_AID]);
	if (info->attrs[NL80211_ATTR_HT_CAPABILITY])
		params->ht_capa =
			nla_data(info->attrs[NL80211_ATTR_HT_CAPABILITY]);
	if (info->attrs[NL80211_ATTR_VHT_CAPABILITY])
		params->vht_capa =
			nla_data(info->attrs[NL80211_ATTR_VHT_CAPABILITY]);
	if (info->attrs[NL80211_ATTR_HE_CAPABILITY]) {
		params->he_capa =
			nla_data(info->attrs[NL80211_ATTR_HE_CAPABILITY]);
		params->he_capa_len =
			nla_len(info->attrs[NL80211_ATTR_HE_CAPABILITY]);

		if (params->he_capa_len < NL80211_HE_MIN_CAPABILITY_LEN)
			return -EINVAL;
	}

	err = nl80211_parse_sta_channel_info(info, params);
	if (err)
		return err;

	return nl80211_parse_sta_wme(info, params);
}

static int nl80211_parse_sta_txpower_setting(struct genl_info *info,
					     struct station_parameters *params)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	int idx;

	if (info->attrs[NL80211_ATTR_STA_TX_POWER_SETTING]) {
		if (!rdev->ops->set_tx_power ||
		    !wiphy_ext_feature_isset(&rdev->wiphy,
					 NL80211_EXT_FEATURE_STA_TX_PWR))
			return -EOPNOTSUPP;

		idx = NL80211_ATTR_STA_TX_POWER_SETTING;
		params->txpwr.type = nla_get_u8(info->attrs[idx]);

		if (params->txpwr.type == NL80211_TX_POWER_LIMITED) {
			idx = NL80211_ATTR_STA_TX_POWER;

			if (info->attrs[idx])
				params->txpwr.power =
					nla_get_s16(info->attrs[idx]);
			else
				return -EINVAL;
		}
		params->sta_modify_mask |= STATION_PARAM_APPLY_STA_TXPOWER;
	}

	return 0;
}

static int nl80211_set_station(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct station_parameters params;
	u8 *mac_addr;
	int err;

	memset(&params, 0, sizeof(params));

	if (!rdev->ops->change_station)
		return -EOPNOTSUPP;

	/*
	 * AID and listen_interval properties can be set only for unassociated
	 * station. Include these parameters here and will check them in
	 * cfg80211_check_station_change().
	 */
	if (info->attrs[NL80211_ATTR_STA_AID])
		params.aid = nla_get_u16(info->attrs[NL80211_ATTR_STA_AID]);

	if (info->attrs[NL80211_ATTR_STA_LISTEN_INTERVAL])
		params.listen_interval =
		     nla_get_u16(info->attrs[NL80211_ATTR_STA_LISTEN_INTERVAL]);
	else
		params.listen_interval = -1;

	if (info->attrs[NL80211_ATTR_STA_SUPPORT_P2P_PS])
		params.support_p2p_ps =
			nla_get_u8(info->attrs[NL80211_ATTR_STA_SUPPORT_P2P_PS]);
	else
		params.support_p2p_ps = -1;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (info->attrs[NL80211_ATTR_STA_SUPPORTED_RATES]) {
		params.supported_rates =
			nla_data(info->attrs[NL80211_ATTR_STA_SUPPORTED_RATES]);
		params.supported_rates_len =
			nla_len(info->attrs[NL80211_ATTR_STA_SUPPORTED_RATES]);
	}

	if (info->attrs[NL80211_ATTR_STA_CAPABILITY]) {
		params.capability =
			nla_get_u16(info->attrs[NL80211_ATTR_STA_CAPABILITY]);
		params.sta_modify_mask |= STATION_PARAM_APPLY_CAPABILITY;
	}

	if (info->attrs[NL80211_ATTR_STA_EXT_CAPABILITY]) {
		params.ext_capab =
			nla_data(info->attrs[NL80211_ATTR_STA_EXT_CAPABILITY]);
		params.ext_capab_len =
			nla_len(info->attrs[NL80211_ATTR_STA_EXT_CAPABILITY]);
	}

	if (parse_station_flags(info, dev->ieee80211_ptr->iftype, &params))
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_STA_PLINK_ACTION])
		params.plink_action =
			nla_get_u8(info->attrs[NL80211_ATTR_STA_PLINK_ACTION]);

	if (info->attrs[NL80211_ATTR_STA_PLINK_STATE]) {
		params.plink_state =
			nla_get_u8(info->attrs[NL80211_ATTR_STA_PLINK_STATE]);
		if (info->attrs[NL80211_ATTR_MESH_PEER_AID])
			params.peer_aid = nla_get_u16(
				info->attrs[NL80211_ATTR_MESH_PEER_AID]);
		params.sta_modify_mask |= STATION_PARAM_APPLY_PLINK_STATE;
	}

	if (info->attrs[NL80211_ATTR_LOCAL_MESH_POWER_MODE])
		params.local_pm = nla_get_u32(
			info->attrs[NL80211_ATTR_LOCAL_MESH_POWER_MODE]);

	if (info->attrs[NL80211_ATTR_OPMODE_NOTIF]) {
		params.opmode_notif_used = true;
		params.opmode_notif =
			nla_get_u8(info->attrs[NL80211_ATTR_OPMODE_NOTIF]);
	}

	if (info->attrs[NL80211_ATTR_AIRTIME_WEIGHT])
		params.airtime_weight =
			nla_get_u16(info->attrs[NL80211_ATTR_AIRTIME_WEIGHT]);

	if (params.airtime_weight &&
	    !wiphy_ext_feature_isset(&rdev->wiphy,
				     NL80211_EXT_FEATURE_AIRTIME_FAIRNESS))
		return -EOPNOTSUPP;

	err = nl80211_parse_sta_txpower_setting(info, &params);
	if (err)
		return err;

	/* Include parameters for TDLS peer (will check later) */
	err = nl80211_set_station_tdls(info, &params);
	if (err)
		return err;

	params.vlan = get_vlan(info, rdev);
	if (IS_ERR(params.vlan))
		return PTR_ERR(params.vlan);

	switch (dev->ieee80211_ptr->iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
		break;
	default:
		err = -EOPNOTSUPP;
		goto out_put_vlan;
	}

	/* driver will call cfg80211_check_station_change() */
	err = rdev_change_station(rdev, dev, mac_addr, &params);

 out_put_vlan:
	if (params.vlan)
		dev_put(params.vlan);

	return err;
}

static int nl80211_new_station(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	int err;
	struct net_device *dev = info->user_ptr[1];
	struct station_parameters params;
	u8 *mac_addr = NULL;
	u32 auth_assoc = BIT(NL80211_STA_FLAG_AUTHENTICATED) |
			 BIT(NL80211_STA_FLAG_ASSOCIATED);

	memset(&params, 0, sizeof(params));

	if (!rdev->ops->add_station)
		return -EOPNOTSUPP;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_STA_LISTEN_INTERVAL])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_STA_SUPPORTED_RATES])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_STA_AID] &&
	    !info->attrs[NL80211_ATTR_PEER_AID])
		return -EINVAL;

	mac_addr = nla_data(info->attrs[NL80211_ATTR_MAC]);
	params.supported_rates =
		nla_data(info->attrs[NL80211_ATTR_STA_SUPPORTED_RATES]);
	params.supported_rates_len =
		nla_len(info->attrs[NL80211_ATTR_STA_SUPPORTED_RATES]);
	params.listen_interval =
		nla_get_u16(info->attrs[NL80211_ATTR_STA_LISTEN_INTERVAL]);

	if (info->attrs[NL80211_ATTR_STA_SUPPORT_P2P_PS]) {
		params.support_p2p_ps =
			nla_get_u8(info->attrs[NL80211_ATTR_STA_SUPPORT_P2P_PS]);
	} else {
		/*
		 * if not specified, assume it's supported for P2P GO interface,
		 * and is NOT supported for AP interface
		 */
		params.support_p2p_ps =
			dev->ieee80211_ptr->iftype == NL80211_IFTYPE_P2P_GO;
	}

	if (info->attrs[NL80211_ATTR_PEER_AID])
		params.aid = nla_get_u16(info->attrs[NL80211_ATTR_PEER_AID]);
	else
		params.aid = nla_get_u16(info->attrs[NL80211_ATTR_STA_AID]);

	if (info->attrs[NL80211_ATTR_STA_CAPABILITY]) {
		params.capability =
			nla_get_u16(info->attrs[NL80211_ATTR_STA_CAPABILITY]);
		params.sta_modify_mask |= STATION_PARAM_APPLY_CAPABILITY;
	}

	if (info->attrs[NL80211_ATTR_STA_EXT_CAPABILITY]) {
		params.ext_capab =
			nla_data(info->attrs[NL80211_ATTR_STA_EXT_CAPABILITY]);
		params.ext_capab_len =
			nla_len(info->attrs[NL80211_ATTR_STA_EXT_CAPABILITY]);
	}

	if (info->attrs[NL80211_ATTR_HT_CAPABILITY])
		params.ht_capa =
			nla_data(info->attrs[NL80211_ATTR_HT_CAPABILITY]);

	if (info->attrs[NL80211_ATTR_VHT_CAPABILITY])
		params.vht_capa =
			nla_data(info->attrs[NL80211_ATTR_VHT_CAPABILITY]);

	if (info->attrs[NL80211_ATTR_HE_CAPABILITY]) {
		params.he_capa =
			nla_data(info->attrs[NL80211_ATTR_HE_CAPABILITY]);
		params.he_capa_len =
			nla_len(info->attrs[NL80211_ATTR_HE_CAPABILITY]);

		/* max len is validated in nla policy */
		if (params.he_capa_len < NL80211_HE_MIN_CAPABILITY_LEN)
			return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_OPMODE_NOTIF]) {
		params.opmode_notif_used = true;
		params.opmode_notif =
			nla_get_u8(info->attrs[NL80211_ATTR_OPMODE_NOTIF]);
	}

	if (info->attrs[NL80211_ATTR_STA_PLINK_ACTION])
		params.plink_action =
			nla_get_u8(info->attrs[NL80211_ATTR_STA_PLINK_ACTION]);

	if (info->attrs[NL80211_ATTR_AIRTIME_WEIGHT])
		params.airtime_weight =
			nla_get_u16(info->attrs[NL80211_ATTR_AIRTIME_WEIGHT]);

	if (params.airtime_weight &&
	    !wiphy_ext_feature_isset(&rdev->wiphy,
				     NL80211_EXT_FEATURE_AIRTIME_FAIRNESS))
		return -EOPNOTSUPP;

	err = nl80211_parse_sta_txpower_setting(info, &params);
	if (err)
		return err;

	err = nl80211_parse_sta_channel_info(info, &params);
	if (err)
		return err;

	err = nl80211_parse_sta_wme(info, &params);
	if (err)
		return err;

	if (parse_station_flags(info, dev->ieee80211_ptr->iftype, &params))
		return -EINVAL;

	/* HT/VHT requires QoS, but if we don't have that just ignore HT/VHT
	 * as userspace might just pass through the capabilities from the IEs
	 * directly, rather than enforcing this restriction and returning an
	 * error in this case.
	 */
	if (!(params.sta_flags_set & BIT(NL80211_STA_FLAG_WME))) {
		params.ht_capa = NULL;
		params.vht_capa = NULL;

		/* HE requires WME */
		if (params.he_capa_len)
			return -EINVAL;
	}

	/* When you run into this, adjust the code below for the new flag */
	BUILD_BUG_ON(NL80211_STA_FLAG_MAX != 7);

	switch (dev->ieee80211_ptr->iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_GO:
		/* ignore WME attributes if iface/sta is not capable */
		if (!(rdev->wiphy.flags & WIPHY_FLAG_AP_UAPSD) ||
		    !(params.sta_flags_set & BIT(NL80211_STA_FLAG_WME)))
			params.sta_modify_mask &= ~STATION_PARAM_APPLY_UAPSD;

		/* TDLS peers cannot be added */
		if ((params.sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER)) ||
		    info->attrs[NL80211_ATTR_PEER_AID])
			return -EINVAL;
		/* but don't bother the driver with it */
		params.sta_flags_mask &= ~BIT(NL80211_STA_FLAG_TDLS_PEER);

		/* allow authenticated/associated only if driver handles it */
		if (!(rdev->wiphy.features &
				NL80211_FEATURE_FULL_AP_CLIENT_STATE) &&
		    params.sta_flags_mask & auth_assoc)
			return -EINVAL;

		/* Older userspace, or userspace wanting to be compatible with
		 * !NL80211_FEATURE_FULL_AP_CLIENT_STATE, will not set the auth
		 * and assoc flags in the mask, but assumes the station will be
		 * added as associated anyway since this was the required driver
		 * behaviour before NL80211_FEATURE_FULL_AP_CLIENT_STATE was
		 * introduced.
		 * In order to not bother drivers with this quirk in the API
		 * set the flags in both the mask and set for new stations in
		 * this case.
		 */
		if (!(params.sta_flags_mask & auth_assoc)) {
			params.sta_flags_mask |= auth_assoc;
			params.sta_flags_set |= auth_assoc;
		}

		/* must be last in here for error handling */
		params.vlan = get_vlan(info, rdev);
		if (IS_ERR(params.vlan))
			return PTR_ERR(params.vlan);
		break;
	case NL80211_IFTYPE_MESH_POINT:
		/* ignore uAPSD data */
		params.sta_modify_mask &= ~STATION_PARAM_APPLY_UAPSD;

		/* associated is disallowed */
		if (params.sta_flags_mask & BIT(NL80211_STA_FLAG_ASSOCIATED))
			return -EINVAL;
		/* TDLS peers cannot be added */
		if ((params.sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER)) ||
		    info->attrs[NL80211_ATTR_PEER_AID])
			return -EINVAL;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		/* ignore uAPSD data */
		params.sta_modify_mask &= ~STATION_PARAM_APPLY_UAPSD;

		/* these are disallowed */
		if (params.sta_flags_mask &
				(BIT(NL80211_STA_FLAG_ASSOCIATED) |
				 BIT(NL80211_STA_FLAG_AUTHENTICATED)))
			return -EINVAL;
		/* Only TDLS peers can be added */
		if (!(params.sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER)))
			return -EINVAL;
		/* Can only add if TDLS ... */
		if (!(rdev->wiphy.flags & WIPHY_FLAG_SUPPORTS_TDLS))
			return -EOPNOTSUPP;
		/* ... with external setup is supported */
		if (!(rdev->wiphy.flags & WIPHY_FLAG_TDLS_EXTERNAL_SETUP))
			return -EOPNOTSUPP;
		/*
		 * Older wpa_supplicant versions always mark the TDLS peer
		 * as authorized, but it shouldn't yet be.
		 */
		params.sta_flags_mask &= ~BIT(NL80211_STA_FLAG_AUTHORIZED);
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* be aware of params.vlan when changing code here */

	err = rdev_add_station(rdev, dev, mac_addr, &params);

	if (params.vlan)
		dev_put(params.vlan);
	return err;
}

static int nl80211_del_station(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct station_del_parameters params;

	memset(&params, 0, sizeof(params));

	if (info->attrs[NL80211_ATTR_MAC])
		params.mac = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP_VLAN &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
		return -EINVAL;

	if (!rdev->ops->del_station)
		return -EOPNOTSUPP;

	if (info->attrs[NL80211_ATTR_MGMT_SUBTYPE]) {
		params.subtype =
			nla_get_u8(info->attrs[NL80211_ATTR_MGMT_SUBTYPE]);
		if (params.subtype != IEEE80211_STYPE_DISASSOC >> 4 &&
		    params.subtype != IEEE80211_STYPE_DEAUTH >> 4)
			return -EINVAL;
	} else {
		/* Default to Deauthentication frame */
		params.subtype = IEEE80211_STYPE_DEAUTH >> 4;
	}

	if (info->attrs[NL80211_ATTR_REASON_CODE]) {
		params.reason_code =
			nla_get_u16(info->attrs[NL80211_ATTR_REASON_CODE]);
		if (params.reason_code == 0)
			return -EINVAL; /* 0 is reserved */
	} else {
		/* Default to reason code 2 */
		params.reason_code = WLAN_REASON_PREV_AUTH_NOT_VALID;
	}

	return rdev_del_station(rdev, dev, &params);
}

static int nl80211_send_mpath(struct sk_buff *msg, u32 portid, u32 seq,
				int flags, struct net_device *dev,
				u8 *dst, u8 *next_hop,
				struct mpath_info *pinfo)
{
	void *hdr;
	struct nlattr *pinfoattr;

	hdr = nl80211hdr_put(msg, portid, seq, flags, NL80211_CMD_NEW_MPATH);
	if (!hdr)
		return -1;

	if (nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, dst) ||
	    nla_put(msg, NL80211_ATTR_MPATH_NEXT_HOP, ETH_ALEN, next_hop) ||
	    nla_put_u32(msg, NL80211_ATTR_GENERATION, pinfo->generation))
		goto nla_put_failure;

	pinfoattr = nla_nest_start_noflag(msg, NL80211_ATTR_MPATH_INFO);
	if (!pinfoattr)
		goto nla_put_failure;
	if ((pinfo->filled & MPATH_INFO_FRAME_QLEN) &&
	    nla_put_u32(msg, NL80211_MPATH_INFO_FRAME_QLEN,
			pinfo->frame_qlen))
		goto nla_put_failure;
	if (((pinfo->filled & MPATH_INFO_SN) &&
	     nla_put_u32(msg, NL80211_MPATH_INFO_SN, pinfo->sn)) ||
	    ((pinfo->filled & MPATH_INFO_METRIC) &&
	     nla_put_u32(msg, NL80211_MPATH_INFO_METRIC,
			 pinfo->metric)) ||
	    ((pinfo->filled & MPATH_INFO_EXPTIME) &&
	     nla_put_u32(msg, NL80211_MPATH_INFO_EXPTIME,
			 pinfo->exptime)) ||
	    ((pinfo->filled & MPATH_INFO_FLAGS) &&
	     nla_put_u8(msg, NL80211_MPATH_INFO_FLAGS,
			pinfo->flags)) ||
	    ((pinfo->filled & MPATH_INFO_DISCOVERY_TIMEOUT) &&
	     nla_put_u32(msg, NL80211_MPATH_INFO_DISCOVERY_TIMEOUT,
			 pinfo->discovery_timeout)) ||
	    ((pinfo->filled & MPATH_INFO_DISCOVERY_RETRIES) &&
	     nla_put_u8(msg, NL80211_MPATH_INFO_DISCOVERY_RETRIES,
			pinfo->discovery_retries)) ||
	    ((pinfo->filled & MPATH_INFO_HOP_COUNT) &&
	     nla_put_u8(msg, NL80211_MPATH_INFO_HOP_COUNT,
			pinfo->hop_count)) ||
	    ((pinfo->filled & MPATH_INFO_PATH_CHANGE) &&
	     nla_put_u32(msg, NL80211_MPATH_INFO_PATH_CHANGE,
			 pinfo->path_change_count)))
		goto nla_put_failure;

	nla_nest_end(msg, pinfoattr);

	genlmsg_end(msg, hdr);
	return 0;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nl80211_dump_mpath(struct sk_buff *skb,
			      struct netlink_callback *cb)
{
	struct mpath_info pinfo;
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *wdev;
	u8 dst[ETH_ALEN];
	u8 next_hop[ETH_ALEN];
	int path_idx = cb->args[2];
	int err;

	rtnl_lock();
	err = nl80211_prepare_wdev_dump(cb, &rdev, &wdev);
	if (err)
		goto out_err;

	if (!rdev->ops->dump_mpath) {
		err = -EOPNOTSUPP;
		goto out_err;
	}

	if (wdev->iftype != NL80211_IFTYPE_MESH_POINT) {
		err = -EOPNOTSUPP;
		goto out_err;
	}

	while (1) {
		err = rdev_dump_mpath(rdev, wdev->netdev, path_idx, dst,
				      next_hop, &pinfo);
		if (err == -ENOENT)
			break;
		if (err)
			goto out_err;

		if (nl80211_send_mpath(skb, NETLINK_CB(cb->skb).portid,
				       cb->nlh->nlmsg_seq, NLM_F_MULTI,
				       wdev->netdev, dst, next_hop,
				       &pinfo) < 0)
			goto out;

		path_idx++;
	}

 out:
	cb->args[2] = path_idx;
	err = skb->len;
 out_err:
	rtnl_unlock();
	return err;
}

static int nl80211_get_mpath(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	int err;
	struct net_device *dev = info->user_ptr[1];
	struct mpath_info pinfo;
	struct sk_buff *msg;
	u8 *dst = NULL;
	u8 next_hop[ETH_ALEN];

	memset(&pinfo, 0, sizeof(pinfo));

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	dst = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (!rdev->ops->get_mpath)
		return -EOPNOTSUPP;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT)
		return -EOPNOTSUPP;

	err = rdev_get_mpath(rdev, dev, dst, next_hop, &pinfo);
	if (err)
		return err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	if (nl80211_send_mpath(msg, info->snd_portid, info->snd_seq, 0,
				 dev, dst, next_hop, &pinfo) < 0) {
		nlmsg_free(msg);
		return -ENOBUFS;
	}

	return genlmsg_reply(msg, info);
}

static int nl80211_set_mpath(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	u8 *dst = NULL;
	u8 *next_hop = NULL;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_MPATH_NEXT_HOP])
		return -EINVAL;

	dst = nla_data(info->attrs[NL80211_ATTR_MAC]);
	next_hop = nla_data(info->attrs[NL80211_ATTR_MPATH_NEXT_HOP]);

	if (!rdev->ops->change_mpath)
		return -EOPNOTSUPP;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT)
		return -EOPNOTSUPP;

	return rdev_change_mpath(rdev, dev, dst, next_hop);
}

static int nl80211_new_mpath(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	u8 *dst = NULL;
	u8 *next_hop = NULL;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_MPATH_NEXT_HOP])
		return -EINVAL;

	dst = nla_data(info->attrs[NL80211_ATTR_MAC]);
	next_hop = nla_data(info->attrs[NL80211_ATTR_MPATH_NEXT_HOP]);

	if (!rdev->ops->add_mpath)
		return -EOPNOTSUPP;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT)
		return -EOPNOTSUPP;

	return rdev_add_mpath(rdev, dev, dst, next_hop);
}

static int nl80211_del_mpath(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	u8 *dst = NULL;

	if (info->attrs[NL80211_ATTR_MAC])
		dst = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (!rdev->ops->del_mpath)
		return -EOPNOTSUPP;

	return rdev_del_mpath(rdev, dev, dst);
}

static int nl80211_get_mpp(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	int err;
	struct net_device *dev = info->user_ptr[1];
	struct mpath_info pinfo;
	struct sk_buff *msg;
	u8 *dst = NULL;
	u8 mpp[ETH_ALEN];

	memset(&pinfo, 0, sizeof(pinfo));

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	dst = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (!rdev->ops->get_mpp)
		return -EOPNOTSUPP;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT)
		return -EOPNOTSUPP;

	err = rdev_get_mpp(rdev, dev, dst, mpp, &pinfo);
	if (err)
		return err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	if (nl80211_send_mpath(msg, info->snd_portid, info->snd_seq, 0,
			       dev, dst, mpp, &pinfo) < 0) {
		nlmsg_free(msg);
		return -ENOBUFS;
	}

	return genlmsg_reply(msg, info);
}

static int nl80211_dump_mpp(struct sk_buff *skb,
			    struct netlink_callback *cb)
{
	struct mpath_info pinfo;
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *wdev;
	u8 dst[ETH_ALEN];
	u8 mpp[ETH_ALEN];
	int path_idx = cb->args[2];
	int err;

	rtnl_lock();
	err = nl80211_prepare_wdev_dump(cb, &rdev, &wdev);
	if (err)
		goto out_err;

	if (!rdev->ops->dump_mpp) {
		err = -EOPNOTSUPP;
		goto out_err;
	}

	if (wdev->iftype != NL80211_IFTYPE_MESH_POINT) {
		err = -EOPNOTSUPP;
		goto out_err;
	}

	while (1) {
		err = rdev_dump_mpp(rdev, wdev->netdev, path_idx, dst,
				    mpp, &pinfo);
		if (err == -ENOENT)
			break;
		if (err)
			goto out_err;

		if (nl80211_send_mpath(skb, NETLINK_CB(cb->skb).portid,
				       cb->nlh->nlmsg_seq, NLM_F_MULTI,
				       wdev->netdev, dst, mpp,
				       &pinfo) < 0)
			goto out;

		path_idx++;
	}

 out:
	cb->args[2] = path_idx;
	err = skb->len;
 out_err:
	rtnl_unlock();
	return err;
}

static int nl80211_set_bss(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct bss_parameters params;
	int err;

	memset(&params, 0, sizeof(params));
	/* default to not changing parameters */
	params.use_cts_prot = -1;
	params.use_short_preamble = -1;
	params.use_short_slot_time = -1;
	params.ap_isolate = -1;
	params.ht_opmode = -1;
	params.p2p_ctwindow = -1;
	params.p2p_opp_ps = -1;

	if (info->attrs[NL80211_ATTR_BSS_CTS_PROT])
		params.use_cts_prot =
		    nla_get_u8(info->attrs[NL80211_ATTR_BSS_CTS_PROT]);
	if (info->attrs[NL80211_ATTR_BSS_SHORT_PREAMBLE])
		params.use_short_preamble =
		    nla_get_u8(info->attrs[NL80211_ATTR_BSS_SHORT_PREAMBLE]);
	if (info->attrs[NL80211_ATTR_BSS_SHORT_SLOT_TIME])
		params.use_short_slot_time =
		    nla_get_u8(info->attrs[NL80211_ATTR_BSS_SHORT_SLOT_TIME]);
	if (info->attrs[NL80211_ATTR_BSS_BASIC_RATES]) {
		params.basic_rates =
			nla_data(info->attrs[NL80211_ATTR_BSS_BASIC_RATES]);
		params.basic_rates_len =
			nla_len(info->attrs[NL80211_ATTR_BSS_BASIC_RATES]);
	}
	if (info->attrs[NL80211_ATTR_AP_ISOLATE])
		params.ap_isolate = !!nla_get_u8(info->attrs[NL80211_ATTR_AP_ISOLATE]);
	if (info->attrs[NL80211_ATTR_BSS_HT_OPMODE])
		params.ht_opmode =
			nla_get_u16(info->attrs[NL80211_ATTR_BSS_HT_OPMODE]);

	if (info->attrs[NL80211_ATTR_P2P_CTWINDOW]) {
		if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
			return -EINVAL;
		params.p2p_ctwindow =
			nla_get_u8(info->attrs[NL80211_ATTR_P2P_CTWINDOW]);
		if (params.p2p_ctwindow != 0 &&
		    !(rdev->wiphy.features & NL80211_FEATURE_P2P_GO_CTWIN))
			return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_P2P_OPPPS]) {
		u8 tmp;

		if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
			return -EINVAL;
		tmp = nla_get_u8(info->attrs[NL80211_ATTR_P2P_OPPPS]);
		params.p2p_opp_ps = tmp;
		if (params.p2p_opp_ps &&
		    !(rdev->wiphy.features & NL80211_FEATURE_P2P_GO_OPPPS))
			return -EINVAL;
	}

	if (!rdev->ops->change_bss)
		return -EOPNOTSUPP;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
		return -EOPNOTSUPP;

	wdev_lock(wdev);
	err = rdev_change_bss(rdev, dev, &params);
	wdev_unlock(wdev);

	return err;
}

static int nl80211_req_set_reg(struct sk_buff *skb, struct genl_info *info)
{
	char *data = NULL;
	bool is_indoor;
	enum nl80211_user_reg_hint_type user_reg_hint_type;
	u32 owner_nlportid;

	/*
	 * You should only get this when cfg80211 hasn't yet initialized
	 * completely when built-in to the kernel right between the time
	 * window between nl80211_init() and regulatory_init(), if that is
	 * even possible.
	 */
	if (unlikely(!rcu_access_pointer(cfg80211_regdomain)))
		return -EINPROGRESS;

	if (info->attrs[NL80211_ATTR_USER_REG_HINT_TYPE])
		user_reg_hint_type =
		  nla_get_u32(info->attrs[NL80211_ATTR_USER_REG_HINT_TYPE]);
	else
		user_reg_hint_type = NL80211_USER_REG_HINT_USER;

	switch (user_reg_hint_type) {
	case NL80211_USER_REG_HINT_USER:
	case NL80211_USER_REG_HINT_CELL_BASE:
		if (!info->attrs[NL80211_ATTR_REG_ALPHA2])
			return -EINVAL;

		data = nla_data(info->attrs[NL80211_ATTR_REG_ALPHA2]);
		return regulatory_hint_user(data, user_reg_hint_type);
	case NL80211_USER_REG_HINT_INDOOR:
		if (info->attrs[NL80211_ATTR_SOCKET_OWNER]) {
			owner_nlportid = info->snd_portid;
			is_indoor = !!info->attrs[NL80211_ATTR_REG_INDOOR];
		} else {
			owner_nlportid = 0;
			is_indoor = true;
		}

		return regulatory_hint_indoor(is_indoor, owner_nlportid);
	default:
		return -EINVAL;
	}
}

static int nl80211_reload_regdb(struct sk_buff *skb, struct genl_info *info)
{
	return reg_reload_regdb();
}

static int nl80211_get_mesh_config(struct sk_buff *skb,
				   struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct mesh_config cur_params;
	int err = 0;
	void *hdr;
	struct nlattr *pinfoattr;
	struct sk_buff *msg;

	if (wdev->iftype != NL80211_IFTYPE_MESH_POINT)
		return -EOPNOTSUPP;

	if (!rdev->ops->get_mesh_config)
		return -EOPNOTSUPP;

	wdev_lock(wdev);
	/* If not connected, get default parameters */
	if (!wdev->mesh_id_len)
		memcpy(&cur_params, &default_mesh_config, sizeof(cur_params));
	else
		err = rdev_get_mesh_config(rdev, dev, &cur_params);
	wdev_unlock(wdev);

	if (err)
		return err;

	/* Draw up a netlink message to send back */
	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;
	hdr = nl80211hdr_put(msg, info->snd_portid, info->snd_seq, 0,
			     NL80211_CMD_GET_MESH_CONFIG);
	if (!hdr)
		goto out;
	pinfoattr = nla_nest_start_noflag(msg, NL80211_ATTR_MESH_CONFIG);
	if (!pinfoattr)
		goto nla_put_failure;
	if (nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex) ||
	    nla_put_u16(msg, NL80211_MESHCONF_RETRY_TIMEOUT,
			cur_params.dot11MeshRetryTimeout) ||
	    nla_put_u16(msg, NL80211_MESHCONF_CONFIRM_TIMEOUT,
			cur_params.dot11MeshConfirmTimeout) ||
	    nla_put_u16(msg, NL80211_MESHCONF_HOLDING_TIMEOUT,
			cur_params.dot11MeshHoldingTimeout) ||
	    nla_put_u16(msg, NL80211_MESHCONF_MAX_PEER_LINKS,
			cur_params.dot11MeshMaxPeerLinks) ||
	    nla_put_u8(msg, NL80211_MESHCONF_MAX_RETRIES,
		       cur_params.dot11MeshMaxRetries) ||
	    nla_put_u8(msg, NL80211_MESHCONF_TTL,
		       cur_params.dot11MeshTTL) ||
	    nla_put_u8(msg, NL80211_MESHCONF_ELEMENT_TTL,
		       cur_params.element_ttl) ||
	    nla_put_u8(msg, NL80211_MESHCONF_AUTO_OPEN_PLINKS,
		       cur_params.auto_open_plinks) ||
	    nla_put_u32(msg, NL80211_MESHCONF_SYNC_OFFSET_MAX_NEIGHBOR,
			cur_params.dot11MeshNbrOffsetMaxNeighbor) ||
	    nla_put_u8(msg, NL80211_MESHCONF_HWMP_MAX_PREQ_RETRIES,
		       cur_params.dot11MeshHWMPmaxPREQretries) ||
	    nla_put_u32(msg, NL80211_MESHCONF_PATH_REFRESH_TIME,
			cur_params.path_refresh_time) ||
	    nla_put_u16(msg, NL80211_MESHCONF_MIN_DISCOVERY_TIMEOUT,
			cur_params.min_discovery_timeout) ||
	    nla_put_u32(msg, NL80211_MESHCONF_HWMP_ACTIVE_PATH_TIMEOUT,
			cur_params.dot11MeshHWMPactivePathTimeout) ||
	    nla_put_u16(msg, NL80211_MESHCONF_HWMP_PREQ_MIN_INTERVAL,
			cur_params.dot11MeshHWMPpreqMinInterval) ||
	    nla_put_u16(msg, NL80211_MESHCONF_HWMP_PERR_MIN_INTERVAL,
			cur_params.dot11MeshHWMPperrMinInterval) ||
	    nla_put_u16(msg, NL80211_MESHCONF_HWMP_NET_DIAM_TRVS_TIME,
			cur_params.dot11MeshHWMPnetDiameterTraversalTime) ||
	    nla_put_u8(msg, NL80211_MESHCONF_HWMP_ROOTMODE,
		       cur_params.dot11MeshHWMPRootMode) ||
	    nla_put_u16(msg, NL80211_MESHCONF_HWMP_RANN_INTERVAL,
			cur_params.dot11MeshHWMPRannInterval) ||
	    nla_put_u8(msg, NL80211_MESHCONF_GATE_ANNOUNCEMENTS,
		       cur_params.dot11MeshGateAnnouncementProtocol) ||
	    nla_put_u8(msg, NL80211_MESHCONF_FORWARDING,
		       cur_params.dot11MeshForwarding) ||
	    nla_put_s32(msg, NL80211_MESHCONF_RSSI_THRESHOLD,
			cur_params.rssi_threshold) ||
	    nla_put_u32(msg, NL80211_MESHCONF_HT_OPMODE,
			cur_params.ht_opmode) ||
	    nla_put_u32(msg, NL80211_MESHCONF_HWMP_PATH_TO_ROOT_TIMEOUT,
			cur_params.dot11MeshHWMPactivePathToRootTimeout) ||
	    nla_put_u16(msg, NL80211_MESHCONF_HWMP_ROOT_INTERVAL,
			cur_params.dot11MeshHWMProotInterval) ||
	    nla_put_u16(msg, NL80211_MESHCONF_HWMP_CONFIRMATION_INTERVAL,
			cur_params.dot11MeshHWMPconfirmationInterval) ||
	    nla_put_u32(msg, NL80211_MESHCONF_POWER_MODE,
			cur_params.power_mode) ||
	    nla_put_u16(msg, NL80211_MESHCONF_AWAKE_WINDOW,
			cur_params.dot11MeshAwakeWindowDuration) ||
	    nla_put_u32(msg, NL80211_MESHCONF_PLINK_TIMEOUT,
			cur_params.plink_timeout) ||
	    nla_put_u8(msg, NL80211_MESHCONF_CONNECTED_TO_GATE,
		       cur_params.dot11MeshConnectedToMeshGate))
		goto nla_put_failure;
	nla_nest_end(msg, pinfoattr);
	genlmsg_end(msg, hdr);
	return genlmsg_reply(msg, info);

 nla_put_failure:
 out:
	nlmsg_free(msg);
	return -ENOBUFS;
}

static const struct nla_policy
nl80211_meshconf_params_policy[NL80211_MESHCONF_ATTR_MAX+1] = {
	[NL80211_MESHCONF_RETRY_TIMEOUT] =
		NLA_POLICY_RANGE(NLA_U16, 1, 255),
	[NL80211_MESHCONF_CONFIRM_TIMEOUT] =
		NLA_POLICY_RANGE(NLA_U16, 1, 255),
	[NL80211_MESHCONF_HOLDING_TIMEOUT] =
		NLA_POLICY_RANGE(NLA_U16, 1, 255),
	[NL80211_MESHCONF_MAX_PEER_LINKS] =
		NLA_POLICY_RANGE(NLA_U16, 0, 255),
	[NL80211_MESHCONF_MAX_RETRIES] = NLA_POLICY_MAX(NLA_U8, 16),
	[NL80211_MESHCONF_TTL] = NLA_POLICY_MIN(NLA_U8, 1),
	[NL80211_MESHCONF_ELEMENT_TTL] = NLA_POLICY_MIN(NLA_U8, 1),
	[NL80211_MESHCONF_AUTO_OPEN_PLINKS] = NLA_POLICY_MAX(NLA_U8, 1),
	[NL80211_MESHCONF_SYNC_OFFSET_MAX_NEIGHBOR] =
		NLA_POLICY_RANGE(NLA_U32, 1, 255),
	[NL80211_MESHCONF_HWMP_MAX_PREQ_RETRIES] = { .type = NLA_U8 },
	[NL80211_MESHCONF_PATH_REFRESH_TIME] = { .type = NLA_U32 },
	[NL80211_MESHCONF_MIN_DISCOVERY_TIMEOUT] = NLA_POLICY_MIN(NLA_U16, 1),
	[NL80211_MESHCONF_HWMP_ACTIVE_PATH_TIMEOUT] = { .type = NLA_U32 },
	[NL80211_MESHCONF_HWMP_PREQ_MIN_INTERVAL] =
		NLA_POLICY_MIN(NLA_U16, 1),
	[NL80211_MESHCONF_HWMP_PERR_MIN_INTERVAL] =
		NLA_POLICY_MIN(NLA_U16, 1),
	[NL80211_MESHCONF_HWMP_NET_DIAM_TRVS_TIME] =
		NLA_POLICY_MIN(NLA_U16, 1),
	[NL80211_MESHCONF_HWMP_ROOTMODE] = NLA_POLICY_MAX(NLA_U8, 4),
	[NL80211_MESHCONF_HWMP_RANN_INTERVAL] =
		NLA_POLICY_MIN(NLA_U16, 1),
	[NL80211_MESHCONF_GATE_ANNOUNCEMENTS] = NLA_POLICY_MAX(NLA_U8, 1),
	[NL80211_MESHCONF_FORWARDING] = NLA_POLICY_MAX(NLA_U8, 1),
	[NL80211_MESHCONF_RSSI_THRESHOLD] =
		NLA_POLICY_RANGE(NLA_S32, -255, 0),
	[NL80211_MESHCONF_HT_OPMODE] = { .type = NLA_U16 },
	[NL80211_MESHCONF_HWMP_PATH_TO_ROOT_TIMEOUT] = { .type = NLA_U32 },
	[NL80211_MESHCONF_HWMP_ROOT_INTERVAL] =
		NLA_POLICY_MIN(NLA_U16, 1),
	[NL80211_MESHCONF_HWMP_CONFIRMATION_INTERVAL] =
		NLA_POLICY_MIN(NLA_U16, 1),
	[NL80211_MESHCONF_POWER_MODE] =
		NLA_POLICY_RANGE(NLA_U32,
				 NL80211_MESH_POWER_ACTIVE,
				 NL80211_MESH_POWER_MAX),
	[NL80211_MESHCONF_AWAKE_WINDOW] = { .type = NLA_U16 },
	[NL80211_MESHCONF_PLINK_TIMEOUT] = { .type = NLA_U32 },
	[NL80211_MESHCONF_CONNECTED_TO_GATE] = NLA_POLICY_RANGE(NLA_U8, 0, 1),
};

static const struct nla_policy
	nl80211_mesh_setup_params_policy[NL80211_MESH_SETUP_ATTR_MAX+1] = {
	[NL80211_MESH_SETUP_ENABLE_VENDOR_SYNC] = { .type = NLA_U8 },
	[NL80211_MESH_SETUP_ENABLE_VENDOR_PATH_SEL] = { .type = NLA_U8 },
	[NL80211_MESH_SETUP_ENABLE_VENDOR_METRIC] = { .type = NLA_U8 },
	[NL80211_MESH_SETUP_USERSPACE_AUTH] = { .type = NLA_FLAG },
	[NL80211_MESH_SETUP_AUTH_PROTOCOL] = { .type = NLA_U8 },
	[NL80211_MESH_SETUP_USERSPACE_MPM] = { .type = NLA_FLAG },
	[NL80211_MESH_SETUP_IE] =
		NLA_POLICY_VALIDATE_FN(NLA_BINARY, validate_ie_attr,
				       IEEE80211_MAX_DATA_LEN),
	[NL80211_MESH_SETUP_USERSPACE_AMPE] = { .type = NLA_FLAG },
};

static int nl80211_parse_mesh_config(struct genl_info *info,
				     struct mesh_config *cfg,
				     u32 *mask_out)
{
	struct nlattr *tb[NL80211_MESHCONF_ATTR_MAX + 1];
	u32 mask = 0;
	u16 ht_opmode;

#define FILL_IN_MESH_PARAM_IF_SET(tb, cfg, param, mask, attr, fn)	\
do {									\
	if (tb[attr]) {							\
		cfg->param = fn(tb[attr]);				\
		mask |= BIT((attr) - 1);				\
	}								\
} while (0)

	if (!info->attrs[NL80211_ATTR_MESH_CONFIG])
		return -EINVAL;
	if (nla_parse_nested_deprecated(tb, NL80211_MESHCONF_ATTR_MAX, info->attrs[NL80211_ATTR_MESH_CONFIG], nl80211_meshconf_params_policy, info->extack))
		return -EINVAL;

	/* This makes sure that there aren't more than 32 mesh config
	 * parameters (otherwise our bitfield scheme would not work.) */
	BUILD_BUG_ON(NL80211_MESHCONF_ATTR_MAX > 32);

	/* Fill in the params struct */
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshRetryTimeout, mask,
				  NL80211_MESHCONF_RETRY_TIMEOUT, nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshConfirmTimeout, mask,
				  NL80211_MESHCONF_CONFIRM_TIMEOUT,
				  nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshHoldingTimeout, mask,
				  NL80211_MESHCONF_HOLDING_TIMEOUT,
				  nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshMaxPeerLinks, mask,
				  NL80211_MESHCONF_MAX_PEER_LINKS,
				  nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshMaxRetries, mask,
				  NL80211_MESHCONF_MAX_RETRIES, nla_get_u8);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshTTL, mask,
				  NL80211_MESHCONF_TTL, nla_get_u8);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, element_ttl, mask,
				  NL80211_MESHCONF_ELEMENT_TTL, nla_get_u8);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, auto_open_plinks, mask,
				  NL80211_MESHCONF_AUTO_OPEN_PLINKS,
				  nla_get_u8);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshNbrOffsetMaxNeighbor,
				  mask,
				  NL80211_MESHCONF_SYNC_OFFSET_MAX_NEIGHBOR,
				  nla_get_u32);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshHWMPmaxPREQretries, mask,
				  NL80211_MESHCONF_HWMP_MAX_PREQ_RETRIES,
				  nla_get_u8);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, path_refresh_time, mask,
				  NL80211_MESHCONF_PATH_REFRESH_TIME,
				  nla_get_u32);
	if (mask & BIT(NL80211_MESHCONF_PATH_REFRESH_TIME) &&
	    (cfg->path_refresh_time < 1 || cfg->path_refresh_time > 65535))
		return -EINVAL;
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, min_discovery_timeout, mask,
				  NL80211_MESHCONF_MIN_DISCOVERY_TIMEOUT,
				  nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshHWMPactivePathTimeout,
				  mask,
				  NL80211_MESHCONF_HWMP_ACTIVE_PATH_TIMEOUT,
				  nla_get_u32);
	if (mask & BIT(NL80211_MESHCONF_HWMP_ACTIVE_PATH_TIMEOUT) &&
	    (cfg->dot11MeshHWMPactivePathTimeout < 1 ||
	     cfg->dot11MeshHWMPactivePathTimeout > 65535))
		return -EINVAL;
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshHWMPpreqMinInterval, mask,
				  NL80211_MESHCONF_HWMP_PREQ_MIN_INTERVAL,
				  nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshHWMPperrMinInterval, mask,
				  NL80211_MESHCONF_HWMP_PERR_MIN_INTERVAL,
				  nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg,
				  dot11MeshHWMPnetDiameterTraversalTime, mask,
				  NL80211_MESHCONF_HWMP_NET_DIAM_TRVS_TIME,
				  nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshHWMPRootMode, mask,
				  NL80211_MESHCONF_HWMP_ROOTMODE, nla_get_u8);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshHWMPRannInterval, mask,
				  NL80211_MESHCONF_HWMP_RANN_INTERVAL,
				  nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshGateAnnouncementProtocol,
				  mask, NL80211_MESHCONF_GATE_ANNOUNCEMENTS,
				  nla_get_u8);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshForwarding, mask,
				  NL80211_MESHCONF_FORWARDING, nla_get_u8);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, rssi_threshold, mask,
				  NL80211_MESHCONF_RSSI_THRESHOLD,
				  nla_get_s32);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshConnectedToMeshGate, mask,
				  NL80211_MESHCONF_CONNECTED_TO_GATE,
				  nla_get_u8);
	/*
	 * Check HT operation mode based on
	 * IEEE 802.11-2016 9.4.2.57 HT Operation element.
	 */
	if (tb[NL80211_MESHCONF_HT_OPMODE]) {
		ht_opmode = nla_get_u16(tb[NL80211_MESHCONF_HT_OPMODE]);

		if (ht_opmode & ~(IEEE80211_HT_OP_MODE_PROTECTION |
				  IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT |
				  IEEE80211_HT_OP_MODE_NON_HT_STA_PRSNT))
			return -EINVAL;

		/* NON_HT_STA bit is reserved, but some programs set it */
		ht_opmode &= ~IEEE80211_HT_OP_MODE_NON_HT_STA_PRSNT;

		cfg->ht_opmode = ht_opmode;
		mask |= (1 << (NL80211_MESHCONF_HT_OPMODE - 1));
	}
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg,
				  dot11MeshHWMPactivePathToRootTimeout, mask,
				  NL80211_MESHCONF_HWMP_PATH_TO_ROOT_TIMEOUT,
				  nla_get_u32);
	if (mask & BIT(NL80211_MESHCONF_HWMP_PATH_TO_ROOT_TIMEOUT) &&
	    (cfg->dot11MeshHWMPactivePathToRootTimeout < 1 ||
	     cfg->dot11MeshHWMPactivePathToRootTimeout > 65535))
		return -EINVAL;
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshHWMProotInterval, mask,
				  NL80211_MESHCONF_HWMP_ROOT_INTERVAL,
				  nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshHWMPconfirmationInterval,
				  mask,
				  NL80211_MESHCONF_HWMP_CONFIRMATION_INTERVAL,
				  nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, power_mode, mask,
				  NL80211_MESHCONF_POWER_MODE, nla_get_u32);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, dot11MeshAwakeWindowDuration, mask,
				  NL80211_MESHCONF_AWAKE_WINDOW, nla_get_u16);
	FILL_IN_MESH_PARAM_IF_SET(tb, cfg, plink_timeout, mask,
				  NL80211_MESHCONF_PLINK_TIMEOUT, nla_get_u32);
	if (mask_out)
		*mask_out = mask;

	return 0;

#undef FILL_IN_MESH_PARAM_IF_SET
}

static int nl80211_parse_mesh_setup(struct genl_info *info,
				     struct mesh_setup *setup)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct nlattr *tb[NL80211_MESH_SETUP_ATTR_MAX + 1];

	if (!info->attrs[NL80211_ATTR_MESH_SETUP])
		return -EINVAL;
	if (nla_parse_nested_deprecated(tb, NL80211_MESH_SETUP_ATTR_MAX, info->attrs[NL80211_ATTR_MESH_SETUP], nl80211_mesh_setup_params_policy, info->extack))
		return -EINVAL;

	if (tb[NL80211_MESH_SETUP_ENABLE_VENDOR_SYNC])
		setup->sync_method =
		(nla_get_u8(tb[NL80211_MESH_SETUP_ENABLE_VENDOR_SYNC])) ?
		 IEEE80211_SYNC_METHOD_VENDOR :
		 IEEE80211_SYNC_METHOD_NEIGHBOR_OFFSET;

	if (tb[NL80211_MESH_SETUP_ENABLE_VENDOR_PATH_SEL])
		setup->path_sel_proto =
		(nla_get_u8(tb[NL80211_MESH_SETUP_ENABLE_VENDOR_PATH_SEL])) ?
		 IEEE80211_PATH_PROTOCOL_VENDOR :
		 IEEE80211_PATH_PROTOCOL_HWMP;

	if (tb[NL80211_MESH_SETUP_ENABLE_VENDOR_METRIC])
		setup->path_metric =
		(nla_get_u8(tb[NL80211_MESH_SETUP_ENABLE_VENDOR_METRIC])) ?
		 IEEE80211_PATH_METRIC_VENDOR :
		 IEEE80211_PATH_METRIC_AIRTIME;

	if (tb[NL80211_MESH_SETUP_IE]) {
		struct nlattr *ieattr =
			tb[NL80211_MESH_SETUP_IE];
		setup->ie = nla_data(ieattr);
		setup->ie_len = nla_len(ieattr);
	}
	if (tb[NL80211_MESH_SETUP_USERSPACE_MPM] &&
	    !(rdev->wiphy.features & NL80211_FEATURE_USERSPACE_MPM))
		return -EINVAL;
	setup->user_mpm = nla_get_flag(tb[NL80211_MESH_SETUP_USERSPACE_MPM]);
	setup->is_authenticated = nla_get_flag(tb[NL80211_MESH_SETUP_USERSPACE_AUTH]);
	setup->is_secure = nla_get_flag(tb[NL80211_MESH_SETUP_USERSPACE_AMPE]);
	if (setup->is_secure)
		setup->user_mpm = true;

	if (tb[NL80211_MESH_SETUP_AUTH_PROTOCOL]) {
		if (!setup->user_mpm)
			return -EINVAL;
		setup->auth_id =
			nla_get_u8(tb[NL80211_MESH_SETUP_AUTH_PROTOCOL]);
	}

	return 0;
}

static int nl80211_update_mesh_config(struct sk_buff *skb,
				      struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct mesh_config cfg;
	u32 mask;
	int err;

	if (wdev->iftype != NL80211_IFTYPE_MESH_POINT)
		return -EOPNOTSUPP;

	if (!rdev->ops->update_mesh_config)
		return -EOPNOTSUPP;

	err = nl80211_parse_mesh_config(info, &cfg, &mask);
	if (err)
		return err;

	wdev_lock(wdev);
	if (!wdev->mesh_id_len)
		err = -ENOLINK;

	if (!err)
		err = rdev_update_mesh_config(rdev, dev, mask, &cfg);

	wdev_unlock(wdev);

	return err;
}

static int nl80211_put_regdom(const struct ieee80211_regdomain *regdom,
			      struct sk_buff *msg)
{
	struct nlattr *nl_reg_rules;
	unsigned int i;

	if (nla_put_string(msg, NL80211_ATTR_REG_ALPHA2, regdom->alpha2) ||
	    (regdom->dfs_region &&
	     nla_put_u8(msg, NL80211_ATTR_DFS_REGION, regdom->dfs_region)))
		goto nla_put_failure;

	nl_reg_rules = nla_nest_start_noflag(msg, NL80211_ATTR_REG_RULES);
	if (!nl_reg_rules)
		goto nla_put_failure;

	for (i = 0; i < regdom->n_reg_rules; i++) {
		struct nlattr *nl_reg_rule;
		const struct ieee80211_reg_rule *reg_rule;
		const struct ieee80211_freq_range *freq_range;
		const struct ieee80211_power_rule *power_rule;
		unsigned int max_bandwidth_khz;

		reg_rule = &regdom->reg_rules[i];
		freq_range = &reg_rule->freq_range;
		power_rule = &reg_rule->power_rule;

		nl_reg_rule = nla_nest_start_noflag(msg, i);
		if (!nl_reg_rule)
			goto nla_put_failure;

		max_bandwidth_khz = freq_range->max_bandwidth_khz;
		if (!max_bandwidth_khz)
			max_bandwidth_khz = reg_get_max_bandwidth(regdom,
								  reg_rule);

		if (nla_put_u32(msg, NL80211_ATTR_REG_RULE_FLAGS,
				reg_rule->flags) ||
		    nla_put_u32(msg, NL80211_ATTR_FREQ_RANGE_START,
				freq_range->start_freq_khz) ||
		    nla_put_u32(msg, NL80211_ATTR_FREQ_RANGE_END,
				freq_range->end_freq_khz) ||
		    nla_put_u32(msg, NL80211_ATTR_FREQ_RANGE_MAX_BW,
				max_bandwidth_khz) ||
		    nla_put_u32(msg, NL80211_ATTR_POWER_RULE_MAX_ANT_GAIN,
				power_rule->max_antenna_gain) ||
		    nla_put_u32(msg, NL80211_ATTR_POWER_RULE_MAX_EIRP,
				power_rule->max_eirp) ||
		    nla_put_u32(msg, NL80211_ATTR_DFS_CAC_TIME,
				reg_rule->dfs_cac_ms))
			goto nla_put_failure;

		nla_nest_end(msg, nl_reg_rule);
	}

	nla_nest_end(msg, nl_reg_rules);
	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int nl80211_get_reg_do(struct sk_buff *skb, struct genl_info *info)
{
	const struct ieee80211_regdomain *regdom = NULL;
	struct cfg80211_registered_device *rdev;
	struct wiphy *wiphy = NULL;
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOBUFS;

	hdr = nl80211hdr_put(msg, info->snd_portid, info->snd_seq, 0,
			     NL80211_CMD_GET_REG);
	if (!hdr)
		goto put_failure;

	if (info->attrs[NL80211_ATTR_WIPHY]) {
		bool self_managed;

		rdev = cfg80211_get_dev_from_info(genl_info_net(info), info);
		if (IS_ERR(rdev)) {
			nlmsg_free(msg);
			return PTR_ERR(rdev);
		}

		wiphy = &rdev->wiphy;
		self_managed = wiphy->regulatory_flags &
			       REGULATORY_WIPHY_SELF_MANAGED;
		regdom = get_wiphy_regdom(wiphy);

		/* a self-managed-reg device must have a private regdom */
		if (WARN_ON(!regdom && self_managed)) {
			nlmsg_free(msg);
			return -EINVAL;
		}

		if (regdom &&
		    nla_put_u32(msg, NL80211_ATTR_WIPHY, get_wiphy_idx(wiphy)))
			goto nla_put_failure;
	}

	if (!wiphy && reg_last_request_cell_base() &&
	    nla_put_u32(msg, NL80211_ATTR_USER_REG_HINT_TYPE,
			NL80211_USER_REG_HINT_CELL_BASE))
		goto nla_put_failure;

	rcu_read_lock();

	if (!regdom)
		regdom = rcu_dereference(cfg80211_regdomain);

	if (nl80211_put_regdom(regdom, msg))
		goto nla_put_failure_rcu;

	rcu_read_unlock();

	genlmsg_end(msg, hdr);
	return genlmsg_reply(msg, info);

nla_put_failure_rcu:
	rcu_read_unlock();
nla_put_failure:
put_failure:
	nlmsg_free(msg);
	return -EMSGSIZE;
}

static int nl80211_send_regdom(struct sk_buff *msg, struct netlink_callback *cb,
			       u32 seq, int flags, struct wiphy *wiphy,
			       const struct ieee80211_regdomain *regdom)
{
	void *hdr = nl80211hdr_put(msg, NETLINK_CB(cb->skb).portid, seq, flags,
				   NL80211_CMD_GET_REG);

	if (!hdr)
		return -1;

	genl_dump_check_consistent(cb, hdr);

	if (nl80211_put_regdom(regdom, msg))
		goto nla_put_failure;

	if (!wiphy && reg_last_request_cell_base() &&
	    nla_put_u32(msg, NL80211_ATTR_USER_REG_HINT_TYPE,
			NL80211_USER_REG_HINT_CELL_BASE))
		goto nla_put_failure;

	if (wiphy &&
	    nla_put_u32(msg, NL80211_ATTR_WIPHY, get_wiphy_idx(wiphy)))
		goto nla_put_failure;

	if (wiphy && wiphy->regulatory_flags & REGULATORY_WIPHY_SELF_MANAGED &&
	    nla_put_flag(msg, NL80211_ATTR_WIPHY_SELF_MANAGED_REG))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nl80211_get_reg_dump(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	const struct ieee80211_regdomain *regdom = NULL;
	struct cfg80211_registered_device *rdev;
	int err, reg_idx, start = cb->args[2];

	rtnl_lock();

	if (cfg80211_regdomain && start == 0) {
		err = nl80211_send_regdom(skb, cb, cb->nlh->nlmsg_seq,
					  NLM_F_MULTI, NULL,
					  rtnl_dereference(cfg80211_regdomain));
		if (err < 0)
			goto out_err;
	}

	/* the global regdom is idx 0 */
	reg_idx = 1;
	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		regdom = get_wiphy_regdom(&rdev->wiphy);
		if (!regdom)
			continue;

		if (++reg_idx <= start)
			continue;

		err = nl80211_send_regdom(skb, cb, cb->nlh->nlmsg_seq,
					  NLM_F_MULTI, &rdev->wiphy, regdom);
		if (err < 0) {
			reg_idx--;
			break;
		}
	}

	cb->args[2] = reg_idx;
	err = skb->len;
out_err:
	rtnl_unlock();
	return err;
}

#ifdef CONFIG_CFG80211_CRDA_SUPPORT
static const struct nla_policy reg_rule_policy[NL80211_REG_RULE_ATTR_MAX + 1] = {
	[NL80211_ATTR_REG_RULE_FLAGS]		= { .type = NLA_U32 },
	[NL80211_ATTR_FREQ_RANGE_START]		= { .type = NLA_U32 },
	[NL80211_ATTR_FREQ_RANGE_END]		= { .type = NLA_U32 },
	[NL80211_ATTR_FREQ_RANGE_MAX_BW]	= { .type = NLA_U32 },
	[NL80211_ATTR_POWER_RULE_MAX_ANT_GAIN]	= { .type = NLA_U32 },
	[NL80211_ATTR_POWER_RULE_MAX_EIRP]	= { .type = NLA_U32 },
	[NL80211_ATTR_DFS_CAC_TIME]		= { .type = NLA_U32 },
};

static int parse_reg_rule(struct nlattr *tb[],
	struct ieee80211_reg_rule *reg_rule)
{
	struct ieee80211_freq_range *freq_range = &reg_rule->freq_range;
	struct ieee80211_power_rule *power_rule = &reg_rule->power_rule;

	if (!tb[NL80211_ATTR_REG_RULE_FLAGS])
		return -EINVAL;
	if (!tb[NL80211_ATTR_FREQ_RANGE_START])
		return -EINVAL;
	if (!tb[NL80211_ATTR_FREQ_RANGE_END])
		return -EINVAL;
	if (!tb[NL80211_ATTR_FREQ_RANGE_MAX_BW])
		return -EINVAL;
	if (!tb[NL80211_ATTR_POWER_RULE_MAX_EIRP])
		return -EINVAL;

	reg_rule->flags = nla_get_u32(tb[NL80211_ATTR_REG_RULE_FLAGS]);

	freq_range->start_freq_khz =
		nla_get_u32(tb[NL80211_ATTR_FREQ_RANGE_START]);
	freq_range->end_freq_khz =
		nla_get_u32(tb[NL80211_ATTR_FREQ_RANGE_END]);
	freq_range->max_bandwidth_khz =
		nla_get_u32(tb[NL80211_ATTR_FREQ_RANGE_MAX_BW]);

	power_rule->max_eirp =
		nla_get_u32(tb[NL80211_ATTR_POWER_RULE_MAX_EIRP]);

	if (tb[NL80211_ATTR_POWER_RULE_MAX_ANT_GAIN])
		power_rule->max_antenna_gain =
			nla_get_u32(tb[NL80211_ATTR_POWER_RULE_MAX_ANT_GAIN]);

	if (tb[NL80211_ATTR_DFS_CAC_TIME])
		reg_rule->dfs_cac_ms =
			nla_get_u32(tb[NL80211_ATTR_DFS_CAC_TIME]);

	return 0;
}

static int nl80211_set_reg(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *tb[NL80211_REG_RULE_ATTR_MAX + 1];
	struct nlattr *nl_reg_rule;
	char *alpha2;
	int rem_reg_rules, r;
	u32 num_rules = 0, rule_idx = 0;
	enum nl80211_dfs_regions dfs_region = NL80211_DFS_UNSET;
	struct ieee80211_regdomain *rd;

	if (!info->attrs[NL80211_ATTR_REG_ALPHA2])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_REG_RULES])
		return -EINVAL;

	alpha2 = nla_data(info->attrs[NL80211_ATTR_REG_ALPHA2]);

	if (info->attrs[NL80211_ATTR_DFS_REGION])
		dfs_region = nla_get_u8(info->attrs[NL80211_ATTR_DFS_REGION]);

	nla_for_each_nested(nl_reg_rule, info->attrs[NL80211_ATTR_REG_RULES],
			    rem_reg_rules) {
		num_rules++;
		if (num_rules > NL80211_MAX_SUPP_REG_RULES)
			return -EINVAL;
	}

	if (!reg_is_valid_request(alpha2))
		return -EINVAL;

	rd = kzalloc(struct_size(rd, reg_rules, num_rules), GFP_KERNEL);
	if (!rd)
		return -ENOMEM;

	rd->n_reg_rules = num_rules;
	rd->alpha2[0] = alpha2[0];
	rd->alpha2[1] = alpha2[1];

	/*
	 * Disable DFS master mode if the DFS region was
	 * not supported or known on this kernel.
	 */
	if (reg_supported_dfs_region(dfs_region))
		rd->dfs_region = dfs_region;

	nla_for_each_nested(nl_reg_rule, info->attrs[NL80211_ATTR_REG_RULES],
			    rem_reg_rules) {
		r = nla_parse_nested_deprecated(tb, NL80211_REG_RULE_ATTR_MAX,
						nl_reg_rule, reg_rule_policy,
						info->extack);
		if (r)
			goto bad_reg;
		r = parse_reg_rule(tb, &rd->reg_rules[rule_idx]);
		if (r)
			goto bad_reg;

		rule_idx++;

		if (rule_idx > NL80211_MAX_SUPP_REG_RULES) {
			r = -EINVAL;
			goto bad_reg;
		}
	}

	/* set_regdom takes ownership of rd */
	return set_regdom(rd, REGD_SOURCE_CRDA);
 bad_reg:
	kfree(rd);
	return r;
}
#endif /* CONFIG_CFG80211_CRDA_SUPPORT */

static int validate_scan_freqs(struct nlattr *freqs)
{
	struct nlattr *attr1, *attr2;
	int n_channels = 0, tmp1, tmp2;

	nla_for_each_nested(attr1, freqs, tmp1)
		if (nla_len(attr1) != sizeof(u32))
			return 0;

	nla_for_each_nested(attr1, freqs, tmp1) {
		n_channels++;
		/*
		 * Some hardware has a limited channel list for
		 * scanning, and it is pretty much nonsensical
		 * to scan for a channel twice, so disallow that
		 * and don't require drivers to check that the
		 * channel list they get isn't longer than what
		 * they can scan, as long as they can scan all
		 * the channels they registered at once.
		 */
		nla_for_each_nested(attr2, freqs, tmp2)
			if (attr1 != attr2 &&
			    nla_get_u32(attr1) == nla_get_u32(attr2))
				return 0;
	}

	return n_channels;
}

static bool is_band_valid(struct wiphy *wiphy, enum nl80211_band b)
{
	return b < NUM_NL80211_BANDS && wiphy->bands[b];
}

static int parse_bss_select(struct nlattr *nla, struct wiphy *wiphy,
			    struct cfg80211_bss_selection *bss_select)
{
	struct nlattr *attr[NL80211_BSS_SELECT_ATTR_MAX + 1];
	struct nlattr *nest;
	int err;
	bool found = false;
	int i;

	/* only process one nested attribute */
	nest = nla_data(nla);
	if (!nla_ok(nest, nla_len(nest)))
		return -EINVAL;

	err = nla_parse_nested_deprecated(attr, NL80211_BSS_SELECT_ATTR_MAX,
					  nest, nl80211_bss_select_policy,
					  NULL);
	if (err)
		return err;

	/* only one attribute may be given */
	for (i = 0; i <= NL80211_BSS_SELECT_ATTR_MAX; i++) {
		if (attr[i]) {
			if (found)
				return -EINVAL;
			found = true;
		}
	}

	bss_select->behaviour = __NL80211_BSS_SELECT_ATTR_INVALID;

	if (attr[NL80211_BSS_SELECT_ATTR_RSSI])
		bss_select->behaviour = NL80211_BSS_SELECT_ATTR_RSSI;

	if (attr[NL80211_BSS_SELECT_ATTR_BAND_PREF]) {
		bss_select->behaviour = NL80211_BSS_SELECT_ATTR_BAND_PREF;
		bss_select->param.band_pref =
			nla_get_u32(attr[NL80211_BSS_SELECT_ATTR_BAND_PREF]);
		if (!is_band_valid(wiphy, bss_select->param.band_pref))
			return -EINVAL;
	}

	if (attr[NL80211_BSS_SELECT_ATTR_RSSI_ADJUST]) {
		struct nl80211_bss_select_rssi_adjust *adj_param;

		adj_param = nla_data(attr[NL80211_BSS_SELECT_ATTR_RSSI_ADJUST]);
		bss_select->behaviour = NL80211_BSS_SELECT_ATTR_RSSI_ADJUST;
		bss_select->param.adjust.band = adj_param->band;
		bss_select->param.adjust.delta = adj_param->delta;
		if (!is_band_valid(wiphy, bss_select->param.adjust.band))
			return -EINVAL;
	}

	/* user-space did not provide behaviour attribute */
	if (bss_select->behaviour == __NL80211_BSS_SELECT_ATTR_INVALID)
		return -EINVAL;

	if (!(wiphy->bss_select_support & BIT(bss_select->behaviour)))
		return -EINVAL;

	return 0;
}

int nl80211_parse_random_mac(struct nlattr **attrs,
			     u8 *mac_addr, u8 *mac_addr_mask)
{
	int i;

	if (!attrs[NL80211_ATTR_MAC] && !attrs[NL80211_ATTR_MAC_MASK]) {
		eth_zero_addr(mac_addr);
		eth_zero_addr(mac_addr_mask);
		mac_addr[0] = 0x2;
		mac_addr_mask[0] = 0x3;

		return 0;
	}

	/* need both or none */
	if (!attrs[NL80211_ATTR_MAC] || !attrs[NL80211_ATTR_MAC_MASK])
		return -EINVAL;

	memcpy(mac_addr, nla_data(attrs[NL80211_ATTR_MAC]), ETH_ALEN);
	memcpy(mac_addr_mask, nla_data(attrs[NL80211_ATTR_MAC_MASK]), ETH_ALEN);

	/* don't allow or configure an mcast address */
	if (!is_multicast_ether_addr(mac_addr_mask) ||
	    is_multicast_ether_addr(mac_addr))
		return -EINVAL;

	/*
	 * allow users to pass a MAC address that has bits set outside
	 * of the mask, but don't bother drivers with having to deal
	 * with such bits
	 */
	for (i = 0; i < ETH_ALEN; i++)
		mac_addr[i] &= mac_addr_mask[i];

	return 0;
}

static bool cfg80211_off_channel_oper_allowed(struct wireless_dev *wdev)
{
	ASSERT_WDEV_LOCK(wdev);

	if (!cfg80211_beaconing_iface_active(wdev))
		return true;

	if (!(wdev->chandef.chan->flags & IEEE80211_CHAN_RADAR))
		return true;

	return regulatory_pre_cac_allowed(wdev->wiphy);
}

static bool nl80211_check_scan_feat(struct wiphy *wiphy, u32 flags, u32 flag,
				    enum nl80211_ext_feature_index feat)
{
	if (!(flags & flag))
		return true;
	if (wiphy_ext_feature_isset(wiphy, feat))
		return true;
	return false;
}

static int
nl80211_check_scan_flags(struct wiphy *wiphy, struct wireless_dev *wdev,
			 void *request, struct nlattr **attrs,
			 bool is_sched_scan)
{
	u8 *mac_addr, *mac_addr_mask;
	u32 *flags;
	enum nl80211_feature_flags randomness_flag;

	if (!attrs[NL80211_ATTR_SCAN_FLAGS])
		return 0;

	if (is_sched_scan) {
		struct cfg80211_sched_scan_request *req = request;

		randomness_flag = wdev ?
				  NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR :
				  NL80211_FEATURE_ND_RANDOM_MAC_ADDR;
		flags = &req->flags;
		mac_addr = req->mac_addr;
		mac_addr_mask = req->mac_addr_mask;
	} else {
		struct cfg80211_scan_request *req = request;

		randomness_flag = NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR;
		flags = &req->flags;
		mac_addr = req->mac_addr;
		mac_addr_mask = req->mac_addr_mask;
	}

	*flags = nla_get_u32(attrs[NL80211_ATTR_SCAN_FLAGS]);

	if (((*flags & NL80211_SCAN_FLAG_LOW_PRIORITY) &&
	     !(wiphy->features & NL80211_FEATURE_LOW_PRIORITY_SCAN)) ||
	    !nl80211_check_scan_feat(wiphy, *flags,
				     NL80211_SCAN_FLAG_LOW_SPAN,
				     NL80211_EXT_FEATURE_LOW_SPAN_SCAN) ||
	    !nl80211_check_scan_feat(wiphy, *flags,
				     NL80211_SCAN_FLAG_LOW_POWER,
				     NL80211_EXT_FEATURE_LOW_POWER_SCAN) ||
	    !nl80211_check_scan_feat(wiphy, *flags,
				     NL80211_SCAN_FLAG_HIGH_ACCURACY,
				     NL80211_EXT_FEATURE_HIGH_ACCURACY_SCAN) ||
	    !nl80211_check_scan_feat(wiphy, *flags,
				     NL80211_SCAN_FLAG_FILS_MAX_CHANNEL_TIME,
				     NL80211_EXT_FEATURE_FILS_MAX_CHANNEL_TIME) ||
	    !nl80211_check_scan_feat(wiphy, *flags,
				     NL80211_SCAN_FLAG_ACCEPT_BCAST_PROBE_RESP,
				     NL80211_EXT_FEATURE_ACCEPT_BCAST_PROBE_RESP) ||
	    !nl80211_check_scan_feat(wiphy, *flags,
				     NL80211_SCAN_FLAG_OCE_PROBE_REQ_DEFERRAL_SUPPRESSION,
				     NL80211_EXT_FEATURE_OCE_PROBE_REQ_DEFERRAL_SUPPRESSION) ||
	    !nl80211_check_scan_feat(wiphy, *flags,
				     NL80211_SCAN_FLAG_OCE_PROBE_REQ_HIGH_TX_RATE,
				     NL80211_EXT_FEATURE_OCE_PROBE_REQ_HIGH_TX_RATE) ||
	    !nl80211_check_scan_feat(wiphy, *flags,
				     NL80211_SCAN_FLAG_RANDOM_SN,
				     NL80211_EXT_FEATURE_SCAN_RANDOM_SN) ||
	    !nl80211_check_scan_feat(wiphy, *flags,
				     NL80211_SCAN_FLAG_MIN_PREQ_CONTENT,
				     NL80211_EXT_FEATURE_SCAN_MIN_PREQ_CONTENT))
		return -EOPNOTSUPP;

	if (*flags & NL80211_SCAN_FLAG_RANDOM_ADDR) {
		int err;

		if (!(wiphy->features & randomness_flag) ||
		    (wdev && wdev->current_bss))
			return -EOPNOTSUPP;

		err = nl80211_parse_random_mac(attrs, mac_addr, mac_addr_mask);
		if (err)
			return err;
	}

	return 0;
}

static int nl80211_trigger_scan(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];
	struct cfg80211_scan_request *request;
	struct nlattr *attr;
	struct wiphy *wiphy;
	int err, tmp, n_ssids = 0, n_channels, i;
	size_t ie_len;

	wiphy = &rdev->wiphy;

	if (wdev->iftype == NL80211_IFTYPE_NAN)
		return -EOPNOTSUPP;

	if (!rdev->ops->scan)
		return -EOPNOTSUPP;

	if (rdev->scan_req || rdev->scan_msg) {
		err = -EBUSY;
		goto unlock;
	}

	if (info->attrs[NL80211_ATTR_SCAN_FREQUENCIES]) {
		n_channels = validate_scan_freqs(
				info->attrs[NL80211_ATTR_SCAN_FREQUENCIES]);
		if (!n_channels) {
			err = -EINVAL;
			goto unlock;
		}
	} else {
		n_channels = ieee80211_get_num_supported_channels(wiphy);
	}

	if (info->attrs[NL80211_ATTR_SCAN_SSIDS])
		nla_for_each_nested(attr, info->attrs[NL80211_ATTR_SCAN_SSIDS], tmp)
			n_ssids++;

	if (n_ssids > wiphy->max_scan_ssids) {
		err = -EINVAL;
		goto unlock;
	}

	if (info->attrs[NL80211_ATTR_IE])
		ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	else
		ie_len = 0;

	if (ie_len > wiphy->max_scan_ie_len) {
		err = -EINVAL;
		goto unlock;
	}

	request = kzalloc(sizeof(*request)
			+ sizeof(*request->ssids) * n_ssids
			+ sizeof(*request->channels) * n_channels
			+ ie_len, GFP_KERNEL);
	if (!request) {
		err = -ENOMEM;
		goto unlock;
	}

	if (n_ssids)
		request->ssids = (void *)&request->channels[n_channels];
	request->n_ssids = n_ssids;
	if (ie_len) {
		if (n_ssids)
			request->ie = (void *)(request->ssids + n_ssids);
		else
			request->ie = (void *)(request->channels + n_channels);
	}

	i = 0;
	if (info->attrs[NL80211_ATTR_SCAN_FREQUENCIES]) {
		/* user specified, bail out if channel not found */
		nla_for_each_nested(attr, info->attrs[NL80211_ATTR_SCAN_FREQUENCIES], tmp) {
			struct ieee80211_channel *chan;

			chan = ieee80211_get_channel(wiphy, nla_get_u32(attr));

			if (!chan) {
				err = -EINVAL;
				goto out_free;
			}

			/* ignore disabled channels */
			if (chan->flags & IEEE80211_CHAN_DISABLED)
				continue;

			request->channels[i] = chan;
			i++;
		}
	} else {
		enum nl80211_band band;

		/* all channels */
		for (band = 0; band < NUM_NL80211_BANDS; band++) {
			int j;

			if (!wiphy->bands[band])
				continue;
			for (j = 0; j < wiphy->bands[band]->n_channels; j++) {
				struct ieee80211_channel *chan;

				chan = &wiphy->bands[band]->channels[j];

				if (chan->flags & IEEE80211_CHAN_DISABLED)
					continue;

				request->channels[i] = chan;
				i++;
			}
		}
	}

	if (!i) {
		err = -EINVAL;
		goto out_free;
	}

	request->n_channels = i;

	wdev_lock(wdev);
	if (!cfg80211_off_channel_oper_allowed(wdev)) {
		struct ieee80211_channel *chan;

		if (request->n_channels != 1) {
			wdev_unlock(wdev);
			err = -EBUSY;
			goto out_free;
		}

		chan = request->channels[0];
		if (chan->center_freq != wdev->chandef.chan->center_freq) {
			wdev_unlock(wdev);
			err = -EBUSY;
			goto out_free;
		}
	}
	wdev_unlock(wdev);

	i = 0;
	if (n_ssids) {
		nla_for_each_nested(attr, info->attrs[NL80211_ATTR_SCAN_SSIDS], tmp) {
			if (nla_len(attr) > IEEE80211_MAX_SSID_LEN) {
				err = -EINVAL;
				goto out_free;
			}
			request->ssids[i].ssid_len = nla_len(attr);
			memcpy(request->ssids[i].ssid, nla_data(attr), nla_len(attr));
			i++;
		}
	}

	if (info->attrs[NL80211_ATTR_IE]) {
		request->ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
		memcpy((void *)request->ie,
		       nla_data(info->attrs[NL80211_ATTR_IE]),
		       request->ie_len);
	}

	for (i = 0; i < NUM_NL80211_BANDS; i++)
		if (wiphy->bands[i])
			request->rates[i] =
				(1 << wiphy->bands[i]->n_bitrates) - 1;

	if (info->attrs[NL80211_ATTR_SCAN_SUPP_RATES]) {
		nla_for_each_nested(attr,
				    info->attrs[NL80211_ATTR_SCAN_SUPP_RATES],
				    tmp) {
			enum nl80211_band band = nla_type(attr);

			if (band < 0 || band >= NUM_NL80211_BANDS) {
				err = -EINVAL;
				goto out_free;
			}

			if (!wiphy->bands[band])
				continue;

			err = ieee80211_get_ratemask(wiphy->bands[band],
						     nla_data(attr),
						     nla_len(attr),
						     &request->rates[band]);
			if (err)
				goto out_free;
		}
	}

	if (info->attrs[NL80211_ATTR_MEASUREMENT_DURATION]) {
		if (!wiphy_ext_feature_isset(wiphy,
					NL80211_EXT_FEATURE_SET_SCAN_DWELL)) {
			err = -EOPNOTSUPP;
			goto out_free;
		}

		request->duration =
			nla_get_u16(info->attrs[NL80211_ATTR_MEASUREMENT_DURATION]);
		request->duration_mandatory =
			nla_get_flag(info->attrs[NL80211_ATTR_MEASUREMENT_DURATION_MANDATORY]);
	}

	err = nl80211_check_scan_flags(wiphy, wdev, request, info->attrs,
				       false);
	if (err)
		goto out_free;

	request->no_cck =
		nla_get_flag(info->attrs[NL80211_ATTR_TX_NO_CCK_RATE]);

	/* Initial implementation used NL80211_ATTR_MAC to set the specific
	 * BSSID to scan for. This was problematic because that same attribute
	 * was already used for another purpose (local random MAC address). The
	 * NL80211_ATTR_BSSID attribute was added to fix this. For backwards
	 * compatibility with older userspace components, also use the
	 * NL80211_ATTR_MAC value here if it can be determined to be used for
	 * the specific BSSID use case instead of the random MAC address
	 * (NL80211_ATTR_SCAN_FLAGS is used to enable random MAC address use).
	 */
	if (info->attrs[NL80211_ATTR_BSSID])
		memcpy(request->bssid,
		       nla_data(info->attrs[NL80211_ATTR_BSSID]), ETH_ALEN);
	else if (!(request->flags & NL80211_SCAN_FLAG_RANDOM_ADDR) &&
		 info->attrs[NL80211_ATTR_MAC])
		memcpy(request->bssid, nla_data(info->attrs[NL80211_ATTR_MAC]),
		       ETH_ALEN);
	else
		eth_broadcast_addr(request->bssid);

	request->wdev = wdev;
	request->wiphy = &rdev->wiphy;
	request->scan_start = jiffies;

	rdev->scan_req = request;
	err = rdev_scan(rdev, request);

	if (!err) {
		nl80211_send_scan_start(rdev, wdev);
		if (wdev->netdev)
			dev_hold(wdev->netdev);
	} else {
 out_free:
		rdev->scan_req = NULL;
		kfree(request);
	}

 unlock:
	return err;
}

static int nl80211_abort_scan(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];

	if (!rdev->ops->abort_scan)
		return -EOPNOTSUPP;

	if (rdev->scan_msg)
		return 0;

	if (!rdev->scan_req)
		return -ENOENT;

	rdev_abort_scan(rdev, wdev);
	return 0;
}

static int
nl80211_parse_sched_scan_plans(struct wiphy *wiphy, int n_plans,
			       struct cfg80211_sched_scan_request *request,
			       struct nlattr **attrs)
{
	int tmp, err, i = 0;
	struct nlattr *attr;

	if (!attrs[NL80211_ATTR_SCHED_SCAN_PLANS]) {
		u32 interval;

		/*
		 * If scan plans are not specified,
		 * %NL80211_ATTR_SCHED_SCAN_INTERVAL will be specified. In this
		 * case one scan plan will be set with the specified scan
		 * interval and infinite number of iterations.
		 */
		interval = nla_get_u32(attrs[NL80211_ATTR_SCHED_SCAN_INTERVAL]);
		if (!interval)
			return -EINVAL;

		request->scan_plans[0].interval =
			DIV_ROUND_UP(interval, MSEC_PER_SEC);
		if (!request->scan_plans[0].interval)
			return -EINVAL;

		if (request->scan_plans[0].interval >
		    wiphy->max_sched_scan_plan_interval)
			request->scan_plans[0].interval =
				wiphy->max_sched_scan_plan_interval;

		return 0;
	}

	nla_for_each_nested(attr, attrs[NL80211_ATTR_SCHED_SCAN_PLANS], tmp) {
		struct nlattr *plan[NL80211_SCHED_SCAN_PLAN_MAX + 1];

		if (WARN_ON(i >= n_plans))
			return -EINVAL;

		err = nla_parse_nested_deprecated(plan,
						  NL80211_SCHED_SCAN_PLAN_MAX,
						  attr, nl80211_plan_policy,
						  NULL);
		if (err)
			return err;

		if (!plan[NL80211_SCHED_SCAN_PLAN_INTERVAL])
			return -EINVAL;

		request->scan_plans[i].interval =
			nla_get_u32(plan[NL80211_SCHED_SCAN_PLAN_INTERVAL]);
		if (!request->scan_plans[i].interval ||
		    request->scan_plans[i].interval >
		    wiphy->max_sched_scan_plan_interval)
			return -EINVAL;

		if (plan[NL80211_SCHED_SCAN_PLAN_ITERATIONS]) {
			request->scan_plans[i].iterations =
				nla_get_u32(plan[NL80211_SCHED_SCAN_PLAN_ITERATIONS]);
			if (!request->scan_plans[i].iterations ||
			    (request->scan_plans[i].iterations >
			     wiphy->max_sched_scan_plan_iterations))
				return -EINVAL;
		} else if (i < n_plans - 1) {
			/*
			 * All scan plans but the last one must specify
			 * a finite number of iterations
			 */
			return -EINVAL;
		}

		i++;
	}

	/*
	 * The last scan plan must not specify the number of
	 * iterations, it is supposed to run infinitely
	 */
	if (request->scan_plans[n_plans - 1].iterations)
		return  -EINVAL;

	return 0;
}

static int
nl80211_parse_sched_scan_per_band_rssi(struct wiphy *wiphy,
				       struct cfg80211_match_set *match_sets,
				       struct nlattr *tb_band_rssi,
				       s32 rssi_thold)
{
	struct nlattr *attr;
	int i, tmp, ret = 0;

	if (!wiphy_ext_feature_isset(wiphy,
		    NL80211_EXT_FEATURE_SCHED_SCAN_BAND_SPECIFIC_RSSI_THOLD)) {
		if (tb_band_rssi)
			ret = -EOPNOTSUPP;
		else
			for (i = 0; i < NUM_NL80211_BANDS; i++)
				match_sets->per_band_rssi_thold[i] =
					NL80211_SCAN_RSSI_THOLD_OFF;
		return ret;
	}

	for (i = 0; i < NUM_NL80211_BANDS; i++)
		match_sets->per_band_rssi_thold[i] = rssi_thold;

	nla_for_each_nested(attr, tb_band_rssi, tmp) {
		enum nl80211_band band = nla_type(attr);

		if (band < 0 || band >= NUM_NL80211_BANDS)
			return -EINVAL;

		match_sets->per_band_rssi_thold[band] =	nla_get_s32(attr);
	}

	return 0;
}

static struct cfg80211_sched_scan_request *
nl80211_parse_sched_scan(struct wiphy *wiphy, struct wireless_dev *wdev,
			 struct nlattr **attrs, int max_match_sets)
{
	struct cfg80211_sched_scan_request *request;
	struct nlattr *attr;
	int err, tmp, n_ssids = 0, n_match_sets = 0, n_channels, i, n_plans = 0;
	enum nl80211_band band;
	size_t ie_len;
	struct nlattr *tb[NL80211_SCHED_SCAN_MATCH_ATTR_MAX + 1];
	s32 default_match_rssi = NL80211_SCAN_RSSI_THOLD_OFF;

	if (attrs[NL80211_ATTR_SCAN_FREQUENCIES]) {
		n_channels = validate_scan_freqs(
				attrs[NL80211_ATTR_SCAN_FREQUENCIES]);
		if (!n_channels)
			return ERR_PTR(-EINVAL);
	} else {
		n_channels = ieee80211_get_num_supported_channels(wiphy);
	}

	if (attrs[NL80211_ATTR_SCAN_SSIDS])
		nla_for_each_nested(attr, attrs[NL80211_ATTR_SCAN_SSIDS],
				    tmp)
			n_ssids++;

	if (n_ssids > wiphy->max_sched_scan_ssids)
		return ERR_PTR(-EINVAL);

	/*
	 * First, count the number of 'real' matchsets. Due to an issue with
	 * the old implementation, matchsets containing only the RSSI attribute
	 * (NL80211_SCHED_SCAN_MATCH_ATTR_RSSI) are considered as the 'default'
	 * RSSI for all matchsets, rather than their own matchset for reporting
	 * all APs with a strong RSSI. This is needed to be compatible with
	 * older userspace that treated a matchset with only the RSSI as the
	 * global RSSI for all other matchsets - if there are other matchsets.
	 */
	if (attrs[NL80211_ATTR_SCHED_SCAN_MATCH]) {
		nla_for_each_nested(attr,
				    attrs[NL80211_ATTR_SCHED_SCAN_MATCH],
				    tmp) {
			struct nlattr *rssi;

			err = nla_parse_nested_deprecated(tb,
							  NL80211_SCHED_SCAN_MATCH_ATTR_MAX,
							  attr,
							  nl80211_match_policy,
							  NULL);
			if (err)
				return ERR_PTR(err);

			/* SSID and BSSID are mutually exclusive */
			if (tb[NL80211_SCHED_SCAN_MATCH_ATTR_SSID] &&
			    tb[NL80211_SCHED_SCAN_MATCH_ATTR_BSSID])
				return ERR_PTR(-EINVAL);

			/* add other standalone attributes here */
			if (tb[NL80211_SCHED_SCAN_MATCH_ATTR_SSID] ||
			    tb[NL80211_SCHED_SCAN_MATCH_ATTR_BSSID]) {
				n_match_sets++;
				continue;
			}
			rssi = tb[NL80211_SCHED_SCAN_MATCH_ATTR_RSSI];
			if (rssi)
				default_match_rssi = nla_get_s32(rssi);
		}
	}

	/* However, if there's no other matchset, add the RSSI one */
	if (!n_match_sets && default_match_rssi != NL80211_SCAN_RSSI_THOLD_OFF)
		n_match_sets = 1;

	if (n_match_sets > max_match_sets)
		return ERR_PTR(-EINVAL);

	if (attrs[NL80211_ATTR_IE])
		ie_len = nla_len(attrs[NL80211_ATTR_IE]);
	else
		ie_len = 0;

	if (ie_len > wiphy->max_sched_scan_ie_len)
		return ERR_PTR(-EINVAL);

	if (attrs[NL80211_ATTR_SCHED_SCAN_PLANS]) {
		/*
		 * NL80211_ATTR_SCHED_SCAN_INTERVAL must not be specified since
		 * each scan plan already specifies its own interval
		 */
		if (attrs[NL80211_ATTR_SCHED_SCAN_INTERVAL])
			return ERR_PTR(-EINVAL);

		nla_for_each_nested(attr,
				    attrs[NL80211_ATTR_SCHED_SCAN_PLANS], tmp)
			n_plans++;
	} else {
		/*
		 * The scan interval attribute is kept for backward
		 * compatibility. If no scan plans are specified and sched scan
		 * interval is specified, one scan plan will be set with this
		 * scan interval and infinite number of iterations.
		 */
		if (!attrs[NL80211_ATTR_SCHED_SCAN_INTERVAL])
			return ERR_PTR(-EINVAL);

		n_plans = 1;
	}

	if (!n_plans || n_plans > wiphy->max_sched_scan_plans)
		return ERR_PTR(-EINVAL);

	if (!wiphy_ext_feature_isset(
		    wiphy, NL80211_EXT_FEATURE_SCHED_SCAN_RELATIVE_RSSI) &&
	    (attrs[NL80211_ATTR_SCHED_SCAN_RELATIVE_RSSI] ||
	     attrs[NL80211_ATTR_SCHED_SCAN_RSSI_ADJUST]))
		return ERR_PTR(-EINVAL);

	request = kzalloc(sizeof(*request)
			+ sizeof(*request->ssids) * n_ssids
			+ sizeof(*request->match_sets) * n_match_sets
			+ sizeof(*request->scan_plans) * n_plans
			+ sizeof(*request->channels) * n_channels
			+ ie_len, GFP_KERNEL);
	if (!request)
		return ERR_PTR(-ENOMEM);

	if (n_ssids)
		request->ssids = (void *)&request->channels[n_channels];
	request->n_ssids = n_ssids;
	if (ie_len) {
		if (n_ssids)
			request->ie = (void *)(request->ssids + n_ssids);
		else
			request->ie = (void *)(request->channels + n_channels);
	}

	if (n_match_sets) {
		if (request->ie)
			request->match_sets = (void *)(request->ie + ie_len);
		else if (n_ssids)
			request->match_sets =
				(void *)(request->ssids + n_ssids);
		else
			request->match_sets =
				(void *)(request->channels + n_channels);
	}
	request->n_match_sets = n_match_sets;

	if (n_match_sets)
		request->scan_plans = (void *)(request->match_sets +
					       n_match_sets);
	else if (request->ie)
		request->scan_plans = (void *)(request->ie + ie_len);
	else if (n_ssids)
		request->scan_plans = (void *)(request->ssids + n_ssids);
	else
		request->scan_plans = (void *)(request->channels + n_channels);

	request->n_scan_plans = n_plans;

	i = 0;
	if (attrs[NL80211_ATTR_SCAN_FREQUENCIES]) {
		/* user specified, bail out if channel not found */
		nla_for_each_nested(attr,
				    attrs[NL80211_ATTR_SCAN_FREQUENCIES],
				    tmp) {
			struct ieee80211_channel *chan;

			chan = ieee80211_get_channel(wiphy, nla_get_u32(attr));

			if (!chan) {
				err = -EINVAL;
				goto out_free;
			}

			/* ignore disabled channels */
			if (chan->flags & IEEE80211_CHAN_DISABLED)
				continue;

			request->channels[i] = chan;
			i++;
		}
	} else {
		/* all channels */
		for (band = 0; band < NUM_NL80211_BANDS; band++) {
			int j;

			if (!wiphy->bands[band])
				continue;
			for (j = 0; j < wiphy->bands[band]->n_channels; j++) {
				struct ieee80211_channel *chan;

				chan = &wiphy->bands[band]->channels[j];

				if (chan->flags & IEEE80211_CHAN_DISABLED)
					continue;

				request->channels[i] = chan;
				i++;
			}
		}
	}

	if (!i) {
		err = -EINVAL;
		goto out_free;
	}

	request->n_channels = i;

	i = 0;
	if (n_ssids) {
		nla_for_each_nested(attr, attrs[NL80211_ATTR_SCAN_SSIDS],
				    tmp) {
			if (nla_len(attr) > IEEE80211_MAX_SSID_LEN) {
				err = -EINVAL;
				goto out_free;
			}
			request->ssids[i].ssid_len = nla_len(attr);
			memcpy(request->ssids[i].ssid, nla_data(attr),
			       nla_len(attr));
			i++;
		}
	}

	i = 0;
	if (attrs[NL80211_ATTR_SCHED_SCAN_MATCH]) {
		nla_for_each_nested(attr,
				    attrs[NL80211_ATTR_SCHED_SCAN_MATCH],
				    tmp) {
			struct nlattr *ssid, *bssid, *rssi;

			err = nla_parse_nested_deprecated(tb,
							  NL80211_SCHED_SCAN_MATCH_ATTR_MAX,
							  attr,
							  nl80211_match_policy,
							  NULL);
			if (err)
				goto out_free;
			ssid = tb[NL80211_SCHED_SCAN_MATCH_ATTR_SSID];
			bssid = tb[NL80211_SCHED_SCAN_MATCH_ATTR_BSSID];

			if (!ssid && !bssid) {
				i++;
				continue;
			}

			if (WARN_ON(i >= n_match_sets)) {
				/* this indicates a programming error,
				 * the loop above should have verified
				 * things properly
				 */
				err = -EINVAL;
				goto out_free;
			}

			if (ssid) {
				if (nla_len(ssid) > IEEE80211_MAX_SSID_LEN) {
					err = -EINVAL;
					goto out_free;
				}
				memcpy(request->match_sets[i].ssid.ssid,
				       nla_data(ssid), nla_len(ssid));
				request->match_sets[i].ssid.ssid_len =
					nla_len(ssid);
			}
			if (bssid) {
				if (nla_len(bssid) != ETH_ALEN) {
					err = -EINVAL;
					goto out_free;
				}
				memcpy(request->match_sets[i].bssid,
				       nla_data(bssid), ETH_ALEN);
			}

			/* special attribute - old implementation w/a */
			request->match_sets[i].rssi_thold = default_match_rssi;
			rssi = tb[NL80211_SCHED_SCAN_MATCH_ATTR_RSSI];
			if (rssi)
				request->match_sets[i].rssi_thold =
					nla_get_s32(rssi);

			/* Parse per band RSSI attribute */
			err = nl80211_parse_sched_scan_per_band_rssi(wiphy,
				&request->match_sets[i],
				tb[NL80211_SCHED_SCAN_MATCH_PER_BAND_RSSI],
				request->match_sets[i].rssi_thold);
			if (err)
				goto out_free;

			i++;
		}

		/* there was no other matchset, so the RSSI one is alone */
		if (i == 0 && n_match_sets)
			request->match_sets[0].rssi_thold = default_match_rssi;

		request->min_rssi_thold = INT_MAX;
		for (i = 0; i < n_match_sets; i++)
			request->min_rssi_thold =
				min(request->match_sets[i].rssi_thold,
				    request->min_rssi_thold);
	} else {
		request->min_rssi_thold = NL80211_SCAN_RSSI_THOLD_OFF;
	}

	if (ie_len) {
		request->ie_len = ie_len;
		memcpy((void *)request->ie,
		       nla_data(attrs[NL80211_ATTR_IE]),
		       request->ie_len);
	}

	err = nl80211_check_scan_flags(wiphy, wdev, request, attrs, true);
	if (err)
		goto out_free;

	if (attrs[NL80211_ATTR_SCHED_SCAN_DELAY])
		request->delay =
			nla_get_u32(attrs[NL80211_ATTR_SCHED_SCAN_DELAY]);

	if (attrs[NL80211_ATTR_SCHED_SCAN_RELATIVE_RSSI]) {
		request->relative_rssi = nla_get_s8(
			attrs[NL80211_ATTR_SCHED_SCAN_RELATIVE_RSSI]);
		request->relative_rssi_set = true;
	}

	if (request->relative_rssi_set &&
	    attrs[NL80211_ATTR_SCHED_SCAN_RSSI_ADJUST]) {
		struct nl80211_bss_select_rssi_adjust *rssi_adjust;

		rssi_adjust = nla_data(
			attrs[NL80211_ATTR_SCHED_SCAN_RSSI_ADJUST]);
		request->rssi_adjust.band = rssi_adjust->band;
		request->rssi_adjust.delta = rssi_adjust->delta;
		if (!is_band_valid(wiphy, request->rssi_adjust.band)) {
			err = -EINVAL;
			goto out_free;
		}
	}

	err = nl80211_parse_sched_scan_plans(wiphy, n_plans, request, attrs);
	if (err)
		goto out_free;

	request->scan_start = jiffies;

	return request;

out_free:
	kfree(request);
	return ERR_PTR(err);
}

static int nl80211_start_sched_scan(struct sk_buff *skb,
				    struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_sched_scan_request *sched_scan_req;
	bool want_multi;
	int err;

	if (!rdev->wiphy.max_sched_scan_reqs || !rdev->ops->sched_scan_start)
		return -EOPNOTSUPP;

	want_multi = info->attrs[NL80211_ATTR_SCHED_SCAN_MULTI];
	err = cfg80211_sched_scan_req_possible(rdev, want_multi);
	if (err)
		return err;

	sched_scan_req = nl80211_parse_sched_scan(&rdev->wiphy, wdev,
						  info->attrs,
						  rdev->wiphy.max_match_sets);

	err = PTR_ERR_OR_ZERO(sched_scan_req);
	if (err)
		goto out_err;

	/* leave request id zero for legacy request
	 * or if driver does not support multi-scheduled scan
	 */
	if (want_multi && rdev->wiphy.max_sched_scan_reqs > 1) {
		while (!sched_scan_req->reqid)
			sched_scan_req->reqid = cfg80211_assign_cookie(rdev);
	}

	err = rdev_sched_scan_start(rdev, dev, sched_scan_req);
	if (err)
		goto out_free;

	sched_scan_req->dev = dev;
	sched_scan_req->wiphy = &rdev->wiphy;

	if (info->attrs[NL80211_ATTR_SOCKET_OWNER])
		sched_scan_req->owner_nlportid = info->snd_portid;

	cfg80211_add_sched_scan_req(rdev, sched_scan_req);

	nl80211_send_sched_scan(sched_scan_req, NL80211_CMD_START_SCHED_SCAN);
	return 0;

out_free:
	kfree(sched_scan_req);
out_err:
	return err;
}

static int nl80211_stop_sched_scan(struct sk_buff *skb,
				   struct genl_info *info)
{
	struct cfg80211_sched_scan_request *req;
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	u64 cookie;

	if (!rdev->wiphy.max_sched_scan_reqs || !rdev->ops->sched_scan_stop)
		return -EOPNOTSUPP;

	if (info->attrs[NL80211_ATTR_COOKIE]) {
		cookie = nla_get_u64(info->attrs[NL80211_ATTR_COOKIE]);
		return __cfg80211_stop_sched_scan(rdev, cookie, false);
	}

	req = list_first_or_null_rcu(&rdev->sched_scan_req_list,
				     struct cfg80211_sched_scan_request,
				     list);
	if (!req || req->reqid ||
	    (req->owner_nlportid &&
	     req->owner_nlportid != info->snd_portid))
		return -ENOENT;

	return cfg80211_stop_sched_scan_req(rdev, req, false);
}

static int nl80211_start_radar_detection(struct sk_buff *skb,
					 struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_chan_def chandef;
	enum nl80211_dfs_regions dfs_region;
	unsigned int cac_time_ms;
	int err;

	dfs_region = reg_get_dfs_region(wiphy);
	if (dfs_region == NL80211_DFS_UNSET)
		return -EINVAL;

	err = nl80211_parse_chandef(rdev, info, &chandef);
	if (err)
		return err;

	if (netif_carrier_ok(dev))
		return -EBUSY;

	if (wdev->cac_started)
		return -EBUSY;

	err = cfg80211_chandef_dfs_required(wiphy, &chandef, wdev->iftype);
	if (err < 0)
		return err;

	if (err == 0)
		return -EINVAL;

	if (!cfg80211_chandef_dfs_usable(wiphy, &chandef))
		return -EINVAL;

	/* CAC start is offloaded to HW and can't be started manually */
	if (wiphy_ext_feature_isset(wiphy, NL80211_EXT_FEATURE_DFS_OFFLOAD))
		return -EOPNOTSUPP;

	if (!rdev->ops->start_radar_detection)
		return -EOPNOTSUPP;

	cac_time_ms = cfg80211_chandef_dfs_cac_time(&rdev->wiphy, &chandef);
	if (WARN_ON(!cac_time_ms))
		cac_time_ms = IEEE80211_DFS_MIN_CAC_TIME_MS;

	err = rdev_start_radar_detection(rdev, dev, &chandef, cac_time_ms);
	if (!err) {
		wdev->chandef = chandef;
		wdev->cac_started = true;
		wdev->cac_start_time = jiffies;
		wdev->cac_time_ms = cac_time_ms;
	}
	return err;
}

static int nl80211_notify_radar_detection(struct sk_buff *skb,
					  struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_chan_def chandef;
	enum nl80211_dfs_regions dfs_region;
	int err;

	dfs_region = reg_get_dfs_region(wiphy);
	if (dfs_region == NL80211_DFS_UNSET) {
		GENL_SET_ERR_MSG(info,
				 "DFS Region is not set. Unexpected Radar indication");
		return -EINVAL;
	}

	err = nl80211_parse_chandef(rdev, info, &chandef);
	if (err) {
		GENL_SET_ERR_MSG(info, "Unable to extract chandef info");
		return err;
	}

	err = cfg80211_chandef_dfs_required(wiphy, &chandef, wdev->iftype);
	if (err < 0) {
		GENL_SET_ERR_MSG(info, "chandef is invalid");
		return err;
	}

	if (err == 0) {
		GENL_SET_ERR_MSG(info,
				 "Unexpected Radar indication for chandef/iftype");
		return -EINVAL;
	}

	/* Do not process this notification if radar is already detected
	 * by kernel on this channel, and return success.
	 */
	if (chandef.chan->dfs_state == NL80211_DFS_UNAVAILABLE)
		return 0;

	cfg80211_set_dfs_state(wiphy, &chandef, NL80211_DFS_UNAVAILABLE);

	cfg80211_sched_dfs_chan_update(rdev);

	rdev->radar_chandef = chandef;

	/* Propagate this notification to other radios as well */
	queue_work(cfg80211_wq, &rdev->propagate_radar_detect_wk);

	return 0;
}

static int nl80211_channel_switch(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_csa_settings params;
	/* csa_attrs is defined static to avoid waste of stack size - this
	 * function is called under RTNL lock, so this should not be a problem.
	 */
	static struct nlattr *csa_attrs[NL80211_ATTR_MAX+1];
	int err;
	bool need_new_beacon = false;
	bool need_handle_dfs_flag = true;
	int len, i;
	u32 cs_count;

	if (!rdev->ops->channel_switch ||
	    !(rdev->wiphy.flags & WIPHY_FLAG_HAS_CHANNEL_SWITCH))
		return -EOPNOTSUPP;

	switch (dev->ieee80211_ptr->iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		need_new_beacon = true;
		/* For all modes except AP the handle_dfs flag needs to be
		 * supplied to tell the kernel that userspace will handle radar
		 * events when they happen. Otherwise a switch to a channel
		 * requiring DFS will be rejected.
		 */
		need_handle_dfs_flag = false;

		/* useless if AP is not running */
		if (!wdev->beacon_interval)
			return -ENOTCONN;
		break;
	case NL80211_IFTYPE_ADHOC:
		if (!wdev->ssid_len)
			return -ENOTCONN;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		if (!wdev->mesh_id_len)
			return -ENOTCONN;
		break;
	default:
		return -EOPNOTSUPP;
	}

	memset(&params, 0, sizeof(params));
	params.beacon_csa.ftm_responder = -1;

	if (!info->attrs[NL80211_ATTR_WIPHY_FREQ] ||
	    !info->attrs[NL80211_ATTR_CH_SWITCH_COUNT])
		return -EINVAL;

	/* only important for AP, IBSS and mesh create IEs internally */
	if (need_new_beacon && !info->attrs[NL80211_ATTR_CSA_IES])
		return -EINVAL;

	/* Even though the attribute is u32, the specification says
	 * u8, so let's make sure we don't overflow.
	 */
	cs_count = nla_get_u32(info->attrs[NL80211_ATTR_CH_SWITCH_COUNT]);
	if (cs_count > 255)
		return -EINVAL;

	params.count = cs_count;

	if (!need_new_beacon)
		goto skip_beacons;

	err = nl80211_parse_beacon(rdev, info->attrs, &params.beacon_after);
	if (err)
		return err;

	err = nla_parse_nested_deprecated(csa_attrs, NL80211_ATTR_MAX,
					  info->attrs[NL80211_ATTR_CSA_IES],
					  nl80211_policy, info->extack);
	if (err)
		return err;

	err = nl80211_parse_beacon(rdev, csa_attrs, &params.beacon_csa);
	if (err)
		return err;

	if (!csa_attrs[NL80211_ATTR_CSA_C_OFF_BEACON])
		return -EINVAL;

	len = nla_len(csa_attrs[NL80211_ATTR_CSA_C_OFF_BEACON]);
	if (!len || (len % sizeof(u16)))
		return -EINVAL;

	params.n_counter_offsets_beacon = len / sizeof(u16);
	if (rdev->wiphy.max_num_csa_counters &&
	    (params.n_counter_offsets_beacon >
	     rdev->wiphy.max_num_csa_counters))
		return -EINVAL;

	params.counter_offsets_beacon =
		nla_data(csa_attrs[NL80211_ATTR_CSA_C_OFF_BEACON]);

	/* sanity checks - counters should fit and be the same */
	for (i = 0; i < params.n_counter_offsets_beacon; i++) {
		u16 offset = params.counter_offsets_beacon[i];

		if (offset >= params.beacon_csa.tail_len)
			return -EINVAL;

		if (params.beacon_csa.tail[offset] != params.count)
			return -EINVAL;
	}

	if (csa_attrs[NL80211_ATTR_CSA_C_OFF_PRESP]) {
		len = nla_len(csa_attrs[NL80211_ATTR_CSA_C_OFF_PRESP]);
		if (!len || (len % sizeof(u16)))
			return -EINVAL;

		params.n_counter_offsets_presp = len / sizeof(u16);
		if (rdev->wiphy.max_num_csa_counters &&
		    (params.n_counter_offsets_presp >
		     rdev->wiphy.max_num_csa_counters))
			return -EINVAL;

		params.counter_offsets_presp =
			nla_data(csa_attrs[NL80211_ATTR_CSA_C_OFF_PRESP]);

		/* sanity checks - counters should fit and be the same */
		for (i = 0; i < params.n_counter_offsets_presp; i++) {
			u16 offset = params.counter_offsets_presp[i];

			if (offset >= params.beacon_csa.probe_resp_len)
				return -EINVAL;

			if (params.beacon_csa.probe_resp[offset] !=
			    params.count)
				return -EINVAL;
		}
	}

skip_beacons:
	err = nl80211_parse_chandef(rdev, info, &params.chandef);
	if (err)
		return err;

	if (!cfg80211_reg_can_beacon_relax(&rdev->wiphy, &params.chandef,
					   wdev->iftype))
		return -EINVAL;

	err = cfg80211_chandef_dfs_required(wdev->wiphy,
					    &params.chandef,
					    wdev->iftype);
	if (err < 0)
		return err;

	if (err > 0) {
		params.radar_required = true;
		if (need_handle_dfs_flag &&
		    !nla_get_flag(info->attrs[NL80211_ATTR_HANDLE_DFS])) {
			return -EINVAL;
		}
	}

	if (info->attrs[NL80211_ATTR_CH_SWITCH_BLOCK_TX])
		params.block_tx = true;

	wdev_lock(wdev);
	err = rdev_channel_switch(rdev, dev, &params);
	wdev_unlock(wdev);

	return err;
}

static int nl80211_send_bss(struct sk_buff *msg, struct netlink_callback *cb,
			    u32 seq, int flags,
			    struct cfg80211_registered_device *rdev,
			    struct wireless_dev *wdev,
			    struct cfg80211_internal_bss *intbss)
{
	struct cfg80211_bss *res = &intbss->pub;
	const struct cfg80211_bss_ies *ies;
	void *hdr;
	struct nlattr *bss;

	ASSERT_WDEV_LOCK(wdev);

	hdr = nl80211hdr_put(msg, NETLINK_CB(cb->skb).portid, seq, flags,
			     NL80211_CMD_NEW_SCAN_RESULTS);
	if (!hdr)
		return -1;

	genl_dump_check_consistent(cb, hdr);

	if (nla_put_u32(msg, NL80211_ATTR_GENERATION, rdev->bss_generation))
		goto nla_put_failure;
	if (wdev->netdev &&
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, wdev->netdev->ifindex))
		goto nla_put_failure;
	if (nla_put_u64_64bit(msg, NL80211_ATTR_WDEV, wdev_id(wdev),
			      NL80211_ATTR_PAD))
		goto nla_put_failure;

	bss = nla_nest_start_noflag(msg, NL80211_ATTR_BSS);
	if (!bss)
		goto nla_put_failure;
	if ((!is_zero_ether_addr(res->bssid) &&
	     nla_put(msg, NL80211_BSS_BSSID, ETH_ALEN, res->bssid)))
		goto nla_put_failure;

	rcu_read_lock();
	/* indicate whether we have probe response data or not */
	if (rcu_access_pointer(res->proberesp_ies) &&
	    nla_put_flag(msg, NL80211_BSS_PRESP_DATA))
		goto fail_unlock_rcu;

	/* this pointer prefers to be pointed to probe response data
	 * but is always valid
	 */
	ies = rcu_dereference(res->ies);
	if (ies) {
		if (nla_put_u64_64bit(msg, NL80211_BSS_TSF, ies->tsf,
				      NL80211_BSS_PAD))
			goto fail_unlock_rcu;
		if (ies->len && nla_put(msg, NL80211_BSS_INFORMATION_ELEMENTS,
					ies->len, ies->data))
			goto fail_unlock_rcu;
	}

	/* and this pointer is always (unless driver didn't know) beacon data */
	ies = rcu_dereference(res->beacon_ies);
	if (ies && ies->from_beacon) {
		if (nla_put_u64_64bit(msg, NL80211_BSS_BEACON_TSF, ies->tsf,
				      NL80211_BSS_PAD))
			goto fail_unlock_rcu;
		if (ies->len && nla_put(msg, NL80211_BSS_BEACON_IES,
					ies->len, ies->data))
			goto fail_unlock_rcu;
	}
	rcu_read_unlock();

	if (res->beacon_interval &&
	    nla_put_u16(msg, NL80211_BSS_BEACON_INTERVAL, res->beacon_interval))
		goto nla_put_failure;
	if (nla_put_u16(msg, NL80211_BSS_CAPABILITY, res->capability) ||
	    nla_put_u32(msg, NL80211_BSS_FREQUENCY, res->channel->center_freq) ||
	    nla_put_u32(msg, NL80211_BSS_CHAN_WIDTH, res->scan_width) ||
	    nla_put_u32(msg, NL80211_BSS_SEEN_MS_AGO,
			jiffies_to_msecs(jiffies - intbss->ts)))
		goto nla_put_failure;

	if (intbss->parent_tsf &&
	    (nla_put_u64_64bit(msg, NL80211_BSS_PARENT_TSF,
			       intbss->parent_tsf, NL80211_BSS_PAD) ||
	     nla_put(msg, NL80211_BSS_PARENT_BSSID, ETH_ALEN,
		     intbss->parent_bssid)))
		goto nla_put_failure;

	if (intbss->ts_boottime &&
	    nla_put_u64_64bit(msg, NL80211_BSS_LAST_SEEN_BOOTTIME,
			      intbss->ts_boottime, NL80211_BSS_PAD))
		goto nla_put_failure;

	if (!nl80211_put_signal(msg, intbss->pub.chains,
				intbss->pub.chain_signal,
				NL80211_BSS_CHAIN_SIGNAL))
		goto nla_put_failure;

	switch (rdev->wiphy.signal_type) {
	case CFG80211_SIGNAL_TYPE_MBM:
		if (nla_put_u32(msg, NL80211_BSS_SIGNAL_MBM, res->signal))
			goto nla_put_failure;
		break;
	case CFG80211_SIGNAL_TYPE_UNSPEC:
		if (nla_put_u8(msg, NL80211_BSS_SIGNAL_UNSPEC, res->signal))
			goto nla_put_failure;
		break;
	default:
		break;
	}

	switch (wdev->iftype) {
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_STATION:
		if (intbss == wdev->current_bss &&
		    nla_put_u32(msg, NL80211_BSS_STATUS,
				NL80211_BSS_STATUS_ASSOCIATED))
			goto nla_put_failure;
		break;
	case NL80211_IFTYPE_ADHOC:
		if (intbss == wdev->current_bss &&
		    nla_put_u32(msg, NL80211_BSS_STATUS,
				NL80211_BSS_STATUS_IBSS_JOINED))
			goto nla_put_failure;
		break;
	default:
		break;
	}

	nla_nest_end(msg, bss);

	genlmsg_end(msg, hdr);
	return 0;

 fail_unlock_rcu:
	rcu_read_unlock();
 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nl80211_dump_scan(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct cfg80211_registered_device *rdev;
	struct cfg80211_internal_bss *scan;
	struct wireless_dev *wdev;
	int start = cb->args[2], idx = 0;
	int err;

	rtnl_lock();
	err = nl80211_prepare_wdev_dump(cb, &rdev, &wdev);
	if (err) {
		rtnl_unlock();
		return err;
	}

	wdev_lock(wdev);
	spin_lock_bh(&rdev->bss_lock);

	/*
	 * dump_scan will be called multiple times to break up the scan results
	 * into multiple messages.  It is unlikely that any more bss-es will be
	 * expired after the first call, so only call only call this on the
	 * first dump_scan invocation.
	 */
	if (start == 0)
		cfg80211_bss_expire(rdev);

	cb->seq = rdev->bss_generation;

	list_for_each_entry(scan, &rdev->bss_list, list) {
		if (++idx <= start)
			continue;
		if (nl80211_send_bss(skb, cb,
				cb->nlh->nlmsg_seq, NLM_F_MULTI,
				rdev, wdev, scan) < 0) {
			idx--;
			break;
		}
	}

	spin_unlock_bh(&rdev->bss_lock);
	wdev_unlock(wdev);

	cb->args[2] = idx;
	rtnl_unlock();

	return skb->len;
}

static int nl80211_send_survey(struct sk_buff *msg, u32 portid, u32 seq,
			       int flags, struct net_device *dev,
			       bool allow_radio_stats,
			       struct survey_info *survey)
{
	void *hdr;
	struct nlattr *infoattr;

	/* skip radio stats if userspace didn't request them */
	if (!survey->channel && !allow_radio_stats)
		return 0;

	hdr = nl80211hdr_put(msg, portid, seq, flags,
			     NL80211_CMD_NEW_SURVEY_RESULTS);
	if (!hdr)
		return -ENOMEM;

	if (nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex))
		goto nla_put_failure;

	infoattr = nla_nest_start_noflag(msg, NL80211_ATTR_SURVEY_INFO);
	if (!infoattr)
		goto nla_put_failure;

	if (survey->channel &&
	    nla_put_u32(msg, NL80211_SURVEY_INFO_FREQUENCY,
			survey->channel->center_freq))
		goto nla_put_failure;

	if ((survey->filled & SURVEY_INFO_NOISE_DBM) &&
	    nla_put_u8(msg, NL80211_SURVEY_INFO_NOISE, survey->noise))
		goto nla_put_failure;
	if ((survey->filled & SURVEY_INFO_IN_USE) &&
	    nla_put_flag(msg, NL80211_SURVEY_INFO_IN_USE))
		goto nla_put_failure;
	if ((survey->filled & SURVEY_INFO_TIME) &&
	    nla_put_u64_64bit(msg, NL80211_SURVEY_INFO_TIME,
			survey->time, NL80211_SURVEY_INFO_PAD))
		goto nla_put_failure;
	if ((survey->filled & SURVEY_INFO_TIME_BUSY) &&
	    nla_put_u64_64bit(msg, NL80211_SURVEY_INFO_TIME_BUSY,
			      survey->time_busy, NL80211_SURVEY_INFO_PAD))
		goto nla_put_failure;
	if ((survey->filled & SURVEY_INFO_TIME_EXT_BUSY) &&
	    nla_put_u64_64bit(msg, NL80211_SURVEY_INFO_TIME_EXT_BUSY,
			      survey->time_ext_busy, NL80211_SURVEY_INFO_PAD))
		goto nla_put_failure;
	if ((survey->filled & SURVEY_INFO_TIME_RX) &&
	    nla_put_u64_64bit(msg, NL80211_SURVEY_INFO_TIME_RX,
			      survey->time_rx, NL80211_SURVEY_INFO_PAD))
		goto nla_put_failure;
	if ((survey->filled & SURVEY_INFO_TIME_TX) &&
	    nla_put_u64_64bit(msg, NL80211_SURVEY_INFO_TIME_TX,
			      survey->time_tx, NL80211_SURVEY_INFO_PAD))
		goto nla_put_failure;
	if ((survey->filled & SURVEY_INFO_TIME_SCAN) &&
	    nla_put_u64_64bit(msg, NL80211_SURVEY_INFO_TIME_SCAN,
			      survey->time_scan, NL80211_SURVEY_INFO_PAD))
		goto nla_put_failure;

	nla_nest_end(msg, infoattr);

	genlmsg_end(msg, hdr);
	return 0;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int nl80211_dump_survey(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct nlattr **attrbuf = genl_family_attrbuf(&nl80211_fam);
	struct survey_info survey;
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *wdev;
	int survey_idx = cb->args[2];
	int res;
	bool radio_stats;

	rtnl_lock();
	res = nl80211_prepare_wdev_dump(cb, &rdev, &wdev);
	if (res)
		goto out_err;

	/* prepare_wdev_dump parsed the attributes */
	radio_stats = attrbuf[NL80211_ATTR_SURVEY_RADIO_STATS];

	if (!wdev->netdev) {
		res = -EINVAL;
		goto out_err;
	}

	if (!rdev->ops->dump_survey) {
		res = -EOPNOTSUPP;
		goto out_err;
	}

	while (1) {
		res = rdev_dump_survey(rdev, wdev->netdev, survey_idx, &survey);
		if (res == -ENOENT)
			break;
		if (res)
			goto out_err;

		/* don't send disabled channels, but do send non-channel data */
		if (survey.channel &&
		    survey.channel->flags & IEEE80211_CHAN_DISABLED) {
			survey_idx++;
			continue;
		}

		if (nl80211_send_survey(skb,
				NETLINK_CB(cb->skb).portid,
				cb->nlh->nlmsg_seq, NLM_F_MULTI,
				wdev->netdev, radio_stats, &survey) < 0)
			goto out;
		survey_idx++;
	}

 out:
	cb->args[2] = survey_idx;
	res = skb->len;
 out_err:
	rtnl_unlock();
	return res;
}

static bool nl80211_valid_wpa_versions(u32 wpa_versions)
{
	return !(wpa_versions & ~(NL80211_WPA_VERSION_1 |
				  NL80211_WPA_VERSION_2));
}

static int nl80211_authenticate(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct ieee80211_channel *chan;
	const u8 *bssid, *ssid, *ie = NULL, *auth_data = NULL;
	int err, ssid_len, ie_len = 0, auth_data_len = 0;
	enum nl80211_auth_type auth_type;
	struct key_parse key;
	bool local_state_change;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_AUTH_TYPE])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_SSID])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_WIPHY_FREQ])
		return -EINVAL;

	err = nl80211_parse_key(info, &key);
	if (err)
		return err;

	if (key.idx >= 0) {
		if (key.type != -1 && key.type != NL80211_KEYTYPE_GROUP)
			return -EINVAL;
		if (!key.p.key || !key.p.key_len)
			return -EINVAL;
		if ((key.p.cipher != WLAN_CIPHER_SUITE_WEP40 ||
		     key.p.key_len != WLAN_KEY_LEN_WEP40) &&
		    (key.p.cipher != WLAN_CIPHER_SUITE_WEP104 ||
		     key.p.key_len != WLAN_KEY_LEN_WEP104))
			return -EINVAL;
		if (key.idx > 3)
			return -EINVAL;
	} else {
		key.p.key_len = 0;
		key.p.key = NULL;
	}

	if (key.idx >= 0) {
		int i;
		bool ok = false;

		for (i = 0; i < rdev->wiphy.n_cipher_suites; i++) {
			if (key.p.cipher == rdev->wiphy.cipher_suites[i]) {
				ok = true;
				break;
			}
		}
		if (!ok)
			return -EINVAL;
	}

	if (!rdev->ops->auth)
		return -EOPNOTSUPP;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT)
		return -EOPNOTSUPP;

	bssid = nla_data(info->attrs[NL80211_ATTR_MAC]);
	chan = nl80211_get_valid_chan(&rdev->wiphy,
				      info->attrs[NL80211_ATTR_WIPHY_FREQ]);
	if (!chan)
		return -EINVAL;

	ssid = nla_data(info->attrs[NL80211_ATTR_SSID]);
	ssid_len = nla_len(info->attrs[NL80211_ATTR_SSID]);

	if (info->attrs[NL80211_ATTR_IE]) {
		ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	auth_type = nla_get_u32(info->attrs[NL80211_ATTR_AUTH_TYPE]);
	if (!nl80211_valid_auth_type(rdev, auth_type, NL80211_CMD_AUTHENTICATE))
		return -EINVAL;

	if ((auth_type == NL80211_AUTHTYPE_SAE ||
	     auth_type == NL80211_AUTHTYPE_FILS_SK ||
	     auth_type == NL80211_AUTHTYPE_FILS_SK_PFS ||
	     auth_type == NL80211_AUTHTYPE_FILS_PK) &&
	    !info->attrs[NL80211_ATTR_AUTH_DATA])
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_AUTH_DATA]) {
		if (auth_type != NL80211_AUTHTYPE_SAE &&
		    auth_type != NL80211_AUTHTYPE_FILS_SK &&
		    auth_type != NL80211_AUTHTYPE_FILS_SK_PFS &&
		    auth_type != NL80211_AUTHTYPE_FILS_PK)
			return -EINVAL;
		auth_data = nla_data(info->attrs[NL80211_ATTR_AUTH_DATA]);
		auth_data_len = nla_len(info->attrs[NL80211_ATTR_AUTH_DATA]);
		/* need to include at least Auth Transaction and Status Code */
		if (auth_data_len < 4)
			return -EINVAL;
	}

	local_state_change = !!info->attrs[NL80211_ATTR_LOCAL_STATE_CHANGE];

	/*
	 * Since we no longer track auth state, ignore
	 * requests to only change local state.
	 */
	if (local_state_change)
		return 0;

	wdev_lock(dev->ieee80211_ptr);
	err = cfg80211_mlme_auth(rdev, dev, chan, auth_type, bssid,
				 ssid, ssid_len, ie, ie_len,
				 key.p.key, key.p.key_len, key.idx,
				 auth_data, auth_data_len);
	wdev_unlock(dev->ieee80211_ptr);
	return err;
}

static int validate_pae_over_nl80211(struct cfg80211_registered_device *rdev,
				     struct genl_info *info)
{
	if (!info->attrs[NL80211_ATTR_SOCKET_OWNER]) {
		GENL_SET_ERR_MSG(info, "SOCKET_OWNER not set");
		return -EINVAL;
	}

	if (!rdev->ops->tx_control_port ||
	    !wiphy_ext_feature_isset(&rdev->wiphy,
				     NL80211_EXT_FEATURE_CONTROL_PORT_OVER_NL80211))
		return -EOPNOTSUPP;

	return 0;
}

static int nl80211_crypto_settings(struct cfg80211_registered_device *rdev,
				   struct genl_info *info,
				   struct cfg80211_crypto_settings *settings,
				   int cipher_limit)
{
	memset(settings, 0, sizeof(*settings));

	settings->control_port = info->attrs[NL80211_ATTR_CONTROL_PORT];

	if (info->attrs[NL80211_ATTR_CONTROL_PORT_ETHERTYPE]) {
		u16 proto;

		proto = nla_get_u16(
			info->attrs[NL80211_ATTR_CONTROL_PORT_ETHERTYPE]);
		settings->control_port_ethertype = cpu_to_be16(proto);
		if (!(rdev->wiphy.flags & WIPHY_FLAG_CONTROL_PORT_PROTOCOL) &&
		    proto != ETH_P_PAE)
			return -EINVAL;
		if (info->attrs[NL80211_ATTR_CONTROL_PORT_NO_ENCRYPT])
			settings->control_port_no_encrypt = true;
	} else
		settings->control_port_ethertype = cpu_to_be16(ETH_P_PAE);

	if (info->attrs[NL80211_ATTR_CONTROL_PORT_OVER_NL80211]) {
		int r = validate_pae_over_nl80211(rdev, info);

		if (r < 0)
			return r;

		settings->control_port_over_nl80211 = true;
	}

	if (info->attrs[NL80211_ATTR_CIPHER_SUITES_PAIRWISE]) {
		void *data;
		int len, i;

		data = nla_data(info->attrs[NL80211_ATTR_CIPHER_SUITES_PAIRWISE]);
		len = nla_len(info->attrs[NL80211_ATTR_CIPHER_SUITES_PAIRWISE]);
		settings->n_ciphers_pairwise = len / sizeof(u32);

		if (len % sizeof(u32))
			return -EINVAL;

		if (settings->n_ciphers_pairwise > cipher_limit)
			return -EINVAL;

		memcpy(settings->ciphers_pairwise, data, len);

		for (i = 0; i < settings->n_ciphers_pairwise; i++)
			if (!cfg80211_supported_cipher_suite(
					&rdev->wiphy,
					settings->ciphers_pairwise[i]))
				return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_CIPHER_SUITE_GROUP]) {
		settings->cipher_group =
			nla_get_u32(info->attrs[NL80211_ATTR_CIPHER_SUITE_GROUP]);
		if (!cfg80211_supported_cipher_suite(&rdev->wiphy,
						     settings->cipher_group))
			return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_WPA_VERSIONS]) {
		settings->wpa_versions =
			nla_get_u32(info->attrs[NL80211_ATTR_WPA_VERSIONS]);
		if (!nl80211_valid_wpa_versions(settings->wpa_versions))
			return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_AKM_SUITES]) {
		void *data;
		int len;

		data = nla_data(info->attrs[NL80211_ATTR_AKM_SUITES]);
		len = nla_len(info->attrs[NL80211_ATTR_AKM_SUITES]);
		settings->n_akm_suites = len / sizeof(u32);

		if (len % sizeof(u32))
			return -EINVAL;

		if (settings->n_akm_suites > NL80211_MAX_NR_AKM_SUITES)
			return -EINVAL;

		memcpy(settings->akm_suites, data, len);
	}

	if (info->attrs[NL80211_ATTR_PMK]) {
		if (nla_len(info->attrs[NL80211_ATTR_PMK]) != WLAN_PMK_LEN)
			return -EINVAL;
		if (!wiphy_ext_feature_isset(&rdev->wiphy,
					     NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_PSK))
			return -EINVAL;
		settings->psk = nla_data(info->attrs[NL80211_ATTR_PMK]);
	}

	return 0;
}

static int nl80211_associate(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct ieee80211_channel *chan;
	struct cfg80211_assoc_request req = {};
	const u8 *bssid, *ssid;
	int err, ssid_len = 0;

	if (dev->ieee80211_ptr->conn_owner_nlportid &&
	    dev->ieee80211_ptr->conn_owner_nlportid != info->snd_portid)
		return -EPERM;

	if (!info->attrs[NL80211_ATTR_MAC] ||
	    !info->attrs[NL80211_ATTR_SSID] ||
	    !info->attrs[NL80211_ATTR_WIPHY_FREQ])
		return -EINVAL;

	if (!rdev->ops->assoc)
		return -EOPNOTSUPP;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT)
		return -EOPNOTSUPP;

	bssid = nla_data(info->attrs[NL80211_ATTR_MAC]);

	chan = nl80211_get_valid_chan(&rdev->wiphy,
				      info->attrs[NL80211_ATTR_WIPHY_FREQ]);
	if (!chan)
		return -EINVAL;

	ssid = nla_data(info->attrs[NL80211_ATTR_SSID]);
	ssid_len = nla_len(info->attrs[NL80211_ATTR_SSID]);

	if (info->attrs[NL80211_ATTR_IE]) {
		req.ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		req.ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	if (info->attrs[NL80211_ATTR_USE_MFP]) {
		enum nl80211_mfp mfp =
			nla_get_u32(info->attrs[NL80211_ATTR_USE_MFP]);
		if (mfp == NL80211_MFP_REQUIRED)
			req.use_mfp = true;
		else if (mfp != NL80211_MFP_NO)
			return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_PREV_BSSID])
		req.prev_bssid = nla_data(info->attrs[NL80211_ATTR_PREV_BSSID]);

	if (nla_get_flag(info->attrs[NL80211_ATTR_DISABLE_HT]))
		req.flags |= ASSOC_REQ_DISABLE_HT;

	if (info->attrs[NL80211_ATTR_HT_CAPABILITY_MASK])
		memcpy(&req.ht_capa_mask,
		       nla_data(info->attrs[NL80211_ATTR_HT_CAPABILITY_MASK]),
		       sizeof(req.ht_capa_mask));

	if (info->attrs[NL80211_ATTR_HT_CAPABILITY]) {
		if (!info->attrs[NL80211_ATTR_HT_CAPABILITY_MASK])
			return -EINVAL;
		memcpy(&req.ht_capa,
		       nla_data(info->attrs[NL80211_ATTR_HT_CAPABILITY]),
		       sizeof(req.ht_capa));
	}

	if (nla_get_flag(info->attrs[NL80211_ATTR_DISABLE_VHT]))
		req.flags |= ASSOC_REQ_DISABLE_VHT;

	if (info->attrs[NL80211_ATTR_VHT_CAPABILITY_MASK])
		memcpy(&req.vht_capa_mask,
		       nla_data(info->attrs[NL80211_ATTR_VHT_CAPABILITY_MASK]),
		       sizeof(req.vht_capa_mask));

	if (info->attrs[NL80211_ATTR_VHT_CAPABILITY]) {
		if (!info->attrs[NL80211_ATTR_VHT_CAPABILITY_MASK])
			return -EINVAL;
		memcpy(&req.vht_capa,
		       nla_data(info->attrs[NL80211_ATTR_VHT_CAPABILITY]),
		       sizeof(req.vht_capa));
	}

	if (nla_get_flag(info->attrs[NL80211_ATTR_USE_RRM])) {
		if (!((rdev->wiphy.features &
			NL80211_FEATURE_DS_PARAM_SET_IE_IN_PROBES) &&
		       (rdev->wiphy.features & NL80211_FEATURE_QUIET)) &&
		    !wiphy_ext_feature_isset(&rdev->wiphy,
					     NL80211_EXT_FEATURE_RRM))
			return -EINVAL;
		req.flags |= ASSOC_REQ_USE_RRM;
	}

	if (info->attrs[NL80211_ATTR_FILS_KEK]) {
		req.fils_kek = nla_data(info->attrs[NL80211_ATTR_FILS_KEK]);
		req.fils_kek_len = nla_len(info->attrs[NL80211_ATTR_FILS_KEK]);
		if (!info->attrs[NL80211_ATTR_FILS_NONCES])
			return -EINVAL;
		req.fils_nonces =
			nla_data(info->attrs[NL80211_ATTR_FILS_NONCES]);
	}

	err = nl80211_crypto_settings(rdev, info, &req.crypto, 1);
	if (!err) {
		wdev_lock(dev->ieee80211_ptr);

		err = cfg80211_mlme_assoc(rdev, dev, chan, bssid,
					  ssid, ssid_len, &req);

		if (!err && info->attrs[NL80211_ATTR_SOCKET_OWNER]) {
			dev->ieee80211_ptr->conn_owner_nlportid =
				info->snd_portid;
			memcpy(dev->ieee80211_ptr->disconnect_bssid,
			       bssid, ETH_ALEN);
		}

		wdev_unlock(dev->ieee80211_ptr);
	}

	return err;
}

static int nl80211_deauthenticate(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	const u8 *ie = NULL, *bssid;
	int ie_len = 0, err;
	u16 reason_code;
	bool local_state_change;

	if (dev->ieee80211_ptr->conn_owner_nlportid &&
	    dev->ieee80211_ptr->conn_owner_nlportid != info->snd_portid)
		return -EPERM;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_REASON_CODE])
		return -EINVAL;

	if (!rdev->ops->deauth)
		return -EOPNOTSUPP;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT)
		return -EOPNOTSUPP;

	bssid = nla_data(info->attrs[NL80211_ATTR_MAC]);

	reason_code = nla_get_u16(info->attrs[NL80211_ATTR_REASON_CODE]);
	if (reason_code == 0) {
		/* Reason Code 0 is reserved */
		return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_IE]) {
		ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	local_state_change = !!info->attrs[NL80211_ATTR_LOCAL_STATE_CHANGE];

	wdev_lock(dev->ieee80211_ptr);
	err = cfg80211_mlme_deauth(rdev, dev, bssid, ie, ie_len, reason_code,
				   local_state_change);
	wdev_unlock(dev->ieee80211_ptr);
	return err;
}

static int nl80211_disassociate(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	const u8 *ie = NULL, *bssid;
	int ie_len = 0, err;
	u16 reason_code;
	bool local_state_change;

	if (dev->ieee80211_ptr->conn_owner_nlportid &&
	    dev->ieee80211_ptr->conn_owner_nlportid != info->snd_portid)
		return -EPERM;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_REASON_CODE])
		return -EINVAL;

	if (!rdev->ops->disassoc)
		return -EOPNOTSUPP;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT)
		return -EOPNOTSUPP;

	bssid = nla_data(info->attrs[NL80211_ATTR_MAC]);

	reason_code = nla_get_u16(info->attrs[NL80211_ATTR_REASON_CODE]);
	if (reason_code == 0) {
		/* Reason Code 0 is reserved */
		return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_IE]) {
		ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	local_state_change = !!info->attrs[NL80211_ATTR_LOCAL_STATE_CHANGE];

	wdev_lock(dev->ieee80211_ptr);
	err = cfg80211_mlme_disassoc(rdev, dev, bssid, ie, ie_len, reason_code,
				     local_state_change);
	wdev_unlock(dev->ieee80211_ptr);
	return err;
}

static bool
nl80211_parse_mcast_rate(struct cfg80211_registered_device *rdev,
			 int mcast_rate[NUM_NL80211_BANDS],
			 int rateval)
{
	struct wiphy *wiphy = &rdev->wiphy;
	bool found = false;
	int band, i;

	for (band = 0; band < NUM_NL80211_BANDS; band++) {
		struct ieee80211_supported_band *sband;

		sband = wiphy->bands[band];
		if (!sband)
			continue;

		for (i = 0; i < sband->n_bitrates; i++) {
			if (sband->bitrates[i].bitrate == rateval) {
				mcast_rate[band] = i + 1;
				found = true;
				break;
			}
		}
	}

	return found;
}

static int nl80211_join_ibss(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct cfg80211_ibss_params ibss;
	struct wiphy *wiphy;
	struct cfg80211_cached_keys *connkeys = NULL;
	int err;

	memset(&ibss, 0, sizeof(ibss));

	if (!info->attrs[NL80211_ATTR_SSID] ||
	    !nla_len(info->attrs[NL80211_ATTR_SSID]))
		return -EINVAL;

	ibss.beacon_interval = 100;

	if (info->attrs[NL80211_ATTR_BEACON_INTERVAL])
		ibss.beacon_interval =
			nla_get_u32(info->attrs[NL80211_ATTR_BEACON_INTERVAL]);

	err = cfg80211_validate_beacon_int(rdev, NL80211_IFTYPE_ADHOC,
					   ibss.beacon_interval);
	if (err)
		return err;

	if (!rdev->ops->join_ibss)
		return -EOPNOTSUPP;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_ADHOC)
		return -EOPNOTSUPP;

	wiphy = &rdev->wiphy;

	if (info->attrs[NL80211_ATTR_MAC]) {
		ibss.bssid = nla_data(info->attrs[NL80211_ATTR_MAC]);

		if (!is_valid_ether_addr(ibss.bssid))
			return -EINVAL;
	}
	ibss.ssid = nla_data(info->attrs[NL80211_ATTR_SSID]);
	ibss.ssid_len = nla_len(info->attrs[NL80211_ATTR_SSID]);

	if (info->attrs[NL80211_ATTR_IE]) {
		ibss.ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		ibss.ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	err = nl80211_parse_chandef(rdev, info, &ibss.chandef);
	if (err)
		return err;

	if (!cfg80211_reg_can_beacon(&rdev->wiphy, &ibss.chandef,
				     NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	switch (ibss.chandef.width) {
	case NL80211_CHAN_WIDTH_5:
	case NL80211_CHAN_WIDTH_10:
	case NL80211_CHAN_WIDTH_20_NOHT:
		break;
	case NL80211_CHAN_WIDTH_20:
	case NL80211_CHAN_WIDTH_40:
		if (!(rdev->wiphy.features & NL80211_FEATURE_HT_IBSS))
			return -EINVAL;
		break;
	case NL80211_CHAN_WIDTH_80:
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_160:
		if (!(rdev->wiphy.features & NL80211_FEATURE_HT_IBSS))
			return -EINVAL;
		if (!wiphy_ext_feature_isset(&rdev->wiphy,
					     NL80211_EXT_FEATURE_VHT_IBSS))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	ibss.channel_fixed = !!info->attrs[NL80211_ATTR_FREQ_FIXED];
	ibss.privacy = !!info->attrs[NL80211_ATTR_PRIVACY];

	if (info->attrs[NL80211_ATTR_BSS_BASIC_RATES]) {
		u8 *rates =
			nla_data(info->attrs[NL80211_ATTR_BSS_BASIC_RATES]);
		int n_rates =
			nla_len(info->attrs[NL80211_ATTR_BSS_BASIC_RATES]);
		struct ieee80211_supported_band *sband =
			wiphy->bands[ibss.chandef.chan->band];

		err = ieee80211_get_ratemask(sband, rates, n_rates,
					     &ibss.basic_rates);
		if (err)
			return err;
	}

	if (info->attrs[NL80211_ATTR_HT_CAPABILITY_MASK])
		memcpy(&ibss.ht_capa_mask,
		       nla_data(info->attrs[NL80211_ATTR_HT_CAPABILITY_MASK]),
		       sizeof(ibss.ht_capa_mask));

	if (info->attrs[NL80211_ATTR_HT_CAPABILITY]) {
		if (!info->attrs[NL80211_ATTR_HT_CAPABILITY_MASK])
			return -EINVAL;
		memcpy(&ibss.ht_capa,
		       nla_data(info->attrs[NL80211_ATTR_HT_CAPABILITY]),
		       sizeof(ibss.ht_capa));
	}

	if (info->attrs[NL80211_ATTR_MCAST_RATE] &&
	    !nl80211_parse_mcast_rate(rdev, ibss.mcast_rate,
			nla_get_u32(info->attrs[NL80211_ATTR_MCAST_RATE])))
		return -EINVAL;

	if (ibss.privacy && info->attrs[NL80211_ATTR_KEYS]) {
		bool no_ht = false;

		connkeys = nl80211_parse_connkeys(rdev, info, &no_ht);
		if (IS_ERR(connkeys))
			return PTR_ERR(connkeys);

		if ((ibss.chandef.width != NL80211_CHAN_WIDTH_20_NOHT) &&
		    no_ht) {
			kzfree(connkeys);
			return -EINVAL;
		}
	}

	ibss.control_port =
		nla_get_flag(info->attrs[NL80211_ATTR_CONTROL_PORT]);

	if (info->attrs[NL80211_ATTR_CONTROL_PORT_OVER_NL80211]) {
		int r = validate_pae_over_nl80211(rdev, info);

		if (r < 0) {
			kzfree(connkeys);
			return r;
		}

		ibss.control_port_over_nl80211 = true;
	}

	ibss.userspace_handles_dfs =
		nla_get_flag(info->attrs[NL80211_ATTR_HANDLE_DFS]);

	wdev_lock(dev->ieee80211_ptr);
	err = __cfg80211_join_ibss(rdev, dev, &ibss, connkeys);
	if (err)
		kzfree(connkeys);
	else if (info->attrs[NL80211_ATTR_SOCKET_OWNER])
		dev->ieee80211_ptr->conn_owner_nlportid = info->snd_portid;
	wdev_unlock(dev->ieee80211_ptr);

	return err;
}

static int nl80211_leave_ibss(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];

	if (!rdev->ops->leave_ibss)
		return -EOPNOTSUPP;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_ADHOC)
		return -EOPNOTSUPP;

	return cfg80211_leave_ibss(rdev, dev, false);
}

static int nl80211_set_mcast_rate(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	int mcast_rate[NUM_NL80211_BANDS];
	u32 nla_rate;
	int err;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_ADHOC &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_MESH_POINT &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_OCB)
		return -EOPNOTSUPP;

	if (!rdev->ops->set_mcast_rate)
		return -EOPNOTSUPP;

	memset(mcast_rate, 0, sizeof(mcast_rate));

	if (!info->attrs[NL80211_ATTR_MCAST_RATE])
		return -EINVAL;

	nla_rate = nla_get_u32(info->attrs[NL80211_ATTR_MCAST_RATE]);
	if (!nl80211_parse_mcast_rate(rdev, mcast_rate, nla_rate))
		return -EINVAL;

	err = rdev_set_mcast_rate(rdev, dev, mcast_rate);

	return err;
}

static struct sk_buff *
__cfg80211_alloc_vendor_skb(struct cfg80211_registered_device *rdev,
			    struct wireless_dev *wdev, int approxlen,
			    u32 portid, u32 seq, enum nl80211_commands cmd,
			    enum nl80211_attrs attr,
			    const struct nl80211_vendor_cmd_info *info,
			    gfp_t gfp)
{
	struct sk_buff *skb;
	void *hdr;
	struct nlattr *data;

	skb = nlmsg_new(approxlen + 100, gfp);
	if (!skb)
		return NULL;

	hdr = nl80211hdr_put(skb, portid, seq, 0, cmd);
	if (!hdr) {
		kfree_skb(skb);
		return NULL;
	}

	if (nla_put_u32(skb, NL80211_ATTR_WIPHY, rdev->wiphy_idx))
		goto nla_put_failure;

	if (info) {
		if (nla_put_u32(skb, NL80211_ATTR_VENDOR_ID,
				info->vendor_id))
			goto nla_put_failure;
		if (nla_put_u32(skb, NL80211_ATTR_VENDOR_SUBCMD,
				info->subcmd))
			goto nla_put_failure;
	}

	if (wdev) {
		if (nla_put_u64_64bit(skb, NL80211_ATTR_WDEV,
				      wdev_id(wdev), NL80211_ATTR_PAD))
			goto nla_put_failure;
		if (wdev->netdev &&
		    nla_put_u32(skb, NL80211_ATTR_IFINDEX,
				wdev->netdev->ifindex))
			goto nla_put_failure;
	}

	data = nla_nest_start_noflag(skb, attr);
	if (!data)
		goto nla_put_failure;

	((void **)skb->cb)[0] = rdev;
	((void **)skb->cb)[1] = hdr;
	((void **)skb->cb)[2] = data;

	return skb;

 nla_put_failure:
	kfree_skb(skb);
	return NULL;
}

struct sk_buff *__cfg80211_alloc_event_skb(struct wiphy *wiphy,
					   struct wireless_dev *wdev,
					   enum nl80211_commands cmd,
					   enum nl80211_attrs attr,
					   unsigned int portid,
					   int vendor_event_idx,
					   int approxlen, gfp_t gfp)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	const struct nl80211_vendor_cmd_info *info;

	switch (cmd) {
	case NL80211_CMD_TESTMODE:
		if (WARN_ON(vendor_event_idx != -1))
			return NULL;
		info = NULL;
		break;
	case NL80211_CMD_VENDOR:
		if (WARN_ON(vendor_event_idx < 0 ||
			    vendor_event_idx >= wiphy->n_vendor_events))
			return NULL;
		info = &wiphy->vendor_events[vendor_event_idx];
		break;
	default:
		WARN_ON(1);
		return NULL;
	}

	return __cfg80211_alloc_vendor_skb(rdev, wdev, approxlen, portid, 0,
					   cmd, attr, info, gfp);
}
EXPORT_SYMBOL(__cfg80211_alloc_event_skb);

void __cfg80211_send_event_skb(struct sk_buff *skb, gfp_t gfp)
{
	struct cfg80211_registered_device *rdev = ((void **)skb->cb)[0];
	void *hdr = ((void **)skb->cb)[1];
	struct nlmsghdr *nlhdr = nlmsg_hdr(skb);
	struct nlattr *data = ((void **)skb->cb)[2];
	enum nl80211_multicast_groups mcgrp = NL80211_MCGRP_TESTMODE;

	/* clear CB data for netlink core to own from now on */
	memset(skb->cb, 0, sizeof(skb->cb));

	nla_nest_end(skb, data);
	genlmsg_end(skb, hdr);

	if (nlhdr->nlmsg_pid) {
		genlmsg_unicast(wiphy_net(&rdev->wiphy), skb,
				nlhdr->nlmsg_pid);
	} else {
		if (data->nla_type == NL80211_ATTR_VENDOR_DATA)
			mcgrp = NL80211_MCGRP_VENDOR;

		genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy),
					skb, 0, mcgrp, gfp);
	}
}
EXPORT_SYMBOL(__cfg80211_send_event_skb);

#ifdef CONFIG_NL80211_TESTMODE
static int nl80211_testmode_do(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev =
		__cfg80211_wdev_from_attrs(genl_info_net(info), info->attrs);
	int err;

	if (!rdev->ops->testmode_cmd)
		return -EOPNOTSUPP;

	if (IS_ERR(wdev)) {
		err = PTR_ERR(wdev);
		if (err != -EINVAL)
			return err;
		wdev = NULL;
	} else if (wdev->wiphy != &rdev->wiphy) {
		return -EINVAL;
	}

	if (!info->attrs[NL80211_ATTR_TESTDATA])
		return -EINVAL;

	rdev->cur_cmd_info = info;
	err = rdev_testmode_cmd(rdev, wdev,
				nla_data(info->attrs[NL80211_ATTR_TESTDATA]),
				nla_len(info->attrs[NL80211_ATTR_TESTDATA]));
	rdev->cur_cmd_info = NULL;

	return err;
}

static int nl80211_testmode_dump(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	struct cfg80211_registered_device *rdev;
	int err;
	long phy_idx;
	void *data = NULL;
	int data_len = 0;

	rtnl_lock();

	if (cb->args[0]) {
		/*
		 * 0 is a valid index, but not valid for args[0],
		 * so we need to offset by 1.
		 */
		phy_idx = cb->args[0] - 1;

		rdev = cfg80211_rdev_by_wiphy_idx(phy_idx);
		if (!rdev) {
			err = -ENOENT;
			goto out_err;
		}
	} else {
		struct nlattr **attrbuf = genl_family_attrbuf(&nl80211_fam);

		err = nlmsg_parse_deprecated(cb->nlh,
					     GENL_HDRLEN + nl80211_fam.hdrsize,
					     attrbuf, nl80211_fam.maxattr,
					     nl80211_policy, NULL);
		if (err)
			goto out_err;

		rdev = __cfg80211_rdev_from_attrs(sock_net(skb->sk), attrbuf);
		if (IS_ERR(rdev)) {
			err = PTR_ERR(rdev);
			goto out_err;
		}
		phy_idx = rdev->wiphy_idx;

		if (attrbuf[NL80211_ATTR_TESTDATA])
			cb->args[1] = (long)attrbuf[NL80211_ATTR_TESTDATA];
	}

	if (cb->args[1]) {
		data = nla_data((void *)cb->args[1]);
		data_len = nla_len((void *)cb->args[1]);
	}

	if (!rdev->ops->testmode_dump) {
		err = -EOPNOTSUPP;
		goto out_err;
	}

	while (1) {
		void *hdr = nl80211hdr_put(skb, NETLINK_CB(cb->skb).portid,
					   cb->nlh->nlmsg_seq, NLM_F_MULTI,
					   NL80211_CMD_TESTMODE);
		struct nlattr *tmdata;

		if (!hdr)
			break;

		if (nla_put_u32(skb, NL80211_ATTR_WIPHY, phy_idx)) {
			genlmsg_cancel(skb, hdr);
			break;
		}

		tmdata = nla_nest_start_noflag(skb, NL80211_ATTR_TESTDATA);
		if (!tmdata) {
			genlmsg_cancel(skb, hdr);
			break;
		}
		err = rdev_testmode_dump(rdev, skb, cb, data, data_len);
		nla_nest_end(skb, tmdata);

		if (err == -ENOBUFS || err == -ENOENT) {
			genlmsg_cancel(skb, hdr);
			break;
		} else if (err) {
			genlmsg_cancel(skb, hdr);
			goto out_err;
		}

		genlmsg_end(skb, hdr);
	}

	err = skb->len;
	/* see above */
	cb->args[0] = phy_idx + 1;
 out_err:
	rtnl_unlock();
	return err;
}
#endif

static int nl80211_connect(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct cfg80211_connect_params connect;
	struct wiphy *wiphy;
	struct cfg80211_cached_keys *connkeys = NULL;
	int err;

	memset(&connect, 0, sizeof(connect));

	if (!info->attrs[NL80211_ATTR_SSID] ||
	    !nla_len(info->attrs[NL80211_ATTR_SSID]))
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_AUTH_TYPE]) {
		connect.auth_type =
			nla_get_u32(info->attrs[NL80211_ATTR_AUTH_TYPE]);
		if (!nl80211_valid_auth_type(rdev, connect.auth_type,
					     NL80211_CMD_CONNECT))
			return -EINVAL;
	} else
		connect.auth_type = NL80211_AUTHTYPE_AUTOMATIC;

	connect.privacy = info->attrs[NL80211_ATTR_PRIVACY];

	if (info->attrs[NL80211_ATTR_WANT_1X_4WAY_HS] &&
	    !wiphy_ext_feature_isset(&rdev->wiphy,
				     NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_1X))
		return -EINVAL;
	connect.want_1x = info->attrs[NL80211_ATTR_WANT_1X_4WAY_HS];

	err = nl80211_crypto_settings(rdev, info, &connect.crypto,
				      NL80211_MAX_NR_CIPHER_SUITES);
	if (err)
		return err;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT)
		return -EOPNOTSUPP;

	wiphy = &rdev->wiphy;

	connect.bg_scan_period = -1;
	if (info->attrs[NL80211_ATTR_BG_SCAN_PERIOD] &&
		(wiphy->flags & WIPHY_FLAG_SUPPORTS_FW_ROAM)) {
		connect.bg_scan_period =
			nla_get_u16(info->attrs[NL80211_ATTR_BG_SCAN_PERIOD]);
	}

	if (info->attrs[NL80211_ATTR_MAC])
		connect.bssid = nla_data(info->attrs[NL80211_ATTR_MAC]);
	else if (info->attrs[NL80211_ATTR_MAC_HINT])
		connect.bssid_hint =
			nla_data(info->attrs[NL80211_ATTR_MAC_HINT]);
	connect.ssid = nla_data(info->attrs[NL80211_ATTR_SSID]);
	connect.ssid_len = nla_len(info->attrs[NL80211_ATTR_SSID]);

	if (info->attrs[NL80211_ATTR_IE]) {
		connect.ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		connect.ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	if (info->attrs[NL80211_ATTR_USE_MFP]) {
		connect.mfp = nla_get_u32(info->attrs[NL80211_ATTR_USE_MFP]);
		if (connect.mfp == NL80211_MFP_OPTIONAL &&
		    !wiphy_ext_feature_isset(&rdev->wiphy,
					     NL80211_EXT_FEATURE_MFP_OPTIONAL))
			return -EOPNOTSUPP;
	} else {
		connect.mfp = NL80211_MFP_NO;
	}

	if (info->attrs[NL80211_ATTR_PREV_BSSID])
		connect.prev_bssid =
			nla_data(info->attrs[NL80211_ATTR_PREV_BSSID]);

	if (info->attrs[NL80211_ATTR_WIPHY_FREQ]) {
		connect.channel = nl80211_get_valid_chan(
			wiphy, info->attrs[NL80211_ATTR_WIPHY_FREQ]);
		if (!connect.channel)
			return -EINVAL;
	} else if (info->attrs[NL80211_ATTR_WIPHY_FREQ_HINT]) {
		connect.channel_hint = nl80211_get_valid_chan(
			wiphy, info->attrs[NL80211_ATTR_WIPHY_FREQ_HINT]);
		if (!connect.channel_hint)
			return -EINVAL;
	}

	if (connect.privacy && info->attrs[NL80211_ATTR_KEYS]) {
		connkeys = nl80211_parse_connkeys(rdev, info, NULL);
		if (IS_ERR(connkeys))
			return PTR_ERR(connkeys);
	}

	if (nla_get_flag(info->attrs[NL80211_ATTR_DISABLE_HT]))
		connect.flags |= ASSOC_REQ_DISABLE_HT;

	if (info->attrs[NL80211_ATTR_HT_CAPABILITY_MASK])
		memcpy(&connect.ht_capa_mask,
		       nla_data(info->attrs[NL80211_ATTR_HT_CAPABILITY_MASK]),
		       sizeof(connect.ht_capa_mask));

	if (info->attrs[NL80211_ATTR_HT_CAPABILITY]) {
		if (!info->attrs[NL80211_ATTR_HT_CAPABILITY_MASK]) {
			kzfree(connkeys);
			return -EINVAL;
		}
		memcpy(&connect.ht_capa,
		       nla_data(info->attrs[NL80211_ATTR_HT_CAPABILITY]),
		       sizeof(connect.ht_capa));
	}

	if (nla_get_flag(info->attrs[NL80211_ATTR_DISABLE_VHT]))
		connect.flags |= ASSOC_REQ_DISABLE_VHT;

	if (info->attrs[NL80211_ATTR_VHT_CAPABILITY_MASK])
		memcpy(&connect.vht_capa_mask,
		       nla_data(info->attrs[NL80211_ATTR_VHT_CAPABILITY_MASK]),
		       sizeof(connect.vht_capa_mask));

	if (info->attrs[NL80211_ATTR_VHT_CAPABILITY]) {
		if (!info->attrs[NL80211_ATTR_VHT_CAPABILITY_MASK]) {
			kzfree(connkeys);
			return -EINVAL;
		}
		memcpy(&connect.vht_capa,
		       nla_data(info->attrs[NL80211_ATTR_VHT_CAPABILITY]),
		       sizeof(connect.vht_capa));
	}

	if (nla_get_flag(info->attrs[NL80211_ATTR_USE_RRM])) {
		if (!((rdev->wiphy.features &
			NL80211_FEATURE_DS_PARAM_SET_IE_IN_PROBES) &&
		       (rdev->wiphy.features & NL80211_FEATURE_QUIET)) &&
		    !wiphy_ext_feature_isset(&rdev->wiphy,
					     NL80211_EXT_FEATURE_RRM)) {
			kzfree(connkeys);
			return -EINVAL;
		}
		connect.flags |= ASSOC_REQ_USE_RRM;
	}

	connect.pbss = nla_get_flag(info->attrs[NL80211_ATTR_PBSS]);
	if (connect.pbss && !rdev->wiphy.bands[NL80211_BAND_60GHZ]) {
		kzfree(connkeys);
		return -EOPNOTSUPP;
	}

	if (info->attrs[NL80211_ATTR_BSS_SELECT]) {
		/* bss selection makes no sense if bssid is set */
		if (connect.bssid) {
			kzfree(connkeys);
			return -EINVAL;
		}

		err = parse_bss_select(info->attrs[NL80211_ATTR_BSS_SELECT],
				       wiphy, &connect.bss_select);
		if (err) {
			kzfree(connkeys);
			return err;
		}
	}

	if (wiphy_ext_feature_isset(&rdev->wiphy,
				    NL80211_EXT_FEATURE_FILS_SK_OFFLOAD) &&
	    info->attrs[NL80211_ATTR_FILS_ERP_USERNAME] &&
	    info->attrs[NL80211_ATTR_FILS_ERP_REALM] &&
	    info->attrs[NL80211_ATTR_FILS_ERP_NEXT_SEQ_NUM] &&
	    info->attrs[NL80211_ATTR_FILS_ERP_RRK]) {
		connect.fils_erp_username =
			nla_data(info->attrs[NL80211_ATTR_FILS_ERP_USERNAME]);
		connect.fils_erp_username_len =
			nla_len(info->attrs[NL80211_ATTR_FILS_ERP_USERNAME]);
		connect.fils_erp_realm =
			nla_data(info->attrs[NL80211_ATTR_FILS_ERP_REALM]);
		connect.fils_erp_realm_len =
			nla_len(info->attrs[NL80211_ATTR_FILS_ERP_REALM]);
		connect.fils_erp_next_seq_num =
			nla_get_u16(
			   info->attrs[NL80211_ATTR_FILS_ERP_NEXT_SEQ_NUM]);
		connect.fils_erp_rrk =
			nla_data(info->attrs[NL80211_ATTR_FILS_ERP_RRK]);
		connect.fils_erp_rrk_len =
			nla_len(info->attrs[NL80211_ATTR_FILS_ERP_RRK]);
	} else if (info->attrs[NL80211_ATTR_FILS_ERP_USERNAME] ||
		   info->attrs[NL80211_ATTR_FILS_ERP_REALM] ||
		   info->attrs[NL80211_ATTR_FILS_ERP_NEXT_SEQ_NUM] ||
		   info->attrs[NL80211_ATTR_FILS_ERP_RRK]) {
		kzfree(connkeys);
		return -EINVAL;
	}

	if (nla_get_flag(info->attrs[NL80211_ATTR_EXTERNAL_AUTH_SUPPORT])) {
		if (!info->attrs[NL80211_ATTR_SOCKET_OWNER]) {
			kzfree(connkeys);
			GENL_SET_ERR_MSG(info,
					 "external auth requires connection ownership");
			return -EINVAL;
		}
		connect.flags |= CONNECT_REQ_EXTERNAL_AUTH_SUPPORT;
	}

	wdev_lock(dev->ieee80211_ptr);

	err = cfg80211_connect(rdev, dev, &connect, connkeys,
			       connect.prev_bssid);
	if (err)
		kzfree(connkeys);

	if (!err && info->attrs[NL80211_ATTR_SOCKET_OWNER]) {
		dev->ieee80211_ptr->conn_owner_nlportid = info->snd_portid;
		if (connect.bssid)
			memcpy(dev->ieee80211_ptr->disconnect_bssid,
			       connect.bssid, ETH_ALEN);
		else
			memset(dev->ieee80211_ptr->disconnect_bssid,
			       0, ETH_ALEN);
	}

	wdev_unlock(dev->ieee80211_ptr);

	return err;
}

static int nl80211_update_connect_params(struct sk_buff *skb,
					 struct genl_info *info)
{
	struct cfg80211_connect_params connect = {};
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	bool fils_sk_offload;
	u32 auth_type;
	u32 changed = 0;
	int ret;

	if (!rdev->ops->update_connect_params)
		return -EOPNOTSUPP;

	if (info->attrs[NL80211_ATTR_IE]) {
		connect.ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		connect.ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
		changed |= UPDATE_ASSOC_IES;
	}

	fils_sk_offload = wiphy_ext_feature_isset(&rdev->wiphy,
						  NL80211_EXT_FEATURE_FILS_SK_OFFLOAD);

	/*
	 * when driver supports fils-sk offload all attributes must be
	 * provided. So the else covers "fils-sk-not-all" and
	 * "no-fils-sk-any".
	 */
	if (fils_sk_offload &&
	    info->attrs[NL80211_ATTR_FILS_ERP_USERNAME] &&
	    info->attrs[NL80211_ATTR_FILS_ERP_REALM] &&
	    info->attrs[NL80211_ATTR_FILS_ERP_NEXT_SEQ_NUM] &&
	    info->attrs[NL80211_ATTR_FILS_ERP_RRK]) {
		connect.fils_erp_username =
			nla_data(info->attrs[NL80211_ATTR_FILS_ERP_USERNAME]);
		connect.fils_erp_username_len =
			nla_len(info->attrs[NL80211_ATTR_FILS_ERP_USERNAME]);
		connect.fils_erp_realm =
			nla_data(info->attrs[NL80211_ATTR_FILS_ERP_REALM]);
		connect.fils_erp_realm_len =
			nla_len(info->attrs[NL80211_ATTR_FILS_ERP_REALM]);
		connect.fils_erp_next_seq_num =
			nla_get_u16(
			   info->attrs[NL80211_ATTR_FILS_ERP_NEXT_SEQ_NUM]);
		connect.fils_erp_rrk =
			nla_data(info->attrs[NL80211_ATTR_FILS_ERP_RRK]);
		connect.fils_erp_rrk_len =
			nla_len(info->attrs[NL80211_ATTR_FILS_ERP_RRK]);
		changed |= UPDATE_FILS_ERP_INFO;
	} else if (info->attrs[NL80211_ATTR_FILS_ERP_USERNAME] ||
		   info->attrs[NL80211_ATTR_FILS_ERP_REALM] ||
		   info->attrs[NL80211_ATTR_FILS_ERP_NEXT_SEQ_NUM] ||
		   info->attrs[NL80211_ATTR_FILS_ERP_RRK]) {
		return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_AUTH_TYPE]) {
		auth_type = nla_get_u32(info->attrs[NL80211_ATTR_AUTH_TYPE]);
		if (!nl80211_valid_auth_type(rdev, auth_type,
					     NL80211_CMD_CONNECT))
			return -EINVAL;

		if (auth_type == NL80211_AUTHTYPE_FILS_SK &&
		    fils_sk_offload && !(changed & UPDATE_FILS_ERP_INFO))
			return -EINVAL;

		connect.auth_type = auth_type;
		changed |= UPDATE_AUTH_TYPE;
	}

	wdev_lock(dev->ieee80211_ptr);
	if (!wdev->current_bss)
		ret = -ENOLINK;
	else
		ret = rdev_update_connect_params(rdev, dev, &connect, changed);
	wdev_unlock(dev->ieee80211_ptr);

	return ret;
}

static int nl80211_disconnect(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	u16 reason;
	int ret;

	if (dev->ieee80211_ptr->conn_owner_nlportid &&
	    dev->ieee80211_ptr->conn_owner_nlportid != info->snd_portid)
		return -EPERM;

	if (!info->attrs[NL80211_ATTR_REASON_CODE])
		reason = WLAN_REASON_DEAUTH_LEAVING;
	else
		reason = nla_get_u16(info->attrs[NL80211_ATTR_REASON_CODE]);

	if (reason == 0)
		return -EINVAL;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT)
		return -EOPNOTSUPP;

	wdev_lock(dev->ieee80211_ptr);
	ret = cfg80211_disconnect(rdev, dev, reason, true);
	wdev_unlock(dev->ieee80211_ptr);
	return ret;
}

static int nl80211_wiphy_netns(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net *net;
	int err;

	if (info->attrs[NL80211_ATTR_PID]) {
		u32 pid = nla_get_u32(info->attrs[NL80211_ATTR_PID]);

		net = get_net_ns_by_pid(pid);
	} else if (info->attrs[NL80211_ATTR_NETNS_FD]) {
		u32 fd = nla_get_u32(info->attrs[NL80211_ATTR_NETNS_FD]);

		net = get_net_ns_by_fd(fd);
	} else {
		return -EINVAL;
	}

	if (IS_ERR(net))
		return PTR_ERR(net);

	err = 0;

	/* check if anything to do */
	if (!net_eq(wiphy_net(&rdev->wiphy), net))
		err = cfg80211_switch_netns(rdev, net);

	put_net(net);
	return err;
}

static int nl80211_setdel_pmksa(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	int (*rdev_ops)(struct wiphy *wiphy, struct net_device *dev,
			struct cfg80211_pmksa *pmksa) = NULL;
	struct net_device *dev = info->user_ptr[1];
	struct cfg80211_pmksa pmksa;

	memset(&pmksa, 0, sizeof(struct cfg80211_pmksa));

	if (!info->attrs[NL80211_ATTR_PMKID])
		return -EINVAL;

	pmksa.pmkid = nla_data(info->attrs[NL80211_ATTR_PMKID]);

	if (info->attrs[NL80211_ATTR_MAC]) {
		pmksa.bssid = nla_data(info->attrs[NL80211_ATTR_MAC]);
	} else if (info->attrs[NL80211_ATTR_SSID] &&
		   info->attrs[NL80211_ATTR_FILS_CACHE_ID] &&
		   (info->genlhdr->cmd == NL80211_CMD_DEL_PMKSA ||
		    info->attrs[NL80211_ATTR_PMK])) {
		pmksa.ssid = nla_data(info->attrs[NL80211_ATTR_SSID]);
		pmksa.ssid_len = nla_len(info->attrs[NL80211_ATTR_SSID]);
		pmksa.cache_id =
			nla_data(info->attrs[NL80211_ATTR_FILS_CACHE_ID]);
	} else {
		return -EINVAL;
	}
	if (info->attrs[NL80211_ATTR_PMK]) {
		pmksa.pmk = nla_data(info->attrs[NL80211_ATTR_PMK]);
		pmksa.pmk_len = nla_len(info->attrs[NL80211_ATTR_PMK]);
	}

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT &&
	    !(dev->ieee80211_ptr->iftype == NL80211_IFTYPE_AP &&
	      wiphy_ext_feature_isset(&rdev->wiphy,
				      NL80211_EXT_FEATURE_AP_PMKSA_CACHING)))
		return -EOPNOTSUPP;

	switch (info->genlhdr->cmd) {
	case NL80211_CMD_SET_PMKSA:
		rdev_ops = rdev->ops->set_pmksa;
		break;
	case NL80211_CMD_DEL_PMKSA:
		rdev_ops = rdev->ops->del_pmksa;
		break;
	default:
		WARN_ON(1);
		break;
	}

	if (!rdev_ops)
		return -EOPNOTSUPP;

	return rdev_ops(&rdev->wiphy, dev, &pmksa);
}

static int nl80211_flush_pmksa(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_STATION &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_CLIENT)
		return -EOPNOTSUPP;

	if (!rdev->ops->flush_pmksa)
		return -EOPNOTSUPP;

	return rdev_flush_pmksa(rdev, dev);
}

static int nl80211_tdls_mgmt(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	u8 action_code, dialog_token;
	u32 peer_capability = 0;
	u16 status_code;
	u8 *peer;
	bool initiator;

	if (!(rdev->wiphy.flags & WIPHY_FLAG_SUPPORTS_TDLS) ||
	    !rdev->ops->tdls_mgmt)
		return -EOPNOTSUPP;

	if (!info->attrs[NL80211_ATTR_TDLS_ACTION] ||
	    !info->attrs[NL80211_ATTR_STATUS_CODE] ||
	    !info->attrs[NL80211_ATTR_TDLS_DIALOG_TOKEN] ||
	    !info->attrs[NL80211_ATTR_IE] ||
	    !info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	peer = nla_data(info->attrs[NL80211_ATTR_MAC]);
	action_code = nla_get_u8(info->attrs[NL80211_ATTR_TDLS_ACTION]);
	status_code = nla_get_u16(info->attrs[NL80211_ATTR_STATUS_CODE]);
	dialog_token = nla_get_u8(info->attrs[NL80211_ATTR_TDLS_DIALOG_TOKEN]);
	initiator = nla_get_flag(info->attrs[NL80211_ATTR_TDLS_INITIATOR]);
	if (info->attrs[NL80211_ATTR_TDLS_PEER_CAPABILITY])
		peer_capability =
			nla_get_u32(info->attrs[NL80211_ATTR_TDLS_PEER_CAPABILITY]);

	return rdev_tdls_mgmt(rdev, dev, peer, action_code,
			      dialog_token, status_code, peer_capability,
			      initiator,
			      nla_data(info->attrs[NL80211_ATTR_IE]),
			      nla_len(info->attrs[NL80211_ATTR_IE]));
}

static int nl80211_tdls_oper(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	enum nl80211_tdls_operation operation;
	u8 *peer;

	if (!(rdev->wiphy.flags & WIPHY_FLAG_SUPPORTS_TDLS) ||
	    !rdev->ops->tdls_oper)
		return -EOPNOTSUPP;

	if (!info->attrs[NL80211_ATTR_TDLS_OPERATION] ||
	    !info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	operation = nla_get_u8(info->attrs[NL80211_ATTR_TDLS_OPERATION]);
	peer = nla_data(info->attrs[NL80211_ATTR_MAC]);

	return rdev_tdls_oper(rdev, dev, peer, operation);
}

static int nl80211_remain_on_channel(struct sk_buff *skb,
				     struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];
	struct cfg80211_chan_def chandef;
	const struct cfg80211_chan_def *compat_chandef;
	struct sk_buff *msg;
	void *hdr;
	u64 cookie;
	u32 duration;
	int err;

	if (!info->attrs[NL80211_ATTR_WIPHY_FREQ] ||
	    !info->attrs[NL80211_ATTR_DURATION])
		return -EINVAL;

	duration = nla_get_u32(info->attrs[NL80211_ATTR_DURATION]);

	if (!rdev->ops->remain_on_channel ||
	    !(rdev->wiphy.flags & WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL))
		return -EOPNOTSUPP;

	/*
	 * We should be on that channel for at least a minimum amount of
	 * time (10ms) but no longer than the driver supports.
	 */
	if (duration < NL80211_MIN_REMAIN_ON_CHANNEL_TIME ||
	    duration > rdev->wiphy.max_remain_on_channel_duration)
		return -EINVAL;

	err = nl80211_parse_chandef(rdev, info, &chandef);
	if (err)
		return err;

	wdev_lock(wdev);
	if (!cfg80211_off_channel_oper_allowed(wdev) &&
	    !cfg80211_chandef_identical(&wdev->chandef, &chandef)) {
		compat_chandef = cfg80211_chandef_compatible(&wdev->chandef,
							     &chandef);
		if (compat_chandef != &chandef) {
			wdev_unlock(wdev);
			return -EBUSY;
		}
	}
	wdev_unlock(wdev);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = nl80211hdr_put(msg, info->snd_portid, info->snd_seq, 0,
			     NL80211_CMD_REMAIN_ON_CHANNEL);
	if (!hdr) {
		err = -ENOBUFS;
		goto free_msg;
	}

	err = rdev_remain_on_channel(rdev, wdev, chandef.chan,
				     duration, &cookie);

	if (err)
		goto free_msg;

	if (nla_put_u64_64bit(msg, NL80211_ATTR_COOKIE, cookie,
			      NL80211_ATTR_PAD))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return genlmsg_reply(msg, info);

 nla_put_failure:
	err = -ENOBUFS;
 free_msg:
	nlmsg_free(msg);
	return err;
}

static int nl80211_cancel_remain_on_channel(struct sk_buff *skb,
					    struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];
	u64 cookie;

	if (!info->attrs[NL80211_ATTR_COOKIE])
		return -EINVAL;

	if (!rdev->ops->cancel_remain_on_channel)
		return -EOPNOTSUPP;

	cookie = nla_get_u64(info->attrs[NL80211_ATTR_COOKIE]);

	return rdev_cancel_remain_on_channel(rdev, wdev, cookie);
}

static int nl80211_set_tx_bitrate_mask(struct sk_buff *skb,
				       struct genl_info *info)
{
	struct cfg80211_bitrate_mask mask;
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	int err;

	if (!rdev->ops->set_bitrate_mask)
		return -EOPNOTSUPP;

	err = nl80211_parse_tx_bitrate_mask(info, &mask);
	if (err)
		return err;

	return rdev_set_bitrate_mask(rdev, dev, NULL, &mask);
}

static int nl80211_register_mgmt(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];
	u16 frame_type = IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ACTION;

	if (!info->attrs[NL80211_ATTR_FRAME_MATCH])
		return -EINVAL;

	if (info->attrs[NL80211_ATTR_FRAME_TYPE])
		frame_type = nla_get_u16(info->attrs[NL80211_ATTR_FRAME_TYPE]);

	switch (wdev->iftype) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_P2P_DEVICE:
		break;
	case NL80211_IFTYPE_NAN:
	default:
		return -EOPNOTSUPP;
	}

	/* not much point in registering if we can't reply */
	if (!rdev->ops->mgmt_tx)
		return -EOPNOTSUPP;

	return cfg80211_mlme_register_mgmt(wdev, info->snd_portid, frame_type,
			nla_data(info->attrs[NL80211_ATTR_FRAME_MATCH]),
			nla_len(info->attrs[NL80211_ATTR_FRAME_MATCH]));
}

static int nl80211_tx_mgmt(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];
	struct cfg80211_chan_def chandef;
	int err;
	void *hdr = NULL;
	u64 cookie;
	struct sk_buff *msg = NULL;
	struct cfg80211_mgmt_tx_params params = {
		.dont_wait_for_ack =
			info->attrs[NL80211_ATTR_DONT_WAIT_FOR_ACK],
	};

	if (!info->attrs[NL80211_ATTR_FRAME])
		return -EINVAL;

	if (!rdev->ops->mgmt_tx)
		return -EOPNOTSUPP;

	switch (wdev->iftype) {
	case NL80211_IFTYPE_P2P_DEVICE:
		if (!info->attrs[NL80211_ATTR_WIPHY_FREQ])
			return -EINVAL;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_P2P_GO:
		break;
	case NL80211_IFTYPE_NAN:
	default:
		return -EOPNOTSUPP;
	}

	if (info->attrs[NL80211_ATTR_DURATION]) {
		if (!(rdev->wiphy.flags & WIPHY_FLAG_OFFCHAN_TX))
			return -EINVAL;
		params.wait = nla_get_u32(info->attrs[NL80211_ATTR_DURATION]);

		/*
		 * We should wait on the channel for at least a minimum amount
		 * of time (10ms) but no longer than the driver supports.
		 */
		if (params.wait < NL80211_MIN_REMAIN_ON_CHANNEL_TIME ||
		    params.wait > rdev->wiphy.max_remain_on_channel_duration)
			return -EINVAL;
	}

	params.offchan = info->attrs[NL80211_ATTR_OFFCHANNEL_TX_OK];

	if (params.offchan && !(rdev->wiphy.flags & WIPHY_FLAG_OFFCHAN_TX))
		return -EINVAL;

	params.no_cck = nla_get_flag(info->attrs[NL80211_ATTR_TX_NO_CCK_RATE]);

	/* get the channel if any has been specified, otherwise pass NULL to
	 * the driver. The latter will use the current one
	 */
	chandef.chan = NULL;
	if (info->attrs[NL80211_ATTR_WIPHY_FREQ]) {
		err = nl80211_parse_chandef(rdev, info, &chandef);
		if (err)
			return err;
	}

	if (!chandef.chan && params.offchan)
		return -EINVAL;

	wdev_lock(wdev);
	if (params.offchan && !cfg80211_off_channel_oper_allowed(wdev)) {
		wdev_unlock(wdev);
		return -EBUSY;
	}
	wdev_unlock(wdev);

	params.buf = nla_data(info->attrs[NL80211_ATTR_FRAME]);
	params.len = nla_len(info->attrs[NL80211_ATTR_FRAME]);

	if (info->attrs[NL80211_ATTR_CSA_C_OFFSETS_TX]) {
		int len = nla_len(info->attrs[NL80211_ATTR_CSA_C_OFFSETS_TX]);
		int i;

		if (len % sizeof(u16))
			return -EINVAL;

		params.n_csa_offsets = len / sizeof(u16);
		params.csa_offsets =
			nla_data(info->attrs[NL80211_ATTR_CSA_C_OFFSETS_TX]);

		/* check that all the offsets fit the frame */
		for (i = 0; i < params.n_csa_offsets; i++) {
			if (params.csa_offsets[i] >= params.len)
				return -EINVAL;
		}
	}

	if (!params.dont_wait_for_ack) {
		msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
		if (!msg)
			return -ENOMEM;

		hdr = nl80211hdr_put(msg, info->snd_portid, info->snd_seq, 0,
				     NL80211_CMD_FRAME);
		if (!hdr) {
			err = -ENOBUFS;
			goto free_msg;
		}
	}

	params.chan = chandef.chan;
	err = cfg80211_mlme_mgmt_tx(rdev, wdev, &params, &cookie);
	if (err)
		goto free_msg;

	if (msg) {
		if (nla_put_u64_64bit(msg, NL80211_ATTR_COOKIE, cookie,
				      NL80211_ATTR_PAD))
			goto nla_put_failure;

		genlmsg_end(msg, hdr);
		return genlmsg_reply(msg, info);
	}

	return 0;

 nla_put_failure:
	err = -ENOBUFS;
 free_msg:
	nlmsg_free(msg);
	return err;
}

static int nl80211_tx_mgmt_cancel_wait(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];
	u64 cookie;

	if (!info->attrs[NL80211_ATTR_COOKIE])
		return -EINVAL;

	if (!rdev->ops->mgmt_tx_cancel_wait)
		return -EOPNOTSUPP;

	switch (wdev->iftype) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_P2P_DEVICE:
		break;
	case NL80211_IFTYPE_NAN:
	default:
		return -EOPNOTSUPP;
	}

	cookie = nla_get_u64(info->attrs[NL80211_ATTR_COOKIE]);

	return rdev_mgmt_tx_cancel_wait(rdev, wdev, cookie);
}

static int nl80211_set_power_save(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev;
	struct net_device *dev = info->user_ptr[1];
	u8 ps_state;
	bool state;
	int err;

	if (!info->attrs[NL80211_ATTR_PS_STATE])
		return -EINVAL;

	ps_state = nla_get_u32(info->attrs[NL80211_ATTR_PS_STATE]);

	wdev = dev->ieee80211_ptr;

	if (!rdev->ops->set_power_mgmt)
		return -EOPNOTSUPP;

	state = (ps_state == NL80211_PS_ENABLED) ? true : false;

	if (state == wdev->ps)
		return 0;

	err = rdev_set_power_mgmt(rdev, dev, state, wdev->ps_timeout);
	if (!err)
		wdev->ps = state;
	return err;
}

static int nl80211_get_power_save(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	enum nl80211_ps_state ps_state;
	struct wireless_dev *wdev;
	struct net_device *dev = info->user_ptr[1];
	struct sk_buff *msg;
	void *hdr;
	int err;

	wdev = dev->ieee80211_ptr;

	if (!rdev->ops->set_power_mgmt)
		return -EOPNOTSUPP;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = nl80211hdr_put(msg, info->snd_portid, info->snd_seq, 0,
			     NL80211_CMD_GET_POWER_SAVE);
	if (!hdr) {
		err = -ENOBUFS;
		goto free_msg;
	}

	if (wdev->ps)
		ps_state = NL80211_PS_ENABLED;
	else
		ps_state = NL80211_PS_DISABLED;

	if (nla_put_u32(msg, NL80211_ATTR_PS_STATE, ps_state))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return genlmsg_reply(msg, info);

 nla_put_failure:
	err = -ENOBUFS;
 free_msg:
	nlmsg_free(msg);
	return err;
}

static const struct nla_policy
nl80211_attr_cqm_policy[NL80211_ATTR_CQM_MAX + 1] = {
	[NL80211_ATTR_CQM_RSSI_THOLD] = { .type = NLA_BINARY },
	[NL80211_ATTR_CQM_RSSI_HYST] = { .type = NLA_U32 },
	[NL80211_ATTR_CQM_RSSI_THRESHOLD_EVENT] = { .type = NLA_U32 },
	[NL80211_ATTR_CQM_TXE_RATE] = { .type = NLA_U32 },
	[NL80211_ATTR_CQM_TXE_PKTS] = { .type = NLA_U32 },
	[NL80211_ATTR_CQM_TXE_INTVL] = { .type = NLA_U32 },
	[NL80211_ATTR_CQM_RSSI_LEVEL] = { .type = NLA_S32 },
};

static int nl80211_set_cqm_txe(struct genl_info *info,
			       u32 rate, u32 pkts, u32 intvl)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	if (rate > 100 || intvl > NL80211_CQM_TXE_MAX_INTVL)
		return -EINVAL;

	if (!rdev->ops->set_cqm_txe_config)
		return -EOPNOTSUPP;

	if (wdev->iftype != NL80211_IFTYPE_STATION &&
	    wdev->iftype != NL80211_IFTYPE_P2P_CLIENT)
		return -EOPNOTSUPP;

	return rdev_set_cqm_txe_config(rdev, dev, rate, pkts, intvl);
}

static int cfg80211_cqm_rssi_update(struct cfg80211_registered_device *rdev,
				    struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	s32 last, low, high;
	u32 hyst;
	int i, n, low_index;
	int err;

	/* RSSI reporting disabled? */
	if (!wdev->cqm_config)
		return rdev_set_cqm_rssi_range_config(rdev, dev, 0, 0);

	/*
	 * Obtain current RSSI value if possible, if not and no RSSI threshold
	 * event has been received yet, we should receive an event after a
	 * connection is established and enough beacons received to calculate
	 * the average.
	 */
	if (!wdev->cqm_config->last_rssi_event_value && wdev->current_bss &&
	    rdev->ops->get_station) {
		struct station_info sinfo = {};
		u8 *mac_addr;

		mac_addr = wdev->current_bss->pub.bssid;

		err = rdev_get_station(rdev, dev, mac_addr, &sinfo);
		if (err)
			return err;

		if (sinfo.filled & BIT_ULL(NL80211_STA_INFO_BEACON_SIGNAL_AVG))
			wdev->cqm_config->last_rssi_event_value =
				(s8) sinfo.rx_beacon_signal_avg;
	}

	last = wdev->cqm_config->last_rssi_event_value;
	hyst = wdev->cqm_config->rssi_hyst;
	n = wdev->cqm_config->n_rssi_thresholds;

	for (i = 0; i < n; i++)
		if (last < wdev->cqm_config->rssi_thresholds[i])
			break;

	low_index = i - 1;
	if (low_index >= 0) {
		low_index = array_index_nospec(low_index, n);
		low = wdev->cqm_config->rssi_thresholds[low_index] - hyst;
	} else {
		low = S32_MIN;
	}
	if (i < n) {
		i = array_index_nospec(i, n);
		high = wdev->cqm_config->rssi_thresholds[i] + hyst - 1;
	} else {
		high = S32_MAX;
	}

	return rdev_set_cqm_rssi_range_config(rdev, dev, low, high);
}

static int nl80211_set_cqm_rssi(struct genl_info *info,
				const s32 *thresholds, int n_thresholds,
				u32 hysteresis)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int i, err;
	s32 prev = S32_MIN;

	/* Check all values negative and sorted */
	for (i = 0; i < n_thresholds; i++) {
		if (thresholds[i] > 0 || thresholds[i] <= prev)
			return -EINVAL;

		prev = thresholds[i];
	}

	if (wdev->iftype != NL80211_IFTYPE_STATION &&
	    wdev->iftype != NL80211_IFTYPE_P2P_CLIENT)
		return -EOPNOTSUPP;

	wdev_lock(wdev);
	cfg80211_cqm_config_free(wdev);
	wdev_unlock(wdev);

	if (n_thresholds <= 1 && rdev->ops->set_cqm_rssi_config) {
		if (n_thresholds == 0 || thresholds[0] == 0) /* Disabling */
			return rdev_set_cqm_rssi_config(rdev, dev, 0, 0);

		return rdev_set_cqm_rssi_config(rdev, dev,
						thresholds[0], hysteresis);
	}

	if (!wiphy_ext_feature_isset(&rdev->wiphy,
				     NL80211_EXT_FEATURE_CQM_RSSI_LIST))
		return -EOPNOTSUPP;

	if (n_thresholds == 1 && thresholds[0] == 0) /* Disabling */
		n_thresholds = 0;

	wdev_lock(wdev);
	if (n_thresholds) {
		struct cfg80211_cqm_config *cqm_config;

		cqm_config = kzalloc(sizeof(struct cfg80211_cqm_config) +
				     n_thresholds * sizeof(s32), GFP_KERNEL);
		if (!cqm_config) {
			err = -ENOMEM;
			goto unlock;
		}

		cqm_config->rssi_hyst = hysteresis;
		cqm_config->n_rssi_thresholds = n_thresholds;
		memcpy(cqm_config->rssi_thresholds, thresholds,
		       n_thresholds * sizeof(s32));

		wdev->cqm_config = cqm_config;
	}

	err = cfg80211_cqm_rssi_update(rdev, dev);

unlock:
	wdev_unlock(wdev);

	return err;
}

static int nl80211_set_cqm(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attrs[NL80211_ATTR_CQM_MAX + 1];
	struct nlattr *cqm;
	int err;

	cqm = info->attrs[NL80211_ATTR_CQM];
	if (!cqm)
		return -EINVAL;

	err = nla_parse_nested_deprecated(attrs, NL80211_ATTR_CQM_MAX, cqm,
					  nl80211_attr_cqm_policy,
					  info->extack);
	if (err)
		return err;

	if (attrs[NL80211_ATTR_CQM_RSSI_THOLD] &&
	    attrs[NL80211_ATTR_CQM_RSSI_HYST]) {
		const s32 *thresholds =
			nla_data(attrs[NL80211_ATTR_CQM_RSSI_THOLD]);
		int len = nla_len(attrs[NL80211_ATTR_CQM_RSSI_THOLD]);
		u32 hysteresis = nla_get_u32(attrs[NL80211_ATTR_CQM_RSSI_HYST]);

		if (len % 4)
			return -EINVAL;

		return nl80211_set_cqm_rssi(info, thresholds, len / 4,
					    hysteresis);
	}

	if (attrs[NL80211_ATTR_CQM_TXE_RATE] &&
	    attrs[NL80211_ATTR_CQM_TXE_PKTS] &&
	    attrs[NL80211_ATTR_CQM_TXE_INTVL]) {
		u32 rate = nla_get_u32(attrs[NL80211_ATTR_CQM_TXE_RATE]);
		u32 pkts = nla_get_u32(attrs[NL80211_ATTR_CQM_TXE_PKTS]);
		u32 intvl = nla_get_u32(attrs[NL80211_ATTR_CQM_TXE_INTVL]);

		return nl80211_set_cqm_txe(info, rate, pkts, intvl);
	}

	return -EINVAL;
}

static int nl80211_join_ocb(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct ocb_setup setup = {};
	int err;

	err = nl80211_parse_chandef(rdev, info, &setup.chandef);
	if (err)
		return err;

	return cfg80211_join_ocb(rdev, dev, &setup);
}

static int nl80211_leave_ocb(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];

	return cfg80211_leave_ocb(rdev, dev);
}

static int nl80211_join_mesh(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct mesh_config cfg;
	struct mesh_setup setup;
	int err;

	/* start with default */
	memcpy(&cfg, &default_mesh_config, sizeof(cfg));
	memcpy(&setup, &default_mesh_setup, sizeof(setup));

	if (info->attrs[NL80211_ATTR_MESH_CONFIG]) {
		/* and parse parameters if given */
		err = nl80211_parse_mesh_config(info, &cfg, NULL);
		if (err)
			return err;
	}

	if (!info->attrs[NL80211_ATTR_MESH_ID] ||
	    !nla_len(info->attrs[NL80211_ATTR_MESH_ID]))
		return -EINVAL;

	setup.mesh_id = nla_data(info->attrs[NL80211_ATTR_MESH_ID]);
	setup.mesh_id_len = nla_len(info->attrs[NL80211_ATTR_MESH_ID]);

	if (info->attrs[NL80211_ATTR_MCAST_RATE] &&
	    !nl80211_parse_mcast_rate(rdev, setup.mcast_rate,
			    nla_get_u32(info->attrs[NL80211_ATTR_MCAST_RATE])))
			return -EINVAL;

	if (info->attrs[NL80211_ATTR_BEACON_INTERVAL]) {
		setup.beacon_interval =
			nla_get_u32(info->attrs[NL80211_ATTR_BEACON_INTERVAL]);

		err = cfg80211_validate_beacon_int(rdev,
						   NL80211_IFTYPE_MESH_POINT,
						   setup.beacon_interval);
		if (err)
			return err;
	}

	if (info->attrs[NL80211_ATTR_DTIM_PERIOD]) {
		setup.dtim_period =
			nla_get_u32(info->attrs[NL80211_ATTR_DTIM_PERIOD]);
		if (setup.dtim_period < 1 || setup.dtim_period > 100)
			return -EINVAL;
	}

	if (info->attrs[NL80211_ATTR_MESH_SETUP]) {
		/* parse additional setup parameters if given */
		err = nl80211_parse_mesh_setup(info, &setup);
		if (err)
			return err;
	}

	if (setup.user_mpm)
		cfg.auto_open_plinks = false;

	if (info->attrs[NL80211_ATTR_WIPHY_FREQ]) {
		err = nl80211_parse_chandef(rdev, info, &setup.chandef);
		if (err)
			return err;
	} else {
		/* __cfg80211_join_mesh() will sort it out */
		setup.chandef.chan = NULL;
	}

	if (info->attrs[NL80211_ATTR_BSS_BASIC_RATES]) {
		u8 *rates = nla_data(info->attrs[NL80211_ATTR_BSS_BASIC_RATES]);
		int n_rates =
			nla_len(info->attrs[NL80211_ATTR_BSS_BASIC_RATES]);
		struct ieee80211_supported_band *sband;

		if (!setup.chandef.chan)
			return -EINVAL;

		sband = rdev->wiphy.bands[setup.chandef.chan->band];

		err = ieee80211_get_ratemask(sband, rates, n_rates,
					     &setup.basic_rates);
		if (err)
			return err;
	}

	if (info->attrs[NL80211_ATTR_TX_RATES]) {
		err = nl80211_parse_tx_bitrate_mask(info, &setup.beacon_rate);
		if (err)
			return err;

		if (!setup.chandef.chan)
			return -EINVAL;

		err = validate_beacon_tx_rate(rdev, setup.chandef.chan->band,
					      &setup.beacon_rate);
		if (err)
			return err;
	}

	setup.userspace_handles_dfs =
		nla_get_flag(info->attrs[NL80211_ATTR_HANDLE_DFS]);

	if (info->attrs[NL80211_ATTR_CONTROL_PORT_OVER_NL80211]) {
		int r = validate_pae_over_nl80211(rdev, info);

		if (r < 0)
			return r;

		setup.control_port_over_nl80211 = true;
	}

	wdev_lock(dev->ieee80211_ptr);
	err = __cfg80211_join_mesh(rdev, dev, &setup, &cfg);
	if (!err && info->attrs[NL80211_ATTR_SOCKET_OWNER])
		dev->ieee80211_ptr->conn_owner_nlportid = info->snd_portid;
	wdev_unlock(dev->ieee80211_ptr);

	return err;
}

static int nl80211_leave_mesh(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];

	return cfg80211_leave_mesh(rdev, dev);
}

#ifdef CONFIG_PM
static int nl80211_send_wowlan_patterns(struct sk_buff *msg,
					struct cfg80211_registered_device *rdev)
{
	struct cfg80211_wowlan *wowlan = rdev->wiphy.wowlan_config;
	struct nlattr *nl_pats, *nl_pat;
	int i, pat_len;

	if (!wowlan->n_patterns)
		return 0;

	nl_pats = nla_nest_start_noflag(msg, NL80211_WOWLAN_TRIG_PKT_PATTERN);
	if (!nl_pats)
		return -ENOBUFS;

	for (i = 0; i < wowlan->n_patterns; i++) {
		nl_pat = nla_nest_start_noflag(msg, i + 1);
		if (!nl_pat)
			return -ENOBUFS;
		pat_len = wowlan->patterns[i].pattern_len;
		if (nla_put(msg, NL80211_PKTPAT_MASK, DIV_ROUND_UP(pat_len, 8),
			    wowlan->patterns[i].mask) ||
		    nla_put(msg, NL80211_PKTPAT_PATTERN, pat_len,
			    wowlan->patterns[i].pattern) ||
		    nla_put_u32(msg, NL80211_PKTPAT_OFFSET,
				wowlan->patterns[i].pkt_offset))
			return -ENOBUFS;
		nla_nest_end(msg, nl_pat);
	}
	nla_nest_end(msg, nl_pats);

	return 0;
}

static int nl80211_send_wowlan_tcp(struct sk_buff *msg,
				   struct cfg80211_wowlan_tcp *tcp)
{
	struct nlattr *nl_tcp;

	if (!tcp)
		return 0;

	nl_tcp = nla_nest_start_noflag(msg,
				       NL80211_WOWLAN_TRIG_TCP_CONNECTION);
	if (!nl_tcp)
		return -ENOBUFS;

	if (nla_put_in_addr(msg, NL80211_WOWLAN_TCP_SRC_IPV4, tcp->src) ||
	    nla_put_in_addr(msg, NL80211_WOWLAN_TCP_DST_IPV4, tcp->dst) ||
	    nla_put(msg, NL80211_WOWLAN_TCP_DST_MAC, ETH_ALEN, tcp->dst_mac) ||
	    nla_put_u16(msg, NL80211_WOWLAN_TCP_SRC_PORT, tcp->src_port) ||
	    nla_put_u16(msg, NL80211_WOWLAN_TCP_DST_PORT, tcp->dst_port) ||
	    nla_put(msg, NL80211_WOWLAN_TCP_DATA_PAYLOAD,
		    tcp->payload_len, tcp->payload) ||
	    nla_put_u32(msg, NL80211_WOWLAN_TCP_DATA_INTERVAL,
			tcp->data_interval) ||
	    nla_put(msg, NL80211_WOWLAN_TCP_WAKE_PAYLOAD,
		    tcp->wake_len, tcp->wake_data) ||
	    nla_put(msg, NL80211_WOWLAN_TCP_WAKE_MASK,
		    DIV_ROUND_UP(tcp->wake_len, 8), tcp->wake_mask))
		return -ENOBUFS;

	if (tcp->payload_seq.len &&
	    nla_put(msg, NL80211_WOWLAN_TCP_DATA_PAYLOAD_SEQ,
		    sizeof(tcp->payload_seq), &tcp->payload_seq))
		return -ENOBUFS;

	if (tcp->payload_tok.len &&
	    nla_put(msg, NL80211_WOWLAN_TCP_DATA_PAYLOAD_TOKEN,
		    sizeof(tcp->payload_tok) + tcp->tokens_size,
		    &tcp->payload_tok))
		return -ENOBUFS;

	nla_nest_end(msg, nl_tcp);

	return 0;
}

static int nl80211_send_wowlan_nd(struct sk_buff *msg,
				  struct cfg80211_sched_scan_request *req)
{
	struct nlattr *nd, *freqs, *matches, *match, *scan_plans, *scan_plan;
	int i;

	if (!req)
		return 0;

	nd = nla_nest_start_noflag(msg, NL80211_WOWLAN_TRIG_NET_DETECT);
	if (!nd)
		return -ENOBUFS;

	if (req->n_scan_plans == 1 &&
	    nla_put_u32(msg, NL80211_ATTR_SCHED_SCAN_INTERVAL,
			req->scan_plans[0].interval * 1000))
		return -ENOBUFS;

	if (nla_put_u32(msg, NL80211_ATTR_SCHED_SCAN_DELAY, req->delay))
		return -ENOBUFS;

	if (req->relative_rssi_set) {
		struct nl80211_bss_select_rssi_adjust rssi_adjust;

		if (nla_put_s8(msg, NL80211_ATTR_SCHED_SCAN_RELATIVE_RSSI,
			       req->relative_rssi))
			return -ENOBUFS;

		rssi_adjust.band = req->rssi_adjust.band;
		rssi_adjust.delta = req->rssi_adjust.delta;
		if (nla_put(msg, NL80211_ATTR_SCHED_SCAN_RSSI_ADJUST,
			    sizeof(rssi_adjust), &rssi_adjust))
			return -ENOBUFS;
	}

	freqs = nla_nest_start_noflag(msg, NL80211_ATTR_SCAN_FREQUENCIES);
	if (!freqs)
		return -ENOBUFS;

	for (i = 0; i < req->n_channels; i++) {
		if (nla_put_u32(msg, i, req->channels[i]->center_freq))
			return -ENOBUFS;
	}

	nla_nest_end(msg, freqs);

	if (req->n_match_sets) {
		matches = nla_nest_start_noflag(msg,
						NL80211_ATTR_SCHED_SCAN_MATCH);
		if (!matches)
			return -ENOBUFS;

		for (i = 0; i < req->n_match_sets; i++) {
			match = nla_nest_start_noflag(msg, i);
			if (!match)
				return -ENOBUFS;

			if (nla_put(msg, NL80211_SCHED_SCAN_MATCH_ATTR_SSID,
				    req->match_sets[i].ssid.ssid_len,
				    req->match_sets[i].ssid.ssid))
				return -ENOBUFS;
			nla_nest_end(msg, match);
		}
		nla_nest_end(msg, matches);
	}

	scan_plans = nla_nest_start_noflag(msg, NL80211_ATTR_SCHED_SCAN_PLANS);
	if (!scan_plans)
		return -ENOBUFS;

	for (i = 0; i < req->n_scan_plans; i++) {
		scan_plan = nla_nest_start_noflag(msg, i + 1);
		if (!scan_plan)
			return -ENOBUFS;

		if (nla_put_u32(msg, NL80211_SCHED_SCAN_PLAN_INTERVAL,
				req->scan_plans[i].interval) ||
		    (req->scan_plans[i].iterations &&
		     nla_put_u32(msg, NL80211_SCHED_SCAN_PLAN_ITERATIONS,
				 req->scan_plans[i].iterations)))
			return -ENOBUFS;
		nla_nest_end(msg, scan_plan);
	}
	nla_nest_end(msg, scan_plans);

	nla_nest_end(msg, nd);

	return 0;
}

static int nl80211_get_wowlan(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct sk_buff *msg;
	void *hdr;
	u32 size = NLMSG_DEFAULT_SIZE;

	if (!rdev->wiphy.wowlan)
		return -EOPNOTSUPP;

	if (rdev->wiphy.wowlan_config && rdev->wiphy.wowlan_config->tcp) {
		/* adjust size to have room for all the data */
		size += rdev->wiphy.wowlan_config->tcp->tokens_size +
			rdev->wiphy.wowlan_config->tcp->payload_len +
			rdev->wiphy.wowlan_config->tcp->wake_len +
			rdev->wiphy.wowlan_config->tcp->wake_len / 8;
	}

	msg = nlmsg_new(size, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = nl80211hdr_put(msg, info->snd_portid, info->snd_seq, 0,
			     NL80211_CMD_GET_WOWLAN);
	if (!hdr)
		goto nla_put_failure;

	if (rdev->wiphy.wowlan_config) {
		struct nlattr *nl_wowlan;

		nl_wowlan = nla_nest_start_noflag(msg,
						  NL80211_ATTR_WOWLAN_TRIGGERS);
		if (!nl_wowlan)
			goto nla_put_failure;

		if ((rdev->wiphy.wowlan_config->any &&
		     nla_put_flag(msg, NL80211_WOWLAN_TRIG_ANY)) ||
		    (rdev->wiphy.wowlan_config->disconnect &&
		     nla_put_flag(msg, NL80211_WOWLAN_TRIG_DISCONNECT)) ||
		    (rdev->wiphy.wowlan_config->magic_pkt &&
		     nla_put_flag(msg, NL80211_WOWLAN_TRIG_MAGIC_PKT)) ||
		    (rdev->wiphy.wowlan_config->gtk_rekey_failure &&
		     nla_put_flag(msg, NL80211_WOWLAN_TRIG_GTK_REKEY_FAILURE)) ||
		    (rdev->wiphy.wowlan_config->eap_identity_req &&
		     nla_put_flag(msg, NL80211_WOWLAN_TRIG_EAP_IDENT_REQUEST)) ||
		    (rdev->wiphy.wowlan_config->four_way_handshake &&
		     nla_put_flag(msg, NL80211_WOWLAN_TRIG_4WAY_HANDSHAKE)) ||
		    (rdev->wiphy.wowlan_config->rfkill_release &&
		     nla_put_flag(msg, NL80211_WOWLAN_TRIG_RFKILL_RELEASE)))
			goto nla_put_failure;

		if (nl80211_send_wowlan_patterns(msg, rdev))
			goto nla_put_failure;

		if (nl80211_send_wowlan_tcp(msg,
					    rdev->wiphy.wowlan_config->tcp))
			goto nla_put_failure;

		if (nl80211_send_wowlan_nd(
			    msg,
			    rdev->wiphy.wowlan_config->nd_config))
			goto nla_put_failure;

		nla_nest_end(msg, nl_wowlan);
	}

	genlmsg_end(msg, hdr);
	return genlmsg_reply(msg, info);

nla_put_failure:
	nlmsg_free(msg);
	return -ENOBUFS;
}

static int nl80211_parse_wowlan_tcp(struct cfg80211_registered_device *rdev,
				    struct nlattr *attr,
				    struct cfg80211_wowlan *trig)
{
	struct nlattr *tb[NUM_NL80211_WOWLAN_TCP];
	struct cfg80211_wowlan_tcp *cfg;
	struct nl80211_wowlan_tcp_data_token *tok = NULL;
	struct nl80211_wowlan_tcp_data_seq *seq = NULL;
	u32 size;
	u32 data_size, wake_size, tokens_size = 0, wake_mask_size;
	int err, port;

	if (!rdev->wiphy.wowlan->tcp)
		return -EINVAL;

	err = nla_parse_nested_deprecated(tb, MAX_NL80211_WOWLAN_TCP, attr,
					  nl80211_wowlan_tcp_policy, NULL);
	if (err)
		return err;

	if (!tb[NL80211_WOWLAN_TCP_SRC_IPV4] ||
	    !tb[NL80211_WOWLAN_TCP_DST_IPV4] ||
	    !tb[NL80211_WOWLAN_TCP_DST_MAC] ||
	    !tb[NL80211_WOWLAN_TCP_DST_PORT] ||
	    !tb[NL80211_WOWLAN_TCP_DATA_PAYLOAD] ||
	    !tb[NL80211_WOWLAN_TCP_DATA_INTERVAL] ||
	    !tb[NL80211_WOWLAN_TCP_WAKE_PAYLOAD] ||
	    !tb[NL80211_WOWLAN_TCP_WAKE_MASK])
		return -EINVAL;

	data_size = nla_len(tb[NL80211_WOWLAN_TCP_DATA_PAYLOAD]);
	if (data_size > rdev->wiphy.wowlan->tcp->data_payload_max)
		return -EINVAL;

	if (nla_get_u32(tb[NL80211_WOWLAN_TCP_DATA_INTERVAL]) >
			rdev->wiphy.wowlan->tcp->data_interval_max ||
	    nla_get_u32(tb[NL80211_WOWLAN_TCP_DATA_INTERVAL]) == 0)
		return -EINVAL;

	wake_size = nla_len(tb[NL80211_WOWLAN_TCP_WAKE_PAYLOAD]);
	if (wake_size > rdev->wiphy.wowlan->tcp->wake_payload_max)
		return -EINVAL;

	wake_mask_size = nla_len(tb[NL80211_WOWLAN_TCP_WAKE_MASK]);
	if (wake_mask_size != DIV_ROUND_UP(wake_size, 8))
		return -EINVAL;

	if (tb[NL80211_WOWLAN_TCP_DATA_PAYLOAD_TOKEN]) {
		u32 tokln = nla_len(tb[NL80211_WOWLAN_TCP_DATA_PAYLOAD_TOKEN]);

		tok = nla_data(tb[NL80211_WOWLAN_TCP_DATA_PAYLOAD_TOKEN]);
		tokens_size = tokln - sizeof(*tok);

		if (!tok->len || tokens_size % tok->len)
			return -EINVAL;
		if (!rdev->wiphy.wowlan->tcp->tok)
			return -EINVAL;
		if (tok->len > rdev->wiphy.wowlan->tcp->tok->max_len)
			return -EINVAL;
		if (tok->len < rdev->wiphy.wowlan->tcp->tok->min_len)
			return -EINVAL;
		if (tokens_size > rdev->wiphy.wowlan->tcp->tok->bufsize)
			return -EINVAL;
		if (tok->offset + tok->len > data_size)
			return -EINVAL;
	}

	if (tb[NL80211_WOWLAN_TCP_DATA_PAYLOAD_SEQ]) {
		seq = nla_data(tb[NL80211_WOWLAN_TCP_DATA_PAYLOAD_SEQ]);
		if (!rdev->wiphy.wowlan->tcp->seq)
			return -EINVAL;
		if (seq->len == 0 || seq->len > 4)
			return -EINVAL;
		if (seq->len + seq->offset > data_size)
			return -EINVAL;
	}

	size = sizeof(*cfg);
	size += data_size;
	size += wake_size + wake_mask_size;
	size += tokens_size;

	cfg = kzalloc(size, GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;
	cfg->src = nla_get_in_addr(tb[NL80211_WOWLAN_TCP_SRC_IPV4]);
	cfg->dst = nla_get_in_addr(tb[NL80211_WOWLAN_TCP_DST_IPV4]);
	memcpy(cfg->dst_mac, nla_data(tb[NL80211_WOWLAN_TCP_DST_MAC]),
	       ETH_ALEN);
	if (tb[NL80211_WOWLAN_TCP_SRC_PORT])
		port = nla_get_u16(tb[NL80211_WOWLAN_TCP_SRC_PORT]);
	else
		port = 0;
#ifdef CONFIG_INET
	/* allocate a socket and port for it and use it */
	err = __sock_create(wiphy_net(&rdev->wiphy), PF_INET, SOCK_STREAM,
			    IPPROTO_TCP, &cfg->sock, 1);
	if (err) {
		kfree(cfg);
		return err;
	}
	if (inet_csk_get_port(cfg->sock->sk, port)) {
		sock_release(cfg->sock);
		kfree(cfg);
		return -EADDRINUSE;
	}
	cfg->src_port = inet_sk(cfg->sock->sk)->inet_num;
#else
	if (!port) {
		kfree(cfg);
		return -EINVAL;
	}
	cfg->src_port = port;
#endif

	cfg->dst_port = nla_get_u16(tb[NL80211_WOWLAN_TCP_DST_PORT]);
	cfg->payload_len = data_size;
	cfg->payload = (u8 *)cfg + sizeof(*cfg) + tokens_size;
	memcpy((void *)cfg->payload,
	       nla_data(tb[NL80211_WOWLAN_TCP_DATA_PAYLOAD]),
	       data_size);
	if (seq)
		cfg->payload_seq = *seq;
	cfg->data_interval = nla_get_u32(tb[NL80211_WOWLAN_TCP_DATA_INTERVAL]);
	cfg->wake_len = wake_size;
	cfg->wake_data = (u8 *)cfg + sizeof(*cfg) + tokens_size + data_size;
	memcpy((void *)cfg->wake_data,
	       nla_data(tb[NL80211_WOWLAN_TCP_WAKE_PAYLOAD]),
	       wake_size);
	cfg->wake_mask = (u8 *)cfg + sizeof(*cfg) + tokens_size +
			 data_size + wake_size;
	memcpy((void *)cfg->wake_mask,
	       nla_data(tb[NL80211_WOWLAN_TCP_WAKE_MASK]),
	       wake_mask_size);
	if (tok) {
		cfg->tokens_size = tokens_size;
		memcpy(&cfg->payload_tok, tok, sizeof(*tok) + tokens_size);
	}

	trig->tcp = cfg;

	return 0;
}

static int nl80211_parse_wowlan_nd(struct cfg80211_registered_device *rdev,
				   const struct wiphy_wowlan_support *wowlan,
				   struct nlattr *attr,
				   struct cfg80211_wowlan *trig)
{
	struct nlattr **tb;
	int err;

	tb = kcalloc(NUM_NL80211_ATTR, sizeof(*tb), GFP_KERNEL);
	if (!tb)
		return -ENOMEM;

	if (!(wowlan->flags & WIPHY_WOWLAN_NET_DETECT)) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = nla_parse_nested_deprecated(tb, NL80211_ATTR_MAX, attr,
					  nl80211_policy, NULL);
	if (err)
		goto out;

	trig->nd_config = nl80211_parse_sched_scan(&rdev->wiphy, NULL, tb,
						   wowlan->max_nd_match_sets);
	err = PTR_ERR_OR_ZERO(trig->nd_config);
	if (err)
		trig->nd_config = NULL;

out:
	kfree(tb);
	return err;
}

static int nl80211_set_wowlan(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct nlattr *tb[NUM_NL80211_WOWLAN_TRIG];
	struct cfg80211_wowlan new_triggers = {};
	struct cfg80211_wowlan *ntrig;
	const struct wiphy_wowlan_support *wowlan = rdev->wiphy.wowlan;
	int err, i;
	bool prev_enabled = rdev->wiphy.wowlan_config;
	bool regular = false;

	if (!wowlan)
		return -EOPNOTSUPP;

	if (!info->attrs[NL80211_ATTR_WOWLAN_TRIGGERS]) {
		cfg80211_rdev_free_wowlan(rdev);
		rdev->wiphy.wowlan_config = NULL;
		goto set_wakeup;
	}

	err = nla_parse_nested_deprecated(tb, MAX_NL80211_WOWLAN_TRIG,
					  info->attrs[NL80211_ATTR_WOWLAN_TRIGGERS],
					  nl80211_wowlan_policy, info->extack);
	if (err)
		return err;

	if (tb[NL80211_WOWLAN_TRIG_ANY]) {
		if (!(wowlan->flags & WIPHY_WOWLAN_ANY))
			return -EINVAL;
		new_triggers.any = true;
	}

	if (tb[NL80211_WOWLAN_TRIG_DISCONNECT]) {
		if (!(wowlan->flags & WIPHY_WOWLAN_DISCONNECT))
			return -EINVAL;
		new_triggers.disconnect = true;
		regular = true;
	}

	if (tb[NL80211_WOWLAN_TRIG_MAGIC_PKT]) {
		if (!(wowlan->flags & WIPHY_WOWLAN_MAGIC_PKT))
			return -EINVAL;
		new_triggers.magic_pkt = true;
		regular = true;
	}

	if (tb[NL80211_WOWLAN_TRIG_GTK_REKEY_SUPPORTED])
		return -EINVAL;

	if (tb[NL80211_WOWLAN_TRIG_GTK_REKEY_FAILURE]) {
		if (!(wowlan->flags & WIPHY_WOWLAN_GTK_REKEY_FAILURE))
			return -EINVAL;
		new_triggers.gtk_rekey_failure = true;
		regular = true;
	}

	if (tb[NL80211_WOWLAN_TRIG_EAP_IDENT_REQUEST]) {
		if (!(wowlan->flags & WIPHY_WOWLAN_EAP_IDENTITY_REQ))
			return -EINVAL;
		new_triggers.eap_identity_req = true;
		regular = true;
	}

	if (tb[NL80211_WOWLAN_TRIG_4WAY_HANDSHAKE]) {
		if (!(wowlan->flags & WIPHY_WOWLAN_4WAY_HANDSHAKE))
			return -EINVAL;
		new_triggers.four_way_handshake = true;
		regular = true;
	}

	if (tb[NL80211_WOWLAN_TRIG_RFKILL_RELEASE]) {
		if (!(wowlan->flags & WIPHY_WOWLAN_RFKILL_RELEASE))
			return -EINVAL;
		new_triggers.rfkill_release = true;
		regular = true;
	}

	if (tb[NL80211_WOWLAN_TRIG_PKT_PATTERN]) {
		struct nlattr *pat;
		int n_patterns = 0;
		int rem, pat_len, mask_len, pkt_offset;
		struct nlattr *pat_tb[NUM_NL80211_PKTPAT];

		regular = true;

		nla_for_each_nested(pat, tb[NL80211_WOWLAN_TRIG_PKT_PATTERN],
				    rem)
			n_patterns++;
		if (n_patterns > wowlan->n_patterns)
			return -EINVAL;

		new_triggers.patterns = kcalloc(n_patterns,
						sizeof(new_triggers.patterns[0]),
						GFP_KERNEL);
		if (!new_triggers.patterns)
			return -ENOMEM;

		new_triggers.n_patterns = n_patterns;
		i = 0;

		nla_for_each_nested(pat, tb[NL80211_WOWLAN_TRIG_PKT_PATTERN],
				    rem) {
			u8 *mask_pat;

			err = nla_parse_nested_deprecated(pat_tb,
							  MAX_NL80211_PKTPAT,
							  pat,
							  nl80211_packet_pattern_policy,
							  info->extack);
			if (err)
				goto error;

			err = -EINVAL;
			if (!pat_tb[NL80211_PKTPAT_MASK] ||
			    !pat_tb[NL80211_PKTPAT_PATTERN])
				goto error;
			pat_len = nla_len(pat_tb[NL80211_PKTPAT_PATTERN]);
			mask_len = DIV_ROUND_UP(pat_len, 8);
			if (nla_len(pat_tb[NL80211_PKTPAT_MASK]) != mask_len)
				goto error;
			if (pat_len > wowlan->pattern_max_len ||
			    pat_len < wowlan->pattern_min_len)
				goto error;

			if (!pat_tb[NL80211_PKTPAT_OFFSET])
				pkt_offset = 0;
			else
				pkt_offset = nla_get_u32(
					pat_tb[NL80211_PKTPAT_OFFSET]);
			if (pkt_offset > wowlan->max_pkt_offset)
				goto error;
			new_triggers.patterns[i].pkt_offset = pkt_offset;

			mask_pat = kmalloc(mask_len + pat_len, GFP_KERNEL);
			if (!mask_pat) {
				err = -ENOMEM;
				goto error;
			}
			new_triggers.patterns[i].mask = mask_pat;
			memcpy(mask_pat, nla_data(pat_tb[NL80211_PKTPAT_MASK]),
			       mask_len);
			mask_pat += mask_len;
			new_triggers.patterns[i].pattern = mask_pat;
			new_triggers.patterns[i].pattern_len = pat_len;
			memcpy(mask_pat,
			       nla_data(pat_tb[NL80211_PKTPAT_PATTERN]),
			       pat_len);
			i++;
		}
	}

	if (tb[NL80211_WOWLAN_TRIG_TCP_CONNECTION]) {
		regular = true;
		err = nl80211_parse_wowlan_tcp(
			rdev, tb[NL80211_WOWLAN_TRIG_TCP_CONNECTION],
			&new_triggers);
		if (err)
			goto error;
	}

	if (tb[NL80211_WOWLAN_TRIG_NET_DETECT]) {
		regular = true;
		err = nl80211_parse_wowlan_nd(
			rdev, wowlan, tb[NL80211_WOWLAN_TRIG_NET_DETECT],
			&new_triggers);
		if (err)
			goto error;
	}

	/* The 'any' trigger means the device continues operating more or less
	 * as in its normal operation mode and wakes up the host on most of the
	 * normal interrupts (like packet RX, ...)
	 * It therefore makes little sense to combine with the more constrained
	 * wakeup trigger modes.
	 */
	if (new_triggers.any && regular) {
		err = -EINVAL;
		goto error;
	}

	ntrig = kmemdup(&new_triggers, sizeof(new_triggers), GFP_KERNEL);
	if (!ntrig) {
		err = -ENOMEM;
		goto error;
	}
	cfg80211_rdev_free_wowlan(rdev);
	rdev->wiphy.wowlan_config = ntrig;

 set_wakeup:
	if (rdev->ops->set_wakeup &&
	    prev_enabled != !!rdev->wiphy.wowlan_config)
		rdev_set_wakeup(rdev, rdev->wiphy.wowlan_config);

	return 0;
 error:
	for (i = 0; i < new_triggers.n_patterns; i++)
		kfree(new_triggers.patterns[i].mask);
	kfree(new_triggers.patterns);
	if (new_triggers.tcp && new_triggers.tcp->sock)
		sock_release(new_triggers.tcp->sock);
	kfree(new_triggers.tcp);
	kfree(new_triggers.nd_config);
	return err;
}
#endif

static int nl80211_send_coalesce_rules(struct sk_buff *msg,
				       struct cfg80211_registered_device *rdev)
{
	struct nlattr *nl_pats, *nl_pat, *nl_rule, *nl_rules;
	int i, j, pat_len;
	struct cfg80211_coalesce_rules *rule;

	if (!rdev->coalesce->n_rules)
		return 0;

	nl_rules = nla_nest_start_noflag(msg, NL80211_ATTR_COALESCE_RULE);
	if (!nl_rules)
		return -ENOBUFS;

	for (i = 0; i < rdev->coalesce->n_rules; i++) {
		nl_rule = nla_nest_start_noflag(msg, i + 1);
		if (!nl_rule)
			return -ENOBUFS;

		rule = &rdev->coalesce->rules[i];
		if (nla_put_u32(msg, NL80211_ATTR_COALESCE_RULE_DELAY,
				rule->delay))
			return -ENOBUFS;

		if (nla_put_u32(msg, NL80211_ATTR_COALESCE_RULE_CONDITION,
				rule->condition))
			return -ENOBUFS;

		nl_pats = nla_nest_start_noflag(msg,
						NL80211_ATTR_COALESCE_RULE_PKT_PATTERN);
		if (!nl_pats)
			return -ENOBUFS;

		for (j = 0; j < rule->n_patterns; j++) {
			nl_pat = nla_nest_start_noflag(msg, j + 1);
			if (!nl_pat)
				return -ENOBUFS;
			pat_len = rule->patterns[j].pattern_len;
			if (nla_put(msg, NL80211_PKTPAT_MASK,
				    DIV_ROUND_UP(pat_len, 8),
				    rule->patterns[j].mask) ||
			    nla_put(msg, NL80211_PKTPAT_PATTERN, pat_len,
				    rule->patterns[j].pattern) ||
			    nla_put_u32(msg, NL80211_PKTPAT_OFFSET,
					rule->patterns[j].pkt_offset))
				return -ENOBUFS;
			nla_nest_end(msg, nl_pat);
		}
		nla_nest_end(msg, nl_pats);
		nla_nest_end(msg, nl_rule);
	}
	nla_nest_end(msg, nl_rules);

	return 0;
}

static int nl80211_get_coalesce(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct sk_buff *msg;
	void *hdr;

	if (!rdev->wiphy.coalesce)
		return -EOPNOTSUPP;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = nl80211hdr_put(msg, info->snd_portid, info->snd_seq, 0,
			     NL80211_CMD_GET_COALESCE);
	if (!hdr)
		goto nla_put_failure;

	if (rdev->coalesce && nl80211_send_coalesce_rules(msg, rdev))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return genlmsg_reply(msg, info);

nla_put_failure:
	nlmsg_free(msg);
	return -ENOBUFS;
}

void cfg80211_rdev_free_coalesce(struct cfg80211_registered_device *rdev)
{
	struct cfg80211_coalesce *coalesce = rdev->coalesce;
	int i, j;
	struct cfg80211_coalesce_rules *rule;

	if (!coalesce)
		return;

	for (i = 0; i < coalesce->n_rules; i++) {
		rule = &coalesce->rules[i];
		for (j = 0; j < rule->n_patterns; j++)
			kfree(rule->patterns[j].mask);
		kfree(rule->patterns);
	}
	kfree(coalesce->rules);
	kfree(coalesce);
	rdev->coalesce = NULL;
}

static int nl80211_parse_coalesce_rule(struct cfg80211_registered_device *rdev,
				       struct nlattr *rule,
				       struct cfg80211_coalesce_rules *new_rule)
{
	int err, i;
	const struct wiphy_coalesce_support *coalesce = rdev->wiphy.coalesce;
	struct nlattr *tb[NUM_NL80211_ATTR_COALESCE_RULE], *pat;
	int rem, pat_len, mask_len, pkt_offset, n_patterns = 0;
	struct nlattr *pat_tb[NUM_NL80211_PKTPAT];

	err = nla_parse_nested_deprecated(tb, NL80211_ATTR_COALESCE_RULE_MAX,
					  rule, nl80211_coalesce_policy, NULL);
	if (err)
		return err;

	if (tb[NL80211_ATTR_COALESCE_RULE_DELAY])
		new_rule->delay =
			nla_get_u32(tb[NL80211_ATTR_COALESCE_RULE_DELAY]);
	if (new_rule->delay > coalesce->max_delay)
		return -EINVAL;

	if (tb[NL80211_ATTR_COALESCE_RULE_CONDITION])
		new_rule->condition =
			nla_get_u32(tb[NL80211_ATTR_COALESCE_RULE_CONDITION]);

	if (!tb[NL80211_ATTR_COALESCE_RULE_PKT_PATTERN])
		return -EINVAL;

	nla_for_each_nested(pat, tb[NL80211_ATTR_COALESCE_RULE_PKT_PATTERN],
			    rem)
		n_patterns++;
	if (n_patterns > coalesce->n_patterns)
		return -EINVAL;

	new_rule->patterns = kcalloc(n_patterns, sizeof(new_rule->patterns[0]),
				     GFP_KERNEL);
	if (!new_rule->patterns)
		return -ENOMEM;

	new_rule->n_patterns = n_patterns;
	i = 0;

	nla_for_each_nested(pat, tb[NL80211_ATTR_COALESCE_RULE_PKT_PATTERN],
			    rem) {
		u8 *mask_pat;

		err = nla_parse_nested_deprecated(pat_tb, MAX_NL80211_PKTPAT,
						  pat,
						  nl80211_packet_pattern_policy,
						  NULL);
		if (err)
			return err;

		if (!pat_tb[NL80211_PKTPAT_MASK] ||
		    !pat_tb[NL80211_PKTPAT_PATTERN])
			return -EINVAL;
		pat_len = nla_len(pat_tb[NL80211_PKTPAT_PATTERN]);
		mask_len = DIV_ROUND_UP(pat_len, 8);
		if (nla_len(pat_tb[NL80211_PKTPAT_MASK]) != mask_len)
			return -EINVAL;
		if (pat_len > coalesce->pattern_max_len ||
		    pat_len < coalesce->pattern_min_len)
			return -EINVAL;

		if (!pat_tb[NL80211_PKTPAT_OFFSET])
			pkt_offset = 0;
		else
			pkt_offset = nla_get_u32(pat_tb[NL80211_PKTPAT_OFFSET]);
		if (pkt_offset > coalesce->max_pkt_offset)
			return -EINVAL;
		new_rule->patterns[i].pkt_offset = pkt_offset;

		mask_pat = kmalloc(mask_len + pat_len, GFP_KERNEL);
		if (!mask_pat)
			return -ENOMEM;

		new_rule->patterns[i].mask = mask_pat;
		memcpy(mask_pat, nla_data(pat_tb[NL80211_PKTPAT_MASK]),
		       mask_len);

		mask_pat += mask_len;
		new_rule->patterns[i].pattern = mask_pat;
		new_rule->patterns[i].pattern_len = pat_len;
		memcpy(mask_pat, nla_data(pat_tb[NL80211_PKTPAT_PATTERN]),
		       pat_len);
		i++;
	}

	return 0;
}

static int nl80211_set_coalesce(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	const struct wiphy_coalesce_support *coalesce = rdev->wiphy.coalesce;
	struct cfg80211_coalesce new_coalesce = {};
	struct cfg80211_coalesce *n_coalesce;
	int err, rem_rule, n_rules = 0, i, j;
	struct nlattr *rule;
	struct cfg80211_coalesce_rules *tmp_rule;

	if (!rdev->wiphy.coalesce || !rdev->ops->set_coalesce)
		return -EOPNOTSUPP;

	if (!info->attrs[NL80211_ATTR_COALESCE_RULE]) {
		cfg80211_rdev_free_coalesce(rdev);
		rdev_set_coalesce(rdev, NULL);
		return 0;
	}

	nla_for_each_nested(rule, info->attrs[NL80211_ATTR_COALESCE_RULE],
			    rem_rule)
		n_rules++;
	if (n_rules > coalesce->n_rules)
		return -EINVAL;

	new_coalesce.rules = kcalloc(n_rules, sizeof(new_coalesce.rules[0]),
				     GFP_KERNEL);
	if (!new_coalesce.rules)
		return -ENOMEM;

	new_coalesce.n_rules = n_rules;
	i = 0;

	nla_for_each_nested(rule, info->attrs[NL80211_ATTR_COALESCE_RULE],
			    rem_rule) {
		err = nl80211_parse_coalesce_rule(rdev, rule,
						  &new_coalesce.rules[i]);
		if (err)
			goto error;

		i++;
	}

	err = rdev_set_coalesce(rdev, &new_coalesce);
	if (err)
		goto error;

	n_coalesce = kmemdup(&new_coalesce, sizeof(new_coalesce), GFP_KERNEL);
	if (!n_coalesce) {
		err = -ENOMEM;
		goto error;
	}
	cfg80211_rdev_free_coalesce(rdev);
	rdev->coalesce = n_coalesce;

	return 0;
error:
	for (i = 0; i < new_coalesce.n_rules; i++) {
		tmp_rule = &new_coalesce.rules[i];
		for (j = 0; j < tmp_rule->n_patterns; j++)
			kfree(tmp_rule->patterns[j].mask);
		kfree(tmp_rule->patterns);
	}
	kfree(new_coalesce.rules);

	return err;
}

static int nl80211_set_rekey_data(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct nlattr *tb[NUM_NL80211_REKEY_DATA];
	struct cfg80211_gtk_rekey_data rekey_data;
	int err;

	if (!info->attrs[NL80211_ATTR_REKEY_DATA])
		return -EINVAL;

	err = nla_parse_nested_deprecated(tb, MAX_NL80211_REKEY_DATA,
					  info->attrs[NL80211_ATTR_REKEY_DATA],
					  nl80211_rekey_policy, info->extack);
	if (err)
		return err;

	if (!tb[NL80211_REKEY_DATA_REPLAY_CTR] || !tb[NL80211_REKEY_DATA_KEK] ||
	    !tb[NL80211_REKEY_DATA_KCK])
		return -EINVAL;
	if (nla_len(tb[NL80211_REKEY_DATA_REPLAY_CTR]) != NL80211_REPLAY_CTR_LEN)
		return -ERANGE;
	if (nla_len(tb[NL80211_REKEY_DATA_KEK]) != NL80211_KEK_LEN)
		return -ERANGE;
	if (nla_len(tb[NL80211_REKEY_DATA_KCK]) != NL80211_KCK_LEN)
		return -ERANGE;

	rekey_data.kek = nla_data(tb[NL80211_REKEY_DATA_KEK]);
	rekey_data.kck = nla_data(tb[NL80211_REKEY_DATA_KCK]);
	rekey_data.replay_ctr = nla_data(tb[NL80211_REKEY_DATA_REPLAY_CTR]);

	wdev_lock(wdev);
	if (!wdev->current_bss) {
		err = -ENOTCONN;
		goto out;
	}

	if (!rdev->ops->set_rekey_data) {
		err = -EOPNOTSUPP;
		goto out;
	}

	err = rdev_set_rekey_data(rdev, dev, &rekey_data);
 out:
	wdev_unlock(wdev);
	return err;
}

static int nl80211_register_unexpected_frame(struct sk_buff *skb,
					     struct genl_info *info)
{
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	if (wdev->iftype != NL80211_IFTYPE_AP &&
	    wdev->iftype != NL80211_IFTYPE_P2P_GO)
		return -EINVAL;

	if (wdev->ap_unexpected_nlportid)
		return -EBUSY;

	wdev->ap_unexpected_nlportid = info->snd_portid;
	return 0;
}

static int nl80211_probe_client(struct sk_buff *skb,
				struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct sk_buff *msg;
	void *hdr;
	const u8 *addr;
	u64 cookie;
	int err;

	if (wdev->iftype != NL80211_IFTYPE_AP &&
	    wdev->iftype != NL80211_IFTYPE_P2P_GO)
		return -EOPNOTSUPP;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	if (!rdev->ops->probe_client)
		return -EOPNOTSUPP;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = nl80211hdr_put(msg, info->snd_portid, info->snd_seq, 0,
			     NL80211_CMD_PROBE_CLIENT);
	if (!hdr) {
		err = -ENOBUFS;
		goto free_msg;
	}

	addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	err = rdev_probe_client(rdev, dev, addr, &cookie);
	if (err)
		goto free_msg;

	if (nla_put_u64_64bit(msg, NL80211_ATTR_COOKIE, cookie,
			      NL80211_ATTR_PAD))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return genlmsg_reply(msg, info);

 nla_put_failure:
	err = -ENOBUFS;
 free_msg:
	nlmsg_free(msg);
	return err;
}

static int nl80211_register_beacons(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct cfg80211_beacon_registration *reg, *nreg;
	int rv;

	if (!(rdev->wiphy.flags & WIPHY_FLAG_REPORTS_OBSS))
		return -EOPNOTSUPP;

	nreg = kzalloc(sizeof(*nreg), GFP_KERNEL);
	if (!nreg)
		return -ENOMEM;

	/* First, check if already registered. */
	spin_lock_bh(&rdev->beacon_registrations_lock);
	list_for_each_entry(reg, &rdev->beacon_registrations, list) {
		if (reg->nlportid == info->snd_portid) {
			rv = -EALREADY;
			goto out_err;
		}
	}
	/* Add it to the list */
	nreg->nlportid = info->snd_portid;
	list_add(&nreg->list, &rdev->beacon_registrations);

	spin_unlock_bh(&rdev->beacon_registrations_lock);

	return 0;
out_err:
	spin_unlock_bh(&rdev->beacon_registrations_lock);
	kfree(nreg);
	return rv;
}

static int nl80211_start_p2p_device(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];
	int err;

	if (!rdev->ops->start_p2p_device)
		return -EOPNOTSUPP;

	if (wdev->iftype != NL80211_IFTYPE_P2P_DEVICE)
		return -EOPNOTSUPP;

	if (wdev_running(wdev))
		return 0;

	if (rfkill_blocked(rdev->rfkill))
		return -ERFKILL;

	err = rdev_start_p2p_device(rdev, wdev);
	if (err)
		return err;

	wdev->is_running = true;
	rdev->opencount++;

	return 0;
}

static int nl80211_stop_p2p_device(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];

	if (wdev->iftype != NL80211_IFTYPE_P2P_DEVICE)
		return -EOPNOTSUPP;

	if (!rdev->ops->stop_p2p_device)
		return -EOPNOTSUPP;

	cfg80211_stop_p2p_device(rdev, wdev);

	return 0;
}

static int nl80211_start_nan(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];
	struct cfg80211_nan_conf conf = {};
	int err;

	if (wdev->iftype != NL80211_IFTYPE_NAN)
		return -EOPNOTSUPP;

	if (wdev_running(wdev))
		return -EEXIST;

	if (rfkill_blocked(rdev->rfkill))
		return -ERFKILL;

	if (!info->attrs[NL80211_ATTR_NAN_MASTER_PREF])
		return -EINVAL;

	conf.master_pref =
		nla_get_u8(info->attrs[NL80211_ATTR_NAN_MASTER_PREF]);

	if (info->attrs[NL80211_ATTR_BANDS]) {
		u32 bands = nla_get_u32(info->attrs[NL80211_ATTR_BANDS]);

		if (bands & ~(u32)wdev->wiphy->nan_supported_bands)
			return -EOPNOTSUPP;

		if (bands && !(bands & BIT(NL80211_BAND_2GHZ)))
			return -EINVAL;

		conf.bands = bands;
	}

	err = rdev_start_nan(rdev, wdev, &conf);
	if (err)
		return err;

	wdev->is_running = true;
	rdev->opencount++;

	return 0;
}

static int nl80211_stop_nan(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];

	if (wdev->iftype != NL80211_IFTYPE_NAN)
		return -EOPNOTSUPP;

	cfg80211_stop_nan(rdev, wdev);

	return 0;
}

static int validate_nan_filter(struct nlattr *filter_attr)
{
	struct nlattr *attr;
	int len = 0, n_entries = 0, rem;

	nla_for_each_nested(attr, filter_attr, rem) {
		len += nla_len(attr);
		n_entries++;
	}

	if (len >= U8_MAX)
		return -EINVAL;

	return n_entries;
}

static int handle_nan_filter(struct nlattr *attr_filter,
			     struct cfg80211_nan_func *func,
			     bool tx)
{
	struct nlattr *attr;
	int n_entries, rem, i;
	struct cfg80211_nan_func_filter *filter;

	n_entries = validate_nan_filter(attr_filter);
	if (n_entries < 0)
		return n_entries;

	BUILD_BUG_ON(sizeof(*func->rx_filters) != sizeof(*func->tx_filters));

	filter = kcalloc(n_entries, sizeof(*func->rx_filters), GFP_KERNEL);
	if (!filter)
		return -ENOMEM;

	i = 0;
	nla_for_each_nested(attr, attr_filter, rem) {
		filter[i].filter = nla_memdup(attr, GFP_KERNEL);
		filter[i].len = nla_len(attr);
		i++;
	}
	if (tx) {
		func->num_tx_filters = n_entries;
		func->tx_filters = filter;
	} else {
		func->num_rx_filters = n_entries;
		func->rx_filters = filter;
	}

	return 0;
}

static int nl80211_nan_add_func(struct sk_buff *skb,
				struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];
	struct nlattr *tb[NUM_NL80211_NAN_FUNC_ATTR], *func_attr;
	struct cfg80211_nan_func *func;
	struct sk_buff *msg = NULL;
	void *hdr = NULL;
	int err = 0;

	if (wdev->iftype != NL80211_IFTYPE_NAN)
		return -EOPNOTSUPP;

	if (!wdev_running(wdev))
		return -ENOTCONN;

	if (!info->attrs[NL80211_ATTR_NAN_FUNC])
		return -EINVAL;

	err = nla_parse_nested_deprecated(tb, NL80211_NAN_FUNC_ATTR_MAX,
					  info->attrs[NL80211_ATTR_NAN_FUNC],
					  nl80211_nan_func_policy,
					  info->extack);
	if (err)
		return err;

	func = kzalloc(sizeof(*func), GFP_KERNEL);
	if (!func)
		return -ENOMEM;

	func->cookie = cfg80211_assign_cookie(rdev);

	if (!tb[NL80211_NAN_FUNC_TYPE] ||
	    nla_get_u8(tb[NL80211_NAN_FUNC_TYPE]) > NL80211_NAN_FUNC_MAX_TYPE) {
		err = -EINVAL;
		goto out;
	}


	func->type = nla_get_u8(tb[NL80211_NAN_FUNC_TYPE]);

	if (!tb[NL80211_NAN_FUNC_SERVICE_ID]) {
		err = -EINVAL;
		goto out;
	}

	memcpy(func->service_id, nla_data(tb[NL80211_NAN_FUNC_SERVICE_ID]),
	       sizeof(func->service_id));

	func->close_range =
		nla_get_flag(tb[NL80211_NAN_FUNC_CLOSE_RANGE]);

	if (tb[NL80211_NAN_FUNC_SERVICE_INFO]) {
		func->serv_spec_info_len =
			nla_len(tb[NL80211_NAN_FUNC_SERVICE_INFO]);
		func->serv_spec_info =
			kmemdup(nla_data(tb[NL80211_NAN_FUNC_SERVICE_INFO]),
				func->serv_spec_info_len,
				GFP_KERNEL);
		if (!func->serv_spec_info) {
			err = -ENOMEM;
			goto out;
		}
	}

	if (tb[NL80211_NAN_FUNC_TTL])
		func->ttl = nla_get_u32(tb[NL80211_NAN_FUNC_TTL]);

	switch (func->type) {
	case NL80211_NAN_FUNC_PUBLISH:
		if (!tb[NL80211_NAN_FUNC_PUBLISH_TYPE]) {
			err = -EINVAL;
			goto out;
		}

		func->publish_type =
			nla_get_u8(tb[NL80211_NAN_FUNC_PUBLISH_TYPE]);
		func->publish_bcast =
			nla_get_flag(tb[NL80211_NAN_FUNC_PUBLISH_BCAST]);

		if ((!(func->publish_type & NL80211_NAN_SOLICITED_PUBLISH)) &&
			func->publish_bcast) {
			err = -EINVAL;
			goto out;
		}
		break;
	case NL80211_NAN_FUNC_SUBSCRIBE:
		func->subscribe_active =
			nla_get_flag(tb[NL80211_NAN_FUNC_SUBSCRIBE_ACTIVE]);
		break;
	case NL80211_NAN_FUNC_FOLLOW_UP:
		if (!tb[NL80211_NAN_FUNC_FOLLOW_UP_ID] ||
		    !tb[NL80211_NAN_FUNC_FOLLOW_UP_REQ_ID] ||
		    !tb[NL80211_NAN_FUNC_FOLLOW_UP_DEST]) {
			err = -EINVAL;
			goto out;
		}

		func->followup_id =
			nla_get_u8(tb[NL80211_NAN_FUNC_FOLLOW_UP_ID]);
		func->followup_reqid =
			nla_get_u8(tb[NL80211_NAN_FUNC_FOLLOW_UP_REQ_ID]);
		memcpy(func->followup_dest.addr,
		       nla_data(tb[NL80211_NAN_FUNC_FOLLOW_UP_DEST]),
		       sizeof(func->followup_dest.addr));
		if (func->ttl) {
			err = -EINVAL;
			goto out;
		}
		break;
	default:
		err = -EINVAL;
		goto out;
	}

	if (tb[NL80211_NAN_FUNC_SRF]) {
		struct nlattr *srf_tb[NUM_NL80211_NAN_SRF_ATTR];

		err = nla_parse_nested_deprecated(srf_tb,
						  NL80211_NAN_SRF_ATTR_MAX,
						  tb[NL80211_NAN_FUNC_SRF],
						  nl80211_nan_srf_policy,
						  info->extack);
		if (err)
			goto out;

		func->srf_include =
			nla_get_flag(srf_tb[NL80211_NAN_SRF_INCLUDE]);

		if (srf_tb[NL80211_NAN_SRF_BF]) {
			if (srf_tb[NL80211_NAN_SRF_MAC_ADDRS] ||
			    !srf_tb[NL80211_NAN_SRF_BF_IDX]) {
				err = -EINVAL;
				goto out;
			}

			func->srf_bf_len =
				nla_len(srf_tb[NL80211_NAN_SRF_BF]);
			func->srf_bf =
				kmemdup(nla_data(srf_tb[NL80211_NAN_SRF_BF]),
					func->srf_bf_len, GFP_KERNEL);
			if (!func->srf_bf) {
				err = -ENOMEM;
				goto out;
			}

			func->srf_bf_idx =
				nla_get_u8(srf_tb[NL80211_NAN_SRF_BF_IDX]);
		} else {
			struct nlattr *attr, *mac_attr =
				srf_tb[NL80211_NAN_SRF_MAC_ADDRS];
			int n_entries, rem, i = 0;

			if (!mac_attr) {
				err = -EINVAL;
				goto out;
			}

			n_entries = validate_acl_mac_addrs(mac_attr);
			if (n_entries <= 0) {
				err = -EINVAL;
				goto out;
			}

			func->srf_num_macs = n_entries;
			func->srf_macs =
				kcalloc(n_entries, sizeof(*func->srf_macs),
					GFP_KERNEL);
			if (!func->srf_macs) {
				err = -ENOMEM;
				goto out;
			}

			nla_for_each_nested(attr, mac_attr, rem)
				memcpy(func->srf_macs[i++].addr, nla_data(attr),
				       sizeof(*func->srf_macs));
		}
	}

	if (tb[NL80211_NAN_FUNC_TX_MATCH_FILTER]) {
		err = handle_nan_filter(tb[NL80211_NAN_FUNC_TX_MATCH_FILTER],
					func, true);
		if (err)
			goto out;
	}

	if (tb[NL80211_NAN_FUNC_RX_MATCH_FILTER]) {
		err = handle_nan_filter(tb[NL80211_NAN_FUNC_RX_MATCH_FILTER],
					func, false);
		if (err)
			goto out;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		err = -ENOMEM;
		goto out;
	}

	hdr = nl80211hdr_put(msg, info->snd_portid, info->snd_seq, 0,
			     NL80211_CMD_ADD_NAN_FUNCTION);
	/* This can't really happen - we just allocated 4KB */
	if (WARN_ON(!hdr)) {
		err = -ENOMEM;
		goto out;
	}

	err = rdev_add_nan_func(rdev, wdev, func);
out:
	if (err < 0) {
		cfg80211_free_nan_func(func);
		nlmsg_free(msg);
		return err;
	}

	/* propagate the instance id and cookie to userspace  */
	if (nla_put_u64_64bit(msg, NL80211_ATTR_COOKIE, func->cookie,
			      NL80211_ATTR_PAD))
		goto nla_put_failure;

	func_attr = nla_nest_start_noflag(msg, NL80211_ATTR_NAN_FUNC);
	if (!func_attr)
		goto nla_put_failure;

	if (nla_put_u8(msg, NL80211_NAN_FUNC_INSTANCE_ID,
		       func->instance_id))
		goto nla_put_failure;

	nla_nest_end(msg, func_attr);

	genlmsg_end(msg, hdr);
	return genlmsg_reply(msg, info);

nla_put_failure:
	nlmsg_free(msg);
	return -ENOBUFS;
}

static int nl80211_nan_del_func(struct sk_buff *skb,
			       struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];
	u64 cookie;

	if (wdev->iftype != NL80211_IFTYPE_NAN)
		return -EOPNOTSUPP;

	if (!wdev_running(wdev))
		return -ENOTCONN;

	if (!info->attrs[NL80211_ATTR_COOKIE])
		return -EINVAL;

	cookie = nla_get_u64(info->attrs[NL80211_ATTR_COOKIE]);

	rdev_del_nan_func(rdev, wdev, cookie);

	return 0;
}

static int nl80211_nan_change_config(struct sk_buff *skb,
				     struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];
	struct cfg80211_nan_conf conf = {};
	u32 changed = 0;

	if (wdev->iftype != NL80211_IFTYPE_NAN)
		return -EOPNOTSUPP;

	if (!wdev_running(wdev))
		return -ENOTCONN;

	if (info->attrs[NL80211_ATTR_NAN_MASTER_PREF]) {
		conf.master_pref =
			nla_get_u8(info->attrs[NL80211_ATTR_NAN_MASTER_PREF]);
		if (conf.master_pref <= 1 || conf.master_pref == 255)
			return -EINVAL;

		changed |= CFG80211_NAN_CONF_CHANGED_PREF;
	}

	if (info->attrs[NL80211_ATTR_BANDS]) {
		u32 bands = nla_get_u32(info->attrs[NL80211_ATTR_BANDS]);

		if (bands & ~(u32)wdev->wiphy->nan_supported_bands)
			return -EOPNOTSUPP;

		if (bands && !(bands & BIT(NL80211_BAND_2GHZ)))
			return -EINVAL;

		conf.bands = bands;
		changed |= CFG80211_NAN_CONF_CHANGED_BANDS;
	}

	if (!changed)
		return -EINVAL;

	return rdev_nan_change_conf(rdev, wdev, &conf, changed);
}

void cfg80211_nan_match(struct wireless_dev *wdev,
			struct cfg80211_nan_match_params *match, gfp_t gfp)
{
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct nlattr *match_attr, *local_func_attr, *peer_func_attr;
	struct sk_buff *msg;
	void *hdr;

	if (WARN_ON(!match->inst_id || !match->peer_inst_id || !match->addr))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_NAN_MATCH);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    (wdev->netdev && nla_put_u32(msg, NL80211_ATTR_IFINDEX,
					 wdev->netdev->ifindex)) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_WDEV, wdev_id(wdev),
			      NL80211_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, NL80211_ATTR_COOKIE, match->cookie,
			      NL80211_ATTR_PAD) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, match->addr))
		goto nla_put_failure;

	match_attr = nla_nest_start_noflag(msg, NL80211_ATTR_NAN_MATCH);
	if (!match_attr)
		goto nla_put_failure;

	local_func_attr = nla_nest_start_noflag(msg,
						NL80211_NAN_MATCH_FUNC_LOCAL);
	if (!local_func_attr)
		goto nla_put_failure;

	if (nla_put_u8(msg, NL80211_NAN_FUNC_INSTANCE_ID, match->inst_id))
		goto nla_put_failure;

	nla_nest_end(msg, local_func_attr);

	peer_func_attr = nla_nest_start_noflag(msg,
					       NL80211_NAN_MATCH_FUNC_PEER);
	if (!peer_func_attr)
		goto nla_put_failure;

	if (nla_put_u8(msg, NL80211_NAN_FUNC_TYPE, match->type) ||
	    nla_put_u8(msg, NL80211_NAN_FUNC_INSTANCE_ID, match->peer_inst_id))
		goto nla_put_failure;

	if (match->info && match->info_len &&
	    nla_put(msg, NL80211_NAN_FUNC_SERVICE_INFO, match->info_len,
		    match->info))
		goto nla_put_failure;

	nla_nest_end(msg, peer_func_attr);
	nla_nest_end(msg, match_attr);
	genlmsg_end(msg, hdr);

	if (!wdev->owner_nlportid)
		genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy),
					msg, 0, NL80211_MCGRP_NAN, gfp);
	else
		genlmsg_unicast(wiphy_net(&rdev->wiphy), msg,
				wdev->owner_nlportid);

	return;

nla_put_failure:
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_nan_match);

void cfg80211_nan_func_terminated(struct wireless_dev *wdev,
				  u8 inst_id,
				  enum nl80211_nan_func_term_reason reason,
				  u64 cookie, gfp_t gfp)
{
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct sk_buff *msg;
	struct nlattr *func_attr;
	void *hdr;

	if (WARN_ON(!inst_id))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_DEL_NAN_FUNCTION);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    (wdev->netdev && nla_put_u32(msg, NL80211_ATTR_IFINDEX,
					 wdev->netdev->ifindex)) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_WDEV, wdev_id(wdev),
			      NL80211_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u64_64bit(msg, NL80211_ATTR_COOKIE, cookie,
			      NL80211_ATTR_PAD))
		goto nla_put_failure;

	func_attr = nla_nest_start_noflag(msg, NL80211_ATTR_NAN_FUNC);
	if (!func_attr)
		goto nla_put_failure;

	if (nla_put_u8(msg, NL80211_NAN_FUNC_INSTANCE_ID, inst_id) ||
	    nla_put_u8(msg, NL80211_NAN_FUNC_TERM_REASON, reason))
		goto nla_put_failure;

	nla_nest_end(msg, func_attr);
	genlmsg_end(msg, hdr);

	if (!wdev->owner_nlportid)
		genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy),
					msg, 0, NL80211_MCGRP_NAN, gfp);
	else
		genlmsg_unicast(wiphy_net(&rdev->wiphy), msg,
				wdev->owner_nlportid);

	return;

nla_put_failure:
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_nan_func_terminated);

static int nl80211_get_protocol_features(struct sk_buff *skb,
					 struct genl_info *info)
{
	void *hdr;
	struct sk_buff *msg;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = nl80211hdr_put(msg, info->snd_portid, info->snd_seq, 0,
			     NL80211_CMD_GET_PROTOCOL_FEATURES);
	if (!hdr)
		goto nla_put_failure;

	if (nla_put_u32(msg, NL80211_ATTR_PROTOCOL_FEATURES,
			NL80211_PROTOCOL_FEATURE_SPLIT_WIPHY_DUMP))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return genlmsg_reply(msg, info);

 nla_put_failure:
	kfree_skb(msg);
	return -ENOBUFS;
}

static int nl80211_update_ft_ies(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct cfg80211_update_ft_ies_params ft_params;
	struct net_device *dev = info->user_ptr[1];

	if (!rdev->ops->update_ft_ies)
		return -EOPNOTSUPP;

	if (!info->attrs[NL80211_ATTR_MDID] ||
	    !info->attrs[NL80211_ATTR_IE])
		return -EINVAL;

	memset(&ft_params, 0, sizeof(ft_params));
	ft_params.md = nla_get_u16(info->attrs[NL80211_ATTR_MDID]);
	ft_params.ie = nla_data(info->attrs[NL80211_ATTR_IE]);
	ft_params.ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);

	return rdev_update_ft_ies(rdev, dev, &ft_params);
}

static int nl80211_crit_protocol_start(struct sk_buff *skb,
				       struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];
	enum nl80211_crit_proto_id proto = NL80211_CRIT_PROTO_UNSPEC;
	u16 duration;
	int ret;

	if (!rdev->ops->crit_proto_start)
		return -EOPNOTSUPP;

	if (WARN_ON(!rdev->ops->crit_proto_stop))
		return -EINVAL;

	if (rdev->crit_proto_nlportid)
		return -EBUSY;

	/* determine protocol if provided */
	if (info->attrs[NL80211_ATTR_CRIT_PROT_ID])
		proto = nla_get_u16(info->attrs[NL80211_ATTR_CRIT_PROT_ID]);

	if (proto >= NUM_NL80211_CRIT_PROTO)
		return -EINVAL;

	/* timeout must be provided */
	if (!info->attrs[NL80211_ATTR_MAX_CRIT_PROT_DURATION])
		return -EINVAL;

	duration =
		nla_get_u16(info->attrs[NL80211_ATTR_MAX_CRIT_PROT_DURATION]);

	if (duration > NL80211_CRIT_PROTO_MAX_DURATION)
		return -ERANGE;

	ret = rdev_crit_proto_start(rdev, wdev, proto, duration);
	if (!ret)
		rdev->crit_proto_nlportid = info->snd_portid;

	return ret;
}

static int nl80211_crit_protocol_stop(struct sk_buff *skb,
				      struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev = info->user_ptr[1];

	if (!rdev->ops->crit_proto_stop)
		return -EOPNOTSUPP;

	if (rdev->crit_proto_nlportid) {
		rdev->crit_proto_nlportid = 0;
		rdev_crit_proto_stop(rdev, wdev);
	}
	return 0;
}

static int nl80211_vendor_cmd(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct wireless_dev *wdev =
		__cfg80211_wdev_from_attrs(genl_info_net(info), info->attrs);
	int i, err;
	u32 vid, subcmd;

	if (!rdev->wiphy.vendor_commands)
		return -EOPNOTSUPP;

	if (IS_ERR(wdev)) {
		err = PTR_ERR(wdev);
		if (err != -EINVAL)
			return err;
		wdev = NULL;
	} else if (wdev->wiphy != &rdev->wiphy) {
		return -EINVAL;
	}

	if (!info->attrs[NL80211_ATTR_VENDOR_ID] ||
	    !info->attrs[NL80211_ATTR_VENDOR_SUBCMD])
		return -EINVAL;

	vid = nla_get_u32(info->attrs[NL80211_ATTR_VENDOR_ID]);
	subcmd = nla_get_u32(info->attrs[NL80211_ATTR_VENDOR_SUBCMD]);
	for (i = 0; i < rdev->wiphy.n_vendor_commands; i++) {
		const struct wiphy_vendor_command *vcmd;
		void *data = NULL;
		int len = 0;

		vcmd = &rdev->wiphy.vendor_commands[i];

		if (vcmd->info.vendor_id != vid || vcmd->info.subcmd != subcmd)
			continue;

		if (vcmd->flags & (WIPHY_VENDOR_CMD_NEED_WDEV |
				   WIPHY_VENDOR_CMD_NEED_NETDEV)) {
			if (!wdev)
				return -EINVAL;
			if (vcmd->flags & WIPHY_VENDOR_CMD_NEED_NETDEV &&
			    !wdev->netdev)
				return -EINVAL;

			if (vcmd->flags & WIPHY_VENDOR_CMD_NEED_RUNNING) {
				if (!wdev_running(wdev))
					return -ENETDOWN;
			}

			if (!vcmd->doit)
				return -EOPNOTSUPP;
		} else {
			wdev = NULL;
		}

		if (info->attrs[NL80211_ATTR_VENDOR_DATA]) {
			data = nla_data(info->attrs[NL80211_ATTR_VENDOR_DATA]);
			len = nla_len(info->attrs[NL80211_ATTR_VENDOR_DATA]);
		}

		rdev->cur_cmd_info = info;
		err = rdev->wiphy.vendor_commands[i].doit(&rdev->wiphy, wdev,
							  data, len);
		rdev->cur_cmd_info = NULL;
		return err;
	}

	return -EOPNOTSUPP;
}

static int nl80211_prepare_vendor_dump(struct sk_buff *skb,
				       struct netlink_callback *cb,
				       struct cfg80211_registered_device **rdev,
				       struct wireless_dev **wdev)
{
	struct nlattr **attrbuf = genl_family_attrbuf(&nl80211_fam);
	u32 vid, subcmd;
	unsigned int i;
	int vcmd_idx = -1;
	int err;
	void *data = NULL;
	unsigned int data_len = 0;

	if (cb->args[0]) {
		/* subtract the 1 again here */
		struct wiphy *wiphy = wiphy_idx_to_wiphy(cb->args[0] - 1);
		struct wireless_dev *tmp;

		if (!wiphy)
			return -ENODEV;
		*rdev = wiphy_to_rdev(wiphy);
		*wdev = NULL;

		if (cb->args[1]) {
			list_for_each_entry(tmp, &wiphy->wdev_list, list) {
				if (tmp->identifier == cb->args[1] - 1) {
					*wdev = tmp;
					break;
				}
			}
		}

		/* keep rtnl locked in successful case */
		return 0;
	}

	err = nlmsg_parse_deprecated(cb->nlh,
				     GENL_HDRLEN + nl80211_fam.hdrsize,
				     attrbuf, nl80211_fam.maxattr,
				     nl80211_policy, NULL);
	if (err)
		return err;

	if (!attrbuf[NL80211_ATTR_VENDOR_ID] ||
	    !attrbuf[NL80211_ATTR_VENDOR_SUBCMD])
		return -EINVAL;

	*wdev = __cfg80211_wdev_from_attrs(sock_net(skb->sk), attrbuf);
	if (IS_ERR(*wdev))
		*wdev = NULL;

	*rdev = __cfg80211_rdev_from_attrs(sock_net(skb->sk), attrbuf);
	if (IS_ERR(*rdev))
		return PTR_ERR(*rdev);

	vid = nla_get_u32(attrbuf[NL80211_ATTR_VENDOR_ID]);
	subcmd = nla_get_u32(attrbuf[NL80211_ATTR_VENDOR_SUBCMD]);

	for (i = 0; i < (*rdev)->wiphy.n_vendor_commands; i++) {
		const struct wiphy_vendor_command *vcmd;

		vcmd = &(*rdev)->wiphy.vendor_commands[i];

		if (vcmd->info.vendor_id != vid || vcmd->info.subcmd != subcmd)
			continue;

		if (!vcmd->dumpit)
			return -EOPNOTSUPP;

		vcmd_idx = i;
		break;
	}

	if (vcmd_idx < 0)
		return -EOPNOTSUPP;

	if (attrbuf[NL80211_ATTR_VENDOR_DATA]) {
		data = nla_data(attrbuf[NL80211_ATTR_VENDOR_DATA]);
		data_len = nla_len(attrbuf[NL80211_ATTR_VENDOR_DATA]);
	}

	/* 0 is the first index - add 1 to parse only once */
	cb->args[0] = (*rdev)->wiphy_idx + 1;
	/* add 1 to know if it was NULL */
	cb->args[1] = *wdev ? (*wdev)->identifier + 1 : 0;
	cb->args[2] = vcmd_idx;
	cb->args[3] = (unsigned long)data;
	cb->args[4] = data_len;

	/* keep rtnl locked in successful case */
	return 0;
}

static int nl80211_vendor_cmd_dump(struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *wdev;
	unsigned int vcmd_idx;
	const struct wiphy_vendor_command *vcmd;
	void *data;
	int data_len;
	int err;
	struct nlattr *vendor_data;

	rtnl_lock();
	err = nl80211_prepare_vendor_dump(skb, cb, &rdev, &wdev);
	if (err)
		goto out;

	vcmd_idx = cb->args[2];
	data = (void *)cb->args[3];
	data_len = cb->args[4];
	vcmd = &rdev->wiphy.vendor_commands[vcmd_idx];

	if (vcmd->flags & (WIPHY_VENDOR_CMD_NEED_WDEV |
			   WIPHY_VENDOR_CMD_NEED_NETDEV)) {
		if (!wdev) {
			err = -EINVAL;
			goto out;
		}
		if (vcmd->flags & WIPHY_VENDOR_CMD_NEED_NETDEV &&
		    !wdev->netdev) {
			err = -EINVAL;
			goto out;
		}

		if (vcmd->flags & WIPHY_VENDOR_CMD_NEED_RUNNING) {
			if (!wdev_running(wdev)) {
				err = -ENETDOWN;
				goto out;
			}
		}
	}

	while (1) {
		void *hdr = nl80211hdr_put(skb, NETLINK_CB(cb->skb).portid,
					   cb->nlh->nlmsg_seq, NLM_F_MULTI,
					   NL80211_CMD_VENDOR);
		if (!hdr)
			break;

		if (nla_put_u32(skb, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
		    (wdev && nla_put_u64_64bit(skb, NL80211_ATTR_WDEV,
					       wdev_id(wdev),
					       NL80211_ATTR_PAD))) {
			genlmsg_cancel(skb, hdr);
			break;
		}

		vendor_data = nla_nest_start_noflag(skb,
						    NL80211_ATTR_VENDOR_DATA);
		if (!vendor_data) {
			genlmsg_cancel(skb, hdr);
			break;
		}

		err = vcmd->dumpit(&rdev->wiphy, wdev, skb, data, data_len,
				   (unsigned long *)&cb->args[5]);
		nla_nest_end(skb, vendor_data);

		if (err == -ENOBUFS || err == -ENOENT) {
			genlmsg_cancel(skb, hdr);
			break;
		} else if (err) {
			genlmsg_cancel(skb, hdr);
			goto out;
		}

		genlmsg_end(skb, hdr);
	}

	err = skb->len;
 out:
	rtnl_unlock();
	return err;
}

struct sk_buff *__cfg80211_alloc_reply_skb(struct wiphy *wiphy,
					   enum nl80211_commands cmd,
					   enum nl80211_attrs attr,
					   int approxlen)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	if (WARN_ON(!rdev->cur_cmd_info))
		return NULL;

	return __cfg80211_alloc_vendor_skb(rdev, NULL, approxlen,
					   rdev->cur_cmd_info->snd_portid,
					   rdev->cur_cmd_info->snd_seq,
					   cmd, attr, NULL, GFP_KERNEL);
}
EXPORT_SYMBOL(__cfg80211_alloc_reply_skb);

int cfg80211_vendor_cmd_reply(struct sk_buff *skb)
{
	struct cfg80211_registered_device *rdev = ((void **)skb->cb)[0];
	void *hdr = ((void **)skb->cb)[1];
	struct nlattr *data = ((void **)skb->cb)[2];

	/* clear CB data for netlink core to own from now on */
	memset(skb->cb, 0, sizeof(skb->cb));

	if (WARN_ON(!rdev->cur_cmd_info)) {
		kfree_skb(skb);
		return -EINVAL;
	}

	nla_nest_end(skb, data);
	genlmsg_end(skb, hdr);
	return genlmsg_reply(skb, rdev->cur_cmd_info);
}
EXPORT_SYMBOL_GPL(cfg80211_vendor_cmd_reply);

unsigned int cfg80211_vendor_cmd_get_sender(struct wiphy *wiphy)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	if (WARN_ON(!rdev->cur_cmd_info))
		return 0;

	return rdev->cur_cmd_info->snd_portid;
}
EXPORT_SYMBOL_GPL(cfg80211_vendor_cmd_get_sender);

static int nl80211_set_qos_map(struct sk_buff *skb,
			       struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct cfg80211_qos_map *qos_map = NULL;
	struct net_device *dev = info->user_ptr[1];
	u8 *pos, len, num_des, des_len, des;
	int ret;

	if (!rdev->ops->set_qos_map)
		return -EOPNOTSUPP;

	if (info->attrs[NL80211_ATTR_QOS_MAP]) {
		pos = nla_data(info->attrs[NL80211_ATTR_QOS_MAP]);
		len = nla_len(info->attrs[NL80211_ATTR_QOS_MAP]);

		if (len % 2 || len < IEEE80211_QOS_MAP_LEN_MIN ||
		    len > IEEE80211_QOS_MAP_LEN_MAX)
			return -EINVAL;

		qos_map = kzalloc(sizeof(struct cfg80211_qos_map), GFP_KERNEL);
		if (!qos_map)
			return -ENOMEM;

		num_des = (len - IEEE80211_QOS_MAP_LEN_MIN) >> 1;
		if (num_des) {
			des_len = num_des *
				sizeof(struct cfg80211_dscp_exception);
			memcpy(qos_map->dscp_exception, pos, des_len);
			qos_map->num_des = num_des;
			for (des = 0; des < num_des; des++) {
				if (qos_map->dscp_exception[des].up > 7) {
					kfree(qos_map);
					return -EINVAL;
				}
			}
			pos += des_len;
		}
		memcpy(qos_map->up, pos, IEEE80211_QOS_MAP_LEN_MIN);
	}

	wdev_lock(dev->ieee80211_ptr);
	ret = nl80211_key_allowed(dev->ieee80211_ptr);
	if (!ret)
		ret = rdev_set_qos_map(rdev, dev, qos_map);
	wdev_unlock(dev->ieee80211_ptr);

	kfree(qos_map);
	return ret;
}

static int nl80211_add_tx_ts(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	const u8 *peer;
	u8 tsid, up;
	u16 admitted_time = 0;
	int err;

	if (!(rdev->wiphy.features & NL80211_FEATURE_SUPPORTS_WMM_ADMISSION))
		return -EOPNOTSUPP;

	if (!info->attrs[NL80211_ATTR_TSID] || !info->attrs[NL80211_ATTR_MAC] ||
	    !info->attrs[NL80211_ATTR_USER_PRIO])
		return -EINVAL;

	tsid = nla_get_u8(info->attrs[NL80211_ATTR_TSID]);
	up = nla_get_u8(info->attrs[NL80211_ATTR_USER_PRIO]);

	/* WMM uses TIDs 0-7 even for TSPEC */
	if (tsid >= IEEE80211_FIRST_TSPEC_TSID) {
		/* TODO: handle 802.11 TSPEC/admission control
		 * need more attributes for that (e.g. BA session requirement);
		 * change the WMM adminssion test above to allow both then
		 */
		return -EINVAL;
	}

	peer = nla_data(info->attrs[NL80211_ATTR_MAC]);

	if (info->attrs[NL80211_ATTR_ADMITTED_TIME]) {
		admitted_time =
			nla_get_u16(info->attrs[NL80211_ATTR_ADMITTED_TIME]);
		if (!admitted_time)
			return -EINVAL;
	}

	wdev_lock(wdev);
	switch (wdev->iftype) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		if (wdev->current_bss)
			break;
		err = -ENOTCONN;
		goto out;
	default:
		err = -EOPNOTSUPP;
		goto out;
	}

	err = rdev_add_tx_ts(rdev, dev, tsid, peer, up, admitted_time);

 out:
	wdev_unlock(wdev);
	return err;
}

static int nl80211_del_tx_ts(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	const u8 *peer;
	u8 tsid;
	int err;

	if (!info->attrs[NL80211_ATTR_TSID] || !info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	tsid = nla_get_u8(info->attrs[NL80211_ATTR_TSID]);
	peer = nla_data(info->attrs[NL80211_ATTR_MAC]);

	wdev_lock(wdev);
	err = rdev_del_tx_ts(rdev, dev, tsid, peer);
	wdev_unlock(wdev);

	return err;
}

static int nl80211_tdls_channel_switch(struct sk_buff *skb,
				       struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_chan_def chandef = {};
	const u8 *addr;
	u8 oper_class;
	int err;

	if (!rdev->ops->tdls_channel_switch ||
	    !(rdev->wiphy.features & NL80211_FEATURE_TDLS_CHANNEL_SWITCH))
		return -EOPNOTSUPP;

	switch (dev->ieee80211_ptr->iftype) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (!info->attrs[NL80211_ATTR_MAC] ||
	    !info->attrs[NL80211_ATTR_OPER_CLASS])
		return -EINVAL;

	err = nl80211_parse_chandef(rdev, info, &chandef);
	if (err)
		return err;

	/*
	 * Don't allow wide channels on the 2.4Ghz band, as per IEEE802.11-2012
	 * section 10.22.6.2.1. Disallow 5/10Mhz channels as well for now, the
	 * specification is not defined for them.
	 */
	if (chandef.chan->band == NL80211_BAND_2GHZ &&
	    chandef.width != NL80211_CHAN_WIDTH_20_NOHT &&
	    chandef.width != NL80211_CHAN_WIDTH_20)
		return -EINVAL;

	/* we will be active on the TDLS link */
	if (!cfg80211_reg_can_beacon_relax(&rdev->wiphy, &chandef,
					   wdev->iftype))
		return -EINVAL;

	/* don't allow switching to DFS channels */
	if (cfg80211_chandef_dfs_required(wdev->wiphy, &chandef, wdev->iftype))
		return -EINVAL;

	addr = nla_data(info->attrs[NL80211_ATTR_MAC]);
	oper_class = nla_get_u8(info->attrs[NL80211_ATTR_OPER_CLASS]);

	wdev_lock(wdev);
	err = rdev_tdls_channel_switch(rdev, dev, addr, oper_class, &chandef);
	wdev_unlock(wdev);

	return err;
}

static int nl80211_tdls_cancel_channel_switch(struct sk_buff *skb,
					      struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	const u8 *addr;

	if (!rdev->ops->tdls_channel_switch ||
	    !rdev->ops->tdls_cancel_channel_switch ||
	    !(rdev->wiphy.features & NL80211_FEATURE_TDLS_CHANNEL_SWITCH))
		return -EOPNOTSUPP;

	switch (dev->ieee80211_ptr->iftype) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	addr = nla_data(info->attrs[NL80211_ATTR_MAC]);

	wdev_lock(wdev);
	rdev_tdls_cancel_channel_switch(rdev, dev, addr);
	wdev_unlock(wdev);

	return 0;
}

static int nl80211_set_multicast_to_unicast(struct sk_buff *skb,
					    struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	const struct nlattr *nla;
	bool enabled;

	if (!rdev->ops->set_multicast_to_unicast)
		return -EOPNOTSUPP;

	if (wdev->iftype != NL80211_IFTYPE_AP &&
	    wdev->iftype != NL80211_IFTYPE_P2P_GO)
		return -EOPNOTSUPP;

	nla = info->attrs[NL80211_ATTR_MULTICAST_TO_UNICAST_ENABLED];
	enabled = nla_get_flag(nla);

	return rdev_set_multicast_to_unicast(rdev, dev, enabled);
}

static int nl80211_set_pmk(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_pmk_conf pmk_conf = {};
	int ret;

	if (wdev->iftype != NL80211_IFTYPE_STATION &&
	    wdev->iftype != NL80211_IFTYPE_P2P_CLIENT)
		return -EOPNOTSUPP;

	if (!wiphy_ext_feature_isset(&rdev->wiphy,
				     NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_1X))
		return -EOPNOTSUPP;

	if (!info->attrs[NL80211_ATTR_MAC] || !info->attrs[NL80211_ATTR_PMK])
		return -EINVAL;

	wdev_lock(wdev);
	if (!wdev->current_bss) {
		ret = -ENOTCONN;
		goto out;
	}

	pmk_conf.aa = nla_data(info->attrs[NL80211_ATTR_MAC]);
	if (memcmp(pmk_conf.aa, wdev->current_bss->pub.bssid, ETH_ALEN)) {
		ret = -EINVAL;
		goto out;
	}

	pmk_conf.pmk = nla_data(info->attrs[NL80211_ATTR_PMK]);
	pmk_conf.pmk_len = nla_len(info->attrs[NL80211_ATTR_PMK]);
	if (pmk_conf.pmk_len != WLAN_PMK_LEN &&
	    pmk_conf.pmk_len != WLAN_PMK_LEN_SUITE_B_192) {
		ret = -EINVAL;
		goto out;
	}

	if (info->attrs[NL80211_ATTR_PMKR0_NAME]) {
		int r0_name_len = nla_len(info->attrs[NL80211_ATTR_PMKR0_NAME]);

		if (r0_name_len != WLAN_PMK_NAME_LEN) {
			ret = -EINVAL;
			goto out;
		}

		pmk_conf.pmk_r0_name =
			nla_data(info->attrs[NL80211_ATTR_PMKR0_NAME]);
	}

	ret = rdev_set_pmk(rdev, dev, &pmk_conf);
out:
	wdev_unlock(wdev);
	return ret;
}

static int nl80211_del_pmk(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	const u8 *aa;
	int ret;

	if (wdev->iftype != NL80211_IFTYPE_STATION &&
	    wdev->iftype != NL80211_IFTYPE_P2P_CLIENT)
		return -EOPNOTSUPP;

	if (!wiphy_ext_feature_isset(&rdev->wiphy,
				     NL80211_EXT_FEATURE_4WAY_HANDSHAKE_STA_1X))
		return -EOPNOTSUPP;

	if (!info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	wdev_lock(wdev);
	aa = nla_data(info->attrs[NL80211_ATTR_MAC]);
	ret = rdev_del_pmk(rdev, dev, aa);
	wdev_unlock(wdev);

	return ret;
}

static int nl80211_external_auth(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct cfg80211_external_auth_params params;

	if (!rdev->ops->external_auth)
		return -EOPNOTSUPP;

	if (!info->attrs[NL80211_ATTR_SSID] &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_BSSID])
		return -EINVAL;

	if (!info->attrs[NL80211_ATTR_STATUS_CODE])
		return -EINVAL;

	memset(&params, 0, sizeof(params));

	if (info->attrs[NL80211_ATTR_SSID]) {
		params.ssid.ssid_len = nla_len(info->attrs[NL80211_ATTR_SSID]);
		if (params.ssid.ssid_len == 0 ||
		    params.ssid.ssid_len > IEEE80211_MAX_SSID_LEN)
			return -EINVAL;
		memcpy(params.ssid.ssid,
		       nla_data(info->attrs[NL80211_ATTR_SSID]),
		       params.ssid.ssid_len);
	}

	memcpy(params.bssid, nla_data(info->attrs[NL80211_ATTR_BSSID]),
	       ETH_ALEN);

	params.status = nla_get_u16(info->attrs[NL80211_ATTR_STATUS_CODE]);

	if (info->attrs[NL80211_ATTR_PMKID])
		params.pmkid = nla_data(info->attrs[NL80211_ATTR_PMKID]);

	return rdev_external_auth(rdev, dev, &params);
}

static int nl80211_tx_control_port(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	const u8 *buf;
	size_t len;
	u8 *dest;
	u16 proto;
	bool noencrypt;
	int err;

	if (!wiphy_ext_feature_isset(&rdev->wiphy,
				     NL80211_EXT_FEATURE_CONTROL_PORT_OVER_NL80211))
		return -EOPNOTSUPP;

	if (!rdev->ops->tx_control_port)
		return -EOPNOTSUPP;

	if (!info->attrs[NL80211_ATTR_FRAME] ||
	    !info->attrs[NL80211_ATTR_MAC] ||
	    !info->attrs[NL80211_ATTR_CONTROL_PORT_ETHERTYPE]) {
		GENL_SET_ERR_MSG(info, "Frame, MAC or ethertype missing");
		return -EINVAL;
	}

	wdev_lock(wdev);

	switch (wdev->iftype) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_MESH_POINT:
		break;
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		if (wdev->current_bss)
			break;
		err = -ENOTCONN;
		goto out;
	default:
		err = -EOPNOTSUPP;
		goto out;
	}

	wdev_unlock(wdev);

	buf = nla_data(info->attrs[NL80211_ATTR_FRAME]);
	len = nla_len(info->attrs[NL80211_ATTR_FRAME]);
	dest = nla_data(info->attrs[NL80211_ATTR_MAC]);
	proto = nla_get_u16(info->attrs[NL80211_ATTR_CONTROL_PORT_ETHERTYPE]);
	noencrypt =
		nla_get_flag(info->attrs[NL80211_ATTR_CONTROL_PORT_NO_ENCRYPT]);

	return rdev_tx_control_port(rdev, dev, buf, len,
				    dest, cpu_to_be16(proto), noencrypt);

 out:
	wdev_unlock(wdev);
	return err;
}

static int nl80211_get_ftm_responder_stats(struct sk_buff *skb,
					   struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_ftm_responder_stats ftm_stats = {};
	struct sk_buff *msg;
	void *hdr;
	struct nlattr *ftm_stats_attr;
	int err;

	if (wdev->iftype != NL80211_IFTYPE_AP || !wdev->beacon_interval)
		return -EOPNOTSUPP;

	err = rdev_get_ftm_responder_stats(rdev, dev, &ftm_stats);
	if (err)
		return err;

	if (!ftm_stats.filled)
		return -ENODATA;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = nl80211hdr_put(msg, info->snd_portid, info->snd_seq, 0,
			     NL80211_CMD_GET_FTM_RESPONDER_STATS);
	if (!hdr)
		return -ENOBUFS;

	if (nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex))
		goto nla_put_failure;

	ftm_stats_attr = nla_nest_start_noflag(msg,
					       NL80211_ATTR_FTM_RESPONDER_STATS);
	if (!ftm_stats_attr)
		goto nla_put_failure;

#define SET_FTM(field, name, type)					 \
	do { if ((ftm_stats.filled & BIT(NL80211_FTM_STATS_ ## name)) && \
	    nla_put_ ## type(msg, NL80211_FTM_STATS_ ## name,		 \
			     ftm_stats.field))				 \
		goto nla_put_failure; } while (0)
#define SET_FTM_U64(field, name)					 \
	do { if ((ftm_stats.filled & BIT(NL80211_FTM_STATS_ ## name)) && \
	    nla_put_u64_64bit(msg, NL80211_FTM_STATS_ ## name,		 \
			      ftm_stats.field, NL80211_FTM_STATS_PAD))	 \
		goto nla_put_failure; } while (0)

	SET_FTM(success_num, SUCCESS_NUM, u32);
	SET_FTM(partial_num, PARTIAL_NUM, u32);
	SET_FTM(failed_num, FAILED_NUM, u32);
	SET_FTM(asap_num, ASAP_NUM, u32);
	SET_FTM(non_asap_num, NON_ASAP_NUM, u32);
	SET_FTM_U64(total_duration_ms, TOTAL_DURATION_MSEC);
	SET_FTM(unknown_triggers_num, UNKNOWN_TRIGGERS_NUM, u32);
	SET_FTM(reschedule_requests_num, RESCHEDULE_REQUESTS_NUM, u32);
	SET_FTM(out_of_window_triggers_num, OUT_OF_WINDOW_TRIGGERS_NUM, u32);
#undef SET_FTM

	nla_nest_end(msg, ftm_stats_attr);

	genlmsg_end(msg, hdr);
	return genlmsg_reply(msg, info);

nla_put_failure:
	nlmsg_free(msg);
	return -ENOBUFS;
}

static int nl80211_update_owe_info(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct cfg80211_update_owe_info owe_info;
	struct net_device *dev = info->user_ptr[1];

	if (!rdev->ops->update_owe_info)
		return -EOPNOTSUPP;

	if (!info->attrs[NL80211_ATTR_STATUS_CODE] ||
	    !info->attrs[NL80211_ATTR_MAC])
		return -EINVAL;

	memset(&owe_info, 0, sizeof(owe_info));
	owe_info.status = nla_get_u16(info->attrs[NL80211_ATTR_STATUS_CODE]);
	nla_memcpy(owe_info.peer, info->attrs[NL80211_ATTR_MAC], ETH_ALEN);

	if (info->attrs[NL80211_ATTR_IE]) {
		owe_info.ie = nla_data(info->attrs[NL80211_ATTR_IE]);
		owe_info.ie_len = nla_len(info->attrs[NL80211_ATTR_IE]);
	}

	return rdev_update_owe_info(rdev, dev, &owe_info);
}

static int nl80211_probe_mesh_link(struct sk_buff *skb, struct genl_info *info)
{
	struct cfg80211_registered_device *rdev = info->user_ptr[0];
	struct net_device *dev = info->user_ptr[1];
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct station_info sinfo = {};
	const u8 *buf;
	size_t len;
	u8 *dest;
	int err;

	if (!rdev->ops->probe_mesh_link || !rdev->ops->get_station)
		return -EOPNOTSUPP;

	if (!info->attrs[NL80211_ATTR_MAC] ||
	    !info->attrs[NL80211_ATTR_FRAME]) {
		GENL_SET_ERR_MSG(info, "Frame or MAC missing");
		return -EINVAL;
	}

	if (wdev->iftype != NL80211_IFTYPE_MESH_POINT)
		return -EOPNOTSUPP;

	dest = nla_data(info->attrs[NL80211_ATTR_MAC]);
	buf = nla_data(info->attrs[NL80211_ATTR_FRAME]);
	len = nla_len(info->attrs[NL80211_ATTR_FRAME]);

	if (len < sizeof(struct ethhdr))
		return -EINVAL;

	if (!ether_addr_equal(buf, dest) || is_multicast_ether_addr(buf) ||
	    !ether_addr_equal(buf + ETH_ALEN, dev->dev_addr))
		return -EINVAL;

	err = rdev_get_station(rdev, dev, dest, &sinfo);
	if (err)
		return err;

	return rdev_probe_mesh_link(rdev, dev, dest, buf, len);
}

#define NL80211_FLAG_NEED_WIPHY		0x01
#define NL80211_FLAG_NEED_NETDEV	0x02
#define NL80211_FLAG_NEED_RTNL		0x04
#define NL80211_FLAG_CHECK_NETDEV_UP	0x08
#define NL80211_FLAG_NEED_NETDEV_UP	(NL80211_FLAG_NEED_NETDEV |\
					 NL80211_FLAG_CHECK_NETDEV_UP)
#define NL80211_FLAG_NEED_WDEV		0x10
/* If a netdev is associated, it must be UP, P2P must be started */
#define NL80211_FLAG_NEED_WDEV_UP	(NL80211_FLAG_NEED_WDEV |\
					 NL80211_FLAG_CHECK_NETDEV_UP)
#define NL80211_FLAG_CLEAR_SKB		0x20

static int nl80211_pre_doit(const struct genl_ops *ops, struct sk_buff *skb,
			    struct genl_info *info)
{
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *wdev;
	struct net_device *dev;
	bool rtnl = ops->internal_flags & NL80211_FLAG_NEED_RTNL;

	if (rtnl)
		rtnl_lock();

	if (ops->internal_flags & NL80211_FLAG_NEED_WIPHY) {
		rdev = cfg80211_get_dev_from_info(genl_info_net(info), info);
		if (IS_ERR(rdev)) {
			if (rtnl)
				rtnl_unlock();
			return PTR_ERR(rdev);
		}
		info->user_ptr[0] = rdev;
	} else if (ops->internal_flags & NL80211_FLAG_NEED_NETDEV ||
		   ops->internal_flags & NL80211_FLAG_NEED_WDEV) {
		ASSERT_RTNL();

		wdev = __cfg80211_wdev_from_attrs(genl_info_net(info),
						  info->attrs);
		if (IS_ERR(wdev)) {
			if (rtnl)
				rtnl_unlock();
			return PTR_ERR(wdev);
		}

		dev = wdev->netdev;
		rdev = wiphy_to_rdev(wdev->wiphy);

		if (ops->internal_flags & NL80211_FLAG_NEED_NETDEV) {
			if (!dev) {
				if (rtnl)
					rtnl_unlock();
				return -EINVAL;
			}

			info->user_ptr[1] = dev;
		} else {
			info->user_ptr[1] = wdev;
		}

		if (ops->internal_flags & NL80211_FLAG_CHECK_NETDEV_UP &&
		    !wdev_running(wdev)) {
			if (rtnl)
				rtnl_unlock();
			return -ENETDOWN;
		}

		if (dev)
			dev_hold(dev);

		info->user_ptr[0] = rdev;
	}

	return 0;
}

static void nl80211_post_doit(const struct genl_ops *ops, struct sk_buff *skb,
			      struct genl_info *info)
{
	if (info->user_ptr[1]) {
		if (ops->internal_flags & NL80211_FLAG_NEED_WDEV) {
			struct wireless_dev *wdev = info->user_ptr[1];

			if (wdev->netdev)
				dev_put(wdev->netdev);
		} else {
			dev_put(info->user_ptr[1]);
		}
	}

	if (ops->internal_flags & NL80211_FLAG_NEED_RTNL)
		rtnl_unlock();

	/* If needed, clear the netlink message payload from the SKB
	 * as it might contain key data that shouldn't stick around on
	 * the heap after the SKB is freed. The netlink message header
	 * is still needed for further processing, so leave it intact.
	 */
	if (ops->internal_flags & NL80211_FLAG_CLEAR_SKB) {
		struct nlmsghdr *nlh = nlmsg_hdr(skb);

		memset(nlmsg_data(nlh), 0, nlmsg_len(nlh));
	}
}

static const struct genl_ops nl80211_ops[] = {
	{
		.cmd = NL80211_CMD_GET_WIPHY,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_get_wiphy,
		.dumpit = nl80211_dump_wiphy,
		.done = nl80211_dump_wiphy_done,
		/* can be retrieved by unprivileged users */
		.internal_flags = NL80211_FLAG_NEED_WIPHY |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_WIPHY,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_wiphy,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_GET_INTERFACE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_get_interface,
		.dumpit = nl80211_dump_interface,
		/* can be retrieved by unprivileged users */
		.internal_flags = NL80211_FLAG_NEED_WDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_INTERFACE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_interface,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_NEW_INTERFACE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_new_interface,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WIPHY |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_DEL_INTERFACE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_del_interface,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_GET_KEY,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_get_key,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_KEY,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_key,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL |
				  NL80211_FLAG_CLEAR_SKB,
	},
	{
		.cmd = NL80211_CMD_NEW_KEY,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_new_key,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL |
				  NL80211_FLAG_CLEAR_SKB,
	},
	{
		.cmd = NL80211_CMD_DEL_KEY,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_del_key,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_BEACON,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.flags = GENL_UNS_ADMIN_PERM,
		.doit = nl80211_set_beacon,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_START_AP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.flags = GENL_UNS_ADMIN_PERM,
		.doit = nl80211_start_ap,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_STOP_AP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.flags = GENL_UNS_ADMIN_PERM,
		.doit = nl80211_stop_ap,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_GET_STATION,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_get_station,
		.dumpit = nl80211_dump_station,
		.internal_flags = NL80211_FLAG_NEED_NETDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_STATION,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_station,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_NEW_STATION,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_new_station,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_DEL_STATION,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_del_station,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_GET_MPATH,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_get_mpath,
		.dumpit = nl80211_dump_mpath,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_GET_MPP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_get_mpp,
		.dumpit = nl80211_dump_mpp,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_MPATH,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_mpath,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_NEW_MPATH,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_new_mpath,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_DEL_MPATH,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_del_mpath,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_BSS,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_bss,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_GET_REG,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_get_reg_do,
		.dumpit = nl80211_get_reg_dump,
		.internal_flags = NL80211_FLAG_NEED_RTNL,
		/* can be retrieved by unprivileged users */
	},
#ifdef CONFIG_CFG80211_CRDA_SUPPORT
	{
		.cmd = NL80211_CMD_SET_REG,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_reg,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_RTNL,
	},
#endif
	{
		.cmd = NL80211_CMD_REQ_SET_REG,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_req_set_reg,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_RELOAD_REGDB,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_reload_regdb,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = NL80211_CMD_GET_MESH_CONFIG,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_get_mesh_config,
		/* can be retrieved by unprivileged users */
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_MESH_CONFIG,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_update_mesh_config,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_TRIGGER_SCAN,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_trigger_scan,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_ABORT_SCAN,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_abort_scan,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_GET_SCAN,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.dumpit = nl80211_dump_scan,
	},
	{
		.cmd = NL80211_CMD_START_SCHED_SCAN,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_start_sched_scan,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_STOP_SCHED_SCAN,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_stop_sched_scan,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_AUTHENTICATE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_authenticate,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL |
				  NL80211_FLAG_CLEAR_SKB,
	},
	{
		.cmd = NL80211_CMD_ASSOCIATE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_associate,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL |
				  NL80211_FLAG_CLEAR_SKB,
	},
	{
		.cmd = NL80211_CMD_DEAUTHENTICATE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_deauthenticate,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_DISASSOCIATE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_disassociate,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_JOIN_IBSS,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_join_ibss,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_LEAVE_IBSS,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_leave_ibss,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
#ifdef CONFIG_NL80211_TESTMODE
	{
		.cmd = NL80211_CMD_TESTMODE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_testmode_do,
		.dumpit = nl80211_testmode_dump,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WIPHY |
				  NL80211_FLAG_NEED_RTNL,
	},
#endif
	{
		.cmd = NL80211_CMD_CONNECT,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_connect,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL |
				  NL80211_FLAG_CLEAR_SKB,
	},
	{
		.cmd = NL80211_CMD_UPDATE_CONNECT_PARAMS,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_update_connect_params,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL |
				  NL80211_FLAG_CLEAR_SKB,
	},
	{
		.cmd = NL80211_CMD_DISCONNECT,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_disconnect,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_WIPHY_NETNS,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_wiphy_netns,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WIPHY |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_GET_SURVEY,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.dumpit = nl80211_dump_survey,
	},
	{
		.cmd = NL80211_CMD_SET_PMKSA,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_setdel_pmksa,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL |
				  NL80211_FLAG_CLEAR_SKB,
	},
	{
		.cmd = NL80211_CMD_DEL_PMKSA,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_setdel_pmksa,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_FLUSH_PMKSA,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_flush_pmksa,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_REMAIN_ON_CHANNEL,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_remain_on_channel,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_CANCEL_REMAIN_ON_CHANNEL,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_cancel_remain_on_channel,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_TX_BITRATE_MASK,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_tx_bitrate_mask,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_REGISTER_FRAME,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_register_mgmt,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_FRAME,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_tx_mgmt,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_FRAME_WAIT_CANCEL,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_tx_mgmt_cancel_wait,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_POWER_SAVE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_power_save,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_GET_POWER_SAVE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_get_power_save,
		/* can be retrieved by unprivileged users */
		.internal_flags = NL80211_FLAG_NEED_NETDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_CQM,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_cqm,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_CHANNEL,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_channel,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_WDS_PEER,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_wds_peer,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_JOIN_MESH,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_join_mesh,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_LEAVE_MESH,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_leave_mesh,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_JOIN_OCB,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_join_ocb,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_LEAVE_OCB,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_leave_ocb,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
#ifdef CONFIG_PM
	{
		.cmd = NL80211_CMD_GET_WOWLAN,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_get_wowlan,
		/* can be retrieved by unprivileged users */
		.internal_flags = NL80211_FLAG_NEED_WIPHY |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_WOWLAN,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_wowlan,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WIPHY |
				  NL80211_FLAG_NEED_RTNL,
	},
#endif
	{
		.cmd = NL80211_CMD_SET_REKEY_OFFLOAD,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_rekey_data,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL |
				  NL80211_FLAG_CLEAR_SKB,
	},
	{
		.cmd = NL80211_CMD_TDLS_MGMT,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_tdls_mgmt,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_TDLS_OPER,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_tdls_oper,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_UNEXPECTED_FRAME,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_register_unexpected_frame,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_PROBE_CLIENT,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_probe_client,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_REGISTER_BEACONS,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_register_beacons,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WIPHY |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_NOACK_MAP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_noack_map,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_START_P2P_DEVICE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_start_p2p_device,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_STOP_P2P_DEVICE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_stop_p2p_device,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_START_NAN,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_start_nan,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_STOP_NAN,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_stop_nan,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_ADD_NAN_FUNCTION,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_nan_add_func,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_DEL_NAN_FUNCTION,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_nan_del_func,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_CHANGE_NAN_CONFIG,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_nan_change_config,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_MCAST_RATE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_mcast_rate,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_MAC_ACL,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_mac_acl,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_RADAR_DETECT,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_start_radar_detection,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_GET_PROTOCOL_FEATURES,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_get_protocol_features,
	},
	{
		.cmd = NL80211_CMD_UPDATE_FT_IES,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_update_ft_ies,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_CRIT_PROTOCOL_START,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_crit_protocol_start,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_CRIT_PROTOCOL_STOP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_crit_protocol_stop,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_GET_COALESCE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_get_coalesce,
		.internal_flags = NL80211_FLAG_NEED_WIPHY |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_COALESCE,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_coalesce,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WIPHY |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_CHANNEL_SWITCH,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_channel_switch,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_VENDOR,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_vendor_cmd,
		.dumpit = nl80211_vendor_cmd_dump,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WIPHY |
				  NL80211_FLAG_NEED_RTNL |
				  NL80211_FLAG_CLEAR_SKB,
	},
	{
		.cmd = NL80211_CMD_SET_QOS_MAP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_qos_map,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_ADD_TX_TS,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_add_tx_ts,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_DEL_TX_TS,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_del_tx_ts,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_TDLS_CHANNEL_SWITCH,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_tdls_channel_switch,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_TDLS_CANCEL_CHANNEL_SWITCH,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_tdls_cancel_channel_switch,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_MULTICAST_TO_UNICAST,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_multicast_to_unicast,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_SET_PMK,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_set_pmk,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL |
				  NL80211_FLAG_CLEAR_SKB,
	},
	{
		.cmd = NL80211_CMD_DEL_PMK,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_del_pmk,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_EXTERNAL_AUTH,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_external_auth,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_CONTROL_PORT_FRAME,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_tx_control_port,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_GET_FTM_RESPONDER_STATS,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_get_ftm_responder_stats,
		.internal_flags = NL80211_FLAG_NEED_NETDEV |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_PEER_MEASUREMENT_START,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_pmsr_start,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_WDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_NOTIFY_RADAR,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = nl80211_notify_radar_detection,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_UPDATE_OWE_INFO,
		.doit = nl80211_update_owe_info,
		.flags = GENL_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
	{
		.cmd = NL80211_CMD_PROBE_MESH_LINK,
		.doit = nl80211_probe_mesh_link,
		.flags = GENL_UNS_ADMIN_PERM,
		.internal_flags = NL80211_FLAG_NEED_NETDEV_UP |
				  NL80211_FLAG_NEED_RTNL,
	},
};

static struct genl_family nl80211_fam __ro_after_init = {
	.name = NL80211_GENL_NAME,	/* have users key off the name instead */
	.hdrsize = 0,			/* no private header */
	.version = 1,			/* no particular meaning now */
	.maxattr = NL80211_ATTR_MAX,
	.policy = nl80211_policy,
	.netnsok = true,
	.pre_doit = nl80211_pre_doit,
	.post_doit = nl80211_post_doit,
	.module = THIS_MODULE,
	.ops = nl80211_ops,
	.n_ops = ARRAY_SIZE(nl80211_ops),
	.mcgrps = nl80211_mcgrps,
	.n_mcgrps = ARRAY_SIZE(nl80211_mcgrps),
};

/* notification functions */

void nl80211_notify_wiphy(struct cfg80211_registered_device *rdev,
			  enum nl80211_commands cmd)
{
	struct sk_buff *msg;
	struct nl80211_dump_wiphy_state state = {};

	WARN_ON(cmd != NL80211_CMD_NEW_WIPHY &&
		cmd != NL80211_CMD_DEL_WIPHY);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	if (nl80211_send_wiphy(rdev, cmd, msg, 0, 0, 0, &state) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_CONFIG, GFP_KERNEL);
}

void nl80211_notify_iface(struct cfg80211_registered_device *rdev,
				struct wireless_dev *wdev,
				enum nl80211_commands cmd)
{
	struct sk_buff *msg;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	if (nl80211_send_iface(msg, 0, 0, 0, rdev, wdev, cmd) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_CONFIG, GFP_KERNEL);
}

static int nl80211_add_scan_req(struct sk_buff *msg,
				struct cfg80211_registered_device *rdev)
{
	struct cfg80211_scan_request *req = rdev->scan_req;
	struct nlattr *nest;
	int i;

	if (WARN_ON(!req))
		return 0;

	nest = nla_nest_start_noflag(msg, NL80211_ATTR_SCAN_SSIDS);
	if (!nest)
		goto nla_put_failure;
	for (i = 0; i < req->n_ssids; i++) {
		if (nla_put(msg, i, req->ssids[i].ssid_len, req->ssids[i].ssid))
			goto nla_put_failure;
	}
	nla_nest_end(msg, nest);

	nest = nla_nest_start_noflag(msg, NL80211_ATTR_SCAN_FREQUENCIES);
	if (!nest)
		goto nla_put_failure;
	for (i = 0; i < req->n_channels; i++) {
		if (nla_put_u32(msg, i, req->channels[i]->center_freq))
			goto nla_put_failure;
	}
	nla_nest_end(msg, nest);

	if (req->ie &&
	    nla_put(msg, NL80211_ATTR_IE, req->ie_len, req->ie))
		goto nla_put_failure;

	if (req->flags &&
	    nla_put_u32(msg, NL80211_ATTR_SCAN_FLAGS, req->flags))
		goto nla_put_failure;

	if (req->info.scan_start_tsf &&
	    (nla_put_u64_64bit(msg, NL80211_ATTR_SCAN_START_TIME_TSF,
			       req->info.scan_start_tsf, NL80211_BSS_PAD) ||
	     nla_put(msg, NL80211_ATTR_SCAN_START_TIME_TSF_BSSID, ETH_ALEN,
		     req->info.tsf_bssid)))
		goto nla_put_failure;

	return 0;
 nla_put_failure:
	return -ENOBUFS;
}

static int nl80211_prep_scan_msg(struct sk_buff *msg,
				 struct cfg80211_registered_device *rdev,
				 struct wireless_dev *wdev,
				 u32 portid, u32 seq, int flags,
				 u32 cmd)
{
	void *hdr;

	hdr = nl80211hdr_put(msg, portid, seq, flags, cmd);
	if (!hdr)
		return -1;

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    (wdev->netdev && nla_put_u32(msg, NL80211_ATTR_IFINDEX,
					 wdev->netdev->ifindex)) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_WDEV, wdev_id(wdev),
			      NL80211_ATTR_PAD))
		goto nla_put_failure;

	/* ignore errors and send incomplete event anyway */
	nl80211_add_scan_req(msg, rdev);

	genlmsg_end(msg, hdr);
	return 0;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int
nl80211_prep_sched_scan_msg(struct sk_buff *msg,
			    struct cfg80211_sched_scan_request *req, u32 cmd)
{
	void *hdr;

	hdr = nl80211hdr_put(msg, 0, 0, 0, cmd);
	if (!hdr)
		return -1;

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY,
			wiphy_to_rdev(req->wiphy)->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, req->dev->ifindex) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_COOKIE, req->reqid,
			      NL80211_ATTR_PAD))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	return 0;

 nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

void nl80211_send_scan_start(struct cfg80211_registered_device *rdev,
			     struct wireless_dev *wdev)
{
	struct sk_buff *msg;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	if (nl80211_prep_scan_msg(msg, rdev, wdev, 0, 0, 0,
				  NL80211_CMD_TRIGGER_SCAN) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_SCAN, GFP_KERNEL);
}

struct sk_buff *nl80211_build_scan_msg(struct cfg80211_registered_device *rdev,
				       struct wireless_dev *wdev, bool aborted)
{
	struct sk_buff *msg;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return NULL;

	if (nl80211_prep_scan_msg(msg, rdev, wdev, 0, 0, 0,
				  aborted ? NL80211_CMD_SCAN_ABORTED :
					    NL80211_CMD_NEW_SCAN_RESULTS) < 0) {
		nlmsg_free(msg);
		return NULL;
	}

	return msg;
}

/* send message created by nl80211_build_scan_msg() */
void nl80211_send_scan_msg(struct cfg80211_registered_device *rdev,
			   struct sk_buff *msg)
{
	if (!msg)
		return;

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_SCAN, GFP_KERNEL);
}

void nl80211_send_sched_scan(struct cfg80211_sched_scan_request *req, u32 cmd)
{
	struct sk_buff *msg;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	if (nl80211_prep_sched_scan_msg(msg, req, cmd) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(req->wiphy), msg, 0,
				NL80211_MCGRP_SCAN, GFP_KERNEL);
}

static bool nl80211_reg_change_event_fill(struct sk_buff *msg,
					  struct regulatory_request *request)
{
	/* Userspace can always count this one always being set */
	if (nla_put_u8(msg, NL80211_ATTR_REG_INITIATOR, request->initiator))
		goto nla_put_failure;

	if (request->alpha2[0] == '0' && request->alpha2[1] == '0') {
		if (nla_put_u8(msg, NL80211_ATTR_REG_TYPE,
			       NL80211_REGDOM_TYPE_WORLD))
			goto nla_put_failure;
	} else if (request->alpha2[0] == '9' && request->alpha2[1] == '9') {
		if (nla_put_u8(msg, NL80211_ATTR_REG_TYPE,
			       NL80211_REGDOM_TYPE_CUSTOM_WORLD))
			goto nla_put_failure;
	} else if ((request->alpha2[0] == '9' && request->alpha2[1] == '8') ||
		   request->intersect) {
		if (nla_put_u8(msg, NL80211_ATTR_REG_TYPE,
			       NL80211_REGDOM_TYPE_INTERSECTION))
			goto nla_put_failure;
	} else {
		if (nla_put_u8(msg, NL80211_ATTR_REG_TYPE,
			       NL80211_REGDOM_TYPE_COUNTRY) ||
		    nla_put_string(msg, NL80211_ATTR_REG_ALPHA2,
				   request->alpha2))
			goto nla_put_failure;
	}

	if (request->wiphy_idx != WIPHY_IDX_INVALID) {
		struct wiphy *wiphy = wiphy_idx_to_wiphy(request->wiphy_idx);

		if (wiphy &&
		    nla_put_u32(msg, NL80211_ATTR_WIPHY, request->wiphy_idx))
			goto nla_put_failure;

		if (wiphy &&
		    wiphy->regulatory_flags & REGULATORY_WIPHY_SELF_MANAGED &&
		    nla_put_flag(msg, NL80211_ATTR_WIPHY_SELF_MANAGED_REG))
			goto nla_put_failure;
	}

	return true;

nla_put_failure:
	return false;
}

/*
 * This can happen on global regulatory changes or device specific settings
 * based on custom regulatory domains.
 */
void nl80211_common_reg_change_event(enum nl80211_commands cmd_id,
				     struct regulatory_request *request)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, cmd_id);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nl80211_reg_change_event_fill(msg, request) == false)
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	rcu_read_lock();
	genlmsg_multicast_allns(&nl80211_fam, msg, 0,
				NL80211_MCGRP_REGULATORY, GFP_ATOMIC);
	rcu_read_unlock();

	return;

nla_put_failure:
	nlmsg_free(msg);
}

static void nl80211_send_mlme_event(struct cfg80211_registered_device *rdev,
				    struct net_device *netdev,
				    const u8 *buf, size_t len,
				    enum nl80211_commands cmd, gfp_t gfp,
				    int uapsd_queues, const u8 *req_ies,
				    size_t req_ies_len)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(100 + len + req_ies_len, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, cmd);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex) ||
	    nla_put(msg, NL80211_ATTR_FRAME, len, buf) ||
	    (req_ies &&
	     nla_put(msg, NL80211_ATTR_REQ_IE, req_ies_len, req_ies)))
		goto nla_put_failure;

	if (uapsd_queues >= 0) {
		struct nlattr *nla_wmm =
			nla_nest_start_noflag(msg, NL80211_ATTR_STA_WME);
		if (!nla_wmm)
			goto nla_put_failure;

		if (nla_put_u8(msg, NL80211_STA_WME_UAPSD_QUEUES,
			       uapsd_queues))
			goto nla_put_failure;

		nla_nest_end(msg, nla_wmm);
	}

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}

void nl80211_send_rx_auth(struct cfg80211_registered_device *rdev,
			  struct net_device *netdev, const u8 *buf,
			  size_t len, gfp_t gfp)
{
	nl80211_send_mlme_event(rdev, netdev, buf, len,
				NL80211_CMD_AUTHENTICATE, gfp, -1, NULL, 0);
}

void nl80211_send_rx_assoc(struct cfg80211_registered_device *rdev,
			   struct net_device *netdev, const u8 *buf,
			   size_t len, gfp_t gfp, int uapsd_queues,
			   const u8 *req_ies, size_t req_ies_len)
{
	nl80211_send_mlme_event(rdev, netdev, buf, len,
				NL80211_CMD_ASSOCIATE, gfp, uapsd_queues,
				req_ies, req_ies_len);
}

void nl80211_send_deauth(struct cfg80211_registered_device *rdev,
			 struct net_device *netdev, const u8 *buf,
			 size_t len, gfp_t gfp)
{
	nl80211_send_mlme_event(rdev, netdev, buf, len,
				NL80211_CMD_DEAUTHENTICATE, gfp, -1, NULL, 0);
}

void nl80211_send_disassoc(struct cfg80211_registered_device *rdev,
			   struct net_device *netdev, const u8 *buf,
			   size_t len, gfp_t gfp)
{
	nl80211_send_mlme_event(rdev, netdev, buf, len,
				NL80211_CMD_DISASSOCIATE, gfp, -1, NULL, 0);
}

void cfg80211_rx_unprot_mlme_mgmt(struct net_device *dev, const u8 *buf,
				  size_t len)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	const struct ieee80211_mgmt *mgmt = (void *)buf;
	u32 cmd;

	if (WARN_ON(len < 2))
		return;

	if (ieee80211_is_deauth(mgmt->frame_control))
		cmd = NL80211_CMD_UNPROT_DEAUTHENTICATE;
	else
		cmd = NL80211_CMD_UNPROT_DISASSOCIATE;

	trace_cfg80211_rx_unprot_mlme_mgmt(dev, buf, len);
	nl80211_send_mlme_event(rdev, dev, buf, len, cmd, GFP_ATOMIC, -1,
				NULL, 0);
}
EXPORT_SYMBOL(cfg80211_rx_unprot_mlme_mgmt);

static void nl80211_send_mlme_timeout(struct cfg80211_registered_device *rdev,
				      struct net_device *netdev, int cmd,
				      const u8 *addr, gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, cmd);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex) ||
	    nla_put_flag(msg, NL80211_ATTR_TIMED_OUT) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, addr))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}

void nl80211_send_auth_timeout(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev, const u8 *addr,
			       gfp_t gfp)
{
	nl80211_send_mlme_timeout(rdev, netdev, NL80211_CMD_AUTHENTICATE,
				  addr, gfp);
}

void nl80211_send_assoc_timeout(struct cfg80211_registered_device *rdev,
				struct net_device *netdev, const u8 *addr,
				gfp_t gfp)
{
	nl80211_send_mlme_timeout(rdev, netdev, NL80211_CMD_ASSOCIATE,
				  addr, gfp);
}

void nl80211_send_connect_result(struct cfg80211_registered_device *rdev,
				 struct net_device *netdev,
				 struct cfg80211_connect_resp_params *cr,
				 gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(100 + cr->req_ie_len + cr->resp_ie_len +
			cr->fils.kek_len + cr->fils.pmk_len +
			(cr->fils.pmkid ? WLAN_PMKID_LEN : 0), gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_CONNECT);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex) ||
	    (cr->bssid &&
	     nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, cr->bssid)) ||
	    nla_put_u16(msg, NL80211_ATTR_STATUS_CODE,
			cr->status < 0 ? WLAN_STATUS_UNSPECIFIED_FAILURE :
			cr->status) ||
	    (cr->status < 0 &&
	     (nla_put_flag(msg, NL80211_ATTR_TIMED_OUT) ||
	      nla_put_u32(msg, NL80211_ATTR_TIMEOUT_REASON,
			  cr->timeout_reason))) ||
	    (cr->req_ie &&
	     nla_put(msg, NL80211_ATTR_REQ_IE, cr->req_ie_len, cr->req_ie)) ||
	    (cr->resp_ie &&
	     nla_put(msg, NL80211_ATTR_RESP_IE, cr->resp_ie_len,
		     cr->resp_ie)) ||
	    (cr->fils.update_erp_next_seq_num &&
	     nla_put_u16(msg, NL80211_ATTR_FILS_ERP_NEXT_SEQ_NUM,
			 cr->fils.erp_next_seq_num)) ||
	    (cr->status == WLAN_STATUS_SUCCESS &&
	     ((cr->fils.kek &&
	       nla_put(msg, NL80211_ATTR_FILS_KEK, cr->fils.kek_len,
		       cr->fils.kek)) ||
	      (cr->fils.pmk &&
	       nla_put(msg, NL80211_ATTR_PMK, cr->fils.pmk_len, cr->fils.pmk)) ||
	      (cr->fils.pmkid &&
	       nla_put(msg, NL80211_ATTR_PMKID, WLAN_PMKID_LEN, cr->fils.pmkid)))))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}

void nl80211_send_roamed(struct cfg80211_registered_device *rdev,
			 struct net_device *netdev,
			 struct cfg80211_roam_info *info, gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;
	const u8 *bssid = info->bss ? info->bss->bssid : info->bssid;

	msg = nlmsg_new(100 + info->req_ie_len + info->resp_ie_len +
			info->fils.kek_len + info->fils.pmk_len +
			(info->fils.pmkid ? WLAN_PMKID_LEN : 0), gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_ROAM);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, bssid) ||
	    (info->req_ie &&
	     nla_put(msg, NL80211_ATTR_REQ_IE, info->req_ie_len,
		     info->req_ie)) ||
	    (info->resp_ie &&
	     nla_put(msg, NL80211_ATTR_RESP_IE, info->resp_ie_len,
		     info->resp_ie)) ||
	    (info->fils.update_erp_next_seq_num &&
	     nla_put_u16(msg, NL80211_ATTR_FILS_ERP_NEXT_SEQ_NUM,
			 info->fils.erp_next_seq_num)) ||
	    (info->fils.kek &&
	     nla_put(msg, NL80211_ATTR_FILS_KEK, info->fils.kek_len,
		     info->fils.kek)) ||
	    (info->fils.pmk &&
	     nla_put(msg, NL80211_ATTR_PMK, info->fils.pmk_len, info->fils.pmk)) ||
	    (info->fils.pmkid &&
	     nla_put(msg, NL80211_ATTR_PMKID, WLAN_PMKID_LEN, info->fils.pmkid)))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}

void nl80211_send_port_authorized(struct cfg80211_registered_device *rdev,
				  struct net_device *netdev, const u8 *bssid)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_PORT_AUTHORIZED);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, bssid))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, GFP_KERNEL);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}

void nl80211_send_disconnected(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev, u16 reason,
			       const u8 *ie, size_t ie_len, bool from_ap)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(100 + ie_len, GFP_KERNEL);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_DISCONNECT);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex) ||
	    (reason &&
	     nla_put_u16(msg, NL80211_ATTR_REASON_CODE, reason)) ||
	    (from_ap &&
	     nla_put_flag(msg, NL80211_ATTR_DISCONNECTED_BY_AP)) ||
	    (ie && nla_put(msg, NL80211_ATTR_IE, ie_len, ie)))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, GFP_KERNEL);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}

void nl80211_send_ibss_bssid(struct cfg80211_registered_device *rdev,
			     struct net_device *netdev, const u8 *bssid,
			     gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_JOIN_IBSS);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, bssid))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}

void cfg80211_notify_new_peer_candidate(struct net_device *dev, const u8 *addr,
					const u8 *ie, u8 ie_len,
					int sig_dbm, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct sk_buff *msg;
	void *hdr;

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_MESH_POINT))
		return;

	trace_cfg80211_notify_new_peer_candidate(dev, addr);

	msg = nlmsg_new(100 + ie_len, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_NEW_PEER_CANDIDATE);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, addr) ||
	    (ie_len && ie &&
	     nla_put(msg, NL80211_ATTR_IE, ie_len, ie)) ||
	    (sig_dbm &&
	     nla_put_u32(msg, NL80211_ATTR_RX_SIGNAL_DBM, sig_dbm)))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_notify_new_peer_candidate);

void nl80211_michael_mic_failure(struct cfg80211_registered_device *rdev,
				 struct net_device *netdev, const u8 *addr,
				 enum nl80211_key_type key_type, int key_id,
				 const u8 *tsc, gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_MICHAEL_MIC_FAILURE);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex) ||
	    (addr && nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, addr)) ||
	    nla_put_u32(msg, NL80211_ATTR_KEY_TYPE, key_type) ||
	    (key_id != -1 &&
	     nla_put_u8(msg, NL80211_ATTR_KEY_IDX, key_id)) ||
	    (tsc && nla_put(msg, NL80211_ATTR_KEY_SEQ, 6, tsc)))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}

void nl80211_send_beacon_hint_event(struct wiphy *wiphy,
				    struct ieee80211_channel *channel_before,
				    struct ieee80211_channel *channel_after)
{
	struct sk_buff *msg;
	void *hdr;
	struct nlattr *nl_freq;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_REG_BEACON_HINT);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	/*
	 * Since we are applying the beacon hint to a wiphy we know its
	 * wiphy_idx is valid
	 */
	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, get_wiphy_idx(wiphy)))
		goto nla_put_failure;

	/* Before */
	nl_freq = nla_nest_start_noflag(msg, NL80211_ATTR_FREQ_BEFORE);
	if (!nl_freq)
		goto nla_put_failure;

	if (nl80211_msg_put_channel(msg, wiphy, channel_before, false))
		goto nla_put_failure;
	nla_nest_end(msg, nl_freq);

	/* After */
	nl_freq = nla_nest_start_noflag(msg, NL80211_ATTR_FREQ_AFTER);
	if (!nl_freq)
		goto nla_put_failure;

	if (nl80211_msg_put_channel(msg, wiphy, channel_after, false))
		goto nla_put_failure;
	nla_nest_end(msg, nl_freq);

	genlmsg_end(msg, hdr);

	rcu_read_lock();
	genlmsg_multicast_allns(&nl80211_fam, msg, 0,
				NL80211_MCGRP_REGULATORY, GFP_ATOMIC);
	rcu_read_unlock();

	return;

nla_put_failure:
	nlmsg_free(msg);
}

static void nl80211_send_remain_on_chan_event(
	int cmd, struct cfg80211_registered_device *rdev,
	struct wireless_dev *wdev, u64 cookie,
	struct ieee80211_channel *chan,
	unsigned int duration, gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, cmd);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    (wdev->netdev && nla_put_u32(msg, NL80211_ATTR_IFINDEX,
					 wdev->netdev->ifindex)) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_WDEV, wdev_id(wdev),
			      NL80211_ATTR_PAD) ||
	    nla_put_u32(msg, NL80211_ATTR_WIPHY_FREQ, chan->center_freq) ||
	    nla_put_u32(msg, NL80211_ATTR_WIPHY_CHANNEL_TYPE,
			NL80211_CHAN_NO_HT) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_COOKIE, cookie,
			      NL80211_ATTR_PAD))
		goto nla_put_failure;

	if (cmd == NL80211_CMD_REMAIN_ON_CHANNEL &&
	    nla_put_u32(msg, NL80211_ATTR_DURATION, duration))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}

void cfg80211_ready_on_channel(struct wireless_dev *wdev, u64 cookie,
			       struct ieee80211_channel *chan,
			       unsigned int duration, gfp_t gfp)
{
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	trace_cfg80211_ready_on_channel(wdev, cookie, chan, duration);
	nl80211_send_remain_on_chan_event(NL80211_CMD_REMAIN_ON_CHANNEL,
					  rdev, wdev, cookie, chan,
					  duration, gfp);
}
EXPORT_SYMBOL(cfg80211_ready_on_channel);

void cfg80211_remain_on_channel_expired(struct wireless_dev *wdev, u64 cookie,
					struct ieee80211_channel *chan,
					gfp_t gfp)
{
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	trace_cfg80211_ready_on_channel_expired(wdev, cookie, chan);
	nl80211_send_remain_on_chan_event(NL80211_CMD_CANCEL_REMAIN_ON_CHANNEL,
					  rdev, wdev, cookie, chan, 0, gfp);
}
EXPORT_SYMBOL(cfg80211_remain_on_channel_expired);

void cfg80211_new_sta(struct net_device *dev, const u8 *mac_addr,
		      struct station_info *sinfo, gfp_t gfp)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct sk_buff *msg;

	trace_cfg80211_new_sta(dev, mac_addr, sinfo);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	if (nl80211_send_station(msg, NL80211_CMD_NEW_STATION, 0, 0, 0,
				 rdev, dev, mac_addr, sinfo) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
}
EXPORT_SYMBOL(cfg80211_new_sta);

void cfg80211_del_sta_sinfo(struct net_device *dev, const u8 *mac_addr,
			    struct station_info *sinfo, gfp_t gfp)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct sk_buff *msg;
	struct station_info empty_sinfo = {};

	if (!sinfo)
		sinfo = &empty_sinfo;

	trace_cfg80211_del_sta(dev, mac_addr);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg) {
		cfg80211_sinfo_release_content(sinfo);
		return;
	}

	if (nl80211_send_station(msg, NL80211_CMD_DEL_STATION, 0, 0, 0,
				 rdev, dev, mac_addr, sinfo) < 0) {
		nlmsg_free(msg);
		return;
	}

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
}
EXPORT_SYMBOL(cfg80211_del_sta_sinfo);

void cfg80211_conn_failed(struct net_device *dev, const u8 *mac_addr,
			  enum nl80211_connect_failed_reason reason,
			  gfp_t gfp)
{
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_GOODSIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_CONN_FAILED);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, mac_addr) ||
	    nla_put_u32(msg, NL80211_ATTR_CONN_FAILED_REASON, reason))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_conn_failed);

static bool __nl80211_unexpected_frame(struct net_device *dev, u8 cmd,
				       const u8 *addr, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct sk_buff *msg;
	void *hdr;
	u32 nlportid = READ_ONCE(wdev->ap_unexpected_nlportid);

	if (!nlportid)
		return false;

	msg = nlmsg_new(100, gfp);
	if (!msg)
		return true;

	hdr = nl80211hdr_put(msg, 0, 0, 0, cmd);
	if (!hdr) {
		nlmsg_free(msg);
		return true;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, addr))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	genlmsg_unicast(wiphy_net(&rdev->wiphy), msg, nlportid);
	return true;

 nla_put_failure:
	nlmsg_free(msg);
	return true;
}

bool cfg80211_rx_spurious_frame(struct net_device *dev,
				const u8 *addr, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	bool ret;

	trace_cfg80211_rx_spurious_frame(dev, addr);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_AP &&
		    wdev->iftype != NL80211_IFTYPE_P2P_GO)) {
		trace_cfg80211_return_bool(false);
		return false;
	}
	ret = __nl80211_unexpected_frame(dev, NL80211_CMD_UNEXPECTED_FRAME,
					 addr, gfp);
	trace_cfg80211_return_bool(ret);
	return ret;
}
EXPORT_SYMBOL(cfg80211_rx_spurious_frame);

bool cfg80211_rx_unexpected_4addr_frame(struct net_device *dev,
					const u8 *addr, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	bool ret;

	trace_cfg80211_rx_unexpected_4addr_frame(dev, addr);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_AP &&
		    wdev->iftype != NL80211_IFTYPE_P2P_GO &&
		    wdev->iftype != NL80211_IFTYPE_AP_VLAN)) {
		trace_cfg80211_return_bool(false);
		return false;
	}
	ret = __nl80211_unexpected_frame(dev,
					 NL80211_CMD_UNEXPECTED_4ADDR_FRAME,
					 addr, gfp);
	trace_cfg80211_return_bool(ret);
	return ret;
}
EXPORT_SYMBOL(cfg80211_rx_unexpected_4addr_frame);

int nl80211_send_mgmt(struct cfg80211_registered_device *rdev,
		      struct wireless_dev *wdev, u32 nlportid,
		      int freq, int sig_dbm,
		      const u8 *buf, size_t len, u32 flags, gfp_t gfp)
{
	struct net_device *netdev = wdev->netdev;
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(100 + len, gfp);
	if (!msg)
		return -ENOMEM;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_FRAME);
	if (!hdr) {
		nlmsg_free(msg);
		return -ENOMEM;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    (netdev && nla_put_u32(msg, NL80211_ATTR_IFINDEX,
					netdev->ifindex)) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_WDEV, wdev_id(wdev),
			      NL80211_ATTR_PAD) ||
	    nla_put_u32(msg, NL80211_ATTR_WIPHY_FREQ, freq) ||
	    (sig_dbm &&
	     nla_put_u32(msg, NL80211_ATTR_RX_SIGNAL_DBM, sig_dbm)) ||
	    nla_put(msg, NL80211_ATTR_FRAME, len, buf) ||
	    (flags &&
	     nla_put_u32(msg, NL80211_ATTR_RXMGMT_FLAGS, flags)))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return genlmsg_unicast(wiphy_net(&rdev->wiphy), msg, nlportid);

 nla_put_failure:
	nlmsg_free(msg);
	return -ENOBUFS;
}

void cfg80211_mgmt_tx_status(struct wireless_dev *wdev, u64 cookie,
			     const u8 *buf, size_t len, bool ack, gfp_t gfp)
{
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct net_device *netdev = wdev->netdev;
	struct sk_buff *msg;
	void *hdr;

	trace_cfg80211_mgmt_tx_status(wdev, cookie, ack);

	msg = nlmsg_new(100 + len, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_FRAME_TX_STATUS);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    (netdev && nla_put_u32(msg, NL80211_ATTR_IFINDEX,
				   netdev->ifindex)) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_WDEV, wdev_id(wdev),
			      NL80211_ATTR_PAD) ||
	    nla_put(msg, NL80211_ATTR_FRAME, len, buf) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_COOKIE, cookie,
			      NL80211_ATTR_PAD) ||
	    (ack && nla_put_flag(msg, NL80211_ATTR_ACK)))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_mgmt_tx_status);

static int __nl80211_rx_control_port(struct net_device *dev,
				     struct sk_buff *skb,
				     bool unencrypted, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct ethhdr *ehdr = eth_hdr(skb);
	const u8 *addr = ehdr->h_source;
	u16 proto = be16_to_cpu(skb->protocol);
	struct sk_buff *msg;
	void *hdr;
	struct nlattr *frame;

	u32 nlportid = READ_ONCE(wdev->conn_owner_nlportid);

	if (!nlportid)
		return -ENOENT;

	msg = nlmsg_new(100 + skb->len, gfp);
	if (!msg)
		return -ENOMEM;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_CONTROL_PORT_FRAME);
	if (!hdr) {
		nlmsg_free(msg);
		return -ENOBUFS;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_WDEV, wdev_id(wdev),
			      NL80211_ATTR_PAD) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, addr) ||
	    nla_put_u16(msg, NL80211_ATTR_CONTROL_PORT_ETHERTYPE, proto) ||
	    (unencrypted && nla_put_flag(msg,
					 NL80211_ATTR_CONTROL_PORT_NO_ENCRYPT)))
		goto nla_put_failure;

	frame = nla_reserve(msg, NL80211_ATTR_FRAME, skb->len);
	if (!frame)
		goto nla_put_failure;

	skb_copy_bits(skb, 0, nla_data(frame), skb->len);
	genlmsg_end(msg, hdr);

	return genlmsg_unicast(wiphy_net(&rdev->wiphy), msg, nlportid);

 nla_put_failure:
	nlmsg_free(msg);
	return -ENOBUFS;
}

bool cfg80211_rx_control_port(struct net_device *dev,
			      struct sk_buff *skb, bool unencrypted)
{
	int ret;

	trace_cfg80211_rx_control_port(dev, skb, unencrypted);
	ret = __nl80211_rx_control_port(dev, skb, unencrypted, GFP_ATOMIC);
	trace_cfg80211_return_bool(ret == 0);
	return ret == 0;
}
EXPORT_SYMBOL(cfg80211_rx_control_port);

static struct sk_buff *cfg80211_prepare_cqm(struct net_device *dev,
					    const char *mac, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct sk_buff *msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	void **cb;

	if (!msg)
		return NULL;

	cb = (void **)msg->cb;

	cb[0] = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_NOTIFY_CQM);
	if (!cb[0]) {
		nlmsg_free(msg);
		return NULL;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex))
		goto nla_put_failure;

	if (mac && nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, mac))
		goto nla_put_failure;

	cb[1] = nla_nest_start_noflag(msg, NL80211_ATTR_CQM);
	if (!cb[1])
		goto nla_put_failure;

	cb[2] = rdev;

	return msg;
 nla_put_failure:
	nlmsg_free(msg);
	return NULL;
}

static void cfg80211_send_cqm(struct sk_buff *msg, gfp_t gfp)
{
	void **cb = (void **)msg->cb;
	struct cfg80211_registered_device *rdev = cb[2];

	nla_nest_end(msg, cb[1]);
	genlmsg_end(msg, cb[0]);

	memset(msg->cb, 0, sizeof(msg->cb));

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
}

void cfg80211_cqm_rssi_notify(struct net_device *dev,
			      enum nl80211_cqm_rssi_threshold_event rssi_event,
			      s32 rssi_level, gfp_t gfp)
{
	struct sk_buff *msg;
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);

	trace_cfg80211_cqm_rssi_notify(dev, rssi_event, rssi_level);

	if (WARN_ON(rssi_event != NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW &&
		    rssi_event != NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH))
		return;

	if (wdev->cqm_config) {
		wdev->cqm_config->last_rssi_event_value = rssi_level;

		cfg80211_cqm_rssi_update(rdev, dev);

		if (rssi_level == 0)
			rssi_level = wdev->cqm_config->last_rssi_event_value;
	}

	msg = cfg80211_prepare_cqm(dev, NULL, gfp);
	if (!msg)
		return;

	if (nla_put_u32(msg, NL80211_ATTR_CQM_RSSI_THRESHOLD_EVENT,
			rssi_event))
		goto nla_put_failure;

	if (rssi_level && nla_put_s32(msg, NL80211_ATTR_CQM_RSSI_LEVEL,
				      rssi_level))
		goto nla_put_failure;

	cfg80211_send_cqm(msg, gfp);

	return;

 nla_put_failure:
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_cqm_rssi_notify);

void cfg80211_cqm_txe_notify(struct net_device *dev,
			     const u8 *peer, u32 num_packets,
			     u32 rate, u32 intvl, gfp_t gfp)
{
	struct sk_buff *msg;

	msg = cfg80211_prepare_cqm(dev, peer, gfp);
	if (!msg)
		return;

	if (nla_put_u32(msg, NL80211_ATTR_CQM_TXE_PKTS, num_packets))
		goto nla_put_failure;

	if (nla_put_u32(msg, NL80211_ATTR_CQM_TXE_RATE, rate))
		goto nla_put_failure;

	if (nla_put_u32(msg, NL80211_ATTR_CQM_TXE_INTVL, intvl))
		goto nla_put_failure;

	cfg80211_send_cqm(msg, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_cqm_txe_notify);

void cfg80211_cqm_pktloss_notify(struct net_device *dev,
				 const u8 *peer, u32 num_packets, gfp_t gfp)
{
	struct sk_buff *msg;

	trace_cfg80211_cqm_pktloss_notify(dev, peer, num_packets);

	msg = cfg80211_prepare_cqm(dev, peer, gfp);
	if (!msg)
		return;

	if (nla_put_u32(msg, NL80211_ATTR_CQM_PKT_LOSS_EVENT, num_packets))
		goto nla_put_failure;

	cfg80211_send_cqm(msg, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_cqm_pktloss_notify);

void cfg80211_cqm_beacon_loss_notify(struct net_device *dev, gfp_t gfp)
{
	struct sk_buff *msg;

	msg = cfg80211_prepare_cqm(dev, NULL, gfp);
	if (!msg)
		return;

	if (nla_put_flag(msg, NL80211_ATTR_CQM_BEACON_LOSS_EVENT))
		goto nla_put_failure;

	cfg80211_send_cqm(msg, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_cqm_beacon_loss_notify);

static void nl80211_gtk_rekey_notify(struct cfg80211_registered_device *rdev,
				     struct net_device *netdev, const u8 *bssid,
				     const u8 *replay_ctr, gfp_t gfp)
{
	struct sk_buff *msg;
	struct nlattr *rekey_attr;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_SET_REKEY_OFFLOAD);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, bssid))
		goto nla_put_failure;

	rekey_attr = nla_nest_start_noflag(msg, NL80211_ATTR_REKEY_DATA);
	if (!rekey_attr)
		goto nla_put_failure;

	if (nla_put(msg, NL80211_REKEY_DATA_REPLAY_CTR,
		    NL80211_REPLAY_CTR_LEN, replay_ctr))
		goto nla_put_failure;

	nla_nest_end(msg, rekey_attr);

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}

void cfg80211_gtk_rekey_notify(struct net_device *dev, const u8 *bssid,
			       const u8 *replay_ctr, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	trace_cfg80211_gtk_rekey_notify(dev, bssid);
	nl80211_gtk_rekey_notify(rdev, dev, bssid, replay_ctr, gfp);
}
EXPORT_SYMBOL(cfg80211_gtk_rekey_notify);

static void
nl80211_pmksa_candidate_notify(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev, int index,
			       const u8 *bssid, bool preauth, gfp_t gfp)
{
	struct sk_buff *msg;
	struct nlattr *attr;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_PMKSA_CANDIDATE);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex))
		goto nla_put_failure;

	attr = nla_nest_start_noflag(msg, NL80211_ATTR_PMKSA_CANDIDATE);
	if (!attr)
		goto nla_put_failure;

	if (nla_put_u32(msg, NL80211_PMKSA_CANDIDATE_INDEX, index) ||
	    nla_put(msg, NL80211_PMKSA_CANDIDATE_BSSID, ETH_ALEN, bssid) ||
	    (preauth &&
	     nla_put_flag(msg, NL80211_PMKSA_CANDIDATE_PREAUTH)))
		goto nla_put_failure;

	nla_nest_end(msg, attr);

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}

void cfg80211_pmksa_candidate_notify(struct net_device *dev, int index,
				     const u8 *bssid, bool preauth, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	trace_cfg80211_pmksa_candidate_notify(dev, index, bssid, preauth);
	nl80211_pmksa_candidate_notify(rdev, dev, index, bssid, preauth, gfp);
}
EXPORT_SYMBOL(cfg80211_pmksa_candidate_notify);

static void nl80211_ch_switch_notify(struct cfg80211_registered_device *rdev,
				     struct net_device *netdev,
				     struct cfg80211_chan_def *chandef,
				     gfp_t gfp,
				     enum nl80211_commands notif,
				     u8 count)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, notif);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex))
		goto nla_put_failure;

	if (nl80211_send_chandef(msg, chandef))
		goto nla_put_failure;

	if ((notif == NL80211_CMD_CH_SWITCH_STARTED_NOTIFY) &&
	    (nla_put_u32(msg, NL80211_ATTR_CH_SWITCH_COUNT, count)))
			goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}

void cfg80211_ch_switch_notify(struct net_device *dev,
			       struct cfg80211_chan_def *chandef)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	ASSERT_WDEV_LOCK(wdev);

	trace_cfg80211_ch_switch_notify(dev, chandef);

	wdev->chandef = *chandef;
	wdev->preset_chandef = *chandef;

	if (wdev->iftype == NL80211_IFTYPE_STATION &&
	    !WARN_ON(!wdev->current_bss))
		wdev->current_bss->pub.channel = chandef->chan;

	nl80211_ch_switch_notify(rdev, dev, chandef, GFP_KERNEL,
				 NL80211_CMD_CH_SWITCH_NOTIFY, 0);
}
EXPORT_SYMBOL(cfg80211_ch_switch_notify);

void cfg80211_ch_switch_started_notify(struct net_device *dev,
				       struct cfg80211_chan_def *chandef,
				       u8 count)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);

	trace_cfg80211_ch_switch_started_notify(dev, chandef);

	nl80211_ch_switch_notify(rdev, dev, chandef, GFP_KERNEL,
				 NL80211_CMD_CH_SWITCH_STARTED_NOTIFY, count);
}
EXPORT_SYMBOL(cfg80211_ch_switch_started_notify);

void
nl80211_radar_notify(struct cfg80211_registered_device *rdev,
		     const struct cfg80211_chan_def *chandef,
		     enum nl80211_radar_event event,
		     struct net_device *netdev, gfp_t gfp)
{
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_RADAR_DETECT);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx))
		goto nla_put_failure;

	/* NOP and radar events don't need a netdev parameter */
	if (netdev) {
		struct wireless_dev *wdev = netdev->ieee80211_ptr;

		if (nla_put_u32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex) ||
		    nla_put_u64_64bit(msg, NL80211_ATTR_WDEV, wdev_id(wdev),
				      NL80211_ATTR_PAD))
			goto nla_put_failure;
	}

	if (nla_put_u32(msg, NL80211_ATTR_RADAR_EVENT, event))
		goto nla_put_failure;

	if (nl80211_send_chandef(msg, chandef))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}

void cfg80211_sta_opmode_change_notify(struct net_device *dev, const u8 *mac,
				       struct sta_opmode_info *sta_opmode,
				       gfp_t gfp)
{
	struct sk_buff *msg;
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	void *hdr;

	if (WARN_ON(!mac))
		return;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_STA_OPMODE_CHANGED);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx))
		goto nla_put_failure;

	if (nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex))
		goto nla_put_failure;

	if (nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, mac))
		goto nla_put_failure;

	if ((sta_opmode->changed & STA_OPMODE_SMPS_MODE_CHANGED) &&
	    nla_put_u8(msg, NL80211_ATTR_SMPS_MODE, sta_opmode->smps_mode))
		goto nla_put_failure;

	if ((sta_opmode->changed & STA_OPMODE_MAX_BW_CHANGED) &&
	    nla_put_u8(msg, NL80211_ATTR_CHANNEL_WIDTH, sta_opmode->bw))
		goto nla_put_failure;

	if ((sta_opmode->changed & STA_OPMODE_N_SS_CHANGED) &&
	    nla_put_u8(msg, NL80211_ATTR_NSS, sta_opmode->rx_nss))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);

	return;

nla_put_failure:
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_sta_opmode_change_notify);

void cfg80211_probe_status(struct net_device *dev, const u8 *addr,
			   u64 cookie, bool acked, s32 ack_signal,
			   bool is_valid_ack_signal, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct sk_buff *msg;
	void *hdr;

	trace_cfg80211_probe_status(dev, addr, cookie, acked);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);

	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_PROBE_CLIENT);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, addr) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_COOKIE, cookie,
			      NL80211_ATTR_PAD) ||
	    (acked && nla_put_flag(msg, NL80211_ATTR_ACK)) ||
	    (is_valid_ack_signal && nla_put_s32(msg, NL80211_ATTR_ACK_SIGNAL,
						ack_signal)))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_probe_status);

void cfg80211_report_obss_beacon(struct wiphy *wiphy,
				 const u8 *frame, size_t len,
				 int freq, int sig_dbm)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct sk_buff *msg;
	void *hdr;
	struct cfg80211_beacon_registration *reg;

	trace_cfg80211_report_obss_beacon(wiphy, frame, len, freq, sig_dbm);

	spin_lock_bh(&rdev->beacon_registrations_lock);
	list_for_each_entry(reg, &rdev->beacon_registrations, list) {
		msg = nlmsg_new(len + 100, GFP_ATOMIC);
		if (!msg) {
			spin_unlock_bh(&rdev->beacon_registrations_lock);
			return;
		}

		hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_FRAME);
		if (!hdr)
			goto nla_put_failure;

		if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
		    (freq &&
		     nla_put_u32(msg, NL80211_ATTR_WIPHY_FREQ, freq)) ||
		    (sig_dbm &&
		     nla_put_u32(msg, NL80211_ATTR_RX_SIGNAL_DBM, sig_dbm)) ||
		    nla_put(msg, NL80211_ATTR_FRAME, len, frame))
			goto nla_put_failure;

		genlmsg_end(msg, hdr);

		genlmsg_unicast(wiphy_net(&rdev->wiphy), msg, reg->nlportid);
	}
	spin_unlock_bh(&rdev->beacon_registrations_lock);
	return;

 nla_put_failure:
	spin_unlock_bh(&rdev->beacon_registrations_lock);
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_report_obss_beacon);

#ifdef CONFIG_PM
static int cfg80211_net_detect_results(struct sk_buff *msg,
				       struct cfg80211_wowlan_wakeup *wakeup)
{
	struct cfg80211_wowlan_nd_info *nd = wakeup->net_detect;
	struct nlattr *nl_results, *nl_match, *nl_freqs;
	int i, j;

	nl_results = nla_nest_start_noflag(msg,
					   NL80211_WOWLAN_TRIG_NET_DETECT_RESULTS);
	if (!nl_results)
		return -EMSGSIZE;

	for (i = 0; i < nd->n_matches; i++) {
		struct cfg80211_wowlan_nd_match *match = nd->matches[i];

		nl_match = nla_nest_start_noflag(msg, i);
		if (!nl_match)
			break;

		/* The SSID attribute is optional in nl80211, but for
		 * simplicity reasons it's always present in the
		 * cfg80211 structure.  If a driver can't pass the
		 * SSID, that needs to be changed.  A zero length SSID
		 * is still a valid SSID (wildcard), so it cannot be
		 * used for this purpose.
		 */
		if (nla_put(msg, NL80211_ATTR_SSID, match->ssid.ssid_len,
			    match->ssid.ssid)) {
			nla_nest_cancel(msg, nl_match);
			goto out;
		}

		if (match->n_channels) {
			nl_freqs = nla_nest_start_noflag(msg,
							 NL80211_ATTR_SCAN_FREQUENCIES);
			if (!nl_freqs) {
				nla_nest_cancel(msg, nl_match);
				goto out;
			}

			for (j = 0; j < match->n_channels; j++) {
				if (nla_put_u32(msg, j, match->channels[j])) {
					nla_nest_cancel(msg, nl_freqs);
					nla_nest_cancel(msg, nl_match);
					goto out;
				}
			}

			nla_nest_end(msg, nl_freqs);
		}

		nla_nest_end(msg, nl_match);
	}

out:
	nla_nest_end(msg, nl_results);
	return 0;
}

void cfg80211_report_wowlan_wakeup(struct wireless_dev *wdev,
				   struct cfg80211_wowlan_wakeup *wakeup,
				   gfp_t gfp)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct sk_buff *msg;
	void *hdr;
	int size = 200;

	trace_cfg80211_report_wowlan_wakeup(wdev->wiphy, wdev, wakeup);

	if (wakeup)
		size += wakeup->packet_present_len;

	msg = nlmsg_new(size, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_SET_WOWLAN);
	if (!hdr)
		goto free_msg;

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_WDEV, wdev_id(wdev),
			      NL80211_ATTR_PAD))
		goto free_msg;

	if (wdev->netdev && nla_put_u32(msg, NL80211_ATTR_IFINDEX,
					wdev->netdev->ifindex))
		goto free_msg;

	if (wakeup) {
		struct nlattr *reasons;

		reasons = nla_nest_start_noflag(msg,
						NL80211_ATTR_WOWLAN_TRIGGERS);
		if (!reasons)
			goto free_msg;

		if (wakeup->disconnect &&
		    nla_put_flag(msg, NL80211_WOWLAN_TRIG_DISCONNECT))
			goto free_msg;
		if (wakeup->magic_pkt &&
		    nla_put_flag(msg, NL80211_WOWLAN_TRIG_MAGIC_PKT))
			goto free_msg;
		if (wakeup->gtk_rekey_failure &&
		    nla_put_flag(msg, NL80211_WOWLAN_TRIG_GTK_REKEY_FAILURE))
			goto free_msg;
		if (wakeup->eap_identity_req &&
		    nla_put_flag(msg, NL80211_WOWLAN_TRIG_EAP_IDENT_REQUEST))
			goto free_msg;
		if (wakeup->four_way_handshake &&
		    nla_put_flag(msg, NL80211_WOWLAN_TRIG_4WAY_HANDSHAKE))
			goto free_msg;
		if (wakeup->rfkill_release &&
		    nla_put_flag(msg, NL80211_WOWLAN_TRIG_RFKILL_RELEASE))
			goto free_msg;

		if (wakeup->pattern_idx >= 0 &&
		    nla_put_u32(msg, NL80211_WOWLAN_TRIG_PKT_PATTERN,
				wakeup->pattern_idx))
			goto free_msg;

		if (wakeup->tcp_match &&
		    nla_put_flag(msg, NL80211_WOWLAN_TRIG_WAKEUP_TCP_MATCH))
			goto free_msg;

		if (wakeup->tcp_connlost &&
		    nla_put_flag(msg, NL80211_WOWLAN_TRIG_WAKEUP_TCP_CONNLOST))
			goto free_msg;

		if (wakeup->tcp_nomoretokens &&
		    nla_put_flag(msg,
				 NL80211_WOWLAN_TRIG_WAKEUP_TCP_NOMORETOKENS))
			goto free_msg;

		if (wakeup->packet) {
			u32 pkt_attr = NL80211_WOWLAN_TRIG_WAKEUP_PKT_80211;
			u32 len_attr = NL80211_WOWLAN_TRIG_WAKEUP_PKT_80211_LEN;

			if (!wakeup->packet_80211) {
				pkt_attr =
					NL80211_WOWLAN_TRIG_WAKEUP_PKT_8023;
				len_attr =
					NL80211_WOWLAN_TRIG_WAKEUP_PKT_8023_LEN;
			}

			if (wakeup->packet_len &&
			    nla_put_u32(msg, len_attr, wakeup->packet_len))
				goto free_msg;

			if (nla_put(msg, pkt_attr, wakeup->packet_present_len,
				    wakeup->packet))
				goto free_msg;
		}

		if (wakeup->net_detect &&
		    cfg80211_net_detect_results(msg, wakeup))
				goto free_msg;

		nla_nest_end(msg, reasons);
	}

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 free_msg:
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_report_wowlan_wakeup);
#endif

void cfg80211_tdls_oper_request(struct net_device *dev, const u8 *peer,
				enum nl80211_tdls_operation oper,
				u16 reason_code, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct sk_buff *msg;
	void *hdr;

	trace_cfg80211_tdls_oper_request(wdev->wiphy, dev, peer, oper,
					 reason_code);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_TDLS_OPER);
	if (!hdr) {
		nlmsg_free(msg);
		return;
	}

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex) ||
	    nla_put_u8(msg, NL80211_ATTR_TDLS_OPERATION, oper) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, peer) ||
	    (reason_code > 0 &&
	     nla_put_u16(msg, NL80211_ATTR_REASON_CODE, reason_code)))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_tdls_oper_request);

static int nl80211_netlink_notify(struct notifier_block * nb,
				  unsigned long state,
				  void *_notify)
{
	struct netlink_notify *notify = _notify;
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *wdev;
	struct cfg80211_beacon_registration *reg, *tmp;

	if (state != NETLINK_URELEASE || notify->protocol != NETLINK_GENERIC)
		return NOTIFY_DONE;

	rcu_read_lock();

	list_for_each_entry_rcu(rdev, &cfg80211_rdev_list, list) {
		struct cfg80211_sched_scan_request *sched_scan_req;

		list_for_each_entry_rcu(sched_scan_req,
					&rdev->sched_scan_req_list,
					list) {
			if (sched_scan_req->owner_nlportid == notify->portid) {
				sched_scan_req->nl_owner_dead = true;
				schedule_work(&rdev->sched_scan_stop_wk);
			}
		}

		list_for_each_entry_rcu(wdev, &rdev->wiphy.wdev_list, list) {
			cfg80211_mlme_unregister_socket(wdev, notify->portid);

			if (wdev->owner_nlportid == notify->portid) {
				wdev->nl_owner_dead = true;
				schedule_work(&rdev->destroy_work);
			} else if (wdev->conn_owner_nlportid == notify->portid) {
				schedule_work(&wdev->disconnect_wk);
			}

			cfg80211_release_pmsr(wdev, notify->portid);
		}

		spin_lock_bh(&rdev->beacon_registrations_lock);
		list_for_each_entry_safe(reg, tmp, &rdev->beacon_registrations,
					 list) {
			if (reg->nlportid == notify->portid) {
				list_del(&reg->list);
				kfree(reg);
				break;
			}
		}
		spin_unlock_bh(&rdev->beacon_registrations_lock);
	}

	rcu_read_unlock();

	/*
	 * It is possible that the user space process that is controlling the
	 * indoor setting disappeared, so notify the regulatory core.
	 */
	regulatory_netlink_notify(notify->portid);
	return NOTIFY_OK;
}

static struct notifier_block nl80211_netlink_notifier = {
	.notifier_call = nl80211_netlink_notify,
};

void cfg80211_ft_event(struct net_device *netdev,
		       struct cfg80211_ft_event_params *ft_event)
{
	struct wiphy *wiphy = netdev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct sk_buff *msg;
	void *hdr;

	trace_cfg80211_ft_event(wiphy, netdev, ft_event);

	if (!ft_event->target_ap)
		return;

	msg = nlmsg_new(100 + ft_event->ies_len + ft_event->ric_ies_len,
			GFP_KERNEL);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_FT_EVENT);
	if (!hdr)
		goto out;

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, ft_event->target_ap))
		goto out;

	if (ft_event->ies &&
	    nla_put(msg, NL80211_ATTR_IE, ft_event->ies_len, ft_event->ies))
		goto out;
	if (ft_event->ric_ies &&
	    nla_put(msg, NL80211_ATTR_IE_RIC, ft_event->ric_ies_len,
		    ft_event->ric_ies))
		goto out;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, GFP_KERNEL);
	return;
 out:
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_ft_event);

void cfg80211_crit_proto_stopped(struct wireless_dev *wdev, gfp_t gfp)
{
	struct cfg80211_registered_device *rdev;
	struct sk_buff *msg;
	void *hdr;
	u32 nlportid;

	rdev = wiphy_to_rdev(wdev->wiphy);
	if (!rdev->crit_proto_nlportid)
		return;

	nlportid = rdev->crit_proto_nlportid;
	rdev->crit_proto_nlportid = 0;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_CRIT_PROTOCOL_STOP);
	if (!hdr)
		goto nla_put_failure;

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_WDEV, wdev_id(wdev),
			      NL80211_ATTR_PAD))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_unicast(wiphy_net(&rdev->wiphy), msg, nlportid);
	return;

 nla_put_failure:
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_crit_proto_stopped);

void nl80211_send_ap_stopped(struct wireless_dev *wdev)
{
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct sk_buff *msg;
	void *hdr;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_STOP_AP);
	if (!hdr)
		goto out;

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, wdev->netdev->ifindex) ||
	    nla_put_u64_64bit(msg, NL80211_ATTR_WDEV, wdev_id(wdev),
			      NL80211_ATTR_PAD))
		goto out;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(wiphy), msg, 0,
				NL80211_MCGRP_MLME, GFP_KERNEL);
	return;
 out:
	nlmsg_free(msg);
}

int cfg80211_external_auth_request(struct net_device *dev,
				   struct cfg80211_external_auth_params *params,
				   gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct sk_buff *msg;
	void *hdr;

	if (!wdev->conn_owner_nlportid)
		return -EINVAL;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return -ENOMEM;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_EXTERNAL_AUTH);
	if (!hdr)
		goto nla_put_failure;

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, dev->ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_AKM_SUITES, params->key_mgmt_suite) ||
	    nla_put_u32(msg, NL80211_ATTR_EXTERNAL_AUTH_ACTION,
			params->action) ||
	    nla_put(msg, NL80211_ATTR_BSSID, ETH_ALEN, params->bssid) ||
	    nla_put(msg, NL80211_ATTR_SSID, params->ssid.ssid_len,
		    params->ssid.ssid))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);
	genlmsg_unicast(wiphy_net(&rdev->wiphy), msg,
			wdev->conn_owner_nlportid);
	return 0;

 nla_put_failure:
	nlmsg_free(msg);
	return -ENOBUFS;
}
EXPORT_SYMBOL(cfg80211_external_auth_request);

void cfg80211_update_owe_info_event(struct net_device *netdev,
				    struct cfg80211_update_owe_info *owe_info,
				    gfp_t gfp)
{
	struct wiphy *wiphy = netdev->ieee80211_ptr->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct sk_buff *msg;
	void *hdr;

	trace_cfg80211_update_owe_info_event(wiphy, netdev, owe_info);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!msg)
		return;

	hdr = nl80211hdr_put(msg, 0, 0, 0, NL80211_CMD_UPDATE_OWE_INFO);
	if (!hdr)
		goto nla_put_failure;

	if (nla_put_u32(msg, NL80211_ATTR_WIPHY, rdev->wiphy_idx) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, netdev->ifindex) ||
	    nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, owe_info->peer))
		goto nla_put_failure;

	if (!owe_info->ie_len ||
	    nla_put(msg, NL80211_ATTR_IE, owe_info->ie_len, owe_info->ie))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	genlmsg_multicast_netns(&nl80211_fam, wiphy_net(&rdev->wiphy), msg, 0,
				NL80211_MCGRP_MLME, gfp);
	return;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	nlmsg_free(msg);
}
EXPORT_SYMBOL(cfg80211_update_owe_info_event);

/* initialisation/exit functions */

int __init nl80211_init(void)
{
	int err;

	err = genl_register_family(&nl80211_fam);
	if (err)
		return err;

	err = netlink_register_notifier(&nl80211_netlink_notifier);
	if (err)
		goto err_out;

	return 0;
 err_out:
	genl_unregister_family(&nl80211_fam);
	return err;
}

void nl80211_exit(void)
{
	netlink_unregister_notifier(&nl80211_netlink_notifier);
	genl_unregister_family(&nl80211_fam);
}
