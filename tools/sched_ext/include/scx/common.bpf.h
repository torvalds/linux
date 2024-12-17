/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#ifndef __SCX_COMMON_BPF_H
#define __SCX_COMMON_BPF_H

#ifdef LSP
#define __bpf__
#include "../vmlinux/vmlinux.h"
#else
#include "vmlinux.h"
#endif

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <asm-generic/errno.h>
#include "user_exit_info.h"

#define PF_WQ_WORKER			0x00000020	/* I'm a workqueue worker */
#define PF_KTHREAD			0x00200000	/* I am a kernel thread */
#define PF_EXITING			0x00000004
#define CLOCK_MONOTONIC			1

/*
 * Earlier versions of clang/pahole lost upper 32bits in 64bit enums which can
 * lead to really confusing misbehaviors. Let's trigger a build failure.
 */
static inline void ___vmlinux_h_sanity_check___(void)
{
	_Static_assert(SCX_DSQ_FLAG_BUILTIN,
		       "bpftool generated vmlinux.h is missing high bits for 64bit enums, upgrade clang and pahole");
}

s32 scx_bpf_create_dsq(u64 dsq_id, s32 node) __ksym;
s32 scx_bpf_select_cpu_dfl(struct task_struct *p, s32 prev_cpu, u64 wake_flags, bool *is_idle) __ksym;
void scx_bpf_dsq_insert(struct task_struct *p, u64 dsq_id, u64 slice, u64 enq_flags) __ksym __weak;
void scx_bpf_dsq_insert_vtime(struct task_struct *p, u64 dsq_id, u64 slice, u64 vtime, u64 enq_flags) __ksym __weak;
u32 scx_bpf_dispatch_nr_slots(void) __ksym;
void scx_bpf_dispatch_cancel(void) __ksym;
bool scx_bpf_dsq_move_to_local(u64 dsq_id) __ksym;
void scx_bpf_dsq_move_set_slice(struct bpf_iter_scx_dsq *it__iter, u64 slice) __ksym;
void scx_bpf_dsq_move_set_vtime(struct bpf_iter_scx_dsq *it__iter, u64 vtime) __ksym;
bool scx_bpf_dsq_move(struct bpf_iter_scx_dsq *it__iter, struct task_struct *p, u64 dsq_id, u64 enq_flags) __ksym __weak;
bool scx_bpf_dsq_move_vtime(struct bpf_iter_scx_dsq *it__iter, struct task_struct *p, u64 dsq_id, u64 enq_flags) __ksym __weak;
u32 scx_bpf_reenqueue_local(void) __ksym;
void scx_bpf_kick_cpu(s32 cpu, u64 flags) __ksym;
s32 scx_bpf_dsq_nr_queued(u64 dsq_id) __ksym;
void scx_bpf_destroy_dsq(u64 dsq_id) __ksym;
int bpf_iter_scx_dsq_new(struct bpf_iter_scx_dsq *it, u64 dsq_id, u64 flags) __ksym __weak;
struct task_struct *bpf_iter_scx_dsq_next(struct bpf_iter_scx_dsq *it) __ksym __weak;
void bpf_iter_scx_dsq_destroy(struct bpf_iter_scx_dsq *it) __ksym __weak;
void scx_bpf_exit_bstr(s64 exit_code, char *fmt, unsigned long long *data, u32 data__sz) __ksym __weak;
void scx_bpf_error_bstr(char *fmt, unsigned long long *data, u32 data_len) __ksym;
void scx_bpf_dump_bstr(char *fmt, unsigned long long *data, u32 data_len) __ksym __weak;
u32 scx_bpf_cpuperf_cap(s32 cpu) __ksym __weak;
u32 scx_bpf_cpuperf_cur(s32 cpu) __ksym __weak;
void scx_bpf_cpuperf_set(s32 cpu, u32 perf) __ksym __weak;
u32 scx_bpf_nr_cpu_ids(void) __ksym __weak;
const struct cpumask *scx_bpf_get_possible_cpumask(void) __ksym __weak;
const struct cpumask *scx_bpf_get_online_cpumask(void) __ksym __weak;
void scx_bpf_put_cpumask(const struct cpumask *cpumask) __ksym __weak;
const struct cpumask *scx_bpf_get_idle_cpumask(void) __ksym;
const struct cpumask *scx_bpf_get_idle_smtmask(void) __ksym;
void scx_bpf_put_idle_cpumask(const struct cpumask *cpumask) __ksym;
bool scx_bpf_test_and_clear_cpu_idle(s32 cpu) __ksym;
s32 scx_bpf_pick_idle_cpu(const cpumask_t *cpus_allowed, u64 flags) __ksym;
s32 scx_bpf_pick_any_cpu(const cpumask_t *cpus_allowed, u64 flags) __ksym;
bool scx_bpf_task_running(const struct task_struct *p) __ksym;
s32 scx_bpf_task_cpu(const struct task_struct *p) __ksym;
struct rq *scx_bpf_cpu_rq(s32 cpu) __ksym;
struct cgroup *scx_bpf_task_cgroup(struct task_struct *p) __ksym __weak;

