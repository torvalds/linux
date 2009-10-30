/*
 * SME code for cfg80211's connect emulation.
 *
 * Copyright 2009	Johannes Berg <johannes@sipsolutions.net>
 * Copyright (C) 2009   Intel Corporation. All rights reserved.
 */

#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/workqueue.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>
#include "nl80211.h"
#include "reg.h"

struct cfg80211_conn {
	struct cfg80211_connect_params params;
	/* these are sub-states of the _CONNECTING sme_state */
	enum {
		CFG80211_CONN_IDLE,
		CFG80211_CONN_SCANNING,
		CFG80211_CONN_SCAN_AGAIN,
		CFG80211_CONN_AUTHENTICATE_NEXT,
		CFG80211_CONN_AUTHENTICATING,
		CFG80211_CONN_ASSOCIATE_NEXT,
		CFG80211_CONN_ASSOCIATING,
		CFG80211_CONN_DEAUTH_ASSOC_FAIL,
	} state;
	u8 bssid[ETH_ALEN], prev_bssid[ETH_ALEN];
	u8 *ie;
	size_t ie_len;
	bool auto_auth, prev_bssid_valid;
};


static int cfg80211_conn_scan(struct wireless_dev *wdev)
{
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	struct cfg80211_scan_request *request;
	int n_channels, err;

	ASSERT_RTNL();
	ASSERT_RDEV_LOCK(rdev);
	ASSERT_WDEV_LOCK(wdev);

	if (rdev->scan_req)
		return -EBUSY;

	if (wdev->conn->params.channel) {
		n_channels = 1;
	} else {
		enum ieee80211_band band;
		n_channels = 0;

		for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
			if (!wdev->wiphy->bands[band])
				continue;
			n_channels += wdev->wiphy->bands[band]->n_channels;
		}
	}
	request = kzalloc(sizeof(*request) + sizeof(request->ssids[0]) +
			  sizeof(request->channels[0]) * n_channels,
			  GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	if (wdev->conn->params.channel)
		request->channels[0] = wdev->conn->params.channel;
	else {
		int i = 0, j;
		enum ieee80211_band band;

		for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
			if (!wdev->wiphy->bands[band])
				continue;
			for (j = 0; j < wdev->wiphy->bands[band]->n_channels;
			     i++, j++)
				request->channels[i] =
					&wdev->wiphy->bands[band]->channels[j];
		}
	}
	request->n_channels = n_channels;
	request->ssids = (void *)&request->channels[n_channels];
	request->n_ssids = 1;

	memcpy(request->ssids[0].ssid, wdev->conn->params.ssid,
		wdev->conn->params.ssid_len);
	request->ssids[0].ssid_len = wdev->conn->params.ssid_len;

	request->dev = wdev->netdev;
	request->wiphy = &rdev->wiphy;

	rdev->scan_req = request;

	err = rdev->ops->scan(wdev->wiphy, wdev->netdev, request);
	if (!err) {
		wdev->conn->state = CFG80211_CONN_SCANNING;
		nl80211_send_scan_start(rdev, wdev->netdev);
		dev_hold(wdev->netdev);
	} else {
		rdev->scan_req = NULL;
		kfree(request);
	}
	return err;
}

