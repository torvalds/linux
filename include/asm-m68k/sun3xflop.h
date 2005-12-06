/* sun3xflop.h: Sun3/80 specific parts of the floppy driver.
 *
 * Derived partially from asm-sparc/floppy.h, which is:
 *     Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *
 * Sun3x version 2/4/2000 Sam Creasey (sammy@sammy.net)
 */

#ifndef __ASM_SUN3X_FLOPPY_H
#define __ASM_SUN3X_FLOPPY_H

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/sun3x.h>

/* default interrupt vector */
#define SUN3X_FDC_IRQ 0x40

/* some constants */
#define FCR_TC 0x1
#define FCR_EJECT 0x2
#define FCR_MTRON 0x4
#define FCR_DSEL1 0x8
#define FCR_DSEL0 0x10

/* We don't need no stinkin' I/O port allocation crap. */
#undef release_region
#undef request_region
#define release_region(X, Y)	do { } while(0)
#define request_region(X, Y, Z)	(1)

struct sun3xflop_private {
	volatile unsigned char *status_r;
	volatile unsigned char *data_r;
	volatile unsigned char *fcr_r;
	volatile unsigned char *fvr_r;
	unsigned char fcr;
} sun3x_fdc;

/* Super paranoid... */
#undef HAVE_DISABLE_HLT

/* Routines unique to each controller type on a Sun. */
static unsigned char sun3x_82072_fd_inb(int port)
{
	static int once = 0;
//	udelay(5);
	switch(port & 7) {
	default:
		printk("floppy: Asked to read unknown port %d\n", port);
		panic("floppy: Port bolixed.");
	case 4: /* FD_STATUS */
		return (*sun3x_fdc.status_r) & ~STATUS_DMA;
	case 5: /* FD_DATA */
		return (*sun3x_fdc.data_r);
	case 7: /* FD_DIR */
		/* ugly hack, I can't find a way to actually detect the disk */
		if(!once) {
			once = 1;
			return 0x80;
		}
		return 0;
	};
	panic("sun_82072_fd_inb: How did I get here?");
}

static void sun3x_82072_fd_outb(unsigned char value, int port)
{
//	udelay(5);
	switch(port & 7) {
	default:
		printk("floppy: Asked to write to unknown port %d\n", port);
		panic("floppy: Port bolixed.");
	case 2: /* FD_DOR */
		/* Oh geese, 82072 on the Sun has no DOR register,
		 * so we make do with taunting the FCR.
		 *
		 * ASSUMPTIONS:  There will only ever be one floppy
		 *               drive attached to a Sun controller
		 *               and it will be at drive zero.
		 */

	{
		unsigned char fcr = sun3x_fdc.fcr;

		if(value & 0x10) {
			fcr |= (FCR_DSEL0 | FCR_MTRON);
		} else
			fcr &= ~(FCR_DSEL0 | FCR_MTRON);


		if(fcr != sun3x_fdc.fcr) {
			*(sun3x_fdc.fcr_r) = fcr;
			sun3x_fdc.fcr = fcr;
		}
	}
		break;
	case 5: /* FD_DATA */
		*(sun3x_fdc.data_r) = value;
		break;
	case 7: /* FD_DCR */
		*(sun3x_fdc.status_r) = value;
		break;
	case 4: /* FD_STATUS */
		*(sun3x_fdc.status_r) = value;
		break;
	};
	return;
}


asmlinkage irqreturn_t sun3xflop_hardint(int irq, void *dev_id,
					 struct pt_regs * regs)
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

//	printk("doing pdma\n");// st %x\n", sun_fdc->status_82072);

#ifdef TRACE_FLPY_INT
	if(!calls)
		bytes = virtual_dma_count;
