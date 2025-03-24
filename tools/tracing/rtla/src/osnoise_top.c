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
#include <sched.h>

#include "osnoise.h"
#include "utils.h"

enum osnoise_mode {
	MODE_OSNOISE = 0,
	MODE_HWNOISE
};

/*
 * osnoise top parameters
 */
struct osnoise_top_params {
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
	int			quiet;
	int			set_sched;
	int			cgroup;
	int			hk_cpus;
	int			warmup;
	int			buffer_size;
	int			pretty_output;
	cpu_set_t		hk_cpu_set;
	struct sched_attr	sched_param;
	struct trace_events	*events;
	enum osnoise_mode	mode;
};

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
static void
osnoise_free_top(struct osnoise_top_data *data)
{
	free(data->cpu_data);
	free(data);
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
	struct osnoise_top_params *params = top->params;
	struct trace_seq *s = top->trace.seq;
	char duration[26];

	get_duration(top->start_time, duration, sizeof(duration));

	if (params->pretty_output)
		trace_seq_printf(s, "\033[2;37;40m");

	trace_seq_printf(s, "                                          ");

	if (params->mode == MODE_OSNOISE) {
		trace_seq_printf(s, "Operating System Noise");
		trace_seq_printf(s, "                                       ");
	} else if (params->mode == MODE_HWNOISE) {
		trace_seq_printf(s, "Hardware-related Noise");
	}

	trace_seq_printf(s, "                                   ");

	if (params->pretty_output)
		trace_seq_printf(s, "\033[0;0;0m");
	trace_seq_printf(s, "\n");

	trace_seq_printf(s, "duration: %9s | time is in us\n", duration);

	if (params->pretty_output)
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
	if (params->pretty_output)
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
	struct osnoise_top_params *params = tool->params;
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
osnoise_print_stats(struct osnoise_top_params *params, struct osnoise_tool *top)
{
	struct trace_instance *trace = &top->trace;
	static int nr_cpus = -1;
	int i;

	if (nr_cpus == -1)
		nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

	if (!params->quiet)
		clear_terminal(trace->seq);

	osnoise_top_header(top);

	for (i = 0; i < nr_cpus; i++) {
		if (params->cpus && !CPU_ISSET(i, &params->monitored_cpus))
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
static void osnoise_top_usage(struct osnoise_top_params *params, char *usage)
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
struct osnoise_top_params *osnoise_top_parse_args(int argc, char **argv)
{
	struct osnoise_top_params *params;
	struct trace_events *tevent;
	int retval;
	int c;

	params = calloc(1, sizeof(*params));
	if (!params)
		exit(1);

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
			params->stop_us = get_llong_from_str(optarg);

			/* set sample threshold to 1 */
			params->threshold = 1;

			/* set trace */
			params->trace_output = "osnoise_trace.txt";

			break;
		case 'c':
			retval = parse_cpu_set(optarg, &params->monitored_cpus);
			if (retval)
				osnoise_top_usage(params, "\nInvalid -c cpu list\n");
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
				osnoise_top_usage(params, "Invalid -d duration\n");
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
		case 'h':
		case '?':
			osnoise_top_usage(params, NULL);
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
				osnoise_top_usage(params, "Period longer than 10 s\n");
			break;
		case 'P':
			retval = parse_prio(optarg, &params->sched_param);
			if (retval == -1)
				osnoise_top_usage(params, "Invalid -P priority");
			params->set_sched = 1;
			break;
		case 'q':
			params->quiet = 1;
			break;
		case 'r':
			params->runtime = get_llong_from_str(optarg);
			if (params->runtime < 100)
				osnoise_top_usage(params, "Runtime shorter than 100 us\n");
			break;
		case 's':
			params->stop_us = get_llong_from_str(optarg);
			break;
		case 'S':
			params->stop_total_us = get_llong_from_str(optarg);
			break;
		case 't':
			if (optarg) {
				if (optarg[0] == '=')
					params->trace_output = &optarg[1];
				else
					params->trace_output = &optarg[0];
			} else if (optind < argc && argv[optind][0] != '-')
				params->trace_output = argv[optind];
			else
				params->trace_output = "osnoise_trace.txt";
			break;
		case 'T':
			params->threshold = get_llong_from_str(optarg);
			break;
		case '0': /* trigger */
			if (params->events) {
				retval = trace_event_add_trigger(params->events, optarg);
				if (retval) {
					err_msg("Error adding trigger %s\n", optarg);
					exit(EXIT_FAILURE);
				}
			} else {
				osnoise_top_usage(params, "--trigger requires a previous -e\n");
			}
			break;
		case '1': /* filter */
			if (params->events) {
				retval = trace_event_add_filter(params->events, optarg);
				if (retval) {
					err_msg("Error adding filter %s\n", optarg);
					exit(EXIT_FAILURE);
				}
			} else {
				osnoise_top_usage(params, "--filter requires a previous -e\n");
			}
			break;
		case '2':
			params->warmup = get_llong_from_str(optarg);
			break;
		case '3':
			params->buffer_size = get_llong_from_str(optarg);
			break;
		default:
			osnoise_top_usage(params, "Invalid option");
		}
	}

	if (geteuid()) {
		err_msg("osnoise needs root permission\n");
		exit(EXIT_FAILURE);
	}

	return params;
}

/*
 * osnoise_top_apply_config - apply the top configs to the initialized tool
 */
static int
osnoise_top_apply_config(struct osnoise_tool *tool, struct osnoise_top_params *params)
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

	if (params->mode == MODE_HWNOISE) {
		retval = osnoise_set_irq_disable(tool->context, 1);
		if (retval) {
			err_msg("Failed to set OSNOISE_IRQ_DISABLE option\n");
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
		 * Even if the user do not set a house-keeping CPU, try to
		 * move rtla to a CPU set different to the one where the user
		 * set the workload to run.
		 *
		 * No need to check results as this is an automatic attempt.
		 */
		auto_house_keeping(&params->monitored_cpus);
	}

	if (isatty(STDOUT_FILENO) && !params->quiet)
		params->pretty_output = 1;

	return 0;

out_err:
	return -1;
}

/*
 * osnoise_init_top - initialize a osnoise top tool with parameters
 */
struct osnoise_tool *osnoise_init_top(struct osnoise_top_params *params)
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

	tool->params = params;

	tep_register_event_handler(tool->trace.tep, -1, "ftrace", "osnoise",
				   osnoise_top_handler, NULL);

	return tool;
}