static int cfg80211_conn_do_work(struct wireless_dev *wdev)
{
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	struct cfg80211_connect_params *params;
	const u8 *prev_bssid = NULL;
	int err;

	ASSERT_WDEV_LOCK(wdev);

	if (!wdev->conn)
		return 0;

	params = &wdev->conn->params;

	switch (wdev->conn->state) {
	case CFG80211_CONN_SCAN_AGAIN:
		return cfg80211_conn_scan(wdev);
	case CFG80211_CONN_AUTHENTICATE_NEXT:
		BUG_ON(!rdev->ops->auth);
		wdev->conn->state = CFG80211_CONN_AUTHENTICATING;
		return __cfg80211_mlme_auth(rdev, wdev->netdev,
					    params->channel, params->auth_type,
					    params->bssid,
					    params->ssid, params->ssid_len,
					    NULL, 0,
					    params->key, params->key_len,
					    params->key_idx);
	case CFG80211_CONN_ASSOCIATE_NEXT:
		BUG_ON(!rdev->ops->assoc);
		wdev->conn->state = CFG80211_CONN_ASSOCIATING;
		if (wdev->conn->prev_bssid_valid)
			prev_bssid = wdev->conn->prev_bssid;
		err = __cfg80211_mlme_assoc(rdev, wdev->netdev,
					    params->channel, params->bssid,
					    prev_bssid,
					    params->ssid, params->ssid_len,
					    params->ie, params->ie_len,
					    false, &params->crypto);
		if (err)
			__cfg80211_mlme_deauth(rdev, wdev->netdev, params->bssid,
					       NULL, 0,
					       WLAN_REASON_DEAUTH_LEAVING);
		return err;
	case CFG80211_CONN_DEAUTH_ASSOC_FAIL:
		__cfg80211_mlme_deauth(rdev, wdev->netdev, params->bssid,
				       NULL, 0,
				       WLAN_REASON_DEAUTH_LEAVING);
		/* return an error so that we call __cfg80211_connect_result() */
		return -EINVAL;
	default:
		return 0;
	}
}

void cfg80211_conn_work(struct work_struct *work)
{
	struct cfg80211_registered_device *rdev =
		container_of(work, struct cfg80211_registered_device, conn_work);
	struct wireless_dev *wdev;
	u8 bssid[ETH_ALEN];

	rtnl_lock();
	cfg80211_lock_rdev(rdev);
	mutex_lock(&rdev->devlist_mtx);

	list_for_each_entry(wdev, &rdev->netdev_list, list) {
		wdev_lock(wdev);
		if (!netif_running(wdev->netdev)) {
			wdev_unlock(wdev);
			continue;
		}
		if (wdev->sme_state != CFG80211_SME_CONNECTING) {
			wdev_unlock(wdev);
			continue;
		}
		memcpy(bssid, wdev->conn->params.bssid, ETH_ALEN);
		if (cfg80211_conn_do_work(wdev))
			__cfg80211_connect_result(
					wdev->netdev, bssid,
					NULL, 0, NULL, 0,
					WLAN_STATUS_UNSPECIFIED_FAILURE,
					false, NULL);
		wdev_unlock(wdev);
	}

	mutex_unlock(&rdev->devlist_mtx);
	cfg80211_unlock_rdev(rdev);
	rtnl_unlock();
}

static struct cfg80211_bss *cfg80211_get_conn_bss(struct wireless_dev *wdev)
{
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	struct cfg80211_bss *bss;
	u16 capa = WLAN_CAPABILITY_ESS;

	ASSERT_WDEV_LOCK(wdev);

	if (wdev->conn->params.privacy)
		capa |= WLAN_CAPABILITY_PRIVACY;

	bss = cfg80211_get_bss(wdev->wiphy, NULL, wdev->conn->params.bssid,
			       wdev->conn->params.ssid,
			       wdev->conn->params.ssid_len,
			       WLAN_CAPABILITY_ESS | WLAN_CAPABILITY_PRIVACY,
			       capa);
	if (!bss)
		return NULL;

	memcpy(wdev->conn->bssid, bss->bssid, ETH_ALEN);
	wdev->conn->params.bssid = wdev->conn->bssid;
	wdev->conn->params.channel = bss->channel;
	wdev->conn->state = CFG80211_CONN_AUTHENTICATE_NEXT;
	schedule_work(&rdev->conn_work);

	return bss;
}

static void __cfg80211_sme_scan_done(struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	struct cfg80211_bss *bss;

	ASSERT_WDEV_LOCK(wdev);

	if (wdev->sme_state != CFG80211_SME_CONNECTING)
		return;

	if (!wdev->conn)
		return;

	if (wdev->conn->state != CFG80211_CONN_SCANNING &&
	    wdev->conn->state != CFG80211_CONN_SCAN_AGAIN)
		return;

	bss = cfg80211_get_conn_bss(wdev);
	if (bss) {
		cfg80211_put_bss(bss);
	} else {
		/* not found */
		if (wdev->conn->state == CFG80211_CONN_SCAN_AGAIN)
			schedule_work(&rdev->conn_work);
		else
			__cfg80211_connect_result(
					wdev->netdev,
					wdev->conn->params.bssid,
					NULL, 0, NULL, 0,
					WLAN_STATUS_UNSPECIFIED_FAILURE,
					false, NULL);
	}
}

