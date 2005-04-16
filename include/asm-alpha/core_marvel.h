/*
 * Marvel systems use the IO7 I/O chip provides PCI/PCIX/AGP access
 *
 * This file is based on:
 *
 * Marvel / EV7 System Programmer's Manual
 * Revision 1.00
 * 14 May 2001
 */

#ifndef __ALPHA_MARVEL__H__
#define __ALPHA_MARVEL__H__

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/spinlock.h>

#include <asm/compiler.h>

#define MARVEL_MAX_PIDS		 32 /* as long as we rely on 43-bit superpage */
#define MARVEL_IRQ_VEC_PE_SHIFT	(10)
#define MARVEL_IRQ_VEC_IRQ_MASK	((1 << MARVEL_IRQ_VEC_PE_SHIFT) - 1)
#define MARVEL_NR_IRQS		\
	(16 + (MARVEL_MAX_PIDS * (1 << MARVEL_IRQ_VEC_PE_SHIFT)))

/*
 * EV7 RBOX Registers
 */
typedef struct {
	volatile unsigned long csr __attribute__((aligned(16)));
} ev7_csr;

typedef struct {
	ev7_csr	RBOX_CFG;		/* 0x0000 */
	ev7_csr	RBOX_NSVC;
	ev7_csr	RBOX_EWVC;
	ev7_csr	RBOX_WHAMI;
	ev7_csr	RBOX_TCTL;		/* 0x0040 */
	ev7_csr	RBOX_INT;
	ev7_csr	RBOX_IMASK;
	ev7_csr	RBOX_IREQ;
	ev7_csr	RBOX_INTQ;		/* 0x0080 */
	ev7_csr	RBOX_INTA;
	ev7_csr	RBOX_IT;
	ev7_csr	RBOX_SCRATCH1;
	ev7_csr	RBOX_SCRATCH2;		/* 0x00c0 */
	ev7_csr	RBOX_L_ERR;
} ev7_csrs;

/*
 * EV7 CSR addressing macros
 */
#define EV7_MASK40(addr)        ((addr) & ((1UL << 41) - 1))
#define EV7_KERN_ADDR(addr)	((void *)(IDENT_ADDR | EV7_MASK40(addr)))

#define EV7_PE_MASK		0x1ffUL /* 9 bits ( 256 + mem/io ) */
#define EV7_IPE(pe)		((~((long)(pe)) & EV7_PE_MASK) << 35)

#define EV7_CSR_PHYS(pe, off)	(EV7_IPE(pe) | (0x7FFCUL << 20) | (off))
#define EV7_CSRS_PHYS(pe)	(EV7_CSR_PHYS(pe, 0UL))

#define EV7_CSR_KERN(pe, off)	(EV7_KERN_ADDR(EV7_CSR_PHYS(pe, off)))
#define EV7_CSRS_KERN(pe)	(EV7_KERN_ADDR(EV7_CSRS_PHYS(pe)))

#define EV7_CSR_OFFSET(name)	((unsigned long)&((ev7_csrs *)NULL)->name.csr)

/*
 * IO7 registers
 */
typedef struct {
	volatile unsigned long csr __attribute__((aligned(64)));
} io7_csr;

