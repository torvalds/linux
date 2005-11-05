/*
 * linux/include/asm/arch-iop3xx/iop321.h
 *
 * Intel IOP321 Chip definitions
 *
 * Author: Rory Bolt <rorybolt@pacbell.net>
 * Copyright (C) 2002 Rory Bolt
 * Copyright (C) 2004 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IOP321_HW_H_
#define _IOP321_HW_H_


/*
 * This is needed for mixed drivers that need to work on all
 * IOP3xx variants but behave slightly differently on each.
 */
#ifndef __ASSEMBLY__
#ifdef	CONFIG_ARCH_IOP321
#define iop_is_321() (((processor_id & 0xfffff5e0) == 0x69052420))
#else
#define	iop_is_321()	0
#endif
#endif

/*
 * IOP321 I/O and Mem space regions for PCI autoconfiguration
 */
#define IOP321_PCI_IO_WINDOW_SIZE   0x00010000
#define IOP321_PCI_LOWER_IO_PA      0x90000000
#define IOP321_PCI_LOWER_IO_VA      0xfe000000
#define IOP321_PCI_LOWER_IO_BA      (*IOP321_OIOWTVR)
#define IOP321_PCI_UPPER_IO_PA      (IOP321_PCI_LOWER_IO_PA + IOP321_PCI_IO_WINDOW_SIZE - 1)
#define IOP321_PCI_UPPER_IO_VA      (IOP321_PCI_LOWER_IO_VA + IOP321_PCI_IO_WINDOW_SIZE - 1)
#define IOP321_PCI_UPPER_IO_BA      (IOP321_PCI_LOWER_IO_BA + IOP321_PCI_IO_WINDOW_SIZE - 1)
#define IOP321_PCI_IO_OFFSET        (IOP321_PCI_LOWER_IO_VA - IOP321_PCI_LOWER_IO_BA)

/* #define IOP321_PCI_MEM_WINDOW_SIZE  (~*IOP321_IALR1 + 1) */
#define IOP321_PCI_MEM_WINDOW_SIZE  0x04000000 /* 64M outbound window */
#define IOP321_PCI_LOWER_MEM_PA     0x80000000
#define IOP321_PCI_LOWER_MEM_BA     (*IOP321_OMWTVR0)
#define IOP321_PCI_UPPER_MEM_PA     (IOP321_PCI_LOWER_MEM_PA + IOP321_PCI_MEM_WINDOW_SIZE - 1)
#define IOP321_PCI_UPPER_MEM_BA     (IOP321_PCI_LOWER_MEM_BA + IOP321_PCI_MEM_WINDOW_SIZE - 1)
#define IOP321_PCI_MEM_OFFSET       (IOP321_PCI_LOWER_MEM_PA - IOP321_PCI_LOWER_MEM_BA)


/*
 * IOP321 chipset registers
 */
#define IOP321_VIRT_MEM_BASE 0xfeffe000  /* chip virtual mem address*/
#define IOP321_PHYS_MEM_BASE 0xffffe000  /* chip physical memory address */
#define IOP321_REG_ADDR(reg) (IOP321_VIRT_MEM_BASE | (reg))

/* Reserved 0x00000000 through 0x000000FF */

