// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2019 Facebook */

#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/time.h>
#include <linux/err.h>
#include <linux/zalloc.h>
#include <api/fs/fs.h>
#include <perf/bpf_perf.h>

#include "bpf_counter.h"
#include "bpf-utils.h"
#include "counts.h"
#include "debug.h"
#include "evsel.h"
#include "evlist.h"
#include "target.h"
#include "cgroup.h"
#include "cpumap.h"
#include "thread_map.h"

#include "bpf_skel/bpf_prog_profiler.skel.h"
#include "bpf_skel/bperf_u.h"
#include "bpf_skel/bperf_leader.skel.h"
#include "bpf_skel/bperf_follower.skel.h"

#define ATTR_MAP_SIZE 16

static inline void *u64_to_ptr(__u64 ptr)
{
	return (void *)(unsigned long)ptr;
}

static struct bpf_counter *bpf_counter_alloc(void)
{
	struct bpf_counter *counter;

	counter = zalloc(sizeof(*counter));
	if (counter)
		INIT_LIST_HEAD(&counter->list);
	return counter;
}

static int bpf_program_profiler__destroy(struct evsel *evsel)
{
	struct bpf_counter *counter, *tmp;

	list_for_each_entry_safe(counter, tmp,
				 &evsel->bpf_counter_list, list) {
		list_del_init(&counter->list);
		bpf_prog_profiler_bpf__destroy(counter->skel);
		free(counter);
	}
	assert(list_empty(&evsel->bpf_counter_list));

	return 0;
}

static char *bpf_target_prog_name(int tgt_fd)
{
	struct bpf_func_info *func_info;
	struct perf_bpil *info_linear;
	const struct btf_type *t;
	struct btf *btf = NULL;
	char *name = NULL;

	info_linear = get_bpf_prog_info_linear(tgt_fd, 1UL << PERF_BPIL_FUNC_INFO);
	if (IS_ERR_OR_NULL(info_linear)) {
		pr_debug("failed to get info_linear for prog FD %d\n", tgt_fd);
		return NULL;
	}

	if (info_linear->info.btf_id == 0) {
		pr_debug("prog FD %d doesn't have valid btf\n", tgt_fd);
		goto out;
	}

	btf = btf__load_from_kernel_by_id(info_linear->info.btf_id);
	if (libbpf_get_error(btf)) {
		pr_debug("failed to load btf for prog FD %d\n", tgt_fd);
		goto out;
	}

	func_info = u64_to_ptr(info_linear->info.func_info);
	t = btf__type_by_id(btf, func_info[0].type_id);
	if (!t) {
		pr_debug("btf %d doesn't have type %d\n",
			 info_linear->info.btf_id, func_info[0].type_id);
		goto out;
	}
	name = strdup(btf__name_by_offset(btf, t->name_off));
out:
	btf__free(btf);
	free(info_linear);
	return name;
}

static int bpf_program_profiler_load_one(struct evsel *evsel, u32 prog_id)
{
	struct bpf_prog_profiler_bpf *skel;
	struct bpf_counter *counter;
	struct bpf_program *prog;
	char *prog_name;
	int prog_fd;
	int err;

	prog_fd = bpf_prog_get_fd_by_id(prog_id);
	if (prog_fd < 0) {
		pr_err("Failed to open fd for bpf prog %u\n", prog_id);
		return -1;
	}
	counter = bpf_counter_alloc();
	if (!counter) {
		close(prog_fd);
		return -1;
	}

	skel = bpf_prog_profiler_bpf__open();
	if (!skel) {
		pr_err("Failed to open bpf skeleton\n");
		goto err_out;
	}

	skel->rodata->num_cpu = evsel__nr_cpus(evsel);

	bpf_map__set_max_entries(skel->maps.events, evsel__nr_cpus(evsel));
	bpf_map__set_max_entries(skel->maps.fentry_readings, 1);
	bpf_map__set_max_entries(skel->maps.accum_readings, 1);

	prog_name = bpf_target_prog_name(prog_fd);
	if (!prog_name) {
		pr_err("Failed to get program name for bpf prog %u. Does it have BTF?\n", prog_id);
		goto err_out;
	}

	bpf_object__for_each_program(prog, skel->obj) {
		err = bpf_program__set_attach_target(prog, prog_fd, prog_name);
		if (err) {
			pr_err("bpf_program__set_attach_target failed.\n"
			       "Does bpf prog %u have BTF?\n", prog_id);
			goto err_out;
		}
	}
	set_max_rlimit();
	err = bpf_prog_profiler_bpf__load(skel);
	if (err) {
		pr_err("bpf_prog_profiler_bpf__load failed\n");
		goto err_out;
	}

	assert(skel != NULL);
	counter->skel = skel;
	list_add(&counter->list, &evsel->bpf_counter_list);
	close(prog_fd);
	return 0;
err_out:
	bpf_prog_profiler_bpf__destroy(skel);
	free(counter);
	close(prog_fd);
	return -1;
}

