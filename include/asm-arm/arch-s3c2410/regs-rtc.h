/* linux/include/asm/arch-s3c2410/regs-rtc.h
 *
 * Copyright (c) 2003 Simtec Electronics <linux@simtec.co.uk>
 *		      http://www.simtec.co.uk/products/SWLINUX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2410 Internal RTC register definition
 *
 *  Changelog:
 *    19-06-2003     BJD     Created file
 *    12-03-2004     BJD     Updated include protection
 *    15-01-2005     LCVR    Changed S3C2410_VA to S3C24XX_VA (s3c2400 support)
*/

#ifndef __ASM_ARCH_REGS_RTC_H
#define __ASM_ARCH_REGS_RTC_H __FILE__

#define S3C2410_RTCREG(x) ((x) + S3C24XX_VA_RTC)

#define S3C2410_RTCCON	      S3C2410_RTCREG(0x40)
#define S3C2410_RTCCON_RTCEN  (1<<0)
#define S3C2410_RTCCON_CLKSEL (1<<1)
#define S3C2410_RTCCON_CNTSEL (1<<2)
#define S3C2410_RTCCON_CLKRST (1<<3)

#define S3C2410_TICNT	      S3C2410_RTCREG(0x44)
#define S3C2410_TICNT_ENABLE  (1<<7)

#define S3C2410_RTCALM	      S3C2410_RTCREG(0x50)
#define S3C2410_RTCALM_ALMEN  (1<<6)
#define S3C2410_RTCALM_YEAREN (1<<5)
#define S3C2410_RTCALM_MONEN  (1<<4)
#define S3C2410_RTCALM_DAYEN  (1<<3)
#define S3C2410_RTCALM_HOUREN (1<<2)
#define S3C2410_RTCALM_MINEN  (1<<1)
#define S3C2410_RTCALM_SECEN  (1<<0)

#define S3C2410_RTCALM_ALL \
  S3C2410_RTCALM_ALMEN | S3C2410_RTCALM_YEAREN | S3C2410_RTCALM_MONEN |\
  S3C2410_RTCALM_DAYEN | S3C2410_RTCALM_HOUREN | S3C2410_RTCALM_MINEN |\
  S3C2410_RTCALM_SECEN


#define S3C2410_ALMSEC	      S3C2410_RTCREG(0x54)
#define S3C2410_ALMMIN	      S3C2410_RTCREG(0x58)
#define S3C2410_ALMHOUR	      S3C2410_RTCREG(0x5c)

#define S3C2410_ALMDATE	      S3C2410_RTCREG(0x60)
#define S3C2410_ALMMON	      S3C2410_RTCREG(0x64)
#define S3C2410_ALMYEAR	      S3C2410_RTCREG(0x68)

#define S3C2410_RTCRST	      S3C2410_RTCREG(0x6c)

#define S3C2410_RTCSEC	      S3C2410_RTCREG(0x70)
#define S3C2410_RTCMIN	      S3C2410_RTCREG(0x74)
#define S3C2410_RTCHOUR	      S3C2410_RTCREG(0x78)
#define S3C2410_RTCDATE	      S3C2410_RTCREG(0x7c)
#define S3C2410_RTCDAY	      S3C2410_RTCREG(0x80)
#define S3C2410_RTCMON	      S3C2410_RTCREG(0x84)
#define S3C2410_RTCYEAR	      S3C2410_RTCREG(0x88)


#endif /* __ASM_ARCH_REGS_RTC_H */
