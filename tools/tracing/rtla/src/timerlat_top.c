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

#include "timerlat.h"
#include "timerlat_aa.h"
#include "timerlat_bpf.h"

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
static void timerlat_free_top(struct timerlat_top_data *data)
{
	free(data->cpu_data);
	free(data);
}

static void timerlat_free_top_tool(struct osnoise_tool *tool)
{
	timerlat_free_top(tool->data);
	timerlat_free(tool);
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
	struct timerlat_params *params = to_timerlat_params(tool->params);
	struct timerlat_top_data *data = tool->data;
	struct timerlat_top_cpu *cpu_data = &data->cpu_data[cpu];

	if (params->common.output_divisor)
		latency = latency / params->common.output_divisor;

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
	unsigned long long latency, thread;
	struct osnoise_tool *top;
	int cpu = record->cpu;

	top = container_of(trace, struct osnoise_tool, trace);

	if (!top->params->aa_only) {
		tep_get_field_val(s, event, "context", record, &thread, 1);
		tep_get_field_val(s, event, "timer_latency", record, &latency, 1);

		timerlat_top_update(top, cpu, thread, latency);
	}

	return 0;
}

/*
 * timerlat_top_bpf_pull_data - copy data from BPF maps into userspace
 */
static int timerlat_top_bpf_pull_data(struct osnoise_tool *tool)
{
	struct timerlat_top_data *data = tool->data;
	int i, err;
	long long value_irq[data->nr_cpus],
		  value_thread[data->nr_cpus],
		  value_user[data->nr_cpus];

	/* Pull summary */
	err = timerlat_bpf_get_summary_value(SUMMARY_CURRENT,
					     value_irq, value_thread, value_user,
					     data->nr_cpus);
	if (err)
		return err;
	for (i = 0; i < data->nr_cpus; i++) {
		data->cpu_data[i].cur_irq = value_irq[i];
		data->cpu_data[i].cur_thread = value_thread[i];
		data->cpu_data[i].cur_user = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_COUNT,
					     value_irq, value_thread, value_user,
					     data->nr_cpus);
	if (err)
		return err;
	for (i = 0; i < data->nr_cpus; i++) {
		data->cpu_data[i].irq_count = value_irq[i];
		data->cpu_data[i].thread_count = value_thread[i];
		data->cpu_data[i].user_count = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_MIN,
					     value_irq, value_thread, value_user,
					     data->nr_cpus);
	if (err)
		return err;
	for (i = 0; i < data->nr_cpus; i++) {
		data->cpu_data[i].min_irq = value_irq[i];
		data->cpu_data[i].min_thread = value_thread[i];
		data->cpu_data[i].min_user = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_MAX,
					     value_irq, value_thread, value_user,
					     data->nr_cpus);
	if (err)
		return err;
	for (i = 0; i < data->nr_cpus; i++) {
		data->cpu_data[i].max_irq = value_irq[i];
		data->cpu_data[i].max_thread = value_thread[i];
		data->cpu_data[i].max_user = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_SUM,
					     value_irq, value_thread, value_user,
					     data->nr_cpus);
	if (err)
		return err;
	for (i = 0; i < data->nr_cpus; i++) {
		data->cpu_data[i].sum_irq = value_irq[i];
		data->cpu_data[i].sum_thread = value_thread[i];
		data->cpu_data[i].sum_user = value_user[i];
	}

	return 0;
}

/*
 * timerlat_top_header - print the header of the tool output
 */
static void timerlat_top_header(struct timerlat_params *params, struct osnoise_tool *top)
{
	struct trace_seq *s = top->trace.seq;
	bool pretty = params->common.pretty_output;
	char duration[26];

	get_duration(top->start_time, duration, sizeof(duration));

	if (pretty)
		trace_seq_printf(s, "\033[2;37;40m");

	trace_seq_printf(s, "                                     Timer Latency                                              ");
	if (params->common.user_data)
		trace_seq_printf(s, "                                         ");

	if (pretty)
		trace_seq_printf(s, "\033[0;0;0m");
	trace_seq_printf(s, "\n");

	trace_seq_printf(s, "%-6s   |          IRQ Timer Latency (%s)        |         Thread Timer Latency (%s)", duration,
			params->common.output_divisor == 1 ? "ns" : "us",
			params->common.output_divisor == 1 ? "ns" : "us");

	if (params->common.user_data) {
		trace_seq_printf(s, "      |    Ret user Timer Latency (%s)",
				params->common.output_divisor == 1 ? "ns" : "us");
	}

	trace_seq_printf(s, "\n");
	if (pretty)
		trace_seq_printf(s, "\033[2;30;47m");

	trace_seq_printf(s, "CPU COUNT      |      cur       min       avg       max |      cur       min       avg       max");
	if (params->common.user_data)
		trace_seq_printf(s, " |      cur       min       avg       max");

	if (pretty)
		trace_seq_printf(s, "\033[0;0;0m");
	trace_seq_printf(s, "\n");
}

