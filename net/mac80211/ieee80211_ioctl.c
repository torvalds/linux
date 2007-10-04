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
#include "hostapd_ioctl.h"
#include "ieee80211_rate.h"
#include "wpa.h"
#include "aes_ccm.h"


/*
 * Wow. This ioctl interface is such crap, it's tied
 * to internal definitions. I hope it dies soon.
 */
static int mode_to_hostapd_mode(enum ieee80211_phymode mode)
{
	switch (mode) {
	case MODE_IEEE80211A:
		return 0;
	case MODE_IEEE80211B:
		return 1;
	case MODE_IEEE80211G:
		return 3;
	case NUM_IEEE80211_MODES:
		WARN_ON(1);
		break;
	}
	WARN_ON(1);
	return -1;
}

static enum ieee80211_phymode hostapd_mode_to_mode(int hostapd_mode)
{
	switch (hostapd_mode) {
	case 0:
		return MODE_IEEE80211A;
	case 1:
		return MODE_IEEE80211B;
	case 3:
		return MODE_IEEE80211G;
	}
	return NUM_IEEE80211_MODES;
}

static int ieee80211_set_encryption(struct net_device *dev, u8 *sta_addr,
				    int idx, int alg, int set_tx_key,
				    const u8 *_key, size_t key_len)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	int ret = 0;
	struct sta_info *sta;
	struct ieee80211_key *key;
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (idx < 0 || idx >= NUM_DEFAULT_KEYS) {
		printk(KERN_DEBUG "%s: set_encrypt - invalid idx=%d\n",
		       dev->name, idx);
		return -EINVAL;
	}

	if (is_broadcast_ether_addr(sta_addr)) {
		sta = NULL;
		key = sdata->keys[idx];
	} else {
		set_tx_key = 0;
		/*
		 * According to the standard, the key index of a pairwise
		 * key must be zero. However, some AP are broken when it
		 * comes to WEP key indices, so we work around this.
		 */
		if (idx != 0 && alg != ALG_WEP) {
			printk(KERN_DEBUG "%s: set_encrypt - non-zero idx for "
			       "individual key\n", dev->name);
			return -EINVAL;
		}

		sta = sta_info_get(local, sta_addr);
		if (!sta) {
#ifdef CONFIG_MAC80211_VERBOSE_DEBUG
			DECLARE_MAC_BUF(mac);
			printk(KERN_DEBUG "%s: set_encrypt - unknown addr "
			       "%s\n",
			       dev->name, print_mac(mac, sta_addr));
#endif /* CONFIG_MAC80211_VERBOSE_DEBUG */

			return -ENOENT;
		}

		key = sta->key;
	}

	if (alg == ALG_NONE) {
		ieee80211_key_free(key);
		key = NULL;
	} else {
		/*
		 * Automatically frees any old key if present.
		 */
		key = ieee80211_key_alloc(sdata, sta, alg, idx, key_len, _key);
		if (!key) {
			ret = -ENOMEM;
			goto err_out;
		}
	}

	if (set_tx_key || (!sta && !sdata->default_key && key))
		ieee80211_set_default_key(sdata, idx);

	ret = 0;
 err_out:
	if (sta)
		sta_info_put(sta);
	return ret;
}

static int ieee80211_ioctl_siwgenie(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_point *data, char *extra)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	if (local->user_space_mlme)
		return -EOPNOTSUPP;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->type == IEEE80211_IF_TYPE_STA ||
	    sdata->type == IEEE80211_IF_TYPE_IBSS) {
		int ret = ieee80211_sta_set_extra_ie(dev, extra, data->length);
		if (ret)
			return ret;
		sdata->u.sta.flags &= ~IEEE80211_STA_AUTO_BSSID_SEL;
		ieee80211_sta_req_auth(dev, &sdata->u.sta);
		return 0;
	}

	if (sdata->type == IEEE80211_IF_TYPE_AP) {
		kfree(sdata->u.ap.generic_elem);
		sdata->u.ap.generic_elem = kmalloc(data->length, GFP_KERNEL);
		if (!sdata->u.ap.generic_elem)
			return -ENOMEM;
		memcpy(sdata->u.ap.generic_elem, extra, data->length);
		sdata->u.ap.generic_elem_len = data->length;
		return ieee80211_if_config(dev);
	}
	return -EOPNOTSUPP;
}

