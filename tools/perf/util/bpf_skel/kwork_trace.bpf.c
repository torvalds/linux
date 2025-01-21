// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2022, Huawei

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define KWORK_COUNT 100
#define MAX_KWORKNAME 128

/*
 * This should be in sync with "util/kwork.h"
 */
enum kwork_class_type {
	KWORK_CLASS_IRQ,
	KWORK_CLASS_SOFTIRQ,
	KWORK_CLASS_WORKQUEUE,
	KWORK_CLASS_MAX,
};

struct work_key {
	__u32 type;
	__u32 cpu;
	__u64 id;
};

struct report_data {
	__u64 nr;
	__u64 total_time;
	__u64 max_time;
	__u64 max_time_start;
	__u64 max_time_end;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(struct work_key));
	__uint(value_size, MAX_KWORKNAME);
	__uint(max_entries, KWORK_COUNT);
} perf_kwork_names SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(struct work_key));
	__uint(value_size, sizeof(__u64));
	__uint(max_entries, KWORK_COUNT);
} perf_kwork_time SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(struct work_key));
	__uint(value_size, sizeof(struct report_data));
	__uint(max_entries, KWORK_COUNT);
} perf_kwork_report SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u8));
	__uint(max_entries, 1);
} perf_kwork_cpu_filter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, MAX_KWORKNAME);
	__uint(max_entries, 1);
} perf_kwork_name_filter SEC(".maps");

int enabled = 0;

const volatile int has_cpu_filter = 0;
const volatile int has_name_filter = 0;

static __always_inline int local_strncmp(const char *s1,
					 unsigned int sz, const char *s2)
{
	int ret = 0;
	unsigned int i;

	for (i = 0; i < sz; i++) {
		ret = (unsigned char)s1[i] - (unsigned char)s2[i];
		if (ret || !s1[i] || !s2[i])
			break;
	}

	return ret;
}

static __always_inline int trace_event_match(struct work_key *key, char *name)
{
	__u8 *cpu_val;
	char *name_val;
	__u32 zero = 0;
	__u32 cpu = bpf_get_smp_processor_id();

	if (!enabled)
		return 0;

	if (has_cpu_filter) {
		cpu_val = bpf_map_lookup_elem(&perf_kwork_cpu_filter, &cpu);
		if (!cpu_val)
			return 0;
	}

	if (has_name_filter && (name != NULL)) {
		name_val = bpf_map_lookup_elem(&perf_kwork_name_filter, &zero);
		if (name_val &&
		    (local_strncmp(name_val, MAX_KWORKNAME, name) != 0)) {
			return 0;
		}
	}

	return 1;
}

static __always_inline void do_update_time(void *map, struct work_key *key,
					   __u64 time_start, __u64 time_end)
{
	struct report_data zero, *data;
	__s64 delta = time_end - time_start;

	if (delta < 0)
		return;

	data = bpf_map_lookup_elem(map, key);
	if (!data) {
		__builtin_memset(&zero, 0, sizeof(zero));
		bpf_map_update_elem(map, key, &zero, BPF_NOEXIST);
		data = bpf_map_lookup_elem(map, key);
		if (!data)
			return;
	}

	if ((delta > data->max_time) ||
	    (data->max_time == 0)) {
		data->max_time       = delta;
		data->max_time_start = time_start;
		data->max_time_end   = time_end;
	}

	data->total_time += delta;
	data->nr++;
}

static __always_inline void do_update_timestart(void *map, struct work_key *key)
{
	__u64 ts = bpf_ktime_get_ns();

	bpf_map_update_elem(map, key, &ts, BPF_ANY);
}

static __always_inline void do_update_timeend(void *report_map, void *time_map,
					      struct work_key *key)
{
	__u64 *time = bpf_map_lookup_elem(time_map, key);

	if (time) {
		bpf_map_delete_elem(time_map, key);
		do_update_time(report_map, key, *time, bpf_ktime_get_ns());
	}
}

static __always_inline void do_update_name(void *map,
					   struct work_key *key, char *name)
{
	if (!bpf_map_lookup_elem(map, key))
		bpf_map_update_elem(map, key, name, BPF_ANY);
}

static __always_inline int update_timestart(void *map, struct work_key *key)
{
	if (!trace_event_match(key, NULL))
		return 0;

	do_update_timestart(map, key);
	return 0;
}

static __always_inline int update_timestart_and_name(void *time_map,
						     void *names_map,
						     struct work_key *key,
						     char *name)
{
	if (!trace_event_match(key, name))
		return 0;

	do_update_timestart(time_map, key);
	do_update_name(names_map, key, name);

	return 0;
}

static __always_inline int update_timeend(void *report_map,
					  void *time_map, struct work_key *key)
{
	if (!trace_event_match(key, NULL))
		return 0;

	do_update_timeend(report_map, time_map, key);

	return 0;
}

static __always_inline int update_timeend_and_name(void *report_map,
						   void *time_map,
						   void *names_map,
						   struct work_key *key,
						   char *name)
{
	if (!trace_event_match(key, name))
		return 0;

	do_update_timeend(report_map, time_map, key);
	do_update_name(names_map, key, name);

	return 0;
}

SEC("tracepoint/irq/irq_handler_entry")
int report_irq_handler_entry(struct trace_event_raw_irq_handler_entry *ctx)
{
	char name[MAX_KWORKNAME];
	struct work_key key = {
		.type = KWORK_CLASS_IRQ,
		.cpu  = bpf_get_smp_processor_id(),
		.id   = (__u64)ctx->irq,
	};
	void *name_addr = (void *)ctx + (ctx->__data_loc_name & 0xffff);

	bpf_probe_read_kernel_str(name, sizeof(name), name_addr);

	return update_timestart_and_name(&perf_kwork_time,
					 &perf_kwork_names, &key, name);
}

