// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <linux/bpf.h>
#include <time.h>
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include "bpf_tcp_helpers.h"

char _license[] SEC("license") = "GPL";
struct hmap_elem {
	int counter;
	struct bpf_timer timer;
	struct bpf_spin_lock lock; /* unused */
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1000);
	__type(key, int);
	__type(value, struct hmap_elem);
} hmap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__uint(max_entries, 1000);
	__type(key, int);
	__type(value, struct hmap_elem);
} hmap_malloc SEC(".maps");

struct elem {
	struct bpf_timer t;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 2);
	__type(key, int);
	__type(value, struct elem);
} array SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4);
	__type(key, int);
	__type(value, struct elem);
} lru SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct elem);
} abs_timer SEC(".maps"), soft_timer_pinned SEC(".maps"), abs_timer_pinned SEC(".maps"),
	race_array SEC(".maps");

__u64 bss_data;
__u64 abs_data;
__u64 err;
__u64 ok;
__u64 callback_check = 52;
__u64 callback2_check = 52;
__u64 pinned_callback_check;
__s32 pinned_cpu;

#define ARRAY 1
#define HTAB 2
#define HTAB_MALLOC 3
#define LRU 4

/* callback for array and lru timers */
static int timer_cb1(void *map, int *key, struct bpf_timer *timer)
{
	/* increment bss variable twice.
	 * Once via array timer callback and once via lru timer callback
	 */
	bss_data += 5;

	/* *key == 0 - the callback was called for array timer.
	 * *key == 4 - the callback was called from lru timer.
	 */
	if (*key == ARRAY) {
		struct bpf_timer *lru_timer;
		int lru_key = LRU;

		/* rearm array timer to be called again in ~35 seconds */
		if (bpf_timer_start(timer, 1ull << 35, 0) != 0)
			err |= 1;

		lru_timer = bpf_map_lookup_elem(&lru, &lru_key);
		if (!lru_timer)
			return 0;
		bpf_timer_set_callback(lru_timer, timer_cb1);
		if (bpf_timer_start(lru_timer, 0, 0) != 0)
			err |= 2;
	} else if (*key == LRU) {
		int lru_key, i;

		for (i = LRU + 1;
		     i <= 100  /* for current LRU eviction algorithm this number
				* should be larger than ~ lru->max_entries * 2
				*/;
		     i++) {
			struct elem init = {};

			/* lru_key cannot be used as loop induction variable
			 * otherwise the loop will be unbounded.
			 */
			lru_key = i;

			/* add more elements into lru map to push out current
			 * element and force deletion of this timer
			 */
			bpf_map_update_elem(map, &lru_key, &init, 0);
			/* look it up to bump it into active list */
			bpf_map_lookup_elem(map, &lru_key);

			/* keep adding until *key changes underneath,
			 * which means that key/timer memory was reused
			 */
			if (*key != LRU)
				break;
		}

		/* check that the timer was removed */
		if (bpf_timer_cancel(timer) != -EINVAL)
			err |= 4;
		ok |= 1;
	}
	return 0;
}

SEC("fentry/bpf_fentry_test1")
int BPF_PROG2(test1, int, a)
{
	struct bpf_timer *arr_timer, *lru_timer;
	struct elem init = {};
	int lru_key = LRU;
	int array_key = ARRAY;

	arr_timer = bpf_map_lookup_elem(&array, &array_key);
	if (!arr_timer)
		return 0;
	bpf_timer_init(arr_timer, &array, CLOCK_MONOTONIC);

	bpf_map_update_elem(&lru, &lru_key, &init, 0);
	lru_timer = bpf_map_lookup_elem(&lru, &lru_key);
	if (!lru_timer)
		return 0;
	bpf_timer_init(lru_timer, &lru, CLOCK_MONOTONIC);

	bpf_timer_set_callback(arr_timer, timer_cb1);
	bpf_timer_start(arr_timer, 0 /* call timer_cb1 asap */, 0);

	/* init more timers to check that array destruction
	 * doesn't leak timer memory.
	 */
	array_key = 0;
	arr_timer = bpf_map_lookup_elem(&array, &array_key);
	if (!arr_timer)
		return 0;
	bpf_timer_init(arr_timer, &array, CLOCK_MONOTONIC);
	return 0;
}

