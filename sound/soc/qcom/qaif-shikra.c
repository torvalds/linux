// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * qaif-shikra.c -- ALSA SoC CPU-Platform DAI driver for QTi QAIF
 */

#include <linux/module.h>
#include <linux/pm.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include "qaif.h"

static const struct qaif_dmaidx_dai_map shikra_mi2s_dma_dai_map[] = {
	{ AIF_MI2S_RX_0, QAIF_DMA_IDX0 },
	{ AIF_MI2S_RX_1, QAIF_DMA_IDX1 },
	{ AIF_MI2S_RX_2, QAIF_DMA_IDX2 },
	{ AIF_MI2S_RX_3, QAIF_DMA_IDX3 },
	{ AIF_MI2S_TX_0, QAIF_DMA_IDX0 },
	{ AIF_MI2S_TX_1, QAIF_DMA_IDX1 },
	{ AIF_MI2S_TX_2, QAIF_DMA_IDX2 },
	{ AIF_MI2S_TX_3, QAIF_DMA_IDX3 },
};

static const struct qaif_dmaidx_dai_map shikra_tdm_dma_dai_map[] = {
	{ AIF_TDM_RX_0, QAIF_DMA_IDX0 },
	{ AIF_TDM_RX_1, QAIF_DMA_IDX1 },
	{ AIF_TDM_RX_2, QAIF_DMA_IDX2 },
	{ AIF_TDM_RX_3, QAIF_DMA_IDX3 },
	{ AIF_TDM_TX_0, QAIF_DMA_IDX0 },
	{ AIF_TDM_TX_1, QAIF_DMA_IDX1 },
	{ AIF_TDM_TX_2, QAIF_DMA_IDX2 },
	{ AIF_TDM_TX_3, QAIF_DMA_IDX3 },
};

static const struct qaif_dmaidx_dai_map shikra_cif_dma_dai_map[] = {
	{ RX_CODEC_DMA_RX_0, QAIF_DMA_IDX0 },
	{ RX_CODEC_DMA_RX_1, QAIF_DMA_IDX1 },
	{ VA_CODEC_DMA_TX_0, QAIF_DMA_IDX0 },
	{ VA_CODEC_DMA_TX_1, QAIF_DMA_IDX1 },
};

