#ifndef __VIDEO_SH_MOBILE_MERAM_H__
#define __VIDEO_SH_MOBILE_MERAM_H__

/* For sh_mobile_meram_info.addr_mode */
enum {
	SH_MOBILE_MERAM_MODE0 = 0,
	SH_MOBILE_MERAM_MODE1
};

enum {
	SH_MOBILE_MERAM_PF_NV = 0,
	SH_MOBILE_MERAM_PF_RGB,
	SH_MOBILE_MERAM_PF_NV24
};


struct sh_mobile_meram_priv;
struct sh_mobile_meram_ops;

struct sh_mobile_meram_info {
	int				addr_mode;
	struct sh_mobile_meram_ops	*ops;
	struct sh_mobile_meram_priv	*priv;
	struct platform_device		*pdev;
};

/* icb config */
struct sh_mobile_meram_icb {
	int marker_icb;		/* ICB # for Marker ICB */
	int cache_icb;		/* ICB # for Cache ICB */
	int meram_offset;	/* MERAM Buffer Offset to use */
	int meram_size;		/* MERAM Buffer Size to use */

	int cache_unit;		/* bytes to cache per ICB */
};

struct sh_mobile_meram_cfg {
	struct sh_mobile_meram_icb	icb[2];
	int				pixelformat;
	int				current_reg;
};

struct module;
struct sh_mobile_meram_ops {
	struct module	*module;
	/* register usage of meram */
	int (*meram_register)(struct sh_mobile_meram_info *meram_dev,
			      struct sh_mobile_meram_cfg *cfg,
			      int xres, int yres, int pixelformat,
			      unsigned long base_addr_y,
			      unsigned long base_addr_c,
			      unsigned long *icb_addr_y,
			      unsigned long *icb_addr_c, int *pitch);

	/* unregister usage of meram */
	int (*meram_unregister)(struct sh_mobile_meram_info *meram_dev,
				struct sh_mobile_meram_cfg *cfg);

	/* update meram settings */
	int (*meram_update)(struct sh_mobile_meram_info *meram_dev,
			    struct sh_mobile_meram_cfg *cfg,
			    unsigned long base_addr_y,
			    unsigned long base_addr_c,
			    unsigned long *icb_addr_y,
			    unsigned long *icb_addr_c);
};

#endif /* __VIDEO_SH_MOBILE_MERAM_H__  */
