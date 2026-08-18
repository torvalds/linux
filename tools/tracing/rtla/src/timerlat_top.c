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
static struct timerlat_top_data *timerlat_alloc_top(void)
{
	struct timerlat_top_data *data;
	int cpu;

	data = calloc(1, sizeof(*data));
	if (!data)
		return NULL;

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
	long long value_irq[nr_cpus],
		  value_thread[nr_cpus],
		  value_user[nr_cpus];

	/* Pull summary */
	err = timerlat_bpf_get_summary_value(SUMMARY_CURRENT,
					     value_irq, value_thread, value_user);
	if (err)
		return err;
	for (i = 0; i < nr_cpus; i++) {
		data->cpu_data[i].cur_irq = value_irq[i];
		data->cpu_data[i].cur_thread = value_thread[i];
		data->cpu_data[i].cur_user = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_COUNT,
					     value_irq, value_thread, value_user);
	if (err)
		return err;
	for (i = 0; i < nr_cpus; i++) {
		data->cpu_data[i].irq_count = value_irq[i];
		data->cpu_data[i].thread_count = value_thread[i];
		data->cpu_data[i].user_count = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_MIN,
					     value_irq, value_thread, value_user);
	if (err)
		return err;
	for (i = 0; i < nr_cpus; i++) {
		data->cpu_data[i].min_irq = value_irq[i];
		data->cpu_data[i].min_thread = value_thread[i];
		data->cpu_data[i].min_user = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_MAX,
					     value_irq, value_thread, value_user);
	if (err)
		return err;
	for (i = 0; i < nr_cpus; i++) {
		data->cpu_data[i].max_irq = value_irq[i];
		data->cpu_data[i].max_thread = value_thread[i];
		data->cpu_data[i].max_user = value_user[i];
	}

	err = timerlat_bpf_get_summary_value(SUMMARY_SUM,
					     value_irq, value_thread, value_user);
	if (err)
		return err;
	for (i = 0; i < nr_cpus; i++) {
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
	int i;

	if (params->common.aa_only)
		return;

	if (!params->common.quiet)
		clear_terminal(trace->seq);

	timerlat_top_reset_sum(&summary);

	timerlat_top_header(params, top);

	for_each_monitored_cpu(i, &params->common) {
		timerlat_top_print(top, i);
		timerlat_top_update_sum(top, i, &summary);
	}

	timerlat_top_print_sum(top, &summary);

	trace_seq_do_printf(trace->seq);
	trace_seq_reset(trace->seq);
	osnoise_report_missed_events(top);
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

	top = osnoise_init_tool("timerlat_top");
	if (!top)
		return NULL;

	top->data = timerlat_alloc_top();
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
	const struct common_params *params = tool->params;
	int retval, wait_retval;

	if (params->aa_only) {
		/* Auto-analysis only, just wait for stop tracing */
		timerlat_bpf_wait(-1);
		return 0;
	}

	/* Pull and display data in a loop */
	while (!stop_tracing) {
		wait_retval = timerlat_bpf_wait(params->quiet ? -1 :
						params->sleep_time);

		retval = timerlat_top_bpf_pull_data(tool);
		if (retval) {
			err_msg("Error pulling BPF data\n");
			return retval;
		}

		if (!params->quiet)
			timerlat_print_stats(tool);

		if (wait_retval != 0) {
			/* Stopping requested by tracer */
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

		/* is there still any user-threads ? */
		if (params->user_workload) {
			if (params->user.stopped_running) {
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
