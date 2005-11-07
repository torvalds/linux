/*
 * linux/include/asm/arch-iop3xx/iop331.h
 *
 * Intel IOP331 Chip definitions
 *
 * Author: Dave Jiang (dave.jiang@intel.com)
 * Copyright (C) 2003, 2004 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _IOP331_HW_H_
#define _IOP331_HW_H_


/*
 * This is needed for mixed drivers that need to work on all
 * IOP3xx variants but behave slightly differently on each.
 */
#ifndef __ASSEMBLY__
#ifdef	CONFIG_ARCH_IOP331
/*#define	iop_is_331()	((processor_id & 0xffffffb0) == 0x69054090) */
#define	iop_is_331()	((processor_id & 0xffffff30) == 0x69054010)
#else
#define	iop_is_331()	0
#endif
#endif

/*
 * IOP331 I/O and Mem space regions for PCI autoconfiguration
 */
#define IOP331_PCI_IO_WINDOW_SIZE   0x00010000
#define IOP331_PCI_LOWER_IO_PA      0x90000000
#define IOP331_PCI_LOWER_IO_VA      0xfe000000
#define IOP331_PCI_LOWER_IO_BA      (*IOP331_OIOWTVR)
#define IOP331_PCI_UPPER_IO_PA      (IOP331_PCI_LOWER_IO_PA + IOP331_PCI_IO_WINDOW_SIZE - 1)
#define IOP331_PCI_UPPER_IO_VA      (IOP331_PCI_LOWER_IO_VA + IOP331_PCI_IO_WINDOW_SIZE - 1)
#define IOP331_PCI_UPPER_IO_BA      (IOP331_PCI_LOWER_IO_BA + IOP331_PCI_IO_WINDOW_SIZE - 1)
#define IOP331_PCI_IO_OFFSET        (IOP331_PCI_LOWER_IO_VA - IOP331_PCI_LOWER_IO_BA)

/* this can be 128M if OMWTVR1 is set */
#define IOP331_PCI_MEM_WINDOW_SIZE	0x04000000 /* 64M outbound window */
/* #define IOP331_PCI_MEM_WINDOW_SIZE  (~*IOP331_IALR1 + 1) */
#define IOP331_PCI_LOWER_MEM_PA     0x80000000
#define IOP331_PCI_LOWER_MEM_BA     (*IOP331_OMWTVR0)
#define IOP331_PCI_UPPER_MEM_PA     (IOP331_PCI_LOWER_MEM_PA + IOP331_PCI_MEM_WINDOW_SIZE - 1)
#define IOP331_PCI_UPPER_MEM_BA     (IOP331_PCI_LOWER_MEM_BA + IOP331_PCI_MEM_WINDOW_SIZE - 1)
#define IOP331_PCI_MEM_OFFSET       (IOP331_PCI_LOWER_MEM_PA - IOP331_PCI_LOWER_MEM_BA)

/*
 * IOP331 chipset registers
 */
#define IOP331_VIRT_MEM_BASE  0xfeffe000  /* chip virtual mem address*/
#define IOP331_PHYS_MEM_BASE  0xffffe000  /* chip physical memory address */
#define IOP331_REG_ADDR(reg) (IOP331_VIRT_MEM_BASE | (reg))

/* Reserved 0x00000000 through 0x000000FF */

