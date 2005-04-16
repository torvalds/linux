/* include/asm-arm/arch-lh7a40x/vmalloc.h
 *
 *  Copyright (C) 2004 Coastal Environmental Systems
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

/*
 * Just any arbitrary offset to the start of the vmalloc VM area: the
 * current 8MB value just means that there will be a 8MB "hole" after
 * the physical memory until the kernel virtual memory starts.  That
 * means that any out-of-bounds memory accesses will hopefully be
 * caught.  The vmalloc() routines leaves a hole of 4kB (one page)
 * between each vmalloced area for the same reason. ;)
 */
#define VMALLOC_OFFSET	  (8*1024*1024)
#define VMALLOC_START	  (((unsigned long)high_memory + VMALLOC_OFFSET) & ~(VMALLOC_OFFSET-1))
#define VMALLOC_END       (0xe8000000)
