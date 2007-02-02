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

#include <asm/ioctl.h>
#include <asm/types.h>

/* IOCTL commands. */

#define OMAP_IOW(num, dtype)	_IOW('O', num, dtype)
#define OMAP_IOR(num, dtype)	_IOR('O', num, dtype)
#define OMAP_IOWR(num, dtype)	_IOWR('O', num, dtype)
#define OMAP_IO(num)		_IO('O', num)

#define OMAPFB_MIRROR		OMAP_IOW(31, int)
#define OMAPFB_SYNC_GFX		OMAP_IO(37)
#define OMAPFB_VSYNC		OMAP_IO(38)
#define OMAPFB_SET_UPDATE_MODE	OMAP_IOW(40, int)
#define OMAPFB_GET_CAPS		OMAP_IOR(42, struct omapfb_caps)
#define OMAPFB_GET_UPDATE_MODE	OMAP_IOW(43, int)
#define OMAPFB_LCD_TEST		OMAP_IOW(45, int)
#define OMAPFB_CTRL_TEST	OMAP_IOW(46, int)
#define OMAPFB_UPDATE_WINDOW_OLD OMAP_IOW(47, struct omapfb_update_window_old)
#define OMAPFB_SET_COLOR_KEY	OMAP_IOW(50, struct omapfb_color_key)
#define OMAPFB_GET_COLOR_KEY	OMAP_IOW(51, struct omapfb_color_key)
#define OMAPFB_SETUP_PLANE	OMAP_IOW(52, struct omapfb_plane_info)
#define OMAPFB_QUERY_PLANE	OMAP_IOW(53, struct omapfb_plane_info)
#define OMAPFB_UPDATE_WINDOW	OMAP_IOW(54, struct omapfb_update_window)
#define OMAPFB_SETUP_MEM	OMAP_IOW(55, struct omapfb_mem_info)
#define OMAPFB_QUERY_MEM	OMAP_IOW(56, struct omapfb_mem_info)

#define OMAPFB_CAPS_GENERIC_MASK	0x00000fff
#define OMAPFB_CAPS_LCDC_MASK		0x00fff000
#define OMAPFB_CAPS_PANEL_MASK		0xff000000

#define OMAPFB_CAPS_MANUAL_UPDATE	0x00001000
#define OMAPFB_CAPS_TEARSYNC		0x00002000
#define OMAPFB_CAPS_PLANE_RELOCATE_MEM	0x00004000
#define OMAPFB_CAPS_PLANE_SCALE		0x00008000
#define OMAPFB_CAPS_WINDOW_PIXEL_DOUBLE	0x00010000
#define OMAPFB_CAPS_WINDOW_SCALE	0x00020000
#define OMAPFB_CAPS_WINDOW_OVERLAY	0x00040000
#define OMAPFB_CAPS_SET_BACKLIGHT	0x01000000

/* Values from DSP must map to lower 16-bits */
#define OMAPFB_FORMAT_MASK		0x00ff
#define OMAPFB_FORMAT_FLAG_DOUBLE	0x0100
#define OMAPFB_FORMAT_FLAG_TEARSYNC	0x0200
#define OMAPFB_FORMAT_FLAG_FORCE_VSYNC	0x0400
#define OMAPFB_FORMAT_FLAG_ENABLE_OVERLAY	0x0800
#define OMAPFB_FORMAT_FLAG_DISABLE_OVERLAY	0x1000

#define OMAPFB_EVENT_READY	1
#define OMAPFB_EVENT_DISABLED	2

#define OMAPFB_MEMTYPE_SDRAM		0
#define OMAPFB_MEMTYPE_SRAM		1
#define OMAPFB_MEMTYPE_MAX		1

enum omapfb_color_format {
	OMAPFB_COLOR_RGB565 = 0,
	OMAPFB_COLOR_YUV422,
	OMAPFB_COLOR_YUV420,
	OMAPFB_COLOR_CLUT_8BPP,
	OMAPFB_COLOR_CLUT_4BPP,
	OMAPFB_COLOR_CLUT_2BPP,
	OMAPFB_COLOR_CLUT_1BPP,
	OMAPFB_COLOR_RGB444,
	OMAPFB_COLOR_YUY422,
};

struct omapfb_update_window {
	__u32 x, y;
	__u32 width, height;
	__u32 format;
	__u32 out_x, out_y;
	__u32 out_width, out_height;
	__u32 reserved[8];
};

struct omapfb_update_window_old {
	__u32 x, y;
	__u32 width, height;
	__u32 format;
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

struct omapfb_plane_info {
	__u32 pos_x;
	__u32 pos_y;
	__u8  enabled;
	__u8  channel_out;
	__u8  mirror;
	__u8  reserved1;
	__u32 out_width;
	__u32 out_height;
	__u32 reserved2[12];
};

struct omapfb_mem_info {
	__u32 size;
	__u8  type;
	__u8  reserved[3];
};

struct omapfb_caps {
	__u32 ctrl;
	__u32 plane_color;
	__u32 wnd_color;
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

#define OMAPFB_PLANE_XRES_MIN		8
#define OMAPFB_PLANE_YRES_MIN		8

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

