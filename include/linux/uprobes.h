#ifndef _LINUX_UPROBES_H
#define _LINUX_UPROBES_H
/*
 * User-space Probes (UProbes)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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

struct vm_area_struct;
struct mm_struct;
struct inode;
struct notifier_block;
struct page;

#define UPROBE_HANDLER_REMOVE		1
#define UPROBE_HANDLER_MASK		1

#define MAX_URETPROBE_DEPTH		64

enum uprobe_filter_ctx {
	UPROBE_FILTER_REGISTER,
	UPROBE_FILTER_UNREGISTER,
	UPROBE_FILTER_MMAP,
};

struct uprobe_consumer {
	int (*handler)(struct uprobe_consumer *self, struct pt_regs *regs);
	int (*ret_handler)(struct uprobe_consumer *self,
				unsigned long func,
				struct pt_regs *regs);
	bool (*filter)(struct uprobe_consumer *self,
				enum uprobe_filter_ctx ctx,
				struct mm_struct *mm);

	struct uprobe_consumer *next;
};

#ifdef CONFIG_UPROBES
#include <asm/uprobes.h>

enum uprobe_task_state {
	UTASK_RUNNING,
	UTASK_SSTEP,
	UTASK_SSTEP_ACK,
	UTASK_SSTEP_TRAPPED,
};

/*
 * uprobe_task: Metadata of a task while it singlesteps.
 */
struct uprobe_task {
	enum uprobe_task_state		state;

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
	unsigned long			xol_vaddr;

	struct return_instance		*return_instances;
	unsigned int			depth;
};

struct return_instance {
	struct uprobe		*uprobe;
	unsigned long		func;
	unsigned long		stack;		/* stack pointer */
	unsigned long		orig_ret_vaddr; /* original return address */
	bool			chained;	/* true, if instance is nested */

	struct return_instance	*next;		/* keep as stack */
};

enum rp_check {
	RP_CHECK_CALL,
	RP_CHECK_CHAIN_CALL,
	RP_CHECK_RET,
};

struct xol_area;

struct uprobes_state {
	struct xol_area		*xol_area;
};

extern int set_swbp(struct arch_uprobe *aup, struct mm_struct *mm, unsigned long vaddr);
extern int set_orig_insn(struct arch_uprobe *aup, struct mm_struct *mm, unsigned long vaddr);
extern bool is_swbp_insn(uprobe_opcode_t *insn);
extern bool is_trap_insn(uprobe_opcode_t *insn);
extern unsigned long uprobe_get_swbp_addr(struct pt_regs *regs);
extern unsigned long uprobe_get_trap_addr(struct pt_regs *regs);
extern int uprobe_write_opcode(struct arch_uprobe *auprobe, struct mm_struct *mm, unsigned long vaddr, uprobe_opcode_t);
extern int uprobe_register(struct inode *inode, loff_t offset, struct uprobe_consumer *uc);
extern int uprobe_apply(struct inode *inode, loff_t offset, struct uprobe_consumer *uc, bool);
extern void uprobe_unregister(struct inode *inode, loff_t offset, struct uprobe_consumer *uc);
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
#else /* !CONFIG_UPROBES */
struct uprobes_state {
};

#define uprobe_get_trap_addr(regs)	instruction_pointer(regs)

static inline int
uprobe_register(struct inode *inode, loff_t offset, struct uprobe_consumer *uc)
{
	return -ENOSYS;
}
static inline int
uprobe_apply(struct inode *inode, loff_t offset, struct uprobe_consumer *uc, bool add)
{
	return -ENOSYS;
}
static inline void
uprobe_unregister(struct inode *inode, loff_t offset, struct uprobe_consumer *uc)
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
