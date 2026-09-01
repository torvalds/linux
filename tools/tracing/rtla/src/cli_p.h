/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

#ifndef RTLA_ALLOW_CLI_P_H
#error "Private header file included outside of cli.c module"
#endif

#include <errno.h>
#include <limits.h>
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
 * Non-zero default values for parameters
 */
static const int default_dma_latency = -1; /* -1 = unset */
static const int default_deepest_idle_state = -2; /* -1 = disable all, -2 = unset */
static const int default_output_divisor = 1000;
static const int default_bucket_size = 1;
static const int default_entries = 256;
static const enum stack_format default_stack_format = STACK_FORMAT_TRUNCATE;

/*
 * Range checking for long long and int option callbacks.
 *
 * Pass a pointer to a const struct as opt->data to enable range checking.
 * If opt->data is NULL, no range check is performed.
 */
struct llong_range {
	long long min;
	long long max;
};

struct int_range {
	int min;
	int max;
};

#define LLONG_RANGE(lo, hi) \
	(&(const struct llong_range){ .min = (lo), .max = (hi) })

#define INT_RANGE(lo, hi) \
	(&(const struct int_range){ .min = (lo), .max = (hi) })

static int check_llong_range(const struct option *opt, long long value)
{
	const struct llong_range *range = opt->data;

	if (!range)
		return 0;
	if (value < range->min || value > range->max) {
		fprintf(stderr, " Error: --%s value %lld is out of range [%lld, %lld]\n",
			opt->long_name, value, range->min, range->max);
		return -1;
	}
	return 0;
}

static int check_int_range(const struct option *opt, int value)
{
	const struct int_range *range = opt->data;

	if (!range)
		return 0;
	if (value < range->min || value > range->max) {
		fprintf(stderr, " Error: --%s value %d is out of range [%d, %d]\n",
			opt->long_name, value, range->min, range->max);
		return -1;
	}
	return 0;
}

/*
 * OPT_CALLBACK variant that populates .data (for range checking).
 */
#define RTLA_OPT_CALLBACK_DATA(s, l, v, a, h, f, d) \
	{ .type = OPTION_CALLBACK, .short_name = (s), .long_name = (l), \
	  .value = (v), .argh = (a), .help = (h), .callback = (f), \
	  .data = (void *)(d) }

#define RTLA_OPT_CALLBACK_DATA_DEFVAL(s, l, v, a, h, f, d, dv) \
	{ .type = OPTION_CALLBACK, .short_name = (s), .long_name = (l), \
	  .value = (v), .argh = (a), .help = (h), .callback = (f), \
	  .data = (void *)(d), .defval = (intptr_t)(dv) }

/*
 * Shorthand macros for integer/long long command line options using
 * opt_int_callback/opt_llong_callback, with variants that set defval
 * and/or data (for range checking).
 *
 * Note: defval's type is intptr_t. opt_int_callback interprets it directly as
 * an int, opt_llong_callback interprets it as a pointer to a long long, as
 * long long does not fit into intptr_t on 32-bit architectures.
 */
#define RTLA_OPT_LLONG(s, l, v, a, h) \
	OPT_CALLBACK(s, l, v, a, h, opt_llong_callback)

#define RTLA_OPT_LLONG_DEFVAL(s, l, v, a, h, d) { .type = OPTION_CALLBACK, \
	.short_name = (s), .long_name = (l), .value = (v), .argh = (a), \
	.help = (h), .callback = opt_llong_callback, .defval = (intptr_t)(d) }

#define RTLA_OPT_LLONG_DATA(s, l, v, a, h, d) { .type = OPTION_CALLBACK, \
	.short_name = (s), .long_name = (l), .value = (v), .argh = (a), \
	.help = (h), .callback = opt_llong_callback, .data = (void *)(d) }

#define RTLA_OPT_INT(s, l, v, a, h) \
	OPT_CALLBACK(s, l, v, a, h, opt_int_callback)

#define RTLA_OPT_INT_DEFVAL(s, l, v, a, h, d) { .type = OPTION_CALLBACK, \
	.short_name = (s), .long_name = (l), .value = (v), .argh = (a), \
	.help = (h), .callback = opt_int_callback, .defval = (intptr_t)(d) }

