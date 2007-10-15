#ifndef _ASM_X86_SIGINFO_H
#define _ASM_X86_SIGINFO_H

#ifdef __x86_64__
# define __ARCH_SI_PREAMBLE_SIZE	(4 * sizeof(int))
#endif

#include <asm-generic/siginfo.h>

#endif
