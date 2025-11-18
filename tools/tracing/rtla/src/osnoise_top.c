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

struct osnoise_top_cpu {
	unsigned long long	sum_runtime;
	unsigned long long	sum_noise;
	unsigned long long	max_noise;
	unsigned long long	max_sample;

	unsigned long long	hw_count;
	unsigned long long	nmi_count;
	unsigned long long	irq_count;
	unsigned long long	softirq_count;
	unsigned long long	thread_count;

	int			sum_cycles;
};

struct osnoise_top_data {
	struct osnoise_top_cpu	*cpu_data;
	int			nr_cpus;
};

/*
 * osnoise_free_top - free runtime data
 */
static void osnoise_free_top(struct osnoise_top_data *data)
{
	free(data->cpu_data);
	free(data);
}

static void osnoise_free_top_tool(struct osnoise_tool *tool)
{
	osnoise_free_top(tool->data);
}

/*
 * osnoise_alloc_histogram - alloc runtime data
 */
static struct osnoise_top_data *osnoise_alloc_top(int nr_cpus)
{
	struct osnoise_top_data *data;

	data = calloc(1, sizeof(*data));
	if (!data)
		return NULL;

	data->nr_cpus = nr_cpus;

	/* one set of histograms per CPU */
	data->cpu_data = calloc(1, sizeof(*data->cpu_data) * nr_cpus);
	if (!data->cpu_data)
		goto cleanup;

	return data;

cleanup:
	osnoise_free_top(data);
	return NULL;
}

/*
 * osnoise_top_handler - this is the handler for osnoise tracer events
 */
static int
osnoise_top_handler(struct trace_seq *s, struct tep_record *record,
		    struct tep_event *event, void *context)
{
	struct trace_instance *trace = context;
	struct osnoise_tool *tool;
	unsigned long long val;
	struct osnoise_top_cpu *cpu_data;
	struct osnoise_top_data *data;
	int cpu = record->cpu;

	tool = container_of(trace, struct osnoise_tool, trace);

	data = tool->data;
	cpu_data = &data->cpu_data[cpu];

	cpu_data->sum_cycles++;

	tep_get_field_val(s, event, "runtime", record, &val, 1);
	update_sum(&cpu_data->sum_runtime, &val);

	tep_get_field_val(s, event, "noise", record, &val, 1);
	update_max(&cpu_data->max_noise, &val);
	update_sum(&cpu_data->sum_noise, &val);

	tep_get_field_val(s, event, "max_sample", record, &val, 1);
	update_max(&cpu_data->max_sample, &val);

	tep_get_field_val(s, event, "hw_count", record, &val, 1);
	update_sum(&cpu_data->hw_count, &val);

	tep_get_field_val(s, event, "nmi_count", record, &val, 1);
	update_sum(&cpu_data->nmi_count, &val);

	tep_get_field_val(s, event, "irq_count", record, &val, 1);
	update_sum(&cpu_data->irq_count, &val);

	tep_get_field_val(s, event, "softirq_count", record, &val, 1);
	update_sum(&cpu_data->softirq_count, &val);

	tep_get_field_val(s, event, "thread_count", record, &val, 1);
	update_sum(&cpu_data->thread_count, &val);

	return 0;
}

/*
 * osnoise_top_header - print the header of the tool output
 */
