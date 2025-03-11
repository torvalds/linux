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
#include <errno.h>
#include <sched.h>
#include <pthread.h>

#include "utils.h"
#include "osnoise.h"
#include "timerlat.h"
#include "timerlat_aa.h"
#include "timerlat_u.h"

struct timerlat_top_params {
	char			*cpus;
	cpu_set_t		monitored_cpus;
	char			*trace_output;
	char			*cgroup_name;
	unsigned long long	runtime;
	long long		stop_us;
	long long		stop_total_us;
	long long		timerlat_period_us;
	long long		print_stack;
	int			sleep_time;
	int			output_divisor;
	int			duration;
	int			quiet;
	int			set_sched;
	int			dma_latency;
	int			no_aa;
	int			aa_only;
	int			dump_tasks;
	int			cgroup;
	int			hk_cpus;
	int			user_top;
	int			user_workload;
	int			kernel_workload;
	int			pretty_output;
	int			warmup;
	int			buffer_size;
	int			deepest_idle_state;
	cpu_set_t		hk_cpu_set;
	struct sched_attr	sched_param;
	struct trace_events	*events;
};

struct timerlat_top_cpu {
	unsigned long long	irq_count;
	unsigned long long	thread_count;
	unsigned long long	user_count;

	unsigned long long	cur_irq;
	unsigned long long	min_irq;
	unsigned long long	sum_irq;
	unsigned long long	max_irq;

	unsigned long long	cur_thread;
	unsigned long long	min_thread;
	unsigned long long	sum_thread;
	unsigned long long	max_thread;

	unsigned long long	cur_user;
	unsigned long long	min_user;
	unsigned long long	sum_user;
	unsigned long long	max_user;
};

struct timerlat_top_data {
	struct timerlat_top_cpu	*cpu_data;
	int			nr_cpus;
};

/*
 * timerlat_free_top - free runtime data
 */
static void
timerlat_free_top(struct timerlat_top_data *data)
{
	free(data->cpu_data);
	free(data);
}

/*
 * timerlat_alloc_histogram - alloc runtime data
 */
static struct timerlat_top_data *timerlat_alloc_top(int nr_cpus)
{
	struct timerlat_top_data *data;
	int cpu;

	data = calloc(1, sizeof(*data));
	if (!data)
		return NULL;

	data->nr_cpus = nr_cpus;

	/* one set of histograms per CPU */
	data->cpu_data = calloc(1, sizeof(*data->cpu_data) * nr_cpus);
	if (!data->cpu_data)
		goto cleanup;

	/* set the min to max */
	for (cpu = 0; cpu < nr_cpus; cpu++) {
		data->cpu_data[cpu].min_irq = ~0;
		data->cpu_data[cpu].min_thread = ~0;
		data->cpu_data[cpu].min_user = ~0;
	}

	return data;

cleanup:
	timerlat_free_top(data);
	return NULL;
}

static void
timerlat_top_reset_sum(struct timerlat_top_cpu *summary)
{
	memset(summary, 0, sizeof(*summary));
	summary->min_irq = ~0;
	summary->min_thread = ~0;
	summary->min_user = ~0;
}

static void
timerlat_top_update_sum(struct osnoise_tool *tool, int cpu, struct timerlat_top_cpu *sum)
{
	struct timerlat_top_data *data = tool->data;
	struct timerlat_top_cpu *cpu_data = &data->cpu_data[cpu];

	sum->irq_count += cpu_data->irq_count;
	update_min(&sum->min_irq, &cpu_data->min_irq);
	update_sum(&sum->sum_irq, &cpu_data->sum_irq);
	update_max(&sum->max_irq, &cpu_data->max_irq);

	sum->thread_count += cpu_data->thread_count;
	update_min(&sum->min_thread, &cpu_data->min_thread);
	update_sum(&sum->sum_thread, &cpu_data->sum_thread);
	update_max(&sum->max_thread, &cpu_data->max_thread);

	sum->user_count += cpu_data->user_count;
	update_min(&sum->min_user, &cpu_data->min_user);
	update_sum(&sum->sum_user, &cpu_data->sum_user);
	update_max(&sum->max_user, &cpu_data->max_user);
}

