/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011,2013-2015 The Linux Foundation. All rights reserved.
 *
 * lpass.h - Definitions for the QTi LPASS
 */

#ifndef __LPASS_H__
#define __LPASS_H__

#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define LPASS_AHBIX_CLOCK_FREQUENCY		131072000
#define LPASS_MAX_MI2S_PORTS			(8)
#define LPASS_MAX_DMA_CHANNELS			(8)

struct lpaif_i2sctl {
	struct regmap_field *loopback;
	struct regmap_field *spken;
	struct regmap_field *spkmode;
	struct regmap_field *spkmono;
	struct regmap_field *micen;
	struct regmap_field *micmode;
	struct regmap_field *micmono;
	struct regmap_field *wssrc;
	struct regmap_field *bitwidth;
};


struct lpaif_dmactl {
	struct regmap_field *intf;
	struct regmap_field *bursten;
	struct regmap_field *wpscnt;
	struct regmap_field *fifowm;
	struct regmap_field *enable;
	struct regmap_field *dyncclk;
};

/* Both the CPU DAI and platform drivers will access this data */
struct lpass_data {

	/* AHB-I/X bus clocks inside the low-power audio subsystem (LPASS) */
	struct clk *ahbix_clk;

	/* MI2S system clock */
	struct clk *mi2s_osr_clk[LPASS_MAX_MI2S_PORTS];

	/* MI2S bit clock (derived from system clock by a divider */
	struct clk *mi2s_bit_clk[LPASS_MAX_MI2S_PORTS];

	/* MI2S SD lines to use for playback/capture */
	unsigned int mi2s_playback_sd_mode[LPASS_MAX_MI2S_PORTS];
	unsigned int mi2s_capture_sd_mode[LPASS_MAX_MI2S_PORTS];

	/* low-power audio interface (LPAIF) registers */
	void __iomem *lpaif;

	/* regmap backed by the low-power audio interface (LPAIF) registers */
	struct regmap *lpaif_map;

	/* interrupts from the low-power audio interface (LPAIF) */
	int lpaif_irq;

	/* SOC specific variations in the LPASS IP integration */
	struct lpass_variant *variant;

	/* bit map to keep track of static channel allocations */
	unsigned long dma_ch_bit_map;

	/* used it for handling interrupt per dma channel */
	struct snd_pcm_substream *substream[LPASS_MAX_DMA_CHANNELS];

	/* SOC specific clock list */
	struct clk_bulk_data *clks;
	int num_clks;

	/* Regmap fields of I2SCTL & DMACTL registers bitfields */
	struct lpaif_i2sctl *i2sctl;
	struct lpaif_dmactl *rd_dmactl;
	struct lpaif_dmactl *wr_dmactl;
};

/* Vairant data per each SOC */
struct lpass_variant {
	u32	i2sctrl_reg_base;
	u32	i2sctrl_reg_stride;
	u32	i2s_ports;
	u32	irq_reg_base;
	u32	irq_reg_stride;
	u32	irq_ports;
	u32	rdma_reg_base;
	u32	rdma_reg_stride;
	u32	rdma_channels;
	u32	wrdma_reg_base;
	u32	wrdma_reg_stride;
	u32	wrdma_channels;

	/* I2SCTL Register fields */
	struct reg_field loopback;
	struct reg_field spken;
	struct reg_field spkmode;
	struct reg_field spkmono;
	struct reg_field micen;
	struct reg_field micmode;
	struct reg_field micmono;
	struct reg_field wssrc;
	struct reg_field bitwidth;

	/* RD_DMA Register fields */
	struct reg_field rdma_intf;
	struct reg_field rdma_bursten;
	struct reg_field rdma_wpscnt;
	struct reg_field rdma_fifowm;
	struct reg_field rdma_enable;
	struct reg_field rdma_dyncclk;

	/* WR_DMA Register fields */
	struct reg_field wrdma_intf;
	struct reg_field wrdma_bursten;
	struct reg_field wrdma_wpscnt;
	struct reg_field wrdma_fifowm;
	struct reg_field wrdma_enable;
	struct reg_field wrdma_dyncclk;

	/**
	 * on SOCs like APQ8016 the channel control bits start
	 * at different offset to ipq806x
	 **/
	u32	dmactl_audif_start;
	u32	wrdma_channel_start;
	/* SOC specific initialization like clocks */
	int (*init)(struct platform_device *pdev);
	int (*exit)(struct platform_device *pdev);
	int (*alloc_dma_channel)(struct lpass_data *data, int direction);
	int (*free_dma_channel)(struct lpass_data *data, int ch);

	/* SOC specific dais */
	struct snd_soc_dai_driver *dai_driver;
	int num_dai;
	const char * const *dai_osr_clk_names;
	const char * const *dai_bit_clk_names;

	/* SOC specific clocks configuration */
	const char **clk_name;
	int num_clks;
};

/* register the platform driver from the CPU DAI driver */
int asoc_qcom_lpass_platform_register(struct platform_device *);
int asoc_qcom_lpass_cpu_platform_remove(struct platform_device *pdev);
int asoc_qcom_lpass_cpu_platform_probe(struct platform_device *pdev);
int asoc_qcom_lpass_cpu_dai_probe(struct snd_soc_dai *dai);
extern const struct snd_soc_dai_ops asoc_qcom_lpass_cpu_dai_ops;

#endif /* __LPASS_H__ */
