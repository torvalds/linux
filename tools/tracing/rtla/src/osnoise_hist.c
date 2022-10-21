// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

#include "utils.h"
#include "osnoise.h"

struct osnoise_hist_params {
	char			*cpus;
	char			*monitored_cpus;
	char			*trace_output;
	unsigned long long	runtime;
	unsigned long long	period;
	long long		threshold;
	long long		stop_us;
	long long		stop_total_us;
	int			sleep_time;
	int			duration;
	int			set_sched;
	int			output_divisor;
	struct sched_attr	sched_param;
	struct trace_events	*events;

	char			no_header;
	char			no_summary;
	char			no_index;
	char			with_zeros;
	int			bucket_size;
	int			entries;
};

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
	struct osnoise_hist_params *params = tool->params;
	struct osnoise_hist_data *data = tool->data;
	int entries = data->entries;
	int bucket;
	int *hist;

	if (params->output_divisor)
		duration = duration / params->output_divisor;

	if (data->bucket_size)
		bucket = duration / data->bucket_size;

	hist = data->hist[cpu].samples;
	data->hist[cpu].count += count;
	update_min(&data->hist[cpu].min_sample, &duration);
	update_sum(&data->hist[cpu].sum_sample, &duration);
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
	struct osnoise_hist_params *params = tool->params;
	struct osnoise_hist_data *data = tool->data;
	int bucket_size;
	char buff[128];
	int retval = 0;

	/*
	 * Set the size of the bucket.
	 */
	bucket_size = params->output_divisor * params->bucket_size;
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
	struct osnoise_hist_params *params = tool->params;
	struct osnoise_hist_data *data = tool->data;
	struct trace_seq *s = tool->trace.seq;
	char duration[26];
	int cpu;

	if (params->no_header)
		return;

	get_duration(tool->start_time, duration, sizeof(duration));
	trace_seq_printf(s, "# RTLA osnoise histogram\n");
	trace_seq_printf(s, "# Time unit is %s (%s)\n",
			params->output_divisor == 1 ? "nanoseconds" : "microseconds",
			params->output_divisor == 1 ? "ns" : "us");

	trace_seq_printf(s, "# Duration: %s\n", duration);

	if (!params->no_index)
		trace_seq_printf(s, "Index");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !params->monitored_cpus[cpu])
			continue;

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
osnoise_print_summary(struct osnoise_hist_params *params,
		       struct trace_instance *trace,
		       struct osnoise_hist_data *data)
{
	int cpu;

	if (params->no_summary)
		return;

