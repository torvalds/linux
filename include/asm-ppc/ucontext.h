#ifndef _ASMPPC_UCONTEXT_H
#define _ASMPPC_UCONTEXT_H

#include <asm/elf.h>
#include <asm/signal.h>

struct mcontext {
	elf_gregset_t	mc_gregs;
	elf_fpregset_t	mc_fregs;
	unsigned long	mc_pad[2];
	elf_vrregset_t	mc_vregs __attribute__((__aligned__(16)));
};

struct ucontext {
	unsigned long	 uc_flags;
	struct ucontext __user *uc_link;
	stack_t		 uc_stack;
	int		 uc_pad[7];
	struct mcontext	__user *uc_regs;/* points to uc_mcontext field */
	sigset_t	 uc_sigmask;
	/* glibc has 1024-bit signal masks, ours are 64-bit */
	int		 uc_maskext[30];
	int		 uc_pad2[3];
	struct mcontext	 uc_mcontext;
};

#endif /* !_ASMPPC_UCONTEXT_H */
