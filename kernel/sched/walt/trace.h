/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM schedwalt

#if !defined(_TRACE_WALT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_WALT_H

#include <linux/tracepoint.h>

#include "walt.h"

struct rq;
struct group_cpu_time;
struct walt_task_struct;
struct walt_rq;
struct walt_related_thread_group;

extern const char *task_event_names[];

TRACE_EVENT(sched_update_pred_demand,

	TP_PROTO(struct task_struct *p, u32 runtime,
		 unsigned int pred_demand_scaled, int start,
		 int first, int final, struct walt_task_struct *wts),

	TP_ARGS(p, runtime, pred_demand_scaled, start, first, final, wts),

	TP_STRUCT__entry(
		__array(char,		comm, TASK_COMM_LEN)
		__field(pid_t,		pid)
		__field(unsigned int,	runtime)
		__field(unsigned int,	pred_demand_scaled)
		__array(u8,		bucket, NUM_BUSY_BUCKETS)
		__field(int,		cpu)
		__field(int,		start)
		__field(int,		first)
		__field(int,		final)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->runtime	= runtime;
		__entry->pred_demand_scaled	= pred_demand_scaled;
		memcpy(__entry->bucket, wts->busy_buckets,
					NUM_BUSY_BUCKETS * sizeof(u8));
		__entry->cpu		= task_cpu(p);
		__entry->start		= start;
		__entry->first		= first;
		__entry->final		= final;
	),

	TP_printk("%d (%s): runtime %u cpu %d pred_demand_scaled %u start %d first %d final %d (buckets: %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u)",
		__entry->pid, __entry->comm,
		__entry->runtime, __entry->cpu,
		__entry->pred_demand_scaled, __entry->start, __entry->first, __entry->final,
		__entry->bucket[0], __entry->bucket[1],
		__entry->bucket[2], __entry->bucket[3], __entry->bucket[4],
		__entry->bucket[5], __entry->bucket[6], __entry->bucket[7],
		__entry->bucket[8], __entry->bucket[9], __entry->bucket[10],
		__entry->bucket[11], __entry->bucket[12], __entry->bucket[13],
		__entry->bucket[14], __entry->bucket[15])
);

TRACE_EVENT(sched_update_history,

	TP_PROTO(struct rq *rq, struct task_struct *p, u32 runtime, int samples,
			enum task_event evt, struct walt_rq *wrq, struct walt_task_struct *wts),

	TP_ARGS(rq, p, runtime, samples, evt, wrq, wts),

	TP_STRUCT__entry(
		__array(char,			comm, TASK_COMM_LEN)
		__field(pid_t,			pid)
		__field(unsigned int,		runtime)
		__field(int,			samples)
		__field(enum task_event,	evt)
		__field(unsigned int,		demand)
		__field(unsigned int,		coloc_demand)
		__field(unsigned int,		pred_demand_scaled)
		__array(u32,			hist, RAVG_HIST_SIZE)
		__array(u16,			hist_util, RAVG_HIST_SIZE)
		__field(unsigned int,		nr_big_tasks)
		__field(int,			cpu)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->runtime	= runtime;
		__entry->samples	= samples;
		__entry->evt		= evt;
		__entry->demand		= wts->demand;
		__entry->coloc_demand	= wts->coloc_demand;
		__entry->pred_demand_scaled	= wts->pred_demand_scaled;
		memcpy(__entry->hist, wts->sum_history,
					RAVG_HIST_SIZE * sizeof(u32));
		memcpy(__entry->hist_util, wts->sum_history_util,
					RAVG_HIST_SIZE * sizeof(u16));
		__entry->nr_big_tasks	= wrq->walt_stats.nr_big_tasks;
		__entry->cpu		= rq->cpu;
	),

	TP_printk("%d (%s): runtime %u samples %d event %s demand %u (hist: %u %u %u %u %u) (hist_util: %u %u %u %u %u) coloc_demand %u pred_demand_scaled %u cpu %d nr_big %u",
		__entry->pid, __entry->comm,
		__entry->runtime, __entry->samples,
		task_event_names[__entry->evt],
		__entry->demand,
		__entry->hist[0], __entry->hist[1],
		__entry->hist[2], __entry->hist[3],
		__entry->hist[4],
		__entry->hist_util[0], __entry->hist_util[1],
		__entry->hist_util[2], __entry->hist_util[3],
		__entry->hist_util[4],
		__entry->coloc_demand, __entry->pred_demand_scaled,
		__entry->cpu, __entry->nr_big_tasks)
);

TRACE_EVENT(sched_get_task_cpu_cycles,

	TP_PROTO(int cpu, int event, u64 cycles,
			u64 exec_time, struct task_struct *p),

	TP_ARGS(cpu, event, cycles, exec_time, p),

	TP_STRUCT__entry(
		__field(int,	cpu)
		__field(int,	event)
		__field(u64,	cycles)
		__field(u64,	exec_time)
		__field(u32,	freq)
		__field(u32,	legacy_freq)
		__field(u32,	max_freq)
		__field(pid_t,	pid)
		__array(char,	comm, TASK_COMM_LEN)
	),

	TP_fast_assign(
		__entry->cpu		= cpu;
		__entry->event		= event;
		__entry->cycles		= cycles;
		__entry->exec_time	= exec_time;
		__entry->freq		= cpu_cycles_to_freq(cycles, exec_time);
		__entry->legacy_freq	= sched_cpu_legacy_freq(cpu);
		__entry->max_freq	= cpu_max_freq(cpu);
		__entry->pid		= p->pid;
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
	),

	TP_printk("cpu=%d event=%d cycles=%llu exec_time=%llu freq=%u legacy_freq=%u max_freq=%u task=%d (%s)",
		  __entry->cpu, __entry->event, __entry->cycles,
		  __entry->exec_time, __entry->freq, __entry->legacy_freq,
		  __entry->max_freq, __entry->pid, __entry->comm)
);

