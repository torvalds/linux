// SPDX-License-Identifier: GPL-2.0
/*
 * bpf_kwork_top.c
 *
 * Copyright (c) 2022  Huawei Inc,  Yang Jihong <yangjihong1@huawei.com>
 */

#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <linux/time64.h>

#include "util/debug.h"
#include "util/evsel.h"
#include "util/kwork.h"

#include <bpf/bpf.h>
#include <perf/cpumap.h>

#include "util/bpf_skel/kwork_top.skel.h"

/*
 * This should be in sync with "util/kwork_top.bpf.c"
 */
#define MAX_COMMAND_LEN 16

struct time_data {
	__u64 timestamp;
};

struct work_data {
	__u64 runtime;
};

struct task_data {
	__u32 tgid;
	__u32 is_kthread;
	char comm[MAX_COMMAND_LEN];
};

struct work_key {
	__u32 type;
	__u32 pid;
	__u64 task_p;
};

struct task_key {
	__u32 pid;
	__u32 cpu;
};

struct kwork_class_bpf {
	struct kwork_class *class;
	void (*load_prepare)(void);
};

static struct kwork_top_bpf *skel;

void perf_kwork__top_start(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	skel->bss->from_timestamp = (u64)ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
	skel->bss->enabled = 1;
	pr_debug("perf kwork top start at: %lld\n", skel->bss->from_timestamp);
}

void perf_kwork__top_finish(void)
{
	struct timespec ts;

	skel->bss->enabled = 0;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	skel->bss->to_timestamp = (u64)ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
	pr_debug("perf kwork top finish at: %lld\n", skel->bss->to_timestamp);
}

static void irq_load_prepare(void)
{
	bpf_program__set_autoload(skel->progs.on_irq_handler_entry, true);
	bpf_program__set_autoload(skel->progs.on_irq_handler_exit, true);
}

static struct kwork_class_bpf kwork_irq_bpf = {
	.load_prepare = irq_load_prepare,
};

static void softirq_load_prepare(void)
{
	bpf_program__set_autoload(skel->progs.on_softirq_entry, true);
	bpf_program__set_autoload(skel->progs.on_softirq_exit, true);
}

static struct kwork_class_bpf kwork_softirq_bpf = {
	.load_prepare = softirq_load_prepare,
};

static void sched_load_prepare(void)
{
	bpf_program__set_autoload(skel->progs.on_switch, true);
}

static struct kwork_class_bpf kwork_sched_bpf = {
	.load_prepare = sched_load_prepare,
};

static struct kwork_class_bpf *
kwork_class_bpf_supported_list[KWORK_CLASS_MAX] = {
	[KWORK_CLASS_IRQ]	= &kwork_irq_bpf,
	[KWORK_CLASS_SOFTIRQ]	= &kwork_softirq_bpf,
	[KWORK_CLASS_SCHED]	= &kwork_sched_bpf,
};

static bool valid_kwork_class_type(enum kwork_class_type type)
{
	return type >= 0 && type < KWORK_CLASS_MAX;
}

static int setup_filters(struct perf_kwork *kwork)
{
	if (kwork->cpu_list) {
		int idx, nr_cpus, fd;
		struct perf_cpu_map *map;
		struct perf_cpu cpu;

		fd = bpf_map__fd(skel->maps.kwork_top_cpu_filter);
		if (fd < 0) {
			pr_debug("Invalid cpu filter fd\n");
			return -1;
		}

		map = perf_cpu_map__new(kwork->cpu_list);
		if (!map) {
			pr_debug("Invalid cpu_list\n");
			return -1;
		}

		nr_cpus = libbpf_num_possible_cpus();
		perf_cpu_map__for_each_cpu(cpu, idx, map) {
			u8 val = 1;

			if (cpu.cpu >= nr_cpus) {
				perf_cpu_map__put(map);
				pr_err("Requested cpu %d too large\n", cpu.cpu);
				return -1;
			}
			bpf_map_update_elem(fd, &cpu.cpu, &val, BPF_ANY);
		}
		perf_cpu_map__put(map);

		skel->bss->has_cpu_filter = 1;
	}

	return 0;
}

