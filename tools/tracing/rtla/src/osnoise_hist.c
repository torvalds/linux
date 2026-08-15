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

#include "osnoise.h"
#include "cli.h"

struct osnoise_hist_cpu {
	int			*samples;
	int			count;

	unsigned long long	min_sample;
	unsigned long long	sum_sample;
	unsigned long long	max_sample;

};

struct osnoise_hist_data {
	struct tracefs_hist	*trace_hist;
	struct osnoise_hist_cpu	*hist;
	int			entries;
	int			bucket_size;
};

/*
 * osnoise_free_histogram - free runtime data
 */
static void
osnoise_free_histogram(struct osnoise_hist_data *data)
{
	int cpu;

	/* one histogram for IRQ and one for thread, per CPU */
	for (cpu = 0; cpu < nr_cpus; cpu++) {
		if (data->hist[cpu].samples)
			free(data->hist[cpu].samples);
	}

	/* one set of histograms per CPU */
	if (data->hist)
		free(data->hist);

	free(data);
}

static void osnoise_free_hist_tool(struct osnoise_tool *tool)
{
	osnoise_free_histogram(tool->data);
}

/*
 * osnoise_alloc_histogram - alloc runtime data
 */
static struct osnoise_hist_data
*osnoise_alloc_histogram(int entries, int bucket_size)
{
	struct osnoise_hist_data *data;
	int cpu;

	data = calloc(1, sizeof(*data));
	if (!data)
		return NULL;

	data->entries = entries;
	data->bucket_size = bucket_size;

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
	osnoise_free_histogram(data);
	return NULL;
}

static void osnoise_hist_update_multiple(struct osnoise_tool *tool, int cpu,
					 unsigned long long duration, int count)
{
	struct osnoise_params *params = to_osnoise_params(tool->params);
	struct osnoise_hist_data *data = tool->data;
	unsigned long long total_duration;
	int entries = data->entries;
	int bucket;
	int *hist;

	if (params->common.output_divisor)
		duration = duration / params->common.output_divisor;

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
 * osnoise_destroy_trace_hist - disable events used to collect histogram
 */
static void osnoise_destroy_trace_hist(struct osnoise_tool *tool)
{
	struct osnoise_hist_data *data = tool->data;

	tracefs_hist_pause(tool->trace.inst, data->trace_hist);
	tracefs_hist_destroy(tool->trace.inst, data->trace_hist);
}

/*
 * osnoise_init_trace_hist - enable events used to collect histogram
 */
static int osnoise_init_trace_hist(struct osnoise_tool *tool)
{
	struct osnoise_params *params = to_osnoise_params(tool->params);
	struct osnoise_hist_data *data = tool->data;
	int bucket_size;
	char buff[128];
	int retval = 0;

	/*
	 * Set the size of the bucket.
	 */
	bucket_size = params->common.output_divisor * params->common.hist.bucket_size;
	snprintf(buff, sizeof(buff), "duration.buckets=%d", bucket_size);

	data->trace_hist = tracefs_hist_alloc(tool->trace.tep, "osnoise", "sample_threshold",
			buff, TRACEFS_HIST_KEY_NORMAL);
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
	osnoise_destroy_trace_hist(tool);
	return 1;
}

/*
 * osnoise_read_trace_hist - parse histogram file and file osnoise histogram
 */
static void osnoise_read_trace_hist(struct osnoise_tool *tool)
{
	struct osnoise_hist_data *data = tool->data;
	long long cpu, counter, duration;
	char *content, *position;

	tracefs_hist_pause(tool->trace.inst, data->trace_hist);

	content = tracefs_event_file_read(tool->trace.inst, "osnoise",
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

		osnoise_hist_update_multiple(tool, cpu, duration, counter);
	}
	free(content);
}

/*
 * osnoise_hist_header - print the header of the tracer to the output
 */
static void osnoise_hist_header(struct osnoise_tool *tool)
{
	struct osnoise_params *params = to_osnoise_params(tool->params);
	struct osnoise_hist_data *data = tool->data;
	struct trace_seq *s = tool->trace.seq;
	char duration[26];
	int cpu;

	if (params->common.hist.no_header)
		return;

	get_duration(tool->start_time, duration, sizeof(duration));
	trace_seq_printf(s, "# RTLA osnoise histogram\n");
	trace_seq_printf(s, "# Time unit is %s (%s)\n",
			params->common.output_divisor == 1 ? "nanoseconds" : "microseconds",
			params->common.output_divisor == 1 ? "ns" : "us");

	trace_seq_printf(s, "# Duration: %s\n", duration);

	if (!params->common.hist.no_index)
		trace_seq_printf(s, "Index");

	for_each_monitored_cpu(cpu, &params->common) {

		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(s, "   CPU-%03d", cpu);
	}
	trace_seq_printf(s, "\n");

	trace_seq_do_printf(s);
	trace_seq_reset(s);
}

/*
 * osnoise_print_summary - print the summary of the hist data to the output
 */
static void
osnoise_print_summary(struct osnoise_params *params,
		       struct trace_instance *trace,
		       struct osnoise_hist_data *data)
{
	int cpu;

	if (params->common.hist.no_summary)
		return;

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "count:");

	for_each_monitored_cpu(cpu, &params->common) {
		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(trace->seq, "%9d ", data->hist[cpu].count);
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "min:  ");

	for_each_monitored_cpu(cpu, &params->common) {

		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(trace->seq, "%9llu ",	data->hist[cpu].min_sample);

	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "avg:  ");

	for_each_monitored_cpu(cpu, &params->common) {

		if (!data->hist[cpu].count)
			continue;

		if (data->hist[cpu].count)
			trace_seq_printf(trace->seq, "%9.2f ",
				((double) data->hist[cpu].sum_sample) / data->hist[cpu].count);
		else
			trace_seq_printf(trace->seq, "        - ");
	}
	trace_seq_printf(trace->seq, "\n");

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "max:  ");

	for_each_monitored_cpu(cpu, &params->common) {

		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(trace->seq, "%9llu ", data->hist[cpu].max_sample);

	}
	trace_seq_printf(trace->seq, "\n");
	trace_seq_do_printf(trace->seq);
	trace_seq_reset(trace->seq);
}

