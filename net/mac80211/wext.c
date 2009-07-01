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

	if (sdata->vif.type == NL80211_IFTYPE_WDS)
		return cfg80211_wds_wext_siwap(dev, info, ap_addr, extra);
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

	if (sdata->vif.type == NL80211_IFTYPE_WDS)
		return cfg80211_wds_wext_giwap(dev, info, ap_addr, extra);

	return -EOPNOTSUPP;
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
	(iw_handler) cfg80211_wext_siwrate,		/* SIOCSIWRATE */
	(iw_handler) cfg80211_wext_giwrate,		/* SIOCGIWRATE */
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
	.get_wireless_stats = cfg80211_wireless_stats,
};
