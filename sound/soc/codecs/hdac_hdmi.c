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
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/hdaudio_ext.h>
#include "../../hda/local.h"

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
	hda_nid_t nid;
	struct hdac_hdmi_cvt_params params;
};

struct hdac_hdmi_pin {
	hda_nid_t nid;
	int num_mux_nids;
	hda_nid_t mux_nids[HDA_MAX_CONNECTIONS];
};

struct hdac_hdmi_dai_pin_map {
	int dai_id;
	struct hdac_hdmi_pin pin;
	struct hdac_hdmi_cvt cvt;
};

struct hdac_hdmi_priv {
	hda_nid_t pin_nid[3];
	hda_nid_t cvt_nid[3];
	struct hdac_hdmi_dai_pin_map dai_map[3];
};

static inline struct hdac_ext_device *to_hda_ext_device(struct device *dev)
{
	struct hdac_device *hdac = container_of(dev, struct hdac_device, dev);

	return container_of(hdac, struct hdac_ext_device, hdac);
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

static int hdac_hdmi_query_pin_connlist(struct hdac_ext_device *hdac,
					struct hdac_hdmi_pin *pin)
{
	if (!(get_wcaps(&hdac->hdac, pin->nid) & AC_WCAP_CONN_LIST)) {
		dev_warn(&hdac->hdac.dev,
			"HDMI: pin %d wcaps %#x does not support connection list\n",
			pin->nid, get_wcaps(&hdac->hdac, pin->nid));
		return -EINVAL;
	}

	pin->num_mux_nids = snd_hdac_get_connections(&hdac->hdac, pin->nid,
			pin->mux_nids, HDA_MAX_CONNECTIONS);
	if (pin->num_mux_nids == 0) {
		dev_err(&hdac->hdac.dev, "No connections found\n");
		return -ENODEV;
	}

	return pin->num_mux_nids;
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

static int hdac_hdmi_init_dai_map(struct hdac_ext_device *edev,
			struct hdac_hdmi_dai_pin_map *dai_map,
			hda_nid_t pin_nid, hda_nid_t cvt_nid, int dai_id)
{
	int ret;

	dai_map->dai_id = dai_id;
	dai_map->pin.nid = pin_nid;

	ret = hdac_hdmi_query_pin_connlist(edev, &dai_map->pin);
	if (ret < 0) {
		dev_err(&edev->hdac.dev,
			"Error querying connection list: %d\n", ret);
		return ret;
	}

	dai_map->cvt.nid = cvt_nid;

	/* Enable out path for this pin widget */
	snd_hdac_codec_write(&edev->hdac, pin_nid, 0,
			AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);

	/* Enable transmission */
	snd_hdac_codec_write(&edev->hdac, cvt_nid, 0,
			AC_VERB_SET_DIGI_CONVERT_1, 1);

	/* Category Code (CC) to zero */
	snd_hdac_codec_write(&edev->hdac, cvt_nid, 0,
			AC_VERB_SET_DIGI_CONVERT_2, 0);

	snd_hdac_codec_write(&edev->hdac, pin_nid, 0,
			AC_VERB_SET_CONNECT_SEL, 0);

	return hdac_hdmi_query_cvt_params(&edev->hdac, &dai_map->cvt);
}

/*
 * Parse all nodes and store the cvt/pin nids in array
 * Add one time initialization for pin and cvt widgets
 */
static int hdac_hdmi_parse_and_map_nid(struct hdac_ext_device *edev)
{
	hda_nid_t nid;
	int i;
	struct hdac_device *hdac = &edev->hdac;
	struct hdac_hdmi_priv *hdmi = edev->private_data;
	int cvt_nid = 0, pin_nid = 0;

	hdac->num_nodes = snd_hdac_get_sub_nodes(hdac, hdac->afg, &nid);
	if (!nid || hdac->num_nodes < 0) {
		dev_warn(&hdac->dev, "HDMI: failed to get afg sub nodes\n");
		return -EINVAL;
	}

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
			hdmi->cvt_nid[cvt_nid] = nid;
			cvt_nid++;
			break;

		case AC_WID_PIN:
			hdmi->pin_nid[pin_nid] = nid;
			pin_nid++;
			break;
		}
	}

	hdac->end_nid = nid;

	if (!pin_nid || !cvt_nid)
		return -EIO;

	/*
	 * Currently on board only 1 pin and 1 converter is enabled for
	 * simplification, more will be added eventually
	 * So using fixed map for dai_id:pin:cvt
	 */
	return hdac_hdmi_init_dai_map(edev, &hdmi->dai_map[0], hdmi->pin_nid[0],
			hdmi->cvt_nid[0], 0);
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

	ret = hdac_hdmi_parse_and_map_nid(edev);
	if (ret < 0)
		return ret;

	/* ASoC specific initialization */
	return snd_soc_register_codec(&codec->dev, &hdmi_hda_codec,
			hdmi_dais, ARRAY_SIZE(hdmi_dais));
}

static int hdac_hdmi_dev_remove(struct hdac_ext_device *edev)
{
	snd_soc_unregister_codec(&edev->hdac.dev);

	return 0;
}

#ifdef CONFIG_PM
static int hdac_hdmi_runtime_suspend(struct device *dev)
{
	struct hdac_ext_device *edev = to_hda_ext_device(dev);
	struct hdac_device *hdac = &edev->hdac;

	dev_dbg(dev, "Enter: %s\n", __func__);

	/* Power down afg */
	if (!snd_hdac_check_power_state(hdac, hdac->afg, AC_PWRST_D3))
		snd_hdac_codec_write(hdac, hdac->afg, 0,
			AC_VERB_SET_POWER_STATE, AC_PWRST_D3);

	return 0;
}

static int hdac_hdmi_runtime_resume(struct device *dev)
{
	struct hdac_ext_device *edev = to_hda_ext_device(dev);
	struct hdac_device *hdac = &edev->hdac;

	dev_dbg(dev, "Enter: %s\n", __func__);

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