TRACE_EVENT(sched_update_task_ravg,

	TP_PROTO(struct task_struct *p, struct rq *rq, enum task_event evt,
		 u64 wallclock, u64 irqtime,
		 struct group_cpu_time *cpu_time, struct walt_rq *wrq,
		 struct walt_task_struct *wts, u64 walt_irq_work_lastq_ws),

	TP_ARGS(p, rq, evt, wallclock, irqtime, cpu_time, wrq, wts, walt_irq_work_lastq_ws),

	TP_STRUCT__entry(
		__array(char,			comm, TASK_COMM_LEN)
		__field(pid_t,			pid)
		__field(pid_t,			cur_pid)
		__field(unsigned int,		cur_freq)
		__field(u64,			wallclock)
		__field(u64,			mark_start)
		__field(u64,			delta_m)
		__field(u64,			win_start)
		__field(u64,			delta)
		__field(u64,			irqtime)
		__field(enum task_event,	evt)
		__field(unsigned int,		demand)
		__field(unsigned int,		coloc_demand)
		__field(unsigned int,		sum)
		__field(int,			cpu)
		__field(unsigned int,		pred_demand_scaled)
		__field(u64,			rq_cs)
		__field(u64,			rq_ps)
		__field(u64,			grp_cs)
		__field(u64,			grp_ps)
		__field(u64,			grp_nt_cs)
		__field(u64,			grp_nt_ps)
		__field(u32,			curr_window)
		__field(u32,			prev_window)
		__dynamic_array(u32,		curr_sum, nr_cpu_ids)
		__dynamic_array(u32,		prev_sum, nr_cpu_ids)
		__field(u64,			nt_cs)
		__field(u64,			nt_ps)
		__field(u64,			active_time)
		__field(u32,			curr_top)
		__field(u32,			prev_top)
		__field(u64,			walt_irq_work_lastq_ws)
	),

	TP_fast_assign(
		__entry->wallclock	= wallclock;
		__entry->win_start	= wrq->window_start;
		__entry->delta		= (wallclock - wrq->window_start);
		__entry->evt		= evt;
		__entry->cpu		= rq->cpu;
		__entry->cur_pid	= rq->curr->pid;
		__entry->cur_freq	= wrq->task_exec_scale;
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->mark_start	= wts->mark_start;
		__entry->delta_m	= (wallclock - wts->mark_start);
		__entry->demand		= wts->demand;
		__entry->coloc_demand	= wts->coloc_demand;
		__entry->sum		= wts->sum;
		__entry->irqtime	= irqtime;
		__entry->pred_demand_scaled	= wts->pred_demand_scaled;
		__entry->rq_cs		= wrq->curr_runnable_sum;
		__entry->rq_ps		= wrq->prev_runnable_sum;
		__entry->grp_cs		= cpu_time ? cpu_time->curr_runnable_sum : 0;
		__entry->grp_ps		= cpu_time ? cpu_time->prev_runnable_sum : 0;
		__entry->grp_nt_cs	= cpu_time ?
					cpu_time->nt_curr_runnable_sum : 0;
		__entry->grp_nt_ps	= cpu_time ?
					cpu_time->nt_prev_runnable_sum : 0;
		__entry->curr_window	= wts->curr_window;
		__entry->prev_window	= wts->prev_window;
		__window_data(__get_dynamic_array(curr_sum),
						wts->curr_window_cpu);
		__window_data(__get_dynamic_array(prev_sum),
						wts->prev_window_cpu);
		__entry->nt_cs		= wrq->nt_curr_runnable_sum;
		__entry->nt_ps		= wrq->nt_prev_runnable_sum;
		__entry->active_time	= wts->active_time;
		__entry->curr_top	= wrq->curr_top;
		__entry->prev_top	= wrq->prev_top;
		__entry->walt_irq_work_lastq_ws	= walt_irq_work_lastq_ws;
	),

	TP_printk("wc %llu ws %llu delta %llu event %s cpu %d cur_freq %u cur_pid %d task %d (%s) ms %llu delta %llu demand %u coloc_demand: %u sum %u irqtime %llu pred_demand_scaled %u rq_cs %llu rq_ps %llu cur_window %u (%s) prev_window %u (%s) nt_cs %llu nt_ps %llu active_time %u grp_cs %lld grp_ps %lld, grp_nt_cs %llu, grp_nt_ps: %llu curr_top %u prev_top %u global_ws %llu",
		__entry->wallclock, __entry->win_start, __entry->delta,
		task_event_names[__entry->evt], __entry->cpu,
		__entry->cur_freq, __entry->cur_pid,
		__entry->pid, __entry->comm, __entry->mark_start,
		__entry->delta_m, __entry->demand, __entry->coloc_demand,
		__entry->sum, __entry->irqtime, __entry->pred_demand_scaled,
		__entry->rq_cs, __entry->rq_ps, __entry->curr_window,
		__window_print(p, __get_dynamic_array(curr_sum), nr_cpu_ids),
		__entry->prev_window,
		__window_print(p, __get_dynamic_array(prev_sum), nr_cpu_ids),
		__entry->nt_cs, __entry->nt_ps,
		__entry->active_time, __entry->grp_cs,
		__entry->grp_ps, __entry->grp_nt_cs, __entry->grp_nt_ps,
		__entry->curr_top, __entry->prev_top, __entry->walt_irq_work_lastq_ws)
);

TRACE_EVENT(sched_update_task_ravg_mini,

	TP_PROTO(struct task_struct *p, struct rq *rq, enum task_event evt,
		 u64 wallclock, u64 irqtime,
		 struct group_cpu_time *cpu_time, struct walt_rq *wrq,
		 struct walt_task_struct *wts, u64 walt_irq_work_lastq_ws),

	TP_ARGS(p, rq, evt, wallclock, irqtime, cpu_time, wrq, wts, walt_irq_work_lastq_ws),

	TP_STRUCT__entry(
		__array(char,			comm, TASK_COMM_LEN)
		__field(pid_t,			pid)
		__field(u64,			wallclock)
		__field(u64,			mark_start)
		__field(u64,			delta_m)
		__field(u64,			win_start)
		__field(u64,			delta)
		__field(enum task_event,	evt)
		__field(unsigned int,		demand)
		__field(int,			cpu)
		__field(u64,			rq_cs)
		__field(u64,			rq_ps)
		__field(u64,			grp_cs)
		__field(u64,			grp_ps)
		__field(u32,			curr_window)
		__field(u32,			prev_window)
		__field(u64,			walt_irq_work_lastq_ws)
	),

	TP_fast_assign(
		__entry->wallclock	= wallclock;
		__entry->win_start	= wrq->window_start;
		__entry->delta		= (wallclock - wrq->window_start);
		__entry->evt		= evt;
		__entry->cpu		= rq->cpu;
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->mark_start	= wts->mark_start;
		__entry->delta_m	= (wallclock - wts->mark_start);
		__entry->demand		= wts->demand;
		__entry->rq_cs		= wrq->curr_runnable_sum;
		__entry->rq_ps		= wrq->prev_runnable_sum;
		__entry->grp_cs		= cpu_time ? cpu_time->curr_runnable_sum : 0;
		__entry->grp_ps		= cpu_time ? cpu_time->prev_runnable_sum : 0;
		__entry->curr_window	= wts->curr_window;
		__entry->prev_window	= wts->prev_window;
		__entry->walt_irq_work_lastq_ws	= walt_irq_work_lastq_ws;
	),

	TP_printk("wc %llu ws %llu delta %llu event %s cpu %d task %d (%s) ms %llu delta %llu demand %u rq_cs %llu rq_ps %llu cur_window %u prev_window %u grp_cs %lld grp_ps %lld global_ws %llu",
		__entry->wallclock, __entry->win_start, __entry->delta,
		task_event_names[__entry->evt], __entry->cpu,
		__entry->pid, __entry->comm, __entry->mark_start,
		__entry->delta_m, __entry->demand,
		__entry->rq_cs, __entry->rq_ps, __entry->curr_window,
		__entry->prev_window, __entry->grp_cs, __entry->grp_ps,
		__entry->walt_irq_work_lastq_ws)
);

struct migration_sum_data;
extern const char *migrate_type_names[];

TRACE_EVENT(sched_set_preferred_cluster,

	TP_PROTO(struct walt_related_thread_group *grp, u64 total_demand,
		bool prev_skip_min),

	TP_ARGS(grp, total_demand, prev_skip_min),

	TP_STRUCT__entry(
		__field(int,		id)
		__field(u64,		total_demand)
		__field(bool,		skip_min)
		__field(bool,		prev_skip_min)
		__field(u64,		start_ktime_ts)
		__field(u64,		last_update)
		__field(unsigned int,	sysctl_sched_hyst_min_coloc_ns)
		__field(u64,		downmigrate_ts)
	),

	TP_fast_assign(
		__entry->id		= grp->id;
		__entry->total_demand	= total_demand;
		__entry->skip_min	= grp->skip_min;
		__entry->prev_skip_min	= prev_skip_min;
		__entry->start_ktime_ts	= grp->start_ktime_ts;
		__entry->last_update	= grp->last_update;
		__entry->sysctl_sched_hyst_min_coloc_ns = sysctl_sched_hyst_min_coloc_ns;
		__entry->downmigrate_ts	= grp->downmigrate_ts;
	),

	TP_printk("group_id %d total_demand %llu skip_min %d prev_skip_min %d start_ktime_ts %llu last_update %llu min_coloc_ns %u downmigrate_ts %llu",
			__entry->id, __entry->total_demand,
			__entry->skip_min, __entry->prev_skip_min,
			__entry->start_ktime_ts, __entry->last_update,
			__entry->sysctl_sched_hyst_min_coloc_ns,
			__entry->downmigrate_ts)
);

