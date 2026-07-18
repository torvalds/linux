/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

#ifndef RTLA_ALLOW_CLI_P_H
#error "Private header file included outside of cli.c module"
#endif

#include <linux/kernel.h>
#include <subcmd/parse-options.h>

#include "cli.h"
#include "osnoise.h"
#include "timerlat.h"

struct osnoise_cb_data {
	struct osnoise_params *params;
	char *trace_output;
};

struct timerlat_cb_data {
	struct timerlat_params *params;
	char *trace_output;
};

/*
 * Macros for command line options common to all tools
 *
 * Note: Some of the options are common to both timerlat and osnoise, but
 * have a slightly different meaning. Such options take additional arguments
 * that have to be provided by the *_parse_args() function of the corresponding
 * tool.
 *
 * All macros defined here assume the presence of a params variable of
 * the corresponding tool type (i.e struct timerlat_params or struct osnoise_params)
 * and a cb_data variable of the matching type.
 */

 #define RTLA_OPT_STOP(short, long, name) OPT_CALLBACK_FLAG(short, long, \
	&params->common.stop_us, \
	"us", \
	"stop trace if " name " is higher than the argument in us", \
	opt_llong_callback, PARSE_OPT_NOAUTONEG)

#define RTLA_OPT_STOP_TOTAL(short, long, name) OPT_CALLBACK_FLAG(short, long, \
	&params->common.stop_total_us, \
	"us", \
	"stop trace if " name " is higher than the argument in us", \
	opt_llong_callback, PARSE_OPT_NOAUTONEG)

#define RTLA_OPT_TRACE_OUTPUT(tracer, cb) OPT_CALLBACK_OPTARG('t', "trace", \
	(const char **)&cb_data.trace_output, \
	tracer "_trace.txt", \
	"[file]", \
	"save the stopped trace to [file|" tracer "_trace.txt]", \
	cb)

#define RTLA_OPT_CPUS OPT_CALLBACK('c', "cpus", &params->common, \
	"cpu-list", \
	"run the tracer only on the given cpus", \
	opt_cpus_cb)

#define RTLA_OPT_CGROUP OPT_CALLBACK_OPTARG('C', "cgroup", &params->common, \
	"[cgroup_name]", NULL, \
	"set cgroup, no argument means rtla's cgroup will be inherited", \
	opt_cgroup_cb)

#define RTLA_OPT_USER_THREADS OPT_CALLBACK_NOOPT('u', "user-threads", params, NULL, \
	"use rtla user-space threads instead of kernel-space timerlat threads", \
	opt_user_threads_cb)

#define RTLA_OPT_KERNEL_THREADS OPT_BOOLEAN('k', "kernel-threads", \
	&params->common.kernel_workload, \
	"use timerlat kernel-space threads instead of rtla user-space threads")

#define RTLA_OPT_USER_LOAD OPT_BOOLEAN('U', "user-load", &params->common.user_data, \
	"enable timerlat for user-defined user-space workload")

#define RTLA_OPT_DURATION OPT_CALLBACK('d', "duration", &params->common, \
	"time[s|m|h|d]", \
	"set the duration of the session", \
	opt_duration_cb)

#define RTLA_OPT_EVENT OPT_CALLBACK('e', "event", &params->common.events, \
	"sys:event", \
	"enable the <sys:event> in the trace instance, multiple -e are allowed", \
	opt_event_cb)

#define RTLA_OPT_HOUSEKEEPING OPT_CALLBACK('H', "house-keeping", &params->common, \
	"cpu-list", \
	"run rtla control threads only on the given cpus", \
	opt_housekeeping_cb)

#define RTLA_OPT_PRIORITY OPT_CALLBACK('P', "priority", &params->common, \
	"o:prio|r:prio|f:prio|d:runtime:period", \
	"set scheduling parameters", \
	opt_priority_cb)

#define RTLA_OPT_TRIGGER OPT_CALLBACK(0, "trigger", &params->common.events, \
	"trigger", \
	"enable a trace event trigger to the previous -e event", \
	opt_trigger_cb)

#define RTLA_OPT_FILTER OPT_CALLBACK(0, "filter", &params->common.events, \
	"filter", \
	"enable a trace event filter to the previous -e event", \
	opt_filter_cb)

