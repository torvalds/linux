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
#include <pthread.h>

#include "utils.h"
#include "osnoise.h"
#include "timerlat.h"
#include "timerlat_aa.h"
#include "timerlat_u.h"

struct timerlat_hist_params {
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
	int			set_sched;
	int			dma_latency;
	int			cgroup;
	int			hk_cpus;
	int			no_aa;
	int			dump_tasks;
	int			user_workload;
	int			kernel_workload;
	int			user_hist;
	cpu_set_t		hk_cpu_set;
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
	int			warmup;
	int			buffer_size;
	int			deepest_idle_state;
};

struct timerlat_hist_cpu {
	int			*irq;
	int			*thread;
	int			*user;

	unsigned long long	irq_count;
	unsigned long long	thread_count;
	unsigned long long	user_count;

	unsigned long long	min_irq;
	unsigned long long	sum_irq;
	unsigned long long	max_irq;

	unsigned long long	min_thread;
	unsigned long long	sum_thread;
	unsigned long long	max_thread;

	unsigned long long	min_user;
	unsigned long long	sum_user;
	unsigned long long	max_user;
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

		if (data->hist[cpu].user)
			free(data->hist[cpu].user);

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

		data->hist[cpu].user = calloc(1, sizeof(*data->hist->user) * (entries + 1));
		if (!data->hist[cpu].user)
			goto cleanup;
	}

	/* set the min to max */
	for (cpu = 0; cpu < nr_cpus; cpu++) {
		data->hist[cpu].min_irq = ~0;
		data->hist[cpu].min_thread = ~0;
		data->hist[cpu].min_user = ~0;
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
		     unsigned long long context,
		     unsigned long long latency)
{
	struct timerlat_hist_params *params = tool->params;
	struct timerlat_hist_data *data = tool->data;
	int entries = data->entries;
	int bucket;
	int *hist;

	if (params->output_divisor)
		latency = latency / params->output_divisor;

	bucket = latency / data->bucket_size;

	if (!context) {
		hist = data->hist[cpu].irq;
		data->hist[cpu].irq_count++;
		update_min(&data->hist[cpu].min_irq, &latency);
		update_sum(&data->hist[cpu].sum_irq, &latency);
		update_max(&data->hist[cpu].max_irq, &latency);
	} else if (context == 1) {
		hist = data->hist[cpu].thread;
		data->hist[cpu].thread_count++;
		update_min(&data->hist[cpu].min_thread, &latency);
		update_sum(&data->hist[cpu].sum_thread, &latency);
		update_max(&data->hist[cpu].max_thread, &latency);
	} else { /* user */
		hist = data->hist[cpu].user;
		data->hist[cpu].user_count++;
		update_min(&data->hist[cpu].min_user, &latency);
		update_sum(&data->hist[cpu].sum_user, &latency);
		update_max(&data->hist[cpu].max_user, &latency);
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
	unsigned long long context, latency;
	struct osnoise_tool *tool;
	int cpu = record->cpu;

	tool = container_of(trace, struct osnoise_tool, trace);

	tep_get_field_val(s, event, "context", record, &context, 1);
	tep_get_field_val(s, event, "timer_latency", record, &latency, 1);

	timerlat_hist_update(tool, cpu, context, latency);

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
		if (params->cpus && !CPU_ISSET(cpu, &params->monitored_cpus))
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->no_irq)
			trace_seq_printf(s, "   IRQ-%03d", cpu);

		if (!params->no_thread)
			trace_seq_printf(s, "   Thr-%03d", cpu);

		if (params->user_hist)
			trace_seq_printf(s, "   Usr-%03d", cpu);
	}
	trace_seq_printf(s, "\n");


	trace_seq_do_printf(s);
	trace_seq_reset(s);
}

/*
 * format_summary_value - format a line of summary value (min, max or avg)
 * of hist data
 */
