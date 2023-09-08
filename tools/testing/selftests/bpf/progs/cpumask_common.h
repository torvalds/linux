/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#ifndef _CPUMASK_COMMON_H
#define _CPUMASK_COMMON_H

#include "errno.h"
#include <stdbool.h>

int err;

#define private(name) SEC(".bss." #name) __hidden __attribute__((aligned(8)))
private(MASK) static struct bpf_cpumask __kptr * global_mask;

struct __cpumask_map_value {
	struct bpf_cpumask __kptr * cpumask;
};

struct array_map {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct __cpumask_map_value);
	__uint(max_entries, 1);
} __cpumask_map SEC(".maps");

struct bpf_cpumask *bpf_cpumask_create(void) __ksym;
void bpf_cpumask_release(struct bpf_cpumask *cpumask) __ksym;
struct bpf_cpumask *bpf_cpumask_acquire(struct bpf_cpumask *cpumask) __ksym;
u32 bpf_cpumask_first(const struct cpumask *cpumask) __ksym;
u32 bpf_cpumask_first_zero(const struct cpumask *cpumask) __ksym;
void bpf_cpumask_set_cpu(u32 cpu, struct bpf_cpumask *cpumask) __ksym;
void bpf_cpumask_clear_cpu(u32 cpu, struct bpf_cpumask *cpumask) __ksym;
bool bpf_cpumask_test_cpu(u32 cpu, const struct cpumask *cpumask) __ksym;
bool bpf_cpumask_test_and_set_cpu(u32 cpu, struct bpf_cpumask *cpumask) __ksym;
bool bpf_cpumask_test_and_clear_cpu(u32 cpu, struct bpf_cpumask *cpumask) __ksym;
void bpf_cpumask_setall(struct bpf_cpumask *cpumask) __ksym;
void bpf_cpumask_clear(struct bpf_cpumask *cpumask) __ksym;
bool bpf_cpumask_and(struct bpf_cpumask *cpumask,
		     const struct cpumask *src1,
		     const struct cpumask *src2) __ksym;
void bpf_cpumask_or(struct bpf_cpumask *cpumask,
		    const struct cpumask *src1,
		    const struct cpumask *src2) __ksym;
void bpf_cpumask_xor(struct bpf_cpumask *cpumask,
		     const struct cpumask *src1,
		     const struct cpumask *src2) __ksym;
bool bpf_cpumask_equal(const struct cpumask *src1, const struct cpumask *src2) __ksym;
bool bpf_cpumask_intersects(const struct cpumask *src1, const struct cpumask *src2) __ksym;
bool bpf_cpumask_subset(const struct cpumask *src1, const struct cpumask *src2) __ksym;
bool bpf_cpumask_empty(const struct cpumask *cpumask) __ksym;
bool bpf_cpumask_full(const struct cpumask *cpumask) __ksym;
void bpf_cpumask_copy(struct bpf_cpumask *dst, const struct cpumask *src) __ksym;
u32 bpf_cpumask_any(const struct cpumask *src) __ksym;
u32 bpf_cpumask_any_and(const struct cpumask *src1, const struct cpumask *src2) __ksym;

void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;

static inline const struct cpumask *cast(struct bpf_cpumask *cpumask)
{
	return (const struct cpumask *)cpumask;
}

static inline struct bpf_cpumask *create_cpumask(void)
{
	struct bpf_cpumask *cpumask;

	cpumask = bpf_cpumask_create();
	if (!cpumask) {
		err = 1;
		return NULL;
	}

	if (!bpf_cpumask_empty(cast(cpumask))) {
		err = 2;
		bpf_cpumask_release(cpumask);
		return NULL;
	}

	return cpumask;
}

static inline struct __cpumask_map_value *cpumask_map_value_lookup(void)
{
	u32 key = 0;

	return bpf_map_lookup_elem(&__cpumask_map, &key);
}

static inline int cpumask_map_insert(struct bpf_cpumask *mask)
{
	struct __cpumask_map_value local, *v;
	long status;
	struct bpf_cpumask *old;
	u32 key = 0;

	local.cpumask = NULL;
	status = bpf_map_update_elem(&__cpumask_map, &key, &local, 0);
	if (status) {
		bpf_cpumask_release(mask);
		return status;
	}

	v = bpf_map_lookup_elem(&__cpumask_map, &key);
	if (!v) {
		bpf_cpumask_release(mask);
		return -ENOENT;
	}

	old = bpf_kptr_xchg(&v->cpumask, mask);
	if (old) {
		bpf_cpumask_release(old);
		return -EEXIST;
	}

	return 0;
}

#endif /* _CPUMASK_COMMON_H */
