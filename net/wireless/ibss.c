// SPDX-License-Identifier: GPL-2.0
/*
 * Some IBSS support code for cfg80211.
 *
 * Copyright 2009	Johannes Berg <johannes@sipsolutions.net>
 * Copyright (C) 2020-2022 Intel Corporation
 */

#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <net/cfg80211.h>
#include "wext-compat.h"
#include "nl80211.h"
#include "rdev-ops.h"


void __cfg80211_ibss_joined(struct net_device *dev, const u8 *bssid,
			    struct ieee80211_channel *channel)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_bss *bss;
#ifdef CONFIG_CFG80211_WEXT
	union iwreq_data wrqu;
#endif

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return;

	if (!wdev->u.ibss.ssid_len)
		return;

	bss = cfg80211_get_bss(wdev->wiphy, channel, bssid, NULL, 0,
			       IEEE80211_BSS_TYPE_IBSS, IEEE80211_PRIVACY_ANY);

	if (WARN_ON(!bss))
		return;

	if (wdev->u.ibss.current_bss) {
		cfg80211_unhold_bss(wdev->u.ibss.current_bss);
		cfg80211_put_bss(wdev->wiphy, &wdev->u.ibss.current_bss->pub);
	}

	cfg80211_hold_bss(bss_from_pub(bss));
	wdev->u.ibss.current_bss = bss_from_pub(bss);

	if (!(wdev->wiphy->flags & WIPHY_FLAG_HAS_STATIC_WEP))
		cfg80211_upload_connect_keys(wdev);

	nl80211_send_ibss_bssid(wiphy_to_rdev(wdev->wiphy), dev, bssid,
				GFP_KERNEL);
#ifdef CONFIG_CFG80211_WEXT
	memset(&wrqu, 0, sizeof(wrqu));
	memcpy(wrqu.ap_addr.sa_data, bssid, ETH_ALEN);
	wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);
#endif
}

void cfg80211_ibss_joined(struct net_device *dev, const u8 *bssid,
			  struct ieee80211_channel *channel, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct cfg80211_event *ev;
	unsigned long flags;

	trace_cfg80211_ibss_joined(dev, bssid, channel);

	if (WARN_ON(!channel))
		return;

	ev = kzalloc(sizeof(*ev), gfp);
	if (!ev)
		return;

	ev->type = EVENT_IBSS_JOINED;
	memcpy(ev->ij.bssid, bssid, ETH_ALEN);
	ev->ij.channel = channel;

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

	lockdep_assert_held(&rdev->wiphy.mtx);
	ASSERT_WDEV_LOCK(wdev);

	if (wdev->u.ibss.ssid_len)
		return -EALREADY;

	if (!params->basic_rates) {
		/*
		* If no rates were explicitly configured,
		* use the mandatory rate set for 11b or
		* 11a for maximum compatibility.
		*/
		struct ieee80211_supported_band *sband;
		enum nl80211_band band;
		u32 flag;
		int j;

		band = params->chandef.chan->band;
		if (band == NL80211_BAND_5GHZ ||
		    band == NL80211_BAND_6GHZ)
			flag = IEEE80211_RATE_MANDATORY_A;
		else
			flag = IEEE80211_RATE_MANDATORY_B;

		sband = rdev->wiphy.bands[band];
		for (j = 0; j < sband->n_bitrates; j++) {
			if (sband->bitrates[j].flags & flag)
				params->basic_rates |= BIT(j);
		}
	}

	if (WARN_ON(connkeys && connkeys->def < 0))
		return -EINVAL;

	if (WARN_ON(wdev->connect_keys))
		kfree_sensitive(wdev->connect_keys);
	wdev->connect_keys = connkeys;

	wdev->u.ibss.chandef = params->chandef;
	if (connkeys) {
		params->wep_keys = connkeys->params;
		params->wep_tx_key = connkeys->def;
	}

#ifdef CONFIG_CFG80211_WEXT
	wdev->wext.ibss.chandef = params->chandef;
#endif
	err = rdev_join_ibss(rdev, dev, params);
	if (err) {
		wdev->connect_keys = NULL;
		return err;
	}

	memcpy(wdev->u.ibss.ssid, params->ssid, params->ssid_len);
	wdev->u.ibss.ssid_len = params->ssid_len;

	return 0;
}

