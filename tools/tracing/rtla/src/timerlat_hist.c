// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

#include "utils.h"
#include "osnoise.h"
#include "timerlat.h"

struct timerlat_hist_params {
	char			*cpus;
	char			*monitored_cpus;
	char			*trace_output;
	unsigned long long	runtime;
	long long		stop_us;
	long long		stop_total_us;
	long long		timerlat_period_us;
	long long		print_stack;
	int			sleep_time;
	int			output_divisor;
	int			duration;
	int			set_sched;
	int			dma_latency;
	struct sched_attr	sched_param;
	struct trace_events	*events;

	char			no_irq;
	char			no_thread;
	char			no_header;
	char			no_summary;
	char			no_index;
	char			with_zeros;
	int			bucket_size;
	int			entries;
};

struct timerlat_hist_cpu {
	int			*irq;
	int			*thread;

	int			irq_count;
	int			thread_count;

	unsigned long long	min_irq;
	unsigned long long	sum_irq;
	unsigned long long	max_irq;

	unsigned long long	min_thread;
	unsigned long long	sum_thread;
	unsigned long long	max_thread;
};

struct timerlat_hist_data {
	struct timerlat_hist_cpu	*hist;
	int				entries;
	int				bucket_size;
	int				nr_cpus;
};

/*
 * timerlat_free_histogram - free runtime data
 */
static void
timerlat_free_histogram(struct timerlat_hist_data *data)
{
	int cpu;

	/* one histogram for IRQ and one for thread, per CPU */
	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (data->hist[cpu].irq)
			free(data->hist[cpu].irq);

		if (data->hist[cpu].thread)
			free(data->hist[cpu].thread);
	}

	/* one set of histograms per CPU */
	if (data->hist)
		free(data->hist);

	free(data);
}

/*
 * timerlat_alloc_histogram - alloc runtime data
 */
static struct timerlat_hist_data
*timerlat_alloc_histogram(int nr_cpus, int entries, int bucket_size)
{
	struct timerlat_hist_data *data;
	int cpu;

	data = calloc(1, sizeof(*data));
	if (!data)
		return NULL;

	data->entries = entries;
	data->bucket_size = bucket_size;
	data->nr_cpus = nr_cpus;

	/* one set of histograms per CPU */
	data->hist = calloc(1, sizeof(*data->hist) * nr_cpus);
	if (!data->hist)
		goto cleanup;

	/* one histogram for IRQ and one for thread, per cpu */
	for (cpu = 0; cpu < nr_cpus; cpu++) {
		data->hist[cpu].irq = calloc(1, sizeof(*data->hist->irq) * (entries + 1));
		if (!data->hist[cpu].irq)
			goto cleanup;
		data->hist[cpu].thread = calloc(1, sizeof(*data->hist->thread) * (entries + 1));
		if (!data->hist[cpu].thread)
			goto cleanup;
	}

	/* set the min to max */
	for (cpu = 0; cpu < nr_cpus; cpu++) {
		data->hist[cpu].min_irq = ~0;
		data->hist[cpu].min_thread = ~0;
	}

	return data;

cleanup:
	timerlat_free_histogram(data);
	return NULL;
}

/*
 * timerlat_hist_update - record a new timerlat occurent on cpu, updating data
 */
static void
timerlat_hist_update(struct osnoise_tool *tool, int cpu,
		     unsigned long long thread,
		     unsigned long long latency)
{
	struct timerlat_hist_params *params = tool->params;
	struct timerlat_hist_data *data = tool->data;
	int entries = data->entries;
	int bucket;
	int *hist;

	if (params->output_divisor)
		latency = latency / params->output_divisor;

	if (data->bucket_size)
		bucket = latency / data->bucket_size;

	if (!thread) {
		hist = data->hist[cpu].irq;
		data->hist[cpu].irq_count++;
		update_min(&data->hist[cpu].min_irq, &latency);
		update_sum(&data->hist[cpu].sum_irq, &latency);
		update_max(&data->hist[cpu].max_irq, &latency);
	} else {
		hist = data->hist[cpu].thread;
		data->hist[cpu].thread_count++;
		update_min(&data->hist[cpu].min_thread, &latency);
		update_sum(&data->hist[cpu].sum_thread, &latency);
		update_max(&data->hist[cpu].max_thread, &latency);
	}

	if (bucket < entries)
		hist[bucket]++;
	else
		hist[entries]++;
}

