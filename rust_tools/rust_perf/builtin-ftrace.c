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
#include <inttypes.h>
#include <math.h>
#include <poll.h>
#include <ctype.h>
#include <linux/capability.h>
#include <linux/string.h>
#include <sys/stat.h>

#include "debug.h"
#include <subcmd/pager.h>
#include <subcmd/parse-options.h>
#include <api/io.h>
#include <api/fs/tracing_path.h>
#include "evlist.h"
#include "target.h"
#include "cpumap.h"
#include "hashmap.h"
#include "thread_map.h"
#include "strfilter.h"
#include "util/cap.h"
#include "util/config.h"
#include "util/ftrace.h"
#include "util/stat.h"
#include "util/units.h"
#include "util/parse-sublevel-options.h"

#define DEFAULT_TRACER  "function_graph"

static volatile sig_atomic_t workload_exec_errno;
static volatile sig_atomic_t done;

static struct stats latency_stats;  /* for tracepoints */

static char tracing_instance[PATH_MAX];	/* Trace instance directory */

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

static bool check_ftrace_capable(void)
{
	bool used_root;

	if (perf_cap__capable(CAP_PERFMON, &used_root))
		return true;

	if (!used_root && perf_cap__capable(CAP_SYS_ADMIN, &used_root))
		return true;

	pr_err("ftrace only works for %s!\n",
		used_root ? "root"
			  : "users with the CAP_PERFMON or CAP_SYS_ADMIN capability"
		);
	return false;
}

static bool is_ftrace_supported(void)
{
	char *file;
	bool supported = false;

	file = get_tracing_file("set_ftrace_pid");
	if (!file) {
		pr_debug("cannot get tracing file set_ftrace_pid\n");
		return false;
	}

	if (!access(file, F_OK))
		supported = true;

	put_tracing_file(file);
	return supported;
}

/*
 * Wrapper to test if a file in directory .../tracing/instances/XXX
 * exists. If so return the .../tracing/instances/XXX file for use.
 * Otherwise the file exists only in directory .../tracing and
 * is applicable to all instances, for example file available_filter_functions.
 * Return that file name in this case.
 *
 * This functions works similar to get_tracing_file() and expects its caller
 * to free the returned file name.
 *
 * The global variable tracing_instance is set in init_tracing_instance()
 * called at the  beginning to a process specific tracing subdirectory.
 */
static char *get_tracing_instance_file(const char *name)
{
	char *file;

	if (asprintf(&file, "%s/%s", tracing_instance, name) < 0)
		return NULL;

	if (!access(file, F_OK))
		return file;

	free(file);
	file = get_tracing_file(name);
	return file;
}