/*
 * Use the following as @it__iter when calling scx_bpf_dsq_move[_vtime]() from
 * within bpf_for_each() loops.
 */
#define BPF_FOR_EACH_ITER	(&___it)

static inline __attribute__((format(printf, 1, 2)))
void ___scx_bpf_bstr_format_checker(const char *fmt, ...) {}

/*
 * Helper macro for initializing the fmt and variadic argument inputs to both
 * bstr exit kfuncs. Callers to this function should use ___fmt and ___param to
 * refer to the initialized list of inputs to the bstr kfunc.
 */
#define scx_bpf_bstr_preamble(fmt, args...)					\
	static char ___fmt[] = fmt;						\
	/*									\
	 * Note that __param[] must have at least one				\
	 * element to keep the verifier happy.					\
	 */									\
	unsigned long long ___param[___bpf_narg(args) ?: 1] = {};		\
										\
	_Pragma("GCC diagnostic push")						\
	_Pragma("GCC diagnostic ignored \"-Wint-conversion\"")			\
	___bpf_fill(___param, args);						\
	_Pragma("GCC diagnostic pop")						\

/*
 * scx_bpf_exit() wraps the scx_bpf_exit_bstr() kfunc with variadic arguments
 * instead of an array of u64. Using this macro will cause the scheduler to
 * exit cleanly with the specified exit code being passed to user space.
 */