/*
 * timerlat_hist_update - record a new timerlat occurent on cpu, updating data
 */
static void
timerlat_top_update(struct osnoise_tool *tool, int cpu,
		    unsigned long long thread,
		    unsigned long long latency)
{
	struct timerlat_top_data *data = tool->data;
	struct timerlat_top_cpu *cpu_data = &data->cpu_data[cpu];

	if (!thread) {
		cpu_data->irq_count++;
		cpu_data->cur_irq = latency;
		update_min(&cpu_data->min_irq, &latency);
		update_sum(&cpu_data->sum_irq, &latency);
		update_max(&cpu_data->max_irq, &latency);
	} else if (thread == 1) {
		cpu_data->thread_count++;
		cpu_data->cur_thread = latency;
		update_min(&cpu_data->min_thread, &latency);
		update_sum(&cpu_data->sum_thread, &latency);
		update_max(&cpu_data->max_thread, &latency);
	} else {
		cpu_data->user_count++;
		cpu_data->cur_user = latency;
		update_min(&cpu_data->min_user, &latency);
		update_sum(&cpu_data->sum_user, &latency);
		update_max(&cpu_data->max_user, &latency);
	}
}

/*
 * timerlat_top_handler - this is the handler for timerlat tracer events
 */
static int
timerlat_top_handler(struct trace_seq *s, struct tep_record *record,
		     struct tep_event *event, void *context)
{
	struct trace_instance *trace = context;
	struct timerlat_top_params *params;
	unsigned long long latency, thread;
	struct osnoise_tool *top;
	int cpu = record->cpu;

	top = container_of(trace, struct osnoise_tool, trace);
	params = top->params;

	if (!params->aa_only) {
		tep_get_field_val(s, event, "context", record, &thread, 1);
		tep_get_field_val(s, event, "timer_latency", record, &latency, 1);

		timerlat_top_update(top, cpu, thread, latency);
	}

	return 0;
}

/*
 * timerlat_top_header - print the header of the tool output
 */
static void timerlat_top_header(struct timerlat_top_params *params, struct osnoise_tool *top)
{
	struct trace_seq *s = top->trace.seq;
	char duration[26];

	get_duration(top->start_time, duration, sizeof(duration));

	if (params->pretty_output)
		trace_seq_printf(s, "\033[2;37;40m");

	trace_seq_printf(s, "                                     Timer Latency                                              ");
	if (params->user_top)
		trace_seq_printf(s, "                                         ");

	if (params->pretty_output)
		trace_seq_printf(s, "\033[0;0;0m");
	trace_seq_printf(s, "\n");

	trace_seq_printf(s, "%-6s   |          IRQ Timer Latency (%s)        |         Thread Timer Latency (%s)", duration,
			params->output_divisor == 1 ? "ns" : "us",
			params->output_divisor == 1 ? "ns" : "us");

	if (params->user_top) {
		trace_seq_printf(s, "      |    Ret user Timer Latency (%s)",
				params->output_divisor == 1 ? "ns" : "us");
	}

	trace_seq_printf(s, "\n");
	if (params->pretty_output)
		trace_seq_printf(s, "\033[2;30;47m");

	trace_seq_printf(s, "CPU COUNT      |      cur       min       avg       max |      cur       min       avg       max");
	if (params->user_top)
		trace_seq_printf(s, " |      cur       min       avg       max");

	if (params->pretty_output)
		trace_seq_printf(s, "\033[0;0;0m");
	trace_seq_printf(s, "\n");
}

static const char *no_value = "        -";

/*
 * timerlat_top_print - prints the output of a given CPU
 */
