/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Portions of this file
 * Copyright (C) 2018, 2020-2024 Intel Corporation
 */
#ifndef __NET_WIRELESS_NL80211_H
#define __NET_WIRELESS_NL80211_H

#include "core.h"

int nl80211_init(void);
void nl80211_exit(void);

void *nl80211hdr_put(struct sk_buff *skb, u32 portid, u32 seq,
		     int flags, u8 cmd);
bool nl80211_put_sta_rate(struct sk_buff *msg, struct rate_info *info,
			  int attr);

static inline u64 wdev_id(struct wireless_dev *wdev)
{
	return (u64)wdev->identifier |
	       ((u64)wiphy_to_rdev(wdev->wiphy)->wiphy_idx << 32);
}

int nl80211_parse_chandef(struct cfg80211_registered_device *rdev,
			  struct genl_info *info,
			  struct cfg80211_chan_def *chandef);
int nl80211_parse_random_mac(struct nlattr **attrs,
			     u8 *mac_addr, u8 *mac_addr_mask);

void nl80211_notify_wiphy(struct cfg80211_registered_device *rdev,
			  enum nl80211_commands cmd);
void nl80211_notify_iface(struct cfg80211_registered_device *rdev,
			  struct wireless_dev *wdev,
			  enum nl80211_commands cmd);
void nl80211_send_scan_start(struct cfg80211_registered_device *rdev,
			     struct wireless_dev *wdev);
struct sk_buff *nl80211_build_scan_msg(struct cfg80211_registered_device *rdev,
				       struct wireless_dev *wdev, bool aborted);
void nl80211_send_scan_msg(struct cfg80211_registered_device *rdev,
			   struct sk_buff *msg);
void nl80211_send_sched_scan(struct cfg80211_sched_scan_request *req, u32 cmd);
void nl80211_common_reg_change_event(enum nl80211_commands cmd_id,
				     struct regulatory_request *request);

static inline void
nl80211_send_reg_change_event(struct regulatory_request *request)
{
	nl80211_common_reg_change_event(NL80211_CMD_REG_CHANGE, request);
}

static inline void
nl80211_send_wiphy_reg_change_event(struct regulatory_request *request)
{
	nl80211_common_reg_change_event(NL80211_CMD_WIPHY_REG_CHANGE, request);
}

void nl80211_send_rx_auth(struct cfg80211_registered_device *rdev,
			  struct net_device *netdev,
			  const u8 *buf, size_t len, gfp_t gfp);
void nl80211_send_rx_assoc(struct cfg80211_registered_device *rdev,
			   struct net_device *netdev,
			   const struct cfg80211_rx_assoc_resp_data *data);
void nl80211_send_deauth(struct cfg80211_registered_device *rdev,
			 struct net_device *netdev,
			 const u8 *buf, size_t len,
			 bool reconnect, gfp_t gfp);
void nl80211_send_disassoc(struct cfg80211_registered_device *rdev,
			   struct net_device *netdev,
			   const u8 *buf, size_t len,
			   bool reconnect, gfp_t gfp);
void nl80211_send_auth_timeout(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev,
			       const u8 *addr, gfp_t gfp);
void nl80211_send_assoc_timeout(struct cfg80211_registered_device *rdev,
				struct net_device *netdev,
				const u8 *addr, gfp_t gfp);
void nl80211_send_connect_result(struct cfg80211_registered_device *rdev,
				 struct net_device *netdev,
				 struct cfg80211_connect_resp_params *params,
				 gfp_t gfp);
void nl80211_send_roamed(struct cfg80211_registered_device *rdev,
			 struct net_device *netdev,
			 struct cfg80211_roam_info *info, gfp_t gfp);
/* For STA/GC, indicate port authorized with AP/GO bssid.
 * For GO/AP, use peer GC/STA mac_addr.
 */
void nl80211_send_port_authorized(struct cfg80211_registered_device *rdev,
				  struct net_device *netdev, const u8 *peer_addr,
				  const u8 *td_bitmap, u8 td_bitmap_len);
void nl80211_send_disconnected(struct cfg80211_registered_device *rdev,
			       struct net_device *netdev, u16 reason,
			       const u8 *ie, size_t ie_len, bool from_ap);

void
nl80211_michael_mic_failure(struct cfg80211_registered_device *rdev,
			    struct net_device *netdev, const u8 *addr,
			    enum nl80211_key_type key_type,
			    int key_id, const u8 *tsc, gfp_t gfp);

void
nl80211_send_beacon_hint_event(struct wiphy *wiphy,
			       struct ieee80211_channel *channel_before,
			       struct ieee80211_channel *channel_after);

void nl80211_send_ibss_bssid(struct cfg80211_registered_device *rdev,
			     struct net_device *netdev, const u8 *bssid,
			     gfp_t gfp);

int nl80211_send_mgmt(struct cfg80211_registered_device *rdev,
		      struct wireless_dev *wdev, u32 nlpid,
		      struct cfg80211_rx_info *info, gfp_t gfp);

void
nl80211_radar_notify(struct cfg80211_registered_device *rdev,
		     const struct cfg80211_chan_def *chandef,
		     enum nl80211_radar_event event,
		     struct net_device *netdev, gfp_t gfp);

void nl80211_send_ap_stopped(struct wireless_dev *wdev, unsigned int link_id);

void cfg80211_free_coalesce(struct cfg80211_coalesce *coalesce);

/* peer measurement */
int nl80211_pmsr_start(struct sk_buff *skb, struct genl_info *info);

#endif /* __NET_WIRELESS_NL80211_H */