TRACE_EVENT(sched_migration_update_sum,

	TP_PROTO(struct task_struct *p, enum migrate_types migrate_type,
							struct rq *rq),

	TP_ARGS(p, migrate_type, rq),

	TP_STRUCT__entry(
		__field(int,			tcpu)
		__field(int,			pid)
		__field(enum migrate_types,	migrate_type)
		__field(s64,			src_cs)
		__field(s64,			src_ps)
		__field(s64,			dst_cs)
		__field(s64,			dst_ps)
		__field(s64,			src_nt_cs)
		__field(s64,			src_nt_ps)
		__field(s64,			dst_nt_cs)
		__field(s64,			dst_nt_ps)
	),

	TP_fast_assign(
		__entry->tcpu		= task_cpu(p);
		__entry->pid		= p->pid;
		__entry->migrate_type	= migrate_type;
		__entry->src_cs		= __get_update_sum(rq, migrate_type,
							   true, false, true);
		__entry->src_ps		= __get_update_sum(rq, migrate_type,
							   true, false, false);
		__entry->dst_cs		= __get_update_sum(rq, migrate_type,
							   false, false, true);
		__entry->dst_ps		= __get_update_sum(rq, migrate_type,
							   false, false, false);
		__entry->src_nt_cs	= __get_update_sum(rq, migrate_type,
							   true, true, true);
		__entry->src_nt_ps	= __get_update_sum(rq, migrate_type,
							   true, true, false);
		__entry->dst_nt_cs	= __get_update_sum(rq, migrate_type,
							   false, true, true);
		__entry->dst_nt_ps	= __get_update_sum(rq, migrate_type,
							   false, true, false);
	),

	TP_printk("pid %d task_cpu %d migrate_type %s src_cs %llu src_ps %llu dst_cs %lld dst_ps %lld src_nt_cs %llu src_nt_ps %llu dst_nt_cs %lld dst_nt_ps %lld",
		__entry->pid, __entry->tcpu,
		migrate_type_names[__entry->migrate_type],
		__entry->src_cs, __entry->src_ps, __entry->dst_cs,
		__entry->dst_ps, __entry->src_nt_cs, __entry->src_nt_ps,
		__entry->dst_nt_cs, __entry->dst_nt_ps)
);

TRACE_EVENT(sched_set_boost,

	TP_PROTO(int type),

	TP_ARGS(type),

	TP_STRUCT__entry(
		__field(int, type)
	),

	TP_fast_assign(
		__entry->type = type;
	),

	TP_printk("type %d", __entry->type)
);

TRACE_EVENT(sched_load_to_gov,

	TP_PROTO(struct rq *rq, u64 aggr_grp_load, u32 tt_load,
		int freq_aggr, u64 load, int policy,
		int big_task_rotation,
		unsigned int user_hint,
		struct walt_rq *wrq,
		unsigned int reasons),
	TP_ARGS(rq, aggr_grp_load, tt_load, freq_aggr, load, policy,
		big_task_rotation, user_hint, wrq, reasons),

	TP_STRUCT__entry(
		__field(int,	cpu)
		__field(int,	policy)
		__field(int,	ed_task_pid)
		__field(u64,	aggr_grp_load)
		__field(int,	freq_aggr)
		__field(u64,	tt_load)
		__field(u64,	rq_ps)
		__field(u64,	grp_rq_ps)
		__field(u64,	nt_ps)
		__field(u64,	grp_nt_ps)
		__field(u64,	pl)
		__field(u64,	load)
		__field(int,	big_task_rotation)
		__field(unsigned int, user_hint)
		__field(unsigned int, reasons)
	),

	TP_fast_assign(
		__entry->cpu		= cpu_of(rq);
		__entry->policy		= policy;
		__entry->ed_task_pid	=
				wrq->ed_task ? wrq->ed_task->pid : -1;
		__entry->aggr_grp_load	= aggr_grp_load;
		__entry->freq_aggr	= freq_aggr;
		__entry->tt_load	= tt_load;
		__entry->rq_ps		= wrq->prev_runnable_sum;
		__entry->grp_rq_ps	= wrq->grp_time.prev_runnable_sum;
		__entry->nt_ps		= wrq->nt_prev_runnable_sum;
		__entry->grp_nt_ps	= wrq->grp_time.nt_prev_runnable_sum;
		__entry->pl		= wrq->walt_stats.pred_demands_sum_scaled;
		__entry->load		= load;
		__entry->big_task_rotation	= big_task_rotation;
		__entry->user_hint	= user_hint;
		__entry->reasons	= reasons;
	),

	TP_printk("cpu=%d policy=%d ed_task_pid=%d aggr_grp_load=%llu freq_aggr=%d tt_load=%llu rq_ps=%llu grp_rq_ps=%llu nt_ps=%llu grp_nt_ps=%llu pl=%llu load=%llu big_task_rotation=%d user_hint=%u reasons=0x%x",
		__entry->cpu, __entry->policy, __entry->ed_task_pid,
		__entry->aggr_grp_load, __entry->freq_aggr,
		__entry->tt_load, __entry->rq_ps, __entry->grp_rq_ps,
		__entry->nt_ps, __entry->grp_nt_ps, __entry->pl, __entry->load,
		__entry->big_task_rotation, __entry->user_hint, __entry->reasons)
);

TRACE_EVENT(core_ctl_eval_need,

	TP_PROTO(unsigned int cpu, unsigned int last_need,
		unsigned int new_need, unsigned int active_cpus,
		unsigned int adj_now, unsigned int adj_possible,
		unsigned int updated, s64 need_ts),
	TP_ARGS(cpu, last_need, new_need, active_cpus, adj_now, adj_possible, updated, need_ts),
	TP_STRUCT__entry(
		__field(u32, cpu)
		__field(u32, last_need)
		__field(u32, new_need)
		__field(u32, active_cpus)
		__field(u32, adj_now)
		__field(u32, adj_possible)
		__field(u32, updated)
		__field(s64, need_ts)
	),
	TP_fast_assign(
		__entry->cpu		= cpu;
		__entry->last_need	= last_need;
		__entry->new_need	= new_need;
		__entry->active_cpus	= active_cpus;
		__entry->adj_now	= adj_now;
		__entry->adj_possible	= adj_possible;
		__entry->updated	= updated;
		__entry->need_ts	= need_ts;
	),
	TP_printk("cpu=%u last_need=%u new_need=%u active_cpus=%u adj_now=%u adj_possible=%u updated=%u need_ts=%llu",
		  __entry->cpu,	__entry->last_need, __entry->new_need,
		  __entry->active_cpus, __entry->adj_now, __entry->adj_possible,
		  __entry->updated, __entry->need_ts)
);

TRACE_EVENT(core_ctl_set_boost,

	TP_PROTO(u32 refcount, s32 ret),
	TP_ARGS(refcount, ret),
	TP_STRUCT__entry(
		__field(u32, refcount)
		__field(s32, ret)
	),
	TP_fast_assign(
		__entry->refcount	= refcount;
		__entry->ret		= ret;
	),
	TP_printk("refcount=%u, ret=%d", __entry->refcount, __entry->ret)
);

TRACE_EVENT(core_ctl_update_nr_need,

	TP_PROTO(int cpu, int nr_need, int nr_misfit_need, int nrrun,
		 int max_nr, int strict_nrrun, int nr_assist_need, int nr_misfit_assist_need,
		 int nr_assist, int nr_busy),

	TP_ARGS(cpu, nr_need, nr_misfit_need, nrrun,
		max_nr, strict_nrrun, nr_assist_need, nr_misfit_assist_need,
		nr_assist, nr_busy),

	TP_STRUCT__entry(
		__field(int, cpu)
		__field(int, nr_need)
		__field(int, nr_misfit_need)
		__field(int, nrrun)
		__field(int, max_nr)
		__field(int, strict_nrrun)
		__field(int, nr_assist_need)
		__field(int, nr_misfit_assist_need)
		__field(int, nr_assist)
		__field(int, nr_busy)
	),

	TP_fast_assign(
		__entry->cpu			= cpu;
		__entry->nr_need		= nr_need;
		__entry->nr_misfit_need		= nr_misfit_need;
		__entry->nrrun			= nrrun;
		__entry->max_nr			= max_nr;
		__entry->strict_nrrun		= strict_nrrun;
		__entry->nr_assist_need		= nr_assist_need;
		__entry->nr_misfit_assist_need	= nr_misfit_assist_need;
		__entry->nr_assist		= nr_assist;
		__entry->nr_busy		= nr_busy;
	),

	TP_printk("cpu=%d nr_need=%d nr_misfit_need=%d nrrun=%d max_nr=%d strict_nrrun=%d nr_assist_need=%d nr_misfit_assist_need=%d nr_assist=%d nr_busy=%d",
		__entry->cpu, __entry->nr_need, __entry->nr_misfit_need, __entry->nrrun,
		__entry->max_nr, __entry->strict_nrrun, __entry->nr_assist_need,
		__entry->nr_misfit_assist_need, __entry->nr_assist, __entry->nr_busy)
);

