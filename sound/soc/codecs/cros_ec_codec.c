// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for ChromeOS Embedded Controller codec.
 *
 * This driver uses the cros-ec interface to communicate with the ChromeOS
 * EC for audio function.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#define DRV_NAME "cros-ec-codec"

/**
 * struct cros_ec_codec_data - ChromeOS EC codec driver data.
 * @dev:		Device structure used in sysfs.
 * @ec_device:		cros_ec_device structure to talk to the physical device.
 * @component:		Pointer to the component.
 * @max_dmic_gain:	Maximum gain in dB supported by EC codec.
 */
struct cros_ec_codec_data {
	struct device *dev;
	struct cros_ec_device *ec_device;
	struct snd_soc_component *component;
	unsigned int max_dmic_gain;
};

static const DECLARE_TLV_DB_SCALE(ec_mic_gain_tlv, 0, 100, 0);

static int ec_command_get_gain(struct snd_soc_component *component,
			       struct ec_param_codec_i2s *param,
			       struct ec_response_codec_gain *resp)
{
	struct cros_ec_codec_data *codec_data =
		snd_soc_component_get_drvdata(component);
	struct cros_ec_device *ec_device = codec_data->ec_device;
	u8 buffer[sizeof(struct cros_ec_command) +
		  max(sizeof(struct ec_param_codec_i2s),
		      sizeof(struct ec_response_codec_gain))];
	struct cros_ec_command *msg = (struct cros_ec_command *)&buffer;
	int ret;

	msg->version = 0;
	msg->command = EC_CMD_CODEC_I2S;
	msg->outsize = sizeof(struct ec_param_codec_i2s);
	msg->insize = sizeof(struct ec_response_codec_gain);

	memcpy(msg->data, param, msg->outsize);

	ret = cros_ec_cmd_xfer_status(ec_device, msg);
	if (ret > 0)
		memcpy(resp, msg->data, msg->insize);

	return ret;
}

/*
 * Wrapper for EC command without response.
 */
static int ec_command_no_resp(struct snd_soc_component *component,
			      struct ec_param_codec_i2s *param)
{
	struct cros_ec_codec_data *codec_data =
		snd_soc_component_get_drvdata(component);
	struct cros_ec_device *ec_device = codec_data->ec_device;
	u8 buffer[sizeof(struct cros_ec_command) +
		  sizeof(struct ec_param_codec_i2s)];
	struct cros_ec_command *msg = (struct cros_ec_command *)&buffer;

	msg->version = 0;
	msg->command = EC_CMD_CODEC_I2S;
	msg->outsize = sizeof(struct ec_param_codec_i2s);
	msg->insize = 0;

	memcpy(msg->data, param, msg->outsize);

	return cros_ec_cmd_xfer_status(ec_device, msg);
}

static int set_i2s_config(struct snd_soc_component *component,
			  enum ec_i2s_config i2s_config)
{
	struct ec_param_codec_i2s param;

	dev_dbg(component->dev, "%s set I2S format to %u\n", __func__,
		i2s_config);

	param.cmd = EC_CODEC_I2S_SET_CONFIG;
	param.i2s_config = i2s_config;

	return ec_command_no_resp(component, &param);
}

static int cros_ec_i2s_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	enum ec_i2s_config i2s_config;

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
		i2s_config = EC_DAI_FMT_I2S;
		break;

	case SND_SOC_DAIFMT_RIGHT_J:
		i2s_config = EC_DAI_FMT_RIGHT_J;
		break;

	case SND_SOC_DAIFMT_LEFT_J:
		i2s_config = EC_DAI_FMT_LEFT_J;
		break;

	case SND_SOC_DAIFMT_DSP_A:
		i2s_config = EC_DAI_FMT_PCM_A;
		break;

	case SND_SOC_DAIFMT_DSP_B:
		i2s_config = EC_DAI_FMT_PCM_B;
		break;

	default:
		return -EINVAL;
	}

	return set_i2s_config(component, i2s_config);
}

