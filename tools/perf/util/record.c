// SPDX-License-Identifier: GPL-2.0
#include "debug.h"
#include "evlist.h"
#include "evsel.h"
#include "evsel_config.h"
#include "parse-events.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <api/fs/fs.h>
#include <subcmd/parse-options.h>
#include <perf/cpumap.h>
#include "cloexec.h"
#include "util/perf_api_probe.h"
#include "record.h"
#include "../perf-sys.h"
#include "topdown.h"

/*
 * evsel__config_leader_sampling() uses special rules for leader sampling.
 * However, if the leader is an AUX area event, then assume the event to sample
 * is the next event.
 */
static struct evsel *evsel__read_sampler(struct evsel *evsel, struct evlist *evlist)
{
	struct evsel *leader = evsel->leader;

	if (evsel__is_aux_event(leader) || arch_topdown_sample_read(leader)) {
		evlist__for_each_entry(evlist, evsel) {
			if (evsel->leader == leader && evsel != evsel->leader)
				return evsel;
		}
	}

	return leader;
}

static u64 evsel__config_term_mask(struct evsel *evsel)
{
	struct evsel_config_term *term;
	struct list_head *config_terms = &evsel->config_terms;
	u64 term_types = 0;

	list_for_each_entry(term, config_terms, list) {
		term_types |= 1 << term->type;
	}
	return term_types;
}

static void evsel__config_leader_sampling(struct evsel *evsel, struct evlist *evlist)
{
	struct perf_event_attr *attr = &evsel->core.attr;
	struct evsel *leader = evsel->leader;
	struct evsel *read_sampler;
	u64 term_types, freq_mask;

	if (!leader->sample_read)
		return;

	read_sampler = evsel__read_sampler(evsel, evlist);

	if (evsel == read_sampler)
		return;

	term_types = evsel__config_term_mask(evsel);
	/*
	 * Disable sampling for all group members except those with explicit
	 * config terms or the leader. In the case of an AUX area event, the 2nd
	 * event in the group is the one that 'leads' the sampling.
	 */
	freq_mask = (1 << EVSEL__CONFIG_TERM_FREQ) | (1 << EVSEL__CONFIG_TERM_PERIOD);
	if ((term_types & freq_mask) == 0) {
		attr->freq           = 0;
		attr->sample_freq    = 0;
		attr->sample_period  = 0;
	}
	if ((term_types & (1 << EVSEL__CONFIG_TERM_OVERWRITE)) == 0)
		attr->write_backward = 0;

	/*
	 * We don't get a sample for slave events, we make them when delivering
	 * the group leader sample. Set the slave event to follow the master
	 * sample_type to ease up reporting.
	 * An AUX area event also has sample_type requirements, so also include
	 * the sample type bits from the leader's sample_type to cover that
	 * case.
	 */
	attr->sample_type = read_sampler->core.attr.sample_type |
			    leader->core.attr.sample_type;
}

void evlist__config(struct evlist *evlist, struct record_opts *opts, struct callchain_param *callchain)
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
		evlist__set_leader(evlist);

	if (evlist->core.cpus->map[0] < 0)
		opts->no_inherit = true;

	use_comm_exec = perf_can_comm_exec();

	evlist__for_each_entry(evlist, evsel) {
		evsel__config(evsel, opts, callchain);
		if (evsel->tracking && use_comm_exec)
			evsel->core.attr.comm_exec = 1;
	}

	/* Configure leader sampling here now that the sample type is known */
	evlist__for_each_entry(evlist, evsel)
		evsel__config_leader_sampling(evsel, evlist);

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
			evsel__set_sample_id(evsel, use_sample_identifier);
	}

	evlist__set_id_pos(evlist);
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

bool evlist__can_select_event(struct evlist *evlist, const char *str)
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
