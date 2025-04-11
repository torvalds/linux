// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek ALSA SoC AFE platform driver for MT7986
 *
 * Copyright (c) 2023 MediaTek Inc.
 * Authors: Vic Wu <vic.wu@mediatek.com>
 *          Maso Huang <maso.huang@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>

#include "mt7986-afe-common.h"
#include "mt7986-reg.h"
#include "../common/mtk-afe-platform-driver.h"
#include "../common/mtk-afe-fe-dai.h"

enum {
	MTK_AFE_RATE_8K = 0,
	MTK_AFE_RATE_11K = 1,
	MTK_AFE_RATE_12K = 2,
	MTK_AFE_RATE_16K = 4,
	MTK_AFE_RATE_22K = 5,
	MTK_AFE_RATE_24K = 6,
	MTK_AFE_RATE_32K = 8,
	MTK_AFE_RATE_44K = 9,
	MTK_AFE_RATE_48K = 10,
	MTK_AFE_RATE_88K = 13,
	MTK_AFE_RATE_96K = 14,
	MTK_AFE_RATE_176K = 17,
	MTK_AFE_RATE_192K = 18,
};

enum {
	CLK_INFRA_AUD_BUS_CK = 0,
	CLK_INFRA_AUD_26M_CK,
	CLK_INFRA_AUD_L_CK,
	CLK_INFRA_AUD_AUD_CK,
	CLK_INFRA_AUD_EG2_CK,
	CLK_NUM
};

static const char *aud_clks[CLK_NUM] = {
	[CLK_INFRA_AUD_BUS_CK] = "aud_bus_ck",
	[CLK_INFRA_AUD_26M_CK] = "aud_26m_ck",
	[CLK_INFRA_AUD_L_CK] = "aud_l_ck",
	[CLK_INFRA_AUD_AUD_CK] = "aud_aud_ck",
	[CLK_INFRA_AUD_EG2_CK] = "aud_eg2_ck",
};

unsigned int mt7986_afe_rate_transform(struct device *dev, unsigned int rate)
{
	switch (rate) {
	case 8000:
		return MTK_AFE_RATE_8K;
	case 11025:
		return MTK_AFE_RATE_11K;
	case 12000:
		return MTK_AFE_RATE_12K;
	case 16000:
		return MTK_AFE_RATE_16K;
	case 22050:
		return MTK_AFE_RATE_22K;
	case 24000:
		return MTK_AFE_RATE_24K;
	case 32000:
		return MTK_AFE_RATE_32K;
	case 44100:
		return MTK_AFE_RATE_44K;
	case 48000:
		return MTK_AFE_RATE_48K;
	case 88200:
		return MTK_AFE_RATE_88K;
	case 96000:
		return MTK_AFE_RATE_96K;
	case 176400:
		return MTK_AFE_RATE_176K;
	case 192000:
		return MTK_AFE_RATE_192K;
	default:
		dev_warn(dev, "%s(), rate %u invalid, using %d!!!\n",
			 __func__, rate, MTK_AFE_RATE_48K);
		return MTK_AFE_RATE_48K;
	}
}

static const struct snd_pcm_hardware mt7986_afe_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		   SNDRV_PCM_FMTBIT_S24_LE |
		   SNDRV_PCM_FMTBIT_S32_LE,
	.period_bytes_min = 256,
	.period_bytes_max = 4 * 48 * 1024,
	.periods_min = 2,
	.periods_max = 256,
	.buffer_bytes_max = 8 * 48 * 1024,
	.fifo_size = 0,
};

static int mt7986_memif_fs(struct snd_pcm_substream *substream,
			   unsigned int rate)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);

	return mt7986_afe_rate_transform(afe->dev, rate);
}

static int mt7986_irq_fs(struct snd_pcm_substream *substream,
			 unsigned int rate)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);

	return mt7986_afe_rate_transform(afe->dev, rate);
}

#define MTK_PCM_RATES (SNDRV_PCM_RATE_8000_48000 |\
		       SNDRV_PCM_RATE_88200 |\
		       SNDRV_PCM_RATE_96000 |\
		       SNDRV_PCM_RATE_176400 |\
		       SNDRV_PCM_RATE_192000)

