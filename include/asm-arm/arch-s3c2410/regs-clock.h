/* linux/include/asm/arch-s3c2410/regs-clock.h
 *
 * Copyright (c) 2003,2004,2005 Simtec Electronics <linux@simtec.co.uk>
 *		      http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2410 clock register definitions
 *
 *  Changelog:
 *    18-Aug-2004 Ben Dooks         Added 2440 definitions
 *    08-Aug-2004 Herbert Pötzl     Added CLKCON definitions
 *    19-06-2003  Ben Dooks         Created file
 *    12-03-2004  Ben Dooks         Updated include protection
 *    29-Sep-2004 Ben Dooks	    Fixed usage for assembly inclusion
 *    10-Feb-2005 Ben Dooks	    Fixed CAMDIVN address (Guillaume Gourat)
 *    10-Mar-2005 Lucas Villa Real  Changed S3C2410_VA to S3C24XX_VA
 *    27-Aug-2005 Ben Dooks	    Add clock-slow info
 *    20-Oct-2005 Ben Dooks	    Fixed overflow in PLL (Guillaume Gourat)
 *    20-Oct-2005 Ben Dooks	    Add masks for DCLK (Guillaume Gourat)
*/

#ifndef __ASM_ARM_REGS_CLOCK
#define __ASM_ARM_REGS_CLOCK "$Id: clock.h,v 1.4 2003/04/30 14:50:51 ben Exp $"

#define S3C2410_CLKREG(x) ((x) + S3C24XX_VA_CLKPWR)

#define S3C2410_PLLVAL(_m,_p,_s) ((_m) << 12 | ((_p) << 4) | ((_s)))

#define S3C2410_LOCKTIME    S3C2410_CLKREG(0x00)
#define S3C2410_MPLLCON	    S3C2410_CLKREG(0x04)
#define S3C2410_UPLLCON	    S3C2410_CLKREG(0x08)
#define S3C2410_CLKCON	    S3C2410_CLKREG(0x0C)
#define S3C2410_CLKSLOW	    S3C2410_CLKREG(0x10)
#define S3C2410_CLKDIVN	    S3C2410_CLKREG(0x14)

#define S3C2410_CLKCON_IDLE	     (1<<2)
#define S3C2410_CLKCON_POWER	     (1<<3)
#define S3C2410_CLKCON_NAND	     (1<<4)
#define S3C2410_CLKCON_LCDC	     (1<<5)
#define S3C2410_CLKCON_USBH	     (1<<6)
#define S3C2410_CLKCON_USBD	     (1<<7)
#define S3C2410_CLKCON_PWMT	     (1<<8)
#define S3C2410_CLKCON_SDI	     (1<<9)
#define S3C2410_CLKCON_UART0	     (1<<10)
#define S3C2410_CLKCON_UART1	     (1<<11)
#define S3C2410_CLKCON_UART2	     (1<<12)
#define S3C2410_CLKCON_GPIO	     (1<<13)
#define S3C2410_CLKCON_RTC	     (1<<14)
#define S3C2410_CLKCON_ADC	     (1<<15)
#define S3C2410_CLKCON_IIC	     (1<<16)
#define S3C2410_CLKCON_IIS	     (1<<17)
#define S3C2410_CLKCON_SPI	     (1<<18)

#define S3C2410_PLLCON_MDIVSHIFT     12
#define S3C2410_PLLCON_PDIVSHIFT     4
#define S3C2410_PLLCON_SDIVSHIFT     0
#define S3C2410_PLLCON_MDIVMASK	     ((1<<(1+(19-12)))-1)
#define S3C2410_PLLCON_PDIVMASK	     ((1<<5)-1)
#define S3C2410_PLLCON_SDIVMASK	     3

/* DCLKCON register addresses in gpio.h */

