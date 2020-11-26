/* SPDX-License-Identifier: GPL-2.0 */
/*
* Portions of this file
* Copyright(c) 2016-2017 Intel Deutschland GmbH
* Copyright (C) 2018 - 2019 Intel Corporation
*/

#if !defined(__MAC80211_DRIVER_TRACE) || defined(TRACE_HEADER_MULTI_READ)
#define __MAC80211_DRIVER_TRACE

#include <linux/tracepoint.h>
#include <net/mac80211.h>
#include "ieee80211_i.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mac80211

#define MAXNAME		32
#define LOCAL_ENTRY	__array(char, wiphy_name, 32)
#define LOCAL_ASSIGN	strlcpy(__entry->wiphy_name, wiphy_name(local->hw.wiphy), MAXNAME)
#define LOCAL_PR_FMT	"%s"
#define LOCAL_PR_ARG	__entry->wiphy_name

#define STA_ENTRY	__array(char, sta_addr, ETH_ALEN)
#define STA_ASSIGN	(sta ? memcpy(__entry->sta_addr, sta->addr, ETH_ALEN) : \
				eth_zero_addr(__entry->sta_addr))
#define STA_NAMED_ASSIGN(s)	memcpy(__entry->sta_addr, (s)->addr, ETH_ALEN)
#define STA_PR_FMT	" sta:%pM"
#define STA_PR_ARG	__entry->sta_addr

#define VIF_ENTRY	__field(enum nl80211_iftype, vif_type) __field(void *, sdata)	\
			__field(bool, p2p)						\
			__string(vif_name, sdata->name)
#define VIF_ASSIGN	__entry->vif_type = sdata->vif.type; __entry->sdata = sdata;	\
			__entry->p2p = sdata->vif.p2p;					\
			__assign_str(vif_name, sdata->name)
#define VIF_PR_FMT	" vif:%s(%d%s)"
#define VIF_PR_ARG	__get_str(vif_name), __entry->vif_type, __entry->p2p ? "/p2p" : ""

#define CHANDEF_ENTRY	__field(u32, control_freq)					\
			__field(u32, freq_offset)					\
			__field(u32, chan_width)					\
			__field(u32, center_freq1)					\
			__field(u32, freq1_offset)					\
			__field(u32, center_freq2)
#define CHANDEF_ASSIGN(c)							\
			__entry->control_freq = (c) ? ((c)->chan ? (c)->chan->center_freq : 0) : 0;	\
			__entry->freq_offset = (c) ? ((c)->chan ? (c)->chan->freq_offset : 0) : 0;	\
			__entry->chan_width = (c) ? (c)->width : 0;			\
			__entry->center_freq1 = (c) ? (c)->center_freq1 : 0;		\
			__entry->freq1_offset = (c) ? (c)->freq1_offset : 0;		\
			__entry->center_freq2 = (c) ? (c)->center_freq2 : 0;
#define CHANDEF_PR_FMT	" control:%d.%03d MHz width:%d center: %d.%03d/%d MHz"
#define CHANDEF_PR_ARG	__entry->control_freq, __entry->freq_offset, __entry->chan_width, \
			__entry->center_freq1, __entry->freq1_offset, __entry->center_freq2

#define MIN_CHANDEF_ENTRY								\
			__field(u32, min_control_freq)					\
			__field(u32, min_freq_offset)					\
			__field(u32, min_chan_width)					\
			__field(u32, min_center_freq1)					\
			__field(u32, min_freq1_offset)					\
			__field(u32, min_center_freq2)

#define MIN_CHANDEF_ASSIGN(c)								\
			__entry->min_control_freq = (c)->chan ? (c)->chan->center_freq : 0;	\
			__entry->min_freq_offset = (c)->chan ? (c)->chan->freq_offset : 0;	\
			__entry->min_chan_width = (c)->width;				\
			__entry->min_center_freq1 = (c)->center_freq1;			\
			__entry->freq1_offset = (c)->freq1_offset;			\
			__entry->min_center_freq2 = (c)->center_freq2;
#define MIN_CHANDEF_PR_FMT	" min_control:%d.%03d MHz min_width:%d min_center: %d.%03d/%d MHz"
#define MIN_CHANDEF_PR_ARG	__entry->min_control_freq, __entry->min_freq_offset,	\
			__entry->min_chan_width,					\
			__entry->min_center_freq1, __entry->min_freq1_offset,		\
			__entry->min_center_freq2

#define CHANCTX_ENTRY	CHANDEF_ENTRY							\
			MIN_CHANDEF_ENTRY						\
			__field(u8, rx_chains_static)					\
			__field(u8, rx_chains_dynamic)
#define CHANCTX_ASSIGN	CHANDEF_ASSIGN(&ctx->conf.def)					\
			MIN_CHANDEF_ASSIGN(&ctx->conf.min_def)				\
			__entry->rx_chains_static = ctx->conf.rx_chains_static;		\
			__entry->rx_chains_dynamic = ctx->conf.rx_chains_dynamic
#define CHANCTX_PR_FMT	CHANDEF_PR_FMT MIN_CHANDEF_PR_FMT " chains:%d/%d"
#define CHANCTX_PR_ARG	CHANDEF_PR_ARG,	MIN_CHANDEF_PR_ARG,				\
			__entry->rx_chains_static, __entry->rx_chains_dynamic

#define KEY_ENTRY	__field(u32, cipher)						\
			__field(u8, hw_key_idx)						\
			__field(u8, flags)						\
			__field(s8, keyidx)
#define KEY_ASSIGN(k)	__entry->cipher = (k)->cipher;					\
			__entry->flags = (k)->flags;					\
			__entry->keyidx = (k)->keyidx;					\
			__entry->hw_key_idx = (k)->hw_key_idx;
#define KEY_PR_FMT	" cipher:0x%x, flags=%#x, keyidx=%d, hw_key_idx=%d"
#define KEY_PR_ARG	__entry->cipher, __entry->flags, __entry->keyidx, __entry->hw_key_idx

#define AMPDU_ACTION_ENTRY	__field(enum ieee80211_ampdu_mlme_action,		\
					ieee80211_ampdu_mlme_action)			\
				STA_ENTRY						\
				__field(u16, tid)					\
				__field(u16, ssn)					\
				__field(u16, buf_size)					\
				__field(bool, amsdu)					\
				__field(u16, timeout)					\
				__field(u16, action)
#define AMPDU_ACTION_ASSIGN	STA_NAMED_ASSIGN(params->sta);				\
				__entry->tid = params->tid;				\
				__entry->ssn = params->ssn;				\
				__entry->buf_size = params->buf_size;			\
				__entry->amsdu = params->amsdu;				\
				__entry->timeout = params->timeout;			\
				__entry->action = params->action;
#define AMPDU_ACTION_PR_FMT	STA_PR_FMT " tid %d, ssn %d, buf_size %u, amsdu %d, timeout %d action %d"
#define AMPDU_ACTION_PR_ARG	STA_PR_ARG, __entry->tid, __entry->ssn,			\
				__entry->buf_size, __entry->amsdu, __entry->timeout,	\
				__entry->action

/*
 * Tracing for driver callbacks.
 */

DECLARE_EVENT_CLASS(local_only_evt,
	TP_PROTO(struct ieee80211_local *local),
	TP_ARGS(local),
	TP_STRUCT__entry(
		LOCAL_ENTRY
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
	),
	TP_printk(LOCAL_PR_FMT, LOCAL_PR_ARG)
);

DECLARE_EVENT_CLASS(local_sdata_addr_evt,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__array(char, addr, ETH_ALEN)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		memcpy(__entry->addr, sdata->vif.addr, ETH_ALEN);
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT " addr:%pM",
		LOCAL_PR_ARG, VIF_PR_ARG, __entry->addr
	)
);

DECLARE_EVENT_CLASS(local_u32_evt,
	TP_PROTO(struct ieee80211_local *local, u32 value),
	TP_ARGS(local, value),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u32, value)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->value = value;
	),

	TP_printk(
		LOCAL_PR_FMT " value:%d",
		LOCAL_PR_ARG, __entry->value
	)
);

