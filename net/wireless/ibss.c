/*
 * Some IBSS support code for cfg80211.
 *
 * Copyright 2009	Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/slab.h>
#include <net/cfg80211.h>
#include "wext-compat.h"
#include "nl80211.h"


void __cfg80211_ibss_joined(struct net_device *dev, const u8 *bssid)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_bss *bss;
#ifdef CONFIG_CFG80211_WEXT
	union iwreq_data wrqu;
#endif

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return;

	if (!wdev->ssid_len)
		return;

	bss = cfg80211_get_bss(wdev->wiphy, NULL, bssid,
			       wdev->ssid, wdev->ssid_len,
			       WLAN_CAPABILITY_IBSS, WLAN_CAPABILITY_IBSS);

	if (WARN_ON(!bss))
		return;

	if (wdev->current_bss) {
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(&wdev->current_bss->pub);
	}

	cfg80211_hold_bss(bss_from_pub(bss));
	wdev->current_bss = bss_from_pub(bss);

	cfg80211_upload_connect_keys(wdev);

	nl80211_send_ibss_bssid(wiphy_to_dev(wdev->wiphy), dev, bssid,
				GFP_KERNEL);
#ifdef CONFIG_CFG80211_WEXT
	memset(&wrqu, 0, sizeof(wrqu));
	memcpy(wrqu.ap_addr.sa_data, bssid, ETH_ALEN);
	wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);
#endif
}

void cfg80211_ibss_joined(struct net_device *dev, const u8 *bssid, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	struct cfg80211_event *ev;
	unsigned long flags;

	CFG80211_DEV_WARN_ON(!wdev->ssid_len);

	ev = kzalloc(sizeof(*ev), gfp);
	if (!ev)
		return;

	ev->type = EVENT_IBSS_JOINED;
	memcpy(ev->cr.bssid, bssid, ETH_ALEN);

	spin_lock_irqsave(&wdev->event_lock, flags);
	list_add_tail(&ev->list, &wdev->event_list);
	spin_unlock_irqrestore(&wdev->event_lock, flags);
	queue_work(cfg80211_wq, &rdev->event_work);
}
EXPORT_SYMBOL(cfg80211_ibss_joined);

int __cfg80211_join_ibss(struct cfg80211_registered_device *rdev,
			 struct net_device *dev,
			 struct cfg80211_ibss_params *params,
			 struct cfg80211_cached_keys *connkeys)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	ASSERT_WDEV_LOCK(wdev);

	if (wdev->ssid_len)
		return -EALREADY;

	if (WARN_ON(wdev->connect_keys))
		kfree(wdev->connect_keys);
	wdev->connect_keys = connkeys;

#ifdef CONFIG_CFG80211_WEXT
	wdev->wext.ibss.channel = params->channel;
#endif
	err = rdev->ops->join_ibss(&rdev->wiphy, dev, params);
	if (err) {
		wdev->connect_keys = NULL;
		return err;
	}

	memcpy(wdev->ssid, params->ssid, params->ssid_len);
	wdev->ssid_len = params->ssid_len;

	return 0;
}

int cfg80211_join_ibss(struct cfg80211_registered_device *rdev,
		       struct net_device *dev,
		       struct cfg80211_ibss_params *params,
		       struct cfg80211_cached_keys *connkeys)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	mutex_lock(&rdev->devlist_mtx);
	wdev_lock(wdev);
	err = __cfg80211_join_ibss(rdev, dev, params, connkeys);
	wdev_unlock(wdev);
	mutex_unlock(&rdev->devlist_mtx);

	return err;
}

static void __cfg80211_clear_ibss(struct net_device *dev, bool nowext)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	int i;

	ASSERT_WDEV_LOCK(wdev);

	kfree(wdev->connect_keys);
	wdev->connect_keys = NULL;

	/*
	 * Delete all the keys ... pairwise keys can't really
	 * exist any more anyway, but default keys might.
	 */
	if (rdev->ops->del_key)
		for (i = 0; i < 6; i++)
			rdev->ops->del_key(wdev->wiphy, dev, i, NULL);

	if (wdev->current_bss) {
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(&wdev->current_bss->pub);
	}

	wdev->current_bss = NULL;
	wdev->ssid_len = 0;
#ifdef CONFIG_CFG80211_WEXT
	if (!nowext)
		wdev->wext.ibss.ssid_len = 0;
#endif
}

void cfg80211_clear_ibss(struct net_device *dev, bool nowext)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	wdev_lock(wdev);
	__cfg80211_clear_ibss(dev, nowext);
	wdev_unlock(wdev);
}

