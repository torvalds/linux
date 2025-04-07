// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024-2025 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#define EDEADLK 35
#define ETIMEDOUT 110

struct arr_elem {
	struct bpf_res_spin_lock lock;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 64);
	__type(key, int);
	__type(value, struct arr_elem);
} arrmap SEC(".maps");

struct bpf_res_spin_lock lockA __hidden SEC(".data.A");
struct bpf_res_spin_lock lockB __hidden SEC(".data.B");

SEC("tc")
int res_spin_lock_test(struct __sk_buff *ctx)
{
	struct arr_elem *elem1, *elem2;
	int r;

	elem1 = bpf_map_lookup_elem(&arrmap, &(int){0});
	if (!elem1)
		return -1;
	elem2 = bpf_map_lookup_elem(&arrmap, &(int){0});
	if (!elem2)
		return -1;

	r = bpf_res_spin_lock(&elem1->lock);
	if (r)
		return r;
	if (!bpf_res_spin_lock(&elem2->lock)) {
		bpf_res_spin_unlock(&elem2->lock);
		bpf_res_spin_unlock(&elem1->lock);
		return -1;
	}
	bpf_res_spin_unlock(&elem1->lock);
	return 0;
}

SEC("tc")
int res_spin_lock_test_AB(struct __sk_buff *ctx)
{
	int r;

	r = bpf_res_spin_lock(&lockA);
	if (r)
		return !r;
	/* Only unlock if we took the lock. */
	if (!bpf_res_spin_lock(&lockB))
		bpf_res_spin_unlock(&lockB);
	bpf_res_spin_unlock(&lockA);
	return 0;
}

int err;

SEC("tc")
int res_spin_lock_test_BA(struct __sk_buff *ctx)
{
	int r;

	r = bpf_res_spin_lock(&lockB);
	if (r)
		return !r;
	if (!bpf_res_spin_lock(&lockA))
		bpf_res_spin_unlock(&lockA);
	else
		err = -EDEADLK;
	bpf_res_spin_unlock(&lockB);
	return err ?: 0;
}

SEC("tc")
int res_spin_lock_test_held_lock_max(struct __sk_buff *ctx)
{
	struct bpf_res_spin_lock *locks[48] = {};
	struct arr_elem *e;
	u64 time_beg, time;
	int ret = 0, i;

	_Static_assert(ARRAY_SIZE(((struct rqspinlock_held){}).locks) == 31,
		       "RES_NR_HELD assumed to be 31");

	for (i = 0; i < 34; i++) {
		int key = i;

		/* We cannot pass in i as it will get spilled/filled by the compiler and
		 * loses bounds in verifier state.
		 */
		e = bpf_map_lookup_elem(&arrmap, &key);
		if (!e)
			return 1;
		locks[i] = &e->lock;
	}

	for (; i < 48; i++) {
		int key = i - 2;

		/* We cannot pass in i as it will get spilled/filled by the compiler and
		 * loses bounds in verifier state.
		 */
		e = bpf_map_lookup_elem(&arrmap, &key);
		if (!e)
			return 1;
		locks[i] = &e->lock;
	}

	time_beg = bpf_ktime_get_ns();
	for (i = 0; i < 34; i++) {
		if (bpf_res_spin_lock(locks[i]))
			goto end;
	}

	/* Trigger AA, after exhausting entries in the held lock table. This
	 * time, only the timeout can save us, as AA detection won't succeed.
	 */
	if (!bpf_res_spin_lock(locks[34])) {
		bpf_res_spin_unlock(locks[34]);
		ret = 1;
		goto end;
	}

end:
	for (i = i - 1; i >= 0; i--)
		bpf_res_spin_unlock(locks[i]);
	time = bpf_ktime_get_ns() - time_beg;
	/* Time spent should be easily above our limit (1/4 s), since AA
	 * detection won't be expedited due to lack of held lock entry.
	 */
	return ret ?: (time > 1000000000 / 4 ? 0 : 1);
}

char _license[] SEC("license") = "GPL";
