// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <unistd.h>
#include "common.h"

/*
 * common_apply_config - apply common configs to the initialized tool
 */
int
common_apply_config(struct osnoise_tool *tool, struct common_params *params)
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
	 * Set workload according to type of thread if the kernel supports it.
	 * On kernels without support, user threads will have already failed
	 * on missing fd, and kernel threads do not need it.
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

