/*
 * include/asm-arm/hardware/iop3xx.h
 *
 * Intel IOP32X and IOP33X register definitions
 *
 * Author: Rory Bolt <rorybolt@pacbell.net>
 * Copyright (C) 2002 Rory Bolt
 * Copyright (C) 2004 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __IOP3XX_H
#define __IOP3XX_H

/*
 * IOP3XX GPIO handling
 */
#define GPIO_IN			0
#define GPIO_OUT		1
#define GPIO_LOW		0
#define GPIO_HIGH		1
#define IOP3XX_GPIO_LINE(x)	(x)

#ifndef __ASSEMBLY__
extern void gpio_line_config(int line, int direction);
extern int  gpio_line_get(int line);
extern void gpio_line_set(int line, int value);
#endif


/*
 * IOP3XX processor registers
 */
#define IOP3XX_PERIPHERAL_PHYS_BASE	0xffffe000
#define IOP3XX_PERIPHERAL_VIRT_BASE	0xfeffe000
#define IOP3XX_PERIPHERAL_SIZE		0x00002000
#define IOP3XX_PERIPHERAL_UPPER_PA (IOP3XX_PERIPHERAL_PHYS_BASE +\
					IOP3XX_PERIPHERAL_SIZE - 1)
#define IOP3XX_PERIPHERAL_UPPER_VA (IOP3XX_PERIPHERAL_VIRT_BASE +\
					IOP3XX_PERIPHERAL_SIZE - 1)
#define IOP3XX_PMMR_PHYS_TO_VIRT(addr) (u32) ((u32) addr -\
					(IOP3XX_PERIPHERAL_PHYS_BASE\
					- IOP3XX_PERIPHERAL_VIRT_BASE))
#define IOP3XX_REG_ADDR(reg)		(IOP3XX_PERIPHERAL_VIRT_BASE + (reg))

