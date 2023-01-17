// SPDX-License-Identifier: GPL-2.0-only
/*
 *  (C) 2011 Thomas Renninger <trenn@suse.de>, Novell Inc.
 */


#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <sys/utsname.h>

#include "helpers/helpers.h"
#include "helpers/sysfs.h"

static struct option set_opts[] = {
     {"perf-bias", optional_argument, NULL, 'b'},
     { },
};

static void print_wrong_arg_exit(void)
{
	printf(_("invalid or unknown argument\n"));
	exit(EXIT_FAILURE);
}

int cmd_info(int argc, char **argv)
{
	extern char *optarg;
	extern int optind, opterr, optopt;
	unsigned int cpu;
	struct utsname uts;

	union {
		struct {
			int perf_bias:1;
		};
		int params;
	} params = {};
	int ret = 0;

	ret = uname(&uts);
	if (!ret && (!strcmp(uts.machine, "ppc64le") ||
		     !strcmp(uts.machine, "ppc64"))) {
		fprintf(stderr, _("Subcommand not supported on POWER.\n"));
		return ret;
	}

	setlocale(LC_ALL, "");
	textdomain(PACKAGE);

	/* parameter parsing */
	while ((ret = getopt_long(argc, argv, "b", set_opts, NULL)) != -1) {
		switch (ret) {
		case 'b':
			if (params.perf_bias)
				print_wrong_arg_exit();
			params.perf_bias = 1;
			break;
		default:
			print_wrong_arg_exit();
		}
	}

	if (!params.params)
		params.params = 0x7;

	/* Default is: show output of base_cpu only */
	if (bitmask_isallclear(cpus_chosen))
		bitmask_setbit(cpus_chosen, base_cpu);

	/* Add more per cpu options here */
	if (!params.perf_bias)
		return ret;

	if (params.perf_bias) {
		if (!run_as_root) {
			params.perf_bias = 0;
			printf(_("Intel's performance bias setting needs root privileges\n"));
		} else if (!(cpupower_cpu_info.caps & CPUPOWER_CAP_PERF_BIAS)) {
			printf(_("System does not support Intel's performance"
				 " bias setting\n"));
			params.perf_bias = 0;
		}
	}

	/* loop over CPUs */
	for (cpu = bitmask_first(cpus_chosen);
	     cpu <= bitmask_last(cpus_chosen); cpu++) {

		if (!bitmask_isbitset(cpus_chosen, cpu))
			continue;

		printf(_("analyzing CPU %d:\n"), cpu);

		if (sysfs_is_cpu_online(cpu) != 1){
			printf(_(" *is offline\n"));
			continue;
		}

		if (params.perf_bias) {
			ret = cpupower_intel_get_perf_bias(cpu);
			if (ret < 0) {
				fprintf(stderr,
			_("Could not read perf-bias value[%d]\n"), ret);
				exit(EXIT_FAILURE);
			} else
				printf(_("perf-bias: %d\n"), ret);
		}
	}
	return 0;
}
