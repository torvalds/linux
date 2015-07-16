#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>

#include <getopt.h>

#include "cpufreq.h"
#include "helpers/helpers.h"
#include "helpers/sysfs.h"

static struct option info_opts[] = {
	{ .name = "disable",
	  .has_arg = required_argument,	.flag = NULL,	.val = 'd'},
	{ .name = "enable",
	  .has_arg = required_argument,	.flag = NULL,	.val = 'e'},
	{ .name = "disable-by-latency",
	  .has_arg = required_argument,	.flag = NULL,	.val = 'D'},
	{ .name = "enable-all",
	  .has_arg = no_argument,	.flag = NULL,	.val = 'E'},
	{ },
};


int cmd_idle_set(int argc, char **argv)
{
	extern char *optarg;
	extern int optind, opterr, optopt;
	int ret = 0, cont = 1, param = 0, disabled;
	unsigned long long latency = 0, state_latency;
	unsigned int cpu = 0, idlestate = 0, idlestates = 0;
	char *endptr;

	do {
		ret = getopt_long(argc, argv, "d:e:ED:", info_opts, NULL);
		if (ret == -1)
			break;
		switch (ret) {
		case '?':
			param = '?';
			cont = 0;
			break;
		case 'd':
			if (param) {
				param = -1;
				cont = 0;
				break;
			}
			param = ret;
			idlestate = atoi(optarg);
			break;
		case 'e':
			if (param) {
				param = -1;
				cont = 0;
				break;
			}
			param = ret;
			idlestate = atoi(optarg);
			break;
		case 'D':
			if (param) {
				param = -1;
				cont = 0;
				break;
			}
			param = ret;
			latency = strtoull(optarg, &endptr, 10);
			if (*endptr != '\0') {
				printf(_("Bad latency value: %s\n"), optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'E':
			if (param) {
				param = -1;
				cont = 0;
				break;
			}
			param = ret;
			break;
		case -1:
			cont = 0;
			break;
		}
	} while (cont);

	switch (param) {
	case -1:
		printf(_("You can't specify more than one "
			 "output-specific argument\n"));
		exit(EXIT_FAILURE);
	case '?':
		printf(_("invalid or unknown argument\n"));
		exit(EXIT_FAILURE);
	}

	/* Default is: set all CPUs */
	if (bitmask_isallclear(cpus_chosen))
		bitmask_setall(cpus_chosen);

	for (cpu = bitmask_first(cpus_chosen);
	     cpu <= bitmask_last(cpus_chosen); cpu++) {

		if (!bitmask_isbitset(cpus_chosen, cpu))
			continue;

		if (sysfs_is_cpu_online(cpu) != 1)
			continue;

		idlestates = sysfs_get_idlestate_count(cpu);
		if (idlestates <= 0)
			continue;

		switch (param) {
		case 'd':
			ret = sysfs_idlestate_disable(cpu, idlestate, 1);
			if (ret == 0)
		printf(_("Idlestate %u disabled on CPU %u\n"),  idlestate, cpu);
			else if (ret == -1)
		printf(_("Idlestate %u not available on CPU %u\n"),
		       idlestate, cpu);
			else if (ret == -2)
		printf(_("Idlestate disabling not supported by kernel\n"));
			else
		printf(_("Idlestate %u not disabled on CPU %u\n"),
		       idlestate, cpu);
			break;
		case 'e':
			ret = sysfs_idlestate_disable(cpu, idlestate, 0);
			if (ret == 0)
		printf(_("Idlestate %u enabled on CPU %u\n"),  idlestate, cpu);
			else if (ret == -1)
		printf(_("Idlestate %u not available on CPU %u\n"),
		       idlestate, cpu);
			else if (ret == -2)
		printf(_("Idlestate enabling not supported by kernel\n"));
			else
		printf(_("Idlestate %u not enabled on CPU %u\n"),
		       idlestate, cpu);
			break;
		case 'D':
			for (idlestate = 0; idlestate < idlestates; idlestate++) {
				disabled = sysfs_is_idlestate_disabled
					(cpu, idlestate);
				state_latency = sysfs_get_idlestate_latency
					(cpu, idlestate);
				printf("CPU: %u - idlestate %u - state_latency: %llu - latency: %llu\n",
				       cpu, idlestate, state_latency, latency);
				if (disabled == 1 || latency > state_latency)
					continue;
				ret = sysfs_idlestate_disable
					(cpu, idlestate, 1);
				if (ret == 0)
		printf(_("Idlestate %u disabled on CPU %u\n"), idlestate, cpu);
			}
			break;
		case 'E':
			for (idlestate = 0; idlestate < idlestates; idlestate++) {
				disabled = sysfs_is_idlestate_disabled
					(cpu, idlestate);
				if (disabled == 1) {
					ret = sysfs_idlestate_disable
						(cpu, idlestate, 0);
					if (ret == 0)
		printf(_("Idlestate %u enabled on CPU %u\n"), idlestate, cpu);
				}
			}
			break;
		default:
			/* Not reachable with proper args checking */
			printf(_("Invalid or unknown argument\n"));
			exit(EXIT_FAILURE);
			break;
		}
	}
	return EXIT_SUCCESS;
}
