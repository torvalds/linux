/*
 *  (C) 2010,2011       Thomas Renninger <trenn@suse.de>, Novell Inc
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <cpuidle.h>

#include "helpers/helpers.h"
#include "idle_monitor/cpupower-monitor.h"

#define CPUIDLE_STATES_MAX 10
static cstate_t cpuidle_cstates[CPUIDLE_STATES_MAX];
struct cpuidle_monitor cpuidle_sysfs_monitor;

static unsigned long long **previous_count;
static unsigned long long **current_count;
struct timespec start_time;
static unsigned long long timediff;

static int cpuidle_get_count_percent(unsigned int id, double *percent,
				     unsigned int cpu)
{
	unsigned long long statediff = current_count[cpu][id]
		- previous_count[cpu][id];
	dprint("%s: - diff: %llu - percent: %f (%u)\n",
	       cpuidle_cstates[id].name, timediff, *percent, cpu);

	if (timediff == 0)
		*percent = 0.0;
	else
		*percent = ((100.0 * statediff) / timediff);

	dprint("%s: - timediff: %llu - statediff: %llu - percent: %f (%u)\n",
	       cpuidle_cstates[id].name, timediff, statediff, *percent, cpu);

	return 0;
}

static int cpuidle_start(void)
{
	int cpu, state;
	clock_gettime(CLOCK_REALTIME, &start_time);
	for (cpu = 0; cpu < cpu_count; cpu++) {
		for (state = 0; state < cpuidle_sysfs_monitor.hw_states_num;
		     state++) {
			previous_count[cpu][state] =
				cpuidle_state_time(cpu, state);
			dprint("CPU %d - State: %d - Val: %llu\n",
			       cpu, state, previous_count[cpu][state]);
		}
	};
	return 0;
}

static int cpuidle_stop(void)
{
	int cpu, state;
	struct timespec end_time;
	clock_gettime(CLOCK_REALTIME, &end_time);
	timediff = timespec_diff_us(start_time, end_time);

	for (cpu = 0; cpu < cpu_count; cpu++) {
		for (state = 0; state < cpuidle_sysfs_monitor.hw_states_num;
		     state++) {
			current_count[cpu][state] =
				cpuidle_state_time(cpu, state);
			dprint("CPU %d - State: %d - Val: %llu\n",
			       cpu, state, previous_count[cpu][state]);
		}
	};
	return 0;
}

void fix_up_intel_idle_driver_name(char *tmp, int num)
{
	/* fix up cpuidle name for intel idle driver */
	if (!strncmp(tmp, "NHM-", 4)) {
		switch (num) {
		case 1:
			strcpy(tmp, "C1");
			break;
		case 2:
			strcpy(tmp, "C3");
			break;
		case 3:
			strcpy(tmp, "C6");
			break;
		}
	} else if (!strncmp(tmp, "SNB-", 4)) {
		switch (num) {
		case 1:
			strcpy(tmp, "C1");
			break;
		case 2:
			strcpy(tmp, "C3");
			break;
		case 3:
			strcpy(tmp, "C6");
			break;
		case 4:
			strcpy(tmp, "C7");
			break;
		}
	} else if (!strncmp(tmp, "ATM-", 4)) {
		switch (num) {
		case 1:
			strcpy(tmp, "C1");
			break;
		case 2:
			strcpy(tmp, "C2");
			break;
		case 3:
			strcpy(tmp, "C4");
			break;
		case 4:
			strcpy(tmp, "C6");
			break;
		}
	}
}

static struct cpuidle_monitor *cpuidle_register(void)
{
	int num;
	char *tmp;

	/* Assume idle state count is the same for all CPUs */
	cpuidle_sysfs_monitor.hw_states_num = cpuidle_state_count(0);

	if (cpuidle_sysfs_monitor.hw_states_num <= 0)
		return NULL;

	for (num = 0; num < cpuidle_sysfs_monitor.hw_states_num; num++) {
		tmp = cpuidle_state_name(0, num);
		if (tmp == NULL)
			continue;

		fix_up_intel_idle_driver_name(tmp, num);
		strncpy(cpuidle_cstates[num].name, tmp, CSTATE_NAME_LEN - 1);
		free(tmp);

		tmp = cpuidle_state_desc(0, num);
		if (tmp == NULL)
			continue;
		strncpy(cpuidle_cstates[num].desc, tmp,	CSTATE_DESC_LEN - 1);
		free(tmp);

		cpuidle_cstates[num].range = RANGE_THREAD;
		cpuidle_cstates[num].id = num;
		cpuidle_cstates[num].get_count_percent =
			cpuidle_get_count_percent;
	};

	/* Free this at program termination */
	previous_count = malloc(sizeof(long long *) * cpu_count);
	current_count = malloc(sizeof(long long *) * cpu_count);
	for (num = 0; num < cpu_count; num++) {
		previous_count[num] = malloc(sizeof(long long) *
					cpuidle_sysfs_monitor.hw_states_num);
		current_count[num] = malloc(sizeof(long long) *
					cpuidle_sysfs_monitor.hw_states_num);
	}

	cpuidle_sysfs_monitor.name_len = strlen(cpuidle_sysfs_monitor.name);
	return &cpuidle_sysfs_monitor;
}

void cpuidle_unregister(void)
{
	int num;

	for (num = 0; num < cpu_count; num++) {
		free(previous_count[num]);
		free(current_count[num]);
	}
	free(previous_count);
	free(current_count);
}

struct cpuidle_monitor cpuidle_sysfs_monitor = {
	.name			= "Idle_Stats",
	.hw_states		= cpuidle_cstates,
	.start			= cpuidle_start,
	.stop			= cpuidle_stop,
	.do_register		= cpuidle_register,
	.unregister		= cpuidle_unregister,
	.needs_root		= 0,
	.overflow_s		= UINT_MAX,
};