/* Address Translation Unit  */
#define IOP3XX_ATUVID		(volatile u16 *)IOP3XX_REG_ADDR(0x0100)
#define IOP3XX_ATUDID		(volatile u16 *)IOP3XX_REG_ADDR(0x0102)
#define IOP3XX_ATUCMD		(volatile u16 *)IOP3XX_REG_ADDR(0x0104)
#define IOP3XX_ATUSR		(volatile u16 *)IOP3XX_REG_ADDR(0x0106)
#define IOP3XX_ATURID		(volatile u8  *)IOP3XX_REG_ADDR(0x0108)
#define IOP3XX_ATUCCR		(volatile u32 *)IOP3XX_REG_ADDR(0x0109)
#define IOP3XX_ATUCLSR		(volatile u8  *)IOP3XX_REG_ADDR(0x010c)
#define IOP3XX_ATULT		(volatile u8  *)IOP3XX_REG_ADDR(0x010d)
#define IOP3XX_ATUHTR		(volatile u8  *)IOP3XX_REG_ADDR(0x010e)
#define IOP3XX_ATUBIST		(volatile u8  *)IOP3XX_REG_ADDR(0x010f)
#define IOP3XX_IABAR0		(volatile u32 *)IOP3XX_REG_ADDR(0x0110)
#define IOP3XX_IAUBAR0		(volatile u32 *)IOP3XX_REG_ADDR(0x0114)
#define IOP3XX_IABAR1		(volatile u32 *)IOP3XX_REG_ADDR(0x0118)
#define IOP3XX_IAUBAR1		(volatile u32 *)IOP3XX_REG_ADDR(0x011c)
#define IOP3XX_IABAR2		(volatile u32 *)IOP3XX_REG_ADDR(0x0120)
#define IOP3XX_IAUBAR2		(volatile u32 *)IOP3XX_REG_ADDR(0x0124)
#define IOP3XX_ASVIR		(volatile u16 *)IOP3XX_REG_ADDR(0x012c)
#define IOP3XX_ASIR		(volatile u16 *)IOP3XX_REG_ADDR(0x012e)
#define IOP3XX_ERBAR		(volatile u32 *)IOP3XX_REG_ADDR(0x0130)
#define IOP3XX_ATUILR		(volatile u8  *)IOP3XX_REG_ADDR(0x013c)
#define IOP3XX_ATUIPR		(volatile u8  *)IOP3XX_REG_ADDR(0x013d)
#define IOP3XX_ATUMGNT		(volatile u8  *)IOP3XX_REG_ADDR(0x013e)
#define IOP3XX_ATUMLAT		(volatile u8  *)IOP3XX_REG_ADDR(0x013f)
#define IOP3XX_IALR0		(volatile u32 *)IOP3XX_REG_ADDR(0x0140)
#define IOP3XX_IATVR0		(volatile u32 *)IOP3XX_REG_ADDR(0x0144)
#define IOP3XX_ERLR		(volatile u32 *)IOP3XX_REG_ADDR(0x0148)
#define IOP3XX_ERTVR		(volatile u32 *)IOP3XX_REG_ADDR(0x014c)
#define IOP3XX_IALR1		(volatile u32 *)IOP3XX_REG_ADDR(0x0150)
#define IOP3XX_IALR2		(volatile u32 *)IOP3XX_REG_ADDR(0x0154)
#define IOP3XX_IATVR2		(volatile u32 *)IOP3XX_REG_ADDR(0x0158)
#define IOP3XX_OIOWTVR		(volatile u32 *)IOP3XX_REG_ADDR(0x015c)
#define IOP3XX_OMWTVR0		(volatile u32 *)IOP3XX_REG_ADDR(0x0160)
#define IOP3XX_OUMWTVR0		(volatile u32 *)IOP3XX_REG_ADDR(0x0164)
#define IOP3XX_OMWTVR1		(volatile u32 *)IOP3XX_REG_ADDR(0x0168)
#define IOP3XX_OUMWTVR1		(volatile u32 *)IOP3XX_REG_ADDR(0x016c)
#define IOP3XX_OUDWTVR		(volatile u32 *)IOP3XX_REG_ADDR(0x0178)
#define IOP3XX_ATUCR		(volatile u32 *)IOP3XX_REG_ADDR(0x0180)
#define IOP3XX_PCSR		(volatile u32 *)IOP3XX_REG_ADDR(0x0184)
#define IOP3XX_ATUISR		(volatile u32 *)IOP3XX_REG_ADDR(0x0188)
#define IOP3XX_ATUIMR		(volatile u32 *)IOP3XX_REG_ADDR(0x018c)
#define IOP3XX_IABAR3		(volatile u32 *)IOP3XX_REG_ADDR(0x0190)
#define IOP3XX_IAUBAR3		(volatile u32 *)IOP3XX_REG_ADDR(0x0194)
#define IOP3XX_IALR3		(volatile u32 *)IOP3XX_REG_ADDR(0x0198)
#define IOP3XX_IATVR3		(volatile u32 *)IOP3XX_REG_ADDR(0x019c)
#define IOP3XX_OCCAR		(volatile u32 *)IOP3XX_REG_ADDR(0x01a4)
#define IOP3XX_OCCDR		(volatile u32 *)IOP3XX_REG_ADDR(0x01ac)
#define IOP3XX_PDSCR		(volatile u32 *)IOP3XX_REG_ADDR(0x01bc)
#define IOP3XX_PMCAPID		(volatile u8  *)IOP3XX_REG_ADDR(0x01c0)
#define IOP3XX_PMNEXT		(volatile u8  *)IOP3XX_REG_ADDR(0x01c1)
#define IOP3XX_APMCR		(volatile u16 *)IOP3XX_REG_ADDR(0x01c2)
#define IOP3XX_APMCSR		(volatile u16 *)IOP3XX_REG_ADDR(0x01c4)
#define IOP3XX_PCIXCAPID	(volatile u8  *)IOP3XX_REG_ADDR(0x01e0)
#define IOP3XX_PCIXNEXT		(volatile u8  *)IOP3XX_REG_ADDR(0x01e1)
#define IOP3XX_PCIXCMD		(volatile u16 *)IOP3XX_REG_ADDR(0x01e2)
#define IOP3XX_PCIXSR		(volatile u32 *)IOP3XX_REG_ADDR(0x01e4)
#define IOP3XX_PCIIRSR		(volatile u32 *)IOP3XX_REG_ADDR(0x01ec)