typedef struct {
	/* I/O Port Control Registers */
	io7_csr	POx_CTRL;	       	/* 0x0000 */
	io7_csr	POx_CACHE_CTL;
	io7_csr POx_TIMER;
	io7_csr POx_IO_ADR_EXT;
	io7_csr	POx_MEM_ADR_EXT;	/* 0x0100 */
	io7_csr POx_XCAL_CTRL;
	io7_csr rsvd1[2];	/* ?? spec doesn't show 0x180 */
	io7_csr POx_DM_SOURCE;		/* 0x0200 */
	io7_csr POx_DM_DEST;
	io7_csr POx_DM_SIZE;
	io7_csr POx_DM_CTRL;
	io7_csr rsvd2[4];		/* 0x0300 */

	/* AGP Control Registers -- port 3 only */
	io7_csr AGP_CAP_ID;		/* 0x0400 */
	io7_csr AGP_STAT;
	io7_csr	AGP_CMD;
	io7_csr	rsvd3;

	/* I/O Port Monitor Registers */
	io7_csr	POx_MONCTL;		/* 0x0500 */
	io7_csr POx_CTRA;
	io7_csr POx_CTRB;
	io7_csr POx_CTR56;
	io7_csr POx_SCRATCH;		/* 0x0600 */
	io7_csr POx_XTRA_A;
	io7_csr POx_XTRA_TS;
	io7_csr POx_XTRA_Z;
	io7_csr rsvd4;			/* 0x0700 */
	io7_csr POx_THRESHA;
	io7_csr POx_THRESHB;
	io7_csr rsvd5[33];

	/* System Address Space Window Control Registers */

	io7_csr POx_WBASE[4];		/* 0x1000 */
	io7_csr POx_WMASK[4];
	io7_csr POx_TBASE[4];
	io7_csr POx_SG_TBIA;
	io7_csr POx_MSI_WBASE;
	io7_csr rsvd6[50];

	/* I/O Port Error Registers */
	io7_csr POx_ERR_SUM;
	io7_csr POx_FIRST_ERR;
	io7_csr POx_MSK_HEI;
	io7_csr POx_TLB_ERR;
	io7_csr POx_SPL_COMPLT;
	io7_csr POx_TRANS_SUM;
	io7_csr POx_FRC_PCI_ERR;
	io7_csr POx_MULT_ERR;
	io7_csr rsvd7[8];

	/* I/O Port End of Interrupt Registers */
	io7_csr EOI_DAT;
	io7_csr rsvd8[7];
	io7_csr POx_IACK_SPECIAL;
	io7_csr rsvd9[103];
} io7_ioport_csrs;

typedef struct {
	io7_csr IO_ASIC_REV;		/* 0x30.0000 */
	io7_csr IO_SYS_REV;
	io7_csr SER_CHAIN3;
	io7_csr PO7_RST1;
	io7_csr PO7_RST2;		/* 0x30.0100 */
	io7_csr POx_RST[4];
	io7_csr IO7_DWNH;
	io7_csr IO7_MAF;
	io7_csr IO7_MAF_TO;
	io7_csr IO7_ACC_CLUMP;		/* 0x30.0300 */
	io7_csr IO7_PMASK;
	io7_csr IO7_IOMASK;
	io7_csr IO7_UPH;
	io7_csr IO7_UPH_TO;		/* 0x30.0400 */
	io7_csr RBX_IREQ_OFF;
	io7_csr RBX_INTA_OFF;
	io7_csr INT_RTY;
	io7_csr PO7_MONCTL;		/* 0x30.0500 */
	io7_csr PO7_CTRA;
	io7_csr PO7_CTRB;
	io7_csr PO7_CTR56;
	io7_csr PO7_SCRATCH;		/* 0x30.0600 */
	io7_csr PO7_XTRA_A;
	io7_csr PO7_XTRA_TS;
	io7_csr PO7_XTRA_Z;
	io7_csr PO7_PMASK;		/* 0x30.0700 */
	io7_csr PO7_THRESHA;
	io7_csr PO7_THRESHB;
	io7_csr rsvd1[97];
	io7_csr PO7_ERROR_SUM;		/* 0x30.2000 */
	io7_csr PO7_BHOLE_MASK;
	io7_csr PO7_HEI_MSK;
	io7_csr PO7_CRD_MSK;
	io7_csr PO7_UNCRR_SYM;		/* 0x30.2100 */
	io7_csr PO7_CRRCT_SYM;
	io7_csr PO7_ERR_PKT[2];
	io7_csr PO7_UGBGE_SYM;		/* 0x30.2200 */
	io7_csr rsbv2[887];
	io7_csr PO7_LSI_CTL[128];	/* 0x31.0000 */
	io7_csr rsvd3[123];
	io7_csr HLT_CTL;		/* 0x31.3ec0 */
	io7_csr HPI_CTL;		/* 0x31.3f00 */
	io7_csr CRD_CTL;
	io7_csr STV_CTL;
	io7_csr HEI_CTL;
	io7_csr PO7_MSI_CTL[16];	/* 0x31.4000 */
	io7_csr rsvd4[240];

	/*
	 * Interrupt Diagnostic / Test
	 */
	struct {
		io7_csr INT_PND;
		io7_csr INT_CLR;
		io7_csr INT_EOI;
		io7_csr rsvd[29];
	} INT_DIAG[4];
	io7_csr rsvd5[125];	    	/* 0x31.a000 */
	io7_csr MISC_PND;		/* 0x31.b800 */
	io7_csr rsvd6[31];
	io7_csr MSI_PND[16];		/* 0x31.c000 */
	io7_csr rsvd7[16];
	io7_csr MSI_CLR[16];		/* 0x31.c800 */
} io7_port7_csrs;

