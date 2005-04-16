/* linux/include/asm-arm/arch-s3c2410/vr1000-map.h
 *
 * (c) 2003-2005 Simtec Electronics
 *  Ben Dooks <ben@simtec.co.uk>
 *
 * Machine VR1000 - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  06-Jan-2003 BJD  Linux 2.6.0 version, split specifics from arch/map.h
 *  12-Mar-2004 BJD  Fixed header include protection
 *  19-Mar-2004 BJD  Copied to VR1000 machine headers.
 *  19-Jan-2005 BJD  Updated map definitions
*/

/* needs arch/map.h including with this */

/* ok, we've used up to 0x13000000, now we need to find space for the
 * peripherals that live in the nGCS[x] areas, which are quite numerous
 * in their space. We also have the board's CPLD to find register space
 * for.
 */

#ifndef __ASM_ARCH_VR1000MAP_H
#define __ASM_ARCH_VR1000MAP_H

#include <asm/arch/bast-map.h>

#define VR1000_IOADDR(x) BAST_IOADDR(x)

/* we put the CPLD registers next, to get them out of the way */

#define VR1000_VA_CTRL1	    VR1000_IOADDR(0x00000000)	 /* 0x01300000 */
#define VR1000_PA_CTRL1	    (S3C2410_CS5 | 0x7800000)

#define VR1000_VA_CTRL2	    VR1000_IOADDR(0x00100000)	 /* 0x01400000 */
#define VR1000_PA_CTRL2	    (S3C2410_CS1 | 0x6000000)

#define VR1000_VA_CTRL3	    VR1000_IOADDR(0x00200000)	 /* 0x01500000 */
#define VR1000_PA_CTRL3	    (S3C2410_CS1 | 0x6800000)

#define VR1000_VA_CTRL4	    VR1000_IOADDR(0x00300000)	 /* 0x01600000 */
#define VR1000_PA_CTRL4	    (S3C2410_CS1 | 0x7000000)

/* next, we have the PC104 ISA interrupt registers */

#define VR1000_PA_PC104_IRQREQ  (S3C2410_CS5 | 0x6000000) /* 0x01700000 */
#define VR1000_VA_PC104_IRQREQ  VR1000_IOADDR(0x00400000)

#define VR1000_PA_PC104_IRQRAW  (S3C2410_CS5 | 0x6800000) /* 0x01800000 */
#define VR1000_VA_PC104_IRQRAW  VR1000_IOADDR(0x00500000)

#define VR1000_PA_PC104_IRQMASK (S3C2410_CS5 | 0x7000000) /* 0x01900000 */
#define VR1000_VA_PC104_IRQMASK VR1000_IOADDR(0x00600000)

/* 0xE0000000 contains the IO space that is split by speed and
 * wether the access is for 8 or 16bit IO... this ensures that
 * the correct access is made
 *
 * 0x10000000 of space, partitioned as so:
 *
 * 0x00000000 to 0x04000000  8bit,  slow
 * 0x04000000 to 0x08000000  16bit, slow
 * 0x08000000 to 0x0C000000  16bit, net
 * 0x0C000000 to 0x10000000  16bit, fast
 *
 * each of these spaces has the following in:
 *
 * 0x02000000 to 0x02100000 1MB  IDE primary channel
 * 0x02100000 to 0x02200000 1MB  IDE primary channel aux
 * 0x02200000 to 0x02400000 1MB  IDE secondary channel
 * 0x02300000 to 0x02400000 1MB  IDE secondary channel aux
 * 0x02500000 to 0x02600000 1MB  Davicom DM9000 ethernet controllers
 * 0x02600000 to 0x02700000 1MB
 *
 * the phyiscal layout of the zones are:
 *  nGCS2 - 8bit, slow
 *  nGCS3 - 16bit, slow
 *  nGCS4 - 16bit, net
 *  nGCS5 - 16bit, fast
 */

#define VR1000_VA_MULTISPACE (0xE0000000)

#define VR1000_VA_ISAIO		   (VR1000_VA_MULTISPACE + 0x00000000)
#define VR1000_VA_ISAMEM	   (VR1000_VA_MULTISPACE + 0x01000000)
#define VR1000_VA_IDEPRI	   (VR1000_VA_MULTISPACE + 0x02000000)
#define VR1000_VA_IDEPRIAUX	   (VR1000_VA_MULTISPACE + 0x02100000)
#define VR1000_VA_IDESEC	   (VR1000_VA_MULTISPACE + 0x02200000)
#define VR1000_VA_IDESECAUX	   (VR1000_VA_MULTISPACE + 0x02300000)
#define VR1000_VA_ASIXNET	   (VR1000_VA_MULTISPACE + 0x02400000)
#define VR1000_VA_DM9000	   (VR1000_VA_MULTISPACE + 0x02500000)
#define VR1000_VA_SUPERIO	   (VR1000_VA_MULTISPACE + 0x02600000)

/* physical offset addresses for the peripherals */

#define VR1000_PA_IDEPRI	   (0x02000000)
#define VR1000_PA_IDEPRIAUX	   (0x02800000)
#define VR1000_PA_IDESEC	   (0x03000000)
#define VR1000_PA_IDESECAUX	   (0x03800000)
#define VR1000_PA_DM9000	   (0x05000000)

#define VR1000_PA_SERIAL	   (0x11800000)
#define VR1000_VA_SERIAL	   (VR1000_IOADDR(0x00700000))

/* VR1000 ram is in CS1, with A26..A24 = 2_101 */
#define VR1000_PA_SRAM		   (S3C2410_CS1 | 0x05000000)

/* some configurations for the peripherals */

#define VR1000_DM9000_CS	 VR1000_VAM_CS4

#endif /* __ASM_ARCH_VR1000MAP_H */
