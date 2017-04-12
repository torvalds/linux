/*
 * SME code for cfg80211
 * both driver SME event handling and the SME implementation
 * (for nl80211's connect() and wext)
 *
 * Copyright 2009	Johannes Berg <johannes@sipsolutions.net>
 * Copyright (C) 2009   Intel Corporation. All rights reserved.
 */

#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/wireless.h>
#include <linux/export.h>
#include <net/iw_handler.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>
#include "nl80211.h"
#include "reg.h"
#include "rdev-ops.h"

/*
 * Software SME in cfg80211, using auth/assoc/deauth calls to the
 * driver. This is is for implementing nl80211's connect/disconnect
 * and wireless extensions (if configured.)
 */

struct cfg80211_conn {
	struct cfg80211_connect_params params;
	/* these are sub-states of the _CONNECTING sme_state */
	enum {
		CFG80211_CONN_SCANNING,
		CFG80211_CONN_SCAN_AGAIN,
		CFG80211_CONN_AUTHENTICATE_NEXT,
		CFG80211_CONN_AUTHENTICATING,
		CFG80211_CONN_AUTH_FAILED_TIMEOUT,
		CFG80211_CONN_ASSOCIATE_NEXT,
		CFG80211_CONN_ASSOCIATING,
		CFG80211_CONN_ASSOC_FAILED,
		CFG80211_CONN_ASSOC_FAILED_TIMEOUT,
		CFG80211_CONN_DEAUTH,
		CFG80211_CONN_ABANDON,
		CFG80211_CONN_CONNECTED,
	} state;
	u8 bssid[ETH_ALEN], prev_bssid[ETH_ALEN];
	const u8 *ie;
	size_t ie_len;
	bool auto_auth, prev_bssid_valid;
};

static void cfg80211_sme_free(struct wireless_dev *wdev)
{
	if (!wdev->conn)
		return;

	kfree(wdev->conn->ie);
	kfree(wdev->conn);
	wdev->conn = NULL;
}

static int cfg80211_conn_scan(struct wireless_dev *wdev)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct cfg80211_scan_request *request;
	int n_channels, err;

	ASSERT_RTNL();
	ASSERT_WDEV_LOCK(wdev);

	if (rdev->scan_req || rdev->scan_msg)
		return -EBUSY;

	if (wdev->conn->params.channel)
		n_channels = 1;
	else
		n_channels = ieee80211_get_num_supported_channels(wdev->wiphy);

	request = kzalloc(sizeof(*request) + sizeof(request->ssids[0]) +
			  sizeof(request->channels[0]) * n_channels,
			  GFP_KERNEL);
	if (!request)
		return -ENOMEM;

	if (wdev->conn->params.channel) {
		enum nl80211_band band = wdev->conn->params.channel->band;
		struct ieee80211_supported_band *sband =
			wdev->wiphy->bands[band];

		if (!sband) {
			kfree(request);
			return -EINVAL;
		}
		request->channels[0] = wdev->conn->params.channel;
		request->rates[band] = (1 << sband->n_bitrates) - 1;
	} else {
		int i = 0, j;
		enum nl80211_band band;
		struct ieee80211_supported_band *bands;
		struct ieee80211_channel *channel;

		for (band = 0; band < NUM_NL80211_BANDS; band++) {
			bands = wdev->wiphy->bands[band];
			if (!bands)
				continue;
			for (j = 0; j < bands->n_channels; j++) {
				channel = &bands->channels[j];
				if (channel->flags & IEEE80211_CHAN_DISABLED)
					continue;
				request->channels[i++] = channel;
			}
			request->rates[band] = (1 << bands->n_bitrates) - 1;
		}
		n_channels = i;
	}
	request->n_channels = n_channels;
	request->ssids = (void *)&request->channels[n_channels];
	request->n_ssids = 1;

	memcpy(request->ssids[0].ssid, wdev->conn->params.ssid,
		wdev->conn->params.ssid_len);
	request->ssids[0].ssid_len = wdev->conn->params.ssid_len;

	eth_broadcast_addr(request->bssid);

	request->wdev = wdev;
	request->wiphy = &rdev->wiphy;
	request->scan_start = jiffies;

	rdev->scan_req = request;

	err = rdev_scan(rdev, request);
	if (!err) {
		wdev->conn->state = CFG80211_CONN_SCANNING;
		nl80211_send_scan_start(rdev, wdev);
		dev_hold(wdev->netdev);
	} else {
		rdev->scan_req = NULL;
		kfree(request);
	}
	return err;
}

