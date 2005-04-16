/*
 * linux/include/asm-arm/arch-iop3xx/system.h
 *
 *  Copyright (C) 2001 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

static inline void arch_idle(void)
{
	cpu_do_idle();
}


static inline void arch_reset(char mode)
{
#ifdef CONFIG_ARCH_IOP321
    *IOP321_PCSR = 0x30;
#endif

#ifdef CONFIG_ARCH_IOP331
    *IOP331_PCSR = 0x30;
#endif

	if ( 1 && mode == 's') {
		/* Jump into ROM at address 0 */
		cpu_reset(0);
	} else {
		/* No on-chip reset capability */
		cpu_reset(0);
	}
}

