#ifndef __ASM_SH64_PLATFORM_H
#define __ASM_SH64_PLATFORM_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/platform.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 * benedict.gaster@superh.com:	 3rd May 2002
 *    Added support for ramdisk, removing statically linked romfs at the same time.
 */

#include <linux/ioport.h>
#include <asm/irq.h>


/*
 * Platform definition structure.
 */
struct sh64_platform {
	unsigned int readonly_rootfs;
	unsigned int ramdisk_flags;
	unsigned int initial_root_dev;
	unsigned int loader_type;
	unsigned int initrd_start;
	unsigned int initrd_size;
	unsigned int fpu_flags;
	unsigned int io_res_count;
	unsigned int kram_res_count;
	unsigned int xram_res_count;
	unsigned int rom_res_count;
	struct resource *io_res_p;
	struct resource *kram_res_p;
	struct resource *xram_res_p;
	struct resource *rom_res_p;
};

extern struct sh64_platform platform_parms;

extern unsigned long long memory_start, memory_end;

extern unsigned long long fpu_in_use;

extern int platform_int_priority[NR_INTC_IRQS];

#define FPU_FLAGS		(platform_parms.fpu_flags)
#define STANDARD_IO_RESOURCES	(platform_parms.io_res_count)
#define STANDARD_KRAM_RESOURCES	(platform_parms.kram_res_count)
#define STANDARD_XRAM_RESOURCES	(platform_parms.xram_res_count)
#define STANDARD_ROM_RESOURCES	(platform_parms.rom_res_count)

/*
 * Kernel Memory description, Respectively:
 * code = last but one memory descriptor
 * data = last memory descriptor
 */
#define code_resource (platform_parms.kram_res_p[STANDARD_KRAM_RESOURCES - 2])
#define data_resource (platform_parms.kram_res_p[STANDARD_KRAM_RESOURCES - 1])

/* Be prepared to 64-bit sign extensions */
#define PFN_UP(x)       ((((x) + PAGE_SIZE-1) >> PAGE_SHIFT) & 0x000fffff)
#define PFN_DOWN(x)     (((x) >> PAGE_SHIFT) & 0x000fffff)
#define PFN_PHYS(x)     ((x) << PAGE_SHIFT)

#endif	/* __ASM_SH64_PLATFORM_H */
