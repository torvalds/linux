#ifndef __ASM_MACH_GENERIC_KMALLOC_H
#define __ASM_MACH_GENERIC_KMALLOC_H

#include <linux/config.h>

#ifndef CONFIG_DMA_COHERENT
/*
 * Total overkill for most systems but need as a safe default.
 */
#define ARCH_KMALLOC_MINALIGN	128
#endif

#endif /* __ASM_MACH_GENERIC_KMALLOC_H */
