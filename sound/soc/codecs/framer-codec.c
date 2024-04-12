// SPDX-License-Identifier: GPL-2.0
//
// Framer ALSA SoC driver
//
// Copyright 2023 CS GROUP France
//
// Author: Herve Codina <herve.codina@bootlin.com>

#include <linux/clk.h>
#include <linux/framer/framer.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#define FRAMER_NB_CHANNEL	32
#define FRAMER_JACK_MASK (SND_JACK_LINEIN | SND_JACK_LINEOUT)

struct framer_codec {
	struct framer *framer;
	struct device *dev;
	struct snd_soc_jack jack;
	struct notifier_block nb;
	struct work_struct carrier_work;
	int max_chan_playback;
	int max_chan_capture;
};

static int framer_dai_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
				   unsigned int rx_mask, int slots, int width)
{
	struct framer_codec *framer = snd_soc_component_get_drvdata(dai->component);

	switch (width) {
	case 0:
		/* Not set -> default 8 */
	case 8:
		break;
	default:
		dev_err(dai->dev, "tdm slot width %d not supported\n", width);
		return -EINVAL;
	}

	framer->max_chan_playback = hweight32(tx_mask);
	if (framer->max_chan_playback > FRAMER_NB_CHANNEL) {
		dev_err(dai->dev, "too many tx slots defined (mask = 0x%x) supported max %d\n",
			tx_mask, FRAMER_NB_CHANNEL);
		return -EINVAL;
	}

	framer->max_chan_capture = hweight32(rx_mask);
	if (framer->max_chan_capture > FRAMER_NB_CHANNEL) {
		dev_err(dai->dev, "too many rx slots defined (mask = 0x%x) supported max %d\n",
			rx_mask, FRAMER_NB_CHANNEL);
		return -EINVAL;
	}

	return 0;
}

/*
 * The constraints for format/channel is to match with the number of 8bit
 * time-slots available.
 */
static int framer_dai_hw_rule_channels_by_format(struct snd_soc_dai *dai,
						 struct snd_pcm_hw_params *params,
						 unsigned int nb_ts)
{
	struct snd_interval *c = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	snd_pcm_format_t format = params_format(params);
	struct snd_interval ch = {0};
	int width;

	width = snd_pcm_format_physical_width(format);
	if (width == 8 || width == 16 || width == 32 || width == 64) {
		ch.max = nb_ts * 8 / width;
	} else {
		dev_err(dai->dev, "format physical width %d not supported\n", width);
		return -EINVAL;
	}

	ch.min = ch.max ? 1 : 0;

	return snd_interval_refine(c, &ch);
}

static int framer_dai_hw_rule_playback_channels_by_format(struct snd_pcm_hw_params *params,
							  struct snd_pcm_hw_rule *rule)
{
	struct snd_soc_dai *dai = rule->private;
	struct framer_codec *framer = snd_soc_component_get_drvdata(dai->component);

	return framer_dai_hw_rule_channels_by_format(dai, params, framer->max_chan_playback);
}

static int framer_dai_hw_rule_capture_channels_by_format(struct snd_pcm_hw_params *params,
							 struct snd_pcm_hw_rule *rule)
{
	struct snd_soc_dai *dai = rule->private;
	struct framer_codec *framer = snd_soc_component_get_drvdata(dai->component);

	return framer_dai_hw_rule_channels_by_format(dai, params, framer->max_chan_capture);
}

static int framer_dai_hw_rule_format_by_channels(struct snd_soc_dai *dai,
						 struct snd_pcm_hw_params *params,
						 unsigned int nb_ts)
{
	struct snd_mask *f_old = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);
	unsigned int channels = params_channels(params);
	unsigned int slot_width;
	snd_pcm_format_t format;
	struct snd_mask f_new;

	if (!channels || channels > nb_ts) {
		dev_err(dai->dev, "channels %u not supported\n", nb_ts);
		return -EINVAL;
	}

	slot_width = (nb_ts / channels) * 8;

	snd_mask_none(&f_new);
	pcm_for_each_format(format) {
		if (snd_mask_test_format(f_old, format)) {
			if (snd_pcm_format_physical_width(format) <= slot_width)
				snd_mask_set_format(&f_new, format);
		}
	}

	return snd_mask_refine(f_old, &f_new);
}

