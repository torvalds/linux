// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#define _GNU_SOURCE
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

#include "osnoise.h"

struct osnoise_hist_cpu {
	int			*samples;
	int			count;

	unsigned long long	min_sample;
	unsigned long long	sum_sample;
	unsigned long long	max_sample;

};

struct osnoise_hist_data {
	struct tracefs_hist	*trace_hist;
	struct osnoise_hist_cpu	*hist;
	int			entries;
	int			bucket_size;
	int			nr_cpus;
};

/*
 * osnoise_free_histogram - free runtime data
 */
static void
osnoise_free_histogram(struct osnoise_hist_data *data)
{
	int cpu;

	/* one histogram for IRQ and one for thread, per CPU */
	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (data->hist[cpu].samples)
			free(data->hist[cpu].samples);
	}

	/* one set of histograms per CPU */
	if (data->hist)
		free(data->hist);

	free(data);
}

static void osnoise_free_hist_tool(struct osnoise_tool *tool)
{
	osnoise_free_histogram(tool->data);
}

/*
 * osnoise_alloc_histogram - alloc runtime data
 */
static struct osnoise_hist_data
*osnoise_alloc_histogram(int nr_cpus, int entries, int bucket_size)
{
	struct osnoise_hist_data *data;
	int cpu;

	data = calloc(1, sizeof(*data));
	if (!data)
		return NULL;

	data->entries = entries;
	data->bucket_size = bucket_size;
	data->nr_cpus = nr_cpus;

	data->hist = calloc(1, sizeof(*data->hist) * nr_cpus);
	if (!data->hist)
		goto cleanup;

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		data->hist[cpu].samples = calloc(1, sizeof(*data->hist->samples) * (entries + 1));
		if (!data->hist[cpu].samples)
			goto cleanup;
	}

	/* set the min to max */
	for (cpu = 0; cpu < nr_cpus; cpu++)
		data->hist[cpu].min_sample = ~0;

	return data;

cleanup:
	osnoise_free_histogram(data);
	return NULL;
}

static void osnoise_hist_update_multiple(struct osnoise_tool *tool, int cpu,
					 unsigned long long duration, int count)
{
	struct osnoise_params *params = to_osnoise_params(tool->params);
	struct osnoise_hist_data *data = tool->data;
	unsigned long long total_duration;
	int entries = data->entries;
	int bucket;
	int *hist;

	if (params->common.output_divisor)
		duration = duration / params->common.output_divisor;

	bucket = duration / data->bucket_size;

	total_duration = duration * count;

	hist = data->hist[cpu].samples;
	data->hist[cpu].count += count;
	update_min(&data->hist[cpu].min_sample, &duration);
	update_sum(&data->hist[cpu].sum_sample, &total_duration);
	update_max(&data->hist[cpu].max_sample, &duration);

	if (bucket < entries)
		hist[bucket] += count;
	else
		hist[entries] += count;
}

/*
 * osnoise_destroy_trace_hist - disable events used to collect histogram
 */
static void osnoise_destroy_trace_hist(struct osnoise_tool *tool)
{
	struct osnoise_hist_data *data = tool->data;

	tracefs_hist_pause(tool->trace.inst, data->trace_hist);
	tracefs_hist_destroy(tool->trace.inst, data->trace_hist);
}

/*
 * osnoise_init_trace_hist - enable events used to collect histogram
 */
static int osnoise_init_trace_hist(struct osnoise_tool *tool)
{
	struct osnoise_params *params = to_osnoise_params(tool->params);
	struct osnoise_hist_data *data = tool->data;
	int bucket_size;
	char buff[128];
	int retval = 0;

	/*
	 * Set the size of the bucket.
	 */
	bucket_size = params->common.output_divisor * params->common.hist.bucket_size;
	snprintf(buff, sizeof(buff), "duration.buckets=%d", bucket_size);

	data->trace_hist = tracefs_hist_alloc(tool->trace.tep, "osnoise", "sample_threshold",
			buff, TRACEFS_HIST_KEY_NORMAL);
	if (!data->trace_hist)
		return 1;

	retval = tracefs_hist_add_key(data->trace_hist, "cpu", 0);
	if (retval)
		goto out_err;

	retval = tracefs_hist_start(tool->trace.inst, data->trace_hist);
	if (retval)
		goto out_err;

	return 0;

out_err:
	osnoise_destroy_trace_hist(tool);
	return 1;
}

