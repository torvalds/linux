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


static int ieee80211_set_encryption(struct ieee80211_sub_if_data *sdata, u8 *sta_addr,
				    int idx, int alg, int remove,
				    int set_tx_key, const u8 *_key,
				    size_t key_len)
{
	struct ieee80211_local *local = sdata->local;
	struct sta_info *sta;
	struct ieee80211_key *key;
	int err;

	if (alg == ALG_AES_CMAC) {
		if (idx < NUM_DEFAULT_KEYS ||
		    idx >= NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS) {
			printk(KERN_DEBUG "%s: set_encrypt - invalid idx=%d "
			       "(BIP)\n", sdata->dev->name, idx);
			return -EINVAL;
		}
	} else if (idx < 0 || idx >= NUM_DEFAULT_KEYS) {
		printk(KERN_DEBUG "%s: set_encrypt - invalid idx=%d\n",
		       sdata->dev->name, idx);
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
		if (alg == ALG_AES_CMAC &&
		    (set_tx_key || (!sta && !sdata->default_mgmt_key && key)))
			ieee80211_set_default_mgmt_key(sdata, idx);
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

	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		int ret = ieee80211_sta_set_extra_ie(sdata, extra, data->length);
		if (ret)
			return ret;
		sdata->u.mgd.flags &= ~IEEE80211_STA_AUTO_BSSID_SEL;
		sdata->u.mgd.flags &= ~IEEE80211_STA_EXT_SME;
		ieee80211_sta_req_auth(sdata);
		return 0;
	}

	return -EOPNOTSUPP;
}

static int ieee80211_ioctl_siwfreq(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_freq *freq, char *extra)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		sdata->u.ibss.flags &= ~IEEE80211_IBSS_AUTO_CHANNEL_SEL;
	else if (sdata->vif.type == NL80211_IFTYPE_STATION)
		sdata->u.mgd.flags &= ~IEEE80211_STA_AUTO_CHANNEL_SEL;

	/* freq->e == 0: freq->m = channel; otherwise freq = m * 10^e */
	if (freq->e == 0) {
		if (freq->m < 0) {
			if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
				sdata->u.ibss.flags |=
					IEEE80211_IBSS_AUTO_CHANNEL_SEL;
			else if (sdata->vif.type == NL80211_IFTYPE_STATION)
				sdata->u.mgd.flags |=
					IEEE80211_STA_AUTO_CHANNEL_SEL;
			return 0;
		} else
			return ieee80211_set_freq(sdata,
				ieee80211_channel_to_frequency(freq->m));
	} else {
		int i, div = 1000000;
		for (i = 0; i < freq->e; i++)
			div /= 10;
		if (div > 0)
			return ieee80211_set_freq(sdata, freq->m / div);
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
	int ret;

	/* iwconfig uses nul termination in SSID.. */
	if (len > 0 && ssid[len - 1] == '\0')
		len--;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		if (data->flags)
			sdata->u.mgd.flags &= ~IEEE80211_STA_AUTO_SSID_SEL;
		else
			sdata->u.mgd.flags |= IEEE80211_STA_AUTO_SSID_SEL;

		ret = ieee80211_sta_set_ssid(sdata, ssid, len);
		if (ret)
			return ret;

		sdata->u.mgd.flags &= ~IEEE80211_STA_EXT_SME;
		ieee80211_sta_req_auth(sdata);
		return 0;
	} else if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		return ieee80211_ibss_set_ssid(sdata, ssid, len);

	return -EOPNOTSUPP;
}


static int ieee80211_ioctl_giwessid(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_point *data, char *ssid)
{
	size_t len;

	struct ieee80211_sub_if_data *sdata;
	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		int res = ieee80211_sta_get_ssid(sdata, ssid, &len);
		if (res == 0) {
			data->length = len;
			data->flags = 1;
		} else
			data->flags = 0;
		return res;
	} else if (sdata->vif.type == NL80211_IFTYPE_ADHOC) {
		int res = ieee80211_ibss_get_ssid(sdata, ssid, &len);
		if (res == 0) {
			data->length = len;
			data->flags = 1;
		} else
			data->flags = 0;
		return res;
	}

	return -EOPNOTSUPP;
}


