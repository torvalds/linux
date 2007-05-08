/*
 *  linux/drivers/video/tgafb.h -- DEC 21030 TGA frame buffer device
 *
 *  	Copyright (C) 1999,2000 Martin Lucina, Tom Zerucha
 *  
 *  $Id: tgafb.h,v 1.4.2.3 2000/04/04 06:44:56 mato Exp $
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#ifndef TGAFB_H
#define TGAFB_H

/*
 * TGA hardware description (minimal)
 */

#define TGA_TYPE_8PLANE			0
#define TGA_TYPE_24PLANE		1
#define TGA_TYPE_24PLUSZ		3

/*
 * Offsets within Memory Space
 */

#define	TGA_ROM_OFFSET			0x0000000
#define	TGA_REGS_OFFSET			0x0100000
#define	TGA_8PLANE_FB_OFFSET		0x0200000
#define	TGA_24PLANE_FB_OFFSET		0x0804000
#define	TGA_24PLUSZ_FB_OFFSET		0x1004000

#define TGA_FOREGROUND_REG		0x0020
#define TGA_BACKGROUND_REG		0x0024
#define	TGA_PLANEMASK_REG		0x0028
#define TGA_PIXELMASK_ONESHOT_REG	0x002c
#define	TGA_MODE_REG			0x0030
#define	TGA_RASTEROP_REG		0x0034
#define	TGA_PIXELSHIFT_REG		0x0038
#define	TGA_DEEP_REG			0x0050
#define	TGA_START_REG			0x0054
#define	TGA_PIXELMASK_REG		0x005c
#define	TGA_CURSOR_BASE_REG		0x0060
#define	TGA_HORIZ_REG			0x0064
#define	TGA_VERT_REG			0x0068
#define	TGA_BASE_ADDR_REG		0x006c
#define	TGA_VALID_REG			0x0070
#define	TGA_CURSOR_XY_REG		0x0074
#define	TGA_INTR_STAT_REG		0x007c
#define TGA_DATA_REG			0x0080
#define	TGA_RAMDAC_SETUP_REG		0x00c0
#define	TGA_BLOCK_COLOR0_REG		0x0140
#define	TGA_BLOCK_COLOR1_REG		0x0144
#define	TGA_BLOCK_COLOR2_REG		0x0148
#define	TGA_BLOCK_COLOR3_REG		0x014c
#define	TGA_BLOCK_COLOR4_REG		0x0150
#define	TGA_BLOCK_COLOR5_REG		0x0154
#define	TGA_BLOCK_COLOR6_REG		0x0158
#define	TGA_BLOCK_COLOR7_REG		0x015c
#define TGA_COPY64_SRC			0x0160
#define TGA_COPY64_DST			0x0164
#define	TGA_CLOCK_REG			0x01e8
#define	TGA_RAMDAC_REG			0x01f0
#define	TGA_CMD_STAT_REG		0x01f8


/* 
 * Useful defines for managing the registers
 */

#define TGA_HORIZ_ODD			0x80000000
#define TGA_HORIZ_POLARITY		0x40000000
#define TGA_HORIZ_ACT_MSB		0x30000000
#define TGA_HORIZ_BP			0x0fe00000
#define TGA_HORIZ_SYNC			0x001fc000
#define TGA_HORIZ_FP			0x00007c00
#define TGA_HORIZ_ACT_LSB		0x000001ff

#define TGA_VERT_SE			0x80000000
#define TGA_VERT_POLARITY		0x40000000
#define TGA_VERT_RESERVED		0x30000000
#define TGA_VERT_BP			0x0fc00000
#define TGA_VERT_SYNC			0x003f0000
#define TGA_VERT_FP			0x0000f800
#define TGA_VERT_ACTIVE			0x000007ff

#define TGA_VALID_VIDEO			0x01
#define TGA_VALID_BLANK			0x02
#define TGA_VALID_CURSOR		0x04

#define TGA_MODE_SBM_8BPP		0x000
#define TGA_MODE_SBM_24BPP		0x300

#define TGA_MODE_SIMPLE			0x00
#define TGA_MODE_SIMPLEZ		0x10
#define TGA_MODE_OPAQUE_STIPPLE		0x01
#define TGA_MODE_OPAQUE_FILL		0x21
#define TGA_MODE_TRANSPARENT_STIPPLE	0x03
#define TGA_MODE_TRANSPARENT_FILL	0x23
#define TGA_MODE_BLOCK_STIPPLE		0x0d
#define TGA_MODE_BLOCK_FILL		0x2d
#define TGA_MODE_COPY			0x07
#define TGA_MODE_DMA_READ_COPY_ND	0x17
#define TGA_MODE_DMA_READ_COPY_D	0x37
#define TGA_MODE_DMA_WRITE_COPY		0x1f


/*
 * Useful defines for managing the ICS1562 PLL clock
 */

#define TGA_PLL_BASE_FREQ 		14318		/* .18 */
#define TGA_PLL_MAX_FREQ 		230000


/*
 * Useful defines for managing the BT485 on the 8-plane TGA
 */

#define	BT485_READ_BIT			0x01
#define	BT485_WRITE_BIT			0x00

#define	BT485_ADDR_PAL_WRITE		0x00
#define	BT485_DATA_PAL			0x02
#define	BT485_PIXEL_MASK		0x04
#define	BT485_ADDR_PAL_READ		0x06
#define	BT485_ADDR_CUR_WRITE		0x08
#define	BT485_DATA_CUR			0x0a
#define	BT485_CMD_0			0x0c
#define	BT485_ADDR_CUR_READ		0x0e
#define	BT485_CMD_1			0x10
#define	BT485_CMD_2			0x12
#define	BT485_STATUS			0x14
#define	BT485_CMD_3			0x14
#define	BT485_CUR_RAM			0x16
#define	BT485_CUR_LOW_X			0x18
#define	BT485_CUR_HIGH_X		0x1a
#define	BT485_CUR_LOW_Y			0x1c
#define	BT485_CUR_HIGH_Y		0x1e


