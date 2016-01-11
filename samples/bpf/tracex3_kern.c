/* Copyright (c) 2013-2015 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/version.h>
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"

struct bpf_map_def SEC("maps") my_map = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(long),
	.value_size = sizeof(u64),
	.max_entries = 4096,
};

/* kprobe is NOT a stable ABI. If kernel internals change this bpf+kprobe
 * example will no longer be meaningful
 */
SEC("kprobe/blk_mq_start_request")
int bpf_prog1(struct pt_regs *ctx)
{
	long rq = PT_REGS_PARM1(ctx);
	u64 val = bpf_ktime_get_ns();

	bpf_map_update_elem(&my_map, &rq, &val, BPF_ANY);
	return 0;
}

static unsigned int log2l(unsigned long long n)
{
#define S(k) if (n >= (1ull << k)) { i += k; n >>= k; }
	int i = -(n == 0);
	S(32); S(16); S(8); S(4); S(2); S(1);
	return i;
#undef S
}

#define SLOTS 100

struct bpf_map_def SEC("maps") lat_map = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(u64),
	.max_entries = SLOTS,
};

SEC("kprobe/blk_update_request")
int bpf_prog2(struct pt_regs *ctx)
{
	long rq = PT_REGS_PARM1(ctx);
	u64 *value, l, base;
	u32 index;

	value = bpf_map_lookup_elem(&my_map, &rq);
	if (!value)
		return 0;

	u64 cur_time = bpf_ktime_get_ns();
	u64 delta = cur_time - *value;

	bpf_map_delete_elem(&my_map, &rq);

	/* the lines below are computing index = log10(delta)*10
	 * using integer arithmetic
	 * index = 29 ~ 1 usec
	 * index = 59 ~ 1 msec
	 * index = 89 ~ 1 sec
	 * index = 99 ~ 10sec or more
	 * log10(x)*10 = log2(x)*10/log2(10) = log2(x)*3
	 */
	l = log2l(delta);
	base = 1ll << l;
	index = (l * 64 + (delta - base) * 64 / base) * 3 / 64;

	if (index >= SLOTS)
		index = SLOTS - 1;

	value = bpf_map_lookup_elem(&lat_map, &index);
	if (value)
		__sync_fetch_and_add((long *)value, 1);

	return 0;
}
char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
