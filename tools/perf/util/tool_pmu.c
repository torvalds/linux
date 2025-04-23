// SPDX-License-Identifier: GPL-2.0-only
#include "cgroup.h"
#include "counts.h"
#include "cputopo.h"
#include "evsel.h"
#include "pmu.h"
#include "print-events.h"
#include "smt.h"
#include "time-utils.h"
#include "tool_pmu.h"
#include "tsc.h"
#include <api/fs/fs.h>
#include <api/io.h>
#include <internal/threadmap.h>
#include <perf/threadmap.h>
#include <fcntl.h>
#include <strings.h>

static const char *const tool_pmu__event_names[TOOL_PMU__EVENT_MAX] = {
	NULL,
	"duration_time",
	"user_time",
	"system_time",
	"has_pmem",
	"num_cores",
	"num_cpus",
	"num_cpus_online",
	"num_dies",
	"num_packages",
	"slots",
	"smt_on",
	"system_tsc_freq",
};

bool tool_pmu__skip_event(const char *name __maybe_unused)
{
#if !defined(__aarch64__)
	/* The slots event should only appear on arm64. */
	if (strcasecmp(name, "slots") == 0)
		return true;
#endif
#if !defined(__i386__) && !defined(__x86_64__)
	/* The system_tsc_freq event should only appear on x86. */
	if (strcasecmp(name, "system_tsc_freq") == 0)
		return true;
#endif
	return false;
}

int tool_pmu__num_skip_events(void)
{
	int num = 0;

#if !defined(__aarch64__)
	num++;
#endif
#if !defined(__i386__) && !defined(__x86_64__)
	num++;
#endif
	return num;
}

const char *tool_pmu__event_to_str(enum tool_pmu_event ev)
{
	if ((ev > TOOL_PMU__EVENT_NONE && ev < TOOL_PMU__EVENT_MAX) &&
	    !tool_pmu__skip_event(tool_pmu__event_names[ev]))
		return tool_pmu__event_names[ev];

	return NULL;
}

enum tool_pmu_event tool_pmu__str_to_event(const char *str)
{
	int i;

	if (tool_pmu__skip_event(str))
		return TOOL_PMU__EVENT_NONE;

	tool_pmu__for_each_event(i) {
		if (!strcasecmp(str, tool_pmu__event_names[i]))
			return i;
	}
	return TOOL_PMU__EVENT_NONE;
}

bool perf_pmu__is_tool(const struct perf_pmu *pmu)
{
	return pmu && pmu->type == PERF_PMU_TYPE_TOOL;
}

bool evsel__is_tool(const struct evsel *evsel)
{
	return perf_pmu__is_tool(evsel->pmu);
}

enum tool_pmu_event evsel__tool_event(const struct evsel *evsel)
{
	if (!evsel__is_tool(evsel))
		return TOOL_PMU__EVENT_NONE;

	return (enum tool_pmu_event)evsel->core.attr.config;
}

const char *evsel__tool_pmu_event_name(const struct evsel *evsel)
{
	return tool_pmu__event_to_str(evsel->core.attr.config);
}

static bool read_until_char(struct io *io, char e)
{
	int c;

	do {
		c = io__get_char(io);
		if (c == -1)
			return false;
	} while (c != e);
	return true;
}

static int read_stat_field(int fd, struct perf_cpu cpu, int field, __u64 *val)
{
	char buf[256];
	struct io io;
	int i;

	io__init(&io, fd, buf, sizeof(buf));

	/* Skip lines to relevant CPU. */
	for (i = -1; i < cpu.cpu; i++) {
		if (!read_until_char(&io, '\n'))
			return -EINVAL;
	}
	/* Skip to "cpu". */
	if (io__get_char(&io) != 'c') return -EINVAL;
	if (io__get_char(&io) != 'p') return -EINVAL;
	if (io__get_char(&io) != 'u') return -EINVAL;

	/* Skip N of cpuN. */
	if (!read_until_char(&io, ' '))
		return -EINVAL;

	i = 1;
	while (true) {
		if (io__get_dec(&io, val) != ' ')
			break;
		if (field == i)
			return 0;
		i++;
	}
	return -EINVAL;
}