static int framer_dai_hw_rule_playback_format_by_channels(struct snd_pcm_hw_params *params,
							  struct snd_pcm_hw_rule *rule)
{
	struct snd_soc_dai *dai = rule->private;
	struct framer_codec *framer = snd_soc_component_get_drvdata(dai->component);

	return framer_dai_hw_rule_format_by_channels(dai, params, framer->max_chan_playback);
}

static int framer_dai_hw_rule_capture_format_by_channels(struct snd_pcm_hw_params *params,
							 struct snd_pcm_hw_rule *rule)
{
	struct snd_soc_dai *dai = rule->private;
	struct framer_codec *framer = snd_soc_component_get_drvdata(dai->component);

	return framer_dai_hw_rule_format_by_channels(dai, params, framer->max_chan_capture);
}

static u64 framer_formats(u8 nb_ts)
{
	unsigned int format_width;
	unsigned int chan_width;
	snd_pcm_format_t format;
	u64 formats_mask;

	if (!nb_ts)
		return 0;

	formats_mask = 0;
	chan_width = nb_ts * 8;
	pcm_for_each_format(format) {
		/* Support physical width multiple of 8bit */
		format_width = snd_pcm_format_physical_width(format);
		if (format_width == 0 || format_width % 8)
			continue;

		/*
		 * And support physical width that can fit N times in the
		 * channel
		 */
		if (format_width > chan_width || chan_width % format_width)
			continue;

		formats_mask |= pcm_format_to_bits(format);
	}
	return formats_mask;
}

static int framer_dai_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct framer_codec *framer = snd_soc_component_get_drvdata(dai->component);
	snd_pcm_hw_rule_func_t hw_rule_channels_by_format;
	snd_pcm_hw_rule_func_t hw_rule_format_by_channels;
	unsigned int frame_bits;
	u64 format;
	int ret;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		format = framer_formats(framer->max_chan_capture);
		hw_rule_channels_by_format = framer_dai_hw_rule_capture_channels_by_format;
		hw_rule_format_by_channels = framer_dai_hw_rule_capture_format_by_channels;
		frame_bits = framer->max_chan_capture * 8;
	} else {
		format = framer_formats(framer->max_chan_playback);
		hw_rule_channels_by_format = framer_dai_hw_rule_playback_channels_by_format;
		hw_rule_format_by_channels = framer_dai_hw_rule_playback_format_by_channels;
		frame_bits = framer->max_chan_playback * 8;
	}

	ret = snd_pcm_hw_constraint_mask64(substream->runtime,
					   SNDRV_PCM_HW_PARAM_FORMAT, format);
	if (ret) {
		dev_err(dai->dev, "Failed to add format constraint (%d)\n", ret);
		return ret;
	}

	ret = snd_pcm_hw_rule_add(substream->runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				  hw_rule_channels_by_format, dai,
				  SNDRV_PCM_HW_PARAM_FORMAT, -1);
	if (ret) {
		dev_err(dai->dev, "Failed to add channels rule (%d)\n", ret);
		return ret;
	}

	ret = snd_pcm_hw_rule_add(substream->runtime, 0,  SNDRV_PCM_HW_PARAM_FORMAT,
				  hw_rule_format_by_channels, dai,
				  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	if (ret) {
		dev_err(dai->dev, "Failed to add format rule (%d)\n", ret);
		return ret;
	}

	ret = snd_pcm_hw_constraint_single(substream->runtime,
					   SNDRV_PCM_HW_PARAM_FRAME_BITS,
					   frame_bits);
	if (ret < 0) {
		dev_err(dai->dev, "Failed to add frame_bits constraint (%d)\n", ret);
		return ret;
	}

	return 0;
}

static u64 framer_dai_formats[] = {
	SND_SOC_POSSIBLE_DAIFMT_DSP_B,
};

static const struct snd_soc_dai_ops framer_dai_ops = {
	.startup	= framer_dai_startup,
	.set_tdm_slot	= framer_dai_set_tdm_slot,
	.auto_selectable_formats     = framer_dai_formats,
	.num_auto_selectable_formats = ARRAY_SIZE(framer_dai_formats),
};

static struct snd_soc_dai_driver framer_dai_driver = {
	.name = "framer",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = FRAMER_NB_CHANNEL,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = U64_MAX, /* Will be refined on DAI .startup() */
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = FRAMER_NB_CHANNEL,
		.rates = SNDRV_PCM_RATE_8000,
		.formats = U64_MAX, /* Will be refined on DAI .startup() */
	},
	.ops = &framer_dai_ops,
};

