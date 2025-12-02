/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

#include "actions.h"
#include "timerlat_u.h"
#include "trace.h"
#include "utils.h"

/*
 * osnoise_context - read, store, write, restore osnoise configs.
 */
struct osnoise_context {
	int			flags;
	int			ref;

	char			*curr_cpus;
	char			*orig_cpus;

	/* 0 as init value */
	unsigned long long	orig_runtime_us;
	unsigned long long	runtime_us;

	/* 0 as init value */
	unsigned long long	orig_period_us;
	unsigned long long	period_us;

	/* 0 as init value */
	long long		orig_timerlat_period_us;
	long long		timerlat_period_us;

	/* 0 as init value */
	long long		orig_tracing_thresh;
	long long		tracing_thresh;

	/* -1 as init value because 0 is disabled */
	long long		orig_stop_us;
	long long		stop_us;

	/* -1 as init value because 0 is disabled */
	long long		orig_stop_total_us;
	long long		stop_total_us;

	/* -1 as init value because 0 is disabled */
	long long		orig_print_stack;
	long long		print_stack;

	/* -1 as init value because 0 is off */
	int			orig_opt_irq_disable;
	int			opt_irq_disable;

	/* -1 as init value because 0 is off */
	int			orig_opt_workload;
	int			opt_workload;
};

extern struct trace_instance *trace_inst;
extern int stop_tracing;

struct hist_params {
	char			no_irq;
	char			no_thread;
	char			no_header;
	char			no_summary;
	char			no_index;
	char			with_zeros;
	int			bucket_size;
	int			entries;
};

/*
 * common_params - Parameters shared between timerlat_params and osnoise_params
 */
struct common_params {
	/* trace configuration */
	char			*cpus;
	cpu_set_t		monitored_cpus;
	struct trace_events	*events;
	int			buffer_size;

	/* Timing parameters */
	int			warmup;
	long long		stop_us;
	long long		stop_total_us;
	int			sleep_time;
	int			duration;

	/* Scheduling parameters */
	int			set_sched;
	struct sched_attr	sched_param;
	int			cgroup;
	char			*cgroup_name;
	int			hk_cpus;
	cpu_set_t		hk_cpu_set;

	/* Other parameters */
	struct hist_params	hist;
	int			output_divisor;
	int			pretty_output;
	int			quiet;
	int			user_workload;
	int			kernel_workload;
	int			user_data;
	int			aa_only;

	struct actions		threshold_actions;
	struct actions		end_actions;
	struct timerlat_u_params user;
};

struct tool_ops;

/*
 * osnoise_tool -  osnoise based tool definition.
 *
 * Only the "trace" and "context" fields are used for
 * the additional trace instances (record and aa).
 */
struct osnoise_tool {
	struct tool_ops			*ops;
	struct trace_instance		trace;
	struct osnoise_context		*context;
	void				*data;
	struct common_params		*params;
	time_t				start_time;
	struct osnoise_tool		*record;
	struct osnoise_tool		*aa;
};

struct tool_ops {
	const char *tracer;
	const char *comm_prefix;
	struct common_params *(*parse_args)(int argc, char *argv[]);
	struct osnoise_tool *(*init_tool)(struct common_params *params);
	int (*apply_config)(struct osnoise_tool *tool);
	int (*enable)(struct osnoise_tool *tool);
	int (*main)(struct osnoise_tool *tool);
	void (*print_stats)(struct osnoise_tool *tool);
	void (*analyze)(struct osnoise_tool *tool, bool stopped);
	void (*free)(struct osnoise_tool *tool);
};

int osnoise_set_cpus(struct osnoise_context *context, char *cpus);
void osnoise_restore_cpus(struct osnoise_context *context);

int osnoise_set_workload(struct osnoise_context *context, bool onoff);

void osnoise_destroy_tool(struct osnoise_tool *top);
struct osnoise_tool *osnoise_init_tool(char *tool_name);
struct osnoise_tool *osnoise_init_trace_tool(const char *tracer);
bool osnoise_trace_is_off(struct osnoise_tool *tool, struct osnoise_tool *record);

int common_apply_config(struct osnoise_tool *tool, struct common_params *params);
int top_main_loop(struct osnoise_tool *tool);
int hist_main_loop(struct osnoise_tool *tool);
