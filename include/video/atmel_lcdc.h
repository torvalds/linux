/*
 *  Header file for AT91/AT32 LCD Controller
 *
 *  Data structure and register user interface
 *
 *  Copyright (C) 2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ATMEL_LCDC_H__
#define __ATMEL_LCDC_H__

 /* LCD Controller info data structure */
struct atmel_lcdfb_info {
	spinlock_t		lock;
	struct fb_info		*info;
	void __iomem		*mmio;
	unsigned long		irq_base;

	unsigned int		guard_time;
	struct platform_device	*pdev;
	struct clk		*bus_clk;
	struct clk		*lcdc_clk;
	unsigned int		default_bpp;
	unsigned int		default_lcdcon2;
	unsigned int		default_dmacon;
	void (*atmel_lcdfb_power_control)(int on);
	struct fb_monspecs	*default_monspecs;
	u32			pseudo_palette[16];
};

#define ATMEL_LCDC_DMABADDR1	0x00
#define ATMEL_LCDC_DMABADDR2	0x04
#define ATMEL_LCDC_DMAFRMPT1	0x08
#define ATMEL_LCDC_DMAFRMPT2	0x0c
#define ATMEL_LCDC_DMAFRMADD1	0x10
#define ATMEL_LCDC_DMAFRMADD2	0x14

#define ATMEL_LCDC_DMAFRMCFG	0x18
#define	ATMEL_LCDC_FRSIZE	(0x7fffff <<  0)
#define	ATMEL_LCDC_BLENGTH_OFFSET	24
#define	ATMEL_LCDC_BLENGTH	(0x7f     << ATMEL_LCDC_BLENGTH_OFFSET)

#define ATMEL_LCDC_DMACON	0x1c
#define	ATMEL_LCDC_DMAEN	(0x1 << 0)
#define	ATMEL_LCDC_DMARST	(0x1 << 1)
#define	ATMEL_LCDC_DMABUSY	(0x1 << 2)
#define		ATMEL_LCDC_DMAUPDT	(0x1 << 3)
#define		ATMEL_LCDC_DMA2DEN	(0x1 << 4)

#define ATMEL_LCDC_DMA2DCFG	0x20
#define		ATMEL_LCDC_ADDRINC_OFFSET	0
#define		ATMEL_LCDC_ADDRINC		(0xffff)
#define		ATMEL_LCDC_PIXELOFF_OFFSET	24
#define		ATMEL_LCDC_PIXELOFF		(0x1f << 24)

#define ATMEL_LCDC_LCDCON1	0x0800
#define	ATMEL_LCDC_BYPASS	(1     <<  0)
#define	ATMEL_LCDC_CLKVAL_OFFSET	12
#define	ATMEL_LCDC_CLKVAL	(0x1ff << ATMEL_LCDC_CLKVAL_OFFSET)
#define	ATMEL_LCDC_LINCNT	(0x7ff << 21)

#define ATMEL_LCDC_LCDCON2	0x0804
#define	ATMEL_LCDC_DISTYPE	(3 << 0)
#define		ATMEL_LCDC_DISTYPE_STNMONO	(0 << 0)
#define		ATMEL_LCDC_DISTYPE_STNCOLOR	(1 << 0)
#define		ATMEL_LCDC_DISTYPE_TFT		(2 << 0)
#define	ATMEL_LCDC_SCANMOD	(1 << 2)
#define		ATMEL_LCDC_SCANMOD_SINGLE	(0 << 2)
#define		ATMEL_LCDC_SCANMOD_DUAL		(1 << 2)
#define	ATMEL_LCDC_IFWIDTH	(3 << 3)
#define		ATMEL_LCDC_IFWIDTH_4		(0 << 3)
#define		ATMEL_LCDC_IFWIDTH_8		(1 << 3)
#define		ATMEL_LCDC_IFWIDTH_16		(2 << 3)
#define	ATMEL_LCDC_PIXELSIZE	(7 << 5)
#define		ATMEL_LCDC_PIXELSIZE_1		(0 << 5)
#define		ATMEL_LCDC_PIXELSIZE_2		(1 << 5)
#define		ATMEL_LCDC_PIXELSIZE_4		(2 << 5)
#define		ATMEL_LCDC_PIXELSIZE_8		(3 << 5)
#define		ATMEL_LCDC_PIXELSIZE_16		(4 << 5)
#define		ATMEL_LCDC_PIXELSIZE_24		(5 << 5)
#define		ATMEL_LCDC_PIXELSIZE_32		(6 << 5)
#define	ATMEL_LCDC_INVVD	(1 << 8)
#define		ATMEL_LCDC_INVVD_NORMAL		(0 << 8)
#define		ATMEL_LCDC_INVVD_INVERTED	(1 << 8)
#define	ATMEL_LCDC_INVFRAME	(1 << 9 )
#define		ATMEL_LCDC_INVFRAME_NORMAL	(0 << 9)
#define		ATMEL_LCDC_INVFRAME_INVERTED	(1 << 9)
#define	ATMEL_LCDC_INVLINE	(1 << 10)
#define		ATMEL_LCDC_INVLINE_NORMAL	(0 << 10)
#define		ATMEL_LCDC_INVLINE_INVERTED	(1 << 10)
#define	ATMEL_LCDC_INVCLK	(1 << 11)
#define		ATMEL_LCDC_INVCLK_NORMAL	(0 << 11)
#define		ATMEL_LCDC_INVCLK_INVERTED	(1 << 11)
#define	ATMEL_LCDC_INVDVAL	(1 << 12)
#define		ATMEL_LCDC_INVDVAL_NORMAL	(0 << 12)
#define		ATMEL_LCDC_INVDVAL_INVERTED	(1 << 12)
#define	ATMEL_LCDC_CLKMOD	(1 << 15)
#define		ATMEL_LCDC_CLKMOD_ACTIVEDISPLAY	(0 << 15)
#define		ATMEL_LCDC_CLKMOD_ALWAYSACTIVE	(1 << 15)
#define	ATMEL_LCDC_MEMOR	(1 << 31)
#define		ATMEL_LCDC_MEMOR_BIG		(0 << 31)
#define		ATMEL_LCDC_MEMOR_LITTLE		(1 << 31)