/* Messaging Unit  */
#define IOP3XX_IMR0		(volatile u32 *)IOP3XX_REG_ADDR(0x0310)
#define IOP3XX_IMR1		(volatile u32 *)IOP3XX_REG_ADDR(0x0314)
#define IOP3XX_OMR0		(volatile u32 *)IOP3XX_REG_ADDR(0x0318)
#define IOP3XX_OMR1		(volatile u32 *)IOP3XX_REG_ADDR(0x031c)
#define IOP3XX_IDR		(volatile u32 *)IOP3XX_REG_ADDR(0x0320)
#define IOP3XX_IISR		(volatile u32 *)IOP3XX_REG_ADDR(0x0324)
#define IOP3XX_IIMR		(volatile u32 *)IOP3XX_REG_ADDR(0x0328)
#define IOP3XX_ODR		(volatile u32 *)IOP3XX_REG_ADDR(0x032c)
#define IOP3XX_OISR		(volatile u32 *)IOP3XX_REG_ADDR(0x0330)
#define IOP3XX_OIMR		(volatile u32 *)IOP3XX_REG_ADDR(0x0334)
#define IOP3XX_MUCR		(volatile u32 *)IOP3XX_REG_ADDR(0x0350)
#define IOP3XX_QBAR		(volatile u32 *)IOP3XX_REG_ADDR(0x0354)
#define IOP3XX_IFHPR		(volatile u32 *)IOP3XX_REG_ADDR(0x0360)
#define IOP3XX_IFTPR		(volatile u32 *)IOP3XX_REG_ADDR(0x0364)
#define IOP3XX_IPHPR		(volatile u32 *)IOP3XX_REG_ADDR(0x0368)
#define IOP3XX_IPTPR		(volatile u32 *)IOP3XX_REG_ADDR(0x036c)
#define IOP3XX_OFHPR		(volatile u32 *)IOP3XX_REG_ADDR(0x0370)
#define IOP3XX_OFTPR		(volatile u32 *)IOP3XX_REG_ADDR(0x0374)
#define IOP3XX_OPHPR		(volatile u32 *)IOP3XX_REG_ADDR(0x0378)
#define IOP3XX_OPTPR		(volatile u32 *)IOP3XX_REG_ADDR(0x037c)
#define IOP3XX_IAR		(volatile u32 *)IOP3XX_REG_ADDR(0x0380)

/* DMA Controller  */
#define IOP3XX_DMA0_CCR		(volatile u32 *)IOP3XX_REG_ADDR(0x0400)
#define IOP3XX_DMA0_CSR		(volatile u32 *)IOP3XX_REG_ADDR(0x0404)
#define IOP3XX_DMA0_DAR		(volatile u32 *)IOP3XX_REG_ADDR(0x040c)
#define IOP3XX_DMA0_NDAR	(volatile u32 *)IOP3XX_REG_ADDR(0x0410)
#define IOP3XX_DMA0_PADR	(volatile u32 *)IOP3XX_REG_ADDR(0x0414)
#define IOP3XX_DMA0_PUADR	(volatile u32 *)IOP3XX_REG_ADDR(0x0418)
#define IOP3XX_DMA0_LADR	(volatile u32 *)IOP3XX_REG_ADDR(0x041c)
#define IOP3XX_DMA0_BCR		(volatile u32 *)IOP3XX_REG_ADDR(0x0420)
#define IOP3XX_DMA0_DCR		(volatile u32 *)IOP3XX_REG_ADDR(0x0424)
#define IOP3XX_DMA1_CCR		(volatile u32 *)IOP3XX_REG_ADDR(0x0440)
#define IOP3XX_DMA1_CSR		(volatile u32 *)IOP3XX_REG_ADDR(0x0444)
#define IOP3XX_DMA1_DAR		(volatile u32 *)IOP3XX_REG_ADDR(0x044c)
#define IOP3XX_DMA1_NDAR	(volatile u32 *)IOP3XX_REG_ADDR(0x0450)
#define IOP3XX_DMA1_PADR	(volatile u32 *)IOP3XX_REG_ADDR(0x0454)
#define IOP3XX_DMA1_PUADR	(volatile u32 *)IOP3XX_REG_ADDR(0x0458)
#define IOP3XX_DMA1_LADR	(volatile u32 *)IOP3XX_REG_ADDR(0x045c)
#define IOP3XX_DMA1_BCR		(volatile u32 *)IOP3XX_REG_ADDR(0x0460)
#define IOP3XX_DMA1_DCR		(volatile u32 *)IOP3XX_REG_ADDR(0x0464)

