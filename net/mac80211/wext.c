/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <asm/uaccess.h>

#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "led.h"
#include "rate.h"
#include "wpa.h"
#include "aes_ccm.h"


static int ieee80211_set_encryption(struct net_device *dev, u8 *sta_addr,
				    int idx, int alg, int remove,
				    int set_tx_key, const u8 *_key,
				    size_t key_len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;
	struct ieee80211_key *key;
	struct ieee80211_sub_if_data *sdata;
	int err;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (idx < 0 || idx >= NUM_DEFAULT_KEYS) {
		printk(KERN_DEBUG "%s: set_encrypt - invalid idx=%d\n",
		       dev->name, idx);
		return -EINVAL;
	}

	if (remove) {
		rcu_read_lock();

		err = 0;

		if (is_broadcast_ether_addr(sta_addr)) {
			key = sdata->keys[idx];
		} else {
			sta = sta_info_get(local, sta_addr);
			if (!sta) {
				err = -ENOENT;
				goto out_unlock;
			}
			key = sta->key;
		}

		ieee80211_key_free(key);
	} else {
		key = ieee80211_key_alloc(alg, idx, key_len, _key);
		if (!key)
			return -ENOMEM;

		sta = NULL;
		err = 0;

		rcu_read_lock();

		if (!is_broadcast_ether_addr(sta_addr)) {
			set_tx_key = 0;
			/*
			 * According to the standard, the key index of a
			 * pairwise key must be zero. However, some AP are
			 * broken when it comes to WEP key indices, so we
			 * work around this.
			 */
			if (idx != 0 && alg != ALG_WEP) {
				ieee80211_key_free(key);
				err = -EINVAL;
				goto out_unlock;
			}

			sta = sta_info_get(local, sta_addr);
			if (!sta) {
				ieee80211_key_free(key);
				err = -ENOENT;
				goto out_unlock;
			}
		}

		if (alg == ALG_WEP &&
			key_len != LEN_WEP40 && key_len != LEN_WEP104) {
			ieee80211_key_free(key);
			err = -EINVAL;
			goto out_unlock;
		}

		ieee80211_key_link(key, sdata, sta);

		if (set_tx_key || (!sta && !sdata->default_key && key))
			ieee80211_set_default_key(sdata, idx);
	}

 out_unlock:
	rcu_read_unlock();

	return err;
}

static int ieee80211_ioctl_siwgenie(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_point *data, char *extra)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->flags & IEEE80211_SDATA_USERSPACE_MLME)
		return -EOPNOTSUPP;

	if (sdata->vif.type == IEEE80211_IF_TYPE_STA ||
	    sdata->vif.type == IEEE80211_IF_TYPE_IBSS) {
		int ret = ieee80211_sta_set_extra_ie(dev, extra, data->length);
		if (ret)
			return ret;
		sdata->u.sta.flags &= ~IEEE80211_STA_AUTO_BSSID_SEL;
		ieee80211_sta_req_auth(dev, &sdata->u.sta);
		return 0;
	}

	return -EOPNOTSUPP;
}

static int ieee80211_ioctl_giwname(struct net_device *dev,
				   struct iw_request_info *info,
				   char *name, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_supported_band *sband;
	u8 is_ht = 0, is_a = 0, is_b = 0, is_g = 0;


	sband = local->hw.wiphy->bands[IEEE80211_BAND_5GHZ];
	if (sband) {
		is_a = 1;
		is_ht |= sband->ht_info.ht_supported;
	}

	sband = local->hw.wiphy->bands[IEEE80211_BAND_2GHZ];
	if (sband) {
		int i;
		/* Check for mandatory rates */
		for (i = 0; i < sband->n_bitrates; i++) {
			if (sband->bitrates[i].bitrate == 10)
				is_b = 1;
			if (sband->bitrates[i].bitrate == 60)
				is_g = 1;
		}
		is_ht |= sband->ht_info.ht_supported;
	}

	strcpy(name, "IEEE 802.11");
	if (is_a)
		strcat(name, "a");
	if (is_b)
		strcat(name, "b");
	if (is_g)
		strcat(name, "g");
	if (is_ht)
		strcat(name, "n");

	return 0;
}


