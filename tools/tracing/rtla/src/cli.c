// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

#include <linux/compiler.h>

#define RTLA_ALLOW_CLI_P_H
#include "cli_p.h"

static const char * const osnoise_top_usage[] = {
	"rtla osnoise [top] [<options>] [-h|--help]",
	NULL,
};

static const char * const osnoise_hist_usage[] = {
	"rtla osnoise hist [<options>] [-h|--help]",
	NULL,
};

static const char * const timerlat_top_usage[] = {
	"rtla timerlat [top] [<options>] [-h|--help]",
	NULL,
};

static const char * const timerlat_hist_usage[] = {
	"rtla timerlat hist [<options>] [-h|--help]",
	NULL,
};

static const char * const hwnoise_usage[] = {
	"rtla hwnoise [<options>] [-h|--help]",
	NULL,
};

static const int common_parse_options_flags = PARSE_OPT_OPTARG_ALLOW_NEXT;

bool in_unit_test;

/*
 * osnoise_top_parse_args - allocs, parse and fill the cmd line parameters
 */
struct common_params *osnoise_top_parse_args(int argc, char **argv)
{
	struct osnoise_params *params;
	struct osnoise_cb_data cb_data;
	const char * const *usage;

	params = calloc_fatal(1, sizeof(*params));

	cb_data.params = params;
	cb_data.trace_output = NULL;

	if (strcmp(argv[0], "hwnoise") == 0) {
		params->mode = MODE_HWNOISE;
		/*
		 * Reduce CPU usage for 75% to avoid killing the system.
		 */
		params->runtime = 750000;
		params->period = 1000000;
		usage = hwnoise_usage;
	} else {
		usage = osnoise_top_usage;
	}

	const struct option osnoise_top_options[] = {
	OPT_GROUP("Tracing Options:"),
		OSNOISE_OPT_PERIOD,
		OSNOISE_OPT_RUNTIME,
		RTLA_OPT_STOP('s', "stop", "single sample"),
		RTLA_OPT_STOP_TOTAL('S', "stop-total", "total sample"),
		OSNOISE_OPT_THRESHOLD,
		RTLA_OPT_TRACE_OUTPUT("osnoise", opt_osnoise_trace_output_cb),

	OPT_GROUP("Event Configuration:"),
		RTLA_OPT_EVENT,
		RTLA_OPT_FILTER,
		RTLA_OPT_TRIGGER,

	OPT_GROUP("CPU Configuration:"),
		RTLA_OPT_CPUS,
		RTLA_OPT_HOUSEKEEPING,

	OPT_GROUP("Thread Configuration:"),
		RTLA_OPT_PRIORITY,
		RTLA_OPT_CGROUP,

	OPT_GROUP("Output:"),
		RTLA_OPT_QUIET,

	OPT_GROUP("System Tuning:"),
		RTLA_OPT_TRACE_BUFFER_SIZE,
		RTLA_OPT_WARM_UP,

	OPT_GROUP("Auto Analysis and Actions:"),
		RTLA_OPT_AUTO(opt_osnoise_auto_cb),
		RTLA_OPT_ON_THRESHOLD("stop-total", opt_osnoise_on_threshold_cb),
		RTLA_OPT_ON_END(opt_osnoise_on_end_cb),

	OPT_GROUP("General:"),
		RTLA_OPT_DURATION,
		RTLA_OPT_DEBUG,

	OPT_END(),
	};

	actions_init(&params->common.threshold_actions);
	actions_init(&params->common.end_actions);

	argc = parse_options(argc, (const char **)argv,
			     osnoise_top_options,
			     usage,
			     common_parse_options_flags);
	if (argc < 0)
		return NULL;

	if (cb_data.trace_output)
		actions_add_trace_output(&params->common.threshold_actions, cb_data.trace_output);

	if (geteuid() && !in_unit_test)
		fatal("osnoise needs root permission");

	return &params->common;
}

/*
 * osnoise_hist_parse_args - allocs, parse and fill the cmd line parameters
 */
struct common_params *osnoise_hist_parse_args(int argc, char **argv)
{
	struct osnoise_params *params;
	struct osnoise_cb_data cb_data;

	params = calloc_fatal(1, sizeof(*params));

	cb_data.params = params;
	cb_data.trace_output = NULL;

