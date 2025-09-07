/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

#include "utils.h"

/*
 * common_params - Parameters shared between timerlat_params and osnoise_params
 */
struct common_params {
	/* trace configuration */
	char			*cpus;
	cpu_set_t		monitored_cpus;
	struct trace_events	*events;
	int			buffer_size;

	/* Timing parameters */
	int			warmup;
	long long		stop_us;
	long long		stop_total_us;
	int			sleep_time;
	int			duration;

	/* Scheduling parameters */
	int			set_sched;
	struct sched_attr	sched_param;
	int			cgroup;
	char			*cgroup_name;
	int			hk_cpus;
	cpu_set_t		hk_cpu_set;
};
