// SPDX-License-Identifier: GPL-2.0
//
// rxk1000_codec.c  --  rk1000 ALSA Soc Audio driver
//
// Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd All rights reserved.

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include "rk1000_codec.h"

#define FREQ441KHZ	(0x11 << 1)
/* rk1000 output volume, DAC Digital Gain */
/* 0x0000 ~ 0xF42 */
#define VOLUME_OUTPUT	0xF42
/* 0x0 ~ 0x3f(bit0-bit5)  max=0x0(+6DB) min=0x3f(-60DB)	 Analog Gain */
#define VOLUME_CODEC_PA	0x0

/* rk1000 input volume, rk610 can not adjust the recording volume */
#define VOLUME_INPUT	0x07

#define OUT_CAPLESS	(1)

static const struct reg_default rk1000_codec_reg[] = {
	{ 0x00, 0x05 },
	{ 0x01, 0x04 },
	{ 0x02, 0xfd },
	{ 0x03, 0xf3 },
	{ 0x04, 0x03 },
	{ 0x05, 0x00 },
	{ 0x06, 0x00 },
	{ 0x07, 0x00 },
	{ 0x08, 0x00 },
	{ 0x09, 0x05 },
	{ 0x0a, 0x00 },
	{ 0x0b, 0x00 },
	{ 0x0c, 0x97 },
	{ 0x0d, 0x97 },
	{ 0x0e, 0x97 },
	{ 0x0f, 0x97 },
	{ 0x10, 0x97 },
	{ 0x11, 0x97 },
	{ 0x12, 0xcc },
	{ 0x13, 0x00 },
	{ 0x14, 0x00 },
	{ 0x15, 0xf1 },
	{ 0x16, 0x90 },
	{ 0x17, 0xff },
	{ 0x18, 0xff },
	{ 0x19, 0xff },
	{ 0x1a, 0x9c },
	{ 0x1b, 0x00 },
	{ 0x1c, 0x00 },
	{ 0x1d, 0xff },
	{ 0x1e, 0xff },
	{ 0x1f, 0xff },
};

struct rk1000_codec_priv {
	struct regmap *regmap;
	struct regmap *ctlmap;
	struct snd_soc_component *component;
	struct delayed_work pa_delayed_work;
	struct gpio_desc *spk_en_gpio;
	/*
	 * Some amplifiers enable a longer time.
	 * config after pa_enable_io delay pa_enable_time(ms)
	 * so value range is 0 - 8000.
	 */
	unsigned int pa_enable_time;
};

static void spk_ctrl_fun(struct snd_soc_component *component, int status)
{
	struct rk1000_codec_priv *rk1000_codec;

	rk1000_codec = snd_soc_component_get_drvdata(component);

	if (rk1000_codec->spk_en_gpio)
		gpiod_set_value(rk1000_codec->spk_en_gpio, status);
}

static int rk1000_codec_set_bias_level(struct snd_soc_component *component,
				       enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		snd_soc_component_write(component, ACCELCODEC_R1D, 0x2a);
		snd_soc_component_write(component, ACCELCODEC_R1E, 0x40);
		snd_soc_component_write(component, ACCELCODEC_R1F, 0x49);
		break;

	case SND_SOC_BIAS_STANDBY:
		snd_soc_component_write(component, ACCELCODEC_R1D, 0x2a);
		snd_soc_component_write(component, ACCELCODEC_R1E, 0x40);
		snd_soc_component_write(component, ACCELCODEC_R1F, 0x49);
		break;

	case SND_SOC_BIAS_OFF:
		spk_ctrl_fun(component, GPIO_LOW);
		snd_soc_component_write(component, ACCELCODEC_R1D, 0xFF);
		snd_soc_component_write(component, ACCELCODEC_R1E, 0xFF);
		snd_soc_component_write(component, ACCELCODEC_R1F, 0xFF);
		break;
	}
	return 0;
}