static int bpf_program_profiler__load(struct evsel *evsel, struct target *target)
{
	char *bpf_str, *bpf_str_, *tok, *saveptr = NULL, *p;
	u32 prog_id;
	int ret;

	bpf_str_ = bpf_str = strdup(target->bpf_str);
	if (!bpf_str)
		return -1;

	while ((tok = strtok_r(bpf_str, ",", &saveptr)) != NULL) {
		prog_id = strtoul(tok, &p, 10);
		if (prog_id == 0 || prog_id == UINT_MAX ||
		    (*p != '\0' && *p != ',')) {
			pr_err("Failed to parse bpf prog ids %s\n",
			       target->bpf_str);
			return -1;
		}

		ret = bpf_program_profiler_load_one(evsel, prog_id);
		if (ret) {
			bpf_program_profiler__destroy(evsel);
			free(bpf_str_);
			return -1;
		}
		bpf_str = NULL;
	}
	free(bpf_str_);
	return 0;
}

static int bpf_program_profiler__enable(struct evsel *evsel)
{
	struct bpf_counter *counter;
	int ret;

	list_for_each_entry(counter, &evsel->bpf_counter_list, list) {
		assert(counter->skel != NULL);
		ret = bpf_prog_profiler_bpf__attach(counter->skel);
		if (ret) {
			bpf_program_profiler__destroy(evsel);
			return ret;
		}
	}
	return 0;
}

static int bpf_program_profiler__disable(struct evsel *evsel)
{
	struct bpf_counter *counter;

	list_for_each_entry(counter, &evsel->bpf_counter_list, list) {
		assert(counter->skel != NULL);
		bpf_prog_profiler_bpf__detach(counter->skel);
	}
	return 0;
}

static int bpf_program_profiler__read(struct evsel *evsel)
{
	// perf_cpu_map uses /sys/devices/system/cpu/online
	int num_cpu = evsel__nr_cpus(evsel);
	// BPF_MAP_TYPE_PERCPU_ARRAY uses /sys/devices/system/cpu/possible
	// Sometimes possible > online, like on a Ryzen 3900X that has 24
	// threads but its possible showed 0-31 -acme
	int num_cpu_bpf = libbpf_num_possible_cpus();
	struct bpf_perf_event_value values[num_cpu_bpf];
	struct bpf_counter *counter;
	int reading_map_fd;
	__u32 key = 0;
	int err, cpu;

	if (list_empty(&evsel->bpf_counter_list))
		return -EAGAIN;

	for (cpu = 0; cpu < num_cpu; cpu++) {
		perf_counts(evsel->counts, cpu, 0)->val = 0;
		perf_counts(evsel->counts, cpu, 0)->ena = 0;
		perf_counts(evsel->counts, cpu, 0)->run = 0;
	}
	list_for_each_entry(counter, &evsel->bpf_counter_list, list) {
		struct bpf_prog_profiler_bpf *skel = counter->skel;

		assert(skel != NULL);
		reading_map_fd = bpf_map__fd(skel->maps.accum_readings);

		err = bpf_map_lookup_elem(reading_map_fd, &key, values);
		if (err) {
			pr_err("failed to read value\n");
			return err;
		}

		for (cpu = 0; cpu < num_cpu; cpu++) {
			perf_counts(evsel->counts, cpu, 0)->val += values[cpu].counter;
			perf_counts(evsel->counts, cpu, 0)->ena += values[cpu].enabled;
			perf_counts(evsel->counts, cpu, 0)->run += values[cpu].running;
		}
	}
	return 0;
}

