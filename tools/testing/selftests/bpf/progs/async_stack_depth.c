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

/*
 * The same shapes scaled to the 2 KiB budget of JITs with large stacks. The
 * compiler caps a single function at 512 bytes, so the depth comes from a
 * chain of 480-byte frames.
 */
__attribute__((noinline))
static int timer_cb_large_0(void *map, int *key, struct bpf_timer *timer)
{
	volatile char buf[480] = {};
	return buf[69];
}

__attribute__((noinline))
static int timer_cb_large_1(void *map, int *key, struct bpf_timer *timer)
{
	volatile char buf[480] = {};
	return buf[69] + timer_cb_large_0(map, key, timer);
}

__attribute__((noinline))
static int timer_cb_large_2(void *map, int *key, struct bpf_timer *timer)
{
	volatile char buf[480] = {};
	return buf[69] + timer_cb_large_1(map, key, timer);
}

__attribute__((noinline))
static int timer_cb_large_3(void *map, int *key, struct bpf_timer *timer)
{
	volatile char buf[480] = {};
	return buf[69] + timer_cb_large_2(map, key, timer);
}

/* 5 * 480 = 2400 bytes on its own */
__attribute__((noinline))
static int bad_timer_cb_large(void *map, int *key, struct bpf_timer *timer)
{
	volatile char buf[480] = {};
	return buf[255] + timer_cb_large_3(map, key, timer);
}

SEC("tc")
__load_if_no_large_stack()
__failure __msg("combined stack size of 2 calls is")
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

/* main plus the four frames under timer_cb_large_3: 2400 bytes */
SEC("tc")
__load_if_large_stack()
__failure __msg("combined stack size of 5 calls is")
int pseudo_call_check_large(struct __sk_buff *ctx)
{
	struct hmap_elem *elem;
	volatile char buf[480] = {};

	elem = bpf_map_lookup_elem(&hmap, &(int){0});
	if (!elem)
		return 0;

	timer_cb_large_3(NULL, NULL, NULL);
	return bpf_timer_set_callback(&elem->timer, timer_cb_large_3) + buf[0];
}

SEC("tc")
__load_if_no_large_stack()
__failure __msg("combined stack size of 2 calls is")
int async_call_root_check(struct __sk_buff *ctx)
{
	struct hmap_elem *elem;
	volatile char buf[256] = {};

	elem = bpf_map_lookup_elem(&hmap, &(int){0});
	if (!elem)
		return 0;

	return bpf_timer_set_callback(&elem->timer, bad_timer_cb) + buf[0];
}

SEC("tc")
__load_if_large_stack()
__failure __msg("combined stack size of 5 calls is")
int async_call_root_check_large(struct __sk_buff *ctx)
{
	struct hmap_elem *elem;
	volatile char buf[480] = {};

	elem = bpf_map_lookup_elem(&hmap, &(int){0});
	if (!elem)
		return 0;

	return bpf_timer_set_callback(&elem->timer, bad_timer_cb_large) + buf[0];
}

char _license[] SEC("license") = "GPL";