DECLARE_EVENT_CLASS(local_sdata_evt,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT,
		LOCAL_PR_ARG, VIF_PR_ARG
	)
);

DEFINE_EVENT(local_only_evt, drv_return_void,
	TP_PROTO(struct ieee80211_local *local),
	TP_ARGS(local)
);

TRACE_EVENT(drv_return_int,
	TP_PROTO(struct ieee80211_local *local, int ret),
	TP_ARGS(local, ret),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(int, ret)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->ret = ret;
	),
	TP_printk(LOCAL_PR_FMT " - %d", LOCAL_PR_ARG, __entry->ret)
);

TRACE_EVENT(drv_return_bool,
	TP_PROTO(struct ieee80211_local *local, bool ret),
	TP_ARGS(local, ret),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(bool, ret)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->ret = ret;
	),
	TP_printk(LOCAL_PR_FMT " - %s", LOCAL_PR_ARG, (__entry->ret) ?
		  "true" : "false")
);

TRACE_EVENT(drv_return_u32,
	TP_PROTO(struct ieee80211_local *local, u32 ret),
	TP_ARGS(local, ret),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u32, ret)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->ret = ret;
	),
	TP_printk(LOCAL_PR_FMT " - %u", LOCAL_PR_ARG, __entry->ret)
);

TRACE_EVENT(drv_return_u64,
	TP_PROTO(struct ieee80211_local *local, u64 ret),
	TP_ARGS(local, ret),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u64, ret)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->ret = ret;
	),
	TP_printk(LOCAL_PR_FMT " - %llu", LOCAL_PR_ARG, __entry->ret)
);

DEFINE_EVENT(local_only_evt, drv_start,
	TP_PROTO(struct ieee80211_local *local),
	TP_ARGS(local)
);

DEFINE_EVENT(local_u32_evt, drv_get_et_strings,
	     TP_PROTO(struct ieee80211_local *local, u32 sset),
	     TP_ARGS(local, sset)
);

DEFINE_EVENT(local_u32_evt, drv_get_et_sset_count,
	     TP_PROTO(struct ieee80211_local *local, u32 sset),
	     TP_ARGS(local, sset)
);

DEFINE_EVENT(local_only_evt, drv_get_et_stats,
	     TP_PROTO(struct ieee80211_local *local),
	     TP_ARGS(local)
);

DEFINE_EVENT(local_only_evt, drv_suspend,
	TP_PROTO(struct ieee80211_local *local),
	TP_ARGS(local)
);

DEFINE_EVENT(local_only_evt, drv_resume,
	TP_PROTO(struct ieee80211_local *local),
	TP_ARGS(local)
);

TRACE_EVENT(drv_set_wakeup,
	TP_PROTO(struct ieee80211_local *local, bool enabled),
	TP_ARGS(local, enabled),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(bool, enabled)
	),
	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->enabled = enabled;
	),
	TP_printk(LOCAL_PR_FMT " enabled:%d", LOCAL_PR_ARG, __entry->enabled)
);

DEFINE_EVENT(local_only_evt, drv_stop,
	TP_PROTO(struct ieee80211_local *local),
	TP_ARGS(local)
);

DEFINE_EVENT(local_sdata_addr_evt, drv_add_interface,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

TRACE_EVENT(drv_change_interface,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 enum nl80211_iftype type, bool p2p),

	TP_ARGS(local, sdata, type, p2p),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(u32, new_type)
		__field(bool, new_p2p)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->new_type = type;
		__entry->new_p2p = p2p;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT " new type:%d%s",
		LOCAL_PR_ARG, VIF_PR_ARG, __entry->new_type,
		__entry->new_p2p ? "/p2p" : ""
	)
);

DEFINE_EVENT(local_sdata_addr_evt, drv_remove_interface,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

TRACE_EVENT(drv_config,
	TP_PROTO(struct ieee80211_local *local,
		 u32 changed),

	TP_ARGS(local, changed),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u32, changed)
		__field(u32, flags)
		__field(int, power_level)
		__field(int, dynamic_ps_timeout)
		__field(u16, listen_interval)
		__field(u8, long_frame_max_tx_count)
		__field(u8, short_frame_max_tx_count)
		CHANDEF_ENTRY
		__field(int, smps)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->changed = changed;
		__entry->flags = local->hw.conf.flags;
		__entry->power_level = local->hw.conf.power_level;
		__entry->dynamic_ps_timeout = local->hw.conf.dynamic_ps_timeout;
		__entry->listen_interval = local->hw.conf.listen_interval;
		__entry->long_frame_max_tx_count =
			local->hw.conf.long_frame_max_tx_count;
		__entry->short_frame_max_tx_count =
			local->hw.conf.short_frame_max_tx_count;
		CHANDEF_ASSIGN(&local->hw.conf.chandef)
		__entry->smps = local->hw.conf.smps_mode;
	),

	TP_printk(
		LOCAL_PR_FMT " ch:%#x" CHANDEF_PR_FMT,
		LOCAL_PR_ARG, __entry->changed, CHANDEF_PR_ARG
	)
);

TRACE_EVENT(drv_bss_info_changed,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_bss_conf *info,
		 u32 changed),

	TP_ARGS(local, sdata, info, changed),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(u32, changed)
		__field(bool, assoc)
		__field(bool, ibss_joined)
		__field(bool, ibss_creator)
		__field(u16, aid)
		__field(bool, cts)
		__field(bool, shortpre)
		__field(bool, shortslot)
		__field(bool, enable_beacon)
		__field(u8, dtimper)
		__field(u16, bcnint)
		__field(u16, assoc_cap)
		__field(u64, sync_tsf)
		__field(u32, sync_device_ts)
		__field(u8, sync_dtim_count)
		__field(u32, basic_rates)
		__array(int, mcast_rate, NUM_NL80211_BANDS)
		__field(u16, ht_operation_mode)
		__field(s32, cqm_rssi_thold)
		__field(s32, cqm_rssi_hyst)
		__field(u32, channel_width)
		__field(u32, channel_cfreq1)
		__field(u32, channel_cfreq1_offset)
		__dynamic_array(u32, arp_addr_list,
				info->arp_addr_cnt > IEEE80211_BSS_ARP_ADDR_LIST_LEN ?
					IEEE80211_BSS_ARP_ADDR_LIST_LEN :
					info->arp_addr_cnt)
		__field(int, arp_addr_cnt)
		__field(bool, qos)
		__field(bool, idle)
		__field(bool, ps)
		__dynamic_array(u8, ssid, info->ssid_len)
		__field(bool, hidden_ssid)
		__field(int, txpower)
		__field(u8, p2p_oppps_ctwindow)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->changed = changed;
		__entry->aid = info->aid;
		__entry->assoc = info->assoc;
		__entry->ibss_joined = info->ibss_joined;
		__entry->ibss_creator = info->ibss_creator;
		__entry->shortpre = info->use_short_preamble;
		__entry->cts = info->use_cts_prot;
		__entry->shortslot = info->use_short_slot;
		__entry->enable_beacon = info->enable_beacon;
		__entry->dtimper = info->dtim_period;
		__entry->bcnint = info->beacon_int;
		__entry->assoc_cap = info->assoc_capability;
		__entry->sync_tsf = info->sync_tsf;
		__entry->sync_device_ts = info->sync_device_ts;
		__entry->sync_dtim_count = info->sync_dtim_count;
		__entry->basic_rates = info->basic_rates;
		memcpy(__entry->mcast_rate, info->mcast_rate,
		       sizeof(__entry->mcast_rate));
		__entry->ht_operation_mode = info->ht_operation_mode;
		__entry->cqm_rssi_thold = info->cqm_rssi_thold;
		__entry->cqm_rssi_hyst = info->cqm_rssi_hyst;
		__entry->channel_width = info->chandef.width;
		__entry->channel_cfreq1 = info->chandef.center_freq1;
		__entry->channel_cfreq1_offset = info->chandef.freq1_offset;
		__entry->arp_addr_cnt = info->arp_addr_cnt;
		memcpy(__get_dynamic_array(arp_addr_list), info->arp_addr_list,
		       sizeof(u32) * (info->arp_addr_cnt > IEEE80211_BSS_ARP_ADDR_LIST_LEN ?
					IEEE80211_BSS_ARP_ADDR_LIST_LEN :
					info->arp_addr_cnt));
		__entry->qos = info->qos;
		__entry->idle = info->idle;
		__entry->ps = info->ps;
		memcpy(__get_dynamic_array(ssid), info->ssid, info->ssid_len);
		__entry->hidden_ssid = info->hidden_ssid;
		__entry->txpower = info->txpower;
		__entry->p2p_oppps_ctwindow = info->p2p_noa_attr.oppps_ctwindow;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT " changed:%#x",
		LOCAL_PR_ARG, VIF_PR_ARG, __entry->changed
	)
);