	const struct option osnoise_hist_options[] = {
	OPT_GROUP("Tracing Options:"),
		OSNOISE_OPT_PERIOD,
		OSNOISE_OPT_RUNTIME,
		RTLA_OPT_STOP('s', "stop", "single sample"),
		RTLA_OPT_STOP_TOTAL('S', "stop-total", "total sample"),
		OSNOISE_OPT_THRESHOLD,
		RTLA_OPT_TRACE_OUTPUT("osnoise", opt_osnoise_trace_output_cb),

	OPT_GROUP("Event Configuration:"),
		RTLA_OPT_EVENT,
		RTLA_OPT_FILTER,
		RTLA_OPT_TRIGGER,

	OPT_GROUP("CPU Configuration:"),
		RTLA_OPT_CPUS,
		RTLA_OPT_HOUSEKEEPING,

	OPT_GROUP("Thread Configuration:"),
		RTLA_OPT_PRIORITY,
		RTLA_OPT_CGROUP,

	OPT_GROUP("Histogram Options:"),
		HIST_OPT_BUCKET_SIZE,
		HIST_OPT_ENTRIES,
		HIST_OPT_NO_HEADER,
		HIST_OPT_NO_SUMMARY,
		HIST_OPT_NO_INDEX,
		HIST_OPT_WITH_ZEROS,

	OPT_GROUP("System Tuning:"),
		RTLA_OPT_TRACE_BUFFER_SIZE,
		RTLA_OPT_WARM_UP,

	OPT_GROUP("Auto Analysis and Actions:"),
		RTLA_OPT_AUTO(opt_osnoise_auto_cb),
		RTLA_OPT_ON_THRESHOLD("stop-total", opt_osnoise_on_threshold_cb),
		RTLA_OPT_ON_END(opt_osnoise_on_end_cb),

	OPT_GROUP("General:"),
		RTLA_OPT_DURATION,
		RTLA_OPT_DEBUG,

	OPT_END(),
	};

	actions_init(&params->common.threshold_actions);
	actions_init(&params->common.end_actions);

	/* display data in microseconds */
	params->common.output_divisor = 1000;
	params->common.hist.bucket_size = 1;
	params->common.hist.entries = 256;

	argc = parse_options(argc, (const char **)argv,
			     osnoise_hist_options, osnoise_hist_usage,
			     common_parse_options_flags);
	if (argc < 0)
		return NULL;

	if (cb_data.trace_output)
		actions_add_trace_output(&params->common.threshold_actions, cb_data.trace_output);

	if (geteuid() && !in_unit_test)
		fatal("rtla needs root permission");

	if (params->common.hist.no_index && !params->common.hist.with_zeros)
		fatal("no-index set and with-zeros not set - it does not make sense");

	return &params->common;
}

struct common_params *timerlat_top_parse_args(int argc, char **argv)
{
	struct timerlat_params *params;
	struct timerlat_cb_data cb_data;

	params = calloc_fatal(1, sizeof(*params));

	cb_data.params = params;
	cb_data.trace_output = NULL;

	const struct option timerlat_top_options[] = {
	OPT_GROUP("Tracing Options:"),
		TIMERLAT_OPT_PERIOD,
		RTLA_OPT_STOP('i', "irq", "irq latency"),
		RTLA_OPT_STOP_TOTAL('T', "thread", "thread latency"),
		TIMERLAT_OPT_STACK,
		RTLA_OPT_TRACE_OUTPUT("timerlat", opt_timerlat_trace_output_cb),

	OPT_GROUP("Event Configuration:"),
		RTLA_OPT_EVENT,
		RTLA_OPT_FILTER,
		RTLA_OPT_TRIGGER,

	OPT_GROUP("CPU Configuration:"),
		RTLA_OPT_CPUS,
		RTLA_OPT_HOUSEKEEPING,

	OPT_GROUP("Thread Configuration:"),
		RTLA_OPT_PRIORITY,
		RTLA_OPT_CGROUP,
		RTLA_OPT_USER_THREADS,
		RTLA_OPT_KERNEL_THREADS,
		RTLA_OPT_USER_LOAD,
		TIMERLAT_OPT_ALIGNED,

	OPT_GROUP("Output:"),
		TIMERLAT_OPT_NANO,
		RTLA_OPT_QUIET,

	OPT_GROUP("System Tuning:"),
		TIMERLAT_OPT_DMA_LATENCY,
		TIMERLAT_OPT_DEEPEST_IDLE_STATE,
		RTLA_OPT_TRACE_BUFFER_SIZE,
		RTLA_OPT_WARM_UP,

	OPT_GROUP("Auto Analysis and Actions:"),
		RTLA_OPT_AUTO(opt_timerlat_auto_cb),
		TIMERLAT_OPT_AA_ONLY,
		TIMERLAT_OPT_NO_AA,
		TIMERLAT_OPT_DUMPS_TASKS,
		RTLA_OPT_ON_THRESHOLD("latency", opt_timerlat_on_threshold_cb),
		RTLA_OPT_ON_END(opt_timerlat_on_end_cb),
		TIMERLAT_OPT_BPF_ACTION,
		TIMERLAT_OPT_STACK_FORMAT,

	OPT_GROUP("General:"),
		RTLA_OPT_DURATION,
		RTLA_OPT_DEBUG,

	OPT_END(),
	};

