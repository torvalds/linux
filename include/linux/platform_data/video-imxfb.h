/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This structure describes the machine which we are running on.
 */
#ifndef __MACH_IMXFB_H__
#define __MACH_IMXFB_H__

#include <linux/fb.h>

#define PCR_TFT		(1 << 31)
#define PCR_COLOR	(1 << 30)
#define PCR_PBSIZ_1	(0 << 28)
#define PCR_PBSIZ_2	(1 << 28)
#define PCR_PBSIZ_4	(2 << 28)
#define PCR_PBSIZ_8	(3 << 28)
#define PCR_BPIX_1	(0 << 25)
#define PCR_BPIX_2	(1 << 25)
#define PCR_BPIX_4	(2 << 25)
#define PCR_BPIX_8	(3 << 25)
#define PCR_BPIX_12	(4 << 25)
#define PCR_BPIX_16	(5 << 25)
#define PCR_BPIX_18	(6 << 25)
#define PCR_PIXPOL	(1 << 24)
#define PCR_FLMPOL	(1 << 23)
#define PCR_LPPOL	(1 << 22)
#define PCR_CLKPOL	(1 << 21)
#define PCR_OEPOL	(1 << 20)
#define PCR_SCLKIDLE	(1 << 19)
#define PCR_END_SEL	(1 << 18)
#define PCR_END_BYTE_SWAP (1 << 17)
#define PCR_REV_VS	(1 << 16)
#define PCR_ACD_SEL	(1 << 15)
#define PCR_ACD(x)	(((x) & 0x7f) << 8)
#define PCR_SCLK_SEL	(1 << 7)
#define PCR_SHARP	(1 << 6)
#define PCR_PCD(x)	((x) & 0x3f)

#define PWMR_CLS(x)	(((x) & 0x1ff) << 16)
#define PWMR_LDMSK	(1 << 15)
#define PWMR_SCR1	(1 << 10)
#define PWMR_SCR0	(1 << 9)
#define PWMR_CC_EN	(1 << 8)
#define PWMR_PW(x)	((x) & 0xff)

#define LSCR1_PS_RISE_DELAY(x)    (((x) & 0x7f) << 26)
#define LSCR1_CLS_RISE_DELAY(x)   (((x) & 0x3f) << 16)
#define LSCR1_REV_TOGGLE_DELAY(x) (((x) & 0xf) << 8)
#define LSCR1_GRAY2(x)            (((x) & 0xf) << 4)
#define LSCR1_GRAY1(x)            (((x) & 0xf))

struct imx_fb_videomode {
	struct fb_videomode mode;
	u32 pcr;
	bool aus_mode;
	unsigned char	bpp;
};

struct imx_fb_platform_data {
	struct imx_fb_videomode *mode;
	int		num_modes;

	u_int		pwmr;
	u_int		lscr1;
	u_int		dmacr;

	int (*init)(struct platform_device *);
	void (*exit)(struct platform_device *);
};

#endif /* ifndef __MACH_IMXFB_H__ */
