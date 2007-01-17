#ifndef _ASM_KPROBES_H
#define _ASM_KPROBES_H
/*
 *  Kernel Probes (KProbes)
 *  include/asm-ia64/kprobes.h
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
 * Copyright (C) Intel Corporation, 2005
 *
 * 2005-Apr     Rusty Lynch <rusty.lynch@intel.com> and Anil S Keshavamurthy
 *              <anil.s.keshavamurthy@intel.com> adapted from i386
 */
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/percpu.h>
#include <asm/break.h>

#define __ARCH_WANT_KPROBES_INSN_SLOT
#define MAX_INSN_SIZE   1
#define BREAK_INST	(long)(__IA64_BREAK_KPROBE << 6)

typedef union cmp_inst {
	struct {
	unsigned long long qp : 6;
	unsigned long long p1 : 6;
	unsigned long long c  : 1;
	unsigned long long r2 : 7;
	unsigned long long r3 : 7;
	unsigned long long p2 : 6;
	unsigned long long ta : 1;
	unsigned long long x2 : 2;
	unsigned long long tb : 1;
	unsigned long long opcode : 4;
	unsigned long long reserved : 23;
	}f;
	unsigned long long l;
} cmp_inst_t;

struct kprobe;

typedef struct _bundle {
	struct {
		unsigned long long template : 5;
		unsigned long long slot0 : 41;
		unsigned long long slot1_p0 : 64-46;
	} quad0;
	struct {
		unsigned long long slot1_p1 : 41 - (64-46);
		unsigned long long slot2 : 41;
	} quad1;
} __attribute__((__aligned__(16)))  bundle_t;

struct prev_kprobe {
	struct kprobe *kp;
	unsigned long status;
};

#define	MAX_PARAM_RSE_SIZE	(0x60+0x60/0x3f)
/* per-cpu kprobe control block */
struct kprobe_ctlblk {
	unsigned long kprobe_status;
	struct pt_regs jprobe_saved_regs;
	unsigned long jprobes_saved_stacked_regs[MAX_PARAM_RSE_SIZE];
	unsigned long *bsp;
	unsigned long cfm;
	struct prev_kprobe prev_kprobe;
};

#define JPROBE_ENTRY(pentry)	(kprobe_opcode_t *)pentry

#define ARCH_SUPPORTS_KRETPROBES
#define  ARCH_INACTIVE_KPROBE_COUNT 1

#define SLOT0_OPCODE_SHIFT	(37)
#define SLOT1_p1_OPCODE_SHIFT	(37 - (64-46))
#define SLOT2_OPCODE_SHIFT 	(37)

#define INDIRECT_CALL_OPCODE		(1)
#define IP_RELATIVE_CALL_OPCODE		(5)
#define IP_RELATIVE_BRANCH_OPCODE	(4)
#define IP_RELATIVE_PREDICT_OPCODE	(7)
#define LONG_BRANCH_OPCODE		(0xC)
#define LONG_CALL_OPCODE		(0xD)
#define flush_insn_slot(p)		do { } while (0)

typedef struct kprobe_opcode {
	bundle_t bundle;
} kprobe_opcode_t;

struct fnptr {
	unsigned long ip;
	unsigned long gp;
};

/* Architecture specific copy of original instruction*/
struct arch_specific_insn {
	/* copy of the instruction to be emulated */
	kprobe_opcode_t *insn;
 #define INST_FLAG_FIX_RELATIVE_IP_ADDR		1
 #define INST_FLAG_FIX_BRANCH_REG		2
 #define INST_FLAG_BREAK_INST			4
 	unsigned long inst_flag;
 	unsigned short target_br_reg;
	unsigned short slot;
};

extern int kprobe_exceptions_notify(struct notifier_block *self,
				    unsigned long val, void *data);

/* ia64 does not need this */
static inline void jprobe_return(void)
{
}
extern void invalidate_stacked_regs(void);
extern void flush_register_stack(void);
extern void arch_remove_kprobe(struct kprobe *p);

#endif				/* _ASM_KPROBES_H */