void cfg80211_sme_scan_done(struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	mutex_lock(&wiphy_to_dev(wdev->wiphy)->devlist_mtx);
	wdev_lock(wdev);
	__cfg80211_sme_scan_done(dev);
	wdev_unlock(wdev);
	mutex_unlock(&wiphy_to_dev(wdev->wiphy)->devlist_mtx);
}

void cfg80211_sme_rx_auth(struct net_device *dev,
			  const u8 *buf, size_t len)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)buf;
	u16 status_code = le16_to_cpu(mgmt->u.auth.status_code);

	ASSERT_WDEV_LOCK(wdev);

	/* should only RX auth frames when connecting */
	if (wdev->sme_state != CFG80211_SME_CONNECTING)
		return;

	if (WARN_ON(!wdev->conn))
		return;

	if (status_code == WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG &&
	    wdev->conn->auto_auth &&
	    wdev->conn->params.auth_type != NL80211_AUTHTYPE_NETWORK_EAP) {
		/* select automatically between only open, shared, leap */
		switch (wdev->conn->params.auth_type) {
		case NL80211_AUTHTYPE_OPEN_SYSTEM:
			if (wdev->connect_keys)
				wdev->conn->params.auth_type =
					NL80211_AUTHTYPE_SHARED_KEY;
			else
				wdev->conn->params.auth_type =
					NL80211_AUTHTYPE_NETWORK_EAP;
			break;
		case NL80211_AUTHTYPE_SHARED_KEY:
			wdev->conn->params.auth_type =
				NL80211_AUTHTYPE_NETWORK_EAP;
			break;
		default:
			/* huh? */
			wdev->conn->params.auth_type =
				NL80211_AUTHTYPE_OPEN_SYSTEM;
			break;
		}
		wdev->conn->state = CFG80211_CONN_AUTHENTICATE_NEXT;
		schedule_work(&rdev->conn_work);
	} else if (status_code != WLAN_STATUS_SUCCESS) {
		__cfg80211_connect_result(dev, mgmt->bssid, NULL, 0, NULL, 0,
					  status_code, false, NULL);
	} else if (wdev->sme_state == CFG80211_SME_CONNECTING &&
		 wdev->conn->state == CFG80211_CONN_AUTHENTICATING) {
		wdev->conn->state = CFG80211_CONN_ASSOCIATE_NEXT;
		schedule_work(&rdev->conn_work);
	}
}

bool cfg80211_sme_failed_reassoc(struct wireless_dev *wdev)
{
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	if (WARN_ON(!wdev->conn))
		return false;

	if (!wdev->conn->prev_bssid_valid)
		return false;

	/*
	 * Some stupid APs don't accept reassoc, so we
	 * need to fall back to trying regular assoc.
	 */
	wdev->conn->prev_bssid_valid = false;
	wdev->conn->state = CFG80211_CONN_ASSOCIATE_NEXT;
	schedule_work(&rdev->conn_work);

	return true;
}

void cfg80211_sme_failed_assoc(struct wireless_dev *wdev)
{
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	wdev->conn->state = CFG80211_CONN_DEAUTH_ASSOC_FAIL;
	schedule_work(&rdev->conn_work);
}

void __cfg80211_connect_result(struct net_device *dev, const u8 *bssid,
			       const u8 *req_ie, size_t req_ie_len,
			       const u8 *resp_ie, size_t resp_ie_len,
			       u16 status, bool wextev,
			       struct cfg80211_bss *bss)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	u8 *country_ie;
#ifdef CONFIG_CFG80211_WEXT
	union iwreq_data wrqu;