static int cfg80211_conn_do_work(struct wireless_dev *wdev,
				 enum nl80211_timeout_reason *treason)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct cfg80211_connect_params *params;
	struct cfg80211_assoc_request req = {};
	int err;

	ASSERT_WDEV_LOCK(wdev);

	if (!wdev->conn)
		return 0;

	params = &wdev->conn->params;

	switch (wdev->conn->state) {
	case CFG80211_CONN_SCANNING:
		/* didn't find it during scan ... */
		return -ENOENT;
	case CFG80211_CONN_SCAN_AGAIN:
		return cfg80211_conn_scan(wdev);
	case CFG80211_CONN_AUTHENTICATE_NEXT:
		if (WARN_ON(!rdev->ops->auth))
			return -EOPNOTSUPP;
		wdev->conn->state = CFG80211_CONN_AUTHENTICATING;
		return cfg80211_mlme_auth(rdev, wdev->netdev,
					  params->channel, params->auth_type,
					  params->bssid,
					  params->ssid, params->ssid_len,
					  NULL, 0,
					  params->key, params->key_len,
					  params->key_idx, NULL, 0);
	case CFG80211_CONN_AUTH_FAILED_TIMEOUT:
		*treason = NL80211_TIMEOUT_AUTH;
		return -ENOTCONN;
	case CFG80211_CONN_ASSOCIATE_NEXT:
		if (WARN_ON(!rdev->ops->assoc))
			return -EOPNOTSUPP;
		wdev->conn->state = CFG80211_CONN_ASSOCIATING;
		if (wdev->conn->prev_bssid_valid)
			req.prev_bssid = wdev->conn->prev_bssid;
		req.ie = params->ie;
		req.ie_len = params->ie_len;
		req.use_mfp = params->mfp != NL80211_MFP_NO;
		req.crypto = params->crypto;
		req.flags = params->flags;
		req.ht_capa = params->ht_capa;
		req.ht_capa_mask = params->ht_capa_mask;
		req.vht_capa = params->vht_capa;
		req.vht_capa_mask = params->vht_capa_mask;

		err = cfg80211_mlme_assoc(rdev, wdev->netdev, params->channel,
					  params->bssid, params->ssid,
					  params->ssid_len, &req);
		if (err)
			cfg80211_mlme_deauth(rdev, wdev->netdev, params->bssid,
					     NULL, 0,
					     WLAN_REASON_DEAUTH_LEAVING,
					     false);
		return err;
	case CFG80211_CONN_ASSOC_FAILED_TIMEOUT:
		*treason = NL80211_TIMEOUT_ASSOC;
		/* fall through */
	case CFG80211_CONN_ASSOC_FAILED:
		cfg80211_mlme_deauth(rdev, wdev->netdev, params->bssid,
				     NULL, 0,
				     WLAN_REASON_DEAUTH_LEAVING, false);
		return -ENOTCONN;
	case CFG80211_CONN_DEAUTH:
		cfg80211_mlme_deauth(rdev, wdev->netdev, params->bssid,
				     NULL, 0,
				     WLAN_REASON_DEAUTH_LEAVING, false);
		/* fall through */
	case CFG80211_CONN_ABANDON:
		/* free directly, disconnected event already sent */
		cfg80211_sme_free(wdev);
		return 0;
	default:
		return 0;
	}
}