#define RTLA_OPT_INT_DATA_DEFVAL(s, l, v, a, h, d, dv) { .type = OPTION_CALLBACK, \
	.short_name = (s), .long_name = (l), .value = (v), .argh = (a), \
	.help = (h), .callback = opt_int_callback, \
	.data = (void *)(d), .defval = (intptr_t)(dv) }

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

#define RTLA_OPT_TRACE_BUFFER_SIZE RTLA_OPT_INT(0, "trace-buffer-size", \
	&params->common.buffer_size, "kB", \
	"set the per-cpu trace buffer size in kB")

#define RTLA_OPT_WARM_UP RTLA_OPT_INT(0, "warm-up", &params->common.warmup, "s", \
	"let the workload run for s seconds before collecting data")

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
 * Helper functions for parsing numeric option arguments.
 */
static void opt_err(const struct option *opt, const char *arg, const char *msg)
{
	fprintf(stderr, " Error: --%s: '%s' %s\n", opt->long_name, arg, msg);
}

static int strtoll_safe(const struct option *opt, const char *arg, long long *value)
{
	long long tmp;
	char *end;

	errno = 0;
	tmp = strtoll(arg, &end, 10);
	if (errno || *end || end == arg) {
		opt_err(opt, arg, "is not a valid number");
		return -1;
	}
	*value = tmp;
	return 0;
}

static int strtoi_safe(const struct option *opt, const char *arg, int *value)
{
	int tmp;

	if (strtoi(arg, &tmp)) {
		opt_err(opt, arg, "is not a valid number");
		return -1;
	}
	*value = tmp;
	return 0;
}

/*
 * Common callback functions for command line options
 */

static int opt_llong_callback(const struct option *opt, const char *arg, int unset)
{
	long long *value = opt->value;

	if (unset) {
		*value = opt->defval ? *(long long *)opt->defval : 0;
		return 0;
	}

	if (!arg)
		return -1;

	if (strtoll_safe(opt, arg, value))
		return -1;
	if (check_llong_range(opt, *value))
		return -1;
	return 0;
}

static int opt_int_callback(const struct option *opt, const char *arg, int unset)
{
	int *value = opt->value;

	if (unset) {
		*value = (int)opt->defval;
		return 0;
	}

	if (!arg)
		return -1;

	if (strtoi_safe(opt, arg, value))
		return -1;
	if (check_int_range(opt, *value))
		return -1;

	return 0;
}

static int opt_cpus_cb(const struct option *opt, const char *arg, int unset)
{
	struct common_params *params = opt->value;
	int retval;

	if (unset) {
		CPU_ZERO(&params->monitored_cpus);
		params->cpus = NULL;
		return 0;
	}

	if (!arg)
		return -1;

	retval = parse_cpu_set((char *)arg, &params->monitored_cpus);
	if (retval) {
		opt_err(opt, arg, "is not a valid cpu set");
		return -1;
	}
	params->cpus = (char *)arg;

	return 0;
}

static int opt_cgroup_cb(const struct option *opt, const char *arg, int unset)
{
	struct common_params *params = opt->value;

	if (unset) {
		params->cgroup = 0;
		params->cgroup_name = NULL;
		return 0;
	}

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

	if (unset) {
		params->duration = 0;
		return 0;
	}

	if (!arg)
		return -1;

	params->duration = parse_seconds_duration((char *)arg);
	if (!params->duration) {
		opt_err(opt, arg, "is not a valid duration");
		return -1;
	}

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

	if (unset) {
		params->hk_cpus = 0;
		CPU_ZERO(&params->hk_cpu_set);
		return 0;
	}

	if (!arg)
		return -1;

	params->hk_cpus = 1;
	retval = parse_cpu_set((char *)arg, &params->hk_cpu_set);
	if (retval) {
		opt_err(opt, arg, "is not a valid cpu set");
		return -1;
	}

	return 0;
}

static int opt_priority_cb(const struct option *opt, const char *arg, int unset)
{
	struct common_params *params = opt->value;
	int retval;

	if (unset) {
		memset(&params->sched_param, 0, sizeof(params->sched_param));
		params->set_sched = 0;
		return 0;
	}

	if (!arg)
		return -1;

	retval = parse_prio((char *)arg, &params->sched_param);
	if (retval == -1) {
		opt_err(opt, arg, "is not a valid priority");
		return -1;
	}
	params->set_sched = 1;

	return 0;
}