TRACE_EVENT(drv_prepare_multicast,
	TP_PROTO(struct ieee80211_local *local, int mc_count),

	TP_ARGS(local, mc_count),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(int, mc_count)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->mc_count = mc_count;
	),

	TP_printk(
		LOCAL_PR_FMT " prepare mc (%d)",
		LOCAL_PR_ARG, __entry->mc_count
	)
);

TRACE_EVENT(drv_configure_filter,
	TP_PROTO(struct ieee80211_local *local,
		 unsigned int changed_flags,
		 unsigned int *total_flags,
		 u64 multicast),

	TP_ARGS(local, changed_flags, total_flags, multicast),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(unsigned int, changed)
		__field(unsigned int, total)
		__field(u64, multicast)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->changed = changed_flags;
		__entry->total = *total_flags;
		__entry->multicast = multicast;
	),

	TP_printk(
		LOCAL_PR_FMT " changed:%#x total:%#x",
		LOCAL_PR_ARG, __entry->changed, __entry->total
	)
);

TRACE_EVENT(drv_config_iface_filter,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 unsigned int filter_flags,
		 unsigned int changed_flags),

	TP_ARGS(local, sdata, filter_flags, changed_flags),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(unsigned int, filter_flags)
		__field(unsigned int, changed_flags)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->filter_flags = filter_flags;
		__entry->changed_flags = changed_flags;
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT
		" filter_flags: %#x changed_flags: %#x",
		LOCAL_PR_ARG, VIF_PR_ARG, __entry->filter_flags,
		__entry->changed_flags
	)
);

TRACE_EVENT(drv_set_tim,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sta *sta, bool set),

	TP_ARGS(local, sta, set),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		STA_ENTRY
		__field(bool, set)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		STA_ASSIGN;
		__entry->set = set;
	),

	TP_printk(
		LOCAL_PR_FMT STA_PR_FMT " set:%d",
		LOCAL_PR_ARG, STA_PR_ARG, __entry->set
	)
);

TRACE_EVENT(drv_set_key,
	TP_PROTO(struct ieee80211_local *local,
		 enum set_key_cmd cmd, struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_sta *sta,
		 struct ieee80211_key_conf *key),

	TP_ARGS(local, cmd, sdata, sta, key),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		STA_ENTRY
		KEY_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		STA_ASSIGN;
		KEY_ASSIGN(key);
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT  STA_PR_FMT KEY_PR_FMT,
		LOCAL_PR_ARG, VIF_PR_ARG, STA_PR_ARG, KEY_PR_ARG
	)
);

TRACE_EVENT(drv_update_tkip_key,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_key_conf *conf,
		 struct ieee80211_sta *sta, u32 iv32),

	TP_ARGS(local, sdata, conf, sta, iv32),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		STA_ENTRY
		__field(u32, iv32)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		STA_ASSIGN;
		__entry->iv32 = iv32;
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT STA_PR_FMT " iv32:%#x",
		LOCAL_PR_ARG, VIF_PR_ARG, STA_PR_ARG, __entry->iv32
	)
);

DEFINE_EVENT(local_sdata_evt, drv_hw_scan,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

DEFINE_EVENT(local_sdata_evt, drv_cancel_hw_scan,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

DEFINE_EVENT(local_sdata_evt, drv_sched_scan_start,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

DEFINE_EVENT(local_sdata_evt, drv_sched_scan_stop,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

TRACE_EVENT(drv_sw_scan_start,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 const u8 *mac_addr),

	TP_ARGS(local, sdata, mac_addr),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__array(char, mac_addr, ETH_ALEN)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		memcpy(__entry->mac_addr, mac_addr, ETH_ALEN);
	),

	TP_printk(LOCAL_PR_FMT ", " VIF_PR_FMT ", addr:%pM",
		  LOCAL_PR_ARG, VIF_PR_ARG, __entry->mac_addr)
);

DEFINE_EVENT(local_sdata_evt, drv_sw_scan_complete,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

TRACE_EVENT(drv_get_stats,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_low_level_stats *stats,
		 int ret),

	TP_ARGS(local, stats, ret),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(int, ret)
		__field(unsigned int, ackfail)
		__field(unsigned int, rtsfail)
		__field(unsigned int, fcserr)
		__field(unsigned int, rtssucc)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->ret = ret;
		__entry->ackfail = stats->dot11ACKFailureCount;
		__entry->rtsfail = stats->dot11RTSFailureCount;
		__entry->fcserr = stats->dot11FCSErrorCount;
		__entry->rtssucc = stats->dot11RTSSuccessCount;
	),

	TP_printk(
		LOCAL_PR_FMT " ret:%d",
		LOCAL_PR_ARG, __entry->ret
	)
);

TRACE_EVENT(drv_get_key_seq,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_key_conf *key),

	TP_ARGS(local, key),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		KEY_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		KEY_ASSIGN(key);
	),

	TP_printk(
		LOCAL_PR_FMT KEY_PR_FMT,
		LOCAL_PR_ARG, KEY_PR_ARG
	)
);

DEFINE_EVENT(local_u32_evt, drv_set_frag_threshold,
	TP_PROTO(struct ieee80211_local *local, u32 value),
	TP_ARGS(local, value)
);

DEFINE_EVENT(local_u32_evt, drv_set_rts_threshold,
	TP_PROTO(struct ieee80211_local *local, u32 value),
	TP_ARGS(local, value)
);

TRACE_EVENT(drv_set_coverage_class,
	TP_PROTO(struct ieee80211_local *local, s16 value),

	TP_ARGS(local, value),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(s16, value)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->value = value;
	),

	TP_printk(
		LOCAL_PR_FMT " value:%d",
		LOCAL_PR_ARG, __entry->value
	)
);

TRACE_EVENT(drv_sta_notify,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 enum sta_notify_cmd cmd,
		 struct ieee80211_sta *sta),

	TP_ARGS(local, sdata, cmd, sta),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		STA_ENTRY
		__field(u32, cmd)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		STA_ASSIGN;
		__entry->cmd = cmd;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT  STA_PR_FMT " cmd:%d",
		LOCAL_PR_ARG, VIF_PR_ARG, STA_PR_ARG, __entry->cmd
	)
);

TRACE_EVENT(drv_sta_state,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_sta *sta,
		 enum ieee80211_sta_state old_state,
		 enum ieee80211_sta_state new_state),

	TP_ARGS(local, sdata, sta, old_state, new_state),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		STA_ENTRY
		__field(u32, old_state)
		__field(u32, new_state)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		STA_ASSIGN;
		__entry->old_state = old_state;
		__entry->new_state = new_state;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT  STA_PR_FMT " state: %d->%d",
		LOCAL_PR_ARG, VIF_PR_ARG, STA_PR_ARG,
		__entry->old_state, __entry->new_state
	)
);

TRACE_EVENT(drv_sta_set_txpwr,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_sta *sta),

	TP_ARGS(local, sdata, sta),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		STA_ENTRY
		__field(s16, txpwr)
		__field(u8, type)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		STA_ASSIGN;
		__entry->txpwr = sta->txpwr.power;
		__entry->type = sta->txpwr.type;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT  STA_PR_FMT " txpwr: %d type %d",
		LOCAL_PR_ARG, VIF_PR_ARG, STA_PR_ARG,
		__entry->txpwr,  __entry->type
	)
);

