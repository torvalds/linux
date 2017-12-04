/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_MOBILE_CEU_H__
#define __ASM_SH_MOBILE_CEU_H__

#define SH_CEU_FLAG_USE_8BIT_BUS	(1 << 0) /* use  8bit bus width */
#define SH_CEU_FLAG_USE_16BIT_BUS	(1 << 1) /* use 16bit bus width */
#define SH_CEU_FLAG_HSYNC_LOW		(1 << 2) /* default High if possible */
#define SH_CEU_FLAG_VSYNC_LOW		(1 << 3) /* default High if possible */
#define SH_CEU_FLAG_LOWER_8BIT		(1 << 4) /* default upper 8bit */

struct device;
struct resource;

struct sh_mobile_ceu_companion {
	u32		num_resources;
	struct resource	*resource;
	int		id;
	void		*platform_data;
};

struct sh_mobile_ceu_info {
	unsigned long flags;
	int max_width;
	int max_height;
	struct v4l2_async_subdev **asd;	/* Flat array, arranged in groups */
	unsigned int *asd_sizes;	/* 0-terminated array pf asd group sizes */
};

#endif /* __ASM_SH_MOBILE_CEU_H__ */
