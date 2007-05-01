/*
 * include/asm-arm/arch-at91/at91_lcdc.h
 *
 * LCD Controller (LCDC).
 * Based on AT91SAM9261 datasheet revision E.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91_LCDC_H
#define AT91_LCDC_H

#define AT91_LCDC_DMABADDR1	0x00		/* DMA Base Address Register 1 */
#define AT91_LCDC_DMABADDR2	0x04		/* DMA Base Address Register 2 */
#define AT91_LCDC_DMAFRMPT1	0x08		/* DMA Frame Pointer Register 1 */
#define AT91_LCDC_DMAFRMPT2	0x0c		/* DMA Frame Pointer Register 2 */
#define AT91_LCDC_DMAFRMADD1	0x10		/* DMA Frame Address Register 1 */
#define AT91_LCDC_DMAFRMADD2	0x14		/* DMA Frame Address Register 2 */

#define AT91_LCDC_DMAFRMCFG	0x18		/* DMA Frame Configuration Register */
#define		AT91_LCDC_FRSIZE	(0x7fffff <<  0)	/* Frame Size */
#define		AT91_LCDC_BLENGTH	(0x7f     << 24)	/* Burst Length */

#define AT91_LCDC_DMACON	0x1c		/* DMA Control Register */
#define		AT91_LCDC_DMAEN		(0x1 << 0)	/* DMA Enable */
#define		AT91_LCDC_DMARST	(0x1 << 1)	/* DMA Reset */
#define		AT91_LCDC_DMABUSY	(0x1 << 2)	/* DMA Busy */

#define AT91_LCDC_LCDCON1	0x0800		/* LCD Control Register 1 */
#define		AT91_LCDC_BYPASS	(1     <<  0)	/* Bypass lcd_dotck divider */
#define		AT91_LCDC_CLKVAL	(0x1ff << 12)	/* Clock Divider */
#define		AT91_LCDC_LINCNT	(0x7ff << 21)	/* Line Counter */

#define AT91_LCDC_LCDCON2	0x0804		/* LCD Control Register 2 */
#define		AT91_LCDC_DISTYPE	(3 << 0)	/* Display Type */
#define			AT91_LCDC_DISTYPE_STNMONO	(0 << 0)
#define			AT91_LCDC_DISTYPE_STNCOLOR	(1 << 0)
#define			AT91_LCDC_DISTYPE_TFT		(2 << 0)
#define		AT91_LCDC_SCANMOD	(1 << 2)	/* Scan Mode */
#define			AT91_LCDC_SCANMOD_SINGLE	(0 << 2)
#define			AT91_LCDC_SCANMOD_DUAL		(1 << 2)
#define		AT91_LCDC_IFWIDTH	(3 << 3)	/*Interface Width */
#define			AT91_LCDC_IFWIDTH_4		(0 << 3)
#define			AT91_LCDC_IFWIDTH_8		(1 << 3)
#define			AT91_LCDC_IFWIDTH_16		(2 << 3)
#define		AT91_LCDC_PIXELSIZE	(7 << 5)	/* Bits per pixel */
#define			AT91_LCDC_PIXELSIZE_1		(0 << 5)
#define			AT91_LCDC_PIXELSIZE_2		(1 << 5)
#define			AT91_LCDC_PIXELSIZE_4		(2 << 5)
#define			AT91_LCDC_PIXELSIZE_8		(3 << 5)
#define			AT91_LCDC_PIXELSIZE_16		(4 << 5)
#define			AT91_LCDC_PIXELSIZE_24		(5 << 5)
#define		AT91_LCDC_INVVD		(1 << 8)	/* LCD Data polarity */
#define			AT91_LCDC_INVVD_NORMAL		(0 << 8)
#define			AT91_LCDC_INVVD_INVERTED	(1 << 8)
#define		AT91_LCDC_INVFRAME	(1 << 9 )	/* LCD VSync polarity */
#define			AT91_LCDC_INVFRAME_NORMAL	(0 << 9)
#define			AT91_LCDC_INVFRAME_INVERTED	(1 << 9)
#define		AT91_LCDC_INVLINE	(1 << 10)	/* LCD HSync polarity */
#define			AT91_LCDC_INVLINE_NORMAL	(0 << 10)
#define			AT91_LCDC_INVLINE_INVERTED	(1 << 10)
#define		AT91_LCDC_INVCLK	(1 << 11)	/* LCD dotclk polarity */
#define			AT91_LCDC_INVCLK_NORMAL		(0 << 11)
#define			AT91_LCDC_INVCLK_INVERTED	(1 << 11)
#define		AT91_LCDC_INVDVAL	(1 << 12)	/* LCD dval polarity */
#define			AT91_LCDC_INVDVAL_NORMAL	(0 << 12)
#define			AT91_LCDC_INVDVAL_INVERTED	(1 << 12)
#define		AT91_LCDC_CLKMOD	(1 << 15)	/* LCD dotclk mode */
#define			AT91_LCDC_CLKMOD_ACTIVEDISPLAY	(0 << 15)
#define			AT91_LCDC_CLKMOD_ALWAYSACTIVE	(1 << 15)
#define		AT91_LCDC_MEMOR		(1 << 31)	/* Memory Ordering Format */
#define			AT91_LCDC_MEMOR_BIG		(0 << 31)
#define			AT91_LCDC_MEMOR_LITTLE		(1 << 31)

