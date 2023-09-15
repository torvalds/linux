/* Copyright (c) 2016, Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include "vmlinux.h"
#include <linux/version.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#ifndef PERF_MAX_STACK_DEPTH
#define PERF_MAX_STACK_DEPTH         127
#endif

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, long);
	__type(value, long);
	__uint(max_entries, 1024);
} my_map SEC(".maps");
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(key_size, sizeof(long));
	__uint(value_size, sizeof(long));
	__uint(max_entries, 1024);
} my_map2 SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_STACK_TRACE);
	__uint(key_size, sizeof(u32));
	__uint(value_size, PERF_MAX_STACK_DEPTH * sizeof(u64));
	__uint(max_entries, 10000);
} stackmap SEC(".maps");

#define PROG(foo) \
int foo(struct pt_regs *ctx) \
{ \
	long v = PT_REGS_IP(ctx), *val; \
\
	val = bpf_map_lookup_elem(&my_map, &v); \
	bpf_map_update_elem(&my_map, &v, &v, BPF_ANY); \
	bpf_map_update_elem(&my_map2, &v, &v, BPF_ANY); \
	bpf_map_delete_elem(&my_map2, &v); \
	bpf_get_stackid(ctx, &stackmap, BPF_F_REUSE_STACKID); \
	return 0; \
}

/* add kprobes to all possible *spin* functions */
SEC("kprobe.multi/spin_*lock*")PROG(spin_lock)
SEC("kprobe.multi/*_spin_on_owner")PROG(spin_on_owner)
SEC("kprobe.multi/_raw_spin_*lock*")PROG(raw_spin_lock)

/* and to inner bpf helpers */
SEC("kprobe/htab_map_update_elem")PROG(p15)
SEC("kprobe/__htab_percpu_map_update_elem")PROG(p16)
SEC("kprobe/htab_map_alloc")PROG(p17)

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
