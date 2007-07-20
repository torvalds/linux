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
extern int init_atu;
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
#define IOP3XX_PMMR_PHYS_TO_VIRT(addr) (u32) ((u32) (addr) -\
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
#define IOP3XX_PCSR_OUT_Q_BUSY (1 << 15)
#define IOP3XX_PCSR_IN_Q_BUSY	(1 << 14)
#define IOP3XX_ATUCR_OUT_EN	(1 << 1)

#define IOP3XX_INIT_ATU_DEFAULT 0
#define IOP3XX_INIT_ATU_DISABLE -1
#define IOP3XX_INIT_ATU_ENABLE	 1

#ifdef CONFIG_IOP3XX_ATU
#define iop3xx_get_init_atu(x) (init_atu == IOP3XX_INIT_ATU_DEFAULT ?\
				IOP3XX_INIT_ATU_ENABLE : init_atu)
#else
#define iop3xx_get_init_atu(x) (init_atu == IOP3XX_INIT_ATU_DEFAULT ?\
				IOP3XX_INIT_ATU_DISABLE : init_atu)
#endif

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
#define IOP3XX_DMA_PHYS_BASE(chan) (IOP3XX_PERIPHERAL_PHYS_BASE + \
					(0x400 + (chan << 6)))
#define IOP3XX_DMA_UPPER_PA(chan)  (IOP3XX_DMA_PHYS_BASE(chan) + 0x27)

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

/* Watchdog timer definitions */
#define IOP_WDTCR_EN_ARM        0x1e1e1e1e
#define IOP_WDTCR_EN            0xe1e1e1e1
/* iop3xx does not support stopping the watchdog, so we just re-arm */
#define IOP_WDTCR_DIS_ARM	(IOP_WDTCR_EN_ARM)
#define IOP_WDTCR_DIS		(IOP_WDTCR_EN)

/* Application accelerator unit  */
#define IOP3XX_AAU_PHYS_BASE (IOP3XX_PERIPHERAL_PHYS_BASE + 0x800)
#define IOP3XX_AAU_UPPER_PA (IOP3XX_AAU_PHYS_BASE + 0xa7)

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
#define IOP3XX_PCI_LOWER_MEM_PA	0x80000000

#define IOP3XX_PCI_IO_WINDOW_SIZE	0x00010000
#define IOP3XX_PCI_LOWER_IO_PA		0x90000000
#define IOP3XX_PCI_LOWER_IO_VA		0xfe000000
#define IOP3XX_PCI_LOWER_IO_BA		0x90000000
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

static inline u32 read_wdtcr(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c7, c1, 0":"=r" (val));
	return val;
}
static inline void write_wdtcr(u32 val)
{
	asm volatile("mcr p6, 0, %0, c7, c1, 0"::"r" (val));
}

extern unsigned long get_iop_tick_rate(void);

/* only iop13xx has these registers, we define these to present a
 * common register interface for the iop_wdt driver.
 */
#define IOP_RCSR_WDT	(0)
static inline u32 read_rcsr(void)
{
	return 0;
}
static inline void write_wdtsr(u32 val)
{
	do { } while (0);
}

extern struct platform_device iop3xx_dma_0_channel;
extern struct platform_device iop3xx_dma_1_channel;
extern struct platform_device iop3xx_aau_channel;
extern struct platform_device iop3xx_i2c0_device;
extern struct platform_device iop3xx_i2c1_device;

#endif


#endif