/* Address Translation Unit 0x00000100 through 0x000001FF */
#define IOP321_ATUVID     (volatile u16 *)IOP321_REG_ADDR(0x00000100)
#define IOP321_ATUDID     (volatile u16 *)IOP321_REG_ADDR(0x00000102)
#define IOP321_ATUCMD     (volatile u16 *)IOP321_REG_ADDR(0x00000104)
#define IOP321_ATUSR      (volatile u16 *)IOP321_REG_ADDR(0x00000106)
#define IOP321_ATURID     (volatile u8  *)IOP321_REG_ADDR(0x00000108)
#define IOP321_ATUCCR     (volatile u32 *)IOP321_REG_ADDR(0x00000109)
#define IOP321_ATUCLSR    (volatile u8  *)IOP321_REG_ADDR(0x0000010C)
#define IOP321_ATULT      (volatile u8  *)IOP321_REG_ADDR(0x0000010D)
#define IOP321_ATUHTR     (volatile u8  *)IOP321_REG_ADDR(0x0000010E)
#define IOP321_ATUBIST    (volatile u8  *)IOP321_REG_ADDR(0x0000010F)
#define IOP321_IABAR0     (volatile u32 *)IOP321_REG_ADDR(0x00000110)
#define IOP321_IAUBAR0    (volatile u32 *)IOP321_REG_ADDR(0x00000114)
#define IOP321_IABAR1     (volatile u32 *)IOP321_REG_ADDR(0x00000118)
#define IOP321_IAUBAR1    (volatile u32 *)IOP321_REG_ADDR(0x0000011C)
#define IOP321_IABAR2     (volatile u32 *)IOP321_REG_ADDR(0x00000120)
#define IOP321_IAUBAR2    (volatile u32 *)IOP321_REG_ADDR(0x00000124)
#define IOP321_ASVIR      (volatile u16 *)IOP321_REG_ADDR(0x0000012C)
#define IOP321_ASIR       (volatile u16 *)IOP321_REG_ADDR(0x0000012E)
#define IOP321_ERBAR      (volatile u32 *)IOP321_REG_ADDR(0x00000130)
/* Reserved 0x00000134 through 0x0000013B */
#define IOP321_ATUILR     (volatile u8  *)IOP321_REG_ADDR(0x0000013C)
#define IOP321_ATUIPR     (volatile u8  *)IOP321_REG_ADDR(0x0000013D)
#define IOP321_ATUMGNT    (volatile u8  *)IOP321_REG_ADDR(0x0000013E)
#define IOP321_ATUMLAT    (volatile u8  *)IOP321_REG_ADDR(0x0000013F)
#define IOP321_IALR0      (volatile u32 *)IOP321_REG_ADDR(0x00000140)
#define IOP321_IATVR0     (volatile u32 *)IOP321_REG_ADDR(0x00000144)
#define IOP321_ERLR       (volatile u32 *)IOP321_REG_ADDR(0x00000148)
#define IOP321_ERTVR      (volatile u32 *)IOP321_REG_ADDR(0x0000014C)
#define IOP321_IALR1      (volatile u32 *)IOP321_REG_ADDR(0x00000150)
#define IOP321_IALR2      (volatile u32 *)IOP321_REG_ADDR(0x00000154)
#define IOP321_IATVR2     (volatile u32 *)IOP321_REG_ADDR(0x00000158)
#define IOP321_OIOWTVR    (volatile u32 *)IOP321_REG_ADDR(0x0000015C)
#define IOP321_OMWTVR0    (volatile u32 *)IOP321_REG_ADDR(0x00000160)
#define IOP321_OUMWTVR0   (volatile u32 *)IOP321_REG_ADDR(0x00000164)
#define IOP321_OMWTVR1    (volatile u32 *)IOP321_REG_ADDR(0x00000168)
#define IOP321_OUMWTVR1   (volatile u32 *)IOP321_REG_ADDR(0x0000016C)
/* Reserved 0x00000170 through 0x00000177*/
#define IOP321_OUDWTVR    (volatile u32 *)IOP321_REG_ADDR(0x00000178)
/* Reserved 0x0000017C through 0x0000017F*/
#define IOP321_ATUCR      (volatile u32 *)IOP321_REG_ADDR(0x00000180)
#define IOP321_PCSR       (volatile u32 *)IOP321_REG_ADDR(0x00000184)
#define IOP321_ATUISR     (volatile u32 *)IOP321_REG_ADDR(0x00000188)
#define IOP321_ATUIMR     (volatile u32 *)IOP321_REG_ADDR(0x0000018C)
#define IOP321_IABAR3     (volatile u32 *)IOP321_REG_ADDR(0x00000190)
#define IOP321_IAUBAR3    (volatile u32 *)IOP321_REG_ADDR(0x00000194)
#define IOP321_IALR3      (volatile u32 *)IOP321_REG_ADDR(0x00000198)
#define IOP321_IATVR3     (volatile u32 *)IOP321_REG_ADDR(0x0000019C)
/* Reserved 0x000001A0 through 0x000001A3*/
#define IOP321_OCCAR      (volatile u32 *)IOP321_REG_ADDR(0x000001A4)
/* Reserved 0x000001A8 through 0x000001AB*/
#define IOP321_OCCDR      (volatile u32 *)IOP321_REG_ADDR(0x000001AC)
/* Reserved 0x000001B0 through 0x000001BB*/
#define IOP321_PDSCR      (volatile u32 *)IOP321_REG_ADDR(0x000001BC)
#define IOP321_PMCAPID    (volatile u8  *)IOP321_REG_ADDR(0x000001C0)
#define IOP321_PMNEXT     (volatile u8  *)IOP321_REG_ADDR(0x000001C1)
#define IOP321_APMCR      (volatile u16 *)IOP321_REG_ADDR(0x000001C2)
#define IOP321_APMCSR     (volatile u16 *)IOP321_REG_ADDR(0x000001C4)
/* Reserved 0x000001C6 through 0x000001DF */
#define IOP321_PCIXCAPID  (volatile u8  *)IOP321_REG_ADDR(0x000001E0)
#define IOP321_PCIXNEXT   (volatile u8  *)IOP321_REG_ADDR(0x000001E1)
#define IOP321_PCIXCMD    (volatile u16 *)IOP321_REG_ADDR(0x000001E2)
#define IOP321_PCIXSR     (volatile u32 *)IOP321_REG_ADDR(0x000001E4)
#define IOP321_PCIIRSR    (volatile u32 *)IOP321_REG_ADDR(0x000001EC)

