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

#include "timerlat.h"
#include "timerlat_aa.h"
#include "timerlat_bpf.h"

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
}

static void timerlat_free_histogram_tool(struct osnoise_tool *tool)
{
	timerlat_free_histogram(tool->data);
	timerlat_free(tool);
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
	struct timerlat_params *params = to_timerlat_params(tool->params);
	struct timerlat_hist_data *data = tool->data;
	int entries = data->entries;
	int bucket;
	int *hist;

	if (params->common.output_divisor)
		latency = latency / params->common.output_divisor;

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
 * timerlat_hist_bpf_pull_data - copy data from BPF maps into userspace
 */
static int timerlat_hist_bpf_pull_data(struct osnoise_tool *tool)
{
	struct timerlat_hist_data *data = tool->data;
	int i, j, err;
	long long value_irq[data->nr_cpus],
		  value_thread[data->nr_cpus],
		  value_user[data->nr_cpus];

	/* Pull histogram */
	for (i = 0; i < data->entries; i++) {
		err = timerlat_bpf_get_hist_value(i, value_irq, value_thread,
						  value_user, data->nr_cpus);
		if (err)
			return err;
		for (j = 0; j < data->nr_cpus; j++) {
			data->hist[j].irq[i] = value_irq[j];
			data->hist[j].thread[i] = value_thread[j];
			data->hist[j].user[i] = value_user[j];
		}
	}

	/* Pull summary */
	err = timerlat_bpf_get_summary_value(SUMMARY_COUNT,
					     value_irq, value_thread, value_user,
					     data->nr_cpus);
	if (err)
		return err;
	for (i = 0; i < data->nr_cpus; i++) {
		data->hist[i].irq_count = value_irq[i];
		data->hist[i].thread_count = value_thread[i];
		data->hist[i].user_count = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_MIN,
					     value_irq, value_thread, value_user,
					     data->nr_cpus);
	if (err)
		return err;
	for (i = 0; i < data->nr_cpus; i++) {
		data->hist[i].min_irq = value_irq[i];
		data->hist[i].min_thread = value_thread[i];
		data->hist[i].min_user = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_MAX,
					     value_irq, value_thread, value_user,
					     data->nr_cpus);
	if (err)
		return err;
	for (i = 0; i < data->nr_cpus; i++) {
		data->hist[i].max_irq = value_irq[i];
		data->hist[i].max_thread = value_thread[i];
		data->hist[i].max_user = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_SUM,
					     value_irq, value_thread, value_user,
					     data->nr_cpus);
	if (err)
		return err;
	for (i = 0; i < data->nr_cpus; i++) {
		data->hist[i].sum_irq = value_irq[i];
		data->hist[i].sum_thread = value_thread[i];
		data->hist[i].sum_user = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_OVERFLOW,
					     value_irq, value_thread, value_user,
					     data->nr_cpus);
	if (err)
		return err;
	for (i = 0; i < data->nr_cpus; i++) {
		data->hist[i].irq[data->entries] = value_irq[i];
		data->hist[i].thread[data->entries] = value_thread[i];
		data->hist[i].user[data->entries] = value_user[i];
	}

	return 0;
}

/*
 * timerlat_hist_header - print the header of the tracer to the output
 */
static void timerlat_hist_header(struct osnoise_tool *tool)
{
	struct timerlat_params *params = to_timerlat_params(tool->params);
	struct timerlat_hist_data *data = tool->data;
	struct trace_seq *s = tool->trace.seq;
	char duration[26];
	int cpu;

	if (params->common.hist.no_header)
		return;

	get_duration(tool->start_time, duration, sizeof(duration));
	trace_seq_printf(s, "# RTLA timerlat histogram\n");
	trace_seq_printf(s, "# Time unit is %s (%s)\n",
			params->common.output_divisor == 1 ? "nanoseconds" : "microseconds",
			params->common.output_divisor == 1 ? "ns" : "us");

	trace_seq_printf(s, "# Duration: %s\n", duration);

	if (!params->common.hist.no_index)
		trace_seq_printf(s, "Index");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->common.cpus && !CPU_ISSET(cpu, &params->common.monitored_cpus))
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->common.hist.no_irq)
			trace_seq_printf(s, "   IRQ-%03d", cpu);

		if (!params->common.hist.no_thread)
			trace_seq_printf(s, "   Thr-%03d", cpu);

		if (params->common.user_data)
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
timerlat_print_summary(struct timerlat_params *params,
		       struct trace_instance *trace,
		       struct timerlat_hist_data *data)
{
	int cpu;