static int stop_tracing;
static void stop_top(int sig)
{
	stop_tracing = 1;
}

/*
 * osnoise_top_set_signals - handles the signal to stop the tool
 */
static void osnoise_top_set_signals(struct osnoise_top_params *params)
{
	signal(SIGINT, stop_top);
	if (params->duration) {
		signal(SIGALRM, stop_top);
		alarm(params->duration);
	}
}

int osnoise_top_main(int argc, char **argv)
{
	struct osnoise_top_params *params;
	struct osnoise_tool *record = NULL;
	struct osnoise_tool *tool = NULL;
	struct trace_instance *trace;
	int return_value = 1;
	int retval;

	params = osnoise_top_parse_args(argc, argv);
	if (!params)
		exit(1);

	tool = osnoise_init_top(params);
	if (!tool) {
		err_msg("Could not init osnoise top\n");
		goto out_exit;
	}

	retval = osnoise_top_apply_config(tool, params);
	if (retval) {
		err_msg("Could not apply config\n");
		goto out_free;
	}

	trace = &tool->trace;

	retval = enable_osnoise(trace);
	if (retval) {
		err_msg("Failed to enable osnoise tracer\n");
		goto out_free;
	}

	if (params->set_sched) {
		retval = set_comm_sched_attr("osnoise/", &params->sched_param);
		if (retval) {
			err_msg("Failed to set sched parameters\n");
			goto out_free;
		}
	}

	if (params->cgroup) {
		retval = set_comm_cgroup("osnoise/", params->cgroup_name);
		if (!retval) {
			err_msg("Failed to move threads to cgroup\n");
			goto out_free;
		}
	}

	if (params->trace_output) {
		record = osnoise_init_trace_tool("osnoise");
		if (!record) {
			err_msg("Failed to enable the trace instance\n");
			goto out_free;
		}

		if (params->events) {
			retval = trace_events_enable(&record->trace, params->events);
			if (retval)
				goto out_top;
		}

		if (params->buffer_size > 0) {
			retval = trace_set_buffer_size(&record->trace, params->buffer_size);
			if (retval)
				goto out_top;
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

	if (params->warmup > 0) {
		debug_msg("Warming up for %d seconds\n", params->warmup);
		sleep(params->warmup);
		if (stop_tracing)
			goto out_top;

		/*
		 * Clean up the buffer. The osnoise workload do not run
		 * with tracing off to avoid creating a performance penalty
		 * when not needed.
		 */
		retval = tracefs_instance_file_write(trace->inst, "trace", "");
		if (retval < 0) {
			debug_msg("Error cleaning up the buffer");
			goto out_top;
		}

	}

	tool->start_time = time(NULL);
	osnoise_top_set_signals(params);

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
			goto out_top;
		}

		if (!params->quiet)
			osnoise_print_stats(params, tool);

		if (osnoise_trace_is_off(tool, record))
			break;

	}

	osnoise_print_stats(params, tool);

	return_value = 0;

	if (osnoise_trace_is_off(tool, record)) {
		printf("osnoise hit stop tracing\n");
		if (params->trace_output) {
			printf("  Saving trace to %s\n", params->trace_output);
			save_trace_to_file(record->trace.inst, params->trace_output);
		}
	}

out_top:
	trace_events_destroy(&record->trace, params->events);
	params->events = NULL;
out_free:
	osnoise_free_top(tool->data);
	osnoise_destroy_tool(record);
	osnoise_destroy_tool(tool);
	free(params);
out_exit:
	exit(return_value);
}
