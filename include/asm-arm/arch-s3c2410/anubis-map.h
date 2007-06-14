/* linux/include/asm-arm/arch-s3c2410/anubis-map.h
 *
 * Copyright (c) 2005 Simtec Electronics
 *	http://www.simtec.co.uk/products/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * ANUBIS - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* needs arch/map.h including with this */

#ifndef __ASM_ARCH_ANUBISMAP_H
#define __ASM_ARCH_ANUBISMAP_H

/* start peripherals off after the S3C2410 */

#define ANUBIS_IOADDR(x)	(S3C2410_ADDR((x) + 0x01800000))

#define ANUBIS_PA_CPLD		(S3C2410_CS1 | (1<<26))

/* we put the CPLD registers next, to get them out of the way */

#define ANUBIS_VA_CTRL1	    ANUBIS_IOADDR(0x00000000)	 /* 0x01800000 */
#define ANUBIS_PA_CTRL1	    (ANUBIS_PA_CPLD)

#define ANUBIS_VA_IDREG	    ANUBIS_IOADDR(0x00300000)	 /* 0x01B00000 */
#define ANUBIS_PA_IDREG	    (ANUBIS_PA_CPLD + (3<<23))

#define ANUBIS_IDEPRI	    ANUBIS_IOADDR(0x01000000)
#define ANUBIS_IDEPRIAUX    ANUBIS_IOADDR(0x01100000)
#define ANUBIS_IDESEC	    ANUBIS_IOADDR(0x01200000)
#define ANUBIS_IDESECAUX    ANUBIS_IOADDR(0x01300000)

#endif /* __ASM_ARCH_ANUBISMAP_H */
