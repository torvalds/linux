// SPDX-License-Identifier: GPL-2.0
/*
 * S1G handling
 * Copyright(c) 2020 Adapt-IP
 * Copyright (C) 2023 Intel Corporation
 */
#include <linux/ieee80211.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"

void ieee80211_s1g_sta_rate_init(struct sta_info *sta)
{
	/* avoid indicating legacy bitrates for S1G STAs */
	sta->deflink.tx_stats.last_rate.flags |= IEEE80211_TX_RC_S1G_MCS;
	sta->deflink.rx_stats.last_rate =
			STA_STATS_FIELD(TYPE, STA_STATS_RATE_TYPE_S1G);
}

bool ieee80211_s1g_is_twt_setup(struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;

	if (likely(!ieee80211_is_action(mgmt->frame_control)))
		return false;

	if (likely(mgmt->u.action.category != WLAN_CATEGORY_S1G))
		return false;

	return mgmt->u.action.u.s1g.action_code == WLAN_S1G_TWT_SETUP;
}

static void
ieee80211_s1g_send_twt_setup(struct ieee80211_sub_if_data *sdata, const u8 *da,
			     const u8 *bssid, struct ieee80211_twt_setup *twt)
{
	int len = IEEE80211_MIN_ACTION_SIZE + 4 + twt->length;
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_mgmt *mgmt;
	struct sk_buff *skb;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom + len);
	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom);
	mgmt = skb_put_zero(skb, len);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, bssid, ETH_ALEN);

	mgmt->u.action.category = WLAN_CATEGORY_S1G;
	mgmt->u.action.u.s1g.action_code = WLAN_S1G_TWT_SETUP;
	memcpy(mgmt->u.action.u.s1g.variable, twt, 3 + twt->length);

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT |
					IEEE80211_TX_INTFL_MLME_CONN_TX |
					IEEE80211_TX_CTL_REQ_TX_STATUS;
	ieee80211_tx_skb(sdata, skb);
}

static void
ieee80211_s1g_send_twt_teardown(struct ieee80211_sub_if_data *sdata,
				const u8 *da, const u8 *bssid, u8 flowid)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_mgmt *mgmt;
	struct sk_buff *skb;
	u8 *id;

	skb = dev_alloc_skb(local->hw.extra_tx_headroom +
			    IEEE80211_MIN_ACTION_SIZE + 2);
	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom);
	mgmt = skb_put_zero(skb, IEEE80211_MIN_ACTION_SIZE + 2);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);
	memcpy(mgmt->da, da, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, bssid, ETH_ALEN);

	mgmt->u.action.category = WLAN_CATEGORY_S1G;
	mgmt->u.action.u.s1g.action_code = WLAN_S1G_TWT_TEARDOWN;
	id = (u8 *)mgmt->u.action.u.s1g.variable;
	*id = flowid;

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT |
					IEEE80211_TX_CTL_REQ_TX_STATUS;
	ieee80211_tx_skb(sdata, skb);
}

static void
ieee80211_s1g_rx_twt_setup(struct ieee80211_sub_if_data *sdata,
			   struct sta_info *sta, struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	struct ieee80211_twt_setup *twt = (void *)mgmt->u.action.u.s1g.variable;
	struct ieee80211_twt_params *twt_agrt = (void *)twt->params;

	twt_agrt->req_type &= cpu_to_le16(~IEEE80211_TWT_REQTYPE_REQUEST);

	/* broadcast TWT not supported yet */
	if (twt->control & IEEE80211_TWT_CONTROL_NEG_TYPE_BROADCAST) {
		twt_agrt->req_type &=
			~cpu_to_le16(IEEE80211_TWT_REQTYPE_SETUP_CMD);
		twt_agrt->req_type |=
			le16_encode_bits(TWT_SETUP_CMD_REJECT,
					 IEEE80211_TWT_REQTYPE_SETUP_CMD);
		goto out;
	}

	/* TWT Information not supported yet */
	twt->control |= IEEE80211_TWT_CONTROL_RX_DISABLED;

	drv_add_twt_setup(sdata->local, sdata, &sta->sta, twt);
out:
	ieee80211_s1g_send_twt_setup(sdata, mgmt->sa, sdata->vif.addr, twt);
}

static void
ieee80211_s1g_rx_twt_teardown(struct ieee80211_sub_if_data *sdata,
			      struct sta_info *sta, struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;

	drv_twt_teardown_request(sdata->local, sdata, &sta->sta,
				 mgmt->u.action.u.s1g.variable[0]);
}

static void
ieee80211_s1g_tx_twt_setup_fail(struct ieee80211_sub_if_data *sdata,
				struct sta_info *sta, struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	struct ieee80211_twt_setup *twt = (void *)mgmt->u.action.u.s1g.variable;
	struct ieee80211_twt_params *twt_agrt = (void *)twt->params;
	u8 flowid = le16_get_bits(twt_agrt->req_type,
				  IEEE80211_TWT_REQTYPE_FLOWID);

	drv_twt_teardown_request(sdata->local, sdata, &sta->sta, flowid);

	ieee80211_s1g_send_twt_teardown(sdata, mgmt->sa, sdata->vif.addr,
					flowid);
}

void ieee80211_s1g_rx_twt_action(struct ieee80211_sub_if_data *sdata,
				 struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;

	lockdep_assert_wiphy(local->hw.wiphy);

	sta = sta_info_get_bss(sdata, mgmt->sa);
	if (!sta)
		return;

	switch (mgmt->u.action.u.s1g.action_code) {
	case WLAN_S1G_TWT_SETUP:
		ieee80211_s1g_rx_twt_setup(sdata, sta, skb);
		break;
	case WLAN_S1G_TWT_TEARDOWN:
		ieee80211_s1g_rx_twt_teardown(sdata, sta, skb);
		break;
	default:
		break;
	}
}

void ieee80211_s1g_status_twt_action(struct ieee80211_sub_if_data *sdata,
				     struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;

	lockdep_assert_wiphy(local->hw.wiphy);

	sta = sta_info_get_bss(sdata, mgmt->da);
	if (!sta)
		return;

	switch (mgmt->u.action.u.s1g.action_code) {
	case WLAN_S1G_TWT_SETUP:
		/* process failed twt setup frames */
		ieee80211_s1g_tx_twt_setup_fail(sdata, sta, skb);
		break;
	default:
		break;
	}
}
