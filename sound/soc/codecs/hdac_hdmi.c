/*
 *  hdac_hdmi.c - ASoc HDA-HDMI codec driver for Intel platforms
 *
 *  Copyright (C) 2014-2015 Intel Corp
 *  Author: Samreen Nilofer <samreen.nilofer@intel.com>
 *	    Subhransu S. Prusty <subhransu.s.prusty@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/hdmi.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/hdaudio_ext.h>
#include <sound/hda_i915.h>
#include "../../hda/local.h"

#define AMP_OUT_MUTE		0xb080
#define AMP_OUT_UNMUTE		0xb000
#define PIN_OUT			(AC_PINCTL_OUT_EN)

#define HDA_MAX_CONNECTIONS     32

struct hdac_hdmi_cvt_params {
	unsigned int channels_min;
	unsigned int channels_max;
	u32 rates;
	u64 formats;
	unsigned int maxbps;
};

struct hdac_hdmi_cvt {
	struct list_head head;
	hda_nid_t nid;
	struct hdac_hdmi_cvt_params params;
};

struct hdac_hdmi_pin {
	struct list_head head;
	hda_nid_t nid;
	int num_mux_nids;
	hda_nid_t mux_nids[HDA_MAX_CONNECTIONS];
};

struct hdac_hdmi_dai_pin_map {
	int dai_id;
	struct hdac_hdmi_pin *pin;
	struct hdac_hdmi_cvt *cvt;
};

struct hdac_hdmi_priv {
	struct hdac_hdmi_dai_pin_map dai_map[3];
	struct list_head pin_list;
	struct list_head cvt_list;
	int num_pin;
	int num_cvt;
};

static inline struct hdac_ext_device *to_hda_ext_device(struct device *dev)
{
	struct hdac_device *hdac = dev_to_hdac_dev(dev);

	return to_ehdac_device(hdac);
}

static int hdac_hdmi_setup_stream(struct hdac_ext_device *hdac,
				hda_nid_t cvt_nid, hda_nid_t pin_nid,
				u32 stream_tag, int format)
{
	unsigned int val;

	dev_dbg(&hdac->hdac.dev, "cvt nid %d pnid %d stream %d format 0x%x\n",
			cvt_nid, pin_nid, stream_tag, format);

	val = (stream_tag << 4);

	snd_hdac_codec_write(&hdac->hdac, cvt_nid, 0,
				AC_VERB_SET_CHANNEL_STREAMID, val);
	snd_hdac_codec_write(&hdac->hdac, cvt_nid, 0,
				AC_VERB_SET_STREAM_FORMAT, format);

	return 0;
}

static void
hdac_hdmi_set_dip_index(struct hdac_ext_device *hdac, hda_nid_t pin_nid,
				int packet_index, int byte_index)
{
	int val;

	val = (packet_index << 5) | (byte_index & 0x1f);

	snd_hdac_codec_write(&hdac->hdac, pin_nid, 0,
				AC_VERB_SET_HDMI_DIP_INDEX, val);
}

static int hdac_hdmi_setup_audio_infoframe(struct hdac_ext_device *hdac,
				hda_nid_t cvt_nid, hda_nid_t pin_nid)
{
	uint8_t buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AUDIO_INFOFRAME_SIZE];
	struct hdmi_audio_infoframe frame;
	u8 *dip = (u8 *)&frame;
	int ret;
	int i;

	hdmi_audio_infoframe_init(&frame);

	/* Default stereo for now */
	frame.channels = 2;

	/* setup channel count */
	snd_hdac_codec_write(&hdac->hdac, cvt_nid, 0,
			    AC_VERB_SET_CVT_CHAN_COUNT, frame.channels - 1);

	ret = hdmi_audio_infoframe_pack(&frame, buffer, sizeof(buffer));
	if (ret < 0)
		return ret;

	/* stop infoframe transmission */
	hdac_hdmi_set_dip_index(hdac, pin_nid, 0x0, 0x0);
	snd_hdac_codec_write(&hdac->hdac, pin_nid, 0,
			AC_VERB_SET_HDMI_DIP_XMIT, AC_DIPXMIT_DISABLE);


	/*  Fill infoframe. Index auto-incremented */
	hdac_hdmi_set_dip_index(hdac, pin_nid, 0x0, 0x0);
	for (i = 0; i < sizeof(frame); i++)
		snd_hdac_codec_write(&hdac->hdac, pin_nid, 0,
				AC_VERB_SET_HDMI_DIP_DATA, dip[i]);

	/* Start infoframe */
	hdac_hdmi_set_dip_index(hdac, pin_nid, 0x0, 0x0);
	snd_hdac_codec_write(&hdac->hdac, pin_nid, 0,
			AC_VERB_SET_HDMI_DIP_XMIT, AC_DIPXMIT_BEST);

	return 0;
}

