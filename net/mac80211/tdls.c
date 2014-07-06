/*
 * mac80211 TDLS handling code
 *
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2014, Intel Corporation
 *
 * This file is GPLv2 as found in COPYING.
 */

#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"
#include "driver-ops.h"

/* give usermode some time for retries in setting up the TDLS session */
#define TDLS_PEER_SETUP_TIMEOUT	(15 * HZ)

void ieee80211_tdls_peer_del_work(struct work_struct *wk)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_local *local;

	sdata = container_of(wk, struct ieee80211_sub_if_data,
			     tdls_peer_del_work.work);
	local = sdata->local;

	mutex_lock(&local->mtx);
	if (!is_zero_ether_addr(sdata->tdls_peer)) {
		tdls_dbg(sdata, "TDLS del peer %pM\n", sdata->tdls_peer);
		sta_info_destroy_addr(sdata, sdata->tdls_peer);
		eth_zero_addr(sdata->tdls_peer);
	}
	mutex_unlock(&local->mtx);
}

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

static int
ieee80211_tdls_prep_mgmt_packet(struct wiphy *wiphy, struct net_device *dev,
				const u8 *peer, u8 action_code,
				u8 dialog_token, u16 status_code,
				u32 peer_capability, bool initiator,
				const u8 *extra_ies, size_t extra_ies_len)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb = NULL;
	bool send_direct;
	const u8 *init_addr, *rsp_addr;
	int ret;

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

	/* sanity check for initiator */
	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
	case WLAN_TDLS_SETUP_CONFIRM:
	case WLAN_TDLS_DISCOVERY_REQUEST:
		if (!initiator) {
			ret = -EINVAL;
			goto fail;
		}
		break;
	case WLAN_TDLS_SETUP_RESPONSE:
	case WLAN_PUB_ACTION_TDLS_DISCOVER_RES:
		if (initiator) {
			ret = -EINVAL;
			goto fail;
		}
		break;
	case WLAN_TDLS_TEARDOWN:
		/* any value is ok */
		break;
	default:
		ret = -ENOTSUPP;
		goto fail;
	}

	if (initiator) {
		init_addr = sdata->vif.addr;
		rsp_addr = peer;
	} else {
		init_addr = peer;
		rsp_addr = sdata->vif.addr;
	}

	ieee80211_tdls_add_link_ie(skb, init_addr, rsp_addr,
				   sdata->u.mgd.bssid);

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

static int
ieee80211_tdls_mgmt_setup(struct wiphy *wiphy, struct net_device *dev,
			  const u8 *peer, u8 action_code, u8 dialog_token,
			  u16 status_code, u32 peer_capability, bool initiator,
			  const u8 *extra_ies, size_t extra_ies_len)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	int ret;

	mutex_lock(&local->mtx);

	/* we don't support concurrent TDLS peer setups */
	if (!is_zero_ether_addr(sdata->tdls_peer) &&
	    !ether_addr_equal(sdata->tdls_peer, peer)) {
		ret = -EBUSY;
		goto exit;
	}

	/*
	 * make sure we have a STA representing the peer so we drop or buffer
	 * non-TDLS-setup frames to the peer. We can't send other packets
	 * during setup through the AP path
	 */
	rcu_read_lock();
	if (!sta_info_get(sdata, peer)) {
		rcu_read_unlock();
		ret = -ENOLINK;
		goto exit;
	}
	rcu_read_unlock();

	ieee80211_flush_queues(local, sdata);

	ret = ieee80211_tdls_prep_mgmt_packet(wiphy, dev, peer, action_code,
					      dialog_token, status_code,
					      peer_capability, initiator,
					      extra_ies, extra_ies_len);
	if (ret < 0)
		goto exit;

	memcpy(sdata->tdls_peer, peer, ETH_ALEN);
	ieee80211_queue_delayed_work(&sdata->local->hw,
				     &sdata->tdls_peer_del_work,
				     TDLS_PEER_SETUP_TIMEOUT);

exit:
	mutex_unlock(&local->mtx);
	return ret;
}

static int
ieee80211_tdls_mgmt_teardown(struct wiphy *wiphy, struct net_device *dev,
			     const u8 *peer, u8 action_code, u8 dialog_token,
			     u16 status_code, u32 peer_capability,
			     bool initiator, const u8 *extra_ies,
			     size_t extra_ies_len)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	int ret;

	/*
	 * No packets can be transmitted to the peer via the AP during setup -
	 * the STA is set as a TDLS peer, but is not authorized.
	 * During teardown, we prevent direct transmissions by stopping the
	 * queues and flushing all direct packets.
	 */
	ieee80211_stop_vif_queues(local, sdata,
				  IEEE80211_QUEUE_STOP_REASON_TDLS_TEARDOWN);
	ieee80211_flush_queues(local, sdata);

	ret = ieee80211_tdls_prep_mgmt_packet(wiphy, dev, peer, action_code,
					      dialog_token, status_code,
					      peer_capability, initiator,
					      extra_ies, extra_ies_len);
	if (ret < 0)
		sdata_err(sdata, "Failed sending TDLS teardown packet %d\n",
			  ret);

	/*
	 * Remove the STA AUTH flag to force further traffic through the AP. If
	 * the STA was unreachable, it was already removed.
	 */
	rcu_read_lock();
	sta = sta_info_get(sdata, peer);
	if (sta)
		clear_sta_flag(sta, WLAN_STA_TDLS_PEER_AUTH);
	rcu_read_unlock();

	ieee80211_wake_vif_queues(local, sdata,
				  IEEE80211_QUEUE_STOP_REASON_TDLS_TEARDOWN);

	return 0;
}

