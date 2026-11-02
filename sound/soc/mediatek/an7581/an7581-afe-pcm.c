// SPDX-License-Identifier: GPL-2.0
/*
 * Airoha ALSA SoC AFE platform driver for AN7581
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include "an7581-afe-common.h"
#include "an7581-reg.h"
#include "../common/mtk-afe-platform-driver.h"
#include "../common/mtk-afe-fe-dai.h"

enum {
	ARH_AFE_RATE_8K = 0,
	ARH_AFE_RATE_12K = 1,
	ARH_AFE_RATE_16K = 2,
	ARH_AFE_RATE_24K = 3,
	ARH_AFE_RATE_32K = 4,
	ARH_AFE_RATE_48K = 5,
	ARH_AFE_RATE_96K = 6,
	ARH_AFE_RATE_192K = 7,
	ARH_AFE_RATE_384K = 8,
	ARH_AFE_RATE_7K = 16,
	ARH_AFE_RATE_11K = 17,
	ARH_AFE_RATE_14K = 18,
	ARH_AFE_RATE_22K = 19,
	ARH_AFE_RATE_29K = 20,
	ARH_AFE_RATE_44K = 21,
	ARH_AFE_RATE_88K = 22,
	ARH_AFE_RATE_176K = 23,
	ARH_AFE_RATE_352K = 24,
};

unsigned int an7581_afe_rate_transform(struct device *dev, unsigned int rate)
{
	switch (rate) {
	case 7350:
		return ARH_AFE_RATE_7K;
	case 8000:
		return ARH_AFE_RATE_8K;
	case 11025:
		return ARH_AFE_RATE_11K;
	case 12000:
		return ARH_AFE_RATE_12K;
	case 14700:
		return ARH_AFE_RATE_14K;
	case 16000:
		return ARH_AFE_RATE_16K;
	case 22050:
		return ARH_AFE_RATE_22K;
	case 24000:
		return ARH_AFE_RATE_24K;
	case 29400:
		return ARH_AFE_RATE_29K;
	case 32000:
		return ARH_AFE_RATE_32K;
	case 44100:
		return ARH_AFE_RATE_44K;
	case 48000:
		return ARH_AFE_RATE_48K;
	case 88200:
		return ARH_AFE_RATE_88K;
	case 96000:
		return ARH_AFE_RATE_96K;
	case 176400:
		return ARH_AFE_RATE_176K;
	case 192000:
		return ARH_AFE_RATE_192K;
	case 352800:
		return ARH_AFE_RATE_352K;
	case 384000:
		return ARH_AFE_RATE_384K;
	default:
		dev_warn_ratelimited(dev, "%s(), rate %u invalid, using %d!\n",
				     __func__, rate, ARH_AFE_RATE_48K);
		return ARH_AFE_RATE_48K;
	}
}

static const struct an7581_memif_irq_desc an7581_memif_irq_descs[AN7581_MEMIF_NUM] = {
	[AN7581_MEMIF_DL1] = {
		.irq = AN7581_IRQ_0,
		.status_bit = AFE_IRQ_STS_PLAY,
		.clear_reg = AFE_IRQ_CON0,
	},
	[AN7581_MEMIF_UL1] = {
		.irq = AN7581_IRQ_1,
		.status_bit = AFE_IRQ_STS_RECORD,
		.clear_reg = AFE_IRQ1_CON0,
	},
};

static const struct snd_pcm_hardware an7581_afe_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		   SNDRV_PCM_FMTBIT_S24_LE |
		   SNDRV_PCM_FMTBIT_S32_LE,
	.period_bytes_min = 512,
	.period_bytes_max = 128 * 1024,
	.periods_min = 2,
	.periods_max = 256,
	.buffer_bytes_max = 256 * 1024,
};

static int an7581_memif_irq_fs(struct snd_pcm_substream *substream, unsigned int rate)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);

	return an7581_afe_rate_transform(afe->dev, rate);
}

static int an7581_afe_fe_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	int ret;

	ret = mtk_afe_fe_startup(substream, dai);
	if (ret < 0)
		return ret;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		ret = snd_pcm_hw_constraint_minmax(runtime,
						   SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
						   0x2000, UINT_MAX);
		if (ret < 0)
			dev_err(afe->dev, "hw_constraint_minmax failed\n");
	}

	return ret;
}

static const struct snd_soc_dai_ops an7581_afe_fe_ops = {
	.startup	= an7581_afe_fe_startup,
	.shutdown	= mtk_afe_fe_shutdown,
	.hw_params	= mtk_afe_fe_hw_params,
	.hw_free	= mtk_afe_fe_hw_free,
	.prepare	= mtk_afe_fe_prepare,
	.trigger	= mtk_afe_fe_trigger,
};

#define ARH_PCM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver an7581_memif_dai_driver[] = {
	/* FE DAIs: memory intefaces to CPU */
	{
		.name = "DL1",
		.id = AN7581_MEMIF_DL1,
		.playback = {
			.stream_name = "DL1",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = ARH_PCM_FORMATS,
		},
		.ops = &an7581_afe_fe_ops,
	},
	{
		.name = "UL1",
		.id = AN7581_MEMIF_UL1,
		.capture = {
			.stream_name = "UL1",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = ARH_PCM_FORMATS,
		},
		.ops = &an7581_afe_fe_ops,
	},
};

