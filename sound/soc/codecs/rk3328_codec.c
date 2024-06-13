// SPDX-License-Identifier: GPL-2.0
//
// rk3328 ALSA SoC Audio driver
//
// Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd All rights reserved.

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include "rk3328_codec.h"

/*
 * volume setting
 * 0: -39dB
 * 26: 0dB
 * 31: 6dB
 * Step: 1.5dB
 */
#define OUT_VOLUME	(0x18)
#define RK3328_GRF_SOC_CON2	(0x0408)
#define RK3328_GRF_SOC_CON10	(0x0428)
#define INITIAL_FREQ	(11289600)

struct rk3328_codec_priv {
	struct regmap *regmap;
	struct gpio_desc *mute;
	struct clk *mclk;
	struct clk *pclk;
	unsigned int sclk;
	int spk_depop_time; /* msec */
};

static const struct reg_default rk3328_codec_reg_defaults[] = {
	{ CODEC_RESET, 0x03 },
	{ DAC_INIT_CTRL1, 0x00 },
	{ DAC_INIT_CTRL2, 0x50 },
	{ DAC_INIT_CTRL3, 0x0e },
	{ DAC_PRECHARGE_CTRL, 0x01 },
	{ DAC_PWR_CTRL, 0x00 },
	{ DAC_CLK_CTRL, 0x00 },
	{ HPMIX_CTRL, 0x00 },
	{ HPOUT_CTRL, 0x00 },
	{ HPOUTL_GAIN_CTRL, 0x00 },
	{ HPOUTR_GAIN_CTRL, 0x00 },
	{ HPOUT_POP_CTRL, 0x11 },
};

static int rk3328_codec_reset(struct rk3328_codec_priv *rk3328)
{
	regmap_write(rk3328->regmap, CODEC_RESET, 0x00);
	mdelay(10);
	regmap_write(rk3328->regmap, CODEC_RESET, 0x03);

	return 0;
}

static int rk3328_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct rk3328_codec_priv *rk3328 =
		snd_soc_component_get_drvdata(dai->component);
	unsigned int val;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		val = PIN_DIRECTION_IN | DAC_I2S_MODE_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBP_CFP:
		val = PIN_DIRECTION_OUT | DAC_I2S_MODE_MASTER;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(rk3328->regmap, DAC_INIT_CTRL1,
			   PIN_DIRECTION_MASK | DAC_I2S_MODE_MASK, val);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		val = DAC_MODE_PCM;
		break;
	case SND_SOC_DAIFMT_I2S:
		val = DAC_MODE_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		val = DAC_MODE_RJM;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val = DAC_MODE_LJM;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(rk3328->regmap, DAC_INIT_CTRL2,
			   DAC_MODE_MASK, val);

	return 0;
}

static int rk3328_mute_stream(struct snd_soc_dai *dai, int mute, int direction)
{
	struct rk3328_codec_priv *rk3328 =
		snd_soc_component_get_drvdata(dai->component);
	unsigned int val;

	if (mute)
		val = HPOUTL_MUTE | HPOUTR_MUTE;
	else
		val = HPOUTL_UNMUTE | HPOUTR_UNMUTE;

	regmap_update_bits(rk3328->regmap, HPOUT_CTRL,
			   HPOUTL_MUTE_MASK | HPOUTR_MUTE_MASK, val);

	return 0;
}

static int rk3328_codec_power_on(struct rk3328_codec_priv *rk3328, int wait_ms)
{
	regmap_update_bits(rk3328->regmap, DAC_PRECHARGE_CTRL,
			   DAC_CHARGE_XCHARGE_MASK, DAC_CHARGE_PRECHARGE);
	mdelay(10);
	regmap_update_bits(rk3328->regmap, DAC_PRECHARGE_CTRL,
			   DAC_CHARGE_CURRENT_ALL_MASK,
			   DAC_CHARGE_CURRENT_ALL_ON);
	mdelay(wait_ms);

	return 0;
}

