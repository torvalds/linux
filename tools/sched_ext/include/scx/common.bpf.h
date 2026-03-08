/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#ifndef __SCX_COMMON_BPF_H
#define __SCX_COMMON_BPF_H

/*
 * The generated kfunc prototypes in vmlinux.h are missing address space
 * attributes which cause build failures. For now, suppress the generated
 * prototypes. See https://github.com/sched-ext/scx/issues/1111.
 */
#define BPF_NO_KFUNC_PROTOTYPES

#ifdef LSP
#define __bpf__
#include "../vmlinux.h"
#else
#include "vmlinux.h"
#endif

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <asm-generic/errno.h>
#include "user_exit_info.bpf.h"
#include "enum_defs.autogen.h"

#define PF_IDLE				0x00000002	/* I am an IDLE thread */
#define PF_IO_WORKER			0x00000010	/* Task is an IO worker */
#define PF_WQ_WORKER			0x00000020	/* I'm a workqueue worker */
#define PF_KCOMPACTD			0x00010000      /* I am kcompactd */
#define PF_KSWAPD			0x00020000      /* I am kswapd */
#define PF_KTHREAD			0x00200000	/* I am a kernel thread */
#define PF_EXITING			0x00000004
#define CLOCK_MONOTONIC			1

#ifndef NR_CPUS
#define NR_CPUS 1024
#endif

#ifndef NUMA_NO_NODE
#define	NUMA_NO_NODE	(-1)
#endif

extern int LINUX_KERNEL_VERSION __kconfig;
extern const char CONFIG_CC_VERSION_TEXT[64] __kconfig __weak;
extern const char CONFIG_LOCALVERSION[64] __kconfig __weak;

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
s32 __scx_bpf_select_cpu_and(struct task_struct *p, const struct cpumask *cpus_allowed,
			     struct scx_bpf_select_cpu_and_args *args) __ksym __weak;
bool __scx_bpf_dsq_insert_vtime(struct task_struct *p, struct scx_bpf_dsq_insert_vtime_args *args) __ksym __weak;
u32 scx_bpf_dispatch_nr_slots(void) __ksym;
void scx_bpf_dispatch_cancel(void) __ksym;
void scx_bpf_kick_cpu(s32 cpu, u64 flags) __ksym;
s32 scx_bpf_dsq_nr_queued(u64 dsq_id) __ksym;
void scx_bpf_destroy_dsq(u64 dsq_id) __ksym;
struct task_struct *scx_bpf_dsq_peek(u64 dsq_id) __ksym __weak;
int bpf_iter_scx_dsq_new(struct bpf_iter_scx_dsq *it, u64 dsq_id, u64 flags) __ksym __weak;
struct task_struct *bpf_iter_scx_dsq_next(struct bpf_iter_scx_dsq *it) __ksym __weak;
void bpf_iter_scx_dsq_destroy(struct bpf_iter_scx_dsq *it) __ksym __weak;
void scx_bpf_exit_bstr(s64 exit_code, char *fmt, unsigned long long *data, u32 data__sz) __ksym __weak;
void scx_bpf_error_bstr(char *fmt, unsigned long long *data, u32 data_len) __ksym;
void scx_bpf_dump_bstr(char *fmt, unsigned long long *data, u32 data_len) __ksym __weak;
u32 scx_bpf_cpuperf_cap(s32 cpu) __ksym __weak;
u32 scx_bpf_cpuperf_cur(s32 cpu) __ksym __weak;
void scx_bpf_cpuperf_set(s32 cpu, u32 perf) __ksym __weak;
u32 scx_bpf_nr_node_ids(void) __ksym __weak;
u32 scx_bpf_nr_cpu_ids(void) __ksym __weak;
int scx_bpf_cpu_node(s32 cpu) __ksym __weak;
const struct cpumask *scx_bpf_get_possible_cpumask(void) __ksym __weak;
const struct cpumask *scx_bpf_get_online_cpumask(void) __ksym __weak;
void scx_bpf_put_cpumask(const struct cpumask *cpumask) __ksym __weak;
const struct cpumask *scx_bpf_get_idle_cpumask_node(int node) __ksym __weak;
const struct cpumask *scx_bpf_get_idle_cpumask(void) __ksym;
const struct cpumask *scx_bpf_get_idle_smtmask_node(int node) __ksym __weak;
const struct cpumask *scx_bpf_get_idle_smtmask(void) __ksym;
void scx_bpf_put_idle_cpumask(const struct cpumask *cpumask) __ksym;
bool scx_bpf_test_and_clear_cpu_idle(s32 cpu) __ksym;
s32 scx_bpf_pick_idle_cpu_node(const cpumask_t *cpus_allowed, int node, u64 flags) __ksym __weak;
s32 scx_bpf_pick_idle_cpu(const cpumask_t *cpus_allowed, u64 flags) __ksym;
s32 scx_bpf_pick_any_cpu_node(const cpumask_t *cpus_allowed, int node, u64 flags) __ksym __weak;
s32 scx_bpf_pick_any_cpu(const cpumask_t *cpus_allowed, u64 flags) __ksym;
bool scx_bpf_task_running(const struct task_struct *p) __ksym;
s32 scx_bpf_task_cpu(const struct task_struct *p) __ksym;
struct rq *scx_bpf_cpu_rq(s32 cpu) __ksym;
struct rq *scx_bpf_locked_rq(void) __ksym;
struct task_struct *scx_bpf_cpu_curr(s32 cpu) __ksym __weak;
u64 scx_bpf_now(void) __ksym __weak;
void scx_bpf_events(struct scx_event_stats *events, size_t events__sz) __ksym __weak;
bool scx_bpf_sub_dispatch(u64 cgroup_id) __ksym __weak;