#define S3C2410_DCLKCON_DCLK0EN	     (1<<0)
#define S3C2410_DCLKCON_DCLK0_PCLK   (0<<1)
#define S3C2410_DCLKCON_DCLK0_UCLK   (1<<1)
#define S3C2410_DCLKCON_DCLK0_DIV(x) (((x) - 1 )<<4)
#define S3C2410_DCLKCON_DCLK0_CMP(x) (((x) - 1 )<<8)
#define S3C2410_DCLKCON_DCLK0_DIV_MASK ((0xf)<<4)
#define S3C2410_DCLKCON_DCLK0_CMP_MASK ((0xf)<<8)

#define S3C2410_DCLKCON_DCLK1EN	     (1<<16)
#define S3C2410_DCLKCON_DCLK1_PCLK   (0<<17)
#define S3C2410_DCLKCON_DCLK1_UCLK   (1<<17)
#define S3C2410_DCLKCON_DCLK1_DIV(x) (((x) - 1) <<20)
#define S3C2410_DCLKCON_DCLK1_CMP(x) (((x) - 1) <<24)
#define S3C2410_DCLKCON_DCLK1_DIV_MASK ((0xf) <<20)
#define S3C2410_DCLKCON_DCLK1_CMP_MASK ((0xf) <<24)

#define S3C2410_CLKDIVN_PDIVN	     (1<<0)
#define S3C2410_CLKDIVN_HDIVN	     (1<<1)

#define S3C2410_CLKSLOW_UCLK_OFF	(1<<7)
#define S3C2410_CLKSLOW_MPLL_OFF	(1<<5)
#define S3C2410_CLKSLOW_SLOW		(1<<4)
#define S3C2410_CLKSLOW_SLOWVAL(x)	(x)
#define S3C2410_CLKSLOW_GET_SLOWVAL(x)	((x) & 7)

#ifndef __ASSEMBLY__

#include <asm/div64.h>

static inline unsigned int
s3c2410_get_pll(unsigned int pllval, unsigned int baseclk)
{
	unsigned int mdiv, pdiv, sdiv;
	uint64_t fvco;

	mdiv = pllval >> S3C2410_PLLCON_MDIVSHIFT;
	pdiv = pllval >> S3C2410_PLLCON_PDIVSHIFT;
	sdiv = pllval >> S3C2410_PLLCON_SDIVSHIFT;

	mdiv &= S3C2410_PLLCON_MDIVMASK;
	pdiv &= S3C2410_PLLCON_PDIVMASK;
	sdiv &= S3C2410_PLLCON_SDIVMASK;

	fvco = (uint64_t)baseclk * (mdiv + 8);
	do_div(fvco, (pdiv + 2) << sdiv);

	return (unsigned int)fvco;
}

#endif /* __ASSEMBLY__ */

#ifdef CONFIG_CPU_S3C2440

/* extra registers */
#define S3C2440_CAMDIVN	    S3C2410_CLKREG(0x18)

#define S3C2440_CLKCON_CAMERA        (1<<19)
#define S3C2440_CLKCON_AC97          (1<<20)

#define S3C2440_CLKDIVN_PDIVN	     (1<<0)
#define S3C2440_CLKDIVN_HDIVN_MASK   (3<<1)
#define S3C2440_CLKDIVN_HDIVN_1      (0<<1)
#define S3C2440_CLKDIVN_HDIVN_2      (1<<1)
#define S3C2440_CLKDIVN_HDIVN_4_8    (2<<1)
#define S3C2440_CLKDIVN_HDIVN_3_6    (3<<1)
#define S3C2440_CLKDIVN_UCLK         (1<<3)

#define S3C2440_CAMDIVN_CAMCLK_MASK  (0xf<<0)
#define S3C2440_CAMDIVN_CAMCLK_SEL   (1<<4)
#define S3C2440_CAMDIVN_HCLK3_HALF   (1<<8)
#define S3C2440_CAMDIVN_HCLK4_HALF   (1<<9)
#define S3C2440_CAMDIVN_DVSEN        (1<<12)

#endif /* CONFIG_CPU_S3C2440 */


#endif /* __ASM_ARM_REGS_CLOCK */