	actions_init(&params->common.threshold_actions);
	actions_init(&params->common.end_actions);

	/* disabled by default */
	params->dma_latency = -1;
	params->deepest_idle_state = -2;

	/* display data in microseconds */
	params->common.output_divisor = 1000;

	/* default to BPF mode */
	params->mode = TRACING_MODE_BPF;

	/* default to truncate stack format */
	params->stack_format = STACK_FORMAT_TRUNCATE;

	argc = parse_options(argc, (const char **)argv,
			     timerlat_top_options, timerlat_top_usage,
			     common_parse_options_flags);
	if (argc < 0)
		return NULL;

	if (cb_data.trace_output)
		actions_add_trace_output(&params->common.threshold_actions, cb_data.trace_output);

	if (geteuid() && !in_unit_test)
		fatal("rtla needs root permission");

	/*
	 * Auto analysis only happens if stop tracing, thus:
	 */
	if (!params->common.stop_us && !params->common.stop_total_us)
		params->no_aa = 1;

	if (params->no_aa && params->common.aa_only)
		fatal("--no-aa and --aa-only are mutually exclusive!");

	if (params->common.kernel_workload && params->common.user_workload)
		fatal("--kernel-threads and --user-threads are mutually exclusive!");

	/*
	 * If auto-analysis or trace output is enabled, switch from BPF mode to
	 * mixed mode
	 */
	if (params->mode == TRACING_MODE_BPF &&
		(params->common.threshold_actions.present[ACTION_TRACE_OUTPUT] ||
		params->common.end_actions.present[ACTION_TRACE_OUTPUT] ||
		!params->no_aa))
		params->mode = TRACING_MODE_MIXED;

	return &params->common;
}

struct common_params *timerlat_hist_parse_args(int argc, char **argv)
{
	struct timerlat_params *params;
	struct timerlat_cb_data cb_data;

	params = calloc_fatal(1, sizeof(*params));

	cb_data.params = params;
	cb_data.trace_output = NULL;

	const struct option timerlat_hist_options[] = {
	OPT_GROUP("Tracing Options:"),
		TIMERLAT_OPT_PERIOD,
		RTLA_OPT_STOP('i', "irq", "irq latency"),
		RTLA_OPT_STOP_TOTAL('T', "thread", "thread latency"),
		TIMERLAT_OPT_STACK,
		RTLA_OPT_TRACE_OUTPUT("timerlat", opt_timerlat_trace_output_cb),

	OPT_GROUP("Event Configuration:"),
		RTLA_OPT_EVENT,
		RTLA_OPT_FILTER,
		RTLA_OPT_TRIGGER,

	OPT_GROUP("CPU Configuration:"),
		RTLA_OPT_CPUS,
		RTLA_OPT_HOUSEKEEPING,

	OPT_GROUP("Thread Configuration:"),
		RTLA_OPT_PRIORITY,
		RTLA_OPT_CGROUP,
		RTLA_OPT_USER_THREADS,
		RTLA_OPT_KERNEL_THREADS,
		RTLA_OPT_USER_LOAD,
		TIMERLAT_OPT_ALIGNED,

	OPT_GROUP("Histogram Options:"),
		HIST_OPT_BUCKET_SIZE,
		HIST_OPT_ENTRIES,
		HIST_OPT_NO_IRQ,
		HIST_OPT_NO_THREAD,
		HIST_OPT_NO_HEADER,
		HIST_OPT_NO_SUMMARY,
		HIST_OPT_NO_INDEX,
		HIST_OPT_WITH_ZEROS,

	OPT_GROUP("Output:"),
		TIMERLAT_OPT_NANO,

	OPT_GROUP("System Tuning:"),
		TIMERLAT_OPT_DMA_LATENCY,
		TIMERLAT_OPT_DEEPEST_IDLE_STATE,
		RTLA_OPT_TRACE_BUFFER_SIZE,
		RTLA_OPT_WARM_UP,

	OPT_GROUP("Auto Analysis and Actions:"),
		RTLA_OPT_AUTO(opt_timerlat_auto_cb),
		TIMERLAT_OPT_NO_AA,
		TIMERLAT_OPT_DUMPS_TASKS,
		RTLA_OPT_ON_THRESHOLD("latency", opt_timerlat_on_threshold_cb),
		RTLA_OPT_ON_END(opt_timerlat_on_end_cb),
		TIMERLAT_OPT_BPF_ACTION,
		TIMERLAT_OPT_STACK_FORMAT,

	OPT_GROUP("General:"),
		RTLA_OPT_DURATION,
		RTLA_OPT_DEBUG,

	OPT_END(),
	};

