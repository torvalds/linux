// SPDX-License-Identifier: GPL-2.0
/*
 * ak4118.c  --  Asahi Kasei ALSA Soc Audio driver
 *
 * Copyright 2018 DEVIALET
 */

#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <sound/asoundef.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/soc.h>

#define AK4118_REG_CLK_PWR_CTL		0x00
#define AK4118_REG_FORMAT_CTL		0x01
#define AK4118_REG_IO_CTL0		0x02
#define AK4118_REG_IO_CTL1		0x03
#define AK4118_REG_INT0_MASK		0x04
#define AK4118_REG_INT1_MASK		0x05
#define AK4118_REG_RCV_STATUS0		0x06
#define AK4118_REG_RCV_STATUS1		0x07
#define AK4118_REG_RXCHAN_STATUS0	0x08
#define AK4118_REG_RXCHAN_STATUS1	0x09
#define AK4118_REG_RXCHAN_STATUS2	0x0a
#define AK4118_REG_RXCHAN_STATUS3	0x0b
#define AK4118_REG_RXCHAN_STATUS4	0x0c
#define AK4118_REG_TXCHAN_STATUS0	0x0d
#define AK4118_REG_TXCHAN_STATUS1	0x0e
#define AK4118_REG_TXCHAN_STATUS2	0x0f
#define AK4118_REG_TXCHAN_STATUS3	0x10
#define AK4118_REG_TXCHAN_STATUS4	0x11
#define AK4118_REG_BURST_PREAMB_PC0	0x12
#define AK4118_REG_BURST_PREAMB_PC1	0x13
#define AK4118_REG_BURST_PREAMB_PD0	0x14
#define AK4118_REG_BURST_PREAMB_PD1	0x15
#define AK4118_REG_QSUB_CTL		0x16
#define AK4118_REG_QSUB_TRACK		0x17
#define AK4118_REG_QSUB_INDEX		0x18
#define AK4118_REG_QSUB_MIN		0x19
#define AK4118_REG_QSUB_SEC		0x1a
#define AK4118_REG_QSUB_FRAME		0x1b
#define AK4118_REG_QSUB_ZERO		0x1c
#define AK4118_REG_QSUB_ABS_MIN		0x1d
#define AK4118_REG_QSUB_ABS_SEC		0x1e
#define AK4118_REG_QSUB_ABS_FRAME	0x1f
#define AK4118_REG_GPE			0x20
#define AK4118_REG_GPDR			0x21
#define AK4118_REG_GPSCR		0x22
#define AK4118_REG_GPLR			0x23
#define AK4118_REG_DAT_MASK_DTS		0x24
#define AK4118_REG_RX_DETECT		0x25
#define AK4118_REG_STC_DAT_DETECT	0x26
#define AK4118_REG_RXCHAN_STATUS5	0x27
#define AK4118_REG_TXCHAN_STATUS5	0x28
#define AK4118_REG_MAX			0x29

#define AK4118_REG_FORMAT_CTL_DIF0	(1 << 4)
#define AK4118_REG_FORMAT_CTL_DIF1	(1 << 5)
#define AK4118_REG_FORMAT_CTL_DIF2	(1 << 6)

struct ak4118_priv {
	struct regmap *regmap;
	struct gpio_desc *reset;
	struct gpio_desc *irq;
	struct snd_soc_component *component;
};