static void hdac_hdmi_set_power_state(struct hdac_ext_device *edev,
		struct hdac_hdmi_dai_pin_map *dai_map, unsigned int pwr_state)
{
	/* Power up pin widget */
	if (!snd_hdac_check_power_state(&edev->hdac, dai_map->pin->nid,
						pwr_state))
		snd_hdac_codec_write(&edev->hdac, dai_map->pin->nid, 0,
			AC_VERB_SET_POWER_STATE, pwr_state);

	/* Power up converter */
	if (!snd_hdac_check_power_state(&edev->hdac, dai_map->cvt->nid,
						pwr_state))
		snd_hdac_codec_write(&edev->hdac, dai_map->cvt->nid, 0,
			AC_VERB_SET_POWER_STATE, pwr_state);
}

static int hdac_hdmi_playback_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct hdac_ext_device *hdac = snd_soc_dai_get_drvdata(dai);
	struct hdac_hdmi_priv *hdmi = hdac->private_data;
	struct hdac_hdmi_dai_pin_map *dai_map;
	struct hdac_ext_dma_params *dd;
	int ret;

	if (dai->id > 0) {
		dev_err(&hdac->hdac.dev, "Only one dai supported as of now\n");
		return -ENODEV;
	}

	dai_map = &hdmi->dai_map[dai->id];

	dd = (struct hdac_ext_dma_params *)snd_soc_dai_get_dma_data(dai, substream);
	dev_dbg(&hdac->hdac.dev, "stream tag from cpu dai %d format in cvt 0x%x\n",
			dd->stream_tag,	dd->format);

	ret = hdac_hdmi_setup_audio_infoframe(hdac, dai_map->cvt->nid,
						dai_map->pin->nid);
	if (ret < 0)
		return ret;

	return hdac_hdmi_setup_stream(hdac, dai_map->cvt->nid,
			dai_map->pin->nid, dd->stream_tag, dd->format);
}

static int hdac_hdmi_set_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *hparams, struct snd_soc_dai *dai)
{
	struct hdac_ext_device *hdac = snd_soc_dai_get_drvdata(dai);
	struct hdac_ext_dma_params *dd;

	if (dai->id > 0) {
		dev_err(&hdac->hdac.dev, "Only one dai supported as of now\n");
		return -ENODEV;
	}

	dd = kzalloc(sizeof(*dd), GFP_KERNEL);
	if (!dd)
		return -ENOMEM;
	dd->format = snd_hdac_calc_stream_format(params_rate(hparams),
			params_channels(hparams), params_format(hparams),
			24, 0);

	snd_soc_dai_set_dma_data(dai, substream, (void *)dd);

	return 0;
}

