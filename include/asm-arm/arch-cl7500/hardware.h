/*
 * linux/include/asm-arm/arch-cl7500/hardware.h
 *
 * Copyright (C) 1996-1999 Russell King.
 * Copyright (C) 1999 Nexus Electronics Ltd.
 *
 * This file contains the hardware definitions of the 
 * CL7500 evaluation board.
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/arch/memory.h>
#include <asm/hardware/iomd.h>

#ifdef __ASSEMBLY__
#define IOMEM(x) x
#else
#define IOMEM(x) ((void __iomem *)(x))
#endif

/*
 * What hardware must be present
 */
#define HAS_IOMD
#define HAS_VIDC20

/* Hardware addresses of major areas.
 *  *_START is the physical address
 *  *_SIZE  is the size of the region
 *  *_BASE  is the virtual address
 */

#define IO_START		0x03000000	/* I/O */
#define IO_SIZE			0x01000000
#define IO_BASE			IOMEM(0xe0000000)

#define ISA_START		0x0c000000	/* ISA */
#define ISA_SIZE		0x00010000
#define ISA_BASE		0xe1000000

#define FLASH_START		0x01000000	/* XXX */
#define FLASH_SIZE		0x01000000
#define FLASH_BASE		0xe2000000

#define LED_START		0x0302B000
#define LED_SIZE		0x00001000
#define LED_BASE		0xe3000000
#define LED_ADDRESS		(LED_BASE + 0xa00)

/* Let's define SCREEN_START for CL7500, even though it's a lie. */
#define SCREEN_START		0x02000000	/* VRAM */
#define SCREEN_END		0xdfc00000
#define SCREEN_BASE		0xdf800000

#define FLUSH_BASE		0xdf000000

#define VIDC_BASE		(void __iomem *)0xe0400000
#define IOMD_BASE		IOMEM(0xe0200000)
#define IOC_BASE		IOMEM(0xe0200000)
#define FLOPPYDMA_BASE		IOMEM(0xe002a000)
#define PCIO_BASE		IOMEM(0xe0010000)

#define FLUSH_BASE_PHYS		0x00000000	/* ROM */

#define vidc_writel(val)	__raw_writel(val, VIDC_BASE)

/* in/out bias for the ISA slot region */
#define ISASLOT_IO		0x80400000

#endif