static int bpf_program_profiler__install_pe(struct evsel *evsel, int cpu,
					    int fd)
{
	struct bpf_prog_profiler_bpf *skel;
	struct bpf_counter *counter;
	int ret;

	list_for_each_entry(counter, &evsel->bpf_counter_list, list) {
		skel = counter->skel;
		assert(skel != NULL);

		ret = bpf_map_update_elem(bpf_map__fd(skel->maps.events),
					  &cpu, &fd, BPF_ANY);
		if (ret)
			return ret;
	}
	return 0;
}

struct bpf_counter_ops bpf_program_profiler_ops = {
	.load       = bpf_program_profiler__load,
	.enable	    = bpf_program_profiler__enable,
	.disable    = bpf_program_profiler__disable,
	.read       = bpf_program_profiler__read,
	.destroy    = bpf_program_profiler__destroy,
	.install_pe = bpf_program_profiler__install_pe,
};

static bool bperf_attr_map_compatible(int attr_map_fd)
{
	struct bpf_map_info map_info = {0};
	__u32 map_info_len = sizeof(map_info);
	int err;

	err = bpf_obj_get_info_by_fd(attr_map_fd, &map_info, &map_info_len);

	if (err)
		return false;
	return (map_info.key_size == sizeof(struct perf_event_attr)) &&
		(map_info.value_size == sizeof(struct perf_event_attr_map_entry));
}

static int bperf_lock_attr_map(struct target *target)
{
	char path[PATH_MAX];
	int map_fd, err;

	if (target->attr_map) {
		scnprintf(path, PATH_MAX, "%s", target->attr_map);
	} else {
		scnprintf(path, PATH_MAX, "%s/fs/bpf/%s", sysfs__mountpoint(),
			  BPF_PERF_DEFAULT_ATTR_MAP_PATH);
	}

	if (access(path, F_OK)) {
		map_fd = bpf_create_map(BPF_MAP_TYPE_HASH,
					sizeof(struct perf_event_attr),
					sizeof(struct perf_event_attr_map_entry),
					ATTR_MAP_SIZE, 0);
		if (map_fd < 0)
			return -1;

		err = bpf_obj_pin(map_fd, path);
		if (err) {
			/* someone pinned the map in parallel? */
			close(map_fd);
			map_fd = bpf_obj_get(path);
			if (map_fd < 0)
				return -1;
		}
	} else {
		map_fd = bpf_obj_get(path);
		if (map_fd < 0)
			return -1;
	}

	if (!bperf_attr_map_compatible(map_fd)) {
		close(map_fd);
		return -1;

	}
	err = flock(map_fd, LOCK_EX);
	if (err) {
		close(map_fd);
		return -1;
	}
	return map_fd;
}

static int bperf_check_target(struct evsel *evsel,
			      struct target *target,
			      enum bperf_filter_type *filter_type,
			      __u32 *filter_entry_cnt)
{
	if (evsel->core.leader->nr_members > 1) {
		pr_err("bpf managed perf events do not yet support groups.\n");
		return -1;
	}

	/* determine filter type based on target */
	if (target->system_wide) {
		*filter_type = BPERF_FILTER_GLOBAL;
		*filter_entry_cnt = 1;
	} else if (target->cpu_list) {
		*filter_type = BPERF_FILTER_CPU;
		*filter_entry_cnt = perf_cpu_map__nr(evsel__cpus(evsel));
	} else if (target->tid) {
		*filter_type = BPERF_FILTER_PID;
		*filter_entry_cnt = perf_thread_map__nr(evsel->core.threads);
	} else if (target->pid || evsel->evlist->workload.pid != -1) {
		*filter_type = BPERF_FILTER_TGID;
		*filter_entry_cnt = perf_thread_map__nr(evsel->core.threads);
	} else {
		pr_err("bpf managed perf events do not yet support these targets.\n");
		return -1;
	}

	return 0;
}

