/* linux/include/asm-arm/arch-s3c2410/regs-dsc.h
 *
 * Copyright (c) 2004 Simtec Electronics <linux@simtec.co.uk>
 *		      http://www.simtec.co.uk/products/SWLINUX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2440/S3C2412 Signal Drive Strength Control
*/


#ifndef __ASM_ARCH_REGS_DSC_H
#define __ASM_ARCH_REGS_DSC_H "2440-dsc"

#if defined(CONFIG_CPU_S3C2412)
#define S3C2412_DSC0	   S3C2410_GPIOREG(0xdc)
#define S3C2412_DSC1	   S3C2410_GPIOREG(0xe0)
#endif

#if defined(CONFIG_CPU_S3C244X)

#define S3C2440_DSC0	   S3C2410_GPIOREG(0xc4)
#define S3C2440_DSC1	   S3C2410_GPIOREG(0xc8)

#define S3C2440_SELECT_DSC0 (0)
#define S3C2440_SELECT_DSC1 (1<<31)

#define S3C2440_DSC_GETSHIFT(x) ((x) & 31)

#define S3C2440_DSC0_DISABLE	(1<<31)

#define S3C2440_DSC0_ADDR       (S3C2440_SELECT_DSC0 | 8)
#define S3C2440_DSC0_ADDR_12mA  (0<<8)
#define S3C2440_DSC0_ADDR_10mA  (1<<8)
#define S3C2440_DSC0_ADDR_8mA   (2<<8)
#define S3C2440_DSC0_ADDR_6mA   (3<<8)
#define S3C2440_DSC0_ADDR_MASK  (3<<8)

/* D24..D31 */
#define S3C2440_DSC0_DATA3      (S3C2440_SELECT_DSC0 | 6)
#define S3C2440_DSC0_DATA3_12mA (0<<6)
#define S3C2440_DSC0_DATA3_10mA (1<<6)
#define S3C2440_DSC0_DATA3_8mA  (2<<6)
#define S3C2440_DSC0_DATA3_6mA  (3<<6)
#define S3C2440_DSC0_DATA3_MASK (3<<6)

/* D16..D23 */
#define S3C2440_DSC0_DATA2      (S3C2440_SELECT_DSC0 | 4)
#define S3C2440_DSC0_DATA2_12mA (0<<4)
#define S3C2440_DSC0_DATA2_10mA (1<<4)
#define S3C2440_DSC0_DATA2_8mA  (2<<4)
#define S3C2440_DSC0_DATA2_6mA  (3<<4)
#define S3C2440_DSC0_DATA2_MASK (3<<4)

/* D8..D15 */
#define S3C2440_DSC0_DATA1      (S3C2440_SELECT_DSC0 | 2)
#define S3C2440_DSC0_DATA1_12mA (0<<2)
#define S3C2440_DSC0_DATA1_10mA (1<<2)
#define S3C2440_DSC0_DATA1_8mA  (2<<2)
#define S3C2440_DSC0_DATA1_6mA  (3<<2)
#define S3C2440_DSC0_DATA1_MASK (3<<2)

/* D0..D7 */
#define S3C2440_DSC0_DATA0      (S3C2440_SELECT_DSC0 | 0)
#define S3C2440_DSC0_DATA0_12mA (0<<0)
#define S3C2440_DSC0_DATA0_10mA (1<<0)
#define S3C2440_DSC0_DATA0_8mA  (2<<0)
#define S3C2440_DSC0_DATA0_6mA  (3<<0)
#define S3C2440_DSC0_DATA0_MASK (3<<0)

#define S3C2440_DSC1_SCK1       (S3C2440_SELECT_DSC1 | 28)
#define S3C2440_DSC1_SCK1_12mA  (0<<28)
#define S3C2440_DSC1_SCK1_10mA  (1<<28)
#define S3C2440_DSC1_SCK1_8mA   (2<<28)
#define S3C2440_DSC1_SCK1_6mA   (3<<28)
#define S3C2440_DSC1_SCK1_MASK  (3<<28)

#define S3C2440_DSC1_SCK0       (S3C2440_SELECT_DSC1 | 26)
#define S3C2440_DSC1_SCK0_12mA  (0<<26)
#define S3C2440_DSC1_SCK0_10mA  (1<<26)
#define S3C2440_DSC1_SCK0_8mA   (2<<26)
#define S3C2440_DSC1_SCK0_6mA   (3<<26)
#define S3C2440_DSC1_SCK0_MASK  (3<<26)

#define S3C2440_DSC1_SCKE       (S3C2440_SELECT_DSC1 | 24)
#define S3C2440_DSC1_SCKE_10mA  (0<<24)
#define S3C2440_DSC1_SCKE_8mA   (1<<24)
#define S3C2440_DSC1_SCKE_6mA   (2<<24)
#define S3C2440_DSC1_SCKE_4mA   (3<<24)
#define S3C2440_DSC1_SCKE_MASK  (3<<24)

/* SDRAM nRAS/nCAS */
#define S3C2440_DSC1_SDR        (S3C2440_SELECT_DSC1 | 22)
#define S3C2440_DSC1_SDR_10mA   (0<<22)
#define S3C2440_DSC1_SDR_8mA    (1<<22)
#define S3C2440_DSC1_SDR_6mA    (2<<22)
#define S3C2440_DSC1_SDR_4mA    (3<<22)
#define S3C2440_DSC1_SDR_MASK   (3<<22)