static int set_i2s_sample_depth(struct snd_soc_component *component,
				enum ec_sample_depth_value depth)
{
	struct ec_param_codec_i2s param;

	dev_dbg(component->dev, "%s set depth to %u\n", __func__, depth);

	param.cmd = EC_CODEC_SET_SAMPLE_DEPTH;
	param.depth = depth;

	return ec_command_no_resp(component, &param);
}

static int set_i2s_bclk(struct snd_soc_component *component, uint32_t bclk)
{
	struct ec_param_codec_i2s param;

	dev_dbg(component->dev, "%s set i2s bclk to %u\n", __func__, bclk);

	param.cmd = EC_CODEC_I2S_SET_BCLK;
	param.bclk = bclk;

	return ec_command_no_resp(component, &param);
}

static int cros_ec_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	unsigned int rate, bclk;
	int ret;

	rate = params_rate(params);
	if (rate != 48000)
		return -EINVAL;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		ret = set_i2s_sample_depth(component, EC_CODEC_SAMPLE_DEPTH_16);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		ret = set_i2s_sample_depth(component, EC_CODEC_SAMPLE_DEPTH_24);
		break;
	default:
		return -EINVAL;
	}
	if (ret < 0)
		return ret;

	bclk = snd_soc_params_to_bclk(params);
	return set_i2s_bclk(component, bclk);
}

static const struct snd_soc_dai_ops cros_ec_i2s_dai_ops = {
	.hw_params = cros_ec_i2s_hw_params,
	.set_fmt = cros_ec_i2s_set_dai_fmt,
};

static struct snd_soc_dai_driver cros_ec_dai[] = {
	{
		.name = "cros_ec_codec I2S",
		.id = 0,
		.capture = {
			.stream_name = "I2S Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE,
		},
		.ops = &cros_ec_i2s_dai_ops,
	}
};

static int get_ec_mic_gain(struct snd_soc_component *component,
			   u8 *left, u8 *right)
{
	struct ec_param_codec_i2s param;
	struct ec_response_codec_gain resp;
	int ret;

	param.cmd = EC_CODEC_GET_GAIN;

	ret = ec_command_get_gain(component, &param, &resp);
	if (ret < 0)
		return ret;

	*left = resp.left;
	*right = resp.right;

	return 0;
}

static int mic_gain_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	u8 left, right;
	int ret;

	ret = get_ec_mic_gain(component, &left, &right);
	if (ret)
		return ret;

	ucontrol->value.integer.value[0] = left;
	ucontrol->value.integer.value[1] = right;

	return 0;
}

static int set_ec_mic_gain(struct snd_soc_component *component,
			   u8 left, u8 right)
{
	struct ec_param_codec_i2s param;

	dev_dbg(component->dev, "%s set mic gain to %u, %u\n",
		__func__, left, right);

	param.cmd = EC_CODEC_SET_GAIN;
	param.gain.left = left;
	param.gain.right = right;

	return ec_command_no_resp(component, &param);
}

static int mic_gain_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct cros_ec_codec_data *codec_data =
		snd_soc_component_get_drvdata(component);
	int left = ucontrol->value.integer.value[0];
	int right = ucontrol->value.integer.value[1];
	unsigned int max_dmic_gain = codec_data->max_dmic_gain;

	if (left > max_dmic_gain || right > max_dmic_gain)
		return -EINVAL;

	return set_ec_mic_gain(component, (u8)left, (u8)right);
}

static struct snd_kcontrol_new mic_gain_control =
	SOC_DOUBLE_EXT_TLV("EC Mic Gain", SND_SOC_NOPM, SND_SOC_NOPM, 0, 0, 0,
			   mic_gain_get, mic_gain_put, ec_mic_gain_tlv);

