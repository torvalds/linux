/*-
 * Copyright (c) 2015 Oleksandr Tymoshenko <gonzo@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	IMX_IOMUXREG_H
#define	IMX_IOMUXREG_H

#define	IMX_IOMUXREG_LOWEST_SET_BIT(__mask) ((((__mask) - 1) & (__mask)) ^ (__mask))
#define	IMX_IOMUXREG_SHIFTIN(__x, __mask) ((__x) * IMX_IOMUXREG_LOWEST_SET_BIT(__mask))

#define IMX_IOMUXREG_BIT(n) (1 << (n))
#define	IMX_IOMUXREG_BITS(__m, __n)	\
	((IMX_IOMUXREG_BIT(MAX((__m), (__n)) + 1) - 1) ^ (IMX_IOMUXREG_BIT(MIN((__m), (__n))) - 1))

#define	IOMUXC_GPR0			0x00
#define	IOMUXC_GPR1			0x04
#define	IOMUXC_GPR2			0x08
#define	IOMUXC_GPR3			0x0C
#define	  IOMUXC_GPR3_HDMI_MASK		  (3 << 2)
#define	  IOMUXC_GPR3_HDMI_IPU1_DI0	  (0 << 2)
#define	  IOMUXC_GPR3_HDMI_IPU1_DI1	  (1 << 2)
#define	  IOMUXC_GPR3_HDMI_IPU2_DI0	  (2 << 2)
#define	  IOMUXC_GPR3_HDMI_IPU2_DI1	  (3 << 2)

#define IOMUX_GPR13			0x34
#define	  IOMUX_GPR13_SATA_PHY_8(n)	  IMX_IOMUXREG_SHIFTIN(n, IMX_IOMUXREG_BITS(26, 24))
#define	  IOMUX_GPR13_SATA_PHY_7(n)	  IMX_IOMUXREG_SHIFTIN(n, IMX_IOMUXREG_BITS(23, 19))
#define	  IOMUX_GPR13_SATA_PHY_6(n)	  IMX_IOMUXREG_SHIFTIN(n, IMX_IOMUXREG_BITS(18, 16))
#define	  IOMUX_GPR13_SATA_SPEED(n)	  IMX_IOMUXREG_SHIFTIN(n, (1 << 15))
#define	  IOMUX_GPR13_SATA_PHY_5(n)	  IMX_IOMUXREG_SHIFTIN(n, (1 << 14))
#define	  IOMUX_GPR13_SATA_PHY_4(n)	  IMX_IOMUXREG_SHIFTIN(n, IMX_IOMUXREG_BITS(13, 11))
#define	  IOMUX_GPR13_SATA_PHY_3(n)	  IMX_IOMUXREG_SHIFTIN(n, IMX_IOMUXREG_BITS(10, 7))
#define	  IOMUX_GPR13_SATA_PHY_2(n)	  IMX_IOMUXREG_SHIFTIN(n, IMX_IOMUXREG_BITS(6, 2))
#define	  IOMUX_GPR13_SATA_PHY_1(n)	  IMX_IOMUXREG_SHIFTIN(n, (1 << 1))
#define	  IOMUX_GPR13_SATA_PHY_0(n)	  IMX_IOMUXREG_SHIFTIN(n, (1 << 0))

#endif