static void osnoise_top_header(struct osnoise_tool *top)
{
	struct osnoise_params *params = to_osnoise_params(top->params);
	struct trace_seq *s = top->trace.seq;
	bool pretty = params->common.pretty_output;
	char duration[26];

	get_duration(top->start_time, duration, sizeof(duration));

	if (pretty)
		trace_seq_printf(s, "\033[2;37;40m");

	trace_seq_printf(s, "                                          ");

	if (params->mode == MODE_OSNOISE) {
		trace_seq_printf(s, "Operating System Noise");
		trace_seq_printf(s, "                                       ");
	} else if (params->mode == MODE_HWNOISE) {
		trace_seq_printf(s, "Hardware-related Noise");
	}

	trace_seq_printf(s, "                                   ");

	if (pretty)
		trace_seq_printf(s, "\033[0;0;0m");
	trace_seq_printf(s, "\n");

	trace_seq_printf(s, "duration: %9s | time is in us\n", duration);

	if (pretty)
		trace_seq_printf(s, "\033[2;30;47m");

	trace_seq_printf(s, "CPU Period       Runtime ");
	trace_seq_printf(s, "       Noise ");
	trace_seq_printf(s, " %% CPU Aval ");
	trace_seq_printf(s, "  Max Noise   Max Single ");
	trace_seq_printf(s, "         HW          NMI");

	if (params->mode == MODE_HWNOISE)
		goto eol;

	trace_seq_printf(s, "          IRQ      Softirq       Thread");

eol:
	if (pretty)
		trace_seq_printf(s, "\033[0;0;0m");
	trace_seq_printf(s, "\n");
}

/*
 * clear_terminal - clears the output terminal
 */
static void clear_terminal(struct trace_seq *seq)
{
	if (!config_debug)
		trace_seq_printf(seq, "\033c");
}

/*
 * osnoise_top_print - prints the output of a given CPU
 */
static void osnoise_top_print(struct osnoise_tool *tool, int cpu)
{
	struct osnoise_params *params = to_osnoise_params(tool->params);
	struct trace_seq *s = tool->trace.seq;
	struct osnoise_top_cpu *cpu_data;
	struct osnoise_top_data *data;
	int percentage;
	int decimal;

	data = tool->data;
	cpu_data = &data->cpu_data[cpu];

	if (!cpu_data->sum_runtime)
		return;

	percentage = ((cpu_data->sum_runtime - cpu_data->sum_noise) * 10000000)
			/ cpu_data->sum_runtime;
	decimal = percentage % 100000;
	percentage = percentage / 100000;

	trace_seq_printf(s, "%3d #%-6d %12llu ", cpu, cpu_data->sum_cycles, cpu_data->sum_runtime);
	trace_seq_printf(s, "%12llu ", cpu_data->sum_noise);
	trace_seq_printf(s, "  %3d.%05d", percentage, decimal);
	trace_seq_printf(s, "%12llu %12llu", cpu_data->max_noise, cpu_data->max_sample);

	trace_seq_printf(s, "%12llu ", cpu_data->hw_count);
	trace_seq_printf(s, "%12llu ", cpu_data->nmi_count);

	if (params->mode == MODE_HWNOISE) {
		trace_seq_printf(s, "\n");
		return;
	}

	trace_seq_printf(s, "%12llu ", cpu_data->irq_count);
	trace_seq_printf(s, "%12llu ", cpu_data->softirq_count);
	trace_seq_printf(s, "%12llu\n", cpu_data->thread_count);
}

/*
 * osnoise_print_stats - print data for all cpus
 */
static void
osnoise_print_stats(struct osnoise_tool *top)
{
	struct osnoise_params *params = to_osnoise_params(top->params);
	struct trace_instance *trace = &top->trace;
	static int nr_cpus = -1;
	int i;

	if (nr_cpus == -1)
		nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

	if (!params->common.quiet)
		clear_terminal(trace->seq);

	osnoise_top_header(top);

	for (i = 0; i < nr_cpus; i++) {
		if (params->common.cpus && !CPU_ISSET(i, &params->common.monitored_cpus))
			continue;
		osnoise_top_print(top, i);
	}

	trace_seq_do_printf(trace->seq);
	trace_seq_reset(trace->seq);
	osnoise_report_missed_events(top);
}

/*
 * osnoise_top_usage - prints osnoise top usage message
 */
