// SPDX-License-Identifier: GPL-2.0
/*
 * Internal adc codec for cv1800b compatible SoC
 *
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <sound/tlv.h>
#include <sound/soc-component.h>
#include <sound/control.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/bitops.h>

#define CV1800B_RXADC_WORD_LEN 16
#define CV1800B_RXADC_CHANNELS 2

#define CV1800B_RXADC_CTRL0    0x00
#define CV1800B_RXADCC_CTRL1   0x04
#define CV1800B_RXADC_STATUS   0x08
#define CV1800B_RXADC_CLK      0x0c
#define CV1800B_RXADC_ANA0     0x10
#define CV1800B_RXADC_ANA1     0x14
#define CV1800B_RXADC_ANA2     0x18
#define CV1800B_RXADC_ANA3     0x1c
#define CV1800B_RXADC_ANA4     0x20

/* CV1800B_RXADC_CTRL0 */
#define REG_RXADC_EN                   GENMASK(0, 0)
#define REG_I2S_TX_EN                  GENMASK(1, 1)

/* CV1800B_RXADCC_CTRL1 */
#define REG_RXADC_CIC_OPT              GENMASK(1, 0)
#define REG_RXADC_IGR_INIT             GENMASK(8, 8)

/* CV1800B_RXADC_ANA0 */
#define REG_GSTEPL_RXPGA               GENMASK(12, 0)
#define REG_G6DBL_RXPGA                GENMASK(13, 13)
#define REG_GAINL_RXADC                GENMASK(15, 14)
#define REG_GSTEPR_RXPGA               GENMASK(28, 16)
#define REG_G6DBR_RXPGA                GENMASK(29, 29)
#define REG_GAINR_RXADC                GENMASK(31, 30)
#define REG_COMB_LEFT_VOLUME           GENMASK(15, 0)
#define REG_COMB_RIGHT_VOLUME          GENMASK(31, 16)

/* CV1800B_RXADC_ANA2 */
#define REG_MUTEL_RXPGA                GENMASK(0, 0)
#define REG_MUTER_RXPGA                GENMASK(1, 1)

/* CV1800B_RXADC_CLK */
#define REG_RXADC_CLK_INV              GENMASK(0, 0)
#define REG_RXADC_SCK_DIV              GENMASK(15, 8)
#define REG_RXADC_DLYEN                GENMASK(23, 16)

enum decimation_values {
	DECIMATION_64 = 0,
	DECIMATION_128,
	DECIMATION_256,
	DECIMATION_512,
};

static const u32 cv1800b_gains[] = {
	0x0001, /* 0dB */
	0x0002, /* 2dB */
	0x0004, /* 4dB */
	0x0008, /* 6dB */
	0x0010, /* 8dB */
	0x0020, /* 10dB */
	0x0040, /* 12dB */
	0x0080, /* 14dB */
	0x0100, /* 16dB */
	0x0200, /* 18dB */
	0x0400, /* 20dB */
	0x0800, /* 22dB */
	0x1000, /* 24dB */
	0x2400, /* 26dB */
	0x2800, /* 28dB */
	0x3000, /* 30dB */
	0x6400, /* 32dB */
	0x6800, /* 34dB */
	0x7000, /* 36dB */
	0xA400, /* 38dB */
	0xA800, /* 40dB */
	0xB000, /* 42dB */
	0xE400, /* 44dB */
	0xE800, /* 46dB */
	0xF000, /* 48dB */
};

struct cv1800b_priv {
	void __iomem *regs;
	struct device *dev;
	unsigned int mclk_rate;
};

static int cv1800b_adc_setbclk_div(struct cv1800b_priv *priv, unsigned int rate)
{
	u32 val;
	u32 bclk_div;
	u64 tmp;

	if (!priv->mclk_rate || !rate)
		return -EINVAL;

	tmp = div_u64(priv->mclk_rate, CV1800B_RXADC_WORD_LEN *
		      CV1800B_RXADC_CHANNELS * rate * 2);

	if (!tmp) {
		dev_err(priv->dev, "computed BCLK divider is zero\n");
		return -EINVAL;
	}

	if (tmp > 256) {
		dev_err(priv->dev, "BCLK divider %llu out of range\n", tmp);
		return -EINVAL;
	}

	bclk_div = tmp - 1;
	val = readl(priv->regs + CV1800B_RXADC_CLK);
	val = u32_replace_bits(val, bclk_div, REG_RXADC_SCK_DIV);
	/* Vendor value for 48kHz, tested on SG2000/SG2002 */
	val = u32_replace_bits(val, 0x19, REG_RXADC_DLYEN);
	writel(val, priv->regs + CV1800B_RXADC_CLK);

	return 0;
}

static void cv1800b_adc_enable(struct cv1800b_priv *priv, bool enable)
{
	u32 val;

	val = readl(priv->regs + CV1800B_RXADC_CTRL0);
	val = u32_replace_bits(val, enable, REG_RXADC_EN);
	val = u32_replace_bits(val, enable, REG_I2S_TX_EN);
	writel(val, priv->regs + CV1800B_RXADC_CTRL0);
}