TRACE_EVENT(drv_sta_rc_update,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_sta *sta,
		 u32 changed),

	TP_ARGS(local, sdata, sta, changed),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		STA_ENTRY
		__field(u32, changed)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		STA_ASSIGN;
		__entry->changed = changed;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT  STA_PR_FMT " changed: 0x%x",
		LOCAL_PR_ARG, VIF_PR_ARG, STA_PR_ARG, __entry->changed
	)
);

DECLARE_EVENT_CLASS(sta_event,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_sta *sta),

	TP_ARGS(local, sdata, sta),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		STA_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		STA_ASSIGN;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT  STA_PR_FMT,
		LOCAL_PR_ARG, VIF_PR_ARG, STA_PR_ARG
	)
);

DEFINE_EVENT(sta_event, drv_sta_statistics,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_sta *sta),
	TP_ARGS(local, sdata, sta)
);

DEFINE_EVENT(sta_event, drv_sta_add,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_sta *sta),
	TP_ARGS(local, sdata, sta)
);

DEFINE_EVENT(sta_event, drv_sta_remove,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_sta *sta),
	TP_ARGS(local, sdata, sta)
);

DEFINE_EVENT(sta_event, drv_sta_pre_rcu_remove,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_sta *sta),
	TP_ARGS(local, sdata, sta)
);

DEFINE_EVENT(sta_event, drv_sync_rx_queues,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_sta *sta),
	TP_ARGS(local, sdata, sta)
);

DEFINE_EVENT(sta_event, drv_sta_rate_tbl_update,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_sta *sta),
	TP_ARGS(local, sdata, sta)
);

TRACE_EVENT(drv_conf_tx,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 u16 ac, const struct ieee80211_tx_queue_params *params),

	TP_ARGS(local, sdata, ac, params),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(u16, ac)
		__field(u16, txop)
		__field(u16, cw_min)
		__field(u16, cw_max)
		__field(u8, aifs)
		__field(bool, uapsd)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->ac = ac;
		__entry->txop = params->txop;
		__entry->cw_max = params->cw_max;
		__entry->cw_min = params->cw_min;
		__entry->aifs = params->aifs;
		__entry->uapsd = params->uapsd;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT  " AC:%d",
		LOCAL_PR_ARG, VIF_PR_ARG, __entry->ac
	)
);

DEFINE_EVENT(local_sdata_evt, drv_get_tsf,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

TRACE_EVENT(drv_set_tsf,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 u64 tsf),

	TP_ARGS(local, sdata, tsf),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(u64, tsf)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->tsf = tsf;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT  " tsf:%llu",
		LOCAL_PR_ARG, VIF_PR_ARG, (unsigned long long)__entry->tsf
	)
);

TRACE_EVENT(drv_offset_tsf,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 s64 offset),

	TP_ARGS(local, sdata, offset),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(s64, tsf_offset)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->tsf_offset = offset;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT  " tsf offset:%lld",
		LOCAL_PR_ARG, VIF_PR_ARG,
		(unsigned long long)__entry->tsf_offset
	)
);

DEFINE_EVENT(local_sdata_evt, drv_reset_tsf,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

DEFINE_EVENT(local_only_evt, drv_tx_last_beacon,
	TP_PROTO(struct ieee80211_local *local),
	TP_ARGS(local)
);

TRACE_EVENT(drv_ampdu_action,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_ampdu_params *params),

	TP_ARGS(local, sdata, params),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		AMPDU_ACTION_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		AMPDU_ACTION_ASSIGN;
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT AMPDU_ACTION_PR_FMT,
		LOCAL_PR_ARG, VIF_PR_ARG, AMPDU_ACTION_PR_ARG
	)
);

TRACE_EVENT(drv_get_survey,
	TP_PROTO(struct ieee80211_local *local, int _idx,
		 struct survey_info *survey),

	TP_ARGS(local, _idx, survey),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(int, idx)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->idx = _idx;
	),

	TP_printk(
		LOCAL_PR_FMT " idx:%d",
		LOCAL_PR_ARG, __entry->idx
	)
);

TRACE_EVENT(drv_flush,
	TP_PROTO(struct ieee80211_local *local,
		 u32 queues, bool drop),

	TP_ARGS(local, queues, drop),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(bool, drop)
		__field(u32, queues)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->drop = drop;
		__entry->queues = queues;
	),

	TP_printk(
		LOCAL_PR_FMT " queues:0x%x drop:%d",
		LOCAL_PR_ARG, __entry->queues, __entry->drop
	)
);

TRACE_EVENT(drv_channel_switch,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_channel_switch *ch_switch),

	TP_ARGS(local, sdata, ch_switch),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		CHANDEF_ENTRY
		__field(u64, timestamp)
		__field(u32, device_timestamp)
		__field(bool, block_tx)
		__field(u8, count)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		CHANDEF_ASSIGN(&ch_switch->chandef)
		__entry->timestamp = ch_switch->timestamp;
		__entry->device_timestamp = ch_switch->device_timestamp;
		__entry->block_tx = ch_switch->block_tx;
		__entry->count = ch_switch->count;
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT " new " CHANDEF_PR_FMT " count:%d",
		LOCAL_PR_ARG, VIF_PR_ARG, CHANDEF_PR_ARG, __entry->count
	)
);

TRACE_EVENT(drv_set_antenna,
	TP_PROTO(struct ieee80211_local *local, u32 tx_ant, u32 rx_ant, int ret),

	TP_ARGS(local, tx_ant, rx_ant, ret),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u32, tx_ant)
		__field(u32, rx_ant)
		__field(int, ret)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->tx_ant = tx_ant;
		__entry->rx_ant = rx_ant;
		__entry->ret = ret;
	),

	TP_printk(
		LOCAL_PR_FMT " tx_ant:%d rx_ant:%d ret:%d",
		LOCAL_PR_ARG, __entry->tx_ant, __entry->rx_ant, __entry->ret
	)
);

TRACE_EVENT(drv_get_antenna,
	TP_PROTO(struct ieee80211_local *local, u32 tx_ant, u32 rx_ant, int ret),

	TP_ARGS(local, tx_ant, rx_ant, ret),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u32, tx_ant)
		__field(u32, rx_ant)
		__field(int, ret)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->tx_ant = tx_ant;
		__entry->rx_ant = rx_ant;
		__entry->ret = ret;
	),

	TP_printk(
		LOCAL_PR_FMT " tx_ant:%d rx_ant:%d ret:%d",
		LOCAL_PR_ARG, __entry->tx_ant, __entry->rx_ant, __entry->ret
	)
);

TRACE_EVENT(drv_remain_on_channel,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_channel *chan,
		 unsigned int duration,
		 enum ieee80211_roc_type type),

	TP_ARGS(local, sdata, chan, duration, type),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(int, center_freq)
		__field(int, freq_offset)
		__field(unsigned int, duration)
		__field(u32, type)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->center_freq = chan->center_freq;
		__entry->freq_offset = chan->freq_offset;
		__entry->duration = duration;
		__entry->type = type;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT " freq:%d.%03dMHz duration:%dms type=%d",
		LOCAL_PR_ARG, VIF_PR_ARG,
		__entry->center_freq, __entry->freq_offset,
		__entry->duration, __entry->type
	)
);

DEFINE_EVENT(local_sdata_evt, drv_cancel_remain_on_channel,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

TRACE_EVENT(drv_set_ringparam,
	TP_PROTO(struct ieee80211_local *local, u32 tx, u32 rx),

	TP_ARGS(local, tx, rx),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u32, tx)
		__field(u32, rx)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->tx = tx;
		__entry->rx = rx;
	),

	TP_printk(
		LOCAL_PR_FMT " tx:%d rx %d",
		LOCAL_PR_ARG, __entry->tx, __entry->rx
	)
);

