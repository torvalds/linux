/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

#include "actions.h"
#include "timerlat_u.h"
#include "trace.h"
#include "utils.h"

/*
 * OSNOISE_LL_OPTIONS - list of long long options backed by tracefs files.
 *   OSNOISE_LL_OPTION(field_name, tracefs_path)
 *
 * OSNOISE_FLAG_OPTIONS - list of boolean options backed by osnoise/options.
 *   OSNOISE_FLAG_OPTION(field_name, option_string)
 *
 * These X-macro lists are invoked in four places:
 *  - struct osnoise_context field declarations (common.h),
 *  - function declarations for osnoise_set_<opt>/osnoise_restore_<opt> (common.h),
 *  - function definitions for set/restore/put (osnoise.c),
 *  - context initialization and teardown in osnoise_context_alloc() and
 *    osnoise_put_context() (osnoise.c).
 */
#define OSNOISE_LL_OPTIONS \
	OSNOISE_LL_OPTION(stop_us,		"osnoise/stop_tracing_us")		\
	OSNOISE_LL_OPTION(stop_total_us,	"osnoise/stop_tracing_total_us")	\
	OSNOISE_LL_OPTION(print_stack,		"osnoise/print_stack")			\
	OSNOISE_LL_OPTION(tracing_thresh,	"tracing_thresh")			\
	OSNOISE_LL_OPTION(timerlat_period_us,	"osnoise/timerlat_period_us")		\
	OSNOISE_LL_OPTION(timerlat_align_us,	"osnoise/timerlat_align_us")

#define OSNOISE_FLAG_OPTIONS \
	OSNOISE_FLAG_OPTION(irq_disable,	"OSNOISE_IRQ_DISABLE")	\
	OSNOISE_FLAG_OPTION(workload,		"OSNOISE_WORKLOAD")	\
	OSNOISE_FLAG_OPTION(timerlat_align,	"TIMERLAT_ALIGN")

/*
 * osnoise_context - read, store, write, restore osnoise configs.
 */
#define OSNOISE_LL_OPTION(name, path)			\
	long long		orig_##name;		\
	long long		name;
#define OSNOISE_FLAG_OPTION(name, option_str)		\
	int			orig_opt_##name;	\
	int			opt_##name;
struct osnoise_context {
	int			flags;
	int			ref;

	char			*curr_cpus;
	char			*orig_cpus;

	long long		orig_runtime_us;
	long long		runtime_us;

	long long		orig_period_us;
	long long		period_us;

	OSNOISE_LL_OPTIONS
	OSNOISE_FLAG_OPTIONS
};
#undef OSNOISE_LL_OPTION
#undef OSNOISE_FLAG_OPTION

extern volatile int stop_tracing;

struct hist_params {
	bool			no_irq;
	bool			no_thread;
	bool			no_header;
	bool			no_summary;
	bool			no_index;
	bool			with_zeros;
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
	bool			pretty_output;
	bool			quiet;
	bool			user_workload;
	bool			kernel_workload;
	bool			user_data;
	bool			aa_only;
	bool			ipi;

	struct actions		threshold_actions;
	struct actions		end_actions;
	struct timerlat_u_params user;
};

#define for_each_monitored_cpu(cpu, common) \
	for (cpu = 0; cpu < nr_cpus; cpu++) \
		if (!(common)->cpus || CPU_ISSET(cpu, &(common)->monitored_cpus))

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

/**
 * should_continue_tracing - check if tracing should continue after threshold
 * @params: pointer to the common parameters structure
 *
 * Returns true if the continue action was configured (--on-threshold continue),
 * indicating that tracing should be restarted after handling the threshold event.
 *
 * Return: 1 if tracing should continue, 0 otherwise.
 */
static inline int
should_continue_tracing(const struct common_params *params)
{
	return params->threshold_actions.continue_flag;
}

int
common_threshold_handler(const struct osnoise_tool *tool);

int osnoise_get_cpu_count(void);
int osnoise_set_cpus(struct osnoise_context *context, char *cpus);
void osnoise_restore_cpus(struct osnoise_context *context);

#define OSNOISE_LL_OPTION(name, path)							\
	int osnoise_set_##name(struct osnoise_context *context, long long name);	\
	void osnoise_restore_##name(struct osnoise_context *context);
#define OSNOISE_FLAG_OPTION(name, option_str)					\
	int osnoise_set_##name(struct osnoise_context *context, bool onoff);	\
	void osnoise_restore_##name(struct osnoise_context *context);
OSNOISE_LL_OPTIONS
OSNOISE_FLAG_OPTIONS
#undef OSNOISE_LL_OPTION
#undef OSNOISE_FLAG_OPTION

void osnoise_destroy_tool(struct osnoise_tool *top);
struct osnoise_tool *osnoise_init_tool(char *tool_name);
struct osnoise_tool *osnoise_init_trace_tool(struct common_params *params, const char *tracer);
bool osnoise_trace_is_off(struct osnoise_tool *tool, struct osnoise_tool *record);

int common_apply_config(struct osnoise_tool *tool, struct common_params *params);
int top_main_loop(struct osnoise_tool *tool);
int hist_main_loop(struct osnoise_tool *tool);
int osn_set_stop(struct osnoise_tool *tool);

void common_usage(const char *tool, const char *mode,
		  const char *desc, const char * const *start_msgs, const char * const *opt_msgs);
