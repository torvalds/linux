/*
 * SME code for cfg80211's connect emulation.
 *
 * Copyright 2009	Johannes Berg <johannes@sipsolutions.net>
 * Copyright (C) 2009   Intel Corporation. All rights reserved.
 */

#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/workqueue.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>
#include "nl80211.h"

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
	} state;
	u8 bssid[ETH_ALEN];
	u8 *ie;
	size_t ie_len;
	bool auto_auth;
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

	request->channels = (void *)((char *)request + sizeof(*request));
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
	request->ssids = (void *)(request->channels + n_channels);
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
		/*
		 * We could, later, implement roaming here and then actually
		 * set prev_bssid to non-NULL. But then we need to be aware
		 * that some APs don't like that -- so we'd need to retry
		 * the association.
		 */
		err = __cfg80211_mlme_assoc(rdev, wdev->netdev,
					    params->channel, params->bssid,
					    NULL,
					    params->ssid, params->ssid_len,
					    params->ie, params->ie_len,
					    false, &params->crypto);
		if (err)
			__cfg80211_mlme_deauth(rdev, wdev->netdev, params->bssid,
					       NULL, 0,
					       WLAN_REASON_DEAUTH_LEAVING);
		return err;
	default:
		return 0;
	}
}

void cfg80211_conn_work(struct work_struct *work)
{
	struct cfg80211_registered_device *rdev =
		container_of(work, struct cfg80211_registered_device, conn_work);
	struct wireless_dev *wdev;

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
		if (cfg80211_conn_do_work(wdev))
			__cfg80211_connect_result(
					wdev->netdev,
					wdev->conn->params.bssid,
					NULL, 0, NULL, 0,
					WLAN_STATUS_UNSPECIFIED_FAILURE,
					false);
		wdev_unlock(wdev);
	}

	mutex_unlock(&rdev->devlist_mtx);
	cfg80211_unlock_rdev(rdev);
	rtnl_unlock();
}

static bool cfg80211_get_conn_bss(struct wireless_dev *wdev)
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
		return false;

	memcpy(wdev->conn->bssid, bss->bssid, ETH_ALEN);
	wdev->conn->params.bssid = wdev->conn->bssid;
	wdev->conn->params.channel = bss->channel;
	wdev->conn->state = CFG80211_CONN_AUTHENTICATE_NEXT;
	schedule_work(&rdev->conn_work);

	cfg80211_put_bss(bss);
	return true;
}

static void __cfg80211_sme_scan_done(struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);

	ASSERT_WDEV_LOCK(wdev);

	if (wdev->sme_state != CFG80211_SME_CONNECTING)
		return;

	if (!wdev->conn)
		return;

	if (wdev->conn->state != CFG80211_CONN_SCANNING &&
	    wdev->conn->state != CFG80211_CONN_SCAN_AGAIN)
		return;

	if (!cfg80211_get_conn_bss(wdev)) {
		/* not found */
		if (wdev->conn->state == CFG80211_CONN_SCAN_AGAIN)
			schedule_work(&rdev->conn_work);
		else
			__cfg80211_connect_result(
					wdev->netdev,
					wdev->conn->params.bssid,
					NULL, 0, NULL, 0,
					WLAN_STATUS_UNSPECIFIED_FAILURE,
					false);
	}
}

void cfg80211_sme_scan_done(struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	wdev_lock(wdev);
	__cfg80211_sme_scan_done(dev);
	wdev_unlock(wdev);
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
					  status_code, false);
	} else if (wdev->sme_state == CFG80211_SME_CONNECTING &&
		 wdev->conn->state == CFG80211_CONN_AUTHENTICATING) {
		wdev->conn->state = CFG80211_CONN_ASSOCIATE_NEXT;
		schedule_work(&rdev->conn_work);
	}
}

void __cfg80211_connect_result(struct net_device *dev, const u8 *bssid,
			       const u8 *req_ie, size_t req_ie_len,
			       const u8 *resp_ie, size_t resp_ie_len,
			       u16 status, bool wextev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_bss *bss;
#ifdef CONFIG_WIRELESS_EXT
	union iwreq_data wrqu;
#endif

	ASSERT_WDEV_LOCK(wdev);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_STATION))
		return;

	if (wdev->sme_state == CFG80211_SME_CONNECTED)
		nl80211_send_roamed(wiphy_to_dev(wdev->wiphy), dev,
				    bssid, req_ie, req_ie_len,
				    resp_ie, resp_ie_len, GFP_KERNEL);
	else
		nl80211_send_connect_result(wiphy_to_dev(wdev->wiphy), dev,
					    bssid, req_ie, req_ie_len,
					    resp_ie, resp_ie_len,
					    status, GFP_KERNEL);

#ifdef CONFIG_WIRELESS_EXT
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
		if (bssid && status == WLAN_STATUS_SUCCESS)
			memcpy(wrqu.ap_addr.sa_data, bssid, ETH_ALEN);
		wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);
	}
