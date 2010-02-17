#ifndef __SH_MOBILE_SDHI_H__
#define __SH_MOBILE_SDHI_H__

struct sh_mobile_sdhi_info {
	void (*set_pwr)(struct platform_device *pdev, int state);
};

#endif /* __SH_MOBILE_SDHI_H__ */