void cfg80211_conn_work(struct work_struct *work)
{
	struct cfg80211_registered_device *rdev =
		container_of(work, struct cfg80211_registered_device, conn_work);
	struct wireless_dev *wdev;
	u8 bssid_buf[ETH_ALEN], *bssid = NULL;
	enum nl80211_timeout_reason treason;

	rtnl_lock();

	list_for_each_entry(wdev, &rdev->wiphy.wdev_list, list) {
		if (!wdev->netdev)
			continue;

		wdev_lock(wdev);
		if (!netif_running(wdev->netdev)) {
			wdev_unlock(wdev);
			continue;
		}
		if (!wdev->conn ||
		    wdev->conn->state == CFG80211_CONN_CONNECTED) {
			wdev_unlock(wdev);
			continue;
		}
		if (wdev->conn->params.bssid) {
			memcpy(bssid_buf, wdev->conn->params.bssid, ETH_ALEN);
			bssid = bssid_buf;
		}
		treason = NL80211_TIMEOUT_UNSPECIFIED;
		if (cfg80211_conn_do_work(wdev, &treason)) {
			__cfg80211_connect_result(
					wdev->netdev, bssid,
					NULL, 0, NULL, 0, -1, false, NULL,
					treason);
		}
		wdev_unlock(wdev);
	}

	rtnl_unlock();
}

/* Returned bss is reference counted and must be cleaned up appropriately. */
static struct cfg80211_bss *cfg80211_get_conn_bss(struct wireless_dev *wdev)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct cfg80211_bss *bss;

	ASSERT_WDEV_LOCK(wdev);

	bss = cfg80211_get_bss(wdev->wiphy, wdev->conn->params.channel,
			       wdev->conn->params.bssid,
			       wdev->conn->params.ssid,
			       wdev->conn->params.ssid_len,
			       wdev->conn_bss_type,
			       IEEE80211_PRIVACY(wdev->conn->params.privacy));
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
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct cfg80211_bss *bss;

	ASSERT_WDEV_LOCK(wdev);

	if (!wdev->conn)
		return;

	if (wdev->conn->state != CFG80211_CONN_SCANNING &&
	    wdev->conn->state != CFG80211_CONN_SCAN_AGAIN)
		return;

	bss = cfg80211_get_conn_bss(wdev);
	if (bss)
		cfg80211_put_bss(&rdev->wiphy, bss);
	else
		schedule_work(&rdev->conn_work);
}

void cfg80211_sme_scan_done(struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	wdev_lock(wdev);
	__cfg80211_sme_scan_done(dev);
	wdev_unlock(wdev);
}

void cfg80211_sme_rx_auth(struct wireless_dev *wdev, const u8 *buf, size_t len)
{
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wiphy);
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)buf;
	u16 status_code = le16_to_cpu(mgmt->u.auth.status_code);

	ASSERT_WDEV_LOCK(wdev);

	if (!wdev->conn || wdev->conn->state == CFG80211_CONN_CONNECTED)
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
		__cfg80211_connect_result(wdev->netdev, mgmt->bssid,
					  NULL, 0, NULL, 0,
					  status_code, false, NULL,
					  NL80211_TIMEOUT_UNSPECIFIED);
	} else if (wdev->conn->state == CFG80211_CONN_AUTHENTICATING) {
		wdev->conn->state = CFG80211_CONN_ASSOCIATE_NEXT;
		schedule_work(&rdev->conn_work);
	}
}

bool cfg80211_sme_rx_assoc_resp(struct wireless_dev *wdev, u16 status)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);

	if (!wdev->conn)
		return false;

	if (status == WLAN_STATUS_SUCCESS) {
		wdev->conn->state = CFG80211_CONN_CONNECTED;
		return false;
	}

	if (wdev->conn->prev_bssid_valid) {
		/*
		 * Some stupid APs don't accept reassoc, so we
		 * need to fall back to trying regular assoc;
		 * return true so no event is sent to userspace.
		 */
		wdev->conn->prev_bssid_valid = false;
		wdev->conn->state = CFG80211_CONN_ASSOCIATE_NEXT;
		schedule_work(&rdev->conn_work);
		return true;
	}

	wdev->conn->state = CFG80211_CONN_ASSOC_FAILED;
	schedule_work(&rdev->conn_work);
	return false;
}

void cfg80211_sme_deauth(struct wireless_dev *wdev)
{
	cfg80211_sme_free(wdev);
}

void cfg80211_sme_auth_timeout(struct wireless_dev *wdev)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);

	if (!wdev->conn)
		return;

	wdev->conn->state = CFG80211_CONN_AUTH_FAILED_TIMEOUT;
	schedule_work(&rdev->conn_work);
}

