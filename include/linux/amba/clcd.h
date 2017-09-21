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
#include <linux/fb.h>
#include <linux/amba/clcd-regs.h>

enum {
	/* individual formats */
	CLCD_CAP_RGB444		= (1 << 0),
	CLCD_CAP_RGB5551	= (1 << 1),
	CLCD_CAP_RGB565		= (1 << 2),
	CLCD_CAP_RGB888		= (1 << 3),
	CLCD_CAP_BGR444		= (1 << 4),
	CLCD_CAP_BGR5551	= (1 << 5),
	CLCD_CAP_BGR565		= (1 << 6),
	CLCD_CAP_BGR888		= (1 << 7),

	/* connection layouts */
	CLCD_CAP_444		= CLCD_CAP_RGB444 | CLCD_CAP_BGR444,
	CLCD_CAP_5551		= CLCD_CAP_RGB5551 | CLCD_CAP_BGR5551,
	CLCD_CAP_565		= CLCD_CAP_RGB565 | CLCD_CAP_BGR565,
	CLCD_CAP_888		= CLCD_CAP_RGB888 | CLCD_CAP_BGR888,

	/* red/blue ordering */
	CLCD_CAP_RGB		= CLCD_CAP_RGB444 | CLCD_CAP_RGB5551 |
				  CLCD_CAP_RGB565 | CLCD_CAP_RGB888,
	CLCD_CAP_BGR		= CLCD_CAP_BGR444 | CLCD_CAP_BGR5551 |
				  CLCD_CAP_BGR565 | CLCD_CAP_BGR888,

	CLCD_CAP_ALL		= CLCD_CAP_BGR | CLCD_CAP_RGB,
};

struct backlight_device;

struct clcd_panel {
	struct fb_videomode	mode;
	signed short		width;	/* width in mm */
	signed short		height;	/* height in mm */
	u32			tim2;
	u32			tim3;
	u32			cntl;
	u32			caps;
	unsigned int		bpp:8,
				fixedtimings:1,
				grayscale:1;
	unsigned int		connector;
	struct backlight_device	*backlight;
	/*
	 * If the B/R lines are switched between the CLCD
	 * and the panel we need to know this and not try to
	 * compensate with the BGR bit in the control register.
	 */
	bool			bgr_connection;
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
	 * Optional.  Hardware capability flags.
	 */
	u32	caps;

	/*
	 * Optional.  Check whether the var structure is acceptable
	 * for this display.
	 */
	int	(*check)(struct clcd_fb *fb, struct fb_var_screeninfo *var);

	/*
	 * Compulsory.  Decode fb->fb.var into regs->*.  In the case of
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

/**
 * struct clcd_vendor_data - holds hardware (IP-block) vendor-specific
 * variant information
 *
 * @clock_timregs: the CLCD needs to be clocked when accessing the
 * timer registers, or the hardware will hang.
 * @packed_24_bit_pixels: this variant supports 24bit packed pixel data,
 * so that RGB accesses 3 bytes at a time, not just on even 32bit
 * boundaries, packing the pixel data in memory. ST Microelectronics
 * have this.
 * @st_bitmux_control: ST Microelectronics have implemented output
 * bit line multiplexing into the CLCD control register. This indicates
 * that we need to use this.
 * @init_board: custom board init function for this variant
 * @init_panel: custom panel init function for this variant
 */
struct clcd_vendor_data {
	bool	clock_timregs;
	bool	packed_24_bit_pixels;
	bool	st_bitmux_control;
	int	(*init_board)(struct amba_device *adev,
			      struct clcd_board *board);
	int	(*init_panel)(struct clcd_fb *fb,
			      struct device_node *panel);
};

/* this data structure describes each frame buffer device we find */
struct clcd_fb {
	struct fb_info		fb;
	struct amba_device	*dev;
	struct clk		*clk;
	struct clcd_vendor_data	*vendor;
	struct clcd_panel	*panel;
	struct clcd_board	*board;
	void			*board_data;
	void __iomem		*regs;
	u16			off_ienb;
	u16			off_cntl;
	u32			clcd_cntl;
	u32			cmap[16];
	bool			clk_enabled;
};

static inline void clcdfb_decode(struct clcd_fb *fb, struct clcd_regs *regs)
{
	struct fb_var_screeninfo *var = &fb->fb.var;
	u32 val, cpl;

	/*
	 * Program the CLCD controller registers and start the CLCD
	 */
	val = ((var->xres / 16) - 1) << 2;
	val |= (var->hsync_len - 1) << 8;
	val |= (var->right_margin - 1) << 16;
	val |= (var->left_margin - 1) << 24;
	regs->tim0 = val;

	val = var->yres;
	if (fb->panel->cntl & CNTL_LCDDUAL)
		val /= 2;
	val -= 1;
	val |= (var->vsync_len - 1) << 10;
	val |= var->lower_margin << 16;
	val |= var->upper_margin << 24;
	regs->tim1 = val;

	val = fb->panel->tim2;
	val |= var->sync & FB_SYNC_HOR_HIGH_ACT  ? 0 : TIM2_IHS;
	val |= var->sync & FB_SYNC_VERT_HIGH_ACT ? 0 : TIM2_IVS;

	cpl = var->xres_virtual;
	if (fb->panel->cntl & CNTL_LCDTFT)	  /* TFT */
		/* / 1 */;
	else if (!var->grayscale)		  /* STN color */
		cpl = cpl * 8 / 3;
	else if (fb->panel->cntl & CNTL_LCDMONO8) /* STN monochrome, 8bit */
		cpl /= 8;
	else					  /* STN monochrome, 4bit */
		cpl /= 4;

	regs->tim2 = val | ((cpl - 1) << 16);

	regs->tim3 = fb->panel->tim3;

	val = fb->panel->cntl;
	if (var->grayscale)
		val |= CNTL_LCDBW;

	if (fb->panel->caps && fb->board->caps && var->bits_per_pixel >= 16) {
		/*
		 * if board and panel supply capabilities, we can support
		 * changing BGR/RGB depending on supplied parameters. Here
		 * we switch to what the framebuffer is providing if need
		 * be, so if the framebuffer is BGR but the display connection
		 * is RGB (first case) we switch it around. Vice versa mutatis
		 * mutandis if the framebuffer is RGB but the display connection
		 * is BGR, we flip it around.
		 */
		if (var->red.offset == 0)
			val &= ~CNTL_BGR;
		else
			val |= CNTL_BGR;
		if (fb->panel->bgr_connection)
			val ^= CNTL_BGR;
	}

	switch (var->bits_per_pixel) {
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
		 * PL110 cannot choose between 5551 and 565 modes in its
		 * control register.  It is possible to use 565 with
		 * custom external wiring.
		 */
		if (amba_part(fb->dev) == 0x110 ||
		    var->green.length == 5)
			val |= CNTL_LCDBPP16;
		else if (var->green.length == 6)
			val |= CNTL_LCDBPP16_565;
		else
			val |= CNTL_LCDBPP16_444;
		break;
	case 24:
		/* Modified variant supporting 24 bit packed pixels */
		val |= CNTL_ST_LCDBPP24_PACKED;
		break;
	case 32:
		val |= CNTL_LCDBPP24;
		break;
	}

	regs->cntl = val;
	regs->pixclock = var->pixclock;
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
