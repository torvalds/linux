// SPDX-License-Identifier: GPL-2.0
#include <subcmd/parse-options.h>
#include <linux/limits.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "builtin.h"
#include "perf.h"
#include "debug.h"
#include "config.h"
#include "util.h"

struct daemon {
	const char		*config;
	char			*config_real;
	const char		*base_user;
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

static void daemon__exit(struct daemon *daemon)
{
	free(daemon->config_real);
	free(daemon->base);
}

static int setup_config(struct daemon *daemon)
{
	if (daemon->base_user) {
		daemon->base = strdup(daemon->base_user);
		if (!daemon->base)
			return -ENOMEM;
	}

	if (daemon->config) {
		char *real = realpath(daemon->config, NULL);

		if (!real) {
			perror("failed: realpath");
			return -1;
		}
		daemon->config_real = real;
		return 0;
	}

	if (perf_config_system() && !access(perf_etc_perfconfig(), R_OK))
		daemon->config_real = strdup(perf_etc_perfconfig());
	else if (perf_config_global() && perf_home_perfconfig())
		daemon->config_real = strdup(perf_home_perfconfig());

	return daemon->config_real ? 0 : -1;
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

	if (setup_config(daemon)) {
		pr_err("failed: config not found\n");
		return -1;
	}

	debug_set_file(daemon->out);
	debug_set_display_time(true);

	pr_info("daemon started (pid %d)\n", getpid());

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	while (!done && !err) {
		sleep(1);
	}

	daemon__exit(daemon);

	pr_info("daemon exited\n");
	fclose(daemon->out);
	return err;
}

int cmd_daemon(int argc, const char **argv)
{
	struct option daemon_options[] = {
		OPT_INCR('v', "verbose", &verbose, "be more verbose"),
		OPT_STRING(0, "config", &__daemon.config,
			"config file", "config file path"),
		OPT_STRING(0, "base", &__daemon.base_user,
			"directory", "base directory"),
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

	if (setup_config(&__daemon)) {
		pr_err("failed: config not found\n");
		return -1;
	}

	return -1;
}
