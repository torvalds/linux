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

/* General Purpose I/O  */
#define IOP3XX_GPOE		(volatile u32 *)IOP3XX_GPIO_REG(0x0004)
#define IOP3XX_GPID		(volatile u32 *)IOP3XX_GPIO_REG(0x0008)
#define IOP3XX_GPOD		(volatile u32 *)IOP3XX_GPIO_REG(0x000c)

/* Timers  */
#define IOP3XX_TU_TMR0		(volatile u32 *)IOP3XX_TIMER_REG(0x0000)
#define IOP3XX_TU_TMR1		(volatile u32 *)IOP3XX_TIMER_REG(0x0004)
#define IOP3XX_TU_TCR0		(volatile u32 *)IOP3XX_TIMER_REG(0x0008)
#define IOP3XX_TU_TCR1		(volatile u32 *)IOP3XX_TIMER_REG(0x000c)
#define IOP3XX_TU_TRR0		(volatile u32 *)IOP3XX_TIMER_REG(0x0010)
#define IOP3XX_TU_TRR1		(volatile u32 *)IOP3XX_TIMER_REG(0x0014)
#define IOP3XX_TU_TISR		(volatile u32 *)IOP3XX_TIMER_REG(0x0018)
#define IOP3XX_TU_WDTCR		(volatile u32 *)IOP3XX_TIMER_REG(0x001c)
#define IOP3XX_TMR_TC		0x01
#define IOP3XX_TMR_EN		0x02
#define IOP3XX_TMR_RELOAD	0x04
#define IOP3XX_TMR_PRIVILEGED	0x09
#define IOP3XX_TMR_RATIO_1_1	0x00
#define IOP3XX_TMR_RATIO_4_1	0x10
#define IOP3XX_TMR_RATIO_8_1	0x20
#define IOP3XX_TMR_RATIO_16_1	0x30

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


#ifndef __ASSEMBLY__
void iop3xx_map_io(void);
void iop3xx_init_time(unsigned long);
unsigned long iop3xx_gettimeoffset(void);

extern struct platform_device iop3xx_i2c0_device;
extern struct platform_device iop3xx_i2c1_device;

extern inline void iop3xx_cp6_enable(void)
{
	u32 temp;

	asm volatile (
		"mrc	p15, 0, %0, c15, c1, 0\n\t"
		"orr	%0, %0, #(1 << 6)\n\t"
		"mcr	p15, 0, %0, c15, c1, 0\n\t"
		"mrc	p15, 0, %0, c15, c1, 0\n\t"
		"mov	%0, %0\n\t"
		"sub	pc, pc, #4\n\t"
		: "=r" (temp) );
}

extern inline void iop3xx_cp6_disable(void)
{
	u32 temp;

	asm volatile (
		"mrc	p15, 0, %0, c15, c1, 0\n\t"
		"bic	%0, %0, #(1 << 6)\n\t"
		"mcr	p15, 0, %0, c15, c1, 0\n\t"
		"mrc	p15, 0, %0, c15, c1, 0\n\t"
		"mov	%0, %0\n\t"
		"sub	pc, pc, #4\n\t"
		: "=r" (temp) );
}
#endif


#endif