/*
 * osnoise_read_trace_hist - parse histogram file and file osnoise histogram
 */
static void osnoise_read_trace_hist(struct osnoise_tool *tool)
{
	struct osnoise_hist_data *data = tool->data;
	long long cpu, counter, duration;
	char *content, *position;

	tracefs_hist_pause(tool->trace.inst, data->trace_hist);

	content = tracefs_event_file_read(tool->trace.inst, "osnoise",
					  "sample_threshold",
					  "hist", NULL);
	if (!content)
		return;

	position = content;
	while (true) {
		position = strstr(position, "duration: ~");
		if (!position)
			break;
		position += strlen("duration: ~");
		duration = get_llong_from_str(position);
		if (duration == -1)
			err_msg("error reading duration from histogram\n");

		position = strstr(position, "cpu:");
		if (!position)
			break;
		position += strlen("cpu: ");
		cpu = get_llong_from_str(position);
		if (cpu == -1)
			err_msg("error reading cpu from histogram\n");

		position = strstr(position, "hitcount:");
		if (!position)
			break;
		position += strlen("hitcount: ");
		counter = get_llong_from_str(position);
		if (counter == -1)
			err_msg("error reading counter from histogram\n");

		osnoise_hist_update_multiple(tool, cpu, duration, counter);
	}
	free(content);
}

/*
 * osnoise_hist_header - print the header of the tracer to the output
 */
static void osnoise_hist_header(struct osnoise_tool *tool)
{
	struct osnoise_params *params = to_osnoise_params(tool->params);
	struct osnoise_hist_data *data = tool->data;
	struct trace_seq *s = tool->trace.seq;
	char duration[26];
	int cpu;

	if (params->common.hist.no_header)
		return;

	get_duration(tool->start_time, duration, sizeof(duration));
	trace_seq_printf(s, "# RTLA osnoise histogram\n");
	trace_seq_printf(s, "# Time unit is %s (%s)\n",
			params->common.output_divisor == 1 ? "nanoseconds" : "microseconds",
			params->common.output_divisor == 1 ? "ns" : "us");

	trace_seq_printf(s, "# Duration: %s\n", duration);

	if (!params->common.hist.no_index)
		trace_seq_printf(s, "Index");

	for_each_monitored_cpu(cpu, data->nr_cpus, &params->common) {

		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(s, "   CPU-%03d", cpu);
	}
	trace_seq_printf(s, "\n");

	trace_seq_do_printf(s);
	trace_seq_reset(s);
}

/*
 * osnoise_print_summary - print the summary of the hist data to the output
 */
static void
osnoise_print_summary(struct osnoise_params *params,
		       struct trace_instance *trace,
		       struct osnoise_hist_data *data)
{
	int cpu;

	if (params->common.hist.no_summary)
		return;

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "count:");

	for_each_monitored_cpu(cpu, data->nr_cpus, &params->common) {

		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(trace->seq, "%9d ", data->hist[cpu].count);
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "min:  ");

	for_each_monitored_cpu(cpu, data->nr_cpus, &params->common) {

		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(trace->seq, "%9llu ",	data->hist[cpu].min_sample);

	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "avg:  ");

	for_each_monitored_cpu(cpu, data->nr_cpus, &params->common) {

		if (!data->hist[cpu].count)
			continue;

		if (data->hist[cpu].count)
			trace_seq_printf(trace->seq, "%9.2f ",
				((double) data->hist[cpu].sum_sample) / data->hist[cpu].count);
		else
			trace_seq_printf(trace->seq, "        - ");
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "max:  ");

	for_each_monitored_cpu(cpu, data->nr_cpus, &params->common) {

		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(trace->seq, "%9llu ", data->hist[cpu].max_sample);

	}
	trace_seq_printf(trace->seq, "\n");
	trace_seq_do_printf(trace->seq);
	trace_seq_reset(trace->seq);
}

