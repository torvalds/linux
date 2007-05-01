/* linux/include/asm-arm/arch-s3c2410/reset.h
 *
 * Copyright (c) 2007 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2410 CPU reset controls
*/

#ifndef __ASM_ARCH_RESET_H
#define __ASM_ARCH_RESET_H __FILE__

/* This allows the over-ride of the default reset code
*/

extern void (*s3c24xx_reset_hook)(void);

#endif /* __ASM_ARCH_RESET_H */