static void timerlat_top_print(struct osnoise_tool *top, int cpu)
{

	struct timerlat_top_params *params = top->params;
	struct timerlat_top_data *data = top->data;
	struct timerlat_top_cpu *cpu_data = &data->cpu_data[cpu];
	int divisor = params->output_divisor;
	struct trace_seq *s = top->trace.seq;

	if (divisor == 0)
		return;

	/*
	 * Skip if no data is available: is this cpu offline?
	 */
	if (!cpu_data->irq_count && !cpu_data->thread_count)
		return;

	/*
	 * Unless trace is being lost, IRQ counter is always the max.
	 */
	trace_seq_printf(s, "%3d #%-9llu |", cpu, cpu_data->irq_count);

	if (!cpu_data->irq_count) {
		trace_seq_printf(s, "%s %s %s %s |", no_value, no_value, no_value, no_value);
	} else {
		trace_seq_printf(s, "%9llu ", cpu_data->cur_irq / params->output_divisor);
		trace_seq_printf(s, "%9llu ", cpu_data->min_irq / params->output_divisor);
		trace_seq_printf(s, "%9llu ", (cpu_data->sum_irq / cpu_data->irq_count) / divisor);
		trace_seq_printf(s, "%9llu |", cpu_data->max_irq / divisor);
	}

	if (!cpu_data->thread_count) {
		trace_seq_printf(s, "%s %s %s %s", no_value, no_value, no_value, no_value);
	} else {
		trace_seq_printf(s, "%9llu ", cpu_data->cur_thread / divisor);
		trace_seq_printf(s, "%9llu ", cpu_data->min_thread / divisor);
		trace_seq_printf(s, "%9llu ",
				(cpu_data->sum_thread / cpu_data->thread_count) / divisor);
		trace_seq_printf(s, "%9llu", cpu_data->max_thread / divisor);
	}

	if (!params->user_top) {
		trace_seq_printf(s, "\n");
		return;
	}

	trace_seq_printf(s, " |");

	if (!cpu_data->user_count) {
		trace_seq_printf(s, "%s %s %s %s\n", no_value, no_value, no_value, no_value);
	} else {
		trace_seq_printf(s, "%9llu ", cpu_data->cur_user / divisor);
		trace_seq_printf(s, "%9llu ", cpu_data->min_user / divisor);
		trace_seq_printf(s, "%9llu ",
				(cpu_data->sum_user / cpu_data->user_count) / divisor);
		trace_seq_printf(s, "%9llu\n", cpu_data->max_user / divisor);
	}
}

/*
 * timerlat_top_print_sum - prints the summary output
 */
