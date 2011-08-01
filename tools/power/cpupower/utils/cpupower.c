/*
 *  (C) 2010,2011       Thomas Renninger <trenn@suse.de>, Novell Inc.
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *
 *  Ideas taken over from the perf userspace tool (included in the Linus
 *  kernel git repo): subcommand builtins and param parsing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "builtin.h"
#include "helpers/helpers.h"
#include "helpers/bitmask.h"

struct cmd_struct {
	const char *cmd;
	int (*main)(int, const char **);
	void (*usage)(void);
	int needs_root;
};

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

int cmd_help(int argc, const char **argv);

/* Global cpu_info object available for all binaries
 * Info only retrieved from CPU 0
 *
 * Values will be zero/unknown on non X86 archs
 */
struct cpupower_cpu_info cpupower_cpu_info;
int run_as_root;
/* Affected cpus chosen by -c/--cpu param */
struct bitmask *cpus_chosen;

#ifdef DEBUG
int be_verbose;
#endif

static void print_help(void);

static struct cmd_struct commands[] = {
	{ "frequency-info",	cmd_freq_info,	freq_info_help,	0	},
	{ "frequency-set",	cmd_freq_set,	freq_set_help,	1	},
	{ "idle-info",		cmd_idle_info,	idle_info_help,	0	},
	{ "set",		cmd_set,	set_help,	1	},
	{ "info",		cmd_info,	info_help,	0	},
	{ "monitor",		cmd_monitor,	monitor_help,	0	},
	{ "help",		cmd_help,	print_help,	0	},
	/*	{ "bench",	cmd_bench,	NULL,		1	}, */
};

int cmd_help(int argc, const char **argv)
{
	unsigned int i;

	if (argc > 1) {
		for (i = 0; i < ARRAY_SIZE(commands); i++) {
			struct cmd_struct *p = commands + i;
			if (strcmp(p->cmd, argv[1]))
				continue;
			if (p->usage) {
				p->usage();
				return EXIT_SUCCESS;
			}
		}
	}
	print_help();
	if (argc == 1)
		return EXIT_SUCCESS; /* cpupower help */
	return EXIT_FAILURE;
}

static void print_help(void)
{
	unsigned int i;

#ifdef DEBUG
	printf(_("cpupower [ -d ][ -c cpulist ] subcommand [ARGS]\n"));
	printf(_("  -d, --debug      May increase output (stderr) on some subcommands\n"));
#else
	printf(_("cpupower [ -c cpulist ] subcommand [ARGS]\n"));
#endif
	printf(_("cpupower --version\n"));
	printf(_("Supported subcommands are:\n"));
	for (i = 0; i < ARRAY_SIZE(commands); i++)
		printf("\t%s\n", commands[i].cmd);
	printf(_("\nSome subcommands can make use of the -c cpulist option.\n"));
	printf(_("Look at the general cpupower manpage how to use it\n"));
	printf(_("and read up the subcommand's manpage whether it is supported.\n"));
	printf(_("\nUse cpupower help subcommand for getting help for above subcommands.\n"));
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

	get_cpu_info(0, &cpupower_cpu_info);
	run_as_root = !getuid();

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
