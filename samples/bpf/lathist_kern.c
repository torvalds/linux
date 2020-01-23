/* Copyright (c) 2013-2015 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2015 BMW Car IT GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/version.h>
#include <linux/ptrace.h>
#include <uapi/linux/bpf.h>
#include <bpf/bpf_helpers.h>

#define MAX_ENTRIES	20
#define MAX_CPU		4

/* We need to stick to static allocated memory (an array instead of
 * hash table) because managing dynamic memory from the
 * trace_preempt_[on|off] tracepoints hooks is not supported.
 */

struct bpf_map_def SEC("maps") my_map = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(u64),
	.max_entries = MAX_CPU,
};

SEC("kprobe/trace_preempt_off")
int bpf_prog1(struct pt_regs *ctx)
{
	int cpu = bpf_get_smp_processor_id();
	u64 *ts = bpf_map_lookup_elem(&my_map, &cpu);

	if (ts)
		*ts = bpf_ktime_get_ns();

	return 0;
}

static unsigned int log2(unsigned int v)
{
	unsigned int r;
	unsigned int shift;

	r = (v > 0xFFFF) << 4; v >>= r;
	shift = (v > 0xFF) << 3; v >>= shift; r |= shift;
	shift = (v > 0xF) << 2; v >>= shift; r |= shift;
	shift = (v > 0x3) << 1; v >>= shift; r |= shift;
	r |= (v >> 1);

	return r;
}

static unsigned int log2l(unsigned long v)
{
	unsigned int hi = v >> 32;

	if (hi)
		return log2(hi) + 32;
	else
		return log2(v);
}

struct bpf_map_def SEC("maps") my_lat = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(long),
	.max_entries = MAX_CPU * MAX_ENTRIES,
};

SEC("kprobe/trace_preempt_on")
int bpf_prog2(struct pt_regs *ctx)
{
	u64 *ts, cur_ts, delta;
	int key, cpu;
	long *val;

	cpu = bpf_get_smp_processor_id();
	ts = bpf_map_lookup_elem(&my_map, &cpu);
	if (!ts)
		return 0;

	cur_ts = bpf_ktime_get_ns();
	delta = log2l(cur_ts - *ts);

	if (delta > MAX_ENTRIES - 1)
		delta = MAX_ENTRIES - 1;

	key = cpu * MAX_ENTRIES + delta;
	val = bpf_map_lookup_elem(&my_lat, &key);
	if (val)
		__sync_fetch_and_add((long *)val, 1);

	return 0;

}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