#define MTK_PCM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mt7986_memif_dai_driver[] = {
	/* FE DAIs: memory intefaces to CPU */
	{
		.name = "DL1",
		.id = MT7986_MEMIF_DL1,
		.playback = {
			.stream_name = "DL1",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "UL1",
		.id = MT7986_MEMIF_VUL12,
		.capture = {
			.stream_name = "UL1",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
};

static const struct snd_kcontrol_new o018_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I150_Switch", AFE_CONN018_4, 22, 1, 0),
};

static const struct snd_kcontrol_new o019_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I151_Switch", AFE_CONN019_4, 23, 1, 0),
};

static const struct snd_soc_dapm_widget mt7986_memif_widgets[] = {
	/* DL */
	SND_SOC_DAPM_MIXER("I032", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I033", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* UL */
	SND_SOC_DAPM_MIXER("O018", SND_SOC_NOPM, 0, 0,
			   o018_mix, ARRAY_SIZE(o018_mix)),
	SND_SOC_DAPM_MIXER("O019", SND_SOC_NOPM, 0, 0,
			   o019_mix, ARRAY_SIZE(o019_mix)),
};

static const struct snd_soc_dapm_route mt7986_memif_routes[] = {
	{"I032", NULL, "DL1"},
	{"I033", NULL, "DL1"},
	{"UL1", NULL, "O018"},
	{"UL1", NULL, "O019"},
	{"O018", "I150_Switch", "I150"},
	{"O019", "I151_Switch", "I151"},
};

static const struct snd_soc_component_driver mt7986_afe_pcm_dai_component = {
	.name = "mt7986-afe-pcm-dai",
};

static const struct mtk_base_memif_data memif_data[MT7986_MEMIF_NUM] = {
	[MT7986_MEMIF_DL1] = {
		.name = "DL1",
		.id = MT7986_MEMIF_DL1,
		.reg_ofs_base = AFE_DL0_BASE,
		.reg_ofs_cur = AFE_DL0_CUR,
		.reg_ofs_end = AFE_DL0_END,
		.reg_ofs_base_msb = AFE_DL0_BASE_MSB,
		.reg_ofs_cur_msb = AFE_DL0_CUR_MSB,
		.reg_ofs_end_msb = AFE_DL0_END_MSB,
		.fs_reg = AFE_DL0_CON0,
		.fs_shift =  DL0_MODE_SFT,
		.fs_maskbit =  DL0_MODE_MASK,
		.mono_reg = AFE_DL0_CON0,
		.mono_shift = DL0_MONO_SFT,
		.enable_reg = AFE_DL0_CON0,
		.enable_shift = DL0_ON_SFT,
		.hd_reg = AFE_DL0_CON0,
		.hd_shift = DL0_HD_MODE_SFT,
		.hd_align_reg = AFE_DL0_CON0,
		.hd_align_mshift = DL0_HALIGN_SFT,
		.pbuf_reg = AFE_DL0_CON0,
		.pbuf_shift = DL0_PBUF_SIZE_SFT,
		.minlen_reg = AFE_DL0_CON0,
		.minlen_shift = DL0_MINLEN_SFT,
	},
	[MT7986_MEMIF_VUL12] = {
		.name = "VUL12",
		.id = MT7986_MEMIF_VUL12,
		.reg_ofs_base = AFE_VUL0_BASE,
		.reg_ofs_cur = AFE_VUL0_CUR,
		.reg_ofs_end = AFE_VUL0_END,
		.reg_ofs_base_msb = AFE_VUL0_BASE_MSB,
		.reg_ofs_cur_msb = AFE_VUL0_CUR_MSB,
		.reg_ofs_end_msb = AFE_VUL0_END_MSB,
		.fs_reg = AFE_VUL0_CON0,
		.fs_shift = VUL0_MODE_SFT,
		.fs_maskbit = VUL0_MODE_MASK,
		.mono_reg = AFE_VUL0_CON0,
		.mono_shift = VUL0_MONO_SFT,
		.enable_reg = AFE_VUL0_CON0,
		.enable_shift = VUL0_ON_SFT,
		.hd_reg = AFE_VUL0_CON0,
		.hd_shift = VUL0_HD_MODE_SFT,
		.hd_align_reg = AFE_VUL0_CON0,
		.hd_align_mshift = VUL0_HALIGN_SFT,
	},
};

static const struct mtk_base_irq_data irq_data[MT7986_IRQ_NUM] = {
	[MT7986_IRQ_0] = {
		.id = MT7986_IRQ_0,
		.irq_cnt_reg = AFE_IRQ0_MCU_CFG1,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ0_MCU_CFG0,
		.irq_fs_shift = IRQ_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ0_MCU_CFG0,
		.irq_en_shift = IRQ_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ0_MCU_CLR_SFT,
	},
	[MT7986_IRQ_1] = {
		.id = MT7986_IRQ_1,
		.irq_cnt_reg = AFE_IRQ1_MCU_CFG1,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ1_MCU_CFG0,
		.irq_fs_shift = IRQ_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ1_MCU_CFG0,
		.irq_en_shift = IRQ_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ1_MCU_CLR_SFT,
	},
	[MT7986_IRQ_2] = {
		.id = MT7986_IRQ_2,
		.irq_cnt_reg = AFE_IRQ2_MCU_CFG1,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ2_MCU_CFG0,
		.irq_fs_shift = IRQ_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ2_MCU_CFG0,
		.irq_en_shift = IRQ_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ2_MCU_CLR_SFT,
	},
};

static bool mt7986_is_volatile_reg(struct device *dev, unsigned int reg)
{
	/*
	 * Those auto-gen regs are read-only, so put it as volatile because
	 * volatile registers cannot be cached, which means that they cannot
	 * be set when power is off
	 */

	switch (reg) {
	case AFE_DL0_CUR_MSB:
	case AFE_DL0_CUR:
	case AFE_DL0_RCH_MON:
	case AFE_DL0_LCH_MON:
	case AFE_VUL0_CUR_MSB:
	case AFE_VUL0_CUR:
	case AFE_IRQ_MCU_STATUS:
	case AFE_MEMIF_RD_MON:
	case AFE_MEMIF_WR_MON:
		return true;
	default:
		return false;
	};
}

static const struct regmap_config mt7986_afe_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.volatile_reg = mt7986_is_volatile_reg,
	.max_register = AFE_MAX_REGISTER,
	.num_reg_defaults_raw = ((AFE_MAX_REGISTER / 4) + 1),
};

