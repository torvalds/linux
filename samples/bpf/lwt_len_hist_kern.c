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

#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/in.h>
#include <bpf/bpf_helpers.h>

# define printk(fmt, ...)						\
		({							\
			char ____fmt[] = fmt;				\
			bpf_trace_printk(____fmt, sizeof(____fmt),	\
				     ##__VA_ARGS__);			\
		})

struct bpf_elf_map {
	__u32 type;
	__u32 size_key;
	__u32 size_value;
	__u32 max_elem;
	__u32 flags;
	__u32 id;
	__u32 pinning;
};

struct bpf_elf_map SEC("maps") lwt_len_hist_map = {
	.type = BPF_MAP_TYPE_PERCPU_HASH,
	.size_key = sizeof(__u64),
	.size_value = sizeof(__u64),
	.pinning = 2,
	.max_elem = 1024,
};

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
