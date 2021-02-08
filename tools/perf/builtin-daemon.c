// SPDX-License-Identifier: GPL-2.0
#include <subcmd/parse-options.h>
#include <linux/limits.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include "builtin.h"
#include "perf.h"
#include "debug.h"
#include "util.h"

struct daemon {
	char			*base;
	FILE			*out;
	char			 perf[PATH_MAX];
};

static struct daemon __daemon = { };

static const char * const daemon_usage[] = {
	"perf daemon start [<options>]",
	"perf daemon [<options>]",
	NULL
};

static bool done;

static void sig_handler(int sig __maybe_unused)
{
	done = true;
}

static int __cmd_start(struct daemon *daemon, struct option parent_options[],
		       int argc, const char **argv)
{
	struct option start_options[] = {
		OPT_PARENT(parent_options),
		OPT_END()
	};
	int err = 0;

	argc = parse_options(argc, argv, start_options, daemon_usage, 0);
	if (argc)
		usage_with_options(daemon_usage, start_options);

	debug_set_file(daemon->out);
	debug_set_display_time(true);

	pr_info("daemon started (pid %d)\n", getpid());

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	while (!done && !err) {
		sleep(1);
	}

	pr_info("daemon exited\n");
	fclose(daemon->out);
	return err;
}

int cmd_daemon(int argc, const char **argv)
{
	struct option daemon_options[] = {
		OPT_INCR('v', "verbose", &verbose, "be more verbose"),
		OPT_END()
	};

	perf_exe(__daemon.perf, sizeof(__daemon.perf));
	__daemon.out = stdout;

	argc = parse_options(argc, argv, daemon_options, daemon_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (argc) {
		if (!strcmp(argv[0], "start"))
			return __cmd_start(&__daemon, daemon_options, argc, argv);

		pr_err("failed: unknown command '%s'\n", argv[0]);
		return -1;
	}

	return -1;
}
