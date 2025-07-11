// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Non-generic simple HDMI codec support
 */

#include <linux/slab.h>
#include <linux/module.h>
#include "hdmi_local.h"
#include "hda_jack.h"

int snd_hda_hdmi_simple_build_pcms(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	struct hda_pcm *info;
	unsigned int chans;
	struct hda_pcm_stream *pstr;
	struct hdmi_spec_per_cvt *per_cvt;

	per_cvt = get_cvt(spec, 0);
	chans = get_wcaps(codec, per_cvt->cvt_nid);
	chans = get_wcaps_channels(chans);

	info = snd_hda_codec_pcm_new(codec, "HDMI 0");
	if (!info)
		return -ENOMEM;
	spec->pcm_rec[0].pcm = info;
	info->pcm_type = HDA_PCM_TYPE_HDMI;
	pstr = &info->stream[SNDRV_PCM_STREAM_PLAYBACK];
	*pstr = spec->pcm_playback;
	pstr->nid = per_cvt->cvt_nid;
	if (pstr->channels_max <= 2 && chans && chans <= 16)
		pstr->channels_max = chans;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_simple_build_pcms, "SND_HDA_CODEC_HDMI");

/* unsolicited event for jack sensing */
void snd_hda_hdmi_simple_unsol_event(struct hda_codec *codec,
				     unsigned int res)
{
	snd_hda_jack_set_dirty_all(codec);
	snd_hda_jack_report_sync(codec);
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_simple_unsol_event, "SND_HDA_CODEC_HDMI");

static void free_hdmi_jack_priv(struct snd_jack *jack)
{
	struct hdmi_pcm *pcm = jack->private_data;

	pcm->jack = NULL;
}

static int simple_hdmi_build_jack(struct hda_codec *codec)
{
	char hdmi_str[32] = "HDMI/DP";
	struct hdmi_spec *spec = codec->spec;
	struct snd_jack *jack;
	struct hdmi_pcm *pcmp = get_hdmi_pcm(spec, 0);
	int pcmdev = pcmp->pcm->device;
	int err;

	if (pcmdev > 0)
		sprintf(hdmi_str + strlen(hdmi_str), ",pcm=%d", pcmdev);

	err = snd_jack_new(codec->card, hdmi_str, SND_JACK_AVOUT, &jack,
			   true, false);
	if (err < 0)
		return err;

	pcmp->jack = jack;
	jack->private_data = pcmp;
	jack->private_free = free_hdmi_jack_priv;
	return 0;
}

int snd_hda_hdmi_simple_build_controls(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_cvt *per_cvt;
	int err;

	per_cvt = get_cvt(spec, 0);
	err = snd_hda_create_dig_out_ctls(codec, per_cvt->cvt_nid,
					  per_cvt->cvt_nid,
					  HDA_PCM_TYPE_HDMI);
	if (err < 0)
		return err;
	return simple_hdmi_build_jack(codec);
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_simple_build_controls, "SND_HDA_CODEC_HDMI");

