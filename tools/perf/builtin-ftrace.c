// SPDX-License-Identifier: GPL-2.0-only
/*
 * builtin-ftrace.c
 *
 * Copyright (c) 2013  LG Electronics,  Namhyung Kim <namhyung@kernel.org>
 */

#include "builtin.h"
#include "perf.h"

#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>

#include "debug.h"
#include <subcmd/parse-options.h>
#include <api/fs/tracing_path.h>
#include "evlist.h"
#include "target.h"
#include "cpumap.h"
#include "thread_map.h"
#include "util/config.h"


#define DEFAULT_TRACER  "function_graph"

struct perf_ftrace {
	struct perf_evlist	*evlist;
	struct target		target;
	const char		*tracer;
	struct list_head	filters;
	struct list_head	notrace;
	struct list_head	graph_funcs;
	struct list_head	nograph_funcs;
	int			graph_depth;
};

struct filter_entry {
	struct list_head	list;
	char			name[];
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
	char errbuf[512];
	char *val_copy;

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
		pr_debug("cannot open tracing file: %s: %s\n",
			 name, str_error_r(errno, errbuf, sizeof(errbuf)));
		goto out;
	}

	/*
	 * Copy the original value and append a '\n'. Without this,
	 * the kernel can hide possible errors.
	 */
	val_copy = strdup(val);
	if (!val_copy)
		goto out_close;
	val_copy[size] = '\n';

	if (write(fd, val_copy, size + 1) == size + 1)
		ret = 0;
	else
		pr_debug("write '%s' to tracing/%s failed: %s\n",
			 val, name, str_error_r(errno, errbuf, sizeof(errbuf)));

	free(val_copy);
out_close:
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

static int reset_tracing_cpu(void);
static void reset_tracing_filters(void);

