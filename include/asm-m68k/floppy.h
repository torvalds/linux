/*
 * Implementation independent bits of the Floppy driver.
 *
 * much of this file is derived from what was originally the Q40 floppy driver.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999, 2000, 2001
 *
 * Sun3x support added 2/4/2000 Sam Creasey (sammy@sammy.net)
 *
 */

#include <asm/io.h>

#include <linux/vmalloc.h>

asmlinkage irqreturn_t floppy_hardint(int irq, void *dev_id,
				      struct pt_regs *regs);

/* constants... */

#undef MAX_DMA_ADDRESS
#define MAX_DMA_ADDRESS   0x00  /* nothing like that */


/*
 * Again, the CMOS information doesn't work on m68k..
 */
#define FLOPPY0_TYPE (MACH_IS_Q40 ? 6 : 4)
#define FLOPPY1_TYPE 0

#define FLOPPY_MOTOR_MASK 0xf0


/* basically PC init + set use_virtual_dma */
#define  FDC1 m68k_floppy_init()

#define N_FDC 1
#define N_DRIVE 8


/* vdma globals adapted from asm-i386/floppy.h */

static int virtual_dma_count=0;
static int virtual_dma_residue=0;
static char *virtual_dma_addr=NULL;
static int virtual_dma_mode=0;
static int doing_pdma=0;

#include <asm/sun3xflop.h>

extern spinlock_t  dma_spin_lock;

static __inline__ unsigned long claim_dma_lock(void)
{
	unsigned long flags;
	spin_lock_irqsave(&dma_spin_lock, flags);
	return flags;
}

static __inline__ void release_dma_lock(unsigned long flags)
{
	spin_unlock_irqrestore(&dma_spin_lock, flags);
}


static __inline__ unsigned char fd_inb(int port)
{
	if(MACH_IS_Q40)
		return inb_p(port);
	else if(MACH_IS_SUN3X)
		return sun3x_82072_fd_inb(port);
	return 0;
}

static __inline__ void fd_outb(unsigned char value, int port)
{
	if(MACH_IS_Q40)
		outb_p(value, port);
	else if(MACH_IS_SUN3X)
		sun3x_82072_fd_outb(value, port);
}


static int fd_request_irq(void)
{
	if(MACH_IS_Q40)
		return request_irq(FLOPPY_IRQ, floppy_hardint,SA_INTERRUPT,
						   "floppy", floppy_hardint);
	else if(MACH_IS_SUN3X)
		return sun3xflop_request_irq();
	return -ENXIO;
}

static void fd_free_irq(void)
{
	if(MACH_IS_Q40)
		free_irq(FLOPPY_IRQ, floppy_hardint);
}

#define fd_request_dma()        vdma_request_dma(FLOPPY_DMA,"floppy")
#define fd_get_dma_residue()    vdma_get_dma_residue(FLOPPY_DMA)
#define fd_dma_mem_alloc(size)	vdma_mem_alloc(size)
#define fd_dma_setup(addr, size, mode, io) vdma_dma_setup(addr, size, mode, io)

#define fd_enable_irq()           /* nothing... */
#define fd_disable_irq()          /* nothing... */

#define fd_free_dma()             /* nothing */

/* No 64k boundary crossing problems on Q40 - no DMA at all */
#define CROSS_64KB(a,s) (0)

#define DMA_MODE_READ  0x44    /* i386 look-alike */
#define DMA_MODE_WRITE 0x48


static int m68k_floppy_init(void)
{
  use_virtual_dma =1;
  can_use_virtual_dma = 1;


  if (MACH_IS_Q40)
	  return 0x3f0;
  else if(MACH_IS_SUN3X)
	  return sun3xflop_init();
  else
    return -1;
}


static int vdma_request_dma(unsigned int dmanr, const char * device_id)
{
	return 0;
}


static int vdma_get_dma_residue(unsigned int dummy)
{
	return virtual_dma_count + virtual_dma_residue;
}


static unsigned long vdma_mem_alloc(unsigned long size)
{
	return (unsigned long) vmalloc(size);

}

static void _fd_dma_mem_free(unsigned long addr, unsigned long size)
{
        vfree((void *)addr);
}
#define fd_dma_mem_free(addr,size) _fd_dma_mem_free(addr, size)


/* choose_dma_mode ???*/

static int vdma_dma_setup(char *addr, unsigned long size, int mode, int io)
{
	doing_pdma = 1;
	virtual_dma_port = (MACH_IS_Q40 ? io : 0);
	virtual_dma_mode = (mode  == DMA_MODE_WRITE);
	virtual_dma_addr = addr;
	virtual_dma_count = size;
	virtual_dma_residue = 0;
	return 0;
}



static void fd_disable_dma(void)
{
	doing_pdma = 0;
	virtual_dma_residue += virtual_dma_count;
	virtual_dma_count=0;
}



/* this is the only truly Q40 specific function */

asmlinkage irqreturn_t floppy_hardint(int irq, void *dev_id,
				      struct pt_regs *regs)
{
	register unsigned char st;

#undef TRACE_FLPY_INT
#define NO_FLOPPY_ASSEMBLER

#ifdef TRACE_FLPY_INT
	static int calls=0;
	static int bytes=0;
	static int dma_wait=0;
#endif
	if(!doing_pdma) {
		floppy_interrupt(irq, dev_id, regs);
		return IRQ_HANDLED;
	}

#ifdef TRACE_FLPY_INT
	if(!calls)
		bytes = virtual_dma_count;
#endif

	{
		register int lcount;
		register char *lptr;

		/* serve 1st byte fast: */

		st=1;
		for(lcount=virtual_dma_count, lptr=virtual_dma_addr;
		    lcount; lcount--, lptr++) {
			st=inb(virtual_dma_port+4) & 0xa0 ;
			if(st != 0xa0)
				break;
			if(virtual_dma_mode)
				outb_p(*lptr, virtual_dma_port+5);
			else
				*lptr = inb_p(virtual_dma_port+5);
		}

		virtual_dma_count = lcount;
		virtual_dma_addr = lptr;
		st = inb(virtual_dma_port+4);
	}

#ifdef TRACE_FLPY_INT
	calls++;
#endif
	if(st == 0x20)
		return IRQ_HANDLED;
	if(!(st & 0x20)) {
		virtual_dma_residue += virtual_dma_count;
		virtual_dma_count=0;
#ifdef TRACE_FLPY_INT
		printk("count=%x, residue=%x calls=%d bytes=%d dma_wait=%d\n",
		       virtual_dma_count, virtual_dma_residue, calls, bytes,
		       dma_wait);
		calls = 0;
		dma_wait=0;
#endif
		doing_pdma = 0;
		floppy_interrupt(irq, dev_id, regs);
		return IRQ_HANDLED;
	}
#ifdef TRACE_FLPY_INT
	if(!virtual_dma_count)
		dma_wait++;
#endif
	return IRQ_HANDLED;
}

#define EXTRA_FLOPPY_PARAMS