/* callback for prealloc and non-prealloca hashtab timers */
static int timer_cb2(void *map, int *key, struct hmap_elem *val)
{
	if (*key == HTAB)
		callback_check--;
	else
		callback2_check--;
	if (val->counter > 0 && --val->counter) {
		/* re-arm the timer again to execute after 1 usec */
		bpf_timer_start(&val->timer, 1000, 0);
	} else if (*key == HTAB) {
		struct bpf_timer *arr_timer;
		int array_key = ARRAY;

		/* cancel arr_timer otherwise bpf_fentry_test1 prog
		 * will stay alive forever.
		 */
		arr_timer = bpf_map_lookup_elem(&array, &array_key);
		if (!arr_timer)
			return 0;
		if (bpf_timer_cancel(arr_timer) != 1)
			/* bpf_timer_cancel should return 1 to indicate
			 * that arr_timer was active at this time
			 */
			err |= 8;

		/* try to cancel ourself. It shouldn't deadlock. */
		if (bpf_timer_cancel(&val->timer) != -EDEADLK)
			err |= 16;

		/* delete this key and this timer anyway.
		 * It shouldn't deadlock either.
		 */
		bpf_map_delete_elem(map, key);

		/* in preallocated hashmap both 'key' and 'val' could have been
		 * reused to store another map element (like in LRU above),
		 * but in controlled test environment the below test works.
		 * It's not a use-after-free. The memory is owned by the map.
		 */
		if (bpf_timer_start(&val->timer, 1000, 0) != -EINVAL)
			err |= 32;
		ok |= 2;
	} else {
		if (*key != HTAB_MALLOC)
			err |= 64;

		/* try to cancel ourself. It shouldn't deadlock. */
		if (bpf_timer_cancel(&val->timer) != -EDEADLK)
			err |= 128;

		/* delete this key and this timer anyway.
		 * It shouldn't deadlock either.
		 */
		bpf_map_delete_elem(map, key);

		ok |= 4;
	}
	return 0;
}

int bpf_timer_test(void)
{
	struct hmap_elem *val;
	int key = HTAB, key_malloc = HTAB_MALLOC;

	val = bpf_map_lookup_elem(&hmap, &key);
	if (val) {
		if (bpf_timer_init(&val->timer, &hmap, CLOCK_BOOTTIME) != 0)
			err |= 512;
		bpf_timer_set_callback(&val->timer, timer_cb2);
		bpf_timer_start(&val->timer, 1000, 0);
	}
	val = bpf_map_lookup_elem(&hmap_malloc, &key_malloc);
	if (val) {
		if (bpf_timer_init(&val->timer, &hmap_malloc, CLOCK_BOOTTIME) != 0)
			err |= 1024;
		bpf_timer_set_callback(&val->timer, timer_cb2);
		bpf_timer_start(&val->timer, 1000, 0);
	}
	return 0;
}

SEC("fentry/bpf_fentry_test2")
int BPF_PROG2(test2, int, a, int, b)
{
	struct hmap_elem init = {}, *val;
	int key = HTAB, key_malloc = HTAB_MALLOC;

	init.counter = 10; /* number of times to trigger timer_cb2 */
	bpf_map_update_elem(&hmap, &key, &init, 0);
	val = bpf_map_lookup_elem(&hmap, &key);
	if (val)
		bpf_timer_init(&val->timer, &hmap, CLOCK_BOOTTIME);
	/* update the same key to free the timer */
	bpf_map_update_elem(&hmap, &key, &init, 0);

	bpf_map_update_elem(&hmap_malloc, &key_malloc, &init, 0);
	val = bpf_map_lookup_elem(&hmap_malloc, &key_malloc);
	if (val)
		bpf_timer_init(&val->timer, &hmap_malloc, CLOCK_BOOTTIME);
	/* update the same key to free the timer */
	bpf_map_update_elem(&hmap_malloc, &key_malloc, &init, 0);

	/* init more timers to check that htab operations
	 * don't leak timer memory.
	 */
	key = 0;
	bpf_map_update_elem(&hmap, &key, &init, 0);
	val = bpf_map_lookup_elem(&hmap, &key);
	if (val)
		bpf_timer_init(&val->timer, &hmap, CLOCK_BOOTTIME);
	bpf_map_delete_elem(&hmap, &key);
	bpf_map_update_elem(&hmap, &key, &init, 0);
	val = bpf_map_lookup_elem(&hmap, &key);
	if (val)
		bpf_timer_init(&val->timer, &hmap, CLOCK_BOOTTIME);

	/* and with non-prealloc htab */
	key_malloc = 0;
	bpf_map_update_elem(&hmap_malloc, &key_malloc, &init, 0);
	val = bpf_map_lookup_elem(&hmap_malloc, &key_malloc);
	if (val)
		bpf_timer_init(&val->timer, &hmap_malloc, CLOCK_BOOTTIME);
	bpf_map_delete_elem(&hmap_malloc, &key_malloc);
	bpf_map_update_elem(&hmap_malloc, &key_malloc, &init, 0);
	val = bpf_map_lookup_elem(&hmap_malloc, &key_malloc);
	if (val)
		bpf_timer_init(&val->timer, &hmap_malloc, CLOCK_BOOTTIME);

	return bpf_timer_test();
}

