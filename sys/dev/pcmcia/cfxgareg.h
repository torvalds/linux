/*	$OpenBSD: cfxgareg.h,v 1.5 2006/11/28 12:01:27 miod Exp $	*/

/*
 * Copyright (c) 2005, 2006, Matthieu Herrb and Miodrag Vallat
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * S1D13806 Registers.
 * Registers larger than 8 bits are little-endian.
 */

/* Revision code register - RO */
#define	CFREG_REV			0x0000
#define	CR_REV_MASK		0x03	/* revision code */
#define	CR_REV_SHIFT		0
#define	CR_PRODUCT_MASK		0xfc	/* product code */
#define	CR_PRODUCT_SHIFT	2
#define	PRODUCT_S1D13806	0x07

/* Miscellaneous register - RW */
#define	CFREG_MISC			0x0001
#define	CM_MEMSEL		0x00
#define	CM_REGSEL		0x80	/* register/memory select */

/* General IO pins configuration register - RW, 12 bits */
#define	CFREG_GPIO_CONF			0x0004

/* General IO pins control register - RW, 12 bits */
#define	CFREG_GPIO_CTRL			0x0008

/* Configuration status register - RO */
#define	CFREG_STATUS			0x000c

/* Memory clock configuration register - RW, needs 16 bits access */
#define	CFREG_MEMCLK			0x0010
#define	MEMCLK_DIVIDE		0x10
#define	MEMCLK_SRC_CLKI		0x00
#define	MEMCLK_SRC_BUSCLK	0x01
#define	MEMCLK_SRC_CLK3		0x02

/* LCD Pixel clock configuration register - RW */
#define	CFREG_LCD_PCLK			0x0014
#define	LCD_PCLK_SRC_CLKI	0x00
#define	LCD_PCLK_SRC_BUSCLK	0x01
#define	LCD_PCLK_SRC_CLKI2	0x02
#define	LCD_PCLK_SRC_MCLK	0x03
#define	LCD_PCLK_DIV_1		0x00
#define	LCD_PCLK_DIV_2		0x10
#define	LCD_PCLK_DIV_3		0x20
#define	LCD_PCLK_DIV_4		0x30

/* CRT/TV Pixel clock configuration register - RW */
#define	CFREG_CRTTV_PCLK		0x0018
#define	CRT_PCLK_SRC_CLKI	0x00
#define	CRT_PCLK_SRC_BUSCLK	0x01
#define	CRT_PCLK_SRC_CLKI2	0x02
#define	CRT_PCLK_SRC_MCLK	0x03
#define	CRT_PCLK_DIV_1		0x00
#define	CRT_PCLK_DIV_2		0x10
#define	CRT_PCLK_DIV_3		0x20
#define	CRT_PCLK_DIV_4		0x30
#define	CRT_PCLK_DOUBLE		0x80

/* MediaPlug clock configuration register - RW */
#define	CFREG_MPLUG_CLK			0x001c
#define	MPLUG_PCLK_SRC_CLKI	0x00
#define	MPLUG_PCLK_SRC_BUSCLK	0x01
#define	MPLUG_PCLK_SRC_CLKI2	0x02
#define	MPLUG_PCLK_SRC_MCLK	0x03
#define	MPLUG_PCLK_DIV_1	0x00
#define	MPLUG_PCLK_DIV_2	0x10
#define	MPLUG_PCLK_DIV_3	0x20
#define	MPLUG_PCLK_DIV_4	0x30

/* CPU to memory wait state select register - RW */
#define	CFREG_WSTATE			0x001e
#define	WSTATE_NONE		0x00
#define	WSTATE_DUAL_MCLK	0x01
#define	WSTATE_MCLK		0x02

/* Memory configuration register - RW */
#define	CFREG_MEMCNF			0x0020
#define	MEMCNF_SDRAM_INIT	0x80

/* DRAM refresh rate register - RW */
#define	CFREG_DRAM_RFRSH		0x0021
#define	DRAM_RFRSH_8MHZ		0x00
#define	DRAM_RFRSH_16MHZ	0x01
#define	DRAM_RFRSH_32MHZ	0x02
#define	DRAM_RFRSH_50MHZ	0x03

/* DRAM timing control register - RW, 10 bits */
#define	CFREG_DRAM_TIMING		0x002a
#define	DRAM_TIMING_33MHZ	0x0311
#define	DRAM_TIMING_44MHZ	0x0200
#define	DRAM_TIMING_50MHZ	0x0100