/*
 * timerlat_hist_handler - this is the handler for timerlat tracer events
 */
static int
timerlat_hist_handler(struct trace_seq *s, struct tep_record *record,
		     struct tep_event *event, void *data)
{
	struct trace_instance *trace = data;
	unsigned long long thread, latency;
	struct osnoise_tool *tool;
	int cpu = record->cpu;

	tool = container_of(trace, struct osnoise_tool, trace);

	tep_get_field_val(s, event, "context", record, &thread, 1);
	tep_get_field_val(s, event, "timer_latency", record, &latency, 1);

	timerlat_hist_update(tool, cpu, thread, latency);

	return 0;
}

/*
 * timerlat_hist_header - print the header of the tracer to the output
 */
static void timerlat_hist_header(struct osnoise_tool *tool)
{
	struct timerlat_hist_params *params = tool->params;
	struct timerlat_hist_data *data = tool->data;
	struct trace_seq *s = tool->trace.seq;
	char duration[26];
	int cpu;

	if (params->no_header)
		return;

	get_duration(tool->start_time, duration, sizeof(duration));
	trace_seq_printf(s, "# RTLA timerlat histogram\n");
	trace_seq_printf(s, "# Time unit is %s (%s)\n",
			params->output_divisor == 1 ? "nanoseconds" : "microseconds",
			params->output_divisor == 1 ? "ns" : "us");

	trace_seq_printf(s, "# Duration: %s\n", duration);

	if (!params->no_index)
		trace_seq_printf(s, "Index");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !params->monitored_cpus[cpu])
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->no_irq)
			trace_seq_printf(s, "   IRQ-%03d", cpu);

		if (!params->no_thread)
			trace_seq_printf(s, "   Thr-%03d", cpu);
	}
	trace_seq_printf(s, "\n");


	trace_seq_do_printf(s);
	trace_seq_reset(s);
}

/*
 * timerlat_print_summary - print the summary of the hist data to the output
 */
static void
timerlat_print_summary(struct timerlat_hist_params *params,
		       struct trace_instance *trace,
		       struct timerlat_hist_data *data)
{
	int cpu;

	if (params->no_summary)
		return;

	if (!params->no_index)
		trace_seq_printf(trace->seq, "count:");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !params->monitored_cpus[cpu])
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->no_irq)
			trace_seq_printf(trace->seq, "%9d ",
					data->hist[cpu].irq_count);

		if (!params->no_thread)
			trace_seq_printf(trace->seq, "%9d ",
					data->hist[cpu].thread_count);
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->no_index)
		trace_seq_printf(trace->seq, "min:  ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !params->monitored_cpus[cpu])
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->no_irq)
			trace_seq_printf(trace->seq, "%9llu ",
					data->hist[cpu].min_irq);

		if (!params->no_thread)
			trace_seq_printf(trace->seq, "%9llu ",
					data->hist[cpu].min_thread);
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->no_index)
		trace_seq_printf(trace->seq, "avg:  ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !params->monitored_cpus[cpu])
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->no_irq) {
			if (data->hist[cpu].irq_count)
				trace_seq_printf(trace->seq, "%9llu ",
						 data->hist[cpu].sum_irq / data->hist[cpu].irq_count);
			else
				trace_seq_printf(trace->seq, "        - ");
		}

		if (!params->no_thread) {
			if (data->hist[cpu].thread_count)
				trace_seq_printf(trace->seq, "%9llu ",
						data->hist[cpu].sum_thread / data->hist[cpu].thread_count);
			else
				trace_seq_printf(trace->seq, "        - ");
		}
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->no_index)
		trace_seq_printf(trace->seq, "max:  ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !params->monitored_cpus[cpu])
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->no_irq)
			trace_seq_printf(trace->seq, "%9llu ",
					data->hist[cpu].max_irq);

		if (!params->no_thread)
			trace_seq_printf(trace->seq, "%9llu ",
					data->hist[cpu].max_thread);
	}
	trace_seq_printf(trace->seq, "\n");
	trace_seq_do_printf(trace->seq);
	trace_seq_reset(trace->seq);
}