static int enable_i2s(struct snd_soc_component *component, int enable)
{
	struct ec_param_codec_i2s param;

	dev_dbg(component->dev, "%s set i2s to %u\n", __func__, enable);

	param.cmd = EC_CODEC_I2S_ENABLE;
	param.i2s_enable = enable;

	return ec_command_no_resp(component, &param);
}

static int cros_ec_i2s_enable_event(struct snd_soc_dapm_widget *w,
				    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		dev_dbg(component->dev,
			"%s got SND_SOC_DAPM_PRE_PMU event\n", __func__);
		return enable_i2s(component, 1);

	case SND_SOC_DAPM_PRE_PMD:
		dev_dbg(component->dev,
			"%s got SND_SOC_DAPM_PRE_PMD event\n", __func__);
		return enable_i2s(component, 0);
	}

	return 0;
}

/*
 * The goal of this DAPM route is to turn on/off I2S using EC
 * host command when capture stream is started/stopped.
 */
static const struct snd_soc_dapm_widget cros_ec_codec_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("DMIC"),

	/*
	 * Control EC to enable/disable I2S.
	 */
	SND_SOC_DAPM_SUPPLY("I2S Enable", SND_SOC_NOPM,
			    0, 0, cros_ec_i2s_enable_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_AIF_OUT("I2STX", "I2S Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route cros_ec_codec_dapm_routes[] = {
	{ "I2STX", NULL, "DMIC" },
	{ "I2STX", NULL, "I2S Enable" },
};

/*
 * Read maximum gain from device property and set it to mixer control.
 */
static int cros_ec_set_gain_range(struct device *dev)
{
	struct soc_mixer_control *control;
	struct cros_ec_codec_data *codec_data = dev_get_drvdata(dev);
	int rc;

	rc = device_property_read_u32(dev, "max-dmic-gain",
				      &codec_data->max_dmic_gain);
	if (rc)
		return rc;

	control = (struct soc_mixer_control *)
				mic_gain_control.private_value;
	control->max = codec_data->max_dmic_gain;
	control->platform_max = codec_data->max_dmic_gain;

	return 0;
}

static int cros_ec_codec_probe(struct snd_soc_component *component)
{
	int rc;

	struct cros_ec_codec_data *codec_data =
		snd_soc_component_get_drvdata(component);

	rc = cros_ec_set_gain_range(codec_data->dev);
	if (rc)
		return rc;

	return snd_soc_add_component_controls(component, &mic_gain_control, 1);
}

static const struct snd_soc_component_driver cros_ec_component_driver = {
	.probe			= cros_ec_codec_probe,
	.dapm_widgets		= cros_ec_codec_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cros_ec_codec_dapm_widgets),
	.dapm_routes		= cros_ec_codec_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(cros_ec_codec_dapm_routes),
};

/*
 * Platform device and platform driver fro cros-ec-codec.
 */
static int cros_ec_codec_platform_probe(struct platform_device *pd)
{
	struct device *dev = &pd->dev;
	struct cros_ec_device *ec_device = dev_get_drvdata(pd->dev.parent);
	struct cros_ec_codec_data *codec_data;

	codec_data = devm_kzalloc(dev, sizeof(struct cros_ec_codec_data),
				  GFP_KERNEL);
	if (!codec_data)
		return -ENOMEM;

	codec_data->dev = dev;
	codec_data->ec_device = ec_device;

	platform_set_drvdata(pd, codec_data);

	return snd_soc_register_component(dev, &cros_ec_component_driver,
					  cros_ec_dai, ARRAY_SIZE(cros_ec_dai));
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
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(cros_ec_codec_of_match),
	},
	.probe = cros_ec_codec_platform_probe,
};

module_platform_driver(cros_ec_codec_platform_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ChromeOS EC codec driver");
MODULE_AUTHOR("Cheng-Yi Chiang <cychiang@chromium.org>");
MODULE_ALIAS("platform:" DRV_NAME);