	if (!params->no_index)
		trace_seq_printf(trace->seq, "count:");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !params->monitored_cpus[cpu])
			continue;

		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(trace->seq, "%9d ", data->hist[cpu].count);
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->no_index)
		trace_seq_printf(trace->seq, "min:  ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !params->monitored_cpus[cpu])
			continue;

		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(trace->seq, "%9llu ",	data->hist[cpu].min_sample);

	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->no_index)
		trace_seq_printf(trace->seq, "avg:  ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !params->monitored_cpus[cpu])
			continue;

		if (!data->hist[cpu].count)
			continue;

		if (data->hist[cpu].count)
			trace_seq_printf(trace->seq, "%9llu ",
					data->hist[cpu].sum_sample / data->hist[cpu].count);
		else
			trace_seq_printf(trace->seq, "        - ");
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->no_index)
		trace_seq_printf(trace->seq, "max:  ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !params->monitored_cpus[cpu])
			continue;

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
osnoise_print_stats(struct osnoise_hist_params *params, struct osnoise_tool *tool)
{
	struct osnoise_hist_data *data = tool->data;
	struct trace_instance *trace = &tool->trace;
	int bucket, cpu;
	int total;

	osnoise_hist_header(tool);

	for (bucket = 0; bucket < data->entries; bucket++) {
		total = 0;

		if (!params->no_index)
			trace_seq_printf(trace->seq, "%-6d",
					 bucket * data->bucket_size);

		for (cpu = 0; cpu < data->nr_cpus; cpu++) {
			if (params->cpus && !params->monitored_cpus[cpu])
				continue;

			if (!data->hist[cpu].count)
				continue;

			total += data->hist[cpu].samples[bucket];
			trace_seq_printf(trace->seq, "%9d ", data->hist[cpu].samples[bucket]);
		}

		if (total == 0 && !params->with_zeros) {
			trace_seq_reset(trace->seq);
			continue;
		}

		trace_seq_printf(trace->seq, "\n");
		trace_seq_do_printf(trace->seq);
		trace_seq_reset(trace->seq);
	}

	if (!params->no_index)
		trace_seq_printf(trace->seq, "over: ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !params->monitored_cpus[cpu])
			continue;

		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(trace->seq, "%9d ",
				 data->hist[cpu].samples[data->entries]);
	}
	trace_seq_printf(trace->seq, "\n");
	trace_seq_do_printf(trace->seq);
	trace_seq_reset(trace->seq);

	osnoise_print_summary(params, trace, data);
}

/*
 * osnoise_hist_usage - prints osnoise hist usage message
 */
static void osnoise_hist_usage(char *usage)
{
	int i;

	static const char * const msg[] = {
		"",
		"  usage: rtla osnoise hist [-h] [-D] [-d s] [-a us] [-p us] [-r us] [-s us] [-S us] \\",
		"	  [-T us] [-t[=file]] [-e sys[:event]] [--filter <filter>] [--trigger <trigger>] \\",
		"	  [-c cpu-list] [-P priority] [-b N] [-E N] [--no-header] [--no-summary] [--no-index] \\",
		"	  [--with-zeros]",
		"",
		"	  -h/--help: print this menu",
		"	  -a/--auto: set automatic trace mode, stopping the session if argument in us sample is hit",
		"	  -p/--period us: osnoise period in us",
		"	  -r/--runtime us: osnoise runtime in us",
		"	  -s/--stop us: stop trace if a single sample is higher than the argument in us",
		"	  -S/--stop-total us: stop trace if the total sample is higher than the argument in us",
		"	  -T/--threshold us: the minimum delta to be considered a noise",
		"	  -c/--cpus cpu-list: list of cpus to run osnoise threads",
		"	  -d/--duration time[s|m|h|d]: duration of the session",
		"	  -D/--debug: print debug info",
		"	  -t/--trace[=file]: save the stopped trace to [file|osnoise_trace.txt]",
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
		NULL,
	};

	if (usage)
		fprintf(stderr, "%s\n", usage);

	fprintf(stderr, "rtla osnoise hist: a per-cpu histogram of the OS noise (version %s)\n",
			VERSION);

	for (i = 0; msg[i]; i++)
		fprintf(stderr, "%s\n", msg[i]);
	exit(1);
}

/*
 * osnoise_hist_parse_args - allocs, parse and fill the cmd line parameters
 */
static struct osnoise_hist_params
*osnoise_hist_parse_args(int argc, char *argv[])
{
	struct osnoise_hist_params *params;
	struct trace_events *tevent;
	int retval;
	int c;

	params = calloc(1, sizeof(*params));
	if (!params)
		exit(1);

	/* display data in microseconds */
	params->output_divisor = 1000;
	params->bucket_size = 1;
	params->entries = 256;

	while (1) {
		static struct option long_options[] = {
			{"auto",		required_argument,	0, 'a'},
			{"bucket-size",		required_argument,	0, 'b'},
			{"entries",		required_argument,	0, 'E'},
			{"cpus",		required_argument,	0, 'c'},
			{"debug",		no_argument,		0, 'D'},
			{"duration",		required_argument,	0, 'd'},
			{"help",		no_argument,		0, 'h'},
			{"period",		required_argument,	0, 'p'},
			{"priority",		required_argument,	0, 'P'},
			{"runtime",		required_argument,	0, 'r'},
			{"stop",		required_argument,	0, 's'},
			{"stop-total",		required_argument,	0, 'S'},
			{"trace",		optional_argument,	0, 't'},
			{"event",		required_argument,	0, 'e'},
			{"threshold",		required_argument,	0, 'T'},
			{"no-header",		no_argument,		0, '0'},
			{"no-summary",		no_argument,		0, '1'},
			{"no-index",		no_argument,		0, '2'},
			{"with-zeros",		no_argument,		0, '3'},
			{"trigger",		required_argument,	0, '4'},
			{"filter",		required_argument,	0, '5'},
			{0, 0, 0, 0}
		};

		/* getopt_long stores the option index here. */
		int option_index = 0;

		c = getopt_long(argc, argv, "a:c:b:d:e:E:Dhp:P:r:s:S:t::T:01234:5:",
				 long_options, &option_index);

		/* detect the end of the options. */
		if (c == -1)
			break;

		switch (c) {
		case 'a':
			/* set sample stop to auto_thresh */
			params->stop_us = get_llong_from_str(optarg);

			/* set sample threshold to 1 */
			params->threshold = 1;

			/* set trace */
			params->trace_output = "osnoise_trace.txt";

			break;
		case 'b':
			params->bucket_size = get_llong_from_str(optarg);
			if ((params->bucket_size == 0) || (params->bucket_size >= 1000000))
				osnoise_hist_usage("Bucket size needs to be > 0 and <= 1000000\n");
			break;
		case 'c':
			retval = parse_cpu_list(optarg, &params->monitored_cpus);
			if (retval)
				osnoise_hist_usage("\nInvalid -c cpu list\n");
			params->cpus = optarg;
			break;
		case 'D':
			config_debug = 1;
			break;
		case 'd':
			params->duration = parse_seconds_duration(optarg);
			if (!params->duration)
				osnoise_hist_usage("Invalid -D duration\n");
			break;
		case 'e':
			tevent = trace_event_alloc(optarg);
			if (!tevent) {
				err_msg("Error alloc trace event");
				exit(EXIT_FAILURE);
			}

			if (params->events)
				tevent->next = params->events;

			params->events = tevent;
			break;
		case 'E':
			params->entries = get_llong_from_str(optarg);
			if ((params->entries < 10) || (params->entries > 9999999))
				osnoise_hist_usage("Entries must be > 10 and < 9999999\n");
			break;
		case 'h':
		case '?':
			osnoise_hist_usage(NULL);
			break;
		case 'p':
			params->period = get_llong_from_str(optarg);
			if (params->period > 10000000)
				osnoise_hist_usage("Period longer than 10 s\n");
			break;
		case 'P':
			retval = parse_prio(optarg, &params->sched_param);
			if (retval == -1)
				osnoise_hist_usage("Invalid -P priority");
			params->set_sched = 1;
			break;
		case 'r':
			params->runtime = get_llong_from_str(optarg);
			if (params->runtime < 100)
				osnoise_hist_usage("Runtime shorter than 100 us\n");
			break;
		case 's':
			params->stop_us = get_llong_from_str(optarg);
			break;
		case 'S':
			params->stop_total_us = get_llong_from_str(optarg);
			break;
		case 'T':
			params->threshold = get_llong_from_str(optarg);
			break;
		case 't':
			if (optarg)
				/* skip = */
				params->trace_output = &optarg[1];
			else
				params->trace_output = "osnoise_trace.txt";
			break;
		case '0': /* no header */
			params->no_header = 1;
			break;
		case '1': /* no summary */
			params->no_summary = 1;
			break;
		case '2': /* no index */
			params->no_index = 1;
			break;
		case '3': /* with zeros */
			params->with_zeros = 1;
			break;
		case '4': /* trigger */
			if (params->events) {
				retval = trace_event_add_trigger(params->events, optarg);
				if (retval) {
					err_msg("Error adding trigger %s\n", optarg);
					exit(EXIT_FAILURE);
				}
			} else {
				osnoise_hist_usage("--trigger requires a previous -e\n");
			}
			break;
		case '5': /* filter */
			if (params->events) {
				retval = trace_event_add_filter(params->events, optarg);
				if (retval) {
					err_msg("Error adding filter %s\n", optarg);
					exit(EXIT_FAILURE);
				}
			} else {
				osnoise_hist_usage("--filter requires a previous -e\n");
			}
			break;
		default:
			osnoise_hist_usage("Invalid option");
		}
	}

	if (geteuid()) {
		err_msg("rtla needs root permission\n");
		exit(EXIT_FAILURE);
	}

	if (params->no_index && !params->with_zeros)
		osnoise_hist_usage("no-index set and with-zeros not set - it does not make sense");

	return params;
}

/*
 * osnoise_hist_apply_config - apply the hist configs to the initialized tool
 */
static int
osnoise_hist_apply_config(struct osnoise_tool *tool, struct osnoise_hist_params *params)
{
	int retval;

	if (!params->sleep_time)
		params->sleep_time = 1;

	if (params->cpus) {
		retval = osnoise_set_cpus(tool->context, params->cpus);
		if (retval) {
			err_msg("Failed to apply CPUs config\n");
			goto out_err;
		}
	}

	if (params->runtime || params->period) {
		retval = osnoise_set_runtime_period(tool->context,
						    params->runtime,
						    params->period);
		if (retval) {
			err_msg("Failed to set runtime and/or period\n");
			goto out_err;
		}
	}

	if (params->stop_us) {
		retval = osnoise_set_stop_us(tool->context, params->stop_us);
		if (retval) {
			err_msg("Failed to set stop us\n");
			goto out_err;
		}
	}

	if (params->stop_total_us) {
		retval = osnoise_set_stop_total_us(tool->context, params->stop_total_us);
		if (retval) {
			err_msg("Failed to set stop total us\n");
			goto out_err;
		}
	}

	if (params->threshold) {
		retval = osnoise_set_tracing_thresh(tool->context, params->threshold);
		if (retval) {
			err_msg("Failed to set tracing_thresh\n");
			goto out_err;
		}
	}

	return 0;

out_err:
	return -1;
}

/*
 * osnoise_init_hist - initialize a osnoise hist tool with parameters
 */
static struct osnoise_tool
*osnoise_init_hist(struct osnoise_hist_params *params)
{
	struct osnoise_tool *tool;
	int nr_cpus;

	nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

	tool = osnoise_init_tool("osnoise_hist");
	if (!tool)
		return NULL;

	tool->data = osnoise_alloc_histogram(nr_cpus, params->entries, params->bucket_size);
	if (!tool->data)
		goto out_err;

	tool->params = params;

	return tool;

out_err:
	osnoise_destroy_tool(tool);
	return NULL;
}

static int stop_tracing;
static void stop_hist(int sig)
{
	stop_tracing = 1;
}

/*
 * osnoise_hist_set_signals - handles the signal to stop the tool
 */
static void
osnoise_hist_set_signals(struct osnoise_hist_params *params)
{
	signal(SIGINT, stop_hist);
	if (params->duration) {
		signal(SIGALRM, stop_hist);
		alarm(params->duration);
	}
}

int osnoise_hist_main(int argc, char *argv[])
{
	struct osnoise_hist_params *params;
	struct osnoise_tool *record = NULL;
	struct osnoise_tool *tool = NULL;
	struct trace_instance *trace;
	int return_value = 1;
	int retval;

	params = osnoise_hist_parse_args(argc, argv);
	if (!params)
		exit(1);

	tool = osnoise_init_hist(params);
	if (!tool) {
		err_msg("Could not init osnoise hist\n");
		goto out_exit;
	}

	retval = osnoise_hist_apply_config(tool, params);
	if (retval) {
		err_msg("Could not apply config\n");
		goto out_destroy;
	}

	trace = &tool->trace;

	retval = enable_osnoise(trace);
	if (retval) {
		err_msg("Failed to enable osnoise tracer\n");
		goto out_destroy;
	}

	retval = osnoise_init_trace_hist(tool);
	if (retval)
		goto out_destroy;

	if (params->set_sched) {
		retval = set_comm_sched_attr("osnoise/", &params->sched_param);
		if (retval) {
			err_msg("Failed to set sched parameters\n");
			goto out_free;
		}
	}

	trace_instance_start(trace);

	if (params->trace_output) {
		record = osnoise_init_trace_tool("osnoise");
		if (!record) {
			err_msg("Failed to enable the trace instance\n");
			goto out_free;
		}

		if (params->events) {
			retval = trace_events_enable(&record->trace, params->events);
			if (retval)
				goto out_hist;
		}

		trace_instance_start(&record->trace);
	}

	tool->start_time = time(NULL);
	osnoise_hist_set_signals(params);

	while (!stop_tracing) {
		sleep(params->sleep_time);

		retval = tracefs_iterate_raw_events(trace->tep,
						    trace->inst,
						    NULL,
						    0,
						    collect_registered_events,
						    trace);
		if (retval < 0) {
			err_msg("Error iterating on events\n");
			goto out_hist;
		}

		if (trace_is_off(&tool->trace, &record->trace))
			break;
	}

	osnoise_read_trace_hist(tool);

	osnoise_print_stats(params, tool);

	return_value = 0;

	if (trace_is_off(&tool->trace, &record->trace)) {
		printf("rtla osnoise hit stop tracing\n");
		if (params->trace_output) {
			printf("  Saving trace to %s\n", params->trace_output);
			save_trace_to_file(record->trace.inst, params->trace_output);
		}
	}

out_hist:
	trace_events_destroy(&record->trace, params->events);
	params->events = NULL;
out_free:
	osnoise_free_histogram(tool->data);
out_destroy:
	osnoise_destroy_tool(record);
	osnoise_destroy_tool(tool);
	free(params);
out_exit:
	exit(return_value);
}