/* NAND Flash Controller */
#define S3C2440_DSC1_NFC        (S3C2440_SELECT_DSC1 | 20)
#define S3C2440_DSC1_NFC_10mA   (0<<20)
#define S3C2440_DSC1_NFC_8mA    (1<<20)
#define S3C2440_DSC1_NFC_6mA    (2<<20)
#define S3C2440_DSC1_NFC_4mA    (3<<20)
#define S3C2440_DSC1_NFC_MASK   (3<<20)

/* nBE[0..3] */
#define S3C2440_DSC1_nBE        (S3C2440_SELECT_DSC1 | 18)
#define S3C2440_DSC1_nBE_10mA   (0<<18)
#define S3C2440_DSC1_nBE_8mA    (1<<18)
#define S3C2440_DSC1_nBE_6mA    (2<<18)
#define S3C2440_DSC1_nBE_4mA    (3<<18)
#define S3C2440_DSC1_nBE_MASK   (3<<18)

#define S3C2440_DSC1_WOE        (S3C2440_SELECT_DSC1 | 16)
#define S3C2440_DSC1_WOE_10mA   (0<<16)
#define S3C2440_DSC1_WOE_8mA    (1<<16)
#define S3C2440_DSC1_WOE_6mA    (2<<16)
#define S3C2440_DSC1_WOE_4mA    (3<<16)
#define S3C2440_DSC1_WOE_MASK   (3<<16)

#define S3C2440_DSC1_CS7        (S3C2440_SELECT_DSC1 | 14)
#define S3C2440_DSC1_CS7_10mA   (0<<14)
#define S3C2440_DSC1_CS7_8mA    (1<<14)
#define S3C2440_DSC1_CS7_6mA    (2<<14)
#define S3C2440_DSC1_CS7_4mA    (3<<14)
#define S3C2440_DSC1_CS7_MASK   (3<<14)

#define S3C2440_DSC1_CS6        (S3C2440_SELECT_DSC1 | 12)
#define S3C2440_DSC1_CS6_10mA   (0<<12)
#define S3C2440_DSC1_CS6_8mA    (1<<12)
#define S3C2440_DSC1_CS6_6mA    (2<<12)
#define S3C2440_DSC1_CS6_4mA    (3<<12)
#define S3C2440_DSC1_CS6_MASK   (3<<12)

#define S3C2440_DSC1_CS5        (S3C2440_SELECT_DSC1 | 10)
#define S3C2440_DSC1_CS5_10mA   (0<<10)
#define S3C2440_DSC1_CS5_8mA    (1<<10)
#define S3C2440_DSC1_CS5_6mA    (2<<10)
#define S3C2440_DSC1_CS5_4mA    (3<<10)
#define S3C2440_DSC1_CS5_MASK   (3<<10)

#define S3C2440_DSC1_CS4        (S3C2440_SELECT_DSC1 | 8)
#define S3C2440_DSC1_CS4_10mA   (0<<8)
#define S3C2440_DSC1_CS4_8mA    (1<<8)
#define S3C2440_DSC1_CS4_6mA    (2<<8)
#define S3C2440_DSC1_CS4_4mA    (3<<8)
#define S3C2440_DSC1_CS4_MASK   (3<<8)

#define S3C2440_DSC1_CS3        (S3C2440_SELECT_DSC1 | 6)
#define S3C2440_DSC1_CS3_10mA   (0<<6)
#define S3C2440_DSC1_CS3_8mA    (1<<6)
#define S3C2440_DSC1_CS3_6mA    (2<<6)
#define S3C2440_DSC1_CS3_4mA    (3<<6)
#define S3C2440_DSC1_CS3_MASK   (3<<6)

#define S3C2440_DSC1_CS2        (S3C2440_SELECT_DSC1 | 4)
#define S3C2440_DSC1_CS2_10mA   (0<<4)
#define S3C2440_DSC1_CS2_8mA    (1<<4)
#define S3C2440_DSC1_CS2_6mA    (2<<4)
#define S3C2440_DSC1_CS2_4mA    (3<<4)
#define S3C2440_DSC1_CS2_MASK   (3<<4)

#define S3C2440_DSC1_CS1        (S3C2440_SELECT_DSC1 | 2)
#define S3C2440_DSC1_CS1_10mA   (0<<2)
#define S3C2440_DSC1_CS1_8mA    (1<<2)
#define S3C2440_DSC1_CS1_6mA    (2<<2)
#define S3C2440_DSC1_CS1_4mA    (3<<2)
#define S3C2440_DSC1_CS1_MASK   (3<<2)

#define S3C2440_DSC1_CS0        (S3C2440_SELECT_DSC1 | 0)
#define S3C2440_DSC1_CS0_10mA   (0<<0)
#define S3C2440_DSC1_CS0_8mA    (1<<0)
#define S3C2440_DSC1_CS0_6mA    (2<<0)
#define S3C2440_DSC1_CS0_4mA    (3<<0)
#define S3C2440_DSC1_CS0_MASK   (3<<0)

#endif /* CONFIG_CPU_S3C2440 */

#endif	/* __ASM_ARCH_REGS_DSC_H */

