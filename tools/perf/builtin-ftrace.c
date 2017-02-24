/*
 * builtin-ftrace.c
 *
 * Copyright (c) 2013  LG Electronics,  Namhyung Kim <namhyung@kernel.org>
 *
 * Released under the GPL v2.
 */

#include "builtin.h"
#include "perf.h"

#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include "debug.h"
#include <subcmd/parse-options.h>
#include "evlist.h"
#include "target.h"
#include "thread_map.h"
#include "util/config.h"


#define DEFAULT_TRACER  "function_graph"

struct perf_ftrace {
	struct perf_evlist *evlist;
	struct target target;
	const char *tracer;
};

static bool done;

static void sig_handler(int sig __maybe_unused)
{
	done = true;
}

/*
 * perf_evlist__prepare_workload will send a SIGUSR1 if the fork fails, since
 * we asked by setting its exec_error to the function below,
 * ftrace__workload_exec_failed_signal.
 *
 * XXX We need to handle this more appropriately, emitting an error, etc.
 */
static void ftrace__workload_exec_failed_signal(int signo __maybe_unused,
						siginfo_t *info __maybe_unused,
						void *ucontext __maybe_unused)
{
	/* workload_exec_errno = info->si_value.sival_int; */
	done = true;
}

static int __write_tracing_file(const char *name, const char *val, bool append)
{
	char *file;
	int fd, ret = -1;
	ssize_t size = strlen(val);
	int flags = O_WRONLY;

	file = get_tracing_file(name);
	if (!file) {
		pr_debug("cannot get tracing file: %s\n", name);
		return -1;
	}

	if (append)
		flags |= O_APPEND;
	else
		flags |= O_TRUNC;

	fd = open(file, flags);
	if (fd < 0) {
		pr_debug("cannot open tracing file: %s\n", name);
		goto out;
	}

	if (write(fd, val, size) == size)
		ret = 0;
	else
		pr_debug("write '%s' to tracing/%s failed\n", val, name);

	close(fd);
out:
	put_tracing_file(file);
	return ret;
}

static int write_tracing_file(const char *name, const char *val)
{
	return __write_tracing_file(name, val, false);
}

static int append_tracing_file(const char *name, const char *val)
{
	return __write_tracing_file(name, val, true);
}

static int reset_tracing_files(struct perf_ftrace *ftrace __maybe_unused)
{
	if (write_tracing_file("tracing_on", "0") < 0)
		return -1;

	if (write_tracing_file("current_tracer", "nop") < 0)
		return -1;

	if (write_tracing_file("set_ftrace_pid", " ") < 0)
		return -1;

	return 0;
}

static int set_tracing_pid(struct perf_ftrace *ftrace)
{
	int i;
	char buf[16];

	if (target__has_cpu(&ftrace->target))
		return 0;

	for (i = 0; i < thread_map__nr(ftrace->evlist->threads); i++) {
		scnprintf(buf, sizeof(buf), "%d",
			  ftrace->evlist->threads->map[i]);
		if (append_tracing_file("set_ftrace_pid", buf) < 0)
			return -1;
	}
	return 0;
}