static int ieee80211_ioctl_giwrange(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *data, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct iw_range *range = (struct iw_range *) extra;
	enum ieee80211_band band;
	int c = 0;

	data->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 21;
	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT;
	range->min_retry = 0;
	range->max_retry = 255;
	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

	range->encoding_size[0] = 5;
	range->encoding_size[1] = 13;
	range->num_encoding_sizes = 2;
	range->max_encoding_tokens = NUM_DEFAULT_KEYS;

	if (local->hw.flags & IEEE80211_HW_SIGNAL_UNSPEC ||
	    local->hw.flags & IEEE80211_HW_SIGNAL_DB)
		range->max_qual.level = local->hw.max_signal;
	else if  (local->hw.flags & IEEE80211_HW_SIGNAL_DBM)
		range->max_qual.level = -110;
	else
		range->max_qual.level = 0;

	if (local->hw.flags & IEEE80211_HW_NOISE_DBM)
		range->max_qual.noise = -110;
	else
		range->max_qual.noise = 0;

	range->max_qual.qual = 100;
	range->max_qual.updated = local->wstats_flags;

	range->avg_qual.qual = 50;
	/* not always true but better than nothing */
	range->avg_qual.level = range->max_qual.level / 2;
	range->avg_qual.noise = range->max_qual.noise / 2;
	range->avg_qual.updated = local->wstats_flags;

	range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
			  IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;


	for (band = 0; band < IEEE80211_NUM_BANDS; band ++) {
		int i;
		struct ieee80211_supported_band *sband;

		sband = local->hw.wiphy->bands[band];

		if (!sband)
			continue;

		for (i = 0; i < sband->n_channels && c < IW_MAX_FREQUENCIES; i++) {
			struct ieee80211_channel *chan = &sband->channels[i];

			if (!(chan->flags & IEEE80211_CHAN_DISABLED)) {
				range->freq[c].i =
					ieee80211_frequency_to_channel(
						chan->center_freq);
				range->freq[c].m = chan->center_freq;
				range->freq[c].e = 6;
				c++;
			}
		}
	}
	range->num_channels = c;
	range->num_frequency = c;

	IW_EVENT_CAPA_SET_KERNEL(range->event_capa);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWAP);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWSCAN);

	range->scan_capa |= IW_SCAN_CAPA_ESSID;

	return 0;
}


static int ieee80211_ioctl_siwmode(struct net_device *dev,
				   struct iw_request_info *info,
				   __u32 *mode, char *extra)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int type;

	if (sdata->vif.type == IEEE80211_IF_TYPE_VLAN)
		return -EOPNOTSUPP;

	switch (*mode) {
	case IW_MODE_INFRA:
		type = IEEE80211_IF_TYPE_STA;
		break;
	case IW_MODE_ADHOC:
		type = IEEE80211_IF_TYPE_IBSS;
		break;
	case IW_MODE_REPEAT:
		type = IEEE80211_IF_TYPE_WDS;
		break;
	case IW_MODE_MONITOR:
		type = IEEE80211_IF_TYPE_MNTR;
		break;
	default:
		return -EINVAL;
	}

	return ieee80211_if_change_type(sdata, type);
}


static int ieee80211_ioctl_giwmode(struct net_device *dev,
				   struct iw_request_info *info,
				   __u32 *mode, char *extra)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	switch (sdata->vif.type) {
	case IEEE80211_IF_TYPE_AP:
		*mode = IW_MODE_MASTER;
		break;
	case IEEE80211_IF_TYPE_STA:
		*mode = IW_MODE_INFRA;
		break;
	case IEEE80211_IF_TYPE_IBSS:
		*mode = IW_MODE_ADHOC;
		break;
	case IEEE80211_IF_TYPE_MNTR:
		*mode = IW_MODE_MONITOR;
		break;
	case IEEE80211_IF_TYPE_WDS:
		*mode = IW_MODE_REPEAT;
		break;
	case IEEE80211_IF_TYPE_VLAN:
		*mode = IW_MODE_SECOND;		/* FIXME */
		break;
	default:
		*mode = IW_MODE_AUTO;
		break;
	}
	return 0;
}

int ieee80211_set_freq(struct net_device *dev, int freqMHz)
{
	int ret = -EINVAL;
	struct ieee80211_channel *chan;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	chan = ieee80211_get_channel(local->hw.wiphy, freqMHz);

	if (chan && !(chan->flags & IEEE80211_CHAN_DISABLED)) {
		if (sdata->vif.type == IEEE80211_IF_TYPE_IBSS &&
		    chan->flags & IEEE80211_CHAN_NO_IBSS) {
			printk(KERN_DEBUG "%s: IBSS not allowed on frequency "
				"%d MHz\n", dev->name, chan->center_freq);
			return ret;
		}
		local->oper_channel = chan;

		if (local->sta_sw_scanning || local->sta_hw_scanning)
			ret = 0;
		else
			ret = ieee80211_hw_config(local);

		rate_control_clear(local);
	}

	return ret;
}

