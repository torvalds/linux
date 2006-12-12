/*
 *  linux/include/asm-arm/arch-pxa/pxafb.h
 *
 *  Support for the xscale frame buffer.
 *
 *  Author:     Jean-Frederic Clere
 *  Created:    Sep 22, 2003
 *  Copyright:  jfclere@sinix.net
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/fb.h>

/*
 * This structure describes the machine which we are running on.
 * It is set in linux/arch/arm/mach-pxa/machine_name.c and used in the probe routine
 * of linux/drivers/video/pxafb.c
 */
struct pxafb_mode_info {
	u_long		pixclock;

	u_short		xres;
	u_short		yres;

	u_char		bpp;
	u_char		hsync_len;
	u_char		left_margin;
	u_char		right_margin;

	u_char		vsync_len;
	u_char		upper_margin;
	u_char		lower_margin;
	u_char		sync;

	u_int		cmap_greyscale:1,
			unused:31;
};

struct pxafb_mach_info {
	struct pxafb_mode_info *modes;
	unsigned int num_modes;

	u_int		fixed_modes:1,
			cmap_inverse:1,
			cmap_static:1,
			unused:29;

	/* The following should be defined in LCCR0
	 *      LCCR0_Act or LCCR0_Pas          Active or Passive
	 *      LCCR0_Sngl or LCCR0_Dual        Single/Dual panel
	 *      LCCR0_Mono or LCCR0_Color       Mono/Color
	 *      LCCR0_4PixMono or LCCR0_8PixMono (in mono single mode)
	 *      LCCR0_DMADel(Tcpu) (optional)   DMA request delay
	 *
	 * The following should not be defined in LCCR0:
	 *      LCCR0_OUM, LCCR0_BM, LCCR0_QDM, LCCR0_DIS, LCCR0_EFM
	 *      LCCR0_IUM, LCCR0_SFM, LCCR0_LDM, LCCR0_ENB
	 */
	u_int		lccr0;
	/* The following should be defined in LCCR3
	 *      LCCR3_OutEnH or LCCR3_OutEnL    Output enable polarity
	 *      LCCR3_PixRsEdg or LCCR3_PixFlEdg Pixel clock edge type
	 *      LCCR3_Acb(X)                    AB Bias pin frequency
	 *      LCCR3_DPC (optional)            Double Pixel Clock mode (untested)
	 *
	 * The following should not be defined in LCCR3
	 *      LCCR3_HSP, LCCR3_VSP, LCCR0_Pcd(x), LCCR3_Bpp
	 */
	u_int		lccr3;

	void (*pxafb_backlight_power)(int);
	void (*pxafb_lcd_power)(int, struct fb_var_screeninfo *);

};
void set_pxa_fb_info(struct pxafb_mach_info *hard_pxa_fb_info);
void set_pxa_fb_parent(struct device *parent_dev);
unsigned long pxafb_get_hsync_time(struct device *dev);