/* Address Translation Unit 0x00000100 through 0x000001FF */
#define IOP331_ATUVID     (volatile u16 *)IOP331_REG_ADDR(0x00000100)
#define IOP331_ATUDID     (volatile u16 *)IOP331_REG_ADDR(0x00000102)
#define IOP331_ATUCMD     (volatile u16 *)IOP331_REG_ADDR(0x00000104)
#define IOP331_ATUSR      (volatile u16 *)IOP331_REG_ADDR(0x00000106)
#define IOP331_ATURID     (volatile u8  *)IOP331_REG_ADDR(0x00000108)
#define IOP331_ATUCCR     (volatile u32 *)IOP331_REG_ADDR(0x00000109)
#define IOP331_ATUCLSR    (volatile u8  *)IOP331_REG_ADDR(0x0000010C)
#define IOP331_ATULT      (volatile u8  *)IOP331_REG_ADDR(0x0000010D)
#define IOP331_ATUHTR     (volatile u8  *)IOP331_REG_ADDR(0x0000010E)
#define IOP331_ATUBIST    (volatile u8  *)IOP331_REG_ADDR(0x0000010F)
#define IOP331_IABAR0     (volatile u32 *)IOP331_REG_ADDR(0x00000110)
#define IOP331_IAUBAR0    (volatile u32 *)IOP331_REG_ADDR(0x00000114)
#define IOP331_IABAR1     (volatile u32 *)IOP331_REG_ADDR(0x00000118)
#define IOP331_IAUBAR1    (volatile u32 *)IOP331_REG_ADDR(0x0000011C)
#define IOP331_IABAR2     (volatile u32 *)IOP331_REG_ADDR(0x00000120)
#define IOP331_IAUBAR2    (volatile u32 *)IOP331_REG_ADDR(0x00000124)
#define IOP331_ASVIR      (volatile u16 *)IOP331_REG_ADDR(0x0000012C)
#define IOP331_ASIR       (volatile u16 *)IOP331_REG_ADDR(0x0000012E)
#define IOP331_ERBAR      (volatile u32 *)IOP331_REG_ADDR(0x00000130)
#define IOP331_ATU_CAPPTR (volatile u32 *)IOP331_REG_ADDR(0x00000134)
/* Reserved 0x00000138 through 0x0000013B */
#define IOP331_ATUILR     (volatile u8  *)IOP331_REG_ADDR(0x0000013C)
#define IOP331_ATUIPR     (volatile u8  *)IOP331_REG_ADDR(0x0000013D)
#define IOP331_ATUMGNT    (volatile u8  *)IOP331_REG_ADDR(0x0000013E)
#define IOP331_ATUMLAT    (volatile u8  *)IOP331_REG_ADDR(0x0000013F)
#define IOP331_IALR0      (volatile u32 *)IOP331_REG_ADDR(0x00000140)
#define IOP331_IATVR0     (volatile u32 *)IOP331_REG_ADDR(0x00000144)
#define IOP331_ERLR       (volatile u32 *)IOP331_REG_ADDR(0x00000148)
#define IOP331_ERTVR      (volatile u32 *)IOP331_REG_ADDR(0x0000014C)
#define IOP331_IALR1      (volatile u32 *)IOP331_REG_ADDR(0x00000150)
#define IOP331_IALR2      (volatile u32 *)IOP331_REG_ADDR(0x00000154)
#define IOP331_IATVR2     (volatile u32 *)IOP331_REG_ADDR(0x00000158)
#define IOP331_OIOWTVR    (volatile u32 *)IOP331_REG_ADDR(0x0000015C)
#define IOP331_OMWTVR0    (volatile u32 *)IOP331_REG_ADDR(0x00000160)
#define IOP331_OUMWTVR0   (volatile u32 *)IOP331_REG_ADDR(0x00000164)
#define IOP331_OMWTVR1    (volatile u32 *)IOP331_REG_ADDR(0x00000168)
#define IOP331_OUMWTVR1   (volatile u32 *)IOP331_REG_ADDR(0x0000016C)
/* Reserved 0x00000170 through 0x00000177*/
#define IOP331_OUDWTVR    (volatile u32 *)IOP331_REG_ADDR(0x00000178)
/* Reserved 0x0000017C through 0x0000017F*/
#define IOP331_ATUCR      (volatile u32 *)IOP331_REG_ADDR(0x00000180)
#define IOP331_PCSR       (volatile u32 *)IOP331_REG_ADDR(0x00000184)
#define IOP331_ATUISR     (volatile u32 *)IOP331_REG_ADDR(0x00000188)
#define IOP331_ATUIMR     (volatile u32 *)IOP331_REG_ADDR(0x0000018C)
#define IOP331_IABAR3     (volatile u32 *)IOP331_REG_ADDR(0x00000190)
#define IOP331_IAUBAR3    (volatile u32 *)IOP331_REG_ADDR(0x00000194)
#define IOP331_IALR3      (volatile u32 *)IOP331_REG_ADDR(0x00000198)
#define IOP331_IATVR3     (volatile u32 *)IOP331_REG_ADDR(0x0000019C)
/* Reserved 0x000001A0 through 0x000001A3*/
#define IOP331_OCCAR      (volatile u32 *)IOP331_REG_ADDR(0x000001A4)
/* Reserved 0x000001A8 through 0x000001AB*/
#define IOP331_OCCDR      (volatile u32 *)IOP331_REG_ADDR(0x000001AC)
/* Reserved 0x000001B0 through 0x000001BB*/
#define IOP331_VPDCAPID   (volatile u8 *)IOP331_REG_ADDR(0x000001B8)
#define IOP331_VPDNXTP    (volatile u8 *)IOP331_REG_ADDR(0x000001B9)
#define IOP331_VPDAR	  (volatile u16 *)IOP331_REG_ADDR(0x000001BA)
#define IOP331_VPDDR      (volatile u32 *)IOP331_REG_ADDR(0x000001BC)
#define IOP331_PMCAPID    (volatile u8 *)IOP331_REG_ADDR(0x000001C0)
#define IOP331_PMNEXT     (volatile u8 *)IOP331_REG_ADDR(0x000001C1)
#define IOP331_APMCR      (volatile u16 *)IOP331_REG_ADDR(0x000001C2)
#define IOP331_APMCSR     (volatile u16 *)IOP331_REG_ADDR(0x000001C4)
/* Reserved 0x000001C6 through 0x000001CF */
#define IOP331_MSICAPID   (volatile u8 *)IOP331_REG_ADDR(0x000001D0)
#define IOP331_MSINXTP	  (volatile u8 *)IOP331_REG_ADDR(0x000001D1)
#define IOP331_MSIMCR     (volatile u16 *)IOP331_REG_ADDR(0x000001D2)
#define IOP331_MSIMAR     (volatile u32 *)IOP331_REG_ADDR(0x000001D4)
#define IOP331_MSIMUAR	  (volatile u32 *)IOP331_REG_ADDR(0x000001D8)
#define IOP331_MSIMDR	  (volatile u32 *)IOP331_REG_ADDR(0x000001DC)
#define IOP331_PCIXCAPID  (volatile u8 *)IOP331_REG_ADDR(0x000001E0)
#define IOP331_PCIXNEXT   (volatile u8 *)IOP331_REG_ADDR(0x000001E1)
#define IOP331_PCIXCMD    (volatile u16 *)IOP331_REG_ADDR(0x000001E2)
#define IOP331_PCIXSR     (volatile u32 *)IOP331_REG_ADDR(0x000001E4)
#define IOP331_PCIIRSR    (volatile u32 *)IOP331_REG_ADDR(0x000001EC)

