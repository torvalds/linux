/* linux/include/asm/arch-s3c2410/regs-irq.h
 *
 * Copyright (c) 2003 Simtec Electronics <linux@simtec.co.uk>
 *		      http://www.simtec.co.uk/products/SWLINUX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 *
 *  Changelog:
 *    19-06-2003     BJD     Created file
 *    12-03-2004     BJD     Updated include protection
 *    10-03-2005     LCVR    Changed S3C2410_VA to S3C24XX_VA
 */


#ifndef ___ASM_ARCH_REGS_IRQ_H
#define ___ASM_ARCH_REGS_IRQ_H "$Id: irq.h,v 1.3 2003/03/25 21:29:06 ben Exp $"

/* interrupt controller */

#define S3C2410_IRQREG(x)   ((x) + S3C24XX_VA_IRQ)
#define S3C2410_EINTREG(x)  ((x) + S3C24XX_VA_GPIO)
#define S3C24XX_EINTREG(x)  ((x) + S3C24XX_VA_GPIO2)

#define S3C2410_SRCPND	       S3C2410_IRQREG(0x000)
#define S3C2410_INTMOD	       S3C2410_IRQREG(0x004)
#define S3C2410_INTMSK	       S3C2410_IRQREG(0x008)
#define S3C2410_PRIORITY       S3C2410_IRQREG(0x00C)
#define S3C2410_INTPND	       S3C2410_IRQREG(0x010)
#define S3C2410_INTOFFSET      S3C2410_IRQREG(0x014)
#define S3C2410_SUBSRCPND      S3C2410_IRQREG(0x018)
#define S3C2410_INTSUBMSK      S3C2410_IRQREG(0x01C)

/* mask: 0=enable, 1=disable
 * 1 bit EINT, 4=EINT4, 23=EINT23
 * EINT0,1,2,3 are not handled here.
*/

#define S3C2410_EINTMASK       S3C2410_EINTREG(0x0A4)
#define S3C2410_EINTPEND       S3C2410_EINTREG(0X0A8)
#define S3C2412_EINTMASK       S3C2410_EINTREG(0x0B4)
#define S3C2412_EINTPEND       S3C2410_EINTREG(0X0B8)

#define S3C24XX_EINTMASK       S3C24XX_EINTREG(0x0A4)
#define S3C24XX_EINTPEND       S3C24XX_EINTREG(0X0A8)

#endif /* ___ASM_ARCH_REGS_IRQ_H */