static	struct perf_cpu_map *all_cpu_map;

static int bperf_reload_leader_program(struct evsel *evsel, int attr_map_fd,
				       struct perf_event_attr_map_entry *entry)
{
	struct bperf_leader_bpf *skel = bperf_leader_bpf__open();
	int link_fd, diff_map_fd, err;
	struct bpf_link *link = NULL;

	if (!skel) {
		pr_err("Failed to open leader skeleton\n");
		return -1;
	}

	bpf_map__set_max_entries(skel->maps.events, libbpf_num_possible_cpus());
	err = bperf_leader_bpf__load(skel);
	if (err) {
		pr_err("Failed to load leader skeleton\n");
		goto out;
	}

	link = bpf_program__attach(skel->progs.on_switch);
	if (IS_ERR(link)) {
		pr_err("Failed to attach leader program\n");
		err = PTR_ERR(link);
		goto out;
	}

	link_fd = bpf_link__fd(link);
	diff_map_fd = bpf_map__fd(skel->maps.diff_readings);
	entry->link_id = bpf_link_get_id(link_fd);
	entry->diff_map_id = bpf_map_get_id(diff_map_fd);
	err = bpf_map_update_elem(attr_map_fd, &evsel->core.attr, entry, BPF_ANY);
	assert(err == 0);

	evsel->bperf_leader_link_fd = bpf_link_get_fd_by_id(entry->link_id);
	assert(evsel->bperf_leader_link_fd >= 0);

	/*
	 * save leader_skel for install_pe, which is called within
	 * following evsel__open_per_cpu call
	 */
	evsel->leader_skel = skel;
	evsel__open_per_cpu(evsel, all_cpu_map, -1);

out:
	bperf_leader_bpf__destroy(skel);
	bpf_link__destroy(link);
	return err;
}