/*
 * timerlat_print_stats - print data for all CPUs
 */
static void
timerlat_print_stats(struct timerlat_hist_params *params, struct osnoise_tool *tool)
{
	struct timerlat_hist_data *data = tool->data;
	struct trace_instance *trace = &tool->trace;
	int bucket, cpu;
	int total;

	timerlat_hist_header(tool);

	for (bucket = 0; bucket < data->entries; bucket++) {
		total = 0;

		if (!params->no_index)
			trace_seq_printf(trace->seq, "%-6d",
					 bucket * data->bucket_size);

		for (cpu = 0; cpu < data->nr_cpus; cpu++) {
			if (params->cpus && !params->monitored_cpus[cpu])
				continue;

			if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
				continue;

			if (!params->no_irq) {
				total += data->hist[cpu].irq[bucket];
				trace_seq_printf(trace->seq, "%9d ",
						data->hist[cpu].irq[bucket]);
			}

			if (!params->no_thread) {
				total += data->hist[cpu].thread[bucket];
				trace_seq_printf(trace->seq, "%9d ",
						data->hist[cpu].thread[bucket]);
			}

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

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->no_irq)
			trace_seq_printf(trace->seq, "%9d ",
					 data->hist[cpu].irq[data->entries]);

		if (!params->no_thread)
			trace_seq_printf(trace->seq, "%9d ",
					 data->hist[cpu].thread[data->entries]);
	}
	trace_seq_printf(trace->seq, "\n");
	trace_seq_do_printf(trace->seq);
	trace_seq_reset(trace->seq);

	timerlat_print_summary(params, trace, data);
}

/*
 * timerlat_hist_usage - prints timerlat top usage message
 */