#endif

	ASSERT_WDEV_LOCK(wdev);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_STATION))
		return;

	if (wdev->sme_state != CFG80211_SME_CONNECTING)
		return;

	nl80211_send_connect_result(wiphy_to_dev(wdev->wiphy), dev,
				    bssid, req_ie, req_ie_len,
				    resp_ie, resp_ie_len,
				    status, GFP_KERNEL);

#ifdef CONFIG_CFG80211_WEXT
	if (wextev) {
		if (req_ie && status == WLAN_STATUS_SUCCESS) {
			memset(&wrqu, 0, sizeof(wrqu));
			wrqu.data.length = req_ie_len;
			wireless_send_event(dev, IWEVASSOCREQIE, &wrqu, req_ie);
		}

		if (resp_ie && status == WLAN_STATUS_SUCCESS) {
			memset(&wrqu, 0, sizeof(wrqu));
			wrqu.data.length = resp_ie_len;
			wireless_send_event(dev, IWEVASSOCRESPIE, &wrqu, resp_ie);
		}

		memset(&wrqu, 0, sizeof(wrqu));
		wrqu.ap_addr.sa_family = ARPHRD_ETHER;
		if (bssid && status == WLAN_STATUS_SUCCESS) {
			memcpy(wrqu.ap_addr.sa_data, bssid, ETH_ALEN);
			memcpy(wdev->wext.prev_bssid, bssid, ETH_ALEN);
			wdev->wext.prev_bssid_valid = true;
		}
		wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);
	}
#endif

	if (wdev->current_bss) {
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(&wdev->current_bss->pub);
		wdev->current_bss = NULL;
	}

	if (wdev->conn)
		wdev->conn->state = CFG80211_CONN_IDLE;

	if (status != WLAN_STATUS_SUCCESS) {
		wdev->sme_state = CFG80211_SME_IDLE;
		if (wdev->conn)
			kfree(wdev->conn->ie);
		kfree(wdev->conn);
		wdev->conn = NULL;
		kfree(wdev->connect_keys);
		wdev->connect_keys = NULL;
		wdev->ssid_len = 0;
		return;
	}

	if (!bss)
		bss = cfg80211_get_bss(wdev->wiphy, NULL, bssid,
				       wdev->ssid, wdev->ssid_len,
				       WLAN_CAPABILITY_ESS,
				       WLAN_CAPABILITY_ESS);

	if (WARN_ON(!bss))
		return;

	cfg80211_hold_bss(bss_from_pub(bss));
	wdev->current_bss = bss_from_pub(bss);

	wdev->sme_state = CFG80211_SME_CONNECTED;
	cfg80211_upload_connect_keys(wdev);

	country_ie = (u8 *) ieee80211_bss_get_ie(bss, WLAN_EID_COUNTRY);

	if (!country_ie)
		return;

	/*
	 * ieee80211_bss_get_ie() ensures we can access:
	 * - country_ie + 2, the start of the country ie data, and
	 * - and country_ie[1] which is the IE length
	 */
	regulatory_hint_11d(wdev->wiphy,
			    country_ie + 2,
			    country_ie[1]);
}

void cfg80211_connect_result(struct net_device *dev, const u8 *bssid,
			     const u8 *req_ie, size_t req_ie_len,
			     const u8 *resp_ie, size_t resp_ie_len,
			     u16 status, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	struct cfg80211_event *ev;
	unsigned long flags;

	CFG80211_DEV_WARN_ON(wdev->sme_state != CFG80211_SME_CONNECTING);

	ev = kzalloc(sizeof(*ev) + req_ie_len + resp_ie_len, gfp);
	if (!ev)
		return;

	ev->type = EVENT_CONNECT_RESULT;
	if (bssid)
		memcpy(ev->cr.bssid, bssid, ETH_ALEN);
	ev->cr.req_ie = ((u8 *)ev) + sizeof(*ev);
	ev->cr.req_ie_len = req_ie_len;
	memcpy((void *)ev->cr.req_ie, req_ie, req_ie_len);
	ev->cr.resp_ie = ((u8 *)ev) + sizeof(*ev) + req_ie_len;
	ev->cr.resp_ie_len = resp_ie_len;
	memcpy((void *)ev->cr.resp_ie, resp_ie, resp_ie_len);
	ev->cr.status = status;

	spin_lock_irqsave(&wdev->event_lock, flags);
	list_add_tail(&ev->list, &wdev->event_list);
	spin_unlock_irqrestore(&wdev->event_lock, flags);
	schedule_work(&rdev->event_work);
}
EXPORT_SYMBOL(cfg80211_connect_result);

