// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

unsigned long global_flags;

extern void bpf_local_irq_save(unsigned long *) __weak __ksym;
extern void bpf_local_irq_restore(unsigned long *) __weak __ksym;
extern int bpf_copy_from_user_str(void *dst, u32 dst__sz, const void *unsafe_ptr__ign, u64 flags) __weak __ksym;

struct bpf_res_spin_lock lockA __hidden SEC(".data.A");
struct bpf_res_spin_lock lockB __hidden SEC(".data.B");

SEC("?tc")
__failure __msg("arg#0 doesn't point to an irq flag on stack")
int irq_save_bad_arg(struct __sk_buff *ctx)
{
	bpf_local_irq_save(&global_flags);
	return 0;
}

SEC("?tc")
__failure __msg("arg#0 doesn't point to an irq flag on stack")
int irq_restore_bad_arg(struct __sk_buff *ctx)
{
	bpf_local_irq_restore(&global_flags);
	return 0;
}

SEC("?tc")
__failure __msg("BPF_EXIT instruction in main prog cannot be used inside bpf_local_irq_save-ed region")
int irq_restore_missing_2(struct __sk_buff *ctx)
{
	unsigned long flags1;
	unsigned long flags2;

	bpf_local_irq_save(&flags1);
	bpf_local_irq_save(&flags2);
	return 0;
}

SEC("?tc")
__failure __msg("BPF_EXIT instruction in main prog cannot be used inside bpf_local_irq_save-ed region")
int irq_restore_missing_3(struct __sk_buff *ctx)
{
	unsigned long flags1;
	unsigned long flags2;
	unsigned long flags3;

	bpf_local_irq_save(&flags1);
	bpf_local_irq_save(&flags2);
	bpf_local_irq_save(&flags3);
	return 0;
}

SEC("?tc")
__failure __msg("BPF_EXIT instruction in main prog cannot be used inside bpf_local_irq_save-ed region")
int irq_restore_missing_3_minus_2(struct __sk_buff *ctx)
{
	unsigned long flags1;
	unsigned long flags2;
	unsigned long flags3;

	bpf_local_irq_save(&flags1);
	bpf_local_irq_save(&flags2);
	bpf_local_irq_save(&flags3);
	bpf_local_irq_restore(&flags3);
	bpf_local_irq_restore(&flags2);
	return 0;
}

static __noinline void local_irq_save(unsigned long *flags)
{
	bpf_local_irq_save(flags);
}

static __noinline void local_irq_restore(unsigned long *flags)
{
	bpf_local_irq_restore(flags);
}

SEC("?tc")
__failure __msg("BPF_EXIT instruction in main prog cannot be used inside bpf_local_irq_save-ed region")
int irq_restore_missing_1_subprog(struct __sk_buff *ctx)
{
	unsigned long flags;

	local_irq_save(&flags);
	return 0;
}

SEC("?tc")
__failure __msg("BPF_EXIT instruction in main prog cannot be used inside bpf_local_irq_save-ed region")
int irq_restore_missing_2_subprog(struct __sk_buff *ctx)
{
	unsigned long flags1;
	unsigned long flags2;

	local_irq_save(&flags1);
	local_irq_save(&flags2);
	return 0;
}

SEC("?tc")
__failure __msg("BPF_EXIT instruction in main prog cannot be used inside bpf_local_irq_save-ed region")
int irq_restore_missing_3_subprog(struct __sk_buff *ctx)
{
	unsigned long flags1;
	unsigned long flags2;
	unsigned long flags3;

	local_irq_save(&flags1);
	local_irq_save(&flags2);
	local_irq_save(&flags3);
	return 0;
}

SEC("?tc")
__failure __msg("BPF_EXIT instruction in main prog cannot be used inside bpf_local_irq_save-ed region")
int irq_restore_missing_3_minus_2_subprog(struct __sk_buff *ctx)
{
	unsigned long flags1;
	unsigned long flags2;
	unsigned long flags3;

	local_irq_save(&flags1);
	local_irq_save(&flags2);
	local_irq_save(&flags3);
	local_irq_restore(&flags3);
	local_irq_restore(&flags2);
	return 0;
}