#define scx_bpf_exit(code, fmt, args...)					\
({										\
	scx_bpf_bstr_preamble(fmt, args)					\
	scx_bpf_exit_bstr(code, ___fmt, ___param, sizeof(___param));		\
	___scx_bpf_bstr_format_checker(fmt, ##args);				\
})

/*
 * scx_bpf_error() wraps the scx_bpf_error_bstr() kfunc with variadic arguments
 * instead of an array of u64. Invoking this macro will cause the scheduler to
 * exit in an erroneous state, with diagnostic information being passed to the
 * user.
 */
#define scx_bpf_error(fmt, args...)						\
({										\
	scx_bpf_bstr_preamble(fmt, args)					\
	scx_bpf_error_bstr(___fmt, ___param, sizeof(___param));			\
	___scx_bpf_bstr_format_checker(fmt, ##args);				\
})

/*
 * scx_bpf_dump() wraps the scx_bpf_dump_bstr() kfunc with variadic arguments
 * instead of an array of u64. To be used from ops.dump() and friends.
 */
#define scx_bpf_dump(fmt, args...)						\
({										\
	scx_bpf_bstr_preamble(fmt, args)					\
	scx_bpf_dump_bstr(___fmt, ___param, sizeof(___param));			\
	___scx_bpf_bstr_format_checker(fmt, ##args);				\
})

#define BPF_STRUCT_OPS(name, args...)						\
SEC("struct_ops/"#name)								\
BPF_PROG(name, ##args)

#define BPF_STRUCT_OPS_SLEEPABLE(name, args...)					\
SEC("struct_ops.s/"#name)							\
BPF_PROG(name, ##args)

/**
 * RESIZABLE_ARRAY - Generates annotations for an array that may be resized
 * @elfsec: the data section of the BPF program in which to place the array
 * @arr: the name of the array
 *
 * libbpf has an API for setting map value sizes. Since data sections (i.e.
 * bss, data, rodata) themselves are maps, a data section can be resized. If
 * a data section has an array as its last element, the BTF info for that
 * array will be adjusted so that length of the array is extended to meet the
 * new length of the data section. This macro annotates an array to have an
 * element count of one with the assumption that this array can be resized
 * within the userspace program. It also annotates the section specifier so
 * this array exists in a custom sub data section which can be resized
 * independently.
 *
 * See RESIZE_ARRAY() for the userspace convenience macro for resizing an
 * array declared with RESIZABLE_ARRAY().
 */
#define RESIZABLE_ARRAY(elfsec, arr) arr[1] SEC("."#elfsec"."#arr)

/**
 * MEMBER_VPTR - Obtain the verified pointer to a struct or array member
 * @base: struct or array to index
 * @member: dereferenced member (e.g. .field, [idx0][idx1], .field[idx0] ...)
 *
 * The verifier often gets confused by the instruction sequence the compiler
 * generates for indexing struct fields or arrays. This macro forces the
 * compiler to generate a code sequence which first calculates the byte offset,
 * checks it against the struct or array size and add that byte offset to
 * generate the pointer to the member to help the verifier.
 *
 * Ideally, we want to abort if the calculated offset is out-of-bounds. However,
 * BPF currently doesn't support abort, so evaluate to %NULL instead. The caller
 * must check for %NULL and take appropriate action to appease the verifier. To
 * avoid confusing the verifier, it's best to check for %NULL and dereference
 * immediately.
 *
 *	vptr = MEMBER_VPTR(my_array, [i][j]);
 *	if (!vptr)
 *		return error;
 *	*vptr = new_value;
 *
 * sizeof(@base) should encompass the memory area to be accessed and thus can't
 * be a pointer to the area. Use `MEMBER_VPTR(*ptr, .member)` instead of
 * `MEMBER_VPTR(ptr, ->member)`.
 */
#define MEMBER_VPTR(base, member) (typeof((base) member) *)			\
({										\
	u64 __base = (u64)&(base);						\
	u64 __addr = (u64)&((base) member) - __base;				\
	_Static_assert(sizeof(base) >= sizeof((base) member),			\
		       "@base is smaller than @member, is @base a pointer?");	\
	asm volatile (								\
		"if %0 <= %[max] goto +2\n"					\
		"%0 = 0\n"							\
		"goto +1\n"							\
		"%0 += %1\n"							\
		: "+r"(__addr)							\
		: "r"(__base),							\
		  [max]"i"(sizeof(base) - sizeof((base) member)));		\
	__addr;									\
})

/**
 * ARRAY_ELEM_PTR - Obtain the verified pointer to an array element
 * @arr: array to index into
 * @i: array index
 * @n: number of elements in array
 *
 * Similar to MEMBER_VPTR() but is intended for use with arrays where the
 * element count needs to be explicit.
 * It can be used in cases where a global array is defined with an initial
 * size but is intended to be be resized before loading the BPF program.
 * Without this version of the macro, MEMBER_VPTR() will use the compile time
 * size of the array to compute the max, which will result in rejection by
 * the verifier.
 */
#define ARRAY_ELEM_PTR(arr, i, n) (typeof(arr[i]) *)				\
({										\
	u64 __base = (u64)arr;							\
	u64 __addr = (u64)&(arr[i]) - __base;					\
	asm volatile (								\
		"if %0 <= %[max] goto +2\n"					\
		"%0 = 0\n"							\
		"goto +1\n"							\
		"%0 += %1\n"							\
		: "+r"(__addr)							\
		: "r"(__base),							\
		  [max]"r"(sizeof(arr[0]) * ((n) - 1)));			\
	__addr;									\
})


/*
 * BPF declarations and helpers
 */

/* list and rbtree */
#define __contains(name, node) __attribute__((btf_decl_tag("contains:" #name ":" #node)))
#define private(name) SEC(".data." #name) __hidden __attribute__((aligned(8)))

void *bpf_obj_new_impl(__u64 local_type_id, void *meta) __ksym;
void bpf_obj_drop_impl(void *kptr, void *meta) __ksym;

#define bpf_obj_new(type) ((type *)bpf_obj_new_impl(bpf_core_type_id_local(type), NULL))
#define bpf_obj_drop(kptr) bpf_obj_drop_impl(kptr, NULL)

void bpf_list_push_front(struct bpf_list_head *head, struct bpf_list_node *node) __ksym;
void bpf_list_push_back(struct bpf_list_head *head, struct bpf_list_node *node) __ksym;
struct bpf_list_node *bpf_list_pop_front(struct bpf_list_head *head) __ksym;
struct bpf_list_node *bpf_list_pop_back(struct bpf_list_head *head) __ksym;
struct bpf_rb_node *bpf_rbtree_remove(struct bpf_rb_root *root,
				      struct bpf_rb_node *node) __ksym;
int bpf_rbtree_add_impl(struct bpf_rb_root *root, struct bpf_rb_node *node,
			bool (less)(struct bpf_rb_node *a, const struct bpf_rb_node *b),
			void *meta, __u64 off) __ksym;
#define bpf_rbtree_add(head, node, less) bpf_rbtree_add_impl(head, node, less, NULL, 0)

struct bpf_rb_node *bpf_rbtree_first(struct bpf_rb_root *root) __ksym;

void *bpf_refcount_acquire_impl(void *kptr, void *meta) __ksym;
#define bpf_refcount_acquire(kptr) bpf_refcount_acquire_impl(kptr, NULL)

/* task */
struct task_struct *bpf_task_from_pid(s32 pid) __ksym;
struct task_struct *bpf_task_acquire(struct task_struct *p) __ksym;
void bpf_task_release(struct task_struct *p) __ksym;

/* cgroup */
struct cgroup *bpf_cgroup_ancestor(struct cgroup *cgrp, int level) __ksym;
void bpf_cgroup_release(struct cgroup *cgrp) __ksym;
struct cgroup *bpf_cgroup_from_id(u64 cgid) __ksym;

/* css iteration */
struct bpf_iter_css;
struct cgroup_subsys_state;
extern int bpf_iter_css_new(struct bpf_iter_css *it,
			    struct cgroup_subsys_state *start,
			    unsigned int flags) __weak __ksym;
extern struct cgroup_subsys_state *
bpf_iter_css_next(struct bpf_iter_css *it) __weak __ksym;
extern void bpf_iter_css_destroy(struct bpf_iter_css *it) __weak __ksym;

/* cpumask */
struct bpf_cpumask *bpf_cpumask_create(void) __ksym;
struct bpf_cpumask *bpf_cpumask_acquire(struct bpf_cpumask *cpumask) __ksym;
void bpf_cpumask_release(struct bpf_cpumask *cpumask) __ksym;
u32 bpf_cpumask_first(const struct cpumask *cpumask) __ksym;
u32 bpf_cpumask_first_zero(const struct cpumask *cpumask) __ksym;
void bpf_cpumask_set_cpu(u32 cpu, struct bpf_cpumask *cpumask) __ksym;
void bpf_cpumask_clear_cpu(u32 cpu, struct bpf_cpumask *cpumask) __ksym;
bool bpf_cpumask_test_cpu(u32 cpu, const struct cpumask *cpumask) __ksym;
bool bpf_cpumask_test_and_set_cpu(u32 cpu, struct bpf_cpumask *cpumask) __ksym;
bool bpf_cpumask_test_and_clear_cpu(u32 cpu, struct bpf_cpumask *cpumask) __ksym;
void bpf_cpumask_setall(struct bpf_cpumask *cpumask) __ksym;
void bpf_cpumask_clear(struct bpf_cpumask *cpumask) __ksym;
bool bpf_cpumask_and(struct bpf_cpumask *dst, const struct cpumask *src1,
		     const struct cpumask *src2) __ksym;
void bpf_cpumask_or(struct bpf_cpumask *dst, const struct cpumask *src1,
		    const struct cpumask *src2) __ksym;
void bpf_cpumask_xor(struct bpf_cpumask *dst, const struct cpumask *src1,
		     const struct cpumask *src2) __ksym;
bool bpf_cpumask_equal(const struct cpumask *src1, const struct cpumask *src2) __ksym;
bool bpf_cpumask_intersects(const struct cpumask *src1, const struct cpumask *src2) __ksym;
bool bpf_cpumask_subset(const struct cpumask *src1, const struct cpumask *src2) __ksym;
bool bpf_cpumask_empty(const struct cpumask *cpumask) __ksym;
bool bpf_cpumask_full(const struct cpumask *cpumask) __ksym;
void bpf_cpumask_copy(struct bpf_cpumask *dst, const struct cpumask *src) __ksym;
u32 bpf_cpumask_any_distribute(const struct cpumask *cpumask) __ksym;
u32 bpf_cpumask_any_and_distribute(const struct cpumask *src1,
				   const struct cpumask *src2) __ksym;
u32 bpf_cpumask_weight(const struct cpumask *cpumask) __ksym;

/*
 * Access a cpumask in read-only mode (typically to check bits).
 */
static __always_inline const struct cpumask *cast_mask(struct bpf_cpumask *mask)
{
	return (const struct cpumask *)mask;
}

/* rcu */
void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;


/*
 * Other helpers
 */

/* useful compiler attributes */
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define __maybe_unused __attribute__((__unused__))

/*
 * READ/WRITE_ONCE() are from kernel (include/asm-generic/rwonce.h). They
 * prevent compiler from caching, redoing or reordering reads or writes.
 */
typedef __u8  __attribute__((__may_alias__))  __u8_alias_t;
typedef __u16 __attribute__((__may_alias__)) __u16_alias_t;
typedef __u32 __attribute__((__may_alias__)) __u32_alias_t;
typedef __u64 __attribute__((__may_alias__)) __u64_alias_t;

static __always_inline void __read_once_size(const volatile void *p, void *res, int size)
{
	switch (size) {
	case 1: *(__u8_alias_t  *) res = *(volatile __u8_alias_t  *) p; break;
	case 2: *(__u16_alias_t *) res = *(volatile __u16_alias_t *) p; break;
	case 4: *(__u32_alias_t *) res = *(volatile __u32_alias_t *) p; break;
	case 8: *(__u64_alias_t *) res = *(volatile __u64_alias_t *) p; break;
	default:
		barrier();
		__builtin_memcpy((void *)res, (const void *)p, size);
		barrier();
	}
}

static __always_inline void __write_once_size(volatile void *p, void *res, int size)
{
	switch (size) {
	case 1: *(volatile  __u8_alias_t *) p = *(__u8_alias_t  *) res; break;
	case 2: *(volatile __u16_alias_t *) p = *(__u16_alias_t *) res; break;
	case 4: *(volatile __u32_alias_t *) p = *(__u32_alias_t *) res; break;
	case 8: *(volatile __u64_alias_t *) p = *(__u64_alias_t *) res; break;
	default:
		barrier();
		__builtin_memcpy((void *)p, (const void *)res, size);
		barrier();
	}
}

#define READ_ONCE(x)					\
({							\
	union { typeof(x) __val; char __c[1]; } __u =	\
		{ .__c = { 0 } };			\
	__read_once_size(&(x), __u.__c, sizeof(x));	\
	__u.__val;					\
})

#define WRITE_ONCE(x, val)				\
({							\
	union { typeof(x) __val; char __c[1]; } __u =	\
		{ .__val = (val) }; 			\
	__write_once_size(&(x), __u.__c, sizeof(x));	\
	__u.__val;					\
})

/*
 * log2_u32 - Compute the base 2 logarithm of a 32-bit exponential value.
 * @v: The value for which we're computing the base 2 logarithm.
 */
static inline u32 log2_u32(u32 v)
{
        u32 r;
        u32 shift;

        r = (v > 0xFFFF) << 4; v >>= r;
        shift = (v > 0xFF) << 3; v >>= shift; r |= shift;
        shift = (v > 0xF) << 2; v >>= shift; r |= shift;
        shift = (v > 0x3) << 1; v >>= shift; r |= shift;
        r |= (v >> 1);
        return r;
}

/*
 * log2_u64 - Compute the base 2 logarithm of a 64-bit exponential value.
 * @v: The value for which we're computing the base 2 logarithm.
 */
static inline u32 log2_u64(u64 v)
{
        u32 hi = v >> 32;
        if (hi)
                return log2_u32(hi) + 32 + 1;
        else
                return log2_u32(v) + 1;
}

#include "compat.bpf.h"

#endif	/* __SCX_COMMON_BPF_H */
