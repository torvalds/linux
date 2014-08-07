/*
 * mac80211 TDLS handling code
 *
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2014, Intel Corporation
 *
 * This file is GPLv2 as found in COPYING.
 */

#include <linux/ieee80211.h>
#include "ieee80211_i.h"

static void ieee80211_tdls_add_ext_capab(struct sk_buff *skb)
{
	u8 *pos = (void *)skb_put(skb, 7);

	*pos++ = WLAN_EID_EXT_CAPABILITY;
	*pos++ = 5; /* len */
	*pos++ = 0x0;
	*pos++ = 0x0;
	*pos++ = 0x0;
	*pos++ = 0x0;
	*pos++ = WLAN_EXT_CAPA5_TDLS_ENABLED;
}

static u16 ieee80211_get_tdls_sta_capab(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	u16 capab;

	capab = 0;
	if (ieee80211_get_sdata_band(sdata) != IEEE80211_BAND_2GHZ)
		return capab;

	if (!(local->hw.flags & IEEE80211_HW_2GHZ_SHORT_SLOT_INCAPABLE))
		capab |= WLAN_CAPABILITY_SHORT_SLOT_TIME;
	if (!(local->hw.flags & IEEE80211_HW_2GHZ_SHORT_PREAMBLE_INCAPABLE))
		capab |= WLAN_CAPABILITY_SHORT_PREAMBLE;

	return capab;
}

static void ieee80211_tdls_add_link_ie(struct sk_buff *skb, const u8 *src_addr,
				       const u8 *peer, const u8 *bssid)
{
	struct ieee80211_tdls_lnkie *lnkid;

	lnkid = (void *)skb_put(skb, sizeof(struct ieee80211_tdls_lnkie));

	lnkid->ie_type = WLAN_EID_LINK_ID;
	lnkid->ie_len = sizeof(struct ieee80211_tdls_lnkie) - 2;

	memcpy(lnkid->bssid, bssid, ETH_ALEN);
	memcpy(lnkid->init_sta, src_addr, ETH_ALEN);
	memcpy(lnkid->resp_sta, peer, ETH_ALEN);
}

static int
ieee80211_prep_tdls_encap_data(struct wiphy *wiphy, struct net_device *dev,
			       const u8 *peer, u8 action_code, u8 dialog_token,
			       u16 status_code, struct sk_buff *skb)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	enum ieee80211_band band = ieee80211_get_sdata_band(sdata);
	struct ieee80211_tdls_data *tf;

	tf = (void *)skb_put(skb, offsetof(struct ieee80211_tdls_data, u));

	memcpy(tf->da, peer, ETH_ALEN);
	memcpy(tf->sa, sdata->vif.addr, ETH_ALEN);
	tf->ether_type = cpu_to_be16(ETH_P_TDLS);
	tf->payload_type = WLAN_TDLS_SNAP_RFTYPE;

	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_SETUP_REQUEST;

		skb_put(skb, sizeof(tf->u.setup_req));
		tf->u.setup_req.dialog_token = dialog_token;
		tf->u.setup_req.capability =
			cpu_to_le16(ieee80211_get_tdls_sta_capab(sdata));

		ieee80211_add_srates_ie(sdata, skb, false, band);
		ieee80211_add_ext_srates_ie(sdata, skb, false, band);
		ieee80211_tdls_add_ext_capab(skb);
		break;
	case WLAN_TDLS_SETUP_RESPONSE:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_SETUP_RESPONSE;

		skb_put(skb, sizeof(tf->u.setup_resp));
		tf->u.setup_resp.status_code = cpu_to_le16(status_code);
		tf->u.setup_resp.dialog_token = dialog_token;
		tf->u.setup_resp.capability =
			cpu_to_le16(ieee80211_get_tdls_sta_capab(sdata));

		ieee80211_add_srates_ie(sdata, skb, false, band);
		ieee80211_add_ext_srates_ie(sdata, skb, false, band);
		ieee80211_tdls_add_ext_capab(skb);
		break;
	case WLAN_TDLS_SETUP_CONFIRM:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_SETUP_CONFIRM;

		skb_put(skb, sizeof(tf->u.setup_cfm));
		tf->u.setup_cfm.status_code = cpu_to_le16(status_code);
		tf->u.setup_cfm.dialog_token = dialog_token;
		break;
	case WLAN_TDLS_TEARDOWN:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_TEARDOWN;

		skb_put(skb, sizeof(tf->u.teardown));
		tf->u.teardown.reason_code = cpu_to_le16(status_code);
		break;
	case WLAN_TDLS_DISCOVERY_REQUEST:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_DISCOVERY_REQUEST;

		skb_put(skb, sizeof(tf->u.discover_req));
		tf->u.discover_req.dialog_token = dialog_token;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
