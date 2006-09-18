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
#endif


#endif
