// SPDX-License-Identifier: GPL-2.0-only
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
 */

#include "builtin.h"
#include "perf.h"
#include "util/cgroup.h"
#include <subcmd/parse-options.h>
#include "util/parse-events.h"
#include "util/pmu.h"
#include "util/event.h"
#include "util/evlist.h"
#include "util/evlist-hybrid.h"
#include "util/evsel.h"
#include "util/debug.h"
#include "util/color.h"
#include "util/stat.h"
#include "util/header.h"
#include "util/cpumap.h"
#include "util/thread_map.h"
#include "util/counts.h"
#include "util/topdown.h"
#include "util/session.h"
#include "util/tool.h"
#include "util/string2.h"
#include "util/metricgroup.h"
#include "util/synthetic-events.h"
#include "util/target.h"
#include "util/time-utils.h"
#include "util/top.h"
#include "util/affinity.h"
#include "util/pfm.h"
#include "util/bpf_counter.h"
#include "util/iostat.h"
#include "util/pmu-hybrid.h"
#include "asm/bug.h"

#include <linux/time64.h>
#include <linux/zalloc.h>
#include <api/fs/fs.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <linux/err.h>

#include <linux/ctype.h>
#include <perf/evlist.h>

#define DEFAULT_SEPARATOR	" "
#define FREEZE_ON_SMI_PATH	"devices/cpu/freeze_on_smi"

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

static const char *topdown_metric_attrs[] = {
	"slots",
	"topdown-retiring",
	"topdown-bad-spec",
	"topdown-fe-bound",
	"topdown-be-bound",
	NULL,
};

static const char *topdown_metric_L2_attrs[] = {
	"slots",
	"topdown-retiring",
	"topdown-bad-spec",
	"topdown-fe-bound",
	"topdown-be-bound",
	"topdown-heavy-ops",
	"topdown-br-mispredict",
	"topdown-fetch-lat",
	"topdown-mem-bound",
	NULL,
};

#define TOPDOWN_MAX_LEVEL			2

static const char *smi_cost_attrs = {
	"{"
	"msr/aperf/,"
	"msr/smi/,"
	"cycles"
	"}"
};

static struct evlist	*evsel_list;
static bool all_counters_use_bpf = true;

static struct target target = {
	.uid	= UINT_MAX,
};

#define METRIC_ONLY_LEN 20

static volatile pid_t		child_pid			= -1;
static int			detailed_run			=  0;
static bool			transaction_run;
static bool			topdown_run			= false;
static bool			smi_cost			= false;
static bool			smi_reset			= false;
static int			big_num_opt			=  -1;
static bool			group				= false;
static const char		*pre_cmd			= NULL;
static const char		*post_cmd			= NULL;
static bool			sync_run			= false;
static bool			forever				= false;
static bool			force_metric_only		= false;
static struct timespec		ref_time;
static bool			append_file;
static bool			interval_count;
static const char		*output_name;
static int			output_fd;
static char			*metrics;

struct perf_stat {
	bool			 record;
	struct perf_data	 data;
	struct perf_session	*session;
	u64			 bytes_written;
	struct perf_tool	 tool;
	bool			 maps_allocated;
	struct perf_cpu_map	*cpus;
	struct perf_thread_map *threads;
	enum aggr_mode		 aggr_mode;
};

static struct perf_stat		perf_stat;
#define STAT_RECORD		perf_stat.record

static volatile int done = 0;

static struct perf_stat_config stat_config = {
	.aggr_mode		= AGGR_GLOBAL,
	.scale			= true,
	.unit_width		= 4, /* strlen("unit") */
	.run_count		= 1,
	.metric_only_len	= METRIC_ONLY_LEN,
	.walltime_nsecs_stats	= &walltime_nsecs_stats,
	.ru_stats		= &ru_stats,
	.big_num		= true,
	.ctl_fd			= -1,
	.ctl_fd_ack		= -1,
	.iostat_run		= false,
};

static bool cpus_map_matched(struct evsel *a, struct evsel *b)
{
	if (!a->core.cpus && !b->core.cpus)
		return true;

	if (!a->core.cpus || !b->core.cpus)
		return false;

	if (perf_cpu_map__nr(a->core.cpus) != perf_cpu_map__nr(b->core.cpus))
		return false;

	for (int i = 0; i < perf_cpu_map__nr(a->core.cpus); i++) {
		if (perf_cpu_map__cpu(a->core.cpus, i).cpu !=
		    perf_cpu_map__cpu(b->core.cpus, i).cpu)
			return false;
	}

	return true;
}

static void evlist__check_cpu_maps(struct evlist *evlist)
{
	struct evsel *evsel, *pos, *leader;
	char buf[1024];

	if (evlist__has_hybrid(evlist))
		evlist__warn_hybrid_group(evlist);

	evlist__for_each_entry(evlist, evsel) {
		leader = evsel__leader(evsel);

		/* Check that leader matches cpus with each member. */
		if (leader == evsel)
			continue;
		if (cpus_map_matched(leader, evsel))
			continue;

		/* If there's mismatch disable the group and warn user. */
		WARN_ONCE(1, "WARNING: grouped events cpus do not match, disabling group:\n");
		evsel__group_desc(leader, buf, sizeof(buf));
		pr_warning("  %s\n", buf);

		if (verbose) {
			cpu_map__snprint(leader->core.cpus, buf, sizeof(buf));
			pr_warning("     %s: %s\n", leader->name, buf);
			cpu_map__snprint(evsel->core.cpus, buf, sizeof(buf));
			pr_warning("     %s: %s\n", evsel->name, buf);
		}

		for_each_group_evsel(pos, leader)
			evsel__remove_from_group(pos, leader);
	}
}

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
	evlist__reset_stats(evsel_list);
	perf_stat__reset_shadow_stats();
}