static void
timerlat_top_print_sum(struct osnoise_tool *top, struct timerlat_top_cpu *summary)
{
	const char *split = "----------------------------------------";
	struct timerlat_top_params *params = top->params;
	unsigned long long count = summary->irq_count;
	int divisor = params->output_divisor;
	struct trace_seq *s = top->trace.seq;
	int e = 0;

	if (divisor == 0)
		return;

	/*
	 * Skip if no data is available: is this cpu offline?
	 */
	if (!summary->irq_count && !summary->thread_count)
		return;

	while (count > 999999) {
		e++;
		count /= 10;
	}

	trace_seq_printf(s, "%.*s|%.*s|%.*s", 15, split, 40, split, 39, split);
	if (params->user_top)
		trace_seq_printf(s, "-|%.*s", 39, split);
	trace_seq_printf(s, "\n");

	trace_seq_printf(s, "ALL #%-6llu e%d |", count, e);

	if (!summary->irq_count) {
		trace_seq_printf(s, "          %s %s %s |", no_value, no_value, no_value);
	} else {
		trace_seq_printf(s, "          ");
		trace_seq_printf(s, "%9llu ", summary->min_irq / params->output_divisor);
		trace_seq_printf(s, "%9llu ", (summary->sum_irq / summary->irq_count) / divisor);
		trace_seq_printf(s, "%9llu |", summary->max_irq / divisor);
	}

	if (!summary->thread_count) {
		trace_seq_printf(s, "%s %s %s %s", no_value, no_value, no_value, no_value);
	} else {
		trace_seq_printf(s, "          ");
		trace_seq_printf(s, "%9llu ", summary->min_thread / divisor);
		trace_seq_printf(s, "%9llu ",
				(summary->sum_thread / summary->thread_count) / divisor);
		trace_seq_printf(s, "%9llu", summary->max_thread / divisor);
	}

	if (!params->user_top) {
		trace_seq_printf(s, "\n");
		return;
	}

	trace_seq_printf(s, " |");

	if (!summary->user_count) {
		trace_seq_printf(s, "          %s %s %s |", no_value, no_value, no_value);
	} else {
		trace_seq_printf(s, "          ");
		trace_seq_printf(s, "%9llu ", summary->min_user / divisor);
		trace_seq_printf(s, "%9llu ",
				(summary->sum_user / summary->user_count) / divisor);
		trace_seq_printf(s, "%9llu\n", summary->max_user / divisor);
	}
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
 * timerlat_print_stats - print data for all cpus
 */
static void
timerlat_print_stats(struct timerlat_top_params *params, struct osnoise_tool *top)
{
	struct trace_instance *trace = &top->trace;
	struct timerlat_top_cpu summary;
	static int nr_cpus = -1;
	int i;

	if (params->aa_only)
		return;

	if (nr_cpus == -1)
		nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

	if (!params->quiet)
		clear_terminal(trace->seq);

	timerlat_top_reset_sum(&summary);

	timerlat_top_header(params, top);

	for (i = 0; i < nr_cpus; i++) {
		if (params->cpus && !CPU_ISSET(i, &params->monitored_cpus))
			continue;
		timerlat_top_print(top, i);
		timerlat_top_update_sum(top, i, &summary);
	}

	timerlat_top_print_sum(top, &summary);

	trace_seq_do_printf(trace->seq);
	trace_seq_reset(trace->seq);
}

/*
 * timerlat_top_usage - prints timerlat top usage message
 */
static void timerlat_top_usage(char *usage)
{
	int i;

	static const char *const msg[] = {
		"",
		"  usage: rtla timerlat [top] [-h] [-q] [-a us] [-d s] [-D] [-n] [-p us] [-i us] [-T us] [-s us] \\",
		"	  [[-t[file]] [-e sys[:event]] [--filter <filter>] [--trigger <trigger>] [-c cpu-list] [-H cpu-list]\\",
		"	  [-P priority] [--dma-latency us] [--aa-only us] [-C[=cgroup_name]] [-u|-k] [--warm-up s] [--deepest-idle-state n]",
		"",
		"	  -h/--help: print this menu",
		"	  -a/--auto: set automatic trace mode, stopping the session if argument in us latency is hit",
		"	     --aa-only us: stop if <us> latency is hit, only printing the auto analysis (reduces CPU usage)",
		"	  -p/--period us: timerlat period in us",
		"	  -i/--irq us: stop trace if the irq latency is higher than the argument in us",
		"	  -T/--thread us: stop trace if the thread latency is higher than the argument in us",
		"	  -s/--stack us: save the stack trace at the IRQ if a thread latency is higher than the argument in us",
		"	  -c/--cpus cpus: run the tracer only on the given cpus",
		"	  -H/--house-keeping cpus: run rtla control threads only on the given cpus",
		"	  -C/--cgroup[=cgroup_name]: set cgroup, if no cgroup_name is passed, the rtla's cgroup will be inherited",
		"	  -d/--duration time[s|m|h|d]: duration of the session",
		"	  -D/--debug: print debug info",
		"	     --dump-tasks: prints the task running on all CPUs if stop conditions are met (depends on !--no-aa)",
		"	  -t/--trace[file]: save the stopped trace to [file|timerlat_trace.txt]",
		"	  -e/--event <sys:event>: enable the <sys:event> in the trace instance, multiple -e are allowed",
		"	     --filter <command>: enable a trace event filter to the previous -e event",
		"	     --trigger <command>: enable a trace event trigger to the previous -e event",
		"	  -n/--nano: display data in nanoseconds",
		"	     --no-aa: disable auto-analysis, reducing rtla timerlat cpu usage",
		"	  -q/--quiet print only a summary at the end",
		"	     --dma-latency us: set /dev/cpu_dma_latency latency <us> to reduce exit from idle latency",
		"	  -P/--priority o:prio|r:prio|f:prio|d:runtime:period : set scheduling parameters",
		"		o:prio - use SCHED_OTHER with prio",
		"		r:prio - use SCHED_RR with prio",
		"		f:prio - use SCHED_FIFO with prio",
		"		d:runtime[us|ms|s]:period[us|ms|s] - use SCHED_DEADLINE with runtime and period",
		"						       in nanoseconds",
		"	  -u/--user-threads: use rtla user-space threads instead of kernel-space timerlat threads",
		"	  -k/--kernel-threads: use timerlat kernel-space threads instead of rtla user-space threads",
		"	  -U/--user-load: enable timerlat for user-defined user-space workload",
		"	     --warm-up s: let the workload run for s seconds before collecting data",
		"	     --trace-buffer-size kB: set the per-cpu trace buffer size in kB",
		"	     --deepest-idle-state n: only go down to idle state n on cpus used by timerlat to reduce exit from idle latency",
		NULL,
	};

	if (usage)
		fprintf(stderr, "%s\n", usage);

	fprintf(stderr, "rtla timerlat top: a per-cpu summary of the timer latency (version %s)\n",
			VERSION);

	for (i = 0; msg[i]; i++)
		fprintf(stderr, "%s\n", msg[i]);

	if (usage)
		exit(EXIT_FAILURE);

	exit(EXIT_SUCCESS);
}

/*
 * timerlat_top_parse_args - allocs, parse and fill the cmd line parameters
 */
static struct timerlat_top_params
*timerlat_top_parse_args(int argc, char **argv)
{
	struct timerlat_top_params *params;
	struct trace_events *tevent;
	long long auto_thresh;
	int retval;
	int c;

	params = calloc(1, sizeof(*params));
	if (!params)
		exit(1);

	/* disabled by default */
	params->dma_latency = -1;

	/* disabled by default */
	params->deepest_idle_state = -2;

	/* display data in microseconds */
	params->output_divisor = 1000;

	while (1) {
		static struct option long_options[] = {
			{"auto",		required_argument,	0, 'a'},
			{"cpus",		required_argument,	0, 'c'},
			{"cgroup",		optional_argument,	0, 'C'},
			{"debug",		no_argument,		0, 'D'},
			{"duration",		required_argument,	0, 'd'},
			{"event",		required_argument,	0, 'e'},
			{"help",		no_argument,		0, 'h'},
			{"house-keeping",	required_argument,	0, 'H'},
			{"irq",			required_argument,	0, 'i'},
			{"nano",		no_argument,		0, 'n'},
			{"period",		required_argument,	0, 'p'},
			{"priority",		required_argument,	0, 'P'},
			{"quiet",		no_argument,		0, 'q'},
			{"stack",		required_argument,	0, 's'},
			{"thread",		required_argument,	0, 'T'},
			{"trace",		optional_argument,	0, 't'},
			{"user-threads",	no_argument,		0, 'u'},
			{"kernel-threads",	no_argument,		0, 'k'},
			{"user-load",		no_argument,		0, 'U'},
			{"trigger",		required_argument,	0, '0'},
			{"filter",		required_argument,	0, '1'},
			{"dma-latency",		required_argument,	0, '2'},
			{"no-aa",		no_argument,		0, '3'},
			{"dump-tasks",		no_argument,		0, '4'},
			{"aa-only",		required_argument,	0, '5'},
			{"warm-up",		required_argument,	0, '6'},
			{"trace-buffer-size",	required_argument,	0, '7'},
			{"deepest-idle-state",	required_argument,	0, '8'},
			{0, 0, 0, 0}
		};

		/* getopt_long stores the option index here. */
		int option_index = 0;

		c = getopt_long(argc, argv, "a:c:C::d:De:hH:i:knp:P:qs:t::T:uU0:1:2:345:6:7:",
				 long_options, &option_index);

		/* detect the end of the options. */
		if (c == -1)
			break;

		switch (c) {
		case 'a':
			auto_thresh = get_llong_from_str(optarg);

			/* set thread stop to auto_thresh */
			params->stop_total_us = auto_thresh;
			params->stop_us = auto_thresh;

			/* get stack trace */
			params->print_stack = auto_thresh;

			/* set trace */
			params->trace_output = "timerlat_trace.txt";
			break;
		case '5':
			/* it is here because it is similar to -a */
			auto_thresh = get_llong_from_str(optarg);

			/* set thread stop to auto_thresh */
			params->stop_total_us = auto_thresh;
			params->stop_us = auto_thresh;

			/* get stack trace */
			params->print_stack = auto_thresh;

			/* set aa_only to avoid parsing the trace */
			params->aa_only = 1;
			break;
		case 'c':
			retval = parse_cpu_set(optarg, &params->monitored_cpus);
			if (retval)
				timerlat_top_usage("\nInvalid -c cpu list\n");
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
				timerlat_top_usage("Invalid -d duration\n");
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
			timerlat_top_usage(NULL);
			break;
		case 'H':
			params->hk_cpus = 1;
			retval = parse_cpu_set(optarg, &params->hk_cpu_set);
			if (retval) {
				err_msg("Error parsing house keeping CPUs\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'i':
			params->stop_us = get_llong_from_str(optarg);
			break;
		case 'k':
			params->kernel_workload = true;
			break;
		case 'n':
			params->output_divisor = 1;
			break;
		case 'p':
			params->timerlat_period_us = get_llong_from_str(optarg);
			if (params->timerlat_period_us > 1000000)
				timerlat_top_usage("Period longer than 1 s\n");
			break;
		case 'P':
			retval = parse_prio(optarg, &params->sched_param);
			if (retval == -1)
				timerlat_top_usage("Invalid -P priority");
			params->set_sched = 1;
			break;
		case 'q':
			params->quiet = 1;
			break;
		case 's':
			params->print_stack = get_llong_from_str(optarg);
			break;
		case 'T':
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
				params->trace_output = "timerlat_trace.txt";

			break;
		case 'u':
			params->user_workload = true;
			/* fallback: -u implies -U */
		case 'U':
			params->user_top = true;
			break;
		case '0': /* trigger */
			if (params->events) {
				retval = trace_event_add_trigger(params->events, optarg);
				if (retval) {
					err_msg("Error adding trigger %s\n", optarg);
					exit(EXIT_FAILURE);
				}
			} else {
				timerlat_top_usage("--trigger requires a previous -e\n");
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
				timerlat_top_usage("--filter requires a previous -e\n");
			}
			break;
		case '2': /* dma-latency */
			params->dma_latency = get_llong_from_str(optarg);
			if (params->dma_latency < 0 || params->dma_latency > 10000) {
				err_msg("--dma-latency needs to be >= 0 and < 10000");
				exit(EXIT_FAILURE);
			}
			break;
		case '3': /* no-aa */
			params->no_aa = 1;
			break;
		case '4':
			params->dump_tasks = 1;
			break;
		case '6':
			params->warmup = get_llong_from_str(optarg);
			break;
		case '7':
			params->buffer_size = get_llong_from_str(optarg);
			break;
		case '8':
			params->deepest_idle_state = get_llong_from_str(optarg);
			break;
		default:
			timerlat_top_usage("Invalid option");
		}
	}

	if (geteuid()) {
		err_msg("rtla needs root permission\n");
		exit(EXIT_FAILURE);
	}

	/*
	 * Auto analysis only happens if stop tracing, thus:
	 */
	if (!params->stop_us && !params->stop_total_us)
		params->no_aa = 1;

	if (params->no_aa && params->aa_only)
		timerlat_top_usage("--no-aa and --aa-only are mutually exclusive!");

	if (params->kernel_workload && params->user_workload)
		timerlat_top_usage("--kernel-threads and --user-threads are mutually exclusive!");

	return params;
}

/*
 * timerlat_top_apply_config - apply the top configs to the initialized tool
 */
static int
timerlat_top_apply_config(struct osnoise_tool *top, struct timerlat_top_params *params)
{
	int retval;
	int i;

	if (!params->sleep_time)
		params->sleep_time = 1;

	if (params->cpus) {
		retval = osnoise_set_cpus(top->context, params->cpus);
		if (retval) {
			err_msg("Failed to apply CPUs config\n");
			goto out_err;
		}
	} else {
		for (i = 0; i < sysconf(_SC_NPROCESSORS_CONF); i++)
			CPU_SET(i, &params->monitored_cpus);
	}

	if (params->stop_us) {
		retval = osnoise_set_stop_us(top->context, params->stop_us);
		if (retval) {
			err_msg("Failed to set stop us\n");
			goto out_err;
		}
	}

	if (params->stop_total_us) {
		retval = osnoise_set_stop_total_us(top->context, params->stop_total_us);
		if (retval) {
			err_msg("Failed to set stop total us\n");
			goto out_err;
		}
	}


	if (params->timerlat_period_us) {
		retval = osnoise_set_timerlat_period_us(top->context, params->timerlat_period_us);
		if (retval) {
			err_msg("Failed to set timerlat period\n");
			goto out_err;
		}
	}


	if (params->print_stack) {
		retval = osnoise_set_print_stack(top->context, params->print_stack);
		if (retval) {
			err_msg("Failed to set print stack\n");
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

	/*
	 * If the user did not specify a type of thread, try user-threads first.
	 * Fall back to kernel threads otherwise.
	 */
	if (!params->kernel_workload && !params->user_top) {
		retval = tracefs_file_exists(NULL, "osnoise/per_cpu/cpu0/timerlat_fd");
		if (retval) {
			debug_msg("User-space interface detected, setting user-threads\n");
			params->user_workload = 1;
			params->user_top = 1;
		} else {
			debug_msg("User-space interface not detected, setting kernel-threads\n");
			params->kernel_workload = 1;
		}
	}

	if (params->user_top) {
		retval = osnoise_set_workload(top->context, 0);
		if (retval) {
			err_msg("Failed to set OSNOISE_WORKLOAD option\n");
			goto out_err;
		}
	}

	if (isatty(STDOUT_FILENO) && !params->quiet)
		params->pretty_output = 1;

	return 0;

out_err:
	return -1;
}

/*
 * timerlat_init_top - initialize a timerlat top tool with parameters
 */
static struct osnoise_tool
*timerlat_init_top(struct timerlat_top_params *params)
{
	struct osnoise_tool *top;
	int nr_cpus;

	nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

	top = osnoise_init_tool("timerlat_top");
	if (!top)
		return NULL;

	top->data = timerlat_alloc_top(nr_cpus);
	if (!top->data)
		goto out_err;

	top->params = params;

	tep_register_event_handler(top->trace.tep, -1, "ftrace", "timerlat",
				   timerlat_top_handler, top);

	return top;

out_err:
	osnoise_destroy_tool(top);
	return NULL;
}

static int stop_tracing;
static void stop_top(int sig)
{
	stop_tracing = 1;
}

/*
 * timerlat_top_set_signals - handles the signal to stop the tool
 */
static void
timerlat_top_set_signals(struct timerlat_top_params *params)
{
	signal(SIGINT, stop_top);
	if (params->duration) {
		signal(SIGALRM, stop_top);
		alarm(params->duration);
	}
}

int timerlat_top_main(int argc, char *argv[])
{
	struct timerlat_top_params *params;
	struct osnoise_tool *record = NULL;
	struct timerlat_u_params params_u;
	struct osnoise_tool *top = NULL;
	struct osnoise_tool *aa = NULL;
	struct trace_instance *trace;
	int dma_latency_fd = -1;
	pthread_t timerlat_u;
	int return_value = 1;
	char *max_lat;
	int retval;
	int nr_cpus, i;

	params = timerlat_top_parse_args(argc, argv);
	if (!params)
		exit(1);

	top = timerlat_init_top(params);
	if (!top) {
		err_msg("Could not init osnoise top\n");
		goto out_exit;
	}

	retval = timerlat_top_apply_config(top, params);
	if (retval) {
		err_msg("Could not apply config\n");
		goto out_free;
	}

	trace = &top->trace;

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

	if (params->cgroup && !params->user_top) {
		retval = set_comm_cgroup("timerlat/", params->cgroup_name);
		if (!retval) {
			err_msg("Failed to move threads to cgroup\n");
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

	if (params->deepest_idle_state >= -1) {
		if (!have_libcpupower_support()) {
			err_msg("rtla built without libcpupower, --deepest-idle-state is not supported\n");
			goto out_free;
		}

		nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

		for (i = 0; i < nr_cpus; i++) {
			if (params->cpus && !CPU_ISSET(i, &params->monitored_cpus))
				continue;
			if (save_cpu_idle_disable_state(i) < 0) {
				err_msg("Could not save cpu idle state.\n");
				goto out_free;
			}
			if (set_deepest_cpu_idle_state(i, params->deepest_idle_state) < 0) {
				err_msg("Could not set deepest cpu idle state.\n");
				goto out_free;
			}
		}
	}

	if (params->trace_output) {
		record = osnoise_init_trace_tool("timerlat");
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

	if (!params->no_aa) {
		if (params->aa_only) {
			/* as top is not used for display, use it for aa */
			aa = top;
		} else  {
			/* otherwise, a new instance is needed */
			aa = osnoise_init_tool("timerlat_aa");
			if (!aa)
				goto out_top;
		}

		retval = timerlat_aa_init(aa, params->dump_tasks);
		if (retval) {
			err_msg("Failed to enable the auto analysis instance\n");
			goto out_top;
		}

		/* if it is re-using the main instance, there is no need to start it */
		if (aa != top) {
			retval = enable_timerlat(&aa->trace);
			if (retval) {
				err_msg("Failed to enable timerlat tracer\n");
				goto out_top;
			}
		}
	}

	if (params->user_workload) {
		/* rtla asked to stop */
		params_u.should_run = 1;
		/* all threads left */
		params_u.stopped_running = 0;

		params_u.set = &params->monitored_cpus;
		if (params->set_sched)
			params_u.sched_param = &params->sched_param;
		else
			params_u.sched_param = NULL;

		params_u.cgroup_name = params->cgroup_name;

		retval = pthread_create(&timerlat_u, NULL, timerlat_u_dispatcher, &params_u);
		if (retval)
			err_msg("Error creating timerlat user-space threads\n");
	}

	if (params->warmup > 0) {
		debug_msg("Warming up for %d seconds\n", params->warmup);
		sleep(params->warmup);
	}

	/*
	 * Start the tracers here, after having set all instances.
	 *
	 * Let the trace instance start first for the case of hitting a stop
	 * tracing while enabling other instances. The trace instance is the
	 * one with most valuable information.
	 */
	if (params->trace_output)
		trace_instance_start(&record->trace);
	if (!params->no_aa && aa != top)
		trace_instance_start(&aa->trace);
	trace_instance_start(trace);

	top->start_time = time(NULL);
	timerlat_top_set_signals(params);

	while (!stop_tracing) {
		sleep(params->sleep_time);

		if (params->aa_only && !trace_is_off(&top->trace, &record->trace))
			continue;

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
			timerlat_print_stats(params, top);

		if (trace_is_off(&top->trace, &record->trace))
			break;

		/* is there still any user-threads ? */
		if (params->user_workload) {
			if (params_u.stopped_running) {
				debug_msg("timerlat user space threads stopped!\n");
				break;
			}
		}
	}

	if (params->user_workload && !params_u.stopped_running) {
		params_u.should_run = 0;
		sleep(1);
	}

	timerlat_print_stats(params, top);

	return_value = 0;

	if (trace_is_off(&top->trace, &record->trace)) {
		printf("rtla timerlat hit stop tracing\n");

		if (!params->no_aa)
			timerlat_auto_analysis(params->stop_us, params->stop_total_us);

		if (params->trace_output) {
			printf("  Saving trace to %s\n", params->trace_output);
			save_trace_to_file(record->trace.inst, params->trace_output);
		}
	} else if (params->aa_only) {
		/*
		 * If the trace did not stop with --aa-only, at least print the
		 * max known latency.
		 */
		max_lat = tracefs_instance_file_read(trace->inst, "tracing_max_latency", NULL);
		if (max_lat) {
			printf("  Max latency was %s\n", max_lat);
			free(max_lat);
		}
	}

out_top:
	timerlat_aa_destroy();
	if (dma_latency_fd >= 0)
		close(dma_latency_fd);
	if (params->deepest_idle_state >= -1) {
		for (i = 0; i < nr_cpus; i++) {
			if (params->cpus && !CPU_ISSET(i, &params->monitored_cpus))
				continue;
			restore_cpu_idle_disable_state(i);
		}
	}
	trace_events_destroy(&record->trace, params->events);
	params->events = NULL;
out_free:
	timerlat_free_top(top->data);
	if (aa && aa != top)
		osnoise_destroy_tool(aa);
	osnoise_destroy_tool(record);
	osnoise_destroy_tool(top);
	free(params);
	free_cpu_idle_disable_states();
out_exit:
	exit(return_value);
}