/* Messaging Unit 0x00000300 through 0x000003FF */

/* Reserved 0x00000300 through 0x0000030c */
#define IOP331_IMR0       (volatile u32 *)IOP331_REG_ADDR(0x00000310)
#define IOP331_IMR1       (volatile u32 *)IOP331_REG_ADDR(0x00000314)
#define IOP331_OMR0       (volatile u32 *)IOP331_REG_ADDR(0x00000318)
#define IOP331_OMR1       (volatile u32 *)IOP331_REG_ADDR(0x0000031C)
#define IOP331_IDR        (volatile u32 *)IOP331_REG_ADDR(0x00000320)
#define IOP331_IISR       (volatile u32 *)IOP331_REG_ADDR(0x00000324)
#define IOP331_IIMR       (volatile u32 *)IOP331_REG_ADDR(0x00000328)
#define IOP331_ODR        (volatile u32 *)IOP331_REG_ADDR(0x0000032C)
#define IOP331_OISR       (volatile u32 *)IOP331_REG_ADDR(0x00000330)
#define IOP331_OIMR       (volatile u32 *)IOP331_REG_ADDR(0x00000334)
/* Reserved 0x00000338 through 0x0000034F */
#define IOP331_MUCR       (volatile u32 *)IOP331_REG_ADDR(0x00000350)
#define IOP331_QBAR       (volatile u32 *)IOP331_REG_ADDR(0x00000354)
/* Reserved 0x00000358 through 0x0000035C */
#define IOP331_IFHPR      (volatile u32 *)IOP331_REG_ADDR(0x00000360)
#define IOP331_IFTPR      (volatile u32 *)IOP331_REG_ADDR(0x00000364)
#define IOP331_IPHPR      (volatile u32 *)IOP331_REG_ADDR(0x00000368)
#define IOP331_IPTPR      (volatile u32 *)IOP331_REG_ADDR(0x0000036C)
#define IOP331_OFHPR      (volatile u32 *)IOP331_REG_ADDR(0x00000370)
#define IOP331_OFTPR      (volatile u32 *)IOP331_REG_ADDR(0x00000374)
#define IOP331_OPHPR      (volatile u32 *)IOP331_REG_ADDR(0x00000378)
#define IOP331_OPTPR      (volatile u32 *)IOP331_REG_ADDR(0x0000037C)
#define IOP331_IAR        (volatile u32 *)IOP331_REG_ADDR(0x00000380)
/* Reserved 0x00000384 through 0x000003FF */

