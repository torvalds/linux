// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (c) 2019 Netronome Systems, Inc. */

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/vfs.h>

#include <linux/filter.h>
#include <linux/limits.h>

#include <bpf.h>
#include <libbpf.h>

#include "main.h"

#ifndef PROC_SUPER_MAGIC
# define PROC_SUPER_MAGIC	0x9fa0
#endif

enum probe_component {
	COMPONENT_UNSPEC,
	COMPONENT_KERNEL,
};

#define BPF_HELPER_MAKE_ENTRY(name)	[BPF_FUNC_ ## name] = "bpf_" # name
static const char * const helper_name[] = {
	__BPF_FUNC_MAPPER(BPF_HELPER_MAKE_ENTRY)
};

#undef BPF_HELPER_MAKE_ENTRY

/* Miscellaneous utility functions */

static bool check_procfs(void)
{
	struct statfs st_fs;

	if (statfs("/proc", &st_fs) < 0)
		return false;
	if ((unsigned long)st_fs.f_type != PROC_SUPER_MAGIC)
		return false;

	return true;
}

/* Printing utility functions */

static void
print_bool_feature(const char *feat_name, const char *plain_name, bool res)
{
	if (json_output)
		jsonw_bool_field(json_wtr, feat_name, res);
	else
		printf("%s is %savailable\n", plain_name, res ? "" : "NOT ");
}