/* callback for absolute timer */
static int timer_cb3(void *map, int *key, struct bpf_timer *timer)
{
	abs_data += 6;

	if (abs_data < 12) {
		bpf_timer_start(timer, bpf_ktime_get_boot_ns() + 1000,
				BPF_F_TIMER_ABS);
	} else {
		/* Re-arm timer ~35 seconds in future */
		bpf_timer_start(timer, bpf_ktime_get_boot_ns() + (1ull << 35),
				BPF_F_TIMER_ABS);
	}

	return 0;
}

SEC("fentry/bpf_fentry_test3")
int BPF_PROG2(test3, int, a)
{
	int key = 0;
	struct bpf_timer *timer;

	bpf_printk("test3");

	timer = bpf_map_lookup_elem(&abs_timer, &key);
	if (timer) {
		if (bpf_timer_init(timer, &abs_timer, CLOCK_BOOTTIME) != 0)
			err |= 2048;
		bpf_timer_set_callback(timer, timer_cb3);
		bpf_timer_start(timer, bpf_ktime_get_boot_ns() + 1000,
				BPF_F_TIMER_ABS);
	}

	return 0;
}

/* callback for pinned timer */
static int timer_cb_pinned(void *map, int *key, struct bpf_timer *timer)
{
	__s32 cpu = bpf_get_smp_processor_id();

	if (cpu != pinned_cpu)
		err |= 16384;

	pinned_callback_check++;
	return 0;
}

static void test_pinned_timer(bool soft)
{
	int key = 0;
	void *map;
	struct bpf_timer *timer;
	__u64 flags = BPF_F_TIMER_CPU_PIN;
	__u64 start_time;

	if (soft) {
		map = &soft_timer_pinned;
		start_time = 0;
	} else {
		map = &abs_timer_pinned;
		start_time = bpf_ktime_get_boot_ns();
		flags |= BPF_F_TIMER_ABS;
	}

	timer = bpf_map_lookup_elem(map, &key);
	if (timer) {
		if (bpf_timer_init(timer, map, CLOCK_BOOTTIME) != 0)
			err |= 4096;
		bpf_timer_set_callback(timer, timer_cb_pinned);
		pinned_cpu = bpf_get_smp_processor_id();
		bpf_timer_start(timer, start_time + 1000, flags);
	} else {
		err |= 8192;
	}
}

SEC("fentry/bpf_fentry_test4")
int BPF_PROG2(test4, int, a)
{
	bpf_printk("test4");
	test_pinned_timer(true);

	return 0;
}

SEC("fentry/bpf_fentry_test5")
int BPF_PROG2(test5, int, a)
{
	bpf_printk("test5");
	test_pinned_timer(false);

	return 0;
}

static int race_timer_callback(void *race_array, int *race_key, struct bpf_timer *timer)
{
	bpf_timer_start(timer, 1000000, 0);
	return 0;
}

SEC("syscall")
int race(void *ctx)
{
	struct bpf_timer *timer;
	int err, race_key = 0;
	struct elem init;

	__builtin_memset(&init, 0, sizeof(struct elem));
	bpf_map_update_elem(&race_array, &race_key, &init, BPF_ANY);

	timer = bpf_map_lookup_elem(&race_array, &race_key);
	if (!timer)
		return 1;

	err = bpf_timer_init(timer, &race_array, CLOCK_MONOTONIC);
	if (err && err != -EBUSY)
		return 1;

	bpf_timer_set_callback(timer, race_timer_callback);
	bpf_timer_start(timer, 0, 0);
	bpf_timer_cancel(timer);

	return 0;
}
