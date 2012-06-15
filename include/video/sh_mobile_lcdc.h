#ifndef __ASM_SH_MOBILE_LCDC_H__
#define __ASM_SH_MOBILE_LCDC_H__

#include <linux/fb.h>
#include <video/sh_mobile_meram.h>

/* Register definitions */
#define _LDDCKR			0x410
#define LDDCKR_ICKSEL_BUS	(0 << 16)
#define LDDCKR_ICKSEL_MIPI	(1 << 16)
#define LDDCKR_ICKSEL_HDMI	(2 << 16)
#define LDDCKR_ICKSEL_EXT	(3 << 16)
#define LDDCKR_ICKSEL_MASK	(7 << 16)
#define LDDCKR_MOSEL		(1 << 6)
#define _LDDCKSTPR		0x414
#define _LDINTR			0x468
#define LDINTR_FE		(1 << 10)
#define LDINTR_VSE		(1 << 9)
#define LDINTR_VEE		(1 << 8)
#define LDINTR_FS		(1 << 2)
#define LDINTR_VSS		(1 << 1)
#define LDINTR_VES		(1 << 0)
#define LDINTR_STATUS_MASK	(0xff << 0)
#define _LDSR			0x46c
#define LDSR_MSS		(1 << 10)
#define LDSR_MRS		(1 << 8)
#define LDSR_AS			(1 << 1)
#define _LDCNT1R		0x470
#define LDCNT1R_DE		(1 << 0)
#define _LDCNT2R		0x474
#define LDCNT2R_BR		(1 << 8)
#define LDCNT2R_MD		(1 << 3)
#define LDCNT2R_SE		(1 << 2)
#define LDCNT2R_ME		(1 << 1)
#define LDCNT2R_DO		(1 << 0)
#define _LDRCNTR		0x478
#define LDRCNTR_SRS		(1 << 17)
#define LDRCNTR_SRC		(1 << 16)
#define LDRCNTR_MRS		(1 << 1)
#define LDRCNTR_MRC		(1 << 0)
#define _LDDDSR			0x47c
#define LDDDSR_LS		(1 << 2)
#define LDDDSR_WS		(1 << 1)
#define LDDDSR_BS		(1 << 0)

#define LDMT1R_VPOL		(1 << 28)
#define LDMT1R_HPOL		(1 << 27)
#define LDMT1R_DWPOL		(1 << 26)
#define LDMT1R_DIPOL		(1 << 25)
#define LDMT1R_DAPOL		(1 << 24)
#define LDMT1R_HSCNT		(1 << 17)
#define LDMT1R_DWCNT		(1 << 16)
#define LDMT1R_IFM		(1 << 12)
#define LDMT1R_MIFTYP_RGB8	(0x0 << 0)
#define LDMT1R_MIFTYP_RGB9	(0x4 << 0)
#define LDMT1R_MIFTYP_RGB12A	(0x5 << 0)
#define LDMT1R_MIFTYP_RGB12B	(0x6 << 0)
#define LDMT1R_MIFTYP_RGB16	(0x7 << 0)
#define LDMT1R_MIFTYP_RGB18	(0xa << 0)
#define LDMT1R_MIFTYP_RGB24	(0xb << 0)
#define LDMT1R_MIFTYP_YCBCR	(0xf << 0)
#define LDMT1R_MIFTYP_SYS8A	(0x0 << 0)
#define LDMT1R_MIFTYP_SYS8B	(0x1 << 0)
#define LDMT1R_MIFTYP_SYS8C	(0x2 << 0)
#define LDMT1R_MIFTYP_SYS8D	(0x3 << 0)
#define LDMT1R_MIFTYP_SYS9	(0x4 << 0)
#define LDMT1R_MIFTYP_SYS12	(0x5 << 0)
#define LDMT1R_MIFTYP_SYS16A	(0x7 << 0)
#define LDMT1R_MIFTYP_SYS16B	(0x8 << 0)
#define LDMT1R_MIFTYP_SYS16C	(0x9 << 0)
#define LDMT1R_MIFTYP_SYS18	(0xa << 0)
#define LDMT1R_MIFTYP_SYS24	(0xb << 0)
#define LDMT1R_MIFTYP_MASK	(0xf << 0)

#define LDDFR_CF1		(1 << 18)
#define LDDFR_CF0		(1 << 17)
#define LDDFR_CC		(1 << 16)
#define LDDFR_YF_420		(0 << 8)
#define LDDFR_YF_422		(1 << 8)
#define LDDFR_YF_444		(2 << 8)
#define LDDFR_YF_MASK		(3 << 8)
#define LDDFR_PKF_ARGB32	(0x00 << 0)
#define LDDFR_PKF_RGB16		(0x03 << 0)
#define LDDFR_PKF_RGB24		(0x0b << 0)
#define LDDFR_PKF_MASK		(0x1f << 0)

#define LDSM1R_OS		(1 << 0)

#define LDSM2R_OSTRG		(1 << 0)

#define LDPMR_LPS		(3 << 0)

#define _LDDWD0R		0x800
#define LDDWDxR_WDACT		(1 << 28)
#define LDDWDxR_RSW		(1 << 24)
#define _LDDRDR			0x840
#define LDDRDR_RSR		(1 << 24)
#define LDDRDR_DRD_MASK		(0x3ffff << 0)
#define _LDDWAR			0x900
#define LDDWAR_WA		(1 << 0)
#define _LDDRAR			0x904
#define LDDRAR_RA		(1 << 0)