TRACE_EVENT(core_ctl_notif_data,

	TP_PROTO(u32 nr_big, u32 ta_load, u32 *ta_util, u32 *cur_cap),

	TP_ARGS(nr_big, ta_load, ta_util, cur_cap),

	TP_STRUCT__entry(
		__field(u32, nr_big)
		__field(u32, ta_load)
		__array(u32, ta_util, MAX_CLUSTERS)
		__array(u32, cur_cap, MAX_CLUSTERS)
	),

	TP_fast_assign(
		__entry->nr_big		= nr_big;
		__entry->ta_load	= ta_load;
		memcpy(__entry->ta_util, ta_util, MAX_CLUSTERS * sizeof(u32));
		memcpy(__entry->cur_cap, cur_cap, MAX_CLUSTERS * sizeof(u32));
	),

	TP_printk("nr_big=%u ta_load=%u ta_util=(%u %u %u) cur_cap=(%u %u %u)",
		  __entry->nr_big, __entry->ta_load,
		  __entry->ta_util[0], __entry->ta_util[1],
		  __entry->ta_util[2], __entry->cur_cap[0],
		  __entry->cur_cap[1], __entry->cur_cap[2])
);

/*
 * Tracepoint for sched_get_nr_running_avg
 */
TRACE_EVENT(sched_get_nr_running_avg,

	TP_PROTO(int cpu, int nr, int nr_misfit, int nr_max, int nr_scaled),

	TP_ARGS(cpu, nr, nr_misfit, nr_max, nr_scaled),

	TP_STRUCT__entry(
		__field(int, cpu)
		__field(int, nr)
		__field(int, nr_misfit)
		__field(int, nr_max)
		__field(int, nr_scaled)
	),

	TP_fast_assign(
		__entry->cpu		= cpu;
		__entry->nr		= nr;
		__entry->nr_misfit	= nr_misfit;
		__entry->nr_max		= nr_max;
		__entry->nr_scaled	= nr_scaled;
	),

	TP_printk("cpu=%d nr=%d nr_misfit=%d nr_max=%d nr_scaled=%d",
		__entry->cpu, __entry->nr, __entry->nr_misfit, __entry->nr_max,
		__entry->nr_scaled)
);

TRACE_EVENT(sched_busy_hyst_time,

	TP_PROTO(int cpu, u64 hyst_time, unsigned long nr_run,
		unsigned long cpu_util, u64 busy_hyst_time,
		u64 coloc_hyst_time, u64 util_hyst_time),

	TP_ARGS(cpu, hyst_time, nr_run, cpu_util, busy_hyst_time,
		coloc_hyst_time, util_hyst_time),

	TP_STRUCT__entry(
		__field(int, cpu)
		__field(u64, hyst_time)
		__field(unsigned long, nr_run)
		__field(unsigned long, cpu_util)
		__field(u64, busy_hyst_time)
		__field(u64, coloc_hyst_time)
		__field(u64, util_hyst_time)
	),

	TP_fast_assign(
		__entry->cpu = cpu;
		__entry->hyst_time = hyst_time;
		__entry->nr_run = nr_run;
		__entry->cpu_util = cpu_util;
		__entry->busy_hyst_time = busy_hyst_time;
		__entry->coloc_hyst_time = coloc_hyst_time;
		__entry->util_hyst_time = util_hyst_time;
	),

	TP_printk("cpu=%d hyst_time=%llu nr_run=%lu cpu_util=%lu busy_hyst_time=%llu coloc_hyst_time=%llu util_hyst_time=%llu",
		__entry->cpu, __entry->hyst_time, __entry->nr_run,
		__entry->cpu_util, __entry->busy_hyst_time,
		__entry->coloc_hyst_time, __entry->util_hyst_time)
);

TRACE_EVENT(sched_ravg_window_change,

	TP_PROTO(unsigned int sched_ravg_window, unsigned int new_sched_ravg_window
		, u64 change_time),

	TP_ARGS(sched_ravg_window, new_sched_ravg_window, change_time),

	TP_STRUCT__entry(
		__field(unsigned int, sched_ravg_window)
		__field(unsigned int, new_sched_ravg_window)
		__field(u64, change_time)
	),

	TP_fast_assign(
		__entry->sched_ravg_window	= sched_ravg_window;
		__entry->new_sched_ravg_window	= new_sched_ravg_window;
		__entry->change_time		= change_time;
	),

	TP_printk("from=%u to=%u at=%lu",
		__entry->sched_ravg_window, __entry->new_sched_ravg_window,
		__entry->change_time)
);

TRACE_EVENT(waltgov_util_update,
	    TP_PROTO(int cpu,
		     unsigned long util, unsigned long avg_cap,
		     unsigned long max_cap, unsigned long nl, unsigned long pl,
		     unsigned int rtgb, unsigned int flags),
	    TP_ARGS(cpu, util, avg_cap, max_cap, nl, pl, rtgb, flags),
	    TP_STRUCT__entry(
		    __field(int, cpu)
		    __field(unsigned long, util)
		    __field(unsigned long, avg_cap)
		    __field(unsigned long, max_cap)
		    __field(unsigned long, nl)
		    __field(unsigned long, pl)
		    __field(unsigned int, rtgb)
		    __field(unsigned int, flags)
	    ),
	    TP_fast_assign(
		    __entry->cpu	= cpu;
		    __entry->util	= util;
		    __entry->avg_cap	= avg_cap;
		    __entry->max_cap	= max_cap;
		    __entry->nl		= nl;
		    __entry->pl		= pl;
		    __entry->rtgb	= rtgb;
		    __entry->flags	= flags;
	    ),
	    TP_printk("cpu=%d util=%lu avg_cap=%lu max_cap=%lu nl=%lu pl=%lu rtgb=%u flags=0x%x",
		      __entry->cpu, __entry->util, __entry->avg_cap,
		      __entry->max_cap, __entry->nl,
		      __entry->pl, __entry->rtgb, __entry->flags)
);

TRACE_EVENT(waltgov_next_freq,
	    TP_PROTO(unsigned int cpu, unsigned long util, unsigned long max, unsigned int raw_freq,
		     unsigned int freq, unsigned int policy_min_freq, unsigned int policy_max_freq,
		     unsigned int cached_raw_freq, bool need_freq_update, bool thermal_isolated,
		     unsigned int driving_cpu, unsigned int reason),
	    TP_ARGS(cpu, util, max, raw_freq, freq, policy_min_freq, policy_max_freq,
		    cached_raw_freq, need_freq_update, thermal_isolated, driving_cpu, reason),
	    TP_STRUCT__entry(
		    __field(unsigned int, cpu)
		    __field(unsigned long, util)
		    __field(unsigned long, max)
		    __field(unsigned int, raw_freq)
		    __field(unsigned int, freq)
		    __field(unsigned int, policy_min_freq)
		    __field(unsigned int, policy_max_freq)
		    __field(unsigned int, cached_raw_freq)
		    __field(bool, need_freq_update)
		    __field(bool, thermal_isolated)
		    __field(unsigned int, rt_util)
		    __field(unsigned int, driving_cpu)
		    __field(unsigned int, reason)
	    ),
	    TP_fast_assign(
		    __entry->cpu		= cpu;
		    __entry->util		= util;
		    __entry->max		= max;
		    __entry->raw_freq		= raw_freq;
		    __entry->freq		= freq;
		    __entry->policy_min_freq	= policy_min_freq;
		    __entry->policy_max_freq	= policy_max_freq;
		    __entry->cached_raw_freq	= cached_raw_freq;
		    __entry->need_freq_update	= need_freq_update;
		    __entry->thermal_isolated	= thermal_isolated;
		    __entry->rt_util		= cpu_util_rt(cpu_rq(cpu));
		    __entry->driving_cpu	= driving_cpu;
		    __entry->reason		= reason;
	    ),
	    TP_printk("cpu=%u util=%lu max=%lu raw_freq=%lu freq=%u policy_min_freq=%u policy_max_freq=%u cached_raw_freq=%u need_update=%d thermal_isolated=%d rt_util=%u driv_cpu=%u reason=0x%x",
		      __entry->cpu,
		      __entry->util,
		      __entry->max,
		      __entry->raw_freq,
		      __entry->freq,
		      __entry->policy_min_freq,
		      __entry->policy_max_freq,
		      __entry->cached_raw_freq,
		      __entry->need_freq_update,
		      __entry->thermal_isolated,
		      __entry->rt_util,
		      __entry->driving_cpu,
		      __entry->reason)
);

