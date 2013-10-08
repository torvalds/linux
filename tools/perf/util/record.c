#include "evlist.h"
#include "evsel.h"
#include "cpumap.h"
#include "parse-events.h"

typedef void (*setup_probe_fn_t)(struct perf_evsel *evsel);

static int perf_do_probe_api(setup_probe_fn_t fn, int cpu, const char *str)
{
	struct perf_evlist *evlist;
	struct perf_evsel *evsel;
	int err = -EAGAIN, fd;

	evlist = perf_evlist__new();
	if (!evlist)
		return -ENOMEM;

	if (parse_events(evlist, str))
		goto out_delete;

	evsel = perf_evlist__first(evlist);

	fd = sys_perf_event_open(&evsel->attr, -1, cpu, -1, 0);
	if (fd < 0)
		goto out_delete;
	close(fd);

	fn(evsel);

	fd = sys_perf_event_open(&evsel->attr, -1, cpu, -1, 0);
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
	const char *try[] = {"cycles:u", "instructions:u", "cpu-clock", NULL};
	struct cpu_map *cpus;
	int cpu, ret, i = 0;

	cpus = cpu_map__new(NULL);
	if (!cpus)
		return false;
	cpu = cpus->map[0];
	cpu_map__delete(cpus);

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

bool perf_can_sample_identifier(void)
{
	return perf_probe_api(perf_probe_sample_identifier);
}

void perf_evlist__config(struct perf_evlist *evlist,
			struct perf_record_opts *opts)
{
	struct perf_evsel *evsel;
	bool use_sample_identifier = false;

	/*
	 * Set the evsel leader links before we configure attributes,
	 * since some might depend on this info.
	 */
	if (opts->group)
		perf_evlist__set_leader(evlist);

	if (evlist->cpus->map[0] < 0)
		opts->no_inherit = true;

	list_for_each_entry(evsel, &evlist->entries, node)
		perf_evsel__config(evsel, opts);

	if (evlist->nr_entries > 1) {
		struct perf_evsel *first = perf_evlist__first(evlist);

		list_for_each_entry(evsel, &evlist->entries, node) {
			if (evsel->attr.sample_type == first->attr.sample_type)
				continue;
			use_sample_identifier = perf_can_sample_identifier();
			break;
		}
		list_for_each_entry(evsel, &evlist->entries, node)
			perf_evsel__set_sample_id(evsel, use_sample_identifier);
	}

	perf_evlist__set_id_pos(evlist);
}
