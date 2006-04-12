#ifndef ASMARM_ARCH_MMC_H
#define ASMARM_ARCH_MMC_H

#include <linux/mmc/protocol.h>

struct imxmmc_platform_data {
	int (*card_present)(void);
};

extern void imx_set_mmc_info(struct imxmmc_platform_data *info);

#endif