TRACE_EVENT(walt_active_load_balance,

	TP_PROTO(struct task_struct *p, int prev_cpu, int new_cpu, struct walt_task_struct *wts),

	TP_ARGS(p, prev_cpu, new_cpu, wts),

	TP_STRUCT__entry(
		__field(pid_t, pid)
		__field(bool, misfit)
		__field(int, prev_cpu)
		__field(int, new_cpu)
	),

	TP_fast_assign(
		__entry->pid		= p->pid;
		__entry->misfit		= wts->misfit;
		__entry->prev_cpu	= prev_cpu;
		__entry->new_cpu	= new_cpu;
	),

	TP_printk("pid=%d misfit=%d prev_cpu=%d new_cpu=%d\n",
			__entry->pid, __entry->misfit, __entry->prev_cpu,
			__entry->new_cpu)
);

TRACE_EVENT(walt_find_busiest_queue,

	TP_PROTO(int dst_cpu, int busiest_cpu, unsigned long src_mask),

	TP_ARGS(dst_cpu, busiest_cpu, src_mask),

	TP_STRUCT__entry(
		__field(int, dst_cpu)
		__field(int, busiest_cpu)
		__field(unsigned long, src_mask)
	),

	TP_fast_assign(
		__entry->dst_cpu	= dst_cpu;
		__entry->busiest_cpu	= busiest_cpu;
		__entry->src_mask	= src_mask;
	),

	TP_printk("dst_cpu=%d busiest_cpu=%d src_mask=%lx\n",
			__entry->dst_cpu, __entry->busiest_cpu,
			__entry->src_mask)
);

TRACE_EVENT(walt_nohz_balance_kick,

	TP_PROTO(struct rq *rq),

	TP_ARGS(rq),

	TP_STRUCT__entry(
		__field(int, cpu)
		__field(unsigned int, nr_running)
		__field(unsigned int, nr_cfs_running)
	),

	TP_fast_assign(
		__entry->cpu		= rq->cpu;
		__entry->nr_running	= rq->nr_running;
		__entry->nr_cfs_running	= rq->cfs.h_nr_running;
	),

	TP_printk("cpu=%d nr_running=%u nr_cfs_running=%u",
			__entry->cpu, __entry->nr_running,
			__entry->nr_cfs_running)
);

TRACE_EVENT(walt_newidle_balance,

	TP_PROTO(int this_cpu, int busy_cpu, int pulled, bool help_min_cap, bool enough_idle,
		struct task_struct *p),

	TP_ARGS(this_cpu, busy_cpu, pulled, help_min_cap, enough_idle, p),

	TP_STRUCT__entry(
		__field(int, cpu)
		__field(int, busy_cpu)
		__field(int, pulled)
		__field(unsigned int, nr_running)
		__field(unsigned int, rt_nr_running)
		__field(int, nr_iowait)
		__field(bool, help_min_cap)
		__field(u64, avg_idle)
		__field(bool, enough_idle)
		__field(int, overload)
		__field(int, pid)
	),

	TP_fast_assign(
		__entry->cpu		= this_cpu;
		__entry->busy_cpu	= busy_cpu;
		__entry->pulled		= pulled;
		__entry->nr_running	= cpu_rq(this_cpu)->nr_running;
		__entry->rt_nr_running	= cpu_rq(this_cpu)->rt.rt_nr_running;
		__entry->nr_iowait	= atomic_read(&(cpu_rq(this_cpu)->nr_iowait));
		__entry->help_min_cap	= help_min_cap;
		__entry->avg_idle	= cpu_rq(this_cpu)->avg_idle;
		__entry->enough_idle	= enough_idle;
		__entry->overload	= cpu_rq(this_cpu)->rd->overload;
		__entry->pid		= p ? p->pid : -1;
	),

	TP_printk("cpu=%d busy_cpu=%d pulled=%d nr_running=%u rt_nr_running=%u nr_iowait=%d help_min_cap=%d avg_idle=%llu enough_idle=%d overload=%d pid=%d",
			__entry->cpu, __entry->busy_cpu, __entry->pulled,
			__entry->nr_running, __entry->rt_nr_running,
			__entry->nr_iowait, __entry->help_min_cap,
			__entry->avg_idle, __entry->enough_idle,
			__entry->overload, __entry->pid)
);

TRACE_EVENT(walt_lb_cpu_util,

	TP_PROTO(int cpu, struct walt_rq *wrq),

	TP_ARGS(cpu, wrq),

	TP_STRUCT__entry(
		__field(int, cpu)
		__field(unsigned int, nr_running)
		__field(unsigned int, cfs_nr_running)
		__field(unsigned int, nr_big)
		__field(unsigned int, nr_rtg_high_prio_tasks)
		__field(unsigned int, cpu_util)
		__field(unsigned int, capacity_orig)
	),

	TP_fast_assign(
		__entry->cpu			= cpu;
		__entry->nr_running		= cpu_rq(cpu)->nr_running;
		__entry->cfs_nr_running		= cpu_rq(cpu)->cfs.h_nr_running;
		__entry->nr_big			= wrq->walt_stats.nr_big_tasks;
		__entry->nr_rtg_high_prio_tasks	= walt_nr_rtg_high_prio(cpu);
		__entry->cpu_util		= cpu_util(cpu);
		__entry->capacity_orig		= capacity_orig_of(cpu);
	),

	TP_printk("cpu=%d nr_running=%u cfs_nr_running=%u nr_big=%u nr_rtg_hp=%u cpu_util=%u capacity_orig=%u",
		__entry->cpu, __entry->nr_running, __entry->cfs_nr_running,
		__entry->nr_big, __entry->nr_rtg_high_prio_tasks,
		__entry->cpu_util, __entry->capacity_orig)
);

TRACE_EVENT(sched_cpu_util,

	TP_PROTO(int cpu, struct cpumask *lowest_mask),

	TP_ARGS(cpu, lowest_mask),

	TP_STRUCT__entry(
		__field(unsigned int,	cpu)
		__field(unsigned int,	nr_running)
		__field(long,		cpu_util)
		__field(long,		cpu_util_cum)
		__field(unsigned long,	capacity_curr)
		__field(unsigned long,	capacity)
		__field(unsigned long,	capacity_orig)
		__field(unsigned int,	idle_exit_latency)
		__field(u64,		irqload)
		__field(int,		online)
		__field(int,		inactive)
		__field(int,		halted)
		__field(int,		reserved)
		__field(int,		high_irq_load)
		__field(unsigned int,	nr_rtg_high_prio_tasks)
		__field(u64,	prs_gprs)
		__field(unsigned int,	lowest_mask)
		__field(unsigned long,	thermal_pressure)
	),

	TP_fast_assign(
		struct walt_rq *wrq = &per_cpu(walt_rq, cpu);
		__entry->cpu		= cpu;
		__entry->nr_running	= cpu_rq(cpu)->nr_running;
		__entry->cpu_util	= cpu_util(cpu);
		__entry->cpu_util_cum	= cpu_util_cum(cpu);
		__entry->capacity_curr	= capacity_curr_of(cpu);
		__entry->capacity	= capacity_of(cpu);
		__entry->capacity_orig	= capacity_orig_of(cpu);
		__entry->idle_exit_latency	= walt_get_idle_exit_latency(cpu_rq(cpu));
		__entry->irqload		= sched_irqload(cpu);
		__entry->online			= cpu_online(cpu);
		__entry->inactive		= !cpu_active(cpu);
		__entry->halted			= (cpu_halted(cpu)<<1) + cpu_partial_halted(cpu);
		__entry->reserved		= is_reserved(cpu);
		__entry->high_irq_load		= sched_cpu_high_irqload(cpu);
		__entry->nr_rtg_high_prio_tasks	= walt_nr_rtg_high_prio(cpu);
		__entry->prs_gprs	= wrq->prev_runnable_sum + wrq->grp_time.prev_runnable_sum;
		if (!lowest_mask)
			__entry->lowest_mask	= 0;
		else
			__entry->lowest_mask	= cpumask_bits(lowest_mask)[0];
		__entry->thermal_pressure	= arch_scale_thermal_pressure(cpu);
	),

	TP_printk("cpu=%d nr_running=%d cpu_util=%ld cpu_util_cum=%ld capacity_curr=%lu capacity=%lu capacity_orig=%lu idle_exit_latency=%u irqload=%llu online=%u, inactive=%u, halted=%u, reserved=%u, high_irq_load=%u nr_rtg_hp=%u prs_gprs=%llu lowest_mask=0x%x thermal_pressure=%llu",
		__entry->cpu, __entry->nr_running, __entry->cpu_util,
		__entry->cpu_util_cum, __entry->capacity_curr,
		__entry->capacity, __entry->capacity_orig,
		__entry->idle_exit_latency, __entry->irqload, __entry->online,
		__entry->inactive, __entry->halted, __entry->reserved, __entry->high_irq_load,
		__entry->nr_rtg_high_prio_tasks, __entry->prs_gprs,
		__entry->lowest_mask, __entry->thermal_pressure)
);

