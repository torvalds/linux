/* linux/include/asm-arm/arch-s3c2410/osiris-cpld.h
 *
 * Copyright (c) 2005 Simtec Electronics
 *	http://www.simtec.co.uk/products/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * OSIRIS - CPLD control constants
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_OSIRISCPLD_H
#define __ASM_ARCH_OSIRISCPLD_H

/* CTRL1 - NAND WP control */

#define OSIRIS_CTRL1_NANDSEL		(0x3)
#define OSIRIS_CTRL1_BOOT_INT		(1<<3)
#define OSIRIS_CTRL1_PCMCIA		(1<<4)
#define OSIRIS_CTRL1_PCMCIA_nWAIT	(1<<6)
#define OSIRIS_CTRL1_PCMCIA_nIOIS16	(1<<7)

#endif /* __ASM_ARCH_OSIRISCPLD_H */
