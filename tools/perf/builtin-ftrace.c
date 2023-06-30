// SPDX-License-Identifier: GPL-2.0-only
/*
 * builtin-ftrace.c
 *
 * Copyright (c) 2013  LG Electronics,  Namhyung Kim <namhyung@kernel.org>
 * Copyright (c) 2020  Changbin Du <changbin.du@gmail.com>, significant enhancement.
 */

#include "builtin.h"

#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <ctype.h>
#include <linux/capability.h>
#include <linux/string.h>

#include "debug.h"
#include <subcmd/pager.h>
#include <subcmd/parse-options.h>
#include <api/fs/tracing_path.h>
#include "evlist.h"
#include "target.h"
#include "cpumap.h"
#include "thread_map.h"
#include "strfilter.h"
#include "util/cap.h"
#include "util/config.h"
#include "util/ftrace.h"
#include "util/units.h"
#include "util/parse-sublevel-options.h"

#define DEFAULT_TRACER  "function_graph"

static volatile sig_atomic_t workload_exec_errno;
static volatile sig_atomic_t done;

static void sig_handler(int sig __maybe_unused)
{
	done = true;
}

/*
 * evlist__prepare_workload will send a SIGUSR1 if the fork fails, since
 * we asked by setting its exec_error to the function below,
 * ftrace__workload_exec_failed_signal.
 *
 * XXX We need to handle this more appropriately, emitting an error, etc.
 */
static void ftrace__workload_exec_failed_signal(int signo __maybe_unused,
						siginfo_t *info __maybe_unused,
						void *ucontext __maybe_unused)
{
	workload_exec_errno = info->si_value.sival_int;
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

static int read_tracing_file_to_stdout(const char *name)
{
	char buf[4096];
	char *file;
	int fd;
	int ret = -1;

	file = get_tracing_file(name);
	if (!file) {
		pr_debug("cannot get tracing file: %s\n", name);
		return -1;
	}

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		pr_debug("cannot open tracing file: %s: %s\n",
			 name, str_error_r(errno, buf, sizeof(buf)));
		goto out;
	}

	/* read contents to stdout */
	while (true) {
		int n = read(fd, buf, sizeof(buf));
		if (n == 0)
			break;
		else if (n < 0)
			goto out_close;

		if (fwrite(buf, n, 1, stdout) != 1)
			goto out_close;
	}
	ret = 0;

out_close:
	close(fd);
out:
	put_tracing_file(file);
	return ret;
}

static int read_tracing_file_by_line(const char *name,
				     void (*cb)(char *str, void *arg),
				     void *cb_arg)
{
	char *line = NULL;
	size_t len = 0;
	char *file;
	FILE *fp;

	file = get_tracing_file(name);
	if (!file) {
		pr_debug("cannot get tracing file: %s\n", name);
		return -1;
	}

	fp = fopen(file, "r");
	if (fp == NULL) {
		pr_debug("cannot open tracing file: %s\n", name);
		put_tracing_file(file);
		return -1;
	}

	while (getline(&line, &len, fp) != -1) {
		cb(line, cb_arg);
	}

	if (line)
		free(line);

	fclose(fp);
	put_tracing_file(file);
	return 0;
}

static int write_tracing_file_int(const char *name, int value)
{
	char buf[16];

	snprintf(buf, sizeof(buf), "%d", value);
	if (write_tracing_file(name, buf) < 0)
		return -1;

	return 0;
}

static int write_tracing_option_file(const char *name, const char *val)
{
	char *file;
	int ret;

	if (asprintf(&file, "options/%s", name) < 0)
		return -1;

	ret = __write_tracing_file(file, val, false);
	free(file);
	return ret;
}

static int reset_tracing_cpu(void);
static void reset_tracing_filters(void);

