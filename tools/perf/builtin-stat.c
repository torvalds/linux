/*
 * builtin-stat.c
 *
 * Builtin stat command: Give a precise performance counters summary
 * overview about any workload, CPU or specific PID.
 *
 * Sample output:

   $ perf stat ./hackbench 10

  Time: 0.118

  Performance counter stats for './hackbench 10':

       1708.761321 task-clock                #   11.037 CPUs utilized
            41,190 context-switches          #    0.024 M/sec
             6,735 CPU-migrations            #    0.004 M/sec
            17,318 page-faults               #    0.010 M/sec
     5,205,202,243 cycles                    #    3.046 GHz
     3,856,436,920 stalled-cycles-frontend   #   74.09% frontend cycles idle
     1,600,790,871 stalled-cycles-backend    #   30.75% backend  cycles idle
     2,603,501,247 instructions              #    0.50  insns per cycle
                                             #    1.48  stalled cycles per insn
       484,357,498 branches                  #  283.455 M/sec
         6,388,934 branch-misses             #    1.32% of all branches

        0.154822978  seconds time elapsed

 *
 * Copyright (C) 2008-2011, Red Hat Inc, Ingo Molnar <mingo@redhat.com>
 *
 * Improvements and fixes by:
 *
 *   Arjan van de Ven <arjan@linux.intel.com>
 *   Yanmin Zhang <yanmin.zhang@intel.com>
 *   Wu Fengguang <fengguang.wu@intel.com>
 *   Mike Galbraith <efault@gmx.de>
 *   Paul Mackerras <paulus@samba.org>
 *   Jaswinder Singh Rajput <jaswinder@kernel.org>
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

#include "perf.h"
#include "builtin.h"
#include "util/cgroup.h"
#include "util/util.h"
#include <subcmd/parse-options.h>
#include "util/parse-events.h"
#include "util/pmu.h"
#include "util/event.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include "util/debug.h"
#include "util/drv_configs.h"
#include "util/color.h"
#include "util/stat.h"
#include "util/header.h"
#include "util/cpumap.h"
#include "util/thread.h"
#include "util/thread_map.h"
#include "util/counts.h"
#include "util/group.h"
#include "util/session.h"
#include "util/tool.h"
#include "util/group.h"
#include "asm/bug.h"

#include <linux/time64.h>
#include <api/fs/fs.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <locale.h>
#include <math.h>

#define DEFAULT_SEPARATOR	" "
#define CNTR_NOT_SUPPORTED	"<not supported>"
#define CNTR_NOT_COUNTED	"<not counted>"

static void print_counters(struct timespec *ts, int argc, const char **argv);

/* Default events used for perf stat -T */
static const char *transaction_attrs = {
	"task-clock,"
	"{"
	"instructions,"
	"cycles,"
	"cpu/cycles-t/,"
	"cpu/tx-start/,"
	"cpu/el-start/,"
	"cpu/cycles-ct/"
	"}"
};

/* More limited version when the CPU does not have all events. */
static const char * transaction_limited_attrs = {
	"task-clock,"
	"{"
	"instructions,"
	"cycles,"
	"cpu/cycles-t/,"
	"cpu/tx-start/"
	"}"
};

static const char * topdown_attrs[] = {
	"topdown-total-slots",
	"topdown-slots-retired",
	"topdown-recovery-bubbles",
	"topdown-fetch-bubbles",
	"topdown-slots-issued",
	NULL,
};

static struct perf_evlist	*evsel_list;

static struct target target = {
	.uid	= UINT_MAX,
};

typedef int (*aggr_get_id_t)(struct cpu_map *m, int cpu);

static int			run_count			=  1;
static bool			no_inherit			= false;
static volatile pid_t		child_pid			= -1;
static bool			null_run			=  false;
static int			detailed_run			=  0;
static bool			transaction_run;
static bool			topdown_run			= false;
static bool			big_num				=  true;
static int			big_num_opt			=  -1;
static const char		*csv_sep			= NULL;
static bool			csv_output			= false;
static bool			group				= false;
static const char		*pre_cmd			= NULL;
static const char		*post_cmd			= NULL;
static bool			sync_run			= false;
static unsigned int		initial_delay			= 0;
static unsigned int		unit_width			= 4; /* strlen("unit") */
static bool			forever				= false;
static bool			metric_only			= false;
static bool			force_metric_only		= false;
static struct timespec		ref_time;
static struct cpu_map		*aggr_map;
static aggr_get_id_t		aggr_get_id;
static bool			append_file;
static const char		*output_name;
static int			output_fd;

struct perf_stat {
	bool			 record;
	struct perf_data_file	 file;
	struct perf_session	*session;
	u64			 bytes_written;
	struct perf_tool	 tool;
	bool			 maps_allocated;
	struct cpu_map		*cpus;
	struct thread_map	*threads;
	enum aggr_mode		 aggr_mode;
};

static struct perf_stat		perf_stat;
#define STAT_RECORD		perf_stat.record

static volatile int done = 0;

static struct perf_stat_config stat_config = {
	.aggr_mode	= AGGR_GLOBAL,
	.scale		= true,
};

static inline void diff_timespec(struct timespec *r, struct timespec *a,
				 struct timespec *b)
{
	r->tv_sec = a->tv_sec - b->tv_sec;
	if (a->tv_nsec < b->tv_nsec) {
		r->tv_nsec = a->tv_nsec + NSEC_PER_SEC - b->tv_nsec;
		r->tv_sec--;
	} else {
		r->tv_nsec = a->tv_nsec - b->tv_nsec ;
	}
}

static void perf_stat__reset_stats(void)
{
	perf_evlist__reset_stats(evsel_list);
	perf_stat__reset_shadow_stats();
}

static int create_perf_stat_counter(struct perf_evsel *evsel)
{
	struct perf_event_attr *attr = &evsel->attr;

	if (stat_config.scale)
		attr->read_format = PERF_FORMAT_TOTAL_TIME_ENABLED |
				    PERF_FORMAT_TOTAL_TIME_RUNNING;

	attr->inherit = !no_inherit;

	/*
	 * Some events get initialized with sample_(period/type) set,
	 * like tracepoints. Clear it up for counting.
	 */
	attr->sample_period = 0;

	/*
	 * But set sample_type to PERF_SAMPLE_IDENTIFIER, which should be harmless
	 * while avoiding that older tools show confusing messages.
	 *
	 * However for pipe sessions we need to keep it zero,
	 * because script's perf_evsel__check_attr is triggered
	 * by attr->sample_type != 0, and we can't run it on
	 * stat sessions.
	 */
	if (!(STAT_RECORD && perf_stat.file.is_pipe))
		attr->sample_type = PERF_SAMPLE_IDENTIFIER;

	/*
	 * Disabling all counters initially, they will be enabled
	 * either manually by us or by kernel via enable_on_exec
	 * set later.
	 */
	if (perf_evsel__is_group_leader(evsel)) {
		attr->disabled = 1;

		/*
		 * In case of initial_delay we enable tracee
		 * events manually.
		 */
		if (target__none(&target) && !initial_delay)
			attr->enable_on_exec = 1;
	}

	if (target__has_cpu(&target))
		return perf_evsel__open_per_cpu(evsel, perf_evsel__cpus(evsel));

	return perf_evsel__open_per_thread(evsel, evsel_list->threads);
}

/*
 * Does the counter have nsecs as a unit?
 */
static inline int nsec_counter(struct perf_evsel *evsel)
{
	if (perf_evsel__match(evsel, SOFTWARE, SW_CPU_CLOCK) ||
	    perf_evsel__match(evsel, SOFTWARE, SW_TASK_CLOCK))
		return 1;

	return 0;
}

static int process_synthesized_event(struct perf_tool *tool __maybe_unused,
				     union perf_event *event,
				     struct perf_sample *sample __maybe_unused,
				     struct machine *machine __maybe_unused)
{
	if (perf_data_file__write(&perf_stat.file, event, event->header.size) < 0) {
		pr_err("failed to write perf data, error: %m\n");
		return -1;
	}

	perf_stat.bytes_written += event->header.size;
	return 0;
}

static int write_stat_round_event(u64 tm, u64 type)
{
	return perf_event__synthesize_stat_round(NULL, tm, type,
						 process_synthesized_event,
						 NULL);
}