static int ieee80211_ioctl_siwap(struct net_device *dev,
				 struct iw_request_info *info,
				 struct sockaddr *ap_addr, char *extra)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		int ret;

		if (is_zero_ether_addr((u8 *) &ap_addr->sa_data))
			sdata->u.mgd.flags |= IEEE80211_STA_AUTO_BSSID_SEL |
				IEEE80211_STA_AUTO_CHANNEL_SEL;
		else if (is_broadcast_ether_addr((u8 *) &ap_addr->sa_data))
			sdata->u.mgd.flags |= IEEE80211_STA_AUTO_BSSID_SEL;
		else
			sdata->u.mgd.flags &= ~IEEE80211_STA_AUTO_BSSID_SEL;
		ret = ieee80211_sta_set_bssid(sdata, (u8 *) &ap_addr->sa_data);
		if (ret)
			return ret;
		sdata->u.mgd.flags &= ~IEEE80211_STA_EXT_SME;
		ieee80211_sta_req_auth(sdata);
		return 0;
	} else if (sdata->vif.type == NL80211_IFTYPE_ADHOC) {
		if (is_zero_ether_addr((u8 *) &ap_addr->sa_data))
			sdata->u.ibss.flags |= IEEE80211_IBSS_AUTO_BSSID_SEL |
					       IEEE80211_IBSS_AUTO_CHANNEL_SEL;
		else if (is_broadcast_ether_addr((u8 *) &ap_addr->sa_data))
			sdata->u.ibss.flags |= IEEE80211_IBSS_AUTO_BSSID_SEL;
		else
			sdata->u.ibss.flags &= ~IEEE80211_IBSS_AUTO_BSSID_SEL;

		return ieee80211_ibss_set_bssid(sdata, (u8 *) &ap_addr->sa_data);
	} else if (sdata->vif.type == NL80211_IFTYPE_WDS) {
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
	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		if (sdata->u.mgd.state == IEEE80211_STA_MLME_ASSOCIATED) {
			ap_addr->sa_family = ARPHRD_ETHER;
			memcpy(&ap_addr->sa_data, sdata->u.mgd.bssid, ETH_ALEN);
		} else
			memset(&ap_addr->sa_data, 0, ETH_ALEN);
		return 0;
	} else if (sdata->vif.type == NL80211_IFTYPE_ADHOC) {
		if (sdata->u.ibss.state == IEEE80211_IBSS_MLME_JOINED) {
			ap_addr->sa_family = ARPHRD_ETHER;
			memcpy(&ap_addr->sa_data, sdata->u.ibss.bssid, ETH_ALEN);
		} else
			memset(&ap_addr->sa_data, 0, ETH_ALEN);
		return 0;
	} else if (sdata->vif.type == NL80211_IFTYPE_WDS) {
		ap_addr->sa_family = ARPHRD_ETHER;
		memcpy(&ap_addr->sa_data, sdata->u.wds.remote_addr, ETH_ALEN);
		return 0;
	}

	return -EOPNOTSUPP;
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

	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return -EOPNOTSUPP;

	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

	rcu_read_lock();

	sta = sta_info_get(local, sdata->u.mgd.bssid);

	if (sta && !(sta->last_tx_rate.flags & IEEE80211_TX_RC_MCS))
		rate->value = sband->bitrates[sta->last_tx_rate.idx].bitrate;
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
	struct ieee80211_channel* chan = local->hw.conf.channel;
	bool reconf = false;
	u32 reconf_flags = 0;
	int new_power_level;

	if ((data->txpower.flags & IW_TXPOW_TYPE) != IW_TXPOW_DBM)
		return -EINVAL;
	if (data->txpower.flags & IW_TXPOW_RANGE)
		return -EINVAL;
	if (!chan)
		return -EINVAL;

	/* only change when not disabling */
	if (!data->txpower.disabled) {
		if (data->txpower.fixed) {
			if (data->txpower.value < 0)
				return -EINVAL;
			new_power_level = data->txpower.value;
			/*
			 * Debatable, but we cannot do a fixed power
			 * level above the regulatory constraint.
			 * Use "iwconfig wlan0 txpower 15dBm" instead.
			 */
			if (new_power_level > chan->max_power)
				return -EINVAL;
		} else {
			/*
			 * Automatic power level setting, max being the value
			 * passed in from userland.
			 */
			if (data->txpower.value < 0)
				new_power_level = -1;
			else
				new_power_level = data->txpower.value;
		}

		reconf = true;

		/*
		 * ieee80211_hw_config() will limit to the channel's
		 * max power and possibly power constraint from AP.
		 */
		local->user_power_level = new_power_level;
	}

	if (local->hw.conf.radio_enabled != !(data->txpower.disabled)) {
		local->hw.conf.radio_enabled = !(data->txpower.disabled);
		reconf_flags |= IEEE80211_CONF_CHANGE_RADIO_ENABLED;
		ieee80211_led_radio(local, local->hw.conf.radio_enabled);
	}

	if (reconf || reconf_flags)
		ieee80211_hw_config(local, reconf_flags);

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

	if (retry->flags & IW_RETRY_MAX) {
		local->hw.conf.long_frame_max_tx_count = retry->value;
	} else if (retry->flags & IW_RETRY_MIN) {
		local->hw.conf.short_frame_max_tx_count = retry->value;
	} else {
		local->hw.conf.long_frame_max_tx_count = retry->value;
		local->hw.conf.short_frame_max_tx_count = retry->value;
	}

	ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_RETRY_LIMITS);

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
		retry->value = local->hw.conf.short_frame_max_tx_count;
		if (local->hw.conf.long_frame_max_tx_count !=
		    local->hw.conf.short_frame_max_tx_count)
			retry->flags |= IW_RETRY_MIN;
		return 0;
	}
	if (retry->flags & IW_RETRY_MAX) {
		retry->flags = IW_RETRY_LIMIT | IW_RETRY_MAX;
		retry->value = local->hw.conf.long_frame_max_tx_count;
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
	if (!(sdata->vif.type == NL80211_IFTYPE_STATION))
		return -EINVAL;

	switch (mlme->cmd) {
	case IW_MLME_DEAUTH:
		/* TODO: mlme->addr.sa_data */
		return ieee80211_sta_deauthenticate(sdata, mlme->reason_code);
	case IW_MLME_DISASSOC:
		/* TODO: mlme->addr.sa_data */
		return ieee80211_sta_disassociate(sdata, mlme->reason_code);
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
	int remove = 0, ret;

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

	ret = ieee80211_set_encryption(
		sdata, bcaddr,
		idx, alg, remove,
		!sdata->default_key,
		keybuf, erq->length);

	if (!ret) {
		if (remove)
			sdata->u.mgd.flags &= ~IEEE80211_STA_TKIP_WEP_USED;
		else
			sdata->u.mgd.flags |= IEEE80211_STA_TKIP_WEP_USED;
	}

	return ret;
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

	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		switch (sdata->u.mgd.auth_alg) {
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
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_conf *conf = &local->hw.conf;
	int ret = 0, timeout = 0;
	bool ps;

	if (!(local->hw.flags & IEEE80211_HW_SUPPORTS_PS))
		return -EOPNOTSUPP;

	if (sdata->vif.type != NL80211_IFTYPE_STATION)
		return -EINVAL;

	if (wrq->disabled) {
		ps = false;
		timeout = 0;
		goto set;
	}

	switch (wrq->flags & IW_POWER_MODE) {
	case IW_POWER_ON:       /* If not specified */
	case IW_POWER_MODE:     /* If set all mask */
	case IW_POWER_ALL_R:    /* If explicitely state all */
		ps = true;
		break;
	default:                /* Otherwise we ignore */
		return -EINVAL;
	}

	if (wrq->flags & ~(IW_POWER_MODE | IW_POWER_TIMEOUT))
		return -EINVAL;

	if (wrq->flags & IW_POWER_TIMEOUT)
		timeout = wrq->value / 1000;

 set:
	if (ps == local->powersave && timeout == conf->dynamic_ps_timeout)
		return ret;

	local->powersave = ps;
	conf->dynamic_ps_timeout = timeout;

	if (local->hw.flags & IEEE80211_HW_SUPPORTS_DYNAMIC_PS)
		ret = ieee80211_hw_config(local,
					  IEEE80211_CONF_CHANGE_DYNPS_TIMEOUT);

	if (!(sdata->u.mgd.flags & IEEE80211_STA_ASSOCIATED))
		return ret;

	if (conf->dynamic_ps_timeout > 0 &&
	    !(local->hw.flags & IEEE80211_HW_SUPPORTS_DYNAMIC_PS)) {
		mod_timer(&local->dynamic_ps_timer, jiffies +
			  msecs_to_jiffies(conf->dynamic_ps_timeout));
	} else {
		if (local->powersave) {
			if (local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK)
				ieee80211_send_nullfunc(local, sdata, 1);
			conf->flags |= IEEE80211_CONF_PS;
			ret = ieee80211_hw_config(local,
					IEEE80211_CONF_CHANGE_PS);
		} else {
			conf->flags &= ~IEEE80211_CONF_PS;
			ret = ieee80211_hw_config(local,
					IEEE80211_CONF_CHANGE_PS);
			if (local->hw.flags & IEEE80211_HW_PS_NULLFUNC_STACK)
				ieee80211_send_nullfunc(local, sdata, 0);
			del_timer_sync(&local->dynamic_ps_timer);
			cancel_work_sync(&local->dynamic_ps_enable_work);
		}
	}

	return ret;
}

static int ieee80211_ioctl_giwpower(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu,
				    char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);

	wrqu->power.disabled = !local->powersave;

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
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_WPA_ENABLED:
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
	case IW_AUTH_KEY_MGMT:
	case IW_AUTH_CIPHER_GROUP_MGMT:
		break;
	case IW_AUTH_CIPHER_PAIRWISE:
		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			if (data->value & (IW_AUTH_CIPHER_WEP40 |
			    IW_AUTH_CIPHER_WEP104 | IW_AUTH_CIPHER_TKIP))
				sdata->u.mgd.flags |=
					IEEE80211_STA_TKIP_WEP_USED;
			else
				sdata->u.mgd.flags &=
					~IEEE80211_STA_TKIP_WEP_USED;
		}
		break;
	case IW_AUTH_DROP_UNENCRYPTED:
		sdata->drop_unencrypted = !!data->value;
		break;
	case IW_AUTH_PRIVACY_INVOKED:
		if (sdata->vif.type != NL80211_IFTYPE_STATION)
			ret = -EINVAL;
		else {
			sdata->u.mgd.flags &= ~IEEE80211_STA_PRIVACY_INVOKED;
			/*
			 * Privacy invoked by wpa_supplicant, store the
			 * value and allow associating to a protected
			 * network without having a key up front.
			 */
			if (data->value)
				sdata->u.mgd.flags |=
					IEEE80211_STA_PRIVACY_INVOKED;
		}
		break;
	case IW_AUTH_80211_AUTH_ALG:
		if (sdata->vif.type == NL80211_IFTYPE_STATION)
			sdata->u.mgd.auth_algs = data->value;
		else
			ret = -EOPNOTSUPP;
		break;
	case IW_AUTH_MFP:
		if (!(sdata->local->hw.flags & IEEE80211_HW_MFP_CAPABLE)) {
			ret = -EOPNOTSUPP;
			break;
		}
		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			switch (data->value) {
			case IW_AUTH_MFP_DISABLED:
				sdata->u.mgd.mfp = IEEE80211_MFP_DISABLED;
				break;
			case IW_AUTH_MFP_OPTIONAL:
				sdata->u.mgd.mfp = IEEE80211_MFP_OPTIONAL;
				break;
			case IW_AUTH_MFP_REQUIRED:
				sdata->u.mgd.mfp = IEEE80211_MFP_REQUIRED;
				break;
			default:
				ret = -EINVAL;
			}
		} else
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

	if (sdata->vif.type == NL80211_IFTYPE_STATION)
		sta = sta_info_get(local, sdata->u.mgd.bssid);

	if (!sta) {
		wstats->discard.fragment = 0;
		wstats->discard.misc = 0;
		wstats->qual.qual = 0;
		wstats->qual.level = 0;
		wstats->qual.noise = 0;
		wstats->qual.updated = IW_QUAL_ALL_INVALID;
	} else {
		wstats->qual.updated = 0;
		/*
		 * mirror what cfg80211 does for iwrange/scan results,
		 * otherwise userspace gets confused.
		 */
		if (local->hw.flags & (IEEE80211_HW_SIGNAL_UNSPEC |
				       IEEE80211_HW_SIGNAL_DBM)) {
			wstats->qual.updated |= IW_QUAL_LEVEL_UPDATED;
			wstats->qual.updated |= IW_QUAL_QUAL_UPDATED;
		} else {
			wstats->qual.updated |= IW_QUAL_LEVEL_INVALID;
			wstats->qual.updated |= IW_QUAL_QUAL_INVALID;
		}

		if (local->hw.flags & IEEE80211_HW_SIGNAL_UNSPEC) {
			wstats->qual.level = sta->last_signal;
			wstats->qual.qual = sta->last_signal;
		} else if (local->hw.flags & IEEE80211_HW_SIGNAL_DBM) {
			int sig = sta->last_signal;

			wstats->qual.updated |= IW_QUAL_DBM;
			wstats->qual.level = sig;
			if (sig < -110)
				sig = -110;
			else if (sig > -40)
				sig = -40;
			wstats->qual.qual = sig + 110;
		}

		if (local->hw.flags & IEEE80211_HW_NOISE_DBM) {
			/*
			 * This assumes that if driver reports noise, it also
			 * reports signal in dBm.
			 */
			wstats->qual.noise = sta->last_noise;
			wstats->qual.updated |= IW_QUAL_NOISE_UPDATED;
		} else {
			wstats->qual.updated |= IW_QUAL_NOISE_INVALID;
		}
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
		if (sdata->vif.type == NL80211_IFTYPE_STATION)
			data->value = sdata->u.mgd.auth_algs;
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
	case IW_ENCODE_ALG_AES_CMAC:
		alg = ALG_AES_CMAC;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (erq->flags & IW_ENCODE_DISABLED)
		remove = 1;

	idx = erq->flags & IW_ENCODE_INDEX;
	if (alg == ALG_AES_CMAC) {
		if (idx < NUM_DEFAULT_KEYS + 1 ||
		    idx > NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS) {
			idx = -1;
			if (!sdata->default_mgmt_key)
				idx = 0;
			else for (i = NUM_DEFAULT_KEYS;
				  i < NUM_DEFAULT_KEYS + NUM_DEFAULT_MGMT_KEYS;
				  i++) {
				if (sdata->default_mgmt_key == sdata->keys[i])
				{
					idx = i;
					break;
				}
			}
			if (idx < 0)
				return -EINVAL;
		} else
			idx--;
	} else {
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
	}

	return ieee80211_set_encryption(sdata, ext->addr.sa_data, idx, alg,
					remove,
					ext->ext_flags &
					IW_ENCODE_EXT_SET_TX_KEY,
					ext->key, ext->key_len);
}


