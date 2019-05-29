// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (c) 2019 Netronome Systems, Inc. */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>

#include <linux/filter.h>
#include <linux/limits.h>

#include <bpf.h>

#include "main.h"

enum probe_component {
	COMPONENT_UNSPEC,
	COMPONENT_KERNEL,
};

/* Printing utility functions */

static void
print_bool_feature(const char *feat_name, const char *plain_name, bool res)
{
	if (json_output)
		jsonw_bool_field(json_wtr, feat_name, res);
	else
		printf("%s is %savailable\n", plain_name, res ? "" : "NOT ");
}

static void
print_start_section(const char *json_title, const char *plain_title)
{
	if (json_output) {
		jsonw_name(json_wtr, json_title);
		jsonw_start_object(json_wtr);
	} else {
		printf("%s\n", plain_title);
	}
}

/* Probing functions */

static bool probe_bpf_syscall(void)
{
	bool res;

	bpf_load_program(BPF_PROG_TYPE_UNSPEC, NULL, 0, NULL, 0, NULL, 0);
	res = (errno != ENOSYS);

	print_bool_feature("have_bpf_syscall",
			   "bpf() syscall",
			   res);

	return res;
}

static int do_probe(int argc, char **argv)
{
	enum probe_component target = COMPONENT_UNSPEC;

	/* Detection assumes user has sufficient privileges (CAP_SYS_ADMIN).
	 * Let's approximate, and restrict usage to root user only.
	 */
	if (geteuid()) {
		p_err("please run this command as root user");
		return -1;
	}

	set_max_rlimit();

	while (argc) {
		if (is_prefix(*argv, "kernel")) {
			if (target != COMPONENT_UNSPEC) {
				p_err("component to probe already specified");
				return -1;
			}
			target = COMPONENT_KERNEL;
			NEXT_ARG();
		} else {
			p_err("expected no more arguments, 'kernel', got: '%s'?",
			      *argv);
			return -1;
		}
	}

	if (json_output)
		jsonw_start_object(json_wtr);

	print_start_section("syscall_config",
			    "Scanning system call availability...");

	probe_bpf_syscall();

	if (json_output) {
		/* End current "section" of probes */
		jsonw_end_object(json_wtr);
		/* End root object */
		jsonw_end_object(json_wtr);
	}

	return 0;
}

static int do_help(int argc, char **argv)
{
	if (json_output) {
		jsonw_null(json_wtr);
		return 0;
	}

	fprintf(stderr,
		"Usage: %s %s probe [kernel]\n"
		"       %s %s help\n"
		"",
		bin_name, argv[-2], bin_name, argv[-2]);

	return 0;
}

static const struct cmd cmds[] = {
	{ "help",	do_help },
	{ "probe",	do_probe },
	{ 0 }
};

int do_feature(int argc, char **argv)
{
	return cmd_select(cmds, argc, argv, do_help);
}
