// SPDX-License-Identifier: GPL-2.0
/*
 * perf.c
 *
 * Performance analysis utility.
 *
 * This is the main hub from which the sub-commands (perf stat,
 * perf top, perf record, perf report, etc.) are started.
 */
#include "builtin.h"

#include "util/env.h"
#include <subcmd/exec-cmd.h>
#include "util/config.h"
#include "util/quote.h"
#include <subcmd/run-command.h>
#include "util/parse-events.h"
#include <subcmd/parse-options.h>
#include "util/bpf-loader.h"
#include "util/debug.h"
#include "util/event.h"
#include <api/fs/fs.h>
#include <api/fs/tracing_path.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/kernel.h>

const char perf_usage_string[] =
	"perf [--version] [--help] [OPTIONS] COMMAND [ARGS]";

const char perf_more_info_string[] =
	"See 'perf help COMMAND' for more information on a specific command.";

static int use_pager = -1;
const char *input_name;

struct cmd_struct {
	const char *cmd;
	int (*fn)(int, const char **);
	int option;
};

static struct cmd_struct commands[] = {
	{ "buildid-cache", cmd_buildid_cache, 0 },
	{ "buildid-list", cmd_buildid_list, 0 },
	{ "config",	cmd_config,	0 },
	{ "c2c",	cmd_c2c,	0 },
	{ "diff",	cmd_diff,	0 },
	{ "evlist",	cmd_evlist,	0 },
	{ "help",	cmd_help,	0 },
	{ "kallsyms",	cmd_kallsyms,	0 },
	{ "list",	cmd_list,	0 },
	{ "record",	cmd_record,	0 },
	{ "report",	cmd_report,	0 },
	{ "bench",	cmd_bench,	0 },
	{ "stat",	cmd_stat,	0 },
	{ "timechart",	cmd_timechart,	0 },
	{ "top",	cmd_top,	0 },
	{ "annotate",	cmd_annotate,	0 },
	{ "version",	cmd_version,	0 },
	{ "script",	cmd_script,	0 },
	{ "sched",	cmd_sched,	0 },
#ifdef HAVE_LIBELF_SUPPORT
	{ "probe",	cmd_probe,	0 },
#endif
	{ "kmem",	cmd_kmem,	0 },
	{ "lock",	cmd_lock,	0 },
	{ "kvm",	cmd_kvm,	0 },
	{ "test",	cmd_test,	0 },
#if defined(HAVE_LIBAUDIT_SUPPORT) || defined(HAVE_SYSCALL_TABLE)
	{ "trace",	cmd_trace,	0 },
#endif
	{ "inject",	cmd_inject,	0 },
	{ "mem",	cmd_mem,	0 },
	{ "data",	cmd_data,	0 },
	{ "ftrace",	cmd_ftrace,	0 },
};

struct pager_config {
	const char *cmd;
	int val;
};

static int pager_command_config(const char *var, const char *value, void *data)
{
	struct pager_config *c = data;
	if (strstarts(var, "pager.") && !strcmp(var + 6, c->cmd))
		c->val = perf_config_bool(var, value);
	return 0;
}

/* returns 0 for "no pager", 1 for "use pager", and -1 for "not specified" */
static int check_pager_config(const char *cmd)
{
	int err;
	struct pager_config c;
	c.cmd = cmd;
	c.val = -1;
	err = perf_config(pager_command_config, &c);
	return err ?: c.val;
}

static int browser_command_config(const char *var, const char *value, void *data)
{
	struct pager_config *c = data;
	if (strstarts(var, "tui.") && !strcmp(var + 4, c->cmd))
		c->val = perf_config_bool(var, value);
	if (strstarts(var, "gtk.") && !strcmp(var + 4, c->cmd))
		c->val = perf_config_bool(var, value) ? 2 : 0;
	return 0;
}

/*
 * returns 0 for "no tui", 1 for "use tui", 2 for "use gtk",
 * and -1 for "not specified"
 */
static int check_browser_config(const char *cmd)
{
	int err;
	struct pager_config c;
	c.cmd = cmd;
	c.val = -1;
	err = perf_config(browser_command_config, &c);
	return err ?: c.val;
}