static int rk3328_codec_power_off(struct rk3328_codec_priv *rk3328, int wait_ms)
{
	regmap_update_bits(rk3328->regmap, DAC_PRECHARGE_CTRL,
			   DAC_CHARGE_XCHARGE_MASK, DAC_CHARGE_DISCHARGE);
	mdelay(10);
	regmap_update_bits(rk3328->regmap, DAC_PRECHARGE_CTRL,
			   DAC_CHARGE_CURRENT_ALL_MASK,
			   DAC_CHARGE_CURRENT_ALL_ON);
	mdelay(wait_ms);

	return 0;
}

static const struct rk3328_reg_msk_val playback_open_list[] = {
	{ DAC_PWR_CTRL, DAC_PWR_MASK, DAC_PWR_ON },
	{ DAC_PWR_CTRL, DACL_PATH_REFV_MASK | DACR_PATH_REFV_MASK,
	  DACL_PATH_REFV_ON | DACR_PATH_REFV_ON },
	{ DAC_PWR_CTRL, HPOUTL_ZERO_CROSSING_MASK | HPOUTR_ZERO_CROSSING_MASK,
	  HPOUTL_ZERO_CROSSING_ON | HPOUTR_ZERO_CROSSING_ON },
	{ HPOUT_POP_CTRL, HPOUTR_POP_MASK | HPOUTL_POP_MASK,
	  HPOUTR_POP_WORK | HPOUTL_POP_WORK },
	{ HPMIX_CTRL, HPMIXL_MASK | HPMIXR_MASK, HPMIXL_EN | HPMIXR_EN },
	{ HPMIX_CTRL, HPMIXL_INIT_MASK | HPMIXR_INIT_MASK,
	  HPMIXL_INIT_EN | HPMIXR_INIT_EN },
	{ HPOUT_CTRL, HPOUTL_MASK | HPOUTR_MASK, HPOUTL_EN | HPOUTR_EN },
	{ HPOUT_CTRL, HPOUTL_INIT_MASK | HPOUTR_INIT_MASK,
	  HPOUTL_INIT_EN | HPOUTR_INIT_EN },
	{ DAC_CLK_CTRL, DACL_REFV_MASK | DACR_REFV_MASK,
	  DACL_REFV_ON | DACR_REFV_ON },
	{ DAC_CLK_CTRL, DACL_CLK_MASK | DACR_CLK_MASK,
	  DACL_CLK_ON | DACR_CLK_ON },
	{ DAC_CLK_CTRL, DACL_MASK | DACR_MASK, DACL_ON | DACR_ON },
	{ DAC_CLK_CTRL, DACL_INIT_MASK | DACR_INIT_MASK,
	  DACL_INIT_ON | DACR_INIT_ON },
	{ DAC_SELECT, DACL_SELECT_MASK | DACR_SELECT_MASK,
	  DACL_SELECT | DACR_SELECT },
	{ HPMIX_CTRL, HPMIXL_INIT2_MASK | HPMIXR_INIT2_MASK,
	  HPMIXL_INIT2_EN | HPMIXR_INIT2_EN },
	{ HPOUT_CTRL, HPOUTL_MUTE_MASK | HPOUTR_MUTE_MASK,
	  HPOUTL_UNMUTE | HPOUTR_UNMUTE },
};

static int rk3328_codec_open_playback(struct rk3328_codec_priv *rk3328)
{
	int i;

	regmap_update_bits(rk3328->regmap, DAC_PRECHARGE_CTRL,
			   DAC_CHARGE_CURRENT_ALL_MASK,
			   DAC_CHARGE_CURRENT_I);

	for (i = 0; i < ARRAY_SIZE(playback_open_list); i++) {
		regmap_update_bits(rk3328->regmap,
				   playback_open_list[i].reg,
				   playback_open_list[i].msk,
				   playback_open_list[i].val);
		mdelay(1);
	}

	msleep(rk3328->spk_depop_time);
	gpiod_set_value(rk3328->mute, 0);

	regmap_update_bits(rk3328->regmap, HPOUTL_GAIN_CTRL,
			   HPOUTL_GAIN_MASK, OUT_VOLUME);
	regmap_update_bits(rk3328->regmap, HPOUTR_GAIN_CTRL,
			   HPOUTR_GAIN_MASK, OUT_VOLUME);

	return 0;
}