static int __write_tracing_file(const char *name, const char *val, bool append)
{
	char *file;
	int fd, ret = -1;
	ssize_t size = strlen(val);
	int flags = O_WRONLY;
	char errbuf[512];
	char *val_copy;

	file = get_tracing_instance_file(name);
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

	file = get_tracing_instance_file(name);
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

	file = get_tracing_instance_file(name);
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
	write_tracing_option_file("funcgraph-tail", "0");
	write_tracing_option_file("funcgraph-args", "0");
	write_tracing_option_file("funcgraph-retval", "0");
	write_tracing_option_file("funcgraph-retval-hex", "0");
	write_tracing_option_file("funcgraph-retaddr", "0");
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

/* Remove .../tracing/instances/XXX subdirectory created with
 * init_tracing_instance().
 */
static void exit_tracing_instance(void)
{
	if (rmdir(tracing_instance))
		pr_err("failed to delete tracing/instances directory\n");
}

/* Create subdirectory within .../tracing/instances/XXX to have session
 * or process specific setup. To delete this setup, simply remove the
 * subdirectory.
 */
static int init_tracing_instance(void)
{
	char dirname[] = "instances/perf-ftrace-XXXXXX";
	char *path;

	path = get_tracing_file(dirname);
	if (!path)
		goto error;
	strncpy(tracing_instance, path, sizeof(tracing_instance) - 1);
	put_tracing_file(path);
	path = mkdtemp(tracing_instance);
	if (!path)
		goto error;
	return 0;

error:
	pr_err("failed to create tracing/instances directory\n");
	return -1;
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
	struct perf_cpu_map *cpumap = perf_cpu_map__new_online_cpus();
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

static int set_tracing_funcgraph_args(struct perf_ftrace *ftrace)
{
	if (ftrace->graph_args) {
		if (write_tracing_option_file("funcgraph-args", "1") < 0)
			return -1;
	}

	return 0;
}

static int set_tracing_funcgraph_retval(struct perf_ftrace *ftrace)
{
	if (ftrace->graph_retval || ftrace->graph_retval_hex) {
		if (write_tracing_option_file("funcgraph-retval", "1") < 0)
			return -1;
	}

	if (ftrace->graph_retval_hex) {
		if (write_tracing_option_file("funcgraph-retval-hex", "1") < 0)
			return -1;
	}

	return 0;
}

static int set_tracing_funcgraph_retaddr(struct perf_ftrace *ftrace)
{
	if (ftrace->graph_retaddr) {
		if (write_tracing_option_file("funcgraph-retaddr", "1") < 0)
			return -1;
	}

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

static int set_tracing_funcgraph_tail(struct perf_ftrace *ftrace)
{
	if (!ftrace->graph_tail)
		return 0;

	if (write_tracing_option_file("funcgraph-tail", "1") < 0)
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

	if (set_tracing_funcgraph_args(ftrace) < 0) {
		pr_err("failed to set tracing option funcgraph-args\n");
		return -1;
	}

	if (set_tracing_funcgraph_retval(ftrace) < 0) {
		pr_err("failed to set tracing option funcgraph-retval\n");
		return -1;
	}

	if (set_tracing_funcgraph_retaddr(ftrace) < 0) {
		pr_err("failed to set tracing option funcgraph-retaddr\n");
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

	if (set_tracing_funcgraph_tail(ftrace) < 0) {
		pr_err("failed to set tracing option funcgraph-tail\n");
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

	select_tracer(ftrace);

	if (init_tracing_instance() < 0)
		goto out;

	if (reset_tracing_files(ftrace) < 0) {
		pr_err("failed to reset ftrace\n");
		goto out_reset;
	}

	/* reset ftrace buffer */
	if (write_tracing_file("trace", "0") < 0)
		goto out_reset;

	if (set_tracing_options(ftrace) < 0)
		goto out_reset;

	if (write_tracing_file("current_tracer", ftrace->tracer) < 0) {
		pr_err("failed to set current_tracer to %s\n", ftrace->tracer);
		goto out_reset;
	}

	setup_pager();

	trace_file = get_tracing_instance_file("trace_pipe");
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
			/* flush output since stdout is in full buffering mode due to pager */
			fflush(stdout);
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
	exit_tracing_instance();
out:
	return (done && !workload_exec_errno) ? 0 : -1;
}

static void make_histogram(struct perf_ftrace *ftrace, int buckets[],
			   char *buf, size_t len, char *linebuf)
{
	int min_latency = ftrace->min_latency;
	int max_latency = ftrace->max_latency;
	unsigned int bucket_num = ftrace->bucket_num;
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

		if (ftrace->use_nsec)
			num *= 1000;

		i = 0;
		if (num < min_latency)
			goto do_inc;

		num -= min_latency;

		if (!ftrace->bucket_range) {
			i = log2(num);
			if (i < 0)
				i = 0;
		} else {
			// Less than 1 unit (ms or ns), or, in the future,
			// than the min latency desired.
			if (num > 0) // 1st entry: [ 1 unit .. bucket_range units ]
				i = num / ftrace->bucket_range + 1;
			if (num >= max_latency - min_latency)
				i = bucket_num -1;
		}
		if ((unsigned)i >= bucket_num)
			i = bucket_num - 1;

		num += min_latency;
do_inc:
		buckets[i]++;
		update_stats(&latency_stats, num);

next:
		/* empty the line buffer for the next output  */
		linebuf[0] = '\0';
	}

	/* preserve any remaining output (before newline) */
	strcat(linebuf, p);
}

static void display_histogram(struct perf_ftrace *ftrace, int buckets[])
{
	int min_latency = ftrace->min_latency;
	bool use_nsec = ftrace->use_nsec;
	unsigned int bucket_num = ftrace->bucket_num;
	unsigned int i;
	int total = 0;
	int bar_total = 46;  /* to fit in 80 column */
	char bar[] = "###############################################";
	int bar_len;

	for (i = 0; i < bucket_num; i++)
		total += buckets[i];

	if (total == 0) {
		printf("No data found\n");
		return;
	}

	printf("# %14s | %10s | %-*s |\n",
	       "  DURATION    ", "COUNT", bar_total, "GRAPH");

	bar_len = buckets[0] * bar_total / total;

	if (!ftrace->hide_empty || buckets[0])
		printf("  %4d - %4d %s | %10d | %.*s%*s |\n",
		       0, min_latency ?: 1, use_nsec ? "ns" : "us",
		       buckets[0], bar_len, bar, bar_total - bar_len, "");

	for (i = 1; i < bucket_num - 1; i++) {
		unsigned int start, stop;
		const char *unit = use_nsec ? "ns" : "us";

		if (ftrace->hide_empty && !buckets[i])
			continue;
		if (!ftrace->bucket_range) {
			start = (1 << (i - 1));
			stop  = 1 << i;

			if (start >= 1024) {
				start >>= 10;
				stop >>= 10;
				unit = use_nsec ? "us" : "ms";
			}
		} else {
			start = (i - 1) * ftrace->bucket_range + min_latency;
			stop  = i * ftrace->bucket_range + min_latency;

			if (start >= ftrace->max_latency)
				break;
			if (stop > ftrace->max_latency)
				stop = ftrace->max_latency;

			if (start >= 1000) {
				double dstart = start / 1000.0,
				       dstop  = stop / 1000.0;
				printf("  %4.2f - %-4.2f", dstart, dstop);
				unit = use_nsec ? "us" : "ms";
				goto print_bucket_info;
			}
		}

		printf("  %4d - %4d", start, stop);
print_bucket_info:
		bar_len = buckets[i] * bar_total / total;
		printf(" %s | %10d | %.*s%*s |\n", unit, buckets[i], bar_len, bar,
		       bar_total - bar_len, "");
	}

	bar_len = buckets[bucket_num - 1] * bar_total / total;
	if (ftrace->hide_empty && !buckets[bucket_num - 1])
		goto print_stats;
	if (!ftrace->bucket_range) {
		printf("  %4d - %-4s %s", 1, "...", use_nsec ? "ms" : "s ");
	} else {
		unsigned int upper_outlier = (bucket_num - 2) * ftrace->bucket_range + min_latency;
		if (upper_outlier > ftrace->max_latency)
			upper_outlier = ftrace->max_latency;

		if (upper_outlier >= 1000) {
			double dstart = upper_outlier / 1000.0;

			printf("  %4.2f - %-4s %s", dstart, "...", use_nsec ? "us" : "ms");
		} else {
			printf("  %4d - %4s %s", upper_outlier, "...", use_nsec ? "ns" : "us");
		}
	}
	printf(" | %10d | %.*s%*s |\n", buckets[bucket_num - 1],
	       bar_len, bar, bar_total - bar_len, "");

print_stats:
	printf("\n# statistics  (in %s)\n", ftrace->use_nsec ? "nsec" : "usec");
	printf("  total time: %20.0f\n", latency_stats.mean * latency_stats.n);
	printf("    avg time: %20.0f\n", latency_stats.mean);
	printf("    max time: %20"PRIu64"\n", latency_stats.max);
	printf("    min time: %20"PRIu64"\n", latency_stats.min);
	printf("       count: %20.0f\n", latency_stats.n);
}

static int prepare_func_latency(struct perf_ftrace *ftrace)
{
	char *trace_file;
	int fd;

	if (ftrace->target.use_bpf)
		return perf_ftrace__latency_prepare_bpf(ftrace);

	if (init_tracing_instance() < 0)
		return -1;

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

	trace_file = get_tracing_instance_file("trace_pipe");
	if (!trace_file) {
		pr_err("failed to open trace_pipe\n");
		return -1;
	}

	fd = open(trace_file, O_RDONLY);
	if (fd < 0)
		pr_err("failed to open trace_pipe\n");

	init_stats(&latency_stats);

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
		return perf_ftrace__latency_read_bpf(ftrace, buckets, &latency_stats);

	return 0;
}

static int cleanup_func_latency(struct perf_ftrace *ftrace)
{
	if (ftrace->target.use_bpf)
		return perf_ftrace__latency_cleanup_bpf(ftrace);

	exit_tracing_instance();
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
	int *buckets;

	trace_fd = prepare_func_latency(ftrace);
	if (trace_fd < 0)
		goto out;

	fcntl(trace_fd, F_SETFL, O_NONBLOCK);
	pollfd.fd = trace_fd;

	if (start_func_latency(ftrace) < 0)
		goto out;

	evlist__start_workload(ftrace->evlist);

	buckets = calloc(ftrace->bucket_num, sizeof(*buckets));
	if (buckets == NULL) {
		pr_err("failed to allocate memory for the buckets\n");
		goto out;
	}

	line[0] = '\0';
	while (!done) {
		if (poll(&pollfd, 1, -1) < 0)
			break;

		if (pollfd.revents & POLLIN) {
			int n = read(trace_fd, buf, sizeof(buf) - 1);
			if (n < 0)
				break;

			make_histogram(ftrace, buckets, buf, n, line);
		}
	}

	stop_func_latency(ftrace);

	if (workload_exec_errno) {
		const char *emsg = str_error_r(workload_exec_errno, buf, sizeof(buf));
		pr_err("workload failed: %s\n", emsg);
		goto out_free_buckets;
	}

	/* read remaining buffer contents */
	while (!ftrace->target.use_bpf) {
		int n = read(trace_fd, buf, sizeof(buf) - 1);
		if (n <= 0)
			break;
		make_histogram(ftrace, buckets, buf, n, line);
	}

	read_func_latency(ftrace, buckets);

	display_histogram(ftrace, buckets);

out_free_buckets:
	free(buckets);
out:
	close(trace_fd);
	cleanup_func_latency(ftrace);

	return (done && !workload_exec_errno) ? 0 : -1;
}

static size_t profile_hash(long func, void *ctx __maybe_unused)
{
	return str_hash((char *)func);
}

static bool profile_equal(long func1, long func2, void *ctx __maybe_unused)
{
	return !strcmp((char *)func1, (char *)func2);
}

static int prepare_func_profile(struct perf_ftrace *ftrace)
{
	ftrace->tracer = "function_graph";
	ftrace->graph_tail = 1;
	ftrace->graph_verbose = 0;

	ftrace->profile_hash = hashmap__new(profile_hash, profile_equal, NULL);
	if (ftrace->profile_hash == NULL)
		return -ENOMEM;

	return 0;
}

/* This is saved in a hashmap keyed by the function name */
struct ftrace_profile_data {
	struct stats st;
};

static int add_func_duration(struct perf_ftrace *ftrace, char *func, double time_ns)
{
	struct ftrace_profile_data *prof = NULL;

	if (!hashmap__find(ftrace->profile_hash, func, &prof)) {
		char *key = strdup(func);

		if (key == NULL)
			return -ENOMEM;

		prof = zalloc(sizeof(*prof));
		if (prof == NULL) {
			free(key);
			return -ENOMEM;
		}

		init_stats(&prof->st);
		hashmap__add(ftrace->profile_hash, key, prof);
	}

	update_stats(&prof->st, time_ns);
	return 0;
}

/*
 * The ftrace function_graph text output normally looks like below:
 *
 * CPU   DURATION       FUNCTION
 *
 *  0)               |  syscall_trace_enter.isra.0() {
 *  0)               |    __audit_syscall_entry() {
 *  0)               |      auditd_test_task() {
 *  0)   0.271 us    |        __rcu_read_lock();
 *  0)   0.275 us    |        __rcu_read_unlock();
 *  0)   1.254 us    |      } /\* auditd_test_task *\/
 *  0)   0.279 us    |      ktime_get_coarse_real_ts64();
 *  0)   2.227 us    |    } /\* __audit_syscall_entry *\/
 *  0)   2.713 us    |  } /\* syscall_trace_enter.isra.0 *\/
 *
 *  Parse the line and get the duration and function name.
 */
static int parse_func_duration(struct perf_ftrace *ftrace, char *line, size_t len)
{
	char *p;
	char *func;
	double duration;

	/* skip CPU */
	p = strchr(line, ')');
	if (p == NULL)
		return 0;

	/* get duration */
	p = skip_spaces(p + 1);

	/* no duration? */
	if (p == NULL || *p == '|')
		return 0;

	/* skip markers like '*' or '!' for longer than ms */
	if (!isdigit(*p))
		p++;

	duration = strtod(p, &p);

	if (strncmp(p, " us", 3)) {
		pr_debug("non-usec time found.. ignoring\n");
		return 0;
	}

	/*
	 * profile stat keeps the max and min values as integer,
	 * convert to nsec time so that we can have accurate max.
	 */
	duration *= 1000;

	/* skip to the pipe */
	while (p < line + len && *p != '|')
		p++;

	if (*p++ != '|')
		return -EINVAL;

	/* get function name */
	func = skip_spaces(p);

	/* skip the closing bracket and the start of comment */
	if (*func == '}')
		func += 5;

	/* remove semi-colon or end of comment at the end */
	p = line + len - 1;
	while (!isalnum(*p) && *p != ']') {
		*p = '\0';
		--p;
	}

	return add_func_duration(ftrace, func, duration);
}

enum perf_ftrace_profile_sort_key {
	PFP_SORT_TOTAL = 0,
	PFP_SORT_AVG,
	PFP_SORT_MAX,
	PFP_SORT_COUNT,
	PFP_SORT_NAME,
};

static enum perf_ftrace_profile_sort_key profile_sort = PFP_SORT_TOTAL;

static int cmp_profile_data(const void *a, const void *b)
{
	const struct hashmap_entry *e1 = *(const struct hashmap_entry **)a;
	const struct hashmap_entry *e2 = *(const struct hashmap_entry **)b;
	struct ftrace_profile_data *p1 = e1->pvalue;
	struct ftrace_profile_data *p2 = e2->pvalue;
	double v1, v2;

	switch (profile_sort) {
	case PFP_SORT_NAME:
		return strcmp(e1->pkey, e2->pkey);
	case PFP_SORT_AVG:
		v1 = p1->st.mean;
		v2 = p2->st.mean;
		break;
	case PFP_SORT_MAX:
		v1 = p1->st.max;
		v2 = p2->st.max;
		break;
	case PFP_SORT_COUNT:
		v1 = p1->st.n;
		v2 = p2->st.n;
		break;
	case PFP_SORT_TOTAL:
	default:
		v1 = p1->st.n * p1->st.mean;
		v2 = p2->st.n * p2->st.mean;
		break;
	}

	if (v1 > v2)
		return -1;
	if (v1 < v2)
		return 1;
	return 0;
}

static void print_profile_result(struct perf_ftrace *ftrace)
{
	struct hashmap_entry *entry, **profile;
	size_t i, nr, bkt;

	nr = hashmap__size(ftrace->profile_hash);
	if (nr == 0)
		return;

	profile = calloc(nr, sizeof(*profile));
	if (profile == NULL) {
		pr_err("failed to allocate memory for the result\n");
		return;
	}

	i = 0;
	hashmap__for_each_entry(ftrace->profile_hash, entry, bkt)
		profile[i++] = entry;

	assert(i == nr);

	//cmp_profile_data(profile[0], profile[1]);
	qsort(profile, nr, sizeof(*profile), cmp_profile_data);

	printf("# %10s %10s %10s %10s   %s\n",
	       "Total (us)", "Avg (us)", "Max (us)", "Count", "Function");

	for (i = 0; i < nr; i++) {
		const char *name = profile[i]->pkey;
		struct ftrace_profile_data *p = profile[i]->pvalue;

		printf("%12.3f %10.3f %6"PRIu64".%03"PRIu64" %10.0f   %s\n",
		       p->st.n * p->st.mean / 1000, p->st.mean / 1000,
		       p->st.max / 1000, p->st.max % 1000, p->st.n, name);
	}

	free(profile);

	hashmap__for_each_entry(ftrace->profile_hash, entry, bkt) {
		free((char *)entry->pkey);
		free(entry->pvalue);
	}

	hashmap__free(ftrace->profile_hash);
	ftrace->profile_hash = NULL;
}

static int __cmd_profile(struct perf_ftrace *ftrace)
{
	char *trace_file;
	int trace_fd;
	char buf[4096];
	struct io io;
	char *line = NULL;
	size_t line_len = 0;

	if (prepare_func_profile(ftrace) < 0) {
		pr_err("failed to prepare func profiler\n");
		goto out;
	}

	if (init_tracing_instance() < 0)
		goto out;

	if (reset_tracing_files(ftrace) < 0) {
		pr_err("failed to reset ftrace\n");
		goto out_reset;
	}

	/* reset ftrace buffer */
	if (write_tracing_file("trace", "0") < 0)
		goto out_reset;

	if (set_tracing_options(ftrace) < 0)
		goto out_reset;

	if (write_tracing_file("current_tracer", ftrace->tracer) < 0) {
		pr_err("failed to set current_tracer to %s\n", ftrace->tracer);
		goto out_reset;
	}

	setup_pager();

	trace_file = get_tracing_instance_file("trace_pipe");
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

	if (write_tracing_file("tracing_on", "1") < 0) {
		pr_err("can't enable tracing\n");
		goto out_close_fd;
	}

	evlist__start_workload(ftrace->evlist);

	io__init(&io, trace_fd, buf, sizeof(buf));
	io.timeout_ms = -1;

	while (!done && !io.eof) {
		if (io__getline(&io, &line, &line_len) < 0)
			break;

		if (parse_func_duration(ftrace, line, line_len) < 0)
			break;
	}

	write_tracing_file("tracing_on", "0");

	if (workload_exec_errno) {
		const char *emsg = str_error_r(workload_exec_errno, buf, sizeof(buf));
		/* flush stdout first so below error msg appears at the end. */
		fflush(stdout);
		pr_err("workload failed: %s\n", emsg);
		goto out_free_line;
	}

	/* read remaining buffer contents */
	io.timeout_ms = 0;
	while (!io.eof) {
		if (io__getline(&io, &line, &line_len) < 0)
			break;

		if (parse_func_duration(ftrace, line, line_len) < 0)
			break;
	}

	print_profile_result(ftrace);

out_free_line:
	free(line);
out_close_fd:
	close(trace_fd);
out_reset:
	exit_tracing_instance();
out:
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

static int parse_filter_event(const struct option *opt, const char *str,
			     int unset __maybe_unused)
{
	struct list_head *head = opt->value;
	struct filter_entry *entry;
	char *s, *p;
	int ret = -ENOMEM;

	s = strdup(str);
	if (s == NULL)
		return -ENOMEM;

	while ((p = strsep(&s, ",")) != NULL) {
		entry = malloc(sizeof(*entry) + strlen(p) + 1);
		if (entry == NULL)
			goto out;

		strcpy(entry->name, p);
		list_add_tail(&entry->list, head);
	}
	ret = 0;

out:
	free(s);
	return ret;
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
		{ .name = "args",		.value_ptr = &ftrace->graph_args },
		{ .name = "retval",		.value_ptr = &ftrace->graph_retval },
		{ .name = "retval-hex",		.value_ptr = &ftrace->graph_retval_hex },
		{ .name = "retaddr",		.value_ptr = &ftrace->graph_retaddr },
		{ .name = "nosleep-time",	.value_ptr = &ftrace->graph_nosleep_time },
		{ .name = "noirqs",		.value_ptr = &ftrace->graph_noirqs },
		{ .name = "verbose",		.value_ptr = &ftrace->graph_verbose },
		{ .name = "thresh",		.value_ptr = &ftrace->graph_thresh },
		{ .name = "depth",		.value_ptr = &ftrace->graph_depth },
		{ .name = "tail",		.value_ptr = &ftrace->graph_tail },
		{ .name = NULL, }
	};

	if (unset)
		return 0;

	ret = perf_parse_sublevel_options(str, graph_tracer_opts);
	if (ret)
		return ret;

	return 0;
}

static int parse_sort_key(const struct option *opt, const char *str, int unset)
{
	enum perf_ftrace_profile_sort_key *key = (void *)opt->value;

	if (unset)
		return 0;

	if (!strcmp(str, "total"))
		*key = PFP_SORT_TOTAL;
	else if (!strcmp(str, "avg"))
		*key = PFP_SORT_AVG;
	else if (!strcmp(str, "max"))
		*key = PFP_SORT_MAX;
	else if (!strcmp(str, "count"))
		*key = PFP_SORT_COUNT;
	else if (!strcmp(str, "name"))
		*key = PFP_SORT_NAME;
	else {
		pr_err("Unknown sort key: %s\n", str);
		return -1;
	}
	return 0;
}

enum perf_ftrace_subcommand {
	PERF_FTRACE_NONE,
	PERF_FTRACE_TRACE,
	PERF_FTRACE_LATENCY,
	PERF_FTRACE_PROFILE,
};

int cmd_ftrace(int argc, const char **argv)
{
	int ret;
	int (*cmd_func)(struct perf_ftrace *) = NULL;
	struct perf_ftrace ftrace = {
		.tracer = DEFAULT_TRACER,
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
		     "Graph tracer options, available options: args,retval,retval-hex,retaddr,nosleep-time,noirqs,verbose,thresh=<n>,depth=<n>",
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
	OPT_CALLBACK('e', "events", &ftrace.event_pair, "event1,event2",
		     "Show latency between the two events", parse_filter_event),
#ifdef HAVE_BPF_SKEL
	OPT_BOOLEAN('b', "use-bpf", &ftrace.target.use_bpf,
		    "Use BPF to measure function latency"),
#endif
	OPT_BOOLEAN('n', "use-nsec", &ftrace.use_nsec,
		    "Use nano-second histogram"),
	OPT_UINTEGER(0, "bucket-range", &ftrace.bucket_range,
		    "Bucket range in ms or ns (-n/--use-nsec), default is log2() mode"),
	OPT_UINTEGER(0, "min-latency", &ftrace.min_latency,
		    "Minimum latency (1st bucket). Works only with --bucket-range."),
	OPT_UINTEGER(0, "max-latency", &ftrace.max_latency,
		    "Maximum latency (last bucket). Works only with --bucket-range."),
	OPT_BOOLEAN(0, "hide-empty", &ftrace.hide_empty,
		    "Hide empty buckets in the histogram"),
	OPT_PARENT(common_options),
	};
	const struct option profile_options[] = {
	OPT_CALLBACK('T', "trace-funcs", &ftrace.filters, "func",
		     "Trace given functions using function tracer",
		     parse_filter_func),
	OPT_CALLBACK('N', "notrace-funcs", &ftrace.notrace, "func",
		     "Do not trace given functions", parse_filter_func),
	OPT_CALLBACK('G', "graph-funcs", &ftrace.graph_funcs, "func",
		     "Trace given functions using function_graph tracer",
		     parse_filter_func),
	OPT_CALLBACK('g', "nograph-funcs", &ftrace.nograph_funcs, "func",
		     "Set nograph filter on given functions", parse_filter_func),
	OPT_CALLBACK('m', "buffer-size", &ftrace.percpu_buffer_size, "size",
		     "Size of per cpu buffer, needs to use a B, K, M or G suffix.", parse_buffer_size),
	OPT_CALLBACK('s', "sort", &profile_sort, "key",
		     "Sort result by key: total (default), avg, max, count, name.",
		     parse_sort_key),
	OPT_CALLBACK(0, "graph-opts", &ftrace, "options",
		     "Graph tracer options, available options: nosleep-time,noirqs,thresh=<n>,depth=<n>",
		     parse_graph_tracer_opts),
	OPT_PARENT(common_options),
	};
	const struct option *options = ftrace_options;

	const char * const ftrace_usage[] = {
		"perf ftrace [<options>] [<command>]",
		"perf ftrace [<options>] -- [<command>] [<options>]",
		"perf ftrace {trace|latency|profile} [<options>] [<command>]",
		"perf ftrace {trace|latency|profile} [<options>] -- [<command>] [<options>]",
		NULL
	};
	enum perf_ftrace_subcommand subcmd = PERF_FTRACE_NONE;

	INIT_LIST_HEAD(&ftrace.filters);
	INIT_LIST_HEAD(&ftrace.notrace);
	INIT_LIST_HEAD(&ftrace.graph_funcs);
	INIT_LIST_HEAD(&ftrace.nograph_funcs);
	INIT_LIST_HEAD(&ftrace.event_pair);

	signal(SIGINT, sig_handler);
	signal(SIGUSR1, sig_handler);
	signal(SIGCHLD, sig_handler);
	signal(SIGPIPE, sig_handler);

	if (!check_ftrace_capable())
		return -1;

	if (!is_ftrace_supported()) {
		pr_err("ftrace is not supported on this system\n");
		return -ENOTSUP;
	}

	ret = perf_config(perf_ftrace_config, &ftrace);
	if (ret < 0)
		return -1;

	if (argc > 1) {
		if (!strcmp(argv[1], "trace")) {
			subcmd = PERF_FTRACE_TRACE;
		} else if (!strcmp(argv[1], "latency")) {
			subcmd = PERF_FTRACE_LATENCY;
			options = latency_options;
		} else if (!strcmp(argv[1], "profile")) {
			subcmd = PERF_FTRACE_PROFILE;
			options = profile_options;
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
		if (list_empty(&ftrace.filters) && list_empty(&ftrace.event_pair)) {
			pr_err("Should provide a function or events to measure\n");
			parse_options_usage(ftrace_usage, options, "T", 1);
			parse_options_usage(NULL, options, "e", 1);
			ret = -EINVAL;
			goto out_delete_filters;
		}
		if (!list_empty(&ftrace.filters) && !list_empty(&ftrace.event_pair)) {
			pr_err("Please specify either of function or events\n");
			parse_options_usage(ftrace_usage, options, "T", 1);
			parse_options_usage(NULL, options, "e", 1);
			ret = -EINVAL;
			goto out_delete_filters;
		}
		if (!list_empty(&ftrace.event_pair) && !ftrace.target.use_bpf) {
			pr_err("Event processing needs BPF\n");
			parse_options_usage(ftrace_usage, options, "b", 1);
			parse_options_usage(NULL, options, "e", 1);
			ret = -EINVAL;
			goto out_delete_filters;
		}
		if (!ftrace.bucket_range && ftrace.min_latency) {
			pr_err("--min-latency works only with --bucket-range\n");
			parse_options_usage(ftrace_usage, options,
					    "min-latency", /*short_opt=*/false);
			ret = -EINVAL;
			goto out_delete_filters;
		}
		if (ftrace.bucket_range && !ftrace.min_latency) {
			/* default min latency should be the bucket range */
			ftrace.min_latency = ftrace.bucket_range;
		}
		if (!ftrace.bucket_range && ftrace.max_latency) {
			pr_err("--max-latency works only with --bucket-range\n");
			parse_options_usage(ftrace_usage, options,
					    "max-latency", /*short_opt=*/false);
			ret = -EINVAL;
			goto out_delete_filters;
		}
		if (ftrace.bucket_range && ftrace.max_latency &&
		    ftrace.max_latency < ftrace.min_latency + ftrace.bucket_range) {
			/* we need at least 1 bucket excluding min and max buckets */
			pr_err("--max-latency must be larger than min-latency + bucket-range\n");
			parse_options_usage(ftrace_usage, options,
					    "max-latency", /*short_opt=*/false);
			ret = -EINVAL;
			goto out_delete_filters;
		}
		/* set default unless max_latency is set and valid */
		ftrace.bucket_num = NUM_BUCKET;
		if (ftrace.bucket_range) {
			if (ftrace.max_latency)
				ftrace.bucket_num = (ftrace.max_latency - ftrace.min_latency) /
							ftrace.bucket_range + 2;
			else
				/* default max latency should depend on bucket range and num_buckets */
				ftrace.max_latency = (NUM_BUCKET - 2) * ftrace.bucket_range +
							ftrace.min_latency;
		}
		cmd_func = __cmd_latency;
		break;
	case PERF_FTRACE_PROFILE:
		cmd_func = __cmd_profile;
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
	delete_filter_func(&ftrace.event_pair);

	return ret;
}