static int ieee80211_ioctl_siwfreq(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_freq *freq, char *extra)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type == IEEE80211_IF_TYPE_STA)
		sdata->u.sta.flags &= ~IEEE80211_STA_AUTO_CHANNEL_SEL;

	/* freq->e == 0: freq->m = channel; otherwise freq = m * 10^e */
	if (freq->e == 0) {
		if (freq->m < 0) {
			if (sdata->vif.type == IEEE80211_IF_TYPE_STA)
				sdata->u.sta.flags |=
					IEEE80211_STA_AUTO_CHANNEL_SEL;
			return 0;
		} else
			return ieee80211_set_freq(dev,
				ieee80211_channel_to_frequency(freq->m));
	} else {
		int i, div = 1000000;
		for (i = 0; i < freq->e; i++)
			div /= 10;
		if (div > 0)
			return ieee80211_set_freq(dev, freq->m / div);
		else
			return -EINVAL;
	}
}


static int ieee80211_ioctl_giwfreq(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_freq *freq, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	freq->m = local->hw.conf.channel->center_freq;
	freq->e = 6;

	return 0;
}


static int ieee80211_ioctl_siwessid(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_point *data, char *ssid)
{
	struct ieee80211_sub_if_data *sdata;
	size_t len = data->length;

	/* iwconfig uses nul termination in SSID.. */
	if (len > 0 && ssid[len - 1] == '\0')
		len--;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->vif.type == IEEE80211_IF_TYPE_STA ||
	    sdata->vif.type == IEEE80211_IF_TYPE_IBSS) {
		int ret;
		if (sdata->flags & IEEE80211_SDATA_USERSPACE_MLME) {
			if (len > IEEE80211_MAX_SSID_LEN)
				return -EINVAL;
			memcpy(sdata->u.sta.ssid, ssid, len);
			sdata->u.sta.ssid_len = len;
			return 0;
		}
		if (data->flags)
			sdata->u.sta.flags &= ~IEEE80211_STA_AUTO_SSID_SEL;
		else
			sdata->u.sta.flags |= IEEE80211_STA_AUTO_SSID_SEL;
		ret = ieee80211_sta_set_ssid(dev, ssid, len);
		if (ret)
			return ret;
		ieee80211_sta_req_auth(dev, &sdata->u.sta);
		return 0;
	}

	if (sdata->vif.type == IEEE80211_IF_TYPE_AP) {
		memcpy(sdata->u.ap.ssid, ssid, len);
		memset(sdata->u.ap.ssid + len, 0,
		       IEEE80211_MAX_SSID_LEN - len);
		sdata->u.ap.ssid_len = len;
		return ieee80211_if_config(sdata, IEEE80211_IFCC_SSID);
	}
	return -EOPNOTSUPP;
}


static int ieee80211_ioctl_giwessid(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_point *data, char *ssid)
{
	size_t len;

	struct ieee80211_sub_if_data *sdata;
	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->vif.type == IEEE80211_IF_TYPE_STA ||
	    sdata->vif.type == IEEE80211_IF_TYPE_IBSS) {
		int res = ieee80211_sta_get_ssid(dev, ssid, &len);
		if (res == 0) {
			data->length = len;
			data->flags = 1;
		} else
			data->flags = 0;
		return res;
	}

	if (sdata->vif.type == IEEE80211_IF_TYPE_AP) {
		len = sdata->u.ap.ssid_len;
		if (len > IW_ESSID_MAX_SIZE)
			len = IW_ESSID_MAX_SIZE;
		memcpy(ssid, sdata->u.ap.ssid, len);
		data->length = len;
		data->flags = 1;
		return 0;
	}
	return -EOPNOTSUPP;
}


static int ieee80211_ioctl_siwap(struct net_device *dev,
				 struct iw_request_info *info,
				 struct sockaddr *ap_addr, char *extra)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->vif.type == IEEE80211_IF_TYPE_STA ||
	    sdata->vif.type == IEEE80211_IF_TYPE_IBSS) {
		int ret;
		if (sdata->flags & IEEE80211_SDATA_USERSPACE_MLME) {
			memcpy(sdata->u.sta.bssid, (u8 *) &ap_addr->sa_data,
			       ETH_ALEN);
			return 0;
		}
		if (is_zero_ether_addr((u8 *) &ap_addr->sa_data))
			sdata->u.sta.flags |= IEEE80211_STA_AUTO_BSSID_SEL |
				IEEE80211_STA_AUTO_CHANNEL_SEL;
		else if (is_broadcast_ether_addr((u8 *) &ap_addr->sa_data))
			sdata->u.sta.flags |= IEEE80211_STA_AUTO_BSSID_SEL;
		else
			sdata->u.sta.flags &= ~IEEE80211_STA_AUTO_BSSID_SEL;
		ret = ieee80211_sta_set_bssid(dev, (u8 *) &ap_addr->sa_data);
		if (ret)
			return ret;
		ieee80211_sta_req_auth(dev, &sdata->u.sta);
		return 0;
	} else if (sdata->vif.type == IEEE80211_IF_TYPE_WDS) {
		/*
		 * If it is necessary to update the WDS peer address
		 * while the interface is running, then we need to do
		 * more work here, namely if it is running we need to
		 * add a new and remove the old STA entry, this is
		 * normally handled by _open() and _stop().
		 */
		if (netif_running(dev))
			return -EBUSY;

		memcpy(&sdata->u.wds.remote_addr, (u8 *) &ap_addr->sa_data,
		       ETH_ALEN);

		return 0;
	}

	return -EOPNOTSUPP;
}