	actions_init(&params->common.threshold_actions);
	actions_init(&params->common.end_actions);

	/* disabled by default */
	params->dma_latency = -1;

	/* disabled by default */
	params->deepest_idle_state = -2;

	/* display data in microseconds */
	params->common.output_divisor = 1000;
	params->common.hist.bucket_size = 1;
	params->common.hist.entries = 256;

	/* default to BPF mode */
	params->mode = TRACING_MODE_BPF;

	/* default to truncate stack format */
	params->stack_format = STACK_FORMAT_TRUNCATE;

	argc = parse_options(argc, (const char **)argv,
			     timerlat_hist_options, timerlat_hist_usage,
			     common_parse_options_flags);
	if (argc < 0)
		return NULL;

	if (cb_data.trace_output)
		actions_add_trace_output(&params->common.threshold_actions, cb_data.trace_output);

	if (geteuid() && !in_unit_test)
		fatal("rtla needs root permission");

	if (params->common.hist.no_irq && params->common.hist.no_thread)
		fatal("no-irq and no-thread set, there is nothing to do here");

	if (params->common.hist.no_index && !params->common.hist.with_zeros)
		fatal("no-index set with with-zeros is not set - it does not make sense");

	/*
	 * Auto analysis only happens if stop tracing, thus:
	 */
	if (!params->common.stop_us && !params->common.stop_total_us)
		params->no_aa = 1;

	if (params->common.kernel_workload && params->common.user_workload)
		fatal("--kernel-threads and --user-threads are mutually exclusive!");

	/*
	 * If auto-analysis or trace output is enabled, switch from BPF mode to
	 * mixed mode
	 */
	if (params->mode == TRACING_MODE_BPF &&
		(params->common.threshold_actions.present[ACTION_TRACE_OUTPUT] ||
		params->common.end_actions.present[ACTION_TRACE_OUTPUT] ||
		!params->no_aa))
		params->mode = TRACING_MODE_MIXED;

	return &params->common;
}

/*
 * rtla_usage - print rtla usage
 */
__noreturn static void rtla_usage(int err)
{
	int i;

	static const char * const msg[] = {
		"",
		"rtla version " VERSION,
		"",
		"  usage: rtla COMMAND ...",
		"",
		"  commands:",
		"     osnoise  - gives information about the operating system noise (osnoise)",
		"     hwnoise  - gives information about hardware-related noise",
		"     timerlat - measures the timer irq and thread latency",
		"",
		NULL,
	};

	for (i = 0; msg[i]; i++)
		fprintf(stderr, "%s\n", msg[i]);
	exit(err);
}

/*
 * run_tool_command - try to run a rtla tool command
 *
 * It returns 0 if it fails. The tool's main will generally not
 * return as they should call exit().
 */
int run_tool_command(int argc, char **argv, int start_position)
{
	if (strcmp(argv[start_position], "osnoise") == 0) {
		osnoise_main(argc-start_position, &argv[start_position]);
		goto ran;
	} else if (strcmp(argv[start_position], "hwnoise") == 0) {
		hwnoise_main(argc-start_position, &argv[start_position]);
		goto ran;
	} else if (strcmp(argv[start_position], "timerlat") == 0) {
		timerlat_main(argc-start_position, &argv[start_position]);
		goto ran;
	}

	return 0;
ran:
	return 1;
}

/* Set main as weak to allow overriding it for building unit test binary */
#pragma weak main

int main(int argc, char *argv[])
{
	int retval;

	/* is it an alias? */
	retval = run_tool_command(argc, argv, 0);
	if (retval)
		exit(0);

	if (argc < 2)
		goto usage;

	if (strcmp(argv[1], "-h") == 0)
		rtla_usage(129);
	else if (strcmp(argv[1], "--help") == 0)
		rtla_usage(129);

	retval = run_tool_command(argc, argv, 1);
	if (retval)
		exit(0);

usage:
	rtla_usage(129);
}
