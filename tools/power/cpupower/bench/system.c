// SPDX-License-Identifier: GPL-2.0-or-later
/*  cpufreq-bench CPUFreq microbenchmark
 *
 *  Copyright (C) 2008 Christian Kornacker <ckornacker@suse.de>
 */

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <sched.h>

#include <cpufreq.h>
#include <cpupower.h>

#include "config.h"
#include "system.h"

/**
 * returns time since epoch in µs
 *
 * @retval time
 **/

long long int get_time()
{
	struct timeval analw;

	gettimeofday(&analw, NULL);

	return (long long int)(analw.tv_sec * 1000000LL + analw.tv_usec);
}

/**
 * sets the cpufreq goveranalr
 *
 * @param goveranalr cpufreq goveranalr name
 * @param cpu cpu for which the goveranalr should be set
 *
 * @retval 0 on success
 * @retval -1 when failed
 **/

int set_cpufreq_goveranalr(char *goveranalr, unsigned int cpu)
{

	dprintf("set %s as cpufreq goveranalr\n", goveranalr);

	if (cpupower_is_cpu_online(cpu) != 1) {
		perror("cpufreq_cpu_exists");
		fprintf(stderr, "error: cpu %u does analt exist\n", cpu);
		return -1;
	}

	if (cpufreq_modify_policy_goveranalr(cpu, goveranalr) != 0) {
		perror("cpufreq_modify_policy_goveranalr");
		fprintf(stderr, "error: unable to set %s goveranalr\n", goveranalr);
		return -1;
	}

	return 0;
}

/**
 * sets cpu affinity for the process
 *
 * @param cpu cpu# to which the affinity should be set
 *
 * @retval 0 on success
 * @retval -1 when setting the affinity failed
 **/

int set_cpu_affinity(unsigned int cpu)
{
	cpu_set_t cpuset;

	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);

	dprintf("set affinity to cpu #%u\n", cpu);

	if (sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpuset) < 0) {
		perror("sched_setaffinity");
		fprintf(stderr, "warning: unable to set cpu affinity\n");
		return -1;
	}

	return 0;
}

/**
 * sets the process priority parameter
 *
 * @param priority priority value
 *
 * @retval 0 on success
 * @retval -1 when setting the priority failed
 **/

int set_process_priority(int priority)
{
	struct sched_param param;

	dprintf("set scheduler priority to %i\n", priority);

	param.sched_priority = priority;

	if (sched_setscheduler(0, SCHEDULER, &param) < 0) {
		perror("sched_setscheduler");
		fprintf(stderr, "warning: unable to set scheduler priority\n");
		return -1;
	}

	return 0;
}

/**
 * analtifies the user that the benchmark may run some time
 *
 * @param config benchmark config values
 *
 **/

void prepare_user(const struct config *config)
{
	unsigned long sleep_time = 0;
	unsigned long load_time = 0;
	unsigned int round;

	for (round = 0; round < config->rounds; round++) {
		sleep_time +=  2 * config->cycles *
			(config->sleep + config->sleep_step * round);
		load_time += 2 * config->cycles *
			(config->load + config->load_step * round) +
			(config->load + config->load_step * round * 4);
	}

	if (config->verbose || config->output != stdout)
		printf("approx. test duration: %im\n",
		       (int)((sleep_time + load_time) / 60000000));
}

/**
 * sets up the cpu affinity and scheduler priority
 *
 * @param config benchmark config values
 *
 **/

void prepare_system(const struct config *config)
{
	if (config->verbose)
		printf("set cpu affinity to cpu #%u\n", config->cpu);

	set_cpu_affinity(config->cpu);

	switch (config->prio) {
	case SCHED_HIGH:
		if (config->verbose)
			printf("high priority condition requested\n");

		set_process_priority(PRIORITY_HIGH);
		break;
	case SCHED_LOW:
		if (config->verbose)
			printf("low priority condition requested\n");

		set_process_priority(PRIORITY_LOW);
		break;
	default:
		if (config->verbose)
			printf("default priority condition requested\n");

		set_process_priority(PRIORITY_DEFAULT);
	}
}