	if (params->common.hist.no_summary)
		return;

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "count:");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->common.cpus && !CPU_ISSET(cpu, &params->common.monitored_cpus))
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->common.hist.no_irq)
			trace_seq_printf(trace->seq, "%9llu ",
					data->hist[cpu].irq_count);

		if (!params->common.hist.no_thread)
			trace_seq_printf(trace->seq, "%9llu ",
					data->hist[cpu].thread_count);

		if (params->common.user_data)
			trace_seq_printf(trace->seq, "%9llu ",
					 data->hist[cpu].user_count);
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "min:  ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->common.cpus && !CPU_ISSET(cpu, &params->common.monitored_cpus))
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->common.hist.no_irq)
			format_summary_value(trace->seq,
					     data->hist[cpu].irq_count,
					     data->hist[cpu].min_irq,
					     false);

		if (!params->common.hist.no_thread)
			format_summary_value(trace->seq,
					     data->hist[cpu].thread_count,
					     data->hist[cpu].min_thread,
					     false);

		if (params->common.user_data)
			format_summary_value(trace->seq,
					     data->hist[cpu].user_count,
					     data->hist[cpu].min_user,
					     false);
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "avg:  ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->common.cpus && !CPU_ISSET(cpu, &params->common.monitored_cpus))
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->common.hist.no_irq)
			format_summary_value(trace->seq,
					     data->hist[cpu].irq_count,
					     data->hist[cpu].sum_irq,
					     true);

		if (!params->common.hist.no_thread)
			format_summary_value(trace->seq,
					     data->hist[cpu].thread_count,
					     data->hist[cpu].sum_thread,
					     true);

		if (params->common.user_data)
			format_summary_value(trace->seq,
					     data->hist[cpu].user_count,
					     data->hist[cpu].sum_user,
					     true);
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "max:  ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->common.cpus && !CPU_ISSET(cpu, &params->common.monitored_cpus))
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->common.hist.no_irq)
			format_summary_value(trace->seq,
					     data->hist[cpu].irq_count,
					     data->hist[cpu].max_irq,
					     false);

		if (!params->common.hist.no_thread)
			format_summary_value(trace->seq,
					     data->hist[cpu].thread_count,
					     data->hist[cpu].max_thread,
					     false);

		if (params->common.user_data)
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
timerlat_print_stats_all(struct timerlat_params *params,
			 struct trace_instance *trace,
			 struct timerlat_hist_data *data)
{
	struct timerlat_hist_cpu *cpu_data;
	struct timerlat_hist_cpu sum;
	int cpu;

	if (params->common.hist.no_summary)
		return;