static int hdac_hdmi_playback_cleanup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct hdac_ext_device *edev = snd_soc_dai_get_drvdata(dai);
	struct hdac_ext_dma_params *dd;
	struct hdac_hdmi_priv *hdmi = edev->private_data;
	struct hdac_hdmi_dai_pin_map *dai_map;

	dai_map = &hdmi->dai_map[dai->id];

	snd_hdac_codec_write(&edev->hdac, dai_map->cvt->nid, 0,
				AC_VERB_SET_CHANNEL_STREAMID, 0);
	snd_hdac_codec_write(&edev->hdac, dai_map->cvt->nid, 0,
				AC_VERB_SET_STREAM_FORMAT, 0);

	dd = (struct hdac_ext_dma_params *)snd_soc_dai_get_dma_data(dai, substream);
	snd_soc_dai_set_dma_data(dai, substream, NULL);

	kfree(dd);

	return 0;
}

static int hdac_hdmi_pcm_open(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct hdac_ext_device *hdac = snd_soc_dai_get_drvdata(dai);
	struct hdac_hdmi_priv *hdmi = hdac->private_data;
	struct hdac_hdmi_dai_pin_map *dai_map;
	int val;

	if (dai->id > 0) {
		dev_err(&hdac->hdac.dev, "Only one dai supported as of now\n");
		return -ENODEV;
	}

	dai_map = &hdmi->dai_map[dai->id];

	val = snd_hdac_codec_read(&hdac->hdac, dai_map->pin->nid, 0,
					AC_VERB_GET_PIN_SENSE, 0);
	dev_info(&hdac->hdac.dev, "Val for AC_VERB_GET_PIN_SENSE: %x\n", val);

	if ((!(val & AC_PINSENSE_PRESENCE)) || (!(val & AC_PINSENSE_ELDV))) {
		dev_err(&hdac->hdac.dev, "Monitor presence invalid with val: %x\n", val);
		return -ENODEV;
	}

	hdac_hdmi_set_power_state(hdac, dai_map, AC_PWRST_D0);

	snd_hdac_codec_write(&hdac->hdac, dai_map->pin->nid, 0,
			AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE);

	snd_pcm_hw_constraint_step(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_CHANNELS, 2);

	return 0;
}

static void hdac_hdmi_pcm_close(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct hdac_ext_device *hdac = snd_soc_dai_get_drvdata(dai);
	struct hdac_hdmi_priv *hdmi = hdac->private_data;
	struct hdac_hdmi_dai_pin_map *dai_map;

	dai_map = &hdmi->dai_map[dai->id];

	hdac_hdmi_set_power_state(hdac, dai_map, AC_PWRST_D3);

	snd_hdac_codec_write(&hdac->hdac, dai_map->pin->nid, 0,
			AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);
}

static int
hdac_hdmi_query_cvt_params(struct hdac_device *hdac, struct hdac_hdmi_cvt *cvt)
{
	int err;

	/* Only stereo supported as of now */
	cvt->params.channels_min = cvt->params.channels_max = 2;

	err = snd_hdac_query_supported_pcm(hdac, cvt->nid,
			&cvt->params.rates,
			&cvt->params.formats,
			&cvt->params.maxbps);
	if (err < 0)
		dev_err(&hdac->dev,
			"Failed to query pcm params for nid %d: %d\n",
			cvt->nid, err);

	return err;
}

static void hdac_hdmi_fill_widget_info(struct snd_soc_dapm_widget *w,
				enum snd_soc_dapm_type id,
				const char *wname, const char *stream)
{
	w->id = id;
	w->name = wname;
	w->sname = stream;
	w->reg = SND_SOC_NOPM;
	w->shift = 0;
	w->kcontrol_news = NULL;
	w->num_kcontrols = 0;
	w->priv = NULL;
}

static void hdac_hdmi_fill_route(struct snd_soc_dapm_route *route,
		const char *sink, const char *control, const char *src)
{
	route->sink = sink;
	route->source = src;
	route->control = control;
	route->connected = NULL;
}

static void create_fill_widget_route_map(struct snd_soc_dapm_context *dapm,
					struct hdac_hdmi_dai_pin_map *dai_map)
{
	struct snd_soc_dapm_route route[1];
	struct snd_soc_dapm_widget widgets[2] = { {0} };

