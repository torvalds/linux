/* SPDX-License-Identifier: GPL-2.0 */

#include "perf-sys.h"
#include "util/cloexec.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include "util/parse-events.h"
#include "util/perf_api_probe.h"
#include <perf/cpumap.h>
#include <errno.h>

typedef void (*setup_probe_fn_t)(struct evsel *evsel);

static int perf_do_probe_api(setup_probe_fn_t fn, int cpu, const char *str)
{
	struct evlist *evlist;
	struct evsel *evsel;
	unsigned long flags = perf_event_open_cloexec_flag();
	int err = -EAGAIN, fd;
	static pid_t pid = -1;

	evlist = evlist__new();
	if (!evlist)
		return -ENOMEM;

	if (parse_events(evlist, str, NULL))
		goto out_delete;

	evsel = evlist__first(evlist);

	while (1) {
		fd = sys_perf_event_open(&evsel->core.attr, pid, cpu, -1, flags);
		if (fd < 0) {
			if (pid == -1 && errno == EACCES) {
				pid = 0;
				continue;
			}
			goto out_delete;
		}
		break;
	}
	close(fd);

	fn(evsel);

	fd = sys_perf_event_open(&evsel->core.attr, pid, cpu, -1, flags);
	if (fd < 0) {
		if (errno == EINVAL)
			err = -EINVAL;
		goto out_delete;
	}
	close(fd);
	err = 0;

out_delete:
	evlist__delete(evlist);
	return err;
}

static bool perf_probe_api(setup_probe_fn_t fn)
{
	const char *try[] = {"cycles:u", "instructions:u", "cpu-clock:u", NULL};
	struct perf_cpu_map *cpus;
	int cpu, ret, i = 0;

	cpus = perf_cpu_map__new(NULL);
	if (!cpus)
		return false;
	cpu = cpus->map[0];
	perf_cpu_map__put(cpus);

	do {
		ret = perf_do_probe_api(fn, cpu, try[i++]);
		if (!ret)
			return true;
	} while (ret == -EAGAIN && try[i]);

	return false;
}

static void perf_probe_sample_identifier(struct evsel *evsel)
{
	evsel->core.attr.sample_type |= PERF_SAMPLE_IDENTIFIER;
}

static void perf_probe_comm_exec(struct evsel *evsel)
{
	evsel->core.attr.comm_exec = 1;
}

static void perf_probe_context_switch(struct evsel *evsel)
{
	evsel->core.attr.context_switch = 1;
}

static void perf_probe_text_poke(struct evsel *evsel)
{
	evsel->core.attr.text_poke = 1;
}

static void perf_probe_build_id(struct evsel *evsel)
{
	evsel->core.attr.build_id = 1;
}

bool perf_can_sample_identifier(void)
{
	return perf_probe_api(perf_probe_sample_identifier);
}

bool perf_can_comm_exec(void)
{
	return perf_probe_api(perf_probe_comm_exec);
}

bool perf_can_record_switch_events(void)
{
	return perf_probe_api(perf_probe_context_switch);
}

bool perf_can_record_text_poke_events(void)
{
	return perf_probe_api(perf_probe_text_poke);
}

bool perf_can_record_cpu_wide(void)
{
	struct perf_event_attr attr = {
		.type = PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_CPU_CLOCK,
		.exclude_kernel = 1,
	};
	struct perf_cpu_map *cpus;
	int cpu, fd;

	cpus = perf_cpu_map__new(NULL);
	if (!cpus)
		return false;
	cpu = cpus->map[0];
	perf_cpu_map__put(cpus);

	fd = sys_perf_event_open(&attr, -1, cpu, -1, 0);
	if (fd < 0)
		return false;
	close(fd);

	return true;
}

/*
 * Architectures are expected to know if AUX area sampling is supported by the
 * hardware. Here we check for kernel support.
 */
bool perf_can_aux_sample(void)
{
	struct perf_event_attr attr = {
		.size = sizeof(struct perf_event_attr),
		.exclude_kernel = 1,
		/*
		 * Non-zero value causes the kernel to calculate the effective
		 * attribute size up to that byte.
		 */
		.aux_sample_size = 1,
	};
	int fd;

	fd = sys_perf_event_open(&attr, -1, 0, -1, 0);
	/*
	 * If the kernel attribute is big enough to contain aux_sample_size
	 * then we assume that it is supported. We are relying on the kernel to
	 * validate the attribute size before anything else that could be wrong.
	 */
	if (fd < 0 && errno == E2BIG)
		return false;
	if (fd >= 0)
		close(fd);

	return true;
}

bool perf_can_record_build_id(void)
{
	return perf_probe_api(perf_probe_build_id);
}
