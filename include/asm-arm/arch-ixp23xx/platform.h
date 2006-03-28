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

struct pci_sys_data;

void ixp23xx_map_io(void);
void ixp23xx_init_irq(void);
void ixp23xx_sys_init(void);
int ixp23xx_pci_setup(int, struct pci_sys_data *);
void ixp23xx_pci_preinit(void);
struct pci_bus *ixp23xx_pci_scan_bus(int, struct pci_sys_data*);

extern struct sys_timer ixp23xx_timer;

#define IXP23XX_UART_XTAL		14745600


#endif
