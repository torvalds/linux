#ifndef _ASM_POWERPC_PGALLOC_H
#define _ASM_POWERPC_PGALLOC_H
#ifdef __KERNEL__

#ifdef CONFIG_PPC64
#include <asm/pgalloc-64.h>
#else
#include <asm/pgalloc-32.h>
#endif

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_PGALLOC_H */
