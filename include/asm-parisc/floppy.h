/*    Architecture specific parts of the Floppy driver
 *
 *    Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *    Copyright (C) 2000 Matthew Wilcox (willy a debian . org)
 *    Copyright (C) 2000 Dave Kennedy
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_PARISC_FLOPPY_H
#define __ASM_PARISC_FLOPPY_H

#include <linux/vmalloc.h>


/*
 * The DMA channel used by the floppy controller cannot access data at
 * addresses >= 16MB
 *
 * Went back to the 1MB limit, as some people had problems with the floppy
 * driver otherwise. It doesn't matter much for performance anyway, as most
 * floppy accesses go through the track buffer.
 */
#define _CROSS_64KB(a,s,vdma) \
(!vdma && ((unsigned long)(a)/K_64 != ((unsigned long)(a) + (s) - 1) / K_64))

#define CROSS_64KB(a,s) _CROSS_64KB(a,s,use_virtual_dma & 1)


#define SW fd_routine[use_virtual_dma&1]
#define CSW fd_routine[can_use_virtual_dma & 1]


#define fd_inb(port)			readb(port)
#define fd_outb(value, port)		writeb(value, port)

#define fd_request_dma()        CSW._request_dma(FLOPPY_DMA,"floppy")
#define fd_free_dma()           CSW._free_dma(FLOPPY_DMA)
#define fd_enable_irq()         enable_irq(FLOPPY_IRQ)
#define fd_disable_irq()        disable_irq(FLOPPY_IRQ)
#define fd_free_irq()		free_irq(FLOPPY_IRQ, NULL)
#define fd_get_dma_residue()    SW._get_dma_residue(FLOPPY_DMA)
#define fd_dma_mem_alloc(size)	SW._dma_mem_alloc(size)
#define fd_dma_setup(addr, size, mode, io) SW._dma_setup(addr, size, mode, io)

#define FLOPPY_CAN_FALLBACK_ON_NODMA

static int virtual_dma_count=0;
static int virtual_dma_residue=0;
static char *virtual_dma_addr=0;
static int virtual_dma_mode=0;
static int doing_pdma=0;

static void floppy_hardint(int irq, void *dev_id, struct pt_regs * regs)
{
	register unsigned char st;

#undef TRACE_FLPY_INT

#ifdef TRACE_FLPY_INT
	static int calls=0;
	static int bytes=0;
	static int dma_wait=0;
#endif
	if (!doing_pdma) {
		floppy_interrupt(irq, dev_id, regs);
		return;
	}

#ifdef TRACE_FLPY_INT
	if(!calls)
		bytes = virtual_dma_count;
#endif

	{
		register int lcount;
		register char *lptr = virtual_dma_addr;

		for (lcount = virtual_dma_count; lcount; lcount--) {
			st = fd_inb(virtual_dma_port+4) & 0xa0 ;
			if (st != 0xa0) 
				break;
			if (virtual_dma_mode) {
				fd_outb(*lptr, virtual_dma_port+5);
			} else {
				*lptr = fd_inb(virtual_dma_port+5);
			}
			lptr++;
		}
		virtual_dma_count = lcount;
		virtual_dma_addr = lptr;
		st = fd_inb(virtual_dma_port+4);
	}

#ifdef TRACE_FLPY_INT
	calls++;
#endif
	if (st == 0x20)
		return;
	if (!(st & 0x20)) {
		virtual_dma_residue += virtual_dma_count;
		virtual_dma_count = 0;
#ifdef TRACE_FLPY_INT
		printk("count=%x, residue=%x calls=%d bytes=%d dma_wait=%d\n", 
		       virtual_dma_count, virtual_dma_residue, calls, bytes,
		       dma_wait);
		calls = 0;
		dma_wait=0;
#endif
		doing_pdma = 0;
		floppy_interrupt(irq, dev_id, regs);
		return;
	}
#ifdef TRACE_FLPY_INT
	if (!virtual_dma_count)
		dma_wait++;
#endif
}

static void fd_disable_dma(void)
{
	if(! (can_use_virtual_dma & 1))
		disable_dma(FLOPPY_DMA);
	doing_pdma = 0;
	virtual_dma_residue += virtual_dma_count;
	virtual_dma_count=0;
}

