#ifndef __MBX_FB_H
#define __MBX_FB_H

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

#endif /* __MBX_FB_H */