void cfg80211_sme_disassoc(struct wireless_dev *wdev)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);

	if (!wdev->conn)
		return;

	wdev->conn->state = CFG80211_CONN_DEAUTH;
	schedule_work(&rdev->conn_work);
}

void cfg80211_sme_assoc_timeout(struct wireless_dev *wdev)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);

	if (!wdev->conn)
		return;

	wdev->conn->state = CFG80211_CONN_ASSOC_FAILED_TIMEOUT;
	schedule_work(&rdev->conn_work);
}

void cfg80211_sme_abandon_assoc(struct wireless_dev *wdev)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);

	if (!wdev->conn)
		return;

	wdev->conn->state = CFG80211_CONN_ABANDON;
	schedule_work(&rdev->conn_work);
}

static int cfg80211_sme_get_conn_ies(struct wireless_dev *wdev,
				     const u8 *ies, size_t ies_len,
				     const u8 **out_ies, size_t *out_ies_len)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	u8 *buf;
	size_t offs;

	if (!rdev->wiphy.extended_capabilities_len ||
	    (ies && cfg80211_find_ie(WLAN_EID_EXT_CAPABILITY, ies, ies_len))) {
		*out_ies = kmemdup(ies, ies_len, GFP_KERNEL);
		if (!*out_ies)
			return -ENOMEM;
		*out_ies_len = ies_len;
		return 0;
	}

	buf = kmalloc(ies_len + rdev->wiphy.extended_capabilities_len + 2,
		      GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (ies_len) {
		static const u8 before_extcapa[] = {
			/* not listing IEs expected to be created by driver */
			WLAN_EID_RSN,
			WLAN_EID_QOS_CAPA,
			WLAN_EID_RRM_ENABLED_CAPABILITIES,
			WLAN_EID_MOBILITY_DOMAIN,
			WLAN_EID_SUPPORTED_REGULATORY_CLASSES,
			WLAN_EID_BSS_COEX_2040,
		};

		offs = ieee80211_ie_split(ies, ies_len, before_extcapa,
					  ARRAY_SIZE(before_extcapa), 0);
		memcpy(buf, ies, offs);
		/* leave a whole for extended capabilities IE */
		memcpy(buf + offs + rdev->wiphy.extended_capabilities_len + 2,
		       ies + offs, ies_len - offs);
	} else {
		offs = 0;
	}

	/* place extended capabilities IE (with only driver capabilities) */
	buf[offs] = WLAN_EID_EXT_CAPABILITY;
	buf[offs + 1] = rdev->wiphy.extended_capabilities_len;
	memcpy(buf + offs + 2,
	       rdev->wiphy.extended_capabilities,
	       rdev->wiphy.extended_capabilities_len);

	*out_ies = buf;
	*out_ies_len = ies_len + rdev->wiphy.extended_capabilities_len + 2;

	return 0;
}

static int cfg80211_sme_connect(struct wireless_dev *wdev,
				struct cfg80211_connect_params *connect,
				const u8 *prev_bssid)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct cfg80211_bss *bss;
	int err;

	if (!rdev->ops->auth || !rdev->ops->assoc)
		return -EOPNOTSUPP;

	if (wdev->current_bss) {
		if (!prev_bssid)
			return -EALREADY;
		if (prev_bssid &&
		    !ether_addr_equal(prev_bssid, wdev->current_bss->pub.bssid))
			return -ENOTCONN;
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(wdev->wiphy, &wdev->current_bss->pub);
		wdev->current_bss = NULL;

		cfg80211_sme_free(wdev);
	}

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

	if (cfg80211_sme_get_conn_ies(wdev, connect->ie, connect->ie_len,
				      &wdev->conn->ie,
				      &wdev->conn->params.ie_len)) {
		kfree(wdev->conn);
		wdev->conn = NULL;
		return -ENOMEM;
	}
	wdev->conn->params.ie = wdev->conn->ie;

	if (connect->auth_type == NL80211_AUTHTYPE_AUTOMATIC) {
		wdev->conn->auto_auth = true;
		/* start with open system ... should mostly work */
		wdev->conn->params.auth_type =
			NL80211_AUTHTYPE_OPEN_SYSTEM;
	} else {
		wdev->conn->auto_auth = false;
	}

	wdev->conn->params.ssid = wdev->ssid;
	wdev->conn->params.ssid_len = wdev->ssid_len;

	/* see if we have the bss already */
	bss = cfg80211_get_conn_bss(wdev);

	if (prev_bssid) {
		memcpy(wdev->conn->prev_bssid, prev_bssid, ETH_ALEN);
		wdev->conn->prev_bssid_valid = true;
	}

	/* we're good if we have a matching bss struct */
	if (bss) {
		enum nl80211_timeout_reason treason;

		err = cfg80211_conn_do_work(wdev, &treason);
		cfg80211_put_bss(wdev->wiphy, bss);
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

	if (err)
		cfg80211_sme_free(wdev);

	return err;
}