/*
 * Use the following as @it__iter when calling scx_bpf_dsq_move[_vtime]() from
 * within bpf_for_each() loops.
 */
#define BPF_FOR_EACH_ITER	(&___it)

#define scx_read_event(e, name)							\
	(bpf_core_field_exists((e)->name) ? (e)->name : 0)

static inline __attribute__((format(printf, 1, 2)))
void ___scx_bpf_bstr_format_checker(const char *fmt, ...) {}

#define SCX_STRINGIFY(x) #x
#define SCX_TOSTRING(x) SCX_STRINGIFY(x)

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
	_Pragma("GCC diagnostic pop")

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
 * user. It appends the file and line number to aid debugging.
 */
#define scx_bpf_error(fmt, args...)						\
({										\
	scx_bpf_bstr_preamble(							\
		__FILE__ ":" SCX_TOSTRING(__LINE__) ": " fmt, ##args)		\
	scx_bpf_error_bstr(___fmt, ___param, sizeof(___param));			\
	___scx_bpf_bstr_format_checker(						\
		__FILE__ ":" SCX_TOSTRING(__LINE__) ": " fmt, ##args);		\
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

/*
 * scx_bpf_dump_header() is a wrapper around scx_bpf_dump that adds a header
 * of system information for debugging.
 */
#define scx_bpf_dump_header()							\
({										\
	scx_bpf_dump("kernel: %d.%d.%d %s\ncc: %s\n",				\
		     LINUX_KERNEL_VERSION >> 16,				\
		     LINUX_KERNEL_VERSION >> 8 & 0xFF,				\
		     LINUX_KERNEL_VERSION & 0xFF,				\
		     CONFIG_LOCALVERSION,					\
		     CONFIG_CC_VERSION_TEXT);					\
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
#ifndef MEMBER_VPTR
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
#endif /* MEMBER_VPTR */

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
#ifndef ARRAY_ELEM_PTR
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
#endif /* ARRAY_ELEM_PTR */

/**
 * __sink - Hide @expr's value from the compiler and BPF verifier
 * @expr: The expression whose value should be opacified
 *
 * No-op at runtime. The empty inline assembly with a read-write constraint
 * ("+g") has two effects at compile/verify time:
 *
 * 1. Compiler: treats @expr as both read and written, preventing dead-code
 *    elimination and keeping @expr (and any side effects that produced it)
 *    alive.
 *
 * 2. BPF verifier: forgets the precise value/range of @expr ("makes it
 *    imprecise"). The verifier normally tracks exact ranges for every register
 *    and stack slot. While useful, precision means each distinct value creates a
 *    separate verifier state. Inside loops this leads to state explosion - each
 *    iteration carries different precise values so states never merge and the
 *    verifier explores every iteration individually.
 *
 * Example - preventing loop state explosion::
 *
 *     u32 nr_intersects = 0, nr_covered = 0;
 *     __sink(nr_intersects);
 *     __sink(nr_covered);
 *     bpf_for(i, 0, nr_nodes) {
 *         if (intersects(cpumask, node_mask[i]))
 *             nr_intersects++;
 *         if (covers(cpumask, node_mask[i]))
 *             nr_covered++;
 *     }
 *
 * Without __sink(), the verifier tracks every possible (nr_intersects,
 * nr_covered) pair across iterations, causing "BPF program is too large". With
 * __sink(), the values become unknown scalars so all iterations collapse into
 * one reusable state.
 *
 * Example - keeping a reference alive::
 *
 *     struct task_struct *t = bpf_task_acquire(task);
 *     __sink(t);
 *
 * Follows the convention from BPF selftests (bpf_misc.h).
 */
#define __sink(expr) asm volatile ("" : "+g"(expr))

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

int bpf_list_push_front_impl(struct bpf_list_head *head,
				    struct bpf_list_node *node,
				    void *meta, __u64 off) __ksym;
#define bpf_list_push_front(head, node) bpf_list_push_front_impl(head, node, NULL, 0)

int bpf_list_push_back_impl(struct bpf_list_head *head,
				   struct bpf_list_node *node,
				   void *meta, __u64 off) __ksym;
#define bpf_list_push_back(head, node) bpf_list_push_back_impl(head, node, NULL, 0)

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
struct cgroup *bpf_cgroup_acquire(struct cgroup *cgrp) __ksym;
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

int bpf_iter_bits_new(struct bpf_iter_bits *it, const u64 *unsafe_ptr__ign, u32 nr_words) __ksym;
int *bpf_iter_bits_next(struct bpf_iter_bits *it) __ksym;
void bpf_iter_bits_destroy(struct bpf_iter_bits *it) __ksym;

#define def_iter_struct(name)							\
struct bpf_iter_##name {							\
    struct bpf_iter_bits it;							\
    const struct cpumask *bitmap;						\
};

#define def_iter_new(name)							\
static inline int bpf_iter_##name##_new(					\
	struct bpf_iter_##name *it, const u64 *unsafe_ptr__ign, u32 nr_words)	\
{										\
	it->bitmap = scx_bpf_get_##name##_cpumask();				\
	return bpf_iter_bits_new(&it->it, (const u64 *)it->bitmap,		\
				 sizeof(struct cpumask) / 8);			\
}

#define def_iter_next(name)							\
static inline int *bpf_iter_##name##_next(struct bpf_iter_##name *it) {		\
	return bpf_iter_bits_next(&it->it);					\
}

#define def_iter_destroy(name)							\
static inline void bpf_iter_##name##_destroy(struct bpf_iter_##name *it) {	\
	scx_bpf_put_cpumask(it->bitmap);					\
	bpf_iter_bits_destroy(&it->it);						\
}
#define def_for_each_cpu(cpu, name) for_each_##name##_cpu(cpu)

/// Provides iterator for possible and online cpus.
///
/// # Example
///
/// ```
/// static inline void example_use() {
///     int *cpu;
///
///     for_each_possible_cpu(cpu){
///         bpf_printk("CPU %d is possible", *cpu);
///     }
///
///     for_each_online_cpu(cpu){
///         bpf_printk("CPU %d is online", *cpu);
///     }
/// }
/// ```
def_iter_struct(possible);
def_iter_new(possible);
def_iter_next(possible);
def_iter_destroy(possible);
#define for_each_possible_cpu(cpu) bpf_for_each(possible, cpu, NULL, 0)

def_iter_struct(online);
def_iter_new(online);
def_iter_next(online);
def_iter_destroy(online);
#define for_each_online_cpu(cpu) bpf_for_each(online, cpu, NULL, 0)

/*
 * Access a cpumask in read-only mode (typically to check bits).
 */
static __always_inline const struct cpumask *cast_mask(struct bpf_cpumask *mask)
{
	return (const struct cpumask *)mask;
}

/*
 * Return true if task @p cannot migrate to a different CPU, false
 * otherwise.
 */
static inline bool is_migration_disabled(const struct task_struct *p)
{
	/*
	 * Testing p->migration_disabled in a BPF code is tricky because the
	 * migration is _always_ disabled while running the BPF code.
	 * The prolog (__bpf_prog_enter) and epilog (__bpf_prog_exit) for BPF
	 * code execution disable and re-enable the migration of the current
	 * task, respectively. So, the _current_ task of the sched_ext ops is
	 * always migration-disabled. Moreover, p->migration_disabled could be
	 * two or greater when a sched_ext ops BPF code (e.g., ops.tick) is
	 * executed in the middle of the other BPF code execution.
	 *
	 * Therefore, we should decide that the _current_ task is
	 * migration-disabled only when its migration_disabled count is greater
	 * than one. In other words, when  p->migration_disabled == 1, there is
	 * an ambiguity, so we should check if @p is the current task or not.
	 */
	if (bpf_core_field_exists(p->migration_disabled)) {
		if (p->migration_disabled == 1)
			return bpf_get_current_task_btf() != p;
		else
			return p->migration_disabled;
	}
	return false;
}

/* rcu */
void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;

/*
 * Time helpers, most of which are from jiffies.h.
 */

/**
 * time_delta - Calculate the delta between new and old time stamp
 * @after: first comparable as u64
 * @before: second comparable as u64
 *
 * Return: the time difference, which is >= 0
 */
static inline s64 time_delta(u64 after, u64 before)
{
	return (s64)(after - before) > 0 ? (s64)(after - before) : 0;
}

/**
 * time_after - returns true if the time a is after time b.
 * @a: first comparable as u64
 * @b: second comparable as u64
 *
 * Do this with "<0" and ">=0" to only test the sign of the result. A
 * good compiler would generate better code (and a really good compiler
 * wouldn't care). Gcc is currently neither.
 *
 * Return: %true is time a is after time b, otherwise %false.
 */
static inline bool time_after(u64 a, u64 b)
{
	return (s64)(b - a) < 0;
}

/**
 * time_before - returns true if the time a is before time b.
 * @a: first comparable as u64
 * @b: second comparable as u64
 *
 * Return: %true is time a is before time b, otherwise %false.
 */
static inline bool time_before(u64 a, u64 b)
{
	return time_after(b, a);
}

/**
 * time_after_eq - returns true if the time a is after or the same as time b.
 * @a: first comparable as u64
 * @b: second comparable as u64
 *
 * Return: %true is time a is after or the same as time b, otherwise %false.
 */
static inline bool time_after_eq(u64 a, u64 b)
{
	return (s64)(a - b) >= 0;
}

/**
 * time_before_eq - returns true if the time a is before or the same as time b.
 * @a: first comparable as u64
 * @b: second comparable as u64
 *
 * Return: %true is time a is before or the same as time b, otherwise %false.
 */
static inline bool time_before_eq(u64 a, u64 b)
{
	return time_after_eq(b, a);
}

/**
 * time_in_range - Calculate whether a is in the range of [b, c].
 * @a: time to test
 * @b: beginning of the range
 * @c: end of the range
 *
 * Return: %true is time a is in the range [b, c], otherwise %false.
 */
static inline bool time_in_range(u64 a, u64 b, u64 c)
{
	return time_after_eq(a, b) && time_before_eq(a, c);
}

/**
 * time_in_range_open - Calculate whether a is in the range of [b, c).
 * @a: time to test
 * @b: beginning of the range
 * @c: end of the range
 *
 * Return: %true is time a is in the range [b, c), otherwise %false.
 */
static inline bool time_in_range_open(u64 a, u64 b, u64 c)
{
	return time_after_eq(a, b) && time_before(a, c);
}


/*
 * Other helpers
 */

/* useful compiler attributes */
#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif
#ifndef __maybe_unused
#define __maybe_unused __attribute__((__unused__))
#endif

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

/*
 * __unqual_typeof(x) - Declare an unqualified scalar type, leaving
 *			non-scalar types unchanged,
 *
 * Prefer C11 _Generic for better compile-times and simpler code. Note: 'char'
 * is not type-compatible with 'signed char', and we define a separate case.
 *
 * This is copied verbatim from kernel's include/linux/compiler_types.h, but
 * with default expression (for pointers) changed from (x) to (typeof(x)0).
 *
 * This is because LLVM has a bug where for lvalue (x), it does not get rid of
 * an extra address_space qualifier, but does in case of rvalue (typeof(x)0).
 * Hence, for pointers, we need to create an rvalue expression to get the
 * desired type. See https://github.com/llvm/llvm-project/issues/53400.
 */
#define __scalar_type_to_expr_cases(type) \
	unsigned type : (unsigned type)0, signed type : (signed type)0

#define __unqual_typeof(x)                              \
	typeof(_Generic((x),                            \
		char: (char)0,                          \
		__scalar_type_to_expr_cases(char),      \
		__scalar_type_to_expr_cases(short),     \
		__scalar_type_to_expr_cases(int),       \
		__scalar_type_to_expr_cases(long),      \
		__scalar_type_to_expr_cases(long long), \
		default: (typeof(x))0))

#define READ_ONCE(x)								\
({										\
	union { __unqual_typeof(x) __val; char __c[1]; } __u =			\
		{ .__c = { 0 } };						\
	__read_once_size((__unqual_typeof(x) *)&(x), __u.__c, sizeof(x));	\
	__u.__val;								\
})

#define WRITE_ONCE(x, val)							\
({										\
	union { __unqual_typeof(x) __val; char __c[1]; } __u =			\
		{ .__val = (val) }; 						\
	__write_once_size((__unqual_typeof(x) *)&(x), __u.__c, sizeof(x));	\
	__u.__val;								\
})

/*
 * __calc_avg - Calculate exponential weighted moving average (EWMA) with
 * @old and @new values. @decay represents how large the @old value remains.
 * With a larger @decay value, the moving average changes slowly, exhibiting
 * fewer fluctuations.
 */
#define __calc_avg(old, new, decay) ({						\
	typeof(decay) thr = 1 << (decay);					\
	typeof(old) ret;							\
	if (((old) < thr) || ((new) < thr)) {					\
		if (((old) == 1) && ((new) == 0))				\
			ret = 0;						\
		else								\
			ret = ((old) - ((old) >> 1)) + ((new) >> 1);		\
	} else {								\
		ret = ((old) - ((old) >> (decay))) + ((new) >> (decay));	\
	}									\
	ret;									\
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

/*
 * sqrt_u64 - Calculate the square root of value @x using Newton's method.
 */
static inline u64 __sqrt_u64(u64 x)
{
	if (x == 0 || x == 1)
		return x;

	u64 r = ((1ULL << 32) > x) ? x : (1ULL << 32);

	for (int i = 0; i < 8; ++i) {
		u64 q = x / r;
		if (r <= q)
			break;
		r = (r + q) >> 1;
	}
	return r;
}

/*
 * ctzll -- Counts trailing zeros in an unsigned long long. If the input value
 * is zero, the return value is undefined.
 */
static inline int ctzll(u64 v)
{
#if (!defined(__BPF__) && defined(__SCX_TARGET_ARCH_x86)) || \
	(defined(__BPF__) && defined(__clang_major__) && __clang_major__ >= 19)
	/*
	 * Use the ctz builtin when: (1) building for native x86, or
	 * (2) building for BPF with clang >= 19 (BPF backend supports
	 * the intrinsic from clang 19 onward; earlier versions hit
	 * "unimplemented opcode" in the backend).
	 */
	return __builtin_ctzll(v);
#else
	/*
	 * If neither the target architecture nor the toolchains support ctzll,
	 * use software-based emulation. Let's use the De Bruijn sequence-based
	 * approach to find LSB fastly. See the details of De Bruijn sequence:
	 *
	 * https://en.wikipedia.org/wiki/De_Bruijn_sequence
	 * https://www.chessprogramming.org/BitScan#De_Bruijn_Multiplication
	 */
	const int lookup_table[64] = {
		 0,  1, 48,  2, 57, 49, 28,  3, 61, 58, 50, 42, 38, 29, 17,  4,
		62, 55, 59, 36, 53, 51, 43, 22, 45, 39, 33, 30, 24, 18, 12,  5,
		63, 47, 56, 27, 60, 41, 37, 16, 54, 35, 52, 21, 44, 32, 23, 11,
		46, 26, 40, 15, 34, 20, 31, 10, 25, 14, 19,  9, 13,  8,  7,  6,
	};
	const u64 DEBRUIJN_CONSTANT = 0x03f79d71b4cb0a89ULL;
	unsigned int index;
	u64 lowest_bit;
	const int *lt;

	if (v == 0)
		return -1;

	/*
	 * Isolate the least significant bit (LSB).
	 * For example, if v = 0b...10100, then v & -v = 0b...00100
	 */
	lowest_bit = v & -v;

	/*
	 * Each isolated bit produces a unique 6-bit value, guaranteed by the
	 * De Bruijn property. Calculate a unique index into the lookup table
	 * using the magic constant and a right shift.
	 *
	 * Multiplying by the 64-bit constant "spreads out" that 1-bit into a
	 * unique pattern in the top 6 bits. This uniqueness property is
	 * exactly what a De Bruijn sequence guarantees: Every possible 6-bit
	 * pattern (in top bits) occurs exactly once for each LSB position. So,
	 * the constant 0x03f79d71b4cb0a89ULL is carefully chosen to be a
	 * De Bruijn sequence, ensuring no collisions in the table index.
	 */
	index = (lowest_bit * DEBRUIJN_CONSTANT) >> 58;

	/*
	 * Lookup in a precomputed table. No collision is guaranteed by the
	 * De Bruijn property.
	 */
	lt = MEMBER_VPTR(lookup_table, [index]);
	return (lt)? *lt : -1;
#endif
}

/*
 * Return a value proportionally scaled to the task's weight.
 */
static inline u64 scale_by_task_weight(const struct task_struct *p, u64 value)
{
	return (value * p->scx.weight) / 100;
}

/*
 * Return a value inversely proportional to the task's weight.
 */
static inline u64 scale_by_task_weight_inverse(const struct task_struct *p, u64 value)
{
	return value * 100 / p->scx.weight;
}


/*
 * Get a random u64 from the kernel's pseudo-random generator.
 */
static inline u64 get_prandom_u64()
{
	return ((u64)bpf_get_prandom_u32() << 32) | bpf_get_prandom_u32();
}

/*
 * Define the shadow structure to avoid a compilation error when
 * vmlinux.h does not enable necessary kernel configs. The ___local
 * suffix is a CO-RE convention that tells the loader to match this
 * against the base struct rq in the kernel. The attribute
 * preserve_access_index tells the compiler to generate a CO-RE
 * relocation for these fields.
 */
struct rq___local {
	/*
	 * A monotonically increasing clock per CPU. It is rq->clock minus
	 * cumulative IRQ time and hypervisor steal time. Unlike rq->clock,
	 * it does not advance during IRQ processing or hypervisor preemption.
	 * It does advance during idle (the idle task counts as a running task
	 * for this purpose).
	 */
	u64		clock_task;
	/*
	 * Invariant version of clock_task scaled by CPU capacity and
	 * frequency. For example, clock_pelt advances 2x slower on a CPU
	 * with half the capacity.
	 *
	 * At idle exit, rq->clock_pelt jumps forward to resync with
	 * clock_task. The kernel's rq_clock_pelt() corrects for this jump
	 * by subtracting lost_idle_time, yielding a clock that appears
	 * continuous across idle transitions. scx_clock_pelt() mirrors
	 * rq_clock_pelt() by performing the same subtraction.
	 */
	u64		clock_pelt;
	/*
	 * Accumulates the magnitude of each clock_pelt jump at idle exit.
	 * Subtracting this from clock_pelt gives rq_clock_pelt(): a
	 * continuous, capacity-invariant clock suitable for both task
	 * execution time stamping and cross-idle measurements.
	 */
	unsigned long	lost_idle_time;
	/*
	 * Shadow of paravirt_steal_clock() (the hypervisor's cumulative
	 * stolen time counter). Stays frozen while the hypervisor preempts
	 * the vCPU; catches up the next time update_rq_clock_task() is
	 * called. The delta is the stolen time not yet subtracted from
	 * clock_task.
	 *
	 * Unlike irqtime->total (a plain kernel-side field), the live stolen
	 * time counter lives in hypervisor-specific shared memory and has no
	 * kernel-side equivalent readable from BPF in a hypervisor-agnostic
	 * way. This field is therefore the only portable BPF-accessible
	 * approximation of cumulative steal time.
	 *
	 * Available only when CONFIG_PARAVIRT_TIME_ACCOUNTING is on.
	 */
	u64		prev_steal_time_rq;
} __attribute__((preserve_access_index));

extern struct rq runqueues __ksym;

/*
 * Define the shadow structure to avoid a compilation error when
 * vmlinux.h does not enable necessary kernel configs.
 */
struct irqtime___local {
	/*
	 * Cumulative IRQ time counter for this CPU, in nanoseconds. Advances
	 * immediately at the exit of every hardirq and non-ksoftirqd softirq
	 * via irqtime_account_irq(). ksoftirqd time is counted as normal
	 * task time and is NOT included. NMI time is also NOT included.
	 *
	 * The companion field irqtime->sync (struct u64_stats_sync) protects
	 * against 64-bit tearing on 32-bit architectures. On 64-bit kernels,
	 * u64_stats_sync is an empty struct and all seqcount operations are
	 * no-ops, so a plain BPF_CORE_READ of this field is safe.
	 *
	 * Available only when CONFIG_IRQ_TIME_ACCOUNTING is on.
	 */
	u64		total;
} __attribute__((preserve_access_index));

/*
 * cpu_irqtime is a per-CPU variable defined only when
 * CONFIG_IRQ_TIME_ACCOUNTING is on. Declare it as __weak so the BPF
 * loader sets its address to 0 (rather than failing) when the symbol
 * is absent from the running kernel.
 */
extern struct irqtime___local cpu_irqtime __ksym __weak;

static inline struct rq___local *get_current_rq(u32 cpu)
{
	/*
	 * This is a workaround to get an rq pointer since we decided to
	 * deprecate scx_bpf_cpu_rq().
	 *
	 * WARNING: The caller must hold the rq lock for @cpu. This is
	 * guaranteed when called from scheduling callbacks (ops.running,
	 * ops.stopping, ops.enqueue, ops.dequeue, ops.dispatch, etc.).
	 * There is no runtime check available in BPF for kernel spinlock
	 * state — correctness is enforced by calling context only.
	 */
	return (void *)bpf_per_cpu_ptr(&runqueues, cpu);
}

static inline u64 scx_clock_task(u32 cpu)
{
	struct rq___local *rq = get_current_rq(cpu);

	/* Equivalent to the kernel's rq_clock_task(). */
	return rq ? rq->clock_task : 0;
}

static inline u64 scx_clock_pelt(u32 cpu)
{
	struct rq___local *rq = get_current_rq(cpu);

	/*
	 * Equivalent to the kernel's rq_clock_pelt(): subtracts
	 * lost_idle_time from clock_pelt to absorb the jump that occurs
	 * when clock_pelt resyncs with clock_task at idle exit. The result
	 * is a continuous, capacity-invariant clock safe for both task
	 * execution time stamping and cross-idle measurements.
	 */
	return rq ? (rq->clock_pelt - rq->lost_idle_time) : 0;
}

static inline u64 scx_clock_virt(u32 cpu)
{
	struct rq___local *rq;

	/*
	 * Check field existence before calling get_current_rq() so we avoid
	 * the per_cpu lookup entirely on kernels built without
	 * CONFIG_PARAVIRT_TIME_ACCOUNTING.
	 */
	if (!bpf_core_field_exists(((struct rq___local *)0)->prev_steal_time_rq))
		return 0;

	/* Lagging shadow of the kernel's paravirt_steal_clock(). */
	rq = get_current_rq(cpu);
	return rq ? BPF_CORE_READ(rq, prev_steal_time_rq) : 0;
}

static inline u64 scx_clock_irq(u32 cpu)
{
	struct irqtime___local *irqt;

	/*
	 * bpf_core_type_exists() resolves at load time: if struct irqtime is
	 * absent from kernel BTF (CONFIG_IRQ_TIME_ACCOUNTING off), the loader
	 * patches this into an unconditional return 0, making the
	 * bpf_per_cpu_ptr() call below dead code that the verifier never sees.
	 */
	if (!bpf_core_type_exists(struct irqtime___local))
		return 0;

	/* Equivalent to the kernel's irq_time_read(). */
	irqt = bpf_per_cpu_ptr(&cpu_irqtime, cpu);
	return irqt ? BPF_CORE_READ(irqt, total) : 0;
}

#include "compat.bpf.h"
#include "enums.bpf.h"

#endif	/* __SCX_COMMON_BPF_H */
