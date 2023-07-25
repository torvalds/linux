// SPDX-License-Identifier: GPL-2.0-only
//
// ALSA SoC glue to use IIO devices as audio components
//
// Copyright 2023 CS GROUP France
//
// Author: Herve Codina <herve.codina@bootlin.com>

#include <linux/iio/consumer.h>
#include <linux/minmax.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string_helpers.h>

#include <sound/soc.h>
#include <sound/tlv.h>

struct audio_iio_aux_chan {
	struct iio_channel *iio_chan;
	const char *name;
	int max;
	int min;
	bool is_invert_range;
};

struct audio_iio_aux {
	struct device *dev;
	struct audio_iio_aux_chan *chans;
	unsigned int num_chans;
};

static int audio_iio_aux_info_volsw(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_info *uinfo)
{
	struct audio_iio_aux_chan *chan = (struct audio_iio_aux_chan *)kcontrol->private_value;

	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = chan->max - chan->min;
	uinfo->type = (uinfo->value.integer.max == 1) ?
			SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	return 0;
}

static int audio_iio_aux_get_volsw(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct audio_iio_aux_chan *chan = (struct audio_iio_aux_chan *)kcontrol->private_value;
	int max = chan->max;
	int min = chan->min;
	bool invert_range = chan->is_invert_range;
	int ret;
	int val;

	ret = iio_read_channel_raw(chan->iio_chan, &val);
	if (ret < 0)
		return ret;

	ucontrol->value.integer.value[0] = val - min;
	if (invert_range)
		ucontrol->value.integer.value[0] = max - ucontrol->value.integer.value[0];

	return 0;
}

static int audio_iio_aux_put_volsw(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct audio_iio_aux_chan *chan = (struct audio_iio_aux_chan *)kcontrol->private_value;
	int max = chan->max;
	int min = chan->min;
	bool invert_range = chan->is_invert_range;
	int val;
	int ret;
	int tmp;

	val = ucontrol->value.integer.value[0];
	if (val < 0)
		return -EINVAL;
	if (val > max - min)
		return -EINVAL;

	val = val + min;
	if (invert_range)
		val = max - val;

	ret = iio_read_channel_raw(chan->iio_chan, &tmp);
	if (ret < 0)
		return ret;

	if (tmp == val)
		return 0;

	ret = iio_write_channel_raw(chan->iio_chan, val);
	if (ret)
		return ret;

	return 1; /* The value changed */
}

static int audio_iio_aux_add_controls(struct snd_soc_component *component,
				      struct audio_iio_aux_chan *chan)
{
	struct snd_kcontrol_new control = {
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = chan->name,
		.info = audio_iio_aux_info_volsw,
		.get = audio_iio_aux_get_volsw,
		.put = audio_iio_aux_put_volsw,
		.private_value = (unsigned long)chan,
	};

	return snd_soc_add_component_controls(component, &control, 1);
}

/*
 * These data could be on stack but they are pretty big.
 * As ASoC internally copy them and protect them against concurrent accesses
 * (snd_soc_bind_card() protects using client_mutex), keep them in the global
 * data area.
 */
static struct snd_soc_dapm_widget widgets[3];
static struct snd_soc_dapm_route routes[2];

/* Be sure sizes are correct (need 3 widgets and 2 routes) */
static_assert(ARRAY_SIZE(widgets) >= 3, "3 widgets are needed");
static_assert(ARRAY_SIZE(routes) >= 2, "2 routes are needed");

static int audio_iio_aux_add_dapms(struct snd_soc_component *component,
				   struct audio_iio_aux_chan *chan)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);
	char *output_name;
	char *input_name;
	char *pga_name;
	int ret;

	input_name = kasprintf(GFP_KERNEL, "%s IN", chan->name);
	if (!input_name)
		return -ENOMEM;

	output_name = kasprintf(GFP_KERNEL, "%s OUT", chan->name);
	if (!output_name) {
		ret = -ENOMEM;
		goto out_free_input_name;
	}

	pga_name = kasprintf(GFP_KERNEL, "%s PGA", chan->name);
	if (!pga_name) {
		ret = -ENOMEM;
		goto out_free_output_name;
	}

	widgets[0] = SND_SOC_DAPM_INPUT(input_name);
	widgets[1] = SND_SOC_DAPM_OUTPUT(output_name);
	widgets[2] = SND_SOC_DAPM_PGA(pga_name, SND_SOC_NOPM, 0, 0, NULL, 0);
	ret = snd_soc_dapm_new_controls(dapm, widgets, 3);
	if (ret)
		goto out_free_pga_name;

	routes[0].sink = pga_name;
	routes[0].control = NULL;
	routes[0].source = input_name;
	routes[1].sink = output_name;
	routes[1].control = NULL;
	routes[1].source = pga_name;
	ret = snd_soc_dapm_add_routes(dapm, routes, 2);

	/* Allocated names are no more needed (duplicated in ASoC internals) */

out_free_pga_name:
	kfree(pga_name);
out_free_output_name:
	kfree(output_name);
out_free_input_name:
	kfree(input_name);
	return ret;
}