static const char *no_value = "        -";

/*
 * timerlat_top_print - prints the output of a given CPU
 */
static void timerlat_top_print(struct osnoise_tool *top, int cpu)
{
	struct timerlat_params *params = to_timerlat_params(top->params);
	struct timerlat_top_data *data = top->data;
	struct timerlat_top_cpu *cpu_data = &data->cpu_data[cpu];
	struct trace_seq *s = top->trace.seq;

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
		trace_seq_printf(s, "%9llu ", cpu_data->cur_irq);
		trace_seq_printf(s, "%9llu ", cpu_data->min_irq);
		trace_seq_printf(s, "%9llu ", cpu_data->sum_irq / cpu_data->irq_count);
		trace_seq_printf(s, "%9llu |", cpu_data->max_irq);
	}

	if (!cpu_data->thread_count) {
		trace_seq_printf(s, "%s %s %s %s", no_value, no_value, no_value, no_value);
	} else {
		trace_seq_printf(s, "%9llu ", cpu_data->cur_thread);
		trace_seq_printf(s, "%9llu ", cpu_data->min_thread);
		trace_seq_printf(s, "%9llu ",
				cpu_data->sum_thread / cpu_data->thread_count);
		trace_seq_printf(s, "%9llu", cpu_data->max_thread);
	}

	if (!params->common.user_data) {
		trace_seq_printf(s, "\n");
		return;
	}

	trace_seq_printf(s, " |");

	if (!cpu_data->user_count) {
		trace_seq_printf(s, "%s %s %s %s\n", no_value, no_value, no_value, no_value);
	} else {
		trace_seq_printf(s, "%9llu ", cpu_data->cur_user);
		trace_seq_printf(s, "%9llu ", cpu_data->min_user);
		trace_seq_printf(s, "%9llu ",
				cpu_data->sum_user / cpu_data->user_count);
		trace_seq_printf(s, "%9llu\n", cpu_data->max_user);
	}
}

/*
 * timerlat_top_print_sum - prints the summary output
 */
