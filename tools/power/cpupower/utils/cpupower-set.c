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
#include "helpers/bitmask.h"

void set_help(void)
{
	printf(_("Usage: cpupower set [ -b val ] [ -m val ] [ -s val ]\n"));
	printf(_("Options:\n"));
	printf(_("  -b, --perf-bias [VAL]    Sets CPU's power vs performance policy on some\n"
	       "                           Intel models [0-15], see manpage for details\n"));
	printf(_("  -m, --sched-mc  [VAL]    Sets the kernel's multi core scheduler policy.\n"));
	printf(_("  -s, --sched-smt [VAL]    Sets the kernel's thread sibling scheduler policy.\n"));
	printf(_("  -h, --help               Prints out this screen\n"));
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
	set_help();
	exit(EXIT_FAILURE);
}

int cmd_set(int argc, char **argv)
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
	} params;
	int sched_mc = 0, sched_smt = 0, perf_bias = 0;
	int ret = 0;

	setlocale(LC_ALL, "");
	textdomain(PACKAGE);

	params.params = 0;
	/* parameter parsing */
	while ((ret = getopt_long(argc, argv, "m:s:b:h",
						set_opts, NULL)) != -1) {
		switch (ret) {
		case 'h':
			set_help();
			return 0;
		case 'b':
			if (params.perf_bias)
				print_wrong_arg_exit();
			perf_bias = atoi(optarg);
			if (perf_bias < 0 || perf_bias > 15) {
				printf(_("--perf-bias param out "
					 "of range [0-%d]\n"), 15);
				print_wrong_arg_exit();
			}
			params.perf_bias = 1;
			break;
		case 'm':
			if (params.sched_mc)
				print_wrong_arg_exit();
			sched_mc = atoi(optarg);
			if (sched_mc < 0 || sched_mc > 2) {
				printf(_("--sched-mc param out "
					 "of range [0-%d]\n"), 2);
				print_wrong_arg_exit();
			}
			params.sched_mc = 1;
			break;
		case 's':
			if (params.sched_smt)
				print_wrong_arg_exit();
			sched_smt = atoi(optarg);
			if (sched_smt < 0 || sched_smt > 2) {
				printf(_("--sched-smt param out "
					 "of range [0-%d]\n"), 2);
				print_wrong_arg_exit();
			}
			params.sched_smt = 1;
			break;
		default:
			print_wrong_arg_exit();
		}
	};

	if (!params.params) {
		set_help();
		return -EINVAL;
	}

	if (params.sched_mc) {
		ret = sysfs_set_sched("mc", sched_mc);
		if (ret)
			fprintf(stderr, _("Error setting sched-mc %s\n"),
				(ret == -ENODEV) ? "not supported" : "");
	}
	if (params.sched_smt) {
		ret = sysfs_set_sched("smt", sched_smt);
		if (ret)
			fprintf(stderr, _("Error setting sched-smt %s\n"),
				(ret == -ENODEV) ? "not supported" : "");
	}

	/* Default is: set all CPUs */
	if (bitmask_isallclear(cpus_chosen))
		bitmask_setall(cpus_chosen);

	/* loop over CPUs */
	for (cpu = bitmask_first(cpus_chosen);
	     cpu <= bitmask_last(cpus_chosen); cpu++) {

		if (!bitmask_isbitset(cpus_chosen, cpu) ||
		    cpufreq_cpu_exists(cpu))
			continue;

		if (params.perf_bias) {
			ret = msr_intel_set_perf_bias(cpu, perf_bias);
			if (ret) {
				fprintf(stderr, _("Error setting perf-bias "
						  "value on CPU %d\n"), cpu);
				break;
			}
		}
	}
	return ret;
}
