#ifndef __ASM_GENERIC_SIGNAL_H
#define __ASM_GENERIC_SIGNAL_H

#include <uapi/asm-generic/signal.h>

#ifndef __ASSEMBLY__
#ifdef SA_RESTORER
#endif

#include <asm/sigcontext.h>
#undef __HAVE_ARCH_SIG_BITOPS

#define ptrace_signal_deliver(regs, cookie) do { } while (0)

#endif /* __ASSEMBLY__ */
#endif /* _ASM_GENERIC_SIGNAL_H */