static void commit_pager_choice(void)
{
	switch (use_pager) {
	case 0:
		setenv(PERF_PAGER_ENVIRONMENT, "cat", 1);
		break;
	case 1:
		/* setup_pager(); */
		break;
	default:
		break;
	}
}

struct option options[] = {
	OPT_ARGUMENT("help", "help"),
	OPT_ARGUMENT("version", "version"),
	OPT_ARGUMENT("exec-path", "exec-path"),
	OPT_ARGUMENT("html-path", "html-path"),
	OPT_ARGUMENT("paginate", "paginate"),
	OPT_ARGUMENT("no-pager", "no-pager"),
	OPT_ARGUMENT("debugfs-dir", "debugfs-dir"),
	OPT_ARGUMENT("buildid-dir", "buildid-dir"),
	OPT_ARGUMENT("list-cmds", "list-cmds"),
	OPT_ARGUMENT("list-opts", "list-opts"),
	OPT_ARGUMENT("debug", "debug"),
	OPT_END()
};

static int handle_options(const char ***argv, int *argc, int *envchanged)
{
	int handled = 0;

	while (*argc > 0) {
		const char *cmd = (*argv)[0];
		if (cmd[0] != '-')
			break;

		/*
		 * For legacy reasons, the "version" and "help"
		 * commands can be written with "--" prepended
		 * to make them look like flags.
		 */
		if (!strcmp(cmd, "--help") || !strcmp(cmd, "--version"))
			break;

		/*
		 * Shortcut for '-h' and '-v' options to invoke help
		 * and version command.
		 */
		if (!strcmp(cmd, "-h")) {
			(*argv)[0] = "--help";
			break;
		}

		if (!strcmp(cmd, "-v")) {
			(*argv)[0] = "--version";
			break;
		}

		/*
		 * Check remaining flags.
		 */
		if (strstarts(cmd, CMD_EXEC_PATH)) {
			cmd += strlen(CMD_EXEC_PATH);
			if (*cmd == '=')
				set_argv_exec_path(cmd + 1);
			else {
				puts(get_argv_exec_path());
				exit(0);
			}
		} else if (!strcmp(cmd, "--html-path")) {
			puts(system_path(PERF_HTML_PATH));
			exit(0);
		} else if (!strcmp(cmd, "-p") || !strcmp(cmd, "--paginate")) {
			use_pager = 1;
		} else if (!strcmp(cmd, "--no-pager")) {
			use_pager = 0;
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--debugfs-dir")) {
			if (*argc < 2) {
				fprintf(stderr, "No directory given for --debugfs-dir.\n");
				usage(perf_usage_string);
			}
			tracing_path_set((*argv)[1]);
			if (envchanged)
				*envchanged = 1;
			(*argv)++;
			(*argc)--;
		} else if (!strcmp(cmd, "--buildid-dir")) {
			if (*argc < 2) {
				fprintf(stderr, "No directory given for --buildid-dir.\n");
				usage(perf_usage_string);
			}
			set_buildid_dir((*argv)[1]);
			if (envchanged)
				*envchanged = 1;
			(*argv)++;
			(*argc)--;
		} else if (strstarts(cmd, CMD_DEBUGFS_DIR)) {
			tracing_path_set(cmd + strlen(CMD_DEBUGFS_DIR));
			fprintf(stderr, "dir: %s\n", tracing_path);
			if (envchanged)
				*envchanged = 1;
		} else if (!strcmp(cmd, "--list-cmds")) {
			unsigned int i;

			for (i = 0; i < ARRAY_SIZE(commands); i++) {
				struct cmd_struct *p = commands+i;
				printf("%s ", p->cmd);
			}
			putchar('\n');
			exit(0);
		} else if (!strcmp(cmd, "--list-opts")) {
			unsigned int i;

			for (i = 0; i < ARRAY_SIZE(options)-1; i++) {
				struct option *p = options+i;
				printf("--%s ", p->long_name);
			}
			putchar('\n');
			exit(0);
		} else if (!strcmp(cmd, "--debug")) {
			if (*argc < 2) {
				fprintf(stderr, "No variable specified for --debug.\n");
				usage(perf_usage_string);
			}
			if (perf_debug_option((*argv)[1]))
				usage(perf_usage_string);

			(*argv)++;
			(*argc)--;
		} else {
			fprintf(stderr, "Unknown option: %s\n", cmd);
			usage(perf_usage_string);
		}

		(*argv)++;
		(*argc)--;
		handled++;
	}
	return handled;
}

