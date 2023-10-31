/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * linux/include/video/mmp_disp.h
 * Header file for Marvell MMP Display Controller
 *
 * Copyright (C) 2012 Marvell Technology Group Ltd.
 * Authors: Zhou Zhu <zzhu3@marvell.com>
 */

#ifndef _MMP_DISP_H_
#define _MMP_DISP_H_
#include <linux/kthread.h>

enum {
	PIXFMT_UYVY = 0,
	PIXFMT_VYUY,
	PIXFMT_YUYV,
	PIXFMT_YUV422P,
	PIXFMT_YVU422P,
	PIXFMT_YUV420P,
	PIXFMT_YVU420P,
	PIXFMT_RGB565 = 0x100,
	PIXFMT_BGR565,
	PIXFMT_RGB1555,
	PIXFMT_BGR1555,
	PIXFMT_RGB888PACK,
	PIXFMT_BGR888PACK,
	PIXFMT_RGB888UNPACK,
	PIXFMT_BGR888UNPACK,
	PIXFMT_RGBA888,
	PIXFMT_BGRA888,
	PIXFMT_RGB666, /* for output usage */
	PIXFMT_PSEUDOCOLOR = 0x200,
};

static inline int pixfmt_to_stride(int pix_fmt)
{
	switch (pix_fmt) {
	case PIXFMT_RGB565:
	case PIXFMT_BGR565:
	case PIXFMT_RGB1555:
	case PIXFMT_BGR1555:
	case PIXFMT_UYVY:
	case PIXFMT_VYUY:
	case PIXFMT_YUYV:
		return 2;
	case PIXFMT_RGB888UNPACK:
	case PIXFMT_BGR888UNPACK:
	case PIXFMT_RGBA888:
	case PIXFMT_BGRA888:
		return 4;
	case PIXFMT_RGB888PACK:
	case PIXFMT_BGR888PACK:
		return 3;
	case PIXFMT_YUV422P:
	case PIXFMT_YVU422P:
	case PIXFMT_YUV420P:
	case PIXFMT_YVU420P:
	case PIXFMT_PSEUDOCOLOR:
		return 1;
	default:
		return 0;
	}
}

/* parameters used by path/overlay */
/* overlay related para: win/addr */
struct mmp_win {
	/* position/size of window */
	u16	xsrc;
	u16	ysrc;
	u16	xdst;
	u16	ydst;
	u16	xpos;
	u16	ypos;
	u16	left_crop;
	u16	right_crop;
	u16	up_crop;
	u16	bottom_crop;
	int	pix_fmt;
	/*
	 * pitch[0]: graphics/video layer line length or y pitch
	 * pitch[1]/pitch[2]: video u/v pitch if non-zero
	 */
	u32	pitch[3];
};

struct mmp_addr {
	/* phys address */
	u32	phys[6];
};

/* path related para: mode */
struct mmp_mode {
	const char *name;
	u32 refresh;
	u32 xres;
	u32 yres;
	u32 left_margin;
	u32 right_margin;
	u32 upper_margin;
	u32 lower_margin;
	u32 hsync_len;
	u32 vsync_len;
	u32 hsync_invert;
	u32 vsync_invert;
	u32 invert_pixclock;
	u32 pixclock_freq;
	int pix_fmt_out;
};

/* main structures */
struct mmp_path;
struct mmp_overlay;
struct mmp_panel;

/* status types */
enum {
	MMP_OFF = 0,
	MMP_ON,
};

static inline const char *stat_name(int stat)
{
	switch (stat) {
	case MMP_OFF:
		return "OFF";
	case MMP_ON:
		return "ON";
	default:
		return "UNKNOWNSTAT";
	}
}

struct mmp_overlay_ops {
	/* should be provided by driver */
	void (*set_fetch)(struct mmp_overlay *overlay, int fetch_id);
	void (*set_onoff)(struct mmp_overlay *overlay, int status);
	void (*set_win)(struct mmp_overlay *overlay, struct mmp_win *win);
	int (*set_addr)(struct mmp_overlay *overlay, struct mmp_addr *addr);
};

/* overlay describes a z-order indexed slot in each path. */
struct mmp_overlay {
	int id;
	const char *name;
	struct mmp_path *path;

	/* overlay info: private data */
	int dmafetch_id;
	struct mmp_addr addr;
	struct mmp_win win;

	/* state */
	int open_count;
	int status;
	struct mutex access_ok;

	struct mmp_overlay_ops *ops;
};

/* panel type */
enum {
	PANELTYPE_ACTIVE = 0,
	PANELTYPE_SMART,
	PANELTYPE_TV,
	PANELTYPE_DSI_CMD,
	PANELTYPE_DSI_VIDEO,
};

