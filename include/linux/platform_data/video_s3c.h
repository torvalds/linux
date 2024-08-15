/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PLATFORM_DATA_VIDEO_S3C
#define __PLATFORM_DATA_VIDEO_S3C

/* S3C_FB_MAX_WIN
 * Set to the maximum number of windows that any of the supported hardware
 * can use. Since the platform data uses this for an array size, having it
 * set to the maximum of any version of the hardware can do is safe.
 */
#define S3C_FB_MAX_WIN	(5)

/**
 * struct s3c_fb_pd_win - per window setup data
 * @xres     : The window X size.
 * @yres     : The window Y size.
 * @virtual_x: The virtual X size.
 * @virtual_y: The virtual Y size.
 */
struct s3c_fb_pd_win {
	unsigned short		default_bpp;
	unsigned short		max_bpp;
	unsigned short		xres;
	unsigned short		yres;
	unsigned short		virtual_x;
	unsigned short		virtual_y;
};

/**
 * struct s3c_fb_platdata -  S3C driver platform specific information
 * @setup_gpio: Setup the external GPIO pins to the right state to transfer
 *		the data from the display system to the connected display
 *		device.
 * @vidcon0: The base vidcon0 values to control the panel data format.
 * @vidcon1: The base vidcon1 values to control the panel data output.
 * @vtiming: Video timing when connected to a RGB type panel.
 * @win: The setup data for each hardware window, or NULL for unused.
 * @display_mode: The LCD output display mode.
 *
 * The platform data supplies the video driver with all the information
 * it requires to work with the display(s) attached to the machine. It
 * controls the initial mode, the number of display windows (0 is always
 * the base framebuffer) that are initialised etc.
 *
 */
struct s3c_fb_platdata {
	void	(*setup_gpio)(void);

	struct s3c_fb_pd_win	*win[S3C_FB_MAX_WIN];
	struct fb_videomode     *vtiming;

	u32			 vidcon0;
	u32			 vidcon1;
};

#endif