#define RUN_SETUP	(1<<0)
#define USE_PAGER	(1<<1)

static int run_builtin(struct cmd_struct *p, int argc, const char **argv)
{
	int status;
	struct stat st;
	char sbuf[STRERR_BUFSIZE];

	if (use_browser == -1)
		use_browser = check_browser_config(p->cmd);

	if (use_pager == -1 && p->option & RUN_SETUP)
		use_pager = check_pager_config(p->cmd);
	if (use_pager == -1 && p->option & USE_PAGER)
		use_pager = 1;
	commit_pager_choice();

	perf_env__set_cmdline(&perf_env, argc, argv);
	status = p->fn(argc, argv);
	perf_config__exit();
	exit_browser(status);
	perf_env__exit(&perf_env);
	bpf__clear();

	if (status)
		return status & 0xff;

	/* Somebody closed stdout? */
	if (fstat(fileno(stdout), &st))
		return 0;
	/* Ignore write errors for pipes and sockets.. */
	if (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode))
		return 0;

	status = 1;
	/* Check for ENOSPC and EIO errors.. */
	if (fflush(stdout)) {
		fprintf(stderr, "write failure on standard output: %s",
			str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out;
	}
	if (ferror(stdout)) {
		fprintf(stderr, "unknown write failure on standard output");
		goto out;
	}
	if (fclose(stdout)) {
		fprintf(stderr, "close failed on standard output: %s",
			str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out;
	}
	status = 0;
out:
	return status;
}

static void handle_internal_command(int argc, const char **argv)
{
	const char *cmd = argv[0];
	unsigned int i;

	/* Turn "perf cmd --help" into "perf help cmd" */
	if (argc > 1 && !strcmp(argv[1], "--help")) {
		argv[1] = argv[0];
		argv[0] = cmd = "help";
	}

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		struct cmd_struct *p = commands+i;
		if (strcmp(p->cmd, cmd))
			continue;
		exit(run_builtin(p, argc, argv));
	}
}

static void execv_dashed_external(const char **argv)
{
	char *cmd;
	const char *tmp;
	int status;

	if (asprintf(&cmd, "perf-%s", argv[0]) < 0)
		goto do_die;

	/*
	 * argv[0] must be the perf command, but the argv array
	 * belongs to the caller, and may be reused in
	 * subsequent loop iterations. Save argv[0] and
	 * restore it on error.
	 */
	tmp = argv[0];
	argv[0] = cmd;

	/*
	 * if we fail because the command is not found, it is
	 * OK to return. Otherwise, we just pass along the status code.
	 */
	status = run_command_v_opt(argv, 0);
	if (status != -ERR_RUN_COMMAND_EXEC) {
		if (IS_RUN_COMMAND_ERR(status)) {
do_die:
			pr_err("FATAL: unable to run '%s'", argv[0]);
			status = -128;
		}
		exit(-status);
	}
	errno = ENOENT; /* as if we called execvp */

	argv[0] = tmp;
	zfree(&cmd);
}

static int run_argv(int *argcp, const char ***argv)
{
	/* See if it's an internal command */
	handle_internal_command(*argcp, *argv);

	/* .. then try the external ones */
	execv_dashed_external(*argv);
	return 0;
}

static void pthread__block_sigwinch(void)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGWINCH);
	pthread_sigmask(SIG_BLOCK, &set, NULL);
}

void pthread__unblock_sigwinch(void)
{
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGWINCH);
	pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}