static int cfg80211_sme_disconnect(struct wireless_dev *wdev, u16 reason)
{
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	int err;

	if (!wdev->conn)
		return 0;

	if (!rdev->ops->deauth)
		return -EOPNOTSUPP;

	if (wdev->conn->state == CFG80211_CONN_SCANNING ||
	    wdev->conn->state == CFG80211_CONN_SCAN_AGAIN) {
		err = 0;
		goto out;
	}

	/* wdev->conn->params.bssid must be set if > SCANNING */
	err = cfg80211_mlme_deauth(rdev, wdev->netdev,
				   wdev->conn->params.bssid,
				   NULL, 0, reason, false);
 out:
	cfg80211_sme_free(wdev);
	return err;
}

/*
 * code shared for in-device and software SME
 */

static bool cfg80211_is_all_idle(void)
{
	struct cfg80211_registered_device *rdev;
	struct wireless_dev *wdev;
	bool is_all_idle = true;

	/*
	 * All devices must be idle as otherwise if you are actively
	 * scanning some new beacon hints could be learned and would
	 * count as new regulatory hints.
	 */
	list_for_each_entry(rdev, &cfg80211_rdev_list, list) {
		list_for_each_entry(wdev, &rdev->wiphy.wdev_list, list) {
			wdev_lock(wdev);
			if (wdev->conn || wdev->current_bss)
				is_all_idle = false;
			wdev_unlock(wdev);
		}
	}

	return is_all_idle;
}

static void disconnect_work(struct work_struct *work)
{
	rtnl_lock();
	if (cfg80211_is_all_idle())
		regulatory_hint_disconnect();
	rtnl_unlock();
}

static DECLARE_WORK(cfg80211_disconnect_work, disconnect_work);


/*
 * API calls for drivers implementing connect/disconnect and
 * SME event handling
 */

/* This method must consume bss one way or another */
void __cfg80211_connect_result(struct net_device *dev, const u8 *bssid,
			       const u8 *req_ie, size_t req_ie_len,
			       const u8 *resp_ie, size_t resp_ie_len,
			       int status, bool wextev,
			       struct cfg80211_bss *bss,
			       enum nl80211_timeout_reason timeout_reason)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	const u8 *country_ie;
#ifdef CONFIG_CFG80211_WEXT
	union iwreq_data wrqu;
