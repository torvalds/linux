/* Copyright (c) 2016, Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/version.h>
#include <uapi/linux/bpf.h>
#include <uapi/linux/perf_event.h>
#include "bpf_helpers.h"
#include "bpf_tracing.h"

struct bpf_map_def SEC("maps") my_map = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(long),
	.value_size = sizeof(long),
	.max_entries = 1024,
};
struct bpf_map_def SEC("maps") my_map2 = {
	.type = BPF_MAP_TYPE_PERCPU_HASH,
	.key_size = sizeof(long),
	.value_size = sizeof(long),
	.max_entries = 1024,
};

struct bpf_map_def SEC("maps") stackmap = {
	.type = BPF_MAP_TYPE_STACK_TRACE,
	.key_size = sizeof(u32),
	.value_size = PERF_MAX_STACK_DEPTH * sizeof(u64),
	.max_entries = 10000,
};

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
SEC("kprobe/spin_unlock")PROG(p1)
SEC("kprobe/spin_lock")PROG(p2)
SEC("kprobe/mutex_spin_on_owner")PROG(p3)
SEC("kprobe/rwsem_spin_on_owner")PROG(p4)
SEC("kprobe/spin_unlock_irqrestore")PROG(p5)
SEC("kprobe/_raw_spin_unlock_irqrestore")PROG(p6)
SEC("kprobe/_raw_spin_unlock_bh")PROG(p7)
SEC("kprobe/_raw_spin_unlock")PROG(p8)
SEC("kprobe/_raw_spin_lock_irqsave")PROG(p9)
SEC("kprobe/_raw_spin_trylock_bh")PROG(p10)
SEC("kprobe/_raw_spin_lock_irq")PROG(p11)
SEC("kprobe/_raw_spin_trylock")PROG(p12)
SEC("kprobe/_raw_spin_lock")PROG(p13)
SEC("kprobe/_raw_spin_lock_bh")PROG(p14)
/* and to inner bpf helpers */
SEC("kprobe/htab_map_update_elem")PROG(p15)
SEC("kprobe/__htab_percpu_map_update_elem")PROG(p16)
SEC("kprobe/htab_map_alloc")PROG(p17)

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
