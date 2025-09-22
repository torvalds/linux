/* $OpenBSD: tcdsreg.h,v 1.4 2025/06/29 15:55:22 miod Exp $ */
/* $NetBSD: tcdsreg.h,v 1.1 2000/07/04 02:22:20 nisimura Exp $ */

/*
 * Copyright (c) 1994, 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Keith Bostic, Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*
 * Offsets to the SCSI chips
 */
#define	TCDS_SCSI0_OFFSET	0x080000
#define	TCDS_SCSI1_OFFSET	0x080100

/*
 * TCDS register offsets, bit masks.
 */
#define	TCDS_EEPROM		  0x000000	/* EEPROM offset */
#define	TCDS_EEPROM_IDS		  0x000008	/* SCSI IDs offset in EEPROM */

#define	TCDS_CIR		  0x040000	/* CIR offset */

/*
 * TCDS CIR control bits.
 */
#define	TCDS_CIR_GPO_0		0x00000001	/* Not used */
#define	TCDS_CIR_GPO_1		0x00000002	/* Not used */
#define	TCDS_CIR_GPO_2		0x00000004	/* Not used */
#define	TCDS_CIR_STD		0x00000008	/* Serial transmit disable */
#define	TCDS_CIR_GPI_0		0x00000010	/* Not used */
#define	TCDS_CIR_GPI_1		0x00000020	/* Not used */
#define	TCDS_CIR_GPI_2		0x00000040	/* 1 = 25MHz, 0 = 40MHz */
#define	TCDS_CIR_GPI_3		0x00000080	/* Not used */
#define TCDS_CIR_SCSI0_DMAENA	0x00000100	/* SCSI 0 DMA enable */
#define TCDS_CIR_SCSI1_DMAENA	0x00000200	/* SCSI 1 DMA enable */
#define	TCDS_CIR_SCSI0_RESET	0x00000400	/* SCSI 0 reset */
#define	TCDS_CIR_SCSI1_RESET	0x00000800	/* SCSI 1 reset */
#define	TCDS_CIR_SCSI0_DMA_TEST	0x00001000	/* SCSI 0 DMA buf parity test */
#define	TCDS_CIR_SCSI1_DMA_TEST	0x00002000	/* SCSI 1 DMA buf parity test */
#define	TCDS_CIR_DB_PAR		0x00004000	/* DB parity test mode */
#define	TCDS_CIR_TC_PAR		0x00008000	/* TC parity test mode */
#define	TCDS_CIR_ALLCONTROL	0x0000ffff	/* all control bits */

/* TCDS CIR interrupt bits. */
#define	TCDS_CIR_SCSI0_DREQ	0x00010000	/* SCSI 0 DREQ */
#define	TCDS_CIR_SCSI1_DREQ	0x00020000	/* SCSI 1 DREQ */
#define	TCDS_CIR_SCSI0_INT	0x00040000	/* SCSI 0 interrupt */
#define	TCDS_CIR_SCSI1_INT	0x00080000	/* SCSI 1 interrupt */
#define	TCDS_CIR_SCSI0_PREFETCH	0x00100000	/* SCSI 0 prefetch */
#define	TCDS_CIR_SCSI1_PREFETCH	0x00200000	/* SCSI 1 prefetch */
#define	TCDS_CIR_SCSI0_DMA	0x00400000	/* SCSI 0 DMA error */
#define	TCDS_CIR_SCSI1_DMA	0x00800000	/* SCSI 1 DMA error */
#define	TCDS_CIR_SCSI0_DB	0x01000000	/* SCSI 0 DB parity */
#define	TCDS_CIR_SCSI1_DB	0x02000000	/* SCSI 1 DB parity */
#define	TCDS_CIR_SCSI0_DMAB_PAR	0x04000000	/* SCSI 0 DMA buffer parity */
#define	TCDS_CIR_SCSI1_DMAB_PAR	0x08000000	/* SCSI 1 DMA buffer parity */
#define	TCDS_CIR_SCSI0_DMAR_PAR	0x10000000	/* SCSI 0 DMA read parity */
#define	TCDS_CIR_SCSI1_DMAR_PAR	0x20000000	/* SCSI 1 DMA read parity */
#define	TCDS_CIR_TCIOW_PAR	0x40000000	/* TC I/O write parity */
#define	TCDS_CIR_TCIOA_PAR	0x80000000	/* TC I/O address parity */
#define	TCDS_CIR_ALLINTR	0xffff0000	/* all interrupt bits */

#define TCDS_CIR_CLR(c, b)	c = ((c | TCDS_CIR_ALLINTR) & ~b)
#define TCDS_CIR_SET(c, b)	c = ((c | TCDS_CIR_ALLINTR) | b)

/* TCDS IMER masks and enables, for interrupts in the CIR. */
#define	TCDS_IMER_SCSI0_MASK	      0x04	/* SCSI 0 intr/enable mask */
#define	TCDS_IMER_SCSI1_MASK	      0x08	/* SCSI 1 intr/enable mask */
#define	TCDS_IMER_SCSI0_ENB	(TCDS_IMER_SCSI0_MASK << 16)
#define	TCDS_IMER_SCSI1_ENB	(TCDS_IMER_SCSI1_MASK << 16)
#define	TCDS_IMER		  0x040004	/* IMER offset */

