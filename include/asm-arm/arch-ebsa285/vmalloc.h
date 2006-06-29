/*
 *  linux/include/asm-arm/arch-ebsa285/vmalloc.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#ifdef CONFIG_ARCH_FOOTBRIDGE
#define VMALLOC_END       (PAGE_OFFSET + 0x30000000)
#else
#define VMALLOC_END       (PAGE_OFFSET + 0x20000000)
#endif