static unsigned int cv1800b_adc_calc_db(u32 ana0, bool right)
{
	u32 step_mask = right ? FIELD_GET(REG_GSTEPR_RXPGA, ana0) :
				FIELD_GET(REG_GSTEPL_RXPGA, ana0);
	u32 coarse = right ? FIELD_GET(REG_GAINR_RXADC, ana0) :
			     FIELD_GET(REG_GAINL_RXADC, ana0);
	bool g6db = right ? FIELD_GET(REG_G6DBR_RXPGA, ana0) :
			    FIELD_GET(REG_G6DBL_RXPGA, ana0);

	u32 step = step_mask ? __ffs(step_mask) : 0;

	step = min(step, 12U);
	coarse = min(coarse, 3U);

	return 2 * step + 6 * coarse + (g6db ? 6 : 0);
}

static int cv1800b_adc_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct cv1800b_priv *priv = snd_soc_dai_get_drvdata(dai);
	unsigned int rate = params_rate(params);
	u32 val;
	int ret;

	ret = cv1800b_adc_setbclk_div(priv, rate);
	if (ret) {
		dev_err(priv->dev,
			"could not set rate, check DT node for fixed clock\n");
		return ret;
	}

	/* init adc */
	val = readl(priv->regs + CV1800B_RXADCC_CTRL1);
	val = u32_replace_bits(val, 1, REG_RXADC_IGR_INIT);
	val = u32_replace_bits(val, DECIMATION_64, REG_RXADC_CIC_OPT);
	writel(val, priv->regs + CV1800B_RXADCC_CTRL1);
	return 0;
}

static int cv1800b_adc_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				   struct snd_soc_dai *dai)
{
	struct cv1800b_priv *priv = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		cv1800b_adc_enable(priv, true);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		cv1800b_adc_enable(priv, false);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cv1800b_adc_dai_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				      unsigned int freq, int dir)
{
	struct cv1800b_priv *priv = snd_soc_dai_get_drvdata(dai);

	priv->mclk_rate = freq;
	dev_dbg(priv->dev, "mclk is set to %u\n", freq);
	return 0;
}

static const struct snd_soc_dai_ops cv1800b_adc_dai_ops = {
	.hw_params = cv1800b_adc_hw_params,
	.set_sysclk = cv1800b_adc_dai_set_sysclk,
	.trigger = cv1800b_adc_dai_trigger,
};

static struct snd_soc_dai_driver cv1800b_adc_dai = {
	.name = "adc-hifi",
	.capture = { .stream_name = "ADC Capture",
		     .channels_min = 1,
		     .channels_max = 2,
		     .rates = SNDRV_PCM_RATE_48000,
		     .formats = SNDRV_PCM_FMTBIT_S16_LE },
	.ops = &cv1800b_adc_dai_ops,
};

static int cv1800b_adc_volume_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct cv1800b_priv *priv = snd_soc_component_get_drvdata(component);
	u32 ana0 = readl(priv->regs + CV1800B_RXADC_ANA0);

	unsigned int left = cv1800b_adc_calc_db(ana0, false);
	unsigned int right = cv1800b_adc_calc_db(ana0, true);

	ucontrol->value.integer.value[0] = min(left / 2, 24U);
	ucontrol->value.integer.value[1] = min(right / 2, 24U);
	return 0;
}

static int cv1800b_adc_volume_set(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct cv1800b_priv *priv = snd_soc_component_get_drvdata(component);

	u32 v_left = clamp_t(u32, ucontrol->value.integer.value[0], 0, 24);
	u32 v_right = clamp_t(u32, ucontrol->value.integer.value[1], 0, 24);
	u32 val;

	val = readl(priv->regs + CV1800B_RXADC_ANA0);
	val = u32_replace_bits(val, cv1800b_gains[v_left],
			       REG_COMB_LEFT_VOLUME);
	val = u32_replace_bits(val, cv1800b_gains[v_right],
			       REG_COMB_RIGHT_VOLUME);
	writel(val, priv->regs + CV1800B_RXADC_ANA0);

	return 0;
}

static DECLARE_TLV_DB_SCALE(cv1800b_volume_tlv, 0, 200, 0);

static const struct snd_kcontrol_new cv1800b_adc_controls[] = {
	SOC_DOUBLE_EXT_TLV("Internal I2S Capture Volume", SND_SOC_NOPM, 0, 16, 24, false,
			   cv1800b_adc_volume_get, cv1800b_adc_volume_set,
			   cv1800b_volume_tlv),
};

static const struct snd_soc_component_driver cv1800b_adc_component = {
	.name = "cv1800b-adc-codec",
	.controls = cv1800b_adc_controls,
	.num_controls = ARRAY_SIZE(cv1800b_adc_controls),
};

static int cv1800b_adc_probe(struct platform_device *pdev)
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
					       &cv1800b_adc_component,
					       &cv1800b_adc_dai, 1);
}

static const struct of_device_id cv1800b_adc_of_match[] = {
	{ .compatible = "sophgo,cv1800b-sound-adc" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, cv1800b_adc_of_match);

static struct platform_driver cv1800b_adc_driver = {
	.probe = cv1800b_adc_probe,
	.driver = {
		.name = "cv1800b-sound-adc",
		.of_match_table = cv1800b_adc_of_match,
	},
};

module_platform_driver(cv1800b_adc_driver);

MODULE_DESCRIPTION("ADC codec for CV1800B");
MODULE_AUTHOR("Anton D. Stavinskii <stavinsky@gmail.com>");
MODULE_LICENSE("GPL");