static int bperf__load(struct evsel *evsel, struct target *target)
{
	struct perf_event_attr_map_entry entry = {0xffffffff, 0xffffffff};
	int attr_map_fd, diff_map_fd = -1, err;
	enum bperf_filter_type filter_type;
	__u32 filter_entry_cnt, i;

	if (bperf_check_target(evsel, target, &filter_type, &filter_entry_cnt))
		return -1;

	if (!all_cpu_map) {
		all_cpu_map = perf_cpu_map__new(NULL);
		if (!all_cpu_map)
			return -1;
	}

	evsel->bperf_leader_prog_fd = -1;
	evsel->bperf_leader_link_fd = -1;

	/*
	 * Step 1: hold a fd on the leader program and the bpf_link, if
	 * the program is not already gone, reload the program.
	 * Use flock() to ensure exclusive access to the perf_event_attr
	 * map.
	 */
	attr_map_fd = bperf_lock_attr_map(target);
	if (attr_map_fd < 0) {
		pr_err("Failed to lock perf_event_attr map\n");
		return -1;
	}

	err = bpf_map_lookup_elem(attr_map_fd, &evsel->core.attr, &entry);
	if (err) {
		err = bpf_map_update_elem(attr_map_fd, &evsel->core.attr, &entry, BPF_ANY);
		if (err)
			goto out;
	}

	evsel->bperf_leader_link_fd = bpf_link_get_fd_by_id(entry.link_id);
	if (evsel->bperf_leader_link_fd < 0 &&
	    bperf_reload_leader_program(evsel, attr_map_fd, &entry)) {
		err = -1;
		goto out;
	}
	/*
	 * The bpf_link holds reference to the leader program, and the
	 * leader program holds reference to the maps. Therefore, if
	 * link_id is valid, diff_map_id should also be valid.
	 */
	evsel->bperf_leader_prog_fd = bpf_prog_get_fd_by_id(
		bpf_link_get_prog_id(evsel->bperf_leader_link_fd));
	assert(evsel->bperf_leader_prog_fd >= 0);

	diff_map_fd = bpf_map_get_fd_by_id(entry.diff_map_id);
	assert(diff_map_fd >= 0);

	/*
	 * bperf uses BPF_PROG_TEST_RUN to get accurate reading. Check
	 * whether the kernel support it
	 */
	err = bperf_trigger_reading(evsel->bperf_leader_prog_fd, 0);
	if (err) {
		pr_err("The kernel does not support test_run for raw_tp BPF programs.\n"
		       "Therefore, --use-bpf might show inaccurate readings\n");
		goto out;
	}

	/* Step 2: load the follower skeleton */
	evsel->follower_skel = bperf_follower_bpf__open();
	if (!evsel->follower_skel) {
		err = -1;
		pr_err("Failed to open follower skeleton\n");
		goto out;
	}

	/* attach fexit program to the leader program */
	bpf_program__set_attach_target(evsel->follower_skel->progs.fexit_XXX,
				       evsel->bperf_leader_prog_fd, "on_switch");

	/* connect to leader diff_reading map */
	bpf_map__reuse_fd(evsel->follower_skel->maps.diff_readings, diff_map_fd);

	/* set up reading map */
	bpf_map__set_max_entries(evsel->follower_skel->maps.accum_readings,
				 filter_entry_cnt);
	/* set up follower filter based on target */
	bpf_map__set_max_entries(evsel->follower_skel->maps.filter,
				 filter_entry_cnt);
	err = bperf_follower_bpf__load(evsel->follower_skel);
	if (err) {
		pr_err("Failed to load follower skeleton\n");
		bperf_follower_bpf__destroy(evsel->follower_skel);
		evsel->follower_skel = NULL;
		goto out;
	}

	for (i = 0; i < filter_entry_cnt; i++) {
		int filter_map_fd;
		__u32 key;

		if (filter_type == BPERF_FILTER_PID ||
		    filter_type == BPERF_FILTER_TGID)
			key = evsel->core.threads->map[i].pid;
		else if (filter_type == BPERF_FILTER_CPU)
			key = evsel->core.cpus->map[i];
		else
			break;

		filter_map_fd = bpf_map__fd(evsel->follower_skel->maps.filter);
		bpf_map_update_elem(filter_map_fd, &key, &i, BPF_ANY);
	}

	evsel->follower_skel->bss->type = filter_type;

	err = bperf_follower_bpf__attach(evsel->follower_skel);

out:
	if (err && evsel->bperf_leader_link_fd >= 0)
		close(evsel->bperf_leader_link_fd);
	if (err && evsel->bperf_leader_prog_fd >= 0)
		close(evsel->bperf_leader_prog_fd);
	if (diff_map_fd >= 0)
		close(diff_map_fd);

	flock(attr_map_fd, LOCK_UN);
	close(attr_map_fd);

	return err;
}

static int bperf__install_pe(struct evsel *evsel, int cpu, int fd)
{
	struct bperf_leader_bpf *skel = evsel->leader_skel;

	return bpf_map_update_elem(bpf_map__fd(skel->maps.events),
				   &cpu, &fd, BPF_ANY);
}

/*
 * trigger the leader prog on each cpu, so the accum_reading map could get
 * the latest readings.
 */
static int bperf_sync_counters(struct evsel *evsel)
{
	int num_cpu, i, cpu;

	num_cpu = all_cpu_map->nr;
	for (i = 0; i < num_cpu; i++) {
		cpu = all_cpu_map->map[i];
		bperf_trigger_reading(evsel->bperf_leader_prog_fd, cpu);
	}
	return 0;
}

static int bperf__enable(struct evsel *evsel)
{
	evsel->follower_skel->bss->enabled = 1;
	return 0;
}

static int bperf__disable(struct evsel *evsel)
{
	evsel->follower_skel->bss->enabled = 0;
	return 0;
}

