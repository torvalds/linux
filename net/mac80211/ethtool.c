/*
 * mac80211 ethtool hooks for cfg80211
 *
 * Copied from cfg.c - originally
 * Copyright 2006-2010	Johannes Berg <johannes@sipsolutions.net>
 * Copyright 2014	Intel Corporation (Author: Johannes Berg)
 *
 * This file is GPLv2 as found in COPYING.
 */
#include <linux/types.h>
#include <net/cfg80211.h>
#include "ieee80211_i.h"
#include "sta_info.h"
#include "driver-ops.h"

static int ieee80211_set_ringparam(struct net_device *dev,
				   struct ethtool_ringparam *rp)
{
	struct ieee80211_local *local = wiphy_priv(dev->ieee80211_ptr->wiphy);

	if (rp->rx_mini_pending != 0 || rp->rx_jumbo_pending != 0)
		return -EINVAL;

	return drv_set_ringparam(local, rp->tx_pending, rp->rx_pending);
}

static void ieee80211_get_ringparam(struct net_device *dev,
				    struct ethtool_ringparam *rp)
{
	struct ieee80211_local *local = wiphy_priv(dev->ieee80211_ptr->wiphy);

	memset(rp, 0, sizeof(*rp));

	drv_get_ringparam(local, &rp->tx_pending, &rp->tx_max_pending,
			  &rp->rx_pending, &rp->rx_max_pending);
}

static const char ieee80211_gstrings_sta_stats[][ETH_GSTRING_LEN] = {
	"rx_packets", "rx_bytes",
	"rx_duplicates", "rx_fragments", "rx_dropped",
	"tx_packets", "tx_bytes",
	"tx_filtered", "tx_retry_failed", "tx_retries",
	"sta_state", "txrate", "rxrate", "signal",
	"channel", "noise", "ch_time", "ch_time_busy",
	"ch_time_ext_busy", "ch_time_rx", "ch_time_tx"
};
#define STA_STATS_LEN	ARRAY_SIZE(ieee80211_gstrings_sta_stats)

static int ieee80211_get_sset_count(struct net_device *dev, int sset)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int rv = 0;

	if (sset == ETH_SS_STATS)
		rv += STA_STATS_LEN;

	rv += drv_get_et_sset_count(sdata, sset);

	if (rv == 0)
		return -EOPNOTSUPP;
	return rv;
}

static void ieee80211_get_stats(struct net_device *dev,
				struct ethtool_stats *stats,
				u64 *data)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_channel *channel;
	struct sta_info *sta;
	struct ieee80211_local *local = sdata->local;
	struct station_info sinfo;
	struct survey_info survey;
	int i, q;
#define STA_STATS_SURVEY_LEN 7

	memset(data, 0, sizeof(u64) * STA_STATS_LEN);

#define ADD_STA_STATS(sta)					\
	do {							\
		data[i++] += sta->rx_stats.packets;		\
		data[i++] += sta->rx_stats.bytes;		\
		data[i++] += sta->rx_stats.num_duplicates;	\
		data[i++] += sta->rx_stats.fragments;		\
		data[i++] += sta->rx_stats.dropped;		\
								\
		data[i++] += sinfo.tx_packets;			\
		data[i++] += sinfo.tx_bytes;			\
		data[i++] += sta->status_stats.filtered;	\
		data[i++] += sta->status_stats.retry_failed;	\
		data[i++] += sta->status_stats.retry_count;	\
	} while (0)

	/* For Managed stations, find the single station based on BSSID
	 * and use that.  For interface types, iterate through all available
	 * stations and add stats for any station that is assigned to this
	 * network device.
	 */

	mutex_lock(&local->sta_mtx);

	if (sdata->vif.type == NL80211_IFTYPE_STATION) {
		sta = sta_info_get_bss(sdata, sdata->u.mgd.bssid);

		if (!(sta && !WARN_ON(sta->sdata->dev != dev)))
			goto do_survey;

		sinfo.filled = 0;
		sta_set_sinfo(sta, &sinfo);

		i = 0;
		ADD_STA_STATS(sta);

		data[i++] = sta->sta_state;


		if (sinfo.filled & BIT(NL80211_STA_INFO_TX_BITRATE))
			data[i] = 100000 *
				cfg80211_calculate_bitrate(&sinfo.txrate);
		i++;
		if (sinfo.filled & BIT(NL80211_STA_INFO_RX_BITRATE))
			data[i] = 100000 *
				cfg80211_calculate_bitrate(&sinfo.rxrate);
		i++;

		if (sinfo.filled & BIT(NL80211_STA_INFO_SIGNAL_AVG))
			data[i] = (u8)sinfo.signal_avg;
		i++;
	} else {
		list_for_each_entry(sta, &local->sta_list, list) {
			/* Make sure this station belongs to the proper dev */
			if (sta->sdata->dev != dev)
				continue;

			sinfo.filled = 0;
			sta_set_sinfo(sta, &sinfo);
			i = 0;
			ADD_STA_STATS(sta);
		}
	}