/* Panel type register - RW */
#define	CFREG_PANEL			0x0030
#define	PANEL_PASSIVE		0x00
#define	PANEL_TFT		0x01
#define	PANEL_SINGLE		0x00
#define	PANEL_DUAL		0x02
#define	PANEL_MONO		0x00
#define	PANEL_COLOR		0x04
#define	PANEL_FORMAT_1X		0x00
#define	PANEL_FORMAT_2X		0x08
#define	PANEL_WIDTH_4_9		0x00	/* passive: 4 bits, TDT: 9/2x9 bits */
#define	PANEL_WIDTH_8_12	0x10	/* passive: 8 bits, TDT: 12/2x12 bits */
#define	PANEL_WIDTH_16_18	0x20	/* passive: 16 bits, TDT: 18 bits */

/* MOD rate register - RW */
#define	CFREG_MODRATE			0x0031

/* LCD horizontal display width register - RW */
#define	CFREG_LCD_HWIDTH		0x0032

/* LCD horizontal non-display period register - RW */
#define	CFREG_LCD_HNDISP		0x0034

/* TFT FPLINE start position register - RW */
#define	CFREG_TFT_FPLINE_START		0x0035

/* TFT FPLINE pulse width register - RW */
#define	CFREG_TFT_FPLINE_WIDTH		0x0036
#define	TFT_FPLINE_POL_TFT_LOW		0x00
#define	TFT_FPLINE_POL_TFT_HIGH		0x80
#define	TFT_FPLINE_POL_PASSIVE_LOW	0x80
#define	TFT_FPLINE_POL_PASSIVE_HIGH	0x00

/* LCD vertical display height - RW, 10 bits */
#define	CFREG_LCD_VHEIGHT		0x0038

/* LCD vertical non-display period register - RW */
#define	CFREG_LCD_VNDISP		0x003a
#define	LCD_VNDISP_STATUS	0x80			/* read only */

/* TFT FPFRAME start position register - RW */
#define	CFREG_TFT_FPFRAME_START		0x003b

/* TFT FPFRAME pulse width register - RW */
#define	CFREG_TFT_FPFRAME_WIDTH		0x003c
#define	TFT_FPFRAME_POL_TFT_LOW		0x00
#define	TFT_FPFRAME_POL_TFT_HIGH	0x80
#define	TFT_FPFRAME_POL_PASSIVE_LOW	0x80
#define	TFT_FPFRAME_POL_PASSIVE_HIGH	0x00

/* LCD line count register - RO */
#define	CFREG_LCD_LINECNT		0x003e

/* LCD display mode register - RW */
#define	CFREG_LCD_MODE			0x0040
#define	LCD_MODE_4BPP		0x02
#define	LCD_MODE_8BPP		0x03
#define	LCD_MODE_15BPP		0x04
#define	LCD_MODE_16BPP		0x05
#define	LCD_MODE_SWIVEL_BIT1	0x10
#define	LCD_MODE_BLANK		0x80

/* LCD miscellaneous register - RW */
#define	CFREG_LCD_MISC			0x0041
#define	LCD_MISC_DUAL_PANEL_BUFFER_DISABLE	0x01
#define	LCD_MISC_DITHERING_DISABLE		0x02

/* LCD display start address - RW, 20 bits */
#define	CFREG_LCD_START_LOW		0x0042
#define	CFREG_LCD_START_HIGH		0x0044

/* LCD memory address register - RW, 11 bits */
#define	CFREG_LCD_MEMORY		0x0046

/* LCD pixel panning register - RW */
#define	CFREG_LCD_PANNING		0x0048
#define	PIXEL_PANNING_MASK_4BPP			0x03
#define	PIXEL_PANNING_MASK_8BPP			0x01
#define	PIXEL_PANNING_MASK_15BPP		0x00
#define	PIXEL_PANNING_MASK_16BPP		0x00

/* LCD display FIFO high threshold control register - RW */
#define	CFREG_LCD_FIFO_THRESHOLD_HIGH	0x004a

/* LCD display FIFO low threshold control register - RW */
#define	CFREG_LCD_FIFO_THRESHOLD_LOW	0x004b

/* CRT/TV horizontal display width register - RW */
#define	CFREG_CRT_HWIDTH		0x0050

/* CRT/TV horizontal non-display period register - RW */
#define	CFREG_CRT_HNDISP		0x0052

/* CRT/TV HRTC start position register - RW */
#define	CFREG_CRT_HSTART		0x0053

/* CRT/TV HRTC pulse width register - RW */
#define	CFREG_CRT_HPULSE		0x0054
#define	HRTC_POLARITY		0x80

/* CRT/TV vertical display height register - RW, 10 bits */
#define	CFREG_CRT_VHEIGHT		0x0056

/* CRT/TV vertical non-display period register - RW */
#define	CFREG_CRT_VNDISP		0x0058
#define	CRT_VNDISP_STATUS	0x80			/* RO */

