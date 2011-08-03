/*
 *  (C) 2011 Thomas Renninger <trenn@suse.de>, Novell Inc.
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 */


#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>

#include <cpufreq.h>
#include "helpers/helpers.h"
#include "helpers/sysfs.h"

void info_help(void)
{
	printf(_("Usage: cpupower info [ -b ] [ -m ] [ -s ]\n"));
	printf(_("Options:\n"));
	printf(_("  -b, --perf-bias    Gets CPU's power vs performance policy on some\n"
	       "                           Intel models [0-15], see manpage for details\n"));
	printf(_("  -m, --sched-mc     Gets the kernel's multi core scheduler policy.\n"));
	printf(_("  -s, --sched-smt    Gets the kernel's thread sibling scheduler policy.\n"));
	printf(_("  -h, --help               Prints out this screen\n"));
	printf(_("\nPassing no option will show all info, by default only on core 0\n"));
	printf("\n");
}

static struct option set_opts[] = {
	{ .name = "perf-bias",	.has_arg = optional_argument,	.flag = NULL,	.val = 'b'},
	{ .name = "sched-mc",	.has_arg = optional_argument,	.flag = NULL,	.val = 'm'},
	{ .name = "sched-smt",	.has_arg = optional_argument,	.flag = NULL,	.val = 's'},
	{ .name = "help",	.has_arg = no_argument,		.flag = NULL,	.val = 'h'},
	{ },
};

static void print_wrong_arg_exit(void)
{
	printf(_("invalid or unknown argument\n"));
	info_help();
	exit(EXIT_FAILURE);
}

int cmd_info(int argc, char **argv)
{
	extern char *optarg;
	extern int optind, opterr, optopt;
	unsigned int cpu;

	union {
		struct {
			int sched_mc:1;
			int sched_smt:1;
			int perf_bias:1;
		};
		int params;
	} params = {};
	int ret = 0;

	setlocale(LC_ALL, "");
	textdomain(PACKAGE);

	/* parameter parsing */
	while ((ret = getopt_long(argc, argv, "msbh", set_opts, NULL)) != -1) {
		switch (ret) {
		case 'h':
			info_help();
			return 0;
		case 'b':
			if (params.perf_bias)
				print_wrong_arg_exit();
			params.perf_bias = 1;
			break;
		case 'm':
			if (params.sched_mc)
				print_wrong_arg_exit();
			params.sched_mc = 1;
			break;
		case 's':
			if (params.sched_smt)
				print_wrong_arg_exit();
			params.sched_smt = 1;
			break;
		default:
			print_wrong_arg_exit();
		}
	};

	if (!params.params)
		params.params = 0x7;

	/* Default is: show output of CPU 0 only */
	if (bitmask_isallclear(cpus_chosen))
		bitmask_setbit(cpus_chosen, 0);

	if (params.sched_mc) {
		ret = sysfs_get_sched("mc");
		printf(_("System's multi core scheduler setting: "));
		if (ret < 0)
			/* if sysfs file is missing it's: errno == ENOENT */
			printf(_("not supported\n"));
		else
			printf("%d\n", ret);
	}
	if (params.sched_smt) {
		ret = sysfs_get_sched("smt");
		printf(_("System's thread sibling scheduler setting: "));
		if (ret < 0)
			/* if sysfs file is missing it's: errno == ENOENT */
			printf(_("not supported\n"));
		else
			printf("%d\n", ret);
	}

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

		if (!bitmask_isbitset(cpus_chosen, cpu) ||
		    cpufreq_cpu_exists(cpu))
			continue;

		printf(_("analyzing CPU %d:\n"), cpu);

		if (params.perf_bias) {
			ret = msr_intel_get_perf_bias(cpu);
			if (ret < 0) {
				printf(_("Could not read perf-bias value\n"));
				break;
			} else
				printf(_("perf-bias: %d\n"), ret);
		}
	}
	return ret;
}
