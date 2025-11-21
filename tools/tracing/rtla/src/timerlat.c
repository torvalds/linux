// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sched.h>

#include "timerlat.h"
#include "timerlat_aa.h"
#include "timerlat_bpf.h"

#define DEFAULT_TIMERLAT_PERIOD	1000			/* 1ms */

static int dma_latency_fd = -1;

/*
 * timerlat_apply_config - apply common configs to the initialized tool
 */
int
timerlat_apply_config(struct osnoise_tool *tool, struct timerlat_params *params)
{
	int retval;

	/*
	 * Try to enable BPF, unless disabled explicitly.
	 * If BPF enablement fails, fall back to tracefs mode.
	 */
	if (getenv("RTLA_NO_BPF") && strncmp(getenv("RTLA_NO_BPF"), "1", 2) == 0) {
		debug_msg("RTLA_NO_BPF set, disabling BPF\n");
		params->mode = TRACING_MODE_TRACEFS;
	} else if (!tep_find_event_by_name(tool->trace.tep, "osnoise", "timerlat_sample")) {
		debug_msg("osnoise:timerlat_sample missing, disabling BPF\n");
		params->mode = TRACING_MODE_TRACEFS;
	} else {
		retval = timerlat_bpf_init(params);
		if (retval) {
			debug_msg("Could not enable BPF\n");
			params->mode = TRACING_MODE_TRACEFS;
		}
	}

	if (params->mode != TRACING_MODE_BPF) {
		/*
		 * In tracefs and mixed mode, timerlat tracer handles stopping
		 * on threshold
		 */
		retval = osnoise_set_stop_us(tool->context, params->common.stop_us);
		if (retval) {
			err_msg("Failed to set stop us\n");
			goto out_err;
		}

		retval = osnoise_set_stop_total_us(tool->context, params->common.stop_total_us);
		if (retval) {
			err_msg("Failed to set stop total us\n");
			goto out_err;
		}
	}


	retval = osnoise_set_timerlat_period_us(tool->context,
						params->timerlat_period_us ?
						params->timerlat_period_us :
						DEFAULT_TIMERLAT_PERIOD);
	if (retval) {
		err_msg("Failed to set timerlat period\n");
		goto out_err;
	}


	retval = osnoise_set_print_stack(tool->context, params->print_stack);
	if (retval) {
		err_msg("Failed to set print stack\n");
		goto out_err;
	}

	/*
	 * If the user did not specify a type of thread, try user-threads first.
	 * Fall back to kernel threads otherwise.
	 */
	if (!params->common.kernel_workload && !params->common.user_data) {
		retval = tracefs_file_exists(NULL, "osnoise/per_cpu/cpu0/timerlat_fd");
		if (retval) {
			debug_msg("User-space interface detected, setting user-threads\n");
			params->common.user_workload = 1;
			params->common.user_data = 1;
		} else {
			debug_msg("User-space interface not detected, setting kernel-threads\n");
			params->common.kernel_workload = 1;
		}
	}

	return common_apply_config(tool, &params->common);

out_err:
	return -1;
}