static void reset_tracing_options(struct perf_ftrace *ftrace __maybe_unused)
{
	write_tracing_option_file("function-fork", "0");
	write_tracing_option_file("func_stack_trace", "0");
	write_tracing_option_file("sleep-time", "1");
	write_tracing_option_file("funcgraph-irqs", "1");
	write_tracing_option_file("funcgraph-proc", "0");
	write_tracing_option_file("funcgraph-abstime", "0");
	write_tracing_option_file("latency-format", "0");
	write_tracing_option_file("irq-info", "0");
}

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

	if (write_tracing_file("tracing_thresh", "0") < 0)
		return -1;

	reset_tracing_filters();
	reset_tracing_options(ftrace);
	return 0;
}

static int set_tracing_pid(struct perf_ftrace *ftrace)
{
	int i;
	char buf[16];

	if (target__has_cpu(&ftrace->target))
		return 0;

	for (i = 0; i < perf_thread_map__nr(ftrace->evlist->core.threads); i++) {
		scnprintf(buf, sizeof(buf), "%d",
			  perf_thread_map__pid(ftrace->evlist->core.threads, i));
		if (append_tracing_file("set_ftrace_pid", buf) < 0)
			return -1;
	}
	return 0;
}

static int set_tracing_cpumask(struct perf_cpu_map *cpumap)
{
	char *cpumask;
	size_t mask_size;
	int ret;
	int last_cpu;

	last_cpu = perf_cpu_map__cpu(cpumap, perf_cpu_map__nr(cpumap) - 1).cpu;
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
	struct perf_cpu_map *cpumap = ftrace->evlist->core.user_requested_cpus;

	if (!target__has_cpu(&ftrace->target))
		return 0;

	return set_tracing_cpumask(cpumap);
}

static int set_tracing_func_stack_trace(struct perf_ftrace *ftrace)
{
	if (!ftrace->func_stack_trace)
		return 0;

	if (write_tracing_option_file("func_stack_trace", "1") < 0)
		return -1;

	return 0;
}

static int set_tracing_func_irqinfo(struct perf_ftrace *ftrace)
{
	if (!ftrace->func_irq_info)
		return 0;

	if (write_tracing_option_file("irq-info", "1") < 0)
		return -1;

	return 0;
}

static int reset_tracing_cpu(void)
{
	struct perf_cpu_map *cpumap = perf_cpu_map__new(NULL);
	int ret;

	ret = set_tracing_cpumask(cpumap);
	perf_cpu_map__put(cpumap);
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
	if (ftrace->graph_depth == 0)
		return 0;

	if (ftrace->graph_depth < 0) {
		pr_err("invalid graph depth: %d\n", ftrace->graph_depth);
		return -1;
	}

	if (write_tracing_file_int("max_graph_depth", ftrace->graph_depth) < 0)
		return -1;

	return 0;
}

static int set_tracing_percpu_buffer_size(struct perf_ftrace *ftrace)
{
	int ret;

	if (ftrace->percpu_buffer_size == 0)
		return 0;

	ret = write_tracing_file_int("buffer_size_kb",
				     ftrace->percpu_buffer_size / 1024);
	if (ret < 0)
		return ret;

	return 0;
}

static int set_tracing_trace_inherit(struct perf_ftrace *ftrace)
{
	if (!ftrace->inherit)
		return 0;

	if (write_tracing_option_file("function-fork", "1") < 0)
		return -1;

	return 0;
}

static int set_tracing_sleep_time(struct perf_ftrace *ftrace)
{
	if (!ftrace->graph_nosleep_time)
		return 0;

	if (write_tracing_option_file("sleep-time", "0") < 0)
		return -1;

	return 0;
}

static int set_tracing_funcgraph_irqs(struct perf_ftrace *ftrace)
{
	if (!ftrace->graph_noirqs)
		return 0;

	if (write_tracing_option_file("funcgraph-irqs", "0") < 0)
		return -1;

	return 0;
}