static int read_pid_stat_field(int fd, int field, __u64 *val)
{
	char buf[256];
	struct io io;
	int c, i;

	io__init(&io, fd, buf, sizeof(buf));
	if (io__get_dec(&io, val) != ' ')
		return -EINVAL;
	if (field == 1)
		return 0;

	/* Skip comm. */
	if (io__get_char(&io) != '(' || !read_until_char(&io, ')'))
		return -EINVAL;
	if (field == 2)
		return -EINVAL; /* String can't be returned. */

	/* Skip state */
	if (io__get_char(&io) != ' ' || io__get_char(&io) == -1)
		return -EINVAL;
	if (field == 3)
		return -EINVAL; /* String can't be returned. */

	/* Loop over numeric fields*/
	if (io__get_char(&io) != ' ')
		return -EINVAL;

	i = 4;
	while (true) {
		c = io__get_dec(&io, val);
		if (c == -1)
			return -EINVAL;
		if (c == -2) {
			/* Assume a -ve was read */
			c = io__get_dec(&io, val);
			*val *= -1;
		}
		if (c != ' ')
			return -EINVAL;
		if (field == i)
			return 0;
		i++;
	}
	return -EINVAL;
}

int evsel__tool_pmu_prepare_open(struct evsel *evsel,
				 struct perf_cpu_map *cpus,
				 int nthreads)
{
	if ((evsel__tool_event(evsel) == TOOL_PMU__EVENT_SYSTEM_TIME ||
	     evsel__tool_event(evsel) == TOOL_PMU__EVENT_USER_TIME) &&
	    !evsel->start_times) {
		evsel->start_times = xyarray__new(perf_cpu_map__nr(cpus),
						  nthreads,
						  sizeof(__u64));
		if (!evsel->start_times)
			return -ENOMEM;
	}
	return 0;
}

#define FD(e, x, y) (*(int *)xyarray__entry(e->core.fd, x, y))

int evsel__tool_pmu_open(struct evsel *evsel,
			 struct perf_thread_map *threads,
			 int start_cpu_map_idx, int end_cpu_map_idx)
{
	enum tool_pmu_event ev = evsel__tool_event(evsel);
	int pid = -1, idx = 0, thread = 0, nthreads, err = 0, old_errno;

	if (ev == TOOL_PMU__EVENT_NUM_CPUS)
		return 0;

	if (ev == TOOL_PMU__EVENT_DURATION_TIME) {
		if (evsel->core.attr.sample_period) /* no sampling */
			return -EINVAL;
		evsel->start_time = rdclock();
		return 0;
	}

	if (evsel->cgrp)
		pid = evsel->cgrp->fd;

	nthreads = perf_thread_map__nr(threads);
	for (idx = start_cpu_map_idx; idx < end_cpu_map_idx; idx++) {
		for (thread = 0; thread < nthreads; thread++) {
			if (thread >= nthreads)
				break;

			if (!evsel->cgrp && !evsel->core.system_wide)
				pid = perf_thread_map__pid(threads, thread);

			if (ev == TOOL_PMU__EVENT_USER_TIME || ev == TOOL_PMU__EVENT_SYSTEM_TIME) {
				bool system = ev == TOOL_PMU__EVENT_SYSTEM_TIME;
				__u64 *start_time = NULL;
				int fd;

				if (evsel->core.attr.sample_period) {
					/* no sampling */
					err = -EINVAL;
					goto out_close;
				}
				if (pid > -1) {
					char buf[64];

					snprintf(buf, sizeof(buf), "/proc/%d/stat", pid);
					fd = open(buf, O_RDONLY);
					evsel->pid_stat = true;
				} else {
					fd = open("/proc/stat", O_RDONLY);
				}
				FD(evsel, idx, thread) = fd;
				if (fd < 0) {
					err = -errno;
					goto out_close;
				}
				start_time = xyarray__entry(evsel->start_times, idx, thread);
				if (pid > -1) {
					err = read_pid_stat_field(fd, system ? 15 : 14,
								  start_time);
				} else {
					struct perf_cpu cpu;

					cpu = perf_cpu_map__cpu(evsel->core.cpus, idx);
					err = read_stat_field(fd, cpu, system ? 3 : 1,
							      start_time);
				}
				if (err)
					goto out_close;
			}

		}
	}
	return 0;
out_close:
	if (err)
		threads->err_thread = thread;

	old_errno = errno;
	do {
		while (--thread >= 0) {
			if (FD(evsel, idx, thread) >= 0)
				close(FD(evsel, idx, thread));
			FD(evsel, idx, thread) = -1;
		}
		thread = nthreads;
	} while (--idx >= 0);
	errno = old_errno;
	return err;
}