void __cfg80211_roamed(struct wireless_dev *wdev, const u8 *bssid,
		       const u8 *req_ie, size_t req_ie_len,
		       const u8 *resp_ie, size_t resp_ie_len)
{
	struct cfg80211_bss *bss;
#ifdef CONFIG_CFG80211_WEXT
	union iwreq_data wrqu;
#endif

	ASSERT_WDEV_LOCK(wdev);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_STATION))
		return;

	if (wdev->sme_state != CFG80211_SME_CONNECTED)
		return;

	/* internal error -- how did we get to CONNECTED w/o BSS? */
	if (WARN_ON(!wdev->current_bss)) {
		return;
	}

	cfg80211_unhold_bss(wdev->current_bss);
	cfg80211_put_bss(&wdev->current_bss->pub);
	wdev->current_bss = NULL;

	bss = cfg80211_get_bss(wdev->wiphy, NULL, bssid,
			       wdev->ssid, wdev->ssid_len,
			       WLAN_CAPABILITY_ESS, WLAN_CAPABILITY_ESS);

	if (WARN_ON(!bss))
		return;

	cfg80211_hold_bss(bss_from_pub(bss));
	wdev->current_bss = bss_from_pub(bss);

	nl80211_send_roamed(wiphy_to_dev(wdev->wiphy), wdev->netdev, bssid,
			    req_ie, req_ie_len, resp_ie, resp_ie_len,
			    GFP_KERNEL);

#ifdef CONFIG_CFG80211_WEXT
	if (req_ie) {
		memset(&wrqu, 0, sizeof(wrqu));
		wrqu.data.length = req_ie_len;
		wireless_send_event(wdev->netdev, IWEVASSOCREQIE,
				    &wrqu, req_ie);
	}

	if (resp_ie) {
		memset(&wrqu, 0, sizeof(wrqu));
		wrqu.data.length = resp_ie_len;
		wireless_send_event(wdev->netdev, IWEVASSOCRESPIE,
				    &wrqu, resp_ie);
	}

	memset(&wrqu, 0, sizeof(wrqu));
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(wrqu.ap_addr.sa_data, bssid, ETH_ALEN);
	memcpy(wdev->wext.prev_bssid, bssid, ETH_ALEN);
	wdev->wext.prev_bssid_valid = true;
	wireless_send_event(wdev->netdev, SIOCGIWAP, &wrqu, NULL);
#endif
}

void cfg80211_roamed(struct net_device *dev, const u8 *bssid,
		     const u8 *req_ie, size_t req_ie_len,
		     const u8 *resp_ie, size_t resp_ie_len, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	struct cfg80211_event *ev;
	unsigned long flags;

	CFG80211_DEV_WARN_ON(wdev->sme_state != CFG80211_SME_CONNECTED);

	ev = kzalloc(sizeof(*ev) + req_ie_len + resp_ie_len, gfp);
	if (!ev)
		return;

	ev->type = EVENT_ROAMED;
	memcpy(ev->rm.bssid, bssid, ETH_ALEN);
	ev->rm.req_ie = ((u8 *)ev) + sizeof(*ev);
	ev->rm.req_ie_len = req_ie_len;
	memcpy((void *)ev->rm.req_ie, req_ie, req_ie_len);
	ev->rm.resp_ie = ((u8 *)ev) + sizeof(*ev) + req_ie_len;
	ev->rm.resp_ie_len = resp_ie_len;
	memcpy((void *)ev->rm.resp_ie, resp_ie, resp_ie_len);

	spin_lock_irqsave(&wdev->event_lock, flags);
	list_add_tail(&ev->list, &wdev->event_list);
	spin_unlock_irqrestore(&wdev->event_lock, flags);
	schedule_work(&rdev->event_work);
}
EXPORT_SYMBOL(cfg80211_roamed);