/* Messaging Unit 0x00000300 through 0x000003FF */

/* Reserved 0x00000300 through 0x0000030c */
#define IOP321_IMR0       (volatile u32 *)IOP321_REG_ADDR(0x00000310)
#define IOP321_IMR1       (volatile u32 *)IOP321_REG_ADDR(0x00000314)
#define IOP321_OMR0       (volatile u32 *)IOP321_REG_ADDR(0x00000318)
#define IOP321_OMR1       (volatile u32 *)IOP321_REG_ADDR(0x0000031C)
#define IOP321_IDR        (volatile u32 *)IOP321_REG_ADDR(0x00000320)
#define IOP321_IISR       (volatile u32 *)IOP321_REG_ADDR(0x00000324)
#define IOP321_IIMR       (volatile u32 *)IOP321_REG_ADDR(0x00000328)
#define IOP321_ODR        (volatile u32 *)IOP321_REG_ADDR(0x0000032C)
#define IOP321_OISR       (volatile u32 *)IOP321_REG_ADDR(0x00000330)
#define IOP321_OIMR       (volatile u32 *)IOP321_REG_ADDR(0x00000334)
/* Reserved 0x00000338 through 0x0000034F */
#define IOP321_MUCR       (volatile u32 *)IOP321_REG_ADDR(0x00000350)
#define IOP321_QBAR       (volatile u32 *)IOP321_REG_ADDR(0x00000354)
/* Reserved 0x00000358 through 0x0000035C */
#define IOP321_IFHPR      (volatile u32 *)IOP321_REG_ADDR(0x00000360)
#define IOP321_IFTPR      (volatile u32 *)IOP321_REG_ADDR(0x00000364)
#define IOP321_IPHPR      (volatile u32 *)IOP321_REG_ADDR(0x00000368)
#define IOP321_IPTPR      (volatile u32 *)IOP321_REG_ADDR(0x0000036C)
#define IOP321_OFHPR      (volatile u32 *)IOP321_REG_ADDR(0x00000370)
#define IOP321_OFTPR      (volatile u32 *)IOP321_REG_ADDR(0x00000374)
#define IOP321_OPHPR      (volatile u32 *)IOP321_REG_ADDR(0x00000378)
#define IOP321_OPTPR      (volatile u32 *)IOP321_REG_ADDR(0x0000037C)
#define IOP321_IAR        (volatile u32 *)IOP321_REG_ADDR(0x00000380)

