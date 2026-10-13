/* SPDX-License-Identifier: GPL-2.0 */
/*
 * an7581-afe-common.h  --  Airoha AN7581 audio driver definitions
 */

#ifndef _AN7581_AFE_COMMON_H_
#define _AN7581_AFE_COMMON_H_

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#include "../../mediatek/common/mtk-base-afe.h"

enum {
	AN7581_MEMIF_DL1,
	AN7581_MEMIF_UL1,
	AN7581_MEMIF_NUM,
	AN7581_DAI_ETDM = AN7581_MEMIF_NUM,
	AN7581_DAI_NUM,
};

enum {
	AN7581_IRQ_0,
	AN7581_IRQ_1,
	AN7581_IRQ_NUM,
};

struct an7581_memif_irq_desc {
	u8 irq;
	u32 status_bit;
	u32 clear_reg;
};

struct an7581_afe_private {
	unsigned int users;
	/* protect concurrent ETDM user tracking */
	struct mutex user_lock;

	void *dai_priv[AN7581_DAI_NUM];
};

unsigned int an7581_afe_rate_transform(struct device *dev,
				       unsigned int rate);

int an7581_dai_etdm_register(struct mtk_base_afe *afe);

#endif