#endif

	ASSERT_WDEV_LOCK(wdev);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_STATION &&
		    wdev->iftype != NL80211_IFTYPE_P2P_CLIENT)) {
		cfg80211_put_bss(wdev->wiphy, bss);
		return;
	}

	nl80211_send_connect_result(wiphy_to_rdev(wdev->wiphy), dev,
				    bssid, req_ie, req_ie_len,
				    resp_ie, resp_ie_len,
				    status, timeout_reason, GFP_KERNEL);

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

	if (!bss && (status == WLAN_STATUS_SUCCESS)) {
		WARN_ON_ONCE(!wiphy_to_rdev(wdev->wiphy)->ops->connect);
		bss = cfg80211_get_bss(wdev->wiphy, NULL, bssid,
				       wdev->ssid, wdev->ssid_len,
				       wdev->conn_bss_type,
				       IEEE80211_PRIVACY_ANY);
		if (bss)
			cfg80211_hold_bss(bss_from_pub(bss));
	}

	if (wdev->current_bss) {
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(wdev->wiphy, &wdev->current_bss->pub);
		wdev->current_bss = NULL;
	}

	if (status != WLAN_STATUS_SUCCESS) {
		kzfree(wdev->connect_keys);
		wdev->connect_keys = NULL;
		wdev->ssid_len = 0;
		wdev->conn_owner_nlportid = 0;
		if (bss) {
			cfg80211_unhold_bss(bss_from_pub(bss));
			cfg80211_put_bss(wdev->wiphy, bss);
		}
		cfg80211_sme_free(wdev);
		return;
	}

	if (WARN_ON(!bss))
		return;

	wdev->current_bss = bss_from_pub(bss);

	if (!(wdev->wiphy->flags & WIPHY_FLAG_HAS_STATIC_WEP))
		cfg80211_upload_connect_keys(wdev);

	rcu_read_lock();
	country_ie = ieee80211_bss_get_ie(bss, WLAN_EID_COUNTRY);
	if (!country_ie) {
		rcu_read_unlock();
		return;
	}

	country_ie = kmemdup(country_ie, 2 + country_ie[1], GFP_ATOMIC);
	rcu_read_unlock();

	if (!country_ie)
		return;

	/*
	 * ieee80211_bss_get_ie() ensures we can access:
	 * - country_ie + 2, the start of the country ie data, and
	 * - and country_ie[1] which is the IE length
	 */
	regulatory_hint_country_ie(wdev->wiphy, bss->channel->band,
				   country_ie + 2, country_ie[1]);
	kfree(country_ie);
}

/* Consumes bss object one way or another */
void cfg80211_connect_bss(struct net_device *dev, const u8 *bssid,
			  struct cfg80211_bss *bss, const u8 *req_ie,
			  size_t req_ie_len, const u8 *resp_ie,
			  size_t resp_ie_len, int status, gfp_t gfp,
			  enum nl80211_timeout_reason timeout_reason)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct cfg80211_event *ev;
	unsigned long flags;

	if (bss) {
		/* Make sure the bss entry provided by the driver is valid. */
		struct cfg80211_internal_bss *ibss = bss_from_pub(bss);

		if (WARN_ON(list_empty(&ibss->list))) {
			cfg80211_put_bss(wdev->wiphy, bss);
			return;
		}
	}

	ev = kzalloc(sizeof(*ev) + req_ie_len + resp_ie_len, gfp);
	if (!ev) {
		cfg80211_put_bss(wdev->wiphy, bss);
		return;
	}

	ev->type = EVENT_CONNECT_RESULT;
	if (bssid)
		memcpy(ev->cr.bssid, bssid, ETH_ALEN);
	if (req_ie_len) {
		ev->cr.req_ie = ((u8 *)ev) + sizeof(*ev);
		ev->cr.req_ie_len = req_ie_len;
		memcpy((void *)ev->cr.req_ie, req_ie, req_ie_len);
	}
	if (resp_ie_len) {
		ev->cr.resp_ie = ((u8 *)ev) + sizeof(*ev) + req_ie_len;
		ev->cr.resp_ie_len = resp_ie_len;
		memcpy((void *)ev->cr.resp_ie, resp_ie, resp_ie_len);
	}
	if (bss)
		cfg80211_hold_bss(bss_from_pub(bss));
	ev->cr.bss = bss;
	ev->cr.status = status;
	ev->cr.timeout_reason = timeout_reason;

	spin_lock_irqsave(&wdev->event_lock, flags);
	list_add_tail(&ev->list, &wdev->event_list);
	spin_unlock_irqrestore(&wdev->event_lock, flags);
	queue_work(cfg80211_wq, &rdev->event_work);
}
EXPORT_SYMBOL(cfg80211_connect_bss);