#define WRITE_STAT_ROUND_EVENT(time, interval) \
	write_stat_round_event(time, PERF_STAT_ROUND_TYPE__ ## interval)

#define SID(e, x, y) xyarray__entry(e->sample_id, x, y)

static int
perf_evsel__write_stat_event(struct perf_evsel *counter, u32 cpu, u32 thread,
			     struct perf_counts_values *count)
{
	struct perf_sample_id *sid = SID(counter, cpu, thread);

	return perf_event__synthesize_stat(NULL, cpu, thread, sid->id, count,
					   process_synthesized_event, NULL);
}

/*
 * Read out the results of a single counter:
 * do not aggregate counts across CPUs in system-wide mode
 */
static int read_counter(struct perf_evsel *counter)
{
	int nthreads = thread_map__nr(evsel_list->threads);
	int ncpus, cpu, thread;

	if (target__has_cpu(&target))
		ncpus = perf_evsel__nr_cpus(counter);
	else
		ncpus = 1;

	if (!counter->supported)
		return -ENOENT;

	if (counter->system_wide)
		nthreads = 1;

	for (thread = 0; thread < nthreads; thread++) {
		for (cpu = 0; cpu < ncpus; cpu++) {
			struct perf_counts_values *count;

			count = perf_counts(counter->counts, cpu, thread);
			if (perf_evsel__read(counter, cpu, thread, count))
				return -1;

			if (STAT_RECORD) {
				if (perf_evsel__write_stat_event(counter, cpu, thread, count)) {
					pr_err("failed to write stat event\n");
					return -1;
				}
			}

			if (verbose > 1) {
				fprintf(stat_config.output,
					"%s: %d: %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
						perf_evsel__name(counter),
						cpu,
						count->val, count->ena, count->run);
			}
		}
	}

	return 0;
}

static void read_counters(void)
{
	struct perf_evsel *counter;

	evlist__for_each_entry(evsel_list, counter) {
		if (read_counter(counter))
			pr_debug("failed to read counter %s\n", counter->name);

		if (perf_stat_process_counter(&stat_config, counter))
			pr_warning("failed to process counter %s\n", counter->name);
	}
}

static void process_interval(void)
{
	struct timespec ts, rs;

	read_counters();

	clock_gettime(CLOCK_MONOTONIC, &ts);
	diff_timespec(&rs, &ts, &ref_time);

	if (STAT_RECORD) {
		if (WRITE_STAT_ROUND_EVENT(rs.tv_sec * NSEC_PER_SEC + rs.tv_nsec, INTERVAL))
			pr_err("failed to write stat round event\n");
	}

	print_counters(&rs, 0, NULL);
}

static void enable_counters(void)
{
	if (initial_delay)
		usleep(initial_delay * USEC_PER_MSEC);

	/*
	 * We need to enable counters only if:
	 * - we don't have tracee (attaching to task or cpu)
	 * - we have initial delay configured
	 */
	if (!target__none(&target) || initial_delay)
		perf_evlist__enable(evsel_list);
}

static void disable_counters(void)
{
	/*
	 * If we don't have tracee (attaching to task or cpu), counters may
	 * still be running. To get accurate group ratios, we must stop groups
	 * from counting before reading their constituent counters.
	 */
	if (!target__none(&target))
		perf_evlist__disable(evsel_list);
}

static volatile int workload_exec_errno;

/*
 * perf_evlist__prepare_workload will send a SIGUSR1
 * if the fork fails, since we asked by setting its
 * want_signal to true.
 */
static void workload_exec_failed_signal(int signo __maybe_unused, siginfo_t *info,
					void *ucontext __maybe_unused)
{
	workload_exec_errno = info->si_value.sival_int;
}

static bool has_unit(struct perf_evsel *counter)
{
	return counter->unit && *counter->unit;
}

static bool has_scale(struct perf_evsel *counter)
{
	return counter->scale != 1;
}

static int perf_stat_synthesize_config(bool is_pipe)
{
	struct perf_evsel *counter;
	int err;

	if (is_pipe) {
		err = perf_event__synthesize_attrs(NULL, perf_stat.session,
						   process_synthesized_event);
		if (err < 0) {
			pr_err("Couldn't synthesize attrs.\n");
			return err;
		}
	}

	/*
	 * Synthesize other events stuff not carried within
	 * attr event - unit, scale, name
	 */
	evlist__for_each_entry(evsel_list, counter) {
		if (!counter->supported)
			continue;

		/*
		 * Synthesize unit and scale only if it's defined.
		 */
		if (has_unit(counter)) {
			err = perf_event__synthesize_event_update_unit(NULL, counter, process_synthesized_event);
			if (err < 0) {
				pr_err("Couldn't synthesize evsel unit.\n");
				return err;
			}
		}

		if (has_scale(counter)) {
			err = perf_event__synthesize_event_update_scale(NULL, counter, process_synthesized_event);
			if (err < 0) {
				pr_err("Couldn't synthesize evsel scale.\n");
				return err;
			}
		}

		if (counter->own_cpus) {
			err = perf_event__synthesize_event_update_cpus(NULL, counter, process_synthesized_event);
			if (err < 0) {
				pr_err("Couldn't synthesize evsel scale.\n");
				return err;
			}
		}

		/*
		 * Name is needed only for pipe output,
		 * perf.data carries event names.
		 */
		if (is_pipe) {
			err = perf_event__synthesize_event_update_name(NULL, counter, process_synthesized_event);
			if (err < 0) {
				pr_err("Couldn't synthesize evsel name.\n");
				return err;
			}
		}
	}

	err = perf_event__synthesize_thread_map2(NULL, evsel_list->threads,
						process_synthesized_event,
						NULL);
	if (err < 0) {
		pr_err("Couldn't synthesize thread map.\n");
		return err;
	}

	err = perf_event__synthesize_cpu_map(NULL, evsel_list->cpus,
					     process_synthesized_event, NULL);
	if (err < 0) {
		pr_err("Couldn't synthesize thread map.\n");
		return err;
	}

	err = perf_event__synthesize_stat_config(NULL, &stat_config,
						 process_synthesized_event, NULL);
	if (err < 0) {
		pr_err("Couldn't synthesize config.\n");
		return err;
	}

	return 0;
}

#define FD(e, x, y) (*(int *)xyarray__entry(e->fd, x, y))

static int __store_counter_ids(struct perf_evsel *counter,
			       struct cpu_map *cpus,
			       struct thread_map *threads)
{
	int cpu, thread;

	for (cpu = 0; cpu < cpus->nr; cpu++) {
		for (thread = 0; thread < threads->nr; thread++) {
			int fd = FD(counter, cpu, thread);

			if (perf_evlist__id_add_fd(evsel_list, counter,
						   cpu, thread, fd) < 0)
				return -1;
		}
	}

	return 0;
}

static int store_counter_ids(struct perf_evsel *counter)
{
	struct cpu_map *cpus = counter->cpus;
	struct thread_map *threads = counter->threads;

	if (perf_evsel__alloc_id(counter, cpus->nr, threads->nr))
		return -ENOMEM;

	return __store_counter_ids(counter, cpus, threads);
}

static int __run_perf_stat(int argc, const char **argv)
{
	int interval = stat_config.interval;
	char msg[BUFSIZ];
	unsigned long long t0, t1;
	struct perf_evsel *counter;
	struct timespec ts;
	size_t l;
	int status = 0;
	const bool forks = (argc > 0);
	bool is_pipe = STAT_RECORD ? perf_stat.file.is_pipe : false;
	struct perf_evsel_config_term *err_term;

	if (interval) {
		ts.tv_sec  = interval / USEC_PER_MSEC;
		ts.tv_nsec = (interval % USEC_PER_MSEC) * NSEC_PER_MSEC;
	} else {
		ts.tv_sec  = 1;
		ts.tv_nsec = 0;
	}

	if (forks) {
		if (perf_evlist__prepare_workload(evsel_list, &target, argv, is_pipe,
						  workload_exec_failed_signal) < 0) {
			perror("failed to prepare workload");
			return -1;
		}
		child_pid = evsel_list->workload.pid;
	}

	if (group)
		perf_evlist__set_leader(evsel_list);

	evlist__for_each_entry(evsel_list, counter) {
try_again:
		if (create_perf_stat_counter(counter) < 0) {
			/*
			 * PPC returns ENXIO for HW counters until 2.6.37
			 * (behavior changed with commit b0a873e).
			 */
			if (errno == EINVAL || errno == ENOSYS ||
			    errno == ENOENT || errno == EOPNOTSUPP ||
			    errno == ENXIO) {
				if (verbose)
					ui__warning("%s event is not supported by the kernel.\n",
						    perf_evsel__name(counter));
				counter->supported = false;

				if ((counter->leader != counter) ||
				    !(counter->leader->nr_members > 1))
					continue;
			} else if (perf_evsel__fallback(counter, errno, msg, sizeof(msg))) {
                                if (verbose)
                                        ui__warning("%s\n", msg);
                                goto try_again;
                        }

			perf_evsel__open_strerror(counter, &target,
						  errno, msg, sizeof(msg));
			ui__error("%s\n", msg);

			if (child_pid != -1)
				kill(child_pid, SIGTERM);

			return -1;
		}
		counter->supported = true;

		l = strlen(counter->unit);
		if (l > unit_width)
			unit_width = l;

		if (STAT_RECORD && store_counter_ids(counter))
			return -1;
	}

	if (perf_evlist__apply_filters(evsel_list, &counter)) {
		error("failed to set filter \"%s\" on event %s with %d (%s)\n",
			counter->filter, perf_evsel__name(counter), errno,
			str_error_r(errno, msg, sizeof(msg)));
		return -1;
	}

	if (perf_evlist__apply_drv_configs(evsel_list, &counter, &err_term)) {
		error("failed to set config \"%s\" on event %s with %d (%s)\n",
		      err_term->val.drv_cfg, perf_evsel__name(counter), errno,
		      str_error_r(errno, msg, sizeof(msg)));
		return -1;
	}

	if (STAT_RECORD) {
		int err, fd = perf_data_file__fd(&perf_stat.file);

		if (is_pipe) {
			err = perf_header__write_pipe(perf_data_file__fd(&perf_stat.file));
		} else {
			err = perf_session__write_header(perf_stat.session, evsel_list,
							 fd, false);
		}

		if (err < 0)
			return err;

		err = perf_stat_synthesize_config(is_pipe);
		if (err < 0)
			return err;
	}

	/*
	 * Enable counters and exec the command:
	 */
	t0 = rdclock();
	clock_gettime(CLOCK_MONOTONIC, &ref_time);

	if (forks) {
		perf_evlist__start_workload(evsel_list);
		enable_counters();

		if (interval) {
			while (!waitpid(child_pid, &status, WNOHANG)) {
				nanosleep(&ts, NULL);
				process_interval();
			}
		}
		wait(&status);

		if (workload_exec_errno) {
			const char *emsg = str_error_r(workload_exec_errno, msg, sizeof(msg));
			pr_err("Workload failed: %s\n", emsg);
			return -1;
		}

		if (WIFSIGNALED(status))
			psignal(WTERMSIG(status), argv[0]);
	} else {
		enable_counters();
		while (!done) {
			nanosleep(&ts, NULL);
			if (interval)
				process_interval();
		}
	}

	disable_counters();

	t1 = rdclock();

	update_stats(&walltime_nsecs_stats, t1 - t0);

	/*
	 * Closing a group leader splits the group, and as we only disable
	 * group leaders, results in remaining events becoming enabled. To
	 * avoid arbitrary skew, we must read all counters before closing any
	 * group leaders.
	 */
	read_counters();
	perf_evlist__close(evsel_list);

	return WEXITSTATUS(status);
}

static int run_perf_stat(int argc, const char **argv)
{
	int ret;

	if (pre_cmd) {
		ret = system(pre_cmd);
		if (ret)
			return ret;
	}

	if (sync_run)
		sync();

	ret = __run_perf_stat(argc, argv);
	if (ret)
		return ret;

	if (post_cmd) {
		ret = system(post_cmd);
		if (ret)
			return ret;
	}

	return ret;
}

static void print_running(u64 run, u64 ena)
{
	if (csv_output) {
		fprintf(stat_config.output, "%s%" PRIu64 "%s%.2f",
					csv_sep,
					run,
					csv_sep,
					ena ? 100.0 * run / ena : 100.0);
	} else if (run != ena) {
		fprintf(stat_config.output, "  (%.2f%%)", 100.0 * run / ena);
	}
}

static void print_noise_pct(double total, double avg)
{
	double pct = rel_stddev_stats(total, avg);

	if (csv_output)
		fprintf(stat_config.output, "%s%.2f%%", csv_sep, pct);
	else if (pct)
		fprintf(stat_config.output, "  ( +-%6.2f%% )", pct);
}

static void print_noise(struct perf_evsel *evsel, double avg)
{
	struct perf_stat_evsel *ps;

	if (run_count == 1)
		return;

	ps = evsel->priv;
	print_noise_pct(stddev_stats(&ps->res_stats[0]), avg);
}

static void aggr_printout(struct perf_evsel *evsel, int id, int nr)
{
	switch (stat_config.aggr_mode) {
	case AGGR_CORE:
		fprintf(stat_config.output, "S%d-C%*d%s%*d%s",
			cpu_map__id_to_socket(id),
			csv_output ? 0 : -8,
			cpu_map__id_to_cpu(id),
			csv_sep,
			csv_output ? 0 : 4,
			nr,
			csv_sep);
		break;
	case AGGR_SOCKET:
		fprintf(stat_config.output, "S%*d%s%*d%s",
			csv_output ? 0 : -5,
			id,
			csv_sep,
			csv_output ? 0 : 4,
			nr,
			csv_sep);
			break;
	case AGGR_NONE:
		fprintf(stat_config.output, "CPU%*d%s",
			csv_output ? 0 : -4,
			perf_evsel__cpus(evsel)->map[id], csv_sep);
		break;
	case AGGR_THREAD:
		fprintf(stat_config.output, "%*s-%*d%s",
			csv_output ? 0 : 16,
			thread_map__comm(evsel->threads, id),
			csv_output ? 0 : -8,
			thread_map__pid(evsel->threads, id),
			csv_sep);
		break;
	case AGGR_GLOBAL:
	case AGGR_UNSET:
	default:
		break;
	}
}

struct outstate {
	FILE *fh;
	bool newline;
	const char *prefix;
	int  nfields;
	int  id, nr;
	struct perf_evsel *evsel;
};

#define METRIC_LEN  35

static void new_line_std(void *ctx)
{
	struct outstate *os = ctx;

	os->newline = true;
}

static void do_new_line_std(struct outstate *os)
{
	fputc('\n', os->fh);
	fputs(os->prefix, os->fh);
	aggr_printout(os->evsel, os->id, os->nr);
	if (stat_config.aggr_mode == AGGR_NONE)
		fprintf(os->fh, "        ");
	fprintf(os->fh, "                                                 ");
}

static void print_metric_std(void *ctx, const char *color, const char *fmt,
			     const char *unit, double val)
{
	struct outstate *os = ctx;
	FILE *out = os->fh;
	int n;
	bool newline = os->newline;

	os->newline = false;

	if (unit == NULL || fmt == NULL) {
		fprintf(out, "%-*s", METRIC_LEN, "");
		return;
	}

	if (newline)
		do_new_line_std(os);

	n = fprintf(out, " # ");
	if (color)
		n += color_fprintf(out, color, fmt, val);
	else
		n += fprintf(out, fmt, val);
	fprintf(out, " %-*s", METRIC_LEN - n - 1, unit);
}

static void new_line_csv(void *ctx)
{
	struct outstate *os = ctx;
	int i;

	fputc('\n', os->fh);
	if (os->prefix)
		fprintf(os->fh, "%s%s", os->prefix, csv_sep);
	aggr_printout(os->evsel, os->id, os->nr);
	for (i = 0; i < os->nfields; i++)
		fputs(csv_sep, os->fh);
}

static void print_metric_csv(void *ctx,
			     const char *color __maybe_unused,
			     const char *fmt, const char *unit, double val)
{
	struct outstate *os = ctx;
	FILE *out = os->fh;
	char buf[64], *vals, *ends;

	if (unit == NULL || fmt == NULL) {
		fprintf(out, "%s%s%s%s", csv_sep, csv_sep, csv_sep, csv_sep);
		return;
	}
	snprintf(buf, sizeof(buf), fmt, val);
	vals = buf;
	while (isspace(*vals))
		vals++;
	ends = vals;
	while (isdigit(*ends) || *ends == '.')
		ends++;
	*ends = 0;
	while (isspace(*unit))
		unit++;
	fprintf(out, "%s%s%s%s", csv_sep, vals, csv_sep, unit);
}

#define METRIC_ONLY_LEN 20

/* Filter out some columns that don't work well in metrics only mode */

static bool valid_only_metric(const char *unit)
{
	if (!unit)
		return false;
	if (strstr(unit, "/sec") ||
	    strstr(unit, "hz") ||
	    strstr(unit, "Hz") ||
	    strstr(unit, "CPUs utilized"))
		return false;
	return true;
}

static const char *fixunit(char *buf, struct perf_evsel *evsel,
			   const char *unit)
{
	if (!strncmp(unit, "of all", 6)) {
		snprintf(buf, 1024, "%s %s", perf_evsel__name(evsel),
			 unit);
		return buf;
	}
	return unit;
}

static void print_metric_only(void *ctx, const char *color, const char *fmt,
			      const char *unit, double val)
{
	struct outstate *os = ctx;
	FILE *out = os->fh;
	int n;
	char buf[1024];
	unsigned mlen = METRIC_ONLY_LEN;

	if (!valid_only_metric(unit))
		return;
	unit = fixunit(buf, os->evsel, unit);
	if (color)
		n = color_fprintf(out, color, fmt, val);
	else
		n = fprintf(out, fmt, val);
	if (n > METRIC_ONLY_LEN)
		n = METRIC_ONLY_LEN;
	if (mlen < strlen(unit))
		mlen = strlen(unit) + 1;
	fprintf(out, "%*s", mlen - n, "");
}

static void print_metric_only_csv(void *ctx, const char *color __maybe_unused,
				  const char *fmt,
				  const char *unit, double val)
{
	struct outstate *os = ctx;
	FILE *out = os->fh;
	char buf[64], *vals, *ends;
	char tbuf[1024];

	if (!valid_only_metric(unit))
		return;
	unit = fixunit(tbuf, os->evsel, unit);
	snprintf(buf, sizeof buf, fmt, val);
	vals = buf;
	while (isspace(*vals))
		vals++;
	ends = vals;
	while (isdigit(*ends) || *ends == '.')
		ends++;
	*ends = 0;
	fprintf(out, "%s%s", vals, csv_sep);
}

static void new_line_metric(void *ctx __maybe_unused)
{
}

static void print_metric_header(void *ctx, const char *color __maybe_unused,
				const char *fmt __maybe_unused,
				const char *unit, double val __maybe_unused)
{
	struct outstate *os = ctx;
	char tbuf[1024];

	if (!valid_only_metric(unit))
		return;
	unit = fixunit(tbuf, os->evsel, unit);
	if (csv_output)
		fprintf(os->fh, "%s%s", unit, csv_sep);
	else
		fprintf(os->fh, "%-*s ", METRIC_ONLY_LEN, unit);
}

static void nsec_printout(int id, int nr, struct perf_evsel *evsel, double avg)
{
	FILE *output = stat_config.output;
	double msecs = avg / NSEC_PER_MSEC;
	const char *fmt_v, *fmt_n;
	char name[25];

	fmt_v = csv_output ? "%.6f%s" : "%18.6f%s";
	fmt_n = csv_output ? "%s" : "%-25s";

	aggr_printout(evsel, id, nr);

	scnprintf(name, sizeof(name), "%s%s",
		  perf_evsel__name(evsel), csv_output ? "" : " (msec)");

	fprintf(output, fmt_v, msecs, csv_sep);

	if (csv_output)
		fprintf(output, "%s%s", evsel->unit, csv_sep);
	else
		fprintf(output, "%-*s%s", unit_width, evsel->unit, csv_sep);

	fprintf(output, fmt_n, name);

	if (evsel->cgrp)
		fprintf(output, "%s%s", csv_sep, evsel->cgrp->name);
}

static int first_shadow_cpu(struct perf_evsel *evsel, int id)
{
	int i;

	if (!aggr_get_id)
		return 0;

	if (stat_config.aggr_mode == AGGR_NONE)
		return id;

	if (stat_config.aggr_mode == AGGR_GLOBAL)
		return 0;

	for (i = 0; i < perf_evsel__nr_cpus(evsel); i++) {
		int cpu2 = perf_evsel__cpus(evsel)->map[i];

		if (aggr_get_id(evsel_list->cpus, cpu2) == id)
			return cpu2;
	}
	return 0;
}

static void abs_printout(int id, int nr, struct perf_evsel *evsel, double avg)
{
	FILE *output = stat_config.output;
	double sc =  evsel->scale;
	const char *fmt;

	if (csv_output) {
		fmt = floor(sc) != sc ?  "%.2f%s" : "%.0f%s";
	} else {
		if (big_num)
			fmt = floor(sc) != sc ? "%'18.2f%s" : "%'18.0f%s";
		else
			fmt = floor(sc) != sc ? "%18.2f%s" : "%18.0f%s";
	}

	aggr_printout(evsel, id, nr);

	fprintf(output, fmt, avg, csv_sep);

	if (evsel->unit)
		fprintf(output, "%-*s%s",
			csv_output ? 0 : unit_width,
			evsel->unit, csv_sep);

	fprintf(output, "%-*s", csv_output ? 0 : 25, perf_evsel__name(evsel));

	if (evsel->cgrp)
		fprintf(output, "%s%s", csv_sep, evsel->cgrp->name);
}

static void printout(int id, int nr, struct perf_evsel *counter, double uval,
		     char *prefix, u64 run, u64 ena, double noise)
{
	struct perf_stat_output_ctx out;
	struct outstate os = {
		.fh = stat_config.output,
		.prefix = prefix ? prefix : "",
		.id = id,
		.nr = nr,
		.evsel = counter,
	};
	print_metric_t pm = print_metric_std;
	void (*nl)(void *);

	if (metric_only) {
		nl = new_line_metric;
		if (csv_output)
			pm = print_metric_only_csv;
		else
			pm = print_metric_only;
	} else
		nl = new_line_std;

	if (csv_output && !metric_only) {
		static int aggr_fields[] = {
			[AGGR_GLOBAL] = 0,
			[AGGR_THREAD] = 1,
			[AGGR_NONE] = 1,
			[AGGR_SOCKET] = 2,
			[AGGR_CORE] = 2,
		};

		pm = print_metric_csv;
		nl = new_line_csv;
		os.nfields = 3;
		os.nfields += aggr_fields[stat_config.aggr_mode];
		if (counter->cgrp)
			os.nfields++;
	}
	if (run == 0 || ena == 0 || counter->counts->scaled == -1) {
		if (metric_only) {
			pm(&os, NULL, "", "", 0);
			return;
		}
		aggr_printout(counter, id, nr);

		fprintf(stat_config.output, "%*s%s",
			csv_output ? 0 : 18,
			counter->supported ? CNTR_NOT_COUNTED : CNTR_NOT_SUPPORTED,
			csv_sep);

		fprintf(stat_config.output, "%-*s%s",
			csv_output ? 0 : unit_width,
			counter->unit, csv_sep);

		fprintf(stat_config.output, "%*s",
			csv_output ? 0 : -25,
			perf_evsel__name(counter));

		if (counter->cgrp)
			fprintf(stat_config.output, "%s%s",
				csv_sep, counter->cgrp->name);

		if (!csv_output)
			pm(&os, NULL, NULL, "", 0);
		print_noise(counter, noise);
		print_running(run, ena);
		if (csv_output)
			pm(&os, NULL, NULL, "", 0);
		return;
	}

	if (metric_only)
		/* nothing */;
	else if (nsec_counter(counter))
		nsec_printout(id, nr, counter, uval);
	else
		abs_printout(id, nr, counter, uval);

	out.print_metric = pm;
	out.new_line = nl;
	out.ctx = &os;

	if (csv_output && !metric_only) {
		print_noise(counter, noise);
		print_running(run, ena);
	}

	perf_stat__print_shadow_stats(counter, uval,
				first_shadow_cpu(counter, id),
				&out);
	if (!csv_output && !metric_only) {
		print_noise(counter, noise);
		print_running(run, ena);
	}
}

static void aggr_update_shadow(void)
{
	int cpu, s2, id, s;
	u64 val;
	struct perf_evsel *counter;

	for (s = 0; s < aggr_map->nr; s++) {
		id = aggr_map->map[s];
		evlist__for_each_entry(evsel_list, counter) {
			val = 0;
			for (cpu = 0; cpu < perf_evsel__nr_cpus(counter); cpu++) {
				s2 = aggr_get_id(evsel_list->cpus, cpu);
				if (s2 != id)
					continue;
				val += perf_counts(counter->counts, cpu, 0)->val;
			}
			val = val * counter->scale;
			perf_stat__update_shadow_stats(counter, &val,
						       first_shadow_cpu(counter, id));
		}
	}
}

static void print_aggr(char *prefix)
{
	FILE *output = stat_config.output;
	struct perf_evsel *counter;
	int cpu, s, s2, id, nr;
	double uval;
	u64 ena, run, val;
	bool first;

	if (!(aggr_map || aggr_get_id))
		return;

	aggr_update_shadow();

	/*
	 * With metric_only everything is on a single line.
	 * Without each counter has its own line.
	 */
	for (s = 0; s < aggr_map->nr; s++) {
		if (prefix && metric_only)
			fprintf(output, "%s", prefix);

		id = aggr_map->map[s];
		first = true;
		evlist__for_each_entry(evsel_list, counter) {
			val = ena = run = 0;
			nr = 0;
			for (cpu = 0; cpu < perf_evsel__nr_cpus(counter); cpu++) {
				s2 = aggr_get_id(perf_evsel__cpus(counter), cpu);
				if (s2 != id)
					continue;
				val += perf_counts(counter->counts, cpu, 0)->val;
				ena += perf_counts(counter->counts, cpu, 0)->ena;
				run += perf_counts(counter->counts, cpu, 0)->run;
				nr++;
			}
			if (first && metric_only) {
				first = false;
				aggr_printout(counter, id, nr);
			}
			if (prefix && !metric_only)
				fprintf(output, "%s", prefix);

			uval = val * counter->scale;
			printout(id, nr, counter, uval, prefix, run, ena, 1.0);
			if (!metric_only)
				fputc('\n', output);
		}
		if (metric_only)
			fputc('\n', output);
	}
}

static void print_aggr_thread(struct perf_evsel *counter, char *prefix)
{
	FILE *output = stat_config.output;
	int nthreads = thread_map__nr(counter->threads);
	int ncpus = cpu_map__nr(counter->cpus);
	int cpu, thread;
	double uval;

	for (thread = 0; thread < nthreads; thread++) {
		u64 ena = 0, run = 0, val = 0;

		for (cpu = 0; cpu < ncpus; cpu++) {
			val += perf_counts(counter->counts, cpu, thread)->val;
			ena += perf_counts(counter->counts, cpu, thread)->ena;
			run += perf_counts(counter->counts, cpu, thread)->run;
		}

		if (prefix)
			fprintf(output, "%s", prefix);

		uval = val * counter->scale;
		printout(thread, 0, counter, uval, prefix, run, ena, 1.0);
		fputc('\n', output);
	}
}

/*
 * Print out the results of a single counter:
 * aggregated counts in system-wide mode
 */
static void print_counter_aggr(struct perf_evsel *counter, char *prefix)
{
	FILE *output = stat_config.output;
	struct perf_stat_evsel *ps = counter->priv;
	double avg = avg_stats(&ps->res_stats[0]);
	double uval;
	double avg_enabled, avg_running;

	avg_enabled = avg_stats(&ps->res_stats[1]);
	avg_running = avg_stats(&ps->res_stats[2]);

	if (prefix && !metric_only)
		fprintf(output, "%s", prefix);

	uval = avg * counter->scale;
	printout(-1, 0, counter, uval, prefix, avg_running, avg_enabled, avg);
	if (!metric_only)
		fprintf(output, "\n");
}

/*
 * Print out the results of a single counter:
 * does not use aggregated count in system-wide
 */
static void print_counter(struct perf_evsel *counter, char *prefix)
{
	FILE *output = stat_config.output;
	u64 ena, run, val;
	double uval;
	int cpu;

	for (cpu = 0; cpu < perf_evsel__nr_cpus(counter); cpu++) {
		val = perf_counts(counter->counts, cpu, 0)->val;
		ena = perf_counts(counter->counts, cpu, 0)->ena;
		run = perf_counts(counter->counts, cpu, 0)->run;

		if (prefix)
			fprintf(output, "%s", prefix);

		uval = val * counter->scale;
		printout(cpu, 0, counter, uval, prefix, run, ena, 1.0);

		fputc('\n', output);
	}
}

static void print_no_aggr_metric(char *prefix)
{
	int cpu;
	int nrcpus = 0;
	struct perf_evsel *counter;
	u64 ena, run, val;
	double uval;

	nrcpus = evsel_list->cpus->nr;
	for (cpu = 0; cpu < nrcpus; cpu++) {
		bool first = true;

		if (prefix)
			fputs(prefix, stat_config.output);
		evlist__for_each_entry(evsel_list, counter) {
			if (first) {
				aggr_printout(counter, cpu, 0);
				first = false;
			}
			val = perf_counts(counter->counts, cpu, 0)->val;
			ena = perf_counts(counter->counts, cpu, 0)->ena;
			run = perf_counts(counter->counts, cpu, 0)->run;

			uval = val * counter->scale;
			printout(cpu, 0, counter, uval, prefix, run, ena, 1.0);
		}
		fputc('\n', stat_config.output);
	}
}

static int aggr_header_lens[] = {
	[AGGR_CORE] = 18,
	[AGGR_SOCKET] = 12,
	[AGGR_NONE] = 6,
	[AGGR_THREAD] = 24,
	[AGGR_GLOBAL] = 0,
};

static const char *aggr_header_csv[] = {
	[AGGR_CORE] 	= 	"core,cpus,",
	[AGGR_SOCKET] 	= 	"socket,cpus",
	[AGGR_NONE] 	= 	"cpu,",
	[AGGR_THREAD] 	= 	"comm-pid,",
	[AGGR_GLOBAL] 	=	""
};

static void print_metric_headers(const char *prefix, bool no_indent)
{
	struct perf_stat_output_ctx out;
	struct perf_evsel *counter;
	struct outstate os = {
		.fh = stat_config.output
	};

	if (prefix)
		fprintf(stat_config.output, "%s", prefix);

	if (!csv_output && !no_indent)
		fprintf(stat_config.output, "%*s",
			aggr_header_lens[stat_config.aggr_mode], "");
	if (csv_output) {
		if (stat_config.interval)
			fputs("time,", stat_config.output);
		fputs(aggr_header_csv[stat_config.aggr_mode],
			stat_config.output);
	}

	/* Print metrics headers only */
	evlist__for_each_entry(evsel_list, counter) {
		os.evsel = counter;
		out.ctx = &os;
		out.print_metric = print_metric_header;
		out.new_line = new_line_metric;
		os.evsel = counter;
		perf_stat__print_shadow_stats(counter, 0,
					      0,
					      &out);
	}
	fputc('\n', stat_config.output);
}

static void print_interval(char *prefix, struct timespec *ts)
{
	FILE *output = stat_config.output;
	static int num_print_interval;

	sprintf(prefix, "%6lu.%09lu%s", ts->tv_sec, ts->tv_nsec, csv_sep);

	if (num_print_interval == 0 && !csv_output) {
		switch (stat_config.aggr_mode) {
		case AGGR_SOCKET:
			fprintf(output, "#           time socket cpus");
			if (!metric_only)
				fprintf(output, "             counts %*s events\n", unit_width, "unit");
			break;
		case AGGR_CORE:
			fprintf(output, "#           time core         cpus");
			if (!metric_only)
				fprintf(output, "             counts %*s events\n", unit_width, "unit");
			break;
		case AGGR_NONE:
			fprintf(output, "#           time CPU");
			if (!metric_only)
				fprintf(output, "                counts %*s events\n", unit_width, "unit");
			break;
		case AGGR_THREAD:
			fprintf(output, "#           time             comm-pid");
			if (!metric_only)
				fprintf(output, "                  counts %*s events\n", unit_width, "unit");
			break;
		case AGGR_GLOBAL:
		default:
			fprintf(output, "#           time");
			if (!metric_only)
				fprintf(output, "             counts %*s events\n", unit_width, "unit");
		case AGGR_UNSET:
			break;
		}
	}

	if (num_print_interval == 0 && metric_only)
		print_metric_headers(" ", true);
	if (++num_print_interval == 25)
		num_print_interval = 0;
}

static void print_header(int argc, const char **argv)
{
	FILE *output = stat_config.output;
	int i;

	fflush(stdout);

	if (!csv_output) {
		fprintf(output, "\n");
		fprintf(output, " Performance counter stats for ");
		if (target.system_wide)
			fprintf(output, "\'system wide");
		else if (target.cpu_list)
			fprintf(output, "\'CPU(s) %s", target.cpu_list);
		else if (!target__has_task(&target)) {
			fprintf(output, "\'%s", argv ? argv[0] : "pipe");
			for (i = 1; argv && (i < argc); i++)
				fprintf(output, " %s", argv[i]);
		} else if (target.pid)
			fprintf(output, "process id \'%s", target.pid);
		else
			fprintf(output, "thread id \'%s", target.tid);

		fprintf(output, "\'");
		if (run_count > 1)
			fprintf(output, " (%d runs)", run_count);
		fprintf(output, ":\n\n");
	}
}

static void print_footer(void)
{
	FILE *output = stat_config.output;

	if (!null_run)
		fprintf(output, "\n");
	fprintf(output, " %17.9f seconds time elapsed",
			avg_stats(&walltime_nsecs_stats) / NSEC_PER_SEC);
	if (run_count > 1) {
		fprintf(output, "                                        ");
		print_noise_pct(stddev_stats(&walltime_nsecs_stats),
				avg_stats(&walltime_nsecs_stats));
	}
	fprintf(output, "\n\n");
}

static void print_counters(struct timespec *ts, int argc, const char **argv)
{
	int interval = stat_config.interval;
	struct perf_evsel *counter;
	char buf[64], *prefix = NULL;

	/* Do not print anything if we record to the pipe. */
	if (STAT_RECORD && perf_stat.file.is_pipe)
		return;

	if (interval)
		print_interval(prefix = buf, ts);
	else
		print_header(argc, argv);

	if (metric_only) {
		static int num_print_iv;

		if (num_print_iv == 0 && !interval)
			print_metric_headers(prefix, false);
		if (num_print_iv++ == 25)
			num_print_iv = 0;
		if (stat_config.aggr_mode == AGGR_GLOBAL && prefix)
			fprintf(stat_config.output, "%s", prefix);
	}

	switch (stat_config.aggr_mode) {
	case AGGR_CORE:
	case AGGR_SOCKET:
		print_aggr(prefix);
		break;
	case AGGR_THREAD:
		evlist__for_each_entry(evsel_list, counter)
			print_aggr_thread(counter, prefix);
		break;
	case AGGR_GLOBAL:
		evlist__for_each_entry(evsel_list, counter)
			print_counter_aggr(counter, prefix);
		if (metric_only)
			fputc('\n', stat_config.output);
		break;
	case AGGR_NONE:
		if (metric_only)
			print_no_aggr_metric(prefix);
		else {
			evlist__for_each_entry(evsel_list, counter)
				print_counter(counter, prefix);
		}
		break;
	case AGGR_UNSET:
	default:
		break;
	}

	if (!interval && !csv_output)
		print_footer();

	fflush(stat_config.output);
}

static volatile int signr = -1;

static void skip_signal(int signo)
{
	if ((child_pid == -1) || stat_config.interval)
		done = 1;

	signr = signo;
	/*
	 * render child_pid harmless
	 * won't send SIGTERM to a random
	 * process in case of race condition
	 * and fast PID recycling
	 */
	child_pid = -1;
}

static void sig_atexit(void)
{
	sigset_t set, oset;

	/*
	 * avoid race condition with SIGCHLD handler
	 * in skip_signal() which is modifying child_pid
	 * goal is to avoid send SIGTERM to a random
	 * process
	 */
	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	sigprocmask(SIG_BLOCK, &set, &oset);

	if (child_pid != -1)
		kill(child_pid, SIGTERM);

	sigprocmask(SIG_SETMASK, &oset, NULL);

	if (signr == -1)
		return;

	signal(signr, SIG_DFL);
	kill(getpid(), signr);
}

static int stat__set_big_num(const struct option *opt __maybe_unused,
			     const char *s __maybe_unused, int unset)
{
	big_num_opt = unset ? 0 : 1;
	return 0;
}

static int enable_metric_only(const struct option *opt __maybe_unused,
			      const char *s __maybe_unused, int unset)
{
	force_metric_only = true;
	metric_only = !unset;
	return 0;
}

static const struct option stat_options[] = {
	OPT_BOOLEAN('T', "transaction", &transaction_run,
		    "hardware transaction statistics"),
	OPT_CALLBACK('e', "event", &evsel_list, "event",
		     "event selector. use 'perf list' to list available events",
		     parse_events_option),
	OPT_CALLBACK(0, "filter", &evsel_list, "filter",
		     "event filter", parse_filter),
	OPT_BOOLEAN('i', "no-inherit", &no_inherit,
		    "child tasks do not inherit counters"),
	OPT_STRING('p', "pid", &target.pid, "pid",
		   "stat events on existing process id"),
	OPT_STRING('t', "tid", &target.tid, "tid",
		   "stat events on existing thread id"),
	OPT_BOOLEAN('a', "all-cpus", &target.system_wide,
		    "system-wide collection from all CPUs"),
	OPT_BOOLEAN('g', "group", &group,
		    "put the counters into a counter group"),
	OPT_BOOLEAN('c', "scale", &stat_config.scale, "scale/normalize counters"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show counter open errors, etc)"),
	OPT_INTEGER('r', "repeat", &run_count,
		    "repeat command and print average + stddev (max: 100, forever: 0)"),
	OPT_BOOLEAN('n', "null", &null_run,
		    "null run - dont start any counters"),
	OPT_INCR('d', "detailed", &detailed_run,
		    "detailed run - start a lot of events"),
	OPT_BOOLEAN('S', "sync", &sync_run,
		    "call sync() before starting a run"),
	OPT_CALLBACK_NOOPT('B', "big-num", NULL, NULL,
			   "print large numbers with thousands\' separators",
			   stat__set_big_num),
	OPT_STRING('C', "cpu", &target.cpu_list, "cpu",
		    "list of cpus to monitor in system-wide"),
	OPT_SET_UINT('A', "no-aggr", &stat_config.aggr_mode,
		    "disable CPU count aggregation", AGGR_NONE),
	OPT_STRING('x', "field-separator", &csv_sep, "separator",
		   "print counts with custom separator"),
	OPT_CALLBACK('G', "cgroup", &evsel_list, "name",
		     "monitor event in cgroup name only", parse_cgroups),
	OPT_STRING('o', "output", &output_name, "file", "output file name"),
	OPT_BOOLEAN(0, "append", &append_file, "append to the output file"),
	OPT_INTEGER(0, "log-fd", &output_fd,
		    "log output to fd, instead of stderr"),
	OPT_STRING(0, "pre", &pre_cmd, "command",
			"command to run prior to the measured command"),
	OPT_STRING(0, "post", &post_cmd, "command",
			"command to run after to the measured command"),
	OPT_UINTEGER('I', "interval-print", &stat_config.interval,
		    "print counts at regular interval in ms (>= 10)"),
	OPT_SET_UINT(0, "per-socket", &stat_config.aggr_mode,
		     "aggregate counts per processor socket", AGGR_SOCKET),
	OPT_SET_UINT(0, "per-core", &stat_config.aggr_mode,
		     "aggregate counts per physical processor core", AGGR_CORE),
	OPT_SET_UINT(0, "per-thread", &stat_config.aggr_mode,
		     "aggregate counts per thread", AGGR_THREAD),
	OPT_UINTEGER('D', "delay", &initial_delay,
		     "ms to wait before starting measurement after program start"),
	OPT_CALLBACK_NOOPT(0, "metric-only", &metric_only, NULL,
			"Only print computed metrics. No raw values", enable_metric_only),
	OPT_BOOLEAN(0, "topdown", &topdown_run,
			"measure topdown level 1 statistics"),
	OPT_END()
};

static int perf_stat__get_socket(struct cpu_map *map, int cpu)
{
	return cpu_map__get_socket(map, cpu, NULL);
}

static int perf_stat__get_core(struct cpu_map *map, int cpu)
{
	return cpu_map__get_core(map, cpu, NULL);
}

static int cpu_map__get_max(struct cpu_map *map)
{
	int i, max = -1;

	for (i = 0; i < map->nr; i++) {
		if (map->map[i] > max)
			max = map->map[i];
	}

	return max;
}

static struct cpu_map *cpus_aggr_map;

static int perf_stat__get_aggr(aggr_get_id_t get_id, struct cpu_map *map, int idx)
{
	int cpu;

	if (idx >= map->nr)
		return -1;

	cpu = map->map[idx];

	if (cpus_aggr_map->map[cpu] == -1)
		cpus_aggr_map->map[cpu] = get_id(map, idx);

	return cpus_aggr_map->map[cpu];
}

static int perf_stat__get_socket_cached(struct cpu_map *map, int idx)
{
	return perf_stat__get_aggr(perf_stat__get_socket, map, idx);
}

static int perf_stat__get_core_cached(struct cpu_map *map, int idx)
{
	return perf_stat__get_aggr(perf_stat__get_core, map, idx);
}

static int perf_stat_init_aggr_mode(void)
{
	int nr;

	switch (stat_config.aggr_mode) {
	case AGGR_SOCKET:
		if (cpu_map__build_socket_map(evsel_list->cpus, &aggr_map)) {
			perror("cannot build socket map");
			return -1;
		}
		aggr_get_id = perf_stat__get_socket_cached;
		break;
	case AGGR_CORE:
		if (cpu_map__build_core_map(evsel_list->cpus, &aggr_map)) {
			perror("cannot build core map");
			return -1;
		}
		aggr_get_id = perf_stat__get_core_cached;
		break;
	case AGGR_NONE:
	case AGGR_GLOBAL:
	case AGGR_THREAD:
	case AGGR_UNSET:
	default:
		break;
	}

	/*
	 * The evsel_list->cpus is the base we operate on,
	 * taking the highest cpu number to be the size of
	 * the aggregation translate cpumap.
	 */
	nr = cpu_map__get_max(evsel_list->cpus);
	cpus_aggr_map = cpu_map__empty_new(nr + 1);
	return cpus_aggr_map ? 0 : -ENOMEM;
}

static void perf_stat__exit_aggr_mode(void)
{
	cpu_map__put(aggr_map);
	cpu_map__put(cpus_aggr_map);
	aggr_map = NULL;
	cpus_aggr_map = NULL;
}

static inline int perf_env__get_cpu(struct perf_env *env, struct cpu_map *map, int idx)
{
	int cpu;

	if (idx > map->nr)
		return -1;

	cpu = map->map[idx];

	if (cpu >= env->nr_cpus_avail)
		return -1;

	return cpu;
}

static int perf_env__get_socket(struct cpu_map *map, int idx, void *data)
{
	struct perf_env *env = data;
	int cpu = perf_env__get_cpu(env, map, idx);

	return cpu == -1 ? -1 : env->cpu[cpu].socket_id;
}

static int perf_env__get_core(struct cpu_map *map, int idx, void *data)
{
	struct perf_env *env = data;
	int core = -1, cpu = perf_env__get_cpu(env, map, idx);

	if (cpu != -1) {
		int socket_id = env->cpu[cpu].socket_id;

		/*
		 * Encode socket in upper 16 bits
		 * core_id is relative to socket, and
		 * we need a global id. So we combine
		 * socket + core id.
		 */
		core = (socket_id << 16) | (env->cpu[cpu].core_id & 0xffff);
	}

	return core;
}

static int perf_env__build_socket_map(struct perf_env *env, struct cpu_map *cpus,
				      struct cpu_map **sockp)
{
	return cpu_map__build_map(cpus, sockp, perf_env__get_socket, env);
}

static int perf_env__build_core_map(struct perf_env *env, struct cpu_map *cpus,
				    struct cpu_map **corep)
{
	return cpu_map__build_map(cpus, corep, perf_env__get_core, env);
}

static int perf_stat__get_socket_file(struct cpu_map *map, int idx)
{
	return perf_env__get_socket(map, idx, &perf_stat.session->header.env);
}

static int perf_stat__get_core_file(struct cpu_map *map, int idx)
{
	return perf_env__get_core(map, idx, &perf_stat.session->header.env);
}

static int perf_stat_init_aggr_mode_file(struct perf_stat *st)
{
	struct perf_env *env = &st->session->header.env;

	switch (stat_config.aggr_mode) {
	case AGGR_SOCKET:
		if (perf_env__build_socket_map(env, evsel_list->cpus, &aggr_map)) {
			perror("cannot build socket map");
			return -1;
		}
		aggr_get_id = perf_stat__get_socket_file;
		break;
	case AGGR_CORE:
		if (perf_env__build_core_map(env, evsel_list->cpus, &aggr_map)) {
			perror("cannot build core map");
			return -1;
		}
		aggr_get_id = perf_stat__get_core_file;
		break;
	case AGGR_NONE:
	case AGGR_GLOBAL:
	case AGGR_THREAD:
	case AGGR_UNSET:
	default:
		break;
	}

	return 0;
}

static int topdown_filter_events(const char **attr, char **str, bool use_group)
{
	int off = 0;
	int i;
	int len = 0;
	char *s;

	for (i = 0; attr[i]; i++) {
		if (pmu_have_event("cpu", attr[i])) {
			len += strlen(attr[i]) + 1;
			attr[i - off] = attr[i];
		} else
			off++;
	}
	attr[i - off] = NULL;

	*str = malloc(len + 1 + 2);
	if (!*str)
		return -1;
	s = *str;
	if (i - off == 0) {
		*s = 0;
		return 0;
	}
	if (use_group)
		*s++ = '{';
	for (i = 0; attr[i]; i++) {
		strcpy(s, attr[i]);
		s += strlen(s);
		*s++ = ',';
	}
	if (use_group) {
		s[-1] = '}';
		*s = 0;
	} else
		s[-1] = 0;
	return 0;
}

__weak bool arch_topdown_check_group(bool *warn)
{
	*warn = false;
	return false;
}

__weak void arch_topdown_group_warn(void)
{
}

/*
 * Add default attributes, if there were no attributes specified or
 * if -d/--detailed, -d -d or -d -d -d is used:
 */
static int add_default_attributes(void)
{
	int err;
	struct perf_event_attr default_attrs0[] = {

  { .type = PERF_TYPE_SOFTWARE, .config = PERF_COUNT_SW_TASK_CLOCK		},
  { .type = PERF_TYPE_SOFTWARE, .config = PERF_COUNT_SW_CONTEXT_SWITCHES	},
  { .type = PERF_TYPE_SOFTWARE, .config = PERF_COUNT_SW_CPU_MIGRATIONS		},
  { .type = PERF_TYPE_SOFTWARE, .config = PERF_COUNT_SW_PAGE_FAULTS		},

  { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_CPU_CYCLES		},
};
	struct perf_event_attr frontend_attrs[] = {
  { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_STALLED_CYCLES_FRONTEND	},
};
	struct perf_event_attr backend_attrs[] = {
  { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_STALLED_CYCLES_BACKEND	},
};
	struct perf_event_attr default_attrs1[] = {
  { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_INSTRUCTIONS		},
  { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS	},
  { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_BRANCH_MISSES		},

};

/*
 * Detailed stats (-d), covering the L1 and last level data caches:
 */
	struct perf_event_attr detailed_attrs[] = {

  { .type = PERF_TYPE_HW_CACHE,
    .config =
	 PERF_COUNT_HW_CACHE_L1D		<<  0  |
	(PERF_COUNT_HW_CACHE_OP_READ		<<  8) |
	(PERF_COUNT_HW_CACHE_RESULT_ACCESS	<< 16)				},

  { .type = PERF_TYPE_HW_CACHE,
    .config =
	 PERF_COUNT_HW_CACHE_L1D		<<  0  |
	(PERF_COUNT_HW_CACHE_OP_READ		<<  8) |
	(PERF_COUNT_HW_CACHE_RESULT_MISS	<< 16)				},

  { .type = PERF_TYPE_HW_CACHE,
    .config =
	 PERF_COUNT_HW_CACHE_LL			<<  0  |
	(PERF_COUNT_HW_CACHE_OP_READ		<<  8) |
	(PERF_COUNT_HW_CACHE_RESULT_ACCESS	<< 16)				},

  { .type = PERF_TYPE_HW_CACHE,
    .config =
	 PERF_COUNT_HW_CACHE_LL			<<  0  |
	(PERF_COUNT_HW_CACHE_OP_READ		<<  8) |
	(PERF_COUNT_HW_CACHE_RESULT_MISS	<< 16)				},
};

/*
 * Very detailed stats (-d -d), covering the instruction cache and the TLB caches:
 */
	struct perf_event_attr very_detailed_attrs[] = {

  { .type = PERF_TYPE_HW_CACHE,
    .config =
	 PERF_COUNT_HW_CACHE_L1I		<<  0  |
	(PERF_COUNT_HW_CACHE_OP_READ		<<  8) |
	(PERF_COUNT_HW_CACHE_RESULT_ACCESS	<< 16)				},

  { .type = PERF_TYPE_HW_CACHE,
    .config =
	 PERF_COUNT_HW_CACHE_L1I		<<  0  |
	(PERF_COUNT_HW_CACHE_OP_READ		<<  8) |
	(PERF_COUNT_HW_CACHE_RESULT_MISS	<< 16)				},

  { .type = PERF_TYPE_HW_CACHE,
    .config =
	 PERF_COUNT_HW_CACHE_DTLB		<<  0  |
	(PERF_COUNT_HW_CACHE_OP_READ		<<  8) |
	(PERF_COUNT_HW_CACHE_RESULT_ACCESS	<< 16)				},

  { .type = PERF_TYPE_HW_CACHE,
    .config =
	 PERF_COUNT_HW_CACHE_DTLB		<<  0  |
	(PERF_COUNT_HW_CACHE_OP_READ		<<  8) |
	(PERF_COUNT_HW_CACHE_RESULT_MISS	<< 16)				},

  { .type = PERF_TYPE_HW_CACHE,
    .config =
	 PERF_COUNT_HW_CACHE_ITLB		<<  0  |
	(PERF_COUNT_HW_CACHE_OP_READ		<<  8) |
	(PERF_COUNT_HW_CACHE_RESULT_ACCESS	<< 16)				},

  { .type = PERF_TYPE_HW_CACHE,
    .config =
	 PERF_COUNT_HW_CACHE_ITLB		<<  0  |
	(PERF_COUNT_HW_CACHE_OP_READ		<<  8) |
	(PERF_COUNT_HW_CACHE_RESULT_MISS	<< 16)				},

};

/*
 * Very, very detailed stats (-d -d -d), adding prefetch events:
 */
	struct perf_event_attr very_very_detailed_attrs[] = {

  { .type = PERF_TYPE_HW_CACHE,
    .config =
	 PERF_COUNT_HW_CACHE_L1D		<<  0  |
	(PERF_COUNT_HW_CACHE_OP_PREFETCH	<<  8) |
	(PERF_COUNT_HW_CACHE_RESULT_ACCESS	<< 16)				},

  { .type = PERF_TYPE_HW_CACHE,
    .config =
	 PERF_COUNT_HW_CACHE_L1D		<<  0  |
	(PERF_COUNT_HW_CACHE_OP_PREFETCH	<<  8) |
	(PERF_COUNT_HW_CACHE_RESULT_MISS	<< 16)				},
};

	/* Set attrs if no event is selected and !null_run: */
	if (null_run)
		return 0;

	if (transaction_run) {
		if (pmu_have_event("cpu", "cycles-ct") &&
		    pmu_have_event("cpu", "el-start"))
			err = parse_events(evsel_list, transaction_attrs, NULL);
		else
			err = parse_events(evsel_list, transaction_limited_attrs, NULL);
		if (err) {
			fprintf(stderr, "Cannot set up transaction events\n");
			return -1;
		}
		return 0;
	}

	if (topdown_run) {
		char *str = NULL;
		bool warn = false;

		if (stat_config.aggr_mode != AGGR_GLOBAL &&
		    stat_config.aggr_mode != AGGR_CORE) {
			pr_err("top down event configuration requires --per-core mode\n");
			return -1;
		}
		stat_config.aggr_mode = AGGR_CORE;
		if (nr_cgroups || !target__has_cpu(&target)) {
			pr_err("top down event configuration requires system-wide mode (-a)\n");
			return -1;
		}

		if (!force_metric_only)
			metric_only = true;
		if (topdown_filter_events(topdown_attrs, &str,
				arch_topdown_check_group(&warn)) < 0) {
			pr_err("Out of memory\n");
			return -1;
		}
		if (topdown_attrs[0] && str) {
			if (warn)
				arch_topdown_group_warn();
			err = parse_events(evsel_list, str, NULL);
			if (err) {
				fprintf(stderr,
					"Cannot set up top down events %s: %d\n",
					str, err);
				free(str);
				return -1;
			}
		} else {
			fprintf(stderr, "System does not support topdown\n");
			return -1;
		}
		free(str);
	}

	if (!evsel_list->nr_entries) {
		if (target__has_cpu(&target))
			default_attrs0[0].config = PERF_COUNT_SW_CPU_CLOCK;

		if (perf_evlist__add_default_attrs(evsel_list, default_attrs0) < 0)
			return -1;
		if (pmu_have_event("cpu", "stalled-cycles-frontend")) {
			if (perf_evlist__add_default_attrs(evsel_list,
						frontend_attrs) < 0)
				return -1;
		}
		if (pmu_have_event("cpu", "stalled-cycles-backend")) {
			if (perf_evlist__add_default_attrs(evsel_list,
						backend_attrs) < 0)
				return -1;
		}
		if (perf_evlist__add_default_attrs(evsel_list, default_attrs1) < 0)
			return -1;
	}

	/* Detailed events get appended to the event list: */

	if (detailed_run <  1)
		return 0;

	/* Append detailed run extra attributes: */
	if (perf_evlist__add_default_attrs(evsel_list, detailed_attrs) < 0)
		return -1;

	if (detailed_run < 2)
		return 0;

	/* Append very detailed run extra attributes: */
	if (perf_evlist__add_default_attrs(evsel_list, very_detailed_attrs) < 0)
		return -1;

	if (detailed_run < 3)
		return 0;

	/* Append very, very detailed run extra attributes: */
	return perf_evlist__add_default_attrs(evsel_list, very_very_detailed_attrs);
}

static const char * const stat_record_usage[] = {
	"perf stat record [<options>]",
	NULL,
};

static void init_features(struct perf_session *session)
{
	int feat;

	for (feat = HEADER_FIRST_FEATURE; feat < HEADER_LAST_FEATURE; feat++)
		perf_header__set_feat(&session->header, feat);

	perf_header__clear_feat(&session->header, HEADER_BUILD_ID);
	perf_header__clear_feat(&session->header, HEADER_TRACING_DATA);
	perf_header__clear_feat(&session->header, HEADER_BRANCH_STACK);
	perf_header__clear_feat(&session->header, HEADER_AUXTRACE);
}

static int __cmd_record(int argc, const char **argv)
{
	struct perf_session *session;
	struct perf_data_file *file = &perf_stat.file;

	argc = parse_options(argc, argv, stat_options, stat_record_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (output_name)
		file->path = output_name;

	if (run_count != 1 || forever) {
		pr_err("Cannot use -r option with perf stat record.\n");
		return -1;
	}

	session = perf_session__new(file, false, NULL);
	if (session == NULL) {
		pr_err("Perf session creation failed.\n");
		return -1;
	}

	init_features(session);

	session->evlist   = evsel_list;
	perf_stat.session = session;
	perf_stat.record  = true;
	return argc;
}

static int process_stat_round_event(struct perf_tool *tool __maybe_unused,
				    union perf_event *event,
				    struct perf_session *session)
{
	struct stat_round_event *stat_round = &event->stat_round;
	struct perf_evsel *counter;
	struct timespec tsh, *ts = NULL;
	const char **argv = session->header.env.cmdline_argv;
	int argc = session->header.env.nr_cmdline;

	evlist__for_each_entry(evsel_list, counter)
		perf_stat_process_counter(&stat_config, counter);

	if (stat_round->type == PERF_STAT_ROUND_TYPE__FINAL)
		update_stats(&walltime_nsecs_stats, stat_round->time);

	if (stat_config.interval && stat_round->time) {
		tsh.tv_sec  = stat_round->time / NSEC_PER_SEC;
		tsh.tv_nsec = stat_round->time % NSEC_PER_SEC;
		ts = &tsh;
	}

	print_counters(ts, argc, argv);
	return 0;
}

static
int process_stat_config_event(struct perf_tool *tool,
			      union perf_event *event,
			      struct perf_session *session __maybe_unused)
{
	struct perf_stat *st = container_of(tool, struct perf_stat, tool);

	perf_event__read_stat_config(&stat_config, &event->stat_config);

	if (cpu_map__empty(st->cpus)) {
		if (st->aggr_mode != AGGR_UNSET)
			pr_warning("warning: processing task data, aggregation mode not set\n");
		return 0;
	}

	if (st->aggr_mode != AGGR_UNSET)
		stat_config.aggr_mode = st->aggr_mode;

	if (perf_stat.file.is_pipe)
		perf_stat_init_aggr_mode();
	else
		perf_stat_init_aggr_mode_file(st);

	return 0;
}

static int set_maps(struct perf_stat *st)
{
	if (!st->cpus || !st->threads)
		return 0;

	if (WARN_ONCE(st->maps_allocated, "stats double allocation\n"))
		return -EINVAL;

	perf_evlist__set_maps(evsel_list, st->cpus, st->threads);

	if (perf_evlist__alloc_stats(evsel_list, true))
		return -ENOMEM;

	st->maps_allocated = true;
	return 0;
}

static
int process_thread_map_event(struct perf_tool *tool,
			     union perf_event *event,
			     struct perf_session *session __maybe_unused)
{
	struct perf_stat *st = container_of(tool, struct perf_stat, tool);

	if (st->threads) {
		pr_warning("Extra thread map event, ignoring.\n");
		return 0;
	}

	st->threads = thread_map__new_event(&event->thread_map);
	if (!st->threads)
		return -ENOMEM;

	return set_maps(st);
}

static
int process_cpu_map_event(struct perf_tool *tool,
			  union perf_event *event,
			  struct perf_session *session __maybe_unused)
{
	struct perf_stat *st = container_of(tool, struct perf_stat, tool);
	struct cpu_map *cpus;

	if (st->cpus) {
		pr_warning("Extra cpu map event, ignoring.\n");
		return 0;
	}

	cpus = cpu_map__new_data(&event->cpu_map.data);
	if (!cpus)
		return -ENOMEM;

	st->cpus = cpus;
	return set_maps(st);
}

static const char * const stat_report_usage[] = {
	"perf stat report [<options>]",
	NULL,
};

static struct perf_stat perf_stat = {
	.tool = {
		.attr		= perf_event__process_attr,
		.event_update	= perf_event__process_event_update,
		.thread_map	= process_thread_map_event,
		.cpu_map	= process_cpu_map_event,
		.stat_config	= process_stat_config_event,
		.stat		= perf_event__process_stat_event,
		.stat_round	= process_stat_round_event,
	},
	.aggr_mode = AGGR_UNSET,
};

static int __cmd_report(int argc, const char **argv)
{
	struct perf_session *session;
	const struct option options[] = {
	OPT_STRING('i', "input", &input_name, "file", "input file name"),
	OPT_SET_UINT(0, "per-socket", &perf_stat.aggr_mode,
		     "aggregate counts per processor socket", AGGR_SOCKET),
	OPT_SET_UINT(0, "per-core", &perf_stat.aggr_mode,
		     "aggregate counts per physical processor core", AGGR_CORE),
	OPT_SET_UINT('A', "no-aggr", &perf_stat.aggr_mode,
		     "disable CPU count aggregation", AGGR_NONE),
	OPT_END()
	};
	struct stat st;
	int ret;

	argc = parse_options(argc, argv, options, stat_report_usage, 0);

	if (!input_name || !strlen(input_name)) {
		if (!fstat(STDIN_FILENO, &st) && S_ISFIFO(st.st_mode))
			input_name = "-";
		else
			input_name = "perf.data";
	}

	perf_stat.file.path = input_name;
	perf_stat.file.mode = PERF_DATA_MODE_READ;

	session = perf_session__new(&perf_stat.file, false, &perf_stat.tool);
	if (session == NULL)
		return -1;

	perf_stat.session  = session;
	stat_config.output = stderr;
	evsel_list         = session->evlist;

	ret = perf_session__process_events(session);
	if (ret)
		return ret;

	perf_session__delete(session);
	return 0;
}

int cmd_stat(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const char * const stat_usage[] = {
		"perf stat [<options>] [<command>]",
		NULL
	};
	int status = -EINVAL, run_idx;
	const char *mode;
	FILE *output = stderr;
	unsigned int interval;
	const char * const stat_subcommands[] = { "record", "report" };

	setlocale(LC_ALL, "");

	evsel_list = perf_evlist__new();
	if (evsel_list == NULL)
		return -ENOMEM;

	parse_events__shrink_config_terms();
	argc = parse_options_subcommand(argc, argv, stat_options, stat_subcommands,
					(const char **) stat_usage,
					PARSE_OPT_STOP_AT_NON_OPTION);
	perf_stat__init_shadow_stats();

	if (csv_sep) {
		csv_output = true;
		if (!strcmp(csv_sep, "\\t"))
			csv_sep = "\t";
	} else
		csv_sep = DEFAULT_SEPARATOR;

	if (argc && !strncmp(argv[0], "rec", 3)) {
		argc = __cmd_record(argc, argv);
		if (argc < 0)
			return -1;
	} else if (argc && !strncmp(argv[0], "rep", 3))
		return __cmd_report(argc, argv);

	interval = stat_config.interval;

	/*
	 * For record command the -o is already taken care of.
	 */
	if (!STAT_RECORD && output_name && strcmp(output_name, "-"))
		output = NULL;

	if (output_name && output_fd) {
		fprintf(stderr, "cannot use both --output and --log-fd\n");
		parse_options_usage(stat_usage, stat_options, "o", 1);
		parse_options_usage(NULL, stat_options, "log-fd", 0);
		goto out;
	}

	if (metric_only && stat_config.aggr_mode == AGGR_THREAD) {
		fprintf(stderr, "--metric-only is not supported with --per-thread\n");
		goto out;
	}

	if (metric_only && run_count > 1) {
		fprintf(stderr, "--metric-only is not supported with -r\n");
		goto out;
	}

	if (output_fd < 0) {
		fprintf(stderr, "argument to --log-fd must be a > 0\n");
		parse_options_usage(stat_usage, stat_options, "log-fd", 0);
		goto out;
	}

	if (!output) {
		struct timespec tm;
		mode = append_file ? "a" : "w";

		output = fopen(output_name, mode);
		if (!output) {
			perror("failed to create output file");
			return -1;
		}
		clock_gettime(CLOCK_REALTIME, &tm);
		fprintf(output, "# started on %s\n", ctime(&tm.tv_sec));
	} else if (output_fd > 0) {
		mode = append_file ? "a" : "w";
		output = fdopen(output_fd, mode);
		if (!output) {
			perror("Failed opening logfd");
			return -errno;
		}
	}

	stat_config.output = output;

	/*
	 * let the spreadsheet do the pretty-printing
	 */
	if (csv_output) {
		/* User explicitly passed -B? */
		if (big_num_opt == 1) {
			fprintf(stderr, "-B option not supported with -x\n");
			parse_options_usage(stat_usage, stat_options, "B", 1);
			parse_options_usage(NULL, stat_options, "x", 1);
			goto out;
		} else /* Nope, so disable big number formatting */
			big_num = false;
	} else if (big_num_opt == 0) /* User passed --no-big-num */
		big_num = false;

	/* Make system wide (-a) the default target. */
	if (!argc && target__none(&target))
		target.system_wide = true;

	if (run_count < 0) {
		pr_err("Run count must be a positive number\n");
		parse_options_usage(stat_usage, stat_options, "r", 1);
		goto out;
	} else if (run_count == 0) {
		forever = true;
		run_count = 1;
	}

	if ((stat_config.aggr_mode == AGGR_THREAD) && !target__has_task(&target)) {
		fprintf(stderr, "The --per-thread option is only available "
			"when monitoring via -p -t options.\n");
		parse_options_usage(NULL, stat_options, "p", 1);
		parse_options_usage(NULL, stat_options, "t", 1);
		goto out;
	}

	/*
	 * no_aggr, cgroup are for system-wide only
	 * --per-thread is aggregated per thread, we dont mix it with cpu mode
	 */
	if (((stat_config.aggr_mode != AGGR_GLOBAL &&
	      stat_config.aggr_mode != AGGR_THREAD) || nr_cgroups) &&
	    !target__has_cpu(&target)) {
		fprintf(stderr, "both cgroup and no-aggregation "
			"modes only available in system-wide mode\n");

		parse_options_usage(stat_usage, stat_options, "G", 1);
		parse_options_usage(NULL, stat_options, "A", 1);
		parse_options_usage(NULL, stat_options, "a", 1);
		goto out;
	}

	if (add_default_attributes())
		goto out;

	target__validate(&target);

	if (perf_evlist__create_maps(evsel_list, &target) < 0) {
		if (target__has_task(&target)) {
			pr_err("Problems finding threads of monitor\n");
			parse_options_usage(stat_usage, stat_options, "p", 1);
			parse_options_usage(NULL, stat_options, "t", 1);
		} else if (target__has_cpu(&target)) {
			perror("failed to parse CPUs map");
			parse_options_usage(stat_usage, stat_options, "C", 1);
			parse_options_usage(NULL, stat_options, "a", 1);
		}
		goto out;
	}

	/*
	 * Initialize thread_map with comm names,
	 * so we could print it out on output.
	 */
	if (stat_config.aggr_mode == AGGR_THREAD)
		thread_map__read_comms(evsel_list->threads);

	if (interval && interval < 100) {
		if (interval < 10) {
			pr_err("print interval must be >= 10ms\n");
			parse_options_usage(stat_usage, stat_options, "I", 1);
			goto out;
		} else
			pr_warning("print interval < 100ms. "
				   "The overhead percentage could be high in some cases. "
				   "Please proceed with caution.\n");
	}

	if (perf_evlist__alloc_stats(evsel_list, interval))
		goto out;

	if (perf_stat_init_aggr_mode())
		goto out;

	/*
	 * We dont want to block the signals - that would cause
	 * child tasks to inherit that and Ctrl-C would not work.
	 * What we want is for Ctrl-C to work in the exec()-ed
	 * task, but being ignored by perf stat itself:
	 */
	atexit(sig_atexit);
	if (!forever)
		signal(SIGINT,  skip_signal);
	signal(SIGCHLD, skip_signal);
	signal(SIGALRM, skip_signal);
	signal(SIGABRT, skip_signal);

	status = 0;
	for (run_idx = 0; forever || run_idx < run_count; run_idx++) {
		if (run_count != 1 && verbose)
			fprintf(output, "[ perf stat: executing run #%d ... ]\n",
				run_idx + 1);

		status = run_perf_stat(argc, argv);
		if (forever && status != -1) {
			print_counters(NULL, argc, argv);
			perf_stat__reset_stats();
		}
	}

	if (!forever && status != -1 && !interval)
		print_counters(NULL, argc, argv);

	if (STAT_RECORD) {
		/*
		 * We synthesize the kernel mmap record just so that older tools
		 * don't emit warnings about not being able to resolve symbols
		 * due to /proc/sys/kernel/kptr_restrict settings and instear provide
		 * a saner message about no samples being in the perf.data file.
		 *
		 * This also serves to suppress a warning about f_header.data.size == 0
		 * in header.c at the moment 'perf stat record' gets introduced, which
		 * is not really needed once we start adding the stat specific PERF_RECORD_
		 * records, but the need to suppress the kptr_restrict messages in older
		 * tools remain  -acme
		 */
		int fd = perf_data_file__fd(&perf_stat.file);
		int err = perf_event__synthesize_kernel_mmap((void *)&perf_stat,
							     process_synthesized_event,
							     &perf_stat.session->machines.host);
		if (err) {
			pr_warning("Couldn't synthesize the kernel mmap record, harmless, "
				   "older tools may produce warnings about this file\n.");
		}

		if (!interval) {
			if (WRITE_STAT_ROUND_EVENT(walltime_nsecs_stats.max, FINAL))
				pr_err("failed to write stat round event\n");
		}

		if (!perf_stat.file.is_pipe) {
			perf_stat.session->header.data_size += perf_stat.bytes_written;
			perf_session__write_header(perf_stat.session, evsel_list, fd, true);
		}

		perf_session__delete(perf_stat.session);
	}

	perf_stat__exit_aggr_mode();
	perf_evlist__free_stats(evsel_list);
out:
	perf_evlist__delete(evsel_list);
	return status;
}
