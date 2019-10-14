// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google, Inc.
 *
 * ChromeOS Embedded Controller codec driver.
 *
 * This driver uses the cros-ec interface to communicate with the ChromeOS
 * EC for audio function.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

struct cros_ec_codec_priv {
	struct device *dev;
	struct cros_ec_device *ec_device;
};

static int send_ec_host_command(struct cros_ec_device *ec_dev, uint32_t cmd,
				uint8_t *out, size_t outsize,
				uint8_t *in, size_t insize)
{
	int ret;
	struct cros_ec_command *msg;

	msg = kmalloc(sizeof(*msg) + max(outsize, insize), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->version = 0;
	msg->command = cmd;
	msg->outsize = outsize;
	msg->insize = insize;

	if (outsize)
		memcpy(msg->data, out, outsize);

	ret = cros_ec_cmd_xfer_status(ec_dev, msg);
	if (ret < 0)
		goto error;

	if (insize)
		memcpy(in, msg->data, insize);

	ret = 0;
error:
	kfree(msg);
	return ret;
}

static int dmic_get_gain(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);
	struct ec_param_ec_codec_i2s_rx p;
	struct ec_response_ec_codec_i2s_rx_get_gain r;
	int ret;

	p.cmd = EC_CODEC_I2S_RX_GET_GAIN;
	ret = send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_I2S_RX,
				   (uint8_t *)&p, sizeof(p),
				   (uint8_t *)&r, sizeof(r));
	if (ret < 0)
		return ret;

	ucontrol->value.integer.value[0] = r.left;
	ucontrol->value.integer.value[1] = r.right;

	return 0;
}

static int dmic_put_gain(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);
	struct soc_mixer_control *control =
		(struct soc_mixer_control *)kcontrol->private_value;
	int max_dmic_gain = control->max;
	int left = ucontrol->value.integer.value[0];
	int right = ucontrol->value.integer.value[1];
	struct ec_param_ec_codec_i2s_rx p;

	if (left > max_dmic_gain || right > max_dmic_gain)
		return -EINVAL;

	dev_dbg(component->dev, "set mic gain to %u, %u\n", left, right);

	p.cmd = EC_CODEC_I2S_RX_SET_GAIN;
	p.set_gain_param.left = left;
	p.set_gain_param.right = right;
	return send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_I2S_RX,
				    (uint8_t *)&p, sizeof(p), NULL, 0);
}

static const DECLARE_TLV_DB_SCALE(dmic_gain_tlv, 0, 100, 0);

enum {
	DMIC_CTL_GAIN = 0,
};

static struct snd_kcontrol_new dmic_controls[] = {
	[DMIC_CTL_GAIN] =
		SOC_DOUBLE_EXT_TLV("EC Mic Gain", SND_SOC_NOPM, SND_SOC_NOPM,
				   0, 0, 0, dmic_get_gain, dmic_put_gain,
				   dmic_gain_tlv),
};

static int i2s_rx_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);
	struct ec_param_ec_codec_i2s_rx p;
	enum ec_codec_i2s_rx_sample_depth depth;
	int ret;

	if (params_rate(params) != 48000)
		return -EINVAL;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		depth = EC_CODEC_I2S_RX_SAMPLE_DEPTH_16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		depth = EC_CODEC_I2S_RX_SAMPLE_DEPTH_24;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(component->dev, "set depth to %u\n", depth);

	p.cmd = EC_CODEC_I2S_RX_SET_SAMPLE_DEPTH;
	p.set_sample_depth_param.depth = depth;
	ret = send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_I2S_RX,
				   (uint8_t *)&p, sizeof(p), NULL, 0);
	if (ret < 0)
		return ret;

	dev_dbg(component->dev, "set bclk to %u\n",
		snd_soc_params_to_bclk(params));

	p.cmd = EC_CODEC_I2S_RX_SET_BCLK;
	p.set_bclk_param.bclk = snd_soc_params_to_bclk(params);
	return send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_I2S_RX,
				    (uint8_t *)&p, sizeof(p), NULL, 0);
}