static int set_tracing_funcgraph_verbose(struct perf_ftrace *ftrace)
{
	if (!ftrace->graph_verbose)
		return 0;

	if (write_tracing_option_file("funcgraph-proc", "1") < 0)
		return -1;

	if (write_tracing_option_file("funcgraph-abstime", "1") < 0)
		return -1;

	if (write_tracing_option_file("latency-format", "1") < 0)
		return -1;

	return 0;
}

static int set_tracing_thresh(struct perf_ftrace *ftrace)
{
	int ret;

	if (ftrace->graph_thresh == 0)
		return 0;

	ret = write_tracing_file_int("tracing_thresh", ftrace->graph_thresh);
	if (ret < 0)
		return ret;

	return 0;
}

static int set_tracing_options(struct perf_ftrace *ftrace)
{
	if (set_tracing_pid(ftrace) < 0) {
		pr_err("failed to set ftrace pid\n");
		return -1;
	}

	if (set_tracing_cpu(ftrace) < 0) {
		pr_err("failed to set tracing cpumask\n");
		return -1;
	}

	if (set_tracing_func_stack_trace(ftrace) < 0) {
		pr_err("failed to set tracing option func_stack_trace\n");
		return -1;
	}

	if (set_tracing_func_irqinfo(ftrace) < 0) {
		pr_err("failed to set tracing option irq-info\n");
		return -1;
	}

	if (set_tracing_filters(ftrace) < 0) {
		pr_err("failed to set tracing filters\n");
		return -1;
	}

	if (set_tracing_depth(ftrace) < 0) {
		pr_err("failed to set graph depth\n");
		return -1;
	}

	if (set_tracing_percpu_buffer_size(ftrace) < 0) {
		pr_err("failed to set tracing per-cpu buffer size\n");
		return -1;
	}

	if (set_tracing_trace_inherit(ftrace) < 0) {
		pr_err("failed to set tracing option function-fork\n");
		return -1;
	}

	if (set_tracing_sleep_time(ftrace) < 0) {
		pr_err("failed to set tracing option sleep-time\n");
		return -1;
	}

	if (set_tracing_funcgraph_irqs(ftrace) < 0) {
		pr_err("failed to set tracing option funcgraph-irqs\n");
		return -1;
	}

	if (set_tracing_funcgraph_verbose(ftrace) < 0) {
		pr_err("failed to set tracing option funcgraph-proc/funcgraph-abstime\n");
		return -1;
	}

	if (set_tracing_thresh(ftrace) < 0) {
		pr_err("failed to set tracing thresh\n");
		return -1;
	}

	return 0;
}

static void select_tracer(struct perf_ftrace *ftrace)
{
	bool graph = !list_empty(&ftrace->graph_funcs) ||
		     !list_empty(&ftrace->nograph_funcs);
	bool func = !list_empty(&ftrace->filters) ||
		    !list_empty(&ftrace->notrace);

	/* The function_graph has priority over function tracer. */
	if (graph)
		ftrace->tracer = "function_graph";
	else if (func)
		ftrace->tracer = "function";
	/* Otherwise, the default tracer is used. */

	pr_debug("%s tracer is used\n", ftrace->tracer);
}