int __cfg80211_leave_ibss(struct cfg80211_registered_device *rdev,
			  struct net_device *dev, bool nowext)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	ASSERT_WDEV_LOCK(wdev);

	if (!wdev->ssid_len)
		return -ENOLINK;

	err = rdev->ops->leave_ibss(&rdev->wiphy, dev);

	if (err)
		return err;

	__cfg80211_clear_ibss(dev, nowext);

	return 0;
}

int cfg80211_leave_ibss(struct cfg80211_registered_device *rdev,
			struct net_device *dev, bool nowext)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	wdev_lock(wdev);
	err = __cfg80211_leave_ibss(rdev, dev, nowext);
	wdev_unlock(wdev);

	return err;
}

#ifdef CONFIG_CFG80211_WEXT
int cfg80211_ibss_wext_join(struct cfg80211_registered_device *rdev,
			    struct wireless_dev *wdev)
{
	struct cfg80211_cached_keys *ck = NULL;
	enum ieee80211_band band;
	int i, err;

	ASSERT_WDEV_LOCK(wdev);

	if (!wdev->wext.ibss.beacon_interval)
		wdev->wext.ibss.beacon_interval = 100;

	/* try to find an IBSS channel if none requested ... */
	if (!wdev->wext.ibss.channel) {
		for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
			struct ieee80211_supported_band *sband;
			struct ieee80211_channel *chan;

			sband = rdev->wiphy.bands[band];
			if (!sband)
				continue;

			for (i = 0; i < sband->n_channels; i++) {
				chan = &sband->channels[i];
				if (chan->flags & IEEE80211_CHAN_NO_IBSS)
					continue;
				if (chan->flags & IEEE80211_CHAN_DISABLED)
					continue;
				wdev->wext.ibss.channel = chan;
				break;
			}

			if (wdev->wext.ibss.channel)
				break;
		}

		if (!wdev->wext.ibss.channel)
			return -EINVAL;
	}

	/* don't join -- SSID is not there */
	if (!wdev->wext.ibss.ssid_len)
		return 0;

	if (!netif_running(wdev->netdev))
		return 0;

	if (wdev->wext.keys) {
		wdev->wext.keys->def = wdev->wext.default_key;
		wdev->wext.keys->defmgmt = wdev->wext.default_mgmt_key;
	}

	wdev->wext.ibss.privacy = wdev->wext.default_key != -1;

	if (wdev->wext.keys) {
		ck = kmemdup(wdev->wext.keys, sizeof(*ck), GFP_KERNEL);
		if (!ck)
			return -ENOMEM;
		for (i = 0; i < 6; i++)
			ck->params[i].key = ck->data[i];
	}
	err = __cfg80211_join_ibss(rdev, wdev->netdev,
				   &wdev->wext.ibss, ck);
	if (err)
		kfree(ck);

	return err;
}

int cfg80211_ibss_wext_siwfreq(struct net_device *dev,
			       struct iw_request_info *info,
			       struct iw_freq *wextfreq, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	struct ieee80211_channel *chan = NULL;
	int err, freq;

	/* call only for ibss! */
	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	if (!rdev->ops->join_ibss)
		return -EOPNOTSUPP;

	freq = cfg80211_wext_freq(wdev->wiphy, wextfreq);
	if (freq < 0)
		return freq;

	if (freq) {
		chan = ieee80211_get_channel(wdev->wiphy, freq);
		if (!chan)
			return -EINVAL;
		if (chan->flags & IEEE80211_CHAN_NO_IBSS ||
		    chan->flags & IEEE80211_CHAN_DISABLED)
			return -EINVAL;
	}

	if (wdev->wext.ibss.channel == chan)
		return 0;

	wdev_lock(wdev);
	err = 0;
	if (wdev->ssid_len)
		err = __cfg80211_leave_ibss(rdev, dev, true);
	wdev_unlock(wdev);

	if (err)
		return err;

	if (chan) {
		wdev->wext.ibss.channel = chan;
		wdev->wext.ibss.channel_fixed = true;
	} else {
		/* cfg80211_ibss_wext_join will pick one if needed */
		wdev->wext.ibss.channel_fixed = false;
	}

	mutex_lock(&rdev->devlist_mtx);
	wdev_lock(wdev);
	err = cfg80211_ibss_wext_join(rdev, wdev);
	wdev_unlock(wdev);
	mutex_unlock(&rdev->devlist_mtx);

	return err;
}