#define RTLA_OPT_QUIET OPT_BOOLEAN('q', "quiet", &params->common.quiet, \
	"print only a summary at the end")

#define RTLA_OPT_TRACE_BUFFER_SIZE OPT_CALLBACK(0, "trace-buffer-size", \
	&params->common.buffer_size, "kB", \
	"set the per-cpu trace buffer size in kB", \
	opt_int_callback)

#define RTLA_OPT_WARM_UP OPT_CALLBACK(0, "warm-up", &params->common.warmup, "s", \
	"let the workload run for s seconds before collecting data", \
	opt_int_callback)

#define RTLA_OPT_AUTO(cb) OPT_CALLBACK('a', "auto", &cb_data, "us", \
	"set automatic trace mode, stopping the session if argument in us sample is hit", \
	cb)

#define RTLA_OPT_ON_THRESHOLD(threshold, cb) OPT_CALLBACK(0, "on-threshold", \
	&params->common.threshold_actions, \
	"action", \
	"define action to be executed at " threshold " threshold, multiple are allowed", \
	cb)

#define RTLA_OPT_ON_END(cb) OPT_CALLBACK(0, "on-end", &params->common.end_actions, \
	"action", \
	"define action to be executed at measurement end, multiple are allowed", \
	cb)

#define RTLA_OPT_DEBUG OPT_BOOLEAN('D', "debug", &config_debug, \
	"print debug info")

/*
 * Common callback functions for command line options
 */

static int opt_llong_callback(const struct option *opt, const char *arg, int unset)
{
	long long *value = opt->value;

	if (unset || !arg)
		return -1;

	*value = get_llong_from_str((char *)arg);
	return 0;
}

static int opt_int_callback(const struct option *opt, const char *arg, int unset)
{
	int *value = opt->value;

	if (unset || !arg)
		return -1;

	if (strtoi(arg, value))
		return -1;

	return 0;
}

static int opt_cpus_cb(const struct option *opt, const char *arg, int unset)
{
	struct common_params *params = opt->value;
	int retval;

	if (unset || !arg)
		return -1;

	retval = parse_cpu_set((char *)arg, &params->monitored_cpus);
	if (retval)
		fatal("Invalid -c cpu list");
	params->cpus = (char *)arg;

	return 0;
}

static int opt_cgroup_cb(const struct option *opt, const char *arg, int unset)
{
	struct common_params *params = opt->value;

	if (unset)
		return -1;

	params->cgroup = 1;
	params->cgroup_name = (char *)arg;
	if (params->cgroup_name && params->cgroup_name[0] == '=')
		/* Allow -C=<cgroup_name> next to -C[ ]<cgroup_name> */
		++params->cgroup_name;

	return 0;
}

static int opt_duration_cb(const struct option *opt, const char *arg, int unset)
{
	struct common_params *params = opt->value;

	if (unset || !arg)
		return -1;

	params->duration = parse_seconds_duration((char *)arg);
	if (!params->duration)
		fatal("Invalid -d duration");

	return 0;
}

static int opt_event_cb(const struct option *opt, const char *arg, int unset)
{
	struct trace_events **events = opt->value;
	struct trace_events *tevent;

	if (unset || !arg)
		return -1;

	tevent = trace_event_alloc((char *)arg);
	if (!tevent)
		fatal("Error alloc trace event");

	if (*events)
		tevent->next = *events;
	*events = tevent;

	return 0;
}

static int opt_housekeeping_cb(const struct option *opt, const char *arg, int unset)
{
	struct common_params *params = opt->value;
	int retval;

	if (unset || !arg)
		return -1;

	params->hk_cpus = 1;
	retval = parse_cpu_set((char *)arg, &params->hk_cpu_set);
	if (retval)
		fatal("Error parsing house keeping CPUs");

	return 0;
}

static int opt_priority_cb(const struct option *opt, const char *arg, int unset)
{
	struct common_params *params = opt->value;
	int retval;

	if (unset || !arg)
		return -1;

	retval = parse_prio((char *)arg, &params->sched_param);
	if (retval == -1)
		fatal("Invalid -P priority");
	params->set_sched = 1;

	return 0;
}

