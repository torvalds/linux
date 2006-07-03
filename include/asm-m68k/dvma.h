/* $Id: dvma.h,v 1.4 1999/03/27 20:23:41 tsbogend Exp $
 * include/asm-m68k/dma.h
 *
 * Copyright 1995 (C) David S. Miller (davem@caip.rutgers.edu)
 *
 * Hacked to fit Sun3x needs by Thomas Bogendoerfer
 */

#ifndef __M68K_DVMA_H
#define __M68K_DVMA_H


#define DVMA_PAGE_SHIFT	13
#define DVMA_PAGE_SIZE	(1UL << DVMA_PAGE_SHIFT)
#define DVMA_PAGE_MASK	(~(DVMA_PAGE_SIZE-1))
#define DVMA_PAGE_ALIGN(addr)	(((addr)+DVMA_PAGE_SIZE-1)&DVMA_PAGE_MASK)

extern void dvma_init(void);
extern int dvma_map_iommu(unsigned long kaddr, unsigned long baddr,
			  int len);

#define dvma_malloc(x) dvma_malloc_align(x, 0)
#define dvma_map(x, y) dvma_map_align(x, y, 0)
#define dvma_map_vme(x, y) (dvma_map(x, y) & 0xfffff)
#define dvma_map_align_vme(x, y, z) (dvma_map_align (x, y, z) & 0xfffff)
extern unsigned long dvma_map_align(unsigned long kaddr, int len,
			    int align);
extern void *dvma_malloc_align(unsigned long len, unsigned long align);

extern void dvma_unmap(void *baddr);
extern void dvma_free(void *vaddr);


#ifdef CONFIG_SUN3
/* sun3 dvma page support */

/* memory and pmegs potentially reserved for dvma */
#define DVMA_PMEG_START 10
#define DVMA_PMEG_END 16
#define DVMA_START 0xf00000
#define DVMA_END 0xfe0000
#define DVMA_SIZE (DVMA_END-DVMA_START)
#define IOMMU_TOTAL_ENTRIES 128
#define IOMMU_ENTRIES 120

/* empirical kludge -- dvma regions only seem to work right on 0x10000
   byte boundaries */
#define DVMA_REGION_SIZE 0x10000
#define DVMA_ALIGN(addr) (((addr)+DVMA_REGION_SIZE-1) & \
                         ~(DVMA_REGION_SIZE-1))

/* virt <-> phys conversions */
#define dvma_vtop(x) ((unsigned long)(x) & 0xffffff)
#define dvma_ptov(x) ((unsigned long)(x) | 0xf000000)
#define dvma_vtovme(x) ((unsigned long)(x) & 0x00fffff)
#define dvma_vmetov(x) ((unsigned long)(x) | 0xff00000)
#define dvma_vtob(x) dvma_vtop(x)
#define dvma_btov(x) dvma_ptov(x)

static inline int dvma_map_cpu(unsigned long kaddr, unsigned long vaddr,
			       int len)
{
	return 0;
}

extern unsigned long dvma_page(unsigned long kaddr, unsigned long vaddr);

#else /* Sun3x */

/* sun3x dvma page support */

#define DVMA_START 0x0
#define DVMA_END 0xf00000
#define DVMA_SIZE (DVMA_END-DVMA_START)
#define IOMMU_TOTAL_ENTRIES	   2048
/* the prom takes the top meg */
#define IOMMU_ENTRIES              (IOMMU_TOTAL_ENTRIES - 0x80)

#define dvma_vtob(x) ((unsigned long)(x) & 0x00ffffff)
#define dvma_btov(x) ((unsigned long)(x) | 0xff000000)

extern int dvma_map_cpu(unsigned long kaddr, unsigned long vaddr, int len);



/* everything below this line is specific to dma used for the onboard
   ESP scsi on sun3x */

/* Structure to describe the current status of DMA registers on the Sparc */
struct sparc_dma_registers {
  __volatile__ unsigned long cond_reg;	/* DMA condition register */
  __volatile__ unsigned long st_addr;	/* Start address of this transfer */
  __volatile__ unsigned long  cnt;	/* How many bytes to transfer */
  __volatile__ unsigned long dma_test;	/* DMA test register */
};

/* DVMA chip revisions */
enum dvma_rev {
	dvmarev0,
	dvmaesc1,
	dvmarev1,
	dvmarev2,
	dvmarev3,
	dvmarevplus,
	dvmahme
};