struct mmp_panel {
	/* use node to register to list */
	struct list_head node;
	const char *name;
	/* path name used to connect to proper path configed */
	const char *plat_path_name;
	struct device *dev;
	int panel_type;
	void *plat_data;
	int (*get_modelist)(struct mmp_panel *panel,
			struct mmp_mode **modelist);
	void (*set_mode)(struct mmp_panel *panel,
			struct mmp_mode *mode);
	void (*set_onoff)(struct mmp_panel *panel,
			int status);
};

struct mmp_path_ops {
	int (*check_status)(struct mmp_path *path);
	struct mmp_overlay *(*get_overlay)(struct mmp_path *path,
			int overlay_id);
	int (*get_modelist)(struct mmp_path *path,
			struct mmp_mode **modelist);

	/* follow ops should be provided by driver */
	void (*set_mode)(struct mmp_path *path, struct mmp_mode *mode);
	void (*set_onoff)(struct mmp_path *path, int status);
	/* todo: add query */
};

/* path output types */
enum {
	PATH_OUT_PARALLEL,
	PATH_OUT_DSI,
	PATH_OUT_HDMI,
};

/* path is main part of mmp-disp */
struct mmp_path {
	/* use node to register to list */
	struct list_head node;

	/* init data */
	struct device *dev;

	int id;
	const char *name;
	int output_type;
	struct mmp_panel *panel;
	void *plat_data;

	/* dynamic use */
	struct mmp_mode mode;

	/* state */
	int open_count;
	int status;
	struct mutex access_ok;

	struct mmp_path_ops ops;

	/* layers */
	int overlay_num;
	struct mmp_overlay overlays[] __counted_by(overlay_num);
};

extern struct mmp_path *mmp_get_path(const char *name);
static inline void mmp_path_set_mode(struct mmp_path *path,
		struct mmp_mode *mode)
{
	if (path)
		path->ops.set_mode(path, mode);
}
static inline void mmp_path_set_onoff(struct mmp_path *path, int status)
{
	if (path)
		path->ops.set_onoff(path, status);
}
static inline int mmp_path_get_modelist(struct mmp_path *path,
		struct mmp_mode **modelist)
{
	if (path)
		return path->ops.get_modelist(path, modelist);
	return 0;
}
static inline struct mmp_overlay *mmp_path_get_overlay(
		struct mmp_path *path, int overlay_id)
{
	if (path)
		return path->ops.get_overlay(path, overlay_id);
	return NULL;
}
static inline void mmp_overlay_set_fetch(struct mmp_overlay *overlay,
		int fetch_id)
{
	if (overlay)
		overlay->ops->set_fetch(overlay, fetch_id);
}
static inline void mmp_overlay_set_onoff(struct mmp_overlay *overlay,
		int status)
{
	if (overlay)
		overlay->ops->set_onoff(overlay, status);
}
static inline void mmp_overlay_set_win(struct mmp_overlay *overlay,
		struct mmp_win *win)
{
	if (overlay)
		overlay->ops->set_win(overlay, win);
}
static inline int mmp_overlay_set_addr(struct mmp_overlay *overlay,
		struct mmp_addr *addr)
{
	if (overlay)
		return overlay->ops->set_addr(overlay, addr);
	return 0;
}

/*
 * driver data is set from each detailed ctrl driver for path usage
 * it defined a common interface that plat driver need to implement
 */
struct mmp_path_info {
	/* driver data, set when registed*/
	const char *name;
	struct device *dev;
	int id;
	int output_type;
	int overlay_num;
	void (*set_mode)(struct mmp_path *path, struct mmp_mode *mode);
	void (*set_onoff)(struct mmp_path *path, int status);
	struct mmp_overlay_ops *overlay_ops;
	void *plat_data;
};

extern struct mmp_path *mmp_register_path(
		struct mmp_path_info *info);
extern void mmp_unregister_path(struct mmp_path *path);
extern void mmp_register_panel(struct mmp_panel *panel);
extern void mmp_unregister_panel(struct mmp_panel *panel);

/* defintions for platform data */
/* interface for buffer driver */
struct mmp_buffer_driver_mach_info {
	const char	*name;
	const char	*path_name;
	int	overlay_id;
	int	dmafetch_id;
	int	default_pixfmt;
};

/* interface for controllers driver */
struct mmp_mach_path_config {
	const char *name;
	int overlay_num;
	int output_type;
	u32 path_config;
	u32 link_config;
	u32 dsi_rbswap;
};

struct mmp_mach_plat_info {
	const char *name;
	const char *clk_name;
	int path_num;
	struct mmp_mach_path_config *paths;
};

/* interface for panel drivers */
struct mmp_mach_panel_info {
	const char *name;
	void (*plat_set_onoff)(int status);
	const char *plat_path_name;
};
#endif	/* _MMP_DISP_H_ */
