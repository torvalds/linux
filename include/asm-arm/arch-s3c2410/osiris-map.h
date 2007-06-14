/* linux/include/asm-arm/arch-s3c2410/osiris-map.h
 *
 * (c) 2005 Simtec Electronics
 *	http://www.simtec.co.uk/products/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * OSIRIS - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* needs arch/map.h including with this */

#ifndef __ASM_ARCH_OSIRISMAP_H
#define __ASM_ARCH_OSIRISMAP_H

/* start peripherals off after the S3C2410 */

#define OSIRIS_IOADDR(x)	(S3C2410_ADDR((x) + 0x04000000))

#define OSIRIS_PA_CPLD		(S3C2410_CS1 | (1<<26))

/* we put the CPLD registers next, to get them out of the way */

#define OSIRIS_VA_CTRL0		OSIRIS_IOADDR(0x00000000)
#define OSIRIS_PA_CTRL0		(OSIRIS_PA_CPLD)

#define OSIRIS_VA_CTRL1		OSIRIS_IOADDR(0x00100000)
#define OSIRIS_PA_CTRL1		(OSIRIS_PA_CPLD + (1<<23))

#define OSIRIS_VA_CTRL2		OSIRIS_IOADDR(0x00200000)
#define OSIRIS_PA_CTRL2		(OSIRIS_PA_CPLD + (2<<23))

#define OSIRIS_VA_CTRL3		OSIRIS_IOADDR(0x00300000)
#define OSIRIS_PA_CTRL3		(OSIRIS_PA_CPLD + (2<<23))

#define OSIRIS_VA_IDREG		OSIRIS_IOADDR(0x00700000)
#define OSIRIS_PA_IDREG		(OSIRIS_PA_CPLD + (7<<23))

#endif /* __ASM_ARCH_OSIRISMAP_H */
