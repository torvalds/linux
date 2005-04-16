#ifndef _ASM_PPC64_SIGCONTEXT_H
#define _ASM_PPC64_SIGCONTEXT_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/compiler.h>
#include <asm/ptrace.h>
#include <asm/elf.h>


struct sigcontext {
	unsigned long	_unused[4];
	int		signal;
	int		_pad0;
	unsigned long	handler;
	unsigned long	oldmask;
	struct pt_regs	__user *regs;
	elf_gregset_t	gp_regs;
	elf_fpregset_t	fp_regs;
/*
 * To maintain compatibility with current implementations the sigcontext is 
 * extended by appending a pointer (v_regs) to a quadword type (elf_vrreg_t) 
 * followed by an unstructured (vmx_reserve) field of 69 doublewords.  This 
 * allows the array of vector registers to be quadword aligned independent of 
 * the alignment of the containing sigcontext or ucontext. It is the 
 * responsibility of the code setting the sigcontext to set this pointer to 
 * either NULL (if this processor does not support the VMX feature) or the 
 * address of the first quadword within the allocated (vmx_reserve) area.
 *
 * The pointer (v_regs) of vector type (elf_vrreg_t) is type compatible with 
 * an array of 34 quadword entries (elf_vrregset_t).  The entries with 
 * indexes 0-31 contain the corresponding vector registers.  The entry with 
 * index 32 contains the vscr as the last word (offset 12) within the 
 * quadword.  This allows the vscr to be stored as either a quadword (since 
 * it must be copied via a vector register to/from storage) or as a word.  
 * The entry with index 33 contains the vrsave as the first word (offset 0) 
 * within the quadword.
 */
	elf_vrreg_t	__user *v_regs;
	long		vmx_reserve[ELF_NVRREG+ELF_NVRREG+1];
};

#endif /* _ASM_PPC64_SIGCONTEXT_H */