static void osnoise_top_usage(struct osnoise_params *params, char *usage)
{
	int i;

	static const char * const msg[] = {
		" [-h] [-q] [-D] [-d s] [-a us] [-p us] [-r us] [-s us] [-S us] \\",
		"	  [-T us] [-t[file]] [-e sys[:event]] [--filter <filter>] [--trigger <trigger>] \\",
		"	  [-c cpu-list] [-H cpu-list] [-P priority] [-C[=cgroup_name]] [--warm-up s]",
		"",
		"	  -h/--help: print this menu",
		"	  -a/--auto: set automatic trace mode, stopping the session if argument in us sample is hit",
		"	  -p/--period us: osnoise period in us",
		"	  -r/--runtime us: osnoise runtime in us",
		"	  -s/--stop us: stop trace if a single sample is higher than the argument in us",
		"	  -S/--stop-total us: stop trace if the total sample is higher than the argument in us",
		"	  -T/--threshold us: the minimum delta to be considered a noise",
		"	  -c/--cpus cpu-list: list of cpus to run osnoise threads",
		"	  -H/--house-keeping cpus: run rtla control threads only on the given cpus",
		"	  -C/--cgroup[=cgroup_name]: set cgroup, if no cgroup_name is passed, the rtla's cgroup will be inherited",
		"	  -d/--duration time[s|m|h|d]: duration of the session",
		"	  -D/--debug: print debug info",
		"	  -t/--trace[file]: save the stopped trace to [file|osnoise_trace.txt]",
		"	  -e/--event <sys:event>: enable the <sys:event> in the trace instance, multiple -e are allowed",
		"	     --filter <filter>: enable a trace event filter to the previous -e event",
		"	     --trigger <trigger>: enable a trace event trigger to the previous -e event",
		"	  -q/--quiet print only a summary at the end",
		"	  -P/--priority o:prio|r:prio|f:prio|d:runtime:period : set scheduling parameters",
		"		o:prio - use SCHED_OTHER with prio",
		"		r:prio - use SCHED_RR with prio",
		"		f:prio - use SCHED_FIFO with prio",
		"		d:runtime[us|ms|s]:period[us|ms|s] - use SCHED_DEADLINE with runtime and period",
		"						       in nanoseconds",
		"	     --warm-up s: let the workload run for s seconds before collecting data",
		"	     --trace-buffer-size kB: set the per-cpu trace buffer size in kB",
		"	     --on-threshold <action>: define action to be executed at stop-total threshold, multiple are allowed",
		"	     --on-end: define action to be executed at measurement end, multiple are allowed",
		NULL,
	};

	if (usage)
		fprintf(stderr, "%s\n", usage);

	if (params->mode == MODE_OSNOISE) {
		fprintf(stderr,
			"rtla osnoise top: a per-cpu summary of the OS noise (version %s)\n",
			VERSION);

		fprintf(stderr, "  usage: rtla osnoise [top]");
	}

	if (params->mode == MODE_HWNOISE) {
		fprintf(stderr,
			"rtla hwnoise: a summary of hardware-related noise (version %s)\n",
			VERSION);

		fprintf(stderr, "  usage: rtla hwnoise");
	}

	for (i = 0; msg[i]; i++)
		fprintf(stderr, "%s\n", msg[i]);

	if (usage)
		exit(EXIT_FAILURE);

	exit(EXIT_SUCCESS);
}

/*
 * osnoise_top_parse_args - allocs, parse and fill the cmd line parameters
 */
struct common_params *osnoise_top_parse_args(int argc, char **argv)
{
	struct osnoise_params *params;
	struct trace_events *tevent;
	int retval;
	int c;
	char *trace_output = NULL;

	params = calloc(1, sizeof(*params));
	if (!params)
		exit(1);

	actions_init(&params->common.threshold_actions);
	actions_init(&params->common.end_actions);

	if (strcmp(argv[0], "hwnoise") == 0) {
		params->mode = MODE_HWNOISE;
		/*
		 * Reduce CPU usage for 75% to avoid killing the system.
		 */
		params->runtime = 750000;
		params->period = 1000000;
	}