static const struct rk3328_reg_msk_val playback_close_list[] = {
	{ HPMIX_CTRL, HPMIXL_INIT2_MASK | HPMIXR_INIT2_MASK,
	  HPMIXL_INIT2_DIS | HPMIXR_INIT2_DIS },
	{ DAC_SELECT, DACL_SELECT_MASK | DACR_SELECT_MASK,
	  DACL_UNSELECT | DACR_UNSELECT },
	{ HPOUT_CTRL, HPOUTL_MUTE_MASK | HPOUTR_MUTE_MASK,
	  HPOUTL_MUTE | HPOUTR_MUTE },
	{ HPOUT_CTRL, HPOUTL_INIT_MASK | HPOUTR_INIT_MASK,
	  HPOUTL_INIT_DIS | HPOUTR_INIT_DIS },
	{ HPOUT_CTRL, HPOUTL_MASK | HPOUTR_MASK, HPOUTL_DIS | HPOUTR_DIS },
	{ HPMIX_CTRL, HPMIXL_MASK | HPMIXR_MASK, HPMIXL_DIS | HPMIXR_DIS },
	{ DAC_CLK_CTRL, DACL_MASK | DACR_MASK, DACL_OFF | DACR_OFF },
	{ DAC_CLK_CTRL, DACL_CLK_MASK | DACR_CLK_MASK,
	  DACL_CLK_OFF | DACR_CLK_OFF },
	{ DAC_CLK_CTRL, DACL_REFV_MASK | DACR_REFV_MASK,
	  DACL_REFV_OFF | DACR_REFV_OFF },
	{ HPOUT_POP_CTRL, HPOUTR_POP_MASK | HPOUTL_POP_MASK,
	  HPOUTR_POP_XCHARGE | HPOUTL_POP_XCHARGE },
	{ DAC_PWR_CTRL, DACL_PATH_REFV_MASK | DACR_PATH_REFV_MASK,
	  DACL_PATH_REFV_OFF | DACR_PATH_REFV_OFF },
	{ DAC_PWR_CTRL, DAC_PWR_MASK, DAC_PWR_OFF },
	{ HPMIX_CTRL, HPMIXL_INIT_MASK | HPMIXR_INIT_MASK,
	  HPMIXL_INIT_DIS | HPMIXR_INIT_DIS },
	{ DAC_CLK_CTRL, DACL_INIT_MASK | DACR_INIT_MASK,
	  DACL_INIT_OFF | DACR_INIT_OFF },
};

static int rk3328_codec_close_playback(struct rk3328_codec_priv *rk3328)
{
	size_t i;

	gpiod_set_value(rk3328->mute, 1);

	regmap_update_bits(rk3328->regmap, HPOUTL_GAIN_CTRL,
			   HPOUTL_GAIN_MASK, 0);
	regmap_update_bits(rk3328->regmap, HPOUTR_GAIN_CTRL,
			   HPOUTR_GAIN_MASK, 0);

	for (i = 0; i < ARRAY_SIZE(playback_close_list); i++) {
		regmap_update_bits(rk3328->regmap,
				   playback_close_list[i].reg,
				   playback_close_list[i].msk,
				   playback_close_list[i].val);
		mdelay(1);
	}

	/* Workaround for silence when changed Fs 48 -> 44.1kHz */
	rk3328_codec_reset(rk3328);

	regmap_update_bits(rk3328->regmap, DAC_PRECHARGE_CTRL,
			   DAC_CHARGE_CURRENT_ALL_MASK,
			   DAC_CHARGE_CURRENT_ALL_ON);

	return 0;
}

