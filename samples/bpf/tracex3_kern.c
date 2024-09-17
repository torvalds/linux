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
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct start_key {
	dev_t dev;
	u32 _pad;
	sector_t sector;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, long);
	__type(value, u64);
	__uint(max_entries, 4096);
} my_map SEC(".maps");

/* from /sys/kernel/tracing/events/block/block_io_start/format */
SEC("tracepoint/block/block_io_start")
int bpf_prog1(struct trace_event_raw_block_rq *ctx)
{
	u64 val = bpf_ktime_get_ns();
	struct start_key key = {
		.dev = ctx->dev,
		.sector = ctx->sector
	};

	bpf_map_update_elem(&my_map, &key, &val, BPF_ANY);
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

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(u32));
	__uint(value_size, sizeof(u64));
	__uint(max_entries, SLOTS);
} lat_map SEC(".maps");

/* from /sys/kernel/tracing/events/block/block_io_done/format */
SEC("tracepoint/block/block_io_done")
int bpf_prog2(struct trace_event_raw_block_rq *ctx)
{
	struct start_key key = {
		.dev = ctx->dev,
		.sector = ctx->sector
	};

	u64 *value, l, base;
	u32 index;

	value = bpf_map_lookup_elem(&my_map, &key);
	if (!value)
		return 0;

	u64 cur_time = bpf_ktime_get_ns();
	u64 delta = cur_time - *value;

	bpf_map_delete_elem(&my_map, &key);

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
		*value += 1;

	return 0;
}
char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