SEC("tracepoint/irq/irq_handler_exit")
int report_irq_handler_exit(struct trace_event_raw_irq_handler_exit *ctx)
{
	struct work_key key = {
		.type = KWORK_CLASS_IRQ,
		.cpu  = bpf_get_smp_processor_id(),
		.id   = (__u64)ctx->irq,
	};

	return update_timeend(&perf_kwork_report, &perf_kwork_time, &key);
}

static char softirq_name_list[NR_SOFTIRQS][MAX_KWORKNAME] = {
	{ "HI"       },
	{ "TIMER"    },
	{ "NET_TX"   },
	{ "NET_RX"   },
	{ "BLOCK"    },
	{ "IRQ_POLL" },
	{ "TASKLET"  },
	{ "SCHED"    },
	{ "HRTIMER"  },
	{ "RCU"      },
};

SEC("tracepoint/irq/softirq_entry")
int report_softirq_entry(struct trace_event_raw_softirq *ctx)
{
	unsigned int vec = ctx->vec;
	struct work_key key = {
		.type = KWORK_CLASS_SOFTIRQ,
		.cpu  = bpf_get_smp_processor_id(),
		.id   = (__u64)vec,
	};

	if (vec < NR_SOFTIRQS) {
		return update_timestart_and_name(&perf_kwork_time,
						 &perf_kwork_names, &key,
						 softirq_name_list[vec]);
	}

	return 0;
}

SEC("tracepoint/irq/softirq_exit")
int report_softirq_exit(struct trace_event_raw_softirq *ctx)
{
	struct work_key key = {
		.type = KWORK_CLASS_SOFTIRQ,
		.cpu  = bpf_get_smp_processor_id(),
		.id   = (__u64)ctx->vec,
	};

	return update_timeend(&perf_kwork_report, &perf_kwork_time, &key);
}

SEC("tracepoint/irq/softirq_raise")
int latency_softirq_raise(struct trace_event_raw_softirq *ctx)
{
	unsigned int vec = ctx->vec;
	struct work_key key = {
		.type = KWORK_CLASS_SOFTIRQ,
		.cpu  = bpf_get_smp_processor_id(),
		.id   = (__u64)vec,
	};

	if (vec < NR_SOFTIRQS) {
		return update_timestart_and_name(&perf_kwork_time,
						 &perf_kwork_names, &key,
						 softirq_name_list[vec]);
	}

	return 0;
}

SEC("tracepoint/irq/softirq_entry")
int latency_softirq_entry(struct trace_event_raw_softirq *ctx)
{
	struct work_key key = {
		.type = KWORK_CLASS_SOFTIRQ,
		.cpu  = bpf_get_smp_processor_id(),
		.id   = (__u64)ctx->vec,
	};

	return update_timeend(&perf_kwork_report, &perf_kwork_time, &key);
}

SEC("tracepoint/workqueue/workqueue_execute_start")
int report_workqueue_execute_start(struct trace_event_raw_workqueue_execute_start *ctx)
{
	struct work_key key = {
		.type = KWORK_CLASS_WORKQUEUE,
		.cpu  = bpf_get_smp_processor_id(),
		.id   = (__u64)ctx->work,
	};

	return update_timestart(&perf_kwork_time, &key);
}

SEC("tracepoint/workqueue/workqueue_execute_end")
int report_workqueue_execute_end(struct trace_event_raw_workqueue_execute_end *ctx)
{
	char name[MAX_KWORKNAME];
	struct work_key key = {
		.type = KWORK_CLASS_WORKQUEUE,
		.cpu  = bpf_get_smp_processor_id(),
		.id   = (__u64)ctx->work,
	};
	unsigned long long func_addr = (unsigned long long)ctx->function;

	__builtin_memset(name, 0, sizeof(name));
	bpf_snprintf(name, sizeof(name), "%ps", &func_addr, sizeof(func_addr));

	return update_timeend_and_name(&perf_kwork_report, &perf_kwork_time,
				       &perf_kwork_names, &key, name);
}

SEC("tracepoint/workqueue/workqueue_activate_work")
int latency_workqueue_activate_work(struct trace_event_raw_workqueue_activate_work *ctx)
{
	struct work_key key = {
		.type = KWORK_CLASS_WORKQUEUE,
		.cpu  = bpf_get_smp_processor_id(),
		.id   = (__u64)ctx->work,
	};

	return update_timestart(&perf_kwork_time, &key);
}

SEC("tracepoint/workqueue/workqueue_execute_start")
int latency_workqueue_execute_start(struct trace_event_raw_workqueue_execute_start *ctx)
{
	char name[MAX_KWORKNAME];
	struct work_key key = {
		.type = KWORK_CLASS_WORKQUEUE,
		.cpu  = bpf_get_smp_processor_id(),
		.id   = (__u64)ctx->work,
	};
	unsigned long long func_addr = (unsigned long long)ctx->function;

	__builtin_memset(name, 0, sizeof(name));
	bpf_snprintf(name, sizeof(name), "%ps", &func_addr, sizeof(func_addr));

	return update_timeend_and_name(&perf_kwork_report, &perf_kwork_time,
				       &perf_kwork_names, &key, name);
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