static int mt7986_init_clock(struct mtk_base_afe *afe)
{
	struct mt7986_afe_private *afe_priv = afe->platform_priv;
	int ret, i;

	afe_priv->clks = devm_kcalloc(afe->dev, CLK_NUM,
				sizeof(*afe_priv->clks), GFP_KERNEL);
	if (!afe_priv->clks)
		return -ENOMEM;
	afe_priv->num_clks = CLK_NUM;

	for (i = 0; i < afe_priv->num_clks; i++)
		afe_priv->clks[i].id = aud_clks[i];

	ret = devm_clk_bulk_get(afe->dev, afe_priv->num_clks, afe_priv->clks);
	if (ret)
		return dev_err_probe(afe->dev, ret, "Failed to get clocks\n");

	return 0;
}

static irqreturn_t mt7986_afe_irq_handler(int irq_id, void *dev)
{
	struct mtk_base_afe *afe = dev;
	struct mtk_base_afe_irq *irq;
	u32 mcu_en, status, status_mcu;
	int i, ret;
	irqreturn_t irq_ret = IRQ_HANDLED;

	/* get irq that is sent to MCU */
	regmap_read(afe->regmap, AFE_IRQ_MCU_EN, &mcu_en);

	ret = regmap_read(afe->regmap, AFE_IRQ_MCU_STATUS, &status);
	/* only care IRQ which is sent to MCU */
	status_mcu = status & mcu_en & AFE_IRQ_STATUS_BITS;

	if (ret || status_mcu == 0) {
		dev_err(afe->dev, "%s(), irq status err, ret %d, status 0x%x, mcu_en 0x%x\n",
			__func__, ret, status, mcu_en);

		irq_ret = IRQ_NONE;
		goto err_irq;
	}

	for (i = 0; i < MT7986_MEMIF_NUM; i++) {
		struct mtk_base_afe_memif *memif = &afe->memif[i];

		if (!memif->substream)
			continue;

		if (memif->irq_usage < 0)
			continue;

		irq = &afe->irqs[memif->irq_usage];

		if (status_mcu & (1 << irq->irq_data->irq_en_shift))
			snd_pcm_period_elapsed(memif->substream);
	}

err_irq:
	/* clear irq */
	regmap_write(afe->regmap, AFE_IRQ_MCU_CLR, status_mcu);

	return irq_ret;
}