static void timerlat_hist_usage(char *usage)
{
	int i;

	char *msg[] = {
		"",
		"  usage: [rtla] timerlat hist [-h] [-q] [-d s] [-D] [-n] [-a us] [-p us] [-i us] [-T us] [-s us] \\",
		"         [-t[=file]] [-e sys[:event]] [--filter <filter>] [--trigger <trigger>] [-c cpu-list] \\",
		"	  [-P priority] [-E N] [-b N] [--no-irq] [--no-thread] [--no-header] [--no-summary] \\",
		"	  [--no-index] [--with-zeros] [--dma-latency us]",
		"",
		"	  -h/--help: print this menu",
		"	  -a/--auto: set automatic trace mode, stopping the session if argument in us latency is hit",
		"	  -p/--period us: timerlat period in us",
		"	  -i/--irq us: stop trace if the irq latency is higher than the argument in us",
		"	  -T/--thread us: stop trace if the thread latency is higher than the argument in us",
		"	  -s/--stack us: save the stack trace at the IRQ if a thread latency is higher than the argument in us",
		"	  -c/--cpus cpus: run the tracer only on the given cpus",
		"	  -d/--duration time[m|h|d]: duration of the session in seconds",
		"	  -D/--debug: print debug info",
		"	  -t/--trace[=file]: save the stopped trace to [file|timerlat_trace.txt]",
		"	  -e/--event <sys:event>: enable the <sys:event> in the trace instance, multiple -e are allowed",
		"	     --filter <filter>: enable a trace event filter to the previous -e event",
		"	     --trigger <trigger>: enable a trace event trigger to the previous -e event",
		"	  -n/--nano: display data in nanoseconds",
		"	  -b/--bucket-size N: set the histogram bucket size (default 1)",
		"	  -E/--entries N: set the number of entries of the histogram (default 256)",
		"	     --no-irq: ignore IRQ latencies",
		"	     --no-thread: ignore thread latencies",
		"	     --no-header: do not print header",
		"	     --no-summary: do not print summary",
		"	     --no-index: do not print index",
		"	     --with-zeros: print zero only entries",
		"	     --dma-latency us: set /dev/cpu_dma_latency latency <us> to reduce exit from idle latency",
		"	  -P/--priority o:prio|r:prio|f:prio|d:runtime:period : set scheduling parameters",
		"		o:prio - use SCHED_OTHER with prio",
		"		r:prio - use SCHED_RR with prio",
		"		f:prio - use SCHED_FIFO with prio",
		"		d:runtime[us|ms|s]:period[us|ms|s] - use SCHED_DEADLINE with runtime and period",
		"						       in nanoseconds",
		NULL,
	};

	if (usage)
		fprintf(stderr, "%s\n", usage);

	fprintf(stderr, "rtla timerlat hist: a per-cpu histogram of the timer latency (version %s)\n",
			VERSION);

	for (i = 0; msg[i]; i++)
		fprintf(stderr, "%s\n", msg[i]);
	exit(1);
}

/*
 * timerlat_hist_parse_args - allocs, parse and fill the cmd line parameters
 */
