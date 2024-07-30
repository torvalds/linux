/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for Cirrus Logic CS35L56 smart amp
 *
 * Copyright (C) 2023 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#ifndef CS35L56_H
#define CS35L56_H

#include <linux/completion.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/workqueue.h>
#include <sound/cs35l56.h>
#include "wm_adsp.h"

#define CS35L56_SDW_GEN_INT_STAT_1	0xc0
#define CS35L56_SDW_GEN_INT_MASK_1	0xc1
#define CS35L56_SDW_INT_MASK_CODEC_IRQ	BIT(0)

#define CS35L56_SDW_INVALID_BUS_SCALE	0xf

#define CS35L56_RX_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)
#define CS35L56_TX_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE \
			    | SNDRV_PCM_FMTBIT_S32_LE)

#define CS35L56_RATES (SNDRV_PCM_RATE_48000)

struct sdw_slave;

struct cs35l56_private {
	struct wm_adsp dsp; /* must be first member */
	struct cs35l56_base base;
	struct work_struct dsp_work;
	struct workqueue_struct *dsp_wq;
	struct snd_soc_component *component;
	struct regulator_bulk_data supplies[CS35L56_NUM_BULK_SUPPLIES];
	struct sdw_slave *sdw_peripheral;
	struct work_struct sdw_irq_work;
	bool sdw_irq_no_unmask;
	bool soft_resetting;
	bool sdw_attached;
	struct completion init_completion;

	int speaker_id;
	u32 rx_mask;
	u32 tx_mask;
	u8 asp_slot_width;
	u8 asp_slot_count;
	bool tdm_mode;
	bool sysclk_set;
};

extern const struct dev_pm_ops cs35l56_pm_ops_i2c_spi;

int cs35l56_system_suspend(struct device *dev);
int cs35l56_system_suspend_late(struct device *dev);
int cs35l56_system_suspend_no_irq(struct device *dev);
int cs35l56_system_resume_no_irq(struct device *dev);
int cs35l56_system_resume_early(struct device *dev);
int cs35l56_system_resume(struct device *dev);
irqreturn_t cs35l56_irq(int irq, void *data);
int cs35l56_irq_request(struct cs35l56_base *cs35l56_base, int irq);
int cs35l56_common_probe(struct cs35l56_private *cs35l56);
int cs35l56_init(struct cs35l56_private *cs35l56);
void cs35l56_remove(struct cs35l56_private *cs35l56);

#endif /* ifndef CS35L56_H */
