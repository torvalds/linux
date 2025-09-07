/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

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

/*
 * osnoise_tool -  osnoise based tool definition.
 */
struct osnoise_tool {
	struct trace_instance		trace;
	struct osnoise_context		*context;
	void				*data;
	void				*params;
	time_t				start_time;
};

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
	int			kernel_workload;
};

int osnoise_set_cpus(struct osnoise_context *context, char *cpus);
void osnoise_restore_cpus(struct osnoise_context *context);

int osnoise_set_workload(struct osnoise_context *context, bool onoff);

int common_apply_config(struct osnoise_tool *tool, struct common_params *params);