#if !defined(__i386__) && !defined(__x86_64__)
u64 arch_get_tsc_freq(void)
{
	return 0;
}
#endif

#if !defined(__aarch64__)
u64 tool_pmu__cpu_slots_per_cycle(void)
{
	return 0;
}
#endif

static bool has_pmem(void)
{
	static bool has_pmem, cached;
	const char *sysfs = sysfs__mountpoint();
	char path[PATH_MAX];

	if (!cached) {
		snprintf(path, sizeof(path), "%s/firmware/acpi/tables/NFIT", sysfs);
		has_pmem = access(path, F_OK) == 0;
		cached = true;
	}
	return has_pmem;
}

bool tool_pmu__read_event(enum tool_pmu_event ev, u64 *result)
{
	const struct cpu_topology *topology;

	switch (ev) {
	case TOOL_PMU__EVENT_HAS_PMEM:
		*result = has_pmem() ? 1 : 0;
		return true;

	case TOOL_PMU__EVENT_NUM_CORES:
		topology = online_topology();
		*result = topology->core_cpus_lists;
		return true;

	case TOOL_PMU__EVENT_NUM_CPUS:
		*result = cpu__max_present_cpu().cpu;
		return true;

	case TOOL_PMU__EVENT_NUM_CPUS_ONLINE: {
		struct perf_cpu_map *online = cpu_map__online();

		if (online) {
			*result = perf_cpu_map__nr(online);
			perf_cpu_map__put(online);
			return true;
		}
		return false;
	}
	case TOOL_PMU__EVENT_NUM_DIES:
		topology = online_topology();
		*result = topology->die_cpus_lists;
		return true;

	case TOOL_PMU__EVENT_NUM_PACKAGES:
		topology = online_topology();
		*result = topology->package_cpus_lists;
		return true;

	case TOOL_PMU__EVENT_SLOTS:
		*result = tool_pmu__cpu_slots_per_cycle();
		return *result ? true : false;

	case TOOL_PMU__EVENT_SMT_ON:
		*result = smt_on() ? 1 : 0;
		return true;

	case TOOL_PMU__EVENT_SYSTEM_TSC_FREQ:
		*result = arch_get_tsc_freq();
		return true;

	case TOOL_PMU__EVENT_NONE:
	case TOOL_PMU__EVENT_DURATION_TIME:
	case TOOL_PMU__EVENT_USER_TIME:
	case TOOL_PMU__EVENT_SYSTEM_TIME:
	case TOOL_PMU__EVENT_MAX:
	default:
		return false;
	}
}

