/*
 * Architecture specific parts of the Floppy driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995
 */
#ifndef __ASM_ALPHA_FLOPPY_H
#define __ASM_ALPHA_FLOPPY_H


#define fd_inb(port)			inb_p(port)
#define fd_outb(value,port)		outb_p(value,port)

#define fd_enable_dma()         enable_dma(FLOPPY_DMA)
#define fd_disable_dma()        disable_dma(FLOPPY_DMA)
#define fd_request_dma()        request_dma(FLOPPY_DMA,"floppy")
#define fd_free_dma()           free_dma(FLOPPY_DMA)
#define fd_clear_dma_ff()       clear_dma_ff(FLOPPY_DMA)
#define fd_set_dma_mode(mode)   set_dma_mode(FLOPPY_DMA,mode)
#define fd_set_dma_addr(addr)   set_dma_addr(FLOPPY_DMA,virt_to_bus(addr))
#define fd_set_dma_count(count) set_dma_count(FLOPPY_DMA,count)
#define fd_enable_irq()         enable_irq(FLOPPY_IRQ)
#define fd_disable_irq()        disable_irq(FLOPPY_IRQ)
#define fd_cacheflush(addr,size) /* nothing */
#define fd_request_irq()        request_irq(FLOPPY_IRQ, floppy_interrupt, \
					    SA_INTERRUPT|SA_SAMPLE_RANDOM, \
				            "floppy", NULL)
#define fd_free_irq()           free_irq(FLOPPY_IRQ, NULL);

#ifdef CONFIG_PCI

#include <linux/pci.h>

#define fd_dma_setup(addr,size,mode,io) alpha_fd_dma_setup(addr,size,mode,io)

static __inline__ int 
alpha_fd_dma_setup(char *addr, unsigned long size, int mode, int io)
{
	static unsigned long prev_size;
	static dma_addr_t bus_addr = 0;
	static char *prev_addr;
	static int prev_dir;
	int dir;

	dir = (mode != DMA_MODE_READ) ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE;

	if (bus_addr 
	    && (addr != prev_addr || size != prev_size || dir != prev_dir)) {
		/* different from last time -- unmap prev */
		pci_unmap_single(isa_bridge, bus_addr, prev_size, prev_dir);
		bus_addr = 0;
	}

	if (!bus_addr)	/* need to map it */
		bus_addr = pci_map_single(isa_bridge, addr, size, dir);

	/* remember this one as prev */
	prev_addr = addr;
	prev_size = size;
	prev_dir = dir;

	fd_clear_dma_ff();
	fd_cacheflush(addr, size);
	fd_set_dma_mode(mode);
	set_dma_addr(FLOPPY_DMA, bus_addr);
	fd_set_dma_count(size);
	virtual_dma_port = io;
	fd_enable_dma();

	return 0;
}

#endif /* CONFIG_PCI */

__inline__ void virtual_dma_init(void)
{
	/* Nothing to do on an Alpha */
}

static int FDC1 = 0x3f0;
static int FDC2 = -1;

/*
 * Again, the CMOS information doesn't work on the alpha..
 */
#define FLOPPY0_TYPE 6
#define FLOPPY1_TYPE 0

#define N_FDC 2
#define N_DRIVE 8

#define FLOPPY_MOTOR_MASK 0xf0

/*
 * Most Alphas have no problems with floppy DMA crossing 64k borders,
 * except for certain ones, like XL and RUFFIAN.
 *
 * However, the test is simple and fast, and this *is* floppy, after all,
 * so we do it for all platforms, just to make sure.
 *
 * This is advantageous in other circumstances as well, as in moving
 * about the PCI DMA windows and forcing the floppy to start doing
 * scatter-gather when it never had before, and there *is* a problem
 * on that platform... ;-}
 */

static inline unsigned long CROSS_64KB(void *a, unsigned long s)
{
	unsigned long p = (unsigned long)a;
	return ((p + s - 1) ^ p) & ~0xffffUL;
}

#define EXTRA_FLOPPY_PARAMS

#endif /* __ASM_ALPHA_FLOPPY_H */