static int ieee80211_ioctl_giwap(struct net_device *dev,
				 struct iw_request_info *info,
				 struct sockaddr *ap_addr, char *extra)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->vif.type == IEEE80211_IF_TYPE_STA ||
	    sdata->vif.type == IEEE80211_IF_TYPE_IBSS) {
		if (sdata->u.sta.state == IEEE80211_ASSOCIATED ||
		    sdata->u.sta.state == IEEE80211_IBSS_JOINED) {
			ap_addr->sa_family = ARPHRD_ETHER;
			memcpy(&ap_addr->sa_data, sdata->u.sta.bssid, ETH_ALEN);
			return 0;
		} else {
			memset(&ap_addr->sa_data, 0, ETH_ALEN);
			return 0;
		}
	} else if (sdata->vif.type == IEEE80211_IF_TYPE_WDS) {
		ap_addr->sa_family = ARPHRD_ETHER;
		memcpy(&ap_addr->sa_data, sdata->u.wds.remote_addr, ETH_ALEN);
		return 0;
	}

	return -EOPNOTSUPP;
}


static int ieee80211_ioctl_siwscan(struct net_device *dev,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *extra)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct iw_scan_req *req = NULL;
	u8 *ssid = NULL;
	size_t ssid_len = 0;

	if (!netif_running(dev))
		return -ENETDOWN;

	if (sdata->vif.type != IEEE80211_IF_TYPE_STA &&
	    sdata->vif.type != IEEE80211_IF_TYPE_IBSS &&
	    sdata->vif.type != IEEE80211_IF_TYPE_MESH_POINT &&
	    sdata->vif.type != IEEE80211_IF_TYPE_AP)
		return -EOPNOTSUPP;

	/* if SSID was specified explicitly then use that */
	if (wrqu->data.length == sizeof(struct iw_scan_req) &&
	    wrqu->data.flags & IW_SCAN_THIS_ESSID) {
		req = (struct iw_scan_req *)extra;
		ssid = req->essid;
		ssid_len = req->essid_len;
	}

	return ieee80211_sta_req_scan(dev, ssid, ssid_len);
}


static int ieee80211_ioctl_giwscan(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_point *data, char *extra)
{
	int res;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	if (local->sta_sw_scanning || local->sta_hw_scanning)
		return -EAGAIN;

	res = ieee80211_sta_scan_results(dev, info, extra, data->length);
	if (res >= 0) {
		data->length = res;
		return 0;
	}
	data->length = 0;
	return res;
}


static int ieee80211_ioctl_siwrate(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_param *rate, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	int i, err = -EINVAL;
	u32 target_rate = rate->value / 100000;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_supported_band *sband;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

	/* target_rate = -1, rate->fixed = 0 means auto only, so use all rates
	 * target_rate = X, rate->fixed = 1 means only rate X
	 * target_rate = X, rate->fixed = 0 means all rates <= X */
	sdata->max_ratectrl_rateidx = -1;
	sdata->force_unicast_rateidx = -1;
	if (rate->value < 0)
		return 0;

	for (i=0; i< sband->n_bitrates; i++) {
		struct ieee80211_rate *brate = &sband->bitrates[i];
		int this_rate = brate->bitrate;

		if (target_rate == this_rate) {
			sdata->max_ratectrl_rateidx = i;
			if (rate->fixed)
				sdata->force_unicast_rateidx = i;
			err = 0;
			break;
		}
	}
	return err;
}

static int ieee80211_ioctl_giwrate(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_param *rate, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_supported_band *sband;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type != IEEE80211_IF_TYPE_STA)
		return -EOPNOTSUPP;

	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

	rcu_read_lock();

	sta = sta_info_get(local, sdata->u.sta.bssid);

	if (sta && sta->txrate_idx < sband->n_bitrates)
		rate->value = sband->bitrates[sta->txrate_idx].bitrate;
	else
		rate->value = 0;

	rcu_read_unlock();

	if (!sta)
		return -ENODEV;

	rate->value *= 100000;

	return 0;
}

