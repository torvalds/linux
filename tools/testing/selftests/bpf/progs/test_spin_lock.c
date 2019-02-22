// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <linux/bpf.h>
#include <linux/version.h>
#include "bpf_helpers.h"

struct hmap_elem {
	volatile int cnt;
	struct bpf_spin_lock lock;
	int test_padding;
};

struct bpf_map_def SEC("maps") hmap = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(int),
	.value_size = sizeof(struct hmap_elem),
	.max_entries = 1,
};

BPF_ANNOTATE_KV_PAIR(hmap, int, struct hmap_elem);


struct cls_elem {
	struct bpf_spin_lock lock;
	volatile int cnt;
};

struct bpf_map_def SEC("maps") cls_map = {
	.type = BPF_MAP_TYPE_CGROUP_STORAGE,
	.key_size = sizeof(struct bpf_cgroup_storage_key),
	.value_size = sizeof(struct cls_elem),
};

BPF_ANNOTATE_KV_PAIR(cls_map, struct bpf_cgroup_storage_key,
		     struct cls_elem);

struct bpf_vqueue {
	struct bpf_spin_lock lock;
	/* 4 byte hole */
	unsigned long long lasttime;
	int credit;
	unsigned int rate;
};

struct bpf_map_def SEC("maps") vqueue = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(struct bpf_vqueue),
	.max_entries = 1,
};

BPF_ANNOTATE_KV_PAIR(vqueue, int, struct bpf_vqueue);
#define CREDIT_PER_NS(delta, rate) (((delta) * rate) >> 20)

SEC("spin_lock_demo")
int bpf_sping_lock_test(struct __sk_buff *skb)
{
	volatile int credit = 0, max_credit = 100, pkt_len = 64;
	struct hmap_elem zero = {}, *val;
	unsigned long long curtime;
	struct bpf_vqueue *q;
	struct cls_elem *cls;
	int key = 0;
	int err = 0;

	val = bpf_map_lookup_elem(&hmap, &key);
	if (!val) {
		bpf_map_update_elem(&hmap, &key, &zero, 0);
		val = bpf_map_lookup_elem(&hmap, &key);
		if (!val) {
			err = 1;
			goto err;
		}
	}
	/* spin_lock in hash map run time test */
	bpf_spin_lock(&val->lock);
	if (val->cnt)
		val->cnt--;
	else
		val->cnt++;
	if (val->cnt != 0 && val->cnt != 1)
		err = 1;
	bpf_spin_unlock(&val->lock);

	/* spin_lock in array. virtual queue demo */
	q = bpf_map_lookup_elem(&vqueue, &key);
	if (!q)
		goto err;
	curtime = bpf_ktime_get_ns();
	bpf_spin_lock(&q->lock);
	q->credit += CREDIT_PER_NS(curtime - q->lasttime, q->rate);
	q->lasttime = curtime;
	if (q->credit > max_credit)
		q->credit = max_credit;
	q->credit -= pkt_len;
	credit = q->credit;
	bpf_spin_unlock(&q->lock);

	/* spin_lock in cgroup local storage */
	cls = bpf_get_local_storage(&cls_map, 0);
	bpf_spin_lock(&cls->lock);
	cls->cnt++;
	bpf_spin_unlock(&cls->lock);

err:
	return err;
}
char _license[] SEC("license") = "GPL";