/* Peripheral bus interface  */
#define IOP3XX_PBCR		(volatile u32 *)IOP3XX_REG_ADDR(0x0680)
#define IOP3XX_PBISR		(volatile u32 *)IOP3XX_REG_ADDR(0x0684)
#define IOP3XX_PBBAR0		(volatile u32 *)IOP3XX_REG_ADDR(0x0688)
#define IOP3XX_PBLR0		(volatile u32 *)IOP3XX_REG_ADDR(0x068c)
#define IOP3XX_PBBAR1		(volatile u32 *)IOP3XX_REG_ADDR(0x0690)
#define IOP3XX_PBLR1		(volatile u32 *)IOP3XX_REG_ADDR(0x0694)
#define IOP3XX_PBBAR2		(volatile u32 *)IOP3XX_REG_ADDR(0x0698)
#define IOP3XX_PBLR2		(volatile u32 *)IOP3XX_REG_ADDR(0x069c)
#define IOP3XX_PBBAR3		(volatile u32 *)IOP3XX_REG_ADDR(0x06a0)
#define IOP3XX_PBLR3		(volatile u32 *)IOP3XX_REG_ADDR(0x06a4)
#define IOP3XX_PBBAR4		(volatile u32 *)IOP3XX_REG_ADDR(0x06a8)
#define IOP3XX_PBLR4		(volatile u32 *)IOP3XX_REG_ADDR(0x06ac)
#define IOP3XX_PBBAR5		(volatile u32 *)IOP3XX_REG_ADDR(0x06b0)
#define IOP3XX_PBLR5		(volatile u32 *)IOP3XX_REG_ADDR(0x06b4)
#define IOP3XX_PMBR0		(volatile u32 *)IOP3XX_REG_ADDR(0x06c0)
#define IOP3XX_PMBR1		(volatile u32 *)IOP3XX_REG_ADDR(0x06e0)
#define IOP3XX_PMBR2		(volatile u32 *)IOP3XX_REG_ADDR(0x06e4)

/* Peripheral performance monitoring unit  */
#define IOP3XX_GTMR		(volatile u32 *)IOP3XX_REG_ADDR(0x0700)
#define IOP3XX_ESR		(volatile u32 *)IOP3XX_REG_ADDR(0x0704)
#define IOP3XX_EMISR		(volatile u32 *)IOP3XX_REG_ADDR(0x0708)
#define IOP3XX_GTSR		(volatile u32 *)IOP3XX_REG_ADDR(0x0710)
/* PERCR0 DOESN'T EXIST - index from 1! */
#define IOP3XX_PERCR0		(volatile u32 *)IOP3XX_REG_ADDR(0x0710)

/* General Purpose I/O  */
#define IOP3XX_GPOE		(volatile u32 *)IOP3XX_GPIO_REG(0x0000)
#define IOP3XX_GPID		(volatile u32 *)IOP3XX_GPIO_REG(0x0004)
#define IOP3XX_GPOD		(volatile u32 *)IOP3XX_GPIO_REG(0x0008)

/* Timers  */
#define IOP3XX_TU_TMR0		(volatile u32 *)IOP3XX_TIMER_REG(0x0000)
#define IOP3XX_TU_TMR1		(volatile u32 *)IOP3XX_TIMER_REG(0x0004)
#define IOP3XX_TU_TCR0		(volatile u32 *)IOP3XX_TIMER_REG(0x0008)
#define IOP3XX_TU_TCR1		(volatile u32 *)IOP3XX_TIMER_REG(0x000c)
#define IOP3XX_TU_TRR0		(volatile u32 *)IOP3XX_TIMER_REG(0x0010)
#define IOP3XX_TU_TRR1		(volatile u32 *)IOP3XX_TIMER_REG(0x0014)
#define IOP3XX_TU_TISR		(volatile u32 *)IOP3XX_TIMER_REG(0x0018)
#define IOP3XX_TU_WDTCR		(volatile u32 *)IOP3XX_TIMER_REG(0x001c)
#define IOP_TMR_EN	    0x02
#define IOP_TMR_RELOAD	    0x04
#define IOP_TMR_PRIVILEGED 0x08
#define IOP_TMR_RATIO_1_1  0x00