	memset(&route, 0, sizeof(route));

	hdac_hdmi_fill_widget_info(&widgets[0], snd_soc_dapm_output,
			"hif1 Output", NULL);
	hdac_hdmi_fill_widget_info(&widgets[1], snd_soc_dapm_aif_in,
			"Coverter 1", "hif1");

	hdac_hdmi_fill_route(&route[0], "hif1 Output", NULL, "Coverter 1");

	snd_soc_dapm_new_controls(dapm, widgets, ARRAY_SIZE(widgets));
	snd_soc_dapm_add_routes(dapm, route, ARRAY_SIZE(route));
}

static int hdac_hdmi_init_dai_map(struct hdac_ext_device *edev)
{
	struct hdac_hdmi_priv *hdmi = edev->private_data;
	struct hdac_hdmi_dai_pin_map *dai_map = &hdmi->dai_map[0];
	struct hdac_hdmi_cvt *cvt;
	struct hdac_hdmi_pin *pin;

	if (list_empty(&hdmi->cvt_list) || list_empty(&hdmi->pin_list))
		return -EINVAL;

	/*
	 * Currently on board only 1 pin and 1 converter is enabled for
	 * simplification, more will be added eventually
	 * So using fixed map for dai_id:pin:cvt
	 */
	cvt = list_first_entry(&hdmi->cvt_list, struct hdac_hdmi_cvt, head);
	pin = list_first_entry(&hdmi->pin_list, struct hdac_hdmi_pin, head);

	dai_map->dai_id = 0;
	dai_map->pin = pin;

	dai_map->cvt = cvt;

	/* Enable out path for this pin widget */
	snd_hdac_codec_write(&edev->hdac, pin->nid, 0,
			AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);

	/* Enable transmission */
	snd_hdac_codec_write(&edev->hdac, cvt->nid, 0,
			AC_VERB_SET_DIGI_CONVERT_1, 1);

	/* Category Code (CC) to zero */
	snd_hdac_codec_write(&edev->hdac, cvt->nid, 0,
			AC_VERB_SET_DIGI_CONVERT_2, 0);

	snd_hdac_codec_write(&edev->hdac, pin->nid, 0,
			AC_VERB_SET_CONNECT_SEL, 0);

	return 0;
}

static int hdac_hdmi_add_cvt(struct hdac_ext_device *edev, hda_nid_t nid)
{
	struct hdac_hdmi_priv *hdmi = edev->private_data;
	struct hdac_hdmi_cvt *cvt;

	cvt = kzalloc(sizeof(*cvt), GFP_KERNEL);
	if (!cvt)
		return -ENOMEM;

	cvt->nid = nid;

	list_add_tail(&cvt->head, &hdmi->cvt_list);
	hdmi->num_cvt++;

	return hdac_hdmi_query_cvt_params(&edev->hdac, cvt);
}

static int hdac_hdmi_add_pin(struct hdac_ext_device *edev, hda_nid_t nid)
{
	struct hdac_hdmi_priv *hdmi = edev->private_data;
	struct hdac_hdmi_pin *pin;

	pin = kzalloc(sizeof(*pin), GFP_KERNEL);
	if (!pin)
		return -ENOMEM;

	pin->nid = nid;

	list_add_tail(&pin->head, &hdmi->pin_list);
	hdmi->num_pin++;

	return 0;
}

/*
 * Parse all nodes and store the cvt/pin nids in array
 * Add one time initialization for pin and cvt widgets
 */