static void format_summary_value(struct trace_seq *seq,
				 int count,
				 unsigned long long val,
				 bool avg)
{
	if (count)
		trace_seq_printf(seq, "%9llu ", avg ? val / count : val);
	else
		trace_seq_printf(seq, "%9c ", '-');
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
		if (params->cpus && !CPU_ISSET(cpu, &params->monitored_cpus))
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->no_irq)
			trace_seq_printf(trace->seq, "%9llu ",
					data->hist[cpu].irq_count);

		if (!params->no_thread)
			trace_seq_printf(trace->seq, "%9llu ",
					data->hist[cpu].thread_count);

		if (params->user_hist)
			trace_seq_printf(trace->seq, "%9llu ",
					 data->hist[cpu].user_count);
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->no_index)
		trace_seq_printf(trace->seq, "min:  ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !CPU_ISSET(cpu, &params->monitored_cpus))
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->no_irq)
			format_summary_value(trace->seq,
					     data->hist[cpu].irq_count,
					     data->hist[cpu].min_irq,
					     false);

		if (!params->no_thread)
			format_summary_value(trace->seq,
					     data->hist[cpu].thread_count,
					     data->hist[cpu].min_thread,
					     false);

		if (params->user_hist)
			format_summary_value(trace->seq,
					     data->hist[cpu].user_count,
					     data->hist[cpu].min_user,
					     false);
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->no_index)
		trace_seq_printf(trace->seq, "avg:  ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !CPU_ISSET(cpu, &params->monitored_cpus))
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->no_irq)
			format_summary_value(trace->seq,
					     data->hist[cpu].irq_count,
					     data->hist[cpu].sum_irq,
					     true);

		if (!params->no_thread)
			format_summary_value(trace->seq,
					     data->hist[cpu].thread_count,
					     data->hist[cpu].sum_thread,
					     true);

		if (params->user_hist)
			format_summary_value(trace->seq,
					     data->hist[cpu].user_count,
					     data->hist[cpu].sum_user,
					     true);
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->no_index)
		trace_seq_printf(trace->seq, "max:  ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !CPU_ISSET(cpu, &params->monitored_cpus))
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->no_irq)
			format_summary_value(trace->seq,
					     data->hist[cpu].irq_count,
					     data->hist[cpu].max_irq,
					     false);

		if (!params->no_thread)
			format_summary_value(trace->seq,
					     data->hist[cpu].thread_count,
					     data->hist[cpu].max_thread,
					     false);

		if (params->user_hist)
			format_summary_value(trace->seq,
					     data->hist[cpu].user_count,
					     data->hist[cpu].max_user,
					     false);
	}
	trace_seq_printf(trace->seq, "\n");
	trace_seq_do_printf(trace->seq);
	trace_seq_reset(trace->seq);
}

static void
timerlat_print_stats_all(struct timerlat_hist_params *params,
			 struct trace_instance *trace,
			 struct timerlat_hist_data *data)
{
	struct timerlat_hist_cpu *cpu_data;
	struct timerlat_hist_cpu sum;
	int cpu;

	if (params->no_summary)
		return;