static int __cmd_ftrace(struct perf_ftrace *ftrace)
{
	char *trace_file;
	int trace_fd;
	char buf[4096];
	struct pollfd pollfd = {
		.events = POLLIN,
	};

	if (!(perf_cap__capable(CAP_PERFMON) ||
	      perf_cap__capable(CAP_SYS_ADMIN))) {
		pr_err("ftrace only works for %s!\n",
#ifdef HAVE_LIBCAP_SUPPORT
		"users with the CAP_PERFMON or CAP_SYS_ADMIN capability"
#else
		"root"
#endif
		);
		return -1;
	}

	select_tracer(ftrace);

	if (reset_tracing_files(ftrace) < 0) {
		pr_err("failed to reset ftrace\n");
		goto out;
	}

	/* reset ftrace buffer */
	if (write_tracing_file("trace", "0") < 0)
		goto out;

	if (set_tracing_options(ftrace) < 0)
		goto out_reset;

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

	/* display column headers */
	read_tracing_file_to_stdout("trace");

	if (!ftrace->target.initial_delay) {
		if (write_tracing_file("tracing_on", "1") < 0) {
			pr_err("can't enable tracing\n");
			goto out_close_fd;
		}
	}

	evlist__start_workload(ftrace->evlist);

	if (ftrace->target.initial_delay > 0) {
		usleep(ftrace->target.initial_delay * 1000);
		if (write_tracing_file("tracing_on", "1") < 0) {
			pr_err("can't enable tracing\n");
			goto out_close_fd;
		}
	}

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

	if (workload_exec_errno) {
		const char *emsg = str_error_r(workload_exec_errno, buf, sizeof(buf));
		/* flush stdout first so below error msg appears at the end. */
		fflush(stdout);
		pr_err("workload failed: %s\n", emsg);
		goto out_close_fd;
	}

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
	return (done && !workload_exec_errno) ? 0 : -1;
}

static void make_histogram(int buckets[], char *buf, size_t len, char *linebuf,
			   bool use_nsec)
{
	char *p, *q;
	char *unit;
	double num;
	int i;

	/* ensure NUL termination */
	buf[len] = '\0';

	/* handle data line by line */
	for (p = buf; (q = strchr(p, '\n')) != NULL; p = q + 1) {
		*q = '\0';
		/* move it to the line buffer */
		strcat(linebuf, p);

		/*
		 * parse trace output to get function duration like in
		 *
		 * # tracer: function_graph
		 * #
		 * # CPU  DURATION                  FUNCTION CALLS
		 * # |     |   |                     |   |   |   |
		 *  1) + 10.291 us   |  do_filp_open();
		 *  1)   4.889 us    |  do_filp_open();
		 *  1)   6.086 us    |  do_filp_open();
		 *
		 */
		if (linebuf[0] == '#')
			goto next;

		/* ignore CPU */
		p = strchr(linebuf, ')');
		if (p == NULL)
			p = linebuf;

		while (*p && !isdigit(*p) && (*p != '|'))
			p++;

		/* no duration */
		if (*p == '\0' || *p == '|')
			goto next;

		num = strtod(p, &unit);
		if (!unit || strncmp(unit, " us", 3))
			goto next;

		if (use_nsec)
			num *= 1000;

		i = log2(num);
		if (i < 0)
			i = 0;
		if (i >= NUM_BUCKET)
			i = NUM_BUCKET - 1;

		buckets[i]++;

next:
		/* empty the line buffer for the next output  */
		linebuf[0] = '\0';
	}

	/* preserve any remaining output (before newline) */
	strcat(linebuf, p);
}

static void display_histogram(int buckets[], bool use_nsec)
{
	int i;
	int total = 0;
	int bar_total = 46;  /* to fit in 80 column */
	char bar[] = "###############################################";
	int bar_len;

	for (i = 0; i < NUM_BUCKET; i++)
		total += buckets[i];

	if (total == 0) {
		printf("No data found\n");
		return;
	}

	printf("# %14s | %10s | %-*s |\n",
	       "  DURATION    ", "COUNT", bar_total, "GRAPH");

	bar_len = buckets[0] * bar_total / total;
	printf("  %4d - %-4d %s | %10d | %.*s%*s |\n",
	       0, 1, "us", buckets[0], bar_len, bar, bar_total - bar_len, "");

	for (i = 1; i < NUM_BUCKET - 1; i++) {
		int start = (1 << (i - 1));
		int stop = 1 << i;
		const char *unit = use_nsec ? "ns" : "us";

		if (start >= 1024) {
			start >>= 10;
			stop >>= 10;
			unit = use_nsec ? "us" : "ms";
		}
		bar_len = buckets[i] * bar_total / total;
		printf("  %4d - %-4d %s | %10d | %.*s%*s |\n",
		       start, stop, unit, buckets[i], bar_len, bar,
		       bar_total - bar_len, "");
	}

	bar_len = buckets[NUM_BUCKET - 1] * bar_total / total;
	printf("  %4d - %-4s %s | %10d | %.*s%*s |\n",
	       1, "...", use_nsec ? "ms" : " s", buckets[NUM_BUCKET - 1],
	       bar_len, bar, bar_total - bar_len, "");

}