TRACE_EVENT(sched_compute_energy,

	TP_PROTO(struct task_struct *p, int eval_cpu,
		unsigned long eval_energy,
		unsigned long prev_energy,
		unsigned long best_energy,
		unsigned long best_energy_cpu,
		struct compute_energy_output *o),

	TP_ARGS(p, eval_cpu, eval_energy, prev_energy, best_energy,
		best_energy_cpu, o),

	TP_STRUCT__entry(
		__field(int,		pid)
		__array(char,		comm, TASK_COMM_LEN)
		__field(unsigned long,	util)
		__field(int,		prev_cpu)
		__field(unsigned long,	prev_energy)
		__field(int,		eval_cpu)
		__field(unsigned long,	eval_energy)
		__field(int,		best_energy_cpu)
		__field(unsigned long,	best_energy)
		__field(unsigned int,	cluster_first_cpu0)
		__field(unsigned int,	cluster_first_cpu1)
		__field(unsigned int,	cluster_first_cpu2)
		__field(unsigned long,	s0)
		__field(unsigned long,	s1)
		__field(unsigned long,	s2)
		__field(unsigned long,	m0)
		__field(unsigned long,	m1)
		__field(unsigned long,	m2)
		__field(unsigned long,	c0)
		__field(unsigned long,	c1)
		__field(unsigned long,	c2)
	),

	TP_fast_assign(
		__entry->pid			= p->pid;
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->util			= task_util(p);
		__entry->prev_cpu		= task_cpu(p);
		__entry->prev_energy		= prev_energy;
		__entry->eval_cpu		= eval_cpu;
		__entry->eval_energy		= eval_energy;
		__entry->best_energy_cpu	= best_energy_cpu;
		__entry->best_energy		= best_energy;
		__entry->cluster_first_cpu0	= o->cluster_first_cpu[0];
		__entry->cluster_first_cpu1	= o->cluster_first_cpu[1];
		__entry->cluster_first_cpu2	= o->cluster_first_cpu[2];
		__entry->s0	= o->sum_util[0];
		__entry->s1	= o->sum_util[1];
		__entry->s2	= o->sum_util[2];
		__entry->m0	= o->max_util[0];
		__entry->m1	= o->max_util[1];
		__entry->m2	= o->max_util[2];
		__entry->c0	= o->cost[0];
		__entry->c1	= o->cost[1];
		__entry->c2	= o->cost[2];
	),

	TP_printk("pid=%d comm=%s util=%lu prev_cpu=%d prev_energy=%lu eval_cpu=%d eval_energy=%lu best_energy_cpu=%d best_energy=%lu, fcpu s m c = %u %u %u %u, %u %u %u %u, %u %u %u %u",
		__entry->pid, __entry->comm, __entry->util, __entry->prev_cpu,
		__entry->prev_energy, __entry->eval_cpu, __entry->eval_energy,
		__entry->best_energy_cpu, __entry->best_energy,
		__entry->cluster_first_cpu0, __entry->s0, __entry->m0, __entry->c0,
		__entry->cluster_first_cpu1, __entry->s1, __entry->m1, __entry->c1,
		__entry->cluster_first_cpu2, __entry->s2, __entry->m2, __entry->c2)
)

TRACE_EVENT(sched_select_task_rt,

	TP_PROTO(struct task_struct *p, int fastpath, int new_cpu),

	TP_ARGS(p, fastpath, new_cpu),

	TP_STRUCT__entry(
		__field(int,		pid)
		__array(char,		comm, TASK_COMM_LEN)
		__field(int,		fastpath)
		__field(int,		new_cpu)
	),

	TP_fast_assign(
		__entry->pid			= p->pid;
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->fastpath		= fastpath;
		__entry->new_cpu		= new_cpu;
	),

	TP_printk("pid=%d comm=%s fastpath=%u best_cpu=%d",
		__entry->pid, __entry->comm, __entry->fastpath, __entry->new_cpu)
);

TRACE_EVENT(sched_rt_find_lowest_rq,

	TP_PROTO(struct task_struct *p, int fastpath, int best_cpu),

	TP_ARGS(p, fastpath, best_cpu),

	TP_STRUCT__entry(
		__field(int,		pid)
		__array(char,		comm, TASK_COMM_LEN)
		__field(int,		fastpath)
		__field(int,		best_cpu)
	),

	TP_fast_assign(
		__entry->pid			= p->pid;
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->fastpath		= fastpath;
		__entry->best_cpu		= best_cpu;
	),

	TP_printk("pid=%d comm=%s fastpath=%u best_cpu=%d",
		__entry->pid, __entry->comm, __entry->fastpath, __entry->best_cpu)
);

TRACE_EVENT(sched_task_util,

	TP_PROTO(struct task_struct *p, unsigned long candidates,
		int best_energy_cpu, bool sync, int need_idle, int fastpath,
		u64 start_t, bool uclamp_boosted, int start_cpu),

	TP_ARGS(p, candidates, best_energy_cpu, sync, need_idle, fastpath,
		start_t, uclamp_boosted, start_cpu),

	TP_STRUCT__entry(
		__field(int,		pid)
		__array(char,		comm, TASK_COMM_LEN)
		__field(unsigned long,	util)
		__field(unsigned long,	candidates)
		__field(int,		prev_cpu)
		__field(int,		best_energy_cpu)
		__field(bool,		sync)
		__field(int,		need_idle)
		__field(int,		fastpath)
		__field(int,		placement_boost)
		__field(int,		rtg_cpu)
		__field(u64,		latency)
		__field(bool,		uclamp_boosted)
		__field(bool,		is_rtg)
		__field(bool,		rtg_skip_min)
		__field(int,		start_cpu)
		__field(u32,		unfilter)
		__field(unsigned long,	cpus_allowed)
		__field(int,		task_boost)
		__field(bool,		low_latency)
		__field(bool,		iowaited)
		__field(int,		load_boost)
		__field(bool,		sync_state)
		__field(int,		pipeline_cpu)
	),

	TP_fast_assign(
		__entry->pid			= p->pid;
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->util			= task_util(p);
		__entry->prev_cpu		= task_cpu(p);
		__entry->candidates		= candidates;
		__entry->best_energy_cpu	= best_energy_cpu;
		__entry->sync			= sync;
		__entry->need_idle		= need_idle;
		__entry->fastpath		= fastpath;
		__entry->placement_boost	= task_boost_policy(p);
		__entry->latency		= (sched_clock() - start_t);
		__entry->uclamp_boosted		= uclamp_boosted;
		__entry->is_rtg			= task_in_related_thread_group(p);
		__entry->rtg_skip_min		= walt_get_rtg_status(p);
		__entry->start_cpu		= start_cpu;
		__entry->unfilter		=
			((struct walt_task_struct *) p->android_vendor_data1)->unfilter;
		__entry->cpus_allowed		= cpumask_bits(p->cpus_ptr)[0];
		__entry->task_boost		= per_task_boost(p);
		__entry->low_latency		= walt_low_latency_task(p);
		__entry->iowaited		=
			((struct walt_task_struct *) p->android_vendor_data1)->iowaited;
		__entry->load_boost		=
			((struct walt_task_struct *) p->android_vendor_data1)->load_boost;
		__entry->sync_state		= !cluster_partial_halted();
		__entry->pipeline_cpu		=
			((struct walt_task_struct *) p->android_vendor_data1)->pipeline_cpu;
	),

	TP_printk("pid=%d comm=%s util=%lu prev_cpu=%d candidates=%#lx best_energy_cpu=%d sync=%d need_idle=%d fastpath=%d placement_boost=%d latency=%llu stune_boosted=%d is_rtg=%d rtg_skip_min=%d start_cpu=%d unfilter=%u affinity=%lx task_boost=%d low_latency=%d iowaited=%d load_boost=%d sync_state=%d pipeline_cpu=%d",
		__entry->pid, __entry->comm, __entry->util, __entry->prev_cpu,
		__entry->candidates, __entry->best_energy_cpu, __entry->sync,
		__entry->need_idle, __entry->fastpath, __entry->placement_boost,
		__entry->latency, __entry->uclamp_boosted,
		__entry->is_rtg, __entry->rtg_skip_min, __entry->start_cpu,
		__entry->unfilter, __entry->cpus_allowed, __entry->task_boost,
		__entry->low_latency, __entry->iowaited, __entry->load_boost,
		__entry->sync_state, __entry->pipeline_cpu)
);

