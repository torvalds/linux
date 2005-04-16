/* linux/include/asm-arm/arch-s3c2410/bast-map.h
 *
 * (c) 2003,2004 Simtec Electronics
 *  Ben Dooks <ben@simtec.co.uk>
 *
 * Machine BAST - Memory map definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  06-Jan-2003 BJD  Linux 2.6.0 version, moved bast specifics from arch/map.h
 *  12-Mar-2004 BJD  Fixed header include protection
*/

/* needs arch/map.h including with this */

/* ok, we've used up to 0x13000000, now we need to find space for the
 * peripherals that live in the nGCS[x] areas, which are quite numerous
 * in their space. We also have the board's CPLD to find register space
 * for.
 */

#ifndef __ASM_ARCH_BASTMAP_H
#define __ASM_ARCH_BASTMAP_H

#define BAST_IOADDR(x)	   (S3C2410_ADDR((x) + 0x01300000))

/* we put the CPLD registers next, to get them out of the way */

#define BAST_VA_CTRL1	    BAST_IOADDR(0x00000000)	 /* 0x01300000 */
#define BAST_PA_CTRL1	    (S3C2410_CS5 | 0x7800000)

#define BAST_VA_CTRL2	    BAST_IOADDR(0x00100000)	 /* 0x01400000 */
#define BAST_PA_CTRL2	    (S3C2410_CS1 | 0x6000000)

#define BAST_VA_CTRL3	    BAST_IOADDR(0x00200000)	 /* 0x01500000 */
#define BAST_PA_CTRL3	    (S3C2410_CS1 | 0x6800000)

#define BAST_VA_CTRL4	    BAST_IOADDR(0x00300000)	 /* 0x01600000 */
#define BAST_PA_CTRL4	    (S3C2410_CS1 | 0x7000000)

/* next, we have the PC104 ISA interrupt registers */

#define BAST_PA_PC104_IRQREQ  (S3C2410_CS5 | 0x6000000) /* 0x01700000 */
#define BAST_VA_PC104_IRQREQ  BAST_IOADDR(0x00400000)

#define BAST_PA_PC104_IRQRAW  (S3C2410_CS5 | 0x6800000) /* 0x01800000 */
#define BAST_VA_PC104_IRQRAW  BAST_IOADDR(0x00500000)

#define BAST_PA_PC104_IRQMASK (S3C2410_CS5 | 0x7000000) /* 0x01900000 */
#define BAST_VA_PC104_IRQMASK BAST_IOADDR(0x00600000)

#define BAST_PA_LCD_RCMD1     (0x8800000)
#define BAST_VA_LCD_RCMD1     BAST_IOADDR(0x00700000)

#define BAST_PA_LCD_WCMD1     (0x8000000)
#define BAST_VA_LCD_WCMD1     BAST_IOADDR(0x00800000)

#define BAST_PA_LCD_RDATA1    (0x9800000)
#define BAST_VA_LCD_RDATA1    BAST_IOADDR(0x00900000)

#define BAST_PA_LCD_WDATA1    (0x9000000)
#define BAST_VA_LCD_WDATA1    BAST_IOADDR(0x00A00000)

#define BAST_PA_LCD_RCMD2     (0xA800000)
#define BAST_VA_LCD_RCMD2     BAST_IOADDR(0x00B00000)

#define BAST_PA_LCD_WCMD2     (0xA000000)
#define BAST_VA_LCD_WCMD2     BAST_IOADDR(0x00C00000)

#define BAST_PA_LCD_RDATA2    (0xB800000)
#define BAST_VA_LCD_RDATA2    BAST_IOADDR(0x00D00000)

#define BAST_PA_LCD_WDATA2    (0xB000000)
#define BAST_VA_LCD_WDATA2    BAST_IOADDR(0x00E00000)


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
 * 0x00000000 to 0x01000000 16MB ISA IO space
 * 0x01000000 to 0x02000000 16MB ISA memory space
 * 0x02000000 to 0x02100000 1MB  IDE primary channel
 * 0x02100000 to 0x02200000 1MB  IDE primary channel aux
 * 0x02200000 to 0x02400000 1MB  IDE secondary channel
 * 0x02300000 to 0x02400000 1MB  IDE secondary channel aux
 * 0x02400000 to 0x02500000 1MB  ASIX ethernet controller
 * 0x02500000 to 0x02600000 1MB  Davicom DM9000 ethernet controller
 * 0x02600000 to 0x02700000 1MB  PC SuperIO controller
 *
 * the phyiscal layout of the zones are:
 *  nGCS2 - 8bit, slow
 *  nGCS3 - 16bit, slow
 *  nGCS4 - 16bit, net
 *  nGCS5 - 16bit, fast
 */

#define BAST_VA_MULTISPACE (0xE0000000)

#define BAST_VA_ISAIO	   (BAST_VA_MULTISPACE + 0x00000000)
#define BAST_VA_ISAMEM	   (BAST_VA_MULTISPACE + 0x01000000)
#define BAST_VA_IDEPRI	   (BAST_VA_MULTISPACE + 0x02000000)
#define BAST_VA_IDEPRIAUX  (BAST_VA_MULTISPACE + 0x02100000)
#define BAST_VA_IDESEC	   (BAST_VA_MULTISPACE + 0x02200000)
#define BAST_VA_IDESECAUX  (BAST_VA_MULTISPACE + 0x02300000)
#define BAST_VA_ASIXNET	   (BAST_VA_MULTISPACE + 0x02400000)
#define BAST_VA_DM9000	   (BAST_VA_MULTISPACE + 0x02500000)
#define BAST_VA_SUPERIO	   (BAST_VA_MULTISPACE + 0x02600000)

#define BAST_VA_MULTISPACE (0xE0000000)

#define BAST_VAM_CS2 (0x00000000)
#define BAST_VAM_CS3 (0x04000000)
#define BAST_VAM_CS4 (0x08000000)
#define BAST_VAM_CS5 (0x0C000000)

/* physical offset addresses for the peripherals */

#define BAST_PA_ISAIO	  (0x00000000)
#define BAST_PA_ASIXNET	  (0x01000000)
#define BAST_PA_SUPERIO	  (0x01800000)
#define BAST_PA_IDEPRI	  (0x02000000)
#define BAST_PA_IDEPRIAUX (0x02800000)
#define BAST_PA_IDESEC	  (0x03000000)
#define BAST_PA_IDESECAUX (0x03800000)
#define BAST_PA_ISAMEM	  (0x04000000)
#define BAST_PA_DM9000	  (0x05000000)

/* some configurations for the peripherals */

#define BAST_PCSIO (BAST_VA_SUPERIO + BAST_VAM_CS2)
/*  */

#define BAST_ASIXNET_CS  BAST_VAM_CS5
#define BAST_IDE_CS	 BAST_VAM_CS5
#define BAST_DM9000_CS	 BAST_VAM_CS4

#endif /* __ASM_ARCH_BASTMAP_H */
