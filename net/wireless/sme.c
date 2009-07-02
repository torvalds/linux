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
	struct cfg80211_registered_device *drv = wiphy_to_dev(wdev->wiphy);
	struct cfg80211_scan_request *request;
	int n_channels, err;

	ASSERT_RTNL();

	if (drv->scan_req)
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

	request->ifidx = wdev->netdev->ifindex;
	request->wiphy = &drv->wiphy;

	drv->scan_req = request;

	err = drv->ops->scan(wdev->wiphy, wdev->netdev, request);
	if (!err) {
		wdev->conn->state = CFG80211_CONN_SCANNING;
		nl80211_send_scan_start(drv, wdev->netdev);
	} else {
		drv->scan_req = NULL;
		kfree(request);
	}
	return err;
}

static int cfg80211_conn_do_work(struct wireless_dev *wdev)
{
	struct cfg80211_registered_device *drv = wiphy_to_dev(wdev->wiphy);
	struct cfg80211_connect_params *params;
	int err;

	if (!wdev->conn)
		return 0;

	params = &wdev->conn->params;

	switch (wdev->conn->state) {
	case CFG80211_CONN_SCAN_AGAIN:
		return cfg80211_conn_scan(wdev);
	case CFG80211_CONN_AUTHENTICATE_NEXT:
		BUG_ON(!drv->ops->auth);
		wdev->conn->state = CFG80211_CONN_AUTHENTICATING;
		return cfg80211_mlme_auth(drv, wdev->netdev,
					  params->channel, params->auth_type,
					  params->bssid,
					  params->ssid, params->ssid_len,
					  NULL, 0);
	case CFG80211_CONN_ASSOCIATE_NEXT:
		BUG_ON(!drv->ops->assoc);
		wdev->conn->state = CFG80211_CONN_ASSOCIATING;
		err = cfg80211_mlme_assoc(drv, wdev->netdev,
					  params->channel, params->bssid,
					  params->ssid, params->ssid_len,
					  params->ie, params->ie_len,
					  false, &params->crypto);
		if (err)
			cfg80211_mlme_deauth(drv, wdev->netdev, params->bssid,
					     NULL, 0, WLAN_REASON_DEAUTH_LEAVING);
		return err;
	default:
		return 0;
	}
}

void cfg80211_conn_work(struct work_struct *work)
{
	struct cfg80211_registered_device *drv =
		container_of(work, struct cfg80211_registered_device, conn_work);
	struct wireless_dev *wdev;

	rtnl_lock();
	mutex_lock(&drv->devlist_mtx);

	list_for_each_entry(wdev, &drv->netdev_list, list) {
		if (!netif_running(wdev->netdev))
			continue;
		if (wdev->sme_state != CFG80211_SME_CONNECTING)
			continue;
		if (cfg80211_conn_do_work(wdev))
			cfg80211_connect_result(wdev->netdev,
						wdev->conn->params.bssid,
						NULL, 0, NULL, 0,
						WLAN_STATUS_UNSPECIFIED_FAILURE,
						GFP_ATOMIC);
	}

	mutex_unlock(&drv->devlist_mtx);
	rtnl_unlock();
}

static bool cfg80211_get_conn_bss(struct wireless_dev *wdev)
{
	struct cfg80211_registered_device *drv = wiphy_to_dev(wdev->wiphy);
	struct cfg80211_bss *bss;
	u16 capa = WLAN_CAPABILITY_ESS;

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
	schedule_work(&drv->conn_work);

	cfg80211_put_bss(bss);
	return true;
}

void cfg80211_sme_scan_done(struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *drv = wiphy_to_dev(wdev->wiphy);

	if (wdev->sme_state != CFG80211_SME_CONNECTING)
		return;

	if (WARN_ON(!wdev->conn))
		return;

	if (wdev->conn->state != CFG80211_CONN_SCANNING &&
	    wdev->conn->state != CFG80211_CONN_SCAN_AGAIN)
		return;

	if (!cfg80211_get_conn_bss(wdev)) {
		/* not found */
		if (wdev->conn->state == CFG80211_CONN_SCAN_AGAIN)
			schedule_work(&drv->conn_work);
		else
			cfg80211_connect_result(dev, wdev->conn->params.bssid,
						NULL, 0, NULL, 0,
						WLAN_STATUS_UNSPECIFIED_FAILURE,
						GFP_ATOMIC);
		return;
	}
}