static int rk1000_codec_set_dai_fmt(struct snd_soc_dai *codec_dai,
				    unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct rk1000_codec_priv *rk1000_codec;
	u16 iface = 0;

	rk1000_codec = snd_soc_component_get_drvdata(component);
	/* setup Vmid and Vref, other module power down */
	snd_soc_component_write(component, ACCELCODEC_R1D, 0x2a);
	snd_soc_component_write(component, ACCELCODEC_R1E, 0x40);
	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface = 0x0040;
		break;

	case SND_SOC_DAIFMT_CBS_CFS:
		iface = 0x0000;
		break;

	default:
		return -EINVAL;
	}
	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0002;
		break;

	case SND_SOC_DAIFMT_RIGHT_J:
		break;

	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0001;
		break;

	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x0003;
		break;

	case SND_SOC_DAIFMT_DSP_B:
		iface |= 0x0013;
		break;

	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;

	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x0090;
		break;

	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x0080;
		break;

	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x0010;
		break;

	default:
		return -EINVAL;
	}

	snd_soc_component_write(component, ACCELCODEC_R09, iface);

	return 0;
}

static int rk1000_codec_pcm_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	u32 iface;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component = dai->component;
	unsigned int dai_fmt;

	dai_fmt = rtd->card->dai_link[0].dai_fmt;
	iface = snd_soc_component_read(component, ACCELCODEC_R09) & 0x1f3;
	snd_soc_component_write(component, ACCELCODEC_R0C, 0x17);
	snd_soc_component_write(component, ACCELCODEC_R04,
				ASC_INT_MUTE_L | ASC_INT_MUTE_R |
		      ASC_SIDETONE_L_OFF | ASC_SIDETONE_R_OFF);
	snd_soc_component_write(component, ACCELCODEC_R0B,
				ASC_DEC_DISABLE | ASC_INT_DISABLE);

	if ((dai_fmt & SND_SOC_DAIFMT_MASTER_MASK) == SND_SOC_DAIFMT_CBM_CFM)
		iface |= ASC_INVERT_BCLK;
	snd_soc_component_write(component, ACCELCODEC_R09, iface);
	snd_soc_component_write(component, ACCELCODEC_R0A, 0xa0);
	snd_soc_component_write(component, ACCELCODEC_R0B, ASC_DEC_ENABLE | ASC_INT_ENABLE);

	return 0;
}

static int rk1000_codec_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *component = dai->component;
	struct rk1000_codec_priv *rk1000_codec;

	rk1000_codec = snd_soc_component_get_drvdata(component);
	if (mute) {
		/* AOL */
		snd_soc_component_write(component, ACCELCODEC_R17, 0xFF);
		/* AOR */
		snd_soc_component_write(component, ACCELCODEC_R18, 0xFF);
		/* AOM */
		snd_soc_component_write(component, ACCELCODEC_R19, 0xFF);
		/* soft mute */
		snd_soc_component_write(component, ACCELCODEC_R04,
					ASC_INT_MUTE_L | ASC_INT_MUTE_R |
			      ASC_SIDETONE_L_OFF | ASC_SIDETONE_R_OFF);
	} else {
		/* setup Vmid and Vref, other module power down */
		snd_soc_component_write(component, ACCELCODEC_R1D, 0x2a);
		snd_soc_component_write(component, ACCELCODEC_R1E, 0x40);
		/* AOL */
		snd_soc_component_write(component, ACCELCODEC_R17,
					VOLUME_CODEC_PA | ASC_OUTPUT_ACTIVE |
			      ASC_CROSSZERO_EN);
		/* AOR */
		snd_soc_component_write(component, ACCELCODEC_R18,
					VOLUME_CODEC_PA | ASC_OUTPUT_ACTIVE |
			      ASC_CROSSZERO_EN);
		snd_soc_component_write(component, ACCELCODEC_R04,
					ASC_INT_ACTIVE_L | ASC_INT_ACTIVE_R |
			      ASC_SIDETONE_L_OFF | ASC_SIDETONE_R_OFF);
		/* AOM */
		snd_soc_component_write(component, ACCELCODEC_R19, 0x7F);
		#if OUT_CAPLESS
		snd_soc_component_write(component, ACCELCODEC_R1F,
					0x09 | ASC_PDMIXM_ENABLE);
		#else
		snd_soc_component_write(component, ACCELCODEC_R1F,
					0x09 | ASC_PDMIXM_ENABLE | ASC_PDPAM_ENABLE);
		#endif
	}

	return 0;
}

