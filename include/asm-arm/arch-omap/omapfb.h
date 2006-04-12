/*
 * File: include/asm-arm/arch-omap/omapfb.h
 *
 * Framebuffer driver for TI OMAP boards
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __OMAPFB_H
#define __OMAPFB_H

/* IOCTL commands. */

#define OMAP_IOW(num, dtype)	_IOW('O', num, dtype)
#define OMAP_IOR(num, dtype)	_IOR('O', num, dtype)
#define OMAP_IOWR(num, dtype)	_IOWR('O', num, dtype)
#define OMAP_IO(num)		_IO('O', num)

#define OMAPFB_MIRROR		OMAP_IOW(31, int)
#define OMAPFB_SYNC_GFX		OMAP_IO(37)
#define OMAPFB_VSYNC		OMAP_IO(38)
#define OMAPFB_SET_UPDATE_MODE	OMAP_IOW(40, int)
#define OMAPFB_UPDATE_WINDOW_OLD OMAP_IOW(41, struct omapfb_update_window_old)
#define OMAPFB_GET_CAPS		OMAP_IOR(42, unsigned long)
#define OMAPFB_GET_UPDATE_MODE	OMAP_IOW(43, int)
#define OMAPFB_LCD_TEST		OMAP_IOW(45, int)
#define OMAPFB_CTRL_TEST	OMAP_IOW(46, int)
#define OMAPFB_UPDATE_WINDOW	OMAP_IOW(47, struct omapfb_update_window)
#define OMAPFB_SETUP_PLANE	OMAP_IOW(48, struct omapfb_setup_plane)
#define OMAPFB_ENABLE_PLANE	OMAP_IOW(49, struct omapfb_enable_plane)
#define OMAPFB_SET_COLOR_KEY	OMAP_IOW(50, struct omapfb_color_key)

#define OMAPFB_CAPS_GENERIC_MASK	0x00000fff
#define OMAPFB_CAPS_LCDC_MASK		0x00fff000
#define OMAPFB_CAPS_PANEL_MASK		0xff000000

#define OMAPFB_CAPS_MANUAL_UPDATE	0x00001000
#define OMAPFB_CAPS_SET_BACKLIGHT	0x01000000

/* Values from DSP must map to lower 16-bits */
#define OMAPFB_FORMAT_MASK         0x00ff
#define OMAPFB_FORMAT_FLAG_DOUBLE  0x0100

enum omapfb_color_format {
	OMAPFB_COLOR_RGB565 = 0,
	OMAPFB_COLOR_YUV422,
	OMAPFB_COLOR_YUV420,
	OMAPFB_COLOR_CLUT_8BPP,
	OMAPFB_COLOR_CLUT_4BPP,
	OMAPFB_COLOR_CLUT_2BPP,
	OMAPFB_COLOR_CLUT_1BPP,
};

struct omapfb_update_window {
	__u32 x, y;
	__u32 width, height;
	__u32 format;
};

struct omapfb_update_window_old {
	__u32 x, y;
	__u32 width, height;
};

enum omapfb_plane {
	OMAPFB_PLANE_GFX = 0,
	OMAPFB_PLANE_VID1,
	OMAPFB_PLANE_VID2,
};

enum omapfb_channel_out {
	OMAPFB_CHANNEL_OUT_LCD = 0,
	OMAPFB_CHANNEL_OUT_DIGIT,
};

struct omapfb_setup_plane {
	__u8  plane;
	__u8  channel_out;
	__u32 offset;
	__u32 pos_x, pos_y;
	__u32 width, height;
	__u32 color_mode;
};

struct omapfb_enable_plane {
	__u8  plane;
	__u8  enable;
};

enum omapfb_color_key_type {
	OMAPFB_COLOR_KEY_DISABLED = 0,
	OMAPFB_COLOR_KEY_GFX_DST,
	OMAPFB_COLOR_KEY_VID_SRC,
};

struct omapfb_color_key {
	__u8  channel_out;
	__u32 background;
	__u32 trans_key;
	__u8  key_type;
};

enum omapfb_update_mode {
	OMAPFB_UPDATE_DISABLED = 0,
	OMAPFB_AUTO_UPDATE,
	OMAPFB_MANUAL_UPDATE
};

#ifdef __KERNEL__

#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/mutex.h>

#include <asm/arch/board.h>

#define OMAP_LCDC_INV_VSYNC             0x0001
#define OMAP_LCDC_INV_HSYNC             0x0002
#define OMAP_LCDC_INV_PIX_CLOCK         0x0004
#define OMAP_LCDC_INV_OUTPUT_EN         0x0008
#define OMAP_LCDC_HSVS_RISING_EDGE      0x0010
#define OMAP_LCDC_HSVS_OPPOSITE         0x0020

#define OMAP_LCDC_SIGNAL_MASK		0x003f

#define OMAP_LCDC_PANEL_TFT		0x0100

#ifdef CONFIG_ARCH_OMAP1
#define OMAPFB_PLANE_NUM		1
#else
#define OMAPFB_PLANE_NUM		3
#endif

struct omapfb_device;

struct lcd_panel {
	const char	*name;
	int		config;		/* TFT/STN, signal inversion */
	int		bpp;		/* Pixel format in fb mem */
	int		data_lines;	/* Lines on LCD HW interface */

