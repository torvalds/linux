// SPDX-License-Identifier: GPL-2.0-only
#include "cgroup.h"
#include "counts.h"
#include "evsel.h"
#include "pmu.h"
#include "print-events.h"
#include "time-utils.h"
#include "tool_pmu.h"
#include <api/io.h>
#include <internal/threadmap.h>
#include <perf/threadmap.h>
#include <fcntl.h>
#include <strings.h>

static const char *const tool_pmu__event_names[PERF_TOOL_MAX] = {
	NULL,
	"duration_time",
	"user_time",
	"system_time",
};


const char *perf_tool_event__to_str(enum perf_tool_event ev)
{
	if (ev > PERF_TOOL_NONE && ev < PERF_TOOL_MAX)
		return tool_pmu__event_names[ev];

	return NULL;
}

enum perf_tool_event perf_tool_event__from_str(const char *str)
{
	int i;

	perf_tool_event__for_each_event(i) {
		if (!strcasecmp(str, tool_pmu__event_names[i]))
			return i;
	}
	return PERF_TOOL_NONE;
}

static int tool_pmu__config_term(struct perf_event_attr *attr,
				 struct parse_events_term *term,
				 struct parse_events_error *err)
{
	if (term->type_term == PARSE_EVENTS__TERM_TYPE_USER) {
		enum perf_tool_event ev = perf_tool_event__from_str(term->config);

		if (ev == PERF_TOOL_NONE)
			goto err_out;

		attr->config = ev;
		return 0;
	}
err_out:
	if (err) {
		char *err_str;

		parse_events_error__handle(err, term->err_val,
					asprintf(&err_str,
						"unexpected tool event term (%s) %s",
						parse_events__term_type_str(term->type_term),
						term->config) < 0
					? strdup("unexpected tool event term")
					: err_str,
					NULL);
	}
	return -EINVAL;
}

int tool_pmu__config_terms(struct perf_event_attr *attr,
			   struct parse_events_terms *terms,
			   struct parse_events_error *err)
{
	struct parse_events_term *term;

	list_for_each_entry(term, &terms->terms, list) {
		if (tool_pmu__config_term(attr, term, err))
			return -EINVAL;
	}

	return 0;

}

int tool_pmu__for_each_event_cb(struct perf_pmu *pmu, void *state, pmu_event_callback cb)
{
	struct pmu_event_info info = {
		.pmu = pmu,
		.event_type_desc = "Tool event",
	};
	int i;

	perf_tool_event__for_each_event(i) {
		int ret;

		info.name = perf_tool_event__to_str(i);
		info.alias = NULL;
		info.scale_unit = NULL;
		info.desc = NULL;
		info.long_desc = NULL;
		info.encoding_desc = NULL;
		info.topic = NULL;
		info.pmu_name = pmu->name;
		info.deprecated = false;
		ret = cb(state, &info);
		if (ret)
			return ret;
	}
	return 0;
}

bool perf_pmu__is_tool(const struct perf_pmu *pmu)
{
	return pmu && pmu->type == PERF_PMU_TYPE_TOOL;
}

bool evsel__is_tool(const struct evsel *evsel)
{
	return perf_pmu__is_tool(evsel->pmu);
}

enum perf_tool_event evsel__tool_event(const struct evsel *evsel)
{
	if (!evsel__is_tool(evsel))
		return PERF_TOOL_NONE;

	return (enum perf_tool_event)evsel->core.attr.config;
}

const char *evsel__tool_pmu_event_name(const struct evsel *evsel)
{
	return perf_tool_event__to_str(evsel->core.attr.config);
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
	if ((evsel__tool_event(evsel) == PERF_TOOL_SYSTEM_TIME ||
	     evsel__tool_event(evsel) == PERF_TOOL_USER_TIME) &&
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
	enum perf_tool_event ev = evsel__tool_event(evsel);
	int pid = -1, idx = 0, thread = 0, nthreads, err = 0, old_errno;

	if (ev == PERF_TOOL_DURATION_TIME) {
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

			if (ev == PERF_TOOL_USER_TIME || ev == PERF_TOOL_SYSTEM_TIME) {
				bool system = ev == PERF_TOOL_SYSTEM_TIME;
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

int evsel__read_tool(struct evsel *evsel, int cpu_map_idx, int thread)
{
	__u64 *start_time, cur_time, delta_start;
	int fd, err = 0;
	struct perf_counts_values *count;
	bool adjust = false;

	count = perf_counts(evsel->counts, cpu_map_idx, thread);

	switch (evsel__tool_event(evsel)) {
	case PERF_TOOL_DURATION_TIME:
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
	case PERF_TOOL_USER_TIME:
	case PERF_TOOL_SYSTEM_TIME: {
		bool system = evsel__tool_event(evsel) == PERF_TOOL_SYSTEM_TIME;

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
	case PERF_TOOL_NONE:
	case PERF_TOOL_MAX:
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
	count->ena    = count->run = delta_start;
	count->lost   = 0;
	return 0;
}

struct perf_pmu *perf_pmus__tool_pmu(void)
{
	static struct perf_pmu tool = {
		.name = "tool",
		.type = PERF_PMU_TYPE_TOOL,
		.aliases = LIST_HEAD_INIT(tool.aliases),
		.caps = LIST_HEAD_INIT(tool.caps),
		.format = LIST_HEAD_INIT(tool.format),
	};

	return &tool;
}
