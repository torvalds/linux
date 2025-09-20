// SPDX-License-Identifier: GPL-2.0-only
/*
 *  (C) 2004-2009  Dominik Brodowski <linux@dominikbrodowski.de>
 *  (C) 2010       Thomas Renninger <trenn@suse.de>
 */


#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <cpuidle.h>

#include "helpers/sysfs.h"
#include "helpers/helpers.h"
#include "helpers/bitmask.h"

#define LINE_LEN 10

static void cpuidle_cpu_output(unsigned int cpu, int verbose)
{
	unsigned int idlestates, idlestate;
	char *tmp;

	idlestates = cpuidle_state_count(cpu);
	if (idlestates == 0) {
		printf(_("CPU %u: No idle states\n"), cpu);
		return;
	}

	printf(_("Number of idle states: %d\n"), idlestates);
	printf(_("Available idle states:"));
	for (idlestate = 0; idlestate < idlestates; idlestate++) {
		tmp = cpuidle_state_name(cpu, idlestate);
		if (!tmp)
			continue;
		printf(" %s", tmp);
		free(tmp);
	}
	printf("\n");

	if (!verbose)
		return;

	for (idlestate = 0; idlestate < idlestates; idlestate++) {
		int disabled = cpuidle_is_state_disabled(cpu, idlestate);
		/* Disabled interface not supported on older kernels */
		if (disabled < 0)
			disabled = 0;
		tmp = cpuidle_state_name(cpu, idlestate);
		if (!tmp)
			continue;
		printf("%s%s:\n", tmp, (disabled) ? " (DISABLED) " : "");
		free(tmp);

		tmp = cpuidle_state_desc(cpu, idlestate);
		if (!tmp)
			continue;
		printf(_("Flags/Description: %s\n"), tmp);
		free(tmp);

		printf(_("Latency: %lu\n"),
		       cpuidle_state_latency(cpu, idlestate));
		printf(_("Residency: %lu\n"),
		       cpuidle_state_residency(cpu, idlestate));
		printf(_("Usage: %lu\n"),
		       cpuidle_state_usage(cpu, idlestate));
		printf(_("Duration: %llu\n"),
		       cpuidle_state_time(cpu, idlestate));
	}
}

static void cpuidle_general_output(void)
{
	char *tmp;

	tmp = cpuidle_get_driver();
	if (!tmp) {
		printf(_("Could not determine cpuidle driver\n"));
		return;
	}

	printf(_("CPUidle driver: %s\n"), tmp);
	free(tmp);

	tmp = cpuidle_get_governor();
	if (!tmp) {
		printf(_("Could not determine cpuidle governor\n"));
		return;
	}

	printf(_("CPUidle governor: %s\n"), tmp);
	free(tmp);
}

static void proc_cpuidle_cpu_output(unsigned int cpu)
{
	long max_allowed_cstate = 2000000000;
	unsigned int cstate, cstates;

	cstates = cpuidle_state_count(cpu);
	if (cstates == 0) {
		printf(_("CPU %u: No C-states info\n"), cpu);
		return;
	}

	printf(_("active state:            C0\n"));
	printf(_("max_cstate:              C%u\n"), cstates-1);
	printf(_("maximum allowed latency: %lu usec\n"), max_allowed_cstate);
	printf(_("states:\t\n"));
	for (cstate = 1; cstate < cstates; cstate++) {
		printf(_("    C%d:                  "
			 "type[C%d] "), cstate, cstate);
		printf(_("promotion[--] demotion[--] "));
		printf(_("latency[%03lu] "),
		       cpuidle_state_latency(cpu, cstate));
		printf(_("residency[%05lu] "),
		       cpuidle_state_residency(cpu, cstate));
		printf(_("usage[%08lu] "),
		       cpuidle_state_usage(cpu, cstate));
		printf(_("duration[%020Lu] \n"),
		       cpuidle_state_time(cpu, cstate));
	}
}

static struct option info_opts[] = {
	{"silent", no_argument, NULL, 's'},
	{"proc", no_argument, NULL, 'o'},
	{ },
};

static inline void cpuidle_exit(int fail)
{
	exit(EXIT_FAILURE);
}

int cmd_idle_info(int argc, char **argv)
{
	extern char *optarg;
	extern int optind, opterr, optopt;
	int ret = 0, cont = 1, output_param = 0, verbose = 1;
	unsigned int cpu = 0;

	do {
		ret = getopt_long(argc, argv, "os", info_opts, NULL);
		if (ret == -1)
			break;
		switch (ret) {
		case '?':
			output_param = '?';
			cont = 0;
			break;
		case 's':
			verbose = 0;
			break;
		case -1:
			cont = 0;
			break;
		case 'o':
			if (output_param) {
				output_param = -1;
				cont = 0;
				break;
			}
			output_param = ret;
			break;
		}
	} while (cont);

	switch (output_param) {
	case -1:
		printf(_("You can't specify more than one "
			 "output-specific argument\n"));
		cpuidle_exit(EXIT_FAILURE);
	case '?':
		printf(_("invalid or unknown argument\n"));
		cpuidle_exit(EXIT_FAILURE);
	}

	/* Default is: show output of base_cpu only */
	if (bitmask_isallclear(cpus_chosen))
		bitmask_setbit(cpus_chosen, base_cpu);

	if (output_param == 0)
		cpuidle_general_output();

	for (cpu = bitmask_first(cpus_chosen);
	     cpu <= bitmask_last(cpus_chosen); cpu++) {

		if (!bitmask_isbitset(cpus_chosen, cpu))
			continue;

		printf(_("analyzing CPU %d:\n"), cpu);

		if (sysfs_is_cpu_online(cpu) != 1) {
			printf(_(" *is offline\n"));
			printf("\n");
			continue;
		}

		switch (output_param) {

		case 'o':
			proc_cpuidle_cpu_output(cpu);
			break;
		case 0:
			printf("\n");
			cpuidle_cpu_output(cpu, verbose);
			break;
		}
		printf("\n");
	}
	return EXIT_SUCCESS;
}