#define DMA_HASCOUNT(rev)  ((rev)==dvmaesc1)

/* Linux DMA information structure, filled during probe. */
struct Linux_SBus_DMA {
	struct Linux_SBus_DMA *next;
	struct linux_sbus_device *SBus_dev;
	struct sparc_dma_registers *regs;

	/* Status, misc info */
	int node;                /* Prom node for this DMA device */
	int running;             /* Are we doing DMA now? */
	int allocated;           /* Are we "owned" by anyone yet? */

	/* Transfer information. */
	unsigned long addr;      /* Start address of current transfer */
	int nbytes;              /* Size of current transfer */
	int realbytes;           /* For splitting up large transfers, etc. */

	/* DMA revision */
	enum dvma_rev revision;
};

extern struct Linux_SBus_DMA *dma_chain;

/* Broken hardware... */
#define DMA_ISBROKEN(dma)    ((dma)->revision == dvmarev1)
#define DMA_ISESC1(dma)      ((dma)->revision == dvmaesc1)

/* Fields in the cond_reg register */
/* First, the version identification bits */
#define DMA_DEVICE_ID    0xf0000000        /* Device identification bits */
#define DMA_VERS0        0x00000000        /* Sunray DMA version */
#define DMA_ESCV1        0x40000000        /* DMA ESC Version 1 */
#define DMA_VERS1        0x80000000        /* DMA rev 1 */
#define DMA_VERS2        0xa0000000        /* DMA rev 2 */
#define DMA_VERHME       0xb0000000        /* DMA hme gate array */
#define DMA_VERSPLUS     0x90000000        /* DMA rev 1 PLUS */

#define DMA_HNDL_INTR    0x00000001        /* An IRQ needs to be handled */
#define DMA_HNDL_ERROR   0x00000002        /* We need to take an error */
#define DMA_FIFO_ISDRAIN 0x0000000c        /* The DMA FIFO is draining */
#define DMA_INT_ENAB     0x00000010        /* Turn on interrupts */
#define DMA_FIFO_INV     0x00000020        /* Invalidate the FIFO */
#define DMA_ACC_SZ_ERR   0x00000040        /* The access size was bad */
#define DMA_FIFO_STDRAIN 0x00000040        /* DMA_VERS1 Drain the FIFO */
#define DMA_RST_SCSI     0x00000080        /* Reset the SCSI controller */
#define DMA_RST_ENET     DMA_RST_SCSI      /* Reset the ENET controller */
#define DMA_ST_WRITE     0x00000100        /* write from device to memory */
#define DMA_ENABLE       0x00000200        /* Fire up DMA, handle requests */
#define DMA_PEND_READ    0x00000400        /* DMA_VERS1/0/PLUS Pending Read */
#define DMA_ESC_BURST    0x00000800        /* 1=16byte 0=32byte */
#define DMA_READ_AHEAD   0x00001800        /* DMA read ahead partial longword */
#define DMA_DSBL_RD_DRN  0x00001000        /* No EC drain on slave reads */
#define DMA_BCNT_ENAB    0x00002000        /* If on, use the byte counter */
#define DMA_TERM_CNTR    0x00004000        /* Terminal counter */
#define DMA_CSR_DISAB    0x00010000        /* No FIFO drains during csr */
#define DMA_SCSI_DISAB   0x00020000        /* No FIFO drains during reg */
#define DMA_DSBL_WR_INV  0x00020000        /* No EC inval. on slave writes */
#define DMA_ADD_ENABLE   0x00040000        /* Special ESC DVMA optimization */
#define DMA_E_BURST8	 0x00040000	   /* ENET: SBUS r/w burst size */
#define DMA_BRST_SZ      0x000c0000        /* SCSI: SBUS r/w burst size */
#define DMA_BRST64       0x00080000        /* SCSI: 64byte bursts (HME on UltraSparc only) */
#define DMA_BRST32       0x00040000        /* SCSI: 32byte bursts */
#define DMA_BRST16       0x00000000        /* SCSI: 16byte bursts */
#define DMA_BRST0        0x00080000        /* SCSI: no bursts (non-HME gate arrays) */
#define DMA_ADDR_DISAB   0x00100000        /* No FIFO drains during addr */
#define DMA_2CLKS        0x00200000        /* Each transfer = 2 clock ticks */
#define DMA_3CLKS        0x00400000        /* Each transfer = 3 clock ticks */
#define DMA_EN_ENETAUI   DMA_3CLKS         /* Put lance into AUI-cable mode */
#define DMA_CNTR_DISAB   0x00800000        /* No IRQ when DMA_TERM_CNTR set */
#define DMA_AUTO_NADDR   0x01000000        /* Use "auto nxt addr" feature */
#define DMA_SCSI_ON      0x02000000        /* Enable SCSI dma */
#define DMA_PARITY_OFF   0x02000000        /* HME: disable parity checking */
#define DMA_LOADED_ADDR  0x04000000        /* Address has been loaded */
#define DMA_LOADED_NADDR 0x08000000        /* Next address has been loaded */

