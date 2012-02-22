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
 * Copyright (C) IBM Corporation, 2008-2011
 * Authors:
 *	Srikar Dronamraju
 *	Jim Keniston
 */

#include <linux/errno.h>
#include <linux/rbtree.h>

struct vm_area_struct;
#ifdef CONFIG_ARCH_SUPPORTS_UPROBES
#include <asm/uprobes.h>
#endif

/* flags that denote/change uprobes behaviour */

/* Have a copy of original instruction */
#define UPROBES_COPY_INSN	0x1

/* Dont run handlers when first register/ last unregister in progress*/
#define UPROBES_RUN_HANDLER	0x2

struct uprobe_consumer {
	int (*handler)(struct uprobe_consumer *self, struct pt_regs *regs);
	/*
	 * filter is optional; If a filter exists, handler is run
	 * if and only if filter returns true.
	 */
	bool (*filter)(struct uprobe_consumer *self, struct task_struct *task);

	struct uprobe_consumer *next;
};

#ifdef CONFIG_UPROBES
extern int __weak set_bkpt(struct mm_struct *mm, struct arch_uprobe *auprobe, unsigned long vaddr);
extern int __weak set_orig_insn(struct mm_struct *mm, struct arch_uprobe *auprobe, unsigned long vaddr, bool verify);
extern bool __weak is_bkpt_insn(uprobe_opcode_t *insn);
extern int uprobe_register(struct inode *inode, loff_t offset, struct uprobe_consumer *consumer);
extern void uprobe_unregister(struct inode *inode, loff_t offset, struct uprobe_consumer *consumer);
extern int uprobe_mmap(struct vm_area_struct *vma);
#else /* CONFIG_UPROBES is not defined */
static inline int
uprobe_register(struct inode *inode, loff_t offset, struct uprobe_consumer *consumer)
{
	return -ENOSYS;
}
static inline void
uprobe_unregister(struct inode *inode, loff_t offset, struct uprobe_consumer *consumer)
{
}
static inline int uprobe_mmap(struct vm_area_struct *vma)
{
	return 0;
}
#endif /* CONFIG_UPROBES */
#endif	/* _LINUX_UPROBES_H */