static int __cmd_ftrace(struct perf_ftrace *ftrace, int argc, const char **argv)
{
	char *trace_file;
	int trace_fd;
	char buf[4096];
	struct pollfd pollfd = {
		.events = POLLIN,
	};

	if (geteuid() != 0) {
		pr_err("ftrace only works for root!\n");
		return -1;
	}

	signal(SIGINT, sig_handler);
	signal(SIGUSR1, sig_handler);
	signal(SIGCHLD, sig_handler);

	if (reset_tracing_files(ftrace) < 0)
		goto out;

	/* reset ftrace buffer */
	if (write_tracing_file("trace", "0") < 0)
		goto out;

	if (argc && perf_evlist__prepare_workload(ftrace->evlist,
				&ftrace->target, argv, false,
				ftrace__workload_exec_failed_signal) < 0) {
		goto out;
	}

	if (set_tracing_pid(ftrace) < 0) {
		pr_err("failed to set ftrace pid\n");
		goto out_reset;
	}

	if (write_tracing_file("current_tracer", ftrace->tracer) < 0) {
		pr_err("failed to set current_tracer to %s\n", ftrace->tracer);
		goto out_reset;
	}

	trace_file = get_tracing_file("trace_pipe");
	if (!trace_file) {
		pr_err("failed to open trace_pipe\n");
		goto out_reset;
	}

	trace_fd = open(trace_file, O_RDONLY);

	put_tracing_file(trace_file);

	if (trace_fd < 0) {
		pr_err("failed to open trace_pipe\n");
		goto out_reset;
	}

	fcntl(trace_fd, F_SETFL, O_NONBLOCK);
	pollfd.fd = trace_fd;

	if (write_tracing_file("tracing_on", "1") < 0) {
		pr_err("can't enable tracing\n");
		goto out_close_fd;
	}

	perf_evlist__start_workload(ftrace->evlist);

	while (!done) {
		if (poll(&pollfd, 1, -1) < 0)
			break;

		if (pollfd.revents & POLLIN) {
			int n = read(trace_fd, buf, sizeof(buf));
			if (n < 0)
				break;
			if (fwrite(buf, n, 1, stdout) != 1)
				break;
		}
	}

	write_tracing_file("tracing_on", "0");

	/* read remaining buffer contents */
	while (true) {
		int n = read(trace_fd, buf, sizeof(buf));
		if (n <= 0)
			break;
		if (fwrite(buf, n, 1, stdout) != 1)
			break;
	}

out_close_fd:
	close(trace_fd);
out_reset:
	reset_tracing_files(ftrace);
out:
	return done ? 0 : -1;
}

static int perf_ftrace_config(const char *var, const char *value, void *cb)
{
	struct perf_ftrace *ftrace = cb;

	if (prefixcmp(var, "ftrace."))
		return 0;

	if (strcmp(var, "ftrace.tracer"))
		return -1;

	if (!strcmp(value, "function_graph") ||
	    !strcmp(value, "function")) {
		ftrace->tracer = value;
		return 0;
	}

	pr_err("Please select \"function_graph\" (default) or \"function\"\n");
	return -1;
}

int cmd_ftrace(int argc, const char **argv, const char *prefix __maybe_unused)
{
	int ret;
	struct perf_ftrace ftrace = {
		.tracer = DEFAULT_TRACER,
		.target = { .uid = UINT_MAX, },
	};
	const char * const ftrace_usage[] = {
		"perf ftrace [<options>] [<command>]",
		"perf ftrace [<options>] -- <command> [<options>]",
		NULL
	};
	const struct option ftrace_options[] = {
	OPT_STRING('t', "tracer", &ftrace.tracer, "tracer",
		   "tracer to use: function_graph(default) or function"),
	OPT_STRING('p', "pid", &ftrace.target.pid, "pid",
		   "trace on existing process id"),
	OPT_INCR('v', "verbose", &verbose,
		 "be more verbose"),
	OPT_END()
	};

	ret = perf_config(perf_ftrace_config, &ftrace);
	if (ret < 0)
		return -1;

	argc = parse_options(argc, argv, ftrace_options, ftrace_usage,
			    PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc && target__none(&ftrace.target))
		usage_with_options(ftrace_usage, ftrace_options);

	ret = target__validate(&ftrace.target);
	if (ret) {
		char errbuf[512];

		target__strerror(&ftrace.target, ret, errbuf, 512);
		pr_err("%s\n", errbuf);
		return -EINVAL;
	}

	ftrace.evlist = perf_evlist__new();
	if (ftrace.evlist == NULL)
		return -ENOMEM;

	ret = perf_evlist__create_maps(ftrace.evlist, &ftrace.target);
	if (ret < 0)
		goto out_delete_evlist;

	ret = __cmd_ftrace(&ftrace, argc, argv);

out_delete_evlist:
	perf_evlist__delete(ftrace.evlist);

	return ret;
}
