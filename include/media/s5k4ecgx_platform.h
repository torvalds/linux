/* linux/include/media/s5k4ecgx_platform.h
 *
 * Copyright (c) 2012 Hardkernel Co., Ltd.
 * 		http://www.hardkernel.com/
 *
 * Driver for S5K4EAGX from Samsung Electronics
 * CMOS Image Sensor SoC with an Embedded Image Processor
 * supporting PVI
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/
//#include <linux/device.h>
//#include <media/v4l2-mediabus.h>
#define DEFAULT_PIX_FMT     V4L2_PIX_FMT_UYVY   /* YUV422 */
#define DEFAULT_MCLK        24000000
#define S5K5CCGX_STREAMOFF_DELAY    120


#define S5K4ECGX_DRIVER_NAME	"S5K4ECGX"
enum {
	S5K4ECGX_FLASH_MODE_NORMAL,
	S5K4ECGX_FLASH_MODE_MOVIE,
	S5K4ECGX_FLASH_MODE_MAX,
};

enum {
	S5K4ECGX_FLASH_OFF = 0,
	S5K4ECGX_FLASH_ON = 1,
};

/* Define debug level */
#define CAMDBG_LEVEL_ERR		(1 << 0)
#define CAMDBG_LEVEL_WARN		(1 << 1)
#define CAMDBG_LEVEL_INFO		(1 << 2)
#define CAMDBG_LEVEL_DEBUG		(1 << 3)
#define CAMDBG_LEVEL_TRACE		(1 << 4)
#define CAMDBG_LEVEL_DEFAULT	\
	(CAMDBG_LEVEL_ERR | CAMDBG_LEVEL_WARN | CAMDBG_LEVEL_INFO)

#if 0
struct s5k4ecgx_mbus_platform_data {
	int id;
	struct v4l2_mbus_framefmt fmt;
	unsigned long clk_rate; /* master clock frequency in Hz */
	int (*set_power)(int on);
	int (*set_clock)(struct device *dev, int on);
};
#endif

struct s5k4ecgx_platform_data {
	u32 default_width;
	u32 default_height;
	u32 pixelformat;
	u32 freq;	/* MCLK in Hz */

	/* This SoC supports Parallel & CSI-2 */
	u32 is_mipi;		/* set to 1 if mipi */
	s32 streamoff_delay;	/* ms, type is signed */

	/* ISP interrupt */
	/* int (*config_isp_irq)(void);*/

	//#define S5K5CCGX_SUPPORT_FLASH
	int (*flash_en)(u32 mode, u32 onoff);
	int (*is_flash_on)(void);
	int (*set_power)(int enable);

	u8 dbg_level;
};