static int ieee80211_ioctl_siwtxpower(struct net_device *dev,
				      struct iw_request_info *info,
				      union iwreq_data *data, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	bool need_reconfig = 0;
	int new_power_level;

	if ((data->txpower.flags & IW_TXPOW_TYPE) != IW_TXPOW_DBM)
		return -EINVAL;
	if (data->txpower.flags & IW_TXPOW_RANGE)
		return -EINVAL;

	if (data->txpower.fixed) {
		new_power_level = data->txpower.value;
	} else {
		/*
		 * Automatic power level. Use maximum power for the current
		 * channel. Should be part of rate control.
		 */
		struct ieee80211_channel* chan = local->hw.conf.channel;
		if (!chan)
			return -EINVAL;

		new_power_level = chan->max_power;
	}

	if (local->hw.conf.power_level != new_power_level) {
		local->hw.conf.power_level = new_power_level;
		need_reconfig = 1;
	}

	if (local->hw.conf.radio_enabled != !(data->txpower.disabled)) {
		local->hw.conf.radio_enabled = !(data->txpower.disabled);
		need_reconfig = 1;
		ieee80211_led_radio(local, local->hw.conf.radio_enabled);
	}

	if (need_reconfig) {
		ieee80211_hw_config(local);
		/* The return value of hw_config is not of big interest here,
		 * as it doesn't say that it failed because of _this_ config
		 * change or something else. Ignore it. */
	}

	return 0;
}

static int ieee80211_ioctl_giwtxpower(struct net_device *dev,
				   struct iw_request_info *info,
				   union iwreq_data *data, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	data->txpower.fixed = 1;
	data->txpower.disabled = !(local->hw.conf.radio_enabled);
	data->txpower.value = local->hw.conf.power_level;
	data->txpower.flags = IW_TXPOW_DBM;

	return 0;
}

static int ieee80211_ioctl_siwrts(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_param *rts, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	if (rts->disabled)
		local->rts_threshold = IEEE80211_MAX_RTS_THRESHOLD;
	else if (!rts->fixed)
		/* if the rts value is not fixed, then take default */
		local->rts_threshold = IEEE80211_MAX_RTS_THRESHOLD;
	else if (rts->value < 0 || rts->value > IEEE80211_MAX_RTS_THRESHOLD)
		return -EINVAL;
	else
		local->rts_threshold = rts->value;

	/* If the wlan card performs RTS/CTS in hardware/firmware,
	 * configure it here */

	if (local->ops->set_rts_threshold)
		local->ops->set_rts_threshold(local_to_hw(local),
					     local->rts_threshold);

	return 0;
}

static int ieee80211_ioctl_giwrts(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_param *rts, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	rts->value = local->rts_threshold;
	rts->disabled = (rts->value >= IEEE80211_MAX_RTS_THRESHOLD);
	rts->fixed = 1;

	return 0;
}


static int ieee80211_ioctl_siwfrag(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_param *frag, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	if (frag->disabled)
		local->fragmentation_threshold = IEEE80211_MAX_FRAG_THRESHOLD;
	else if (!frag->fixed)
		local->fragmentation_threshold = IEEE80211_MAX_FRAG_THRESHOLD;
	else if (frag->value < 256 ||
		 frag->value > IEEE80211_MAX_FRAG_THRESHOLD)
		return -EINVAL;
	else {
		/* Fragment length must be even, so strip LSB. */
		local->fragmentation_threshold = frag->value & ~0x1;
	}

	/* If the wlan card performs fragmentation in hardware/firmware,
	 * configure it here */

	if (local->ops->set_frag_threshold)
		local->ops->set_frag_threshold(
			local_to_hw(local),
			local->fragmentation_threshold);

	return 0;
}

static int ieee80211_ioctl_giwfrag(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_param *frag, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	frag->value = local->fragmentation_threshold;
	frag->disabled = (frag->value >= IEEE80211_MAX_RTS_THRESHOLD);
	frag->fixed = 1;

	return 0;
}


static int ieee80211_ioctl_siwretry(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_param *retry, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	if (retry->disabled ||
	    (retry->flags & IW_RETRY_TYPE) != IW_RETRY_LIMIT)
		return -EINVAL;

	if (retry->flags & IW_RETRY_MAX)
		local->long_retry_limit = retry->value;
	else if (retry->flags & IW_RETRY_MIN)
		local->short_retry_limit = retry->value;
	else {
		local->long_retry_limit = retry->value;
		local->short_retry_limit = retry->value;
	}

	if (local->ops->set_retry_limit) {
		return local->ops->set_retry_limit(
			local_to_hw(local),
			local->short_retry_limit,
			local->long_retry_limit);
	}

	return 0;
}