static int bperf__read(struct evsel *evsel)
{
	struct bperf_follower_bpf *skel = evsel->follower_skel;
	__u32 num_cpu_bpf = cpu__max_cpu();
	struct bpf_perf_event_value values[num_cpu_bpf];
	int reading_map_fd, err = 0;
	__u32 i, j, num_cpu;

	bperf_sync_counters(evsel);
	reading_map_fd = bpf_map__fd(skel->maps.accum_readings);

	for (i = 0; i < bpf_map__max_entries(skel->maps.accum_readings); i++) {
		__u32 cpu;

		err = bpf_map_lookup_elem(reading_map_fd, &i, values);
		if (err)
			goto out;
		switch (evsel->follower_skel->bss->type) {
		case BPERF_FILTER_GLOBAL:
			assert(i == 0);

			num_cpu = all_cpu_map->nr;
			for (j = 0; j < num_cpu; j++) {
				cpu = all_cpu_map->map[j];
				perf_counts(evsel->counts, cpu, 0)->val = values[cpu].counter;
				perf_counts(evsel->counts, cpu, 0)->ena = values[cpu].enabled;
				perf_counts(evsel->counts, cpu, 0)->run = values[cpu].running;
			}
			break;
		case BPERF_FILTER_CPU:
			cpu = evsel->core.cpus->map[i];
			perf_counts(evsel->counts, i, 0)->val = values[cpu].counter;
			perf_counts(evsel->counts, i, 0)->ena = values[cpu].enabled;
			perf_counts(evsel->counts, i, 0)->run = values[cpu].running;
			break;
		case BPERF_FILTER_PID:
		case BPERF_FILTER_TGID:
			perf_counts(evsel->counts, 0, i)->val = 0;
			perf_counts(evsel->counts, 0, i)->ena = 0;
			perf_counts(evsel->counts, 0, i)->run = 0;

			for (cpu = 0; cpu < num_cpu_bpf; cpu++) {
				perf_counts(evsel->counts, 0, i)->val += values[cpu].counter;
				perf_counts(evsel->counts, 0, i)->ena += values[cpu].enabled;
				perf_counts(evsel->counts, 0, i)->run += values[cpu].running;
			}
			break;
		default:
			break;
		}
	}
out:
	return err;
}

static int bperf__destroy(struct evsel *evsel)
{
	bperf_follower_bpf__destroy(evsel->follower_skel);
	close(evsel->bperf_leader_prog_fd);
	close(evsel->bperf_leader_link_fd);
	return 0;
}

