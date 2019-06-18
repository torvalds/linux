// SPDX-License-Identifier: GPL-2.0-only
/*
 *  (C) 2010,2011       Thomas Renninger <trenn@suse.de>, Novell Inc.
 *
 *  Ideas taken over from the perf userspace tool (included in the Linus
 *  kernel git repo): subcommand builtins and param parsing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include "builtin.h"
#include "helpers/helpers.h"
#include "helpers/bitmask.h"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

static int cmd_help(int argc, const char **argv);

/* Global cpu_info object available for all binaries
 * Info only retrieved from CPU 0
 *
 * Values will be zero/unknown on non X86 archs
 */
struct cpupower_cpu_info cpupower_cpu_info;
int run_as_root;
int base_cpu;
/* Affected cpus chosen by -c/--cpu param */
struct bitmask *cpus_chosen;

#ifdef DEBUG
int be_verbose;
#endif

static void print_help(void);

struct cmd_struct {
	const char *cmd;
	int (*main)(int, const char **);
	int needs_root;
};

static struct cmd_struct commands[] = {
	{ "frequency-info",	cmd_freq_info,	0	},
	{ "frequency-set",	cmd_freq_set,	1	},
	{ "idle-info",		cmd_idle_info,	0	},
	{ "idle-set",		cmd_idle_set,	1	},
	{ "set",		cmd_set,	1	},
	{ "info",		cmd_info,	0	},
	{ "monitor",		cmd_monitor,	0	},
	{ "help",		cmd_help,	0	},
	/*	{ "bench",	cmd_bench,	1	}, */
};

static void print_help(void)
{
	unsigned int i;

#ifdef DEBUG
	printf(_("Usage:\tcpupower [-d|--debug] [-c|--cpu cpulist ] <command> [<args>]\n"));
#else
	printf(_("Usage:\tcpupower [-c|--cpu cpulist ] <command> [<args>]\n"));
#endif
	printf(_("Supported commands are:\n"));
	for (i = 0; i < ARRAY_SIZE(commands); i++)
		printf("\t%s\n", commands[i].cmd);
	printf(_("\nNot all commands can make use of the -c cpulist option.\n"));
	printf(_("\nUse 'cpupower help <command>' for getting help for above commands.\n"));
}

static int print_man_page(const char *subpage)
{
	int len;
	char *page;

	len = 10; /* enough for "cpupower-" */
	if (subpage != NULL)
		len += strlen(subpage);

	page = malloc(len);
	if (!page)
		return -ENOMEM;

	sprintf(page, "cpupower");
	if ((subpage != NULL) && strcmp(subpage, "help")) {
		strcat(page, "-");
		strcat(page, subpage);
	}

	execlp("man", "man", page, NULL);

	/* should not be reached */
	return -EINVAL;
}

static int cmd_help(int argc, const char **argv)
{
	if (argc > 1) {
		print_man_page(argv[1]); /* exits within execlp() */
		return EXIT_FAILURE;
	}

	print_help();
	return EXIT_SUCCESS;
}

static void print_version(void)
{
	printf(PACKAGE " " VERSION "\n");
	printf(_("Report errors and bugs to %s, please.\n"), PACKAGE_BUGREPORT);
}

static void handle_options(int *argc, const char ***argv)
{
	int ret, x, new_argc = 0;

	if (*argc < 1)
		return;

	for (x = 0;  x < *argc && ((*argv)[x])[0] == '-'; x++) {
		const char *param = (*argv)[x];
		if (!strcmp(param, "-h") || !strcmp(param, "--help")) {
			print_help();
			exit(EXIT_SUCCESS);
		} else if (!strcmp(param, "-c") || !strcmp(param, "--cpu")) {
			if (*argc < 2) {
				print_help();
				exit(EXIT_FAILURE);
			}
			if (!strcmp((*argv)[x+1], "all"))
				bitmask_setall(cpus_chosen);
			else {
				ret = bitmask_parselist(
						(*argv)[x+1], cpus_chosen);
				if (ret < 0) {
					fprintf(stderr, _("Error parsing cpu "
							  "list\n"));
					exit(EXIT_FAILURE);
				}
			}
			x += 1;
			/* Cut out param: cpupower -c 1 info -> cpupower info */
			new_argc += 2;
			continue;
		} else if (!strcmp(param, "-v") ||
			!strcmp(param, "--version")) {
			print_version();
			exit(EXIT_SUCCESS);
#ifdef DEBUG
		} else if (!strcmp(param, "-d") || !strcmp(param, "--debug")) {
			be_verbose = 1;
			new_argc++;
			continue;
#endif
		} else {
			fprintf(stderr, "Unknown option: %s\n", param);
			print_help();
			exit(EXIT_FAILURE);
		}
	}
	*argc -= new_argc;
	*argv += new_argc;
}

int main(int argc, const char *argv[])
{
	const char *cmd;
	unsigned int i, ret;
	struct stat statbuf;
	struct utsname uts;
	char pathname[32];

	cpus_chosen = bitmask_alloc(sysconf(_SC_NPROCESSORS_CONF));

	argc--;
	argv += 1;

	handle_options(&argc, &argv);

	cmd = argv[0];

	if (argc < 1) {
		print_help();
		return EXIT_FAILURE;
	}

	setlocale(LC_ALL, "");
	textdomain(PACKAGE);

	/* Turn "perf cmd --help" into "perf help cmd" */
	if (argc > 1 && !strcmp(argv[1], "--help")) {
		argv[1] = argv[0];
		argv[0] = cmd = "help";
	}

	base_cpu = sched_getcpu();
	if (base_cpu < 0) {
		fprintf(stderr, _("No valid cpus found.\n"));
		return EXIT_FAILURE;
	}

	get_cpu_info(&cpupower_cpu_info);
	run_as_root = !geteuid();
	if (run_as_root) {
		ret = uname(&uts);
		sprintf(pathname, "/dev/cpu/%d/msr", base_cpu);
		if (!ret && !strcmp(uts.machine, "x86_64") &&
		    stat(pathname, &statbuf) != 0) {
			if (system("modprobe msr") == -1)
	fprintf(stderr, _("MSR access not available.\n"));
		}
	}

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		struct cmd_struct *p = commands + i;
		if (strcmp(p->cmd, cmd))
			continue;
		if (!run_as_root && p->needs_root) {
			fprintf(stderr, _("Subcommand %s needs root "
					  "privileges\n"), cmd);
			return EXIT_FAILURE;
		}
		ret = p->main(argc, argv);
		if (cpus_chosen)
			bitmask_free(cpus_chosen);
		return ret;
	}
	print_help();
	return EXIT_FAILURE;
}