static int ieee80211_ioctl_giwretry(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_param *retry, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	retry->disabled = 0;
	if (retry->flags == 0 || retry->flags & IW_RETRY_MIN) {
		/* first return min value, iwconfig will ask max value
		 * later if needed */
		retry->flags |= IW_RETRY_LIMIT;
		retry->value = local->short_retry_limit;
		if (local->long_retry_limit != local->short_retry_limit)
			retry->flags |= IW_RETRY_MIN;
		return 0;
	}
	if (retry->flags & IW_RETRY_MAX) {
		retry->flags = IW_RETRY_LIMIT | IW_RETRY_MAX;
		retry->value = local->long_retry_limit;
	}

	return 0;
}

static int ieee80211_ioctl_siwmlme(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_point *data, char *extra)
{
	struct ieee80211_sub_if_data *sdata;
	struct iw_mlme *mlme = (struct iw_mlme *) extra;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->vif.type != IEEE80211_IF_TYPE_STA &&
	    sdata->vif.type != IEEE80211_IF_TYPE_IBSS)
		return -EINVAL;

	switch (mlme->cmd) {
	case IW_MLME_DEAUTH:
		/* TODO: mlme->addr.sa_data */
		return ieee80211_sta_deauthenticate(dev, mlme->reason_code);
	case IW_MLME_DISASSOC:
		/* TODO: mlme->addr.sa_data */
		return ieee80211_sta_disassociate(dev, mlme->reason_code);
	default:
		return -EOPNOTSUPP;
	}
}


static int ieee80211_ioctl_siwencode(struct net_device *dev,
				     struct iw_request_info *info,
				     struct iw_point *erq, char *keybuf)
{
	struct ieee80211_sub_if_data *sdata;
	int idx, i, alg = ALG_WEP;
	u8 bcaddr[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	int remove = 0;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	idx = erq->flags & IW_ENCODE_INDEX;
	if (idx == 0) {
		if (sdata->default_key)
			for (i = 0; i < NUM_DEFAULT_KEYS; i++) {
				if (sdata->default_key == sdata->keys[i]) {
					idx = i;
					break;
				}
			}
	} else if (idx < 1 || idx > 4)
		return -EINVAL;
	else
		idx--;

	if (erq->flags & IW_ENCODE_DISABLED)
		remove = 1;
	else if (erq->length == 0) {
		/* No key data - just set the default TX key index */
		ieee80211_set_default_key(sdata, idx);
		return 0;
	}

	return ieee80211_set_encryption(
		dev, bcaddr,
		idx, alg, remove,
		!sdata->default_key,
		keybuf, erq->length);
}


static int ieee80211_ioctl_giwencode(struct net_device *dev,
				     struct iw_request_info *info,
				     struct iw_point *erq, char *key)
{
	struct ieee80211_sub_if_data *sdata;
	int idx, i;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	idx = erq->flags & IW_ENCODE_INDEX;
	if (idx < 1 || idx > 4) {
		idx = -1;
		if (!sdata->default_key)
			idx = 0;
		else for (i = 0; i < NUM_DEFAULT_KEYS; i++) {
			if (sdata->default_key == sdata->keys[i]) {
				idx = i;
				break;
			}
		}
		if (idx < 0)
			return -EINVAL;
	} else
		idx--;

	erq->flags = idx + 1;

	if (!sdata->keys[idx]) {
		erq->length = 0;
		erq->flags |= IW_ENCODE_DISABLED;
		return 0;
	}

	memcpy(key, sdata->keys[idx]->conf.key,
	       min_t(int, erq->length, sdata->keys[idx]->conf.keylen));
	erq->length = sdata->keys[idx]->conf.keylen;
	erq->flags |= IW_ENCODE_ENABLED;

	if (sdata->vif.type == IEEE80211_IF_TYPE_STA) {
		struct ieee80211_if_sta *ifsta = &sdata->u.sta;
		switch (ifsta->auth_alg) {
		case WLAN_AUTH_OPEN:
		case WLAN_AUTH_LEAP:
			erq->flags |= IW_ENCODE_OPEN;
			break;
		case WLAN_AUTH_SHARED_KEY:
			erq->flags |= IW_ENCODE_RESTRICTED;
			break;
		}
	}

	return 0;
}

static int ieee80211_ioctl_siwpower(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_param *wrq,
				    char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_conf *conf = &local->hw.conf;

	if (wrq->disabled) {
		conf->flags &= ~IEEE80211_CONF_PS;
		return ieee80211_hw_config(local);
	}

	switch (wrq->flags & IW_POWER_MODE) {
	case IW_POWER_ON:       /* If not specified */
	case IW_POWER_MODE:     /* If set all mask */
	case IW_POWER_ALL_R:    /* If explicitely state all */
		conf->flags |= IEEE80211_CONF_PS;
		break;
	default:                /* Otherwise we don't support it */
		return -EINVAL;
	}

	return ieee80211_hw_config(local);
}

static int ieee80211_ioctl_giwpower(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu,
				    char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_conf *conf = &local->hw.conf;

	wrqu->power.disabled = !(conf->flags & IEEE80211_CONF_PS);

	return 0;
}

static int ieee80211_ioctl_siwauth(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_param *data, char *extra)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int ret = 0;

