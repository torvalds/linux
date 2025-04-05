// SPDX-License-Identifier: GPL-2.0
/*
 * bpf_kwork.c
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

#include "util/bpf_skel/kwork_trace.skel.h"

/*
 * This should be in sync with "util/kwork_trace.bpf.c"
 */
#define MAX_KWORKNAME 128

struct work_key {
	u32 type;
	u32 cpu;
	u64 id;
};

struct report_data {
	u64 nr;
	u64 total_time;
	u64 max_time;
	u64 max_time_start;
	u64 max_time_end;
};

struct kwork_class_bpf {
	struct kwork_class *class;

	void (*load_prepare)(struct perf_kwork *kwork);
	int  (*get_work_name)(struct work_key *key, char **ret_name);
};

static struct kwork_trace_bpf *skel;

static struct timespec ts_start;
static struct timespec ts_end;

void perf_kwork__trace_start(void)
{
	clock_gettime(CLOCK_MONOTONIC, &ts_start);
	skel->bss->enabled = 1;
}

void perf_kwork__trace_finish(void)
{
	clock_gettime(CLOCK_MONOTONIC, &ts_end);
	skel->bss->enabled = 0;
}

static int get_work_name_from_map(struct work_key *key, char **ret_name)
{
	char name[MAX_KWORKNAME] = { 0 };
	int fd = bpf_map__fd(skel->maps.perf_kwork_names);

	*ret_name = NULL;

	if (fd < 0) {
		pr_debug("Invalid names map fd\n");
		return 0;
	}

	if ((bpf_map_lookup_elem(fd, key, name) == 0) && (strlen(name) != 0)) {
		*ret_name = strdup(name);
		if (*ret_name == NULL) {
			pr_err("Failed to copy work name\n");
			return -1;
		}
	}

	return 0;
}

static void irq_load_prepare(struct perf_kwork *kwork)
{
	if (kwork->report == KWORK_REPORT_RUNTIME) {
		bpf_program__set_autoload(skel->progs.report_irq_handler_entry, true);
		bpf_program__set_autoload(skel->progs.report_irq_handler_exit, true);
	}
}

static struct kwork_class_bpf kwork_irq_bpf = {
	.load_prepare  = irq_load_prepare,
	.get_work_name = get_work_name_from_map,
};

static void softirq_load_prepare(struct perf_kwork *kwork)
{
	if (kwork->report == KWORK_REPORT_RUNTIME) {
		bpf_program__set_autoload(skel->progs.report_softirq_entry, true);
		bpf_program__set_autoload(skel->progs.report_softirq_exit, true);
	} else if (kwork->report == KWORK_REPORT_LATENCY) {
		bpf_program__set_autoload(skel->progs.latency_softirq_raise, true);
		bpf_program__set_autoload(skel->progs.latency_softirq_entry, true);
	}
}

static struct kwork_class_bpf kwork_softirq_bpf = {
	.load_prepare  = softirq_load_prepare,
	.get_work_name = get_work_name_from_map,
};

static void workqueue_load_prepare(struct perf_kwork *kwork)
{
	if (kwork->report == KWORK_REPORT_RUNTIME) {
		bpf_program__set_autoload(skel->progs.report_workqueue_execute_start, true);
		bpf_program__set_autoload(skel->progs.report_workqueue_execute_end, true);
	} else if (kwork->report == KWORK_REPORT_LATENCY) {
		bpf_program__set_autoload(skel->progs.latency_workqueue_activate_work, true);
		bpf_program__set_autoload(skel->progs.latency_workqueue_execute_start, true);
	}
}

static struct kwork_class_bpf kwork_workqueue_bpf = {
	.load_prepare  = workqueue_load_prepare,
	.get_work_name = get_work_name_from_map,
};

static struct kwork_class_bpf *
kwork_class_bpf_supported_list[KWORK_CLASS_MAX] = {
	[KWORK_CLASS_IRQ]       = &kwork_irq_bpf,
	[KWORK_CLASS_SOFTIRQ]   = &kwork_softirq_bpf,
	[KWORK_CLASS_WORKQUEUE] = &kwork_workqueue_bpf,
};

static bool valid_kwork_class_type(enum kwork_class_type type)
{
	return type >= 0 && type < KWORK_CLASS_MAX ? true : false;
}