TRACE_EVENT(drv_get_ringparam,
	TP_PROTO(struct ieee80211_local *local, u32 *tx, u32 *tx_max,
		 u32 *rx, u32 *rx_max),

	TP_ARGS(local, tx, tx_max, rx, rx_max),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u32, tx)
		__field(u32, tx_max)
		__field(u32, rx)
		__field(u32, rx_max)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->tx = *tx;
		__entry->tx_max = *tx_max;
		__entry->rx = *rx;
		__entry->rx_max = *rx_max;
	),

	TP_printk(
		LOCAL_PR_FMT " tx:%d tx_max %d rx %d rx_max %d",
		LOCAL_PR_ARG,
		__entry->tx, __entry->tx_max, __entry->rx, __entry->rx_max
	)
);

DEFINE_EVENT(local_only_evt, drv_tx_frames_pending,
	TP_PROTO(struct ieee80211_local *local),
	TP_ARGS(local)
);

DEFINE_EVENT(local_only_evt, drv_offchannel_tx_cancel_wait,
	TP_PROTO(struct ieee80211_local *local),
	TP_ARGS(local)
);

TRACE_EVENT(drv_set_bitrate_mask,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 const struct cfg80211_bitrate_mask *mask),

	TP_ARGS(local, sdata, mask),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(u32, legacy_2g)
		__field(u32, legacy_5g)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->legacy_2g = mask->control[NL80211_BAND_2GHZ].legacy;
		__entry->legacy_5g = mask->control[NL80211_BAND_5GHZ].legacy;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT " 2G Mask:0x%x 5G Mask:0x%x",
		LOCAL_PR_ARG, VIF_PR_ARG, __entry->legacy_2g, __entry->legacy_5g
	)
);

TRACE_EVENT(drv_set_rekey_data,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct cfg80211_gtk_rekey_data *data),

	TP_ARGS(local, sdata, data),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__array(u8, kek, NL80211_KEK_LEN)
		__array(u8, kck, NL80211_KCK_LEN)
		__array(u8, replay_ctr, NL80211_REPLAY_CTR_LEN)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		memcpy(__entry->kek, data->kek, NL80211_KEK_LEN);
		memcpy(__entry->kck, data->kck, NL80211_KCK_LEN);
		memcpy(__entry->replay_ctr, data->replay_ctr,
		       NL80211_REPLAY_CTR_LEN);
	),

	TP_printk(LOCAL_PR_FMT VIF_PR_FMT,
		  LOCAL_PR_ARG, VIF_PR_ARG)
);

TRACE_EVENT(drv_event_callback,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 const struct ieee80211_event *_event),

	TP_ARGS(local, sdata, _event),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(u32, type)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->type = _event->type;
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT " event:%d",
		LOCAL_PR_ARG, VIF_PR_ARG, __entry->type
	)
);

DECLARE_EVENT_CLASS(release_evt,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sta *sta,
		 u16 tids, int num_frames,
		 enum ieee80211_frame_release_type reason,
		 bool more_data),

	TP_ARGS(local, sta, tids, num_frames, reason, more_data),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		STA_ENTRY
		__field(u16, tids)
		__field(int, num_frames)
		__field(int, reason)
		__field(bool, more_data)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		STA_ASSIGN;
		__entry->tids = tids;
		__entry->num_frames = num_frames;
		__entry->reason = reason;
		__entry->more_data = more_data;
	),

	TP_printk(
		LOCAL_PR_FMT STA_PR_FMT
		" TIDs:0x%.4x frames:%d reason:%d more:%d",
		LOCAL_PR_ARG, STA_PR_ARG, __entry->tids, __entry->num_frames,
		__entry->reason, __entry->more_data
	)
);

DEFINE_EVENT(release_evt, drv_release_buffered_frames,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sta *sta,
		 u16 tids, int num_frames,
		 enum ieee80211_frame_release_type reason,
		 bool more_data),

	TP_ARGS(local, sta, tids, num_frames, reason, more_data)
);

DEFINE_EVENT(release_evt, drv_allow_buffered_frames,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sta *sta,
		 u16 tids, int num_frames,
		 enum ieee80211_frame_release_type reason,
		 bool more_data),

	TP_ARGS(local, sta, tids, num_frames, reason, more_data)
);

TRACE_EVENT(drv_mgd_prepare_tx,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 u16 duration),

	TP_ARGS(local, sdata, duration),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(u32, duration)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->duration = duration;
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT " duration: %u",
		LOCAL_PR_ARG, VIF_PR_ARG, __entry->duration
	)
);

DEFINE_EVENT(local_sdata_evt, drv_mgd_protect_tdls_discover,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),

	TP_ARGS(local, sdata)
);

DECLARE_EVENT_CLASS(local_chanctx,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_chanctx *ctx),

	TP_ARGS(local, ctx),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		CHANCTX_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		CHANCTX_ASSIGN;
	),

	TP_printk(
		LOCAL_PR_FMT CHANCTX_PR_FMT,
		LOCAL_PR_ARG, CHANCTX_PR_ARG
	)
);

DEFINE_EVENT(local_chanctx, drv_add_chanctx,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_chanctx *ctx),
	TP_ARGS(local, ctx)
);

DEFINE_EVENT(local_chanctx, drv_remove_chanctx,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_chanctx *ctx),
	TP_ARGS(local, ctx)
);

TRACE_EVENT(drv_change_chanctx,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_chanctx *ctx,
		 u32 changed),

	TP_ARGS(local, ctx, changed),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		CHANCTX_ENTRY
		__field(u32, changed)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		CHANCTX_ASSIGN;
		__entry->changed = changed;
	),

	TP_printk(
		LOCAL_PR_FMT CHANCTX_PR_FMT " changed:%#x",
		LOCAL_PR_ARG, CHANCTX_PR_ARG, __entry->changed
	)
);

#if !defined(__TRACE_VIF_ENTRY)
#define __TRACE_VIF_ENTRY
struct trace_vif_entry {
	enum nl80211_iftype vif_type;
	bool p2p;
	char vif_name[IFNAMSIZ];
} __packed;

struct trace_chandef_entry {
	u32 control_freq;
	u32 freq_offset;
	u32 chan_width;
	u32 center_freq1;
	u32 freq1_offset;
	u32 center_freq2;
} __packed;

struct trace_switch_entry {
	struct trace_vif_entry vif;
	struct trace_chandef_entry old_chandef;
	struct trace_chandef_entry new_chandef;
} __packed;

#define SWITCH_ENTRY_ASSIGN(to, from) local_vifs[i].to = vifs[i].from
#endif

TRACE_EVENT(drv_switch_vif_chanctx,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_vif_chanctx_switch *vifs,
		 int n_vifs, enum ieee80211_chanctx_switch_mode mode),
	    TP_ARGS(local, vifs, n_vifs, mode),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(int, n_vifs)
		__field(u32, mode)
		__dynamic_array(u8, vifs,
				sizeof(struct trace_switch_entry) * n_vifs)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->n_vifs = n_vifs;
		__entry->mode = mode;
		{
			struct trace_switch_entry *local_vifs =
				__get_dynamic_array(vifs);
			int i;

			for (i = 0; i < n_vifs; i++) {
				struct ieee80211_sub_if_data *sdata;

				sdata = container_of(vifs[i].vif,
						struct ieee80211_sub_if_data,
						vif);

				SWITCH_ENTRY_ASSIGN(vif.vif_type, vif->type);
				SWITCH_ENTRY_ASSIGN(vif.p2p, vif->p2p);
				strncpy(local_vifs[i].vif.vif_name,
					sdata->name,
					sizeof(local_vifs[i].vif.vif_name));
				SWITCH_ENTRY_ASSIGN(old_chandef.control_freq,
						old_ctx->def.chan->center_freq);
				SWITCH_ENTRY_ASSIGN(old_chandef.freq_offset,
						old_ctx->def.chan->freq_offset);
				SWITCH_ENTRY_ASSIGN(old_chandef.chan_width,
						    old_ctx->def.width);
				SWITCH_ENTRY_ASSIGN(old_chandef.center_freq1,
						    old_ctx->def.center_freq1);
				SWITCH_ENTRY_ASSIGN(old_chandef.freq1_offset,
						    old_ctx->def.freq1_offset);
				SWITCH_ENTRY_ASSIGN(old_chandef.center_freq2,
						    old_ctx->def.center_freq2);
				SWITCH_ENTRY_ASSIGN(new_chandef.control_freq,
						new_ctx->def.chan->center_freq);
				SWITCH_ENTRY_ASSIGN(new_chandef.freq_offset,
						new_ctx->def.chan->freq_offset);
				SWITCH_ENTRY_ASSIGN(new_chandef.chan_width,
						    new_ctx->def.width);
				SWITCH_ENTRY_ASSIGN(new_chandef.center_freq1,
						    new_ctx->def.center_freq1);
				SWITCH_ENTRY_ASSIGN(new_chandef.freq1_offset,
						    new_ctx->def.freq1_offset);
				SWITCH_ENTRY_ASSIGN(new_chandef.center_freq2,
						    new_ctx->def.center_freq2);
			}
		}
	),

	TP_printk(
		LOCAL_PR_FMT " n_vifs:%d mode:%d",
		LOCAL_PR_ARG, __entry->n_vifs, __entry->mode
	)
);