/* Application accelerator unit  */
#define IOP3XX_AAU_ACR		(volatile u32 *)IOP3XX_REG_ADDR(0x0800)
#define IOP3XX_AAU_ASR		(volatile u32 *)IOP3XX_REG_ADDR(0x0804)
#define IOP3XX_AAU_ADAR		(volatile u32 *)IOP3XX_REG_ADDR(0x0808)
#define IOP3XX_AAU_ANDAR	(volatile u32 *)IOP3XX_REG_ADDR(0x080c)
#define IOP3XX_AAU_SAR1		(volatile u32 *)IOP3XX_REG_ADDR(0x0810)
#define IOP3XX_AAU_SAR2		(volatile u32 *)IOP3XX_REG_ADDR(0x0814)
#define IOP3XX_AAU_SAR3		(volatile u32 *)IOP3XX_REG_ADDR(0x0818)
#define IOP3XX_AAU_SAR4		(volatile u32 *)IOP3XX_REG_ADDR(0x081c)
#define IOP3XX_AAU_DAR		(volatile u32 *)IOP3XX_REG_ADDR(0x0820)
#define IOP3XX_AAU_ABCR		(volatile u32 *)IOP3XX_REG_ADDR(0x0824)
#define IOP3XX_AAU_ADCR		(volatile u32 *)IOP3XX_REG_ADDR(0x0828)
#define IOP3XX_AAU_SAR5		(volatile u32 *)IOP3XX_REG_ADDR(0x082c)
#define IOP3XX_AAU_SAR6		(volatile u32 *)IOP3XX_REG_ADDR(0x0830)
#define IOP3XX_AAU_SAR7		(volatile u32 *)IOP3XX_REG_ADDR(0x0834)
#define IOP3XX_AAU_SAR8		(volatile u32 *)IOP3XX_REG_ADDR(0x0838)
#define IOP3XX_AAU_EDCR0	(volatile u32 *)IOP3XX_REG_ADDR(0x083c)
#define IOP3XX_AAU_SAR9		(volatile u32 *)IOP3XX_REG_ADDR(0x0840)
#define IOP3XX_AAU_SAR10	(volatile u32 *)IOP3XX_REG_ADDR(0x0844)
#define IOP3XX_AAU_SAR11	(volatile u32 *)IOP3XX_REG_ADDR(0x0848)
#define IOP3XX_AAU_SAR12	(volatile u32 *)IOP3XX_REG_ADDR(0x084c)
#define IOP3XX_AAU_SAR13	(volatile u32 *)IOP3XX_REG_ADDR(0x0850)
#define IOP3XX_AAU_SAR14	(volatile u32 *)IOP3XX_REG_ADDR(0x0854)
#define IOP3XX_AAU_SAR15	(volatile u32 *)IOP3XX_REG_ADDR(0x0858)
#define IOP3XX_AAU_SAR16	(volatile u32 *)IOP3XX_REG_ADDR(0x085c)
#define IOP3XX_AAU_EDCR1	(volatile u32 *)IOP3XX_REG_ADDR(0x0860)
#define IOP3XX_AAU_SAR17	(volatile u32 *)IOP3XX_REG_ADDR(0x0864)
#define IOP3XX_AAU_SAR18	(volatile u32 *)IOP3XX_REG_ADDR(0x0868)
#define IOP3XX_AAU_SAR19	(volatile u32 *)IOP3XX_REG_ADDR(0x086c)
#define IOP3XX_AAU_SAR20	(volatile u32 *)IOP3XX_REG_ADDR(0x0870)
#define IOP3XX_AAU_SAR21	(volatile u32 *)IOP3XX_REG_ADDR(0x0874)
#define IOP3XX_AAU_SAR22	(volatile u32 *)IOP3XX_REG_ADDR(0x0878)
#define IOP3XX_AAU_SAR23	(volatile u32 *)IOP3XX_REG_ADDR(0x087c)
#define IOP3XX_AAU_SAR24	(volatile u32 *)IOP3XX_REG_ADDR(0x0880)
#define IOP3XX_AAU_EDCR2	(volatile u32 *)IOP3XX_REG_ADDR(0x0884)
#define IOP3XX_AAU_SAR25	(volatile u32 *)IOP3XX_REG_ADDR(0x0888)
#define IOP3XX_AAU_SAR26	(volatile u32 *)IOP3XX_REG_ADDR(0x088c)
#define IOP3XX_AAU_SAR27	(volatile u32 *)IOP3XX_REG_ADDR(0x0890)
#define IOP3XX_AAU_SAR28	(volatile u32 *)IOP3XX_REG_ADDR(0x0894)
#define IOP3XX_AAU_SAR29	(volatile u32 *)IOP3XX_REG_ADDR(0x0898)
#define IOP3XX_AAU_SAR30	(volatile u32 *)IOP3XX_REG_ADDR(0x089c)
#define IOP3XX_AAU_SAR31	(volatile u32 *)IOP3XX_REG_ADDR(0x08a0)
#define IOP3XX_AAU_SAR32	(volatile u32 *)IOP3XX_REG_ADDR(0x08a4)