static int rk3328_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct rk3328_codec_priv *rk3328 =
		snd_soc_component_get_drvdata(dai->component);
	unsigned int val = 0;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val = DAC_VDL_16BITS;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		val = DAC_VDL_20BITS;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val = DAC_VDL_24BITS;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		val = DAC_VDL_32BITS;
		break;
	default:
		return -EINVAL;
	}
	regmap_update_bits(rk3328->regmap, DAC_INIT_CTRL2, DAC_VDL_MASK, val);

	val = DAC_WL_32BITS | DAC_RST_DIS;
	regmap_update_bits(rk3328->regmap, DAC_INIT_CTRL3,
			   DAC_WL_MASK | DAC_RST_MASK, val);

	return 0;
}

static int rk3328_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct rk3328_codec_priv *rk3328 =
		snd_soc_component_get_drvdata(dai->component);

	return rk3328_codec_open_playback(rk3328);
}

static void rk3328_pcm_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct rk3328_codec_priv *rk3328 =
		snd_soc_component_get_drvdata(dai->component);

	rk3328_codec_close_playback(rk3328);
}

static const struct snd_soc_dai_ops rk3328_dai_ops = {
	.hw_params = rk3328_hw_params,
	.set_fmt = rk3328_set_dai_fmt,
	.mute_stream = rk3328_mute_stream,
	.startup = rk3328_pcm_startup,
	.shutdown = rk3328_pcm_shutdown,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver rk3328_dai[] = {
	{
		.name = "rk3328-hifi",
		.id = RK3328_HIFI,
		.playback = {
			.stream_name = "HIFI Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S20_3LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
		.capture = {
			.stream_name = "HIFI Capture",
			.channels_min = 2,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_96000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE |
				    SNDRV_PCM_FMTBIT_S20_3LE |
				    SNDRV_PCM_FMTBIT_S24_LE |
				    SNDRV_PCM_FMTBIT_S32_LE),
		},
		.ops = &rk3328_dai_ops,
	},
};

static int rk3328_codec_probe(struct snd_soc_component *component)
{
	struct rk3328_codec_priv *rk3328 =
		snd_soc_component_get_drvdata(component);

	rk3328_codec_reset(rk3328);
	rk3328_codec_power_on(rk3328, 0);

	return 0;
}

static void rk3328_codec_remove(struct snd_soc_component *component)
{
	struct rk3328_codec_priv *rk3328 =
		snd_soc_component_get_drvdata(component);

	rk3328_codec_close_playback(rk3328);
	rk3328_codec_power_off(rk3328, 0);
}

static const struct snd_soc_component_driver soc_codec_rk3328 = {
	.probe = rk3328_codec_probe,
	.remove = rk3328_codec_remove,
};

static bool rk3328_codec_write_read_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CODEC_RESET:
	case DAC_INIT_CTRL1:
	case DAC_INIT_CTRL2:
	case DAC_INIT_CTRL3:
	case DAC_PRECHARGE_CTRL:
	case DAC_PWR_CTRL:
	case DAC_CLK_CTRL:
	case HPMIX_CTRL:
	case DAC_SELECT:
	case HPOUT_CTRL:
	case HPOUTL_GAIN_CTRL:
	case HPOUTR_GAIN_CTRL:
	case HPOUT_POP_CTRL:
		return true;
	default:
		return false;
	}
}

static bool rk3328_codec_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CODEC_RESET:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rk3328_codec_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = HPOUT_POP_CTRL,
	.writeable_reg = rk3328_codec_write_read_reg,
	.readable_reg = rk3328_codec_write_read_reg,
	.volatile_reg = rk3328_codec_volatile_reg,
	.reg_defaults = rk3328_codec_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rk3328_codec_reg_defaults),
	.cache_type = REGCACHE_FLAT,
};

