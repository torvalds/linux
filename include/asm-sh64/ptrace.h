#ifndef __ASM_SH64_PTRACE_H
#define __ASM_SH64_PTRACE_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/ptrace.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 */

/*
 * This struct defines the way the registers are stored on the
 * kernel stack during a system call or other kernel entry.
 */
struct pt_regs {
	unsigned long long pc;
	unsigned long long sr;
	unsigned long long syscall_nr;
	unsigned long long regs[63];
	unsigned long long tregs[8];
	unsigned long long pad[2];
};

#ifdef __KERNEL__
#define user_mode(regs) (((regs)->sr & 0x40000000)==0)
#define instruction_pointer(regs) ((regs)->pc)
#define profile_pc(regs) ((unsigned long)instruction_pointer(regs))
extern void show_regs(struct pt_regs *);
#endif

#endif /* __ASM_SH64_PTRACE_H */
