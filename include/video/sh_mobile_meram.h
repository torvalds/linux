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

/*
 * struct sh_mobile_meram_info - MERAM platform data
 * @reserved_icbs: Bitmask of reserved ICBs (for instance used through UIO)
 */
struct sh_mobile_meram_info {
	int				addr_mode;
	u32				reserved_icbs;
	struct sh_mobile_meram_priv	*priv;
	struct platform_device		*pdev;
};

/* icb config */
struct sh_mobile_meram_icb_cfg {
	unsigned int meram_size;	/* MERAM Buffer Size to use */
};

struct sh_mobile_meram_cfg {
	struct sh_mobile_meram_icb_cfg icb[2];
};

#if defined(CONFIG_FB_SH_MOBILE_MERAM) || \
    defined(CONFIG_FB_SH_MOBILE_MERAM_MODULE)
unsigned long sh_mobile_meram_alloc(struct sh_mobile_meram_info *meram_dev,
				    size_t size);
void sh_mobile_meram_free(struct sh_mobile_meram_info *meram_dev,
			  unsigned long mem, size_t size);
void *sh_mobile_meram_cache_alloc(struct sh_mobile_meram_info *dev,
				  const struct sh_mobile_meram_cfg *cfg,
				  unsigned int xres, unsigned int yres,
				  unsigned int pixelformat,
				  unsigned int *pitch);
void sh_mobile_meram_cache_free(struct sh_mobile_meram_info *dev, void *data);
void sh_mobile_meram_cache_update(struct sh_mobile_meram_info *dev, void *data,
				  unsigned long base_addr_y,
				  unsigned long base_addr_c,
				  unsigned long *icb_addr_y,
				  unsigned long *icb_addr_c);
#else
static inline unsigned long
sh_mobile_meram_alloc(struct sh_mobile_meram_info *meram_dev, size_t size)
{
	return 0;
}

static inline void
sh_mobile_meram_free(struct sh_mobile_meram_info *meram_dev,
		     unsigned long mem, size_t size)
{
}

static inline void *
sh_mobile_meram_cache_alloc(struct sh_mobile_meram_info *dev,
			    const struct sh_mobile_meram_cfg *cfg,
			    unsigned int xres, unsigned int yres,
			    unsigned int pixelformat,
			    unsigned int *pitch)
{
	return ERR_PTR(-ENODEV);
}

static inline void
sh_mobile_meram_cache_free(struct sh_mobile_meram_info *dev, void *data)
{
}

static inline void
sh_mobile_meram_cache_update(struct sh_mobile_meram_info *dev, void *data,
			     unsigned long base_addr_y,
			     unsigned long base_addr_c,
			     unsigned long *icb_addr_y,
			     unsigned long *icb_addr_c)
{
}
#endif

#endif /* __VIDEO_SH_MOBILE_MERAM_H__  */