#define IOP321_IIxR_MASK	0x7f /* masks all */
#define IOP321_IIxR_IRI		0x40 /* RC Index Register Interrupt */
#define IOP321_IIxR_OFQF	0x20 /* RC Output Free Q Full (ERROR) */
#define IOP321_IIxR_ipq		0x10 /* RC Inbound Post Q (post) */
#define IOP321_IIxR_ERRDI	0x08 /* RO Error Doorbell Interrupt */
#define IOP321_IIxR_IDI		0x04 /* RO Inbound Doorbell Interrupt */
#define IOP321_IIxR_IM1		0x02 /* RC Inbound Message 1 Interrupt */
#define IOP321_IIxR_IM0		0x01 /* RC Inbound Message 0 Interrupt */

/* Reserved 0x00000384 through 0x000003FF */

/* DMA Controller 0x00000400 through 0x000004FF */
#define IOP321_DMA0_CCR   (volatile u32 *)IOP321_REG_ADDR(0x00000400)
#define IOP321_DMA0_CSR   (volatile u32 *)IOP321_REG_ADDR(0x00000404)
#define IOP321_DMA0_DAR   (volatile u32 *)IOP321_REG_ADDR(0x0000040C)
#define IOP321_DMA0_NDAR  (volatile u32 *)IOP321_REG_ADDR(0x00000410)
#define IOP321_DMA0_PADR  (volatile u32 *)IOP321_REG_ADDR(0x00000414)
#define IOP321_DMA0_PUADR (volatile u32 *)IOP321_REG_ADDR(0x00000418)
#define IOP321_DMA0_LADR  (volatile u32 *)IOP321_REG_ADDR(0X0000041C)
#define IOP321_DMA0_BCR   (volatile u32 *)IOP321_REG_ADDR(0x00000420)
#define IOP321_DMA0_DCR   (volatile u32 *)IOP321_REG_ADDR(0x00000424)
/* Reserved 0x00000428 through 0x0000043C */
#define IOP321_DMA1_CCR   (volatile u32 *)IOP321_REG_ADDR(0x00000440)
#define IOP321_DMA1_CSR   (volatile u32 *)IOP321_REG_ADDR(0x00000444)
#define IOP321_DMA1_DAR   (volatile u32 *)IOP321_REG_ADDR(0x0000044C)
#define IOP321_DMA1_NDAR  (volatile u32 *)IOP321_REG_ADDR(0x00000450)
#define IOP321_DMA1_PADR  (volatile u32 *)IOP321_REG_ADDR(0x00000454)
#define IOP321_DMA1_PUADR (volatile u32 *)IOP321_REG_ADDR(0x00000458)
#define IOP321_DMA1_LADR  (volatile u32 *)IOP321_REG_ADDR(0x0000045C)
#define IOP321_DMA1_BCR   (volatile u32 *)IOP321_REG_ADDR(0x00000460)
#define IOP321_DMA1_DCR   (volatile u32 *)IOP321_REG_ADDR(0x00000464)
/* Reserved 0x00000468 through 0x000004FF */

/* Memory controller 0x00000500 through 0x0005FF */