void cfg80211_sme_rx_auth(struct net_device *dev, const u8 *buf, size_t len)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)buf;
	u16 status_code = le16_to_cpu(mgmt->u.auth.status_code);

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
			wdev->conn->params.auth_type =
				NL80211_AUTHTYPE_SHARED_KEY;
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
		wdev->sme_state = CFG80211_SME_IDLE;
		kfree(wdev->conn);
		wdev->conn = NULL;
	} else if (wdev->sme_state == CFG80211_SME_CONNECTING &&
		 wdev->conn->state == CFG80211_CONN_AUTHENTICATING) {
		wdev->conn->state = CFG80211_CONN_ASSOCIATE_NEXT;
		schedule_work(&rdev->conn_work);
	}
}

static void __cfg80211_connect_result(struct net_device *dev, const u8 *bssid,
				      const u8 *req_ie, size_t req_ie_len,
				      const u8 *resp_ie, size_t resp_ie_len,
				      u16 status, bool wextev, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_bss *bss;
#ifdef CONFIG_WIRELESS_EXT
	union iwreq_data wrqu;
#endif

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_STATION))
		return;

	if (wdev->sme_state == CFG80211_SME_CONNECTED)
		nl80211_send_roamed(wiphy_to_dev(wdev->wiphy), dev,
				    bssid, req_ie, req_ie_len,
				    resp_ie, resp_ie_len, gfp);
	else
		nl80211_send_connect_result(wiphy_to_dev(wdev->wiphy), dev,
					    bssid, req_ie, req_ie_len,
					    resp_ie, resp_ie_len,
					    status, gfp);

#ifdef CONFIG_WIRELESS_EXT
	if (wextev) {
		if (req_ie && status == WLAN_STATUS_SUCCESS) {
			memset(&wrqu, 0, sizeof(wrqu));
			wrqu.data.length = req_ie_len;
			wireless_send_event(dev, IWEVASSOCRESPIE, &wrqu, req_ie);
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
	    wdev->sme_state == CFG80211_SME_IDLE) {
		wdev->sme_state = CFG80211_SME_CONNECTED;
		return;
	}

	if (wdev->sme_state != CFG80211_SME_CONNECTING)
		return;

	if (wdev->current_bss) {
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(&wdev->current_bss->pub);
		wdev->current_bss = NULL;
	}

	if (wdev->conn)
		wdev->conn->state = CFG80211_CONN_IDLE;

	if (status == WLAN_STATUS_SUCCESS) {
		bss = cfg80211_get_bss(wdev->wiphy, NULL, bssid,
				       wdev->ssid, wdev->ssid_len,
				       WLAN_CAPABILITY_ESS,
				       WLAN_CAPABILITY_ESS);

		if (WARN_ON(!bss))
			return;

		cfg80211_hold_bss(bss_from_pub(bss));
		wdev->current_bss = bss_from_pub(bss);

		wdev->sme_state = CFG80211_SME_CONNECTED;
	} else {
		wdev->sme_state = CFG80211_SME_IDLE;
		kfree(wdev->conn);
		wdev->conn = NULL;
	}
}

void cfg80211_connect_result(struct net_device *dev, const u8 *bssid,
			     const u8 *req_ie, size_t req_ie_len,
			     const u8 *resp_ie, size_t resp_ie_len,
			     u16 status, gfp_t gfp)
{
	bool wextev = status == WLAN_STATUS_SUCCESS;
	__cfg80211_connect_result(dev, bssid, req_ie, req_ie_len, resp_ie, resp_ie_len, status, wextev, gfp);
}
EXPORT_SYMBOL(cfg80211_connect_result);