static const struct snd_soc_dapm_widget an7581_memif_widgets[] = {
	/* DL */
	SND_SOC_DAPM_MIXER("I032", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I033", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* UL */
	SND_SOC_DAPM_MIXER("O018", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("O019", SND_SOC_NOPM, 0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route an7581_memif_routes[] = {
	{"I032", NULL, "DL1"},
	{"I033", NULL, "DL1"},
	{"UL1", NULL, "O018"},
	{"UL1", NULL, "O019"},
	{"O018", NULL, "I150"},
	{"O019", NULL, "I151"},
};

static const struct snd_soc_component_driver an7581_afe_pcm_dai_component = {
	.name = "an7581-afe-pcm-dai",
};

static const struct mtk_base_memif_data memif_data[AN7581_MEMIF_NUM] = {
	[AN7581_MEMIF_DL1] = {
		.name = "DL1",
		.id = AN7581_MEMIF_DL1,
		.reg_ofs_base = AFE_DL1_BASE,
		.reg_ofs_cur = AFE_DL1_CUR,
		.reg_ofs_end = AFE_DL1_END,
		.fs_reg = -1,
		.fs_shift = -1,
		.fs_maskbit = -1,
		.mono_reg = -1,
		.mono_shift = -1,
		.hd_reg = AFE_DL1_CON0,
		.hd_shift = AFE_HD_SHIFT,
		.hd_align_reg = -1,
		.hd_align_mshift = -1,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = AFE_DL1_ENABLE_SHIFT,
		.msb_reg = -1,
		.msb_shift = -1,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
	},
	[AN7581_MEMIF_UL1] = {
		.name = "UL1",
		.id = AN7581_MEMIF_UL1,
		.reg_ofs_base = AFE_UL1_BASE,
		.reg_ofs_cur = AFE_UL1_CUR,
		.reg_ofs_end = AFE_UL1_END,
		.fs_reg = -1,
		.fs_shift = -1,
		.fs_maskbit = -1,
		.mono_reg = -1,
		.mono_shift = -1,
		.hd_reg = AFE_UL1_CON0,
		.hd_shift = AFE_HD_SHIFT,
		.hd_align_reg = AFE_UL1_CON0,
		.hd_align_mshift = AFE_HD_ALIGN_SHIFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = AFE_UL1_ENABLE_SHIFT,
		.msb_reg = -1,
		.msb_shift = -1,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
	},
};

static const struct mtk_base_irq_data irq_data[AN7581_IRQ_NUM] = {
	[AN7581_IRQ_0] = {
		.id = AN7581_IRQ_0,
		.irq_cnt_reg = AFE_IRQ_CNT,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_en_reg = AFE_IRQ_CON0,
		.irq_en_shift = AFE_IRQ_ON_SHIFT,
		.irq_fs_reg = -1,
		.irq_fs_shift = -1,
		.irq_fs_maskbit = -1,
		.irq_clr_reg = -1,
		.irq_clr_shift = -1,
	},
	[AN7581_IRQ_1] = {
		.id = AN7581_IRQ_1,
		.irq_cnt_reg = AFE_IRQ1_CNT,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_en_reg = AFE_IRQ1_CON0,
		.irq_en_shift = AFE_IRQ_ON_SHIFT,
		.irq_fs_reg = -1,
		.irq_fs_shift = -1,
		.irq_fs_maskbit = -1,
		.irq_clr_reg = -1,
		.irq_clr_shift = -1,
	},
};

static const struct regmap_config an7581_afe_regmap_config = {
	.name = "afe",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = AFE_MAX_REGISTER,
};

static const struct regmap_config an7581_afe_irq1_regmap_config = {
	.name = "afe_irq1",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = AFE_IRQ1_MAX_REGISTER,
};

static irqreturn_t an7581_afe_irq_handler(int irq_id, void *dev)
{
	const struct an7581_memif_irq_desc *irq_desc;
	struct mtk_base_afe *afe = dev;
	u32 status = 0;
	int i;

	regmap_read(afe->regmap, AFE_IRQ_STS, &status);

	for (i = 0; i < AN7581_MEMIF_NUM; i++) {
		struct mtk_base_afe_memif *memif = &afe->memif[i];

		if (!memif->substream)
			continue;

		if (memif->irq_usage < 0)
			continue;

		irq_desc = &an7581_memif_irq_descs[i];
		if (status & irq_desc->status_bit)
			snd_pcm_period_elapsed(memif->substream);
	}

	for (i = 0; i < AN7581_MEMIF_NUM; i++) {
		struct regmap *irq_regmap;

		irq_desc = &an7581_memif_irq_descs[i];
		if (!(status & irq_desc->status_bit))
			continue;

		irq_regmap = afe->irqs[irq_desc->irq].regmap;
		regmap_set_bits(irq_regmap, irq_desc->clear_reg,
				BIT(AFE_IRQ_CLR_SHIFT));
		regmap_clear_bits(irq_regmap, irq_desc->clear_reg,
				  BIT(AFE_IRQ_CLR_SHIFT));

		regmap_set_bits(irq_regmap, irq_desc->clear_reg,
				BIT(AFE_IRQ_MISS_FLG_CLR_SHIFT));
		regmap_clear_bits(irq_regmap, irq_desc->clear_reg,
				  BIT(AFE_IRQ_MISS_FLG_CLR_SHIFT));
	}

	return IRQ_HANDLED;
}

static int an7581_dai_memif_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = an7581_memif_dai_driver;
	dai->num_dai_drivers = ARRAY_SIZE(an7581_memif_dai_driver);

	dai->dapm_widgets = an7581_memif_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(an7581_memif_widgets);
	dai->dapm_routes = an7581_memif_routes;
	dai->num_dapm_routes = ARRAY_SIZE(an7581_memif_routes);

	return 0;
}

typedef int (*dai_register_cb)(struct mtk_base_afe *);
static const dai_register_cb dai_register_cbs[] = {
	an7581_dai_etdm_register,
	an7581_dai_memif_register,
};

static int an7581_afe_pcm_dev_probe(struct platform_device *pdev)
{
	struct an7581_afe_private *afe_priv;
	struct device *dev = &pdev->dev;
	struct reset_control *reset;
	struct mtk_base_afe *afe;
	int i, irq_id, ret;
	void __iomem *base;

	afe = devm_kzalloc(dev, sizeof(*afe), GFP_KERNEL);
	if (!afe)
		return -ENOMEM;
	platform_set_drvdata(pdev, afe);

	afe->platform_priv = devm_kzalloc(dev, sizeof(*afe_priv),
					  GFP_KERNEL);
	if (!afe->platform_priv)
		return -ENOMEM;

	afe->irqs_size = AN7581_IRQ_NUM;
	afe->irqs = devm_kcalloc(dev, afe->irqs_size, sizeof(*afe->irqs),
				 GFP_KERNEL);
	if (!afe->irqs)
		return -ENOMEM;

	afe->memif_size = AN7581_MEMIF_NUM;
	afe->memif = devm_kcalloc(dev, afe->memif_size, sizeof(*afe->memif),
				  GFP_KERNEL);
	if (!afe->memif)
		return -ENOMEM;

	afe_priv = afe->platform_priv;
	mutex_init(&afe_priv->user_lock);
	afe->dev = &pdev->dev;

	reset = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(reset))
		return PTR_ERR(reset);

	/* Global reset I2S */
	reset_control_assert(reset);
	usleep_range(10, 20);
	reset_control_deassert(reset);

	afe->base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(afe->base_addr))
		return PTR_ERR(afe->base_addr);

	base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(base))
		return PTR_ERR(base);

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	afe->regmap = devm_regmap_init_mmio(&pdev->dev, afe->base_addr,
					    &an7581_afe_regmap_config);
	if (IS_ERR(afe->regmap))
		return PTR_ERR(afe->regmap);

	mutex_init(&afe->irq_alloc_lock);

	/* irq initialize */
	for (i = 0; i < afe->irqs_size; i++)
		afe->irqs[i].irq_data = &irq_data[i];

	afe->irqs[AN7581_IRQ_0].regmap = afe->regmap;
	afe->irqs[AN7581_IRQ_1].regmap = devm_regmap_init_mmio(&pdev->dev, base,
							       &an7581_afe_irq1_regmap_config);
	if (IS_ERR(afe->irqs[AN7581_IRQ_1].regmap))
		return PTR_ERR(afe->irqs[AN7581_IRQ_1].regmap);

	/* request irq */
	irq_id = platform_get_irq(pdev, 0);
	if (irq_id < 0)
		return irq_id;

	/* init memif */
	for (i = 0; i < afe->memif_size; i++) {
		int sel_irq = an7581_memif_irq_descs[i].irq;

		afe->memif[i].data = &memif_data[i];
		afe->memif[i].irq_usage = sel_irq;
		afe->memif[i].const_irq = 1;
		afe->irqs[sel_irq].irq_occupyed = true;
	}

	/* init sub_dais */
	INIT_LIST_HEAD(&afe->sub_dais);

	ret = devm_request_irq(dev, irq_id, an7581_afe_irq_handler,
			       IRQF_TRIGGER_NONE, "asys-isr", (void *)afe);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request irq for asys-isr\n");

	for (i = 0; i < ARRAY_SIZE(dai_register_cbs); i++) {
		ret = dai_register_cbs[i](afe);
		if (ret)
			return dev_err_probe(dev, ret, "DAI register failed, i: %d\n", i);
	}

	/* init dai_driver and component_driver */
	ret = mtk_afe_combine_sub_dai(afe);
	if (ret)
		return dev_err_probe(dev, ret, "mtk_afe_combine_sub_dai fail\n");

	afe->mtk_afe_hardware = &an7581_afe_hardware;
	afe->memif_fs = an7581_memif_irq_fs;
	afe->irq_fs = an7581_memif_irq_fs;

	/* register component */
	ret = devm_snd_soc_register_component(&pdev->dev,
					      &mtk_afe_pcm_platform,
					      NULL, 0);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot register AFE component\n");

	ret = devm_snd_soc_register_component(afe->dev,
					      &an7581_afe_pcm_dai_component,
					      afe->dai_drivers,
					      afe->num_dai_drivers);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot register PCM DAI component\n");

	return 0;
}

static const struct of_device_id an7581_afe_pcm_dt_match[] = {
	{ .compatible = "airoha,an7581-afe" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, an7581_afe_pcm_dt_match);

static struct platform_driver an7581_afe_pcm_driver = {
	.driver = {
		   .name = "an7581-audio",
		   .of_match_table = an7581_afe_pcm_dt_match,
	},
	.probe = an7581_afe_pcm_dev_probe,
};
module_platform_driver(an7581_afe_pcm_driver);

MODULE_DESCRIPTION("Airoha SoC AFE platform driver for ALSA AN7581");
MODULE_LICENSE("GPL");