static void print_kernel_option(const char *name, const char *value)
{
	char *endptr;
	int res;

	if (json_output) {
		if (!value) {
			jsonw_null_field(json_wtr, name);
			return;
		}
		errno = 0;
		res = strtol(value, &endptr, 0);
		if (!errno && *endptr == '\n')
			jsonw_int_field(json_wtr, name, res);
		else
			jsonw_string_field(json_wtr, name, value);
	} else {
		if (value)
			printf("%s is set to %s\n", name, value);
		else
			printf("%s is not set\n", name);
	}
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

static void
print_end_then_start_section(const char *json_title, const char *plain_title)
{
	if (json_output)
		jsonw_end_object(json_wtr);
	else
		printf("\n");

	print_start_section(json_title, plain_title);
}

/* Probing functions */

static int read_procfs(const char *path)
{
	char *endptr, *line = NULL;
	size_t len = 0;
	FILE *fd;
	int res;

	fd = fopen(path, "r");
	if (!fd)
		return -1;

	res = getline(&line, &len, fd);
	fclose(fd);
	if (res < 0)
		return -1;

	errno = 0;
	res = strtol(line, &endptr, 10);
	if (errno || *line == '\0' || *endptr != '\n')
		res = -1;
	free(line);

	return res;
}

static void probe_unprivileged_disabled(void)
{
	int res;

	res = read_procfs("/proc/sys/kernel/unprivileged_bpf_disabled");
	if (json_output) {
		jsonw_int_field(json_wtr, "unprivileged_bpf_disabled", res);
	} else {
		switch (res) {
		case 0:
			printf("bpf() syscall for unprivileged users is enabled\n");
			break;
		case 1:
			printf("bpf() syscall restricted to privileged users\n");
			break;
		case -1:
			printf("Unable to retrieve required privileges for bpf() syscall\n");
			break;
		default:
			printf("bpf() syscall restriction has unknown value %d\n", res);
		}
	}
}

static void probe_jit_enable(void)
{
	int res;

	res = read_procfs("/proc/sys/net/core/bpf_jit_enable");
	if (json_output) {
		jsonw_int_field(json_wtr, "bpf_jit_enable", res);
	} else {
		switch (res) {
		case 0:
			printf("JIT compiler is disabled\n");
			break;
		case 1:
			printf("JIT compiler is enabled\n");
			break;
		case 2:
			printf("JIT compiler is enabled with debugging traces in kernel logs\n");
			break;
		case -1:
			printf("Unable to retrieve JIT-compiler status\n");
			break;
		default:
			printf("JIT-compiler status has unknown value %d\n",
			       res);
		}
	}
}

static void probe_jit_harden(void)
{
	int res;

	res = read_procfs("/proc/sys/net/core/bpf_jit_harden");
	if (json_output) {
		jsonw_int_field(json_wtr, "bpf_jit_harden", res);
	} else {
		switch (res) {
		case 0:
			printf("JIT compiler hardening is disabled\n");
			break;
		case 1:
			printf("JIT compiler hardening is enabled for unprivileged users\n");
			break;
		case 2:
			printf("JIT compiler hardening is enabled for all users\n");
			break;
		case -1:
			printf("Unable to retrieve JIT hardening status\n");
			break;
		default:
			printf("JIT hardening status has unknown value %d\n",
			       res);
		}
	}
}

static void probe_jit_kallsyms(void)
{
	int res;

	res = read_procfs("/proc/sys/net/core/bpf_jit_kallsyms");
	if (json_output) {
		jsonw_int_field(json_wtr, "bpf_jit_kallsyms", res);
	} else {
		switch (res) {
		case 0:
			printf("JIT compiler kallsyms exports are disabled\n");
			break;
		case 1:
			printf("JIT compiler kallsyms exports are enabled for root\n");
			break;
		case -1:
			printf("Unable to retrieve JIT kallsyms export status\n");
			break;
		default:
			printf("JIT kallsyms exports status has unknown value %d\n", res);
		}
	}
}

static void probe_jit_limit(void)
{
	int res;

	res = read_procfs("/proc/sys/net/core/bpf_jit_limit");
	if (json_output) {
		jsonw_int_field(json_wtr, "bpf_jit_limit", res);
	} else {
		switch (res) {
		case -1:
			printf("Unable to retrieve global memory limit for JIT compiler for unprivileged users\n");
			break;
		default:
			printf("Global memory limit for JIT compiler for unprivileged users is %d bytes\n", res);
		}
	}
}

static char *get_kernel_config_option(FILE *fd, const char *option)
{
	size_t line_n = 0, optlen = strlen(option);
	char *res, *strval, *line = NULL;
	ssize_t n;

	rewind(fd);
	while ((n = getline(&line, &line_n, fd)) > 0) {
		if (strncmp(line, option, optlen))
			continue;
		/* Check we have at least '=', value, and '\n' */
		if (strlen(line) < optlen + 3)
			continue;
		if (*(line + optlen) != '=')
			continue;

		/* Trim ending '\n' */
		line[strlen(line) - 1] = '\0';

		/* Copy and return config option value */
		strval = line + optlen + 1;
		res = strdup(strval);
		free(line);
		return res;
	}
	free(line);

	return NULL;
}

static void probe_kernel_image_config(void)
{
	static const char * const options[] = {
		/* Enable BPF */
		"CONFIG_BPF",
		/* Enable bpf() syscall */
		"CONFIG_BPF_SYSCALL",
		/* Does selected architecture support eBPF JIT compiler */
		"CONFIG_HAVE_EBPF_JIT",
		/* Compile eBPF JIT compiler */
		"CONFIG_BPF_JIT",
		/* Avoid compiling eBPF interpreter (use JIT only) */
		"CONFIG_BPF_JIT_ALWAYS_ON",

		/* cgroups */
		"CONFIG_CGROUPS",
		/* BPF programs attached to cgroups */
		"CONFIG_CGROUP_BPF",
		/* bpf_get_cgroup_classid() helper */
		"CONFIG_CGROUP_NET_CLASSID",
		/* bpf_skb_{,ancestor_}cgroup_id() helpers */
		"CONFIG_SOCK_CGROUP_DATA",

		/* Tracing: attach BPF to kprobes, tracepoints, etc. */
		"CONFIG_BPF_EVENTS",
		/* Kprobes */
		"CONFIG_KPROBE_EVENTS",
		/* Uprobes */
		"CONFIG_UPROBE_EVENTS",
		/* Tracepoints */
		"CONFIG_TRACING",
		/* Syscall tracepoints */
		"CONFIG_FTRACE_SYSCALLS",
		/* bpf_override_return() helper support for selected arch */
		"CONFIG_FUNCTION_ERROR_INJECTION",
		/* bpf_override_return() helper */
		"CONFIG_BPF_KPROBE_OVERRIDE",

		/* Network */
		"CONFIG_NET",
		/* AF_XDP sockets */
		"CONFIG_XDP_SOCKETS",
		/* BPF_PROG_TYPE_LWT_* and related helpers */
		"CONFIG_LWTUNNEL_BPF",
		/* BPF_PROG_TYPE_SCHED_ACT, TC (traffic control) actions */
		"CONFIG_NET_ACT_BPF",
		/* BPF_PROG_TYPE_SCHED_CLS, TC filters */
		"CONFIG_NET_CLS_BPF",
		/* TC clsact qdisc */
		"CONFIG_NET_CLS_ACT",
		/* Ingress filtering with TC */
		"CONFIG_NET_SCH_INGRESS",
		/* bpf_skb_get_xfrm_state() helper */
		"CONFIG_XFRM",
		/* bpf_get_route_realm() helper */
		"CONFIG_IP_ROUTE_CLASSID",
		/* BPF_PROG_TYPE_LWT_SEG6_LOCAL and related helpers */
		"CONFIG_IPV6_SEG6_BPF",
		/* BPF_PROG_TYPE_LIRC_MODE2 and related helpers */
		"CONFIG_BPF_LIRC_MODE2",
		/* BPF stream parser and BPF socket maps */
		"CONFIG_BPF_STREAM_PARSER",
		/* xt_bpf module for passing BPF programs to netfilter  */
		"CONFIG_NETFILTER_XT_MATCH_BPF",
		/* bpfilter back-end for iptables */
		"CONFIG_BPFILTER",
		/* bpftilter module with "user mode helper" */
		"CONFIG_BPFILTER_UMH",

		/* test_bpf module for BPF tests */
		"CONFIG_TEST_BPF",
	};
	char *value, *buf = NULL;
	struct utsname utsn;
	char path[PATH_MAX];
	size_t i, n;
	ssize_t ret;
	FILE *fd;

	if (uname(&utsn))
		goto no_config;

	snprintf(path, sizeof(path), "/boot/config-%s", utsn.release);

	fd = fopen(path, "r");
	if (!fd && errno == ENOENT) {
		/* Some distributions put the config file at /proc/config, give
		 * it a try.
		 * Sometimes it is also at /proc/config.gz but we do not try
		 * this one for now, it would require linking against libz.
		 */
		fd = fopen("/proc/config", "r");
	}
	if (!fd) {
		p_info("skipping kernel config, can't open file: %s",
		       strerror(errno));
		goto no_config;
	}
	/* Sanity checks */
	ret = getline(&buf, &n, fd);
	ret = getline(&buf, &n, fd);
	if (!buf || !ret) {
		p_info("skipping kernel config, can't read from file: %s",
		       strerror(errno));
		free(buf);
		goto no_config;
	}
	if (strcmp(buf, "# Automatically generated file; DO NOT EDIT.\n")) {
		p_info("skipping kernel config, can't find correct file");
		free(buf);
		goto no_config;
	}
	free(buf);

	for (i = 0; i < ARRAY_SIZE(options); i++) {
		value = get_kernel_config_option(fd, options[i]);
		print_kernel_option(options[i], value);
		free(value);
	}
	fclose(fd);
	return;

no_config:
	for (i = 0; i < ARRAY_SIZE(options); i++)
		print_kernel_option(options[i], NULL);
}

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

static void probe_prog_type(enum bpf_prog_type prog_type, bool *supported_types)
{
	const char *plain_comment = "eBPF program_type ";
	char feat_name[128], plain_desc[128];
	size_t maxlen;
	bool res;

	res = bpf_probe_prog_type(prog_type, 0);

	supported_types[prog_type] |= res;

	maxlen = sizeof(plain_desc) - strlen(plain_comment) - 1;
	if (strlen(prog_type_name[prog_type]) > maxlen) {
		p_info("program type name too long");
		return;
	}

	sprintf(feat_name, "have_%s_prog_type", prog_type_name[prog_type]);
	sprintf(plain_desc, "%s%s", plain_comment, prog_type_name[prog_type]);
	print_bool_feature(feat_name, plain_desc, res);
}

static void probe_map_type(enum bpf_map_type map_type)
{
	const char *plain_comment = "eBPF map_type ";
	char feat_name[128], plain_desc[128];
	size_t maxlen;
	bool res;

	res = bpf_probe_map_type(map_type, 0);

	maxlen = sizeof(plain_desc) - strlen(plain_comment) - 1;
	if (strlen(map_type_name[map_type]) > maxlen) {
		p_info("map type name too long");
		return;
	}

	sprintf(feat_name, "have_%s_map_type", map_type_name[map_type]);
	sprintf(plain_desc, "%s%s", plain_comment, map_type_name[map_type]);
	print_bool_feature(feat_name, plain_desc, res);
}

static void
probe_helpers_for_progtype(enum bpf_prog_type prog_type, bool supported_type)
{
	const char *ptype_name = prog_type_name[prog_type];
	char feat_name[128];
	unsigned int id;
	bool res;

	if (json_output) {
		sprintf(feat_name, "%s_available_helpers", ptype_name);
		jsonw_name(json_wtr, feat_name);
		jsonw_start_array(json_wtr);
	} else {
		printf("eBPF helpers supported for program type %s:",
		       ptype_name);
	}

	for (id = 1; id < ARRAY_SIZE(helper_name); id++) {
		if (!supported_type)
			res = false;
		else
			res = bpf_probe_helper(id, prog_type, 0);

		if (json_output) {
			if (res)
				jsonw_string(json_wtr, helper_name[id]);
		} else {
			if (res)
				printf("\n\t- %s", helper_name[id]);
		}
	}

	if (json_output)
		jsonw_end_array(json_wtr);
	else
		printf("\n");
}

static int do_probe(int argc, char **argv)
{
	enum probe_component target = COMPONENT_UNSPEC;
	bool supported_types[128] = {};
	unsigned int i;

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

	switch (target) {
	case COMPONENT_KERNEL:
	case COMPONENT_UNSPEC:
		print_start_section("system_config",
				    "Scanning system configuration...");
		if (check_procfs()) {
			probe_unprivileged_disabled();
			probe_jit_enable();
			probe_jit_harden();
			probe_jit_kallsyms();
			probe_jit_limit();
		} else {
			p_info("/* procfs not mounted, skipping related probes */");
		}
		probe_kernel_image_config();
		if (json_output)
			jsonw_end_object(json_wtr);
		else
			printf("\n");
		break;
	}

	print_start_section("syscall_config",
			    "Scanning system call availability...");

	if (!probe_bpf_syscall())
		/* bpf() syscall unavailable, don't probe other BPF features */
		goto exit_close_json;

	print_end_then_start_section("program_types",
				     "Scanning eBPF program types...");

	for (i = BPF_PROG_TYPE_UNSPEC + 1; i < ARRAY_SIZE(prog_type_name); i++)
		probe_prog_type(i, supported_types);

	print_end_then_start_section("map_types",
				     "Scanning eBPF map types...");

	for (i = BPF_MAP_TYPE_UNSPEC + 1; i < map_type_name_size; i++)
		probe_map_type(i);

	print_end_then_start_section("helpers",
				     "Scanning eBPF helper functions...");

	for (i = BPF_PROG_TYPE_UNSPEC + 1; i < ARRAY_SIZE(prog_type_name); i++)
		probe_helpers_for_progtype(i, supported_types[i]);

exit_close_json:
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