int snd_hda_hdmi_simple_init(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;
	struct hdmi_spec_per_pin *per_pin = get_pin(spec, 0);
	hda_nid_t pin = per_pin->pin_nid;

	snd_hda_codec_write(codec, pin, 0,
			    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
	/* some codecs require to unmute the pin */
	if (get_wcaps(codec, pin) & AC_WCAP_OUT_AMP)
		snd_hda_codec_write(codec, pin, 0, AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_OUT_UNMUTE);
	snd_hda_jack_detect_enable(codec, pin, per_pin->dev_id);
	return 0;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_simple_init, "SND_HDA_CODEC_HDMI");

void snd_hda_hdmi_simple_remove(struct hda_codec *codec)
{
	struct hdmi_spec *spec = codec->spec;

	snd_array_free(&spec->pins);
	snd_array_free(&spec->cvts);
	kfree(spec);
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_simple_remove, "SND_HDA_CODEC_HDMI");

int snd_hda_hdmi_simple_pcm_open(struct hda_pcm_stream *hinfo,
				 struct hda_codec *codec,
				 struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;

	if (spec->hw_constraints_channels) {
		snd_pcm_hw_constraint_list(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_CHANNELS,
				spec->hw_constraints_channels);
	} else {
		snd_pcm_hw_constraint_step(substream->runtime, 0,
					   SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	}

	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_simple_pcm_open, "SND_HDA_CODEC_HDMI");

static int simple_playback_pcm_close(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;

	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int simple_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       unsigned int stream_tag,
				       unsigned int format,
				       struct snd_pcm_substream *substream)
{
	struct hdmi_spec *spec = codec->spec;

	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout,
					     stream_tag, format, substream);
}

static const struct hda_pcm_stream simple_pcm_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.open = snd_hda_hdmi_simple_pcm_open,
		.close = simple_playback_pcm_close,
		.prepare = simple_playback_pcm_prepare
	},
};

int snd_hda_hdmi_simple_probe(struct hda_codec *codec,
			      hda_nid_t cvt_nid, hda_nid_t pin_nid)
{
	struct hdmi_spec *spec;
	struct hdmi_spec_per_cvt *per_cvt;
	struct hdmi_spec_per_pin *per_pin;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	spec->codec = codec;
	codec->spec = spec;
	snd_array_init(&spec->pins, sizeof(struct hdmi_spec_per_pin), 1);
	snd_array_init(&spec->cvts, sizeof(struct hdmi_spec_per_cvt), 1);

	spec->multiout.num_dacs = 0;  /* no analog */
	spec->multiout.max_channels = 2;
	spec->multiout.dig_out_nid = cvt_nid;
	spec->num_cvts = 1;
	spec->num_pins = 1;
	per_pin = snd_array_new(&spec->pins);
	per_cvt = snd_array_new(&spec->cvts);
	if (!per_pin || !per_cvt) {
		snd_hda_hdmi_simple_remove(codec);
		return -ENOMEM;
	}
	per_cvt->cvt_nid = cvt_nid;
	per_pin->pin_nid = pin_nid;
	spec->pcm_playback = simple_pcm_playback;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(snd_hda_hdmi_simple_probe, "SND_HDA_CODEC_HDMI");

/*
 * driver entries
 */

enum { MODEL_VIA };

/* VIA HDMI Implementation */
#define VIAHDMI_CVT_NID	0x02	/* audio converter1 */
#define VIAHDMI_PIN_NID	0x03	/* HDMI output pin1 */

static int simplehdmi_probe(struct hda_codec *codec,
			    const struct hda_device_id *id)
{
	switch (id->driver_data) {
	case MODEL_VIA:
		return snd_hda_hdmi_simple_probe(codec, VIAHDMI_CVT_NID,
						 VIAHDMI_PIN_NID);
	default:
		return -EINVAL;
	}
}

static const struct hda_codec_ops simplehdmi_codec_ops = {
	.probe = simplehdmi_probe,
	.remove = snd_hda_hdmi_simple_remove,
	.build_controls = snd_hda_hdmi_simple_build_controls,
	.build_pcms = snd_hda_hdmi_simple_build_pcms,
	.init = snd_hda_hdmi_simple_init,
	.unsol_event = snd_hda_hdmi_simple_unsol_event,
};

static const struct hda_device_id snd_hda_id_simplehdmi[] = {
	HDA_CODEC_ID_MODEL(0x11069f80, "VX900 HDMI/DP",	MODEL_VIA),
	HDA_CODEC_ID_MODEL(0x11069f81, "VX900 HDMI/DP",	MODEL_VIA),
	{} /* terminator */
};

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple HDMI HD-audio codec support");

static struct hda_codec_driver simplehdmi_driver = {
	.id = snd_hda_id_simplehdmi,
	.ops = &simplehdmi_codec_ops,
};

module_hda_codec_driver(simplehdmi_driver);
