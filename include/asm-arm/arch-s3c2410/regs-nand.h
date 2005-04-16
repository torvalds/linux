/* linux/include/asm-arm/arch-s3c2410/regs-nand.h
 *
 * Copyright (c) 2004 Simtec Electronics <linux@simtec.co.uk>
 *		      http://www.simtec.co.uk/products/SWLINUX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2410 clock register definitions
 *
 *  Changelog:
 *    18-Aug-2004    BJD     Copied file from 2.4 and updated
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

#define S3C2410_NFCONF_EN          (1<<15)
#define S3C2410_NFCONF_512BYTE     (1<<14)
#define S3C2410_NFCONF_4STEP       (1<<13)
#define S3C2410_NFCONF_INITECC     (1<<12)
#define S3C2410_NFCONF_nFCE        (1<<11)
#define S3C2410_NFCONF_TACLS(x)    ((x)<<8)
#define S3C2410_NFCONF_TWRPH0(x)   ((x)<<4)
#define S3C2410_NFCONF_TWRPH1(x)   ((x)<<0)

#define S3C2410_NFSTAT_BUSY        (1<<0)

/* think ECC can only be 8bit read? */

#endif /* __ASM_ARM_REGS_NAND */