static void __cfg80211_clear_ibss(struct net_device *dev, bool nowext)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	int i;

	ASSERT_WDEV_LOCK(wdev);

	kfree_sensitive(wdev->connect_keys);
	wdev->connect_keys = NULL;

	rdev_set_qos_map(rdev, dev, NULL);

	/*
	 * Delete all the keys ... pairwise keys can't really
	 * exist any more anyway, but default keys might.
	 */
	if (rdev->ops->del_key)
		for (i = 0; i < 6; i++)
			rdev_del_key(rdev, dev, -1, i, false, NULL);

	if (wdev->u.ibss.current_bss) {
		cfg80211_unhold_bss(wdev->u.ibss.current_bss);
		cfg80211_put_bss(wdev->wiphy, &wdev->u.ibss.current_bss->pub);
	}

	wdev->u.ibss.current_bss = NULL;
	wdev->u.ibss.ssid_len = 0;
	memset(&wdev->u.ibss.chandef, 0, sizeof(wdev->u.ibss.chandef));
#ifdef CONFIG_CFG80211_WEXT
	if (!nowext)
		wdev->wext.ibss.ssid_len = 0;
#endif
	cfg80211_sched_dfs_chan_update(rdev);
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

	if (!wdev->u.ibss.ssid_len)
		return -ENOLINK;

	err = rdev_leave_ibss(rdev, dev);

	if (err)
		return err;

	wdev->conn_owner_nlportid = 0;
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
	enum nl80211_band band;
	int i, err;

	ASSERT_WDEV_LOCK(wdev);

	if (!wdev->wext.ibss.beacon_interval)
		wdev->wext.ibss.beacon_interval = 100;

	/* try to find an IBSS channel if none requested ... */
	if (!wdev->wext.ibss.chandef.chan) {
		struct ieee80211_channel *new_chan = NULL;

		for (band = 0; band < NUM_NL80211_BANDS; band++) {
			struct ieee80211_supported_band *sband;
			struct ieee80211_channel *chan;

			sband = rdev->wiphy.bands[band];
			if (!sband)
				continue;

			for (i = 0; i < sband->n_channels; i++) {
				chan = &sband->channels[i];
				if (chan->flags & IEEE80211_CHAN_NO_IR)
					continue;
				if (chan->flags & IEEE80211_CHAN_DISABLED)
					continue;
				new_chan = chan;
				break;
			}

			if (new_chan)
				break;
		}

		if (!new_chan)
			return -EINVAL;

		cfg80211_chandef_create(&wdev->wext.ibss.chandef, new_chan,
					NL80211_CHAN_NO_HT);
	}

	/* don't join -- SSID is not there */
	if (!wdev->wext.ibss.ssid_len)
		return 0;

	if (!netif_running(wdev->netdev))
		return 0;

	if (wdev->wext.keys)
		wdev->wext.keys->def = wdev->wext.default_key;

	wdev->wext.ibss.privacy = wdev->wext.default_key != -1;

	if (wdev->wext.keys && wdev->wext.keys->def != -1) {
		ck = kmemdup(wdev->wext.keys, sizeof(*ck), GFP_KERNEL);
		if (!ck)
			return -ENOMEM;
		for (i = 0; i < CFG80211_MAX_WEP_KEYS; i++)
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
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	struct ieee80211_channel *chan = NULL;
	int err, freq;

	/* call only for ibss! */
	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	if (!rdev->ops->join_ibss)
		return -EOPNOTSUPP;

	freq = cfg80211_wext_freq(wextfreq);
	if (freq < 0)
		return freq;

	if (freq) {
		chan = ieee80211_get_channel(wdev->wiphy, freq);
		if (!chan)
			return -EINVAL;
		if (chan->flags & IEEE80211_CHAN_NO_IR ||
		    chan->flags & IEEE80211_CHAN_DISABLED)
			return -EINVAL;
	}

	if (wdev->wext.ibss.chandef.chan == chan)
		return 0;

	wdev_lock(wdev);
	err = 0;
	if (wdev->u.ibss.ssid_len)
		err = __cfg80211_leave_ibss(rdev, dev, true);
	wdev_unlock(wdev);

	if (err)
		return err;

	if (chan) {
		cfg80211_chandef_create(&wdev->wext.ibss.chandef, chan,
					NL80211_CHAN_NO_HT);
		wdev->wext.ibss.channel_fixed = true;
	} else {
		/* cfg80211_ibss_wext_join will pick one if needed */
		wdev->wext.ibss.channel_fixed = false;
	}

	wdev_lock(wdev);
	err = cfg80211_ibss_wext_join(rdev, wdev);
	wdev_unlock(wdev);

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
	if (wdev->u.ibss.current_bss)
		chan = wdev->u.ibss.current_bss->pub.channel;
	else if (wdev->wext.ibss.chandef.chan)
		chan = wdev->wext.ibss.chandef.chan;
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
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
	size_t len = data->length;
	int err;

	/* call only for ibss! */
	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	if (!rdev->ops->join_ibss)
		return -EOPNOTSUPP;

	wdev_lock(wdev);
	err = 0;
	if (wdev->u.ibss.ssid_len)
		err = __cfg80211_leave_ibss(rdev, dev, true);
	wdev_unlock(wdev);

	if (err)
		return err;

	/* iwconfig uses nul termination in SSID.. */
	if (len > 0 && ssid[len - 1] == '\0')
		len--;

	memcpy(wdev->u.ibss.ssid, ssid, len);
	wdev->wext.ibss.ssid = wdev->u.ibss.ssid;
	wdev->wext.ibss.ssid_len = len;

	wdev_lock(wdev);
	err = cfg80211_ibss_wext_join(rdev, wdev);
	wdev_unlock(wdev);

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
	if (wdev->u.ibss.ssid_len) {
		data->flags = 1;
		data->length = wdev->u.ibss.ssid_len;
		memcpy(ssid, wdev->u.ibss.ssid, data->length);
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
	struct cfg80211_registered_device *rdev = wiphy_to_rdev(wdev->wiphy);
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

	if (bssid && !is_valid_ether_addr(bssid))
		return -EINVAL;

	/* both automatic */
	if (!bssid && !wdev->wext.ibss.bssid)
		return 0;

	/* fixed already - and no change */
	if (wdev->wext.ibss.bssid && bssid &&
	    ether_addr_equal(bssid, wdev->wext.ibss.bssid))
		return 0;

	wdev_lock(wdev);
	err = 0;
	if (wdev->u.ibss.ssid_len)
		err = __cfg80211_leave_ibss(rdev, dev, true);
	wdev_unlock(wdev);

	if (err)
		return err;

	if (bssid) {
		memcpy(wdev->wext.bssid, bssid, ETH_ALEN);
		wdev->wext.ibss.bssid = wdev->wext.bssid;
	} else
		wdev->wext.ibss.bssid = NULL;

	wdev_lock(wdev);
	err = cfg80211_ibss_wext_join(rdev, wdev);
	wdev_unlock(wdev);

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
	if (wdev->u.ibss.current_bss)
		memcpy(ap_addr->sa_data, wdev->u.ibss.current_bss->pub.bssid,
		       ETH_ALEN);
	else if (wdev->wext.ibss.bssid)
		memcpy(ap_addr->sa_data, wdev->wext.ibss.bssid, ETH_ALEN);
	else
		eth_zero_addr(ap_addr->sa_data);

	wdev_unlock(wdev);

	return 0;
}
#endif