/* Consumes bss object one way or another */
void __cfg80211_roamed(struct wireless_dev *wdev,
		       struct cfg80211_bss *bss,
		       const u8 *req_ie, size_t req_ie_len,
		       const u8 *resp_ie, size_t resp_ie_len)
{
#ifdef CONFIG_CFG80211_WEXT
	union iwreq_data wrqu;
#endif
	ASSERT_WDEV_LOCK(wdev);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_STATION &&
		    wdev->iftype != NL80211_IFTYPE_P2P_CLIENT))
		goto out;

	if (WARN_ON(!wdev->current_bss))
		goto out;

	cfg80211_unhold_bss(wdev->current_bss);
	cfg80211_put_bss(wdev->wiphy, &wdev->current_bss->pub);
	wdev->current_bss = NULL;

	cfg80211_hold_bss(bss_from_pub(bss));
	wdev->current_bss = bss_from_pub(bss);

	nl80211_send_roamed(wiphy_to_rdev(wdev->wiphy),
			    wdev->netdev, bss->bssid,
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
	memcpy(wrqu.ap_addr.sa_data, bss->bssid, ETH_ALEN);
	memcpy(wdev->wext.prev_bssid, bss->bssid, ETH_ALEN);
	wdev->wext.prev_bssid_valid = true;
	wireless_send_event(wdev->netdev, SIOCGIWAP, &wrqu, NULL);
#endif

	return;
out:
	cfg80211_put_bss(wdev->wiphy, bss);
}

void cfg80211_roamed(struct net_device *dev,
		     struct ieee80211_channel *channel,
		     const u8 *bssid,
		     const u8 *req_ie, size_t req_ie_len,
		     const u8 *resp_ie, size_t resp_ie_len, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_bss *bss;

	bss = cfg80211_get_bss(wdev->wiphy, channel, bssid, wdev->ssid,
			       wdev->ssid_len,
			       wdev->conn_bss_type, IEEE80211_PRIVACY_ANY);
	if (WARN_ON(!bss))
		return;

	cfg80211_roamed_bss(dev, bss, req_ie, req_ie_len, resp_ie,
			    resp_ie_len, gfp);
}
EXPORT_SYMBOL(cfg80211_roamed);

/* Consumes bss object one way or another */
void cfg80211_roamed_bss(struct net_device *dev,
			 struct cfg80211_bss *bss, const u8 *req_ie,
			 size_t req_ie_len, const u8 *resp_ie,
			 size_t resp_ie_len, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct cfg80211_event *ev;
	unsigned long flags;

	if (WARN_ON(!bss))
		return;

	ev = kzalloc(sizeof(*ev) + req_ie_len + resp_ie_len, gfp);
	if (!ev) {
		cfg80211_put_bss(wdev->wiphy, bss);
		return;
	}

	ev->type = EVENT_ROAMED;
	ev->rm.req_ie = ((u8 *)ev) + sizeof(*ev);
	ev->rm.req_ie_len = req_ie_len;
	memcpy((void *)ev->rm.req_ie, req_ie, req_ie_len);
	ev->rm.resp_ie = ((u8 *)ev) + sizeof(*ev) + req_ie_len;
	ev->rm.resp_ie_len = resp_ie_len;
	memcpy((void *)ev->rm.resp_ie, resp_ie, resp_ie_len);
	ev->rm.bss = bss;

	spin_lock_irqsave(&wdev->event_lock, flags);
	list_add_tail(&ev->list, &wdev->event_list);
	spin_unlock_irqrestore(&wdev->event_lock, flags);
	queue_work(cfg80211_wq, &rdev->event_work);
}
EXPORT_SYMBOL(cfg80211_roamed_bss);

void __cfg80211_disconnected(struct net_device *dev, const u8 *ie,
			     size_t ie_len, u16 reason, bool from_ap)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	int i;
#ifdef CONFIG_CFG80211_WEXT
	union iwreq_data wrqu;
#endif

	ASSERT_WDEV_LOCK(wdev);

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_STATION &&
		    wdev->iftype != NL80211_IFTYPE_P2P_CLIENT))
		return;

	if (wdev->current_bss) {
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(wdev->wiphy, &wdev->current_bss->pub);
	}

	wdev->current_bss = NULL;
	wdev->ssid_len = 0;
	wdev->conn_owner_nlportid = 0;

	nl80211_send_disconnected(rdev, dev, reason, ie, ie_len, from_ap);

	/* stop critical protocol if supported */
	if (rdev->ops->crit_proto_stop && rdev->crit_proto_nlportid) {
		rdev->crit_proto_nlportid = 0;
		rdev_crit_proto_stop(rdev, wdev);
	}

	/*
	 * Delete all the keys ... pairwise keys can't really
	 * exist any more anyway, but default keys might.
	 */
	if (rdev->ops->del_key)
		for (i = 0; i < 6; i++)
			rdev_del_key(rdev, dev, i, false, NULL);

	rdev_set_qos_map(rdev, dev, NULL);

