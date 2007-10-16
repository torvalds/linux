#ifndef _ASM_KPROBES_H
#define _ASM_KPROBES_H
/*
 *  Kernel Probes (KProbes)
 *  include/asm-x86_64/kprobes.h
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
 * Copyright (C) IBM Corporation, 2002, 2004
 *
 * 2004-Oct	Prasanna S Panchamukhi <prasanna@in.ibm.com> and Jim Keniston
 *		kenistoj@us.ibm.com adopted from i386.
 */
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/percpu.h>

#define  __ARCH_WANT_KPROBES_INSN_SLOT

struct pt_regs;
struct kprobe;

typedef u8 kprobe_opcode_t;
#define BREAKPOINT_INSTRUCTION	0xcc
#define MAX_INSN_SIZE 15
#define MAX_STACK_SIZE 64
#define MIN_STACK_SIZE(ADDR) (((MAX_STACK_SIZE) < \
	(((unsigned long)current_thread_info()) + THREAD_SIZE - (ADDR))) \
	? (MAX_STACK_SIZE) \
	: (((unsigned long)current_thread_info()) + THREAD_SIZE - (ADDR)))

#define ARCH_SUPPORTS_KRETPROBES

void kretprobe_trampoline(void);
extern void arch_remove_kprobe(struct kprobe *p);
#define flush_insn_slot(p)	do { } while (0)

/* Architecture specific copy of original instruction*/
struct arch_specific_insn {
	/* copy of the original instruction */
	kprobe_opcode_t *insn;
};

struct prev_kprobe {
	struct kprobe *kp;
	unsigned long status;
	unsigned long old_rflags;
	unsigned long saved_rflags;
};

/* per-cpu kprobe control block */
struct kprobe_ctlblk {
	unsigned long kprobe_status;
	unsigned long kprobe_old_rflags;
	unsigned long kprobe_saved_rflags;
	long *jprobe_saved_rsp;
	struct pt_regs jprobe_saved_regs;
	kprobe_opcode_t jprobes_stack[MAX_STACK_SIZE];
	struct prev_kprobe prev_kprobe;
};

/* trap3/1 are intr gates for kprobes.  So, restore the status of IF,
 * if necessary, before executing the original int3/1 (trap) handler.
 */
static inline void restore_interrupts(struct pt_regs *regs)
{
	if (regs->eflags & IF_MASK)
		local_irq_enable();
}

extern int post_kprobe_handler(struct pt_regs *regs);
extern int kprobe_fault_handler(struct pt_regs *regs, int trapnr);
extern int kprobe_handler(struct pt_regs *regs);

extern int kprobe_exceptions_notify(struct notifier_block *self,
				    unsigned long val, void *data);
#endif				/* _ASM_KPROBES_H */