int evsel__tool_pmu_read(struct evsel *evsel, int cpu_map_idx, int thread)
{
	__u64 *start_time, cur_time, delta_start;
	u64 val;
	int fd, err = 0;
	struct perf_counts_values *count, *old_count = NULL;
	bool adjust = false;
	enum tool_pmu_event ev = evsel__tool_event(evsel);

	count = perf_counts(evsel->counts, cpu_map_idx, thread);

	switch (ev) {
	case TOOL_PMU__EVENT_HAS_PMEM:
	case TOOL_PMU__EVENT_NUM_CORES:
	case TOOL_PMU__EVENT_NUM_CPUS:
	case TOOL_PMU__EVENT_NUM_CPUS_ONLINE:
	case TOOL_PMU__EVENT_NUM_DIES:
	case TOOL_PMU__EVENT_NUM_PACKAGES:
	case TOOL_PMU__EVENT_SLOTS:
	case TOOL_PMU__EVENT_SMT_ON:
	case TOOL_PMU__EVENT_SYSTEM_TSC_FREQ:
		if (evsel->prev_raw_counts)
			old_count = perf_counts(evsel->prev_raw_counts, cpu_map_idx, thread);
		val = 0;
		if (cpu_map_idx == 0 && thread == 0) {
			if (!tool_pmu__read_event(ev, &val)) {
				count->lost++;
				val = 0;
			}
		}
		if (old_count) {
			count->val = old_count->val + val;
			count->run = old_count->run + 1;
			count->ena = old_count->ena + 1;
		} else {
			count->val = val;
			count->run++;
			count->ena++;
		}
		return 0;
	case TOOL_PMU__EVENT_DURATION_TIME:
		/*
		 * Pretend duration_time is only on the first CPU and thread, or
		 * else aggregation will scale duration_time by the number of
		 * CPUs/threads.
		 */
		start_time = &evsel->start_time;
		if (cpu_map_idx == 0 && thread == 0)
			cur_time = rdclock();
		else
			cur_time = *start_time;
		break;
	case TOOL_PMU__EVENT_USER_TIME:
	case TOOL_PMU__EVENT_SYSTEM_TIME: {
		bool system = evsel__tool_event(evsel) == TOOL_PMU__EVENT_SYSTEM_TIME;

		start_time = xyarray__entry(evsel->start_times, cpu_map_idx, thread);
		fd = FD(evsel, cpu_map_idx, thread);
		lseek(fd, SEEK_SET, 0);
		if (evsel->pid_stat) {
			/* The event exists solely on 1 CPU. */
			if (cpu_map_idx == 0)
				err = read_pid_stat_field(fd, system ? 15 : 14, &cur_time);
			else
				cur_time = 0;
		} else {
			/* The event is for all threads. */
			if (thread == 0) {
				struct perf_cpu cpu = perf_cpu_map__cpu(evsel->core.cpus,
									cpu_map_idx);

				err = read_stat_field(fd, cpu, system ? 3 : 1, &cur_time);
			} else {
				cur_time = 0;
			}
		}
		adjust = true;
		break;
	}
	case TOOL_PMU__EVENT_NONE:
	case TOOL_PMU__EVENT_MAX:
	default:
		err = -EINVAL;
	}
	if (err)
		return err;

	delta_start = cur_time - *start_time;
	if (adjust) {
		__u64 ticks_per_sec = sysconf(_SC_CLK_TCK);

		delta_start *= 1000000000 / ticks_per_sec;
	}
	count->val    = delta_start;
	count->lost   = 0;
	/*
	 * The values of enabled and running must make a ratio of 100%. The
	 * exact values don't matter as long as they are non-zero to avoid
	 * issues with evsel__count_has_error.
	 */
	count->ena++;
	count->run++;
	return 0;
}

struct perf_pmu *tool_pmu__new(void)
{
	struct perf_pmu *tool = zalloc(sizeof(struct perf_pmu));

	if (!tool)
		goto out;
	tool->name = strdup("tool");
	if (!tool->name) {
		zfree(&tool);
		goto out;
	}

	tool->type = PERF_PMU_TYPE_TOOL;
	INIT_LIST_HEAD(&tool->aliases);
	INIT_LIST_HEAD(&tool->caps);
	INIT_LIST_HEAD(&tool->format);
	tool->events_table = find_core_events_table("common", "common");

out:
	return tool;
}
