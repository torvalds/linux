/* linux/include/asm-arm/arch-s3c2410/vmalloc.h
 *
 * from linux/include/asm-arm/arch-iop3xx/vmalloc.h
 *
 * Copyright (c) 2003 Simtec Electronics <linux@simtec.co.uk>
 *		      http://www.simtec.co.uk/products/SWLINUX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2410 vmalloc definition
 *
 *  Changelog:
 *    12-Mar-2004 BJD	Fixed header, added include protection
 *    12=Mar-2004 BJD	Fixed VMALLOC_END definitions
 */

#ifndef __ASM_ARCH_VMALLOC_H
#define __ASM_ARCH_VMALLOC_H

/*
 * Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after the
 * physical memory until the kernel virtual memory starts.  That means that
 * any out-of-bounds memory accesses will hopefully be caught.
 * The vmalloc() routines leaves a hole of 4kB between each vmalloced
 * area for the same reason. ;)
 */

#define VMALLOC_OFFSET	  (8*1024*1024)
#define VMALLOC_START	  (((unsigned long)high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_END	  (0xE0000000)

#endif /* __ASM_ARCH_VMALLOC_H */