enum {
	RGB8	= LDMT1R_MIFTYP_RGB8,	/* 24bpp, 8:8:8 */
	RGB9	= LDMT1R_MIFTYP_RGB9,	/* 18bpp, 9:9 */
	RGB12A	= LDMT1R_MIFTYP_RGB12A,	/* 24bpp, 12:12 */
	RGB12B	= LDMT1R_MIFTYP_RGB12B,	/* 12bpp */
	RGB16	= LDMT1R_MIFTYP_RGB16,	/* 16bpp */
	RGB18	= LDMT1R_MIFTYP_RGB18,	/* 18bpp */
	RGB24	= LDMT1R_MIFTYP_RGB24,	/* 24bpp */
	YUV422	= LDMT1R_MIFTYP_YCBCR,	/* 16bpp */
	SYS8A	= LDMT1R_IFM | LDMT1R_MIFTYP_SYS8A,	/* 24bpp, 8:8:8 */
	SYS8B	= LDMT1R_IFM | LDMT1R_MIFTYP_SYS8B,	/* 18bpp, 8:8:2 */
	SYS8C	= LDMT1R_IFM | LDMT1R_MIFTYP_SYS8C,	/* 18bpp, 2:8:8 */
	SYS8D	= LDMT1R_IFM | LDMT1R_MIFTYP_SYS8D,	/* 16bpp, 8:8 */
	SYS9	= LDMT1R_IFM | LDMT1R_MIFTYP_SYS9,	/* 18bpp, 9:9 */
	SYS12	= LDMT1R_IFM | LDMT1R_MIFTYP_SYS12,	/* 24bpp, 12:12 */
	SYS16A	= LDMT1R_IFM | LDMT1R_MIFTYP_SYS16A,	/* 16bpp */
	SYS16B	= LDMT1R_IFM | LDMT1R_MIFTYP_SYS16B,	/* 18bpp, 16:2 */
	SYS16C	= LDMT1R_IFM | LDMT1R_MIFTYP_SYS16C,	/* 18bpp, 2:16 */
	SYS18	= LDMT1R_IFM | LDMT1R_MIFTYP_SYS18,	/* 18bpp */
	SYS24	= LDMT1R_IFM | LDMT1R_MIFTYP_SYS24,	/* 24bpp */
};

enum { LCDC_CHAN_DISABLED = 0,
       LCDC_CHAN_MAINLCD,
       LCDC_CHAN_SUBLCD };

enum { LCDC_CLK_BUS, LCDC_CLK_PERIPHERAL, LCDC_CLK_EXTERNAL };

#define LCDC_FLAGS_DWPOL (1 << 0) /* Rising edge dot clock data latch */
#define LCDC_FLAGS_DIPOL (1 << 1) /* Active low display enable polarity */
#define LCDC_FLAGS_DAPOL (1 << 2) /* Active low display data polarity */
#define LCDC_FLAGS_HSCNT (1 << 3) /* Disable HSYNC during VBLANK */
#define LCDC_FLAGS_DWCNT (1 << 4) /* Disable dotclock during blanking */

struct sh_mobile_lcdc_sys_bus_cfg {
	unsigned long ldmt2r;
	unsigned long ldmt3r;
	unsigned long deferred_io_msec;
};

struct sh_mobile_lcdc_sys_bus_ops {
	void (*write_index)(void *handle, unsigned long data);
	void (*write_data)(void *handle, unsigned long data);
	unsigned long (*read_data)(void *handle);
};

struct sh_mobile_lcdc_panel_cfg {
	unsigned long width;		/* Panel width in mm */
	unsigned long height;		/* Panel height in mm */
	int (*setup_sys)(void *sys_ops_handle,
			 struct sh_mobile_lcdc_sys_bus_ops *sys_ops);
	void (*start_transfer)(void *sys_ops_handle,
			       struct sh_mobile_lcdc_sys_bus_ops *sys_ops);
	void (*display_on)(void);
	void (*display_off)(void);
};

/* backlight info */
struct sh_mobile_lcdc_bl_info {
	const char *name;
	int max_brightness;
	int (*set_brightness)(int brightness);
	int (*get_brightness)(void);
};

struct sh_mobile_lcdc_chan_cfg {
	int chan;
	int fourcc;
	int colorspace;
	int interface_type; /* selects RGBn or SYSn I/F, see above */
	int clock_divider;
	unsigned long flags; /* LCDC_FLAGS_... */
	const struct fb_videomode *lcd_modes;
	int num_modes;
	struct sh_mobile_lcdc_panel_cfg panel_cfg;
	struct sh_mobile_lcdc_bl_info bl_info;
	struct sh_mobile_lcdc_sys_bus_cfg sys_bus_cfg; /* only for SYSn I/F */
	const struct sh_mobile_meram_cfg *meram_cfg;

	struct platform_device *tx_dev;	/* HDMI/DSI transmitter device */
};

struct sh_mobile_lcdc_info {
	int clock_source;
	struct sh_mobile_lcdc_chan_cfg ch[2];
	struct sh_mobile_meram_info *meram_dev;
};

#endif /* __ASM_SH_MOBILE_LCDC_H__ */
