#ifndef ASM_X86__SIGINFO_H
#define ASM_X86__SIGINFO_H

#ifdef __x86_64__
# define __ARCH_SI_PREAMBLE_SIZE	(4 * sizeof(int))
#endif

#include <asm-generic/siginfo.h>

#endif /* ASM_X86__SIGINFO_H */