static struct timerlat_hist_params
*timerlat_hist_parse_args(int argc, char *argv[])
{
	struct timerlat_hist_params *params;
	struct trace_events *tevent;
	int auto_thresh;
	int retval;
	int c;

	params = calloc(1, sizeof(*params));
	if (!params)
		exit(1);

	/* disabled by default */
	params->dma_latency = -1;

	/* display data in microseconds */
	params->output_divisor = 1000;
	params->bucket_size = 1;
	params->entries = 256;

	while (1) {
		static struct option long_options[] = {
			{"auto",		required_argument,	0, 'a'},
			{"cpus",		required_argument,	0, 'c'},
			{"bucket-size",		required_argument,	0, 'b'},
			{"debug",		no_argument,		0, 'D'},
			{"entries",		required_argument,	0, 'E'},
			{"duration",		required_argument,	0, 'd'},
			{"help",		no_argument,		0, 'h'},
			{"irq",			required_argument,	0, 'i'},
			{"nano",		no_argument,		0, 'n'},
			{"period",		required_argument,	0, 'p'},
			{"priority",		required_argument,	0, 'P'},
			{"stack",		required_argument,	0, 's'},
			{"thread",		required_argument,	0, 'T'},
			{"trace",		optional_argument,	0, 't'},
			{"event",		required_argument,	0, 'e'},
			{"no-irq",		no_argument,		0, '0'},
			{"no-thread",		no_argument,		0, '1'},
			{"no-header",		no_argument,		0, '2'},
			{"no-summary",		no_argument,		0, '3'},
			{"no-index",		no_argument,		0, '4'},
			{"with-zeros",		no_argument,		0, '5'},
			{"trigger",		required_argument,	0, '6'},
			{"filter",		required_argument,	0, '7'},
			{"dma-latency",		required_argument,	0, '8'},
			{0, 0, 0, 0}
		};

		/* getopt_long stores the option index here. */
		int option_index = 0;

		c = getopt_long(argc, argv, "a:c:b:d:e:E:Dhi:np:P:s:t::T:0123456:7:8:",
				 long_options, &option_index);

		/* detect the end of the options. */
		if (c == -1)
			break;

		switch (c) {
		case 'a':
			auto_thresh = get_llong_from_str(optarg);

			/* set thread stop to auto_thresh */
			params->stop_total_us = auto_thresh;

			/* get stack trace */
			params->print_stack = auto_thresh;

			/* set trace */
			params->trace_output = "timerlat_trace.txt";

			break;
		case 'c':
			retval = parse_cpu_list(optarg, &params->monitored_cpus);
			if (retval)
				timerlat_hist_usage("\nInvalid -c cpu list\n");
			params->cpus = optarg;
			break;
		case 'b':
			params->bucket_size = get_llong_from_str(optarg);
			if ((params->bucket_size == 0) || (params->bucket_size >= 1000000))
				timerlat_hist_usage("Bucket size needs to be > 0 and <= 1000000\n");
			break;
		case 'D':
			config_debug = 1;
			break;
		case 'd':
			params->duration = parse_seconds_duration(optarg);
			if (!params->duration)
				timerlat_hist_usage("Invalid -D duration\n");
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
					timerlat_hist_usage("Entries must be > 10 and < 9999999\n");
			break;
		case 'h':
		case '?':
			timerlat_hist_usage(NULL);
			break;
		case 'i':
			params->stop_us = get_llong_from_str(optarg);
			break;
		case 'n':
			params->output_divisor = 1;
			break;
		case 'p':
			params->timerlat_period_us = get_llong_from_str(optarg);
			if (params->timerlat_period_us > 1000000)
				timerlat_hist_usage("Period longer than 1 s\n");
			break;
		case 'P':
			retval = parse_prio(optarg, &params->sched_param);
			if (retval == -1)
				timerlat_hist_usage("Invalid -P priority");
			params->set_sched = 1;
			break;
		case 's':
			params->print_stack = get_llong_from_str(optarg);
			break;
		case 'T':
			params->stop_total_us = get_llong_from_str(optarg);
			break;
		case 't':
			if (optarg)
				/* skip = */
				params->trace_output = &optarg[1];
			else
				params->trace_output = "timerlat_trace.txt";
			break;
		case '0': /* no irq */
			params->no_irq = 1;
			break;
		case '1': /* no thread */
			params->no_thread = 1;
			break;
		case '2': /* no header */
			params->no_header = 1;
			break;
		case '3': /* no summary */
			params->no_summary = 1;
			break;
		case '4': /* no index */
			params->no_index = 1;
			break;
		case '5': /* with zeros */
			params->with_zeros = 1;
			break;
		case '6': /* trigger */
			if (params->events) {
				retval = trace_event_add_trigger(params->events, optarg);
				if (retval) {
					err_msg("Error adding trigger %s\n", optarg);
					exit(EXIT_FAILURE);
				}
			} else {
				timerlat_hist_usage("--trigger requires a previous -e\n");
			}
			break;
		case '7': /* filter */
			if (params->events) {
				retval = trace_event_add_filter(params->events, optarg);
				if (retval) {
					err_msg("Error adding filter %s\n", optarg);
					exit(EXIT_FAILURE);
				}
			} else {
				timerlat_hist_usage("--filter requires a previous -e\n");
			}
			break;
		case '8':
			params->dma_latency = get_llong_from_str(optarg);
			if (params->dma_latency < 0 || params->dma_latency > 10000) {
				err_msg("--dma-latency needs to be >= 0 and < 10000");
				exit(EXIT_FAILURE);
			}
			break;
		default:
			timerlat_hist_usage("Invalid option");
		}
	}

	if (geteuid()) {
		err_msg("rtla needs root permission\n");
		exit(EXIT_FAILURE);
	}

	if (params->no_irq && params->no_thread)
		timerlat_hist_usage("no-irq and no-thread set, there is nothing to do here");

	if (params->no_index && !params->with_zeros)
		timerlat_hist_usage("no-index set with with-zeros is not set - it does not make sense");

	return params;
}