static int opt_trigger_cb(const struct option *opt, const char *arg, int unset)
{
	struct trace_events **events = opt->value;

	if (unset || !arg)
		return -1;

	if (!*events)
		fatal("--trigger requires a previous -e");

	trace_event_add_trigger(*events, (char *)arg);

	return 0;
}

static int opt_filter_cb(const struct option *opt, const char *arg, int unset)
{
	struct trace_events **events = opt->value;

	if (unset || !arg)
		return -1;

	if (!*events)
		fatal("--filter requires a previous -e");

	trace_event_add_filter(*events, (char *)arg);

	return 0;
}

/*
 * Macros for command line options specific to osnoise
 */
#define OSNOISE_OPT_PERIOD OPT_CALLBACK('p', "period", &params->period, "us", \
	"osnoise period in us", \
	opt_osnoise_period_cb)

#define OSNOISE_OPT_RUNTIME OPT_CALLBACK('r', "runtime", &params->runtime, "us", \
	"osnoise runtime in us", \
	opt_osnoise_runtime_cb)

#define OSNOISE_OPT_THRESHOLD OPT_CALLBACK('T', "threshold", &params->threshold, "us", \
	"the minimum delta to be considered a noise", \
	opt_llong_callback)

/*
 * Callback functions for command line options for osnoise tools
 */

static int opt_osnoise_auto_cb(const struct option *opt, const char *arg, int unset)
{
	struct osnoise_cb_data *cb_data = opt->value;
	struct osnoise_params *params = cb_data->params;
	long long auto_thresh;

	if (unset || !arg)
		return -1;

	auto_thresh = get_llong_from_str((char *)arg);
	params->common.stop_us = auto_thresh;
	params->threshold = 1;

	if (!cb_data->trace_output)
		cb_data->trace_output = "osnoise_trace.txt";

	return 0;
}

static int opt_osnoise_period_cb(const struct option *opt, const char *arg, int unset)
{
	unsigned long long *period = opt->value;

	if (unset || !arg)
		return -1;

	*period = get_llong_from_str((char *)arg);
	if (*period > 10000000)
		fatal("Period longer than 10 s");

	return 0;
}

static int opt_osnoise_runtime_cb(const struct option *opt, const char *arg, int unset)
{
	unsigned long long *runtime = opt->value;

	if (unset || !arg)
		return -1;

	*runtime = get_llong_from_str((char *)arg);
	if (*runtime < 100)
		fatal("Runtime shorter than 100 us");

	return 0;
}

static int opt_osnoise_trace_output_cb(const struct option *opt, const char *arg, int unset)
{
	const char **trace_output = opt->value;

	if (unset)
		return -1;

	if (!arg) {
		*trace_output = "osnoise_trace.txt";
	} else {
		*trace_output = (char *)arg;
		if (*trace_output && (*trace_output)[0] == '=')
			/* Allow -t=<trace_output> next to -t[ ]<trace_output> */
			++*trace_output;
	}

	return 0;
}

static int opt_osnoise_on_threshold_cb(const struct option *opt, const char *arg, int unset)
{
	struct actions *actions = opt->value;
	int retval;

	if (unset || !arg)
		return -1;

	retval = actions_parse(actions, (char *)arg, "osnoise_trace.txt");
	if (retval)
		fatal("Invalid action %s", arg);

	return 0;
}

static int opt_osnoise_on_end_cb(const struct option *opt, const char *arg, int unset)
{
	struct actions *actions = opt->value;
	int retval;

	if (unset || !arg)
		return -1;

	retval = actions_parse(actions, (char *)arg, "osnoise_trace.txt");
	if (retval)
		fatal("Invalid action %s", arg);

	return 0;
}

/*
 * Macros for command line options specific to timerlat
 */
#define TIMERLAT_OPT_PERIOD OPT_CALLBACK('p', "period", &params->timerlat_period_us, "us", \
	"timerlat period in us", \
	opt_timerlat_period_cb)

#define TIMERLAT_OPT_STACK OPT_CALLBACK('s', "stack", &params->print_stack, "us", \
	"save the stack trace at the IRQ if a thread latency is higher than the argument in us", \
	opt_llong_callback)