ieee80211_prep_tdls_direct(struct wiphy *wiphy, struct net_device *dev,
			   const u8 *peer, u8 action_code, u8 dialog_token,
			   u16 status_code, struct sk_buff *skb)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	enum ieee80211_band band = ieee80211_get_sdata_band(sdata);
	struct ieee80211_mgmt *mgmt;

	mgmt = (void *)skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, peer, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, sdata->u.mgd.bssid, ETH_ALEN);

	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);

	switch (action_code) {
	case WLAN_PUB_ACTION_TDLS_DISCOVER_RES:
		skb_put(skb, 1 + sizeof(mgmt->u.action.u.tdls_discover_resp));
		mgmt->u.action.category = WLAN_CATEGORY_PUBLIC;
		mgmt->u.action.u.tdls_discover_resp.action_code =
			WLAN_PUB_ACTION_TDLS_DISCOVER_RES;
		mgmt->u.action.u.tdls_discover_resp.dialog_token =
			dialog_token;
		mgmt->u.action.u.tdls_discover_resp.capability =
			cpu_to_le16(ieee80211_get_tdls_sta_capab(sdata));

		ieee80211_add_srates_ie(sdata, skb, false, band);
		ieee80211_add_ext_srates_ie(sdata, skb, false, band);
		ieee80211_tdls_add_ext_capab(skb);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int ieee80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
			const u8 *peer, u8 action_code, u8 dialog_token,
			u16 status_code, u32 peer_capability,
			const u8 *extra_ies, size_t extra_ies_len)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb = NULL;
	bool send_direct;
	int ret;

	if (!(wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return -ENOTSUPP;

	/* make sure we are in managed mode, and associated */
	if (sdata->vif.type != NL80211_IFTYPE_STATION ||
	    !sdata->u.mgd.associated)
		return -EINVAL;

	tdls_dbg(sdata, "TDLS mgmt action %d peer %pM\n",
		 action_code, peer);

	skb = dev_alloc_skb(local->hw.extra_tx_headroom +
			    max(sizeof(struct ieee80211_mgmt),
				sizeof(struct ieee80211_tdls_data)) +
			    50 + /* supported rates */
			    7 + /* ext capab */
			    extra_ies_len +
			    sizeof(struct ieee80211_tdls_lnkie));
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb, local->hw.extra_tx_headroom);

	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
	case WLAN_TDLS_SETUP_RESPONSE:
	case WLAN_TDLS_SETUP_CONFIRM:
	case WLAN_TDLS_TEARDOWN:
	case WLAN_TDLS_DISCOVERY_REQUEST:
		ret = ieee80211_prep_tdls_encap_data(wiphy, dev, peer,
						     action_code, dialog_token,
						     status_code, skb);
		send_direct = false;
		break;
	case WLAN_PUB_ACTION_TDLS_DISCOVER_RES:
		ret = ieee80211_prep_tdls_direct(wiphy, dev, peer, action_code,
						 dialog_token, status_code,
						 skb);
		send_direct = true;
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}

	if (ret < 0)
		goto fail;

	if (extra_ies_len)
		memcpy(skb_put(skb, extra_ies_len), extra_ies, extra_ies_len);

	/* the TDLS link IE is always added last */
	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
	case WLAN_TDLS_SETUP_CONFIRM:
	case WLAN_TDLS_TEARDOWN:
	case WLAN_TDLS_DISCOVERY_REQUEST:
		/* we are the initiator */
		ieee80211_tdls_add_link_ie(skb, sdata->vif.addr, peer,
					   sdata->u.mgd.bssid);
		break;
	case WLAN_TDLS_SETUP_RESPONSE:
	case WLAN_PUB_ACTION_TDLS_DISCOVER_RES:
		/* we are the responder */
		ieee80211_tdls_add_link_ie(skb, peer, sdata->vif.addr,
					   sdata->u.mgd.bssid);
		break;
	default:
		ret = -ENOTSUPP;
		goto fail;
	}

	if (send_direct) {
		ieee80211_tx_skb(sdata, skb);
		return 0;
	}

	/*
	 * According to 802.11z: Setup req/resp are sent in AC_BK, otherwise
	 * we should default to AC_VI.
	 */
	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
	case WLAN_TDLS_SETUP_RESPONSE:
		skb_set_queue_mapping(skb, IEEE80211_AC_BK);
		skb->priority = 2;
		break;
	default:
		skb_set_queue_mapping(skb, IEEE80211_AC_VI);
		skb->priority = 5;
		break;
	}

	/* disable bottom halves when entering the Tx path */
	local_bh_disable();
	ret = ieee80211_subif_start_xmit(skb, dev);
	local_bh_enable();

	return ret;

fail:
	dev_kfree_skb(skb);
	return ret;
}

int ieee80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
			const u8 *peer, enum nl80211_tdls_operation oper)
{
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (!(wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return -ENOTSUPP;

	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return -EINVAL;

	tdls_dbg(sdata, "TDLS oper %d peer %pM\n", oper, peer);

	switch (oper) {
	case NL80211_TDLS_ENABLE_LINK:
		rcu_read_lock();
		sta = sta_info_get(sdata, peer);
		if (!sta) {
			rcu_read_unlock();
			return -ENOLINK;
		}

		set_sta_flag(sta, WLAN_STA_TDLS_PEER_AUTH);
		rcu_read_unlock();
		break;
	case NL80211_TDLS_DISABLE_LINK:
		return sta_info_destroy_addr(sdata, peer);
	case NL80211_TDLS_TEARDOWN:
	case NL80211_TDLS_SETUP:
	case NL80211_TDLS_DISCOVERY_REQ:
		/* We don't support in-driver setup/teardown/discovery */
		return -ENOTSUPP;
	default:
		return -ENOTSUPP;
	}

	return 0;
}
