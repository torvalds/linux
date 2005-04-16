/*
 * linux/include/asm-arm/arch-ixp2000/system.h
 *
 * Copyright (C) 2002 Intel Corp.
 * Copyricht (C) 2003-2005 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/hardware.h>
#include <asm/mach-types.h>

static inline void arch_idle(void)
{
	cpu_do_idle();
}

static inline void arch_reset(char mode)
{
	local_irq_disable();

	/*
	 * Reset flash banking register so that we are pointing at
	 * RedBoot bank.
	 */
	if (machine_is_ixdp2401()) {
		*IXDP2X01_CPLD_FLASH_REG = ((0 >> IXDP2X01_FLASH_WINDOW_BITS)
						| IXDP2X01_CPLD_FLASH_INTERN);
		*IXDP2X01_CPLD_RESET_REG = 0xffffffff;
	}

	/*
	 * On IXDP2801 we need to write this magic sequence to the CPLD
	 * to cause a complete reset of the CPU and all external devices
	 * and moves the flash bank register back to 0.
	 */
	if (machine_is_ixdp2801()) {
		unsigned long reset_reg = *IXDP2X01_CPLD_RESET_REG;
		reset_reg = 0x55AA0000 | (reset_reg & 0x0000FFFF);
		*IXDP2X01_CPLD_RESET_REG = reset_reg;
		mb();
		*IXDP2X01_CPLD_RESET_REG = 0x80000000;
	}

	/*
	 * We do a reset all if we are PCI master. We could be a slave and we
	 * don't want to do anything funky on the PCI bus.
	 */
	if (*IXP2000_STRAP_OPTIONS & CFG_PCI_BOOT_HOST) {
		*(IXP2000_RESET0) |= (RSTALL);
	}
}