/* DMA Controller 0x00000400 through 0x000004FF */
#define IOP331_DMA0_CCR   (volatile u32 *)IOP331_REG_ADDR(0x00000400)
#define IOP331_DMA0_CSR   (volatile u32 *)IOP331_REG_ADDR(0x00000404)
#define IOP331_DMA0_DAR   (volatile u32 *)IOP331_REG_ADDR(0x0000040C)
#define IOP331_DMA0_NDAR  (volatile u32 *)IOP331_REG_ADDR(0x00000410)
#define IOP331_DMA0_PADR  (volatile u32 *)IOP331_REG_ADDR(0x00000414)
#define IOP331_DMA0_PUADR (volatile u32 *)IOP331_REG_ADDR(0x00000418)
#define IOP331_DMA0_LADR  (volatile u32 *)IOP331_REG_ADDR(0X0000041C)
#define IOP331_DMA0_BCR   (volatile u32 *)IOP331_REG_ADDR(0x00000420)
#define IOP331_DMA0_DCR   (volatile u32 *)IOP331_REG_ADDR(0x00000424)
/* Reserved 0x00000428 through 0x0000043C */
#define IOP331_DMA1_CCR   (volatile u32 *)IOP331_REG_ADDR(0x00000440)
#define IOP331_DMA1_CSR   (volatile u32 *)IOP331_REG_ADDR(0x00000444)
#define IOP331_DMA1_DAR   (volatile u32 *)IOP331_REG_ADDR(0x0000044C)
#define IOP331_DMA1_NDAR  (volatile u32 *)IOP331_REG_ADDR(0x00000450)
#define IOP331_DMA1_PADR  (volatile u32 *)IOP331_REG_ADDR(0x00000454)
#define IOP331_DMA1_PUADR (volatile u32 *)IOP331_REG_ADDR(0x00000458)
#define IOP331_DMA1_LADR  (volatile u32 *)IOP331_REG_ADDR(0x0000045C)
#define IOP331_DMA1_BCR   (volatile u32 *)IOP331_REG_ADDR(0x00000460)
#define IOP331_DMA1_DCR   (volatile u32 *)IOP331_REG_ADDR(0x00000464)
/* Reserved 0x00000468 through 0x000004FF */

/* Memory controller 0x00000500 through 0x0005FF */

/* Peripheral bus interface unit 0x00000680 through 0x0006FF */
#define IOP331_PBCR       (volatile u32 *)IOP331_REG_ADDR(0x00000680)
#define IOP331_PBISR      (volatile u32 *)IOP331_REG_ADDR(0x00000684)
#define IOP331_PBBAR0     (volatile u32 *)IOP331_REG_ADDR(0x00000688)
#define IOP331_PBLR0      (volatile u32 *)IOP331_REG_ADDR(0x0000068C)
#define IOP331_PBBAR1     (volatile u32 *)IOP331_REG_ADDR(0x00000690)
#define IOP331_PBLR1      (volatile u32 *)IOP331_REG_ADDR(0x00000694)
#define IOP331_PBBAR2     (volatile u32 *)IOP331_REG_ADDR(0x00000698)
#define IOP331_PBLR2      (volatile u32 *)IOP331_REG_ADDR(0x0000069C)
#define IOP331_PBBAR3     (volatile u32 *)IOP331_REG_ADDR(0x000006A0)
#define IOP331_PBLR3      (volatile u32 *)IOP331_REG_ADDR(0x000006A4)
#define IOP331_PBBAR4     (volatile u32 *)IOP331_REG_ADDR(0x000006A8)
#define IOP331_PBLR4      (volatile u32 *)IOP331_REG_ADDR(0x000006AC)
#define IOP331_PBBAR5     (volatile u32 *)IOP331_REG_ADDR(0x000006B0)
#define IOP331_PBLR5      (volatile u32 *)IOP331_REG_ADDR(0x000006B4)
#define IOP331_PBDSCR     (volatile u32 *)IOP331_REG_ADDR(0x000006B8)
/* Reserved 0x000006BC */
#define IOP331_PMBR0      (volatile u32 *)IOP331_REG_ADDR(0x000006C0)
/* Reserved 0x000006C4 through 0x000006DC */
#define IOP331_PMBR1      (volatile u32 *)IOP331_REG_ADDR(0x000006E0)
#define IOP331_PMBR2      (volatile u32 *)IOP331_REG_ADDR(0x000006E4)