do_survey:
	i = STA_STATS_LEN - STA_STATS_SURVEY_LEN;
	/* Get survey stats for current channel */
	survey.filled = 0;

	rcu_read_lock();
	chanctx_conf = rcu_dereference(sdata->vif.chanctx_conf);
	if (chanctx_conf)
		channel = chanctx_conf->def.chan;
	else
		channel = NULL;
	rcu_read_unlock();

	if (channel) {
		q = 0;
		do {
			survey.filled = 0;
			if (drv_get_survey(local, q, &survey) != 0) {
				survey.filled = 0;
				break;
			}
			q++;
		} while (channel != survey.channel);
	}

	if (survey.filled)
		data[i++] = survey.channel->center_freq;
	else
		data[i++] = 0;
	if (survey.filled & SURVEY_INFO_NOISE_DBM)
		data[i++] = (u8)survey.noise;
	else
		data[i++] = -1LL;
	if (survey.filled & SURVEY_INFO_TIME)
		data[i++] = survey.time;
	else
		data[i++] = -1LL;
	if (survey.filled & SURVEY_INFO_TIME_BUSY)
		data[i++] = survey.time_busy;
	else
		data[i++] = -1LL;
	if (survey.filled & SURVEY_INFO_TIME_EXT_BUSY)
		data[i++] = survey.time_ext_busy;
	else
		data[i++] = -1LL;
	if (survey.filled & SURVEY_INFO_TIME_RX)
		data[i++] = survey.time_rx;
	else
		data[i++] = -1LL;
	if (survey.filled & SURVEY_INFO_TIME_TX)
		data[i++] = survey.time_tx;
	else
		data[i++] = -1LL;

	mutex_unlock(&local->sta_mtx);

	if (WARN_ON(i != STA_STATS_LEN))
		return;

	drv_get_et_stats(sdata, stats, &(data[STA_STATS_LEN]));
}

static void ieee80211_get_strings(struct net_device *dev, u32 sset, u8 *data)
{
	struct ieee80211_sub_if_data *sdata = IEEE80211_DEV_TO_SUB_IF(dev);
	int sz_sta_stats = 0;

	if (sset == ETH_SS_STATS) {
		sz_sta_stats = sizeof(ieee80211_gstrings_sta_stats);
		memcpy(data, ieee80211_gstrings_sta_stats, sz_sta_stats);
	}
	drv_get_et_strings(sdata, sset, &(data[sz_sta_stats]));
}

static int ieee80211_get_regs_len(struct net_device *dev)
{
	return 0;
}

static void ieee80211_get_regs(struct net_device *dev,
			       struct ethtool_regs *regs,
			       void *data)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;

	regs->version = wdev->wiphy->hw_version;
	regs->len = 0;
}

const struct ethtool_ops ieee80211_ethtool_ops = {
	.get_drvinfo = cfg80211_get_drvinfo,
	.get_regs_len = ieee80211_get_regs_len,
	.get_regs = ieee80211_get_regs,
	.get_link = ethtool_op_get_link,
	.get_ringparam = ieee80211_get_ringparam,
	.set_ringparam = ieee80211_set_ringparam,
	.get_strings = ieee80211_get_strings,
	.get_ethtool_stats = ieee80211_get_stats,
	.get_sset_count = ieee80211_get_sset_count,
};
