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
	{ .name = "disable",	.has_arg = required_argument,	.flag = NULL,	.val = 'd'},
	{ .name = "enable",	.has_arg = required_argument,	.flag = NULL,	.val = 'e'},
	{ },
};


int cmd_idle_set(int argc, char **argv)
{
	extern char *optarg;
	extern int optind, opterr, optopt;
	int ret = 0, cont = 1, param = 0, idlestate = 0;
	unsigned int cpu = 0;

	do {
		ret = getopt_long(argc, argv, "d:e:", info_opts, NULL);
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
		default:
			/* Not reachable with proper args checking */
			printf(_("Invalid or unknown argument\n"));
			exit(EXIT_FAILURE);
			break;
		}
	}
	return EXIT_SUCCESS;
}