#define IOP331_PBCR_EN    0x1

#define IOP331_PBISR_BOOR_ERR 0x1



/* Peripheral performance monitoring unit 0x00000700 through 0x00077F */
/* Internal arbitration unit 0x00000780 through 0x0007BF */

/* Interrupt Controller */
#define IOP331_INTCTL0    (volatile u32 *)IOP331_REG_ADDR(0x00000790)
#define IOP331_INTCTL1    (volatile u32 *)IOP331_REG_ADDR(0x00000794)
#define IOP331_INTSTR0    (volatile u32 *)IOP331_REG_ADDR(0x00000798)
#define IOP331_INTSTR1    (volatile u32 *)IOP331_REG_ADDR(0x0000079C)
#define IOP331_IINTSRC0   (volatile u32 *)IOP331_REG_ADDR(0x000007A0)
#define IOP331_IINTSRC1   (volatile u32 *)IOP331_REG_ADDR(0x000007A4)
#define IOP331_FINTSRC0   (volatile u32 *)IOP331_REG_ADDR(0x000007A8)
#define IOP331_FINTSRC1   (volatile u32 *)IOP331_REG_ADDR(0x000007AC)
#define IOP331_IPR0       (volatile u32 *)IOP331_REG_ADDR(0x000007B0)
#define IOP331_IPR1       (volatile u32 *)IOP331_REG_ADDR(0x000007B4)
#define IOP331_IPR2       (volatile u32 *)IOP331_REG_ADDR(0x000007B8)
#define IOP331_IPR3       (volatile u32 *)IOP331_REG_ADDR(0x000007BC)
#define IOP331_INTBASE    (volatile u32 *)IOP331_REG_ADDR(0x000007C0)
#define IOP331_INTSIZE    (volatile u32 *)IOP331_REG_ADDR(0x000007C4)
#define IOP331_IINTVEC    (volatile u32 *)IOP331_REG_ADDR(0x000007C8)
#define IOP331_FINTVEC    (volatile u32 *)IOP331_REG_ADDR(0x000007CC)


/* Timers */

#define IOP331_TU_TMR0		(volatile u32 *)IOP331_REG_ADDR(0x000007D0)
#define IOP331_TU_TMR1		(volatile u32 *)IOP331_REG_ADDR(0x000007D4)

#define IOP331_TMR_TC		0x01
#define	IOP331_TMR_EN		0x02
#define IOP331_TMR_RELOAD	0x04
#define	IOP331_TMR_PRIVILEGED	0x09

#define	IOP331_TMR_RATIO_1_1	0x00
#define	IOP331_TMR_RATIO_4_1	0x10
#define	IOP331_TMR_RATIO_8_1	0x20
#define	IOP331_TMR_RATIO_16_1	0x30

#define IOP331_TU_TCR0    (volatile u32 *)IOP331_REG_ADDR(0x000007D8)
#define IOP331_TU_TCR1    (volatile u32 *)IOP331_REG_ADDR(0x000007DC)
#define IOP331_TU_TRR0    (volatile u32 *)IOP331_REG_ADDR(0x000007E0)
#define IOP331_TU_TRR1    (volatile u32 *)IOP331_REG_ADDR(0x000007E4)
#define IOP331_TU_TISR    (volatile u32 *)IOP331_REG_ADDR(0x000007E8)
#define IOP331_TU_WDTCR   (volatile u32 *)IOP331_REG_ADDR(0x000007EC)