/* Values describing the burst-size property from the PROM */
#define DMA_BURST1       0x01
#define DMA_BURST2       0x02
#define DMA_BURST4       0x04
#define DMA_BURST8       0x08
#define DMA_BURST16      0x10
#define DMA_BURST32      0x20
#define DMA_BURST64      0x40
#define DMA_BURSTBITS    0x7f

/* Determine highest possible final transfer address given a base */
#define DMA_MAXEND(addr) (0x01000000UL-(((unsigned long)(addr))&0x00ffffffUL))

/* Yes, I hack a lot of elisp in my spare time... */
#define DMA_ERROR_P(regs)  ((((regs)->cond_reg) & DMA_HNDL_ERROR))
#define DMA_IRQ_P(regs)    ((((regs)->cond_reg) & (DMA_HNDL_INTR | DMA_HNDL_ERROR)))
#define DMA_WRITE_P(regs)  ((((regs)->cond_reg) & DMA_ST_WRITE))
#define DMA_OFF(regs)      ((((regs)->cond_reg) &= (~DMA_ENABLE)))
#define DMA_INTSOFF(regs)  ((((regs)->cond_reg) &= (~DMA_INT_ENAB)))
#define DMA_INTSON(regs)   ((((regs)->cond_reg) |= (DMA_INT_ENAB)))
#define DMA_PUNTFIFO(regs) ((((regs)->cond_reg) |= DMA_FIFO_INV))
#define DMA_SETSTART(regs, addr)  ((((regs)->st_addr) = (char *) addr))
#define DMA_BEGINDMA_W(regs) \
        ((((regs)->cond_reg |= (DMA_ST_WRITE|DMA_ENABLE|DMA_INT_ENAB))))
#define DMA_BEGINDMA_R(regs) \
        ((((regs)->cond_reg |= ((DMA_ENABLE|DMA_INT_ENAB)&(~DMA_ST_WRITE)))))

/* For certain DMA chips, we need to disable ints upon irq entry
 * and turn them back on when we are done.  So in any ESP interrupt
 * handler you *must* call DMA_IRQ_ENTRY upon entry and DMA_IRQ_EXIT
 * when leaving the handler.  You have been warned...
 */
#define DMA_IRQ_ENTRY(dma, dregs) do { \
        if(DMA_ISBROKEN(dma)) DMA_INTSOFF(dregs); \
   } while (0)

#define DMA_IRQ_EXIT(dma, dregs) do { \
	if(DMA_ISBROKEN(dma)) DMA_INTSON(dregs); \
   } while(0)

/* Reset the friggin' thing... */
#define DMA_RESET(dma) do { \
	struct sparc_dma_registers *regs = dma->regs;                      \
	/* Let the current FIFO drain itself */                            \
	sparc_dma_pause(regs, (DMA_FIFO_ISDRAIN));                         \
	/* Reset the logic */                                              \
	regs->cond_reg |= (DMA_RST_SCSI);     /* assert */                 \
	__delay(400);                         /* let the bits set ;) */    \
	regs->cond_reg &= ~(DMA_RST_SCSI);    /* de-assert */              \
	sparc_dma_enable_interrupts(regs);    /* Re-enable interrupts */   \
	/* Enable FAST transfers if available */                           \
	if(dma->revision>dvmarev1) regs->cond_reg |= DMA_3CLKS;            \
	dma->running = 0;                                                  \
} while(0)


#endif /* !CONFIG_SUN3 */

#endif /* !(__M68K_DVMA_H) */