/* Peripheral bus interface unit 0x00000680 through 0x0006FF */
#define IOP321_PBCR       (volatile u32 *)IOP321_REG_ADDR(0x00000680)
#define IOP321_PBISR      (volatile u32 *)IOP321_REG_ADDR(0x00000684)
#define IOP321_PBBAR0     (volatile u32 *)IOP321_REG_ADDR(0x00000688)
#define IOP321_PBLR0      (volatile u32 *)IOP321_REG_ADDR(0x0000068C)
#define IOP321_PBBAR1     (volatile u32 *)IOP321_REG_ADDR(0x00000690)
#define IOP321_PBLR1      (volatile u32 *)IOP321_REG_ADDR(0x00000694)
#define IOP321_PBBAR2     (volatile u32 *)IOP321_REG_ADDR(0x00000698)
#define IOP321_PBLR2      (volatile u32 *)IOP321_REG_ADDR(0x0000069C)
#define IOP321_PBBAR3     (volatile u32 *)IOP321_REG_ADDR(0x000006A0)
#define IOP321_PBLR3      (volatile u32 *)IOP321_REG_ADDR(0x000006A4)
#define IOP321_PBBAR4     (volatile u32 *)IOP321_REG_ADDR(0x000006A8)
#define IOP321_PBLR4      (volatile u32 *)IOP321_REG_ADDR(0x000006AC)
#define IOP321_PBBAR5     (volatile u32 *)IOP321_REG_ADDR(0x000006B0)
#define IOP321_PBLR5      (volatile u32 *)IOP321_REG_ADDR(0x000006B4)
#define IOP321_PBDSCR     (volatile u32 *)IOP321_REG_ADDR(0x000006B8)
/* Reserved 0x000006BC */
#define IOP321_PMBR0      (volatile u32 *)IOP321_REG_ADDR(0x000006C0)
/* Reserved 0x000006C4 through 0x000006DC */
#define IOP321_PMBR1      (volatile u32 *)IOP321_REG_ADDR(0x000006E0)
#define IOP321_PMBR2      (volatile u32 *)IOP321_REG_ADDR(0x000006E4)

#define IOP321_PBCR_EN    0x1

#define IOP321_PBISR_BOOR_ERR 0x1

/* Peripheral performance monitoring unit 0x00000700 through 0x00077F */
#define IOP321_GTMR	(volatile u32 *)IOP321_REG_ADDR(0x00000700)
#define IOP321_ESR	(volatile u32 *)IOP321_REG_ADDR(0x00000704)
#define IOP321_EMISR	(volatile u32 *)IOP321_REG_ADDR(0x00000708)
/* reserved 0x00000070c */
#define IOP321_GTSR	(volatile u32 *)IOP321_REG_ADDR(0x00000710)
/* PERC0 DOESN'T EXIST - index from 1! */
#define IOP321_PERCR0	(volatile u32 *)IOP321_REG_ADDR(0x00000710)

#define IOP321_GTMR_NGCE	0x04 /* (Not) Global Counter Enable */

/* Internal arbitration unit 0x00000780 through 0x0007BF */
#define IOP321_IACR	(volatile u32 *)IOP321_REG_ADDR(0x00000780)
#define IOP321_MTTR1	(volatile u32 *)IOP321_REG_ADDR(0x00000784)
#define IOP321_MTTR2	(volatile u32 *)IOP321_REG_ADDR(0x00000788)

/* General Purpose I/O Registers */
#define IOP321_GPOE       (volatile u32 *)IOP321_REG_ADDR(0x000007C4)
#define IOP321_GPID       (volatile u32 *)IOP321_REG_ADDR(0x000007C8)
#define IOP321_GPOD       (volatile u32 *)IOP321_REG_ADDR(0x000007CC)

/* Interrupt Controller */
#define IOP321_INTCTL     (volatile u32 *)IOP321_REG_ADDR(0x000007D0)
#define IOP321_INTSTR     (volatile u32 *)IOP321_REG_ADDR(0x000007D4)
#define IOP321_IINTSRC    (volatile u32 *)IOP321_REG_ADDR(0x000007D8)
#define IOP321_FINTSRC    (volatile u32 *)IOP321_REG_ADDR(0x000007DC)

/* Timers */

#define IOP321_TU_TMR0		(volatile u32 *)IOP321_REG_ADDR(0x000007E0)
#define IOP321_TU_TMR1		(volatile u32 *)IOP321_REG_ADDR(0x000007E4)

#ifdef CONFIG_ARCH_IQ80321
#define	IOP321_TICK_RATE	200000000	/* 200 MHz clock */
#elif defined(CONFIG_ARCH_IQ31244)
#define IOP321_TICK_RATE	198000000	/* 33.000 MHz crystal */
#endif

