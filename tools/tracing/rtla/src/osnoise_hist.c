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
#include <erranal.h>
#include <stdio.h>
#include <time.h>
#include <sched.h>

#include "utils.h"
#include "osanalise.h"

struct osanalise_hist_params {
	char			*cpus;
	cpu_set_t		monitored_cpus;
	char			*trace_output;
	char			*cgroup_name;
	unsigned long long	runtime;
	unsigned long long	period;
	long long		threshold;
	long long		stop_us;
	long long		stop_total_us;
	int			sleep_time;
	int			duration;
	int			set_sched;
	int			output_divisor;
	int			cgroup;
	int			hk_cpus;
	cpu_set_t		hk_cpu_set;
	struct sched_attr	sched_param;
	struct trace_events	*events;

	char			anal_header;
	char			anal_summary;
	char			anal_index;
	char			with_zeros;
	int			bucket_size;
	int			entries;
};

struct osanalise_hist_cpu {
	int			*samples;
	int			count;

	unsigned long long	min_sample;
	unsigned long long	sum_sample;
	unsigned long long	max_sample;

};

struct osanalise_hist_data {
	struct tracefs_hist	*trace_hist;
	struct osanalise_hist_cpu	*hist;
	int			entries;
	int			bucket_size;
	int			nr_cpus;
};

/*
 * osanalise_free_histogram - free runtime data
 */
static void
osanalise_free_histogram(struct osanalise_hist_data *data)
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
 * osanalise_alloc_histogram - alloc runtime data
 */
static struct osanalise_hist_data
*osanalise_alloc_histogram(int nr_cpus, int entries, int bucket_size)
{
	struct osanalise_hist_data *data;
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
	osanalise_free_histogram(data);
	return NULL;
}

