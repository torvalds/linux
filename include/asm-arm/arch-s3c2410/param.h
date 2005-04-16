/* linux/include/asm-arm/arch-s3c2410/param.h
 *
 * (c) 2003 Simtec Electronics
 *  Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - Machine parameters
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  02-Sep-2003 BJD  Created file
 *  12-Mar-2004 BJD  Added include protection
*/

#ifndef __ASM_ARCH_PARAM_H
#define __ASM_ARCH_PARAM_H

/* we cannot get our timer down to 100Hz with the setup as is, but we can
 * manage 200 clock ticks per second... if this is a problem, we can always
 * add a software pre-scaler to the evil timer systems.
*/

#define HZ   200

#endif /* __ASM_ARCH_PARAM_H */
