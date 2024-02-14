// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <linux/bpf.h>
#include <linux/version.h>
#include <bpf/bpf_helpers.h>

struct hmap_elem {
	volatile int cnt;
	struct bpf_spin_lock lock;
	int test_padding;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct hmap_elem);
} hmap SEC(".maps");

struct cls_elem {
	struct bpf_spin_lock lock;
	volatile int cnt;
};

struct {
	__uint(type, BPF_MAP_TYPE_CGROUP_STORAGE);
	__type(key, struct bpf_cgroup_storage_key);
	__type(value, struct cls_elem);
} cls_map SEC(".maps");

struct bpf_vqueue {
	struct bpf_spin_lock lock;
	/* 4 byte hole */
	unsigned long long lasttime;
	int credit;
	unsigned int rate;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct bpf_vqueue);
} vqueue SEC(".maps");

#define CREDIT_PER_NS(delta, rate) (((delta) * rate) >> 20)

SEC("tc")
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