int perf_kwork__top_prepare_bpf(struct perf_kwork *kwork __maybe_unused)
{
	struct bpf_program *prog;
	struct kwork_class *class;
	struct kwork_class_bpf *class_bpf;
	enum kwork_class_type type;

	skel = kwork_top_bpf__open();
	if (!skel) {
		pr_debug("Failed to open kwork top skeleton\n");
		return -1;
	}

	/*
	 * set all progs to non-autoload,
	 * then set corresponding progs according to config
	 */
	bpf_object__for_each_program(prog, skel->obj)
		bpf_program__set_autoload(prog, false);

	list_for_each_entry(class, &kwork->class_list, list) {
		type = class->type;
		if (!valid_kwork_class_type(type) ||
		    !kwork_class_bpf_supported_list[type]) {
			pr_err("Unsupported bpf trace class %s\n", class->name);
			goto out;
		}

		class_bpf = kwork_class_bpf_supported_list[type];
		class_bpf->class = class;

		if (class_bpf->load_prepare)
			class_bpf->load_prepare();
	}

	if (kwork_top_bpf__load(skel)) {
		pr_debug("Failed to load kwork top skeleton\n");
		goto out;
	}

	if (setup_filters(kwork))
		goto out;

	if (kwork_top_bpf__attach(skel)) {
		pr_debug("Failed to attach kwork top skeleton\n");
		goto out;
	}

	return 0;

out:
	kwork_top_bpf__destroy(skel);
	return -1;
}

static void read_task_info(struct kwork_work *work)
{
	int fd;
	struct task_data data;
	struct task_key key = {
		.pid = work->id,
		.cpu = work->cpu,
	};

	fd = bpf_map__fd(skel->maps.kwork_top_tasks);
	if (fd < 0) {
		pr_debug("Invalid top tasks map fd\n");
		return;
	}

	if (!bpf_map_lookup_elem(fd, &key, &data)) {
		work->tgid = data.tgid;
		work->is_kthread = data.is_kthread;
		work->name = strdup(data.comm);
	}
}
static int add_work(struct perf_kwork *kwork, struct work_key *key,
		    struct work_data *data, int cpu)
{
	struct kwork_class_bpf *bpf_trace;
	struct kwork_work *work;
	struct kwork_work tmp = {
		.id = key->pid,
		.cpu = cpu,
		.name = NULL,
	};
	enum kwork_class_type type = key->type;

	if (!valid_kwork_class_type(type)) {
		pr_debug("Invalid class type %d to add work\n", type);
		return -1;
	}

	bpf_trace = kwork_class_bpf_supported_list[type];
	tmp.class = bpf_trace->class;

	work = perf_kwork_add_work(kwork, tmp.class, &tmp);
	if (!work)
		return -1;

	work->total_runtime = data->runtime;
	read_task_info(work);

	return 0;
}

int perf_kwork__top_read_bpf(struct perf_kwork *kwork)
{
	int i, fd, nr_cpus;
	struct work_data *data;
	struct work_key key, prev;

	fd = bpf_map__fd(skel->maps.kwork_top_works);
	if (fd < 0) {
		pr_debug("Invalid top runtime fd\n");
		return -1;
	}

	nr_cpus = libbpf_num_possible_cpus();
	data = calloc(nr_cpus, sizeof(struct work_data));
	if (!data)
		return -1;

	memset(&prev, 0, sizeof(prev));
	while (!bpf_map_get_next_key(fd, &prev, &key)) {
		if ((bpf_map_lookup_elem(fd, &key, data)) != 0) {
			pr_debug("Failed to lookup top elem\n");
			return -1;
		}

		for (i = 0; i < nr_cpus; i++) {
			if (data[i].runtime == 0)
				continue;

			if (add_work(kwork, &key, &data[i], i))
				return -1;
		}
		prev = key;
	}
	free(data);

	return 0;
}

void perf_kwork__top_cleanup_bpf(void)
{
	kwork_top_bpf__destroy(skel);
}