static const struct reg_default ak4118_reg_defaults[] = {
	{AK4118_REG_CLK_PWR_CTL,	0x43},
	{AK4118_REG_FORMAT_CTL,		0x6a},
	{AK4118_REG_IO_CTL0,		0x88},
	{AK4118_REG_IO_CTL1,		0x48},
	{AK4118_REG_INT0_MASK,		0xee},
	{AK4118_REG_INT1_MASK,		0xb5},
	{AK4118_REG_RCV_STATUS0,	0x00},
	{AK4118_REG_RCV_STATUS1,	0x10},
	{AK4118_REG_TXCHAN_STATUS0,	0x00},
	{AK4118_REG_TXCHAN_STATUS1,	0x00},
	{AK4118_REG_TXCHAN_STATUS2,	0x00},
	{AK4118_REG_TXCHAN_STATUS3,	0x00},
	{AK4118_REG_TXCHAN_STATUS4,	0x00},
	{AK4118_REG_GPE,		0x77},
	{AK4118_REG_GPDR,		0x00},
	{AK4118_REG_GPSCR,		0x00},
	{AK4118_REG_GPLR,		0x00},
	{AK4118_REG_DAT_MASK_DTS,	0x3f},
	{AK4118_REG_RX_DETECT,		0x00},
	{AK4118_REG_STC_DAT_DETECT,	0x00},
	{AK4118_REG_TXCHAN_STATUS5,	0x00},
};

static const char * const ak4118_input_select_txt[] = {
	"RX0", "RX1", "RX2", "RX3", "RX4", "RX5", "RX6", "RX7",
};
static SOC_ENUM_SINGLE_DECL(ak4118_insel_enum, AK4118_REG_IO_CTL1, 0x0,
			    ak4118_input_select_txt);

static const struct snd_kcontrol_new ak4118_input_mux_controls =
	SOC_DAPM_ENUM("Input Select", ak4118_insel_enum);

static const char * const ak4118_iec958_fs_txt[] = {
	"44100", "48000", "32000", "22050", "11025", "24000", "16000", "88200",
	"8000", "96000", "64000", "176400", "192000",
};

static const int ak4118_iec958_fs_val[] = {
	0x0, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xC, 0xE,
};

static SOC_VALUE_ENUM_SINGLE_DECL(ak4118_iec958_fs_enum, AK4118_REG_RCV_STATUS1,
				  0x4, 0x4, ak4118_iec958_fs_txt,
				  ak4118_iec958_fs_val);

static struct snd_kcontrol_new ak4118_iec958_controls[] = {
	SOC_SINGLE("IEC958 Parity Errors", AK4118_REG_RCV_STATUS0, 0, 1, 0),
	SOC_SINGLE("IEC958 No Audio", AK4118_REG_RCV_STATUS0, 1, 1, 0),
	SOC_SINGLE("IEC958 PLL Lock", AK4118_REG_RCV_STATUS0, 4, 1, 1),
	SOC_SINGLE("IEC958 Non PCM", AK4118_REG_RCV_STATUS0, 6, 1, 0),
	SOC_ENUM("IEC958 Sampling Freq", ak4118_iec958_fs_enum),
};

static const struct snd_soc_dapm_widget ak4118_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("INRX0"),
	SND_SOC_DAPM_INPUT("INRX1"),
	SND_SOC_DAPM_INPUT("INRX2"),
	SND_SOC_DAPM_INPUT("INRX3"),
	SND_SOC_DAPM_INPUT("INRX4"),
	SND_SOC_DAPM_INPUT("INRX5"),
	SND_SOC_DAPM_INPUT("INRX6"),
	SND_SOC_DAPM_INPUT("INRX7"),
	SND_SOC_DAPM_MUX("Input Mux", SND_SOC_NOPM, 0, 0,
			 &ak4118_input_mux_controls),
};

static const struct snd_soc_dapm_route ak4118_dapm_routes[] = {
	{"Input Mux", "RX0", "INRX0"},
	{"Input Mux", "RX1", "INRX1"},
	{"Input Mux", "RX2", "INRX2"},
	{"Input Mux", "RX3", "INRX3"},
	{"Input Mux", "RX4", "INRX4"},
	{"Input Mux", "RX5", "INRX5"},
	{"Input Mux", "RX6", "INRX6"},
	{"Input Mux", "RX7", "INRX7"},
};