#if defined(CONFIG_ARCH_IOP331)
#define	IOP331_TICK_RATE	266000000	/* 266 MHz IB clock */
#endif

#if defined(CONFIG_IOP331_STEPD) || defined(CONFIG_ARCH_IQ80333)
#undef IOP331_TICK_RATE
#define IOP331_TICK_RATE	333000000	/* 333 Mhz IB clock */
#endif

/* Application accelerator unit 0x00000800 - 0x000008FF */
#define IOP331_AAU_ACR     (volatile u32 *)IOP331_REG_ADDR(0x00000800)
#define IOP331_AAU_ASR     (volatile u32 *)IOP331_REG_ADDR(0x00000804)
#define IOP331_AAU_ADAR    (volatile u32 *)IOP331_REG_ADDR(0x00000808)
#define IOP331_AAU_ANDAR   (volatile u32 *)IOP331_REG_ADDR(0x0000080C)
#define IOP331_AAU_SAR1    (volatile u32 *)IOP331_REG_ADDR(0x00000810)
#define IOP331_AAU_SAR2    (volatile u32 *)IOP331_REG_ADDR(0x00000814)
#define IOP331_AAU_SAR3    (volatile u32 *)IOP331_REG_ADDR(0x00000818)
#define IOP331_AAU_SAR4    (volatile u32 *)IOP331_REG_ADDR(0x0000081C)
#define IOP331_AAU_SAR5    (volatile u32 *)IOP331_REG_ADDR(0x0000082C)
#define IOP331_AAU_SAR6    (volatile u32 *)IOP331_REG_ADDR(0x00000830)
#define IOP331_AAU_SAR7    (volatile u32 *)IOP331_REG_ADDR(0x00000834)
#define IOP331_AAU_SAR8    (volatile u32 *)IOP331_REG_ADDR(0x00000838)
#define IOP331_AAU_SAR9    (volatile u32 *)IOP331_REG_ADDR(0x00000840)
#define IOP331_AAU_SAR10   (volatile u32 *)IOP331_REG_ADDR(0x00000844)
#define IOP331_AAU_SAR11   (volatile u32 *)IOP331_REG_ADDR(0x00000848)
#define IOP331_AAU_SAR12   (volatile u32 *)IOP331_REG_ADDR(0x0000084C)
#define IOP331_AAU_SAR13   (volatile u32 *)IOP331_REG_ADDR(0x00000850)
#define IOP331_AAU_SAR14   (volatile u32 *)IOP331_REG_ADDR(0x00000854)
#define IOP331_AAU_SAR15   (volatile u32 *)IOP331_REG_ADDR(0x00000858)
#define IOP331_AAU_SAR16   (volatile u32 *)IOP331_REG_ADDR(0x0000085C)
#define IOP331_AAU_SAR17   (volatile u32 *)IOP331_REG_ADDR(0x00000864)
#define IOP331_AAU_SAR18   (volatile u32 *)IOP331_REG_ADDR(0x00000868)
#define IOP331_AAU_SAR19   (volatile u32 *)IOP331_REG_ADDR(0x0000086C)
#define IOP331_AAU_SAR20   (volatile u32 *)IOP331_REG_ADDR(0x00000870)
#define IOP331_AAU_SAR21   (volatile u32 *)IOP331_REG_ADDR(0x00000874)
#define IOP331_AAU_SAR22   (volatile u32 *)IOP331_REG_ADDR(0x00000878)
#define IOP331_AAU_SAR23   (volatile u32 *)IOP331_REG_ADDR(0x0000087C)
#define IOP331_AAU_SAR24   (volatile u32 *)IOP331_REG_ADDR(0x00000880)
#define IOP331_AAU_SAR25   (volatile u32 *)IOP331_REG_ADDR(0x00000888)
#define IOP331_AAU_SAR26   (volatile u32 *)IOP331_REG_ADDR(0x0000088C)
#define IOP331_AAU_SAR27   (volatile u32 *)IOP331_REG_ADDR(0x00000890)
#define IOP331_AAU_SAR28   (volatile u32 *)IOP331_REG_ADDR(0x00000894)
#define IOP331_AAU_SAR29   (volatile u32 *)IOP331_REG_ADDR(0x00000898)
#define IOP331_AAU_SAR30   (volatile u32 *)IOP331_REG_ADDR(0x0000089C)
#define IOP331_AAU_SAR31   (volatile u32 *)IOP331_REG_ADDR(0x000008A0)
#define IOP331_AAU_SAR32   (volatile u32 *)IOP331_REG_ADDR(0x000008A4)
#define IOP331_AAU_DAR     (volatile u32 *)IOP331_REG_ADDR(0x00000820)
#define IOP331_AAU_ABCR    (volatile u32 *)IOP331_REG_ADDR(0x00000824)
#define IOP331_AAU_ADCR    (volatile u32 *)IOP331_REG_ADDR(0x00000828)
#define IOP331_AAU_EDCR0   (volatile u32 *)IOP331_REG_ADDR(0x0000083c)
#define IOP331_AAU_EDCR1   (volatile u32 *)IOP331_REG_ADDR(0x00000860)
#define IOP331_AAU_EDCR2   (volatile u32 *)IOP331_REG_ADDR(0x00000884)