static int opt_trigger_cb(const struct option *opt, const char *arg, int unset)
{
	struct trace_events **events = opt->value;

	if (unset || !arg)
		return -1;

	if (!*events) {
		opt_err(opt, arg, "has no previous event to apply to");
		return -1;
	}

	trace_event_add_trigger(*events, (char *)arg);

	return 0;
}

static int opt_filter_cb(const struct option *opt, const char *arg, int unset)
{
	struct trace_events **events = opt->value;

	if (unset || !arg)
		return -1;

	if (!*events) {
		opt_err(opt, arg, "has no previous event to apply to");
		return -1;
	}

	trace_event_add_filter(*events, (char *)arg);

	return 0;
}

/*
 * Macros for command line options specific to osnoise
 */
#define OSNOISE_OPT_PERIOD RTLA_OPT_LLONG_DATA('p', "period", &params->period, "us", \
	"osnoise period in us", \
	LLONG_RANGE(1, 10000000))

#define OSNOISE_OPT_RUNTIME RTLA_OPT_LLONG_DATA('r', "runtime", &params->runtime, "us", \
	"osnoise runtime in us", \
	LLONG_RANGE(100, LLONG_MAX))

#define OSNOISE_OPT_THRESHOLD RTLA_OPT_LLONG('T', "threshold", &params->threshold, "us", \
	"the minimum delta to be considered a noise")

/*
 * Callback functions for command line options for osnoise tools
 */

static int opt_osnoise_auto_cb(const struct option *opt, const char *arg, int unset)
{
	struct osnoise_cb_data *cb_data = opt->value;
	struct osnoise_params *params = cb_data->params;
	long long auto_thresh;

	if (unset) {
		params->common.stop_us = 0;
		params->threshold = 0;
		cb_data->trace_output = NULL;
		return 0;
	}

	if (!arg)
		return -1;

	if (strtoll_safe(opt, arg, &auto_thresh))
		return -1;
	params->common.stop_us = auto_thresh;
	params->threshold = 1;

	if (!cb_data->trace_output)
		cb_data->trace_output = "osnoise_trace.txt";

	return 0;
}

static int opt_osnoise_trace_output_cb(const struct option *opt, const char *arg, int unset)
{
	const char **trace_output = opt->value;

	if (unset) {
		*trace_output = NULL;
		return 0;
	}

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
	if (retval) {
		opt_err(opt, arg, "is not a valid action");
		return -1;
	}

	return 0;
}

static int opt_osnoise_on_end_cb(const struct option *opt, const char *arg, int unset)
{
	struct actions *actions = opt->value;
	int retval;

	if (unset || !arg)
		return -1;

	retval = actions_parse(actions, (char *)arg, "osnoise_trace.txt");
	if (retval) {
		opt_err(opt, arg, "is not a valid action");
		return -1;
	}

	return 0;
}

/*
 * Macros for command line options specific to timerlat
 */
#define TIMERLAT_OPT_PERIOD RTLA_OPT_LLONG_DATA('p', "period", &params->timerlat_period_us, "us", \
	"timerlat period in us", \
	LLONG_RANGE(100, 1000000))

#define TIMERLAT_OPT_STACK RTLA_OPT_LLONG('s', "stack", &params->print_stack, "us", \
	"save the stack trace at the IRQ if a thread latency is higher than the argument in us")

#define TIMERLAT_OPT_NANO OPT_CALLBACK_NOOPT('n', "nano", params, NULL, \
	"display data in nanoseconds", \
	opt_nano_cb)

#define TIMERLAT_OPT_DMA_LATENCY RTLA_OPT_INT_DATA_DEFVAL(0, "dma-latency", \
	&params->dma_latency, "us", \
	"set /dev/cpu_dma_latency latency <us> to reduce exit from idle latency", \
	INT_RANGE(0, 10000), default_dma_latency)

#define TIMERLAT_OPT_DEEPEST_IDLE_STATE RTLA_OPT_INT_DATA_DEFVAL(0, "deepest-idle-state", \
	&params->deepest_idle_state, "n", \
	"only go down to idle state n on cpus used by timerlat to reduce exit from idle latency", \
	INT_RANGE(-1, INT_MAX), default_deepest_idle_state)

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

#define TIMERLAT_OPT_ALIGNED RTLA_OPT_CALLBACK_DATA('A', "aligned", params, "us", \
	"align thread wakeups to a specific offset", \
	opt_timerlat_align_cb, LLONG_RANGE(0, LLONG_MAX))