/*
 * osnoise_print_stats - print data for all CPUs
 */
static void
osnoise_print_stats(struct osnoise_tool *tool)
{
	struct osnoise_params *params = to_osnoise_params(tool->params);
	struct osnoise_hist_data *data = tool->data;
	struct trace_instance *trace = &tool->trace;
	int has_samples = 0;
	int bucket, cpu;
	int total;

	osnoise_hist_header(tool);

	for (bucket = 0; bucket < data->entries; bucket++) {
		total = 0;

		if (!params->common.hist.no_index)
			trace_seq_printf(trace->seq, "%-6d",
					 bucket * data->bucket_size);

		for_each_monitored_cpu(cpu, &params->common) {

			if (!data->hist[cpu].count)
				continue;

			total += data->hist[cpu].samples[bucket];
			trace_seq_printf(trace->seq, "%9d ", data->hist[cpu].samples[bucket]);
		}

		if (total == 0 && !params->common.hist.with_zeros) {
			trace_seq_reset(trace->seq);
			continue;
		}

		/* There are samples above the threshold */
		has_samples = 1;
		trace_seq_printf(trace->seq, "\n");
		trace_seq_do_printf(trace->seq);
		trace_seq_reset(trace->seq);
	}

	/*
	 * If no samples were recorded, skip calculations, print zeroed statistics
	 * and return.
	 */
	if (!has_samples) {
		trace_seq_reset(trace->seq);
		trace_seq_printf(trace->seq, "over: 0\ncount: 0\nmin: 0\navg: 0\nmax: 0\n");
		trace_seq_do_printf(trace->seq);
		trace_seq_reset(trace->seq);
		return;
	}

	if (!params->common.hist.no_index)
		trace_seq_printf(trace->seq, "over: ");

	for_each_monitored_cpu(cpu, &params->common) {

		if (!data->hist[cpu].count)
			continue;

		trace_seq_printf(trace->seq, "%9d ",
				 data->hist[cpu].samples[data->entries]);
	}
	trace_seq_printf(trace->seq, "\n");
	trace_seq_do_printf(trace->seq);
	trace_seq_reset(trace->seq);

	osnoise_print_summary(params, trace, data);
	osnoise_report_missed_events(tool);
}

/*
 * osnoise_hist_apply_config - apply the hist configs to the initialized tool
 */
static int
osnoise_hist_apply_config(struct osnoise_tool *tool)
{
	return osnoise_apply_config(tool, to_osnoise_params(tool->params));
}

/*
 * osnoise_init_hist - initialize a osnoise hist tool with parameters
 */
static struct osnoise_tool
*osnoise_init_hist(struct common_params *params)
{
	struct osnoise_tool *tool;

	tool = osnoise_init_tool("osnoise_hist");
	if (!tool)
		return NULL;

	tool->data = osnoise_alloc_histogram(params->hist.entries,
					     params->hist.bucket_size);
	if (!tool->data)
		goto out_err;

	return tool;

out_err:
	osnoise_destroy_tool(tool);
	return NULL;
}

static int osnoise_hist_enable(struct osnoise_tool *tool)
{
	int retval;

	retval = osnoise_init_trace_hist(tool);
	if (retval)
		return retval;

	return osnoise_enable(tool);
}

static int osnoise_hist_main_loop(struct osnoise_tool *tool)
{
	int retval;

	retval = hist_main_loop(tool);
	osnoise_read_trace_hist(tool);

	return retval;
}

struct tool_ops osnoise_hist_ops = {
	.tracer = "osnoise",
	.comm_prefix = "osnoise/",
	.parse_args = osnoise_hist_parse_args,
	.init_tool = osnoise_init_hist,
	.apply_config = osnoise_hist_apply_config,
	.enable = osnoise_hist_enable,
	.main = osnoise_hist_main_loop,
	.print_stats = osnoise_print_stats,
	.free = osnoise_free_hist_tool,
};