void __cfg80211_disconnected(struct net_device *dev, const u8 *ie,
			     size_t ie_len, u16 reason, bool from_ap)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	int i;
#ifdef CONFIG_CFG80211_WEXT
	union iwreq_data wrqu;
#endif

	ASSERT_WDEV_LOCK(wdev);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_STATION))
		return;

	if (wdev->sme_state != CFG80211_SME_CONNECTED)
		return;

	if (wdev->current_bss) {
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(&wdev->current_bss->pub);
	}

	wdev->current_bss = NULL;
	wdev->sme_state = CFG80211_SME_IDLE;
	wdev->ssid_len = 0;

	if (wdev->conn) {
		const u8 *bssid;
		int ret;

		kfree(wdev->conn->ie);
		wdev->conn->ie = NULL;
		kfree(wdev->conn);
		wdev->conn = NULL;

		/*
		 * If this disconnect was due to a disassoc, we
		 * we might still have an auth BSS around. For
		 * the userspace SME that's currently expected,
		 * but for the kernel SME (nl80211 CONNECT or
		 * wireless extensions) we want to clear up all
		 * state.
		 */
		for (i = 0; i < MAX_AUTH_BSSES; i++) {
			if (!wdev->auth_bsses[i])
				continue;
			bssid = wdev->auth_bsses[i]->pub.bssid;
			ret = __cfg80211_mlme_deauth(rdev, dev, bssid, NULL, 0,
						WLAN_REASON_DEAUTH_LEAVING);
			WARN(ret, "deauth failed: %d\n", ret);
		}
	}

	nl80211_send_disconnected(rdev, dev, reason, ie, ie_len, from_ap);

	/*
	 * Delete all the keys ... pairwise keys can't really
	 * exist any more anyway, but default keys might.
	 */
	if (rdev->ops->del_key)
		for (i = 0; i < 6; i++)
			rdev->ops->del_key(wdev->wiphy, dev, i, NULL);

#ifdef CONFIG_CFG80211_WEXT
	memset(&wrqu, 0, sizeof(wrqu));
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);
#endif
}

void cfg80211_disconnected(struct net_device *dev, u16 reason,
			   u8 *ie, size_t ie_len, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	struct cfg80211_event *ev;
	unsigned long flags;

	CFG80211_DEV_WARN_ON(wdev->sme_state != CFG80211_SME_CONNECTED);

	ev = kzalloc(sizeof(*ev) + ie_len, gfp);
	if (!ev)
		return;

	ev->type = EVENT_DISCONNECTED;
	ev->dc.ie = ((u8 *)ev) + sizeof(*ev);
	ev->dc.ie_len = ie_len;
	memcpy((void *)ev->dc.ie, ie, ie_len);
	ev->dc.reason = reason;

	spin_lock_irqsave(&wdev->event_lock, flags);
	list_add_tail(&ev->list, &wdev->event_list);
	spin_unlock_irqrestore(&wdev->event_lock, flags);
	schedule_work(&rdev->event_work);
}
EXPORT_SYMBOL(cfg80211_disconnected);

