#ifndef __SH_MOBILE_SDHI_H__
#define __SH_MOBILE_SDHI_H__

#include <linux/types.h>

struct platform_device;
struct tmio_mmc_data;

struct sh_mobile_sdhi_info {
	int dma_slave_tx;
	int dma_slave_rx;
	unsigned long tmio_flags;
	unsigned long tmio_caps;
	u32 tmio_ocr_mask;	/* available MMC voltages */
	struct tmio_mmc_data *pdata;
	void (*set_pwr)(struct platform_device *pdev, int state);
	int (*get_cd)(struct platform_device *pdev);
};

#endif /* __SH_MOBILE_SDHI_H__ */