#define AT91_LCDC_TIM1		0x0808		/* LCD Timing Register 1 */
#define		AT91_LCDC_VFP		(0xff <<  0)	/* Vertical Front Porch */
#define		AT91_LCDC_VBP		(0xff <<  8)	/* Vertical Back Porch */
#define		AT91_LCDC_VPW		(0x3f << 16)	/* Vertical Synchronization Pulse Width */
#define		AT91_LCDC_VHDLY		(0xf  << 24)	/* Vertical to Horizontal Delay */

#define AT91_LCDC_TIM2		0x080c		/* LCD Timing Register 2 */
#define		AT91_LCDC_HBP		(0xff  <<  0)	/* Horizontal Back Porch */
#define		AT91_LCDC_HPW		(0x3f  <<  8)	/* Horizontal Synchronization Pulse Width */
#define		AT91_LCDC_HFP		(0x7ff << 21)	/* Horizontal Front Porch */

#define AT91_LCDC_LCDFRMCFG	0x0810		/* LCD Frame Configuration Register */
#define		AT91_LCDC_LINEVAL	(0x7ff <<  0)	/* Vertical Size of LCD Module */
#define		AT91_LCDC_HOZVAL	(0x7ff << 21)	/* Horizontal Size of LCD Module */

#define AT91_LCDC_FIFO		0x0814		/* LCD FIFO Register */
#define		AT91_LCDC_FIFOTH	(0xffff)	/* FIFO Threshold */

#define AT91_LCDC_DP1_2		0x081c		/* Dithering Pattern DP1_2 Register */
#define AT91_LCDC_DP4_7		0x0820		/* Dithering Pattern DP4_7 Register */
#define AT91_LCDC_DP3_5		0x0824		/* Dithering Pattern DP3_5 Register */
#define AT91_LCDC_DP2_3		0x0828		/* Dithering Pattern DP2_3 Register */
#define AT91_LCDC_DP5_7		0x082c		/* Dithering Pattern DP5_7 Register */
#define AT91_LCDC_DP3_4		0x0830		/* Dithering Pattern DP3_4 Register */
#define AT91_LCDC_DP4_5		0x0834		/* Dithering Pattern DP4_5 Register */
#define AT91_LCDC_DP6_7		0x0838		/* Dithering Pattern DP6_7 Register */
#define		AT91_LCDC_DP1_2_VAL	(0xff)
#define		AT91_LCDC_DP4_7_VAL	(0xfffffff)
#define		AT91_LCDC_DP3_5_VAL	(0xfffff)
#define		AT91_LCDC_DP2_3_VAL	(0xfff)
#define		AT91_LCDC_DP5_7_VAL	(0xfffffff)
#define		AT91_LCDC_DP3_4_VAL	(0xffff)
#define		AT91_LCDC_DP4_5_VAL	(0xfffff)
#define		AT91_LCDC_DP6_7_VAL	(0xfffffff)

#define AT91_LCDC_PWRCON	0x083c		/* Power Control Register */
#define		AT91_LCDC_PWR		(1    <<  0)	/* LCD Module Power Control */
#define		AT91_LCDC_GUARDT	(0x7f <<  1)	/* Delay in Frame Period */
#define		AT91_LCDC_BUSY		(1    << 31)	/* LCD Busy */

#define AT91_LCDC_CONTRAST_CTR	0x0840		/* Contrast Control Register */
#define		AT91_LCDC_PS		(3 << 0)	/* Contrast Counter Prescaler */
#define			AT91_LCDC_PS_DIV1		(0 << 0)
#define			AT91_LCDC_PS_DIV2		(1 << 0)
#define			AT91_LCDC_PS_DIV4		(2 << 0)
#define			AT91_LCDC_PS_DIV8		(3 << 0)
#define		AT91_LCDC_POL		(1 << 2)	/* Polarity of output Pulse */
#define			AT91_LCDC_POL_NEGATIVE		(0 << 2)
#define			AT91_LCDC_POL_POSITIVE		(1 << 2)
#define		AT91_LCDC_ENA		(1 << 3)	/* PWM generator Control */
#define			AT91_LCDC_ENA_PWMDISABLE	(0 << 3)
#define			AT91_LCDC_ENA_PWMENABLE		(1 << 3)

#define AT91_LCDC_CONTRAST_VAL	0x0844		/* Contrast Value Register */
#define		AT91_LCDC_CVAL		(0xff)		/* PWM compare value */

#define AT91_LCDC_IER		0x0848		/* Interrupt Enable Register */
#define AT91_LCDC_IDR		0x084c		/* Interrupt Disable Register */
#define AT91_LCDC_IMR		0x0850		/* Interrupt Mask Register */
#define AT91_LCDC_ISR		0x0854		/* Interrupt Enable Register */
#define AT91_LCDC_ICR		0x0858		/* Interrupt Clear Register */
#define		AT91_LCDC_LNI		(1 << 0)	/* Line Interrupt */
#define		AT91_LCDC_LSTLNI	(1 << 1)	/* Last Line Interrupt */
#define		AT91_LCDC_EOFI		(1 << 2)	/* DMA End Of Frame Interrupt */
#define		AT91_LCDC_UFLWI		(1 << 4)	/* FIFO Underflow Interrupt */
#define		AT91_LCDC_OWRI		(1 << 5)	/* FIFO Overwrite Interrupt */
#define		AT91_LCDC_MERI		(1 << 6)	/* DMA Memory Error Interrupt */

#define AT91_LCDC_LUT_(n)	(0x0c00 + ((n)*4))	/* Palette Entry 0..255 */

#endif