SEC("?tc")
__success
int irq_balance(struct __sk_buff *ctx)
{
	unsigned long flags;

	local_irq_save(&flags);
	local_irq_restore(&flags);
	return 0;
}

SEC("?tc")
__success
int irq_balance_n(struct __sk_buff *ctx)
{
	unsigned long flags1;
	unsigned long flags2;
	unsigned long flags3;

	local_irq_save(&flags1);
	local_irq_save(&flags2);
	local_irq_save(&flags3);
	local_irq_restore(&flags3);
	local_irq_restore(&flags2);
	local_irq_restore(&flags1);
	return 0;
}

static __noinline void local_irq_balance(void)
{
	unsigned long flags;

	local_irq_save(&flags);
	local_irq_restore(&flags);
}

static __noinline void local_irq_balance_n(void)
{
	unsigned long flags1;
	unsigned long flags2;
	unsigned long flags3;

	local_irq_save(&flags1);
	local_irq_save(&flags2);
	local_irq_save(&flags3);
	local_irq_restore(&flags3);
	local_irq_restore(&flags2);
	local_irq_restore(&flags1);
}

SEC("?tc")
__success
int irq_balance_subprog(struct __sk_buff *ctx)
{
	local_irq_balance();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__failure __msg("sleepable helper bpf_copy_from_user#")
int irq_sleepable_helper(void *ctx)
{
	unsigned long flags;
	u32 data;

	local_irq_save(&flags);
	bpf_copy_from_user(&data, sizeof(data), NULL);
	local_irq_restore(&flags);
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__failure __msg("kernel func bpf_copy_from_user_str is sleepable within IRQ-disabled region")
int irq_sleepable_kfunc(void *ctx)
{
	unsigned long flags;
	u32 data;

	local_irq_save(&flags);
	bpf_copy_from_user_str(&data, sizeof(data), NULL, 0);
	local_irq_restore(&flags);
	return 0;
}

int __noinline global_local_irq_balance(void)
{
	local_irq_balance_n();
	return 0;
}

SEC("?tc")
__success
int irq_global_subprog(struct __sk_buff *ctx)
{
	unsigned long flags;

	bpf_local_irq_save(&flags);
	global_local_irq_balance();
	bpf_local_irq_restore(&flags);
	return 0;
}

SEC("?tc")
__failure __msg("cannot restore irq state out of order")
int irq_restore_ooo(struct __sk_buff *ctx)
{
	unsigned long flags1;
	unsigned long flags2;

	bpf_local_irq_save(&flags1);
	bpf_local_irq_save(&flags2);
	bpf_local_irq_restore(&flags1);
	bpf_local_irq_restore(&flags2);
	return 0;
}

SEC("?tc")
__failure __msg("cannot restore irq state out of order")
int irq_restore_ooo_3(struct __sk_buff *ctx)
{
	unsigned long flags1;
	unsigned long flags2;
	unsigned long flags3;

	bpf_local_irq_save(&flags1);
	bpf_local_irq_save(&flags2);
	bpf_local_irq_restore(&flags2);
	bpf_local_irq_save(&flags3);
	bpf_local_irq_restore(&flags1);
	bpf_local_irq_restore(&flags3);
	return 0;
}

static __noinline void local_irq_save_3(unsigned long *flags1, unsigned long *flags2,
					unsigned long *flags3)
{
	local_irq_save(flags1);
	local_irq_save(flags2);
	local_irq_save(flags3);
}

SEC("?tc")
__success
int irq_restore_3_subprog(struct __sk_buff *ctx)
{
	unsigned long flags1;
	unsigned long flags2;
	unsigned long flags3;

	local_irq_save_3(&flags1, &flags2, &flags3);
	bpf_local_irq_restore(&flags3);
	bpf_local_irq_restore(&flags2);
	bpf_local_irq_restore(&flags1);
	return 0;
}

SEC("?tc")
__failure __msg("cannot restore irq state out of order")
int irq_restore_4_subprog(struct __sk_buff *ctx)
{
	unsigned long flags1;
	unsigned long flags2;
	unsigned long flags3;
	unsigned long flags4;

	local_irq_save_3(&flags1, &flags2, &flags3);
	bpf_local_irq_restore(&flags3);
	bpf_local_irq_save(&flags4);
	bpf_local_irq_restore(&flags4);
	bpf_local_irq_restore(&flags1);
	return 0;
}

SEC("?tc")
__failure __msg("cannot restore irq state out of order")
int irq_restore_ooo_3_subprog(struct __sk_buff *ctx)
{
	unsigned long flags1;
	unsigned long flags2;
	unsigned long flags3;

	local_irq_save_3(&flags1, &flags2, &flags3);
	bpf_local_irq_restore(&flags3);
	bpf_local_irq_restore(&flags2);
	bpf_local_irq_save(&flags3);
	bpf_local_irq_restore(&flags1);
	return 0;
}

SEC("?tc")
__failure __msg("expected an initialized")
int irq_restore_invalid(struct __sk_buff *ctx)
{
	unsigned long flags1;
	unsigned long flags = 0xfaceb00c;

	bpf_local_irq_save(&flags1);
	bpf_local_irq_restore(&flags);
	return 0;
}

SEC("?tc")
__failure __msg("expected uninitialized")
int irq_save_invalid(struct __sk_buff *ctx)
{
	unsigned long flags1;

	bpf_local_irq_save(&flags1);
	bpf_local_irq_save(&flags1);
	return 0;
}

SEC("?tc")
__failure __msg("expected an initialized")
int irq_restore_iter(struct __sk_buff *ctx)
{
	struct bpf_iter_num it;

	bpf_iter_num_new(&it, 0, 42);
	bpf_local_irq_restore((unsigned long *)&it);
	return 0;
}

SEC("?tc")
__failure __msg("Unreleased reference id=1")
int irq_save_iter(struct __sk_buff *ctx)
{
	struct bpf_iter_num it;

	/* Ensure same sized slot has st->ref_obj_id set, so we reject based on
	 * slot_type != STACK_IRQ_FLAG...
	 */
	_Static_assert(sizeof(it) == sizeof(unsigned long), "broken iterator size");

	bpf_iter_num_new(&it, 0, 42);
	bpf_local_irq_save((unsigned long *)&it);
	bpf_local_irq_restore((unsigned long *)&it);
	return 0;
}

SEC("?tc")
__failure __msg("expected an initialized")
int irq_flag_overwrite(struct __sk_buff *ctx)
{
	unsigned long flags;

	bpf_local_irq_save(&flags);
	flags = 0xdeadbeef;
	bpf_local_irq_restore(&flags);
	return 0;
}

SEC("?tc")
__failure __msg("expected an initialized")
int irq_flag_overwrite_partial(struct __sk_buff *ctx)
{
	unsigned long flags;

	bpf_local_irq_save(&flags);
	*(((char *)&flags) + 1) = 0xff;
	bpf_local_irq_restore(&flags);
	return 0;
}

SEC("?tc")
__failure __msg("cannot restore irq state out of order")
int irq_ooo_refs_array(struct __sk_buff *ctx)
{
	unsigned long flags[4];
	struct { int i; } *p;

	/* refs=1 */
	bpf_local_irq_save(&flags[0]);

	/* refs=1,2 */
	p = bpf_obj_new(typeof(*p));
	if (!p) {
		bpf_local_irq_restore(&flags[0]);
		return 0;
	}

	/* refs=1,2,3 */
	bpf_local_irq_save(&flags[1]);

	/* refs=1,2,3,4 */
	bpf_local_irq_save(&flags[2]);

	/* Now when we remove ref=2, the verifier must not break the ordering in
	 * the refs array between 1,3,4. With an older implementation, the
	 * verifier would swap the last element with the removed element, but to
	 * maintain the stack property we need to use memmove.
	 */
	bpf_obj_drop(p);

	/* Save and restore to reset active_irq_id to 3, as the ordering is now
	 * refs=1,4,3. When restoring the linear scan will find prev_id in order
	 * as 3 instead of 4.
	 */
	bpf_local_irq_save(&flags[3]);
	bpf_local_irq_restore(&flags[3]);

	/* With the incorrect implementation, we can release flags[1], flags[2],
	 * and flags[0], i.e. in the wrong order.
	 */
	bpf_local_irq_restore(&flags[1]);
	bpf_local_irq_restore(&flags[2]);
	bpf_local_irq_restore(&flags[0]);
	return 0;
}

int __noinline
global_subprog(int i)
{
	if (i)
		bpf_printk("%p", &i);
	return i;
}

int __noinline
global_sleepable_helper_subprog(int i)
{
	if (i)
		bpf_copy_from_user(&i, sizeof(i), NULL);
	return i;
}

int __noinline
global_sleepable_kfunc_subprog(int i)
{
	if (i)
		bpf_copy_from_user_str(&i, sizeof(i), NULL, 0);
	global_subprog(i);
	return i;
}

int __noinline
global_subprog_calling_sleepable_global(int i)
{
	if (!i)
		global_sleepable_kfunc_subprog(i);
	return i;
}

SEC("?syscall")
__success
int irq_non_sleepable_global_subprog(void *ctx)
{
	unsigned long flags;

	bpf_local_irq_save(&flags);
	global_subprog(0);
	bpf_local_irq_restore(&flags);
	return 0;
}

SEC("?syscall")
__failure __msg("global functions that may sleep are not allowed in non-sleepable context")
int irq_sleepable_helper_global_subprog(void *ctx)
{
	unsigned long flags;

	bpf_local_irq_save(&flags);
	global_sleepable_helper_subprog(0);
	bpf_local_irq_restore(&flags);
	return 0;
}

SEC("?syscall")
__failure __msg("global functions that may sleep are not allowed in non-sleepable context")
int irq_sleepable_global_subprog_indirect(void *ctx)
{
	unsigned long flags;

	bpf_local_irq_save(&flags);
	global_subprog_calling_sleepable_global(0);
	bpf_local_irq_restore(&flags);
	return 0;
}

SEC("?tc")
__failure __msg("cannot restore irq state out of order")
int irq_ooo_lock_cond_inv(struct __sk_buff *ctx)
{
	unsigned long flags1, flags2;

	if (bpf_res_spin_lock_irqsave(&lockA, &flags1))
		return 0;
	if (bpf_res_spin_lock_irqsave(&lockB, &flags2)) {
		bpf_res_spin_unlock_irqrestore(&lockA, &flags1);
		return 0;
	}

	bpf_res_spin_unlock_irqrestore(&lockB, &flags1);
	bpf_res_spin_unlock_irqrestore(&lockA, &flags2);
	return 0;
}

SEC("?tc")
__failure __msg("function calls are not allowed")
int irq_wrong_kfunc_class_1(struct __sk_buff *ctx)
{
	unsigned long flags1;

	if (bpf_res_spin_lock_irqsave(&lockA, &flags1))
		return 0;
	/* For now, bpf_local_irq_restore is not allowed in critical section,
	 * but this test ensures error will be caught with kfunc_class when it's
	 * opened up. Tested by temporarily permitting this kfunc in critical
	 * section.
	 */
	bpf_local_irq_restore(&flags1);
	bpf_res_spin_unlock_irqrestore(&lockA, &flags1);
	return 0;
}

SEC("?tc")
__failure __msg("function calls are not allowed")
int irq_wrong_kfunc_class_2(struct __sk_buff *ctx)
{
	unsigned long flags1, flags2;

	bpf_local_irq_save(&flags1);
	if (bpf_res_spin_lock_irqsave(&lockA, &flags2))
		return 0;
	bpf_local_irq_restore(&flags2);
	bpf_res_spin_unlock_irqrestore(&lockA, &flags1);
	return 0;
}

char _license[] SEC("license") = "GPL";