static int mt7986_afe_runtime_suspend(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	struct mt7986_afe_private *afe_priv = afe->platform_priv;

	if (!afe->regmap || afe_priv->pm_runtime_bypass_reg_ctl)
		goto skip_regmap;

	/* disable clk*/
	regmap_update_bits(afe->regmap, AUDIO_TOP_CON4, 0x3fff, 0x3fff);
	regmap_update_bits(afe->regmap, AUDIO_ENGEN_CON0, AUD_APLL2_EN_MASK, 0);
	regmap_update_bits(afe->regmap, AUDIO_ENGEN_CON0, AUD_26M_EN_MASK, 0);

	/* make sure all irq status are cleared, twice intended */
	regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CLR, 0xffff, 0xffff);

skip_regmap:
	clk_bulk_disable_unprepare(afe_priv->num_clks, afe_priv->clks);

	return 0;
}

static int mt7986_afe_runtime_resume(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	struct mt7986_afe_private *afe_priv = afe->platform_priv;
	int ret;

	ret = clk_bulk_prepare_enable(afe_priv->num_clks, afe_priv->clks);
	if (ret)
		return dev_err_probe(afe->dev, ret, "Failed to enable clocks\n");

	if (!afe->regmap || afe_priv->pm_runtime_bypass_reg_ctl)
		return 0;

	/* enable clk*/
	regmap_update_bits(afe->regmap, AUDIO_TOP_CON4, 0x3fff, 0);
	regmap_update_bits(afe->regmap, AUDIO_ENGEN_CON0, AUD_APLL2_EN_MASK,
			   AUD_APLL2_EN);
	regmap_update_bits(afe->regmap, AUDIO_ENGEN_CON0, AUD_26M_EN_MASK,
			   AUD_26M_EN);

	return 0;
}

static int mt7986_dai_memif_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mt7986_memif_dai_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mt7986_memif_dai_driver);

	dai->dapm_widgets = mt7986_memif_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mt7986_memif_widgets);
	dai->dapm_routes = mt7986_memif_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mt7986_memif_routes);

	return 0;
}

typedef int (*dai_register_cb)(struct mtk_base_afe *);
static const dai_register_cb dai_register_cbs[] = {
	mt7986_dai_etdm_register,
	mt7986_dai_memif_register,
};

