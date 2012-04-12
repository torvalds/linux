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
#define STA_ASSIGN	(sta ? memcpy(__entry->sta_addr, sta->addr, ETH_ALEN) : memset(__entry->sta_addr, 0, ETH_ALEN))
#define STA_PR_FMT	" sta:%pM"
#define STA_PR_ARG	__entry->sta_addr

#define VIF_ENTRY	__field(enum nl80211_iftype, vif_type) __field(void *, sdata)	\
			__field(bool, p2p)						\
			__string(vif_name, sdata->dev ? sdata->dev->name : "<nodev>")
#define VIF_ASSIGN	__entry->vif_type = sdata->vif.type; __entry->sdata = sdata;	\
			__entry->p2p = sdata->vif.p2p;					\
			__assign_str(vif_name, sdata->dev ? sdata->dev->name : "<nodev>")
#define VIF_PR_FMT	" vif:%s(%d%s)"
#define VIF_PR_ARG	__get_str(vif_name), __entry->vif_type, __entry->p2p ? "/p2p" : ""

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
		__array(char, addr, 6)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		memcpy(__entry->addr, sdata->vif.addr, 6);
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
		__field(int, max_sleep_period)
		__field(u16, listen_interval)
		__field(u8, long_frame_max_tx_count)
		__field(u8, short_frame_max_tx_count)
		__field(int, center_freq)
		__field(int, channel_type)
		__field(int, smps)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->changed = changed;
		__entry->flags = local->hw.conf.flags;
		__entry->power_level = local->hw.conf.power_level;
		__entry->dynamic_ps_timeout = local->hw.conf.dynamic_ps_timeout;
		__entry->max_sleep_period = local->hw.conf.max_sleep_period;
		__entry->listen_interval = local->hw.conf.listen_interval;
		__entry->long_frame_max_tx_count = local->hw.conf.long_frame_max_tx_count;
		__entry->short_frame_max_tx_count = local->hw.conf.short_frame_max_tx_count;
		__entry->center_freq = local->hw.conf.channel->center_freq;
		__entry->channel_type = local->hw.conf.channel_type;
		__entry->smps = local->hw.conf.smps_mode;
	),

	TP_printk(
		LOCAL_PR_FMT " ch:%#x freq:%d",
		LOCAL_PR_ARG, __entry->changed, __entry->center_freq
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
		__field(bool, assoc)
		__field(u16, aid)
		__field(bool, cts)
		__field(bool, shortpre)
		__field(bool, shortslot)
		__field(u8, dtimper)
		__field(u16, bcnint)
		__field(u16, assoc_cap)
		__field(u64, timestamp)
		__field(u32, basic_rates)
		__field(u32, changed)
		__field(bool, enable_beacon)
		__field(u16, ht_operation_mode)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		__entry->changed = changed;
		__entry->aid = info->aid;
		__entry->assoc = info->assoc;
		__entry->shortpre = info->use_short_preamble;
		__entry->cts = info->use_cts_prot;
		__entry->shortslot = info->use_short_slot;
		__entry->dtimper = info->dtim_period;
		__entry->bcnint = info->beacon_int;
		__entry->assoc_cap = info->assoc_capability;
		__entry->timestamp = info->last_tsf;
		__entry->basic_rates = info->basic_rates;
		__entry->enable_beacon = info->enable_beacon;
		__entry->ht_operation_mode = info->ht_operation_mode;
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
		LOCAL_PR_ARG, STA_PR_FMT, __entry->set
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
		__field(u32, cipher)
		__field(u8, hw_key_idx)
		__field(u8, flags)
		__field(s8, keyidx)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		STA_ASSIGN;
		__entry->cipher = key->cipher;
		__entry->flags = key->flags;
		__entry->keyidx = key->keyidx;
		__entry->hw_key_idx = key->hw_key_idx;
	),

	TP_printk(
		LOCAL_PR_FMT  VIF_PR_FMT  STA_PR_FMT,
		LOCAL_PR_ARG, VIF_PR_ARG, STA_PR_ARG
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
		LOCAL_PR_ARG,VIF_PR_ARG,STA_PR_ARG, __entry->iv32
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

DEFINE_EVENT(local_only_evt, drv_sw_scan_start,
	TP_PROTO(struct ieee80211_local *local),
	TP_ARGS(local)
);

DEFINE_EVENT(local_only_evt, drv_sw_scan_complete,
	TP_PROTO(struct ieee80211_local *local),
	TP_ARGS(local)
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

TRACE_EVENT(drv_get_tkip_seq,
	TP_PROTO(struct ieee80211_local *local,
		 u8 hw_key_idx, u32 *iv32, u16 *iv16),

	TP_ARGS(local, hw_key_idx, iv32, iv16),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u8, hw_key_idx)
		__field(u32, iv32)
		__field(u16, iv16)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->hw_key_idx = hw_key_idx;
		__entry->iv32 = *iv32;
		__entry->iv16 = *iv16;
	),

	TP_printk(
		LOCAL_PR_FMT, LOCAL_PR_ARG
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
	TP_PROTO(struct ieee80211_local *local, u8 value),

	TP_ARGS(local, value),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u8, value)
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

TRACE_EVENT(drv_sta_add,
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

TRACE_EVENT(drv_sta_remove,
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
		 enum ieee80211_ampdu_mlme_action action,
		 struct ieee80211_sta *sta, u16 tid,
		 u16 *ssn, u8 buf_size),

	TP_ARGS(local, sdata, action, sta, tid, ssn, buf_size),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		STA_ENTRY
		__field(u32, action)
		__field(u16, tid)
		__field(u16, ssn)
		__field(u8, buf_size)
		VIF_ENTRY
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		VIF_ASSIGN;
		STA_ASSIGN;
		__entry->action = action;
		__entry->tid = tid;
		__entry->ssn = ssn ? *ssn : 0;
		__entry->buf_size = buf_size;
	),

	TP_printk(
		LOCAL_PR_FMT VIF_PR_FMT STA_PR_FMT " action:%d tid:%d buf:%d",
		LOCAL_PR_ARG, VIF_PR_ARG, STA_PR_ARG, __entry->action,
		__entry->tid, __entry->buf_size
	)
);