static int audio_iio_aux_component_probe(struct snd_soc_component *component)
{
	struct audio_iio_aux *iio_aux = snd_soc_component_get_drvdata(component);
	struct audio_iio_aux_chan *chan;
	int ret;
	int i;

	for (i = 0; i < iio_aux->num_chans; i++) {
		chan = iio_aux->chans + i;

		ret = iio_read_max_channel_raw(chan->iio_chan, &chan->max);
		if (ret)
			return dev_err_probe(component->dev, ret,
					     "chan[%d] %s: Cannot get max raw value\n",
					     i, chan->name);

		ret = iio_read_min_channel_raw(chan->iio_chan, &chan->min);
		if (ret)
			return dev_err_probe(component->dev, ret,
					     "chan[%d] %s: Cannot get min raw value\n",
					     i, chan->name);

		if (chan->min > chan->max) {
			/*
			 * This should never happen but to avoid any check
			 * later, just swap values here to ensure that the
			 * minimum value is lower than the maximum value.
			 */
			dev_dbg(component->dev, "chan[%d] %s: Swap min and max\n",
				i, chan->name);
			swap(chan->min, chan->max);
		}

		/* Set initial value */
		ret = iio_write_channel_raw(chan->iio_chan,
					    chan->is_invert_range ? chan->max : chan->min);
		if (ret)
			return dev_err_probe(component->dev, ret,
					     "chan[%d] %s: Cannot set initial value\n",
					     i, chan->name);

		ret = audio_iio_aux_add_controls(component, chan);
		if (ret)
			return ret;

		ret = audio_iio_aux_add_dapms(component, chan);
		if (ret)
			return ret;

		dev_dbg(component->dev, "chan[%d]: Added %s (min=%d, max=%d, invert=%s)\n",
			i, chan->name, chan->min, chan->max,
			str_on_off(chan->is_invert_range));
	}

	return 0;
}

static const struct snd_soc_component_driver audio_iio_aux_component_driver = {
	.probe = audio_iio_aux_component_probe,
};

static int audio_iio_aux_probe(struct platform_device *pdev)
{
	struct audio_iio_aux_chan *iio_aux_chan;
	struct device *dev = &pdev->dev;
	struct audio_iio_aux *iio_aux;
	const char **names;
	u32 *invert_ranges;
	int count;
	int ret;
	int i;

	iio_aux = devm_kzalloc(dev, sizeof(*iio_aux), GFP_KERNEL);
	if (!iio_aux)
		return -ENOMEM;

	iio_aux->dev = dev;

	count = device_property_string_array_count(dev, "io-channel-names");
	if (count < 0)
		return dev_err_probe(dev, count, "failed to count io-channel-names\n");

	iio_aux->num_chans = count;

	iio_aux->chans = devm_kmalloc_array(dev, iio_aux->num_chans,
					    sizeof(*iio_aux->chans), GFP_KERNEL);
	if (!iio_aux->chans)
		return -ENOMEM;

	names = kcalloc(iio_aux->num_chans, sizeof(*names), GFP_KERNEL);
	if (!names)
		return -ENOMEM;

	invert_ranges = kcalloc(iio_aux->num_chans, sizeof(*invert_ranges), GFP_KERNEL);
	if (!invert_ranges) {
		ret = -ENOMEM;
		goto out_free_names;
	}

	ret = device_property_read_string_array(dev, "io-channel-names",
						names, iio_aux->num_chans);
	if (ret < 0) {
		dev_err_probe(dev, ret, "failed to read io-channel-names\n");
		goto out_free_invert_ranges;
	}

	/*
	 * snd-control-invert-range is optional and can contain fewer items
	 * than the number of channels. Unset values default to 0.
	 */
	count = device_property_count_u32(dev, "snd-control-invert-range");
	if (count > 0) {
		count = min_t(unsigned int, count, iio_aux->num_chans);
		ret = device_property_read_u32_array(dev, "snd-control-invert-range",
						     invert_ranges, count);
		if (ret < 0) {
			dev_err_probe(dev, ret, "failed to read snd-control-invert-range\n");
			goto out_free_invert_ranges;
		}
	}

	for (i = 0; i < iio_aux->num_chans; i++) {
		iio_aux_chan = iio_aux->chans + i;
		iio_aux_chan->name = names[i];
		iio_aux_chan->is_invert_range = invert_ranges[i];

		iio_aux_chan->iio_chan = devm_iio_channel_get(dev, iio_aux_chan->name);
		if (IS_ERR(iio_aux_chan->iio_chan)) {
			ret = PTR_ERR(iio_aux_chan->iio_chan);
			dev_err_probe(dev, ret, "get IIO channel '%s' failed\n",
				      iio_aux_chan->name);
			goto out_free_invert_ranges;
		}
	}

	platform_set_drvdata(pdev, iio_aux);

	ret = devm_snd_soc_register_component(dev, &audio_iio_aux_component_driver,
					      NULL, 0);
out_free_invert_ranges:
	kfree(invert_ranges);
out_free_names:
	kfree(names);
	return ret;
}

static const struct of_device_id audio_iio_aux_ids[] = {
	{ .compatible = "audio-iio-aux" },
	{ }
};
MODULE_DEVICE_TABLE(of, audio_iio_aux_ids);

static struct platform_driver audio_iio_aux_driver = {
	.driver = {
		.name = "audio-iio-aux",
		.of_match_table = audio_iio_aux_ids,
	},
	.probe = audio_iio_aux_probe,
};
module_platform_driver(audio_iio_aux_driver);

MODULE_AUTHOR("Herve Codina <herve.codina@bootlin.com>");
MODULE_DESCRIPTION("IIO ALSA SoC aux driver");
MODULE_LICENSE("GPL");
