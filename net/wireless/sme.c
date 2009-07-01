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


void cfg80211_connect_result(struct net_device *dev, const u8 *bssid,
			     const u8 *req_ie, size_t req_ie_len,
			     const u8 *resp_ie, size_t resp_ie_len,
			     u16 status, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_bss *bss;
#ifdef CONFIG_WIRELESS_EXT
	union iwreq_data wrqu;
#endif

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_STATION))
		return;

	if (WARN_ON(wdev->sme_state != CFG80211_SME_CONNECTING))
		return;

	if (wdev->current_bss) {
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(wdev->current_bss);
		wdev->current_bss = NULL;
	}

	if (status == WLAN_STATUS_SUCCESS) {
		bss = cfg80211_get_bss(wdev->wiphy, NULL, bssid,
				       wdev->ssid, wdev->ssid_len,
				       WLAN_CAPABILITY_ESS,
				       WLAN_CAPABILITY_ESS);

		if (WARN_ON(!bss))
			return;

		cfg80211_hold_bss(bss);
		wdev->current_bss = bss;

		wdev->sme_state = CFG80211_SME_CONNECTED;
	} else {
		wdev->sme_state = CFG80211_SME_IDLE;
	}

	nl80211_send_connect_result(wiphy_to_dev(wdev->wiphy), dev, bssid,
				    req_ie, req_ie_len, resp_ie, resp_ie_len,
				    status, gfp);

#ifdef CONFIG_WIRELESS_EXT
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
	if (bssid)
		memcpy(wrqu.ap_addr.sa_data, bssid, ETH_ALEN);
	wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);
#endif
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
	cfg80211_put_bss(wdev->current_bss);
	wdev->current_bss = NULL;

	bss = cfg80211_get_bss(wdev->wiphy, NULL, bssid,
			       wdev->ssid, wdev->ssid_len,
			       WLAN_CAPABILITY_ESS, WLAN_CAPABILITY_ESS);

	if (WARN_ON(!bss))
		return;

	cfg80211_hold_bss(bss);
	wdev->current_bss = bss;

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

static void __cfg80211_disconnected(struct net_device *dev, gfp_t gfp,
				    u8 *ie, size_t ie_len, u16 reason,
				    bool from_ap)
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
		cfg80211_put_bss(wdev->current_bss);
	}

	wdev->current_bss = NULL;
	wdev->sme_state = CFG80211_SME_IDLE;

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
	__cfg80211_disconnected(dev, reason, ie, ie_len, true, gfp);
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
		return -EOPNOTSUPP;
	} else {
		wdev->sme_state = CFG80211_SME_CONNECTING;
		err = rdev->ops->connect(&rdev->wiphy, dev, connect);
		if (err) {
			wdev->sme_state = CFG80211_SME_IDLE;
			return err;
		}
	}

	memcpy(wdev->ssid, connect->ssid, connect->ssid_len);
	wdev->ssid_len = connect->ssid_len;

	return 0;
}

int cfg80211_disconnect(struct cfg80211_registered_device *rdev,
			struct net_device *dev, u16 reason)
{
	int err;

	if (!rdev->ops->disconnect) {
		return -EOPNOTSUPP;
	} else {
		err = rdev->ops->disconnect(&rdev->wiphy, dev, reason);
		if (err)
			return err;
	}

	__cfg80211_disconnected(dev, 0, NULL, 0, false, GFP_KERNEL);

	return 0;
}
