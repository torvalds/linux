/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM msm_low_power

#if !defined(_TRACE_MSM_LOW_POWER_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MSM_LOW_POWER_H_

#include <linux/tracepoint.h>

TRACE_EVENT(cpu_power_select,

	TP_PROTO(int index, u32 sleep_us, u32 latency, u64 sched_bias),

	TP_ARGS(index, sleep_us, latency, sched_bias),

	TP_STRUCT__entry(
		__field(int, index)
		__field(u32, sleep_us)
		__field(u32, latency)
		__field(u64, sched_bias)
	),

	TP_fast_assign(
		__entry->index = index;
		__entry->sleep_us = sleep_us;
		__entry->latency = latency;
		__entry->sched_bias = sched_bias;
	),

	TP_printk("idx:%d sleep_time:%u latency:%u sched_bias:%lu",
		__entry->index, __entry->sleep_us, __entry->latency,
		__entry->sched_bias)
);

TRACE_EVENT(cpu_pred_select,

	TP_PROTO(u32 predtype, u64 predicted, u32 tmr_time),

	TP_ARGS(predtype, predicted, tmr_time),

	TP_STRUCT__entry(
		__field(u32, predtype)
		__field(u64, predicted)
		__field(u32, tmr_time)
	),

	TP_fast_assign(
		__entry->predtype = predtype;
		__entry->predicted = predicted;
		__entry->tmr_time = tmr_time;
	),

	TP_printk("pred:%u time:%lu tmr_time:%u",
		__entry->predtype, (unsigned long)__entry->predicted,
		__entry->tmr_time)
);

TRACE_EVENT(cpu_pred_hist,

	TP_PROTO(int idx, u32 resi, u32 sample, u32 tmr),

	TP_ARGS(idx, resi, sample, tmr),

	TP_STRUCT__entry(
		__field(int, idx)
		__field(u32, resi)
		__field(u32, sample)
		__field(u32, tmr)
	),

	TP_fast_assign(
		__entry->idx = idx;
		__entry->resi = resi;
		__entry->sample = sample;
		__entry->tmr = tmr;
	),

	TP_printk("idx:%d resi:%u sample:%u tmr:%u",
		__entry->idx, __entry->resi,
		__entry->sample, __entry->tmr)
);

TRACE_EVENT(cpu_idle_enter,

	TP_PROTO(int index),

	TP_ARGS(index),

	TP_STRUCT__entry(
		__field(int, index)
	),

	TP_fast_assign(
		__entry->index = index;
	),

	TP_printk("idx:%d",
		__entry->index)
);

TRACE_EVENT(cpu_idle_exit,

	TP_PROTO(int index, int ret),

	TP_ARGS(index, ret),

	TP_STRUCT__entry(
		__field(int, index)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->index = index;
		__entry->ret = ret;
	),

	TP_printk("idx:%d ret:%d",
		__entry->index,
		__entry->ret)
);

TRACE_EVENT(cluster_enter,

	TP_PROTO(const char *name, int index, unsigned long sync_cpus,
		unsigned long child_cpus, bool from_idle),

	TP_ARGS(name, index, sync_cpus, child_cpus, from_idle),

	TP_STRUCT__entry(
		__string(name, name)
		__field(int, index)
		__field(unsigned long, sync_cpus)
		__field(unsigned long, child_cpus)
		__field(bool, from_idle)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->index = index;
		__entry->sync_cpus = sync_cpus;
		__entry->child_cpus = child_cpus;
		__entry->from_idle = from_idle;
	),

	TP_printk("cluster_name:%s idx:%d sync:0x%lx child:0x%lx idle:%d",
		__get_str(name),
		__entry->index,
		__entry->sync_cpus,
		__entry->child_cpus,
		__entry->from_idle)
);

TRACE_EVENT(cluster_exit,

	TP_PROTO(const char *name, int index, unsigned long sync_cpus,
		unsigned long child_cpus, bool from_idle),

	TP_ARGS(name, index, sync_cpus, child_cpus, from_idle),

	TP_STRUCT__entry(
		__string(name, name)
		__field(int, index)
		__field(unsigned long, sync_cpus)
		__field(unsigned long, child_cpus)
		__field(bool, from_idle)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->index = index;
		__entry->sync_cpus = sync_cpus;
		__entry->child_cpus = child_cpus;
		__entry->from_idle = from_idle;
	),

	TP_printk("cluster_name:%s idx:%d sync:0x%lx child:0x%lx idle:%d",
		__get_str(name),
		__entry->index,
		__entry->sync_cpus,
		__entry->child_cpus,
		__entry->from_idle)
);

TRACE_EVENT(cluster_pred_select,

	TP_PROTO(const char *name, int index, u32 sleep_us,
				u32 latency, int pred, u32 pred_us),

	TP_ARGS(name, index, sleep_us, latency, pred, pred_us),

	TP_STRUCT__entry(
		__string(name, name)
		__field(int, index)
		__field(u32, sleep_us)
		__field(u32, latency)
		__field(int, pred)
		__field(u32, pred_us)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->index = index;
		__entry->sleep_us = sleep_us;
		__entry->latency = latency;
		__entry->pred = pred;
		__entry->pred_us = pred_us;
	),

	TP_printk("name:%s idx:%d sleep_time:%u latency:%u pred:%d pred_us:%u",
		__get_str(name), __entry->index, __entry->sleep_us,
		__entry->latency, __entry->pred, __entry->pred_us)
);

TRACE_EVENT(cluster_pred_hist,

	TP_PROTO(const char *name, int idx, u32 resi,
					u32 sample, u32 tmr),

	TP_ARGS(name, idx, resi, sample, tmr),

	TP_STRUCT__entry(
		__string(name, name)
		__field(int, idx)
		__field(u32, resi)
		__field(u32, sample)
		__field(u32, tmr)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->idx = idx;
		__entry->resi = resi;
		__entry->sample = sample;
		__entry->tmr = tmr;
	),

	TP_printk("name:%s idx:%d resi:%u sample:%u tmr:%u",
		__get_str(name), __entry->idx, __entry->resi,
		__entry->sample, __entry->tmr)
);

TRACE_EVENT(ipi_wakeup_time,

	TP_PROTO(u64 wakeup),

	TP_ARGS(wakeup),

	TP_STRUCT__entry(
		__field(u64, wakeup)
	),

	TP_fast_assign(
		__entry->wakeup = wakeup;
	),

	TP_printk("wakeup:%llu", __entry->wakeup)
);

TRACE_EVENT(pre_pc_cb,

	TP_PROTO(int tzflag),

	TP_ARGS(tzflag),

	TP_STRUCT__entry(
		__field(int, tzflag)
	),

	TP_fast_assign(
		__entry->tzflag = tzflag;
	),

	TP_printk("tzflag:%d",
		__entry->tzflag
	)
);

#endif
#define TRACE_INCLUDE_FILE trace_msm_low_power
#include <trace/define_trace.h>