/* I2C bus interface unit  */
#define IOP3XX_ICR0		(volatile u32 *)IOP3XX_REG_ADDR(0x1680)
#define IOP3XX_ISR0		(volatile u32 *)IOP3XX_REG_ADDR(0x1684)
#define IOP3XX_ISAR0		(volatile u32 *)IOP3XX_REG_ADDR(0x1688)
#define IOP3XX_IDBR0		(volatile u32 *)IOP3XX_REG_ADDR(0x168c)
#define IOP3XX_IBMR0		(volatile u32 *)IOP3XX_REG_ADDR(0x1694)
#define IOP3XX_ICR1		(volatile u32 *)IOP3XX_REG_ADDR(0x16a0)
#define IOP3XX_ISR1		(volatile u32 *)IOP3XX_REG_ADDR(0x16a4)
#define IOP3XX_ISAR1		(volatile u32 *)IOP3XX_REG_ADDR(0x16a8)
#define IOP3XX_IDBR1		(volatile u32 *)IOP3XX_REG_ADDR(0x16ac)
#define IOP3XX_IBMR1		(volatile u32 *)IOP3XX_REG_ADDR(0x16b4)


/*
 * IOP3XX I/O and Mem space regions for PCI autoconfiguration
 */
#define IOP3XX_PCI_MEM_WINDOW_SIZE	0x04000000
#define IOP3XX_PCI_LOWER_MEM_PA		0x80000000
#define IOP3XX_PCI_LOWER_MEM_BA		(*IOP3XX_OMWTVR0)

#define IOP3XX_PCI_IO_WINDOW_SIZE	0x00010000
#define IOP3XX_PCI_LOWER_IO_PA		0x90000000
#define IOP3XX_PCI_LOWER_IO_VA		0xfe000000
#define IOP3XX_PCI_LOWER_IO_BA		(*IOP3XX_OIOWTVR)
#define IOP3XX_PCI_UPPER_IO_PA		(IOP3XX_PCI_LOWER_IO_PA +\
					IOP3XX_PCI_IO_WINDOW_SIZE - 1)
#define IOP3XX_PCI_UPPER_IO_VA		(IOP3XX_PCI_LOWER_IO_VA +\
					IOP3XX_PCI_IO_WINDOW_SIZE - 1)
#define IOP3XX_PCI_IO_PHYS_TO_VIRT(addr) (((u32) addr -\
					IOP3XX_PCI_LOWER_IO_PA) +\
					IOP3XX_PCI_LOWER_IO_VA)


#ifndef __ASSEMBLY__
void iop3xx_map_io(void);
void iop_init_cp6_handler(void);
void iop_init_time(unsigned long tickrate);
unsigned long iop_gettimeoffset(void);

static inline void write_tmr0(u32 val)
{
	asm volatile("mcr p6, 0, %0, c0, c1, 0" : : "r" (val));
}

static inline void write_tmr1(u32 val)
{
	asm volatile("mcr p6, 0, %0, c1, c1, 0" : : "r" (val));
}

static inline u32 read_tcr0(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c2, c1, 0" : "=r" (val));
	return val;
}

static inline u32 read_tcr1(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c3, c1, 0" : "=r" (val));
	return val;
}

static inline void write_trr0(u32 val)
{
	asm volatile("mcr p6, 0, %0, c4, c1, 0" : : "r" (val));
}

static inline void write_trr1(u32 val)
{
	asm volatile("mcr p6, 0, %0, c5, c1, 0" : : "r" (val));
}

static inline void write_tisr(u32 val)
{
	asm volatile("mcr p6, 0, %0, c6, c1, 0" : : "r" (val));
}

extern struct platform_device iop3xx_i2c0_device;
extern struct platform_device iop3xx_i2c1_device;

#endif


#endif