static struct snd_soc_dai_driver shikra_qaif_cpu_dai_driver[] = {
	{
		.id = AIF_MI2S_RX_0,
		.name = "MI2S AIF RX Zero",
		.playback = {
			.stream_name = "MI2S AIF RX Zero Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = AIF_MI2S_TX_0,
		.name = "MI2S AIF TX Zero",
		.capture = {
			.stream_name = "MI2S AIF TX Zero Capture",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = AIF_TDM_RX_0,
		.name = "TDM AIF RX Zero",
		.playback = {
			.stream_name = "TDM AIF RX Zero Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S24 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = AIF_TDM_TX_0,
		.name = "TDM AIF TX Zero",
		.capture = {
			.stream_name = "TDM AIF TX Zero Capture",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S24 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = AIF_MI2S_RX_1,
		.name = "MI2S AIF RX One",
		.playback = {
			.stream_name = "MI2S AIF RX One Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = AIF_MI2S_TX_1,
		.name = "MI2S AIF TX One",
		.capture = {
			.stream_name = "MI2S AIF TX One Capture",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = AIF_TDM_RX_1,
		.name = "TDM AIF RX One",
		.playback = {
			.stream_name = "TDM AIF RX One Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S24 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = AIF_TDM_TX_1,
		.name = "TDM AIF TX One",
		.capture = {
			.stream_name = "TDM AIF TX One Capture",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S24 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = AIF_MI2S_RX_2,
		.name = "MI2S AIF RX Two",
		.playback = {
			.stream_name = "MI2S AIF RX Two Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = AIF_MI2S_TX_2,
		.name = "MI2S AIF TX Two",
		.capture = {
			.stream_name = "MI2S AIF TX Two Capture",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = AIF_TDM_RX_2,
		.name = "TDM AIF RX Two",
		.playback = {
			.stream_name = "TDM AIF RX Two Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S24 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = AIF_TDM_TX_2,
		.name = "TDM AIF TX Two",
		.capture = {
			.stream_name = "TDM AIF TX Two Capture",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S24 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = AIF_MI2S_RX_3,
		.name = "MI2S AIF RX Three",
		.playback = {
			.stream_name = "MI2S AIF RX Three Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = AIF_MI2S_TX_3,
		.name = "MI2S AIF TX Three",
		.capture = {
			.stream_name = "MI2S AIF TX Three Capture",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = AIF_TDM_RX_3,
		.name = "TDM AIF RX Three",
		.playback = {
			.stream_name = "TDM AIF RX Three Playback",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S24 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = AIF_TDM_TX_3,
		.name = "TDM AIF TX Three",
		.capture = {
			.stream_name = "TDM AIF TX Three Capture",
			.formats	= SNDRV_PCM_FMTBIT_S16 |
					  SNDRV_PCM_FMTBIT_S24 |
					  SNDRV_PCM_FMTBIT_S32,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.channels_min	= 1,
			.channels_max	= 8,
		},
		.ops = &asoc_qcom_qaif_aif_cpu_dai_ops,
	}, {
		.id = RX_CODEC_DMA_RX_0,
		.name = "RX CDC DMA RX0",
		.playback = {
			.stream_name = "RX CDC DMA RX0 Playback",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops	= &asoc_qcom_qaif_cif_dai_ops,
	}, {
		.id = RX_CODEC_DMA_RX_1,
		.name = "RX CDC DMA RX1",
		.playback = {
			.stream_name = "RX CDC DMA RX1 Playback",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops	= &asoc_qcom_qaif_cif_dai_ops,
	}, {
		.id = VA_CODEC_DMA_TX_0,
		.name = "VA CDC DMA TX0",
		.capture = {
			.stream_name = "VA CDC DMA TX0 Capture",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 1,
			.channels_max	= 4,
		},
		.ops	= &asoc_qcom_qaif_cif_dai_ops,
	}, {
		.id = VA_CODEC_DMA_TX_1,
		.name = "VA CDC DMA TX1",
		.capture = {
			.stream_name = "VA CDC DMA TX1 Capture",
			.formats = SNDRV_PCM_FMTBIT_S16,
			.rates = SNDRV_PCM_RATE_48000,
			.rate_min	= 48000,
			.rate_max	= 48000,
			.channels_min	= 1,
			.channels_max	= 4,
		},
		.ops	= &asoc_qcom_qaif_cif_dai_ops,
	},
};

static int shikra_qaif_get_dma_idx(unsigned int dai_id)
{
	const struct qaif_dmaidx_dai_map *map;
	int i, size;

	if (dai_id >= AIF_MI2S_RX_0 && dai_id <= AIF_MI2S_TX_12) {
		map = shikra_mi2s_dma_dai_map;
		size = ARRAY_SIZE(shikra_mi2s_dma_dai_map);
	} else if (dai_id >= AIF_TDM_RX_0 && dai_id <= AIF_TDM_TX_12) {
		map = shikra_tdm_dma_dai_map;
		size = ARRAY_SIZE(shikra_tdm_dma_dai_map);
	} else {
		map = shikra_cif_dma_dai_map;
		size = ARRAY_SIZE(shikra_cif_dma_dai_map);
	}

	for (i = 0; i < size; i++) {
		if (map[i].dai_id == dai_id)
			return map[i].dma_idx;
	}
	return -EINVAL;
}

static int shikra_qaif_alloc_stream_dma_idx(struct qaif_drv_data *drvdata,
					    int direction,
					    unsigned int dai_id)
{
	const struct qaif_variant *v = drvdata->variant;
	int dma_idx, index;

	dma_idx = shikra_qaif_get_dma_idx(dai_id);
	if (dma_idx < 0)
		return dma_idx;

	if (qaif_is_aif_port(dai_id)) {
		index = (direction == SNDRV_PCM_STREAM_PLAYBACK) ?
			dma_idx : v->wrdma_start + dma_idx;
		if (index >= QAIF_MAX_AIF_DMA_IDX)
			return -EINVAL;
		if (test_and_set_bit(index, &drvdata->aif_dma_idx_bit_map))
			return -EBUSY;
	} else if (qaif_is_cif_port(dai_id)) {
		index = (direction == SNDRV_PCM_STREAM_PLAYBACK) ?
			dma_idx : v->codec_wrdma_start + dma_idx;
		if (index >= QAIF_MAX_CIF_DMA_IDX)
			return -EINVAL;
		if (test_and_set_bit(index, &drvdata->cif_dma_idx_bit_map))
			return -EBUSY;
	} else {
		return -EINVAL;
	}

	return index;
}

static int shikra_qaif_free_stream_dma_idx(struct qaif_drv_data *drvdata,
					   int index,
					   unsigned int dai_id)
{
	if (qaif_is_aif_port(dai_id))
		clear_bit(index, &drvdata->aif_dma_idx_bit_map);
	else
		clear_bit(index, &drvdata->cif_dma_idx_bit_map);
	return 0;
}

static const struct qaif_variant shikra_qaif_data = {
	.ee = 0,

	.num_rddma = 4,
	.num_wrdma = 4,
	.wrdma_start = 4,

	.num_codec_rddma = 4,
	.num_codec_wrdma = 4,
	.codec_wrdma_start = 4,
	.num_intf = 4,

	.rddma_reg_base = 0x8000,
	.rddma_stride = 0x1000,
	.codec_rddma_reg_base = 0xc000,
	.codec_rddma_stride = 0x1000,

	.wrdma_reg_base = 0x11000,
	.wrdma_stride = 0x1000,
	.codec_wrdma_reg_base = 0x15000,
	.codec_wrdma_stride = 0x1000,

	.rddma_irq_reg_base = 0x19000,
	.rddma_irq_stride = 0x1000,
	.codec_rddma_irq_reg_base = 0x191A0,
	.codec_rddma_irq_stride = 0x1000,

	.wrdma_irq_reg_base = 0x19078,
	.wrdma_irq_stride = 0x1000,
	.codec_wrdma_irq_reg_base = 0x19290,
	.codec_wrdma_irq_stride = 0x1000,

	.qxm_type = QAIF_QXM0,
	.rddma_shram_len = 64,
	.rddma_shram_start_addr = {0, 256},
	.wrdma_shram_len = 64,
	.wrdma_shram_start_addr = {0, 256},

	.clk_name		= (const char * const []) {
					"lpass_config",
					"lpass_core_axim",
					"bus",
				},
	.num_clks		= 3,

	.dai_driver		= shikra_qaif_cpu_dai_driver,
	.num_dai		= ARRAY_SIZE(shikra_qaif_cpu_dai_driver),

	.dai_bit_clk_names	= (const char * const []) {
					"aif_if0_ibit",
					"aif_if1_ibit",
					"aif_if2_ibit",
					"aif_if3_ibit",
				},
	.aif_full_cycle_en	= (const bool []) { false, false, false, false },
	.aif_ctrl_data_oe	= (const bool []) { false, false, false, false },
	.aif_loopback_en	= (const bool []) { false, false, false, false },
	.alloc_stream_dma_idx	= shikra_qaif_alloc_stream_dma_idx,
	.free_stream_dma_idx	= shikra_qaif_free_stream_dma_idx,
	.get_dma_idx		= shikra_qaif_get_dma_idx,
};

static const struct of_device_id shikra_qaif_cpu_device_id[] = {
	{ .compatible = "qcom,shikra-qaif-cpu", .data = &shikra_qaif_data },
	{}
};
MODULE_DEVICE_TABLE(of, shikra_qaif_cpu_device_id);

static struct platform_driver shikra_qaif_cpu_platform_driver = {
	.driver = {
		.name = "shikra-qaif-cpu",
		.of_match_table = shikra_qaif_cpu_device_id,
		.pm = pm_ptr(&asoc_qcom_qaif_pm_ops),
	},
	.probe = asoc_qcom_qaif_cpu_platform_probe,
};
module_platform_driver(shikra_qaif_cpu_platform_driver);

MODULE_DESCRIPTION("Qualcomm Audio Interface (QAIF) Shikra variant driver");
MODULE_AUTHOR("Harendra Gautam <harendra.gautam@oss.qualcomm.com>");
MODULE_LICENSE("GPL");
