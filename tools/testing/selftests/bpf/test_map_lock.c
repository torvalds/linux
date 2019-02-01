// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <linux/bpf.h>
#include <linux/version.h>
#include "bpf_helpers.h"

#define VAR_NUM 16

struct hmap_elem {
	struct bpf_spin_lock lock;
	int var[VAR_NUM];
};

struct bpf_map_def SEC("maps") hash_map = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(int),
	.value_size = sizeof(struct hmap_elem),
	.max_entries = 1,
};

BPF_ANNOTATE_KV_PAIR(hash_map, int, struct hmap_elem);

struct array_elem {
	struct bpf_spin_lock lock;
	int var[VAR_NUM];
};

struct bpf_map_def SEC("maps") array_map = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(struct array_elem),
	.max_entries = 1,
};

BPF_ANNOTATE_KV_PAIR(array_map, int, struct array_elem);

SEC("map_lock_demo")
int bpf_map_lock_test(struct __sk_buff *skb)
{
	struct hmap_elem zero = {}, *val;
	int rnd = bpf_get_prandom_u32();
	int key = 0, err = 1, i;
	struct array_elem *q;

	val = bpf_map_lookup_elem(&hash_map, &key);
	if (!val)
		goto err;
	/* spin_lock in hash map */
	bpf_spin_lock(&val->lock);
	for (i = 0; i < VAR_NUM; i++)
		val->var[i] = rnd;
	bpf_spin_unlock(&val->lock);

	/* spin_lock in array */
	q = bpf_map_lookup_elem(&array_map, &key);
	if (!q)
		goto err;
	bpf_spin_lock(&q->lock);
	for (i = 0; i < VAR_NUM; i++)
		q->var[i] = rnd;
	bpf_spin_unlock(&q->lock);
	err = 0;
err:
	return err;
}
char _license[] SEC("license") = "GPL";
