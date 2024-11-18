// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2021 Facebook */
/* Copyright (c) 2021 Google */

#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <linux/err.h>
#include <linux/zalloc.h>
#include <linux/perf_event.h>
#include <api/fs/fs.h>
#include <perf/bpf_perf.h>

#include "affinity.h"
#include "bpf_counter.h"
#include "cgroup.h"
#include "counts.h"
#include "debug.h"
#include "evsel.h"
#include "evlist.h"
#include "target.h"
#include "cpumap.h"
#include "thread_map.h"

#include "bpf_skel/bperf_cgroup.skel.h"

static struct perf_event_attr cgrp_switch_attr = {
	.type = PERF_TYPE_SOFTWARE,
	.config = PERF_COUNT_SW_CGROUP_SWITCHES,
	.size = sizeof(cgrp_switch_attr),
	.sample_period = 1,
	.disabled = 1,
};

static struct evsel *cgrp_switch;
static struct bperf_cgroup_bpf *skel;

#define FD(evt, cpu) (*(int *)xyarray__entry(evt->core.fd, cpu, 0))

static int bperf_load_program(struct evlist *evlist)
{
	struct bpf_link *link;
	struct evsel *evsel;
	struct cgroup *cgrp, *leader_cgrp;
	int i, j;
	struct perf_cpu cpu;
	int total_cpus = cpu__max_cpu().cpu;
	int map_size, map_fd;
	int prog_fd, err;

	skel = bperf_cgroup_bpf__open();
	if (!skel) {
		pr_err("Failed to open cgroup skeleton\n");
		return -1;
	}

	skel->rodata->num_cpus = total_cpus;
	skel->rodata->num_events = evlist->core.nr_entries / nr_cgroups;

	if (cgroup_is_v2("perf_event") > 0)
		skel->rodata->use_cgroup_v2 = 1;

	BUG_ON(evlist->core.nr_entries % nr_cgroups != 0);

	/* we need one copy of events per cpu for reading */
	map_size = total_cpus * evlist->core.nr_entries / nr_cgroups;
	bpf_map__set_max_entries(skel->maps.events, map_size);
	bpf_map__set_max_entries(skel->maps.cgrp_idx, nr_cgroups);
	/* previous result is saved in a per-cpu array */
	map_size = evlist->core.nr_entries / nr_cgroups;
	bpf_map__set_max_entries(skel->maps.prev_readings, map_size);
	/* cgroup result needs all events (per-cpu) */
	map_size = evlist->core.nr_entries;
	bpf_map__set_max_entries(skel->maps.cgrp_readings, map_size);

	set_max_rlimit();

	err = bperf_cgroup_bpf__load(skel);
	if (err) {
		pr_err("Failed to load cgroup skeleton\n");
		goto out;
	}

	err = -1;

	cgrp_switch = evsel__new(&cgrp_switch_attr);
	if (evsel__open_per_cpu(cgrp_switch, evlist->core.all_cpus, -1) < 0) {
		pr_err("Failed to open cgroup switches event\n");
		goto out;
	}

	perf_cpu_map__for_each_cpu(cpu, i, evlist->core.all_cpus) {
		link = bpf_program__attach_perf_event(skel->progs.on_cgrp_switch,
						      FD(cgrp_switch, i));
		if (IS_ERR(link)) {
			pr_err("Failed to attach cgroup program\n");
			err = PTR_ERR(link);
			goto out;
		}
	}

	/*
	 * Update cgrp_idx map from cgroup-id to event index.
	 */
	cgrp = NULL;
	i = 0;

	evlist__for_each_entry(evlist, evsel) {
		if (cgrp == NULL || evsel->cgrp == leader_cgrp) {
			leader_cgrp = evsel->cgrp;
			evsel->cgrp = NULL;

			/* open single copy of the events w/o cgroup */
			err = evsel__open_per_cpu(evsel, evsel->core.cpus, -1);
			if (err == 0)
				evsel->supported = true;

			map_fd = bpf_map__fd(skel->maps.events);
			perf_cpu_map__for_each_cpu(cpu, j, evsel->core.cpus) {
				int fd = FD(evsel, j);
				__u32 idx = evsel->core.idx * total_cpus + cpu.cpu;

				bpf_map_update_elem(map_fd, &idx, &fd, BPF_ANY);
			}

			evsel->cgrp = leader_cgrp;
		}

		if (evsel->cgrp == cgrp)
			continue;

		cgrp = evsel->cgrp;

		if (read_cgroup_id(cgrp) < 0) {
			pr_debug("Failed to get cgroup id for %s\n", cgrp->name);
			cgrp->id = 0;
		}

		map_fd = bpf_map__fd(skel->maps.cgrp_idx);
		err = bpf_map_update_elem(map_fd, &cgrp->id, &i, BPF_ANY);
		if (err < 0) {
			pr_err("Failed to update cgroup index map\n");
			goto out;
		}

		i++;
	}

	/*
	 * bperf uses BPF_PROG_TEST_RUN to get accurate reading. Check
	 * whether the kernel support it
	 */
	prog_fd = bpf_program__fd(skel->progs.trigger_read);
	err = bperf_trigger_reading(prog_fd, 0);
	if (err) {
		pr_warning("The kernel does not support test_run for raw_tp BPF programs.\n"
			   "Therefore, --for-each-cgroup might show inaccurate readings\n");
		err = 0;
	}

out:
	return err;
}

