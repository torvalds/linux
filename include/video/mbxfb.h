#ifndef __MBX_FB_H
#define __MBX_FB_H

#include <asm/ioctl.h>
#include <asm/types.h>

struct mbxfb_val {
	unsigned int	defval;
	unsigned int	min;
	unsigned int	max;
};

struct fb_info;

struct mbxfb_platform_data {
		/* Screen info */
		struct mbxfb_val xres;
		struct mbxfb_val yres;
		struct mbxfb_val bpp;

		/* Memory info */
		unsigned long memsize; /* if 0 use ODFB? */
		unsigned long timings1;
		unsigned long timings2;
		unsigned long timings3;

		int (*probe)(struct fb_info *fb);
		int (*remove)(struct fb_info *fb);
};

/* planar */
#define MBXFB_FMT_YUV12		0

/* packed */
#define MBXFB_FMT_UY0VY1	1
#define MBXFB_FMT_VY0UY1	2
#define MBXFB_FMT_Y0UY1V	3
#define MBXFB_FMT_Y0VY1U	4
struct mbxfb_overlaySetup {
	__u32 enable;
	__u32 x, y;
	__u32 width, height;
	__u32 alpha;
	__u32 fmt;
	__u32 mem_offset;
	__u32 scaled_width;
	__u32 scaled_height;

	/* Filled by the driver */
	__u32 U_offset;
	__u32 V_offset;

	__u16 Y_stride;
	__u16 UV_stride;
};

#define MBXFB_IOCX_OVERLAY	_IOWR(0xF4, 0x00,struct mbxfb_overlaySetup)

#endif /* __MBX_FB_H */
