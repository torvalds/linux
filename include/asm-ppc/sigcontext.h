#ifndef _ASM_PPC_SIGCONTEXT_H
#define _ASM_PPC_SIGCONTEXT_H

#include <asm/ptrace.h>
#include <linux/compiler.h>

struct sigcontext {
	unsigned long	_unused[4];
	int		signal;
	unsigned long	handler;
	unsigned long	oldmask;
	struct pt_regs 	__user *regs;
};

#endif