static int reset_tracing_files(struct perf_ftrace *ftrace __maybe_unused)
{
	if (write_tracing_file("tracing_on", "0") < 0)
		return -1;

	if (write_tracing_file("current_tracer", "nop") < 0)
		return -1;

	if (write_tracing_file("set_ftrace_pid", " ") < 0)
		return -1;

	if (reset_tracing_cpu() < 0)
		return -1;

	if (write_tracing_file("max_graph_depth", "0") < 0)
		return -1;

	reset_tracing_filters();
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

static int set_tracing_cpumask(struct cpu_map *cpumap)
{
	char *cpumask;
	size_t mask_size;
	int ret;
	int last_cpu;

	last_cpu = cpu_map__cpu(cpumap, cpumap->nr - 1);
	mask_size = last_cpu / 4 + 2; /* one more byte for EOS */
	mask_size += last_cpu / 32; /* ',' is needed for every 32th cpus */

	cpumask = malloc(mask_size);
	if (cpumask == NULL) {
		pr_debug("failed to allocate cpu mask\n");
		return -1;
	}

	cpu_map__snprint_mask(cpumap, cpumask, mask_size);

	ret = write_tracing_file("tracing_cpumask", cpumask);

	free(cpumask);
	return ret;
}

static int set_tracing_cpu(struct perf_ftrace *ftrace)
{
	struct cpu_map *cpumap = ftrace->evlist->cpus;

	if (!target__has_cpu(&ftrace->target))
		return 0;

	return set_tracing_cpumask(cpumap);
}

static int reset_tracing_cpu(void)
{
	struct cpu_map *cpumap = cpu_map__new(NULL);
	int ret;

	ret = set_tracing_cpumask(cpumap);
	cpu_map__put(cpumap);
	return ret;
}

static int __set_tracing_filter(const char *filter_file, struct list_head *funcs)
{
	struct filter_entry *pos;

	list_for_each_entry(pos, funcs, list) {
		if (append_tracing_file(filter_file, pos->name) < 0)
			return -1;
	}

	return 0;
}

static int set_tracing_filters(struct perf_ftrace *ftrace)
{
	int ret;

	ret = __set_tracing_filter("set_ftrace_filter", &ftrace->filters);
	if (ret < 0)
		return ret;

	ret = __set_tracing_filter("set_ftrace_notrace", &ftrace->notrace);
	if (ret < 0)
		return ret;

	ret = __set_tracing_filter("set_graph_function", &ftrace->graph_funcs);
	if (ret < 0)
		return ret;

	/* old kernels do not have this filter */
	__set_tracing_filter("set_graph_notrace", &ftrace->nograph_funcs);

	return ret;
}

static void reset_tracing_filters(void)
{
	write_tracing_file("set_ftrace_filter", " ");
	write_tracing_file("set_ftrace_notrace", " ");
	write_tracing_file("set_graph_function", " ");
	write_tracing_file("set_graph_notrace", " ");
}

static int set_tracing_depth(struct perf_ftrace *ftrace)
{
	char buf[16];

	if (ftrace->graph_depth == 0)
		return 0;

	if (ftrace->graph_depth < 0) {
		pr_err("invalid graph depth: %d\n", ftrace->graph_depth);
		return -1;
	}

	snprintf(buf, sizeof(buf), "%d", ftrace->graph_depth);

	if (write_tracing_file("max_graph_depth", buf) < 0)
		return -1;

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
	signal(SIGPIPE, sig_handler);

	if (reset_tracing_files(ftrace) < 0) {
		pr_err("failed to reset ftrace\n");
		goto out;
	}

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

	if (set_tracing_cpu(ftrace) < 0) {
		pr_err("failed to set tracing cpumask\n");
		goto out_reset;
	}

	if (set_tracing_filters(ftrace) < 0) {
		pr_err("failed to set tracing filters\n");
		goto out_reset;
	}

	if (set_tracing_depth(ftrace) < 0) {
		pr_err("failed to set graph depth\n");
		goto out_reset;
	}

	if (write_tracing_file("current_tracer", ftrace->tracer) < 0) {
		pr_err("failed to set current_tracer to %s\n", ftrace->tracer);
		goto out_reset;
	}

	setup_pager();

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

	if (!strstarts(var, "ftrace."))
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

static int parse_filter_func(const struct option *opt, const char *str,
			     int unset __maybe_unused)
{
	struct list_head *head = opt->value;
	struct filter_entry *entry;

	entry = malloc(sizeof(*entry) + strlen(str) + 1);
	if (entry == NULL)
		return -ENOMEM;

	strcpy(entry->name, str);
	list_add_tail(&entry->list, head);

	return 0;
}

static void delete_filter_func(struct list_head *head)
{
	struct filter_entry *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, head, list) {
		list_del_init(&pos->list);
		free(pos);
	}
}

int cmd_ftrace(int argc, const char **argv)
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
	OPT_BOOLEAN('a', "all-cpus", &ftrace.target.system_wide,
		    "system-wide collection from all CPUs"),
	OPT_STRING('C', "cpu", &ftrace.target.cpu_list, "cpu",
		    "list of cpus to monitor"),
	OPT_CALLBACK('T', "trace-funcs", &ftrace.filters, "func",
		     "trace given functions only", parse_filter_func),
	OPT_CALLBACK('N', "notrace-funcs", &ftrace.notrace, "func",
		     "do not trace given functions", parse_filter_func),
	OPT_CALLBACK('G', "graph-funcs", &ftrace.graph_funcs, "func",
		     "Set graph filter on given functions", parse_filter_func),
	OPT_CALLBACK('g', "nograph-funcs", &ftrace.nograph_funcs, "func",
		     "Set nograph filter on given functions", parse_filter_func),
	OPT_INTEGER('D', "graph-depth", &ftrace.graph_depth,
		    "Max depth for function graph tracer"),
	OPT_END()
	};

	INIT_LIST_HEAD(&ftrace.filters);
	INIT_LIST_HEAD(&ftrace.notrace);
	INIT_LIST_HEAD(&ftrace.graph_funcs);
	INIT_LIST_HEAD(&ftrace.nograph_funcs);

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
		goto out_delete_filters;
	}

	ftrace.evlist = perf_evlist__new();
	if (ftrace.evlist == NULL) {
		ret = -ENOMEM;
		goto out_delete_filters;
	}

	ret = perf_evlist__create_maps(ftrace.evlist, &ftrace.target);
	if (ret < 0)
		goto out_delete_evlist;

	ret = __cmd_ftrace(&ftrace, argc, argv);

out_delete_evlist:
	perf_evlist__delete(ftrace.evlist);

out_delete_filters:
	delete_filter_func(&ftrace.filters);
	delete_filter_func(&ftrace.notrace);
	delete_filter_func(&ftrace.graph_funcs);
	delete_filter_func(&ftrace.nograph_funcs);

	return ret;
}
