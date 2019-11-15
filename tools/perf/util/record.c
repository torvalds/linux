// SPDX-License-Identifier: GPL-2.0
#include "debug.h"
#include "evlist.h"
#include "evsel.h"
#include "parse-events.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <api/fs/fs.h>
#include <subcmd/parse-options.h>
#include <perf/cpumap.h>
#include "cloexec.h"
#include "record.h"
#include "../perf-sys.h"

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

void perf_evlist__config(struct evlist *evlist, struct record_opts *opts,
			 struct callchain_param *callchain)
{
	struct evsel *evsel;
	bool use_sample_identifier = false;
	bool use_comm_exec;
	bool sample_id = opts->sample_id;

	/*
	 * Set the evsel leader links before we configure attributes,
	 * since some might depend on this info.
	 */
	if (opts->group)
		perf_evlist__set_leader(evlist);

	if (evlist->core.cpus->map[0] < 0)
		opts->no_inherit = true;

	use_comm_exec = perf_can_comm_exec();

	evlist__for_each_entry(evlist, evsel) {
		perf_evsel__config(evsel, opts, callchain);
		if (evsel->tracking && use_comm_exec)
			evsel->core.attr.comm_exec = 1;
	}

	if (opts->full_auxtrace) {
		/*
		 * Need to be able to synthesize and parse selected events with
		 * arbitrary sample types, which requires always being able to
		 * match the id.
		 */
		use_sample_identifier = perf_can_sample_identifier();
		sample_id = true;
	} else if (evlist->core.nr_entries > 1) {
		struct evsel *first = evlist__first(evlist);

		evlist__for_each_entry(evlist, evsel) {
			if (evsel->core.attr.sample_type == first->core.attr.sample_type)
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
		if (opts->strict_freq) {
			pr_err("error: Maximum frequency rate (%'u Hz) exceeded.\n"
			       "       Please use -F freq option with a lower value or consider\n"
			       "       tweaking /proc/sys/kernel/perf_event_max_sample_rate.\n",
			       max_rate);
			return -1;
		} else {
			pr_warning("warning: Maximum frequency rate (%'u Hz) exceeded, throttling from %'u Hz to %'u Hz.\n"
				   "         The limit can be raised via /proc/sys/kernel/perf_event_max_sample_rate.\n"
				   "         The kernel will lower it when perf's interrupts take too long.\n"
				   "         Use --strict-freq to disable this throttling, refusing to record.\n",
				   max_rate, opts->freq, max_rate);

			opts->freq = max_rate;
		}
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

bool perf_evlist__can_select_event(struct evlist *evlist, const char *str)
{
	struct evlist *temp_evlist;
	struct evsel *evsel;
	int err, fd, cpu;
	bool ret = false;
	pid_t pid = -1;

	temp_evlist = evlist__new();
	if (!temp_evlist)
		return false;

	err = parse_events(temp_evlist, str, NULL);
	if (err)
		goto out_delete;

	evsel = evlist__last(temp_evlist);

	if (!evlist || perf_cpu_map__empty(evlist->core.cpus)) {
		struct perf_cpu_map *cpus = perf_cpu_map__new(NULL);

		cpu =  cpus ? cpus->map[0] : 0;
		perf_cpu_map__put(cpus);
	} else {
		cpu = evlist->core.cpus->map[0];
	}

	while (1) {
		fd = sys_perf_event_open(&evsel->core.attr, pid, cpu, -1,
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
	evlist__delete(temp_evlist);
	return ret;
}

int record__parse_freq(const struct option *opt, const char *str, int unset __maybe_unused)
{
	unsigned int freq;
	struct record_opts *opts = opt->value;

	if (!str)
		return -EINVAL;

	if (strcasecmp(str, "max") == 0) {
		if (get_max_rate(&freq)) {
			pr_err("couldn't read /proc/sys/kernel/perf_event_max_sample_rate\n");
			return -1;
		}
		pr_info("info: Using a maximum frequency rate of %'d Hz\n", freq);
	} else {
		freq = atoi(str);
	}

	opts->user_freq = freq;
	return 0;
}