/* 
 * IO7 DMA Window Base register (POx_WBASEx)
 */
#define wbase_m_ena  0x1
#define wbase_m_sg   0x2
#define wbase_m_dac  0x4
#define wbase_m_addr 0xFFF00000
union IO7_POx_WBASE {
	struct {
		unsigned ena : 1;	/* <0>			*/
		unsigned sg : 1;	/* <1>			*/
		unsigned dac : 1;	/* <2> -- window 3 only */
		unsigned rsvd1 : 17; 
		unsigned addr : 12;	/* <31:20>		*/
		unsigned rsvd2 : 32;
	} bits;
	unsigned as_long[2];
	unsigned as_quad;
};

/*
 * IO7 IID (Interrupt IDentifier) format
 *
 * For level-sensative interrupts, int_num is encoded as:
 *
 *	bus/port	slot/device	INTx
 *	<7:5>		<4:2>		<1:0>
 */
union IO7_IID {
	struct {
		unsigned int_num : 9;		/* <8:0>    	*/
		unsigned tpu_mask : 4;		/* <12:9> rsvd	*/
		unsigned msi : 1;		/* 13		*/
		unsigned ipe : 10;		/* <23:14>	*/
		unsigned long rsvd : 40;		
	} bits;
	unsigned int as_long[2];
	unsigned long as_quad;
};

/*
 * IO7 addressing macros
 */
#define IO7_KERN_ADDR(addr)	(EV7_KERN_ADDR(addr))

#define IO7_PORT_MASK	   	0x07UL	/* 3 bits of port 	   */

#define IO7_IPE(pe)		(EV7_IPE(pe))
#define IO7_IPORT(port)		((~((long)(port)) & IO7_PORT_MASK) << 32)

#define IO7_HOSE(pe, port)	(IO7_IPE(pe) | IO7_IPORT(port))

#define IO7_MEM_PHYS(pe, port)	(IO7_HOSE(pe, port) | 0x00000000UL)
#define IO7_CONF_PHYS(pe, port)	(IO7_HOSE(pe, port) | 0xFE000000UL)
#define IO7_IO_PHYS(pe, port)	(IO7_HOSE(pe, port) | 0xFF000000UL)
#define IO7_CSR_PHYS(pe, port, off) \
                                (IO7_HOSE(pe, port) | 0xFF800000UL | (off))
#define IO7_CSRS_PHYS(pe, port)	(IO7_CSR_PHYS(pe, port, 0UL))
#define IO7_PORT7_CSRS_PHYS(pe) (IO7_CSR_PHYS(pe, 7, 0x300000UL))