/*
 * Tracepoint for find_best_target
 */
TRACE_EVENT(sched_find_best_target,

	TP_PROTO(struct task_struct *tsk,
		 unsigned long min_util, int start_cpu,
		 unsigned long candidates,
		 int most_spare_cap,
		 int order_index, int end_index,
		 int skip, bool running,
		 int most_spare_rq_cpu, unsigned int cpu_rq_runnable_cnt),

	TP_ARGS(tsk, min_util, start_cpu, candidates,
		most_spare_cap,
		order_index, end_index, skip, running,
		most_spare_rq_cpu, cpu_rq_runnable_cnt),

	TP_STRUCT__entry(
		__array(char,		comm, TASK_COMM_LEN)
		__field(pid_t,		pid)
		__field(unsigned long,	min_util)
		__field(int,		start_cpu)
		__field(unsigned long,	candidates)
		__field(int,		most_spare_cap)
		__field(int,		order_index)
		__field(int,		end_index)
		__field(int,		skip)
		__field(bool,		running)
		__field(int,		most_spare_rq_cpu)
		__field(unsigned int,	cpu_rq_runnable_cnt)
		),

	TP_fast_assign(
		memcpy(__entry->comm, tsk->comm, TASK_COMM_LEN);
		__entry->pid		= tsk->pid;
		__entry->min_util	= min_util;
		__entry->start_cpu	= start_cpu;
		__entry->candidates	= candidates;
		__entry->most_spare_cap = most_spare_cap;
		__entry->order_index	= order_index;
		__entry->end_index	= end_index;
		__entry->skip		= skip;
		__entry->running	= running;
		__entry->most_spare_rq_cpu	= most_spare_rq_cpu;
		__entry->cpu_rq_runnable_cnt	= cpu_rq_runnable_cnt;
		),

	TP_printk("pid=%d comm=%s start_cpu=%d candidates=%#lx most_spare_cap=%d order_index=%d end_index=%d skip=%d running=%d min_util=%lu spare_rq_cpu=%d min_runnable=%u",
		  __entry->pid, __entry->comm,
		  __entry->start_cpu,
		  __entry->candidates,
		  __entry->most_spare_cap,
		  __entry->order_index,
		  __entry->end_index,
		  __entry->skip,
		  __entry->running,
		  __entry->min_util,
		  __entry->most_spare_rq_cpu,
		  __entry->cpu_rq_runnable_cnt)
);

TRACE_EVENT(sched_enq_deq_task,

	TP_PROTO(struct task_struct *p, bool enqueue, unsigned int cpus_allowed, bool mvp),

	TP_ARGS(p, enqueue, cpus_allowed, mvp),

	TP_STRUCT__entry(
		__array(char,		comm, TASK_COMM_LEN)
		__field(pid_t,		pid)
		__field(int,		prio)
		__field(int,		cpu)
		__field(bool,		enqueue)
		__field(unsigned int,	nr_running)
		__field(unsigned int,	rt_nr_running)
		__field(unsigned int,	cpus_allowed)
		__field(unsigned int,	demand)
		__field(unsigned int,	pred_demand_scaled)
		__field(bool,		compat_thread)
		__field(bool,		mvp)
		__field(bool,		misfit)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->prio		= p->prio;
		__entry->cpu		= task_cpu(p);
		__entry->enqueue	= enqueue;
		__entry->nr_running	= task_rq(p)->nr_running;
		__entry->rt_nr_running	= task_rq(p)->rt.rt_nr_running;
		__entry->cpus_allowed	= cpus_allowed;
		__entry->demand		= task_load(p);
		__entry->pred_demand_scaled	=
			((struct walt_task_struct *) p->android_vendor_data1)->pred_demand_scaled;
		__entry->compat_thread	= is_compat_thread(task_thread_info(p));
		__entry->mvp		= mvp;
		__entry->misfit		=
			((struct walt_task_struct *) p->android_vendor_data1)->misfit;
	),

	TP_printk("cpu=%d %s comm=%s pid=%d prio=%d nr_running=%u rt_nr_running=%u affine=%x demand=%u pred_demand_scaled=%u is_compat_t=%d mvp=%d misfit=%d",
			__entry->cpu,
			__entry->enqueue ? "enqueue" : "dequeue",
			__entry->comm, __entry->pid,
			__entry->prio, __entry->nr_running,
			__entry->rt_nr_running,
			__entry->cpus_allowed, __entry->demand,
			__entry->pred_demand_scaled,
			__entry->compat_thread, __entry->mvp, __entry->misfit)
);

TRACE_EVENT(walt_window_rollover,

	TP_PROTO(u64 window_start),

	TP_ARGS(window_start),

	TP_STRUCT__entry(
		__field(u64, window_start)
	),

	TP_fast_assign(
		__entry->window_start = window_start;
	),

	TP_printk("window_start=%llu", __entry->window_start)
);

DECLARE_EVENT_CLASS(walt_cfs_mvp_task_template,

	TP_PROTO(struct task_struct *p, struct walt_task_struct *wts, unsigned int limit),

	TP_ARGS(p, wts, limit),

	TP_STRUCT__entry(
		__array(char,		comm,	TASK_COMM_LEN)
		__field(pid_t,		pid)
		__field(int,		prio)
		__field(int,		mvp_prio)
		__field(int,		cpu)
		__field(u64,		exec)
		__field(unsigned int,	limit)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->prio		= p->prio;
		__entry->mvp_prio	= wts->mvp_prio;
		__entry->cpu		= task_cpu(p);
		__entry->exec		= wts->total_exec;
		__entry->limit		= limit;
	),

	TP_printk("comm=%s pid=%d prio=%d mvp_prio=%d cpu=%d exec=%llu limit=%u",
		__entry->comm, __entry->pid, __entry->prio,
		__entry->mvp_prio, __entry->cpu, __entry->exec,
		__entry->limit)
);

/* called upon MVP task de-activation. exec will be more than limit */
DEFINE_EVENT(walt_cfs_mvp_task_template, walt_cfs_deactivate_mvp_task,
	     TP_PROTO(struct task_struct *p, struct walt_task_struct *wts, unsigned int limit),
	     TP_ARGS(p, wts, limit));

/* called upon when MVP is returned to run next */
DEFINE_EVENT(walt_cfs_mvp_task_template, walt_cfs_mvp_pick_next,
	     TP_PROTO(struct task_struct *p, struct walt_task_struct *wts, unsigned int limit),
	     TP_ARGS(p, wts, limit));