#define	TCDS_SCSI0_DMA_ADDR	  0x041000	/* DMA address */
#define	TCDS_SCSI0_DMA_INTR	  0x041004	/* DMA interrupt control */
#define	TCDS_SCSI0_DMA_DUD0	  0x041008	/* DMA unaligned data[0] */
#define	TCDS_SCSI0_DMA_DUD1	  0x04100c	/* DMA unaligned data[1] */

#define	TCDS_SCSI1_DMA_ADDR	  0x041100	/* DMA address */
#define	TCDS_SCSI1_DMA_INTR	  0x041104	/* DMA interrupt control */
#define	TCDS_SCSI1_DMA_DUD0	  0x041108	/* DMA unaligned data[0] */
#define	TCDS_SCSI1_DMA_DUD1	  0x04110c	/* DMA unaligned data[1] */

#define	TCDS_DIC_ADDRMASK	      0x03	/* DMA address bits <1:0> */
#define	TCDS_DIC_READ_PREFETCH	      0x40	/* DMA read prefetch enable */
#define	TCDS_DIC_WRITE		      0x80	/* DMA write */

#define	TCDS_DUD0_VALID00	0x00000001	/* byte 00 valid mask (zero) */
#define	TCDS_DUD0_VALID01	0x00000002	/* byte 01 valid mask */
#define	TCDS_DUD0_VALID10	0x00000004	/* byte 10 valid mask */
#define	TCDS_DUD0_VALID11	0x00000008	/* byte 11 valid mask */
#define	TCDS_DUD0_VALIDBITS	0x0000000f	/* bits that show valid bytes */

#define	TCDS_DUD1_VALID00	0x01000000	/* byte 00 valid mask */
#define	TCDS_DUD1_VALID01	0x02000000	/* byte 01 valid mask */
#define	TCDS_DUD1_VALID10	0x04000000	/* byte 10 valid mask */
#define	TCDS_DUD1_VALID11	0x08000000	/* byte 11 valid mask (zero) */
#define	TCDS_DUD1_VALIDBITS	0x0f000000	/* bits that show valid bytes */

#define	TCDS_DUD_BYTE00		0x000000ff	/* byte 00 mask */
#define	TCDS_DUD_BYTE01		0x0000ff00	/* byte 01 mask */
#define	TCDS_DUD_BYTE10		0x00ff0000	/* byte 10 mask */
#define	TCDS_DUD_BYTE11		0xff000000	/* byte 11 mask */

#if 0
int  tcds_scsi_iserr(struct dma_softc *);
int  tcds_scsi_isintr(int, int);
void tcds_dma_disable(int);
void tcds_dma_enable(int);
void tcds_dma_init(struct dma_softc *, int);
void tcds_scsi_disable(int);
void tcds_scsi_enable(int);
void tcds_scsi_reset(int);

/*
 * XXX
 * Start of MACH #defines, minimal changes to port to NetBSD.
 *
 * The following register is the SCSI control interrupt register.  It
 * starts, stops and resets scsi DMA.  It takes over the SCSI functions
 * that were handled by the ASIC on the 3min.
 */