static void pa_delayedwork(struct work_struct *work)
{
	struct rk1000_codec_priv *priv = container_of(work,
						      struct rk1000_codec_priv,
						      pa_delayed_work.work);
	struct snd_soc_component *component = priv->component;

	spk_ctrl_fun(component, GPIO_HIGH);
}

static struct snd_soc_dai_ops rk1000_codec_ops = {
	.hw_params = rk1000_codec_pcm_hw_params,
	.set_fmt = rk1000_codec_set_dai_fmt,
	.mute_stream = rk1000_codec_mute,
	.no_capture_mute = 1,
};

#define RK1000_CODEC_RATES SNDRV_PCM_RATE_8000_192000
#define RK1000_CODEC_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |  \
			      SNDRV_PCM_FMTBIT_S20_3LE | \
			      SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver rk1000_codec_dai[] = {
	{
		.name = "rk1000_codec",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 2,
			.channels_max = 8,
			.rates = RK1000_CODEC_RATES,
			.formats = RK1000_CODEC_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = RK1000_CODEC_RATES,
			.formats = RK1000_CODEC_FORMATS,
		 },
		.ops = &rk1000_codec_ops,
		.symmetric_rates = 1,
	}
};

static void rk1000_codec_reg_init(struct snd_soc_component *component)
{
	struct rk1000_codec_priv *rk1000_codec;
	unsigned int digital_gain;
	unsigned int mic_vol;
	int ret;

	mic_vol = VOLUME_INPUT;
	rk1000_codec = snd_soc_component_get_drvdata(component);
	ret = snd_soc_component_write(component, ACCELCODEC_R1D, 0x30);
	snd_soc_component_write(component, ACCELCODEC_R1E, 0x40);
	/*Route R-LPF->R-Mixer, L-LPF->L-Mixer*/
	snd_soc_component_write(component, ACCELCODEC_R15, 0xC1);
	/*With Cap Output, VMID ramp up slow*/
	snd_soc_component_write(component, ACCELCODEC_R1A, 0x14);
	mdelay(10);
	snd_soc_component_write(component, ACCELCODEC_R0C, 0x10 | ASC_INPUT_VOL_0DB);
	snd_soc_component_write(component, ACCELCODEC_R0D, 0x10 | ASC_INPUT_VOL_0DB);
	if (mic_vol > 0x07) {
		/*Select MIC input*/
		snd_soc_component_write(component, ACCELCODEC_R12,
					0x4c | ASC_MIC_INPUT | ASC_MIC_BOOST_20DB);
		mic_vol -= 0x07;
	} else {
		/*Select MIC input*/
		snd_soc_component_write(component, ACCELCODEC_R12, 0x4c | ASC_MIC_INPUT);
	}
	/*use default value*/
	snd_soc_component_write(component, ACCELCODEC_R1C, ASC_DEM_ENABLE);
	snd_soc_component_write(component, ACCELCODEC_R0E, 0x10 | mic_vol);
	/* disable route PGA->R/L Mixer, PGA gain 0db. */
	snd_soc_component_write(component, ACCELCODEC_R13, 0x05 | 0 << 3);
	snd_soc_component_write(component, ACCELCODEC_R14, 0x05 | 0 << 3);
	/*2soft mute*/
	snd_soc_component_write(component, ACCELCODEC_R04,
				ASC_INT_MUTE_L | ASC_INT_MUTE_R |
				ASC_SIDETONE_L_OFF | ASC_SIDETONE_R_OFF);
	/*2set default SR and clk*/
	snd_soc_component_write(component, ACCELCODEC_R0A, FREQ441KHZ | ASC_NORMAL_MODE |
				(0x10 << 1) | ASC_CLKNODIV | ASC_CLK_ENABLE);
	/*2Config audio  interface*/
	snd_soc_component_write(component, ACCELCODEC_R09, ASC_I2S_MODE |
				ASC_16BIT_MODE | ASC_NORMAL_LRCLK |
				ASC_LRSWAP_DISABLE | ASC_NORMAL_BCLK);
	snd_soc_component_write(component, ACCELCODEC_R00, ASC_HPF_ENABLE |
				ASC_DSM_MODE_ENABLE | ASC_SCRAMBLE_ENABLE |
				ASC_DITHER_ENABLE | ASC_BCLKDIV_4);
	/*2volume,input,output*/
	digital_gain = VOLUME_OUTPUT;
	if (snd_soc_component_read(component, ACCELCODEC_R05) != 0x0f) {
		snd_soc_component_write(component, ACCELCODEC_R05,
					(digital_gain >> 8) & 0xFF);
		snd_soc_component_write(component, ACCELCODEC_R06, digital_gain & 0xFF);
	}

	if (snd_soc_component_read(component, ACCELCODEC_R07) != 0x0f) {
		snd_soc_component_write(component, ACCELCODEC_R07,
					(digital_gain >> 8) & 0xFF);
		snd_soc_component_write(component, ACCELCODEC_R08, digital_gain & 0xFF);
	}

	snd_soc_component_write(component, ACCELCODEC_R0B,
				ASC_DEC_ENABLE | ASC_INT_ENABLE);

	#if OUT_CAPLESS
	snd_soc_component_write(component, ACCELCODEC_R1F,
				0x09 | ASC_PDMIXM_ENABLE);
	#else
	snd_soc_component_write(component, ACCELCODEC_R1F, 0x09 |
				ASC_PDMIXM_ENABLE | ASC_PDPAM_ENABLE);
	#endif

	snd_soc_component_write(component, ACCELCODEC_R17, VOLUME_CODEC_PA |
				ASC_OUTPUT_ACTIVE | ASC_CROSSZERO_EN);
	snd_soc_component_write(component, ACCELCODEC_R18, VOLUME_CODEC_PA |
				ASC_OUTPUT_ACTIVE | ASC_CROSSZERO_EN);
	snd_soc_component_write(component, ACCELCODEC_R04, ASC_INT_ACTIVE_L |
				ASC_INT_ACTIVE_R | ASC_SIDETONE_L_OFF |
				ASC_SIDETONE_R_OFF);
	snd_soc_component_write(component, ACCELCODEC_R19, 0x7F);
}