/* CRT/TV VRTC start position register - RW */
#define	CFREG_CRT_VSTART		0x0059

/* CRT VRTC pulse width register - RW */
#define	CFREG_CRT_VPULSE		0x005a

/* TV output control register - RW */
#define	CFREG_TV_CONTROL		0x005b
#define	TV_NTSC_OUTPUT		0x00
#define	TV_PAL_OUTPUT		0x01
#define	TV_COMPOSITE_OUTPUT	0x00
#define	TV_SVIDEO_OUTPUT	0x02
#define	TV_DAC_OUTPUT_HIGH	0x00	/* 9.2 mA IREF */
#define	TV_DAC_OUTPUT_LOW	0x08	/* 4.6 mA IREF - CRT only */
#define	TV_LUMINANCE_FILTER	0x10
#define	TV_CHROMINANCE_FILTER	0x20

/* CRT/TV line count register - RW */
#define	CFREG_CRT_LINECNT		0x005e

/* CRT/TV display mode register - RW */
#define	CFREG_CRT_MODE			0x0060
#define	CRT_MODE_4BPP		0x02
#define	CRT_MODE_8BPP		0x03
#define	CRT_MODE_15BPP		0x04
#define	CRT_MODE_16BPP		0x05
#define	CRT_MODE_BLANK		0x80

/* CRT/TV display start address - RW, 20 bits */
#define	CFREG_CRT_START_LOW		0x0062
#define	CFREG_CRT_START_HIGH		0x0064

/* CRT/TV memory address register - RW, 11 bits */
#define	CFREG_CRT_MEMORY		0x0066

/* CRT/TV pixel panning register - RW */
#define	CFREG_CRT_PANNING		0x0068

/* CRT/TV display FIFO high threshold control register - RW */
#define	CFREG_CRT_FIFO_THRESHOLD_HIGH	0x006a

/* CRT/TV display FIFO low threshold control register - RW */
#define	CFREG_CRT_FIFO_THRESHOLD_LOW	0x006b

/* LCD ink/cursor control register - RW */
#define	CFREG_LCD_CURSOR_CONTROL	0x0070
#define	CURSOR_INACTIVE		0x00
#define	CURSOR_CURSOR		0x01
#define	CURSOR_INK		0x02

/* LCD ink/cursor start address register - RW */
#define	CFREG_LCD_CURSOR_ADDRESS	0x0071

/* LCD cursor X position register - RW, 10 bits + sign */
#define	CFREG_LCD_CURSOR_X		0x0072
#define	LCD_CURSOR_X_SIGN	0x8000

/* LCD cursor Y position register - RW, 10 bits + sign */
#define	CFREG_LCD_CURSOR_Y		0x0074
#define	LCD_CURSOR_Y_SIGN	0x8000

/* LCD ink/cursor color registers - RW */
#define	CFREG_LCD_CURSOR_B0		0x0076	/* 5 bits */
#define	CFREG_LCD_CURSOR_G0		0x0077	/* 6 bits */
#define	CFREG_LCD_CURSOR_R0		0x0078	/* 5 bits */
#define	CFREG_LCD_CURSOR_B1		0x007a	/* 5 bits */
#define	CFREG_LCD_CURSOR_G1		0x007b	/* 6 bits */
#define	CFREG_LCD_CURSOR_R1		0x007c	/* 5 bits */

/* LCD ink/cursor FIFO threshold register - RW */
#define	CFREG_LCD_CURSOR_FIFO		0x007e

/* CRT/TV ink/cursor control register - RW */
#define	CFREG_CRT_CURSOR_CONTROL	0x0080

/* CRT/TV ink/cursor start address register - RW */
#define	CFREG_CRT_CURSOR_ADDRESS	0x0081

/* CRT/TV cursor X position register - RW, 10 bits + sign */
#define	CFREG_CRT_CURSOR_X		0x0082
#define	CRT_CURSOR_X_SIGN	0x8000

/* CRT/TV cursor Y position register - RW, 10 bits + sign */
#define	CFREG_CRT_CURSOR_Y		0x0084
#define	CRT_CURSOR_Y_SIGN	0x8000

/* CRT/TV ink/cursor color registers - RW */
#define	CFREG_CRT_CURSOR_B0		0x0086	/* 5 bits */
#define	CFREG_CRT_CURSOR_G0		0x0087	/* 6 bits */
#define	CFREG_CRT_CURSOR_R0		0x0088	/* 5 bits */
#define	CFREG_CRT_CURSOR_B1		0x008a	/* 5 bits */
#define	CFREG_CRT_CURSOR_G1		0x008b	/* 6 bits */
#define	CFREG_CRT_CURSOR_R1		0x008c	/* 5 bits */