#define IOP331_SPDSCR	  (volatile u32 *)IOP331_REG_ADDR(0x000015C0)
#define IOP331_PPDSCR	  (volatile u32 *)IOP331_REG_ADDR(0x000015C8)
/* SSP serial port unit 0x00001600 - 0x0000167F */

/* I2C bus interface unit 0x00001680 - 0x000016FF */
/* for I2C bit defs see drivers/i2c/i2c-iop3xx.h */

#define IOP331_ICR0       (volatile u32 *)IOP331_REG_ADDR(0x00001680)
#define IOP331_ISR0       (volatile u32 *)IOP331_REG_ADDR(0x00001684)
#define IOP331_ISAR0      (volatile u32 *)IOP331_REG_ADDR(0x00001688)
#define IOP331_IDBR0      (volatile u32 *)IOP331_REG_ADDR(0x0000168C)
/* Reserved 0x00001690 */
#define IOP331_IBMR0      (volatile u32 *)IOP331_REG_ADDR(0x00001694)
/* Reserved 0x00001698 */
/* Reserved 0x0000169C */
#define IOP331_ICR1       (volatile u32 *)IOP331_REG_ADDR(0x000016A0)
#define IOP331_ISR1       (volatile u32 *)IOP331_REG_ADDR(0x000016A4)
#define IOP331_ISAR1      (volatile u32 *)IOP331_REG_ADDR(0x000016A8)
#define IOP331_IDBR1      (volatile u32 *)IOP331_REG_ADDR(0x000016AC)
#define IOP331_IBMR1      (volatile u32 *)IOP331_REG_ADDR(0x000016B4)
/* Reserved 0x000016B8 through 0x000016FF */

/* 0x00001700 through 0x0000172C  UART 0 */

/* Reserved 0x00001730 through 0x0000173F */

/* 0x00001740 through 0x0000176C UART 1 */

#define IOP331_UART0_PHYS  (IOP331_PHYS_MEM_BASE | 0x00001700)	/* UART #1 physical */
#define IOP331_UART1_PHYS  (IOP331_PHYS_MEM_BASE | 0x00001740)	/* UART #2 physical */
#define IOP331_UART0_VIRT  (IOP331_VIRT_MEM_BASE | 0x00001700) /* UART #1 virtual addr */
#define IOP331_UART1_VIRT  (IOP331_VIRT_MEM_BASE | 0x00001740) /* UART #2 virtual addr */

/* Reserved 0x00001770 through 0x0000177F */

/* General Purpose I/O Registers */
#define IOP331_GPOE       (volatile u32 *)IOP331_REG_ADDR(0x00001780)
#define IOP331_GPID       (volatile u32 *)IOP331_REG_ADDR(0x00001784)
#define IOP331_GPOD       (volatile u32 *)IOP331_REG_ADDR(0x00001788)

/* Reserved 0x0000178c through 0x000019ff */


#ifndef __ASSEMBLY__
extern void iop331_map_io(void);
extern void iop331_init_irq(void);
extern void iop331_time_init(void);
#endif

#endif // _IOP331_HW_H_
