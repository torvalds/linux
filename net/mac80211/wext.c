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


static int ieee80211_ioctl_siwgenie(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_point *data, char *extra)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		int ret = ieee80211_sta_set_extra_ie(sdata, extra, data->length);
		if (ret && ret != -EALREADY)
			return ret;
		sdata->u.mgd.flags &= ~IEEE80211_STA_AUTO_BSSID_SEL;
		sdata->u.mgd.flags &= ~IEEE80211_STA_EXT_SME;
		sdata->u.mgd.flags &= ~IEEE80211_STA_CONTROL_PORT;
		if (ret != -EALREADY)
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
		return cfg80211_ibss_wext_siwfreq(dev, info, freq, extra);
	else if (sdata->vif.type == NL80211_IFTYPE_STATION)
		sdata->u.mgd.flags &= ~IEEE80211_STA_AUTO_CHANNEL_SEL;

	/* freq->e == 0: freq->m = channel; otherwise freq = m * 10^e */
	if (freq->e == 0) {
		if (freq->m < 0) {
			if (sdata->vif.type == NL80211_IFTYPE_STATION)
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
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		return cfg80211_ibss_wext_giwfreq(dev, info, freq, extra);

	freq->m = local->oper_channel->center_freq;
	freq->e = 6;

	return 0;
}


static int ieee80211_ioctl_siwessid(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_point *data, char *ssid)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	size_t len = data->length;
	int ret;

	if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		return cfg80211_ibss_wext_siwessid(dev, info, data, ssid);

	/* iwconfig uses nul termination in SSID.. */
	if (len > 0 && ssid[len - 1] == '\0')
		len--;

	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		if (data->flags)
			sdata->u.mgd.flags &= ~IEEE80211_STA_AUTO_SSID_SEL;
		else
			sdata->u.mgd.flags |= IEEE80211_STA_AUTO_SSID_SEL;

		ret = ieee80211_sta_set_ssid(sdata, ssid, len);
		if (ret)
			return ret;

		sdata->u.mgd.flags &= ~IEEE80211_STA_EXT_SME;
		sdata->u.mgd.flags &= ~IEEE80211_STA_CONTROL_PORT;
		ieee80211_sta_req_auth(sdata);
		return 0;
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

	if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		return cfg80211_ibss_wext_giwessid(dev, info, data, ssid);

	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		int res = ieee80211_sta_get_ssid(sdata, ssid, &len);
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
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		return cfg80211_ibss_wext_siwap(dev, info, ap_addr, extra);

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
		sdata->u.mgd.flags &= ~IEEE80211_STA_CONTROL_PORT;
		ieee80211_sta_req_auth(sdata);
		return 0;
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
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		return cfg80211_ibss_wext_giwap(dev, info, ap_addr, extra);

	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		if (sdata->u.mgd.state == IEEE80211_STA_MLME_ASSOCIATED) {
			ap_addr->sa_family = ARPHRD_ETHER;
			memcpy(&ap_addr->sa_data, sdata->u.mgd.bssid, ETH_ALEN);
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

static int ieee80211_ioctl_siwpower(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_param *wrq,
				    char *extra)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_conf *conf = &local->hw.conf;
	int timeout = 0;
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
	if (ps == sdata->u.mgd.powersave && timeout == conf->dynamic_ps_timeout)
		return 0;

	sdata->u.mgd.powersave = ps;
	conf->dynamic_ps_timeout = timeout;

	if (local->hw.flags & IEEE80211_HW_SUPPORTS_DYNAMIC_PS)
		ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_PS);

	ieee80211_recalc_ps(local, -1);

	return 0;
}

static int ieee80211_ioctl_giwpower(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu,
				    char *extra)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	wrqu->power.disabled = !sdata->u.mgd.powersave;

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
	(iw_handler) cfg80211_wext_siwmlme,		/* SIOCSIWMLME */
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
	(iw_handler) cfg80211_wext_siwrts,		/* SIOCSIWRTS */
	(iw_handler) cfg80211_wext_giwrts,		/* SIOCGIWRTS */
	(iw_handler) cfg80211_wext_siwfrag,		/* SIOCSIWFRAG */
	(iw_handler) cfg80211_wext_giwfrag,		/* SIOCGIWFRAG */
	(iw_handler) cfg80211_wext_siwtxpower,		/* SIOCSIWTXPOW */
	(iw_handler) cfg80211_wext_giwtxpower,		/* SIOCGIWTXPOW */
	(iw_handler) cfg80211_wext_siwretry,		/* SIOCSIWRETRY */
	(iw_handler) cfg80211_wext_giwretry,		/* SIOCGIWRETRY */
	(iw_handler) cfg80211_wext_siwencode,		/* SIOCSIWENCODE */
	(iw_handler) cfg80211_wext_giwencode,		/* SIOCGIWENCODE */
	(iw_handler) ieee80211_ioctl_siwpower,		/* SIOCSIWPOWER */
	(iw_handler) ieee80211_ioctl_giwpower,		/* SIOCGIWPOWER */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) ieee80211_ioctl_siwgenie,		/* SIOCSIWGENIE */
	(iw_handler) NULL,				/* SIOCGIWGENIE */
	(iw_handler) ieee80211_ioctl_siwauth,		/* SIOCSIWAUTH */
	(iw_handler) ieee80211_ioctl_giwauth,		/* SIOCGIWAUTH */
	(iw_handler) cfg80211_wext_siwencodeext,	/* SIOCSIWENCODEEXT */
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
