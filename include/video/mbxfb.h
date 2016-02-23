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
#define MBXFB_FMT_YUV16		0
#define MBXFB_FMT_YUV12		1

/* packed */
#define MBXFB_FMT_UY0VY1	2
#define MBXFB_FMT_VY0UY1	3
#define MBXFB_FMT_Y0UY1V	4
#define MBXFB_FMT_Y0VY1U	5
struct mbxfb_overlaySetup {
	__u32 enable;
	__u32 x, y;
	__u32 width, height;
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

#define MBXFB_ALPHABLEND_NONE		0
#define MBXFB_ALPHABLEND_GLOBAL		1
#define MBXFB_ALPHABLEND_PIXEL		2

#define MBXFB_COLORKEY_DISABLED		0
#define MBXFB_COLORKEY_PREVIOUS		1
#define MBXFB_COLORKEY_CURRENT		2
struct mbxfb_alphaCtl {
	__u8 overlay_blend_mode;
	__u8 overlay_colorkey_mode;
	__u8 overlay_global_alpha;
	__u32 overlay_colorkey;
	__u32 overlay_colorkey_mask;

	__u8 graphics_blend_mode;
	__u8 graphics_colorkey_mode;
	__u8 graphics_global_alpha;
	__u32 graphics_colorkey;
	__u32 graphics_colorkey_mask;
};

#define MBXFB_PLANE_GRAPHICS	0
#define MBXFB_PLANE_VIDEO	1
struct mbxfb_planeorder {
	__u8 bottom;
	__u8 top;
};

struct mbxfb_reg {
	__u32 addr; 	/* offset from 0x03fe 0000 */
	__u32 val;		/* value */
	__u32 mask;		/* which bits to touch (for write) */
};

#define MBXFB_IOCX_OVERLAY		_IOWR(0xF4, 0x00,struct mbxfb_overlaySetup)
#define MBXFB_IOCG_ALPHA		_IOR(0xF4, 0x01,struct mbxfb_alphaCtl)
#define MBXFB_IOCS_ALPHA		_IOW(0xF4, 0x02,struct mbxfb_alphaCtl)
#define MBXFB_IOCS_PLANEORDER	_IOR(0xF4, 0x03,struct mbxfb_planeorder)
#define MBXFB_IOCS_REG			_IOW(0xF4, 0x04,struct mbxfb_reg)
#define MBXFB_IOCX_REG			_IOWR(0xF4, 0x05,struct mbxfb_reg)

#endif /* __MBX_FB_H */