#define ATMEL_LCDC_TIM1		0x0808
#define	ATMEL_LCDC_VFP		(0xff <<  0)
#define	ATMEL_LCDC_VBP_OFFSET		8
#define	ATMEL_LCDC_VBP		(0xff <<  ATMEL_LCDC_VBP_OFFSET)
#define	ATMEL_LCDC_VPW_OFFSET		16
#define	ATMEL_LCDC_VPW		(0x3f << ATMEL_LCDC_VPW_OFFSET)
#define	ATMEL_LCDC_VHDLY_OFFSET		24
#define	ATMEL_LCDC_VHDLY	(0xf  << ATMEL_LCDC_VHDLY_OFFSET)

#define ATMEL_LCDC_TIM2		0x080c
#define	ATMEL_LCDC_HBP		(0xff  <<  0)
#define	ATMEL_LCDC_HPW_OFFSET		8
#define	ATMEL_LCDC_HPW		(0x3f  <<  ATMEL_LCDC_HPW_OFFSET)
#define	ATMEL_LCDC_HFP_OFFSET		21
#define	ATMEL_LCDC_HFP		(0x7ff << ATMEL_LCDC_HFP_OFFSET)

#define ATMEL_LCDC_LCDFRMCFG	0x0810
#define	ATMEL_LCDC_LINEVAL	(0x7ff <<  0)
#define	ATMEL_LCDC_HOZVAL_OFFSET	21
#define	ATMEL_LCDC_HOZVAL	(0x7ff << ATMEL_LCDC_HOZVAL_OFFSET)

#define ATMEL_LCDC_FIFO		0x0814
#define	ATMEL_LCDC_FIFOTH	(0xffff)

#define ATMEL_LCDC_MVAL		0x0818

#define ATMEL_LCDC_DP1_2	0x081c
#define ATMEL_LCDC_DP4_7	0x0820
#define ATMEL_LCDC_DP3_5	0x0824
#define ATMEL_LCDC_DP2_3	0x0828
#define ATMEL_LCDC_DP5_7	0x082c
#define ATMEL_LCDC_DP3_4	0x0830
#define ATMEL_LCDC_DP4_5	0x0834
#define ATMEL_LCDC_DP6_7	0x0838
#define	ATMEL_LCDC_DP1_2_VAL	(0xff)
#define	ATMEL_LCDC_DP4_7_VAL	(0xfffffff)
#define	ATMEL_LCDC_DP3_5_VAL	(0xfffff)
#define	ATMEL_LCDC_DP2_3_VAL	(0xfff)
#define	ATMEL_LCDC_DP5_7_VAL	(0xfffffff)
#define	ATMEL_LCDC_DP3_4_VAL	(0xffff)
#define	ATMEL_LCDC_DP4_5_VAL	(0xfffff)
#define	ATMEL_LCDC_DP6_7_VAL	(0xfffffff)

#define ATMEL_LCDC_PWRCON	0x083c
#define	ATMEL_LCDC_PWR		(1    <<  0)
#define	ATMEL_LCDC_GUARDT_OFFSET	1
#define	ATMEL_LCDC_GUARDT	(0x7f <<  ATMEL_LCDC_GUARDT_OFFSET)
#define	ATMEL_LCDC_BUSY		(1    << 31)

#define ATMEL_LCDC_CONTRAST_CTR	0x0840
#define	ATMEL_LCDC_PS		(3 << 0)
#define		ATMEL_LCDC_PS_DIV1		(0 << 0)
#define		ATMEL_LCDC_PS_DIV2		(1 << 0)
#define		ATMEL_LCDC_PS_DIV4		(2 << 0)
#define		ATMEL_LCDC_PS_DIV8		(3 << 0)
#define	ATMEL_LCDC_POL		(1 << 2)
#define		ATMEL_LCDC_POL_NEGATIVE		(0 << 2)
#define		ATMEL_LCDC_POL_POSITIVE		(1 << 2)
#define	ATMEL_LCDC_ENA		(1 << 3)
#define		ATMEL_LCDC_ENA_PWMDISABLE	(0 << 3)
#define		ATMEL_LCDC_ENA_PWMENABLE	(1 << 3)

#define ATMEL_LCDC_CONTRAST_VAL	0x0844
#define	ATMEL_LCDC_CVAL	(0xff)

#define ATMEL_LCDC_IER		0x0848
#define ATMEL_LCDC_IDR		0x084c
#define ATMEL_LCDC_IMR		0x0850
#define ATMEL_LCDC_ISR		0x0854
#define ATMEL_LCDC_ICR		0x0858
#define	ATMEL_LCDC_LNI		(1 << 0)
#define	ATMEL_LCDC_LSTLNI	(1 << 1)
#define	ATMEL_LCDC_EOFI		(1 << 2)
#define	ATMEL_LCDC_UFLWI	(1 << 4)
#define	ATMEL_LCDC_OWRI		(1 << 5)
#define	ATMEL_LCDC_MERI		(1 << 6)

#define ATMEL_LCDC_LUT(n)	(0x0c00 + ((n)*4))

#endif /* __ATMEL_LCDC_H__ */
