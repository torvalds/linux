/*
 * include/asm-arm/arch-ixp23xx/platform.h
 *
 * Various bits of code used by platform-level code.
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2005 (c) MontaVista Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASSEMBLY__

extern inline unsigned long ixp2000_reg_read(volatile void *reg)
{
	return *((volatile unsigned long *)reg);
}

extern inline void ixp2000_reg_write(volatile void *reg, unsigned long val)
{
	*((volatile unsigned long *)reg) = val;
}

extern inline void ixp2000_reg_wrb(volatile void *reg, unsigned long val)
{
	*((volatile unsigned long *)reg) = val;
}

struct pci_sys_data;

void ixp23xx_map_io(void);
void ixp23xx_init_irq(void);
void ixp23xx_sys_init(void);
int ixp23xx_pci_setup(int, struct pci_sys_data *);
void ixp23xx_pci_preinit(void);
struct pci_bus *ixp23xx_pci_scan_bus(int, struct pci_sys_data*);
void ixp23xx_pci_slave_init(void);

extern struct sys_timer ixp23xx_timer;

#define IXP23XX_UART_XTAL		14745600

#ifndef __ASSEMBLY__
/*
 * Is system memory on the XSI or CPP bus?
 */
static inline unsigned ixp23xx_cpp_boot(void)
{
	return (*IXP23XX_EXP_CFG0 & IXP23XX_EXP_CFG0_XSI_NOT_PRES);
}
#endif


#endif
