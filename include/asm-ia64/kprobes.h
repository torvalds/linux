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
#include <asm/break.h>

#define BREAK_INST	(long)(__IA64_BREAK_KPROBE << 6)

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

#define JPROBE_ENTRY(pentry)	(kprobe_opcode_t *)pentry

typedef struct kprobe_opcode {
	bundle_t bundle;
} kprobe_opcode_t;

struct fnptr {
	unsigned long ip;
	unsigned long gp;
};

/* Architecture specific copy of original instruction*/
struct arch_specific_insn {
	/* copy of the original instruction */
	kprobe_opcode_t insn;
};

#ifdef CONFIG_KPROBES
extern int kprobe_exceptions_notify(struct notifier_block *self,
				    unsigned long val, void *data);
#else				/* !CONFIG_KPROBES */
static inline int kprobe_exceptions_notify(struct notifier_block *self,
					   unsigned long val, void *data)
{
	return 0;
}
#endif
#endif				/* _ASM_KPROBES_H */
