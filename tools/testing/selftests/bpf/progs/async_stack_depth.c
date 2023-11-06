// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

#include "bpf_misc.h"

struct hmap_elem {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 64);
	__type(key, int);
	__type(value, struct hmap_elem);
} hmap SEC(".maps");

__attribute__((noinline))
static int timer_cb(void *map, int *key, struct bpf_timer *timer)
{
	volatile char buf[256] = {};
	return buf[69];
}

__attribute__((noinline))
static int bad_timer_cb(void *map, int *key, struct bpf_timer *timer)
{
	volatile char buf[300] = {};
	return buf[255] + timer_cb(NULL, NULL, NULL);
}

SEC("tc")
__failure __msg("combined stack size of 2 calls is 576. Too large")
int pseudo_call_check(struct __sk_buff *ctx)
{
	struct hmap_elem *elem;
	volatile char buf[256] = {};

	elem = bpf_map_lookup_elem(&hmap, &(int){0});
	if (!elem)
		return 0;

	timer_cb(NULL, NULL, NULL);
	return bpf_timer_set_callback(&elem->timer, timer_cb) + buf[0];
}

SEC("tc")
__failure __msg("combined stack size of 2 calls is 608. Too large")
int async_call_root_check(struct __sk_buff *ctx)
{
	struct hmap_elem *elem;
	volatile char buf[256] = {};

	elem = bpf_map_lookup_elem(&hmap, &(int){0});
	if (!elem)
		return 0;

	return bpf_timer_set_callback(&elem->timer, bad_timer_cb) + buf[0];
}

char _license[] SEC("license") = "GPL";