#ifdef _SC_LEVEL1_DCACHE_LINESIZE
#define cache_line_size(cacheline_sizep) *cacheline_sizep = sysconf(_SC_LEVEL1_DCACHE_LINESIZE)
#else
static void cache_line_size(int *cacheline_sizep)
{
	if (sysfs__read_int("devices/system/cpu/cpu0/cache/index0/coherency_line_size", cacheline_sizep))
		pr_debug("cannot determine cache line size");
}
#endif

int main(int argc, const char **argv)
{
	int err;
	const char *cmd;
	char sbuf[STRERR_BUFSIZE];
	int value;

	/* libsubcmd init */
	exec_cmd_init("perf", PREFIX, PERF_EXEC_PATH, EXEC_PATH_ENVIRONMENT);
	pager_init(PERF_PAGER_ENVIRONMENT);

	/* The page_size is placed in util object. */
	page_size = sysconf(_SC_PAGE_SIZE);
	cache_line_size(&cacheline_size);

	if (sysctl__read_int("kernel/perf_event_max_stack", &value) == 0)
		sysctl_perf_event_max_stack = value;

	if (sysctl__read_int("kernel/perf_event_max_contexts_per_stack", &value) == 0)
		sysctl_perf_event_max_contexts_per_stack = value;

	cmd = extract_argv0_path(argv[0]);
	if (!cmd)
		cmd = "perf-help";

	srandom(time(NULL));

	perf_config__init();
	err = perf_config(perf_default_config, NULL);
	if (err)
		return err;
	set_buildid_dir(NULL);

	/* get debugfs/tracefs mount point from /proc/mounts */
	tracing_path_mount();

	/*
	 * "perf-xxxx" is the same as "perf xxxx", but we obviously:
	 *
	 *  - cannot take flags in between the "perf" and the "xxxx".
	 *  - cannot execute it externally (since it would just do
	 *    the same thing over again)
	 *
	 * So we just directly call the internal command handler. If that one
	 * fails to handle this, then maybe we just run a renamed perf binary
	 * that contains a dash in its name. To handle this scenario, we just
	 * fall through and ignore the "xxxx" part of the command string.
	 */
	if (strstarts(cmd, "perf-")) {
		cmd += 5;
		argv[0] = cmd;
		handle_internal_command(argc, argv);
		/*
		 * If the command is handled, the above function does not
		 * return undo changes and fall through in such a case.
		 */
		cmd -= 5;
		argv[0] = cmd;
	}
	if (strstarts(cmd, "trace")) {
#if defined(HAVE_LIBAUDIT_SUPPORT) || defined(HAVE_SYSCALL_TABLE)
		setup_path();
		argv[0] = "trace";
		return cmd_trace(argc, argv);
#else
		fprintf(stderr,
			"trace command not available: missing audit-libs devel package at build time.\n");
		goto out;
#endif
	}
	/* Look for flags.. */
	argv++;
	argc--;
	handle_options(&argv, &argc, NULL);
	commit_pager_choice();

	if (argc > 0) {
		if (strstarts(argv[0], "--"))
			argv[0] += 2;
	} else {
		/* The user didn't specify a command; give them help */
		printf("\n usage: %s\n\n", perf_usage_string);
		list_common_cmds_help();
		printf("\n %s\n\n", perf_more_info_string);
		goto out;
	}
	cmd = argv[0];

	test_attr__init();

	/*
	 * We use PATH to find perf commands, but we prepend some higher
	 * precedence paths: the "--exec-path" option, the PERF_EXEC_PATH
	 * environment, and the $(perfexecdir) from the Makefile at build
	 * time.
	 */
	setup_path();
	/*
	 * Block SIGWINCH notifications so that the thread that wants it can
	 * unblock and get syscalls like select interrupted instead of waiting
	 * forever while the signal goes to some other non interested thread.
	 */
	pthread__block_sigwinch();

	perf_debug_setup();

	while (1) {
		static int done_help;

		run_argv(&argc, &argv);

		if (errno != ENOENT)
			break;

		if (!done_help) {
			cmd = argv[0] = help_unknown_cmd(cmd);
			done_help = 1;
		} else
			break;
	}

	fprintf(stderr, "Failed to run command '%s': %s\n",
		cmd, str_error_r(errno, sbuf, sizeof(sbuf)));
out:
	return 1;
}