#ifdef CONFIG_CFG80211_WEXT
	memset(&wrqu, 0, sizeof(wrqu));
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);
	wdev->wext.connect.ssid_len = 0;
#endif

	schedule_work(&cfg80211_disconnect_work);
}

void cfg80211_disconnected(struct net_device *dev, u16 reason,
			   const u8 *ie, size_t ie_len,
			   bool locally_generated, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
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
	ev->dc.locally_generated = locally_generated;

	spin_lock_irqsave(&wdev->event_lock, flags);
	list_add_tail(&ev->list, &wdev->event_list);
	spin_unlock_irqrestore(&wdev->event_lock, flags);
	queue_work(cfg80211_wq, &rdev->event_work);
}
EXPORT_SYMBOL(cfg80211_disconnected);

/*
 * API calls for nl80211/wext compatibility code
 */
int cfg80211_connect(struct cfg80211_registered_device *rdev,
		     struct net_device *dev,
		     struct cfg80211_connect_params *connect,
		     struct cfg80211_cached_keys *connkeys,
		     const u8 *prev_bssid)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	ASSERT_WDEV_LOCK(wdev);

	if (WARN_ON(wdev->connect_keys)) {
		kzfree(wdev->connect_keys);
		wdev->connect_keys = NULL;
	}

	cfg80211_oper_and_ht_capa(&connect->ht_capa_mask,
				  rdev->wiphy.ht_capa_mod_mask);

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

		connect->crypto.wep_keys = connkeys->params;
		connect->crypto.wep_tx_key = connkeys->def;
	} else {
		if (WARN_ON(connkeys))
			return -EINVAL;
	}

	wdev->connect_keys = connkeys;
	memcpy(wdev->ssid, connect->ssid, connect->ssid_len);
	wdev->ssid_len = connect->ssid_len;

	wdev->conn_bss_type = connect->pbss ? IEEE80211_BSS_TYPE_PBSS :
					      IEEE80211_BSS_TYPE_ESS;

	if (!rdev->ops->connect)
		err = cfg80211_sme_connect(wdev, connect, prev_bssid);
	else
		err = rdev_connect(rdev, dev, connect);

	if (err) {
		wdev->connect_keys = NULL;
		wdev->ssid_len = 0;
		return err;
	}

	return 0;
}

int cfg80211_disconnect(struct cfg80211_registered_device *rdev,
			struct net_device *dev, u16 reason, bool wextev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err = 0;

	ASSERT_WDEV_LOCK(wdev);

	kzfree(wdev->connect_keys);
	wdev->connect_keys = NULL;

	wdev->conn_owner_nlportid = 0;

	if (wdev->conn)
		err = cfg80211_sme_disconnect(wdev, reason);
	else if (!rdev->ops->disconnect)
		cfg80211_mlme_down(rdev, dev);
	else if (wdev->ssid_len)
		err = rdev_disconnect(rdev, dev, reason);

	return err;
}

/*
 * Used to clean up after the connection / connection attempt owner socket
 * disconnects
 */
void cfg80211_autodisconnect_wk(struct work_struct *work)
{
	struct wireless_dev *wdev =
		container_of(work, struct wireless_dev, disconnect_wk);
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);

	wdev_lock(wdev);

	if (wdev->conn_owner_nlportid) {
		/*
		 * Use disconnect_bssid if still connecting and ops->disconnect
		 * not implemented.  Otherwise we can use cfg80211_disconnect.
		 */
		if (rdev->ops->disconnect || wdev->current_bss)
			cfg80211_disconnect(rdev, wdev->netdev,
					    WLAN_REASON_DEAUTH_LEAVING, true);
		else
			cfg80211_mlme_deauth(rdev, wdev->netdev,
					     wdev->disconnect_bssid, NULL, 0,
					     WLAN_REASON_DEAUTH_LEAVING, false);
	}

	wdev_unlock(wdev);
}