static int rk1000_codec_suspend(struct snd_soc_component *component)
{
	spk_ctrl_fun(component, GPIO_LOW);
	rk1000_codec_set_bias_level(component, SND_SOC_BIAS_OFF);

	return 0;
}

static int rk1000_codec_resume(struct snd_soc_component *component)
{
	rk1000_codec_set_bias_level(component, SND_SOC_BIAS_PREPARE);
	spk_ctrl_fun(component, GPIO_HIGH);

	return 0;
}

static int rk1000_codec_probe(struct snd_soc_component *component)
{
	struct rk1000_codec_priv *rk1000_codec;

	rk1000_codec = snd_soc_component_get_drvdata(component);
	rk1000_codec->component = component;

	INIT_DELAYED_WORK(&rk1000_codec->pa_delayed_work,
			  pa_delayedwork);

	rk1000_codec_set_bias_level(component, SND_SOC_BIAS_PREPARE);
	schedule_delayed_work(&rk1000_codec->pa_delayed_work,
			      msecs_to_jiffies(rk1000_codec->pa_enable_time));
	rk1000_codec_reg_init(component);

	return 0;
}

static void rk1000_codec_remove(struct snd_soc_component *component)
{
	rk1000_codec_set_bias_level(component, SND_SOC_BIAS_OFF);
}

static const struct snd_soc_component_driver soc_codec_dev_rk1000_codec = {
	.probe = rk1000_codec_probe,
	.remove = rk1000_codec_remove,
	.suspend = rk1000_codec_suspend,
	.resume = rk1000_codec_resume,
	.set_bias_level = rk1000_codec_set_bias_level,
};

static int rk1000_reg_write(void *context, unsigned int reg,
			    unsigned int value)
{
	struct i2c_client *i2c = context;
	struct i2c_msg msg;
	u8 buf;
	int ret;

	buf = value;
	msg.addr = i2c->addr | reg;
	msg.flags = i2c->flags & I2C_M_TEN;
	msg.len = 1;
	msg.buf = &buf;

	ret = i2c_transfer(i2c->adapter, &msg, 1);

	return (ret == 1) ? 0 : ret;
}