	memset(&sum, 0, sizeof(sum));
	sum.min_irq = ~0;
	sum.min_thread = ~0;
	sum.min_user = ~0;

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->cpus && !CPU_ISSET(cpu, &params->monitored_cpus))
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		cpu_data = &data->hist[cpu];

		sum.irq_count += cpu_data->irq_count;
		update_min(&sum.min_irq, &cpu_data->min_irq);
		update_sum(&sum.sum_irq, &cpu_data->sum_irq);
		update_max(&sum.max_irq, &cpu_data->max_irq);

		sum.thread_count += cpu_data->thread_count;
		update_min(&sum.min_thread, &cpu_data->min_thread);
		update_sum(&sum.sum_thread, &cpu_data->sum_thread);
		update_max(&sum.max_thread, &cpu_data->max_thread);

		sum.user_count += cpu_data->user_count;
		update_min(&sum.min_user, &cpu_data->min_user);
		update_sum(&sum.sum_user, &cpu_data->sum_user);
		update_max(&sum.max_user, &cpu_data->max_user);
	}

	if (!params->no_index)
		trace_seq_printf(trace->seq, "ALL:  ");

	if (!params->no_irq)
		trace_seq_printf(trace->seq, "      IRQ");

	if (!params->no_thread)
		trace_seq_printf(trace->seq, "       Thr");

	if (params->user_hist)
		trace_seq_printf(trace->seq, "       Usr");

	trace_seq_printf(trace->seq, "\n");

	if (!params->no_index)
		trace_seq_printf(trace->seq, "count:");

	if (!params->no_irq)
		trace_seq_printf(trace->seq, "%9llu ",
				 sum.irq_count);

	if (!params->no_thread)
		trace_seq_printf(trace->seq, "%9llu ",
				 sum.thread_count);

	if (params->user_hist)
		trace_seq_printf(trace->seq, "%9llu ",
				 sum.user_count);

	trace_seq_printf(trace->seq, "\n");

	if (!params->no_index)
		trace_seq_printf(trace->seq, "min:  ");

	if (!params->no_irq)
		format_summary_value(trace->seq,
				     sum.irq_count,
				     sum.min_irq,
				     false);

	if (!params->no_thread)
		format_summary_value(trace->seq,
				     sum.thread_count,
				     sum.min_thread,
				     false);

	if (params->user_hist)
		format_summary_value(trace->seq,
				     sum.user_count,
				     sum.min_user,
				     false);

	trace_seq_printf(trace->seq, "\n");

	if (!params->no_index)
		trace_seq_printf(trace->seq, "avg:  ");

	if (!params->no_irq)
		format_summary_value(trace->seq,
				     sum.irq_count,
				     sum.sum_irq,
				     true);

	if (!params->no_thread)
		format_summary_value(trace->seq,
				     sum.thread_count,
				     sum.sum_thread,
				     true);

	if (params->user_hist)
		format_summary_value(trace->seq,
				     sum.user_count,
				     sum.sum_user,
				     true);

	trace_seq_printf(trace->seq, "\n");

	if (!params->no_index)
		trace_seq_printf(trace->seq, "max:  ");

	if (!params->no_irq)
		format_summary_value(trace->seq,
				     sum.irq_count,
				     sum.max_irq,
				     false);

	if (!params->no_thread)
		format_summary_value(trace->seq,
				     sum.thread_count,
				     sum.max_thread,
				     false);

	if (params->user_hist)
		format_summary_value(trace->seq,
				     sum.user_count,
				     sum.max_user,
				     false);

	trace_seq_printf(trace->seq, "\n");
	trace_seq_do_printf(trace->seq);
	trace_seq_reset(trace->seq);
}

