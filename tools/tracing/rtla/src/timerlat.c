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

#define DEFAULT_TIMERLAT_PERIOD	1000			/* 1ms */

/*
 * timerlat_apply_config - apply common configs to the initialized tool
 */
int
timerlat_apply_config(struct osnoise_tool *tool, struct timerlat_params *params)
{
	int retval, i;

	if (!params->sleep_time)
		params->sleep_time = 1;

	retval = osnoise_set_cpus(tool->context, params->cpus ? params->cpus : "all");
	if (retval) {
		err_msg("Failed to apply CPUs config\n");
		goto out_err;
	}

	if (!params->cpus) {
		for (i = 0; i < sysconf(_SC_NPROCESSORS_CONF); i++)
			CPU_SET(i, &params->monitored_cpus);
	}

	retval = osnoise_set_stop_us(tool->context, params->stop_us);
	if (retval) {
		err_msg("Failed to set stop us\n");
		goto out_err;
	}

	retval = osnoise_set_stop_total_us(tool->context, params->stop_total_us);
	if (retval) {
		err_msg("Failed to set stop total us\n");
		goto out_err;
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
	if (!params->kernel_workload && !params->user_data) {
		retval = tracefs_file_exists(NULL, "osnoise/per_cpu/cpu0/timerlat_fd");
		if (retval) {
			debug_msg("User-space interface detected, setting user-threads\n");
			params->user_workload = 1;
			params->user_data = 1;
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
		timerlat_top_main(argc, argv);
		exit(0);
	}

	if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)) {
		timerlat_usage(0);
	} else if (strncmp(argv[1], "-", 1) == 0) {
		/* the user skipped the tool, call the default one */
		timerlat_top_main(argc, argv);
		exit(0);
	} else if (strcmp(argv[1], "top") == 0) {
		timerlat_top_main(argc-1, &argv[1]);
		exit(0);
	} else if (strcmp(argv[1], "hist") == 0) {
		timerlat_hist_main(argc-1, &argv[1]);
		exit(0);
	}

usage:
	timerlat_usage(1);
	exit(1);
}