/* called upon when MVP (current) is not preempted by waking task */
DEFINE_EVENT(walt_cfs_mvp_task_template, walt_cfs_mvp_wakeup_nopreempt,
	     TP_PROTO(struct task_struct *p, struct walt_task_struct *wts, unsigned int limit),
	     TP_ARGS(p, wts, limit));

/* called upon when MVP (waking task) preempts the current */
DEFINE_EVENT(walt_cfs_mvp_task_template, walt_cfs_mvp_wakeup_preempt,
	     TP_PROTO(struct task_struct *p, struct walt_task_struct *wts, unsigned int limit),
	     TP_ARGS(p, wts, limit));

#define SPAN_SIZE	(NR_CPUS/4)

TRACE_EVENT(sched_overutilized,

	TP_PROTO(int overutilized, char *span),

	TP_ARGS(overutilized, span),

	TP_STRUCT__entry(
	__field(int,	overutilized)
	__array(char,	span,	SPAN_SIZE)
	),

	TP_fast_assign(
	__entry->overutilized	= overutilized;
	strscpy(__entry->span, span, SPAN_SIZE);
	),

	TP_printk("overutilized=%d span=0x%s",
	 __entry->overutilized, __entry->span)
);

TRACE_EVENT(sched_cgroup_attach,

	TP_PROTO(struct task_struct *p, unsigned int grp_id, int ret),

	TP_ARGS(p, grp_id, ret),

	TP_STRUCT__entry(
		__array(char,		comm, TASK_COMM_LEN)
		__field(pid_t,		pid)
		__field(unsigned int,	grp_id)
		__field(int,		ret)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid	= p->pid;
		__entry->grp_id = grp_id;
		__entry->ret = ret;
	),

	TP_printk("comm=%s pid=%d grp_id=%u ret=%d",
			__entry->comm, __entry->pid,
			__entry->grp_id, __entry->ret)

);

TRACE_EVENT(halt_cpus_start,
	    TP_PROTO(struct cpumask *cpus, unsigned char halt),

	    TP_ARGS(cpus, halt),

	    TP_STRUCT__entry(
		    __field(unsigned int,   cpus)
		    __field(unsigned int,   halted_cpus)
		    __field(unsigned int,   partial_halted_cpus)
		    __field(unsigned char,  halt)
		    ),

	    TP_fast_assign(
		    __entry->cpus        = cpumask_bits(cpus)[0];
		    __entry->halted_cpus = cpumask_bits(cpu_halt_mask)[0];
		    __entry->partial_halted_cpus = cpumask_bits(cpu_partial_halt_mask)[0];
		    __entry->halt        = halt;
		    ),

	    TP_printk("req_cpus=0x%x halt_cpus=0x%x partial_halt_cpus=0x%x halt=%d",
		      __entry->cpus, __entry->halted_cpus,
		      __entry->partial_halted_cpus, __entry->halt)

);

TRACE_EVENT(halt_cpus,
	    TP_PROTO(struct cpumask *cpus, u64 start_time, unsigned char halt, int err),

	    TP_ARGS(cpus, start_time, halt, err),

	    TP_STRUCT__entry(
		    __field(unsigned int,   cpus)
		    __field(unsigned int,   halted_cpus)
		    __field(unsigned int,   partial_halted_cpus)
		    __field(unsigned int,   time)
		    __field(unsigned char,  halt)
		    __field(unsigned char,  success)
		    ),

	    TP_fast_assign(
		    __entry->cpus        = cpumask_bits(cpus)[0];
		    __entry->halted_cpus = cpumask_bits(cpu_halt_mask)[0];
		    __entry->partial_halted_cpus = cpumask_bits(cpu_partial_halt_mask)[0];
		    __entry->time        = div64_u64(sched_clock() - start_time, 1000);
		    __entry->halt        = halt;
		    __entry->success     = ((err >= 0)?1:0);
		    ),

	    TP_printk("req_cpus=0x%x halt_cpus=0x%x partial_halt_cpus=0x%x time=%u us halt=%d success=%d",
		      __entry->cpus, __entry->halted_cpus, __entry->partial_halted_cpus,
		      __entry->time, __entry->halt, __entry->success)
);

TRACE_EVENT(sched_task_handler,
	TP_PROTO(struct task_struct *p, int param, int val, unsigned long c0,
		unsigned long c1, unsigned long c2, unsigned long c3,
		unsigned long c4, unsigned long c5),

	TP_ARGS(p, param, val, c0, c1, c2, c3, c4, c5),

	TP_STRUCT__entry(
		__array(char,		comm,	TASK_COMM_LEN)
		__field(pid_t,		pid)
		__field(int,		param)
		__field(int,		val)
		__field(unsigned long,	c0)
		__field(unsigned long,	c1)
		__field(unsigned long,	c2)
		__field(unsigned long,	c3)
		__field(unsigned long,	c4)
		__field(unsigned long,	c5)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid	= p->pid;
		__entry->param	= param;
		__entry->val	= val;
		__entry->c0	= c0;
		__entry->c1	= c1;
		__entry->c2	= c2;
		__entry->c3	= c3;
		__entry->c4	= c4;
		__entry->c5	= c5;
	),

	TP_printk("comm=%s pid=%d param=%d val=%d callers=%ps <- %ps <- %ps <- %ps <- %ps <- %ps",
		__entry->comm, __entry->pid, __entry->param, __entry->val, __entry->c0,
		__entry->c1, __entry->c2, __entry->c3, __entry->c4, __entry->c5)
);

TRACE_EVENT(update_cpu_capacity,

	TP_PROTO(int cpu, unsigned long fmax_capacity,
		unsigned long rq_cpu_capacity_orig),

	TP_ARGS(cpu, fmax_capacity, rq_cpu_capacity_orig),

	TP_STRUCT__entry(
		__field(int, cpu)
		__field(unsigned long, fmax_capacity)
		__field(unsigned long, rq_cpu_capacity_orig)
		__field(unsigned long, arch_capacity)
		__field(unsigned long, thermal_cap)
		__field(unsigned long, max_possible_freq)
		__field(unsigned long, max_freq)
	),

	TP_fast_assign(
		struct walt_sched_cluster *cluster = cpu_cluster(cpu);

		__entry->cpu = cpu;
		__entry->fmax_capacity = fmax_capacity;
		__entry->rq_cpu_capacity_orig = rq_cpu_capacity_orig;
		__entry->arch_capacity = arch_scale_cpu_capacity(cpu);
		__entry->thermal_cap = arch_scale_cpu_capacity(cpu) -
					arch_scale_thermal_pressure(cpu);
		__entry->max_freq = cluster->max_freq;
		__entry->max_possible_freq = cluster->max_possible_freq;
	),

	TP_printk("cpu=%d arch_capacity=%lu thermal_cap=%lu fmax_capacity=%lu max_freq=%lu max_possible_freq=%lu rq_cpu_capacity_orig=%lu",
			__entry->cpu, __entry->arch_capacity,
			__entry->thermal_cap, __entry->fmax_capacity,
			__entry->max_freq, __entry->max_possible_freq,
			__entry->rq_cpu_capacity_orig)
);

TRACE_EVENT(sched_qos_freq_request,

	TP_PROTO(struct cpumask cpus, s32 max_freq, enum qos_clients client, int ret,
		enum qos_request_type type),

	TP_ARGS(cpus, max_freq, client, ret, type),

	TP_STRUCT__entry(
		__field(int, cpus)
		__field(s32, max_freq)
		__field(int, client)
		__field(int, ret)
		__field(int, type)
	),

	TP_fast_assign(
		__entry->cpus = cpumask_bits(&cpus)[0];
		__entry->max_freq = max_freq;
		__entry->client = client;
		__entry->ret = ret;
		__entry->type = type;
	),

	TP_printk("cpus=0x%x max_freq=%d client=%d ret=%d type=%d",
			__entry->cpus, __entry->max_freq,
			__entry->client, __entry->ret,
			__entry->type)
);

#endif /* _TRACE_WALT_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../kernel/sched/walt
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