static int prepare_func_latency(struct perf_ftrace *ftrace)
{
	char *trace_file;
	int fd;

	if (ftrace->target.use_bpf)
		return perf_ftrace__latency_prepare_bpf(ftrace);

	if (reset_tracing_files(ftrace) < 0) {
		pr_err("failed to reset ftrace\n");
		return -1;
	}

	/* reset ftrace buffer */
	if (write_tracing_file("trace", "0") < 0)
		return -1;

	if (set_tracing_options(ftrace) < 0)
		return -1;

	/* force to use the function_graph tracer to track duration */
	if (write_tracing_file("current_tracer", "function_graph") < 0) {
		pr_err("failed to set current_tracer to function_graph\n");
		return -1;
	}

	trace_file = get_tracing_file("trace_pipe");
	if (!trace_file) {
		pr_err("failed to open trace_pipe\n");
		return -1;
	}

	fd = open(trace_file, O_RDONLY);
	if (fd < 0)
		pr_err("failed to open trace_pipe\n");

	put_tracing_file(trace_file);
	return fd;
}

static int start_func_latency(struct perf_ftrace *ftrace)
{
	if (ftrace->target.use_bpf)
		return perf_ftrace__latency_start_bpf(ftrace);

	if (write_tracing_file("tracing_on", "1") < 0) {
		pr_err("can't enable tracing\n");
		return -1;
	}

	return 0;
}

static int stop_func_latency(struct perf_ftrace *ftrace)
{
	if (ftrace->target.use_bpf)
		return perf_ftrace__latency_stop_bpf(ftrace);

	write_tracing_file("tracing_on", "0");
	return 0;
}

static int read_func_latency(struct perf_ftrace *ftrace, int buckets[])
{
	if (ftrace->target.use_bpf)
		return perf_ftrace__latency_read_bpf(ftrace, buckets);

	return 0;
}

static int cleanup_func_latency(struct perf_ftrace *ftrace)
{
	if (ftrace->target.use_bpf)
		return perf_ftrace__latency_cleanup_bpf(ftrace);

	reset_tracing_files(ftrace);
	return 0;
}

