/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This structure describes the machine which we are running on.
 */
#ifndef __MACH_IMXFB_H__
#define __MACH_IMXFB_H__

#include <linux/fb.h>

#define PCR_TFT		(1 << 31)
#define PCR_BPIX_8	(3 << 25)
#define PCR_BPIX_12	(4 << 25)
#define PCR_BPIX_16	(5 << 25)
#define PCR_BPIX_18	(6 << 25)

struct imx_fb_videomode {
	struct fb_videomode mode;
	u32 pcr;
	bool aus_mode;
	unsigned char	bpp;
};

#endif /* ifndef __MACH_IMXFB_H__ */
