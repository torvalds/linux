/*
 * linux/include/asm-arm/hardware/amba_clcd.h -- Integrator LCD panel.
 *
 * David A Rusling
 *
 * Copyright (C) 2001 ARM Limited
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */
#include <linux/config.h>
#include <linux/fb.h>

/*
 * CLCD Controller Internal Register addresses
 */
#define CLCD_TIM0		0x00000000
#define CLCD_TIM1 		0x00000004
#define CLCD_TIM2 		0x00000008
#define CLCD_TIM3 		0x0000000c
#define CLCD_UBAS 		0x00000010
#define CLCD_LBAS 		0x00000014

#if !defined(CONFIG_ARCH_VERSATILE) && !defined(CONFIG_ARCH_REALVIEW)
#define CLCD_IENB 		0x00000018
#define CLCD_CNTL 		0x0000001c
#else
/*
 * Someone rearranged these two registers on the Versatile
 * platform...
 */
#define CLCD_IENB 		0x0000001c
#define CLCD_CNTL 		0x00000018
#endif

#define CLCD_STAT 		0x00000020
#define CLCD_INTR 		0x00000024
#define CLCD_UCUR 		0x00000028
#define CLCD_LCUR 		0x0000002C
#define CLCD_PALL 		0x00000200
#define CLCD_PALETTE		0x00000200

#define TIM2_CLKSEL		(1 << 5)
#define TIM2_IVS		(1 << 11)
#define TIM2_IHS		(1 << 12)
#define TIM2_IPC		(1 << 13)
#define TIM2_IOE		(1 << 14)
#define TIM2_BCD		(1 << 26)

#define CNTL_LCDEN		(1 << 0)
#define CNTL_LCDBPP1		(0 << 1)
#define CNTL_LCDBPP2		(1 << 1)
#define CNTL_LCDBPP4		(2 << 1)
#define CNTL_LCDBPP8		(3 << 1)
#define CNTL_LCDBPP16		(4 << 1)
#define CNTL_LCDBPP16_565	(6 << 1)
#define CNTL_LCDBPP24		(5 << 1)
#define CNTL_LCDBW		(1 << 4)
#define CNTL_LCDTFT		(1 << 5)
#define CNTL_LCDMONO8		(1 << 6)
#define CNTL_LCDDUAL		(1 << 7)
#define CNTL_BGR		(1 << 8)
#define CNTL_BEBO		(1 << 9)
#define CNTL_BEPO		(1 << 10)
#define CNTL_LCDPWR		(1 << 11)
#define CNTL_LCDVCOMP(x)	((x) << 12)
#define CNTL_LDMAFIFOTIME	(1 << 15)
#define CNTL_WATERMARK		(1 << 16)

struct clcd_panel {
	struct fb_videomode	mode;
	signed short		width;	/* width in mm */
	signed short		height;	/* height in mm */
	u32			tim2;
	u32			tim3;
	u32			cntl;
	unsigned int		bpp:8,
				fixedtimings:1,
				grayscale:1;
	unsigned int		connector;
};

struct clcd_regs {
	u32			tim0;
	u32			tim1;
	u32			tim2;
	u32			tim3;
	u32			cntl;
	unsigned long		pixclock;
};

struct clcd_fb;

/*
 * the board-type specific routines
 */
struct clcd_board {
	const char *name;

	/*
	 * Optional.  Check whether the var structure is acceptable
	 * for this display.
	 */
	int	(*check)(struct clcd_fb *fb, struct fb_var_screeninfo *var);

	/*
	 * Compulsary.  Decode fb->fb.var into regs->*.  In the case of
	 * fixed timing, set regs->* to the register values required.
	 */
	void	(*decode)(struct clcd_fb *fb, struct clcd_regs *regs);

	/*
	 * Optional.  Disable any extra display hardware.
	 */
	void	(*disable)(struct clcd_fb *);

	/*
	 * Optional.  Enable any extra display hardware.
	 */
	void	(*enable)(struct clcd_fb *);

	/*
	 * Setup platform specific parts of CLCD driver
	 */
	int	(*setup)(struct clcd_fb *);

	/*
	 * mmap the framebuffer memory
	 */
	int	(*mmap)(struct clcd_fb *, struct vm_area_struct *);

	/*
	 * Remove platform specific parts of CLCD driver
	 */
	void	(*remove)(struct clcd_fb *);
};

struct amba_device;
struct clk;

/* this data structure describes each frame buffer device we find */
struct clcd_fb {
	struct fb_info		fb;
	struct amba_device	*dev;
	struct clk		*clk;
	struct clcd_panel	*panel;
	struct clcd_board	*board;
	void			*board_data;
	void __iomem		*regs;
	u32			clcd_cntl;
	u32			cmap[16];
};

