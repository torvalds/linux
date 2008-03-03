#ifndef _ASM_X86_SMP_H_
#define _ASM_X86_SMP_H_
#ifndef __ASSEMBLY__

#ifdef CONFIG_X86_32
# include "smp_32.h"
#else
# include "smp_64.h"
#endif

#endif /* __ASSEMBLY__ */
#endif
