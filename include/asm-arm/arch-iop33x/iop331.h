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

/* DMA Controller 0x00000400 through 0x000004FF */

/* Memory controller 0x00000500 through 0x0005FF */

/* Peripheral bus interface unit 0x00000680 through 0x0006FF */

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


/* Application accelerator unit 0x00000800 - 0x000008FF */

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
#define IOP3XX_GPIO_REG(reg)   (IOP3XX_PERIPHERAL_VIRT_BASE + 0x1780 + (reg))
#define IOP3XX_TIMER_REG(reg)  (IOP3XX_PERIPHERAL_VIRT_BASE + 0x07d0 + (reg))

#include <asm/hardware/iop3xx.h>


#ifndef __ASSEMBLY__
extern void iop331_init_irq(void);
extern void iop331_time_init(void);
#endif

#endif // _IOP331_HW_H_
