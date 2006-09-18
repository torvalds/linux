/*
 * linux/include/asm/arch-iop33x/iop331.h
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
#define	iop_is_331()		1
#endif

/*
 * IOP331 chipset registers
 */
#define IOP331_VIRT_MEM_BASE  0xfeffe000  /* chip virtual mem address*/
#define IOP331_PHYS_MEM_BASE  0xffffe000  /* chip physical memory address */
#define IOP331_REG_ADDR(reg) (IOP331_VIRT_MEM_BASE | (reg))

/* Reserved 0x00000000 through 0x000000FF */

/* Address Translation Unit 0x00000100 through 0x000001FF */

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
#if defined(CONFIG_ARCH_IOP33X)
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

/*
 * Peripherals that are shared between the iop32x and iop33x but
 * located at different addresses.
 */
#define IOP3XX_TIMER_REG(reg)  (IOP3XX_PERIPHERAL_VIRT_BASE + 0x07d0 + (reg))

#include <asm/hardware/iop3xx.h>


#ifndef __ASSEMBLY__
extern void iop331_init_irq(void);
extern void iop331_time_init(void);
#endif

#endif // _IOP331_HW_H_