/* CRT/TV ink/cursor FIFO threshold register - RW */
#define	CFREG_CRT_CURSOR_FIFO		0x008e

/* Bitblt control register - RW, 16 bits */
#define	CFREG_BITBLT_CONTROL		0x0100
#define	BITBLT_SRC_LINEAR	0x0001
#define	BITBLT_DST_LINEAR	0x0002
#define	BITBLT_FIFO_FULL	0x0010			/* RO */
#define	BITBLT_FIFO_HALF_FULL	0x0020			/* RO */
#define	BITBLT_FIFO_NOT_EMPTY	0x0040			/* RO */
#define	BITBLT_ACTIVE		0x0080
#define	BITBLT_COLOR_8		0x0000
#define	BITBLT_COLOR_16		0x0100

/* Bitblt ROP code / color expansion register - RW */
#define	CFREG_BITBLT_ROP		0x0102
#define	CFREG_COLOR_EXPANSION		0x0102
#define	ROP_ZERO		0x00
#define	ROP_DST			0x0a
#define	ROP_SRC			0x0c
#define	ROP_ONES		0x0f

/* Bitblt operation register - RW */
#define	CFREG_BITBLT_OPERATION		0x103
#define	OP_WRITE_ROP				0x00
#define	OP_READ					0x01
#define	OP_MOVE_POSITIVE_ROP			0x02
#define	OP_MOVE_NEGATIVE_ROP			0x03
#define	OP_TRANSPARENT_WRITE			0x04
#define	OP_TRANSPARENT_MOVE_POSITIVE		0x05
#define	OP_PATTERN_FILL_ROP			0x06
#define	OP_PATTERN_FILL_TRANSPARENCY		0x07
#define	OP_COLOR_EXPANSION			0x08
#define	OP_COLOR_EXPANSION_TRANSPARENCY		0x09
#define	OP_MOVE_COLOR_EXPANSION			0x0a
#define	OP_MOVE_COLOR_EXPANSION_TRANSPARENCY	0x0b
#define	OP_SOLID_FILL				0x0c

/* Bitblt source address register - RW, 21 bits */
#define	CFREG_BITBLT_SRC_LOW		0x104
#define	CFREG_BITBLT_SRC_HIGH		0x106

/* Bitblt destination start address register - RW, 21 bits */
#define	CFREG_BITBLT_DST_LOW		0x108
#define	CFREG_BITBLT_DST_HIGH		0x10a

/* Bitblt memory address offset register - RW, 11 bits */
#define	CFREG_BITBLT_OFFSET		0x10c

/* Bitblt width register - RW, 10 bits */
#define	CFREG_BITBLT_WIDTH		0x110

/* Bitblt height register - RW, 10 bits */
#define	CFREG_BITBLT_HEIGHT		0x112

/* Bitblt color registers - RW, 16 bits */
#define	CFREG_BITBLT_BG			0x114
#define	CFREG_BITBLT_FG			0x118

/* Lookup table mode register - RW */
#define	CFREG_LUT_MODE			0x1e0
#define	LUT_BOTH		0x00	/* read LCD, write LCD and CRT/TV */
#define	LUT_LCD			0x01	/* read LCD, write LCD */
#define	LUT_CRT			0x02	/* read CRT/TV, write CRT/TV */

/* LUT address register - RW */
#define	CFREG_LUT_ADDRESS		0x1e2

/* LUT data register - RW */
#define	CFREG_LUT_DATA			0x1e4	/* data in the high 4 bits */

/* Power save configuration register - RW */
#define	CFREG_POWER_CONF		0x1f0
#define	POWERSAVE_ENABLE	0x01
#define	POWERSAVE_MBO		0x10

/* Power save status register - RW */
#define	CFREG_POWER_STATUS		0x1f1
#define	POWERSAVE_STATUS	0x01
#define	LCD_POWERSAVE_STATUS	0x02

/* CPU to memory access watchdog timer register - RW */
#define	CFREG_WATCHDOG			0x1f4

/* Display mode register - RW */
#define	CFREG_MODE			0x1fc
#define	MODE_NO_DISPLAY		0x00
#define	MODE_LCD		0x01	/* can be combined with all modes */
#define	MODE_CRT		0x02
#define	MODE_TV_NO_FLICKER	0x04
#define	MODE_TV_FLICKER		0x06
#define	LCD_MODE_SWIVEL_BIT_0	0x40

/* BitBlt aperture */
#define	CFREG_BITBLT_DATA		0x0400

#ifdef	_KERNEL
#define	CFXGA_MEM_RANGE		0x0800
#endif
