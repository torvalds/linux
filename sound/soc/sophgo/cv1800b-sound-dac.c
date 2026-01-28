// SPDX-License-Identifier: GPL-2.0
/*
 * Internal DAC codec for cv1800b based CPUs
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <sound/soc.h>
#include <linux/io.h>

#define CV1800B_TXDAC_CTRL0   0x00
#define CV1800B_TXDAC_CTRL1   0x04
#define CV1800B_TXDAC_STATUS  0x08
#define CV1800B_TXDAC_AFE0    0x0c
#define CV1800B_TXDAC_AFE1    0x10
#define CV1800B_TXDAC_ANA0    0x20
#define CV1800B_TXDAC_ANA1    0x24
#define CV1800B_TXDAC_ANA2    0x28

/* cv1800b_TXDAC_CTRL0 */
#define REG_TXDAC_EN GENMASK(0, 0)
#define REG_I2S_RX_EN GENMASK(1, 1)

/* cv1800b_TXDAC_CTRL1 */
#define REG_TXDAC_CIC_OPT GENMASK(1, 0)

/* cv1800b_TXDAC_AFE0 */
#define REG_TXDAC_INIT_DLY_CNT GENMASK(5, 0)

/* cv1800b_TXDAC_ANA2 */
#define TXDAC_OW_VAL_L_MASK GENMASK(7, 0)
#define TXDAC_OW_VAL_R_MASK GENMASK(15, 8)
#define TXDAC_OW_EN_L_MASK GENMASK(16, 16)
#define TXDAC_OW_EN_R_MASK GENMASK(17, 17)

struct cv1800b_priv {
	void __iomem *regs;
	struct device *dev;
};

enum decimation_values {
	DECIMATION_64 = 0,
	DECIMATION_128,
	DECIMATION_256,
	DECIMATION_512,
};

static void cv1800b_dac_enable(struct cv1800b_priv *priv, bool enable)
{
	u32 val;

	val = readl(priv->regs + CV1800B_TXDAC_CTRL0);
	val = u32_replace_bits(val, enable, REG_TXDAC_EN);
	val = u32_replace_bits(val, enable, REG_I2S_RX_EN);
	writel(val, priv->regs + CV1800B_TXDAC_CTRL0);
}

/*
 * Control the DAC overwrite bits. When enabled, the DAC outputs the fixed
 * overwrite value instead of samples from the I2S input.
 */
static void cv1800b_dac_mute(struct cv1800b_priv *priv, bool enable)
{
	u32 val;

	val = readl(priv->regs + CV1800B_TXDAC_ANA2);
	val = u32_replace_bits(val, enable, TXDAC_OW_EN_L_MASK);
	val = u32_replace_bits(val, enable, TXDAC_OW_EN_R_MASK);
	writel(val, priv->regs + CV1800B_TXDAC_ANA2);
}

static int cv1800b_dac_decimation(struct cv1800b_priv *priv, u8 dec)
{
	u32 val;

	if (dec > 3)
		return -EINVAL;

	val = readl(priv->regs + CV1800B_TXDAC_CTRL1);
	val = u32_replace_bits(val, dec, REG_TXDAC_CIC_OPT);
	writel(val, priv->regs + CV1800B_TXDAC_CTRL1);
	return 0;
}

static int cv1800b_dac_dly(struct cv1800b_priv *priv, u32 dly)
{
	u32 val;

	if (dly > 63)
		return -EINVAL;

	val = readl(priv->regs + CV1800B_TXDAC_AFE0);
	val = u32_replace_bits(val, dly, REG_TXDAC_INIT_DLY_CNT);
	writel(val, priv->regs + CV1800B_TXDAC_AFE0);
	return 0;
}

static int cv1800b_dac_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct cv1800b_priv *priv = snd_soc_dai_get_drvdata(dai);
	int ret;
	unsigned int rate = params_rate(params);

	if (rate != 48000) {
		dev_err(priv->dev, "rate %u is not supported\n", rate);
		return -EINVAL;
	}
	/* Clear DAC overwrite so playback uses I2S data. */
	cv1800b_dac_mute(priv, false);
	/* minimal decimation for 48kHz is 64*/
	ret = cv1800b_dac_decimation(priv, DECIMATION_64);
	if (ret)
		return ret;

	/* value is taken from vendors driver 48kHz
	 * tested on sg2000 and sg2002.
	 */
	ret = cv1800b_dac_dly(priv, 0x19);
	if (ret)
		return ret;

	return 0;
}

static int cv1800b_dac_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				   struct snd_soc_dai *dai)
{
	struct cv1800b_priv *priv = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		cv1800b_dac_enable(priv, true);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		cv1800b_dac_enable(priv, false);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops cv1800b_dac_dai_ops = {
	.hw_params = cv1800b_dac_hw_params,
	.trigger = cv1800b_dac_dai_trigger,
};

static struct snd_soc_dai_driver cv1800b_dac_dai = {
	.name = "dac-hifi",
	.playback = { .stream_name = "DAC Playback",
		      .channels_min = 2,
		      .channels_max = 2,
		      .rates = SNDRV_PCM_RATE_48000,
		      .formats = SNDRV_PCM_FMTBIT_S16_LE },
	.ops = &cv1800b_dac_dai_ops,
};

static const struct snd_soc_component_driver cv1800b_dac_component = {
	.name = "cv1800b-dac-codec",
};

static int cv1800b_dac_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cv1800b_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	platform_set_drvdata(pdev, priv);
	return devm_snd_soc_register_component(&pdev->dev,
					       &cv1800b_dac_component,
					       &cv1800b_dac_dai, 1);
}

static const struct of_device_id cv1800b_dac_of_match[] = {
	{ .compatible = "sophgo,cv1800b-sound-dac" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cv1800b_dac_of_match);

static struct platform_driver cv1800b_dac_driver = {
	.probe = cv1800b_dac_probe,
	.driver = {
		.name = "cv1800b-dac-codec",
		.of_match_table = cv1800b_dac_of_match,
	},
};
module_platform_driver(cv1800b_dac_driver);

MODULE_DESCRIPTION("DAC codec for CV1800B");
MODULE_AUTHOR("Anton D. Stavinskii <stavinsky@gmail.com>");
MODULE_LICENSE("GPL");
