/*
 * Architecture specific parts of the Floppy driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995
 */
#ifdef __KERNEL__
#ifndef __ASM_PPC_FLOPPY_H
#define __ASM_PPC_FLOPPY_H

#define fd_inb(port)		inb_p(port)
#define fd_outb(value,port)	outb_p(value,port)

#define fd_disable_dma()	fd_ops->_disable_dma(FLOPPY_DMA)
#define fd_free_dma()           fd_ops->_free_dma(FLOPPY_DMA)
#define fd_get_dma_residue()    fd_ops->_get_dma_residue(FLOPPY_DMA)
#define fd_dma_setup(addr, size, mode, io) fd_ops->_dma_setup(addr, size, mode, io)
#define fd_enable_irq()         enable_irq(FLOPPY_IRQ)
#define fd_disable_irq()        disable_irq(FLOPPY_IRQ)
#define fd_free_irq()           free_irq(FLOPPY_IRQ, NULL);

static int fd_request_dma(void);

struct fd_dma_ops {
	void (*_disable_dma)(unsigned int dmanr);
	void (*_free_dma)(unsigned int dmanr);
	int (*_get_dma_residue)(unsigned int dummy);
	int (*_dma_setup)(char *addr, unsigned long size, int mode, int io);
};

static int virtual_dma_count;
static int virtual_dma_residue;
static char *virtual_dma_addr;
static int virtual_dma_mode;
static int doing_vdma;
static struct fd_dma_ops *fd_ops;

static irqreturn_t floppy_hardint(int irq, void *dev_id)
{
	unsigned char st;
	int lcount;
	char *lptr;

	if (!doing_vdma)
		return floppy_interrupt(irq, dev_id);


	st = 1;
	for (lcount=virtual_dma_count, lptr=virtual_dma_addr;
	     lcount; lcount--, lptr++) {
		st=inb(virtual_dma_port+4) & 0xa0 ;
		if (st != 0xa0)
			break;
		if (virtual_dma_mode)
			outb_p(*lptr, virtual_dma_port+5);
		else
			*lptr = inb_p(virtual_dma_port+5);
	}
	virtual_dma_count = lcount;
	virtual_dma_addr = lptr;
	st = inb(virtual_dma_port+4);

	if (st == 0x20)
		return IRQ_HANDLED;
	if (!(st & 0x20)) {
		virtual_dma_residue += virtual_dma_count;
		virtual_dma_count=0;
		doing_vdma = 0;
		floppy_interrupt(irq, dev_id);
		return IRQ_HANDLED;
	}
	return IRQ_HANDLED;
}

static void vdma_disable_dma(unsigned int dummy)
{
	doing_vdma = 0;
	virtual_dma_residue += virtual_dma_count;
	virtual_dma_count=0;
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
	if (can_use_virtual_dma)
		return request_irq(FLOPPY_IRQ, floppy_hardint,
				   IRQF_DISABLED, "floppy", NULL);
	else
		return request_irq(FLOPPY_IRQ, floppy_interrupt,
				   IRQF_DISABLED, "floppy", NULL);
}

static int vdma_dma_setup(char *addr, unsigned long size, int mode, int io)
{
	doing_vdma = 1;
	virtual_dma_port = io;
	virtual_dma_mode = (mode  == DMA_MODE_WRITE);
	virtual_dma_addr = addr;
	virtual_dma_count = size;
	virtual_dma_residue = 0;
	return 0;
}

static int hard_dma_setup(char *addr, unsigned long size, int mode, int io)
{
	/* actual, physical DMA */
	doing_vdma = 0;
	clear_dma_ff(FLOPPY_DMA);
	set_dma_mode(FLOPPY_DMA,mode);
	set_dma_addr(FLOPPY_DMA,(unsigned int)virt_to_bus(addr));
	set_dma_count(FLOPPY_DMA,size);
	enable_dma(FLOPPY_DMA);
	return 0;
}

static struct fd_dma_ops real_dma_ops =
{
	._disable_dma = disable_dma,
	._free_dma = free_dma,
	._get_dma_residue = get_dma_residue,
	._dma_setup = hard_dma_setup
};

static struct fd_dma_ops virt_dma_ops =
{
	._disable_dma = vdma_disable_dma,
	._free_dma = vdma_nop,
	._get_dma_residue = vdma_get_dma_residue,
	._dma_setup = vdma_dma_setup
};

static int fd_request_dma()
{
	if (can_use_virtual_dma & 1) {
		fd_ops = &virt_dma_ops;
		return 0;
	}
	else {
		fd_ops = &real_dma_ops;
		return request_dma(FLOPPY_DMA, "floppy");
	}
}

static int FDC1 = 0x3f0;
static int FDC2 = -1;

/*
 * Again, the CMOS information not available
 */
#define FLOPPY0_TYPE 6
#define FLOPPY1_TYPE 0

#define N_FDC 2			/* Don't change this! */
#define N_DRIVE 8

/*
 * The PowerPC has no problems with floppy DMA crossing 64k borders.
 */
#define CROSS_64KB(a,s)	(0)

#endif /* __ASM_PPC_FLOPPY_H */

#define EXTRA_FLOPPY_PARAMS

#endif /* __KERNEL__ */