int timerlat_enable(struct osnoise_tool *tool)
{
	struct timerlat_params *params = to_timerlat_params(tool->params);
	int retval, nr_cpus, i;

	if (params->dma_latency >= 0) {
		dma_latency_fd = set_cpu_dma_latency(params->dma_latency);
		if (dma_latency_fd < 0) {
			err_msg("Could not set /dev/cpu_dma_latency.\n");
			return -1;
		}
	}

	if (params->deepest_idle_state >= -1) {
		if (!have_libcpupower_support()) {
			err_msg("rtla built without libcpupower, --deepest-idle-state is not supported\n");
			return -1;
		}

		nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

		for (i = 0; i < nr_cpus; i++) {
			if (params->common.cpus && !CPU_ISSET(i, &params->common.monitored_cpus))
				continue;
			if (save_cpu_idle_disable_state(i) < 0) {
				err_msg("Could not save cpu idle state.\n");
				return -1;
			}
			if (set_deepest_cpu_idle_state(i, params->deepest_idle_state) < 0) {
				err_msg("Could not set deepest cpu idle state.\n");
				return -1;
			}
		}
	}

	if (!params->no_aa) {
		tool->aa = osnoise_init_tool("timerlat_aa");
		if (!tool->aa)
			return -1;

		retval = timerlat_aa_init(tool->aa, params->dump_tasks);
		if (retval) {
			err_msg("Failed to enable the auto analysis instance\n");
			return retval;
		}

		retval = enable_tracer_by_name(tool->aa->trace.inst, "timerlat");
		if (retval) {
			err_msg("Failed to enable aa tracer\n");
			return retval;
		}
	}

	if (params->common.warmup > 0) {
		debug_msg("Warming up for %d seconds\n", params->common.warmup);
		sleep(params->common.warmup);
		if (stop_tracing)
			return -1;
	}

	/*
	 * Start the tracers here, after having set all instances.
	 *
	 * Let the trace instance start first for the case of hitting a stop
	 * tracing while enabling other instances. The trace instance is the
	 * one with most valuable information.
	 */
	if (tool->record)
		trace_instance_start(&tool->record->trace);
	if (!params->no_aa)
		trace_instance_start(&tool->aa->trace);
	if (params->mode == TRACING_MODE_TRACEFS) {
		trace_instance_start(&tool->trace);
	} else {
		retval = timerlat_bpf_attach();
		if (retval) {
			err_msg("Error attaching BPF program\n");
			return retval;
		}
	}

	return 0;
}

void timerlat_analyze(struct osnoise_tool *tool, bool stopped)
{
	struct timerlat_params *params = to_timerlat_params(tool->params);

	if (stopped) {
		if (!params->no_aa)
			timerlat_auto_analysis(params->common.stop_us,
					       params->common.stop_total_us);
	} else if (params->common.aa_only) {
		char *max_lat;

		/*
		 * If the trace did not stop with --aa-only, at least print
		 * the max known latency.
		 */
		max_lat = tracefs_instance_file_read(trace_inst->inst, "tracing_max_latency", NULL);
		if (max_lat) {
			printf("  Max latency was %s\n", max_lat);
			free(max_lat);
		}
	}
}

void timerlat_free(struct osnoise_tool *tool)
{
	struct timerlat_params *params = to_timerlat_params(tool->params);
	int nr_cpus, i;

	timerlat_aa_destroy();
	if (dma_latency_fd >= 0)
		close(dma_latency_fd);
	if (params->deepest_idle_state >= -1) {
		for (i = 0; i < nr_cpus; i++) {
			if (params->common.cpus &&
			    !CPU_ISSET(i, &params->common.monitored_cpus))
				continue;
			restore_cpu_idle_disable_state(i);
		}
	}

	osnoise_destroy_tool(tool->aa);

	if (params->mode != TRACING_MODE_TRACEFS)
		timerlat_bpf_destroy();
	free_cpu_idle_disable_states();
}

static void timerlat_usage(int err)
{
	int i;

	static const char * const msg[] = {
		"",
		"timerlat version " VERSION,
		"",
		"  usage: [rtla] timerlat [MODE] ...",
		"",
		"  modes:",
		"     top   - prints the summary from timerlat tracer",
		"     hist  - prints a histogram of timer latencies",
		"",
		"if no MODE is given, the top mode is called, passing the arguments",
		NULL,
	};

	for (i = 0; msg[i]; i++)
		fprintf(stderr, "%s\n", msg[i]);
	exit(err);
}

int timerlat_main(int argc, char *argv[])
{
	if (argc == 0)
		goto usage;

	/*
	 * if timerlat was called without any argument, run the
	 * default cmdline.
	 */
	if (argc == 1) {
		run_tool(&timerlat_top_ops, argc, argv);
		exit(0);
	}

	if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)) {
		timerlat_usage(0);
	} else if (strncmp(argv[1], "-", 1) == 0) {
		/* the user skipped the tool, call the default one */
		run_tool(&timerlat_top_ops, argc, argv);
		exit(0);
	} else if (strcmp(argv[1], "top") == 0) {
		run_tool(&timerlat_top_ops, argc-1, &argv[1]);
		exit(0);
	} else if (strcmp(argv[1], "hist") == 0) {
		run_tool(&timerlat_hist_ops, argc-1, &argv[1]);
		exit(0);
	}

usage:
	timerlat_usage(1);
	exit(1);
}