static int hdac_hdmi_parse_and_map_nid(struct hdac_ext_device *edev)
{
	hda_nid_t nid;
	int i, num_nodes;
	struct hdac_device *hdac = &edev->hdac;
	struct hdac_hdmi_priv *hdmi = edev->private_data;
	int ret;

	num_nodes = snd_hdac_get_sub_nodes(hdac, hdac->afg, &nid);
	if (!nid || num_nodes <= 0) {
		dev_warn(&hdac->dev, "HDMI: failed to get afg sub nodes\n");
		return -EINVAL;
	}

	hdac->num_nodes = num_nodes;
	hdac->start_nid = nid;

	for (i = 0; i < hdac->num_nodes; i++, nid++) {
		unsigned int caps;
		unsigned int type;

		caps = get_wcaps(hdac, nid);
		type = get_wcaps_type(caps);

		if (!(caps & AC_WCAP_DIGITAL))
			continue;

		switch (type) {

		case AC_WID_AUD_OUT:
			ret = hdac_hdmi_add_cvt(edev, nid);
			if (ret < 0)
				return ret;
			break;

		case AC_WID_PIN:
			ret = hdac_hdmi_add_pin(edev, nid);
			if (ret < 0)
				return ret;
			break;
		}
	}

	hdac->end_nid = nid;

	if (!hdmi->num_pin || !hdmi->num_cvt)
		return -EIO;

	return hdac_hdmi_init_dai_map(edev);
}

static int hdmi_codec_probe(struct snd_soc_codec *codec)
{
	struct hdac_ext_device *edev = snd_soc_codec_get_drvdata(codec);
	struct hdac_hdmi_priv *hdmi = edev->private_data;
	struct snd_soc_dapm_context *dapm =
		snd_soc_component_get_dapm(&codec->component);

	edev->scodec = codec;

	create_fill_widget_route_map(dapm, &hdmi->dai_map[0]);

	/* Imp: Store the card pointer in hda_codec */
	edev->card = dapm->card->snd_card;

	/*
	 * hdac_device core already sets the state to active and calls
	 * get_noresume. So enable runtime and set the device to suspend.
	 */
	pm_runtime_enable(&edev->hdac.dev);
	pm_runtime_put(&edev->hdac.dev);
	pm_runtime_suspend(&edev->hdac.dev);

	return 0;
}

static int hdmi_codec_remove(struct snd_soc_codec *codec)
{
	struct hdac_ext_device *edev = snd_soc_codec_get_drvdata(codec);

	pm_runtime_disable(&edev->hdac.dev);
	return 0;
}

static struct snd_soc_codec_driver hdmi_hda_codec = {
	.probe		= hdmi_codec_probe,
	.remove		= hdmi_codec_remove,
	.idle_bias_off	= true,
};

static struct snd_soc_dai_ops hdmi_dai_ops = {
	.startup = hdac_hdmi_pcm_open,
	.shutdown = hdac_hdmi_pcm_close,
	.hw_params = hdac_hdmi_set_hw_params,
	.prepare = hdac_hdmi_playback_prepare,
	.hw_free = hdac_hdmi_playback_cleanup,
};

static struct snd_soc_dai_driver hdmi_dais[] = {
	{	.name = "intel-hdmi-hif1",
		.playback = {
			.stream_name = "hif1",
			.channels_min = 2,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_32000 |
				SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
				SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S20_3LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S32_LE,

		},
		.ops = &hdmi_dai_ops,
	},
};

static int hdac_hdmi_dev_probe(struct hdac_ext_device *edev)
{
	struct hdac_device *codec = &edev->hdac;
	struct hdac_hdmi_priv *hdmi_priv;
	int ret = 0;

	hdmi_priv = devm_kzalloc(&codec->dev, sizeof(*hdmi_priv), GFP_KERNEL);
	if (hdmi_priv == NULL)
		return -ENOMEM;

	edev->private_data = hdmi_priv;

	dev_set_drvdata(&codec->dev, edev);

	INIT_LIST_HEAD(&hdmi_priv->pin_list);
	INIT_LIST_HEAD(&hdmi_priv->cvt_list);

	ret = hdac_hdmi_parse_and_map_nid(edev);
	if (ret < 0)
		return ret;

	/* ASoC specific initialization */
	return snd_soc_register_codec(&codec->dev, &hdmi_hda_codec,
			hdmi_dais, ARRAY_SIZE(hdmi_dais));
}