DECLARE_EVENT_CLASS(local_sdata_chanctx,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_chanctx *ctx),

	TP_ARGS(local, sdata, ctx),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		CHANCTX_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		CHANCTX_ASSIGN;
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT CHANCTX_PR_FMT,
		LOCAL_PR_ARG, VIF_PR_ARG, CHANCTX_PR_ARG
	)
);

DEFINE_EVENT(local_sdata_chanctx, drv_assign_vif_chanctx,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_chanctx *ctx),
	TP_ARGS(local, sdata, ctx)
);

DEFINE_EVENT(local_sdata_chanctx, drv_unassign_vif_chanctx,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_chanctx *ctx),
	TP_ARGS(local, sdata, ctx)
);

TRACE_EVENT(drv_start_ap,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_bss_conf *info),

	TP_ARGS(local, sdata, info),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(u8, dtimper)
		__field(u16, bcnint)
		__dynamic_array(u8, ssid, info->ssid_len)
		__field(bool, hidden_ssid)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->dtimper = info->dtim_period;
		__entry->bcnint = info->beacon_int;
		memcpy(__get_dynamic_array(ssid), info->ssid, info->ssid_len);
		__entry->hidden_ssid = info->hidden_ssid;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT,
		LOCAL_PR_ARG, VIF_PR_ARG
	)
);

DEFINE_EVENT(local_sdata_evt, drv_stop_ap,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

TRACE_EVENT(drv_reconfig_complete,
	TP_PROTO(struct ieee80211_local *local,
		 enum ieee80211_reconfig_type reconfig_type),
	TP_ARGS(local, reconfig_type),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u8, reconfig_type)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->reconfig_type = reconfig_type;
	),

	TP_printk(
		LOCAL_PR_FMT  " reconfig_type:%d",
		LOCAL_PR_ARG, __entry->reconfig_type
	)

);

#if IS_ENABLED(CONFIG_IPV6)
DEFINE_EVENT(local_sdata_evt, drv_ipv6_addr_change,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);
#endif

TRACE_EVENT(drv_join_ibss,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_bss_conf *info),

	TP_ARGS(local, sdata, info),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(u8, dtimper)
		__field(u16, bcnint)
		__dynamic_array(u8, ssid, info->ssid_len)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->dtimper = info->dtim_period;
		__entry->bcnint = info->beacon_int;
		memcpy(__get_dynamic_array(ssid), info->ssid, info->ssid_len);
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT,
		LOCAL_PR_ARG, VIF_PR_ARG
	)
);

DEFINE_EVENT(local_sdata_evt, drv_leave_ibss,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

TRACE_EVENT(drv_get_expected_throughput,
	TP_PROTO(struct ieee80211_sta *sta),

	TP_ARGS(sta),

	TP_STRUCT__entry(
		STA_ENTRY
	),

	TP_fast_assign(
		STA_ASSIGN;
	),

	TP_printk(
		STA_PR_FMT, STA_PR_ARG
	)
);

TRACE_EVENT(drv_start_nan,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct cfg80211_nan_conf *conf),

	TP_ARGS(local, sdata, conf),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(u8, master_pref)
		__field(u8, bands)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->master_pref = conf->master_pref;
		__entry->bands = conf->bands;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT
		", master preference: %u, bands: 0x%0x",
		LOCAL_PR_ARG, VIF_PR_ARG, __entry->master_pref,
		__entry->bands
	)
);

TRACE_EVENT(drv_stop_nan,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),

	TP_ARGS(local, sdata),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT,
		LOCAL_PR_ARG, VIF_PR_ARG
	)
);

TRACE_EVENT(drv_nan_change_conf,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct cfg80211_nan_conf *conf,
		 u32 changes),

	TP_ARGS(local, sdata, conf, changes),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(u8, master_pref)
		__field(u8, bands)
		__field(u32, changes)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->master_pref = conf->master_pref;
		__entry->bands = conf->bands;
		__entry->changes = changes;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT
		", master preference: %u, bands: 0x%0x, changes: 0x%x",
		LOCAL_PR_ARG, VIF_PR_ARG, __entry->master_pref,
		__entry->bands, __entry->changes
	)
);

TRACE_EVENT(drv_add_nan_func,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 const struct cfg80211_nan_func *func),

	TP_ARGS(local, sdata, func),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(u8, type)
		__field(u8, inst_id)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->type = func->type;
		__entry->inst_id = func->instance_id;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT
		", type: %u, inst_id: %u",
		LOCAL_PR_ARG, VIF_PR_ARG, __entry->type, __entry->inst_id
	)
);

TRACE_EVENT(drv_del_nan_func,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 u8 instance_id),

	TP_ARGS(local, sdata, instance_id),
	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(u8, instance_id)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->instance_id = instance_id;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT
		", instance_id: %u",
		LOCAL_PR_ARG, VIF_PR_ARG, __entry->instance_id
	)
);

