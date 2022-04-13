// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <errno.h>
#include <linux/bpf.h>
#include <stdbool.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

struct bpf_map;

__u8 rand_vals[2500000];
const __u32 nr_rand_bytes = 2500000;

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(key_size, sizeof(__u32));
	/* max entries and value_size will be set programmatically.
	 * They are configurable from the userspace bench program.
	 */
} array_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_BLOOM_FILTER);
	/* max entries,  value_size, and # of hash functions will be set
	 * programmatically. They are configurable from the userspace
	 * bench program.
	 */
	__uint(map_extra, 3);
} bloom_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	/* max entries, key_size, and value_size, will be set
	 * programmatically. They are configurable from the userspace
	 * bench program.
	 */
} hashmap SEC(".maps");

struct callback_ctx {
	struct bpf_map *map;
	bool update;
};

/* Tracks the number of hits, drops, and false hits */
struct {
	__u32 stats[3];
} __attribute__((__aligned__(256))) percpu_stats[256];

const __u32 hit_key  = 0;
const __u32 drop_key  = 1;
const __u32 false_hit_key = 2;

__u8 value_size;

const volatile bool hashmap_use_bloom;
const volatile bool count_false_hits;

int error = 0;

static __always_inline void log_result(__u32 key)
{
	__u32 cpu = bpf_get_smp_processor_id();

	percpu_stats[cpu & 255].stats[key]++;
}

static __u64
bloom_callback(struct bpf_map *map, __u32 *key, void *val,
	       struct callback_ctx *data)
{
	int err;

	if (data->update)
		err = bpf_map_push_elem(data->map, val, 0);
	else
		err = bpf_map_peek_elem(data->map, val);

	if (err) {
		error |= 1;
		return 1; /* stop the iteration */
	}

	log_result(hit_key);

	return 0;
}

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int bloom_lookup(void *ctx)
{
	struct callback_ctx data;

	data.map = (struct bpf_map *)&bloom_map;
	data.update = false;

	bpf_for_each_map_elem(&array_map, bloom_callback, &data, 0);

	return 0;
}

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int bloom_update(void *ctx)
{
	struct callback_ctx data;

	data.map = (struct bpf_map *)&bloom_map;
	data.update = true;

	bpf_for_each_map_elem(&array_map, bloom_callback, &data, 0);

	return 0;
}

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int bloom_hashmap_lookup(void *ctx)
{
	__u64 *result;
	int i, err;

	__u32 index = bpf_get_prandom_u32();
	__u32 bitmask = (1ULL << 21) - 1;

	for (i = 0; i < 1024; i++, index += value_size) {
		index = index & bitmask;

		if (hashmap_use_bloom) {
			err = bpf_map_peek_elem(&bloom_map,
						rand_vals + index);
			if (err) {
				if (err != -ENOENT) {
					error |= 2;
					return 0;
				}
				log_result(hit_key);
				continue;
			}
		}

		result = bpf_map_lookup_elem(&hashmap,
					     rand_vals + index);
		if (result) {
			log_result(hit_key);
		} else {
			if (hashmap_use_bloom && count_false_hits)
				log_result(false_hit_key);
			log_result(drop_key);
		}
	}

	return 0;
}
