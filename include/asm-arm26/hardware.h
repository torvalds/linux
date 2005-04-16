/*
 *  linux/include/asm-arm/arch-arc/hardware.h
 *
 *  Copyright (C) 1996-1999 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file contains the hardware definitions of the
 *  Acorn Archimedes/A5000 machines.
 *
 *  Modifications:
 *   04-04-1998	PJB/RMK	Merged arc and a5k versions
 */
#ifndef __ASM_HARDWARE_H
#define __ASM_HARDWARE_H

#include <linux/config.h>


/*
 * What hardware must be present - these can be tested by the kernel
 * source.
 */
#define HAS_IOC
#define HAS_MEMC
#define HAS_VIDC

#define VDMA_ALIGNMENT  PAGE_SIZE
#define VDMA_XFERSIZE   16
#define VDMA_INIT       0
#define VDMA_START      1
#define VDMA_END        2

#ifndef __ASSEMBLY__
extern void memc_write(unsigned int reg, unsigned long val);

#define video_set_dma(start,end,offset)                         \
do {                                                            \
        memc_write (VDMA_START, (start >> 2));                  \
        memc_write (VDMA_END, (end - VDMA_XFERSIZE) >> 2);      \
        memc_write (VDMA_INIT, (offset >> 2));                  \
} while (0)
#endif


/* Hardware addresses of major areas.
 *  *_START is the physical address
 *  *_SIZE  is the size of the region
 *  *_BASE  is the virtual address
 */
#define IO_START		0x03000000
#define IO_SIZE			0x01000000
#define IO_BASE			0x03000000

/*
 * Screen mapping information
 */
#define SCREEN_START		0x02000000
#define SCREEN_END		0x02078000
#define SCREEN_SIZE		0x00078000
#define SCREEN_BASE		0x02000000


#define EXPMASK_BASE		0x03360000
#define IOEB_BASE		0x03350000
#define VIDC_BASE		0x03400000
#define LATCHA_BASE		0x03250040
#define LATCHB_BASE		0x03250018
#define IOC_BASE		0x03200000
#define FLOPPYDMA_BASE		0x0302a000
#define PCIO_BASE		0x03010000

// FIXME - are the below correct?
#define PODSLOT_IOC0_BASE       0x03240000
#define PODSLOT_IOC_SIZE        (1 << 14)
#define PODSLOT_MEMC_BASE       0x03000000
#define PODSLOT_MEMC_SIZE       (1 << 14)

#define vidc_writel(val)	__raw_writel(val, VIDC_BASE)

#ifndef __ASSEMBLY__

/*
 * for use with inb/outb
 */
#define IOEB_VID_CTL		(IOEB_BASE + 0x48)
#define IOEB_PRESENT		(IOEB_BASE + 0x50)
#define IOEB_PSCLR		(IOEB_BASE + 0x58)
#define IOEB_MONTYPE		(IOEB_BASE + 0x70)

//FIXME - These adresses are weird - ISTR some weirdo address shifting stuff was going on here...
#define IO_EC_IOC_BASE		0x80090000
#define IO_EC_MEMC_BASE		0x80000000

#ifdef CONFIG_ARCH_ARC
/* A680 hardware */
#define WD1973_BASE		0x03290000
#define WD1973_LATCH		0x03350000
#define Z8530_BASE		0x032b0008
#define SCSI_BASE		0x03100000
#endif

#endif

#define	EXPMASK_STATUS		(EXPMASK_BASE + 0x00)
#define EXPMASK_ENABLE		(EXPMASK_BASE + 0x04)

#endif