#define IO7_MEM_KERN(pe, port)      (IO7_KERN_ADDR(IO7_MEM_PHYS(pe, port)))
#define IO7_CONF_KERN(pe, port)     (IO7_KERN_ADDR(IO7_CONF_PHYS(pe, port)))
#define IO7_IO_KERN(pe, port)       (IO7_KERN_ADDR(IO7_IO_PHYS(pe, port)))
#define IO7_CSR_KERN(pe, port, off) (IO7_KERN_ADDR(IO7_CSR_PHYS(pe,port,off)))
#define IO7_CSRS_KERN(pe, port)     (IO7_KERN_ADDR(IO7_CSRS_PHYS(pe, port)))
#define IO7_PORT7_CSRS_KERN(pe)	    (IO7_KERN_ADDR(IO7_PORT7_CSRS_PHYS(pe)))

#define IO7_PLL_RNGA(pll)	(((pll) >> 3) & 0x7)
#define IO7_PLL_RNGB(pll)	(((pll) >> 6) & 0x7)

#define IO7_MEM_SPACE		(2UL * 1024 * 1024 * 1024)	/* 2GB MEM */
#define IO7_IO_SPACE		(8UL * 1024 * 1024)		/* 8MB I/O */

 
/* 
 * Offset between ram physical addresses and pci64 DAC addresses
 */
#define IO7_DAC_OFFSET		(1UL << 49)

/*
 * This is needed to satisify the IO() macro used in initializing the machvec
 */
#define MARVEL_IACK_SC 							\
        ((unsigned long)						\
	 (&(((io7_ioport_csrs *)IO7_CSRS_KERN(0, 0))->POx_IACK_SPECIAL)))

#ifdef __KERNEL__

/*
 * IO7 structs
 */
#define IO7_NUM_PORTS 4
#define IO7_AGP_PORT  3

struct io7_port {
	struct io7 *io7;
	struct pci_controller *hose;

	int enabled;
	unsigned int port;
	io7_ioport_csrs *csrs;

	unsigned long saved_wbase[4];
	unsigned long saved_wmask[4];
	unsigned long saved_tbase[4];
};

struct io7 {
	struct io7 *next;

	unsigned int pe;
	io7_port7_csrs *csrs;
	struct io7_port ports[IO7_NUM_PORTS];

	spinlock_t irq_lock;
};

#ifndef __EXTERN_INLINE
# define __EXTERN_INLINE extern inline
# define __IO_EXTERN_INLINE
#endif

/*
 * I/O functions. All access through linear space.
 */

/*
 * Memory functions.  All accesses through linear space.
 */

#define vucp	volatile unsigned char __force *
#define vusp	volatile unsigned short __force *

extern unsigned int marvel_ioread8(void __iomem *);
extern void marvel_iowrite8(u8 b, void __iomem *);

__EXTERN_INLINE unsigned int marvel_ioread16(void __iomem *addr)
{
	return __kernel_ldwu(*(vusp)addr);
}

__EXTERN_INLINE void marvel_iowrite16(u16 b, void __iomem *addr)
{
	__kernel_stw(b, *(vusp)addr);
}

extern void __iomem *marvel_ioremap(unsigned long addr, unsigned long size);
extern void marvel_iounmap(volatile void __iomem *addr);
extern void __iomem *marvel_ioportmap (unsigned long addr);

__EXTERN_INLINE int marvel_is_ioaddr(unsigned long addr)
{
	return (addr >> 40) & 1;
}

extern int marvel_is_mmio(const volatile void __iomem *);

#undef vucp
#undef vusp

#undef __IO_PREFIX
#define __IO_PREFIX		marvel
#define marvel_trivial_rw_bw	1
#define marvel_trivial_rw_lq	1
#define marvel_trivial_io_bw	0
#define marvel_trivial_io_lq	1
#define marvel_trivial_iounmap	0
#include <asm/io_trivial.h>

#ifdef __IO_EXTERN_INLINE
# undef __EXTERN_INLINE
# undef __IO_EXTERN_INLINE
#endif

#endif /* __KERNEL__ */

#endif /* __ALPHA_MARVEL__H__ */