static int ak4118_set_dai_fmt_provider(struct ak4118_priv *ak4118,
				       unsigned int format)
{
	int dif;

	switch (format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		dif = AK4118_REG_FORMAT_CTL_DIF0 | AK4118_REG_FORMAT_CTL_DIF2;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		dif = AK4118_REG_FORMAT_CTL_DIF0 | AK4118_REG_FORMAT_CTL_DIF1;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		dif = AK4118_REG_FORMAT_CTL_DIF2;
		break;
	default:
		return -ENOTSUPP;
	}

	return dif;
}

static int ak4118_set_dai_fmt_consumer(struct ak4118_priv *ak4118,
				       unsigned int format)
{
	int dif;

	switch (format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		dif = AK4118_REG_FORMAT_CTL_DIF0 | AK4118_REG_FORMAT_CTL_DIF1 |
		      AK4118_REG_FORMAT_CTL_DIF2;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		dif = AK4118_REG_FORMAT_CTL_DIF1 | AK4118_REG_FORMAT_CTL_DIF2;
		break;
	default:
		return -ENOTSUPP;
	}

	return dif;
}

static int ak4118_set_dai_fmt(struct snd_soc_dai *dai,
			      unsigned int format)
{
	struct snd_soc_component *component = dai->component;
	struct ak4118_priv *ak4118 = snd_soc_component_get_drvdata(component);
	int dif;
	int ret = 0;

	switch (format & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		dif = ak4118_set_dai_fmt_provider(ak4118, format);
		break;
	case SND_SOC_DAIFMT_CBC_CFC:
		dif = ak4118_set_dai_fmt_consumer(ak4118, format);
		break;
	default:
		ret = -ENOTSUPP;
		goto exit;
	}

	/* format not supported */
	if (dif < 0) {
		ret = dif;
		goto exit;
	}

	ret = regmap_update_bits(ak4118->regmap, AK4118_REG_FORMAT_CTL,
				 AK4118_REG_FORMAT_CTL_DIF0 |
				 AK4118_REG_FORMAT_CTL_DIF1 |
				 AK4118_REG_FORMAT_CTL_DIF2, dif);
	if (ret < 0)
		goto exit;

exit:
	return ret;
}

static int ak4118_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	return 0;
}

static const struct snd_soc_dai_ops ak4118_dai_ops = {
	.hw_params = ak4118_hw_params,
	.set_fmt   = ak4118_set_dai_fmt,
};

static struct snd_soc_dai_driver ak4118_dai = {
	.name = "ak4118-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_32000 |
			 SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
			 SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
			 SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE  |
			   SNDRV_PCM_FMTBIT_S24_3LE |
			   SNDRV_PCM_FMTBIT_S24_LE
	},
	.ops = &ak4118_dai_ops,
};

static irqreturn_t ak4118_irq_handler(int irq, void *data)
{
	struct ak4118_priv *ak4118 = data;
	struct snd_soc_component *component = ak4118->component;
	struct snd_kcontrol_new *kctl_new;
	struct snd_kcontrol *kctl;
	struct snd_ctl_elem_id *id;
	unsigned int i;

	if (!component)
		return IRQ_NONE;

	for (i = 0; i < ARRAY_SIZE(ak4118_iec958_controls); i++) {
		kctl_new = &ak4118_iec958_controls[i];
		kctl = snd_soc_card_get_kcontrol(component->card,
						 kctl_new->name);
		if (!kctl)
			continue;
		id = &kctl->id;
		snd_ctl_notify(component->card->snd_card,
			       SNDRV_CTL_EVENT_MASK_VALUE, id);
	}

	return IRQ_HANDLED;
}

