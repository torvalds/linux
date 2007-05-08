#ifndef _ASM_POWERPC_PGTABLE_H
#define _ASM_POWERPC_PGTABLE_H
#ifdef __KERNEL__

#if defined(CONFIG_PPC64)
#  include <asm/pgtable-ppc64.h>
#else
#  include <asm/pgtable-ppc32.h>
#endif

#ifndef __ASSEMBLY__
#include <asm-generic/pgtable.h>
#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_PGTABLE_H */
