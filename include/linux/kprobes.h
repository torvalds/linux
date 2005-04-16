#ifndef _LINUX_KPROBES_H
#define _LINUX_KPROBES_H
/*
 *  Kernel Probes (KProbes)
 *  include/linux/kprobes.h
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
 * 2002-Oct	Created by Vamsi Krishna S <vamsi_krishna@in.ibm.com> Kernel
 *		Probes initial implementation ( includes suggestions from
 *		Rusty Russell).
 * 2004-July	Suparna Bhattacharya <suparna@in.ibm.com> added jumper probes
 *		interface to access function arguments.
 */
#include <linux/config.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <asm/kprobes.h>

struct kprobe;
struct pt_regs;
typedef int (*kprobe_pre_handler_t) (struct kprobe *, struct pt_regs *);
typedef int (*kprobe_break_handler_t) (struct kprobe *, struct pt_regs *);
typedef void (*kprobe_post_handler_t) (struct kprobe *, struct pt_regs *,
				       unsigned long flags);
typedef int (*kprobe_fault_handler_t) (struct kprobe *, struct pt_regs *,
				       int trapnr);
struct kprobe {
	struct hlist_node hlist;

	/* location of the probe point */
	kprobe_opcode_t *addr;

	/* Called before addr is executed. */
	kprobe_pre_handler_t pre_handler;

	/* Called after addr is executed, unless... */
	kprobe_post_handler_t post_handler;

	/* ... called if executing addr causes a fault (eg. page fault).
	 * Return 1 if it handled fault, otherwise kernel will see it. */
	kprobe_fault_handler_t fault_handler;

	/* ... called if breakpoint trap occurs in probe handler.
	 * Return 1 if it handled break, otherwise kernel will see it. */
	kprobe_break_handler_t break_handler;

	/* Saved opcode (which has been replaced with breakpoint) */
	kprobe_opcode_t opcode;

	/* copy of the original instruction */
	struct arch_specific_insn ainsn;
};

/*
 * Special probe type that uses setjmp-longjmp type tricks to resume
 * execution at a specified entry with a matching prototype corresponding
 * to the probed function - a trick to enable arguments to become
 * accessible seamlessly by probe handling logic.
 * Note:
 * Because of the way compilers allocate stack space for local variables
 * etc upfront, regardless of sub-scopes within a function, this mirroring
 * principle currently works only for probes placed on function entry points.
 */
struct jprobe {
	struct kprobe kp;
	kprobe_opcode_t *entry;	/* probe handling code to jump to */
};

#ifdef CONFIG_KPROBES
/* Locks kprobe: irq must be disabled */
void lock_kprobes(void);
void unlock_kprobes(void);

/* kprobe running now on this CPU? */
static inline int kprobe_running(void)
{
	extern unsigned int kprobe_cpu;
	return kprobe_cpu == smp_processor_id();
}

extern int arch_prepare_kprobe(struct kprobe *p);
extern void arch_copy_kprobe(struct kprobe *p);
extern void arch_remove_kprobe(struct kprobe *p);
extern void show_registers(struct pt_regs *regs);

/* Get the kprobe at this addr (if any).  Must have called lock_kprobes */
struct kprobe *get_kprobe(void *addr);

int register_kprobe(struct kprobe *p);
void unregister_kprobe(struct kprobe *p);
int setjmp_pre_handler(struct kprobe *, struct pt_regs *);
int longjmp_break_handler(struct kprobe *, struct pt_regs *);
int register_jprobe(struct jprobe *p);
void unregister_jprobe(struct jprobe *p);
void jprobe_return(void);

#else
static inline int kprobe_running(void)
{
	return 0;
}
static inline int register_kprobe(struct kprobe *p)
{
	return -ENOSYS;
}
static inline void unregister_kprobe(struct kprobe *p)
{
}
static inline int register_jprobe(struct jprobe *p)
{
	return -ENOSYS;
}
static inline void unregister_jprobe(struct jprobe *p)
{
}
static inline void jprobe_return(void)
{
}
#endif
#endif				/* _LINUX_KPROBES_H */
