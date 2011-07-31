#ifndef __SH_MOBILE_SDHI_H__
#define __SH_MOBILE_SDHI_H__

#include <linux/types.h>

struct sh_mobile_sdhi_info {
	int dma_slave_tx;
	int dma_slave_rx;
	unsigned long tmio_flags;
	u32 tmio_ocr_mask;	/* available MMC voltages */
	void (*set_pwr)(struct platform_device *pdev, int state);
};

#endif /* __SH_MOBILE_SDHI_H__ */