static int process_synthesized_event(struct perf_tool *tool __maybe_unused,
				     union perf_event *event,
				     struct perf_sample *sample __maybe_unused,
				     struct machine *machine __maybe_unused)
{
	if (perf_data__write(&perf_stat.data, event, event->header.size) < 0) {
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

#define SID(e, x, y) xyarray__entry(e->core.sample_id, x, y)

static int evsel__write_stat_event(struct evsel *counter, int cpu_map_idx, u32 thread,
				   struct perf_counts_values *count)
{
	struct perf_sample_id *sid = SID(counter, cpu_map_idx, thread);
	struct perf_cpu cpu = perf_cpu_map__cpu(evsel__cpus(counter), cpu_map_idx);

	return perf_event__synthesize_stat(NULL, cpu, thread, sid->id, count,
					   process_synthesized_event, NULL);
}

static int read_single_counter(struct evsel *counter, int cpu_map_idx,
			       int thread, struct timespec *rs)
{
	switch(counter->tool_event) {
		case PERF_TOOL_DURATION_TIME: {
			u64 val = rs->tv_nsec + rs->tv_sec*1000000000ULL;
			struct perf_counts_values *count =
				perf_counts(counter->counts, cpu_map_idx, thread);
			count->ena = count->run = val;
			count->val = val;
			return 0;
		}
		case PERF_TOOL_USER_TIME:
		case PERF_TOOL_SYSTEM_TIME: {
			u64 val;
			struct perf_counts_values *count =
				perf_counts(counter->counts, cpu_map_idx, thread);
			if (counter->tool_event == PERF_TOOL_USER_TIME)
				val = ru_stats.ru_utime_usec_stat.mean;
			else
				val = ru_stats.ru_stime_usec_stat.mean;
			count->ena = count->run = val;
			count->val = val;
			return 0;
		}
		default:
		case PERF_TOOL_NONE:
			return evsel__read_counter(counter, cpu_map_idx, thread);
		case PERF_TOOL_MAX:
			/* This should never be reached */
			return 0;
	}
}

/*
 * Read out the results of a single counter:
 * do not aggregate counts across CPUs in system-wide mode
 */
static int read_counter_cpu(struct evsel *counter, struct timespec *rs, int cpu_map_idx)
{
	int nthreads = perf_thread_map__nr(evsel_list->core.threads);
	int thread;

	if (!counter->supported)
		return -ENOENT;

	for (thread = 0; thread < nthreads; thread++) {
		struct perf_counts_values *count;

		count = perf_counts(counter->counts, cpu_map_idx, thread);

		/*
		 * The leader's group read loads data into its group members
		 * (via evsel__read_counter()) and sets their count->loaded.
		 */
		if (!perf_counts__is_loaded(counter->counts, cpu_map_idx, thread) &&
		    read_single_counter(counter, cpu_map_idx, thread, rs)) {
			counter->counts->scaled = -1;
			perf_counts(counter->counts, cpu_map_idx, thread)->ena = 0;
			perf_counts(counter->counts, cpu_map_idx, thread)->run = 0;
			return -1;
		}

		perf_counts__set_loaded(counter->counts, cpu_map_idx, thread, false);

		if (STAT_RECORD) {
			if (evsel__write_stat_event(counter, cpu_map_idx, thread, count)) {
				pr_err("failed to write stat event\n");
				return -1;
			}
		}

		if (verbose > 1) {
			fprintf(stat_config.output,
				"%s: %d: %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
					evsel__name(counter),
					perf_cpu_map__cpu(evsel__cpus(counter),
							  cpu_map_idx).cpu,
					count->val, count->ena, count->run);
		}
	}

	return 0;
}

static int read_affinity_counters(struct timespec *rs)
{
	struct evlist_cpu_iterator evlist_cpu_itr;
	struct affinity saved_affinity, *affinity;

	if (all_counters_use_bpf)
		return 0;

	if (!target__has_cpu(&target) || target__has_per_thread(&target))
		affinity = NULL;
	else if (affinity__setup(&saved_affinity) < 0)
		return -1;
	else
		affinity = &saved_affinity;

	evlist__for_each_cpu(evlist_cpu_itr, evsel_list, affinity) {
		struct evsel *counter = evlist_cpu_itr.evsel;

		if (evsel__is_bpf(counter))
			continue;

		if (!counter->err) {
			counter->err = read_counter_cpu(counter, rs,
							evlist_cpu_itr.cpu_map_idx);
		}
	}
	if (affinity)
		affinity__cleanup(&saved_affinity);

	return 0;
}

static int read_bpf_map_counters(void)
{
	struct evsel *counter;
	int err;

	evlist__for_each_entry(evsel_list, counter) {
		if (!evsel__is_bpf(counter))
			continue;

		err = bpf_counter__read(counter);
		if (err)
			return err;
	}
	return 0;
}

static void read_counters(struct timespec *rs)
{
	struct evsel *counter;

	if (!stat_config.stop_read_counter) {
		if (read_bpf_map_counters() ||
		    read_affinity_counters(rs))
			return;
	}

	evlist__for_each_entry(evsel_list, counter) {
		if (counter->err)
			pr_debug("failed to read counter %s\n", counter->name);
		if (counter->err == 0 && perf_stat_process_counter(&stat_config, counter))
			pr_warning("failed to process counter %s\n", counter->name);
		counter->err = 0;
	}
}

static void process_interval(void)
{
	struct timespec ts, rs;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	diff_timespec(&rs, &ts, &ref_time);

	perf_stat__reset_shadow_per_stat(&rt_stat);
	read_counters(&rs);

	if (STAT_RECORD) {
		if (WRITE_STAT_ROUND_EVENT(rs.tv_sec * NSEC_PER_SEC + rs.tv_nsec, INTERVAL))
			pr_err("failed to write stat round event\n");
	}

	init_stats(&walltime_nsecs_stats);
	update_stats(&walltime_nsecs_stats, stat_config.interval * 1000000ULL);
	print_counters(&rs, 0, NULL);
}

static bool handle_interval(unsigned int interval, int *times)
{
	if (interval) {
		process_interval();
		if (interval_count && !(--(*times)))
			return true;
	}
	return false;
}

static int enable_counters(void)
{
	struct evsel *evsel;
	int err;

	evlist__for_each_entry(evsel_list, evsel) {
		if (!evsel__is_bpf(evsel))
			continue;

		err = bpf_counter__enable(evsel);
		if (err)
			return err;
	}

	if (stat_config.initial_delay < 0) {
		pr_info(EVLIST_DISABLED_MSG);
		return 0;
	}

	if (stat_config.initial_delay > 0) {
		pr_info(EVLIST_DISABLED_MSG);
		usleep(stat_config.initial_delay * USEC_PER_MSEC);
	}

	/*
	 * We need to enable counters only if:
	 * - we don't have tracee (attaching to task or cpu)
	 * - we have initial delay configured
	 */
	if (!target__none(&target) || stat_config.initial_delay) {
		if (!all_counters_use_bpf)
			evlist__enable(evsel_list);
		if (stat_config.initial_delay > 0)
			pr_info(EVLIST_ENABLED_MSG);
	}
	return 0;
}

static void disable_counters(void)
{
	struct evsel *counter;

	/*
	 * If we don't have tracee (attaching to task or cpu), counters may
	 * still be running. To get accurate group ratios, we must stop groups
	 * from counting before reading their constituent counters.
	 */
	if (!target__none(&target)) {
		evlist__for_each_entry(evsel_list, counter)
			bpf_counter__disable(counter);
		if (!all_counters_use_bpf)
			evlist__disable(evsel_list);
	}
}

static volatile int workload_exec_errno;

/*
 * evlist__prepare_workload will send a SIGUSR1
 * if the fork fails, since we asked by setting its
 * want_signal to true.
 */
static void workload_exec_failed_signal(int signo __maybe_unused, siginfo_t *info,
					void *ucontext __maybe_unused)
{
	workload_exec_errno = info->si_value.sival_int;
}

static bool evsel__should_store_id(struct evsel *counter)
{
	return STAT_RECORD || counter->core.attr.read_format & PERF_FORMAT_ID;
}

static bool is_target_alive(struct target *_target,
			    struct perf_thread_map *threads)
{
	struct stat st;
	int i;

	if (!target__has_task(_target))
		return true;

	for (i = 0; i < threads->nr; i++) {
		char path[PATH_MAX];

		scnprintf(path, PATH_MAX, "%s/%d", procfs__mountpoint(),
			  threads->map[i].pid);

		if (!stat(path, &st))
			return true;
	}

	return false;
}

static void process_evlist(struct evlist *evlist, unsigned int interval)
{
	enum evlist_ctl_cmd cmd = EVLIST_CTL_CMD_UNSUPPORTED;

	if (evlist__ctlfd_process(evlist, &cmd) > 0) {
		switch (cmd) {
		case EVLIST_CTL_CMD_ENABLE:
			__fallthrough;
		case EVLIST_CTL_CMD_DISABLE:
			if (interval)
				process_interval();
			break;
		case EVLIST_CTL_CMD_SNAPSHOT:
		case EVLIST_CTL_CMD_ACK:
		case EVLIST_CTL_CMD_UNSUPPORTED:
		case EVLIST_CTL_CMD_EVLIST:
		case EVLIST_CTL_CMD_STOP:
		case EVLIST_CTL_CMD_PING:
		default:
			break;
		}
	}
}

static void compute_tts(struct timespec *time_start, struct timespec *time_stop,
			int *time_to_sleep)
{
	int tts = *time_to_sleep;
	struct timespec time_diff;

	diff_timespec(&time_diff, time_stop, time_start);

	tts -= time_diff.tv_sec * MSEC_PER_SEC +
	       time_diff.tv_nsec / NSEC_PER_MSEC;

	if (tts < 0)
		tts = 0;

	*time_to_sleep = tts;
}

static int dispatch_events(bool forks, int timeout, int interval, int *times)
{
	int child_exited = 0, status = 0;
	int time_to_sleep, sleep_time;
	struct timespec time_start, time_stop;

	if (interval)
		sleep_time = interval;
	else if (timeout)
		sleep_time = timeout;
	else
		sleep_time = 1000;

	time_to_sleep = sleep_time;

	while (!done) {
		if (forks)
			child_exited = waitpid(child_pid, &status, WNOHANG);
		else
			child_exited = !is_target_alive(&target, evsel_list->core.threads) ? 1 : 0;

		if (child_exited)
			break;

		clock_gettime(CLOCK_MONOTONIC, &time_start);
		if (!(evlist__poll(evsel_list, time_to_sleep) > 0)) { /* poll timeout or EINTR */
			if (timeout || handle_interval(interval, times))
				break;
			time_to_sleep = sleep_time;
		} else { /* fd revent */
			process_evlist(evsel_list, interval);
			clock_gettime(CLOCK_MONOTONIC, &time_stop);
			compute_tts(&time_start, &time_stop, &time_to_sleep);
		}
	}

	return status;
}

enum counter_recovery {
	COUNTER_SKIP,
	COUNTER_RETRY,
	COUNTER_FATAL,
};

static enum counter_recovery stat_handle_error(struct evsel *counter)
{
	char msg[BUFSIZ];
	/*
	 * PPC returns ENXIO for HW counters until 2.6.37
	 * (behavior changed with commit b0a873e).
	 */
	if (errno == EINVAL || errno == ENOSYS ||
	    errno == ENOENT || errno == EOPNOTSUPP ||
	    errno == ENXIO) {
		if (verbose > 0)
			ui__warning("%s event is not supported by the kernel.\n",
				    evsel__name(counter));
		counter->supported = false;
		/*
		 * errored is a sticky flag that means one of the counter's
		 * cpu event had a problem and needs to be reexamined.
		 */
		counter->errored = true;

		if ((evsel__leader(counter) != counter) ||
		    !(counter->core.leader->nr_members > 1))
			return COUNTER_SKIP;
	} else if (evsel__fallback(counter, errno, msg, sizeof(msg))) {
		if (verbose > 0)
			ui__warning("%s\n", msg);
		return COUNTER_RETRY;
	} else if (target__has_per_thread(&target) &&
		   evsel_list->core.threads &&
		   evsel_list->core.threads->err_thread != -1) {
		/*
		 * For global --per-thread case, skip current
		 * error thread.
		 */
		if (!thread_map__remove(evsel_list->core.threads,
					evsel_list->core.threads->err_thread)) {
			evsel_list->core.threads->err_thread = -1;
			return COUNTER_RETRY;
		}
	}

	evsel__open_strerror(counter, &target, errno, msg, sizeof(msg));
	ui__error("%s\n", msg);

	if (child_pid != -1)
		kill(child_pid, SIGTERM);
	return COUNTER_FATAL;
}

static int __run_perf_stat(int argc, const char **argv, int run_idx)
{
	int interval = stat_config.interval;
	int times = stat_config.times;
	int timeout = stat_config.timeout;
	char msg[BUFSIZ];
	unsigned long long t0, t1;
	struct evsel *counter;
	size_t l;
	int status = 0;
	const bool forks = (argc > 0);
	bool is_pipe = STAT_RECORD ? perf_stat.data.is_pipe : false;
	struct evlist_cpu_iterator evlist_cpu_itr;
	struct affinity saved_affinity, *affinity = NULL;
	int err;
	bool second_pass = false;

	if (forks) {
		if (evlist__prepare_workload(evsel_list, &target, argv, is_pipe, workload_exec_failed_signal) < 0) {
			perror("failed to prepare workload");
			return -1;
		}
		child_pid = evsel_list->workload.pid;
	}

	if (group)
		evlist__set_leader(evsel_list);

	if (!cpu_map__is_dummy(evsel_list->core.user_requested_cpus)) {
		if (affinity__setup(&saved_affinity) < 0)
			return -1;
		affinity = &saved_affinity;
	}

	evlist__for_each_entry(evsel_list, counter) {
		counter->reset_group = false;
		if (bpf_counter__load(counter, &target))
			return -1;
		if (!evsel__is_bpf(counter))
			all_counters_use_bpf = false;
	}

	evlist__for_each_cpu(evlist_cpu_itr, evsel_list, affinity) {
		counter = evlist_cpu_itr.evsel;

		/*
		 * bperf calls evsel__open_per_cpu() in bperf__load(), so
		 * no need to call it again here.
		 */
		if (target.use_bpf)
			break;

		if (counter->reset_group || counter->errored)
			continue;
		if (evsel__is_bpf(counter))
			continue;
try_again:
		if (create_perf_stat_counter(counter, &stat_config, &target,
					     evlist_cpu_itr.cpu_map_idx) < 0) {

			/*
			 * Weak group failed. We cannot just undo this here
			 * because earlier CPUs might be in group mode, and the kernel
			 * doesn't support mixing group and non group reads. Defer
			 * it to later.
			 * Don't close here because we're in the wrong affinity.
			 */
			if ((errno == EINVAL || errno == EBADF) &&
				evsel__leader(counter) != counter &&
				counter->weak_group) {
				evlist__reset_weak_group(evsel_list, counter, false);
				assert(counter->reset_group);
				second_pass = true;
				continue;
			}

			switch (stat_handle_error(counter)) {
			case COUNTER_FATAL:
				return -1;
			case COUNTER_RETRY:
				goto try_again;
			case COUNTER_SKIP:
				continue;
			default:
				break;
			}

		}
		counter->supported = true;
	}

	if (second_pass) {
		/*
		 * Now redo all the weak group after closing them,
		 * and also close errored counters.
		 */

		/* First close errored or weak retry */
		evlist__for_each_cpu(evlist_cpu_itr, evsel_list, affinity) {
			counter = evlist_cpu_itr.evsel;

			if (!counter->reset_group && !counter->errored)
				continue;

			perf_evsel__close_cpu(&counter->core, evlist_cpu_itr.cpu_map_idx);
		}
		/* Now reopen weak */
		evlist__for_each_cpu(evlist_cpu_itr, evsel_list, affinity) {
			counter = evlist_cpu_itr.evsel;

			if (!counter->reset_group)
				continue;
try_again_reset:
			pr_debug2("reopening weak %s\n", evsel__name(counter));
			if (create_perf_stat_counter(counter, &stat_config, &target,
						     evlist_cpu_itr.cpu_map_idx) < 0) {

				switch (stat_handle_error(counter)) {
				case COUNTER_FATAL:
					return -1;
				case COUNTER_RETRY:
					goto try_again_reset;
				case COUNTER_SKIP:
					continue;
				default:
					break;
				}
			}
			counter->supported = true;
		}
	}
	affinity__cleanup(affinity);

	evlist__for_each_entry(evsel_list, counter) {
		if (!counter->supported) {
			perf_evsel__free_fd(&counter->core);
			continue;
		}

		l = strlen(counter->unit);
		if (l > stat_config.unit_width)
			stat_config.unit_width = l;

		if (evsel__should_store_id(counter) &&
		    evsel__store_ids(counter, evsel_list))
			return -1;
	}

	if (evlist__apply_filters(evsel_list, &counter)) {
		pr_err("failed to set filter \"%s\" on event %s with %d (%s)\n",
			counter->filter, evsel__name(counter), errno,
			str_error_r(errno, msg, sizeof(msg)));
		return -1;
	}

	if (STAT_RECORD) {
		int fd = perf_data__fd(&perf_stat.data);

		if (is_pipe) {
			err = perf_header__write_pipe(perf_data__fd(&perf_stat.data));
		} else {
			err = perf_session__write_header(perf_stat.session, evsel_list,
							 fd, false);
		}

		if (err < 0)
			return err;

		err = perf_event__synthesize_stat_events(&stat_config, NULL, evsel_list,
							 process_synthesized_event, is_pipe);
		if (err < 0)
			return err;
	}

	err = enable_counters();
	if (err)
		return -1;

	/* Exec the command, if any */
	if (forks)
		evlist__start_workload(evsel_list);

	t0 = rdclock();
	clock_gettime(CLOCK_MONOTONIC, &ref_time);

	if (forks) {
		if (interval || timeout || evlist__ctlfd_initialized(evsel_list))
			status = dispatch_events(forks, timeout, interval, &times);
		if (child_pid != -1) {
			if (timeout)
				kill(child_pid, SIGTERM);
			wait4(child_pid, &status, 0, &stat_config.ru_data);
		}

		if (workload_exec_errno) {
			const char *emsg = str_error_r(workload_exec_errno, msg, sizeof(msg));
			pr_err("Workload failed: %s\n", emsg);
			return -1;
		}

		if (WIFSIGNALED(status))
			psignal(WTERMSIG(status), argv[0]);
	} else {
		status = dispatch_events(forks, timeout, interval, &times);
	}

	disable_counters();

	t1 = rdclock();

	if (stat_config.walltime_run_table)
		stat_config.walltime_run[run_idx] = t1 - t0;

	if (interval && stat_config.summary) {
		stat_config.interval = 0;
		stat_config.stop_read_counter = true;
		init_stats(&walltime_nsecs_stats);
		update_stats(&walltime_nsecs_stats, t1 - t0);

		evlist__copy_prev_raw_counts(evsel_list);
		evlist__reset_prev_raw_counts(evsel_list);
		perf_stat__reset_shadow_per_stat(&rt_stat);
	} else {
		update_stats(&walltime_nsecs_stats, t1 - t0);
		update_rusage_stats(&ru_stats, &stat_config.ru_data);
	}

	/*
	 * Closing a group leader splits the group, and as we only disable
	 * group leaders, results in remaining events becoming enabled. To
	 * avoid arbitrary skew, we must read all counters before closing any
	 * group leaders.
	 */
	read_counters(&(struct timespec) { .tv_nsec = t1-t0 });

	/*
	 * We need to keep evsel_list alive, because it's processed
	 * later the evsel_list will be closed after.
	 */
	if (!STAT_RECORD)
		evlist__close(evsel_list);

	return WEXITSTATUS(status);
}

static int run_perf_stat(int argc, const char **argv, int run_idx)
{
	int ret;

	if (pre_cmd) {
		ret = system(pre_cmd);
		if (ret)
			return ret;
	}

	if (sync_run)
		sync();

	ret = __run_perf_stat(argc, argv, run_idx);
	if (ret)
		return ret;

	if (post_cmd) {
		ret = system(post_cmd);
		if (ret)
			return ret;
	}

	return ret;
}

static void print_counters(struct timespec *ts, int argc, const char **argv)
{
	/* Do not print anything if we record to the pipe. */
	if (STAT_RECORD && perf_stat.data.is_pipe)
		return;
	if (stat_config.quiet)
		return;

	evlist__print_counters(evsel_list, &stat_config, &target, ts, argc, argv);
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

void perf_stat__set_big_num(int set)
{
	stat_config.big_num = (set != 0);
}

void perf_stat__set_no_csv_summary(int set)
{
	stat_config.no_csv_summary = (set != 0);
}

static int stat__set_big_num(const struct option *opt __maybe_unused,
			     const char *s __maybe_unused, int unset)
{
	big_num_opt = unset ? 0 : 1;
	perf_stat__set_big_num(!unset);
	return 0;
}

static int enable_metric_only(const struct option *opt __maybe_unused,
			      const char *s __maybe_unused, int unset)
{
	force_metric_only = true;
	stat_config.metric_only = !unset;
	return 0;
}

static int append_metric_groups(const struct option *opt __maybe_unused,
			       const char *str,
			       int unset __maybe_unused)
{
	if (metrics) {
		char *tmp;

		if (asprintf(&tmp, "%s,%s", metrics, str) < 0)
			return -ENOMEM;
		free(metrics);
		metrics = tmp;
	} else {
		metrics = strdup(str);
		if (!metrics)
			return -ENOMEM;
	}
	return 0;
}

static int parse_control_option(const struct option *opt,
				const char *str,
				int unset __maybe_unused)
{
	struct perf_stat_config *config = opt->value;

	return evlist__parse_control(str, &config->ctl_fd, &config->ctl_fd_ack, &config->ctl_fd_close);
}

static int parse_stat_cgroups(const struct option *opt,
			      const char *str, int unset)
{
	if (stat_config.cgroup_list) {
		pr_err("--cgroup and --for-each-cgroup cannot be used together\n");
		return -1;
	}

	return parse_cgroups(opt, str, unset);
}

static int parse_hybrid_type(const struct option *opt,
			     const char *str,
			     int unset __maybe_unused)
{
	struct evlist *evlist = *(struct evlist **)opt->value;

	if (!list_empty(&evlist->core.entries)) {
		fprintf(stderr, "Must define cputype before events/metrics\n");
		return -1;
	}

	evlist->hybrid_pmu_name = perf_pmu__hybrid_type_to_pmu(str);
	if (!evlist->hybrid_pmu_name) {
		fprintf(stderr, "--cputype %s is not supported!\n", str);
		return -1;
	}

	return 0;
}

static struct option stat_options[] = {
	OPT_BOOLEAN('T', "transaction", &transaction_run,
		    "hardware transaction statistics"),
	OPT_CALLBACK('e', "event", &evsel_list, "event",
		     "event selector. use 'perf list' to list available events",
		     parse_events_option),
	OPT_CALLBACK(0, "filter", &evsel_list, "filter",
		     "event filter", parse_filter),
	OPT_BOOLEAN('i', "no-inherit", &stat_config.no_inherit,
		    "child tasks do not inherit counters"),
	OPT_STRING('p', "pid", &target.pid, "pid",
		   "stat events on existing process id"),
	OPT_STRING('t', "tid", &target.tid, "tid",
		   "stat events on existing thread id"),
#ifdef HAVE_BPF_SKEL
	OPT_STRING('b', "bpf-prog", &target.bpf_str, "bpf-prog-id",
		   "stat events on existing bpf program id"),
	OPT_BOOLEAN(0, "bpf-counters", &target.use_bpf,
		    "use bpf program to count events"),
	OPT_STRING(0, "bpf-attr-map", &target.attr_map, "attr-map-path",
		   "path to perf_event_attr map"),
#endif
	OPT_BOOLEAN('a', "all-cpus", &target.system_wide,
		    "system-wide collection from all CPUs"),
	OPT_BOOLEAN('g', "group", &group,
		    "put the counters into a counter group"),
	OPT_BOOLEAN(0, "scale", &stat_config.scale,
		    "Use --no-scale to disable counter scaling for multiplexing"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show counter open errors, etc)"),
	OPT_INTEGER('r', "repeat", &stat_config.run_count,
		    "repeat command and print average + stddev (max: 100, forever: 0)"),
	OPT_BOOLEAN(0, "table", &stat_config.walltime_run_table,
		    "display details about each run (only with -r option)"),
	OPT_BOOLEAN('n', "null", &stat_config.null_run,
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
	OPT_BOOLEAN(0, "no-merge", &stat_config.no_merge, "Do not merge identical named events"),
	OPT_BOOLEAN(0, "hybrid-merge", &stat_config.hybrid_merge,
		    "Merge identical named hybrid events"),
	OPT_STRING('x', "field-separator", &stat_config.csv_sep, "separator",
		   "print counts with custom separator"),
	OPT_BOOLEAN('j', "json-output", &stat_config.json_output,
		   "print counts in JSON format"),
	OPT_CALLBACK('G', "cgroup", &evsel_list, "name",
		     "monitor event in cgroup name only", parse_stat_cgroups),
	OPT_STRING(0, "for-each-cgroup", &stat_config.cgroup_list, "name",
		    "expand events for each cgroup"),
	OPT_STRING('o', "output", &output_name, "file", "output file name"),
	OPT_BOOLEAN(0, "append", &append_file, "append to the output file"),
	OPT_INTEGER(0, "log-fd", &output_fd,
		    "log output to fd, instead of stderr"),
	OPT_STRING(0, "pre", &pre_cmd, "command",
			"command to run prior to the measured command"),
	OPT_STRING(0, "post", &post_cmd, "command",
			"command to run after to the measured command"),
	OPT_UINTEGER('I', "interval-print", &stat_config.interval,
		    "print counts at regular interval in ms "
		    "(overhead is possible for values <= 100ms)"),
	OPT_INTEGER(0, "interval-count", &stat_config.times,
		    "print counts for fixed number of times"),
	OPT_BOOLEAN(0, "interval-clear", &stat_config.interval_clear,
		    "clear screen in between new interval"),
	OPT_UINTEGER(0, "timeout", &stat_config.timeout,
		    "stop workload and print counts after a timeout period in ms (>= 10ms)"),
	OPT_SET_UINT(0, "per-socket", &stat_config.aggr_mode,
		     "aggregate counts per processor socket", AGGR_SOCKET),
	OPT_SET_UINT(0, "per-die", &stat_config.aggr_mode,
		     "aggregate counts per processor die", AGGR_DIE),
	OPT_SET_UINT(0, "per-core", &stat_config.aggr_mode,
		     "aggregate counts per physical processor core", AGGR_CORE),
	OPT_SET_UINT(0, "per-thread", &stat_config.aggr_mode,
		     "aggregate counts per thread", AGGR_THREAD),
	OPT_SET_UINT(0, "per-node", &stat_config.aggr_mode,
		     "aggregate counts per numa node", AGGR_NODE),
	OPT_INTEGER('D', "delay", &stat_config.initial_delay,
		    "ms to wait before starting measurement after program start (-1: start with events disabled)"),
	OPT_CALLBACK_NOOPT(0, "metric-only", &stat_config.metric_only, NULL,
			"Only print computed metrics. No raw values", enable_metric_only),
	OPT_BOOLEAN(0, "metric-no-group", &stat_config.metric_no_group,
		       "don't group metric events, impacts multiplexing"),
	OPT_BOOLEAN(0, "metric-no-merge", &stat_config.metric_no_merge,
		       "don't try to share events between metrics in a group"),
	OPT_BOOLEAN(0, "topdown", &topdown_run,
			"measure top-down statistics"),
	OPT_UINTEGER(0, "td-level", &stat_config.topdown_level,
			"Set the metrics level for the top-down statistics (0: max level)"),
	OPT_BOOLEAN(0, "smi-cost", &smi_cost,
			"measure SMI cost"),
	OPT_CALLBACK('M', "metrics", &evsel_list, "metric/metric group list",
		     "monitor specified metrics or metric groups (separated by ,)",
		     append_metric_groups),
	OPT_BOOLEAN_FLAG(0, "all-kernel", &stat_config.all_kernel,
			 "Configure all used events to run in kernel space.",
			 PARSE_OPT_EXCLUSIVE),
	OPT_BOOLEAN_FLAG(0, "all-user", &stat_config.all_user,
			 "Configure all used events to run in user space.",
			 PARSE_OPT_EXCLUSIVE),
	OPT_BOOLEAN(0, "percore-show-thread", &stat_config.percore_show_thread,
		    "Use with 'percore' event qualifier to show the event "
		    "counts of one hardware thread by sum up total hardware "
		    "threads of same physical core"),
	OPT_BOOLEAN(0, "summary", &stat_config.summary,
		       "print summary for interval mode"),
	OPT_BOOLEAN(0, "no-csv-summary", &stat_config.no_csv_summary,
		       "don't print 'summary' for CSV summary output"),
	OPT_BOOLEAN(0, "quiet", &stat_config.quiet,
			"don't print output (useful with record)"),
	OPT_CALLBACK(0, "cputype", &evsel_list, "hybrid cpu type",
		     "Only enable events on applying cpu with this type "
		     "for hybrid platform (e.g. core or atom)",
		     parse_hybrid_type),
#ifdef HAVE_LIBPFM
	OPT_CALLBACK(0, "pfm-events", &evsel_list, "event",
		"libpfm4 event selector. use 'perf list' to list available events",
		parse_libpfm_events_option),
#endif
	OPT_CALLBACK(0, "control", &stat_config, "fd:ctl-fd[,ack-fd] or fifo:ctl-fifo[,ack-fifo]",
		     "Listen on ctl-fd descriptor for command to control measurement ('enable': enable events, 'disable': disable events).\n"
		     "\t\t\t  Optionally send control command completion ('ack\\n') to ack-fd descriptor.\n"
		     "\t\t\t  Alternatively, ctl-fifo / ack-fifo will be opened and used as ctl-fd / ack-fd.",
		      parse_control_option),
	OPT_CALLBACK_OPTARG(0, "iostat", &evsel_list, &stat_config, "default",
			    "measure I/O performance metrics provided by arch/platform",
			    iostat_parse),
	OPT_END()
};

static const char *const aggr_mode__string[] = {
	[AGGR_CORE] = "core",
	[AGGR_DIE] = "die",
	[AGGR_GLOBAL] = "global",
	[AGGR_NODE] = "node",
	[AGGR_NONE] = "none",
	[AGGR_SOCKET] = "socket",
	[AGGR_THREAD] = "thread",
	[AGGR_UNSET] = "unset",
};

static struct aggr_cpu_id perf_stat__get_socket(struct perf_stat_config *config __maybe_unused,
						struct perf_cpu cpu)
{
	return aggr_cpu_id__socket(cpu, /*data=*/NULL);
}

static struct aggr_cpu_id perf_stat__get_die(struct perf_stat_config *config __maybe_unused,
					     struct perf_cpu cpu)
{
	return aggr_cpu_id__die(cpu, /*data=*/NULL);
}

static struct aggr_cpu_id perf_stat__get_core(struct perf_stat_config *config __maybe_unused,
					      struct perf_cpu cpu)
{
	return aggr_cpu_id__core(cpu, /*data=*/NULL);
}

static struct aggr_cpu_id perf_stat__get_node(struct perf_stat_config *config __maybe_unused,
					      struct perf_cpu cpu)
{
	return aggr_cpu_id__node(cpu, /*data=*/NULL);
}

static struct aggr_cpu_id perf_stat__get_global(struct perf_stat_config *config __maybe_unused,
						struct perf_cpu cpu)
{
	return aggr_cpu_id__global(cpu, /*data=*/NULL);
}

static struct aggr_cpu_id perf_stat__get_cpu(struct perf_stat_config *config __maybe_unused,
					     struct perf_cpu cpu)
{
	return aggr_cpu_id__cpu(cpu, /*data=*/NULL);
}

static struct aggr_cpu_id perf_stat__get_aggr(struct perf_stat_config *config,
					      aggr_get_id_t get_id, struct perf_cpu cpu)
{
	struct aggr_cpu_id id = aggr_cpu_id__empty();

	if (aggr_cpu_id__is_empty(&config->cpus_aggr_map->map[cpu.cpu]))
		config->cpus_aggr_map->map[cpu.cpu] = get_id(config, cpu);

	id = config->cpus_aggr_map->map[cpu.cpu];
	return id;
}

static struct aggr_cpu_id perf_stat__get_socket_cached(struct perf_stat_config *config,
						       struct perf_cpu cpu)
{
	return perf_stat__get_aggr(config, perf_stat__get_socket, cpu);
}

static struct aggr_cpu_id perf_stat__get_die_cached(struct perf_stat_config *config,
						    struct perf_cpu cpu)
{
	return perf_stat__get_aggr(config, perf_stat__get_die, cpu);
}

static struct aggr_cpu_id perf_stat__get_core_cached(struct perf_stat_config *config,
						     struct perf_cpu cpu)
{
	return perf_stat__get_aggr(config, perf_stat__get_core, cpu);
}

static struct aggr_cpu_id perf_stat__get_node_cached(struct perf_stat_config *config,
						     struct perf_cpu cpu)
{
	return perf_stat__get_aggr(config, perf_stat__get_node, cpu);
}

static struct aggr_cpu_id perf_stat__get_global_cached(struct perf_stat_config *config,
						       struct perf_cpu cpu)
{
	return perf_stat__get_aggr(config, perf_stat__get_global, cpu);
}

static struct aggr_cpu_id perf_stat__get_cpu_cached(struct perf_stat_config *config,
						    struct perf_cpu cpu)
{
	return perf_stat__get_aggr(config, perf_stat__get_cpu, cpu);
}

static bool term_percore_set(void)
{
	struct evsel *counter;

	evlist__for_each_entry(evsel_list, counter) {
		if (counter->percore)
			return true;
	}

	return false;
}

static aggr_cpu_id_get_t aggr_mode__get_aggr(enum aggr_mode aggr_mode)
{
	switch (aggr_mode) {
	case AGGR_SOCKET:
		return aggr_cpu_id__socket;
	case AGGR_DIE:
		return aggr_cpu_id__die;
	case AGGR_CORE:
		return aggr_cpu_id__core;
	case AGGR_NODE:
		return aggr_cpu_id__node;
	case AGGR_NONE:
		if (term_percore_set())
			return aggr_cpu_id__core;
		return aggr_cpu_id__cpu;
	case AGGR_GLOBAL:
		return aggr_cpu_id__global;
	case AGGR_THREAD:
	case AGGR_UNSET:
	case AGGR_MAX:
	default:
		return NULL;
	}
}

static aggr_get_id_t aggr_mode__get_id(enum aggr_mode aggr_mode)
{
	switch (aggr_mode) {
	case AGGR_SOCKET:
		return perf_stat__get_socket_cached;
	case AGGR_DIE:
		return perf_stat__get_die_cached;
	case AGGR_CORE:
		return perf_stat__get_core_cached;
	case AGGR_NODE:
		return perf_stat__get_node_cached;
	case AGGR_NONE:
		if (term_percore_set())
			return perf_stat__get_core_cached;
		return perf_stat__get_cpu_cached;
	case AGGR_GLOBAL:
		return perf_stat__get_global_cached;
	case AGGR_THREAD:
	case AGGR_UNSET:
	case AGGR_MAX:
	default:
		return NULL;
	}
}

static int perf_stat_init_aggr_mode(void)
{
	int nr;
	aggr_cpu_id_get_t get_id = aggr_mode__get_aggr(stat_config.aggr_mode);

	if (get_id) {
		bool needs_sort = stat_config.aggr_mode != AGGR_NONE;
		stat_config.aggr_map = cpu_aggr_map__new(evsel_list->core.user_requested_cpus,
							 get_id, /*data=*/NULL, needs_sort);
		if (!stat_config.aggr_map) {
			pr_err("cannot build %s map", aggr_mode__string[stat_config.aggr_mode]);
			return -1;
		}
		stat_config.aggr_get_id = aggr_mode__get_id(stat_config.aggr_mode);
	}

	/*
	 * The evsel_list->cpus is the base we operate on,
	 * taking the highest cpu number to be the size of
	 * the aggregation translate cpumap.
	 */
	if (evsel_list->core.user_requested_cpus)
		nr = perf_cpu_map__max(evsel_list->core.user_requested_cpus).cpu;
	else
		nr = 0;
	stat_config.cpus_aggr_map = cpu_aggr_map__empty_new(nr + 1);
	return stat_config.cpus_aggr_map ? 0 : -ENOMEM;
}

static void cpu_aggr_map__delete(struct cpu_aggr_map *map)
{
	if (map) {
		WARN_ONCE(refcount_read(&map->refcnt) != 0,
			  "cpu_aggr_map refcnt unbalanced\n");
		free(map);
	}
}

static void cpu_aggr_map__put(struct cpu_aggr_map *map)
{
	if (map && refcount_dec_and_test(&map->refcnt))
		cpu_aggr_map__delete(map);
}

static void perf_stat__exit_aggr_mode(void)
{
	cpu_aggr_map__put(stat_config.aggr_map);
	cpu_aggr_map__put(stat_config.cpus_aggr_map);
	stat_config.aggr_map = NULL;
	stat_config.cpus_aggr_map = NULL;
}

static struct aggr_cpu_id perf_env__get_socket_aggr_by_cpu(struct perf_cpu cpu, void *data)
{
	struct perf_env *env = data;
	struct aggr_cpu_id id = aggr_cpu_id__empty();

	if (cpu.cpu != -1)
		id.socket = env->cpu[cpu.cpu].socket_id;

	return id;
}

static struct aggr_cpu_id perf_env__get_die_aggr_by_cpu(struct perf_cpu cpu, void *data)
{
	struct perf_env *env = data;
	struct aggr_cpu_id id = aggr_cpu_id__empty();

	if (cpu.cpu != -1) {
		/*
		 * die_id is relative to socket, so start
		 * with the socket ID and then add die to
		 * make a unique ID.
		 */
		id.socket = env->cpu[cpu.cpu].socket_id;
		id.die = env->cpu[cpu.cpu].die_id;
	}

	return id;
}

static struct aggr_cpu_id perf_env__get_core_aggr_by_cpu(struct perf_cpu cpu, void *data)
{
	struct perf_env *env = data;
	struct aggr_cpu_id id = aggr_cpu_id__empty();

	if (cpu.cpu != -1) {
		/*
		 * core_id is relative to socket and die,
		 * we need a global id. So we set
		 * socket, die id and core id
		 */
		id.socket = env->cpu[cpu.cpu].socket_id;
		id.die = env->cpu[cpu.cpu].die_id;
		id.core = env->cpu[cpu.cpu].core_id;
	}

	return id;
}

static struct aggr_cpu_id perf_env__get_cpu_aggr_by_cpu(struct perf_cpu cpu, void *data)
{
	struct perf_env *env = data;
	struct aggr_cpu_id id = aggr_cpu_id__empty();

	if (cpu.cpu != -1) {
		/*
		 * core_id is relative to socket and die,
		 * we need a global id. So we set
		 * socket, die id and core id
		 */
		id.socket = env->cpu[cpu.cpu].socket_id;
		id.die = env->cpu[cpu.cpu].die_id;
		id.core = env->cpu[cpu.cpu].core_id;
		id.cpu = cpu;
	}

	return id;
}

static struct aggr_cpu_id perf_env__get_node_aggr_by_cpu(struct perf_cpu cpu, void *data)
{
	struct aggr_cpu_id id = aggr_cpu_id__empty();

	id.node = perf_env__numa_node(data, cpu);
	return id;
}

static struct aggr_cpu_id perf_env__get_global_aggr_by_cpu(struct perf_cpu cpu __maybe_unused,
							   void *data __maybe_unused)
{
	struct aggr_cpu_id id = aggr_cpu_id__empty();

	/* it always aggregates to the cpu 0 */
	id.cpu = (struct perf_cpu){ .cpu = 0 };
	return id;
}

static struct aggr_cpu_id perf_stat__get_socket_file(struct perf_stat_config *config __maybe_unused,
						     struct perf_cpu cpu)
{
	return perf_env__get_socket_aggr_by_cpu(cpu, &perf_stat.session->header.env);
}
static struct aggr_cpu_id perf_stat__get_die_file(struct perf_stat_config *config __maybe_unused,
						  struct perf_cpu cpu)
{
	return perf_env__get_die_aggr_by_cpu(cpu, &perf_stat.session->header.env);
}

static struct aggr_cpu_id perf_stat__get_core_file(struct perf_stat_config *config __maybe_unused,
						   struct perf_cpu cpu)
{
	return perf_env__get_core_aggr_by_cpu(cpu, &perf_stat.session->header.env);
}

static struct aggr_cpu_id perf_stat__get_cpu_file(struct perf_stat_config *config __maybe_unused,
						  struct perf_cpu cpu)
{
	return perf_env__get_cpu_aggr_by_cpu(cpu, &perf_stat.session->header.env);
}

static struct aggr_cpu_id perf_stat__get_node_file(struct perf_stat_config *config __maybe_unused,
						   struct perf_cpu cpu)
{
	return perf_env__get_node_aggr_by_cpu(cpu, &perf_stat.session->header.env);
}

static struct aggr_cpu_id perf_stat__get_global_file(struct perf_stat_config *config __maybe_unused,
						     struct perf_cpu cpu)
{
	return perf_env__get_global_aggr_by_cpu(cpu, &perf_stat.session->header.env);
}

static aggr_cpu_id_get_t aggr_mode__get_aggr_file(enum aggr_mode aggr_mode)
{
	switch (aggr_mode) {
	case AGGR_SOCKET:
		return perf_env__get_socket_aggr_by_cpu;
	case AGGR_DIE:
		return perf_env__get_die_aggr_by_cpu;
	case AGGR_CORE:
		return perf_env__get_core_aggr_by_cpu;
	case AGGR_NODE:
		return perf_env__get_node_aggr_by_cpu;
	case AGGR_GLOBAL:
		return perf_env__get_global_aggr_by_cpu;
	case AGGR_NONE:
		return perf_env__get_cpu_aggr_by_cpu;
	case AGGR_THREAD:
	case AGGR_UNSET:
	case AGGR_MAX:
	default:
		return NULL;
	}
}

static aggr_get_id_t aggr_mode__get_id_file(enum aggr_mode aggr_mode)
{
	switch (aggr_mode) {
	case AGGR_SOCKET:
		return perf_stat__get_socket_file;
	case AGGR_DIE:
		return perf_stat__get_die_file;
	case AGGR_CORE:
		return perf_stat__get_core_file;
	case AGGR_NODE:
		return perf_stat__get_node_file;
	case AGGR_GLOBAL:
		return perf_stat__get_global_file;
	case AGGR_NONE:
		return perf_stat__get_cpu_file;
	case AGGR_THREAD:
	case AGGR_UNSET:
	case AGGR_MAX:
	default:
		return NULL;
	}
}

static int perf_stat_init_aggr_mode_file(struct perf_stat *st)
{
	struct perf_env *env = &st->session->header.env;
	aggr_cpu_id_get_t get_id = aggr_mode__get_aggr_file(stat_config.aggr_mode);
	bool needs_sort = stat_config.aggr_mode != AGGR_NONE;

	if (!get_id)
		return 0;

	stat_config.aggr_map = cpu_aggr_map__new(evsel_list->core.user_requested_cpus,
						 get_id, env, needs_sort);
	if (!stat_config.aggr_map) {
		pr_err("cannot build %s map", aggr_mode__string[stat_config.aggr_mode]);
		return -1;
	}
	stat_config.aggr_get_id = aggr_mode__get_id_file(stat_config.aggr_mode);
	return 0;
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

	struct perf_event_attr default_null_attrs[] = {};

	/* Set attrs if no event is selected and !null_run: */
	if (stat_config.null_run)
		return 0;

	if (transaction_run) {
		struct parse_events_error errinfo;
		/* Handle -T as -M transaction. Once platform specific metrics
		 * support has been added to the json files, all architectures
		 * will use this approach. To determine transaction support
		 * on an architecture test for such a metric name.
		 */
		if (metricgroup__has_metric("transaction")) {
			return metricgroup__parse_groups(evsel_list, "transaction",
							 stat_config.metric_no_group,
							 stat_config.metric_no_merge,
							 stat_config.user_requested_cpu_list,
							 stat_config.system_wide,
							 &stat_config.metric_events);
		}

		parse_events_error__init(&errinfo);
		if (pmu_have_event("cpu", "cycles-ct") &&
		    pmu_have_event("cpu", "el-start"))
			err = parse_events(evsel_list, transaction_attrs,
					   &errinfo);
		else
			err = parse_events(evsel_list,
					   transaction_limited_attrs,
					   &errinfo);
		if (err) {
			fprintf(stderr, "Cannot set up transaction events\n");
			parse_events_error__print(&errinfo, transaction_attrs);
		}
		parse_events_error__exit(&errinfo);
		return err ? -1 : 0;
	}

	if (smi_cost) {
		struct parse_events_error errinfo;
		int smi;

		if (sysfs__read_int(FREEZE_ON_SMI_PATH, &smi) < 0) {
			fprintf(stderr, "freeze_on_smi is not supported.\n");
			return -1;
		}

		if (!smi) {
			if (sysfs__write_int(FREEZE_ON_SMI_PATH, 1) < 0) {
				fprintf(stderr, "Failed to set freeze_on_smi.\n");
				return -1;
			}
			smi_reset = true;
		}

		if (!pmu_have_event("msr", "aperf") ||
		    !pmu_have_event("msr", "smi")) {
			fprintf(stderr, "To measure SMI cost, it needs "
				"msr/aperf/, msr/smi/ and cpu/cycles/ support\n");
			return -1;
		}
		if (!force_metric_only)
			stat_config.metric_only = true;

		parse_events_error__init(&errinfo);
		err = parse_events(evsel_list, smi_cost_attrs, &errinfo);
		if (err) {
			parse_events_error__print(&errinfo, smi_cost_attrs);
			fprintf(stderr, "Cannot set up SMI cost events\n");
		}
		parse_events_error__exit(&errinfo);
		return err ? -1 : 0;
	}

	if (topdown_run) {
		const char **metric_attrs = topdown_metric_attrs;
		unsigned int max_level = 1;
		char *str = NULL;
		bool warn = false;
		const char *pmu_name = arch_get_topdown_pmu_name(evsel_list, true);

		if (!force_metric_only)
			stat_config.metric_only = true;

		if (pmu_have_event(pmu_name, topdown_metric_L2_attrs[5])) {
			metric_attrs = topdown_metric_L2_attrs;
			max_level = 2;
		}

		if (stat_config.topdown_level > max_level) {
			pr_err("Invalid top-down metrics level. The max level is %u.\n", max_level);
			return -1;
		} else if (!stat_config.topdown_level)
			stat_config.topdown_level = max_level;

		if (topdown_filter_events(metric_attrs, &str, 1, pmu_name) < 0) {
			pr_err("Out of memory\n");
			return -1;
		}

		if (metric_attrs[0] && str) {
			if (!stat_config.interval && !stat_config.metric_only) {
				fprintf(stat_config.output,
					"Topdown accuracy may decrease when measuring long periods.\n"
					"Please print the result regularly, e.g. -I1000\n");
			}
			goto setup_metrics;
		}

		zfree(&str);

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

		if (topdown_filter_events(topdown_attrs, &str,
				arch_topdown_check_group(&warn),
				pmu_name) < 0) {
			pr_err("Out of memory\n");
			return -1;
		}

		if (topdown_attrs[0] && str) {
			struct parse_events_error errinfo;
			if (warn)
				arch_topdown_group_warn();
setup_metrics:
			parse_events_error__init(&errinfo);
			err = parse_events(evsel_list, str, &errinfo);
			if (err) {
				fprintf(stderr,
					"Cannot set up top down events %s: %d\n",
					str, err);
				parse_events_error__print(&errinfo, str);
				parse_events_error__exit(&errinfo);
				free(str);
				return -1;
			}
			parse_events_error__exit(&errinfo);
		} else {
			fprintf(stderr, "System does not support topdown\n");
			return -1;
		}
		free(str);
	}

	if (!stat_config.topdown_level)
		stat_config.topdown_level = TOPDOWN_MAX_LEVEL;

	if (!evsel_list->core.nr_entries) {
		if (target__has_cpu(&target))
			default_attrs0[0].config = PERF_COUNT_SW_CPU_CLOCK;

		if (evlist__add_default_attrs(evsel_list, default_attrs0) < 0)
			return -1;
		if (pmu_have_event("cpu", "stalled-cycles-frontend")) {
			if (evlist__add_default_attrs(evsel_list, frontend_attrs) < 0)
				return -1;
		}
		if (pmu_have_event("cpu", "stalled-cycles-backend")) {
			if (evlist__add_default_attrs(evsel_list, backend_attrs) < 0)
				return -1;
		}
		if (evlist__add_default_attrs(evsel_list, default_attrs1) < 0)
			return -1;
		/* Platform specific attrs */
		if (evlist__add_default_attrs(evsel_list, default_null_attrs) < 0)
			return -1;
	}

	/* Detailed events get appended to the event list: */

	if (detailed_run <  1)
		return 0;

	/* Append detailed run extra attributes: */
	if (evlist__add_default_attrs(evsel_list, detailed_attrs) < 0)
		return -1;

	if (detailed_run < 2)
		return 0;

	/* Append very detailed run extra attributes: */
	if (evlist__add_default_attrs(evsel_list, very_detailed_attrs) < 0)
		return -1;

	if (detailed_run < 3)
		return 0;

	/* Append very, very detailed run extra attributes: */
	return evlist__add_default_attrs(evsel_list, very_very_detailed_attrs);
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

	perf_header__clear_feat(&session->header, HEADER_DIR_FORMAT);
	perf_header__clear_feat(&session->header, HEADER_BUILD_ID);
	perf_header__clear_feat(&session->header, HEADER_TRACING_DATA);
	perf_header__clear_feat(&session->header, HEADER_BRANCH_STACK);
	perf_header__clear_feat(&session->header, HEADER_AUXTRACE);
}

static int __cmd_record(int argc, const char **argv)
{
	struct perf_session *session;
	struct perf_data *data = &perf_stat.data;

	argc = parse_options(argc, argv, stat_options, stat_record_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (output_name)
		data->path = output_name;

	if (stat_config.run_count != 1 || forever) {
		pr_err("Cannot use -r option with perf stat record.\n");
		return -1;
	}

	session = perf_session__new(data, NULL);
	if (IS_ERR(session)) {
		pr_err("Perf session creation failed\n");
		return PTR_ERR(session);
	}

	init_features(session);

	session->evlist   = evsel_list;
	perf_stat.session = session;
	perf_stat.record  = true;
	return argc;
}

static int process_stat_round_event(struct perf_session *session,
				    union perf_event *event)
{
	struct perf_record_stat_round *stat_round = &event->stat_round;
	struct evsel *counter;
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
int process_stat_config_event(struct perf_session *session,
			      union perf_event *event)
{
	struct perf_tool *tool = session->tool;
	struct perf_stat *st = container_of(tool, struct perf_stat, tool);

	perf_event__read_stat_config(&stat_config, &event->stat_config);

	if (perf_cpu_map__empty(st->cpus)) {
		if (st->aggr_mode != AGGR_UNSET)
			pr_warning("warning: processing task data, aggregation mode not set\n");
		return 0;
	}

	if (st->aggr_mode != AGGR_UNSET)
		stat_config.aggr_mode = st->aggr_mode;

	if (perf_stat.data.is_pipe)
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

	perf_evlist__set_maps(&evsel_list->core, st->cpus, st->threads);

	if (evlist__alloc_stats(&stat_config, evsel_list, /*alloc_raw=*/true))
		return -ENOMEM;

	st->maps_allocated = true;
	return 0;
}

static
int process_thread_map_event(struct perf_session *session,
			     union perf_event *event)
{
	struct perf_tool *tool = session->tool;
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
int process_cpu_map_event(struct perf_session *session,
			  union perf_event *event)
{
	struct perf_tool *tool = session->tool;
	struct perf_stat *st = container_of(tool, struct perf_stat, tool);
	struct perf_cpu_map *cpus;

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
	OPT_SET_UINT(0, "per-die", &perf_stat.aggr_mode,
		     "aggregate counts per processor die", AGGR_DIE),
	OPT_SET_UINT(0, "per-core", &perf_stat.aggr_mode,
		     "aggregate counts per physical processor core", AGGR_CORE),
	OPT_SET_UINT(0, "per-node", &perf_stat.aggr_mode,
		     "aggregate counts per numa node", AGGR_NODE),
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

	perf_stat__init_shadow_stats();

	perf_stat.data.path = input_name;
	perf_stat.data.mode = PERF_DATA_MODE_READ;

	session = perf_session__new(&perf_stat.data, &perf_stat.tool);
	if (IS_ERR(session))
		return PTR_ERR(session);

	perf_stat.session  = session;
	stat_config.output = stderr;
	evsel_list         = session->evlist;

	ret = perf_session__process_events(session);
	if (ret)
		return ret;

	perf_session__delete(session);
	return 0;
}

static void setup_system_wide(int forks)
{
	/*
	 * Make system wide (-a) the default target if
	 * no target was specified and one of following
	 * conditions is met:
	 *
	 *   - there's no workload specified
	 *   - there is workload specified but all requested
	 *     events are system wide events
	 */
	if (!target__none(&target))
		return;

	if (!forks)
		target.system_wide = true;
	else {
		struct evsel *counter;

		evlist__for_each_entry(evsel_list, counter) {
			if (!counter->core.requires_cpu &&
			    strcmp(counter->name, "duration_time")) {
				return;
			}
		}

		if (evsel_list->core.nr_entries)
			target.system_wide = true;
	}
}

int cmd_stat(int argc, const char **argv)
{
	const char * const stat_usage[] = {
		"perf stat [<options>] [<command>]",
		NULL
	};
	int status = -EINVAL, run_idx, err;
	const char *mode;
	FILE *output = stderr;
	unsigned int interval, timeout;
	const char * const stat_subcommands[] = { "record", "report" };
	char errbuf[BUFSIZ];

	setlocale(LC_ALL, "");

	evsel_list = evlist__new();
	if (evsel_list == NULL)
		return -ENOMEM;

	parse_events__shrink_config_terms();

	/* String-parsing callback-based options would segfault when negated */
	set_option_flag(stat_options, 'e', "event", PARSE_OPT_NONEG);
	set_option_flag(stat_options, 'M', "metrics", PARSE_OPT_NONEG);
	set_option_flag(stat_options, 'G', "cgroup", PARSE_OPT_NONEG);

	argc = parse_options_subcommand(argc, argv, stat_options, stat_subcommands,
					(const char **) stat_usage,
					PARSE_OPT_STOP_AT_NON_OPTION);

	if (stat_config.csv_sep) {
		stat_config.csv_output = true;
		if (!strcmp(stat_config.csv_sep, "\\t"))
			stat_config.csv_sep = "\t";
	} else
		stat_config.csv_sep = DEFAULT_SEPARATOR;

	if (argc && strlen(argv[0]) > 2 && strstarts("record", argv[0])) {
		argc = __cmd_record(argc, argv);
		if (argc < 0)
			return -1;
	} else if (argc && strlen(argv[0]) > 2 && strstarts("report", argv[0]))
		return __cmd_report(argc, argv);

	interval = stat_config.interval;
	timeout = stat_config.timeout;

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

	if (stat_config.metric_only && stat_config.aggr_mode == AGGR_THREAD) {
		fprintf(stderr, "--metric-only is not supported with --per-thread\n");
		goto out;
	}

	if (stat_config.metric_only && stat_config.run_count > 1) {
		fprintf(stderr, "--metric-only is not supported with -r\n");
		goto out;
	}

	if (stat_config.walltime_run_table && stat_config.run_count <= 1) {
		fprintf(stderr, "--table is only supported with -r\n");
		parse_options_usage(stat_usage, stat_options, "r", 1);
		parse_options_usage(NULL, stat_options, "table", 0);
		goto out;
	}

	if (output_fd < 0) {
		fprintf(stderr, "argument to --log-fd must be a > 0\n");
		parse_options_usage(stat_usage, stat_options, "log-fd", 0);
		goto out;
	}

	if (!output && !stat_config.quiet) {
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
	if (stat_config.csv_output) {
		/* User explicitly passed -B? */
		if (big_num_opt == 1) {
			fprintf(stderr, "-B option not supported with -x\n");
			parse_options_usage(stat_usage, stat_options, "B", 1);
			parse_options_usage(NULL, stat_options, "x", 1);
			goto out;
		} else /* Nope, so disable big number formatting */
			stat_config.big_num = false;
	} else if (big_num_opt == 0) /* User passed --no-big-num */
		stat_config.big_num = false;

	err = target__validate(&target);
	if (err) {
		target__strerror(&target, err, errbuf, BUFSIZ);
		pr_warning("%s\n", errbuf);
	}

	setup_system_wide(argc);

	/*
	 * Display user/system times only for single
	 * run and when there's specified tracee.
	 */
	if ((stat_config.run_count == 1) && target__none(&target))
		stat_config.ru_display = true;

	if (stat_config.run_count < 0) {
		pr_err("Run count must be a positive number\n");
		parse_options_usage(stat_usage, stat_options, "r", 1);
		goto out;
	} else if (stat_config.run_count == 0) {
		forever = true;
		stat_config.run_count = 1;
	}

	if (stat_config.walltime_run_table) {
		stat_config.walltime_run = zalloc(stat_config.run_count * sizeof(stat_config.walltime_run[0]));
		if (!stat_config.walltime_run) {
			pr_err("failed to setup -r option");
			goto out;
		}
	}

	if ((stat_config.aggr_mode == AGGR_THREAD) &&
		!target__has_task(&target)) {
		if (!target.system_wide || target.cpu_list) {
			fprintf(stderr, "The --per-thread option is only "
				"available when monitoring via -p -t -a "
				"options or only --per-thread.\n");
			parse_options_usage(NULL, stat_options, "p", 1);
			parse_options_usage(NULL, stat_options, "t", 1);
			goto out;
		}
	}

	/*
	 * no_aggr, cgroup are for system-wide only
	 * --per-thread is aggregated per thread, we dont mix it with cpu mode
	 */
	if (((stat_config.aggr_mode != AGGR_GLOBAL &&
	      stat_config.aggr_mode != AGGR_THREAD) ||
	     (nr_cgroups || stat_config.cgroup_list)) &&
	    !target__has_cpu(&target)) {
		fprintf(stderr, "both cgroup and no-aggregation "
			"modes only available in system-wide mode\n");

		parse_options_usage(stat_usage, stat_options, "G", 1);
		parse_options_usage(NULL, stat_options, "A", 1);
		parse_options_usage(NULL, stat_options, "a", 1);
		parse_options_usage(NULL, stat_options, "for-each-cgroup", 0);
		goto out;
	}

	if (stat_config.iostat_run) {
		status = iostat_prepare(evsel_list, &stat_config);
		if (status)
			goto out;
		if (iostat_mode == IOSTAT_LIST) {
			iostat_list(evsel_list, &stat_config);
			goto out;
		} else if (verbose)
			iostat_list(evsel_list, &stat_config);
		if (iostat_mode == IOSTAT_RUN && !target__has_cpu(&target))
			target.system_wide = true;
	}

	if ((stat_config.aggr_mode == AGGR_THREAD) && (target.system_wide))
		target.per_thread = true;

	stat_config.system_wide = target.system_wide;
	if (target.cpu_list) {
		stat_config.user_requested_cpu_list = strdup(target.cpu_list);
		if (!stat_config.user_requested_cpu_list) {
			status = -ENOMEM;
			goto out;
		}
	}

	/*
	 * Metric parsing needs to be delayed as metrics may optimize events
	 * knowing the target is system-wide.
	 */
	if (metrics) {
		metricgroup__parse_groups(evsel_list, metrics,
					stat_config.metric_no_group,
					stat_config.metric_no_merge,
					stat_config.user_requested_cpu_list,
					stat_config.system_wide,
					&stat_config.metric_events);
		zfree(&metrics);
	}
	perf_stat__collect_metric_expr(evsel_list);
	perf_stat__init_shadow_stats();

	if (add_default_attributes())
		goto out;

	if (stat_config.cgroup_list) {
		if (nr_cgroups > 0) {
			pr_err("--cgroup and --for-each-cgroup cannot be used together\n");
			parse_options_usage(stat_usage, stat_options, "G", 1);
			parse_options_usage(NULL, stat_options, "for-each-cgroup", 0);
			goto out;
		}

		if (evlist__expand_cgroup(evsel_list, stat_config.cgroup_list,
					  &stat_config.metric_events, true) < 0) {
			parse_options_usage(stat_usage, stat_options,
					    "for-each-cgroup", 0);
			goto out;
		}
	}

	if (evlist__fix_hybrid_cpus(evsel_list, target.cpu_list)) {
		pr_err("failed to use cpu list %s\n", target.cpu_list);
		goto out;
	}

	target.hybrid = perf_pmu__has_hybrid();
	if (evlist__create_maps(evsel_list, &target) < 0) {
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

	evlist__check_cpu_maps(evsel_list);

	/*
	 * Initialize thread_map with comm names,
	 * so we could print it out on output.
	 */
	if (stat_config.aggr_mode == AGGR_THREAD) {
		thread_map__read_comms(evsel_list->core.threads);
	}

	if (stat_config.aggr_mode == AGGR_NODE)
		cpu__setup_cpunode_map();

	if (stat_config.times && interval)
		interval_count = true;
	else if (stat_config.times && !interval) {
		pr_err("interval-count option should be used together with "
				"interval-print.\n");
		parse_options_usage(stat_usage, stat_options, "interval-count", 0);
		parse_options_usage(stat_usage, stat_options, "I", 1);
		goto out;
	}

	if (timeout && timeout < 100) {
		if (timeout < 10) {
			pr_err("timeout must be >= 10ms.\n");
			parse_options_usage(stat_usage, stat_options, "timeout", 0);
			goto out;
		} else
			pr_warning("timeout < 100ms. "
				   "The overhead percentage could be high in some cases. "
				   "Please proceed with caution.\n");
	}
	if (timeout && interval) {
		pr_err("timeout option is not supported with interval-print.\n");
		parse_options_usage(stat_usage, stat_options, "timeout", 0);
		parse_options_usage(stat_usage, stat_options, "I", 1);
		goto out;
	}

	if (perf_stat_init_aggr_mode())
		goto out;

	if (evlist__alloc_stats(&stat_config, evsel_list, interval))
		goto out;

	/*
	 * Set sample_type to PERF_SAMPLE_IDENTIFIER, which should be harmless
	 * while avoiding that older tools show confusing messages.
	 *
	 * However for pipe sessions we need to keep it zero,
	 * because script's perf_evsel__check_attr is triggered
	 * by attr->sample_type != 0, and we can't run it on
	 * stat sessions.
	 */
	stat_config.identifier = !(STAT_RECORD && perf_stat.data.is_pipe);

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

	if (evlist__initialize_ctlfd(evsel_list, stat_config.ctl_fd, stat_config.ctl_fd_ack))
		goto out;

	/* Enable ignoring missing threads when -p option is defined. */
	evlist__first(evsel_list)->ignore_missing_thread = target.pid;
	status = 0;
	for (run_idx = 0; forever || run_idx < stat_config.run_count; run_idx++) {
		if (stat_config.run_count != 1 && verbose > 0)
			fprintf(output, "[ perf stat: executing run #%d ... ]\n",
				run_idx + 1);

		if (run_idx != 0)
			evlist__reset_prev_raw_counts(evsel_list);

		status = run_perf_stat(argc, argv, run_idx);
		if (forever && status != -1 && !interval) {
			print_counters(NULL, argc, argv);
			perf_stat__reset_stats();
		}
	}

	if (!forever && status != -1 && (!interval || stat_config.summary))
		print_counters(NULL, argc, argv);

	evlist__finalize_ctlfd(evsel_list);

	if (STAT_RECORD) {
		/*
		 * We synthesize the kernel mmap record just so that older tools
		 * don't emit warnings about not being able to resolve symbols
		 * due to /proc/sys/kernel/kptr_restrict settings and instead provide
		 * a saner message about no samples being in the perf.data file.
		 *
		 * This also serves to suppress a warning about f_header.data.size == 0
		 * in header.c at the moment 'perf stat record' gets introduced, which
		 * is not really needed once we start adding the stat specific PERF_RECORD_
		 * records, but the need to suppress the kptr_restrict messages in older
		 * tools remain  -acme
		 */
		int fd = perf_data__fd(&perf_stat.data);

		err = perf_event__synthesize_kernel_mmap((void *)&perf_stat,
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

		if (!perf_stat.data.is_pipe) {
			perf_stat.session->header.data_size += perf_stat.bytes_written;
			perf_session__write_header(perf_stat.session, evsel_list, fd, true);
		}

		evlist__close(evsel_list);
		perf_session__delete(perf_stat.session);
	}

	perf_stat__exit_aggr_mode();
	evlist__free_stats(evsel_list);
out:
	if (stat_config.iostat_run)
		iostat_release(evsel_list);

	zfree(&stat_config.walltime_run);
	zfree(&stat_config.user_requested_cpu_list);

	if (smi_cost && smi_reset)
		sysfs__write_int(FREEZE_ON_SMI_PATH, 0);

	evlist__delete(evsel_list);

	metricgroup__rblist_exit(&stat_config.metric_events);
	evlist__close_control(stat_config.ctl_fd, stat_config.ctl_fd_ack, &stat_config.ctl_fd_close);

	return status;
}