static void framer_carrier_work(struct work_struct *work)
{
	struct framer_codec *framer = container_of(work, struct framer_codec, carrier_work);
	struct framer_status framer_status;
	int jack_status;
	int ret;

	ret = framer_get_status(framer->framer, &framer_status);
	if (ret) {
		dev_err(framer->dev, "get framer status failed (%d)\n", ret);
		return;
	}

	jack_status = framer_status.link_is_on ? FRAMER_JACK_MASK : 0;
	snd_soc_jack_report(&framer->jack, jack_status, FRAMER_JACK_MASK);
}

static int framer_carrier_notifier(struct notifier_block *nb, unsigned long action,
				   void *data)
{
	struct framer_codec *framer = container_of(nb, struct framer_codec, nb);

	switch (action) {
	case FRAMER_EVENT_STATUS:
		queue_work(system_power_efficient_wq, &framer->carrier_work);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static int framer_component_probe(struct snd_soc_component *component)
{
	struct framer_codec *framer = snd_soc_component_get_drvdata(component);
	struct framer_status status;
	char *name;
	int ret;

	INIT_WORK(&framer->carrier_work, framer_carrier_work);

	name = "carrier";
	if (component->name_prefix) {
		name = kasprintf(GFP_KERNEL, "%s carrier", component->name_prefix);
		if (!name)
			return -ENOMEM;
	}

	ret = snd_soc_card_jack_new(component->card, name, FRAMER_JACK_MASK, &framer->jack);
	if (component->name_prefix)
		kfree(name); /* A copy is done by snd_soc_card_jack_new */
	if (ret) {
		dev_err(component->dev, "Cannot create jack\n");
		return ret;
	}

	ret = framer_init(framer->framer);
	if (ret) {
		dev_err(component->dev, "framer init failed (%d)\n", ret);
		return ret;
	}

	ret = framer_power_on(framer->framer);
	if (ret) {
		dev_err(component->dev, "framer power-on failed (%d)\n", ret);
		goto framer_exit;
	}

	/* Be sure that get_status is supported */
	ret = framer_get_status(framer->framer, &status);
	if (ret) {
		dev_err(component->dev, "get framer status failed (%d)\n", ret);
		goto framer_power_off;
	}

	framer->nb.notifier_call = framer_carrier_notifier;
	ret = framer_notifier_register(framer->framer, &framer->nb);
	if (ret) {
		dev_err(component->dev, "Cannot register event notifier\n");
		goto framer_power_off;
	}

	/* Queue work to set the initial value */
	queue_work(system_power_efficient_wq, &framer->carrier_work);

	return 0;

framer_power_off:
	framer_power_off(framer->framer);
framer_exit:
	framer_exit(framer->framer);
	return ret;
}

static void framer_component_remove(struct snd_soc_component *component)
{
	struct framer_codec *framer = snd_soc_component_get_drvdata(component);

	framer_notifier_unregister(framer->framer, &framer->nb);
	cancel_work_sync(&framer->carrier_work);
	framer_power_off(framer->framer);
	framer_exit(framer->framer);
}

static const struct snd_soc_component_driver framer_component_driver = {
	.probe		= framer_component_probe,
	.remove		= framer_component_remove,
	.endianness	= 1,
};

static int framer_codec_probe(struct platform_device *pdev)
{
	struct framer_codec *framer;

	framer = devm_kzalloc(&pdev->dev, sizeof(*framer), GFP_KERNEL);
	if (!framer)
		return -ENOMEM;

	framer->dev = &pdev->dev;

	/* Get framer from parents node */
	framer->framer = devm_framer_get(&pdev->dev, NULL);
	if (IS_ERR(framer->framer))
		return dev_err_probe(&pdev->dev, PTR_ERR(framer->framer), "get framer failed\n");

	platform_set_drvdata(pdev, framer);

	return devm_snd_soc_register_component(&pdev->dev, &framer_component_driver,
					       &framer_dai_driver, 1);
}

static struct platform_driver framer_codec_driver = {
	.driver = {
		.name = "framer-codec",
	},
	.probe = framer_codec_probe,
};
module_platform_driver(framer_codec_driver);

MODULE_AUTHOR("Herve Codina <herve.codina@bootlin.com>");
MODULE_DESCRIPTION("FRAMER ALSA SoC driver");
MODULE_LICENSE("GPL");