static int rk1000_reg_read(void *context, unsigned int reg,
			   unsigned int *value)
{
	struct i2c_client *i2c = context;
	struct i2c_msg msg;
	u8 buf;
	int ret;

	msg.addr = i2c->addr | reg;
	msg.flags = I2C_M_RD;
	msg.len = 1;
	msg.buf = &buf;

	ret = i2c_transfer(i2c->adapter, &msg, 1);
	if (ret != 1)
		return ret;

	*value = buf;

	return 0;
}

static const struct regmap_config rk1000_codec_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_write = rk1000_reg_write,
	.reg_read = rk1000_reg_read,
	.max_register = ACCELCODEC_R1F,
	.cache_type = REGCACHE_FLAT,
	.reg_defaults = rk1000_codec_reg,
	.num_reg_defaults = ARRAY_SIZE(rk1000_codec_reg),
};

static const struct regmap_config rk1000_ctl_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = CODEC_CON,
	.cache_type = REGCACHE_FLAT,
};

static int rk1000_codec_i2c_probe(struct i2c_client *i2c,
				  const struct i2c_device_id *id)
{
	struct rk1000_codec_priv *rk1000;
	struct device_node *np = i2c->dev.of_node;
	struct device_node *ctl;
	struct i2c_client *ctl_client;

	rk1000 = devm_kzalloc(&i2c->dev, sizeof(*rk1000), GFP_KERNEL);
	if (!rk1000)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rk1000);

	of_property_read_u32(np, "rockchip,pa-en-time-ms",
			     &rk1000->pa_enable_time);

	rk1000->spk_en_gpio = devm_gpiod_get_optional(&i2c->dev, "rockchip,spk-en",
						      GPIOD_OUT_LOW);
	if (IS_ERR(rk1000->spk_en_gpio))
		return PTR_ERR(rk1000->spk_en_gpio);

	ctl = of_parse_phandle(np, "rockchip,ctl", 0);
	if (!ctl)
		return -EINVAL;

	ctl_client = of_find_i2c_device_by_node(ctl);
	if (!ctl_client) {
		dev_err(&i2c->dev, "can't find control client\n");
		return -EPROBE_DEFER;
	}

	rk1000->regmap = devm_regmap_init(&i2c->dev, NULL,
					  i2c, &rk1000_codec_regmap);
	if (IS_ERR(rk1000->regmap))
		return PTR_ERR(rk1000->regmap);

	rk1000->ctlmap = devm_regmap_init_i2c(ctl_client,
					      &rk1000_ctl_regmap);
	if (IS_ERR(rk1000->ctlmap))
		return PTR_ERR(rk1000->ctlmap);

	regmap_write(rk1000->ctlmap, CODEC_CON, CODEC_ON);

	return devm_snd_soc_register_component(&i2c->dev, &soc_codec_dev_rk1000_codec,
					       rk1000_codec_dai,
					       ARRAY_SIZE(rk1000_codec_dai));
}

static int rk1000_codec_i2c_remove(struct i2c_client *i2c)
{
	struct rk1000_codec_priv *rk1000 = i2c_get_clientdata(i2c);

	regmap_write(rk1000->ctlmap, CODEC_CON, CODEC_OFF);

	return 0;
}

static const struct i2c_device_id rk1000_codec_i2c_id[] = {
	{ "rk1000_codec", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rk1000_codec_i2c_id);

static const struct of_device_id rk1000_codec_of_match[] = {
	{ .compatible = "rockchip,rk1000-codec", },
	{},
};

static struct i2c_driver rk1000_codec_i2c_driver = {
	.driver = {
		.name = "rk1000_codec",
		.of_match_table = of_match_ptr(rk1000_codec_of_match),
	},
	.probe = rk1000_codec_i2c_probe,
	.remove   = rk1000_codec_i2c_remove,
	.id_table = rk1000_codec_i2c_id,
};
module_i2c_driver(rk1000_codec_i2c_driver);

MODULE_DESCRIPTION("Rockchip RK1000 CODEC driver");
MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_LICENSE("GPL v2");
