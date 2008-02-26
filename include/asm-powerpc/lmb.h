#ifndef _ASM_POWERPC_LMB_H
#define _ASM_POWERPC_LMB_H

#include <asm/udbg.h>

#define LMB_DBG(fmt...) udbg_printf(fmt)

#ifdef CONFIG_PPC32
extern unsigned long __max_low_memory;
#define LMB_REAL_LIMIT	__max_low_memory
#else
#define LMB_REAL_LIMIT	0
#endif

#endif /* _ASM_POWERPC_LMB_H */