static int setup_filters(struct perf_kwork *kwork)
{
	if (kwork->cpu_list != NULL) {
		int idx, nr_cpus;
		struct perf_cpu_map *map;
		struct perf_cpu cpu;
		int fd = bpf_map__fd(skel->maps.perf_kwork_cpu_filter);

		if (fd < 0) {
			pr_debug("Invalid cpu filter fd\n");
			return -1;
		}

		map = perf_cpu_map__new(kwork->cpu_list);
		if (map == NULL) {
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
	}

	if (kwork->profile_name != NULL) {
		int key, fd;

		if (strlen(kwork->profile_name) >= MAX_KWORKNAME) {
			pr_err("Requested name filter %s too large, limit to %d\n",
			       kwork->profile_name, MAX_KWORKNAME - 1);
			return -1;
		}

		fd = bpf_map__fd(skel->maps.perf_kwork_name_filter);
		if (fd < 0) {
			pr_debug("Invalid name filter fd\n");
			return -1;
		}

		key = 0;
		bpf_map_update_elem(fd, &key, kwork->profile_name, BPF_ANY);
	}

	return 0;
}

int perf_kwork__trace_prepare_bpf(struct perf_kwork *kwork)
{
	struct bpf_program *prog;
	struct kwork_class *class;
	struct kwork_class_bpf *class_bpf;
	enum kwork_class_type type;

	skel = kwork_trace_bpf__open();
	if (!skel) {
		pr_debug("Failed to open kwork trace skeleton\n");
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
		    (kwork_class_bpf_supported_list[type] == NULL)) {
			pr_err("Unsupported bpf trace class %s\n", class->name);
			goto out;
		}

		class_bpf = kwork_class_bpf_supported_list[type];
		class_bpf->class = class;

		if (class_bpf->load_prepare != NULL)
			class_bpf->load_prepare(kwork);
	}

	if (kwork->cpu_list != NULL)
		skel->rodata->has_cpu_filter = 1;
	if (kwork->profile_name != NULL)
		skel->rodata->has_name_filter = 1;

	if (kwork_trace_bpf__load(skel)) {
		pr_debug("Failed to load kwork trace skeleton\n");
		goto out;
	}

	if (setup_filters(kwork))
		goto out;

	if (kwork_trace_bpf__attach(skel)) {
		pr_debug("Failed to attach kwork trace skeleton\n");
		goto out;
	}

	return 0;

out:
	kwork_trace_bpf__destroy(skel);
	return -1;
}

static int add_work(struct perf_kwork *kwork,
		    struct work_key *key,
		    struct report_data *data)
{
	struct kwork_work *work;
	struct kwork_class_bpf *bpf_trace;
	struct kwork_work tmp = {
		.id = key->id,
		.name = NULL,
		.cpu = key->cpu,
	};
	enum kwork_class_type type = key->type;

	if (!valid_kwork_class_type(type)) {
		pr_debug("Invalid class type %d to add work\n", type);
		return -1;
	}

	bpf_trace = kwork_class_bpf_supported_list[type];
	tmp.class = bpf_trace->class;

	if ((bpf_trace->get_work_name != NULL) &&
	    (bpf_trace->get_work_name(key, &tmp.name)))
		return -1;

	work = kwork->add_work(kwork, tmp.class, &tmp);
	if (work == NULL)
		return -1;

	if (kwork->report == KWORK_REPORT_RUNTIME) {
		work->nr_atoms = data->nr;
		work->total_runtime = data->total_time;
		work->max_runtime = data->max_time;
		work->max_runtime_start = data->max_time_start;
		work->max_runtime_end = data->max_time_end;
	} else if (kwork->report == KWORK_REPORT_LATENCY) {
		work->nr_atoms = data->nr;
		work->total_latency = data->total_time;
		work->max_latency = data->max_time;
		work->max_latency_start = data->max_time_start;
		work->max_latency_end = data->max_time_end;
	} else {
		pr_debug("Invalid bpf report type %d\n", kwork->report);
		return -1;
	}

	kwork->timestart = (u64)ts_start.tv_sec * NSEC_PER_SEC + ts_start.tv_nsec;
	kwork->timeend = (u64)ts_end.tv_sec * NSEC_PER_SEC + ts_end.tv_nsec;

	return 0;
}

int perf_kwork__report_read_bpf(struct perf_kwork *kwork)
{
	struct report_data data;
	struct work_key key = {
		.type = 0,
		.cpu  = 0,
		.id   = 0,
	};
	struct work_key prev = {
		.type = 0,
		.cpu  = 0,
		.id   = 0,
	};
	int fd = bpf_map__fd(skel->maps.perf_kwork_report);

	if (fd < 0) {
		pr_debug("Invalid report fd\n");
		return -1;
	}

	while (!bpf_map_get_next_key(fd, &prev, &key)) {
		if ((bpf_map_lookup_elem(fd, &key, &data)) != 0) {
			pr_debug("Failed to lookup report elem\n");
			return -1;
		}

		if ((data.nr != 0) && (add_work(kwork, &key, &data) != 0))
			return -1;

		prev = key;
	}
	return 0;
}

void perf_kwork__report_cleanup_bpf(void)
{
	kwork_trace_bpf__destroy(skel);
}
