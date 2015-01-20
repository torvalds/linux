#ifndef LINUX_MMC_SH_MOBILE_SDHI_H
#define LINUX_MMC_SH_MOBILE_SDHI_H

#include <linux/types.h>

#define SH_MOBILE_SDHI_IRQ_CARD_DETECT	"card_detect"
#define SH_MOBILE_SDHI_IRQ_SDCARD	"sdcard"
#define SH_MOBILE_SDHI_IRQ_SDIO		"sdio"

struct sh_mobile_sdhi_info {
	int dma_slave_tx;
	int dma_slave_rx;
	unsigned long tmio_flags;
	unsigned long tmio_caps;
	unsigned long tmio_caps2;
	u32 tmio_ocr_mask;	/* available MMC voltages */
	unsigned int cd_gpio;
};

#endif /* LINUX_MMC_SH_MOBILE_SDHI_H */
