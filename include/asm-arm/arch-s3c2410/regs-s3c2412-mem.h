/* linux/include/asm-arm/arch-s3c2410/regs-s3c2412-mem.h
 *
 * Copyright (c) 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2412 memory register definitions
*/

#ifndef __ASM_ARM_REGS_S3C2412_MEM
#define __ASM_ARM_REGS_S3C2412_MEM

#ifndef S3C2412_MEMREG
#define S3C2412_MEMREG(x) (S3C24XX_VA_MEMCTRL + (x))
#endif

#define S3C2412_BANKCFG			S3C2412_MEMREG(0x00)
#define S3C2412_BANKCON1		S3C2412_MEMREG(0x04)
#define S3C2412_BANKCON2		S3C2412_MEMREG(0x08)
#define S3C2412_BANKCON3		S3C2412_MEMREG(0x0C)

#define S3C2412_REFRESH			S3C2412_MEMREG(0x10)
#define S3C2412_TIMEOUT			S3C2412_MEMREG(0x14)

#endif /*  __ASM_ARM_REGS_S3C2412_MEM */
