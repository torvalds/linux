// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2022, Huawei

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

/*
 * This should be in sync with "util/kwork.h"
 */
enum kwork_class_type {
	KWORK_CLASS_IRQ,
	KWORK_CLASS_SOFTIRQ,
	KWORK_CLASS_WORKQUEUE,
	KWORK_CLASS_SCHED,
	KWORK_CLASS_MAX,
};

#define MAX_ENTRIES     102400
#define MAX_NR_CPUS     2048
#define PF_KTHREAD      0x00200000
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

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct time_data);
} kwork_top_task_time SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(key_size, sizeof(struct work_key));
	__uint(value_size, sizeof(struct time_data));
	__uint(max_entries, MAX_ENTRIES);
} kwork_top_irq_time SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(struct task_key));
	__uint(value_size, sizeof(struct task_data));
	__uint(max_entries, MAX_ENTRIES);
} kwork_top_tasks SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(key_size, sizeof(struct work_key));
	__uint(value_size, sizeof(struct work_data));
	__uint(max_entries, MAX_ENTRIES);
} kwork_top_works SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(u32));
	__uint(value_size, sizeof(u8));
	__uint(max_entries, MAX_NR_CPUS);
} kwork_top_cpu_filter SEC(".maps");

int enabled = 0;

int has_cpu_filter = 0;

__u64 from_timestamp = 0;
__u64 to_timestamp = 0;

static __always_inline int cpu_is_filtered(__u32 cpu)
{
	__u8 *cpu_val;

	if (has_cpu_filter) {
		cpu_val = bpf_map_lookup_elem(&kwork_top_cpu_filter, &cpu);
		if (!cpu_val)
			return 1;
	}

	return 0;
}

static __always_inline void update_task_info(struct task_struct *task, __u32 cpu)
{
	struct task_key key = {
		.pid = task->pid,
		.cpu = cpu,
	};

	if (!bpf_map_lookup_elem(&kwork_top_tasks, &key)) {
		struct task_data data = {
			.tgid = task->tgid,
			.is_kthread = task->flags & PF_KTHREAD ? 1 : 0,
		};
		BPF_CORE_READ_STR_INTO(&data.comm, task, comm);

		bpf_map_update_elem(&kwork_top_tasks, &key, &data, BPF_ANY);
	}
}

static __always_inline void update_work(struct work_key *key, __u64 delta)
{
	struct work_data *data;

	data = bpf_map_lookup_elem(&kwork_top_works, key);
	if (data) {
		data->runtime += delta;
	} else {
		struct work_data new_data = {
			.runtime = delta,
		};

		bpf_map_update_elem(&kwork_top_works, key, &new_data, BPF_ANY);
	}
}

static void on_sched_out(struct task_struct *task, __u64 ts, __u32 cpu)
{
	__u64 delta;
	struct time_data *pelem;

	pelem = bpf_task_storage_get(&kwork_top_task_time, task, NULL, 0);
	if (pelem)
		delta = ts - pelem->timestamp;
	else
		delta = ts - from_timestamp;

	struct work_key key = {
		.type = KWORK_CLASS_SCHED,
		.pid = task->pid,
		.task_p = (__u64)task,
	};

	update_work(&key, delta);
	update_task_info(task, cpu);
}

static void on_sched_in(struct task_struct *task, __u64 ts)
{
	struct time_data *pelem;

	pelem = bpf_task_storage_get(&kwork_top_task_time, task, NULL,
				     BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (pelem)
		pelem->timestamp = ts;
}

SEC("tp_btf/sched_switch")
int on_switch(u64 *ctx)
{
	struct task_struct *prev, *next;

	prev = (struct task_struct *)ctx[1];
	next = (struct task_struct *)ctx[2];

	if (!enabled)
		return 0;

	__u32 cpu = bpf_get_smp_processor_id();

	if (cpu_is_filtered(cpu))
		return 0;

	__u64 ts = bpf_ktime_get_ns();

	on_sched_out(prev, ts, cpu);
	on_sched_in(next, ts);

	return 0;
}

SEC("tp_btf/irq_handler_entry")
int on_irq_handler_entry(u64 *cxt)
{
	struct task_struct *task;

	if (!enabled)
		return 0;

	__u32 cpu = bpf_get_smp_processor_id();

	if (cpu_is_filtered(cpu))
		return 0;

	__u64 ts = bpf_ktime_get_ns();

	task = (struct task_struct *)bpf_get_current_task();
	if (!task)
		return 0;

	struct work_key key = {
		.type = KWORK_CLASS_IRQ,
		.pid = BPF_CORE_READ(task, pid),
		.task_p = (__u64)task,
	};

	struct time_data data = {
		.timestamp = ts,
	};

	bpf_map_update_elem(&kwork_top_irq_time, &key, &data, BPF_ANY);

	return 0;
}

SEC("tp_btf/irq_handler_exit")
int on_irq_handler_exit(u64 *cxt)
{
	__u64 delta;
	struct task_struct *task;
	struct time_data *pelem;

	if (!enabled)
		return 0;

	__u32 cpu = bpf_get_smp_processor_id();

	if (cpu_is_filtered(cpu))
		return 0;

	__u64 ts = bpf_ktime_get_ns();

	task = (struct task_struct *)bpf_get_current_task();
	if (!task)
		return 0;

	struct work_key key = {
		.type = KWORK_CLASS_IRQ,
		.pid = BPF_CORE_READ(task, pid),
		.task_p = (__u64)task,
	};

	pelem = bpf_map_lookup_elem(&kwork_top_irq_time, &key);
	if (pelem && pelem->timestamp != 0)
		delta = ts - pelem->timestamp;
	else
		delta = ts - from_timestamp;

	update_work(&key, delta);

	return 0;
}

SEC("tp_btf/softirq_entry")
int on_softirq_entry(u64 *cxt)
{
	struct task_struct *task;

	if (!enabled)
		return 0;

	__u32 cpu = bpf_get_smp_processor_id();

	if (cpu_is_filtered(cpu))
		return 0;

	__u64 ts = bpf_ktime_get_ns();

	task = (struct task_struct *)bpf_get_current_task();
	if (!task)
		return 0;

	struct work_key key = {
		.type = KWORK_CLASS_SOFTIRQ,
		.pid = BPF_CORE_READ(task, pid),
		.task_p = (__u64)task,
	};

	struct time_data data = {
		.timestamp = ts,
	};

	bpf_map_update_elem(&kwork_top_irq_time, &key, &data, BPF_ANY);

	return 0;
}

SEC("tp_btf/softirq_exit")
int on_softirq_exit(u64 *cxt)
{
	__u64 delta;
	struct task_struct *task;
	struct time_data *pelem;

	if (!enabled)
		return 0;

	__u32 cpu = bpf_get_smp_processor_id();

	if (cpu_is_filtered(cpu))
		return 0;

	__u64 ts = bpf_ktime_get_ns();

	task = (struct task_struct *)bpf_get_current_task();
	if (!task)
		return 0;

	struct work_key key = {
		.type = KWORK_CLASS_SOFTIRQ,
		.pid = BPF_CORE_READ(task, pid),
		.task_p = (__u64)task,
	};

	pelem = bpf_map_lookup_elem(&kwork_top_irq_time, &key);
	if (pelem)
		delta = ts - pelem->timestamp;
	else
		delta = ts - from_timestamp;

	update_work(&key, delta);

	return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