int __cfg80211_connect(struct cfg80211_registered_device *rdev,
		       struct net_device *dev,
		       struct cfg80211_connect_params *connect,
		       struct cfg80211_cached_keys *connkeys,
		       const u8 *prev_bssid)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct ieee80211_channel *chan;
	struct cfg80211_bss *bss = NULL;
	int err;

	ASSERT_WDEV_LOCK(wdev);

	if (wdev->sme_state != CFG80211_SME_IDLE)
		return -EALREADY;

	chan = rdev_fixed_channel(rdev, wdev);
	if (chan && chan != connect->channel)
		return -EBUSY;

	if (WARN_ON(wdev->connect_keys)) {
		kfree(wdev->connect_keys);
		wdev->connect_keys = NULL;
	}

	if (connkeys && connkeys->def >= 0) {
		int idx;
		u32 cipher;

		idx = connkeys->def;
		cipher = connkeys->params[idx].cipher;
		/* If given a WEP key we may need it for shared key auth */
		if (cipher == WLAN_CIPHER_SUITE_WEP40 ||
		    cipher == WLAN_CIPHER_SUITE_WEP104) {
			connect->key_idx = idx;
			connect->key = connkeys->params[idx].key;
			connect->key_len = connkeys->params[idx].key_len;

			/*
			 * If ciphers are not set (e.g. when going through
			 * iwconfig), we have to set them appropriately here.
			 */
			if (connect->crypto.cipher_group == 0)
				connect->crypto.cipher_group = cipher;

			if (connect->crypto.n_ciphers_pairwise == 0) {
				connect->crypto.n_ciphers_pairwise = 1;
				connect->crypto.ciphers_pairwise[0] = cipher;
			}
		}
	}

	if (!rdev->ops->connect) {
		if (!rdev->ops->auth || !rdev->ops->assoc)
			return -EOPNOTSUPP;

		if (WARN_ON(wdev->conn))
			return -EINPROGRESS;

		wdev->conn = kzalloc(sizeof(*wdev->conn), GFP_KERNEL);
		if (!wdev->conn)
			return -ENOMEM;

		/*
		 * Copy all parameters, and treat explicitly IEs, BSSID, SSID.
		 */
		memcpy(&wdev->conn->params, connect, sizeof(*connect));
		if (connect->bssid) {
			wdev->conn->params.bssid = wdev->conn->bssid;
			memcpy(wdev->conn->bssid, connect->bssid, ETH_ALEN);
		}

		if (connect->ie) {
			wdev->conn->ie = kmemdup(connect->ie, connect->ie_len,
						GFP_KERNEL);
			wdev->conn->params.ie = wdev->conn->ie;
			if (!wdev->conn->ie) {
				kfree(wdev->conn);
				wdev->conn = NULL;
				return -ENOMEM;
			}
		}

		if (connect->auth_type == NL80211_AUTHTYPE_AUTOMATIC) {
			wdev->conn->auto_auth = true;
			/* start with open system ... should mostly work */
			wdev->conn->params.auth_type =
				NL80211_AUTHTYPE_OPEN_SYSTEM;
		} else {
			wdev->conn->auto_auth = false;
		}

		memcpy(wdev->ssid, connect->ssid, connect->ssid_len);
		wdev->ssid_len = connect->ssid_len;
		wdev->conn->params.ssid = wdev->ssid;
		wdev->conn->params.ssid_len = connect->ssid_len;

		/* see if we have the bss already */
		bss = cfg80211_get_conn_bss(wdev);

		wdev->sme_state = CFG80211_SME_CONNECTING;
		wdev->connect_keys = connkeys;

		if (prev_bssid) {
			memcpy(wdev->conn->prev_bssid, prev_bssid, ETH_ALEN);
			wdev->conn->prev_bssid_valid = true;
		}

		/* we're good if we have a matching bss struct */
		if (bss) {
			wdev->conn->state = CFG80211_CONN_AUTHENTICATE_NEXT;
			err = cfg80211_conn_do_work(wdev);
			cfg80211_put_bss(bss);
		} else {
			/* otherwise we'll need to scan for the AP first */
			err = cfg80211_conn_scan(wdev);
			/*
			 * If we can't scan right now, then we need to scan again
			 * after the current scan finished, since the parameters
			 * changed (unless we find a good AP anyway).
			 */
			if (err == -EBUSY) {
				err = 0;
				wdev->conn->state = CFG80211_CONN_SCAN_AGAIN;
			}
		}
		if (err) {
			kfree(wdev->conn->ie);
			kfree(wdev->conn);
			wdev->conn = NULL;
			wdev->sme_state = CFG80211_SME_IDLE;
			wdev->connect_keys = NULL;
			wdev->ssid_len = 0;
		}

		return err;
	} else {
		wdev->sme_state = CFG80211_SME_CONNECTING;
		wdev->connect_keys = connkeys;
		err = rdev->ops->connect(&rdev->wiphy, dev, connect);
		if (err) {
			wdev->connect_keys = NULL;
			wdev->sme_state = CFG80211_SME_IDLE;
			return err;
		}

		memcpy(wdev->ssid, connect->ssid, connect->ssid_len);
		wdev->ssid_len = connect->ssid_len;

		return 0;
	}
}

