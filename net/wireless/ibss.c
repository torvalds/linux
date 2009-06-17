/*
 * Some IBSS support code for cfg80211.
 *
 * Copyright 2009	Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <net/cfg80211.h>
#include "nl80211.h"


void cfg80211_ibss_joined(struct net_device *dev, const u8 *bssid, gfp_t gfp)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct cfg80211_bss *bss;
#ifdef CONFIG_WIRELESS_EXT
	union iwreq_data wrqu;
#endif

	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return;

	if (WARN_ON(!wdev->ssid_len))
		return;

	if (memcmp(bssid, wdev->bssid, ETH_ALEN) == 0)
		return;

	bss = cfg80211_get_bss(wdev->wiphy, NULL, bssid,
			       wdev->ssid, wdev->ssid_len,
			       WLAN_CAPABILITY_IBSS, WLAN_CAPABILITY_IBSS);

	if (WARN_ON(!bss))
		return;

	if (wdev->current_bss) {
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(wdev->current_bss);
	}

	cfg80211_hold_bss(bss);
	wdev->current_bss = bss;
	memcpy(wdev->bssid, bssid, ETH_ALEN);

	nl80211_send_ibss_bssid(wiphy_to_dev(wdev->wiphy), dev, bssid, gfp);
#ifdef CONFIG_WIRELESS_EXT
	memset(&wrqu, 0, sizeof(wrqu));
	memcpy(wrqu.ap_addr.sa_data, bssid, ETH_ALEN);
	wireless_send_event(dev, SIOCGIWAP, &wrqu, NULL);
#endif
}
EXPORT_SYMBOL(cfg80211_ibss_joined);

int cfg80211_join_ibss(struct cfg80211_registered_device *rdev,
		       struct net_device *dev,
		       struct cfg80211_ibss_params *params)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	if (wdev->ssid_len)
		return -EALREADY;

#ifdef CONFIG_WIRELESS_EXT
	wdev->wext.ibss.channel = params->channel;
#endif
	err = rdev->ops->join_ibss(&rdev->wiphy, dev, params);

	if (err)
		return err;

	memcpy(wdev->ssid, params->ssid, params->ssid_len);
	wdev->ssid_len = params->ssid_len;

	return 0;
}

void cfg80211_clear_ibss(struct net_device *dev, bool nowext)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	if (wdev->current_bss) {
		cfg80211_unhold_bss(wdev->current_bss);
		cfg80211_put_bss(wdev->current_bss);
	}

	wdev->current_bss = NULL;
	wdev->ssid_len = 0;
	memset(wdev->bssid, 0, ETH_ALEN);
#ifdef CONFIG_WIRELESS_EXT
	if (!nowext)
		wdev->wext.ibss.ssid_len = 0;
#endif
}

int cfg80211_leave_ibss(struct cfg80211_registered_device *rdev,
			struct net_device *dev, bool nowext)
{
	int err;

	err = rdev->ops->leave_ibss(&rdev->wiphy, dev);

	if (err)
		return err;

	cfg80211_clear_ibss(dev, nowext);

	return 0;
}

#ifdef CONFIG_WIRELESS_EXT
static int cfg80211_ibss_wext_join(struct cfg80211_registered_device *rdev,
				   struct wireless_dev *wdev)
{
	enum ieee80211_band band;
	int i;

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

	return cfg80211_join_ibss(wiphy_to_dev(wdev->wiphy),
				  wdev->netdev, &wdev->wext.ibss);
}

int cfg80211_ibss_wext_siwfreq(struct net_device *dev,
			       struct iw_request_info *info,
			       struct iw_freq *freq, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct ieee80211_channel *chan;
	int err;

	/* call only for ibss! */
	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	if (!wiphy_to_dev(wdev->wiphy)->ops->join_ibss)
		return -EOPNOTSUPP;

	chan = cfg80211_wext_freq(wdev->wiphy, freq);
	if (chan && IS_ERR(chan))
		return PTR_ERR(chan);

	if (chan &&
	    (chan->flags & IEEE80211_CHAN_NO_IBSS ||
	     chan->flags & IEEE80211_CHAN_DISABLED))
		return -EINVAL;

	if (wdev->wext.ibss.channel == chan)
		return 0;

	if (wdev->ssid_len) {
		err = cfg80211_leave_ibss(wiphy_to_dev(wdev->wiphy),
					  dev, true);
		if (err)
			return err;
	}

	if (chan) {
		wdev->wext.ibss.channel = chan;
		wdev->wext.ibss.channel_fixed = true;
	} else {
		/* cfg80211_ibss_wext_join will pick one if needed */
		wdev->wext.ibss.channel_fixed = false;
	}

	return cfg80211_ibss_wext_join(wiphy_to_dev(wdev->wiphy), wdev);
}
/* temporary symbol - mark GPL - in the future the handler won't be */
EXPORT_SYMBOL_GPL(cfg80211_ibss_wext_siwfreq);