/*
 * osnoise_print_stats - print data for all CPUs
 */
static void
osnoise_print_stats(struct osnoise_tool *tool)
{
	struct osnoise_params *params = to_osnoise_params(tool->params);
	struct osnoise_hist_data *data = tool->data;
	struct trace_instance *trace = &tool->trace;
	int has_samples = 0;
	int bucket, cpu;
	int total;

	osnoise_hist_header(tool);

	for (bucket = 0; bucket < data->entries; bucket++) {
		total = 0;

		if (!params->common.hist.no_index)
			trace_seq_printf(trace->seq, "%-6d",
					 bucket * data->bucket_size);

		for_each_monitored_cpu(cpu, data->nr_cpus, &params->common) {

			if (!data->hist[cpu].count)
				continue;

			total += data->hist[cpu].samples[bucket];
			trace_seq_printf(trace->seq, "%9d ", data->hist[cpu].samples[bucket]);
		}

		if (total == 0 && !params->common.hist.with_zeros) {
			trace_seq_reset(trace->seq);
			continue;
		}

		/* There are samples above the threshold */
		has_samples = 1;
		trace_seq_printf(trace->seq, "\n");
		trace_seq_do_printf(trace->seq);
		trace_seq_reset(trace->seq);
	}

	/*
	 * If no samples were recorded, skip calculations, print zeroed statistics
	 * and return.
	 */
	if (!has_samples) {
		trace_seq_reset(trace->seq);
		trace_seq_printf(trace->seq, "over: 0\ncount: 0\nmin: 0\navg: 0\nmax: 0\n");
		trace_seq_do_printf(trace->seq);
		trace_seq_reset(trace->seq);
		return;
	}

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "over: ");

	for_each_monitored_cpu(cpu, data->nr_cpus, &params->common) {

		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(trace->seq, "%9d ",
				 data->hist[cpu].samples[data->entries]);
	}
	trace_seq_printf(trace->seq, "\n");
	trace_seq_do_printf(trace->seq);
	trace_seq_reset(trace->seq);

	osnoise_print_summary(params, trace, data);
	osnoise_report_missed_events(tool);
}

/*
 * osnoise_hist_usage - prints osnoise hist usage message
 */
static void osnoise_hist_usage(void)
{
	static const char * const msg_start[] = {
		"[-D] [-d s] [-a us] [-p us] [-r us] [-s us] [-S us] \\",
		"	  [-T us] [-t [file]] [-e sys[:event]] [--filter <filter>] [--trigger <trigger>] \\",
		"	  [-c cpu-list] [-H cpu-list] [-P priority] [-b N] [-E N] [--no-header] [--no-summary] \\",
		"	  [--no-index] [--with-zeros] [-C [cgroup_name]] [--warm-up]",
		NULL,
	};

	static const char * const msg_opts[] = {
		"	  -a/--auto: set automatic trace mode, stopping the session if argument in us sample is hit",
		"	  -p/--period us: osnoise period in us",
		"	  -r/--runtime us: osnoise runtime in us",
		"	  -s/--stop us: stop trace if a single sample is higher than the argument in us",
		"	  -S/--stop-total us: stop trace if the total sample is higher than the argument in us",
		"	  -T/--threshold us: the minimum delta to be considered a noise",
		"	  -c/--cpus cpu-list: list of cpus to run osnoise threads",
		"	  -H/--house-keeping cpus: run rtla control threads only on the given cpus",
		"	  -C/--cgroup [cgroup_name]: set cgroup, if no cgroup_name is passed, the rtla's cgroup will be inherited",
		"	  -d/--duration time[s|m|h|d]: duration of the session",
		"	  -D/--debug: print debug info",
		"	  -t/--trace [file]: save the stopped trace to [file|osnoise_trace.txt]",
		"	  -e/--event <sys:event>: enable the <sys:event> in the trace instance, multiple -e are allowed",
		"	     --filter <filter>: enable a trace event filter to the previous -e event",
		"	     --trigger <trigger>: enable a trace event trigger to the previous -e event",
		"	  -b/--bucket-size N: set the histogram bucket size (default 1)",
		"	  -E/--entries N: set the number of entries of the histogram (default 256)",
		"	     --no-header: do not print header",
		"	     --no-summary: do not print summary",
		"	     --no-index: do not print index",
		"	     --with-zeros: print zero only entries",
		"	  -P/--priority o:prio|r:prio|f:prio|d:runtime:period: set scheduling parameters",
		"		o:prio - use SCHED_OTHER with prio",
		"		r:prio - use SCHED_RR with prio",
		"		f:prio - use SCHED_FIFO with prio",
		"		d:runtime[us|ms|s]:period[us|ms|s] - use SCHED_DEADLINE with runtime and period",
		"						       in nanoseconds",
		"	     --warm-up: let the workload run for s seconds before collecting data",
		"	     --trace-buffer-size kB: set the per-cpu trace buffer size in kB",
		"	     --on-threshold <action>: define action to be executed at stop-total threshold, multiple are allowed",
		"	     --on-end <action>: define action to be executed at measurement end, multiple are allowed",
		NULL,
	};

	common_usage("osnoise", "hist", "a per-cpu histogram of the OS noise",
		     msg_start, msg_opts);
}