static int vdma_request_dma(unsigned int dmanr, const char * device_id)
{
	return 0;
}

static void vdma_nop(unsigned int dummy)
{
}


static int vdma_get_dma_residue(unsigned int dummy)
{
	return virtual_dma_count + virtual_dma_residue;
}


static int fd_request_irq(void)
{
	if(can_use_virtual_dma)
		return request_irq(FLOPPY_IRQ, floppy_hardint,SA_INTERRUPT,
						   "floppy", NULL);
	else
		return request_irq(FLOPPY_IRQ, floppy_interrupt, SA_INTERRUPT,
				   "floppy", NULL);
}

static unsigned long dma_mem_alloc(unsigned long size)
{
	return __get_dma_pages(GFP_KERNEL, get_order(size));
}


static unsigned long vdma_mem_alloc(unsigned long size)
{
	return (unsigned long) vmalloc(size);

}

#define nodma_mem_alloc(size) vdma_mem_alloc(size)

static void _fd_dma_mem_free(unsigned long addr, unsigned long size)
{
	if((unsigned int) addr >= (unsigned int) high_memory)
		return vfree((void *)addr);
	else
		free_pages(addr, get_order(size));		
}

#define fd_dma_mem_free(addr, size)  _fd_dma_mem_free(addr, size) 

static void _fd_chose_dma_mode(char *addr, unsigned long size)
{
	if(can_use_virtual_dma == 2) {
		if((unsigned int) addr >= (unsigned int) high_memory ||
		   virt_to_bus(addr) >= 0x1000000 ||
		   _CROSS_64KB(addr, size, 0))
			use_virtual_dma = 1;
		else
			use_virtual_dma = 0;
	} else {
		use_virtual_dma = can_use_virtual_dma & 1;
	}
}

#define fd_chose_dma_mode(addr, size) _fd_chose_dma_mode(addr, size)


static int vdma_dma_setup(char *addr, unsigned long size, int mode, int io)
{
	doing_pdma = 1;
	virtual_dma_port = io;
	virtual_dma_mode = (mode  == DMA_MODE_WRITE);
	virtual_dma_addr = addr;
	virtual_dma_count = size;
	virtual_dma_residue = 0;
	return 0;
}

static int hard_dma_setup(char *addr, unsigned long size, int mode, int io)
{
#ifdef FLOPPY_SANITY_CHECK
	if (CROSS_64KB(addr, size)) {
		printk("DMA crossing 64-K boundary %p-%p\n", addr, addr+size);
		return -1;
	}
#endif
	/* actual, physical DMA */
	doing_pdma = 0;
	clear_dma_ff(FLOPPY_DMA);
	set_dma_mode(FLOPPY_DMA,mode);
	set_dma_addr(FLOPPY_DMA,virt_to_bus(addr));
	set_dma_count(FLOPPY_DMA,size);
	enable_dma(FLOPPY_DMA);
	return 0;
}

static struct fd_routine_l {
	int (*_request_dma)(unsigned int dmanr, const char * device_id);
	void (*_free_dma)(unsigned int dmanr);
	int (*_get_dma_residue)(unsigned int dummy);
	unsigned long (*_dma_mem_alloc) (unsigned long size);
	int (*_dma_setup)(char *addr, unsigned long size, int mode, int io);
} fd_routine[] = {
	{
		request_dma,
		free_dma,
		get_dma_residue,
		dma_mem_alloc,
		hard_dma_setup
	},
	{
		vdma_request_dma,
		vdma_nop,
		vdma_get_dma_residue,
		vdma_mem_alloc,
		vdma_dma_setup
	}
};


static int FDC1 = 0x3f0; /* Lies.  Floppy controller is memory mapped, not io mapped */
static int FDC2 = -1;

#define FLOPPY0_TYPE	0
#define FLOPPY1_TYPE	0

#define N_FDC 1
#define N_DRIVE 8

#define FLOPPY_MOTOR_MASK 0xf0

#define AUTO_DMA

#define EXTRA_FLOPPY_PARAMS

#endif /* __ASM_PARISC_FLOPPY_H */
