/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM clk

#if !defined(_TRACE_CLK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CLK_H

#include <linux/tracepoint.h>

struct clk_core;

DECLARE_EVENT_CLASS(clk,

	TP_PROTO(struct clk_core *core),

	TP_ARGS(core),

	TP_STRUCT__entry(
		__string(        name,           core->name       )
	),

	TP_fast_assign(
		__assign_str(name);
	),

	TP_printk("%s", __get_str(name))
);

DEFINE_EVENT(clk, clk_enable,

	TP_PROTO(struct clk_core *core),

	TP_ARGS(core)
);

DEFINE_EVENT(clk, clk_enable_complete,

	TP_PROTO(struct clk_core *core),

	TP_ARGS(core)
);

DEFINE_EVENT(clk, clk_disable,

	TP_PROTO(struct clk_core *core),

	TP_ARGS(core)
);

DEFINE_EVENT(clk, clk_disable_complete,

	TP_PROTO(struct clk_core *core),

	TP_ARGS(core)
);

DEFINE_EVENT(clk, clk_prepare,

	TP_PROTO(struct clk_core *core),

	TP_ARGS(core)
);

DEFINE_EVENT(clk, clk_prepare_complete,

	TP_PROTO(struct clk_core *core),

	TP_ARGS(core)
);

DEFINE_EVENT(clk, clk_unprepare,

	TP_PROTO(struct clk_core *core),

	TP_ARGS(core)
);

DEFINE_EVENT(clk, clk_unprepare_complete,

	TP_PROTO(struct clk_core *core),

	TP_ARGS(core)
);

DECLARE_EVENT_CLASS(clk_rate,

	TP_PROTO(struct clk_core *core, unsigned long rate),

	TP_ARGS(core, rate),

	TP_STRUCT__entry(
		__string(        name,           core->name                )
		__field(unsigned long,           rate                      )
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->rate = rate;
	),

	TP_printk("%s %lu", __get_str(name), (unsigned long)__entry->rate)
);

DEFINE_EVENT(clk_rate, clk_set_rate,

	TP_PROTO(struct clk_core *core, unsigned long rate),

	TP_ARGS(core, rate)
);

DEFINE_EVENT(clk_rate, clk_set_rate_complete,

	TP_PROTO(struct clk_core *core, unsigned long rate),

	TP_ARGS(core, rate)
);

DEFINE_EVENT(clk_rate, clk_set_min_rate,

	TP_PROTO(struct clk_core *core, unsigned long rate),

	TP_ARGS(core, rate)
);

DEFINE_EVENT(clk_rate, clk_set_max_rate,

	TP_PROTO(struct clk_core *core, unsigned long rate),

	TP_ARGS(core, rate)
);

DECLARE_EVENT_CLASS(clk_rate_range,

	TP_PROTO(struct clk_core *core, unsigned long min, unsigned long max),

	TP_ARGS(core, min, max),

	TP_STRUCT__entry(
		__string(        name,           core->name                )
		__field(unsigned long,           min                       )
		__field(unsigned long,           max                       )
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->min = min;
		__entry->max = max;
	),

	TP_printk("%s min %lu max %lu", __get_str(name),
		  (unsigned long)__entry->min,
		  (unsigned long)__entry->max)
);

DEFINE_EVENT(clk_rate_range, clk_set_rate_range,

	TP_PROTO(struct clk_core *core, unsigned long min, unsigned long max),

	TP_ARGS(core, min, max)
);

DECLARE_EVENT_CLASS(clk_parent,

	TP_PROTO(struct clk_core *core, struct clk_core *parent),

	TP_ARGS(core, parent),

	TP_STRUCT__entry(
		__string(        name,           core->name                )
		__string(        pname, parent ? parent->name : "none"     )
	),

	TP_fast_assign(
		__assign_str(name);
		__assign_str(pname);
	),

	TP_printk("%s %s", __get_str(name), __get_str(pname))
);

DEFINE_EVENT(clk_parent, clk_set_parent,

	TP_PROTO(struct clk_core *core, struct clk_core *parent),

	TP_ARGS(core, parent)
);

DEFINE_EVENT(clk_parent, clk_set_parent_complete,

	TP_PROTO(struct clk_core *core, struct clk_core *parent),

	TP_ARGS(core, parent)
);

DECLARE_EVENT_CLASS(clk_phase,

	TP_PROTO(struct clk_core *core, int phase),

	TP_ARGS(core, phase),

	TP_STRUCT__entry(
		__string(        name,           core->name                )
		__field(	  int,           phase                     )
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->phase = phase;
	),

	TP_printk("%s %d", __get_str(name), (int)__entry->phase)
);

DEFINE_EVENT(clk_phase, clk_set_phase,

	TP_PROTO(struct clk_core *core, int phase),

	TP_ARGS(core, phase)
);

DEFINE_EVENT(clk_phase, clk_set_phase_complete,

	TP_PROTO(struct clk_core *core, int phase),

	TP_ARGS(core, phase)
);

DECLARE_EVENT_CLASS(clk_duty_cycle,

	TP_PROTO(struct clk_core *core, struct clk_duty *duty),

	TP_ARGS(core, duty),

	TP_STRUCT__entry(
		__string(        name,           core->name              )
		__field( unsigned int,           num                     )
		__field( unsigned int,           den                     )
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->num = duty->num;
		__entry->den = duty->den;
	),

	TP_printk("%s %u/%u", __get_str(name), (unsigned int)__entry->num,
		  (unsigned int)__entry->den)
);

DEFINE_EVENT(clk_duty_cycle, clk_set_duty_cycle,

	TP_PROTO(struct clk_core *core, struct clk_duty *duty),

	TP_ARGS(core, duty)
);

DEFINE_EVENT(clk_duty_cycle, clk_set_duty_cycle_complete,

	TP_PROTO(struct clk_core *core, struct clk_duty *duty),

	TP_ARGS(core, duty)
);

DECLARE_EVENT_CLASS(clk_rate_request,

	TP_PROTO(struct clk_rate_request *req),

	TP_ARGS(req),

	TP_STRUCT__entry(
		__string(        name, req->core ? req->core->name : "none")
		__string(       pname, req->best_parent_hw ? clk_hw_get_name(req->best_parent_hw) : "none" )
		__field(unsigned long,           min                       )
		__field(unsigned long,           max                       )
		__field(unsigned long,           prate                     )
	),

	TP_fast_assign(
		__assign_str(name);
		__assign_str(pname);
		__entry->min = req->min_rate;
		__entry->max = req->max_rate;
		__entry->prate = req->best_parent_rate;
	),

	TP_printk("%s min %lu max %lu, parent %s (%lu)", __get_str(name),
		  (unsigned long)__entry->min,
		  (unsigned long)__entry->max,
		  __get_str(pname),
		  (unsigned long)__entry->prate)
);

DEFINE_EVENT(clk_rate_request, clk_rate_request_start,

	TP_PROTO(struct clk_rate_request *req),

	TP_ARGS(req)
);

DEFINE_EVENT(clk_rate_request, clk_rate_request_done,

	TP_PROTO(struct clk_rate_request *req),

	TP_ARGS(req)
);

#endif /* _TRACE_CLK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