/*
 * bperf: share hardware PMCs with BPF
 *
 * perf uses performance monitoring counters (PMC) to monitor system
 * performance. The PMCs are limited hardware resources. For example,
 * Intel CPUs have 3x fixed PMCs and 4x programmable PMCs per cpu.
 *
 * Modern data center systems use these PMCs in many different ways:
 * system level monitoring, (maybe nested) container level monitoring, per
 * process monitoring, profiling (in sample mode), etc. In some cases,
 * there are more active perf_events than available hardware PMCs. To allow
 * all perf_events to have a chance to run, it is necessary to do expensive
 * time multiplexing of events.
 *
 * On the other hand, many monitoring tools count the common metrics
 * (cycles, instructions). It is a waste to have multiple tools create
 * multiple perf_events of "cycles" and occupy multiple PMCs.
 *
 * bperf tries to reduce such wastes by allowing multiple perf_events of
 * "cycles" or "instructions" (at different scopes) to share PMUs. Instead
 * of having each perf-stat session to read its own perf_events, bperf uses
 * BPF programs to read the perf_events and aggregate readings to BPF maps.
 * Then, the perf-stat session(s) reads the values from these BPF maps.
 *
 *                                ||
 *       shared progs and maps <- || -> per session progs and maps
 *                                ||
 *   ---------------              ||
 *   | perf_events |              ||
 *   ---------------       fexit  ||      -----------------
 *          |             --------||----> | follower prog |
 *       --------------- /        || ---  -----------------
 * cs -> | leader prog |/         ||/        |         |
 *   --> ---------------         /||  --------------  ------------------
 *  /       |         |         / ||  | filter map |  | accum_readings |
 * /  ------------  ------------  ||  --------------  ------------------
 * |  | prev map |  | diff map |  ||                        |
 * |  ------------  ------------  ||                        |
 *  \                             ||                        |
 * = \ ==================================================== | ============
 *    \                                                    /   user space
 *     \                                                  /
 *      \                                                /
 *    BPF_PROG_TEST_RUN                    BPF_MAP_LOOKUP_ELEM
 *        \                                            /
 *         \                                          /
 *          \------  perf-stat ----------------------/
 *
 * The figure above shows the architecture of bperf. Note that the figure
 * is divided into 3 regions: shared progs and maps (top left), per session
 * progs and maps (top right), and user space (bottom).
 *
 * The leader prog is triggered on each context switch (cs). The leader
 * prog reads perf_events and stores the difference (current_reading -
 * previous_reading) to the diff map. For the same metric, e.g. "cycles",
 * multiple perf-stat sessions share the same leader prog.
 *
 * Each perf-stat session creates a follower prog as fexit program to the
 * leader prog. It is possible to attach up to BPF_MAX_TRAMP_PROGS (38)
 * follower progs to the same leader prog. The follower prog checks current
 * task and processor ID to decide whether to add the value from the diff
 * map to its accumulated reading map (accum_readings).
 *
 * Finally, perf-stat user space reads the value from accum_reading map.
 *
 * Besides context switch, it is also necessary to trigger the leader prog
 * before perf-stat reads the value. Otherwise, the accum_reading map may
 * not have the latest reading from the perf_events. This is achieved by
 * triggering the event via sys_bpf(BPF_PROG_TEST_RUN) to each CPU.
 *
 * Comment before the definition of struct perf_event_attr_map_entry
 * describes how different sessions of perf-stat share information about
 * the leader prog.
 */

struct bpf_counter_ops bperf_ops = {
	.load       = bperf__load,
	.enable     = bperf__enable,
	.disable    = bperf__disable,
	.read       = bperf__read,
	.install_pe = bperf__install_pe,
	.destroy    = bperf__destroy,
};

extern struct bpf_counter_ops bperf_cgrp_ops;

static inline bool bpf_counter_skip(struct evsel *evsel)
{
	return list_empty(&evsel->bpf_counter_list) &&
		evsel->follower_skel == NULL;
}

int bpf_counter__install_pe(struct evsel *evsel, int cpu, int fd)
{
	if (bpf_counter_skip(evsel))
		return 0;
	return evsel->bpf_counter_ops->install_pe(evsel, cpu, fd);
}

int bpf_counter__load(struct evsel *evsel, struct target *target)
{
	if (target->bpf_str)
		evsel->bpf_counter_ops = &bpf_program_profiler_ops;
	else if (cgrp_event_expanded && target->use_bpf)
		evsel->bpf_counter_ops = &bperf_cgrp_ops;
	else if (target->use_bpf || evsel->bpf_counter ||
		 evsel__match_bpf_counter_events(evsel->name))
		evsel->bpf_counter_ops = &bperf_ops;

	if (evsel->bpf_counter_ops)
		return evsel->bpf_counter_ops->load(evsel, target);
	return 0;
}

int bpf_counter__enable(struct evsel *evsel)
{
	if (bpf_counter_skip(evsel))
		return 0;
	return evsel->bpf_counter_ops->enable(evsel);
}

int bpf_counter__disable(struct evsel *evsel)
{
	if (bpf_counter_skip(evsel))
		return 0;
	return evsel->bpf_counter_ops->disable(evsel);
}

int bpf_counter__read(struct evsel *evsel)
{
	if (bpf_counter_skip(evsel))
		return -EAGAIN;
	return evsel->bpf_counter_ops->read(evsel);
}

void bpf_counter__destroy(struct evsel *evsel)
{
	if (bpf_counter_skip(evsel))
		return;
	evsel->bpf_counter_ops->destroy(evsel);
	evsel->bpf_counter_ops = NULL;
}