/*
 * timerlat_print_stats - print data for each CPUs
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
			if (params->cpus && !CPU_ISSET(cpu, &params->monitored_cpus))
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

			if (params->user_hist) {
				total += data->hist[cpu].user[bucket];
				trace_seq_printf(trace->seq, "%9d ",
						data->hist[cpu].user[bucket]);
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
		if (params->cpus && !CPU_ISSET(cpu, &params->monitored_cpus))
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->no_irq)
			trace_seq_printf(trace->seq, "%9d ",
					 data->hist[cpu].irq[data->entries]);

		if (!params->no_thread)
			trace_seq_printf(trace->seq, "%9d ",
					 data->hist[cpu].thread[data->entries]);

		if (params->user_hist)
			trace_seq_printf(trace->seq, "%9d ",
					 data->hist[cpu].user[data->entries]);
	}
	trace_seq_printf(trace->seq, "\n");
	trace_seq_do_printf(trace->seq);
	trace_seq_reset(trace->seq);

	timerlat_print_summary(params, trace, data);
	timerlat_print_stats_all(params, trace, data);
	osnoise_report_missed_events(tool);
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
		"         [-t[file]] [-e sys[:event]] [--filter <filter>] [--trigger <trigger>] [-c cpu-list] [-H cpu-list]\\",
		"	  [-P priority] [-E N] [-b N] [--no-irq] [--no-thread] [--no-header] [--no-summary] \\",
		"	  [--no-index] [--with-zeros] [--dma-latency us] [-C[=cgroup_name]] [--no-aa] [--dump-task] [-u|-k]",
		"	  [--warm-up s] [--deepest-idle-state n]",
		"",
		"	  -h/--help: print this menu",
		"	  -a/--auto: set automatic trace mode, stopping the session if argument in us latency is hit",
		"	  -p/--period us: timerlat period in us",
		"	  -i/--irq us: stop trace if the irq latency is higher than the argument in us",
		"	  -T/--thread us: stop trace if the thread latency is higher than the argument in us",
		"	  -s/--stack us: save the stack trace at the IRQ if a thread latency is higher than the argument in us",
		"	  -c/--cpus cpus: run the tracer only on the given cpus",
		"	  -H/--house-keeping cpus: run rtla control threads only on the given cpus",
		"	  -C/--cgroup[=cgroup_name]: set cgroup, if no cgroup_name is passed, the rtla's cgroup will be inherited",
		"	  -d/--duration time[m|h|d]: duration of the session in seconds",
		"	     --dump-tasks: prints the task running on all CPUs if stop conditions are met (depends on !--no-aa)",
		"	  -D/--debug: print debug info",
		"	  -t/--trace[file]: save the stopped trace to [file|timerlat_trace.txt]",
		"	  -e/--event <sys:event>: enable the <sys:event> in the trace instance, multiple -e are allowed",
		"	     --filter <filter>: enable a trace event filter to the previous -e event",
		"	     --trigger <trigger>: enable a trace event trigger to the previous -e event",
		"	  -n/--nano: display data in nanoseconds",
		"	     --no-aa: disable auto-analysis, reducing rtla timerlat cpu usage",
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

	fprintf(stderr, "rtla timerlat hist: a per-cpu histogram of the timer latency (version %s)\n",
			VERSION);

	for (i = 0; msg[i]; i++)
		fprintf(stderr, "%s\n", msg[i]);

	if (usage)
		exit(EXIT_FAILURE);

	exit(EXIT_SUCCESS);
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

	/* disabled by default */
	params->deepest_idle_state = -2;

	/* display data in microseconds */
	params->output_divisor = 1000;
	params->bucket_size = 1;
	params->entries = 256;

	while (1) {
		static struct option long_options[] = {
			{"auto",		required_argument,	0, 'a'},
			{"cpus",		required_argument,	0, 'c'},
			{"cgroup",		optional_argument,	0, 'C'},
			{"bucket-size",		required_argument,	0, 'b'},
			{"debug",		no_argument,		0, 'D'},
			{"entries",		required_argument,	0, 'E'},
			{"duration",		required_argument,	0, 'd'},
			{"house-keeping",	required_argument,	0, 'H'},
			{"help",		no_argument,		0, 'h'},
			{"irq",			required_argument,	0, 'i'},
			{"nano",		no_argument,		0, 'n'},
			{"period",		required_argument,	0, 'p'},
			{"priority",		required_argument,	0, 'P'},
			{"stack",		required_argument,	0, 's'},
			{"thread",		required_argument,	0, 'T'},
			{"trace",		optional_argument,	0, 't'},
			{"user-threads",	no_argument,		0, 'u'},
			{"kernel-threads",	no_argument,		0, 'k'},
			{"user-load",		no_argument,		0, 'U'},
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
			{"no-aa",		no_argument,		0, '9'},
			{"dump-task",		no_argument,		0, '\1'},
			{"warm-up",		required_argument,	0, '\2'},
			{"trace-buffer-size",	required_argument,	0, '\3'},
			{"deepest-idle-state",	required_argument,	0, '\4'},
			{0, 0, 0, 0}
		};

		/* getopt_long stores the option index here. */
		int option_index = 0;

		c = getopt_long(argc, argv, "a:c:C::b:d:e:E:DhH:i:knp:P:s:t::T:uU0123456:7:8:9\1\2:\3:",
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
		case 'c':
			retval = parse_cpu_set(optarg, &params->monitored_cpus);
			if (retval)
				timerlat_hist_usage("\nInvalid -c cpu list\n");
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
			params->kernel_workload = 1;
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
			params->user_workload = 1;
			/* fallback: -u implies in -U */
		case 'U':
			params->user_hist = 1;
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
		case '9':
			params->no_aa = 1;
			break;
		case '\1':
			params->dump_tasks = 1;
			break;
		case '\2':
			params->warmup = get_llong_from_str(optarg);
			break;
		case '\3':
			params->buffer_size = get_llong_from_str(optarg);
			break;
		case '\4':
			params->deepest_idle_state = get_llong_from_str(optarg);
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

	/*
	 * Auto analysis only happens if stop tracing, thus:
	 */
	if (!params->stop_us && !params->stop_total_us)
		params->no_aa = 1;

	if (params->kernel_workload && params->user_workload)
		timerlat_hist_usage("--kernel-threads and --user-threads are mutually exclusive!");

	return params;
}

/*
 * timerlat_hist_apply_config - apply the hist configs to the initialized tool
 */
static int
timerlat_hist_apply_config(struct osnoise_tool *tool, struct timerlat_hist_params *params)
{
	int retval, i;

	if (!params->sleep_time)
		params->sleep_time = 1;

	if (params->cpus) {
		retval = osnoise_set_cpus(tool->context, params->cpus);
		if (retval) {
			err_msg("Failed to apply CPUs config\n");
			goto out_err;
		}
	} else {
		for (i = 0; i < sysconf(_SC_NPROCESSORS_CONF); i++)
			CPU_SET(i, &params->monitored_cpus);
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
	if (!params->kernel_workload && !params->user_hist) {
		retval = tracefs_file_exists(NULL, "osnoise/per_cpu/cpu0/timerlat_fd");
		if (retval) {
			debug_msg("User-space interface detected, setting user-threads\n");
			params->user_workload = 1;
			params->user_hist = 1;
		} else {
			debug_msg("User-space interface not detected, setting kernel-threads\n");
			params->kernel_workload = 1;
		}
	}

	/*
	* Set workload according to type of thread if the kernel supports it.
	* On kernels without support, user threads will have already failed
	* on missing timerlat_fd, and kernel threads do not need it.
	*/
	retval = osnoise_set_workload(tool->context, params->kernel_workload);
	if (retval < -1) {
		err_msg("Failed to set OSNOISE_WORKLOAD option\n");
		goto out_err;
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
static struct trace_instance *hist_inst = NULL;
static void stop_hist(int sig)
{
	if (stop_tracing) {
		/*
		 * Stop requested twice in a row; abort event processing and
		 * exit immediately
		 */
		tracefs_iterate_stop(hist_inst->inst);
		return;
	}
	stop_tracing = 1;
	if (hist_inst)
		trace_instance_stop(hist_inst);
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
	struct timerlat_u_params params_u;
	struct osnoise_tool *tool = NULL;
	struct osnoise_tool *aa = NULL;
	struct trace_instance *trace;
	int dma_latency_fd = -1;
	int return_value = 1;
	pthread_t timerlat_u;
	int retval;
	int nr_cpus, i;

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
	/*
	 * Save trace instance into global variable so that SIGINT can stop
	 * the timerlat tracer.
	 * Otherwise, rtla could loop indefinitely when overloaded.
	 */
	hist_inst = trace;

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

	if (params->cgroup && !params->user_workload) {
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
				goto out_hist;
		}

		if (params->buffer_size > 0) {
			retval = trace_set_buffer_size(&record->trace, params->buffer_size);
			if (retval)
				goto out_hist;
		}
	}

	if (!params->no_aa) {
		aa = osnoise_init_tool("timerlat_aa");
		if (!aa)
			goto out_hist;

		retval = timerlat_aa_init(aa, params->dump_tasks);
		if (retval) {
			err_msg("Failed to enable the auto analysis instance\n");
			goto out_hist;
		}

		retval = enable_timerlat(&aa->trace);
		if (retval) {
			err_msg("Failed to enable timerlat tracer\n");
			goto out_hist;
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
		if (stop_tracing)
			goto out_hist;
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
	if (!params->no_aa)
		trace_instance_start(&aa->trace);
	trace_instance_start(trace);

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

		if (osnoise_trace_is_off(tool, record))
			break;

		/* is there still any user-threads ? */
		if (params->user_workload) {
			if (params_u.stopped_running) {
				debug_msg("timerlat user-space threads stopped!\n");
				break;
			}
		}
	}

	if (params->user_workload && !params_u.stopped_running) {
		params_u.should_run = 0;
		sleep(1);
	}

	timerlat_print_stats(params, tool);

	return_value = 0;

	if (osnoise_trace_is_off(tool, record) && !stop_tracing) {
		printf("rtla timerlat hit stop tracing\n");

		if (!params->no_aa)
			timerlat_auto_analysis(params->stop_us, params->stop_total_us);

		if (params->trace_output) {
			printf("  Saving trace to %s\n", params->trace_output);
			save_trace_to_file(record->trace.inst, params->trace_output);
		}
	}

out_hist:
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
	timerlat_free_histogram(tool->data);
	osnoise_destroy_tool(aa);
	osnoise_destroy_tool(record);
	osnoise_destroy_tool(tool);
	free(params);
	free_cpu_idle_disable_states();
out_exit:
	exit(return_value);
}