	int		x_res, y_res;
	int		pixel_clock;	/* In kHz */
	int		hsw;		/* Horizontal synchronization
					   pulse width */
	int		hfp;		/* Horizontal front porch */
	int		hbp;		/* Horizontal back porch */
	int		vsw;		/* Vertical synchronization
					   pulse width */
	int		vfp;		/* Vertical front porch */
	int		vbp;		/* Vertical back porch */
	int		acb;		/* ac-bias pin frequency */
	int		pcd;		/* pixel clock divider.
					   Obsolete use pixel_clock instead */

	int		(*init)		(struct omapfb_device *fbdev);
	void		(*cleanup)	(void);
	int		(*enable)	(void);
	void		(*disable)	(void);
	unsigned long	(*get_caps)	(void);
	int		(*set_bklight_level)(unsigned int level);
	unsigned int	(*get_bklight_level)(void);
	unsigned int	(*get_bklight_max)  (void);
	int		(*run_test)	(int test_num);
};

struct omapfb_device;

struct extif_timings {
	int cs_on_time;
	int cs_off_time;
	int we_on_time;
	int we_off_time;
	int re_on_time;
	int re_off_time;
	int we_cycle_time;
	int re_cycle_time;
	int cs_pulse_width;
	int access_time;

	int clk_div;

	u32 tim[5];		/* set by extif->convert_timings */

	int converted;
};

struct lcd_ctrl_extif {
	int  (*init)		(void);
	void (*cleanup)		(void);
	void (*get_clk_info)	(u32 *clk_period, u32 *max_clk_div);
	int  (*convert_timings)	(struct extif_timings *timings);
	void (*set_timings)	(const struct extif_timings *timings);
	void (*set_bits_per_cycle)(int bpc);
	void (*write_command)	(const void *buf, unsigned int len);
	void (*read_data)	(void *buf, unsigned int len);
	void (*write_data)	(const void *buf, unsigned int len);
	void (*transfer_area)	(int width, int height,
				 void (callback)(void * data), void *data);
	unsigned long		max_transmit_size;
};

struct omapfb_notifier_block {
	struct notifier_block	nb;
	void			*data;
};

typedef int (*omapfb_notifier_callback_t)(struct omapfb_notifier_block *,
					   unsigned long event,
					   struct omapfb_device *fbdev);

struct lcd_ctrl {
	const char	*name;
	void		*data;

	int		(*init)		  (struct omapfb_device *fbdev,
					   int ext_mode, int req_vram_size);
	void		(*cleanup)	  (void);
	void		(*bind_client)	  (struct omapfb_notifier_block *nb);
	void		(*get_vram_layout)(unsigned long *size,
					   void **virt_base,
					   dma_addr_t *phys_base);
	int		(*mmap)		  (struct vm_area_struct *vma);
	unsigned long	(*get_caps)	  (void);
	int		(*set_update_mode)(enum omapfb_update_mode mode);
	enum omapfb_update_mode (*get_update_mode)(void);
	int		(*setup_plane)	  (int plane, int channel_out,
					   unsigned long offset,
					   int screen_width,
					   int pos_x, int pos_y, int width,
					   int height, int color_mode);
	int		(*enable_plane)	  (int plane, int enable);
	int		(*update_window)  (struct omapfb_update_window *win,
					   void (*callback)(void *),
					   void *callback_data);
	void		(*sync)		  (void);
	void		(*suspend)	  (void);
	void		(*resume)	  (void);
	int		(*run_test)	  (int test_num);
	int		(*setcolreg)	  (u_int regno, u16 red, u16 green,
					   u16 blue, u16 transp,
					   int update_hw_mem);
	int		(*set_color_key)  (struct omapfb_color_key *ck);

};

enum omapfb_state {
	OMAPFB_DISABLED	= 0,
	OMAPFB_SUSPENDED= 99,
	OMAPFB_ACTIVE	= 100
};

struct omapfb_device {
	int			state;
	int                     ext_lcdc;               /* Using external
                                                           LCD controller */
	struct mutex		rqueue_mutex;

	void			*vram_virt_base;
	dma_addr_t		vram_phys_base;
	unsigned long		vram_size;

	int			color_mode;
	int			palette_size;
	int			mirror;
	u32			pseudo_palette[17];

	struct lcd_panel	*panel;			/* LCD panel */
	struct lcd_ctrl         *ctrl;			/* LCD controller */
	struct lcd_ctrl		*int_ctrl;		/* internal LCD ctrl */
	struct lcd_ctrl_extif	*ext_if;		/* LCD ctrl external
							   interface */
	struct fb_info		*fb_info;

	struct device		*dev;
};

struct omapfb_platform_data {
	struct omap_lcd_config   lcd;
	struct omap_fbmem_config fbmem;
};

#define OMAPFB_EVENT_READY	1
#define OMAPFB_EVENT_DISABLED	2

#ifdef CONFIG_ARCH_OMAP1
extern struct lcd_ctrl omap1_lcd_ctrl;
#else
extern struct lcd_ctrl omap2_disp_ctrl;
#endif

extern void omapfb_register_panel(struct lcd_panel *panel);
extern void omapfb_write_first_pixel(struct omapfb_device *fbdev, u16 pixval);
extern void omapfb_notify_clients(struct omapfb_device *fbdev,
				  unsigned long event);
extern int  omapfb_register_client(struct omapfb_notifier_block *nb,
				    omapfb_notifier_callback_t callback,
				    void *callback_data);
extern int  omapfb_unregister_client(struct omapfb_notifier_block *nb);
extern int  omapfb_update_window_async(struct omapfb_update_window *win,
					void (*callback)(void *),
					void *callback_data);

/* in arch/arm/plat-omap/devices.c */
extern void omapfb_reserve_mem(void);

#endif /* __KERNEL__ */

#endif /* __OMAPFB_H */