	while (1) {
		static struct option long_options[] = {
			{"auto",		required_argument,	0, 'a'},
			{"cpus",		required_argument,	0, 'c'},
			{"cgroup",		optional_argument,	0, 'C'},
			{"debug",		no_argument,		0, 'D'},
			{"duration",		required_argument,	0, 'd'},
			{"event",		required_argument,	0, 'e'},
			{"house-keeping",	required_argument,	0, 'H'},
			{"help",		no_argument,		0, 'h'},
			{"period",		required_argument,	0, 'p'},
			{"priority",		required_argument,	0, 'P'},
			{"quiet",		no_argument,		0, 'q'},
			{"runtime",		required_argument,	0, 'r'},
			{"stop",		required_argument,	0, 's'},
			{"stop-total",		required_argument,	0, 'S'},
			{"threshold",		required_argument,	0, 'T'},
			{"trace",		optional_argument,	0, 't'},
			{"trigger",		required_argument,	0, '0'},
			{"filter",		required_argument,	0, '1'},
			{"warm-up",		required_argument,	0, '2'},
			{"trace-buffer-size",	required_argument,	0, '3'},
			{"on-threshold",	required_argument,	0, '4'},
			{"on-end",		required_argument,	0, '5'},
			{0, 0, 0, 0}
		};

		/* getopt_long stores the option index here. */
		int option_index = 0;

		c = getopt_long(argc, argv, "a:c:C::d:De:hH:p:P:qr:s:S:t::T:0:1:2:3:",
				 long_options, &option_index);

		/* Detect the end of the options. */
		if (c == -1)
			break;

		switch (c) {
		case 'a':
			/* set sample stop to auto_thresh */
			params->common.stop_us = get_llong_from_str(optarg);

			/* set sample threshold to 1 */
			params->threshold = 1;

			/* set trace */
			trace_output = "osnoise_trace.txt";

			break;
		case 'c':
			retval = parse_cpu_set(optarg, &params->common.monitored_cpus);
			if (retval)
				osnoise_top_usage(params, "\nInvalid -c cpu list\n");
			params->common.cpus = optarg;
			break;
		case 'C':
			params->common.cgroup = 1;
			if (!optarg) {
				/* will inherit this cgroup */
				params->common.cgroup_name = NULL;
			} else if (*optarg == '=') {
				/* skip the = */
				params->common.cgroup_name = ++optarg;
			}
			break;
		case 'D':
			config_debug = 1;
			break;
		case 'd':
			params->common.duration = parse_seconds_duration(optarg);
			if (!params->common.duration)
				osnoise_top_usage(params, "Invalid -d duration\n");
			break;
		case 'e':
			tevent = trace_event_alloc(optarg);
			if (!tevent) {
				err_msg("Error alloc trace event");
				exit(EXIT_FAILURE);
			}

			if (params->common.events)
				tevent->next = params->common.events;
			params->common.events = tevent;

			break;
		case 'h':
		case '?':
			osnoise_top_usage(params, NULL);
			break;
		case 'H':
			params->common.hk_cpus = 1;
			retval = parse_cpu_set(optarg, &params->common.hk_cpu_set);
			if (retval) {
				err_msg("Error parsing house keeping CPUs\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'p':
			params->period = get_llong_from_str(optarg);
			if (params->period > 10000000)
				osnoise_top_usage(params, "Period longer than 10 s\n");
			break;
		case 'P':
			retval = parse_prio(optarg, &params->common.sched_param);
			if (retval == -1)
				osnoise_top_usage(params, "Invalid -P priority");
			params->common.set_sched = 1;
			break;
		case 'q':
			params->common.quiet = 1;
			break;
		case 'r':
			params->runtime = get_llong_from_str(optarg);
			if (params->runtime < 100)
				osnoise_top_usage(params, "Runtime shorter than 100 us\n");
			break;
		case 's':
			params->common.stop_us = get_llong_from_str(optarg);
			break;
		case 'S':
			params->common.stop_total_us = get_llong_from_str(optarg);
			break;
		case 't':
			if (optarg) {
				if (optarg[0] == '=')
					trace_output = &optarg[1];
				else
					trace_output = &optarg[0];
			} else if (optind < argc && argv[optind][0] != '-')
				trace_output = argv[optind];
			else
				trace_output = "osnoise_trace.txt";
			break;
		case 'T':
			params->threshold = get_llong_from_str(optarg);
			break;
		case '0': /* trigger */
			if (params->common.events) {
				retval = trace_event_add_trigger(params->common.events, optarg);
				if (retval) {
					err_msg("Error adding trigger %s\n", optarg);
					exit(EXIT_FAILURE);
				}
			} else {
				osnoise_top_usage(params, "--trigger requires a previous -e\n");
			}
			break;
		case '1': /* filter */
			if (params->common.events) {
				retval = trace_event_add_filter(params->common.events, optarg);
				if (retval) {
					err_msg("Error adding filter %s\n", optarg);
					exit(EXIT_FAILURE);
				}
			} else {
				osnoise_top_usage(params, "--filter requires a previous -e\n");
			}
			break;
		case '2':
			params->common.warmup = get_llong_from_str(optarg);
			break;
		case '3':
			params->common.buffer_size = get_llong_from_str(optarg);
			break;
		case '4':
			retval = actions_parse(&params->common.threshold_actions, optarg,
					       "osnoise_trace.txt");
			if (retval) {
				err_msg("Invalid action %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case '5':
			retval = actions_parse(&params->common.end_actions, optarg,
					       "osnoise_trace.txt");
			if (retval) {
				err_msg("Invalid action %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		default:
			osnoise_top_usage(params, "Invalid option");
		}
	}

	if (trace_output)
		actions_add_trace_output(&params->common.threshold_actions, trace_output);

	if (geteuid()) {
		err_msg("osnoise needs root permission\n");
		exit(EXIT_FAILURE);
	}

	return &params->common;
}

/*
 * osnoise_top_apply_config - apply the top configs to the initialized tool
 */
static int
osnoise_top_apply_config(struct osnoise_tool *tool)
{
	struct osnoise_params *params = to_osnoise_params(tool->params);
	int retval;

	retval = osnoise_apply_config(tool, params);
	if (retval)
		goto out_err;

	if (params->mode == MODE_HWNOISE) {
		retval = osnoise_set_irq_disable(tool->context, 1);
		if (retval) {
			err_msg("Failed to set OSNOISE_IRQ_DISABLE option\n");
			goto out_err;
		}
	}

	if (isatty(STDOUT_FILENO) && !params->common.quiet)
		params->common.pretty_output = 1;

	return 0;

out_err:
	return -1;
}

/*
 * osnoise_init_top - initialize a osnoise top tool with parameters
 */
struct osnoise_tool *osnoise_init_top(struct common_params *params)
{
	struct osnoise_tool *tool;
	int nr_cpus;

	nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

	tool = osnoise_init_tool("osnoise_top");
	if (!tool)
		return NULL;

	tool->data = osnoise_alloc_top(nr_cpus);
	if (!tool->data) {
		osnoise_destroy_tool(tool);
		return NULL;
	}

	tep_register_event_handler(tool->trace.tep, -1, "ftrace", "osnoise",
				   osnoise_top_handler, NULL);

	return tool;
}

struct tool_ops osnoise_top_ops = {
	.tracer = "osnoise",
	.comm_prefix = "osnoise/",
	.parse_args = osnoise_top_parse_args,
	.init_tool = osnoise_init_top,
	.apply_config = osnoise_top_apply_config,
	.enable = osnoise_enable,
	.main = top_main_loop,
	.print_stats = osnoise_print_stats,
	.free = osnoise_free_top_tool,
};