/*
 * timerlat_hist_apply_config - apply the hist configs to the initialized tool
 */
static int
timerlat_hist_apply_config(struct osnoise_tool *tool, struct timerlat_hist_params *params)
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

	if (params->timerlat_period_us) {
		retval = osnoise_set_timerlat_period_us(tool->context, params->timerlat_period_us);
		if (retval) {
			err_msg("Failed to set timerlat period\n");
			goto out_err;
		}
	}

	if (params->print_stack) {
		retval = osnoise_set_print_stack(tool->context, params->print_stack);
		if (retval) {
			err_msg("Failed to set print stack\n");
			goto out_err;
		}
	}

	return 0;

out_err:
	return -1;
}

/*
 * timerlat_init_hist - initialize a timerlat hist tool with parameters
 */
static struct osnoise_tool
*timerlat_init_hist(struct timerlat_hist_params *params)
{
	struct osnoise_tool *tool;
	int nr_cpus;

	nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

	tool = osnoise_init_tool("timerlat_hist");
	if (!tool)
		return NULL;

	tool->data = timerlat_alloc_histogram(nr_cpus, params->entries, params->bucket_size);
	if (!tool->data)
		goto out_err;

	tool->params = params;

	tep_register_event_handler(tool->trace.tep, -1, "ftrace", "timerlat",
				   timerlat_hist_handler, tool);

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
 * timerlat_hist_set_signals - handles the signal to stop the tool
 */
static void
timerlat_hist_set_signals(struct timerlat_hist_params *params)
{
	signal(SIGINT, stop_hist);
	if (params->duration) {
		signal(SIGALRM, stop_hist);
		alarm(params->duration);
	}
}

int timerlat_hist_main(int argc, char *argv[])
{
	struct timerlat_hist_params *params;
	struct osnoise_tool *record = NULL;
	struct osnoise_tool *tool = NULL;
	struct trace_instance *trace;
	int dma_latency_fd = -1;
	int return_value = 1;
	int retval;

	params = timerlat_hist_parse_args(argc, argv);
	if (!params)
		exit(1);

	tool = timerlat_init_hist(params);
	if (!tool) {
		err_msg("Could not init osnoise hist\n");
		goto out_exit;
	}

	retval = timerlat_hist_apply_config(tool, params);
	if (retval) {
		err_msg("Could not apply config\n");
		goto out_free;
	}

	trace = &tool->trace;

	retval = enable_timerlat(trace);
	if (retval) {
		err_msg("Failed to enable timerlat tracer\n");
		goto out_free;
	}

	if (params->set_sched) {
		retval = set_comm_sched_attr("timerlat/", &params->sched_param);
		if (retval) {
			err_msg("Failed to set sched parameters\n");
			goto out_free;
		}
	}

	if (params->dma_latency >= 0) {
		dma_latency_fd = set_cpu_dma_latency(params->dma_latency);
		if (dma_latency_fd < 0) {
			err_msg("Could not set /dev/cpu_dma_latency.\n");
			goto out_free;
		}
	}

	trace_instance_start(trace);

	if (params->trace_output) {
		record = osnoise_init_trace_tool("timerlat");
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
	timerlat_hist_set_signals(params);

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

	timerlat_print_stats(params, tool);

	return_value = 0;

	if (trace_is_off(&tool->trace, &record->trace)) {
		printf("rtla timelat hit stop tracing\n");
		if (params->trace_output) {
			printf("  Saving trace to %s\n", params->trace_output);
			save_trace_to_file(record->trace.inst, params->trace_output);
		}
	}

out_hist:
	if (dma_latency_fd >= 0)
		close(dma_latency_fd);
	trace_events_destroy(&record->trace, params->events);
	params->events = NULL;
out_free:
	timerlat_free_histogram(tool->data);
	osnoise_destroy_tool(record);
	osnoise_destroy_tool(tool);
	free(params);
out_exit:
	exit(return_value);
}