static int ak4118_probe(struct snd_soc_component *component)
{
	struct ak4118_priv *ak4118 = snd_soc_component_get_drvdata(component);
	int ret = 0;

	ak4118->component = component;

	/* release reset */
	gpiod_set_value(ak4118->reset, 0);

	/* unmask all int1 sources */
	ret = regmap_write(ak4118->regmap, AK4118_REG_INT1_MASK, 0x00);
	if (ret < 0) {
		dev_err(component->dev,
			"failed to write regmap 0x%x 0x%x: %d\n",
			AK4118_REG_INT1_MASK, 0x00, ret);
		return ret;
	}

	/* rx detect enable on all channels */
	ret = regmap_write(ak4118->regmap, AK4118_REG_RX_DETECT, 0xff);
	if (ret < 0) {
		dev_err(component->dev,
			"failed to write regmap 0x%x 0x%x: %d\n",
			AK4118_REG_RX_DETECT, 0xff, ret);
		return ret;
	}

	ret = snd_soc_add_component_controls(component, ak4118_iec958_controls,
					 ARRAY_SIZE(ak4118_iec958_controls));
	if (ret) {
		dev_err(component->dev,
			"failed to add component kcontrols: %d\n", ret);
		return ret;
	}

	return 0;
}

static void ak4118_remove(struct snd_soc_component *component)
{
	struct ak4118_priv *ak4118 = snd_soc_component_get_drvdata(component);

	/* hold reset */
	gpiod_set_value(ak4118->reset, 1);
}

static const struct snd_soc_component_driver soc_component_drv_ak4118 = {
	.probe			= ak4118_probe,
	.remove			= ak4118_remove,
	.dapm_widgets		= ak4118_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(ak4118_dapm_widgets),
	.dapm_routes		= ak4118_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(ak4118_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config ak4118_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.reg_defaults = ak4118_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(ak4118_reg_defaults),

	.cache_type = REGCACHE_NONE,
	.max_register = AK4118_REG_MAX - 1,
};

static int ak4118_i2c_probe(struct i2c_client *i2c)
{
	struct ak4118_priv *ak4118;
	int ret;

	ak4118 = devm_kzalloc(&i2c->dev, sizeof(struct ak4118_priv),
			      GFP_KERNEL);
	if (ak4118 == NULL)
		return -ENOMEM;

	ak4118->regmap = devm_regmap_init_i2c(i2c, &ak4118_regmap);
	if (IS_ERR(ak4118->regmap))
		return PTR_ERR(ak4118->regmap);

	i2c_set_clientdata(i2c, ak4118);

	ak4118->reset = devm_gpiod_get(&i2c->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ak4118->reset))
		return dev_err_probe(&i2c->dev, PTR_ERR(ak4118->reset),
				     "Failed to get reset\n");

	ak4118->irq = devm_gpiod_get(&i2c->dev, "irq", GPIOD_IN);
	if (IS_ERR(ak4118->irq))
		return dev_err_probe(&i2c->dev, PTR_ERR(ak4118->irq),
				     "Failed to get IRQ\n");

	ret = devm_request_threaded_irq(&i2c->dev, gpiod_to_irq(ak4118->irq),
					NULL, ak4118_irq_handler,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"ak4118-irq", ak4118);
	if (ret < 0) {
		dev_err(&i2c->dev, "Fail to request_irq: %d\n", ret);
		return ret;
	}

	return devm_snd_soc_register_component(&i2c->dev,
				&soc_component_drv_ak4118, &ak4118_dai, 1);
}

#ifdef CONFIG_OF
static const struct of_device_id ak4118_of_match[] = {
	{ .compatible = "asahi-kasei,ak4118", },
	{}
};
MODULE_DEVICE_TABLE(of, ak4118_of_match);
#endif

static const struct i2c_device_id ak4118_id_table[] = {
	{ "ak4118", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ak4118_id_table);

static struct i2c_driver ak4118_i2c_driver = {
	.driver  = {
		.name = "ak4118",
		.of_match_table = of_match_ptr(ak4118_of_match),
	},
	.id_table = ak4118_id_table,
	.probe_new = ak4118_i2c_probe,
};

module_i2c_driver(ak4118_i2c_driver);

MODULE_DESCRIPTION("Asahi Kasei AK4118 ALSA SoC driver");
MODULE_AUTHOR("Adrien Charruel <adrien.charruel@devialet.com>");
MODULE_LICENSE("GPL");