	memset(&sum, 0, sizeof(sum));
	sum.min_irq = ~0;
	sum.min_thread = ~0;
	sum.min_user = ~0;

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->common.cpus && !CPU_ISSET(cpu, &params->common.monitored_cpus))
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

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "ALL:  ");

	if (!params->common.hist.no_irq)
		trace_seq_printf(trace->seq, "      IRQ");

	if (!params->common.hist.no_thread)
		trace_seq_printf(trace->seq, "       Thr");

	if (params->common.user_data)
		trace_seq_printf(trace->seq, "       Usr");

	trace_seq_printf(trace->seq, "\n");

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "count:");

	if (!params->common.hist.no_irq)
		trace_seq_printf(trace->seq, "%9llu ",
				 sum.irq_count);

	if (!params->common.hist.no_thread)
		trace_seq_printf(trace->seq, "%9llu ",
				 sum.thread_count);

	if (params->common.user_data)
		trace_seq_printf(trace->seq, "%9llu ",
				 sum.user_count);

	trace_seq_printf(trace->seq, "\n");

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "min:  ");

	if (!params->common.hist.no_irq)
		format_summary_value(trace->seq,
				     sum.irq_count,
				     sum.min_irq,
				     false);

	if (!params->common.hist.no_thread)
		format_summary_value(trace->seq,
				     sum.thread_count,
				     sum.min_thread,
				     false);

	if (params->common.user_data)
		format_summary_value(trace->seq,
				     sum.user_count,
				     sum.min_user,
				     false);

	trace_seq_printf(trace->seq, "\n");

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "avg:  ");

	if (!params->common.hist.no_irq)
		format_summary_value(trace->seq,
				     sum.irq_count,
				     sum.sum_irq,
				     true);

	if (!params->common.hist.no_thread)
		format_summary_value(trace->seq,
				     sum.thread_count,
				     sum.sum_thread,
				     true);

	if (params->common.user_data)
		format_summary_value(trace->seq,
				     sum.user_count,
				     sum.sum_user,
				     true);

	trace_seq_printf(trace->seq, "\n");

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "max:  ");

	if (!params->common.hist.no_irq)
		format_summary_value(trace->seq,
				     sum.irq_count,
				     sum.max_irq,
				     false);

	if (!params->common.hist.no_thread)
		format_summary_value(trace->seq,
				     sum.thread_count,
				     sum.max_thread,
				     false);

	if (params->common.user_data)
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
timerlat_print_stats(struct osnoise_tool *tool)
{
	struct timerlat_params *params = to_timerlat_params(tool->params);
	struct timerlat_hist_data *data = tool->data;
	struct trace_instance *trace = &tool->trace;
	int bucket, cpu;
	int total;

	timerlat_hist_header(tool);

	for (bucket = 0; bucket < data->entries; bucket++) {
		total = 0;

		if (!params->common.hist.no_index)
			trace_seq_printf(trace->seq, "%-6d",
					 bucket * data->bucket_size);

		for (cpu = 0; cpu < data->nr_cpus; cpu++) {
			if (params->common.cpus && !CPU_ISSET(cpu, &params->common.monitored_cpus))
				continue;

			if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
				continue;

			if (!params->common.hist.no_irq) {
				total += data->hist[cpu].irq[bucket];
				trace_seq_printf(trace->seq, "%9d ",
						data->hist[cpu].irq[bucket]);
			}

			if (!params->common.hist.no_thread) {
				total += data->hist[cpu].thread[bucket];
				trace_seq_printf(trace->seq, "%9d ",
						data->hist[cpu].thread[bucket]);
			}

			if (params->common.user_data) {
				total += data->hist[cpu].user[bucket];
				trace_seq_printf(trace->seq, "%9d ",
						data->hist[cpu].user[bucket]);
			}

		}

		if (total == 0 && !params->common.hist.with_zeros) {
			trace_seq_reset(trace->seq);
			continue;
		}

		trace_seq_printf(trace->seq, "\n");
		trace_seq_do_printf(trace->seq);
		trace_seq_reset(trace->seq);
	}

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "over: ");

	for (cpu = 0; cpu < data->nr_cpus; cpu++) {
		if (params->common.cpus && !CPU_ISSET(cpu, &params->common.monitored_cpus))
			continue;

		if (!data->hist[cpu].irq_count && !data->hist[cpu].thread_count)
			continue;

		if (!params->common.hist.no_irq)
			trace_seq_printf(trace->seq, "%9d ",
					 data->hist[cpu].irq[data->entries]);

		if (!params->common.hist.no_thread)
			trace_seq_printf(trace->seq, "%9d ",
					 data->hist[cpu].thread[data->entries]);

		if (params->common.user_data)
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
		"	     --on-threshold <action>: define action to be executed at latency threshold, multiple are allowed",
		"	     --on-end <action>: define action to be executed at measurement end, multiple are allowed",
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
static struct common_params
*timerlat_hist_parse_args(int argc, char *argv[])
{
	struct timerlat_params *params;
	struct trace_events *tevent;
	int auto_thresh;
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
	params->common.hist.bucket_size = 1;
	params->common.hist.entries = 256;

	/* default to BPF mode */
	params->mode = TRACING_MODE_BPF;

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
			{"on-threshold",	required_argument,	0, '\5'},
			{"on-end",		required_argument,	0, '\6'},
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
			params->common.stop_total_us = auto_thresh;
			params->common.stop_us = auto_thresh;

			/* get stack trace */
			params->print_stack = auto_thresh;

			/* set trace */
			trace_output = "timerlat_trace.txt";

			break;
		case 'c':
			retval = parse_cpu_set(optarg, &params->common.monitored_cpus);
			if (retval)
				timerlat_hist_usage("\nInvalid -c cpu list\n");
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
		case 'b':
			params->common.hist.bucket_size = get_llong_from_str(optarg);
			if (params->common.hist.bucket_size == 0 ||
			    params->common.hist.bucket_size >= 1000000)
				timerlat_hist_usage("Bucket size needs to be > 0 and <= 1000000\n");
			break;
		case 'D':
			config_debug = 1;
			break;
		case 'd':
			params->common.duration = parse_seconds_duration(optarg);
			if (!params->common.duration)
				timerlat_hist_usage("Invalid -D duration\n");
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
		case 'E':
			params->common.hist.entries = get_llong_from_str(optarg);
			if (params->common.hist.entries < 10 ||
			    params->common.hist.entries > 9999999)
				timerlat_hist_usage("Entries must be > 10 and < 9999999\n");
			break;
		case 'h':
		case '?':
			timerlat_hist_usage(NULL);
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
			params->common.kernel_workload = 1;
			break;
		case 'n':
			params->common.output_divisor = 1;
			break;
		case 'p':
			params->timerlat_period_us = get_llong_from_str(optarg);
			if (params->timerlat_period_us > 1000000)
				timerlat_hist_usage("Period longer than 1 s\n");
			break;
		case 'P':
			retval = parse_prio(optarg, &params->common.sched_param);
			if (retval == -1)
				timerlat_hist_usage("Invalid -P priority");
			params->common.set_sched = 1;
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
			params->common.user_workload = 1;
			/* fallback: -u implies in -U */
		case 'U':
			params->common.user_data = 1;
			break;
		case '0': /* no irq */
			params->common.hist.no_irq = 1;
			break;
		case '1': /* no thread */
			params->common.hist.no_thread = 1;
			break;
		case '2': /* no header */
			params->common.hist.no_header = 1;
			break;
		case '3': /* no summary */
			params->common.hist.no_summary = 1;
			break;
		case '4': /* no index */
			params->common.hist.no_index = 1;
			break;
		case '5': /* with zeros */
			params->common.hist.with_zeros = 1;
			break;
		case '6': /* trigger */
			if (params->common.events) {
				retval = trace_event_add_trigger(params->common.events, optarg);
				if (retval) {
					err_msg("Error adding trigger %s\n", optarg);
					exit(EXIT_FAILURE);
				}
			} else {
				timerlat_hist_usage("--trigger requires a previous -e\n");
			}
			break;
		case '7': /* filter */
			if (params->common.events) {
				retval = trace_event_add_filter(params->common.events, optarg);
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
			params->common.warmup = get_llong_from_str(optarg);
			break;
		case '\3':
			params->common.buffer_size = get_llong_from_str(optarg);
			break;
		case '\4':
			params->deepest_idle_state = get_llong_from_str(optarg);
			break;
		case '\5':
			retval = actions_parse(&params->common.threshold_actions, optarg,
					       "timerlat_trace.txt");
			if (retval) {
				err_msg("Invalid action %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case '\6':
			retval = actions_parse(&params->common.end_actions, optarg,
					       "timerlat_trace.txt");
			if (retval) {
				err_msg("Invalid action %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		default:
			timerlat_hist_usage("Invalid option");
		}
	}

	if (trace_output)
		actions_add_trace_output(&params->common.threshold_actions, trace_output);

	if (geteuid()) {
		err_msg("rtla needs root permission\n");
		exit(EXIT_FAILURE);
	}

	if (params->common.hist.no_irq && params->common.hist.no_thread)
		timerlat_hist_usage("no-irq and no-thread set, there is nothing to do here");

	if (params->common.hist.no_index && !params->common.hist.with_zeros)
		timerlat_hist_usage("no-index set with with-zeros is not set - it does not make sense");

	/*
	 * Auto analysis only happens if stop tracing, thus:
	 */
	if (!params->common.stop_us && !params->common.stop_total_us)
		params->no_aa = 1;

	if (params->common.kernel_workload && params->common.user_workload)
		timerlat_hist_usage("--kernel-threads and --user-threads are mutually exclusive!");

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
 * timerlat_hist_apply_config - apply the hist configs to the initialized tool
 */
static int
timerlat_hist_apply_config(struct osnoise_tool *tool)
{
	struct timerlat_params *params = to_timerlat_params(tool->params);
	int retval;

	retval = timerlat_apply_config(tool, params);
	if (retval)
		goto out_err;

	return 0;

out_err:
	return -1;
}

/*
 * timerlat_init_hist - initialize a timerlat hist tool with parameters
 */
static struct osnoise_tool
*timerlat_init_hist(struct common_params *params)
{
	struct osnoise_tool *tool;
	int nr_cpus;

	nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

	tool = osnoise_init_tool("timerlat_hist");
	if (!tool)
		return NULL;

	tool->data = timerlat_alloc_histogram(nr_cpus, params->hist.entries,
					      params->hist.bucket_size);
	if (!tool->data)
		goto out_err;

	tep_register_event_handler(tool->trace.tep, -1, "ftrace", "timerlat",
				   timerlat_hist_handler, tool);

	return tool;

out_err:
	osnoise_destroy_tool(tool);
	return NULL;
}

static int timerlat_hist_bpf_main_loop(struct osnoise_tool *tool)
{
	struct timerlat_params *params = to_timerlat_params(tool->params);
	int retval;

	while (!stop_tracing) {
		timerlat_bpf_wait(-1);

		if (!stop_tracing) {
			/* Threshold overflow, perform actions on threshold */
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
	}
	timerlat_bpf_detach();

	retval = timerlat_hist_bpf_pull_data(tool);
	if (retval)
		err_msg("Error pulling BPF data\n");

	return retval;
}

static int timerlat_hist_main(struct osnoise_tool *tool)
{
	struct timerlat_params *params = to_timerlat_params(tool->params);
	int retval;

	if (params->mode == TRACING_MODE_TRACEFS)
		retval = hist_main_loop(tool);
	else
		retval = timerlat_hist_bpf_main_loop(tool);

	return retval;
}

struct tool_ops timerlat_hist_ops = {
	.tracer = "timerlat",
	.comm_prefix = "timerlat/",
	.parse_args = timerlat_hist_parse_args,
	.init_tool = timerlat_init_hist,
	.apply_config = timerlat_hist_apply_config,
	.enable = timerlat_enable,
	.main = timerlat_hist_main,
	.print_stats = timerlat_print_stats,
	.analyze = timerlat_analyze,
	.free = timerlat_free_histogram_tool,
};