/*
 * osnoise_hist_parse_args - allocs, parse and fill the cmd line parameters
 */
static struct common_params
*osnoise_hist_parse_args(int argc, char *argv[])
{
	struct osnoise_params *params;
	int retval;
	int c;
	char *trace_output = NULL;

	params = calloc(1, sizeof(*params));
	if (!params)
		exit(1);

	actions_init(&params->common.threshold_actions);
	actions_init(&params->common.end_actions);

	/* display data in microseconds */
	params->common.output_divisor = 1000;
	params->common.hist.bucket_size = 1;
	params->common.hist.entries = 256;

	while (1) {
		static struct option long_options[] = {
			{"auto",		required_argument,	0, 'a'},
			{"bucket-size",		required_argument,	0, 'b'},
			{"entries",		required_argument,	0, 'E'},
			{"help",		no_argument,		0, 'h'},
			{"period",		required_argument,	0, 'p'},
			{"runtime",		required_argument,	0, 'r'},
			{"stop",		required_argument,	0, 's'},
			{"stop-total",		required_argument,	0, 'S'},
			{"trace",		optional_argument,	0, 't'},
			{"threshold",		required_argument,	0, 'T'},
			{"no-header",		no_argument,		0, '0'},
			{"no-summary",		no_argument,		0, '1'},
			{"no-index",		no_argument,		0, '2'},
			{"with-zeros",		no_argument,		0, '3'},
			{"trigger",		required_argument,	0, '4'},
			{"filter",		required_argument,	0, '5'},
			{"warm-up",		required_argument,	0, '6'},
			{"trace-buffer-size",	required_argument,	0, '7'},
			{"on-threshold",	required_argument,	0, '8'},
			{"on-end",		required_argument,	0, '9'},
			{0, 0, 0, 0}
		};

		if (common_parse_options(argc, argv, &params->common))
			continue;

		c = getopt_long(argc, argv, "a:b:E:hp:r:s:S:t::T:01234:5:6:7:",
				 long_options, NULL);

		/* detect the end of the options. */
		if (c == -1)
			break;

		switch (c) {
		case 'a':
			/* set sample stop to auto_thresh */
			params->common.stop_us = get_llong_from_str(optarg);

			/* set sample threshold to 1 */
			params->threshold = 1;

			/* set trace */
			if (!trace_output)
				trace_output = "osnoise_trace.txt";

			break;
		case 'b':
			params->common.hist.bucket_size = get_llong_from_str(optarg);
			if (params->common.hist.bucket_size == 0 ||
			    params->common.hist.bucket_size >= 1000000)
				fatal("Bucket size needs to be > 0 and <= 1000000");
			break;
		case 'E':
			params->common.hist.entries = get_llong_from_str(optarg);
			if (params->common.hist.entries < 10 ||
			    params->common.hist.entries > 9999999)
				fatal("Entries must be > 10 and < 9999999");
			break;
		case 'h':
		case '?':
			osnoise_hist_usage();
			break;
		case 'p':
			params->period = get_llong_from_str(optarg);
			if (params->period > 10000000)
				fatal("Period longer than 10 s");
			break;
		case 'r':
			params->runtime = get_llong_from_str(optarg);
			if (params->runtime < 100)
				fatal("Runtime shorter than 100 us");
			break;
		case 's':
			params->common.stop_us = get_llong_from_str(optarg);
			break;
		case 'S':
			params->common.stop_total_us = get_llong_from_str(optarg);
			break;
		case 'T':
			params->threshold = get_llong_from_str(optarg);
			break;
		case 't':
			trace_output = parse_optional_arg(argc, argv);
			if (!trace_output)
				trace_output = "osnoise_trace.txt";
			break;
		case '0': /* no header */
			params->common.hist.no_header = 1;
			break;
		case '1': /* no summary */
			params->common.hist.no_summary = 1;
			break;
		case '2': /* no index */
			params->common.hist.no_index = 1;
			break;
		case '3': /* with zeros */
			params->common.hist.with_zeros = 1;
			break;
		case '4': /* trigger */
			if (params->common.events) {
				retval = trace_event_add_trigger(params->common.events, optarg);
				if (retval)
					fatal("Error adding trigger %s", optarg);
			} else {
				fatal("--trigger requires a previous -e");
			}
			break;
		case '5': /* filter */
			if (params->common.events) {
				retval = trace_event_add_filter(params->common.events, optarg);
				if (retval)
					fatal("Error adding filter %s", optarg);
			} else {
				fatal("--filter requires a previous -e");
			}
			break;
		case '6':
			params->common.warmup = get_llong_from_str(optarg);
			break;
		case '7':
			params->common.buffer_size = get_llong_from_str(optarg);
			break;
		case '8':
			retval = actions_parse(&params->common.threshold_actions, optarg,
					       "osnoise_trace.txt");
			if (retval)
				fatal("Invalid action %s", optarg);
			break;
		case '9':
			retval = actions_parse(&params->common.end_actions, optarg,
					       "osnoise_trace.txt");
			if (retval)
				fatal("Invalid action %s", optarg);
			break;
		default:
			fatal("Invalid option");
		}
	}

	if (trace_output)
		actions_add_trace_output(&params->common.threshold_actions, trace_output);

	if (geteuid())
		fatal("rtla needs root permission");

	if (params->common.hist.no_index && !params->common.hist.with_zeros)
		fatal("no-index set and with-zeros not set - it does not make sense");

	return &params->common;
}

