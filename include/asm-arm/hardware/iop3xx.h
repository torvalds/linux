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
 * IOP3XX processor registers
 */
#define IOP3XX_PERIPHERAL_PHYS_BASE	0xffffe000
#define IOP3XX_PERIPHERAL_VIRT_BASE	0xfeffe000
#define IOP3XX_PERIPHERAL_SIZE		0x00002000
#define IOP3XX_REG_ADDR(reg)		(IOP3XX_PERIPHERAL_VIRT_BASE + (reg))

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

#define IOP3XX_PCI_IO_WINDOW_SIZE	0x00010000
#define IOP3XX_PCI_LOWER_IO_PA		0x90000000
#define IOP3XX_PCI_LOWER_IO_VA		0xfe000000


#ifndef __ASSEMBLY__
void iop3xx_map_io(void);

extern struct platform_device iop3xx_i2c0_device;
extern struct platform_device iop3xx_i2c1_device;
#endif


#endif