DEFINE_EVENT(local_sdata_evt, drv_start_pmsr,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

DEFINE_EVENT(local_sdata_evt, drv_abort_pmsr,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

/*
 * Tracing for API calls that drivers call.
 */

TRACE_EVENT(api_start_tx_ba_session,
	TP_PROTO(struct ieee80211_sta *sta, u16 tid),

	TP_ARGS(sta, tid),

	TP_STRUCT__entry(
		STA_ENTRY
		__field(u16, tid)
	),

	TP_fast_assign(
		STA_ASSIGN;
		__entry->tid = tid;
	),

	TP_printk(
		STA_PR_FMT " tid:%d",
		STA_PR_ARG, __entry->tid
	)
);

TRACE_EVENT(api_start_tx_ba_cb,
	TP_PROTO(struct ieee80211_sub_if_data *sdata, const u8 *ra, u16 tid),

	TP_ARGS(sdata, ra, tid),

	TP_STRUCT__entry(
		VIF_ENTRY
		__array(u8, ra, ETH_ALEN)
		__field(u16, tid)
	),

	TP_fast_assign(
		VIF_ASSIGN;
		memcpy(__entry->ra, ra, ETH_ALEN);
		__entry->tid = tid;
	),

	TP_printk(
		VIF_PR_FMT " ra:%pM tid:%d",
		VIF_PR_ARG, __entry->ra, __entry->tid
	)
);

TRACE_EVENT(api_stop_tx_ba_session,
	TP_PROTO(struct ieee80211_sta *sta, u16 tid),

	TP_ARGS(sta, tid),

	TP_STRUCT__entry(
		STA_ENTRY
		__field(u16, tid)
	),

	TP_fast_assign(
		STA_ASSIGN;
		__entry->tid = tid;
	),

	TP_printk(
		STA_PR_FMT " tid:%d",
		STA_PR_ARG, __entry->tid
	)
);

TRACE_EVENT(api_stop_tx_ba_cb,
	TP_PROTO(struct ieee80211_sub_if_data *sdata, const u8 *ra, u16 tid),

	TP_ARGS(sdata, ra, tid),

	TP_STRUCT__entry(
		VIF_ENTRY
		__array(u8, ra, ETH_ALEN)
		__field(u16, tid)
	),

	TP_fast_assign(
		VIF_ASSIGN;
		memcpy(__entry->ra, ra, ETH_ALEN);
		__entry->tid = tid;
	),

	TP_printk(
		VIF_PR_FMT " ra:%pM tid:%d",
		VIF_PR_ARG, __entry->ra, __entry->tid
	)
);

DEFINE_EVENT(local_only_evt, api_restart_hw,
	TP_PROTO(struct ieee80211_local *local),
	TP_ARGS(local)
);

TRACE_EVENT(api_beacon_loss,
	TP_PROTO(struct ieee80211_sub_if_data *sdata),

	TP_ARGS(sdata),

	TP_STRUCT__entry(
		VIF_ENTRY
	),

	TP_fast_assign(
		VIF_ASSIGN;
	),

	TP_printk(
		VIF_PR_FMT,
		VIF_PR_ARG
	)
);

TRACE_EVENT(api_connection_loss,
	TP_PROTO(struct ieee80211_sub_if_data *sdata),

	TP_ARGS(sdata),

	TP_STRUCT__entry(
		VIF_ENTRY
	),

	TP_fast_assign(
		VIF_ASSIGN;
	),

	TP_printk(
		VIF_PR_FMT,
		VIF_PR_ARG
	)
);

TRACE_EVENT(api_cqm_rssi_notify,
	TP_PROTO(struct ieee80211_sub_if_data *sdata,
		 enum nl80211_cqm_rssi_threshold_event rssi_event,
		 s32 rssi_level),

	TP_ARGS(sdata, rssi_event, rssi_level),

	TP_STRUCT__entry(
		VIF_ENTRY
		__field(u32, rssi_event)
		__field(s32, rssi_level)
	),

	TP_fast_assign(
		VIF_ASSIGN;
		__entry->rssi_event = rssi_event;
		__entry->rssi_level = rssi_level;
	),

	TP_printk(
		VIF_PR_FMT " event:%d rssi:%d",
		VIF_PR_ARG, __entry->rssi_event, __entry->rssi_level
	)
);

DEFINE_EVENT(local_sdata_evt, api_cqm_beacon_loss_notify,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

TRACE_EVENT(api_scan_completed,
	TP_PROTO(struct ieee80211_local *local, bool aborted),

	TP_ARGS(local, aborted),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(bool, aborted)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->aborted = aborted;
	),

	TP_printk(
		LOCAL_PR_FMT " aborted:%d",
		LOCAL_PR_ARG, __entry->aborted
	)
);

TRACE_EVENT(api_sched_scan_results,
	TP_PROTO(struct ieee80211_local *local),

	TP_ARGS(local),

	TP_STRUCT__entry(
		LOCAL_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
	),

	TP_printk(
		LOCAL_PR_FMT, LOCAL_PR_ARG
	)
);

TRACE_EVENT(api_sched_scan_stopped,
	TP_PROTO(struct ieee80211_local *local),

	TP_ARGS(local),

	TP_STRUCT__entry(
		LOCAL_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
	),

	TP_printk(
		LOCAL_PR_FMT, LOCAL_PR_ARG
	)
);

TRACE_EVENT(api_sta_block_awake,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sta *sta, bool block),

	TP_ARGS(local, sta, block),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		STA_ENTRY
		__field(bool, block)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		STA_ASSIGN;
		__entry->block = block;
	),

	TP_printk(
		LOCAL_PR_FMT STA_PR_FMT " block:%d",
		LOCAL_PR_ARG, STA_PR_ARG, __entry->block
	)
);

TRACE_EVENT(api_chswitch_done,
	TP_PROTO(struct ieee80211_sub_if_data *sdata, bool success),

	TP_ARGS(sdata, success),

	TP_STRUCT__entry(
		VIF_ENTRY
		__field(bool, success)
	),

	TP_fast_assign(
		VIF_ASSIGN;
		__entry->success = success;
	),

	TP_printk(
		VIF_PR_FMT " success=%d",
		VIF_PR_ARG, __entry->success
	)
);

DEFINE_EVENT(local_only_evt, api_ready_on_channel,
	TP_PROTO(struct ieee80211_local *local),
	TP_ARGS(local)
);

DEFINE_EVENT(local_only_evt, api_remain_on_channel_expired,
	TP_PROTO(struct ieee80211_local *local),
	TP_ARGS(local)
);

TRACE_EVENT(api_gtk_rekey_notify,
	TP_PROTO(struct ieee80211_sub_if_data *sdata,
		 const u8 *bssid, const u8 *replay_ctr),

	TP_ARGS(sdata, bssid, replay_ctr),

	TP_STRUCT__entry(
		VIF_ENTRY
		__array(u8, bssid, ETH_ALEN)
		__array(u8, replay_ctr, NL80211_REPLAY_CTR_LEN)
	),

	TP_fast_assign(
		VIF_ASSIGN;
		memcpy(__entry->bssid, bssid, ETH_ALEN);
		memcpy(__entry->replay_ctr, replay_ctr, NL80211_REPLAY_CTR_LEN);
	),

	TP_printk(VIF_PR_FMT, VIF_PR_ARG)
);

TRACE_EVENT(api_enable_rssi_reports,
	TP_PROTO(struct ieee80211_sub_if_data *sdata,
		 int rssi_min_thold, int rssi_max_thold),

	TP_ARGS(sdata, rssi_min_thold, rssi_max_thold),

	TP_STRUCT__entry(
		VIF_ENTRY
		__field(int, rssi_min_thold)
		__field(int, rssi_max_thold)
	),

	TP_fast_assign(
		VIF_ASSIGN;
		__entry->rssi_min_thold = rssi_min_thold;
		__entry->rssi_max_thold = rssi_max_thold;
	),

	TP_printk(
		VIF_PR_FMT " rssi_min_thold =%d, rssi_max_thold = %d",
		VIF_PR_ARG, __entry->rssi_min_thold, __entry->rssi_max_thold
	)
);

TRACE_EVENT(api_eosp,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sta *sta),

	TP_ARGS(local, sta),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		STA_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		STA_ASSIGN;
	),

	TP_printk(
		LOCAL_PR_FMT STA_PR_FMT,
		LOCAL_PR_ARG, STA_PR_ARG
	)
);

TRACE_EVENT(api_send_eosp_nullfunc,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sta *sta,
		 u8 tid),

	TP_ARGS(local, sta, tid),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		STA_ENTRY
		__field(u8, tid)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		STA_ASSIGN;
		__entry->tid = tid;
	),

	TP_printk(
		LOCAL_PR_FMT STA_PR_FMT " tid:%d",
		LOCAL_PR_ARG, STA_PR_ARG, __entry->tid
	)
);

TRACE_EVENT(api_sta_set_buffered,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sta *sta,
		 u8 tid, bool buffered),

	TP_ARGS(local, sta, tid, buffered),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		STA_ENTRY
		__field(u8, tid)
		__field(bool, buffered)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		STA_ASSIGN;
		__entry->tid = tid;
		__entry->buffered = buffered;
	),

	TP_printk(
		LOCAL_PR_FMT STA_PR_FMT " tid:%d buffered:%d",
		LOCAL_PR_ARG, STA_PR_ARG, __entry->tid, __entry->buffered
	)
);

/*
 * Tracing for internal functions
 * (which may also be called in response to driver calls)
 */

TRACE_EVENT(wake_queue,
	TP_PROTO(struct ieee80211_local *local, u16 queue,
		 enum queue_stop_reason reason),

	TP_ARGS(local, queue, reason),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u16, queue)
		__field(u32, reason)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->queue = queue;
		__entry->reason = reason;
	),

	TP_printk(
		LOCAL_PR_FMT " queue:%d, reason:%d",
		LOCAL_PR_ARG, __entry->queue, __entry->reason
	)
);