TRACE_EVENT(drv_get_survey,
	TP_PROTO(struct ieee80211_local *local, int idx,
		 struct survey_info *survey),

	TP_ARGS(local, idx, survey),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(int, idx)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->idx = idx;
	),

	TP_printk(
		LOCAL_PR_FMT " idx:%d",
		LOCAL_PR_ARG, __entry->idx
	)
);

TRACE_EVENT(drv_flush,
	TP_PROTO(struct ieee80211_local *local, bool drop),

	TP_ARGS(local, drop),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(bool, drop)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->drop = drop;
	),

	TP_printk(
		LOCAL_PR_FMT " drop:%d",
		LOCAL_PR_ARG, __entry->drop
	)
);

TRACE_EVENT(drv_channel_switch,
	TP_PROTO(struct ieee80211_local *local,
		 struct ieee80211_channel_switch *ch_switch),

	TP_ARGS(local, ch_switch),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u64, timestamp)
		__field(bool, block_tx)
		__field(u16, freq)
		__field(u8, count)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->timestamp = ch_switch->timestamp;
		__entry->block_tx = ch_switch->block_tx;
		__entry->freq = ch_switch->channel->center_freq;
		__entry->count = ch_switch->count;
	),

	TP_printk(
		LOCAL_PR_FMT " new freq:%u count:%d",
		LOCAL_PR_ARG, __entry->freq, __entry->count
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
	TP_PROTO(struct ieee80211_local *local, struct ieee80211_channel *chan,
		 enum nl80211_channel_type chantype, unsigned int duration),

	TP_ARGS(local, chan, chantype, duration),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(int, center_freq)
		__field(int, channel_type)
		__field(unsigned int, duration)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->center_freq = chan->center_freq;
		__entry->channel_type = chantype;
		__entry->duration = duration;
	),

	TP_printk(
		LOCAL_PR_FMT " freq:%dMHz duration:%dms",
		LOCAL_PR_ARG, __entry->center_freq, __entry->duration
	)
);

DEFINE_EVENT(local_only_evt, drv_cancel_remain_on_channel,
	TP_PROTO(struct ieee80211_local *local),
	TP_ARGS(local)
);

TRACE_EVENT(drv_offchannel_tx,
	TP_PROTO(struct ieee80211_local *local, struct sk_buff *skb,
		 struct ieee80211_channel *chan,
		 enum nl80211_channel_type channel_type,
		 unsigned int wait),

	TP_ARGS(local, skb, chan, channel_type, wait),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(int, center_freq)
		__field(int, channel_type)
		__field(unsigned int, wait)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->center_freq = chan->center_freq;
		__entry->channel_type = channel_type;
		__entry->wait = wait;
	),

	TP_printk(
		LOCAL_PR_FMT " freq:%dMHz, wait:%dms",
		LOCAL_PR_ARG, __entry->center_freq, __entry->wait
	)
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
		__entry->legacy_2g = mask->control[IEEE80211_BAND_2GHZ].legacy;
		__entry->legacy_5g = mask->control[IEEE80211_BAND_5GHZ].legacy;
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

TRACE_EVENT(drv_rssi_callback,
	TP_PROTO(struct ieee80211_local *local,
		 enum ieee80211_rssi_event rssi_event),

	TP_ARGS(local, rssi_event),

	TP_STRUCT__entry(
		LOCAL_ENTRY
		__field(u32, rssi_event)
	),

	TP_fast_assign(
		LOCAL_ASSIGN;
		__entry->rssi_event = rssi_event;
	),

	TP_printk(
		LOCAL_PR_FMT " rssi_event:%d",
		LOCAL_PR_ARG, __entry->rssi_event
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
		 enum nl80211_cqm_rssi_threshold_event rssi_event),

	TP_ARGS(sdata, rssi_event),

	TP_STRUCT__entry(
		VIF_ENTRY
		__field(u32, rssi_event)
	),

	TP_fast_assign(
		VIF_ASSIGN;
		__entry->rssi_event = rssi_event;
	),

	TP_printk(
		VIF_PR_FMT " event:%d",
		VIF_PR_ARG, __entry->rssi_event
	)
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
		LOCAL_PR_ARG, STA_PR_FMT, __entry->block
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
		LOCAL_PR_ARG, STA_PR_FMT
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
#endif /* !__MAC80211_DRIVER_TRACE || TRACE_HEADER_MULTI_READ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE driver-trace
#include <trace/define_trace.h>
