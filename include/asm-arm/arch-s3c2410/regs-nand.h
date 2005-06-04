/* linux/include/asm-arm/arch-s3c2410/regs-nand.h
 *
 * Copyright (c) 2004,2005 Simtec Electronics <linux@simtec.co.uk>
 *		      http://www.simtec.co.uk/products/SWLINUX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2410 NAND register definitions
 *
 *  Changelog:
 *    18-Aug-2004    BJD     Copied file from 2.4 and updated
 *    01-May-2005    BJD     Added definitions for s3c2440 controller
*/

#ifndef __ASM_ARM_REGS_NAND
#define __ASM_ARM_REGS_NAND "$Id: nand.h,v 1.3 2003/12/09 11:36:29 ben Exp $"


#define S3C2410_NFREG(x) (x)

#define S3C2410_NFCONF  S3C2410_NFREG(0x00)
#define S3C2410_NFCMD   S3C2410_NFREG(0x04)
#define S3C2410_NFADDR  S3C2410_NFREG(0x08)
#define S3C2410_NFDATA  S3C2410_NFREG(0x0C)
#define S3C2410_NFSTAT  S3C2410_NFREG(0x10)
#define S3C2410_NFECC   S3C2410_NFREG(0x14)

#define S3C2440_NFCONT   S3C2410_NFREG(0x04)
#define S3C2440_NFCMD    S3C2410_NFREG(0x08)
#define S3C2440_NFADDR   S3C2410_NFREG(0x0C)
#define S3C2440_NFDATA   S3C2410_NFREG(0x10)
#define S3C2440_NFECCD0  S3C2410_NFREG(0x14)
#define S3C2440_NFECCD1  S3C2410_NFREG(0x18)
#define S3C2440_NFECCD   S3C2410_NFREG(0x1C)
#define S3C2440_NFSTAT   S3C2410_NFREG(0x20)
#define S3C2440_NFESTAT0 S3C2410_NFREG(0x24)
#define S3C2440_NFESTAT1 S3C2410_NFREG(0x28)
#define S3C2440_NFMECC0  S3C2410_NFREG(0x2C)
#define S3C2440_NFMECC1  S3C2410_NFREG(0x30)
#define S3C2440_NFSECC   S3C2410_NFREG(0x34)
#define S3C2440_NFSBLK   S3C2410_NFREG(0x38)
#define S3C2440_NFEBLK   S3C2410_NFREG(0x3C)

#define S3C2410_NFCONF_EN          (1<<15)
#define S3C2410_NFCONF_512BYTE     (1<<14)
#define S3C2410_NFCONF_4STEP       (1<<13)
#define S3C2410_NFCONF_INITECC     (1<<12)
#define S3C2410_NFCONF_nFCE        (1<<11)
#define S3C2410_NFCONF_TACLS(x)    ((x)<<8)
#define S3C2410_NFCONF_TWRPH0(x)   ((x)<<4)
#define S3C2410_NFCONF_TWRPH1(x)   ((x)<<0)

#define S3C2410_NFSTAT_BUSY        (1<<0)

#define S3C2440_NFCONF_BUSWIDTH_8	(0<<0)
#define S3C2440_NFCONF_BUSWIDTH_16	(1<<0)
#define S3C2440_NFCONF_ADVFLASH		(1<<3)
#define S3C2440_NFCONF_TACLS(x)		((x)<<12)
#define S3C2440_NFCONF_TWRPH0(x)	((x)<<8)
#define S3C2440_NFCONF_TWRPH1(x)	((x)<<4)

#define S3C2440_NFCONT_LOCKTIGHT	(1<<13)
#define S3C2440_NFCONT_SOFTLOCK		(1<<12)
#define S3C2440_NFCONT_ILLEGALACC_EN	(1<<10)
#define S3C2440_NFCONT_RNBINT_EN	(1<<9)
#define S3C2440_NFCONT_RN_FALLING	(1<<8)
#define S3C2440_NFCONT_SPARE_ECCLOCK	(1<<6)
#define S3C2440_NFCONT_MAIN_ECCLOCK	(1<<5)
#define S3C2440_NFCONT_INITECC		(1<<4)
#define S3C2440_NFCONT_nFCE		(1<<1)
#define S3C2440_NFCONT_ENABLE		(1<<0)

#define S3C2440_NFSTAT_READY		(1<<0)
#define S3C2440_NFSTAT_nCE		(1<<1)
#define S3C2440_NFSTAT_RnB_CHANGE	(1<<2)
#define S3C2440_NFSTAT_ILLEGAL_ACCESS	(1<<3)

#endif /* __ASM_ARM_REGS_NAND */

