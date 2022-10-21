// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

#define TEST_STACK_DEPTH	2
#define TEST_MAX_ENTRIES	16384

typedef __u64 stack_trace_t[TEST_STACK_DEPTH];

struct {
	__uint(type, BPF_MAP_TYPE_STACK_TRACE);
	__uint(max_entries, TEST_MAX_ENTRIES);
	__type(key, __u32);
	__type(value, stack_trace_t);
} stackmap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, TEST_MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} stackid_hmap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, TEST_MAX_ENTRIES);
	__type(key, __u32);
	__type(value, stack_trace_t);
} stack_amap SEC(".maps");

int pid = 0;
int control = 0;
int failed = 0;

SEC("tracepoint/sched/sched_switch")
int oncpu(struct trace_event_raw_sched_switch *ctx)
{
	__u32 max_len = TEST_STACK_DEPTH * sizeof(__u64);
	__u32 key = 0, val = 0;
	__u64 *stack_p;

	if (pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;

	if (control)
		return 0;

	/* it should allow skipping whole buffer size entries */
	key = bpf_get_stackid(ctx, &stackmap, TEST_STACK_DEPTH);
	if ((int)key >= 0) {
		/* The size of stackmap and stack_amap should be the same */
		bpf_map_update_elem(&stackid_hmap, &key, &val, 0);
		stack_p = bpf_map_lookup_elem(&stack_amap, &key);
		if (stack_p) {
			bpf_get_stack(ctx, stack_p, max_len, TEST_STACK_DEPTH);
			/* it wrongly skipped all the entries and filled zero */
			if (stack_p[0] == 0)
				failed = 1;
		}
	} else {
		/* old kernel doesn't support skipping that many entries */
		failed = 2;
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