	switch (data->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_WPA_ENABLED:
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
	case IW_AUTH_KEY_MGMT:
		break;
	case IW_AUTH_DROP_UNENCRYPTED:
		sdata->drop_unencrypted = !!data->value;
		break;
	case IW_AUTH_PRIVACY_INVOKED:
		if (sdata->vif.type != IEEE80211_IF_TYPE_STA)
			ret = -EINVAL;
		else {
			sdata->u.sta.flags &= ~IEEE80211_STA_PRIVACY_INVOKED;
			/*
			 * Privacy invoked by wpa_supplicant, store the
			 * value and allow associating to a protected
			 * network without having a key up front.
			 */
			if (data->value)
				sdata->u.sta.flags |=
					IEEE80211_STA_PRIVACY_INVOKED;
		}
		break;
	case IW_AUTH_80211_AUTH_ALG:
		if (sdata->vif.type == IEEE80211_IF_TYPE_STA ||
		    sdata->vif.type == IEEE80211_IF_TYPE_IBSS)
			sdata->u.sta.auth_algs = data->value;
		else
			ret = -EOPNOTSUPP;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	return ret;
}

/* Get wireless statistics.  Called by /proc/net/wireless and by SIOCGIWSTATS */
static struct iw_statistics *ieee80211_get_wireless_stats(struct net_device *dev)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct iw_statistics *wstats = &local->wstats;
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct sta_info *sta = NULL;

	rcu_read_lock();

	if (sdata->vif.type == IEEE80211_IF_TYPE_STA ||
	    sdata->vif.type == IEEE80211_IF_TYPE_IBSS)
		sta = sta_info_get(local, sdata->u.sta.bssid);
	if (!sta) {
		wstats->discard.fragment = 0;
		wstats->discard.misc = 0;
		wstats->qual.qual = 0;
		wstats->qual.level = 0;
		wstats->qual.noise = 0;
		wstats->qual.updated = IW_QUAL_ALL_INVALID;
	} else {
		wstats->qual.level = sta->last_signal;
		wstats->qual.qual = sta->last_qual;
		wstats->qual.noise = sta->last_noise;
		wstats->qual.updated = local->wstats_flags;
	}

	rcu_read_unlock();

	return wstats;
}

static int ieee80211_ioctl_giwauth(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_param *data, char *extra)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int ret = 0;

	switch (data->flags & IW_AUTH_INDEX) {
	case IW_AUTH_80211_AUTH_ALG:
		if (sdata->vif.type == IEEE80211_IF_TYPE_STA ||
		    sdata->vif.type == IEEE80211_IF_TYPE_IBSS)
			data->value = sdata->u.sta.auth_algs;
		else
			ret = -EOPNOTSUPP;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	return ret;
}


static int ieee80211_ioctl_siwencodeext(struct net_device *dev,
					struct iw_request_info *info,
					struct iw_point *erq, char *extra)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct iw_encode_ext *ext = (struct iw_encode_ext *) extra;
	int uninitialized_var(alg), idx, i, remove = 0;

	switch (ext->alg) {
	case IW_ENCODE_ALG_NONE:
		remove = 1;
		break;
	case IW_ENCODE_ALG_WEP:
		alg = ALG_WEP;
		break;
	case IW_ENCODE_ALG_TKIP:
		alg = ALG_TKIP;
		break;
	case IW_ENCODE_ALG_CCMP:
		alg = ALG_CCMP;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (erq->flags & IW_ENCODE_DISABLED)
		remove = 1;

	idx = erq->flags & IW_ENCODE_INDEX;
	if (idx < 1 || idx > 4) {
		idx = -1;
		if (!sdata->default_key)
			idx = 0;
		else for (i = 0; i < NUM_DEFAULT_KEYS; i++) {
			if (sdata->default_key == sdata->keys[i]) {
				idx = i;
				break;
			}
		}
		if (idx < 0)
			return -EINVAL;
	} else
		idx--;

	return ieee80211_set_encryption(dev, ext->addr.sa_data, idx, alg,
					remove,
					ext->ext_flags &
					IW_ENCODE_EXT_SET_TX_KEY,
					ext->key, ext->key_len);
}


/* Structures to export the Wireless Handlers */