/*
 * Useful defines for managing the BT463 on the 24-plane TGAs/SFB+s
 */

#define	BT463_ADDR_LO		0x0
#define	BT463_ADDR_HI		0x1
#define	BT463_REG_ACC		0x2
#define	BT463_PALETTE		0x3

#define	BT463_CUR_CLR_0		0x0100
#define	BT463_CUR_CLR_1		0x0101

#define	BT463_CMD_REG_0		0x0201
#define	BT463_CMD_REG_1		0x0202
#define	BT463_CMD_REG_2		0x0203

#define	BT463_READ_MASK_0	0x0205
#define	BT463_READ_MASK_1	0x0206
#define	BT463_READ_MASK_2	0x0207
#define	BT463_READ_MASK_3	0x0208

#define	BT463_BLINK_MASK_0	0x0209
#define	BT463_BLINK_MASK_1	0x020a
#define	BT463_BLINK_MASK_2	0x020b
#define	BT463_BLINK_MASK_3	0x020c

#define	BT463_WINDOW_TYPE_BASE	0x0300

/*
 * Useful defines for managing the BT459 on the 8-plane SFB+s
 */

#define	BT459_ADDR_LO		0x0
#define	BT459_ADDR_HI		0x1
#define	BT459_REG_ACC		0x2
#define	BT459_PALETTE		0x3

#define	BT459_CUR_CLR_1		0x0181
#define	BT459_CUR_CLR_2		0x0182
#define	BT459_CUR_CLR_3		0x0183

#define	BT459_CMD_REG_0		0x0201
#define	BT459_CMD_REG_1		0x0202
#define	BT459_CMD_REG_2		0x0203

#define	BT459_READ_MASK		0x0204

#define	BT459_BLINK_MASK	0x0206

#define	BT459_CUR_CMD_REG	0x0300

/*
 * The framebuffer driver private data.
 */

struct tga_par {
	/* PCI/TC device.  */
	struct device *dev;

	/* Device dependent information.  */
	void __iomem *tga_mem_base;
	void __iomem *tga_fb_base;
	void __iomem *tga_regs_base;
	u8 tga_type;				/* TGA_TYPE_XXX */
	u8 tga_chip_rev;			/* dc21030 revision */

	/* Remember blank mode.  */
	u8 vesa_blanked;

	/* Define the video mode.  */
	u32 xres, yres;			/* resolution in pixels */
	u32 htimings;			/* horizontal timing register */
	u32 vtimings;			/* vertical timing register */
	u32 pll_freq;			/* pixclock in mhz */
	u32 bits_per_pixel;		/* bits per pixel */
	u32 sync_on_green;		/* set if sync is on green */
};


/*
 * Macros for reading/writing TGA and RAMDAC registers
 */

static inline void
TGA_WRITE_REG(struct tga_par *par, u32 v, u32 r)
{
	writel(v, par->tga_regs_base +r);
}

static inline u32
TGA_READ_REG(struct tga_par *par, u32 r)
{
	return readl(par->tga_regs_base +r);
}

static inline void
BT485_WRITE(struct tga_par *par, u8 v, u8 r)
{
	TGA_WRITE_REG(par, r, TGA_RAMDAC_SETUP_REG);
	TGA_WRITE_REG(par, v | (r << 8), TGA_RAMDAC_REG);
}

static inline void
BT463_LOAD_ADDR(struct tga_par *par, u16 a)
{
	TGA_WRITE_REG(par, BT463_ADDR_LO<<2, TGA_RAMDAC_SETUP_REG);
	TGA_WRITE_REG(par, (BT463_ADDR_LO<<10) | (a & 0xff), TGA_RAMDAC_REG);
	TGA_WRITE_REG(par, BT463_ADDR_HI<<2, TGA_RAMDAC_SETUP_REG);
	TGA_WRITE_REG(par, (BT463_ADDR_HI<<10) | (a >> 8), TGA_RAMDAC_REG);
}

static inline void
BT463_WRITE(struct tga_par *par, u32 m, u16 a, u8 v)
{
	BT463_LOAD_ADDR(par, a);
	TGA_WRITE_REG(par, m << 2, TGA_RAMDAC_SETUP_REG);
	TGA_WRITE_REG(par, m << 10 | v, TGA_RAMDAC_REG);
}

static inline void
BT459_LOAD_ADDR(struct tga_par *par, u16 a)
{
	TGA_WRITE_REG(par, BT459_ADDR_LO << 2, TGA_RAMDAC_SETUP_REG);
	TGA_WRITE_REG(par, a & 0xff, TGA_RAMDAC_REG);
	TGA_WRITE_REG(par, BT459_ADDR_HI << 2, TGA_RAMDAC_SETUP_REG);
	TGA_WRITE_REG(par, a >> 8, TGA_RAMDAC_REG);
}

static inline void
BT459_WRITE(struct tga_par *par, u32 m, u16 a, u8 v)
{
	BT459_LOAD_ADDR(par, a);
	TGA_WRITE_REG(par, m << 2, TGA_RAMDAC_SETUP_REG);
	TGA_WRITE_REG(par, v, TGA_RAMDAC_REG);
}

#endif /* TGAFB_H */