static int hdac_hdmi_dev_remove(struct hdac_ext_device *edev)
{
	struct hdac_hdmi_priv *hdmi = edev->private_data;
	struct hdac_hdmi_pin *pin, *pin_next;
	struct hdac_hdmi_cvt *cvt, *cvt_next;

	snd_soc_unregister_codec(&edev->hdac.dev);

	list_for_each_entry_safe(cvt, cvt_next, &hdmi->cvt_list, head) {
		list_del(&cvt->head);
		kfree(cvt);
	}

	list_for_each_entry_safe(pin, pin_next, &hdmi->pin_list, head) {
		list_del(&pin->head);
		kfree(pin);
	}

	return 0;
}

#ifdef CONFIG_PM
static int hdac_hdmi_runtime_suspend(struct device *dev)
{
	struct hdac_ext_device *edev = to_hda_ext_device(dev);
	struct hdac_device *hdac = &edev->hdac;
	struct hdac_bus *bus = hdac->bus;
	int err;

	dev_dbg(dev, "Enter: %s\n", __func__);

	/* controller may not have been initialized for the first time */
	if (!bus)
		return 0;

	/* Power down afg */
	if (!snd_hdac_check_power_state(hdac, hdac->afg, AC_PWRST_D3))
		snd_hdac_codec_write(hdac, hdac->afg, 0,
			AC_VERB_SET_POWER_STATE, AC_PWRST_D3);

	err = snd_hdac_display_power(bus, false);
	if (err < 0) {
		dev_err(bus->dev, "Cannot turn on display power on i915\n");
		return err;
	}

	return 0;
}

static int hdac_hdmi_runtime_resume(struct device *dev)
{
	struct hdac_ext_device *edev = to_hda_ext_device(dev);
	struct hdac_device *hdac = &edev->hdac;
	struct hdac_bus *bus = hdac->bus;
	int err;

	dev_dbg(dev, "Enter: %s\n", __func__);

	/* controller may not have been initialized for the first time */
	if (!bus)
		return 0;

	err = snd_hdac_display_power(bus, true);
	if (err < 0) {
		dev_err(bus->dev, "Cannot turn on display power on i915\n");
		return err;
	}

	/* Power up afg */
	if (!snd_hdac_check_power_state(hdac, hdac->afg, AC_PWRST_D0))
		snd_hdac_codec_write(hdac, hdac->afg, 0,
			AC_VERB_SET_POWER_STATE, AC_PWRST_D0);

	return 0;
}
#else
#define hdac_hdmi_runtime_suspend NULL
#define hdac_hdmi_runtime_resume NULL
#endif

static const struct dev_pm_ops hdac_hdmi_pm = {
	SET_RUNTIME_PM_OPS(hdac_hdmi_runtime_suspend, hdac_hdmi_runtime_resume, NULL)
};

static const struct hda_device_id hdmi_list[] = {
	HDA_CODEC_EXT_ENTRY(0x80862809, 0x100000, "Skylake HDMI", 0),
	{}
};

MODULE_DEVICE_TABLE(hdaudio, hdmi_list);

static struct hdac_ext_driver hdmi_driver = {
	. hdac = {
		.driver = {
			.name   = "HDMI HDA Codec",
			.pm = &hdac_hdmi_pm,
		},
		.id_table       = hdmi_list,
	},
	.probe          = hdac_hdmi_dev_probe,
	.remove         = hdac_hdmi_dev_remove,
};

static int __init hdmi_init(void)
{
	return snd_hda_ext_driver_register(&hdmi_driver);
}

static void __exit hdmi_exit(void)
{
	snd_hda_ext_driver_unregister(&hdmi_driver);
}

module_init(hdmi_init);
module_exit(hdmi_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("HDMI HD codec");
MODULE_AUTHOR("Samreen Nilofer<samreen.nilofer@intel.com>");
MODULE_AUTHOR("Subhransu S. Prusty<subhransu.s.prusty@intel.com>");