/* Structures to export the Wireless Handlers */

static const iw_handler ieee80211_handler[] =
{
	(iw_handler) NULL,				/* SIOCSIWCOMMIT */
	(iw_handler) cfg80211_wext_giwname,		/* SIOCGIWNAME */
	(iw_handler) NULL,				/* SIOCSIWNWID */
	(iw_handler) NULL,				/* SIOCGIWNWID */
	(iw_handler) ieee80211_ioctl_siwfreq,		/* SIOCSIWFREQ */
	(iw_handler) ieee80211_ioctl_giwfreq,		/* SIOCGIWFREQ */
	(iw_handler) cfg80211_wext_siwmode,		/* SIOCSIWMODE */
	(iw_handler) cfg80211_wext_giwmode,		/* SIOCGIWMODE */
	(iw_handler) NULL,				/* SIOCSIWSENS */
	(iw_handler) NULL,				/* SIOCGIWSENS */
	(iw_handler) NULL /* not used */,		/* SIOCSIWRANGE */
	(iw_handler) cfg80211_wext_giwrange,		/* SIOCGIWRANGE */
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
	(iw_handler) cfg80211_wext_siwscan,		/* SIOCSIWSCAN */
	(iw_handler) cfg80211_wext_giwscan,		/* SIOCGIWSCAN */
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