int cfg80211_connect(struct cfg80211_registered_device *rdev,
		     struct net_device *dev,
		     struct cfg80211_connect_params *connect,
		     struct cfg80211_cached_keys *connkeys)
{
	int err;

	mutex_lock(&rdev->devlist_mtx);
	wdev_lock(dev->ieee80211_ptr);
	err = __cfg80211_connect(rdev, dev, connect, connkeys, NULL);
	wdev_unlock(dev->ieee80211_ptr);
	mutex_unlock(&rdev->devlist_mtx);

	return err;
}

int __cfg80211_disconnect(struct cfg80211_registered_device *rdev,
			  struct net_device *dev, u16 reason, bool wextev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	ASSERT_WDEV_LOCK(wdev);

	if (wdev->sme_state == CFG80211_SME_IDLE)
		return -EINVAL;

	kfree(wdev->connect_keys);
	wdev->connect_keys = NULL;

	if (!rdev->ops->disconnect) {
		if (!rdev->ops->deauth)
			return -EOPNOTSUPP;

		/* was it connected by userspace SME? */
		if (!wdev->conn) {
			cfg80211_mlme_down(rdev, dev);
			return 0;
		}

		if (wdev->sme_state == CFG80211_SME_CONNECTING &&
		    (wdev->conn->state == CFG80211_CONN_SCANNING ||
		     wdev->conn->state == CFG80211_CONN_SCAN_AGAIN)) {
			wdev->sme_state = CFG80211_SME_IDLE;
			kfree(wdev->conn->ie);
			kfree(wdev->conn);
			wdev->conn = NULL;
			wdev->ssid_len = 0;
			return 0;
		}

		/* wdev->conn->params.bssid must be set if > SCANNING */
		err = __cfg80211_mlme_deauth(rdev, dev,
					     wdev->conn->params.bssid,
					     NULL, 0, reason);
		if (err)
			return err;
	} else {
		err = rdev->ops->disconnect(&rdev->wiphy, dev, reason);
		if (err)
			return err;
	}

	if (wdev->sme_state == CFG80211_SME_CONNECTED)
		__cfg80211_disconnected(dev, NULL, 0, 0, false);
	else if (wdev->sme_state == CFG80211_SME_CONNECTING)
		__cfg80211_connect_result(dev, NULL, NULL, 0, NULL, 0,
					  WLAN_STATUS_UNSPECIFIED_FAILURE,
					  wextev, NULL);

	return 0;
}

int cfg80211_disconnect(struct cfg80211_registered_device *rdev,
			struct net_device *dev,
			u16 reason, bool wextev)
{
	int err;

	wdev_lock(dev->ieee80211_ptr);
	err = __cfg80211_disconnect(rdev, dev, reason, wextev);
	wdev_unlock(dev->ieee80211_ptr);

	return err;
}

void cfg80211_sme_disassoc(struct net_device *dev, int idx)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	u8 bssid[ETH_ALEN];

	ASSERT_WDEV_LOCK(wdev);

	if (!wdev->conn)
		return;

	if (wdev->conn->state == CFG80211_CONN_IDLE)
		return;

	/*
	 * Ok, so the association was made by this SME -- we don't
	 * want it any more so deauthenticate too.
	 */

	if (!wdev->auth_bsses[idx])
		return;

	memcpy(bssid, wdev->auth_bsses[idx]->pub.bssid, ETH_ALEN);
	if (__cfg80211_mlme_deauth(rdev, dev, bssid,
				   NULL, 0, WLAN_REASON_DEAUTH_LEAVING)) {
		/* whatever -- assume gone anyway */
		cfg80211_unhold_bss(wdev->auth_bsses[idx]);
		cfg80211_put_bss(&wdev->auth_bsses[idx]->pub);
		wdev->auth_bsses[idx] = NULL;
	}
}