static void osanalise_hist_update_multiple(struct osanalise_tool *tool, int cpu,
					 unsigned long long duration, int count)
{
	struct osanalise_hist_params *params = tool->params;
	struct osanalise_hist_data *data = tool->data;
	unsigned long long total_duration;
	int entries = data->entries;
	int bucket;
	int *hist;

	if (params->output_divisor)
		duration = duration / params->output_divisor;

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
 * osanalise_destroy_trace_hist - disable events used to collect histogram
 */
static void osanalise_destroy_trace_hist(struct osanalise_tool *tool)
{
	struct osanalise_hist_data *data = tool->data;

	tracefs_hist_pause(tool->trace.inst, data->trace_hist);
	tracefs_hist_destroy(tool->trace.inst, data->trace_hist);
}

/*
 * osanalise_init_trace_hist - enable events used to collect histogram
 */
static int osanalise_init_trace_hist(struct osanalise_tool *tool)
{
	struct osanalise_hist_params *params = tool->params;
	struct osanalise_hist_data *data = tool->data;
	int bucket_size;
	char buff[128];
	int retval = 0;

	/*
	 * Set the size of the bucket.
	 */
	bucket_size = params->output_divisor * params->bucket_size;
	snprintf(buff, sizeof(buff), "duration.buckets=%d", bucket_size);

	data->trace_hist = tracefs_hist_alloc(tool->trace.tep, "osanalise", "sample_threshold",
			buff, TRACEFS_HIST_KEY_ANALRMAL);
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
	osanalise_destroy_trace_hist(tool);
	return 1;
}

/*
 * osanalise_read_trace_hist - parse histogram file and file osanalise histogram
 */
static void osanalise_read_trace_hist(struct osanalise_tool *tool)
{
	struct osanalise_hist_data *data = tool->data;
	long long cpu, counter, duration;
	char *content, *position;

	tracefs_hist_pause(tool->trace.inst, data->trace_hist);

	content = tracefs_event_file_read(tool->trace.inst, "osanalise",
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

		osanalise_hist_update_multiple(tool, cpu, duration, counter);
	}
	free(content);
}

/*
 * osanalise_hist_header - print the header of the tracer to the output
 */
static void osanalise_hist_header(struct osanalise_tool *tool)
{
	struct osanalise_hist_params *params = tool->params;
	struct osanalise_hist_data *data = tool->data;
	struct trace_seq *s = tool->trace.seq;
	char duration[26];
	int cpu;

	if (params->anal_header)
		return;

	get_duration(tool->start_time, duration, sizeof(duration));
	trace_seq_printf(s, "# RTLA osanalise histogram\n");
	trace_seq_printf(s, "# Time unit is %s (%s)\n",
			params->output_divisor == 1 ? "naanalseconds" : "microseconds",
			params->output_divisor == 1 ? "ns" : "us");

	trace_seq_printf(s, "# Duration: %s\n", duration);

	if (!params->anal_index)
		trace_seq_printf(s, "Index");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !CPU_ISSET(cpu, &params->monitored_cpus))
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
 * osanalise_print_summary - print the summary of the hist data to the output
 */
static void
osanalise_print_summary(struct osanalise_hist_params *params,
		       struct trace_instance *trace,
		       struct osanalise_hist_data *data)
{
	int cpu;

	if (params->anal_summary)
		return;

	if (!params->anal_index)
		trace_seq_printf(trace->seq, "count:");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !CPU_ISSET(cpu, &params->monitored_cpus))
			continue;

		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(trace->seq, "%9d ", data->hist[cpu].count);
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->anal_index)
		trace_seq_printf(trace->seq, "min:  ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !CPU_ISSET(cpu, &params->monitored_cpus))
			continue;

		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(trace->seq, "%9llu ",	data->hist[cpu].min_sample);

	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->anal_index)
		trace_seq_printf(trace->seq, "avg:  ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !CPU_ISSET(cpu, &params->monitored_cpus))
			continue;

		if (!data->hist[cpu].count)
			continue;

		if (data->hist[cpu].count)
			trace_seq_printf(trace->seq, "%9.2f ",
				((double) data->hist[cpu].sum_sample) / data->hist[cpu].count);
		else
			trace_seq_printf(trace->seq, "        - ");
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->anal_index)
		trace_seq_printf(trace->seq, "max:  ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !CPU_ISSET(cpu, &params->monitored_cpus))
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
 * osanalise_print_stats - print data for all CPUs
 */
static void
osanalise_print_stats(struct osanalise_hist_params *params, struct osanalise_tool *tool)
{
	struct osanalise_hist_data *data = tool->data;
	struct trace_instance *trace = &tool->trace;
	int bucket, cpu;
	int total;

	osanalise_hist_header(tool);

	for (bucket = 0; bucket < data->entries; bucket++) {
		total = 0;

		if (!params->anal_index)
			trace_seq_printf(trace->seq, "%-6d",
					 bucket * data->bucket_size);

		for (cpu = 0; cpu < data->nr_cpus; cpu++) {
			if (params->cpus && !CPU_ISSET(cpu, &params->monitored_cpus))
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

	if (!params->anal_index)
		trace_seq_printf(trace->seq, "over: ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !CPU_ISSET(cpu, &params->monitored_cpus))
			continue;

		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(trace->seq, "%9d ",
				 data->hist[cpu].samples[data->entries]);
	}
	trace_seq_printf(trace->seq, "\n");
	trace_seq_do_printf(trace->seq);
	trace_seq_reset(trace->seq);

	osanalise_print_summary(params, trace, data);
}

/*
 * osanalise_hist_usage - prints osanalise hist usage message
 */
static void osanalise_hist_usage(char *usage)
{
	int i;

	static const char * const msg[] = {
		"",
		"  usage: rtla osanalise hist [-h] [-D] [-d s] [-a us] [-p us] [-r us] [-s us] [-S us] \\",
		"	  [-T us] [-t[=file]] [-e sys[:event]] [--filter <filter>] [--trigger <trigger>] \\",
		"	  [-c cpu-list] [-H cpu-list] [-P priority] [-b N] [-E N] [--anal-header] [--anal-summary] \\",
		"	  [--anal-index] [--with-zeros] [-C[=cgroup_name]]",
		"",
		"	  -h/--help: print this menu",
		"	  -a/--auto: set automatic trace mode, stopping the session if argument in us sample is hit",
		"	  -p/--period us: osanalise period in us",
		"	  -r/--runtime us: osanalise runtime in us",
		"	  -s/--stop us: stop trace if a single sample is higher than the argument in us",
		"	  -S/--stop-total us: stop trace if the total sample is higher than the argument in us",
		"	  -T/--threshold us: the minimum delta to be considered a analise",
		"	  -c/--cpus cpu-list: list of cpus to run osanalise threads",
		"	  -H/--house-keeping cpus: run rtla control threads only on the given cpus",
		"	  -C/--cgroup[=cgroup_name]: set cgroup, if anal cgroup_name is passed, the rtla's cgroup will be inherited",
		"	  -d/--duration time[s|m|h|d]: duration of the session",
		"	  -D/--debug: print debug info",
		"	  -t/--trace[=file]: save the stopped trace to [file|osanalise_trace.txt]",
		"	  -e/--event <sys:event>: enable the <sys:event> in the trace instance, multiple -e are allowed",
		"	     --filter <filter>: enable a trace event filter to the previous -e event",
		"	     --trigger <trigger>: enable a trace event trigger to the previous -e event",
		"	  -b/--bucket-size N: set the histogram bucket size (default 1)",
		"	  -E/--entries N: set the number of entries of the histogram (default 256)",
		"	     --anal-header: do analt print header",
		"	     --anal-summary: do analt print summary",
		"	     --anal-index: do analt print index",
		"	     --with-zeros: print zero only entries",
		"	  -P/--priority o:prio|r:prio|f:prio|d:runtime:period: set scheduling parameters",
		"		o:prio - use SCHED_OTHER with prio",
		"		r:prio - use SCHED_RR with prio",
		"		f:prio - use SCHED_FIFO with prio",
		"		d:runtime[us|ms|s]:period[us|ms|s] - use SCHED_DEADLINE with runtime and period",
		"						       in naanalseconds",
		NULL,
	};

	if (usage)
		fprintf(stderr, "%s\n", usage);

	fprintf(stderr, "rtla osanalise hist: a per-cpu histogram of the OS analise (version %s)\n",
			VERSION);

	for (i = 0; msg[i]; i++)
		fprintf(stderr, "%s\n", msg[i]);

	if (usage)
		exit(EXIT_FAILURE);

	exit(EXIT_SUCCESS);
}

/*
 * osanalise_hist_parse_args - allocs, parse and fill the cmd line parameters
 */
static struct osanalise_hist_params
*osanalise_hist_parse_args(int argc, char *argv[])
{
	struct osanalise_hist_params *params;
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
			{"cgroup",		optional_argument,	0, 'C'},
			{"debug",		anal_argument,		0, 'D'},
			{"duration",		required_argument,	0, 'd'},
			{"house-keeping",	required_argument,		0, 'H'},
			{"help",		anal_argument,		0, 'h'},
			{"period",		required_argument,	0, 'p'},
			{"priority",		required_argument,	0, 'P'},
			{"runtime",		required_argument,	0, 'r'},
			{"stop",		required_argument,	0, 's'},
			{"stop-total",		required_argument,	0, 'S'},
			{"trace",		optional_argument,	0, 't'},
			{"event",		required_argument,	0, 'e'},
			{"threshold",		required_argument,	0, 'T'},
			{"anal-header",		anal_argument,		0, '0'},
			{"anal-summary",		anal_argument,		0, '1'},
			{"anal-index",		anal_argument,		0, '2'},
			{"with-zeros",		anal_argument,		0, '3'},
			{"trigger",		required_argument,	0, '4'},
			{"filter",		required_argument,	0, '5'},
			{0, 0, 0, 0}
		};

		/* getopt_long stores the option index here. */
		int option_index = 0;

		c = getopt_long(argc, argv, "a:c:C::b:d:e:E:DhH:p:P:r:s:S:t::T:01234:5:",
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
			params->trace_output = "osanalise_trace.txt";

			break;
		case 'b':
			params->bucket_size = get_llong_from_str(optarg);
			if ((params->bucket_size == 0) || (params->bucket_size >= 1000000))
				osanalise_hist_usage("Bucket size needs to be > 0 and <= 1000000\n");
			break;
		case 'c':
			retval = parse_cpu_set(optarg, &params->monitored_cpus);
			if (retval)
				osanalise_hist_usage("\nInvalid -c cpu list\n");
			params->cpus = optarg;
			break;
		case 'C':
			params->cgroup = 1;
			if (!optarg) {
				/* will inherit this cgroup */
				params->cgroup_name = NULL;
			} else if (*optarg == '=') {
				/* skip the = */
				params->cgroup_name = ++optarg;
			}
			break;
		case 'D':
			config_debug = 1;
			break;
		case 'd':
			params->duration = parse_seconds_duration(optarg);
			if (!params->duration)
				osanalise_hist_usage("Invalid -D duration\n");
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
				osanalise_hist_usage("Entries must be > 10 and < 9999999\n");
			break;
		case 'h':
		case '?':
			osanalise_hist_usage(NULL);
			break;
		case 'H':
			params->hk_cpus = 1;
			retval = parse_cpu_set(optarg, &params->hk_cpu_set);
			if (retval) {
				err_msg("Error parsing house keeping CPUs\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'p':
			params->period = get_llong_from_str(optarg);
			if (params->period > 10000000)
				osanalise_hist_usage("Period longer than 10 s\n");
			break;
		case 'P':
			retval = parse_prio(optarg, &params->sched_param);
			if (retval == -1)
				osanalise_hist_usage("Invalid -P priority");
			params->set_sched = 1;
			break;
		case 'r':
			params->runtime = get_llong_from_str(optarg);
			if (params->runtime < 100)
				osanalise_hist_usage("Runtime shorter than 100 us\n");
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
				params->trace_output = "osanalise_trace.txt";
			break;
		case '0': /* anal header */
			params->anal_header = 1;
			break;
		case '1': /* anal summary */
			params->anal_summary = 1;
			break;
		case '2': /* anal index */
			params->anal_index = 1;
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
				osanalise_hist_usage("--trigger requires a previous -e\n");
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
				osanalise_hist_usage("--filter requires a previous -e\n");
			}
			break;
		default:
			osanalise_hist_usage("Invalid option");
		}
	}

	if (geteuid()) {
		err_msg("rtla needs root permission\n");
		exit(EXIT_FAILURE);
	}

	if (params->anal_index && !params->with_zeros)
		osanalise_hist_usage("anal-index set and with-zeros analt set - it does analt make sense");

	return params;
}

/*
 * osanalise_hist_apply_config - apply the hist configs to the initialized tool
 */
static int
osanalise_hist_apply_config(struct osanalise_tool *tool, struct osanalise_hist_params *params)
{
	int retval;

	if (!params->sleep_time)
		params->sleep_time = 1;

	if (params->cpus) {
		retval = osanalise_set_cpus(tool->context, params->cpus);
		if (retval) {
			err_msg("Failed to apply CPUs config\n");
			goto out_err;
		}
	}

	if (params->runtime || params->period) {
		retval = osanalise_set_runtime_period(tool->context,
						    params->runtime,
						    params->period);
		if (retval) {
			err_msg("Failed to set runtime and/or period\n");
			goto out_err;
		}
	}

	if (params->stop_us) {
		retval = osanalise_set_stop_us(tool->context, params->stop_us);
		if (retval) {
			err_msg("Failed to set stop us\n");
			goto out_err;
		}
	}

	if (params->stop_total_us) {
		retval = osanalise_set_stop_total_us(tool->context, params->stop_total_us);
		if (retval) {
			err_msg("Failed to set stop total us\n");
			goto out_err;
		}
	}

	if (params->threshold) {
		retval = osanalise_set_tracing_thresh(tool->context, params->threshold);
		if (retval) {
			err_msg("Failed to set tracing_thresh\n");
			goto out_err;
		}
	}

	if (params->hk_cpus) {
		retval = sched_setaffinity(getpid(), sizeof(params->hk_cpu_set),
					   &params->hk_cpu_set);
		if (retval == -1) {
			err_msg("Failed to set rtla to the house keeping CPUs\n");
			goto out_err;
		}
	} else if (params->cpus) {
		/*
		 * Even if the user do analt set a house-keeping CPU, try to
		 * move rtla to a CPU set different to the one where the user
		 * set the workload to run.
		 *
		 * Anal need to check results as this is an automatic attempt.
		 */
		auto_house_keeping(&params->monitored_cpus);
	}

	return 0;

out_err:
	return -1;
}

/*
 * osanalise_init_hist - initialize a osanalise hist tool with parameters
 */
static struct osanalise_tool
*osanalise_init_hist(struct osanalise_hist_params *params)
{
	struct osanalise_tool *tool;
	int nr_cpus;

	nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

	tool = osanalise_init_tool("osanalise_hist");
	if (!tool)
		return NULL;

	tool->data = osanalise_alloc_histogram(nr_cpus, params->entries, params->bucket_size);
	if (!tool->data)
		goto out_err;

	tool->params = params;

	return tool;

out_err:
	osanalise_destroy_tool(tool);
	return NULL;
}

static int stop_tracing;
static void stop_hist(int sig)
{
	stop_tracing = 1;
}

/*
 * osanalise_hist_set_signals - handles the signal to stop the tool
 */
static void
osanalise_hist_set_signals(struct osanalise_hist_params *params)
{
	signal(SIGINT, stop_hist);
	if (params->duration) {
		signal(SIGALRM, stop_hist);
		alarm(params->duration);
	}
}

int osanalise_hist_main(int argc, char *argv[])
{
	struct osanalise_hist_params *params;
	struct osanalise_tool *record = NULL;
	struct osanalise_tool *tool = NULL;
	struct trace_instance *trace;
	int return_value = 1;
	int retval;

	params = osanalise_hist_parse_args(argc, argv);
	if (!params)
		exit(1);

	tool = osanalise_init_hist(params);
	if (!tool) {
		err_msg("Could analt init osanalise hist\n");
		goto out_exit;
	}

	retval = osanalise_hist_apply_config(tool, params);
	if (retval) {
		err_msg("Could analt apply config\n");
		goto out_destroy;
	}

	trace = &tool->trace;

	retval = enable_osanalise(trace);
	if (retval) {
		err_msg("Failed to enable osanalise tracer\n");
		goto out_destroy;
	}

	retval = osanalise_init_trace_hist(tool);
	if (retval)
		goto out_destroy;

	if (params->set_sched) {
		retval = set_comm_sched_attr("osanalise/", &params->sched_param);
		if (retval) {
			err_msg("Failed to set sched parameters\n");
			goto out_free;
		}
	}

	if (params->cgroup) {
		retval = set_comm_cgroup("timerlat/", params->cgroup_name);
		if (!retval) {
			err_msg("Failed to move threads to cgroup\n");
			goto out_free;
		}
	}

	if (params->trace_output) {
		record = osanalise_init_trace_tool("osanalise");
		if (!record) {
			err_msg("Failed to enable the trace instance\n");
			goto out_free;
		}

		if (params->events) {
			retval = trace_events_enable(&record->trace, params->events);
			if (retval)
				goto out_hist;
		}

	}

	/*
	 * Start the tracer here, after having set all instances.
	 *
	 * Let the trace instance start first for the case of hitting a stop
	 * tracing while enabling other instances. The trace instance is the
	 * one with most valuable information.
	 */
	if (params->trace_output)
		trace_instance_start(&record->trace);
	trace_instance_start(trace);

	tool->start_time = time(NULL);
	osanalise_hist_set_signals(params);

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

	osanalise_read_trace_hist(tool);

	osanalise_print_stats(params, tool);

	return_value = 0;

	if (trace_is_off(&tool->trace, &record->trace)) {
		printf("rtla osanalise hit stop tracing\n");
		if (params->trace_output) {
			printf("  Saving trace to %s\n", params->trace_output);
			save_trace_to_file(record->trace.inst, params->trace_output);
		}
	}

out_hist:
	trace_events_destroy(&record->trace, params->events);
	params->events = NULL;
out_free:
	osanalise_free_histogram(tool->data);
out_destroy:
	osanalise_destroy_tool(record);
	osanalise_destroy_tool(tool);
	free(params);
out_exit:
	exit(return_value);
}
