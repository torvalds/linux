/*
 * This structure describes the machine which we are running on.
 */
struct imxfb_mach_info {
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
			cmap_inverse:1,
			cmap_static:1,
			unused:29;

	u_int		pcr;
	u_int		pwmr;
	u_int		lscr1;

	u_char * fixed_screen_cpu;
	dma_addr_t fixed_screen_dma;

	void (*lcd_power)(int);
	void (*backlight_power)(int);
};
void set_imx_fb_info(struct imxfb_mach_info *hard_imx_fb_info);