#ifdef CONFIG_ARCH_EP80219
#undef IOP321_TICK_RATE
#define IOP321_TICK_RATE 200000000 /* 33.333333 Mhz crystal */
#endif

#define IOP321_TMR_TC		0x01
#define	IOP321_TMR_EN		0x02
#define IOP321_TMR_RELOAD	0x04
#define	IOP321_TMR_PRIVILEGED	0x09

#define	IOP321_TMR_RATIO_1_1	0x00
#define	IOP321_TMR_RATIO_4_1	0x10
#define	IOP321_TMR_RATIO_8_1	0x20
#define	IOP321_TMR_RATIO_16_1	0x30

#define IOP321_TU_TCR0    (volatile u32 *)IOP321_REG_ADDR(0x000007E8)
#define IOP321_TU_TCR1    (volatile u32 *)IOP321_REG_ADDR(0x000007EC)
#define IOP321_TU_TRR0    (volatile u32 *)IOP321_REG_ADDR(0x000007F0)
#define IOP321_TU_TRR1    (volatile u32 *)IOP321_REG_ADDR(0x000007F4)
#define IOP321_TU_TISR    (volatile u32 *)IOP321_REG_ADDR(0x000007F8)
#define IOP321_TU_WDTCR   (volatile u32 *)IOP321_REG_ADDR(0x000007FC)

/* Application accelerator unit 0x00000800 - 0x000008FF */
#define IOP321_AAU_ACR     (volatile u32 *)IOP321_REG_ADDR(0x00000800)
#define IOP321_AAU_ASR     (volatile u32 *)IOP321_REG_ADDR(0x00000804)
#define IOP321_AAU_ADAR    (volatile u32 *)IOP321_REG_ADDR(0x00000808)
#define IOP321_AAU_ANDAR   (volatile u32 *)IOP321_REG_ADDR(0x0000080C)
#define IOP321_AAU_SAR1    (volatile u32 *)IOP321_REG_ADDR(0x00000810)
#define IOP321_AAU_SAR2    (volatile u32 *)IOP321_REG_ADDR(0x00000814)
#define IOP321_AAU_SAR3    (volatile u32 *)IOP321_REG_ADDR(0x00000818)
#define IOP321_AAU_SAR4    (volatile u32 *)IOP321_REG_ADDR(0x0000081C)
#define IOP321_AAU_SAR5    (volatile u32 *)IOP321_REG_ADDR(0x0000082C)
#define IOP321_AAU_SAR6    (volatile u32 *)IOP321_REG_ADDR(0x00000830)
#define IOP321_AAU_SAR7    (volatile u32 *)IOP321_REG_ADDR(0x00000834)
#define IOP321_AAU_SAR8    (volatile u32 *)IOP321_REG_ADDR(0x00000838)
#define IOP321_AAU_SAR9    (volatile u32 *)IOP321_REG_ADDR(0x00000840)
#define IOP321_AAU_SAR10   (volatile u32 *)IOP321_REG_ADDR(0x00000844)
#define IOP321_AAU_SAR11   (volatile u32 *)IOP321_REG_ADDR(0x00000848)
#define IOP321_AAU_SAR12   (volatile u32 *)IOP321_REG_ADDR(0x0000084C)
#define IOP321_AAU_SAR13   (volatile u32 *)IOP321_REG_ADDR(0x00000850)
#define IOP321_AAU_SAR14   (volatile u32 *)IOP321_REG_ADDR(0x00000854)
#define IOP321_AAU_SAR15   (volatile u32 *)IOP321_REG_ADDR(0x00000858)
#define IOP321_AAU_SAR16   (volatile u32 *)IOP321_REG_ADDR(0x0000085C)
#define IOP321_AAU_SAR17   (volatile u32 *)IOP321_REG_ADDR(0x00000864)
#define IOP321_AAU_SAR18   (volatile u32 *)IOP321_REG_ADDR(0x00000868)
#define IOP321_AAU_SAR19   (volatile u32 *)IOP321_REG_ADDR(0x0000086C)
#define IOP321_AAU_SAR20   (volatile u32 *)IOP321_REG_ADDR(0x00000870)
#define IOP321_AAU_SAR21   (volatile u32 *)IOP321_REG_ADDR(0x00000874)
#define IOP321_AAU_SAR22   (volatile u32 *)IOP321_REG_ADDR(0x00000878)
#define IOP321_AAU_SAR23   (volatile u32 *)IOP321_REG_ADDR(0x0000087C)
#define IOP321_AAU_SAR24   (volatile u32 *)IOP321_REG_ADDR(0x00000880)
#define IOP321_AAU_SAR25   (volatile u32 *)IOP321_REG_ADDR(0x00000888)
#define IOP321_AAU_SAR26   (volatile u32 *)IOP321_REG_ADDR(0x0000088C)
#define IOP321_AAU_SAR27   (volatile u32 *)IOP321_REG_ADDR(0x00000890)
#define IOP321_AAU_SAR28   (volatile u32 *)IOP321_REG_ADDR(0x00000894)
#define IOP321_AAU_SAR29   (volatile u32 *)IOP321_REG_ADDR(0x00000898)
#define IOP321_AAU_SAR30   (volatile u32 *)IOP321_REG_ADDR(0x0000089C)
#define IOP321_AAU_SAR31   (volatile u32 *)IOP321_REG_ADDR(0x000008A0)
#define IOP321_AAU_SAR32   (volatile u32 *)IOP321_REG_ADDR(0x000008A4)
#define IOP321_AAU_DAR     (volatile u32 *)IOP321_REG_ADDR(0x00000820)
#define IOP321_AAU_ABCR    (volatile u32 *)IOP321_REG_ADDR(0x00000824)
#define IOP321_AAU_ADCR    (volatile u32 *)IOP321_REG_ADDR(0x00000828)
#define IOP321_AAU_EDCR0   (volatile u32 *)IOP321_REG_ADDR(0x0000083c)
#define IOP321_AAU_EDCR1   (volatile u32 *)IOP321_REG_ADDR(0x00000860)
#define IOP321_AAU_EDCR2   (volatile u32 *)IOP321_REG_ADDR(0x00000884)