static int __cmd_latency(struct perf_ftrace *ftrace)
{
	int trace_fd;
	char buf[4096];
	char line[256];
	struct pollfd pollfd = {
		.events = POLLIN,
	};
	int buckets[NUM_BUCKET] = { };

	if (!(perf_cap__capable(CAP_PERFMON) ||
	      perf_cap__capable(CAP_SYS_ADMIN))) {
		pr_err("ftrace only works for %s!\n",
#ifdef HAVE_LIBCAP_SUPPORT
		"users with the CAP_PERFMON or CAP_SYS_ADMIN capability"
#else
		"root"
#endif
		);
		return -1;
	}

	trace_fd = prepare_func_latency(ftrace);
	if (trace_fd < 0)
		goto out;

	fcntl(trace_fd, F_SETFL, O_NONBLOCK);
	pollfd.fd = trace_fd;

	if (start_func_latency(ftrace) < 0)
		goto out;

	evlist__start_workload(ftrace->evlist);

	line[0] = '\0';
	while (!done) {
		if (poll(&pollfd, 1, -1) < 0)
			break;

		if (pollfd.revents & POLLIN) {
			int n = read(trace_fd, buf, sizeof(buf) - 1);
			if (n < 0)
				break;

			make_histogram(buckets, buf, n, line, ftrace->use_nsec);
		}
	}

	stop_func_latency(ftrace);

	if (workload_exec_errno) {
		const char *emsg = str_error_r(workload_exec_errno, buf, sizeof(buf));
		pr_err("workload failed: %s\n", emsg);
		goto out;
	}

	/* read remaining buffer contents */
	while (!ftrace->target.use_bpf) {
		int n = read(trace_fd, buf, sizeof(buf) - 1);
		if (n <= 0)
			break;
		make_histogram(buckets, buf, n, line, ftrace->use_nsec);
	}

	read_func_latency(ftrace, buckets);

	display_histogram(buckets, ftrace->use_nsec);

out:
	close(trace_fd);
	cleanup_func_latency(ftrace);

	return (done && !workload_exec_errno) ? 0 : -1;
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

static void list_function_cb(char *str, void *arg)
{
	struct strfilter *filter = (struct strfilter *)arg;

	if (strfilter__compare(filter, str))
		printf("%s", str);
}

static int opt_list_avail_functions(const struct option *opt __maybe_unused,
				    const char *str, int unset)
{
	struct strfilter *filter;
	const char *err = NULL;
	int ret;

	if (unset || !str)
		return -1;

	filter = strfilter__new(str, &err);
	if (!filter)
		return err ? -EINVAL : -ENOMEM;

	ret = strfilter__or(filter, str, &err);
	if (ret == -EINVAL) {
		pr_err("Filter parse error at %td.\n", err - str + 1);
		pr_err("Source: \"%s\"\n", str);
		pr_err("         %*c\n", (int)(err - str + 1), '^');
		strfilter__delete(filter);
		return ret;
	}

	ret = read_tracing_file_by_line("available_filter_functions",
					list_function_cb, filter);
	strfilter__delete(filter);
	if (ret < 0)
		return ret;

	exit(0);
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

static int parse_buffer_size(const struct option *opt,
			     const char *str, int unset)
{
	unsigned long *s = (unsigned long *)opt->value;
	static struct parse_tag tags_size[] = {
		{ .tag  = 'B', .mult = 1       },
		{ .tag  = 'K', .mult = 1 << 10 },
		{ .tag  = 'M', .mult = 1 << 20 },
		{ .tag  = 'G', .mult = 1 << 30 },
		{ .tag  = 0 },
	};
	unsigned long val;

	if (unset) {
		*s = 0;
		return 0;
	}

	val = parse_tag_value(str, tags_size);
	if (val != (unsigned long) -1) {
		if (val < 1024) {
			pr_err("buffer size too small, must larger than 1KB.");
			return -1;
		}
		*s = val;
		return 0;
	}

	return -1;
}

static int parse_func_tracer_opts(const struct option *opt,
				  const char *str, int unset)
{
	int ret;
	struct perf_ftrace *ftrace = (struct perf_ftrace *) opt->value;
	struct sublevel_option func_tracer_opts[] = {
		{ .name = "call-graph",	.value_ptr = &ftrace->func_stack_trace },
		{ .name = "irq-info",	.value_ptr = &ftrace->func_irq_info },
		{ .name = NULL, }
	};

	if (unset)
		return 0;

	ret = perf_parse_sublevel_options(str, func_tracer_opts);
	if (ret)
		return ret;

	return 0;
}

static int parse_graph_tracer_opts(const struct option *opt,
				  const char *str, int unset)
{
	int ret;
	struct perf_ftrace *ftrace = (struct perf_ftrace *) opt->value;
	struct sublevel_option graph_tracer_opts[] = {
		{ .name = "nosleep-time",	.value_ptr = &ftrace->graph_nosleep_time },
		{ .name = "noirqs",		.value_ptr = &ftrace->graph_noirqs },
		{ .name = "verbose",		.value_ptr = &ftrace->graph_verbose },
		{ .name = "thresh",		.value_ptr = &ftrace->graph_thresh },
		{ .name = "depth",		.value_ptr = &ftrace->graph_depth },
		{ .name = NULL, }
	};

	if (unset)
		return 0;

	ret = perf_parse_sublevel_options(str, graph_tracer_opts);
	if (ret)
		return ret;

	return 0;
}

enum perf_ftrace_subcommand {
	PERF_FTRACE_NONE,
	PERF_FTRACE_TRACE,
	PERF_FTRACE_LATENCY,
};

int cmd_ftrace(int argc, const char **argv)
{
	int ret;
	int (*cmd_func)(struct perf_ftrace *) = NULL;
	struct perf_ftrace ftrace = {
		.tracer = DEFAULT_TRACER,
		.target = { .uid = UINT_MAX, },
	};
	const struct option common_options[] = {
	OPT_STRING('p', "pid", &ftrace.target.pid, "pid",
		   "Trace on existing process id"),
	/* TODO: Add short option -t after -t/--tracer can be removed. */
	OPT_STRING(0, "tid", &ftrace.target.tid, "tid",
		   "Trace on existing thread id (exclusive to --pid)"),
	OPT_INCR('v', "verbose", &verbose,
		 "Be more verbose"),
	OPT_BOOLEAN('a', "all-cpus", &ftrace.target.system_wide,
		    "System-wide collection from all CPUs"),
	OPT_STRING('C', "cpu", &ftrace.target.cpu_list, "cpu",
		    "List of cpus to monitor"),
	OPT_END()
	};
	const struct option ftrace_options[] = {
	OPT_STRING('t', "tracer", &ftrace.tracer, "tracer",
		   "Tracer to use: function_graph(default) or function"),
	OPT_CALLBACK_DEFAULT('F', "funcs", NULL, "[FILTER]",
			     "Show available functions to filter",
			     opt_list_avail_functions, "*"),
	OPT_CALLBACK('T', "trace-funcs", &ftrace.filters, "func",
		     "Trace given functions using function tracer",
		     parse_filter_func),
	OPT_CALLBACK('N', "notrace-funcs", &ftrace.notrace, "func",
		     "Do not trace given functions", parse_filter_func),
	OPT_CALLBACK(0, "func-opts", &ftrace, "options",
		     "Function tracer options, available options: call-graph,irq-info",
		     parse_func_tracer_opts),
	OPT_CALLBACK('G', "graph-funcs", &ftrace.graph_funcs, "func",
		     "Trace given functions using function_graph tracer",
		     parse_filter_func),
	OPT_CALLBACK('g', "nograph-funcs", &ftrace.nograph_funcs, "func",
		     "Set nograph filter on given functions", parse_filter_func),
	OPT_CALLBACK(0, "graph-opts", &ftrace, "options",
		     "Graph tracer options, available options: nosleep-time,noirqs,verbose,thresh=<n>,depth=<n>",
		     parse_graph_tracer_opts),
	OPT_CALLBACK('m', "buffer-size", &ftrace.percpu_buffer_size, "size",
		     "Size of per cpu buffer, needs to use a B, K, M or G suffix.", parse_buffer_size),
	OPT_BOOLEAN(0, "inherit", &ftrace.inherit,
		    "Trace children processes"),
	OPT_INTEGER('D', "delay", &ftrace.target.initial_delay,
		    "Number of milliseconds to wait before starting tracing after program start"),
	OPT_PARENT(common_options),
	};
	const struct option latency_options[] = {
	OPT_CALLBACK('T', "trace-funcs", &ftrace.filters, "func",
		     "Show latency of given function", parse_filter_func),
#ifdef HAVE_BPF_SKEL
	OPT_BOOLEAN('b', "use-bpf", &ftrace.target.use_bpf,
		    "Use BPF to measure function latency"),
#endif
	OPT_BOOLEAN('n', "use-nsec", &ftrace.use_nsec,
		    "Use nano-second histogram"),
	OPT_PARENT(common_options),
	};
	const struct option *options = ftrace_options;

	const char * const ftrace_usage[] = {
		"perf ftrace [<options>] [<command>]",
		"perf ftrace [<options>] -- [<command>] [<options>]",
		"perf ftrace {trace|latency} [<options>] [<command>]",
		"perf ftrace {trace|latency} [<options>] -- [<command>] [<options>]",
		NULL
	};
	enum perf_ftrace_subcommand subcmd = PERF_FTRACE_NONE;

	INIT_LIST_HEAD(&ftrace.filters);
	INIT_LIST_HEAD(&ftrace.notrace);
	INIT_LIST_HEAD(&ftrace.graph_funcs);
	INIT_LIST_HEAD(&ftrace.nograph_funcs);

	signal(SIGINT, sig_handler);
	signal(SIGUSR1, sig_handler);
	signal(SIGCHLD, sig_handler);
	signal(SIGPIPE, sig_handler);

	ret = perf_config(perf_ftrace_config, &ftrace);
	if (ret < 0)
		return -1;

	if (argc > 1) {
		if (!strcmp(argv[1], "trace")) {
			subcmd = PERF_FTRACE_TRACE;
		} else if (!strcmp(argv[1], "latency")) {
			subcmd = PERF_FTRACE_LATENCY;
			options = latency_options;
		}

		if (subcmd != PERF_FTRACE_NONE) {
			argc--;
			argv++;
		}
	}
	/* for backward compatibility */
	if (subcmd == PERF_FTRACE_NONE)
		subcmd = PERF_FTRACE_TRACE;

	argc = parse_options(argc, argv, options, ftrace_usage,
			    PARSE_OPT_STOP_AT_NON_OPTION);
	if (argc < 0) {
		ret = -EINVAL;
		goto out_delete_filters;
	}

	/* Make system wide (-a) the default target. */
	if (!argc && target__none(&ftrace.target))
		ftrace.target.system_wide = true;

	switch (subcmd) {
	case PERF_FTRACE_TRACE:
		cmd_func = __cmd_ftrace;
		break;
	case PERF_FTRACE_LATENCY:
		if (list_empty(&ftrace.filters)) {
			pr_err("Should provide a function to measure\n");
			parse_options_usage(ftrace_usage, options, "T", 1);
			ret = -EINVAL;
			goto out_delete_filters;
		}
		cmd_func = __cmd_latency;
		break;
	case PERF_FTRACE_NONE:
	default:
		pr_err("Invalid subcommand\n");
		ret = -EINVAL;
		goto out_delete_filters;
	}

	ret = target__validate(&ftrace.target);
	if (ret) {
		char errbuf[512];

		target__strerror(&ftrace.target, ret, errbuf, 512);
		pr_err("%s\n", errbuf);
		goto out_delete_filters;
	}

	ftrace.evlist = evlist__new();
	if (ftrace.evlist == NULL) {
		ret = -ENOMEM;
		goto out_delete_filters;
	}

	ret = evlist__create_maps(ftrace.evlist, &ftrace.target);
	if (ret < 0)
		goto out_delete_evlist;

	if (argc) {
		ret = evlist__prepare_workload(ftrace.evlist, &ftrace.target,
					       argv, false,
					       ftrace__workload_exec_failed_signal);
		if (ret < 0)
			goto out_delete_evlist;
	}

	ret = cmd_func(&ftrace);

out_delete_evlist:
	evlist__delete(ftrace.evlist);

out_delete_filters:
	delete_filter_func(&ftrace.filters);
	delete_filter_func(&ftrace.notrace);
	delete_filter_func(&ftrace.graph_funcs);
	delete_filter_func(&ftrace.nograph_funcs);

	return ret;
}
