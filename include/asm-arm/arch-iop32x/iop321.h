/*
 * linux/include/asm/arch-iop32x/iop321.h
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
#define iop_is_321()		1
#endif

/*
 * IOP321 chipset registers
 */
#define IOP321_VIRT_MEM_BASE 0xfeffe000  /* chip virtual mem address*/
#define IOP321_PHYS_MEM_BASE 0xffffe000  /* chip physical memory address */
#define IOP321_REG_ADDR(reg) (IOP321_VIRT_MEM_BASE | (reg))

/* Reserved 0x00000000 through 0x000000FF */

/* Address Translation Unit 0x00000100 through 0x000001FF */

/* Messaging Unit 0x00000300 through 0x000003FF */

/* DMA Controller 0x00000400 through 0x000004FF */

/* Memory controller 0x00000500 through 0x0005FF */

/* Peripheral bus interface unit 0x00000680 through 0x0006FF */

/* Peripheral performance monitoring unit 0x00000700 through 0x00077F */

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

/* Application accelerator unit 0x00000800 - 0x000008FF */

/* SSP serial port unit 0x00001600 - 0x0000167F */
/* I2C bus interface unit 0x00001680 - 0x000016FF */

/* for I2C bit defs see drivers/i2c/i2c-iop3xx.h */

/*
 * Peripherals that are shared between the iop32x and iop33x but
 * located at different addresses.
 */
#define IOP3XX_GPIO_REG(reg)   (IOP3XX_PERIPHERAL_VIRT_BASE + 0x07c0 + (reg))
#define IOP3XX_TIMER_REG(reg)  (IOP3XX_PERIPHERAL_VIRT_BASE + 0x07e0 + (reg))

#include <asm/hardware/iop3xx.h>


#ifndef __ASSEMBLY__
extern void iop321_init_irq(void);
extern void iop321_time_init(void);
#endif

#endif // _IOP321_HW_H_
