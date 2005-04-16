/*
 * linux/include/asm-arm/arch-iop3xx/irqs.h
 *
 * Copyright:	(C) 2001-2003 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/*
 * Chipset-specific bits
 */
#ifdef CONFIG_ARCH_IOP321
#include "iop321-irqs.h"
#endif

#ifdef CONFIG_ARCH_IOP331
#include "iop331-irqs.h"
#endif
