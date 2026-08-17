// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#define _GNU_SOURCE
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
#include "cli.h"
#include "common.h"

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
};

/*
 * timerlat_free_histogram - free runtime data
 */
static void
timerlat_free_histogram(struct timerlat_hist_data *data)
{
	int cpu;

	/* one histogram for IRQ and one for thread, per CPU */
	for (cpu = 0; cpu < nr_cpus; cpu++) {
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
*timerlat_alloc_histogram(int entries, int bucket_size)
{
	struct timerlat_hist_data *data;
	int cpu;

	data = calloc(1, sizeof(*data));
	if (!data)
		return NULL;

	data->entries = entries;
	data->bucket_size = bucket_size;

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
	long long value_irq[nr_cpus],
		  value_thread[nr_cpus],
		  value_user[nr_cpus];

	/* Pull histogram */
	for (i = 0; i < data->entries; i++) {
		err = timerlat_bpf_get_hist_value(i, value_irq, value_thread,
						  value_user);
		if (err)
			return err;
		for (j = 0; j < nr_cpus; j++) {
			data->hist[j].irq[i] = value_irq[j];
			data->hist[j].thread[i] = value_thread[j];
			data->hist[j].user[i] = value_user[j];
		}
	}

	/* Pull summary */
	err = timerlat_bpf_get_summary_value(SUMMARY_COUNT,
					     value_irq, value_thread, value_user);
	if (err)
		return err;
	for (i = 0; i < nr_cpus; i++) {
		data->hist[i].irq_count = value_irq[i];
		data->hist[i].thread_count = value_thread[i];
		data->hist[i].user_count = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_MIN,
					     value_irq, value_thread, value_user);
	if (err)
		return err;
	for (i = 0; i < nr_cpus; i++) {
		data->hist[i].min_irq = value_irq[i];
		data->hist[i].min_thread = value_thread[i];
		data->hist[i].min_user = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_MAX,
					     value_irq, value_thread, value_user);
	if (err)
		return err;
	for (i = 0; i < nr_cpus; i++) {
		data->hist[i].max_irq = value_irq[i];
		data->hist[i].max_thread = value_thread[i];
		data->hist[i].max_user = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_SUM,
					     value_irq, value_thread, value_user);
	if (err)
		return err;
	for (i = 0; i < nr_cpus; i++) {
		data->hist[i].sum_irq = value_irq[i];
		data->hist[i].sum_thread = value_thread[i];
		data->hist[i].sum_user = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_OVERFLOW,
					     value_irq, value_thread, value_user);
	if (err)
		return err;
	for (i = 0; i < nr_cpus; i++) {
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

	for_each_monitored_cpu(cpu, &params->common) {

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

	for_each_monitored_cpu(cpu, &params->common) {

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

	for_each_monitored_cpu(cpu, &params->common) {

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

	for_each_monitored_cpu(cpu, &params->common) {

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

	for_each_monitored_cpu(cpu, &params->common) {

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

	for_each_monitored_cpu(cpu, &params->common) {

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

		for_each_monitored_cpu(cpu, &params->common) {

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

	for_each_monitored_cpu(cpu, &params->common) {

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

	tool = osnoise_init_tool("timerlat_hist");
	if (!tool)
		return NULL;

	tool->data = timerlat_alloc_histogram(params->hist.entries,
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
	int retval;

	while (!stop_tracing) {
		timerlat_bpf_wait(-1);

		if (!stop_tracing) {
			/* Threshold overflow, perform actions on threshold */
			retval = common_threshold_handler(tool);
			if (retval)
				return retval;

			if (!should_continue_tracing(tool->params))
				break;

			if (timerlat_bpf_restart_tracing()) {
				err_msg("Error restarting BPF trace\n");
				return -1;
			}
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