#endif

	if (status == WLAN_STATUS_SUCCESS &&
	    wdev->sme_state == CFG80211_SME_IDLE)
		goto success;

	if (wdev->sme_state != CFG80211_SME_CONNECTING)
		return;

	if (wdev->current_bss) {
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(&wdev->current_bss->pub);
		wdev->current_bss = NULL;
	}

	if (wdev->conn)
		wdev->conn->state = CFG80211_CONN_IDLE;

	if (status != WLAN_STATUS_SUCCESS) {
		wdev->sme_state = CFG80211_SME_IDLE;
		kfree(wdev->conn);
		wdev->conn = NULL;
		kfree(wdev->connect_keys);
		wdev->connect_keys = NULL;
		return;
	}

	bss = cfg80211_get_bss(wdev->wiphy, NULL, bssid,
			       wdev->ssid, wdev->ssid_len,
			       WLAN_CAPABILITY_ESS,
			       WLAN_CAPABILITY_ESS);

	if (WARN_ON(!bss))
		return;

	cfg80211_hold_bss(bss_from_pub(bss));
	wdev->current_bss = bss_from_pub(bss);

 success:
	wdev->sme_state = CFG80211_SME_CONNECTED;
	cfg80211_upload_connect_keys(wdev);
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

	ev = kzalloc(sizeof(*ev) + req_ie_len + resp_ie_len, gfp);
	if (!ev)
		return;

	ev->type = EVENT_CONNECT_RESULT;
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
#ifdef CONFIG_WIRELESS_EXT
	union iwreq_data wrqu;
#endif

	ASSERT_WDEV_LOCK(wdev);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_STATION))
		return;

	if (WARN_ON(wdev->sme_state != CFG80211_SME_CONNECTED))
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

#ifdef CONFIG_WIRELESS_EXT
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
#ifdef CONFIG_WIRELESS_EXT
	union iwreq_data wrqu;
#endif

	ASSERT_WDEV_LOCK(wdev);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_STATION))
		return;

	if (WARN_ON(wdev->sme_state != CFG80211_SME_CONNECTED))
		return;

	if (wdev->current_bss) {
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(&wdev->current_bss->pub);
	}

	wdev->current_bss = NULL;
	wdev->sme_state = CFG80211_SME_IDLE;

	if (wdev->conn) {
		kfree(wdev->conn->ie);
		wdev->conn->ie = NULL;
		kfree(wdev->conn);
		wdev->conn = NULL;
	}

	nl80211_send_disconnected(rdev, dev, reason, ie, ie_len, from_ap);

	/*
	 * Delete all the keys ... pairwise keys can't really
	 * exist any more anyway, but default keys might.
	 */
	if (rdev->ops->del_key)
		for (i = 0; i < 6; i++)
			rdev->ops->del_key(wdev->wiphy, dev, i, NULL);

#ifdef CONFIG_WIRELESS_EXT
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
		       struct cfg80211_cached_keys *connkeys)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	ASSERT_WDEV_LOCK(wdev);

	if (wdev->sme_state != CFG80211_SME_IDLE)
		return -EALREADY;

	if (WARN_ON(wdev->connect_keys)) {
		kfree(wdev->connect_keys);
		wdev->connect_keys = NULL;
	}

	if (connkeys && connkeys->def >= 0) {
		int idx;

		idx = connkeys->def;
		/* If given a WEP key we may need it for shared key auth */
		if (connkeys->params[idx].cipher == WLAN_CIPHER_SUITE_WEP40 ||
		    connkeys->params[idx].cipher == WLAN_CIPHER_SUITE_WEP104) {
			connect->key_idx = idx;
			connect->key = connkeys->params[idx].key;
			connect->key_len = connkeys->params[idx].key_len;
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

		/* don't care about result -- but fill bssid & channel */
		if (!wdev->conn->params.bssid || !wdev->conn->params.channel)
			cfg80211_get_conn_bss(wdev);

		wdev->sme_state = CFG80211_SME_CONNECTING;
		wdev->connect_keys = connkeys;

		/* we're good if we have both BSSID and channel */
		if (wdev->conn->params.bssid && wdev->conn->params.channel) {
			wdev->conn->state = CFG80211_CONN_AUTHENTICATE_NEXT;
			err = cfg80211_conn_do_work(wdev);
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
			kfree(wdev->conn);
			wdev->conn = NULL;
			wdev->sme_state = CFG80211_SME_IDLE;
			wdev->connect_keys = NULL;
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

	wdev_lock(dev->ieee80211_ptr);
	err = __cfg80211_connect(rdev, dev, connect, connkeys);
	wdev_unlock(dev->ieee80211_ptr);

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
			kfree(wdev->conn);
			wdev->conn = NULL;
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
					  wextev);

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
