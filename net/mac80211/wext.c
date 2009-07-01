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


static int ieee80211_ioctl_siwfreq(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_freq *freq, char *extra)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel *chan;

	if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		return cfg80211_ibss_wext_siwfreq(dev, info, freq, extra);
	else if (sdata->vif.type == NL80211_IFTYPE_STATION)
		return cfg80211_mgd_wext_siwfreq(dev, info, freq, extra);

	/* freq->e == 0: freq->m = channel; otherwise freq = m * 10^e */
	if (freq->e == 0) {
		if (freq->m < 0)
			return -EINVAL;
		else
			chan = ieee80211_get_channel(local->hw.wiphy,
				ieee80211_channel_to_frequency(freq->m));
	} else {
		int i, div = 1000000;
		for (i = 0; i < freq->e; i++)
			div /= 10;
		if (div <= 0)
			return -EINVAL;
		chan = ieee80211_get_channel(local->hw.wiphy, freq->m / div);
	}

	if (!chan)
		return -EINVAL;

	if (chan->flags & IEEE80211_CHAN_DISABLED)
		return -EINVAL;

	/*
	 * no change except maybe auto -> fixed, ignore the HT
	 * setting so you can fix a channel you're on already
	 */
	if (local->oper_channel == chan)
		return 0;

	local->oper_channel = chan;
	local->oper_channel_type = NL80211_CHAN_NO_HT;
	ieee80211_hw_config(local, 0);

	return 0;
}


static int ieee80211_ioctl_giwfreq(struct net_device *dev,
				   struct iw_request_info *info,
				   struct iw_freq *freq, char *extra)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		return cfg80211_ibss_wext_giwfreq(dev, info, freq, extra);
	else if (sdata->vif.type == NL80211_IFTYPE_STATION)
		return cfg80211_mgd_wext_giwfreq(dev, info, freq, extra);

	freq->m = local->oper_channel->center_freq;
	freq->e = 6;

	return 0;
}


static int ieee80211_ioctl_siwessid(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_point *data, char *ssid)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		return cfg80211_ibss_wext_siwessid(dev, info, data, ssid);
	else if (sdata->vif.type == NL80211_IFTYPE_STATION)
		return cfg80211_mgd_wext_siwessid(dev, info, data, ssid);

	return -EOPNOTSUPP;
}


static int ieee80211_ioctl_giwessid(struct net_device *dev,
				    struct iw_request_info *info,
				    struct iw_point *data, char *ssid)
{
	struct ieee80211_sub_if_data *sdata;

	sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		return cfg80211_ibss_wext_giwessid(dev, info, data, ssid);
	else if (sdata->vif.type == NL80211_IFTYPE_STATION)
		return cfg80211_mgd_wext_giwessid(dev, info, data, ssid);

	return -EOPNOTSUPP;
}


static int ieee80211_ioctl_siwap(struct net_device *dev,
				 struct iw_request_info *info,
				 struct sockaddr *ap_addr, char *extra)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);

	if (sdata->vif.type == NL80211_IFTYPE_ADHOC)
		return cfg80211_ibss_wext_siwap(dev, info, ap_addr, extra);

	if (sdata->vif.type == NL80211_IFTYPE_STATION)
		return cfg80211_mgd_wext_siwap(dev, info, ap_addr, extra);

	if (sdata->vif.type == NL80211_IFTYPE_WDS) {
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

	if (sdata->vif.type == NL80211_IFTYPE_STATION)
		return cfg80211_mgd_wext_giwap(dev, info, ap_addr, extra);

	if (sdata->vif.type == NL80211_IFTYPE_WDS) {
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
	(iw_handler) cfg80211_wext_siwpower,		/* SIOCSIWPOWER */
	(iw_handler) cfg80211_wext_giwpower,		/* SIOCGIWPOWER */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) NULL,				/* -- hole -- */
	(iw_handler) cfg80211_wext_siwgenie,		/* SIOCSIWGENIE */
	(iw_handler) NULL,				/* SIOCGIWGENIE */
	(iw_handler) cfg80211_wext_siwauth,		/* SIOCSIWAUTH */
	(iw_handler) cfg80211_wext_giwauth,		/* SIOCGIWAUTH */
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