	int		(*init)		(struct lcd_panel *panel,
					 struct omapfb_device *fbdev);
	void		(*cleanup)	(struct lcd_panel *panel);
	int		(*enable)	(struct lcd_panel *panel);
	void		(*disable)	(struct lcd_panel *panel);
	unsigned long	(*get_caps)	(struct lcd_panel *panel);
	int		(*set_bklight_level)(struct lcd_panel *panel,
					     unsigned int level);
	unsigned int	(*get_bklight_level)(struct lcd_panel *panel);
	unsigned int	(*get_bklight_max)  (struct lcd_panel *panel);
	int		(*run_test)	(struct lcd_panel *panel, int test_num);
};

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
	int  (*init)		(struct omapfb_device *fbdev);
	void (*cleanup)		(void);
	void (*get_clk_info)	(u32 *clk_period, u32 *max_clk_div);
	unsigned long (*get_max_tx_rate)(void);
	int  (*convert_timings)	(struct extif_timings *timings);
	void (*set_timings)	(const struct extif_timings *timings);
	void (*set_bits_per_cycle)(int bpc);
	void (*write_command)	(const void *buf, unsigned int len);
	void (*read_data)	(void *buf, unsigned int len);
	void (*write_data)	(const void *buf, unsigned int len);
	void (*transfer_area)	(int width, int height,
				 void (callback)(void * data), void *data);
	int  (*setup_tearsync)	(unsigned pin_cnt,
				 unsigned hs_pulse_time, unsigned vs_pulse_time,
				 int hs_pol_inv, int vs_pol_inv, int div);
	int  (*enable_tearsync) (int enable, unsigned line);

	unsigned long		max_transmit_size;
};

struct omapfb_notifier_block {
	struct notifier_block	nb;
	void			*data;
	int			plane_idx;
};

typedef int (*omapfb_notifier_callback_t)(struct notifier_block *,
					  unsigned long event,
					  void *fbi);

struct omapfb_mem_region {
	dma_addr_t	paddr;
	void		*vaddr;
	unsigned long	size;
	u8		type;		/* OMAPFB_PLANE_MEM_* */
	unsigned	alloc:1;	/* allocated by the driver */
	unsigned	map:1;		/* kernel mapped by the driver */
};

struct omapfb_mem_desc {
	int				region_cnt;
	struct omapfb_mem_region	region[OMAPFB_PLANE_NUM];
};

struct lcd_ctrl {
	const char	*name;
	void		*data;

	int		(*init)		  (struct omapfb_device *fbdev,
					   int ext_mode,
					   struct omapfb_mem_desc *req_md);
	void		(*cleanup)	  (void);
	void		(*bind_client)	  (struct omapfb_notifier_block *nb);
	void		(*get_caps)	  (int plane, struct omapfb_caps *caps);
	int		(*set_update_mode)(enum omapfb_update_mode mode);
	enum omapfb_update_mode (*get_update_mode)(void);
	int		(*setup_plane)	  (int plane, int channel_out,
					   unsigned long offset,
					   int screen_width,
					   int pos_x, int pos_y, int width,
					   int height, int color_mode);
	int		(*setup_mem)	  (int plane, size_t size,
					   int mem_type, unsigned long *paddr);
	int		(*mmap)		  (struct fb_info *info,
					   struct vm_area_struct *vma);
	int		(*set_scale)	  (int plane,
					   int orig_width, int orig_height,
					   int out_width, int out_height);
	int		(*enable_plane)	  (int plane, int enable);
	int		(*update_window)  (struct fb_info *fbi,
					   struct omapfb_update_window *win,
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
	int		(*get_color_key)  (struct omapfb_color_key *ck);
};

enum omapfb_state {
	OMAPFB_DISABLED	= 0,
	OMAPFB_SUSPENDED= 99,
	OMAPFB_ACTIVE	= 100
};

struct omapfb_plane_struct {
	int				idx;
	struct omapfb_plane_info	info;
	enum omapfb_color_format	color_mode;
	struct omapfb_device		*fbdev;
};

struct omapfb_device {
	int			state;
	int                     ext_lcdc;               /* Using external
                                                           LCD controller */
	struct mutex		rqueue_mutex;

	int			palette_size;
	u32			pseudo_palette[17];

	struct lcd_panel	*panel;			/* LCD panel */
	struct lcd_ctrl         *ctrl;			/* LCD controller */
	struct lcd_ctrl		*int_ctrl;		/* internal LCD ctrl */
	struct lcd_ctrl_extif	*ext_if;		/* LCD ctrl external
							   interface */
	struct device		*dev;
	struct fb_var_screeninfo	new_var;	/* for mode changes */

	struct omapfb_mem_desc		mem_desc;
	struct fb_info			*fb_info[OMAPFB_PLANE_NUM];
};

struct omapfb_platform_data {
	struct omap_lcd_config		lcd;
	struct omapfb_mem_desc		mem_desc;
	void				*ctrl_platform_data;
};

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
extern int  omapfb_update_window_async(struct fb_info *fbi,
				       struct omapfb_update_window *win,
				       void (*callback)(void *),
				       void *callback_data);

/* in arch/arm/plat-omap/fb.c */
extern void omapfb_set_ctrl_platform_data(void *pdata);

#endif /* __KERNEL__ */

#endif /* __OMAPFB_H */