static int rk3328_platform_probe(struct platform_device *pdev)
{
	struct device_node *rk3328_np = pdev->dev.of_node;
	struct rk3328_codec_priv *rk3328;
	struct regmap *grf;
	void __iomem *base;
	int ret = 0;

	rk3328 = devm_kzalloc(&pdev->dev, sizeof(*rk3328), GFP_KERNEL);
	if (!rk3328)
		return -ENOMEM;

	grf = syscon_regmap_lookup_by_phandle(rk3328_np,
					      "rockchip,grf");
	if (IS_ERR(grf)) {
		dev_err(&pdev->dev, "missing 'rockchip,grf'\n");
		return PTR_ERR(grf);
	}
	/* enable i2s_acodec_en */
	regmap_write(grf, RK3328_GRF_SOC_CON2,
		     (BIT(14) << 16 | BIT(14)));

	ret = of_property_read_u32(rk3328_np, "spk-depop-time-ms",
				   &rk3328->spk_depop_time);
	if (ret < 0) {
		dev_info(&pdev->dev, "spk_depop_time use default value.\n");
		rk3328->spk_depop_time = 200;
	}

	rk3328->mute = gpiod_get_optional(&pdev->dev, "mute", GPIOD_OUT_HIGH);
	if (IS_ERR(rk3328->mute))
		return PTR_ERR(rk3328->mute);
	/*
	 * Rock64 is the only supported platform to have widely relied on
	 * this; if we do happen to come across an old DTB, just leave the
	 * external mute forced off.
	 */
	if (!rk3328->mute && of_machine_is_compatible("pine64,rock64")) {
		dev_warn(&pdev->dev, "assuming implicit control of GPIO_MUTE; update devicetree if possible\n");
		regmap_write(grf, RK3328_GRF_SOC_CON10, BIT(17) | BIT(1));
	}

	rk3328->mclk = devm_clk_get(&pdev->dev, "mclk");
	if (IS_ERR(rk3328->mclk))
		return PTR_ERR(rk3328->mclk);

	ret = clk_prepare_enable(rk3328->mclk);
	if (ret)
		return ret;
	clk_set_rate(rk3328->mclk, INITIAL_FREQ);

	rk3328->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(rk3328->pclk)) {
		dev_err(&pdev->dev, "can't get acodec pclk\n");
		ret = PTR_ERR(rk3328->pclk);
		goto err_unprepare_mclk;
	}

	ret = clk_prepare_enable(rk3328->pclk);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to enable acodec pclk\n");
		goto err_unprepare_mclk;
	}

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto err_unprepare_pclk;
	}

	rk3328->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					       &rk3328_codec_regmap_config);
	if (IS_ERR(rk3328->regmap)) {
		ret = PTR_ERR(rk3328->regmap);
		goto err_unprepare_pclk;
	}

	platform_set_drvdata(pdev, rk3328);

	ret = devm_snd_soc_register_component(&pdev->dev, &soc_codec_rk3328,
					       rk3328_dai,
					       ARRAY_SIZE(rk3328_dai));
	if (ret)
		goto err_unprepare_pclk;

	return 0;

err_unprepare_pclk:
	clk_disable_unprepare(rk3328->pclk);

err_unprepare_mclk:
	clk_disable_unprepare(rk3328->mclk);
	return ret;
}

static const struct of_device_id rk3328_codec_of_match[] __maybe_unused = {
		{ .compatible = "rockchip,rk3328-codec", },
		{},
};
MODULE_DEVICE_TABLE(of, rk3328_codec_of_match);

static struct platform_driver rk3328_codec_driver = {
	.driver = {
		   .name = "rk3328-codec",
		   .of_match_table = of_match_ptr(rk3328_codec_of_match),
	},
	.probe = rk3328_platform_probe,
};
module_platform_driver(rk3328_codec_driver);

MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_DESCRIPTION("ASoC rk3328 codec driver");
MODULE_LICENSE("GPL v2");
