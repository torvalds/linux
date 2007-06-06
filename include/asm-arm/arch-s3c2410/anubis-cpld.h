/* linux/include/asm-arm/arch-s3c2410/anubis-cpld.h
 *
 * Copyright (c) 2005 Simtec Electronics
 *	http://www.simtec.co.uk/products/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * ANUBIS - CPLD control constants
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_ANUBISCPLD_H
#define __ASM_ARCH_ANUBISCPLD_H

/* CTRL2 - NAND WP control, IDE Reset assert/check */

#define ANUBIS_CTRL1_NANDSEL		(0x3)

/* IDREG - revision */

#define ANUBIS_IDREG_REVMASK		(0x7)

#endif /* __ASM_ARCH_ANUBISCPLD_H */
