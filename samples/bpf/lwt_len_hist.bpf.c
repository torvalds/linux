/* Copyright (c) 2016 Thomas Graf <tgraf@tgraf.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__type(key, u64);
	__type(value, u64);
	__uint(pinning, LIBBPF_PIN_BY_NAME);
	__uint(max_entries, 1024);
} lwt_len_hist_map SEC(".maps");

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

SEC("len_hist")
int do_len_hist(struct __sk_buff *skb)
{
	__u64 *value, key, init_val = 1;

	key = log2l(skb->len);

	value = bpf_map_lookup_elem(&lwt_len_hist_map, &key);
	if (value)
		__sync_fetch_and_add(value, 1);
	else
		bpf_map_update_elem(&lwt_len_hist_map, &key, &init_val, BPF_ANY);

	return BPF_OK;
}

char _license[] SEC("license") = "GPL";