static int bperf_cgrp__load(struct evsel *evsel,
			    struct target *target __maybe_unused)
{
	static bool bperf_loaded = false;

	evsel->bperf_leader_prog_fd = -1;
	evsel->bperf_leader_link_fd = -1;

	if (!bperf_loaded && bperf_load_program(evsel->evlist))
		return -1;

	bperf_loaded = true;
	/* just to bypass bpf_counter_skip() */
	evsel->follower_skel = (struct bperf_follower_bpf *)skel;

	return 0;
}

static int bperf_cgrp__install_pe(struct evsel *evsel __maybe_unused,
				  int cpu __maybe_unused, int fd __maybe_unused)
{
	/* nothing to do */
	return 0;
}

/*
 * trigger the leader prog on each cpu, so the cgrp_reading map could get
 * the latest results.
 */
static int bperf_cgrp__sync_counters(struct evlist *evlist)
{
	struct perf_cpu cpu;
	int idx;
	int prog_fd = bpf_program__fd(skel->progs.trigger_read);

	perf_cpu_map__for_each_cpu(cpu, idx, evlist->core.all_cpus)
		bperf_trigger_reading(prog_fd, cpu.cpu);

	return 0;
}

static int bperf_cgrp__enable(struct evsel *evsel)
{
	if (evsel->core.idx)
		return 0;

	bperf_cgrp__sync_counters(evsel->evlist);

	skel->bss->enabled = 1;
	return 0;
}

static int bperf_cgrp__disable(struct evsel *evsel)
{
	if (evsel->core.idx)
		return 0;

	bperf_cgrp__sync_counters(evsel->evlist);

	skel->bss->enabled = 0;
	return 0;
}

static int bperf_cgrp__read(struct evsel *evsel)
{
	struct evlist *evlist = evsel->evlist;
	int total_cpus = cpu__max_cpu().cpu;
	struct perf_counts_values *counts;
	struct bpf_perf_event_value *values;
	int reading_map_fd, err = 0;

	if (evsel->core.idx)
		return 0;

	bperf_cgrp__sync_counters(evsel->evlist);

	values = calloc(total_cpus, sizeof(*values));
	if (values == NULL)
		return -ENOMEM;

	reading_map_fd = bpf_map__fd(skel->maps.cgrp_readings);

	evlist__for_each_entry(evlist, evsel) {
		__u32 idx = evsel->core.idx;
		int i;
		struct perf_cpu cpu;

		err = bpf_map_lookup_elem(reading_map_fd, &idx, values);
		if (err) {
			pr_err("bpf map lookup failed: idx=%u, event=%s, cgrp=%s\n",
			       idx, evsel__name(evsel), evsel->cgrp->name);
			goto out;
		}

		perf_cpu_map__for_each_cpu(cpu, i, evsel->core.cpus) {
			counts = perf_counts(evsel->counts, i, 0);
			counts->val = values[cpu.cpu].counter;
			counts->ena = values[cpu.cpu].enabled;
			counts->run = values[cpu.cpu].running;
		}
	}

out:
	free(values);
	return err;
}

static int bperf_cgrp__destroy(struct evsel *evsel)
{
	if (evsel->core.idx)
		return 0;

	bperf_cgroup_bpf__destroy(skel);
	evsel__delete(cgrp_switch);  // it'll destroy on_switch progs too

	return 0;
}

struct bpf_counter_ops bperf_cgrp_ops = {
	.load       = bperf_cgrp__load,
	.enable     = bperf_cgrp__enable,
	.disable    = bperf_cgrp__disable,
	.read       = bperf_cgrp__read,
	.install_pe = bperf_cgrp__install_pe,
	.destroy    = bperf_cgrp__destroy,
};