#define TIMERLAT_OPT_NANO OPT_CALLBACK_NOOPT('n', "nano", params, NULL, \
	"display data in nanoseconds", \
	opt_nano_cb)

#define TIMERLAT_OPT_DMA_LATENCY OPT_CALLBACK(0, "dma-latency", &params->dma_latency, "us", \
	"set /dev/cpu_dma_latency latency <us> to reduce exit from idle latency", \
	opt_dma_latency_cb)

#define TIMERLAT_OPT_DEEPEST_IDLE_STATE OPT_CALLBACK(0, "deepest-idle-state", \
	&params->deepest_idle_state, "n", \
	"only go down to idle state n on cpus used by timerlat to reduce exit from idle latency", \
	opt_int_callback)

#define TIMERLAT_OPT_AA_ONLY OPT_CALLBACK(0, "aa-only", params, "us", \
	"stop if <us> latency is hit, only printing the auto analysis (reduces CPU usage)", \
	opt_aa_only_cb)

#define TIMERLAT_OPT_NO_AA OPT_BOOLEAN(0, "no-aa", &params->no_aa, \
	"disable auto-analysis, reducing rtla timerlat cpu usage")

#define TIMERLAT_OPT_DUMPS_TASKS OPT_BOOLEAN(0, "dump-tasks", &params->dump_tasks, \
	"prints the task running on all CPUs if stop conditions are met (depends on !--no-aa)")

#define TIMERLAT_OPT_BPF_ACTION OPT_STRING(0, "bpf-action", &params->bpf_action_program, \
	"program", \
	"load and execute BPF program when latency threshold is exceeded")

#define TIMERLAT_OPT_STACK_FORMAT OPT_CALLBACK(0, "stack-format", &params->stack_format, "format", \
	"set the stack format (truncate, skip, full)", \
	opt_stack_format_cb)

#define TIMERLAT_OPT_ALIGNED OPT_CALLBACK('A', "aligned", params, "us", \
	"align thread wakeups to a specific offset", \
	opt_timerlat_align_cb)

/*
 * Callback functions for command line options for timerlat tools
 */

static int opt_timerlat_period_cb(const struct option *opt, const char *arg, int unset)
{
	long long *period = opt->value;

	if (unset || !arg)
		return -1;

	*period = get_llong_from_str((char *)arg);
	if (*period > 1000000)
		fatal("Period longer than 1 s");

	return 0;
}

static int opt_timerlat_auto_cb(const struct option *opt, const char *arg, int unset)
{
	struct timerlat_cb_data *cb_data = opt->value;
	struct timerlat_params *params = cb_data->params;
	long long auto_thresh;

	if (unset || !arg)
		return -1;

	auto_thresh = get_llong_from_str((char *)arg);
	params->common.stop_total_us = auto_thresh;
	params->common.stop_us = auto_thresh;
	params->print_stack = auto_thresh;

	if (!cb_data->trace_output)
		cb_data->trace_output = "timerlat_trace.txt";

	return 0;
}

static int opt_dma_latency_cb(const struct option *opt, const char *arg, int unset)
{
	int *dma_latency = opt->value;
	int retval;

	if (unset || !arg)
		return -1;

	retval = strtoi((char *)arg, dma_latency);
	if (retval)
		fatal("Invalid -dma-latency %s", arg);
	if (*dma_latency < 0 || *dma_latency > 10000)
		fatal("--dma-latency needs to be >= 0 and <= 10000");

	return 0;
}

static int opt_aa_only_cb(const struct option *opt, const char *arg, int unset)
{
	struct timerlat_params *params = opt->value;
	long long auto_thresh;

	if (unset || !arg)
		return -1;

	auto_thresh = get_llong_from_str((char *)arg);
	params->common.stop_total_us = auto_thresh;
	params->common.stop_us = auto_thresh;
	params->print_stack = auto_thresh;
	params->common.aa_only = 1;

	return 0;
}

static int opt_timerlat_trace_output_cb(const struct option *opt, const char *arg, int unset)
{
	const char **trace_output = opt->value;

	if (unset)
		return -1;

	if (!arg) {
		*trace_output = "timerlat_trace.txt";
	} else {
		*trace_output = (char *)arg;
		if (*trace_output && (*trace_output)[0] == '=')
			/* Allow -t=<trace_output> next to -t[ ]<trace_output> */
			++*trace_output;
	}

	return 0;
}