static void
timerlat_top_print_sum(struct osnoise_tool *top, struct timerlat_top_cpu *summary)
{
	const char *split = "----------------------------------------";
	struct timerlat_params *params = to_timerlat_params(top->params);
	unsigned long long count = summary->irq_count;
	struct trace_seq *s = top->trace.seq;
	int e = 0;

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
	if (params->common.user_data)
		trace_seq_printf(s, "-|%.*s", 39, split);
	trace_seq_printf(s, "\n");

	trace_seq_printf(s, "ALL #%-6llu e%d |", count, e);

	if (!summary->irq_count) {
		trace_seq_printf(s, "          %s %s %s |", no_value, no_value, no_value);
	} else {
		trace_seq_printf(s, "          ");
		trace_seq_printf(s, "%9llu ", summary->min_irq);
		trace_seq_printf(s, "%9llu ", summary->sum_irq / summary->irq_count);
		trace_seq_printf(s, "%9llu |", summary->max_irq);
	}

	if (!summary->thread_count) {
		trace_seq_printf(s, "%s %s %s %s", no_value, no_value, no_value, no_value);
	} else {
		trace_seq_printf(s, "          ");
		trace_seq_printf(s, "%9llu ", summary->min_thread);
		trace_seq_printf(s, "%9llu ",
				summary->sum_thread / summary->thread_count);
		trace_seq_printf(s, "%9llu", summary->max_thread);
	}

	if (!params->common.user_data) {
		trace_seq_printf(s, "\n");
		return;
	}

	trace_seq_printf(s, " |");

	if (!summary->user_count) {
		trace_seq_printf(s, "          %s %s %s |", no_value, no_value, no_value);
	} else {
		trace_seq_printf(s, "          ");
		trace_seq_printf(s, "%9llu ", summary->min_user);
		trace_seq_printf(s, "%9llu ",
				summary->sum_user / summary->user_count);
		trace_seq_printf(s, "%9llu\n", summary->max_user);
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
timerlat_print_stats(struct osnoise_tool *top)
{
	struct timerlat_params *params = to_timerlat_params(top->params);
	struct trace_instance *trace = &top->trace;
	struct timerlat_top_cpu summary;
	static int nr_cpus = -1;
	int i;

	if (params->common.aa_only)
		return;

	if (nr_cpus == -1)
		nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

	if (!params->common.quiet)
		clear_terminal(trace->seq);

	timerlat_top_reset_sum(&summary);

	timerlat_top_header(params, top);

	for (i = 0; i < nr_cpus; i++) {
		if (params->common.cpus && !CPU_ISSET(i, &params->common.monitored_cpus))
			continue;
		timerlat_top_print(top, i);
		timerlat_top_update_sum(top, i, &summary);
	}

	timerlat_top_print_sum(top, &summary);

	trace_seq_do_printf(trace->seq);
	trace_seq_reset(trace->seq);
	osnoise_report_missed_events(top);
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
		"	     --on-threshold <action>: define action to be executed at latency threshold, multiple are allowed",
		"	     --on-end: define action to be executed at measurement end, multiple are allowed",
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
static struct common_params
*timerlat_top_parse_args(int argc, char **argv)
{
	struct timerlat_params *params;
	struct trace_events *tevent;
	long long auto_thresh;
	int retval;
	int c;
	char *trace_output = NULL;

	params = calloc(1, sizeof(*params));
	if (!params)
		exit(1);

	actions_init(&params->common.threshold_actions);
	actions_init(&params->common.end_actions);

	/* disabled by default */
	params->dma_latency = -1;

	/* disabled by default */
	params->deepest_idle_state = -2;

	/* display data in microseconds */
	params->common.output_divisor = 1000;

	/* default to BPF mode */
	params->mode = TRACING_MODE_BPF;

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
			{"on-threshold",	required_argument,	0, '9'},
			{"on-end",		required_argument,	0, '\1'},
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
			params->common.stop_total_us = auto_thresh;
			params->common.stop_us = auto_thresh;

			/* get stack trace */
			params->print_stack = auto_thresh;

			/* set trace */
			trace_output = "timerlat_trace.txt";

			break;
		case '5':
			/* it is here because it is similar to -a */
			auto_thresh = get_llong_from_str(optarg);

			/* set thread stop to auto_thresh */
			params->common.stop_total_us = auto_thresh;
			params->common.stop_us = auto_thresh;

			/* get stack trace */
			params->print_stack = auto_thresh;

			/* set aa_only to avoid parsing the trace */
			params->common.aa_only = 1;
			break;
		case 'c':
			retval = parse_cpu_set(optarg, &params->common.monitored_cpus);
			if (retval)
				timerlat_top_usage("\nInvalid -c cpu list\n");
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
				timerlat_top_usage("Invalid -d duration\n");
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
			timerlat_top_usage(NULL);
			break;
		case 'H':
			params->common.hk_cpus = 1;
			retval = parse_cpu_set(optarg, &params->common.hk_cpu_set);
			if (retval) {
				err_msg("Error parsing house keeping CPUs\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'i':
			params->common.stop_us = get_llong_from_str(optarg);
			break;
		case 'k':
			params->common.kernel_workload = true;
			break;
		case 'n':
			params->common.output_divisor = 1;
			break;
		case 'p':
			params->timerlat_period_us = get_llong_from_str(optarg);
			if (params->timerlat_period_us > 1000000)
				timerlat_top_usage("Period longer than 1 s\n");
			break;
		case 'P':
			retval = parse_prio(optarg, &params->common.sched_param);
			if (retval == -1)
				timerlat_top_usage("Invalid -P priority");
			params->common.set_sched = 1;
			break;
		case 'q':
			params->common.quiet = 1;
			break;
		case 's':
			params->print_stack = get_llong_from_str(optarg);
			break;
		case 'T':
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
				trace_output = "timerlat_trace.txt";
			break;
		case 'u':
			params->common.user_workload = true;
			/* fallback: -u implies -U */
		case 'U':
			params->common.user_data = true;
			break;
		case '0': /* trigger */
			if (params->common.events) {
				retval = trace_event_add_trigger(params->common.events, optarg);
				if (retval) {
					err_msg("Error adding trigger %s\n", optarg);
					exit(EXIT_FAILURE);
				}
			} else {
				timerlat_top_usage("--trigger requires a previous -e\n");
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
			params->common.warmup = get_llong_from_str(optarg);
			break;
		case '7':
			params->common.buffer_size = get_llong_from_str(optarg);
			break;
		case '8':
			params->deepest_idle_state = get_llong_from_str(optarg);
			break;
		case '9':
			retval = actions_parse(&params->common.threshold_actions, optarg,
					       "timerlat_trace.txt");
			if (retval) {
				err_msg("Invalid action %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case '\1':
			retval = actions_parse(&params->common.end_actions, optarg,
					       "timerlat_trace.txt");
			if (retval) {
				err_msg("Invalid action %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		default:
			timerlat_top_usage("Invalid option");
		}
	}

	if (trace_output)
		actions_add_trace_output(&params->common.threshold_actions, trace_output);

	if (geteuid()) {
		err_msg("rtla needs root permission\n");
		exit(EXIT_FAILURE);
	}

	/*
	 * Auto analysis only happens if stop tracing, thus:
	 */
	if (!params->common.stop_us && !params->common.stop_total_us)
		params->no_aa = 1;

	if (params->no_aa && params->common.aa_only)
		timerlat_top_usage("--no-aa and --aa-only are mutually exclusive!");

	if (params->common.kernel_workload && params->common.user_workload)
		timerlat_top_usage("--kernel-threads and --user-threads are mutually exclusive!");

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
 * timerlat_top_apply_config - apply the top configs to the initialized tool
 */
static int
timerlat_top_apply_config(struct osnoise_tool *top)
{
	struct timerlat_params *params = to_timerlat_params(top->params);
	int retval;

	retval = timerlat_apply_config(top, params);
	if (retval)
		goto out_err;

	if (isatty(STDOUT_FILENO) && !params->common.quiet)
		params->common.pretty_output = 1;

	return 0;

out_err:
	return -1;
}

/*
 * timerlat_init_top - initialize a timerlat top tool with parameters
 */
static struct osnoise_tool
*timerlat_init_top(struct common_params *params)
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

	tep_register_event_handler(top->trace.tep, -1, "ftrace", "timerlat",
				   timerlat_top_handler, top);

	return top;

out_err:
	osnoise_destroy_tool(top);
	return NULL;
}

/*
 * timerlat_top_bpf_main_loop - main loop to process events (BPF variant)
 */
static int
timerlat_top_bpf_main_loop(struct osnoise_tool *tool)
{
	struct timerlat_params *params = to_timerlat_params(tool->params);
	int retval, wait_retval;

	if (params->common.aa_only) {
		/* Auto-analysis only, just wait for stop tracing */
		timerlat_bpf_wait(-1);
		return 0;
	}

	/* Pull and display data in a loop */
	while (!stop_tracing) {
		wait_retval = timerlat_bpf_wait(params->common.quiet ? -1 :
						params->common.sleep_time);

		retval = timerlat_top_bpf_pull_data(tool);
		if (retval) {
			err_msg("Error pulling BPF data\n");
			return retval;
		}

		if (!params->common.quiet)
			timerlat_print_stats(tool);

		if (wait_retval == 1) {
			/* Stopping requested by tracer */
			actions_perform(&params->common.threshold_actions);

			if (!params->common.threshold_actions.continue_flag)
				/* continue flag not set, break */
				break;

			/* continue action reached, re-enable tracing */
			if (tool->record)
				trace_instance_start(&tool->record->trace);
			if (tool->aa)
				trace_instance_start(&tool->aa->trace);
			timerlat_bpf_restart_tracing();
		}

		/* is there still any user-threads ? */
		if (params->common.user_workload) {
			if (params->common.user.stopped_running) {
				debug_msg("timerlat user space threads stopped!\n");
				break;
			}
		}
	}

	return 0;
}

static int timerlat_top_main_loop(struct osnoise_tool *tool)
{
	struct timerlat_params *params = to_timerlat_params(tool->params);
	int retval;

	if (params->mode == TRACING_MODE_TRACEFS) {
		retval = top_main_loop(tool);
	} else {
		retval = timerlat_top_bpf_main_loop(tool);
		timerlat_bpf_detach();
	}

	return retval;
}

struct tool_ops timerlat_top_ops = {
	.tracer = "timerlat",
	.comm_prefix = "timerlat/",
	.parse_args = timerlat_top_parse_args,
	.init_tool = timerlat_init_top,
	.apply_config = timerlat_top_apply_config,
	.enable = timerlat_enable,
	.main = timerlat_top_main_loop,
	.print_stats = timerlat_print_stats,
	.analyze = timerlat_analyze,
	.free = timerlat_free_top_tool,
};