/*
 * Callback functions for command line options for timerlat tools
 */

static int opt_timerlat_auto_cb(const struct option *opt, const char *arg, int unset)
{
	struct timerlat_cb_data *cb_data = opt->value;
	struct timerlat_params *params = cb_data->params;
	long long auto_thresh;

	if (unset) {
		params->common.stop_total_us = 0;
		params->common.stop_us = 0;
		params->print_stack = 0;
		cb_data->trace_output = NULL;
		return 0;
	}

	if (!arg)
		return -1;

	if (strtoll_safe(opt, arg, &auto_thresh))
		return -1;
	params->common.stop_total_us = auto_thresh;
	params->common.stop_us = auto_thresh;
	params->print_stack = auto_thresh;

	if (!cb_data->trace_output)
		cb_data->trace_output = "timerlat_trace.txt";

	return 0;
}

static int opt_aa_only_cb(const struct option *opt, const char *arg, int unset)
{
	struct timerlat_params *params = opt->value;
	long long auto_thresh;

	if (unset) {
		params->common.stop_total_us = 0;
		params->common.stop_us = 0;
		params->print_stack = 0;
		params->common.aa_only = 0;
		return 0;
	}

	if (!arg)
		return -1;

	if (strtoll_safe(opt, arg, &auto_thresh))
		return -1;
	params->common.stop_total_us = auto_thresh;
	params->common.stop_us = auto_thresh;
	params->print_stack = auto_thresh;
	params->common.aa_only = 1;

	return 0;
}

static int opt_timerlat_trace_output_cb(const struct option *opt, const char *arg, int unset)
{
	const char **trace_output = opt->value;

	if (unset) {
		*trace_output = NULL;
		return 0;
	}

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
	if (retval) {
		opt_err(opt, arg, "is not a valid action");
		return -1;
	}

	return 0;
}

static int opt_timerlat_on_end_cb(const struct option *opt, const char *arg, int unset)
{
	struct actions *actions = opt->value;
	int retval;

	if (unset || !arg)
		return -1;

	retval = actions_parse(actions, (char *)arg, "timerlat_trace.txt");
	if (retval) {
		opt_err(opt, arg, "is not a valid action");
		return -1;
	}

	return 0;
}

static int opt_user_threads_cb(const struct option *opt, const char *arg, int unset)
{
	struct timerlat_params *params = opt->value;

	if (unset) {
		params->common.user_workload = false;
		params->common.user_data = false;
		return 0;
	}

	params->common.user_workload = true;
	params->common.user_data = true;

	return 0;
}

static int opt_nano_cb(const struct option *opt, const char *arg, int unset)
{
	struct timerlat_params *params = opt->value;

	if (unset) {
		params->common.output_divisor = default_output_divisor;
		return 0;
	}

	params->common.output_divisor = 1;

	return 0;
}

static int opt_stack_format_cb(const struct option *opt, const char *arg, int unset)
{
	int *format = opt->value;

	if (unset) {
		*format = default_stack_format;
		return 0;
	}

	if (!arg)
		return -1;

	*format = parse_stack_format((char *)arg);

	if (*format == -1) {
		opt_err(opt, arg, "is not a valid stack format");
		return -1;
	}

	return 0;
}

static int opt_timerlat_align_cb(const struct option *opt, const char *arg, int unset)
{
	struct timerlat_params *params = opt->value;
	long long val;

	if (unset) {
		params->timerlat_align = false;
		params->timerlat_align_us = 0;
		return 0;
	}

	if (!arg)
		return -1;

	if (strtoll_safe(opt, arg, &val))
		return -1;
	if (check_llong_range(opt, val))
		return -1;

	params->timerlat_align = true;
	params->timerlat_align_us = val;

	return 0;
}

/*
 * Macros for command line options specific to histogram-based tools
 */

#define HIST_OPT_BUCKET_SIZE RTLA_OPT_INT_DATA_DEFVAL('b', "bucket-size", \
	&params->common.hist.bucket_size, "N", \
	"set the histogram bucket size (default 1)", \
	INT_RANGE(1, 999999), default_bucket_size)

#define HIST_OPT_ENTRIES RTLA_OPT_INT_DATA_DEFVAL('E', "entries", \
	&params->common.hist.entries, "N", \
	"set the number of entries of the histogram (default 256)", \
	INT_RANGE(10, 9999999), default_entries)

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