void cfg80211_roamed(struct net_device *dev, const u8 *bssid,
		     const u8 *req_ie, size_t req_ie_len,
		     const u8 *resp_ie, size_t resp_ie_len, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_bss *bss;
#ifdef CONFIG_WIRELESS_EXT
	union iwreq_data wrqu;
#endif

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

	nl80211_send_roamed(wiphy_to_dev(wdev->wiphy), dev, bssid,
			    req_ie, req_ie_len, resp_ie, resp_ie_len, gfp);

#ifdef CONFIG_WIRELESS_EXT
	if (req_ie) {
		memset(&wrqu, 0, sizeof(wrqu));
		wrqu.data.length = req_ie_len;
		wireless_send_event(dev, IWEVASSOCRESPIE, &wrqu, req_ie);
	}

	if (resp_ie) {
		memset(&wrqu, 0, sizeof(wrqu));
		wrqu.data.length = resp_ie_len;
		wireless_send_event(dev, IWEVASSOCRESPIE, &wrqu, resp_ie);
	}

	memset(&wrqu, 0, sizeof(wrqu));
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(wrqu.ap_addr.sa_data, bssid, ETH_ALEN);
	wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);
#endif
}
EXPORT_SYMBOL(cfg80211_roamed);

void __cfg80211_disconnected(struct net_device *dev, gfp_t gfp, u8 *ie,
			     size_t ie_len, u16 reason, bool from_ap)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
#ifdef CONFIG_WIRELESS_EXT
	union iwreq_data wrqu;
#endif

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

	nl80211_send_disconnected(wiphy_to_dev(wdev->wiphy), dev,
				  reason, ie, ie_len, from_ap, gfp);

#ifdef CONFIG_WIRELESS_EXT
	memset(&wrqu, 0, sizeof(wrqu));
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);
#endif
}

void cfg80211_disconnected(struct net_device *dev, u16 reason,
			   u8 *ie, size_t ie_len, gfp_t gfp)
{
	__cfg80211_disconnected(dev, gfp, ie, ie_len, reason, true);
}
EXPORT_SYMBOL(cfg80211_disconnected);

int cfg80211_connect(struct cfg80211_registered_device *rdev,
		     struct net_device *dev,
		     struct cfg80211_connect_params *connect)
{
	int err;
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	if (wdev->sme_state != CFG80211_SME_IDLE)
		return -EALREADY;

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
		}

		return err;
	} else {
		wdev->sme_state = CFG80211_SME_CONNECTING;
		err = rdev->ops->connect(&rdev->wiphy, dev, connect);
		if (err) {
			wdev->sme_state = CFG80211_SME_IDLE;
			return err;
		}

		memcpy(wdev->ssid, connect->ssid, connect->ssid_len);
		wdev->ssid_len = connect->ssid_len;

		return 0;
	}
}

int cfg80211_disconnect(struct cfg80211_registered_device *rdev,
			struct net_device *dev, u16 reason, bool wextev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	if (wdev->sme_state == CFG80211_SME_IDLE)
		return -EINVAL;

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
		err = cfg80211_mlme_deauth(rdev, dev, wdev->conn->params.bssid,
					   NULL, 0, reason);
		if (err)
			return err;
	} else {
		err = rdev->ops->disconnect(&rdev->wiphy, dev, reason);
		if (err)
			return err;
	}

	if (wdev->sme_state == CFG80211_SME_CONNECTED)
		__cfg80211_disconnected(dev, GFP_KERNEL, NULL, 0, 0, false);
	else if (wdev->sme_state == CFG80211_SME_CONNECTING)
		__cfg80211_connect_result(dev, NULL, NULL, 0, NULL, 0,
					  WLAN_STATUS_UNSPECIFIED_FAILURE,
					  wextev, GFP_KERNEL);

	return 0;
}

void cfg80211_sme_disassoc(struct net_device *dev, int idx)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	u8 bssid[ETH_ALEN];

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
	if (cfg80211_mlme_deauth(rdev, dev, bssid,
				 NULL, 0, WLAN_REASON_DEAUTH_LEAVING)) {
		/* whatever -- assume gone anyway */
		cfg80211_unhold_bss(wdev->auth_bsses[idx]);
		cfg80211_put_bss(&wdev->auth_bsses[idx]->pub);
		wdev->auth_bsses[idx] = NULL;
	}
}
