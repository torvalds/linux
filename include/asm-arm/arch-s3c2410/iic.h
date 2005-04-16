/* linux/include/asm-arm/arch-s3c2410/iic.h
 *
 * (c) 2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - I2C Controller platfrom_device info
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *	05-Oct-2004 BJD  Created file
 *	19-Oct-2004 BJD	 Updated for s3c2440
*/

#ifndef __ASM_ARCH_IIC_H
#define __ASM_ARCH_IIC_H __FILE__

#define S3C_IICFLG_FILTER	(1<<0)	/* enable s3c2440 filter */

/* Notes:
 *	1) All frequencies are expressed in Hz
 *	2) A value of zero is `do not care`
*/

struct s3c2410_platform_i2c {
	unsigned int	flags;
	unsigned int	slave_addr;	/* slave address for controller */
	unsigned long	bus_freq;	/* standard bus frequency */
	unsigned long	max_freq;	/* max frequency for the bus */
	unsigned long	min_freq;	/* min frequency for the bus */
	unsigned int	sda_delay;	/* pclks (s3c2440 only) */
};

#endif /* __ASM_ARCH_IIC_H */