static inline void clcdfb_decode(struct clcd_fb *fb, struct clcd_regs *regs)
{
	u32 val, cpl;

	/*
	 * Program the CLCD controller registers and start the CLCD
	 */
	val = ((fb->fb.var.xres / 16) - 1) << 2;
	val |= (fb->fb.var.hsync_len - 1) << 8;
	val |= (fb->fb.var.right_margin - 1) << 16;
	val |= (fb->fb.var.left_margin - 1) << 24;
	regs->tim0 = val;

	val = fb->fb.var.yres;
	if (fb->panel->cntl & CNTL_LCDDUAL)
		val /= 2;
	val -= 1;
	val |= (fb->fb.var.vsync_len - 1) << 10;
	val |= fb->fb.var.lower_margin << 16;
	val |= fb->fb.var.upper_margin << 24;
	regs->tim1 = val;

	val = fb->panel->tim2;
	val |= fb->fb.var.sync & FB_SYNC_HOR_HIGH_ACT  ? 0 : TIM2_IHS;
	val |= fb->fb.var.sync & FB_SYNC_VERT_HIGH_ACT ? 0 : TIM2_IVS;

	cpl = fb->fb.var.xres_virtual;
	if (fb->panel->cntl & CNTL_LCDTFT)	  /* TFT */
		/* / 1 */;
	else if (!fb->fb.var.grayscale)		  /* STN color */
		cpl = cpl * 8 / 3;
	else if (fb->panel->cntl & CNTL_LCDMONO8) /* STN monochrome, 8bit */
		cpl /= 8;
	else					  /* STN monochrome, 4bit */
		cpl /= 4;

	regs->tim2 = val | ((cpl - 1) << 16);

	regs->tim3 = fb->panel->tim3;

	val = fb->panel->cntl;
	if (fb->fb.var.grayscale)
		val |= CNTL_LCDBW;

	switch (fb->fb.var.bits_per_pixel) {
	case 1:
		val |= CNTL_LCDBPP1;
		break;
	case 2:
		val |= CNTL_LCDBPP2;
		break;
	case 4:
		val |= CNTL_LCDBPP4;
		break;
	case 8:
		val |= CNTL_LCDBPP8;
		break;
	case 16:
		/*
		 * PL110 cannot choose between 5551 and 565 modes in
		 * its control register
		 */
		if ((fb->dev->periphid & 0x000fffff) == 0x00041110)
			val |= CNTL_LCDBPP16;
		else if (fb->fb.var.green.length == 5)
			val |= CNTL_LCDBPP16;
		else
			val |= CNTL_LCDBPP16_565;
		break;
	case 32:
		val |= CNTL_LCDBPP24;
		break;
	}

	regs->cntl = val;
	regs->pixclock = fb->fb.var.pixclock;
}

static inline int clcdfb_check(struct clcd_fb *fb, struct fb_var_screeninfo *var)
{
	var->xres_virtual = var->xres = (var->xres + 15) & ~15;
	var->yres_virtual = var->yres = (var->yres + 1) & ~1;

#define CHECK(e,l,h) (var->e < l || var->e > h)
	if (CHECK(right_margin, (5+1), 256) ||	/* back porch */
	    CHECK(left_margin, (5+1), 256) ||	/* front porch */
	    CHECK(hsync_len, (5+1), 256) ||
	    var->xres > 4096 ||
	    var->lower_margin > 255 ||		/* back porch */
	    var->upper_margin > 255 ||		/* front porch */
	    var->vsync_len > 32 ||
	    var->yres > 1024)
		return -EINVAL;
#undef CHECK

	/* single panel mode: PCD = max(PCD, 1) */
	/* dual panel mode: PCD = max(PCD, 5) */

	/*
	 * You can't change the grayscale setting, and
	 * we can only do non-interlaced video.
	 */
	if (var->grayscale != fb->fb.var.grayscale ||
	    (var->vmode & FB_VMODE_MASK) != FB_VMODE_NONINTERLACED)
		return -EINVAL;

#define CHECK(e) (var->e != fb->fb.var.e)
	if (fb->panel->fixedtimings &&
	    (CHECK(xres)		||
	     CHECK(yres)		||
	     CHECK(bits_per_pixel)	||
	     CHECK(pixclock)		||
	     CHECK(left_margin)		||
	     CHECK(right_margin)	||
	     CHECK(upper_margin)	||
	     CHECK(lower_margin)	||
	     CHECK(hsync_len)		||
	     CHECK(vsync_len)		||
	     CHECK(sync)))
		return -EINVAL;
#undef CHECK

	var->nonstd = 0;
	var->accel_flags = 0;

	return 0;
}