static int i2s_rx_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);
	struct ec_param_ec_codec_i2s_rx p;
	enum ec_codec_i2s_rx_daifmt daifmt;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		daifmt = EC_CODEC_I2S_RX_DAIFMT_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		daifmt = EC_CODEC_I2S_RX_DAIFMT_RIGHT_J;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		daifmt = EC_CODEC_I2S_RX_DAIFMT_LEFT_J;
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(component->dev, "set format to %u\n", daifmt);

	p.cmd = EC_CODEC_I2S_RX_SET_DAIFMT;
	p.set_daifmt_param.daifmt = daifmt;
	return send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_I2S_RX,
				    (uint8_t *)&p, sizeof(p), NULL, 0);
}

static const struct snd_soc_dai_ops i2s_rx_dai_ops = {
	.hw_params = i2s_rx_hw_params,
	.set_fmt = i2s_rx_set_fmt,
};

static int i2s_rx_event(struct snd_soc_dapm_widget *w,
			struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);
	struct ec_param_ec_codec_i2s_rx p;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		dev_dbg(component->dev, "enable I2S RX\n");
		p.cmd = EC_CODEC_I2S_RX_ENABLE;
		break;
	case SND_SOC_DAPM_PRE_PMD:
		dev_dbg(component->dev, "disable I2S RX\n");
		p.cmd = EC_CODEC_I2S_RX_DISABLE;
		break;
	default:
		return 0;
	}

	return send_ec_host_command(priv->ec_device, EC_CMD_EC_CODEC_I2S_RX,
				    (uint8_t *)&p, sizeof(p), NULL, 0);
}

static struct snd_soc_dapm_widget i2s_rx_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("DMIC"),
	SND_SOC_DAPM_SUPPLY("I2S RX Enable", SND_SOC_NOPM, 0, 0, i2s_rx_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_AIF_OUT("I2S RX", "I2S Capture", 0, SND_SOC_NOPM, 0, 0),
};

static struct snd_soc_dapm_route i2s_rx_dapm_routes[] = {
	{"I2S RX", NULL, "DMIC"},
	{"I2S RX", NULL, "I2S RX Enable"},
};

static struct snd_soc_dai_driver i2s_rx_dai_driver = {
	.name = "EC Codec I2S RX",
	.capture = {
		.stream_name = "I2S Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &i2s_rx_dai_ops,
};

static int i2s_rx_probe(struct snd_soc_component *component)
{
	struct cros_ec_codec_priv *priv =
		snd_soc_component_get_drvdata(component);
	struct device *dev = priv->dev;
	int ret, val;
	struct soc_mixer_control *control;

	ret = device_property_read_u32(dev, "max-dmic-gain", &val);
	if (ret) {
		dev_err(dev, "Failed to read 'max-dmic-gain'\n");
		return ret;
	}

	control = (struct soc_mixer_control *)
			dmic_controls[DMIC_CTL_GAIN].private_value;
	control->max = val;
	control->platform_max = val;

	return snd_soc_add_component_controls(component,
			&dmic_controls[DMIC_CTL_GAIN], 1);
}

static const struct snd_soc_component_driver i2s_rx_component_driver = {
	.probe			= i2s_rx_probe,
	.dapm_widgets		= i2s_rx_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(i2s_rx_dapm_widgets),
	.dapm_routes		= i2s_rx_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(i2s_rx_dapm_routes),
};

static int cros_ec_codec_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_device *ec_device = dev_get_drvdata(pdev->dev.parent);
	struct cros_ec_codec_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->ec_device = ec_device;

	platform_set_drvdata(pdev, priv);

	return devm_snd_soc_register_component(dev, &i2s_rx_component_driver,
					       &i2s_rx_dai_driver, 1);
}

#ifdef CONFIG_OF
static const struct of_device_id cros_ec_codec_of_match[] = {
	{ .compatible = "google,cros-ec-codec" },
	{},
};
MODULE_DEVICE_TABLE(of, cros_ec_codec_of_match);
#endif

static struct platform_driver cros_ec_codec_platform_driver = {
	.driver = {
		.name = "cros-ec-codec",
		.of_match_table = of_match_ptr(cros_ec_codec_of_match),
	},
	.probe = cros_ec_codec_platform_probe,
};

module_platform_driver(cros_ec_codec_platform_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ChromeOS EC codec driver");
MODULE_AUTHOR("Cheng-Yi Chiang <cychiang@chromium.org>");
MODULE_ALIAS("platform:cros-ec-codec");