/*
 * osnoise_hist_apply_config - apply the hist configs to the initialized tool
 */
static int
osnoise_hist_apply_config(struct osnoise_tool *tool)
{
	return osnoise_apply_config(tool, to_osnoise_params(tool->params));
}

/*
 * osnoise_init_hist - initialize a osnoise hist tool with parameters
 */
static struct osnoise_tool
*osnoise_init_hist(struct common_params *params)
{
	struct osnoise_tool *tool;
	int nr_cpus;

	nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

	tool = osnoise_init_tool("osnoise_hist");
	if (!tool)
		return NULL;

	tool->data = osnoise_alloc_histogram(nr_cpus, params->hist.entries,
					     params->hist.bucket_size);
	if (!tool->data)
		goto out_err;

	return tool;

out_err:
	osnoise_destroy_tool(tool);
	return NULL;
}

static int osnoise_hist_enable(struct osnoise_tool *tool)
{
	int retval;

	retval = osnoise_init_trace_hist(tool);
	if (retval)
		return retval;

	return osnoise_enable(tool);
}

static int osnoise_hist_main_loop(struct osnoise_tool *tool)
{
	int retval;

	retval = hist_main_loop(tool);
	osnoise_read_trace_hist(tool);

	return retval;
}

struct tool_ops osnoise_hist_ops = {
	.tracer = "osnoise",
	.comm_prefix = "osnoise/",
	.parse_args = osnoise_hist_parse_args,
	.init_tool = osnoise_init_hist,
	.apply_config = osnoise_hist_apply_config,
	.enable = osnoise_hist_enable,
	.main = osnoise_hist_main_loop,
	.print_stats = osnoise_print_stats,
	.free = osnoise_free_hist_tool,
};
