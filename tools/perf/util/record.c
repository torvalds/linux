// SPDX-License-Identifier: GPL-2.0
#include "evlist.h"
#include "evsel.h"
#include "cpumap.h"
#include "parse-events.h"
#include <errno.h>
#include <api/fs/fs.h>
#include "util.h"
#include "cloexec.h"

typedef void (*setup_probe_fn_t)(struct perf_evsel *evsel);

static int perf_do_probe_api(setup_probe_fn_t fn, int cpu, const char *str)
{
	struct perf_evlist *evlist;
	struct perf_evsel *evsel;
	unsigned long flags = perf_event_open_cloexec_flag();
	int err = -EAGAIN, fd;
	static pid_t pid = -1;

	evlist = perf_evlist__new();
	if (!evlist)
		return -ENOMEM;

	if (parse_events(evlist, str, NULL))
		goto out_delete;

	evsel = perf_evlist__first(evlist);

	while (1) {
		fd = sys_perf_event_open(&evsel->attr, pid, cpu, -1, flags);
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

	fd = sys_perf_event_open(&evsel->attr, pid, cpu, -1, flags);
	if (fd < 0) {
		if (errno == EINVAL)
			err = -EINVAL;
		goto out_delete;
	}
	close(fd);
	err = 0;

out_delete:
	perf_evlist__delete(evlist);
	return err;
}

static bool perf_probe_api(setup_probe_fn_t fn)
{
	const char *try[] = {"cycles:u", "instructions:u", "cpu-clock:u", NULL};
	struct cpu_map *cpus;
	int cpu, ret, i = 0;

	cpus = cpu_map__new(NULL);
	if (!cpus)
		return false;
	cpu = cpus->map[0];
	cpu_map__put(cpus);

	do {
		ret = perf_do_probe_api(fn, cpu, try[i++]);
		if (!ret)
			return true;
	} while (ret == -EAGAIN && try[i]);

	return false;
}

static void perf_probe_sample_identifier(struct perf_evsel *evsel)
{
	evsel->attr.sample_type |= PERF_SAMPLE_IDENTIFIER;
}

static void perf_probe_comm_exec(struct perf_evsel *evsel)
{
	evsel->attr.comm_exec = 1;
}

static void perf_probe_context_switch(struct perf_evsel *evsel)
{
	evsel->attr.context_switch = 1;
}

bool perf_can_sample_identifier(void)
{
	return perf_probe_api(perf_probe_sample_identifier);
}

static bool perf_can_comm_exec(void)
{
	return perf_probe_api(perf_probe_comm_exec);
}

bool perf_can_record_switch_events(void)
{
	return perf_probe_api(perf_probe_context_switch);
}

bool perf_can_record_cpu_wide(void)
{
	struct perf_event_attr attr = {
		.type = PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_CPU_CLOCK,
		.exclude_kernel = 1,
	};
	struct cpu_map *cpus;
	int cpu, fd;

	cpus = cpu_map__new(NULL);
	if (!cpus)
		return false;
	cpu = cpus->map[0];
	cpu_map__put(cpus);

	fd = sys_perf_event_open(&attr, -1, cpu, -1, 0);
	if (fd < 0)
		return false;
	close(fd);

	return true;
}

void perf_evlist__config(struct perf_evlist *evlist, struct record_opts *opts,
			 struct callchain_param *callchain)
{
	struct perf_evsel *evsel;
	bool use_sample_identifier = false;
	bool use_comm_exec;
	bool sample_id = opts->sample_id;

	/*
	 * Set the evsel leader links before we configure attributes,
	 * since some might depend on this info.
	 */
	if (opts->group)
		perf_evlist__set_leader(evlist);

	if (evlist->cpus->map[0] < 0)
		opts->no_inherit = true;

	use_comm_exec = perf_can_comm_exec();

	evlist__for_each_entry(evlist, evsel) {
		perf_evsel__config(evsel, opts, callchain);
		if (evsel->tracking && use_comm_exec)
			evsel->attr.comm_exec = 1;
	}

	if (opts->full_auxtrace) {
		/*
		 * Need to be able to synthesize and parse selected events with
		 * arbitrary sample types, which requires always being able to
		 * match the id.
		 */
		use_sample_identifier = perf_can_sample_identifier();
		sample_id = true;
	} else if (evlist->nr_entries > 1) {
		struct perf_evsel *first = perf_evlist__first(evlist);

		evlist__for_each_entry(evlist, evsel) {
			if (evsel->attr.sample_type == first->attr.sample_type)
				continue;
			use_sample_identifier = perf_can_sample_identifier();
			break;
		}
		sample_id = true;
	}

	if (sample_id) {
		evlist__for_each_entry(evlist, evsel)
			perf_evsel__set_sample_id(evsel, use_sample_identifier);
	}

	perf_evlist__set_id_pos(evlist);
}

static int get_max_rate(unsigned int *rate)
{
	return sysctl__read_int("kernel/perf_event_max_sample_rate", (int *)rate);
}

static int record_opts__config_freq(struct record_opts *opts)
{
	bool user_freq = opts->user_freq != UINT_MAX;
	unsigned int max_rate;

	if (opts->user_interval != ULLONG_MAX)
		opts->default_interval = opts->user_interval;
	if (user_freq)
		opts->freq = opts->user_freq;

	/*
	 * User specified count overrides default frequency.
	 */
	if (opts->default_interval)
		opts->freq = 0;
	else if (opts->freq) {
		opts->default_interval = opts->freq;
	} else {
		pr_err("frequency and count are zero, aborting\n");
		return -1;
	}

	if (get_max_rate(&max_rate))
		return 0;

	/*
	 * User specified frequency is over current maximum.
	 */
	if (user_freq && (max_rate < opts->freq)) {
		pr_err("Maximum frequency rate (%u) reached.\n"
		   "Please use -F freq option with lower value or consider\n"
		   "tweaking /proc/sys/kernel/perf_event_max_sample_rate.\n",
		   max_rate);
		return -1;
	}

	/*
	 * Default frequency is over current maximum.
	 */
	if (max_rate < opts->freq) {
		pr_warning("Lowering default frequency rate to %u.\n"
			   "Please consider tweaking "
			   "/proc/sys/kernel/perf_event_max_sample_rate.\n",
			   max_rate);
		opts->freq = max_rate;
	}

	return 0;
}

int record_opts__config(struct record_opts *opts)
{
	return record_opts__config_freq(opts);
}

bool perf_evlist__can_select_event(struct perf_evlist *evlist, const char *str)
{
	struct perf_evlist *temp_evlist;
	struct perf_evsel *evsel;
	int err, fd, cpu;
	bool ret = false;
	pid_t pid = -1;

	temp_evlist = perf_evlist__new();
	if (!temp_evlist)
		return false;

	err = parse_events(temp_evlist, str, NULL);
	if (err)
		goto out_delete;

	evsel = perf_evlist__last(temp_evlist);

	if (!evlist || cpu_map__empty(evlist->cpus)) {
		struct cpu_map *cpus = cpu_map__new(NULL);

		cpu =  cpus ? cpus->map[0] : 0;
		cpu_map__put(cpus);
	} else {
		cpu = evlist->cpus->map[0];
	}

	while (1) {
		fd = sys_perf_event_open(&evsel->attr, pid, cpu, -1,
					 perf_event_open_cloexec_flag());
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
	ret = true;

out_delete:
	perf_evlist__delete(temp_evlist);
	return ret;
}
