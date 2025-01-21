/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_UPROBES_H
#define _LINUX_UPROBES_H
/*
 * User-space Probes (UProbes)
 *
 * Copyright (C) IBM Corporation, 2008-2012
 * Authors:
 *	Srikar Dronamraju
 *	Jim Keniston
 * Copyright (C) 2011-2012 Red Hat, Inc., Peter Zijlstra
 */

#include <linux/errno.h>
#include <linux/rbtree.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/timer.h>

struct uprobe;
struct vm_area_struct;
struct mm_struct;
struct inode;
struct notifier_block;
struct page;

/*
 * Allowed return values from uprobe consumer's handler callback
 * with following meaning:
 *
 * UPROBE_HANDLER_REMOVE
 * - Remove the uprobe breakpoint from current->mm.
 * UPROBE_HANDLER_IGNORE
 * - Ignore ret_handler callback for this consumer.
 */
#define UPROBE_HANDLER_REMOVE		1
#define UPROBE_HANDLER_IGNORE		2

#define MAX_URETPROBE_DEPTH		64

struct uprobe_consumer {
	/*
	 * handler() can return UPROBE_HANDLER_REMOVE to signal the need to
	 * unregister uprobe for current process. If UPROBE_HANDLER_REMOVE is
	 * returned, filter() callback has to be implemented as well and it
	 * should return false to "confirm" the decision to uninstall uprobe
	 * for the current process. If filter() is omitted or returns true,
	 * UPROBE_HANDLER_REMOVE is effectively ignored.
	 */
	int (*handler)(struct uprobe_consumer *self, struct pt_regs *regs, __u64 *data);
	int (*ret_handler)(struct uprobe_consumer *self,
				unsigned long func,
				struct pt_regs *regs, __u64 *data);
	bool (*filter)(struct uprobe_consumer *self, struct mm_struct *mm);

	struct list_head cons_node;

	__u64 id;	/* set when uprobe_consumer is registered */
};

#ifdef CONFIG_UPROBES
#include <asm/uprobes.h>

enum uprobe_task_state {
	UTASK_RUNNING,
	UTASK_SSTEP,
	UTASK_SSTEP_ACK,
	UTASK_SSTEP_TRAPPED,
};

/* The state of hybrid-lifetime uprobe inside struct return_instance */
enum hprobe_state {
	HPROBE_LEASED,		/* uretprobes_srcu-protected uprobe */
	HPROBE_STABLE,		/* refcounted uprobe */
	HPROBE_GONE,		/* NULL uprobe, SRCU expired, refcount failed */
	HPROBE_CONSUMED,	/* uprobe "consumed" by uretprobe handler */
};

/*
 * Hybrid lifetime uprobe. Represents a uprobe instance that could be either
 * SRCU protected (with SRCU protection eventually potentially timing out),
 * refcounted using uprobe->ref, or there could be no valid uprobe (NULL).
 *
 * hprobe's internal state is setup such that background timer thread can
 * atomically "downgrade" temporarily RCU-protected uprobe into refcounted one
 * (or no uprobe, if refcounting failed).
 *
 * *stable* pointer always point to the uprobe (or could be NULL if there is
 * was no valid underlying uprobe to begin with).
 *
 * *leased* pointer is the key to achieving race-free atomic lifetime state
 * transition and can have three possible states:
 *   - either the same non-NULL value as *stable*, in which case uprobe is
 *     SRCU-protected;
 *   - NULL, in which case uprobe (if there is any) is refcounted;
 *   - special __UPROBE_DEAD value, which represents an uprobe that was SRCU
 *     protected initially, but SRCU period timed out and we attempted to
 *     convert it to refcounted, but refcount_inc_not_zero() failed, because
 *     uprobe effectively went away (the last consumer unsubscribed). In this
 *     case it's important to know that *stable* pointer (which still has
 *     non-NULL uprobe pointer) shouldn't be used, because lifetime of
 *     underlying uprobe is not guaranteed anymore. __UPROBE_DEAD is just an
 *     internal marker and is handled transparently by hprobe_fetch() helper.
 *
 * When uprobe is SRCU-protected, we also record srcu_idx value, necessary for
 * SRCU unlocking.
 *
 * See hprobe_expire() and hprobe_fetch() for details of race-free uprobe
 * state transitioning details. It all hinges on atomic xchg() over *leaded*
 * pointer. *stable* pointer, once initially set, is not modified concurrently.
 */
struct hprobe {
	enum hprobe_state state;
	int srcu_idx;
	struct uprobe *uprobe;
};

/*
 * uprobe_task: Metadata of a task while it singlesteps.
 */
struct uprobe_task {
	enum uprobe_task_state		state;

	unsigned int			depth;
	struct return_instance		*return_instances;

	union {
		struct {
			struct arch_uprobe_task	autask;
			unsigned long		vaddr;
		};

		struct {
			struct callback_head	dup_xol_work;
			unsigned long		dup_xol_addr;
		};
	};

	struct uprobe			*active_uprobe;
	struct timer_list		ri_timer;
	unsigned long			xol_vaddr;

	struct arch_uprobe              *auprobe;
};

struct return_consumer {
	__u64	cookie;
	__u64	id;
};

struct return_instance {
	struct hprobe		hprobe;
	unsigned long		func;
	unsigned long		stack;		/* stack pointer */
	unsigned long		orig_ret_vaddr; /* original return address */
	bool			chained;	/* true, if instance is nested */
	int			consumers_cnt;