#endif

	{
		register int lcount;
		register char *lptr;

		for(lcount=virtual_dma_count, lptr=virtual_dma_addr;
		    lcount; lcount--, lptr++) {
/*			st=fd_inb(virtual_dma_port+4) & 0x80 ;  */
			st = *(sun3x_fdc.status_r);
/*			if(st != 0xa0)                  */
/*				break;                  */

			if((st & 0x80) == 0) {
				virtual_dma_count = lcount;
				virtual_dma_addr = lptr;
				return IRQ_HANDLED;
			}

			if((st & 0x20) == 0)
				break;

			if(virtual_dma_mode)
/*				fd_outb(*lptr, virtual_dma_port+5); */
				*(sun3x_fdc.data_r) = *lptr;
			else
/*				*lptr = fd_inb(virtual_dma_port+5); */
				*lptr = *(sun3x_fdc.data_r);
		}

		virtual_dma_count = lcount;
		virtual_dma_addr = lptr;
/*		st = fd_inb(virtual_dma_port+4);   */
		st = *(sun3x_fdc.status_r);
	}

#ifdef TRACE_FLPY_INT
	calls++;
#endif
//	printk("st=%02x\n", st);
	if(st == 0x20)
		return IRQ_HANDLED;
	if(!(st & 0x20)) {
		virtual_dma_residue += virtual_dma_count;
		virtual_dma_count=0;
		doing_pdma = 0;

#ifdef TRACE_FLPY_INT
		printk("count=%x, residue=%x calls=%d bytes=%x dma_wait=%d\n",
		       virtual_dma_count, virtual_dma_residue, calls, bytes,
		       dma_wait);
		calls = 0;
		dma_wait=0;
#endif

		floppy_interrupt(irq, dev_id, regs);
		return IRQ_HANDLED;
	}


#ifdef TRACE_FLPY_INT
	if(!virtual_dma_count)
		dma_wait++;
#endif
	return IRQ_HANDLED;
}

static int sun3xflop_request_irq(void)
{
	static int once = 0;
	int error;

	if(!once) {
		once = 1;
		error = request_irq(FLOPPY_IRQ, sun3xflop_hardint, SA_INTERRUPT, "floppy", 0);
		return ((error == 0) ? 0 : -1);
	} else return 0;
}

static void __init floppy_set_flags(int *ints,int param, int param2);

static int sun3xflop_init(void)
{
	if(FLOPPY_IRQ < 0x40)
		FLOPPY_IRQ = SUN3X_FDC_IRQ;

	sun3x_fdc.status_r = (volatile unsigned char *)SUN3X_FDC;
	sun3x_fdc.data_r  = (volatile unsigned char *)(SUN3X_FDC+1);
	sun3x_fdc.fcr_r = (volatile unsigned char *)SUN3X_FDC_FCR;
	sun3x_fdc.fvr_r = (volatile unsigned char *)SUN3X_FDC_FVR;
	sun3x_fdc.fcr = 0;

	/* Last minute sanity check... */
	if(*sun3x_fdc.status_r == 0xff) {
		return -1;
	}

	*sun3x_fdc.fvr_r = FLOPPY_IRQ;

	*sun3x_fdc.fcr_r = FCR_TC;
	udelay(10);
	*sun3x_fdc.fcr_r = 0;

	/* Success... */
	floppy_set_flags(0, 1, FD_BROKEN_DCL); // I don't know how to detect this.
	allowed_drive_mask = 0x01;
	return (int) SUN3X_FDC;
}

/* I'm not precisely sure this eject routine works */
static int sun3x_eject(void)
{
	if(MACH_IS_SUN3X) {

		sun3x_fdc.fcr |= (FCR_DSEL0 | FCR_EJECT);
		*(sun3x_fdc.fcr_r) = sun3x_fdc.fcr;
		udelay(10);
		sun3x_fdc.fcr &= ~(FCR_DSEL0 | FCR_EJECT);
		*(sun3x_fdc.fcr_r) = sun3x_fdc.fcr;
	}

	return 0;
}

#define fd_eject(drive) sun3x_eject()

#endif /* !(__ASM_SUN3X_FLOPPY_H) */