int ieee80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
			const u8 *peer, u8 action_code, u8 dialog_token,
			u16 status_code, u32 peer_capability,
			bool initiator, const u8 *extra_ies,
			size_t extra_ies_len)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int ret;

	if (!(wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return -ENOTSUPP;

	/* make sure we are in managed mode, and associated */
	if (sdata->vif.type != NL80211_IFTYPE_STATION ||
	    !sdata->u.mgd.associated)
		return -EINVAL;

	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
	case WLAN_TDLS_SETUP_RESPONSE:
		ret = ieee80211_tdls_mgmt_setup(wiphy, dev, peer, action_code,
						dialog_token, status_code,
						peer_capability, initiator,
						extra_ies, extra_ies_len);
		break;
	case WLAN_TDLS_TEARDOWN:
		ret = ieee80211_tdls_mgmt_teardown(wiphy, dev, peer,
						   action_code, dialog_token,
						   status_code,
						   peer_capability, initiator,
						   extra_ies, extra_ies_len);
		break;
	case WLAN_TDLS_DISCOVERY_REQUEST:
		/*
		 * Protect the discovery so we can hear the TDLS discovery
		 * response frame. It is transmitted directly and not buffered
		 * by the AP.
		 */
		drv_mgd_protect_tdls_discover(sdata->local, sdata);
		/* fall-through */
	case WLAN_TDLS_SETUP_CONFIRM:
	case WLAN_PUB_ACTION_TDLS_DISCOVER_RES:
		/* no special handling */
		ret = ieee80211_tdls_prep_mgmt_packet(wiphy, dev, peer,
						      action_code,
						      dialog_token,
						      status_code,
						      peer_capability,
						      initiator, extra_ies,
						      extra_ies_len);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	tdls_dbg(sdata, "TDLS mgmt action %d peer %pM status %d\n",
		 action_code, peer, ret);
	return ret;
}

int ieee80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
			const u8 *peer, enum nl80211_tdls_operation oper)
{
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	int ret;

	if (!(wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return -ENOTSUPP;

	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return -EINVAL;

	switch (oper) {
	case NL80211_TDLS_ENABLE_LINK:
	case NL80211_TDLS_DISABLE_LINK:
		break;
	case NL80211_TDLS_TEARDOWN:
	case NL80211_TDLS_SETUP:
	case NL80211_TDLS_DISCOVERY_REQ:
		/* We don't support in-driver setup/teardown/discovery */
		return -ENOTSUPP;
	}

	mutex_lock(&local->mtx);
	tdls_dbg(sdata, "TDLS oper %d peer %pM\n", oper, peer);

	switch (oper) {
	case NL80211_TDLS_ENABLE_LINK:
		rcu_read_lock();
		sta = sta_info_get(sdata, peer);
		if (!sta) {
			rcu_read_unlock();
			ret = -ENOLINK;
			break;
		}

		set_sta_flag(sta, WLAN_STA_TDLS_PEER_AUTH);
		rcu_read_unlock();

		WARN_ON_ONCE(is_zero_ether_addr(sdata->tdls_peer) ||
			     !ether_addr_equal(sdata->tdls_peer, peer));
		ret = 0;
		break;
	case NL80211_TDLS_DISABLE_LINK:
		/* flush a potentially queued teardown packet */
		ieee80211_flush_queues(local, sdata);

		ret = sta_info_destroy_addr(sdata, peer);
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}

	if (ret == 0 && ether_addr_equal(sdata->tdls_peer, peer)) {
		cancel_delayed_work(&sdata->tdls_peer_del_work);
		eth_zero_addr(sdata->tdls_peer);
	}

	mutex_unlock(&local->mtx);
	return ret;
}

void ieee80211_tdls_oper_request(struct ieee80211_vif *vif, const u8 *peer,
				 enum nl80211_tdls_operation oper,
				 u16 reason_code, gfp_t gfp)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	if (vif->type != NL80211_IFTYPE_STATION || !vif->bss_conf.assoc) {
		sdata_err(sdata, "Discarding TDLS oper %d - not STA or disconnected\n",
			  oper);
		return;
	}

	cfg80211_tdls_oper_request(sdata->dev, peer, oper, reason_code, gfp);
}
EXPORT_SYMBOL(ieee80211_tdls_oper_request);