/* SSP serial port unit 0x00001600 - 0x0000167F */
/* I2C bus interface unit 0x00001680 - 0x000016FF */
#define IOP321_ICR0       (volatile u32 *)IOP321_REG_ADDR(0x00001680)
#define IOP321_ISR0       (volatile u32 *)IOP321_REG_ADDR(0x00001684)
#define IOP321_ISAR0      (volatile u32 *)IOP321_REG_ADDR(0x00001688)
#define IOP321_IDBR0      (volatile u32 *)IOP321_REG_ADDR(0x0000168C)
/* Reserved 0x00001690 */
#define IOP321_IBMR0      (volatile u32 *)IOP321_REG_ADDR(0x00001694)
/* Reserved 0x00001698 */
/* Reserved 0x0000169C */
#define IOP321_ICR1       (volatile u32 *)IOP321_REG_ADDR(0x000016A0)
#define IOP321_ISR1       (volatile u32 *)IOP321_REG_ADDR(0x000016A4)
#define IOP321_ISAR1      (volatile u32 *)IOP321_REG_ADDR(0x000016A8)
#define IOP321_IDBR1      (volatile u32 *)IOP321_REG_ADDR(0x000016AC)
#define IOP321_IBMR1      (volatile u32 *)IOP321_REG_ADDR(0x000016B4)
/* Reserved 0x000016B8 through 0x000016FC */

/* for I2C bit defs see drivers/i2c/i2c-iop3xx.h */


#ifndef __ASSEMBLY__
extern void iop321_map_io(void);
extern void iop321_init_irq(void);
extern void iop321_time_init(void);
#endif

#endif // _IOP321_HW_H_