	struct return_instance	*next;		/* keep as stack */
	struct rcu_head		rcu;

	struct return_consumer	consumers[] __counted_by(consumers_cnt);
} ____cacheline_aligned;

enum rp_check {
	RP_CHECK_CALL,
	RP_CHECK_CHAIN_CALL,
	RP_CHECK_RET,
};

struct xol_area;

struct uprobes_state {
	struct xol_area		*xol_area;
};

extern void __init uprobes_init(void);
extern int set_swbp(struct arch_uprobe *aup, struct mm_struct *mm, unsigned long vaddr);
extern int set_orig_insn(struct arch_uprobe *aup, struct mm_struct *mm, unsigned long vaddr);
extern bool is_swbp_insn(uprobe_opcode_t *insn);
extern bool is_trap_insn(uprobe_opcode_t *insn);
extern unsigned long uprobe_get_swbp_addr(struct pt_regs *regs);
extern unsigned long uprobe_get_trap_addr(struct pt_regs *regs);
extern int uprobe_write_opcode(struct arch_uprobe *auprobe, struct mm_struct *mm, unsigned long vaddr, uprobe_opcode_t);
extern struct uprobe *uprobe_register(struct inode *inode, loff_t offset, loff_t ref_ctr_offset, struct uprobe_consumer *uc);
extern int uprobe_apply(struct uprobe *uprobe, struct uprobe_consumer *uc, bool);
extern void uprobe_unregister_nosync(struct uprobe *uprobe, struct uprobe_consumer *uc);
extern void uprobe_unregister_sync(void);
extern int uprobe_mmap(struct vm_area_struct *vma);
extern void uprobe_munmap(struct vm_area_struct *vma, unsigned long start, unsigned long end);
extern void uprobe_start_dup_mmap(void);
extern void uprobe_end_dup_mmap(void);
extern void uprobe_dup_mmap(struct mm_struct *oldmm, struct mm_struct *newmm);
extern void uprobe_free_utask(struct task_struct *t);
extern void uprobe_copy_process(struct task_struct *t, unsigned long flags);
extern int uprobe_post_sstep_notifier(struct pt_regs *regs);
extern int uprobe_pre_sstep_notifier(struct pt_regs *regs);
extern void uprobe_notify_resume(struct pt_regs *regs);
extern bool uprobe_deny_signal(void);
extern bool arch_uprobe_skip_sstep(struct arch_uprobe *aup, struct pt_regs *regs);
extern void uprobe_clear_state(struct mm_struct *mm);
extern int  arch_uprobe_analyze_insn(struct arch_uprobe *aup, struct mm_struct *mm, unsigned long addr);
extern int  arch_uprobe_pre_xol(struct arch_uprobe *aup, struct pt_regs *regs);
extern int  arch_uprobe_post_xol(struct arch_uprobe *aup, struct pt_regs *regs);
extern bool arch_uprobe_xol_was_trapped(struct task_struct *tsk);
extern int  arch_uprobe_exception_notify(struct notifier_block *self, unsigned long val, void *data);
extern void arch_uprobe_abort_xol(struct arch_uprobe *aup, struct pt_regs *regs);
extern unsigned long arch_uretprobe_hijack_return_addr(unsigned long trampoline_vaddr, struct pt_regs *regs);
extern bool arch_uretprobe_is_alive(struct return_instance *ret, enum rp_check ctx, struct pt_regs *regs);
extern bool arch_uprobe_ignore(struct arch_uprobe *aup, struct pt_regs *regs);
extern void arch_uprobe_copy_ixol(struct page *page, unsigned long vaddr,
					 void *src, unsigned long len);
extern void uprobe_handle_trampoline(struct pt_regs *regs);
extern void *arch_uprobe_trampoline(unsigned long *psize);
extern unsigned long uprobe_get_trampoline_vaddr(void);
#else /* !CONFIG_UPROBES */
struct uprobes_state {
};

static inline void uprobes_init(void)
{
}

#define uprobe_get_trap_addr(regs)	instruction_pointer(regs)

static inline struct uprobe *
uprobe_register(struct inode *inode, loff_t offset, loff_t ref_ctr_offset, struct uprobe_consumer *uc)
{
	return ERR_PTR(-ENOSYS);
}
static inline int
uprobe_apply(struct uprobe* uprobe, struct uprobe_consumer *uc, bool add)
{
	return -ENOSYS;
}
static inline void
uprobe_unregister_nosync(struct uprobe *uprobe, struct uprobe_consumer *uc)
{
}
static inline void uprobe_unregister_sync(void)
{
}
static inline int uprobe_mmap(struct vm_area_struct *vma)
{
	return 0;
}
static inline void
uprobe_munmap(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
}
static inline void uprobe_start_dup_mmap(void)
{
}
static inline void uprobe_end_dup_mmap(void)
{
}
static inline void
uprobe_dup_mmap(struct mm_struct *oldmm, struct mm_struct *newmm)
{
}
static inline void uprobe_notify_resume(struct pt_regs *regs)
{
}
static inline bool uprobe_deny_signal(void)
{
	return false;
}
static inline void uprobe_free_utask(struct task_struct *t)
{
}
static inline void uprobe_copy_process(struct task_struct *t, unsigned long flags)
{
}
static inline void uprobe_clear_state(struct mm_struct *mm)
{
}
#endif /* !CONFIG_UPROBES */
#endif	/* _LINUX_UPROBES_H */