static int ieee80211_ioctl_giwname(struct net_device *dev,
				   struct iw_request_info *info,
				   char *name, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	switch (local->hw.conf.phymode) {
	case MODE_IEEE80211A:
		strcpy(name, "IEEE 802.11a");
		break;
	case MODE_IEEE80211B:
		strcpy(name, "IEEE 802.11b");
		break;
	case MODE_IEEE80211G:
		strcpy(name, "IEEE 802.11g");
		break;
	default:
		strcpy(name, "IEEE 802.11");
		break;
	}

	return 0;
}


static int ieee80211_ioctl_giwrange(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *data, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct iw_range *range = (struct iw_range *) extra;
	struct ieee80211_hw_mode *mode = NULL;
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

	range->max_qual.qual = local->hw.max_signal;
	range->max_qual.level = local->hw.max_rssi;
	range->max_qual.noise = local->hw.max_noise;
	range->max_qual.updated = local->wstats_flags;

	range->avg_qual.qual = local->hw.max_signal/2;
	range->avg_qual.level = 0;
	range->avg_qual.noise = 0;
	range->avg_qual.updated = local->wstats_flags;

	range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
			  IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;

	list_for_each_entry(mode, &local->modes_list, list) {
		int i = 0;

		if (!(local->enabled_modes & (1 << mode->mode)) ||
		    (local->hw_modes & local->enabled_modes &
		     (1 << MODE_IEEE80211G) && mode->mode == MODE_IEEE80211B))
			continue;

		while (i < mode->num_channels && c < IW_MAX_FREQUENCIES) {
			struct ieee80211_channel *chan = &mode->channels[i];

			if (chan->flag & IEEE80211_CHAN_W_SCAN) {
				range->freq[c].i = chan->chan;
				range->freq[c].m = chan->freq * 100000;
				range->freq[c].e = 1;
				c++;
			}
			i++;
		}
	}
	range->num_channels = c;
	range->num_frequency = c;

	IW_EVENT_CAPA_SET_KERNEL(range->event_capa);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWTHRSPY);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWAP);
	IW_EVENT_CAPA_SET(range->event_capa, SIOCGIWSCAN);

	return 0;
}


static int ieee80211_ioctl_siwmode(struct net_device *dev,
				   struct iw_request_info *info,
				   __u32 *mode, char *extra)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int type;

	if (sdata->type == IEEE80211_IF_TYPE_VLAN)
		return -EOPNOTSUPP;

	switch (*mode) {
	case IW_MODE_INFRA:
		type = IEEE80211_IF_TYPE_STA;
		break;
	case IW_MODE_ADHOC:
		type = IEEE80211_IF_TYPE_IBSS;
		break;
	case IW_MODE_MONITOR:
		type = IEEE80211_IF_TYPE_MNTR;
		break;
	default:
		return -EINVAL;
	}

	if (type == sdata->type)
		return 0;
	if (netif_running(dev))
		return -EBUSY;

	ieee80211_if_reinit(dev);
	ieee80211_if_set_type(dev, type);

	return 0;
}


