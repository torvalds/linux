// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2019 Facebook */

#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <linux/err.h>
#include <linux/zalloc.h>
#include <bpf/bpf.h>
#include <bpf/btf.h>
#include <bpf/libbpf.h>

#include "bpf_counter.h"
#include "counts.h"
#include "debug.h"
#include "evsel.h"
#include "target.h"

#include "bpf_skel/bpf_prog_profiler.skel.h"

static inline void *u64_to_ptr(__u64 ptr)
{
	return (void *)(unsigned long)ptr;
}

static void set_max_rlimit(void)
{
	struct rlimit rinf = { RLIM_INFINITY, RLIM_INFINITY };

	setrlimit(RLIMIT_MEMLOCK, &rinf);
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
	struct bpf_prog_info_linear *info_linear;
	struct bpf_func_info *func_info;
	const struct btf_type *t;
	char *name = NULL;
	struct btf *btf;

	info_linear = bpf_program__get_prog_info_linear(
		tgt_fd, 1UL << BPF_PROG_INFO_FUNC_INFO);
	if (IS_ERR_OR_NULL(info_linear)) {
		pr_debug("failed to get info_linear for prog FD %d\n", tgt_fd);
		return NULL;
	}

	if (info_linear->info.btf_id == 0 ||
	    btf__get_from_id(info_linear->info.btf_id, &btf)) {
		pr_debug("prog FD %d doesn't have valid btf\n", tgt_fd);
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

	bpf_map__resize(skel->maps.events, evsel__nr_cpus(evsel));
	bpf_map__resize(skel->maps.fentry_readings, 1);
	bpf_map__resize(skel->maps.accum_readings, 1);

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
	.read       = bpf_program_profiler__read,
	.destroy    = bpf_program_profiler__destroy,
	.install_pe = bpf_program_profiler__install_pe,
};

int bpf_counter__install_pe(struct evsel *evsel, int cpu, int fd)
{
	if (list_empty(&evsel->bpf_counter_list))
		return 0;
	return evsel->bpf_counter_ops->install_pe(evsel, cpu, fd);
}

int bpf_counter__load(struct evsel *evsel, struct target *target)
{
	if (target__has_bpf(target))
		evsel->bpf_counter_ops = &bpf_program_profiler_ops;

	if (evsel->bpf_counter_ops)
		return evsel->bpf_counter_ops->load(evsel, target);
	return 0;
}

int bpf_counter__enable(struct evsel *evsel)
{
	if (list_empty(&evsel->bpf_counter_list))
		return 0;
	return evsel->bpf_counter_ops->enable(evsel);
}

int bpf_counter__read(struct evsel *evsel)
{
	if (list_empty(&evsel->bpf_counter_list))
		return -EAGAIN;
	return evsel->bpf_counter_ops->read(evsel);
}

void bpf_counter__destroy(struct evsel *evsel)
{
	if (list_empty(&evsel->bpf_counter_list))
		return;
	evsel->bpf_counter_ops->destroy(evsel);
	evsel->bpf_counter_ops = NULL;
}