static const iw_handler ieee80211_handler[] =
{
	(iw_handler) NULL,				/* SIOCSIWCOMMIT */
	(iw_handler) ieee80211_ioctl_giwname,		/* SIOCGIWNAME */
	(iw_handler) NULL,				/* SIOCSIWNWID */
	(iw_handler) NULL,				/* SIOCGIWNWID */
	(iw_handler) ieee80211_ioctl_siwfreq,		/* SIOCSIWFREQ */
	(iw_handler) ieee80211_ioctl_giwfreq,		/* SIOCGIWFREQ */
	(iw_handler) ieee80211_ioctl_siwmode,		/* SIOCSIWMODE */
	(iw_handler) ieee80211_ioctl_giwmode,		/* SIOCGIWMODE */
	(iw_handler) NULL,				/* SIOCSIWSENS */
	(iw_handler) NULL,				/* SIOCGIWSENS */
	(iw_handler) NULL /* not used */,		/* SIOCSIWRANGE */
	(iw_handler) ieee80211_ioctl_giwrange,		/* SIOCGIWRANGE */
	(iw_handler) NULL /* not used */,		/* SIOCSIWPRIV */
	(iw_handler) NULL /* kernel code */,		/* SIOCGIWPRIV */
	(iw_handler) NULL /* not used */,		/* SIOCSIWSTATS */
	(iw_handler) NULL /* kernel code */,		/* SIOCGIWSTATS */
	(iw_handler) NULL,				/* SIOCSIWSPY */
	(iw_handler) NULL,				/* SIOCGIWSPY */
	(iw_handler) NULL,				/* SIOCSIWTHRSPY */
	(iw_handler) NULL,				/* SIOCGIWTHRSPY */
	(iw_handler) ieee80211_ioctl_siwap,		/* SIOCSIWAP */
	(iw_handler) ieee80211_ioctl_giwap,		/* SIOCGIWAP */
	(iw_handler) ieee80211_ioctl_siwmlme,		/* SIOCSIWMLME */
	(iw_handler) NULL,				/* SIOCGIWAPLIST */
	(iw_handler) ieee80211_ioctl_siwscan,		/* SIOCSIWSCAN */
	(iw_handler) ieee80211_ioctl_giwscan,		/* SIOCGIWSCAN */
	(iw_handler) ieee80211_ioctl_siwessid,		/* SIOCSIWESSID */
	(iw_handler) ieee80211_ioctl_giwessid,		/* SIOCGIWESSID */
	(iw_handler) NULL,				/* SIOCSIWNICKN */
	(iw_handler) NULL,				/* SIOCGIWNICKN */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) ieee80211_ioctl_siwrate,		/* SIOCSIWRATE */
	(iw_handler) ieee80211_ioctl_giwrate,		/* SIOCGIWRATE */
	(iw_handler) ieee80211_ioctl_siwrts,		/* SIOCSIWRTS */
	(iw_handler) ieee80211_ioctl_giwrts,		/* SIOCGIWRTS */
	(iw_handler) ieee80211_ioctl_siwfrag,		/* SIOCSIWFRAG */
	(iw_handler) ieee80211_ioctl_giwfrag,		/* SIOCGIWFRAG */
	(iw_handler) ieee80211_ioctl_siwtxpower,	/* SIOCSIWTXPOW */
	(iw_handler) ieee80211_ioctl_giwtxpower,	/* SIOCGIWTXPOW */
	(iw_handler) ieee80211_ioctl_siwretry,		/* SIOCSIWRETRY */
	(iw_handler) ieee80211_ioctl_giwretry,		/* SIOCGIWRETRY */
	(iw_handler) ieee80211_ioctl_siwencode,		/* SIOCSIWENCODE */
	(iw_handler) ieee80211_ioctl_giwencode,		/* SIOCGIWENCODE */
	(iw_handler) ieee80211_ioctl_siwpower,		/* SIOCSIWPOWER */
	(iw_handler) ieee80211_ioctl_giwpower,		/* SIOCGIWPOWER */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) ieee80211_ioctl_siwgenie,		/* SIOCSIWGENIE */
	(iw_handler) NULL,				/* SIOCGIWGENIE */
	(iw_handler) ieee80211_ioctl_siwauth,		/* SIOCSIWAUTH */
	(iw_handler) ieee80211_ioctl_giwauth,		/* SIOCGIWAUTH */
	(iw_handler) ieee80211_ioctl_siwencodeext,	/* SIOCSIWENCODEEXT */
	(iw_handler) NULL,				/* SIOCGIWENCODEEXT */
	(iw_handler) NULL,				/* SIOCSIWPMKSA */
	(iw_handler) NULL,				/* -- hole -- */
};

const struct iw_handler_def ieee80211_iw_handler_def =
{
	.num_standard	= ARRAY_SIZE(ieee80211_handler),
	.standard	= (iw_handler *) ieee80211_handler,
	.get_wireless_stats = ieee80211_get_wireless_stats,
};