static int mt7986_afe_pcm_dev_probe(struct platform_device *pdev)
{
	struct mtk_base_afe *afe;
	struct mt7986_afe_private *afe_priv;
	struct device *dev;
	int i, irq_id, ret;

	afe = devm_kzalloc(&pdev->dev, sizeof(*afe), GFP_KERNEL);
	if (!afe)
		return -ENOMEM;
	platform_set_drvdata(pdev, afe);

	afe->platform_priv = devm_kzalloc(&pdev->dev, sizeof(*afe_priv),
					  GFP_KERNEL);
	if (!afe->platform_priv)
		return -ENOMEM;

	afe_priv = afe->platform_priv;
	afe->dev = &pdev->dev;
	dev = afe->dev;

	afe->base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(afe->base_addr))
		return PTR_ERR(afe->base_addr);

	/* initial audio related clock */
	ret = mt7986_init_clock(afe);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot initialize clocks\n");

	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	/* enable clock for regcache get default value from hw */
	afe_priv->pm_runtime_bypass_reg_ctl = true;
	pm_runtime_get_sync(&pdev->dev);

	afe->regmap = devm_regmap_init_mmio(&pdev->dev, afe->base_addr,
		      &mt7986_afe_regmap_config);

	pm_runtime_put_sync(&pdev->dev);
	if (IS_ERR(afe->regmap))
		return PTR_ERR(afe->regmap);

	afe_priv->pm_runtime_bypass_reg_ctl = false;

	/* init memif */
	afe->memif_size = MT7986_MEMIF_NUM;
	afe->memif = devm_kcalloc(dev, afe->memif_size, sizeof(*afe->memif),
				  GFP_KERNEL);
	if (!afe->memif)
		return -ENOMEM;

	for (i = 0; i < afe->memif_size; i++) {
		afe->memif[i].data = &memif_data[i];
		afe->memif[i].irq_usage = -1;
	}

	mutex_init(&afe->irq_alloc_lock);

	/* irq initialize */
	afe->irqs_size = MT7986_IRQ_NUM;
	afe->irqs = devm_kcalloc(dev, afe->irqs_size, sizeof(*afe->irqs),
				 GFP_KERNEL);
	if (!afe->irqs)
		return -ENOMEM;

	for (i = 0; i < afe->irqs_size; i++)
		afe->irqs[i].irq_data = &irq_data[i];

	/* request irq */
	irq_id = platform_get_irq(pdev, 0);
	if (irq_id < 0)
		return irq_id;

	ret = devm_request_irq(dev, irq_id, mt7986_afe_irq_handler,
			       IRQF_TRIGGER_NONE, "asys-isr", (void *)afe);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request irq for asys-isr\n");

	/* init sub_dais */
	INIT_LIST_HEAD(&afe->sub_dais);

	for (i = 0; i < ARRAY_SIZE(dai_register_cbs); i++) {
		ret = dai_register_cbs[i](afe);
		if (ret)
			return dev_err_probe(dev, ret, "DAI register failed, i: %d\n", i);
	}

	/* init dai_driver and component_driver */
	ret = mtk_afe_combine_sub_dai(afe);
	if (ret)
		return dev_err_probe(dev, ret, "mtk_afe_combine_sub_dai fail\n");

	afe->mtk_afe_hardware = &mt7986_afe_hardware;
	afe->memif_fs = mt7986_memif_fs;
	afe->irq_fs = mt7986_irq_fs;

	afe->runtime_resume = mt7986_afe_runtime_resume;
	afe->runtime_suspend = mt7986_afe_runtime_suspend;

	/* register component */
	ret = devm_snd_soc_register_component(&pdev->dev,
					      &mtk_afe_pcm_platform,
					      NULL, 0);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot register AFE component\n");

	ret = devm_snd_soc_register_component(afe->dev,
					      &mt7986_afe_pcm_dai_component,
					      afe->dai_drivers,
					      afe->num_dai_drivers);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot register PCM DAI component\n");

	return 0;
}

static void mt7986_afe_pcm_dev_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		mt7986_afe_runtime_suspend(&pdev->dev);
}

static const struct of_device_id mt7986_afe_pcm_dt_match[] = {
	{ .compatible = "mediatek,mt7986-afe" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mt7986_afe_pcm_dt_match);

static const struct dev_pm_ops mt7986_afe_pm_ops = {
	RUNTIME_PM_OPS(mt7986_afe_runtime_suspend,
		       mt7986_afe_runtime_resume, NULL)
};

static struct platform_driver mt7986_afe_pcm_driver = {
	.driver = {
		   .name = "mt7986-audio",
		   .of_match_table = mt7986_afe_pcm_dt_match,
		   .pm = pm_ptr(&mt7986_afe_pm_ops),
	},
	.probe = mt7986_afe_pcm_dev_probe,
	.remove = mt7986_afe_pcm_dev_remove,
};
module_platform_driver(mt7986_afe_pcm_driver);

MODULE_DESCRIPTION("MediaTek SoC AFE platform driver for ALSA MT7986");
MODULE_AUTHOR("Vic Wu <vic.wu@mediatek.com>");
MODULE_LICENSE("GPL");