int cfg80211_ibss_wext_giwfreq(struct net_device *dev,
			       struct iw_request_info *info,
			       struct iw_freq *freq, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct ieee80211_channel *chan = NULL;

	/* call only for ibss! */
	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	wdev_lock(wdev);
	if (wdev->current_bss)
		chan = wdev->current_bss->pub.channel;
	else if (wdev->wext.ibss.channel)
		chan = wdev->wext.ibss.channel;
	wdev_unlock(wdev);

	if (chan) {
		freq->m = chan->center_freq;
		freq->e = 6;
		return 0;
	}

	/* no channel if not joining */
	return -EINVAL;
}

int cfg80211_ibss_wext_siwessid(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_point *data, char *ssid)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	size_t len = data->length;
	int err;

	/* call only for ibss! */
	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	if (!rdev->ops->join_ibss)
		return -EOPNOTSUPP;

	wdev_lock(wdev);
	err = 0;
	if (wdev->ssid_len)
		err = __cfg80211_leave_ibss(rdev, dev, true);
	wdev_unlock(wdev);

	if (err)
		return err;

	/* iwconfig uses nul termination in SSID.. */
	if (len > 0 && ssid[len - 1] == '\0')
		len--;

	wdev->wext.ibss.ssid = wdev->ssid;
	memcpy(wdev->wext.ibss.ssid, ssid, len);
	wdev->wext.ibss.ssid_len = len;

	mutex_lock(&rdev->devlist_mtx);
	wdev_lock(wdev);
	err = cfg80211_ibss_wext_join(rdev, wdev);
	wdev_unlock(wdev);
	mutex_unlock(&rdev->devlist_mtx);

	return err;
}

int cfg80211_ibss_wext_giwessid(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_point *data, char *ssid)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	/* call only for ibss! */
	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	data->flags = 0;

	wdev_lock(wdev);
	if (wdev->ssid_len) {
		data->flags = 1;
		data->length = wdev->ssid_len;
		memcpy(ssid, wdev->ssid, data->length);
	} else if (wdev->wext.ibss.ssid && wdev->wext.ibss.ssid_len) {
		data->flags = 1;
		data->length = wdev->wext.ibss.ssid_len;
		memcpy(ssid, wdev->wext.ibss.ssid, data->length);
	}
	wdev_unlock(wdev);

	return 0;
}

int cfg80211_ibss_wext_siwap(struct net_device *dev,
			     struct iw_request_info *info,
			     struct sockaddr *ap_addr, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wdev->wiphy);
	u8 *bssid = ap_addr->sa_data;
	int err;

	/* call only for ibss! */
	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	if (!rdev->ops->join_ibss)
		return -EOPNOTSUPP;

	if (ap_addr->sa_family != ARPHRD_ETHER)
		return -EINVAL;

	/* automatic mode */
	if (is_zero_ether_addr(bssid) || is_broadcast_ether_addr(bssid))
		bssid = NULL;

	/* both automatic */
	if (!bssid && !wdev->wext.ibss.bssid)
		return 0;

	/* fixed already - and no change */
	if (wdev->wext.ibss.bssid && bssid &&
	    compare_ether_addr(bssid, wdev->wext.ibss.bssid) == 0)
		return 0;

	wdev_lock(wdev);
	err = 0;
	if (wdev->ssid_len)
		err = __cfg80211_leave_ibss(rdev, dev, true);
	wdev_unlock(wdev);

	if (err)
		return err;

	if (bssid) {
		memcpy(wdev->wext.bssid, bssid, ETH_ALEN);
		wdev->wext.ibss.bssid = wdev->wext.bssid;
	} else
		wdev->wext.ibss.bssid = NULL;

	mutex_lock(&rdev->devlist_mtx);
	wdev_lock(wdev);
	err = cfg80211_ibss_wext_join(rdev, wdev);
	wdev_unlock(wdev);
	mutex_unlock(&rdev->devlist_mtx);

	return err;
}

int cfg80211_ibss_wext_giwap(struct net_device *dev,
			     struct iw_request_info *info,
			     struct sockaddr *ap_addr, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	/* call only for ibss! */
	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	ap_addr->sa_family = ARPHRD_ETHER;

	wdev_lock(wdev);
	if (wdev->current_bss)
		memcpy(ap_addr->sa_data, wdev->current_bss->pub.bssid, ETH_ALEN);
	else if (wdev->wext.ibss.bssid)
		memcpy(ap_addr->sa_data, wdev->wext.ibss.bssid, ETH_ALEN);
	else
		memset(ap_addr->sa_data, 0, ETH_ALEN);

	wdev_unlock(wdev);

	return 0;
}
#endif