#define KN15AA_SYS_SCSI		0x1d0000000
#define KN15AA_REG_SCSI_CIR	(KN15AA_SYS_SCSI + 0x80000)
#define SCSI_CIR_AIOPAR		0x80000000 /* TC IO Address parity error */
#define SCSI_CIR_WDIOPAR	0x40000000 /* TC IO  write data parity error */
#define SCSI_CIR_DMARPAR1	0x20000000 /* SCSI[1] TC DMA read data parity */
#define SCSI_CIR_DMARPAR0	0x10000000 /* SCSI[0] TC DMA read data parity */
#define SCSI_CIR_DMABUFPAR1	0x08000000 /* SCSI[1] DMA buffer parity error */
#define SCSI_CIR_DMABUFPAR0	0x04000000 /* SCSI[0] DMA buffer parity error */
#define SCSI_CIR_DBPAR1		0x02000000 /* SCSI[1] DB parity error */
#define SCSI_CIR_DBPAR0		0x01000000 /* SCSI[0] DB parity error */
#define SCSI_CIR_DMAERR1	0x00800000 /* SCSI[1] DMA error */
#define SCSI_CIR_DMAERR0	0x00400000 /* SCSI[0] DMA error */
#if fmm50
#define SCSI_CIR_xxx0		0x00200000 /* RESERVED */
#define SCSI_CIR_xxx1		0x00100000 /* RESERVED */
#else
#define SCSI_CIR_PREF1		0x00200000 /* 53C94 prefetch interrupt */
#define SCSI_CIR_PREF0		0x00100000 /* 53C94 prefetch interrupt */
#endif
#define SCSI_CIR_53C94_INT1	0x00080000 /* SCSI[1] 53C94 Interrupt */
#define SCSI_CIR_53C94_INT0	0x00040000 /* SCSI[0] 53C94 Interrupt */
#define SCSI_CIR_53C94_DREQ1	0x00020000 /* SCSI[1] 53C94 DREQ */
#define SCSI_CIR_53C94_DREQ0	0x00010000 /* SCSI[0] 53C94 DREQ */
#define SCSI_CIR_TC_PAR_TEST	0x00008000 /* TC parity test mode */
#define SCSI_CIR_DB_PAR_TEST	0x00004000 /* DB parity test mode */
#define SCSI_CIR_DBUF_PAR_TEST1	0x00002000 /* SCSI[1] DMA buffer parity test */
#define SCSI_CIR_DBUF_PAR_TEST0	0x00001000 /* SCSI[0] DMA buffer parity test */
#define SCSI_CIR_RESET1		0x00000800 /* SCSI[1] ~Reset,enable(0)/disable(1) */
#define SCSI_CIR_RESET0		0x00000400 /* SCSI[0] ~Reset,enable(0)/disable(1) */
#define SCSI_CIR_DMAENA1	0x00000200 /* SCSI[1] DMA enable */
#define SCSI_CIR_DMAENA0	0x00000100 /* SCSI[1] DMA enable */
#define SCSI_CIR_GPI3		0x00000080 /* General purpose input <3> */
#define SCSI_CIR_GPI2		0x00000040 /* General purpose input <2> */
#define SCSI_CIR_GPI1		0x00000020 /* General purpose input <1> */
#define SCSI_CIR_GPI0		0x00000010 /* General purpose input <0> */
#define SCSI_CIR_TXDIS		0x00000008 /* TXDIS- serial transmit disable */
#define SCSI_CIR_GPO2		0x00000004 /* General purpose output <2> */
#define SCSI_CIR_GPO1		0x00000002 /* General purpose output <1> */
#define SCSI_CIR_GPO0		0x00000001 /* General purpose output <0> */
#define SCSI_CIR_ERROR (SCSI_CIR_AIOPAR | SCSI_CIR_WDIOPAR | SCSI_CIR_DMARPAR1 | SCSI_CIR_DMARPAR0 | SCSI_CIR_DMABUFPAR1 | SCSI_CIR_DMABUFPAR0 | SCSI_CIR_DBPAR1 |SCSI_CIR_DBPAR0 | SCSI_CIR_DMAERR1 | SCSI_CIR_DMAERR0 )

#define KN15AA_REG_SCSI_DMAPTR0 (KN15AA_SYS_SCSI + 0x82000)
#define KN15AA_REG_SCSI_DMAPTR1 (KN15AA_SYS_SCSI + 0x82200)

#define KN15AA_REG_SCSI_DIC0 (KN15AA_SYS_SCSI + 0x82008)
#define KN15AA_REG_SCSI_DIC1 (KN15AA_SYS_SCSI + 0x82208)
#define SCSI_DIC_DMADIR		0x00000080 /* DMA direction read(0)/write(1) */
#define SCSI_DIC_PREFENA	0x00000040 /* DMA read prefetch dis(0)/ena(1) */
#define SCSI_DIC_DMAADDR1	0x00000002 /* DMA address <1> */
#define SCSI_DIC_DMAADDR0	0x00000001 /* DMA address <0> */
#define SCSI_DIC_ADDR_MASK	(SCSI_DIC_DMAADDR0 |SCSI_DIC_DMAADDR1)

#define KN15AA_REG_SCSI_94REG0	(KN15AA_SYS_SCSI + 0x100000)
#define KN15AA_REG_SCSI_94REG1	(KN15AA_SYS_SCSI + 0x100200)

#define KN15AA_REG_SCSI_IMER	(KN15AA_SYS_SCSI + 0x80008)

/* these are the bits that were unaligned at the beginning of the dma */
#define KN15AA_REG_SCSI_DUDB0	(KN15AA_SYS_SCSI + 0x82010)
#define KN15AA_REG_SCSI_DUDB1	(KN15AA_SYS_SCSI + 0x82210)
#	define SCSI_DUDB_MASK01	0x00000001 /* Mask bit for byte[01] */
#	define SCSI_DUDB_MASK10	0x00000002 /* Mask bit for byte[10] */
#	define SCSI_DUDB_MASK11	0x00000004 /* Mask bit for byte[11] */

/* these are the bits that were unaligned at the end of the dma */
#define KN15AA_REG_SCSI_DUDE0	(KN15AA_SYS_SCSI + 0x82018)
#define KN15AA_REG_SCSI_DUDE1	(KN15AA_SYS_SCSI + 0x82218)
#	define SCSI_DUDE_MASK00	0x1000000 /* Mask bit for byte[00] */
#	define SCSI_DUDE_MASK01	0x2000000 /* Mask bit for byte[01] */
#	define SCSI_DUDE_MASK10	0x4000000 /* Mask bit for byte[10] */

#define	SCSI_CIR	ALPHA_PHYS_TO_K0SEG(KN15AA_REG_SCSI_CIR)
#define	SCSI_IMER	ALPHA_PHYS_TO_K0SEG(KN15AA_REG_SCSI_IMER)

#endif