TRACE_EVENT(stop_queue,
	TP_PROTO(struct ieee80211_local *local, u16 queue,
		 enum queue_stop_reason reason),

	TP_ARGS(local, queue, reason),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u16, queue)
		__field(u32, reason)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->queue = queue;
		__entry->reason = reason;
	),

	TP_printk(
		LOCAL_PR_FMT " queue:%d, reason:%d",
		LOCAL_PR_ARG, __entry->queue, __entry->reason
	)
);

TRACE_EVENT(drv_set_default_unicast_key,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 int key_idx),

	TP_ARGS(local, sdata, key_idx),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(int, key_idx)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->key_idx = key_idx;
	),

	TP_printk(LOCAL_PR_FMT VIF_PR_FMT " key_idx:%d",
		  LOCAL_PR_ARG, VIF_PR_ARG, __entry->key_idx)
);

TRACE_EVENT(api_radar_detected,
	TP_PROTO(struct ieee80211_local *local),

	TP_ARGS(local),

	TP_STRUCT__entry(
		LOCAL_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
	),

	TP_printk(
		LOCAL_PR_FMT " radar detected",
		LOCAL_PR_ARG
	)
);

TRACE_EVENT(drv_channel_switch_beacon,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct cfg80211_chan_def *chandef),

	TP_ARGS(local, sdata, chandef),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		CHANDEF_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		CHANDEF_ASSIGN(chandef);
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT " channel switch to " CHANDEF_PR_FMT,
		LOCAL_PR_ARG, VIF_PR_ARG, CHANDEF_PR_ARG
	)
);

TRACE_EVENT(drv_pre_channel_switch,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_channel_switch *ch_switch),

	TP_ARGS(local, sdata, ch_switch),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		CHANDEF_ENTRY
		__field(u64, timestamp)
		__field(u32, device_timestamp)
		__field(bool, block_tx)
		__field(u8, count)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		CHANDEF_ASSIGN(&ch_switch->chandef)
		__entry->timestamp = ch_switch->timestamp;
		__entry->device_timestamp = ch_switch->device_timestamp;
		__entry->block_tx = ch_switch->block_tx;
		__entry->count = ch_switch->count;
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT " prepare channel switch to "
		CHANDEF_PR_FMT  " count:%d block_tx:%d timestamp:%llu",
		LOCAL_PR_ARG, VIF_PR_ARG, CHANDEF_PR_ARG, __entry->count,
		__entry->block_tx, __entry->timestamp
	)
);

DEFINE_EVENT(local_sdata_evt, drv_post_channel_switch,
	     TP_PROTO(struct ieee80211_local *local,
		      struct ieee80211_sub_if_data *sdata),
	     TP_ARGS(local, sdata)
);

DEFINE_EVENT(local_sdata_evt, drv_abort_channel_switch,
	     TP_PROTO(struct ieee80211_local *local,
		      struct ieee80211_sub_if_data *sdata),
	     TP_ARGS(local, sdata)
);

TRACE_EVENT(drv_channel_switch_rx_beacon,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_channel_switch *ch_switch),

	TP_ARGS(local, sdata, ch_switch),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		CHANDEF_ENTRY
		__field(u64, timestamp)
		__field(u32, device_timestamp)
		__field(bool, block_tx)
		__field(u8, count)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		CHANDEF_ASSIGN(&ch_switch->chandef)
		__entry->timestamp = ch_switch->timestamp;
		__entry->device_timestamp = ch_switch->device_timestamp;
		__entry->block_tx = ch_switch->block_tx;
		__entry->count = ch_switch->count;
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT
		" received a channel switch beacon to "
		CHANDEF_PR_FMT  " count:%d block_tx:%d timestamp:%llu",
		LOCAL_PR_ARG, VIF_PR_ARG, CHANDEF_PR_ARG, __entry->count,
		__entry->block_tx, __entry->timestamp
	)
);

TRACE_EVENT(drv_get_txpower,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 int dbm, int ret),

	TP_ARGS(local, sdata, dbm, ret),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(int, dbm)
		__field(int, ret)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->dbm = dbm;
		__entry->ret = ret;
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT " dbm:%d ret:%d",
		LOCAL_PR_ARG, VIF_PR_ARG, __entry->dbm, __entry->ret
	)
);

TRACE_EVENT(drv_tdls_channel_switch,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_sta *sta, u8 oper_class,
		 struct cfg80211_chan_def *chandef),

	TP_ARGS(local, sdata, sta, oper_class, chandef),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		STA_ENTRY
		__field(u8, oper_class)
		CHANDEF_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		STA_ASSIGN;
		__entry->oper_class = oper_class;
		CHANDEF_ASSIGN(chandef)
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT " tdls channel switch to"
		CHANDEF_PR_FMT  " oper_class:%d " STA_PR_FMT,
		LOCAL_PR_ARG, VIF_PR_ARG, CHANDEF_PR_ARG, __entry->oper_class,
		STA_PR_ARG
	)
);

TRACE_EVENT(drv_tdls_cancel_channel_switch,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_sta *sta),

	TP_ARGS(local, sdata, sta),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		STA_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		STA_ASSIGN;
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT
		" tdls cancel channel switch with " STA_PR_FMT,
		LOCAL_PR_ARG, VIF_PR_ARG, STA_PR_ARG
	)
);

TRACE_EVENT(drv_tdls_recv_channel_switch,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_tdls_ch_sw_params *params),

	TP_ARGS(local, sdata, params),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		__field(u8, action_code)
		STA_ENTRY
		CHANDEF_ENTRY
		__field(u32, status)
		__field(bool, peer_initiator)
		__field(u32, timestamp)
		__field(u16, switch_time)
		__field(u16, switch_timeout)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		STA_NAMED_ASSIGN(params->sta);
		CHANDEF_ASSIGN(params->chandef)
		__entry->peer_initiator = params->sta->tdls_initiator;
		__entry->action_code = params->action_code;
		__entry->status = params->status;
		__entry->timestamp = params->timestamp;
		__entry->switch_time = params->switch_time;
		__entry->switch_timeout = params->switch_timeout;
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT " received tdls channel switch packet"
		" action:%d status:%d time:%d switch time:%d switch"
		" timeout:%d initiator: %d chan:" CHANDEF_PR_FMT STA_PR_FMT,
		LOCAL_PR_ARG, VIF_PR_ARG, __entry->action_code, __entry->status,
		__entry->timestamp, __entry->switch_time,
		__entry->switch_timeout, __entry->peer_initiator,
		CHANDEF_PR_ARG, STA_PR_ARG
	)
);

TRACE_EVENT(drv_wake_tx_queue,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct txq_info *txq),

	TP_ARGS(local, sdata, txq),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		STA_ENTRY
		__field(u8, ac)
		__field(u8, tid)
	),

	TP_fast_assign(
		struct ieee80211_sta *sta = txq->txq.sta;

		LOCAL_ASSIGN;
		VIF_ASSIGN;
		STA_ASSIGN;
		__entry->ac = txq->txq.ac;
		__entry->tid = txq->txq.tid;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT  STA_PR_FMT " ac:%d tid:%d",
		LOCAL_PR_ARG, VIF_PR_ARG, STA_PR_ARG, __entry->ac, __entry->tid
	)
);

TRACE_EVENT(drv_get_ftm_responder_stats,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct cfg80211_ftm_responder_stats *ftm_stats),

	TP_ARGS(local, sdata, ftm_stats),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT,
		LOCAL_PR_ARG, VIF_PR_ARG
	)
);

DEFINE_EVENT(local_sdata_addr_evt, drv_update_vif_offload,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata),
	TP_ARGS(local, sdata)
);

TRACE_EVENT(drv_sta_set_4addr,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_sub_if_data *sdata,
		 struct ieee80211_sta *sta, bool enabled),

	TP_ARGS(local, sdata, sta, enabled),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		VIF_ENTRY
		STA_ENTRY
		__field(bool, enabled)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		STA_ASSIGN;
		__entry->enabled = enabled;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT  STA_PR_FMT " enabled:%d",
		LOCAL_PR_ARG, VIF_PR_ARG, STA_PR_ARG, __entry->enabled
	)
);

#endif /* !__MAC80211_DRIVER_TRACE || TRACE_HEADER_MULTI_READ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