int cfg80211_ibss_wext_giwfreq(struct net_device *dev,
			       struct iw_request_info *info,
			       struct iw_freq *freq, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct ieee80211_channel *chan = NULL;

	/* call only for ibss! */
	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	if (wdev->current_bss)
		chan = wdev->current_bss->channel;
	else if (wdev->wext.ibss.channel)
		chan = wdev->wext.ibss.channel;

	if (chan) {
		freq->m = chan->center_freq;
		freq->e = 6;
		return 0;
	}

	/* no channel if not joining */
	return -EINVAL;
}
/* temporary symbol - mark GPL - in the future the handler won't be */
EXPORT_SYMBOL_GPL(cfg80211_ibss_wext_giwfreq);

int cfg80211_ibss_wext_siwessid(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_point *data, char *ssid)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	size_t len = data->length;
	int err;

	/* call only for ibss! */
	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	if (!wiphy_to_dev(wdev->wiphy)->ops->join_ibss)
		return -EOPNOTSUPP;

	if (wdev->ssid_len) {
		err = cfg80211_leave_ibss(wiphy_to_dev(wdev->wiphy),
					  dev, true);
		if (err)
			return err;
	}

	/* iwconfig uses nul termination in SSID.. */
	if (len > 0 && ssid[len - 1] == '\0')
		len--;

	wdev->wext.ibss.ssid = wdev->ssid;
	memcpy(wdev->wext.ibss.ssid, ssid, len);
	wdev->wext.ibss.ssid_len = len;

	return cfg80211_ibss_wext_join(wiphy_to_dev(wdev->wiphy), wdev);
}
/* temporary symbol - mark GPL - in the future the handler won't be */
EXPORT_SYMBOL_GPL(cfg80211_ibss_wext_siwessid);

int cfg80211_ibss_wext_giwessid(struct net_device *dev,
				struct iw_request_info *info,
				struct iw_point *data, char *ssid)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	/* call only for ibss! */
	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	data->flags = 0;

	if (wdev->ssid_len) {
		data->flags = 1;
		data->length = wdev->ssid_len;
		memcpy(ssid, wdev->ssid, data->length);
	} else if (wdev->wext.ibss.ssid && wdev->wext.ibss.ssid_len) {
		data->flags = 1;
		data->length = wdev->wext.ibss.ssid_len;
		memcpy(ssid, wdev->wext.ibss.ssid, data->length);
	}

	return 0;
}
/* temporary symbol - mark GPL - in the future the handler won't be */
EXPORT_SYMBOL_GPL(cfg80211_ibss_wext_giwessid);

int cfg80211_ibss_wext_siwap(struct net_device *dev,
			     struct iw_request_info *info,
			     struct sockaddr *ap_addr, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	u8 *bssid = ap_addr->sa_data;
	int err;

	/* call only for ibss! */
	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	if (!wiphy_to_dev(wdev->wiphy)->ops->join_ibss)
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

	if (wdev->ssid_len) {
		err = cfg80211_leave_ibss(wiphy_to_dev(wdev->wiphy),
					  dev, true);
		if (err)
			return err;
	}

	if (bssid) {
		memcpy(wdev->wext.bssid, bssid, ETH_ALEN);
		wdev->wext.ibss.bssid = wdev->wext.bssid;
	} else
		wdev->wext.ibss.bssid = NULL;

	return cfg80211_ibss_wext_join(wiphy_to_dev(wdev->wiphy), wdev);
}
/* temporary symbol - mark GPL - in the future the handler won't be */
EXPORT_SYMBOL_GPL(cfg80211_ibss_wext_siwap);

int cfg80211_ibss_wext_giwap(struct net_device *dev,
			     struct iw_request_info *info,
			     struct sockaddr *ap_addr, char *extra)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	/* call only for ibss! */
	if (WARN_ON(wdev->iftype != NL80211_IFTYPE_ADHOC))
		return -EINVAL;

	ap_addr->sa_family = ARPHRD_ETHER;

	if (wdev->wext.ibss.bssid) {
		memcpy(ap_addr->sa_data, wdev->wext.ibss.bssid, ETH_ALEN);
		return 0;
	}

	memcpy(ap_addr->sa_data, wdev->bssid, ETH_ALEN);
	return 0;
}
/* temporary symbol - mark GPL - in the future the handler won't be */
EXPORT_SYMBOL_GPL(cfg80211_ibss_wext_giwap);
#endif