static int opt_timerlat_on_threshold_cb(const struct option *opt, const char *arg, int unset)
{
	struct actions *actions = opt->value;
	int retval;

	if (unset || !arg)
		return -1;

	retval = actions_parse(actions, (char *)arg, "timerlat_trace.txt");
	if (retval)
		fatal("Invalid action %s", arg);

	return 0;
}

static int opt_timerlat_on_end_cb(const struct option *opt, const char *arg, int unset)
{
	struct actions *actions = opt->value;
	int retval;

	if (unset || !arg)
		return -1;

	retval = actions_parse(actions, (char *)arg, "timerlat_trace.txt");
	if (retval)
		fatal("Invalid action %s", arg);

	return 0;
}

static int opt_user_threads_cb(const struct option *opt, const char *arg, int unset)
{
	struct timerlat_params *params = opt->value;

	if (unset)
		return -1;

	params->common.user_workload = true;
	params->common.user_data = true;

	return 0;
}

static int opt_nano_cb(const struct option *opt, const char *arg, int unset)
{
	struct timerlat_params *params = opt->value;

	if (unset)
		return -1;

	params->common.output_divisor = 1;

	return 0;
}

static int opt_stack_format_cb(const struct option *opt, const char *arg, int unset)
{
	int *format = opt->value;

	if (unset || !arg)
		return -1;

	*format = parse_stack_format((char *)arg);

	if (*format == -1)
		fatal("Invalid --stack-format option");

	return 0;
}

static int opt_timerlat_align_cb(const struct option *opt, const char *arg, int unset)
{
	struct timerlat_params *params = opt->value;

	if (unset || !arg)
		return -1;

	params->timerlat_align = true;
	params->timerlat_align_us = get_llong_from_str((char *)arg);

	return 0;
}

/*
 * Macros for command line options specific to histogram-based tools
 */

#define HIST_OPT_BUCKET_SIZE OPT_CALLBACK('b', "bucket-size", \
	&params->common.hist.bucket_size, "N", \
	"set the histogram bucket size (default 1)", \
	opt_bucket_size_cb)

#define HIST_OPT_ENTRIES OPT_CALLBACK('E', "entries", &params->common.hist.entries, "N", \
	"set the number of entries of the histogram (default 256)", \
	opt_entries_cb)

#define HIST_OPT_NO_IRQ OPT_BOOLEAN_FLAG(0, "no-irq", &params->common.hist.no_irq, \
	"ignore IRQ latencies", PARSE_OPT_NOAUTONEG)

#define HIST_OPT_NO_THREAD OPT_BOOLEAN_FLAG(0, "no-thread", &params->common.hist.no_thread, \
	"ignore thread latencies", PARSE_OPT_NOAUTONEG)

#define HIST_OPT_NO_HEADER OPT_BOOLEAN(0, "no-header", &params->common.hist.no_header, \
	"do not print header")

#define HIST_OPT_NO_SUMMARY OPT_BOOLEAN(0, "no-summary", &params->common.hist.no_summary, \
	"do not print summary")

#define HIST_OPT_NO_INDEX OPT_BOOLEAN(0, "no-index", &params->common.hist.no_index, \
	"do not print index")

#define HIST_OPT_WITH_ZEROS OPT_BOOLEAN(0, "with-zeros", &params->common.hist.with_zeros, \
	"print zero only entries")

/* Histogram-specific callbacks */

static int opt_bucket_size_cb(const struct option *opt, const char *arg, int unset)
{
	int *bucket_size = opt->value;

	if (unset || !arg)
		return -1;

	*bucket_size = get_llong_from_str((char *)arg);
	if (*bucket_size == 0 || *bucket_size >= 1000000)
		fatal("Bucket size needs to be > 0 and <= 1000000");

	return 0;
}

static int opt_entries_cb(const struct option *opt, const char *arg, int unset)
{
	int *entries = opt->value;

	if (unset || !arg)
		return -1;

	*entries = get_llong_from_str((char *)arg);
	if (*entries < 10 || *entries > 9999999)
		fatal("Entries must be > 10 and < 10000000");

	return 0;
}