static int ieee80211_ioctl_giwmode(struct net_device *dev,
				   struct iw_request_info *info,
				   __u32 *mode, char *extra)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	switch (sdata->type) {
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

int ieee80211_set_channel(struct ieee80211_local *local, int channel, int freq)
{
	struct ieee80211_hw_mode *mode;
	int c, set = 0;
	int ret = -EINVAL;

	list_for_each_entry(mode, &local->modes_list, list) {
		if (!(local->enabled_modes & (1 << mode->mode)))
			continue;
		for (c = 0; c < mode->num_channels; c++) {
			struct ieee80211_channel *chan = &mode->channels[c];
			if (chan->flag & IEEE80211_CHAN_W_SCAN &&
			    ((chan->chan == channel) || (chan->freq == freq))) {
				/* Use next_mode as the mode preference to
				 * resolve non-unique channel numbers. */
				if (set && mode->mode != local->next_mode)
					continue;

				local->oper_channel = chan;
				local->oper_hw_mode = mode;
				set++;
			}
		}
	}

	if (set) {
		if (local->sta_scanning)
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
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->type == IEEE80211_IF_TYPE_STA)
		sdata->u.sta.flags &= ~IEEE80211_STA_AUTO_CHANNEL_SEL;

	/* freq->e == 0: freq->m = channel; otherwise freq = m * 10^e */
	if (freq->e == 0) {
		if (freq->m < 0) {
			if (sdata->type == IEEE80211_IF_TYPE_STA)
				sdata->u.sta.flags |=
					IEEE80211_STA_AUTO_CHANNEL_SEL;
			return 0;
		} else
			return ieee80211_set_channel(local, freq->m, -1);
	} else {
		int i, div = 1000000;
		for (i = 0; i < freq->e; i++)
			div /= 10;
		if (div > 0)
			return ieee80211_set_channel(local, -1, freq->m / div);
		else
			return -EINVAL;
	}
}


static int ieee80211_ioctl_giwfreq(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_freq *freq, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	/* TODO: in station mode (Managed/Ad-hoc) might need to poll low-level
	 * driver for the current channel with firmware-based management */

	freq->m = local->hw.conf.freq;
	freq->e = 6;

	return 0;
}


static int ieee80211_ioctl_siwessid(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_point *data, char *ssid)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;
	size_t len = data->length;

	/* iwconfig uses nul termination in SSID.. */
	if (len > 0 && ssid[len - 1] == '\0')
		len--;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->type == IEEE80211_IF_TYPE_STA ||
	    sdata->type == IEEE80211_IF_TYPE_IBSS) {
		int ret;
		if (local->user_space_mlme) {
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

	if (sdata->type == IEEE80211_IF_TYPE_AP) {
		memcpy(sdata->u.ap.ssid, ssid, len);
		memset(sdata->u.ap.ssid + len, 0,
		       IEEE80211_MAX_SSID_LEN - len);
		sdata->u.ap.ssid_len = len;
		return ieee80211_if_config(dev);
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
	if (sdata->type == IEEE80211_IF_TYPE_STA ||
	    sdata->type == IEEE80211_IF_TYPE_IBSS) {
		int res = ieee80211_sta_get_ssid(dev, ssid, &len);
		if (res == 0) {
			data->length = len;
			data->flags = 1;
		} else
			data->flags = 0;
		return res;
	}

	if (sdata->type == IEEE80211_IF_TYPE_AP) {
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
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->type == IEEE80211_IF_TYPE_STA ||
	    sdata->type == IEEE80211_IF_TYPE_IBSS) {
		int ret;
		if (local->user_space_mlme) {
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
	} else if (sdata->type == IEEE80211_IF_TYPE_WDS) {
		if (memcmp(sdata->u.wds.remote_addr, (u8 *) &ap_addr->sa_data,
			   ETH_ALEN) == 0)
			return 0;
		return ieee80211_if_update_wds(dev, (u8 *) &ap_addr->sa_data);
	}

	return -EOPNOTSUPP;
}


static int ieee80211_ioctl_giwap(struct net_device *dev,
				 struct iw_request_info *info,
				 struct sockaddr *ap_addr, char *extra)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->type == IEEE80211_IF_TYPE_STA ||
	    sdata->type == IEEE80211_IF_TYPE_IBSS) {
		ap_addr->sa_family = ARPHRD_ETHER;
		memcpy(&ap_addr->sa_data, sdata->u.sta.bssid, ETH_ALEN);
		return 0;
	} else if (sdata->type == IEEE80211_IF_TYPE_WDS) {
		ap_addr->sa_family = ARPHRD_ETHER;
		memcpy(&ap_addr->sa_data, sdata->u.wds.remote_addr, ETH_ALEN);
		return 0;
	}

	return -EOPNOTSUPP;
}


static int ieee80211_ioctl_siwscan(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_point *data, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	u8 *ssid = NULL;
	size_t ssid_len = 0;

	if (!netif_running(dev))
		return -ENETDOWN;

	switch (sdata->type) {
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_IBSS:
		if (local->scan_flags & IEEE80211_SCAN_MATCH_SSID) {
			ssid = sdata->u.sta.ssid;
			ssid_len = sdata->u.sta.ssid_len;
		}
		break;
	case IEEE80211_IF_TYPE_AP:
		if (local->scan_flags & IEEE80211_SCAN_MATCH_SSID) {
			ssid = sdata->u.ap.ssid;
			ssid_len = sdata->u.ap.ssid_len;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ieee80211_sta_req_scan(dev, ssid, ssid_len);
}


static int ieee80211_ioctl_giwscan(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_point *data, char *extra)
{
	int res;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	if (local->sta_scanning)
		return -EAGAIN;
	res = ieee80211_sta_scan_results(dev, extra, data->length);
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
	struct ieee80211_hw_mode *mode;
	int i;
	u32 target_rate = rate->value / 100000;
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (!sdata->bss)
		return -ENODEV;
	mode = local->oper_hw_mode;
	/* target_rate = -1, rate->fixed = 0 means auto only, so use all rates
	 * target_rate = X, rate->fixed = 1 means only rate X
	 * target_rate = X, rate->fixed = 0 means all rates <= X */
	sdata->bss->max_ratectrl_rateidx = -1;
	sdata->bss->force_unicast_rateidx = -1;
	if (rate->value < 0)
		return 0;
	for (i=0; i< mode->num_rates; i++) {
		struct ieee80211_rate *rates = &mode->rates[i];
		int this_rate = rates->rate;

		if (target_rate == this_rate) {
			sdata->bss->max_ratectrl_rateidx = i;
			if (rate->fixed)
				sdata->bss->force_unicast_rateidx = i;
			break;
		}
	}
	return 0;
}

static int ieee80211_ioctl_giwrate(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_param *rate, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->type == IEEE80211_IF_TYPE_STA)
		sta = sta_info_get(local, sdata->u.sta.bssid);
	else
		return -EOPNOTSUPP;
	if (!sta)
		return -ENODEV;
	if (sta->txrate < local->oper_hw_mode->num_rates)
		rate->value = local->oper_hw_mode->rates[sta->txrate].rate * 100000;
	else
		rate->value = 0;
	sta_info_put(sta);
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

static int ieee80211_ioctl_prism2_param(struct net_device *dev,
					struct iw_request_info *info,
					void *wrqu, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;
	int *i = (int *) extra;
	int param = *i;
	int value = *(i + 1);
	int ret = 0;
	int mode;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	switch (param) {
	case PRISM2_PARAM_IEEE_802_1X:
		if (local->ops->set_ieee8021x)
			ret = local->ops->set_ieee8021x(local_to_hw(local),
							value);
		if (ret)
			printk(KERN_DEBUG "%s: failed to set IEEE 802.1X (%d) "
			       "for low-level driver\n", dev->name, value);
		else
			sdata->ieee802_1x = value;
		break;

	case PRISM2_PARAM_CTS_PROTECT_ERP_FRAMES:
		if (sdata->type == IEEE80211_IF_TYPE_AP) {
			if (value)
				sdata->flags |= IEEE80211_SDATA_USE_PROTECTION;
			else
				sdata->flags &= ~IEEE80211_SDATA_USE_PROTECTION;
			ieee80211_erp_info_change_notify(dev,
					IEEE80211_ERP_CHANGE_PROTECTION);
		} else {
			ret = -ENOENT;
		}
		break;

	case PRISM2_PARAM_PREAMBLE:
		if (sdata->type == IEEE80211_IF_TYPE_AP) {
			if (value)
				sdata->flags |= IEEE80211_SDATA_SHORT_PREAMBLE;
			else
				sdata->flags &= ~IEEE80211_SDATA_SHORT_PREAMBLE;
			ieee80211_erp_info_change_notify(dev,
					IEEE80211_ERP_CHANGE_PREAMBLE);
		} else {
			ret = -ENOENT;
		}
		break;

	case PRISM2_PARAM_SHORT_SLOT_TIME:
		if (value)
			local->hw.conf.flags |= IEEE80211_CONF_SHORT_SLOT_TIME;
		else
			local->hw.conf.flags &= ~IEEE80211_CONF_SHORT_SLOT_TIME;
		if (ieee80211_hw_config(local))
			ret = -EINVAL;
		break;

	case PRISM2_PARAM_NEXT_MODE:
		local->next_mode = hostapd_mode_to_mode(value);
		break;

	case PRISM2_PARAM_KEY_TX_RX_THRESHOLD:
		local->key_tx_rx_threshold = value;
		break;

	case PRISM2_PARAM_WIFI_WME_NOACK_TEST:
		local->wifi_wme_noack_test = value;
		break;

	case PRISM2_PARAM_SCAN_FLAGS:
		local->scan_flags = value;
		break;

	case PRISM2_PARAM_MIXED_CELL:
		if (sdata->type != IEEE80211_IF_TYPE_STA &&
		    sdata->type != IEEE80211_IF_TYPE_IBSS)
			ret = -EINVAL;
		else {
			if (value)
				sdata->u.sta.flags |= IEEE80211_STA_MIXED_CELL;
			else
				sdata->u.sta.flags &= ~IEEE80211_STA_MIXED_CELL;
		}
		break;

	case PRISM2_PARAM_HW_MODES:
		mode = 1;
		local->enabled_modes = 0;
		while (value) {
			if (value & 1)
				local->enabled_modes |=
					hostapd_mode_to_mode(mode);
			mode <<= 1;
			value >>= 1;
		}
		break;

	case PRISM2_PARAM_CREATE_IBSS:
		if (sdata->type != IEEE80211_IF_TYPE_IBSS)
			ret = -EINVAL;
		else {
			if (value)
				sdata->u.sta.flags |= IEEE80211_STA_CREATE_IBSS;
			else
				sdata->u.sta.flags &= ~IEEE80211_STA_CREATE_IBSS;
		}
		break;
	case PRISM2_PARAM_WMM_ENABLED:
		if (sdata->type != IEEE80211_IF_TYPE_STA &&
		    sdata->type != IEEE80211_IF_TYPE_IBSS)
			ret = -EINVAL;
		else {
			if (value)
				sdata->u.sta.flags |= IEEE80211_STA_WMM_ENABLED;
			else
				sdata->u.sta.flags &= ~IEEE80211_STA_WMM_ENABLED;
		}
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}


static int ieee80211_ioctl_get_prism2_param(struct net_device *dev,
					    struct iw_request_info *info,
					    void *wrqu, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata;
	int *param = (int *) extra;
	int ret = 0;
	int mode;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	switch (*param) {
	case PRISM2_PARAM_IEEE_802_1X:
		*param = sdata->ieee802_1x;
		break;

	case PRISM2_PARAM_CTS_PROTECT_ERP_FRAMES:
		*param = !!(sdata->flags & IEEE80211_SDATA_USE_PROTECTION);
		break;

	case PRISM2_PARAM_PREAMBLE:
		*param = !!(sdata->flags & IEEE80211_SDATA_SHORT_PREAMBLE);
		break;

	case PRISM2_PARAM_SHORT_SLOT_TIME:
		*param = !!(local->hw.conf.flags & IEEE80211_CONF_SHORT_SLOT_TIME);
		break;

	case PRISM2_PARAM_NEXT_MODE:
		*param = local->next_mode;
		break;

	case PRISM2_PARAM_KEY_TX_RX_THRESHOLD:
		*param = local->key_tx_rx_threshold;
		break;

	case PRISM2_PARAM_WIFI_WME_NOACK_TEST:
		*param = local->wifi_wme_noack_test;
		break;

	case PRISM2_PARAM_SCAN_FLAGS:
		*param = local->scan_flags;
		break;

	case PRISM2_PARAM_HW_MODES:
		mode = 0;
		*param = 0;
		while (mode < NUM_IEEE80211_MODES) {
			if (local->enabled_modes & (1<<mode))
				*param |= mode_to_hostapd_mode(1<<mode);
			mode++;
		}
		break;

	case PRISM2_PARAM_CREATE_IBSS:
		if (sdata->type != IEEE80211_IF_TYPE_IBSS)
			ret = -EINVAL;
		else
			*param = !!(sdata->u.sta.flags &
					IEEE80211_STA_CREATE_IBSS);
		break;

	case PRISM2_PARAM_MIXED_CELL:
		if (sdata->type != IEEE80211_IF_TYPE_STA &&
		    sdata->type != IEEE80211_IF_TYPE_IBSS)
			ret = -EINVAL;
		else
			*param = !!(sdata->u.sta.flags &
					IEEE80211_STA_MIXED_CELL);
		break;

	case PRISM2_PARAM_WMM_ENABLED:
		if (sdata->type != IEEE80211_IF_TYPE_STA &&
		    sdata->type != IEEE80211_IF_TYPE_IBSS)
			ret = -EINVAL;
		else
			*param = !!(sdata->u.sta.flags &
					IEEE80211_STA_WMM_ENABLED);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int ieee80211_ioctl_siwmlme(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_point *data, char *extra)
{
	struct ieee80211_sub_if_data *sdata;
	struct iw_mlme *mlme = (struct iw_mlme *) extra;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->type != IEEE80211_IF_TYPE_STA &&
	    sdata->type != IEEE80211_IF_TYPE_IBSS)
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
		alg = ALG_NONE;
	else if (erq->length == 0) {
		/* No key data - just set the default TX key index */
		ieee80211_set_default_key(sdata, idx);
		return 0;
	}

	return ieee80211_set_encryption(
		dev, bcaddr,
		idx, alg,
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

	return 0;
}

static int ieee80211_ioctl_siwauth(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_param *data, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int ret = 0;

	switch (data->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_WPA_ENABLED:
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		break;
	case IW_AUTH_KEY_MGMT:
		if (sdata->type != IEEE80211_IF_TYPE_STA)
			ret = -EINVAL;
		else {
			/*
			 * Key management was set by wpa_supplicant,
			 * we only need this to associate to a network
			 * that has privacy enabled regardless of not
			 * having a key.
			 */
			sdata->u.sta.key_management_enabled = !!data->value;
		}
		break;
	case IW_AUTH_80211_AUTH_ALG:
		if (sdata->type == IEEE80211_IF_TYPE_STA ||
		    sdata->type == IEEE80211_IF_TYPE_IBSS)
			sdata->u.sta.auth_algs = data->value;
		else
			ret = -EOPNOTSUPP;
		break;
	case IW_AUTH_PRIVACY_INVOKED:
		if (local->ops->set_privacy_invoked)
			ret = local->ops->set_privacy_invoked(
					local_to_hw(local), data->value);
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

	if (sdata->type == IEEE80211_IF_TYPE_STA ||
	    sdata->type == IEEE80211_IF_TYPE_IBSS)
		sta = sta_info_get(local, sdata->u.sta.bssid);
	if (!sta) {
		wstats->discard.fragment = 0;
		wstats->discard.misc = 0;
		wstats->qual.qual = 0;
		wstats->qual.level = 0;
		wstats->qual.noise = 0;
		wstats->qual.updated = IW_QUAL_ALL_INVALID;
	} else {
		wstats->qual.level = sta->last_rssi;
		wstats->qual.qual = sta->last_signal;
		wstats->qual.noise = sta->last_noise;
		wstats->qual.updated = local->wstats_flags;
		sta_info_put(sta);
	}
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
		if (sdata->type == IEEE80211_IF_TYPE_STA ||
		    sdata->type == IEEE80211_IF_TYPE_IBSS)
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
	int alg, idx, i;

	switch (ext->alg) {
	case IW_ENCODE_ALG_NONE:
		alg = ALG_NONE;
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
		alg = ALG_NONE;

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
					ext->ext_flags &
					IW_ENCODE_EXT_SET_TX_KEY,
					ext->key, ext->key_len);
}


static const struct iw_priv_args ieee80211_ioctl_priv[] = {
	{ PRISM2_IOCTL_PRISM2_PARAM,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0, "param" },
	{ PRISM2_IOCTL_GET_PRISM2_PARAM,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	  IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "get_param" },
};

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
	(iw_handler) NULL,				/* SIOCSIWTXPOW */
	(iw_handler) ieee80211_ioctl_giwtxpower,	/* SIOCGIWTXPOW */
	(iw_handler) ieee80211_ioctl_siwretry,		/* SIOCSIWRETRY */
	(iw_handler) ieee80211_ioctl_giwretry,		/* SIOCGIWRETRY */
	(iw_handler) ieee80211_ioctl_siwencode,		/* SIOCSIWENCODE */
	(iw_handler) ieee80211_ioctl_giwencode,		/* SIOCGIWENCODE */
	(iw_handler) NULL,				/* SIOCSIWPOWER */
	(iw_handler) NULL,				/* SIOCGIWPOWER */
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

static const iw_handler ieee80211_private_handler[] =
{							/* SIOCIWFIRSTPRIV + */
	(iw_handler) ieee80211_ioctl_prism2_param,	/* 0 */
	(iw_handler) ieee80211_ioctl_get_prism2_param,	/* 1 */
};

const struct iw_handler_def ieee80211_iw_handler_def =
{
	.num_standard	= ARRAY_SIZE(ieee80211_handler),
	.num_private	= ARRAY_SIZE(ieee80211_private_handler),
	.num_private_args = ARRAY_SIZE(ieee80211_ioctl_priv),
	.standard	= (iw_handler *) ieee80211_handler,
	.private	= (iw_handler *) ieee80211_private_handler,
	.private_args	= (struct iw_priv_args *) ieee80211_ioctl_priv,
	.get_wireless_stats = ieee80211_get_wireless_stats,
};
