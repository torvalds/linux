/*
 * HD audio interface patch for Conexant HDA audio codec
 *
 * Copyright (c) 2006 Pototskiy Akex <alex.pototskiy@gmail.com>
 * 		      Takashi Iwai <tiwai@suse.de>
 * 		      Tobin Davis  <tdavis@dsl-only.net>
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"

#define CXT_PIN_DIR_IN              0x00
#define CXT_PIN_DIR_OUT             0x01
#define CXT_PIN_DIR_INOUT           0x02
#define CXT_PIN_DIR_IN_NOMICBIAS    0x03
#define CXT_PIN_DIR_INOUT_NOMICBIAS 0x04

#define CONEXANT_HP_EVENT	0x37
#define CONEXANT_MIC_EVENT	0x38



struct conexant_spec {

	struct snd_kcontrol_new *mixers[5];
	int num_mixers;

	const struct hda_verb *init_verbs[5];	/* initialization verbs
						 * don't forget NULL
						 * termination!
						 */
	unsigned int num_init_verbs;

	/* playback */
	struct hda_multi_out multiout;	/* playback set-up
					 * max_channels, dacs must be set
					 * dig_out_nid and hp_nid are optional
					 */
	unsigned int cur_eapd;
	unsigned int hp_present;
	unsigned int need_dac_fix;

	/* capture */
	unsigned int num_adc_nids;
	hda_nid_t *adc_nids;
	hda_nid_t dig_in_nid;		/* digital-in NID; optional */

	unsigned int cur_adc_idx;
	hda_nid_t cur_adc;
	unsigned int cur_adc_stream_tag;
	unsigned int cur_adc_format;

	/* capture source */
	const struct hda_input_mux *input_mux;
	hda_nid_t *capsrc_nids;
	unsigned int cur_mux[3];

	/* channel model */
	const struct hda_channel_mode *channel_mode;
	int num_channel_mode;

	/* PCM information */
	struct hda_pcm pcm_rec[2];	/* used in build_pcms() */

	struct mutex amp_mutex;	/* PCM volume/mute control mutex */
	unsigned int spdif_route;

	/* dynamic controls, init_verbs and input_mux */
	struct auto_pin_cfg autocfg;
	unsigned int num_kctl_alloc, num_kctl_used;
	struct snd_kcontrol_new *kctl_alloc;
	struct hda_input_mux private_imux;
	hda_nid_t private_dac_nids[AUTO_CFG_MAX_OUTS];

};

static int conexant_playback_pcm_open(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream);
}

static int conexant_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 unsigned int stream_tag,
					 unsigned int format,
					 struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout,
						stream_tag,
						format, substream);
}

static int conexant_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital out
 */
static int conexant_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					  struct hda_codec *codec,
					  struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int conexant_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int conexant_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 unsigned int stream_tag,
					 unsigned int format,
					 struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout,
					     stream_tag,
					     format, substream);
}

/*
 * Analog capture
 */
static int conexant_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
				   stream_tag, 0, format);
	return 0;
}

static int conexant_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
				   0, 0, 0);
	return 0;
}



static struct hda_pcm_stream conexant_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0, /* fill later */
	.ops = {
		.open = conexant_playback_pcm_open,
		.prepare = conexant_playback_pcm_prepare,
		.cleanup = conexant_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream conexant_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0, /* fill later */
	.ops = {
		.prepare = conexant_capture_pcm_prepare,
		.cleanup = conexant_capture_pcm_cleanup
	},
};


static struct hda_pcm_stream conexant_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0, /* fill later */
	.ops = {
		.open = conexant_dig_playback_pcm_open,
		.close = conexant_dig_playback_pcm_close,
		.prepare = conexant_dig_playback_pcm_prepare
	},
};

static struct hda_pcm_stream conexant_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

static int cx5051_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	spec->cur_adc = spec->adc_nids[spec->cur_adc_idx];
	spec->cur_adc_stream_tag = stream_tag;
	spec->cur_adc_format = format;
	snd_hda_codec_setup_stream(codec, spec->cur_adc, stream_tag, 0, format);
	return 0;
}

static int cx5051_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	snd_hda_codec_setup_stream(codec, spec->cur_adc, 0, 0, 0);
	spec->cur_adc = 0;
	return 0;
}

static struct hda_pcm_stream cx5051_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0, /* fill later */
	.ops = {
		.prepare = cx5051_capture_pcm_prepare,
		.cleanup = cx5051_capture_pcm_cleanup
	},
};

static int conexant_build_pcms(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "CONEXANT Analog";
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = conexant_pcm_analog_playback;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max =
		spec->multiout.max_channels;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
		spec->multiout.dac_nids[0];
	if (codec->vendor_id == 0x14f15051)
		info->stream[SNDRV_PCM_STREAM_CAPTURE] =
			cx5051_pcm_analog_capture;
	else
		info->stream[SNDRV_PCM_STREAM_CAPTURE] =
			conexant_pcm_analog_capture;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams = spec->num_adc_nids;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adc_nids[0];

	if (spec->multiout.dig_out_nid) {
		info++;
		codec->num_pcms++;
		info->name = "Conexant Digital";
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
			conexant_pcm_digital_playback;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
			spec->multiout.dig_out_nid;
		if (spec->dig_in_nid) {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] =
				conexant_pcm_digital_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid =
				spec->dig_in_nid;
		}
	}

	return 0;
}

static int conexant_mux_enum_info(struct snd_kcontrol *kcontrol,
	       			  struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;

	return snd_hda_input_mux_info(spec->input_mux, uinfo);
}

static int conexant_mux_enum_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int conexant_mux_enum_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
				     spec->capsrc_nids[adc_idx],
				     &spec->cur_mux[adc_idx]);
}

static int conexant_init(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->num_init_verbs; i++)
		snd_hda_sequence_write(codec, spec->init_verbs[i]);
	return 0;
}

static void conexant_free(struct hda_codec *codec)
{
        struct conexant_spec *spec = codec->spec;
        unsigned int i;

        if (spec->kctl_alloc) {
                for (i = 0; i < spec->num_kctl_used; i++)
                        kfree(spec->kctl_alloc[i].name);
                kfree(spec->kctl_alloc);
        }

	kfree(codec->spec);
}

static int conexant_build_controls(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int i;
	int err;

	for (i = 0; i < spec->num_mixers; i++) {
		err = snd_hda_add_new_ctls(codec, spec->mixers[i]);
		if (err < 0)
			return err;
	}
	if (spec->multiout.dig_out_nid) {
		err = snd_hda_create_spdif_out_ctls(codec,
						    spec->multiout.dig_out_nid);
		if (err < 0)
			return err;
	} 
	if (spec->dig_in_nid) {
		err = snd_hda_create_spdif_in_ctls(codec,spec->dig_in_nid);
		if (err < 0)
			return err;
	}
	return 0;
}

static struct hda_codec_ops conexant_patch_ops = {
	.build_controls = conexant_build_controls,
	.build_pcms = conexant_build_pcms,
	.init = conexant_init,
	.free = conexant_free,
};

/*
 * EAPD control
 * the private value = nid | (invert << 8)
 */

#define cxt_eapd_info		snd_ctl_boolean_mono_info

static int cxt_eapd_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	int invert = (kcontrol->private_value >> 8) & 1;
	if (invert)
		ucontrol->value.integer.value[0] = !spec->cur_eapd;
	else
		ucontrol->value.integer.value[0] = spec->cur_eapd;
	return 0;

}

static int cxt_eapd_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	int invert = (kcontrol->private_value >> 8) & 1;
	hda_nid_t nid = kcontrol->private_value & 0xff;
	unsigned int eapd;

	eapd = !!ucontrol->value.integer.value[0];
	if (invert)
		eapd = !eapd;
	if (eapd == spec->cur_eapd)
		return 0;
	
	spec->cur_eapd = eapd;
	snd_hda_codec_write_cache(codec, nid,
				  0, AC_VERB_SET_EAPD_BTLENABLE,
				  eapd ? 0x02 : 0x00);
	return 1;
}

/* controls for test mode */
#ifdef CONFIG_SND_DEBUG

#define CXT_EAPD_SWITCH(xname, nid, mask) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0,  \
	  .info = cxt_eapd_info, \
	  .get = cxt_eapd_get, \
	  .put = cxt_eapd_put, \
	  .private_value = nid | (mask<<16) }



static int conexant_ch_mode_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	return snd_hda_ch_mode_info(codec, uinfo, spec->channel_mode,
				    spec->num_channel_mode);
}

static int conexant_ch_mode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	return snd_hda_ch_mode_get(codec, ucontrol, spec->channel_mode,
				   spec->num_channel_mode,
				   spec->multiout.max_channels);
}

static int conexant_ch_mode_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	int err = snd_hda_ch_mode_put(codec, ucontrol, spec->channel_mode,
				      spec->num_channel_mode,
				      &spec->multiout.max_channels);
	if (err >= 0 && spec->need_dac_fix)
		spec->multiout.num_dacs = spec->multiout.max_channels / 2;
	return err;
}

#define CXT_PIN_MODE(xname, nid, dir) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0,  \
	  .info = conexant_ch_mode_info, \
	  .get = conexant_ch_mode_get, \
	  .put = conexant_ch_mode_put, \
	  .private_value = nid | (dir<<16) }

#endif /* CONFIG_SND_DEBUG */

/* Conexant 5045 specific */

static hda_nid_t cxt5045_dac_nids[1] = { 0x19 };
static hda_nid_t cxt5045_adc_nids[1] = { 0x1a };
static hda_nid_t cxt5045_capsrc_nids[1] = { 0x1a };
#define CXT5045_SPDIF_OUT	0x18

static struct hda_channel_mode cxt5045_modes[1] = {
	{ 2, NULL },
};

static struct hda_input_mux cxt5045_capture_source = {
	.num_items = 2,
	.items = {
		{ "IntMic", 0x1 },
		{ "ExtMic", 0x2 },
	}
};

static struct hda_input_mux cxt5045_capture_source_benq = {
	.num_items = 3,
	.items = {
		{ "IntMic", 0x1 },
		{ "ExtMic", 0x2 },
		{ "LineIn", 0x3 },
	}
};

/* turn on/off EAPD (+ mute HP) as a master switch */
static int cxt5045_hp_master_sw_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	unsigned int bits;

	if (!cxt_eapd_put(kcontrol, ucontrol))
		return 0;

	/* toggle internal speakers mute depending of presence of
	 * the headphone jack
	 */
	bits = (!spec->hp_present && spec->cur_eapd) ? 0 : HDA_AMP_MUTE;
	snd_hda_codec_amp_stereo(codec, 0x10, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);

	bits = spec->cur_eapd ? 0 : HDA_AMP_MUTE;
	snd_hda_codec_amp_stereo(codec, 0x11, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	return 1;
}

/* bind volumes of both NID 0x10 and 0x11 */
static struct hda_bind_ctls cxt5045_hp_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x10, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x11, 3, 0, HDA_OUTPUT),
		0
	},
};

/* toggle input of built-in and mic jack appropriately */
static void cxt5045_hp_automic(struct hda_codec *codec)
{
	static struct hda_verb mic_jack_on[] = {
		{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
		{0x12, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
		{}
	};
	static struct hda_verb mic_jack_off[] = {
		{0x12, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
		{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
		{}
	};
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x12, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	if (present)
		snd_hda_sequence_write(codec, mic_jack_on);
	else
		snd_hda_sequence_write(codec, mic_jack_off);
}


/* mute internal speaker if HP is plugged */
static void cxt5045_hp_automute(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int bits;

	spec->hp_present = snd_hda_codec_read(codec, 0x11, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;

	bits = (spec->hp_present || !spec->cur_eapd) ? HDA_AMP_MUTE : 0; 
	snd_hda_codec_amp_stereo(codec, 0x10, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

/* unsolicited event for HP jack sensing */
static void cxt5045_hp_unsol_event(struct hda_codec *codec,
				   unsigned int res)
{
	res >>= 26;
	switch (res) {
	case CONEXANT_HP_EVENT:
		cxt5045_hp_automute(codec);
		break;
	case CONEXANT_MIC_EVENT:
		cxt5045_hp_automic(codec);
		break;

	}
}

static struct snd_kcontrol_new cxt5045_mixers[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = conexant_mux_enum_info,
		.get = conexant_mux_enum_get,
		.put = conexant_mux_enum_put
	},
	HDA_CODEC_VOLUME("Int Mic Capture Volume", 0x1a, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Capture Switch", 0x1a, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Ext Mic Capture Volume", 0x1a, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Ext Mic Capture Switch", 0x1a, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x17, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x17, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Ext Mic Playback Volume", 0x17, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Ext Mic Playback Switch", 0x17, 0x2, HDA_INPUT),
	HDA_BIND_VOL("Master Playback Volume", &cxt5045_hp_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = cxt_eapd_info,
		.get = cxt_eapd_get,
		.put = cxt5045_hp_master_sw_put,
		.private_value = 0x10,
	},

	{}
};

static struct snd_kcontrol_new cxt5045_benq_mixers[] = {
	HDA_CODEC_VOLUME("Line In Capture Volume", 0x1a, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Line In Capture Switch", 0x1a, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("Line In Playback Volume", 0x17, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Line In Playback Switch", 0x17, 0x3, HDA_INPUT),

	{}
};

static struct hda_verb cxt5045_init_verbs[] = {
	/* Line in, Mic */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN|AC_PINCTL_VREF_80 },
	/* HP, Amp  */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Record selector: Int mic */
	{0x1a, AC_VERB_SET_CONNECT_SEL,0x1},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE,
	 AC_AMP_SET_INPUT|AC_AMP_SET_RIGHT|AC_AMP_SET_LEFT|0x17},
	/* SPDIF route: PCM */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{ 0x13, AC_VERB_SET_CONNECT_SEL, 0x0 },
	/* EAPD */
	{0x10, AC_VERB_SET_EAPD_BTLENABLE, 0x2 }, /* default on */ 
	{ } /* end */
};

static struct hda_verb cxt5045_benq_init_verbs[] = {
	/* Int Mic, Mic */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN|AC_PINCTL_VREF_80 },
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN|AC_PINCTL_VREF_80 },
	/* Line In,HP, Amp  */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Record selector: Int mic */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x1},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE,
	 AC_AMP_SET_INPUT|AC_AMP_SET_RIGHT|AC_AMP_SET_LEFT|0x17},
	/* SPDIF route: PCM */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* EAPD */
	{0x10, AC_VERB_SET_EAPD_BTLENABLE, 0x2}, /* default on */
	{ } /* end */
};

static struct hda_verb cxt5045_hp_sense_init_verbs[] = {
	/* pin sensing on HP jack */
	{0x11, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_HP_EVENT},
	{ } /* end */
};

static struct hda_verb cxt5045_mic_sense_init_verbs[] = {
	/* pin sensing on HP jack */
	{0x12, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_MIC_EVENT},
	{ } /* end */
};

#ifdef CONFIG_SND_DEBUG
/* Test configuration for debugging, modelled after the ALC260 test
 * configuration.
 */
static struct hda_input_mux cxt5045_test_capture_source = {
	.num_items = 5,
	.items = {
		{ "MIXER", 0x0 },
		{ "MIC1 pin", 0x1 },
		{ "LINE1 pin", 0x2 },
		{ "HP-OUT pin", 0x3 },
		{ "CD pin", 0x4 },
        },
};

static struct snd_kcontrol_new cxt5045_test_mixer[] = {

	/* Output controls */
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x10, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x10, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Node 11 Playback Volume", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Node 11 Playback Switch", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Node 12 Playback Volume", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Node 12 Playback Switch", 0x12, 0x0, HDA_OUTPUT),
	
	/* Modes for retasking pin widgets */
	CXT_PIN_MODE("HP-OUT pin mode", 0x11, CXT_PIN_DIR_INOUT),
	CXT_PIN_MODE("LINE1 pin mode", 0x12, CXT_PIN_DIR_INOUT),

	/* EAPD Switch Control */
	CXT_EAPD_SWITCH("External Amplifier", 0x10, 0x0),

	/* Loopback mixer controls */

	HDA_CODEC_VOLUME("Mixer-1 Volume", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mixer-1 Switch", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mixer-2 Volume", 0x17, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Mixer-2 Switch", 0x17, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Mixer-3 Volume", 0x17, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Mixer-3 Switch", 0x17, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mixer-4 Volume", 0x17, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Mixer-4 Switch", 0x17, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("Mixer-5 Volume", 0x17, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("Mixer-5 Switch", 0x17, 0x4, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Input Source",
		.info = conexant_mux_enum_info,
		.get = conexant_mux_enum_get,
		.put = conexant_mux_enum_put,
	},
	/* Audio input controls */
	HDA_CODEC_VOLUME("Input-1 Volume", 0x1a, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Input-1 Switch", 0x1a, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-2 Volume", 0x1a, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Input-2 Switch", 0x1a, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-3 Volume", 0x1a, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Input-3 Switch", 0x1a, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-4 Volume", 0x1a, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Input-4 Switch", 0x1a, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-5 Volume", 0x1a, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("Input-5 Switch", 0x1a, 0x4, HDA_INPUT),
	{ } /* end */
};

static struct hda_verb cxt5045_test_init_verbs[] = {
	/* Set connections */
	{ 0x10, AC_VERB_SET_CONNECT_SEL, 0x0 },
	{ 0x11, AC_VERB_SET_CONNECT_SEL, 0x0 },
	{ 0x12, AC_VERB_SET_CONNECT_SEL, 0x0 },
	/* Enable retasking pins as output, initially without power amp */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* Disable digital (SPDIF) pins initially, but users can enable
	 * them via a mixer switch.  In the case of SPDIF-out, this initverb
	 * payload also sets the generation to 0, output to be in "consumer"
	 * PCM format, copyright asserted, no pre-emphasis and no validity
	 * control.
	 */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x18, AC_VERB_SET_DIGI_CONVERT_1, 0},

	/* Start with output sum widgets muted and their output gains at min */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	/* Unmute retasking pin widget output buffers since the default
	 * state appears to be output.  As the pin mode is changed by the
	 * user the pin mode control will take care of enabling the pin's
	 * input/output buffers as needed.
	 */
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Mute capture amp left and right */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},

	/* Set ADC connection select to match default mixer setting (mic1
	 * pin)
	 */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Mute all inputs to mixer widget (even unconnected ones) */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)}, /* Mixer pin */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)}, /* Mic1 pin */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)}, /* Line pin */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)}, /* HP pin */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)}, /* CD pin */

	{ }
};
#endif


/* initialize jack-sensing, too */
static int cxt5045_init(struct hda_codec *codec)
{
	conexant_init(codec);
	cxt5045_hp_automute(codec);
	return 0;
}


enum {
	CXT5045_LAPTOP_HPSENSE,
	CXT5045_LAPTOP_MICSENSE,
	CXT5045_LAPTOP_HPMICSENSE,
	CXT5045_BENQ,
#ifdef CONFIG_SND_DEBUG
	CXT5045_TEST,
#endif
	CXT5045_MODELS
};

static const char *cxt5045_models[CXT5045_MODELS] = {
	[CXT5045_LAPTOP_HPSENSE]	= "laptop-hpsense",
	[CXT5045_LAPTOP_MICSENSE]	= "laptop-micsense",
	[CXT5045_LAPTOP_HPMICSENSE]	= "laptop-hpmicsense",
	[CXT5045_BENQ]			= "benq",
#ifdef CONFIG_SND_DEBUG
	[CXT5045_TEST]		= "test",
#endif
};

static struct snd_pci_quirk cxt5045_cfg_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x30a5, "HP", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x103c, 0x30b2, "HP DV Series", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x103c, 0x30b5, "HP DV2120", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x103c, 0x30b7, "HP DV6000Z", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x103c, 0x30bb, "HP DV8000", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x103c, 0x30cd, "HP DV Series", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x103c, 0x30cf, "HP DV9533EG", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x103c, 0x30d5, "HP 530", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x103c, 0x30d9, "HP Spartan", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x152d, 0x0753, "Benq R55E", CXT5045_BENQ),
	SND_PCI_QUIRK(0x1734, 0x10ad, "Fujitsu Si1520", CXT5045_LAPTOP_MICSENSE),
	SND_PCI_QUIRK(0x1734, 0x10cb, "Fujitsu Si3515", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x1734, 0x110e, "Fujitsu V5505", CXT5045_LAPTOP_HPSENSE),
	SND_PCI_QUIRK(0x1509, 0x1e40, "FIC", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x1509, 0x2f05, "FIC", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x1509, 0x2f06, "FIC", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x1631, 0xc106, "Packard Bell", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x1631, 0xc107, "Packard Bell", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x8086, 0x2111, "Conexant Reference board", CXT5045_LAPTOP_HPSENSE),
	{}
};

static int patch_cxt5045(struct hda_codec *codec)
{
	struct conexant_spec *spec;
	int board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	mutex_init(&spec->amp_mutex);
	codec->spec = spec;

	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = ARRAY_SIZE(cxt5045_dac_nids);
	spec->multiout.dac_nids = cxt5045_dac_nids;
	spec->multiout.dig_out_nid = CXT5045_SPDIF_OUT;
	spec->num_adc_nids = 1;
	spec->adc_nids = cxt5045_adc_nids;
	spec->capsrc_nids = cxt5045_capsrc_nids;
	spec->input_mux = &cxt5045_capture_source;
	spec->num_mixers = 1;
	spec->mixers[0] = cxt5045_mixers;
	spec->num_init_verbs = 1;
	spec->init_verbs[0] = cxt5045_init_verbs;
	spec->spdif_route = 0;
	spec->num_channel_mode = ARRAY_SIZE(cxt5045_modes),
	spec->channel_mode = cxt5045_modes,


	codec->patch_ops = conexant_patch_ops;

	board_config = snd_hda_check_board_config(codec, CXT5045_MODELS,
						  cxt5045_models,
						  cxt5045_cfg_tbl);
	switch (board_config) {
	case CXT5045_LAPTOP_HPSENSE:
		codec->patch_ops.unsol_event = cxt5045_hp_unsol_event;
		spec->input_mux = &cxt5045_capture_source;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = cxt5045_hp_sense_init_verbs;
		spec->mixers[0] = cxt5045_mixers;
		codec->patch_ops.init = cxt5045_init;
		break;
	case CXT5045_LAPTOP_MICSENSE:
		spec->input_mux = &cxt5045_capture_source;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = cxt5045_mic_sense_init_verbs;
		spec->mixers[0] = cxt5045_mixers;
		codec->patch_ops.init = cxt5045_init;
		break;
	default:
	case CXT5045_LAPTOP_HPMICSENSE:
		codec->patch_ops.unsol_event = cxt5045_hp_unsol_event;
		spec->input_mux = &cxt5045_capture_source;
		spec->num_init_verbs = 3;
		spec->init_verbs[1] = cxt5045_hp_sense_init_verbs;
		spec->init_verbs[2] = cxt5045_mic_sense_init_verbs;
		spec->mixers[0] = cxt5045_mixers;
		codec->patch_ops.init = cxt5045_init;
		break;
	case CXT5045_BENQ:
		codec->patch_ops.unsol_event = cxt5045_hp_unsol_event;
		spec->input_mux = &cxt5045_capture_source_benq;
		spec->num_init_verbs = 1;
		spec->init_verbs[0] = cxt5045_benq_init_verbs;
		spec->mixers[0] = cxt5045_mixers;
		spec->mixers[1] = cxt5045_benq_mixers;
		spec->num_mixers = 2;
		codec->patch_ops.init = cxt5045_init;
		break;
#ifdef CONFIG_SND_DEBUG
	case CXT5045_TEST:
		spec->input_mux = &cxt5045_test_capture_source;
		spec->mixers[0] = cxt5045_test_mixer;
		spec->init_verbs[0] = cxt5045_test_init_verbs;
		break;
		
#endif	
	}

	/*
	 * Fix max PCM level to 0 dB
	 * (originall it has 0x2b steps with 0dB offset 0x14)
	 */
	snd_hda_override_amp_caps(codec, 0x17, HDA_INPUT,
				  (0x14 << AC_AMPCAP_OFFSET_SHIFT) |
				  (0x14 << AC_AMPCAP_NUM_STEPS_SHIFT) |
				  (0x05 << AC_AMPCAP_STEP_SIZE_SHIFT) |
				  (1 << AC_AMPCAP_MUTE_SHIFT));

	return 0;
}


/* Conexant 5047 specific */
#define CXT5047_SPDIF_OUT	0x11

static hda_nid_t cxt5047_dac_nids[2] = { 0x10, 0x1c };
static hda_nid_t cxt5047_adc_nids[1] = { 0x12 };
static hda_nid_t cxt5047_capsrc_nids[1] = { 0x1a };

static struct hda_channel_mode cxt5047_modes[1] = {
	{ 2, NULL },
};

static struct hda_input_mux cxt5047_capture_source = {
	.num_items = 1,
	.items = {
		{ "Mic", 0x2 },
	}
};

static struct hda_input_mux cxt5047_hp_capture_source = {
	.num_items = 1,
	.items = {
		{ "ExtMic", 0x2 },
	}
};

static struct hda_input_mux cxt5047_toshiba_capture_source = {
	.num_items = 2,
	.items = {
		{ "ExtMic", 0x2 },
		{ "Line-In", 0x1 },
	}
};

/* turn on/off EAPD (+ mute HP) as a master switch */
static int cxt5047_hp_master_sw_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	unsigned int bits;

	if (!cxt_eapd_put(kcontrol, ucontrol))
		return 0;

	/* toggle internal speakers mute depending of presence of
	 * the headphone jack
	 */
	bits = (!spec->hp_present && spec->cur_eapd) ? 0 : HDA_AMP_MUTE;
	snd_hda_codec_amp_stereo(codec, 0x1d, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	bits = spec->cur_eapd ? 0 : HDA_AMP_MUTE;
	snd_hda_codec_amp_stereo(codec, 0x13, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	return 1;
}

/* bind volumes of both NID 0x13 (Headphones) and 0x1d (Speakers) */
static struct hda_bind_ctls cxt5047_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x13, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x1d, 3, 0, HDA_OUTPUT),
		0
	},
};

/* mute internal speaker if HP is plugged */
static void cxt5047_hp_automute(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int bits;

	spec->hp_present = snd_hda_codec_read(codec, 0x13, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;

	bits = (spec->hp_present || !spec->cur_eapd) ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x1d, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	/* Mute/Unmute PCM 2 for good measure - some systems need this */
	snd_hda_codec_amp_stereo(codec, 0x1c, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

/* mute internal speaker if HP is plugged */
static void cxt5047_hp2_automute(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int bits;

	spec->hp_present = snd_hda_codec_read(codec, 0x13, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;

	bits = spec->hp_present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x1d, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	/* Mute/Unmute PCM 2 for good measure - some systems need this */
	snd_hda_codec_amp_stereo(codec, 0x1c, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

/* toggle input of built-in and mic jack appropriately */
static void cxt5047_hp_automic(struct hda_codec *codec)
{
	static struct hda_verb mic_jack_on[] = {
		{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
		{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
		{}
	};
	static struct hda_verb mic_jack_off[] = {
		{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
		{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
		{}
	};
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x15, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	if (present)
		snd_hda_sequence_write(codec, mic_jack_on);
	else
		snd_hda_sequence_write(codec, mic_jack_off);
}

/* unsolicited event for HP jack sensing */
static void cxt5047_hp_unsol_event(struct hda_codec *codec,
				  unsigned int res)
{
	switch (res >> 26) {
	case CONEXANT_HP_EVENT:
		cxt5047_hp_automute(codec);
		break;
	case CONEXANT_MIC_EVENT:
		cxt5047_hp_automic(codec);
		break;
	}
}

/* unsolicited event for HP jack sensing - non-EAPD systems */
static void cxt5047_hp2_unsol_event(struct hda_codec *codec,
				  unsigned int res)
{
	res >>= 26;
	switch (res) {
	case CONEXANT_HP_EVENT:
		cxt5047_hp2_automute(codec);
		break;
	case CONEXANT_MIC_EVENT:
		cxt5047_hp_automic(codec);
		break;
	}
}

static struct snd_kcontrol_new cxt5047_mixers[] = {
	HDA_CODEC_VOLUME("Mic Bypass Capture Volume", 0x19, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Bypass Capture Switch", 0x19, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Gain Volume", 0x1a, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mic Gain Switch", 0x1a, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("PCM Volume", 0x10, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Switch", 0x10, 0x00, HDA_OUTPUT),
	HDA_CODEC_VOLUME("PCM-2 Volume", 0x1c, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM-2 Switch", 0x1c, 0x00, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x1d, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x1d, 0x00, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x13, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x13, 0x00, HDA_OUTPUT),

	{}
};

static struct snd_kcontrol_new cxt5047_toshiba_mixers[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = conexant_mux_enum_info,
		.get = conexant_mux_enum_get,
		.put = conexant_mux_enum_put
	},
	HDA_CODEC_VOLUME("Mic Bypass Capture Volume", 0x19, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Bypass Capture Switch", 0x19, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("PCM Volume", 0x10, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Switch", 0x10, 0x00, HDA_OUTPUT),
	HDA_BIND_VOL("Master Playback Volume", &cxt5047_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = cxt_eapd_info,
		.get = cxt_eapd_get,
		.put = cxt5047_hp_master_sw_put,
		.private_value = 0x13,
	},

	{}
};

static struct snd_kcontrol_new cxt5047_hp_mixers[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = conexant_mux_enum_info,
		.get = conexant_mux_enum_get,
		.put = conexant_mux_enum_put
	},
	HDA_CODEC_VOLUME("Mic Bypass Capture Volume", 0x19, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Bypass Capture Switch", 0x19,0x02,HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("PCM Volume", 0x10, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Switch", 0x10, 0x00, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Master Playback Volume", 0x13, 0x00, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = cxt_eapd_info,
		.get = cxt_eapd_get,
		.put = cxt5047_hp_master_sw_put,
		.private_value = 0x13,
	},
	{ } /* end */
};

static struct hda_verb cxt5047_init_verbs[] = {
	/* Line in, Mic, Built-in Mic */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN|AC_PINCTL_VREF_50 },
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN|AC_PINCTL_VREF_50 },
	/* HP, Speaker  */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{0x13, AC_VERB_SET_CONNECT_SEL,0x1},
	{0x1d, AC_VERB_SET_CONNECT_SEL,0x0},
	/* Record selector: Mic */
	{0x12, AC_VERB_SET_CONNECT_SEL,0x03},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE,
	 AC_AMP_SET_INPUT|AC_AMP_SET_RIGHT|AC_AMP_SET_LEFT|0x17},
	{0x1A, AC_VERB_SET_CONNECT_SEL,0x02},
	{0x1A, AC_VERB_SET_AMP_GAIN_MUTE,
	 AC_AMP_SET_OUTPUT|AC_AMP_SET_RIGHT|AC_AMP_SET_LEFT|0x00},
	{0x1A, AC_VERB_SET_AMP_GAIN_MUTE,
	 AC_AMP_SET_OUTPUT|AC_AMP_SET_RIGHT|AC_AMP_SET_LEFT|0x03},
	/* SPDIF route: PCM */
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x0 },
	/* Enable unsolicited events */
	{0x13, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_HP_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_MIC_EVENT},
	{ } /* end */
};

/* configuration for Toshiba Laptops */
static struct hda_verb cxt5047_toshiba_init_verbs[] = {
	{0x13, AC_VERB_SET_EAPD_BTLENABLE, 0x0 }, /* default on */
	/* pin sensing on HP and Mic jacks */
	{0x13, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_HP_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_MIC_EVENT},
	/* Speaker routing */
	{0x1d, AC_VERB_SET_CONNECT_SEL,0x1},
	{}
};

/* configuration for HP Laptops */
static struct hda_verb cxt5047_hp_init_verbs[] = {
	/* pin sensing on HP jack */
	{0x13, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_HP_EVENT},
	/* 0x13 is actually shared by both HP and speaker;
	 * setting the connection to 0 (=0x19) makes the master volume control
	 * working mysteriouslly...
	 */
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Record selector: Ext Mic */
	{0x12, AC_VERB_SET_CONNECT_SEL,0x03},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE,
	 AC_AMP_SET_INPUT|AC_AMP_SET_RIGHT|AC_AMP_SET_LEFT|0x17},
	/* Speaker routing */
	{0x1d, AC_VERB_SET_CONNECT_SEL,0x1},
	{}
};

/* Test configuration for debugging, modelled after the ALC260 test
 * configuration.
 */
#ifdef CONFIG_SND_DEBUG
static struct hda_input_mux cxt5047_test_capture_source = {
	.num_items = 4,
	.items = {
		{ "LINE1 pin", 0x0 },
		{ "MIC1 pin", 0x1 },
		{ "MIC2 pin", 0x2 },
		{ "CD pin", 0x3 },
        },
};

static struct snd_kcontrol_new cxt5047_test_mixer[] = {

	/* Output only controls */
	HDA_CODEC_VOLUME("OutAmp-1 Volume", 0x10, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("OutAmp-1 Switch", 0x10,0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("OutAmp-2 Volume", 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("OutAmp-2 Switch", 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x1d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x1d, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("HeadPhone Playback Volume", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("HeadPhone Playback Switch", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line1-Out Playback Volume", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Line1-Out Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line2-Out Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Line2-Out Playback Switch", 0x15, 0x0, HDA_OUTPUT),

	/* Modes for retasking pin widgets */
	CXT_PIN_MODE("LINE1 pin mode", 0x14, CXT_PIN_DIR_INOUT),
	CXT_PIN_MODE("MIC1 pin mode", 0x15, CXT_PIN_DIR_INOUT),

	/* EAPD Switch Control */
	CXT_EAPD_SWITCH("External Amplifier", 0x13, 0x0),

	/* Loopback mixer controls */
	HDA_CODEC_VOLUME("MIC1 Playback Volume", 0x12, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("MIC1 Playback Switch", 0x12, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("MIC2 Playback Volume", 0x12, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("MIC2 Playback Switch", 0x12, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("LINE Playback Volume", 0x12, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("LINE Playback Switch", 0x12, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x12, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x12, 0x04, HDA_INPUT),

	HDA_CODEC_VOLUME("Capture-1 Volume", 0x19, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture-1 Switch", 0x19, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture-2 Volume", 0x19, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Capture-2 Switch", 0x19, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture-3 Volume", 0x19, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Capture-3 Switch", 0x19, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture-4 Volume", 0x19, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Capture-4 Switch", 0x19, 0x3, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Input Source",
		.info = conexant_mux_enum_info,
		.get = conexant_mux_enum_get,
		.put = conexant_mux_enum_put,
	},
	HDA_CODEC_VOLUME("Input-1 Volume", 0x1a, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Input-1 Switch", 0x1a, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-2 Volume", 0x1a, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Input-2 Switch", 0x1a, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-3 Volume", 0x1a, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Input-3 Switch", 0x1a, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-4 Volume", 0x1a, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Input-4 Switch", 0x1a, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("Input-5 Volume", 0x1a, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("Input-5 Switch", 0x1a, 0x4, HDA_INPUT),

	{ } /* end */
};

static struct hda_verb cxt5047_test_init_verbs[] = {
	/* Enable retasking pins as output, initially without power amp */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* Disable digital (SPDIF) pins initially, but users can enable
	 * them via a mixer switch.  In the case of SPDIF-out, this initverb
	 * payload also sets the generation to 0, output to be in "consumer"
	 * PCM format, copyright asserted, no pre-emphasis and no validity
	 * control.
	 */
	{0x18, AC_VERB_SET_DIGI_CONVERT_1, 0},

	/* Ensure mic1, mic2, line1 pin widgets take input from the 
	 * OUT1 sum bus when acting as an output.
	 */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0},

	/* Start with output sum widgets muted and their output gains at min */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	/* Unmute retasking pin widget output buffers since the default
	 * state appears to be output.  As the pin mode is changed by the
	 * user the pin mode control will take care of enabling the pin's
	 * input/output buffers as needed.
	 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Mute capture amp left and right */
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},

	/* Set ADC connection select to match default mixer setting (mic1
	 * pin)
	 */
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Mute all inputs to mixer widget (even unconnected ones) */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)}, /* mic1 pin */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)}, /* mic2 pin */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)}, /* line1 pin */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)}, /* line2 pin */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)}, /* CD pin */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)}, /* Beep-gen pin */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)}, /* Line-out pin */
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)}, /* HP-pin pin */

	{ }
};
#endif


/* initialize jack-sensing, too */
static int cxt5047_hp_init(struct hda_codec *codec)
{
	conexant_init(codec);
	cxt5047_hp_automute(codec);
	return 0;
}


enum {
	CXT5047_LAPTOP,		/* Laptops w/o EAPD support */
	CXT5047_LAPTOP_HP,	/* Some HP laptops */
	CXT5047_LAPTOP_EAPD,	/* Laptops with EAPD support */
#ifdef CONFIG_SND_DEBUG
	CXT5047_TEST,
#endif
	CXT5047_MODELS
};

static const char *cxt5047_models[CXT5047_MODELS] = {
	[CXT5047_LAPTOP]	= "laptop",
	[CXT5047_LAPTOP_HP]	= "laptop-hp",
	[CXT5047_LAPTOP_EAPD]	= "laptop-eapd",
#ifdef CONFIG_SND_DEBUG
	[CXT5047_TEST]		= "test",
#endif
};

static struct snd_pci_quirk cxt5047_cfg_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x30a0, "HP DV1000", CXT5047_LAPTOP),
	SND_PCI_QUIRK(0x103c, 0x30a5, "HP DV5200T/DV8000T", CXT5047_LAPTOP_HP),
	SND_PCI_QUIRK(0x103c, 0x30b2, "HP DV2000T/DV3000T", CXT5047_LAPTOP),
	SND_PCI_QUIRK(0x103c, 0x30b5, "HP DV2000Z", CXT5047_LAPTOP),
	SND_PCI_QUIRK(0x1179, 0xff31, "Toshiba P100", CXT5047_LAPTOP_EAPD),
	{}
};

static int patch_cxt5047(struct hda_codec *codec)
{
	struct conexant_spec *spec;
	int board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	mutex_init(&spec->amp_mutex);
	codec->spec = spec;

	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = ARRAY_SIZE(cxt5047_dac_nids);
	spec->multiout.dac_nids = cxt5047_dac_nids;
	spec->multiout.dig_out_nid = CXT5047_SPDIF_OUT;
	spec->num_adc_nids = 1;
	spec->adc_nids = cxt5047_adc_nids;
	spec->capsrc_nids = cxt5047_capsrc_nids;
	spec->input_mux = &cxt5047_capture_source;
	spec->num_mixers = 1;
	spec->mixers[0] = cxt5047_mixers;
	spec->num_init_verbs = 1;
	spec->init_verbs[0] = cxt5047_init_verbs;
	spec->spdif_route = 0;
	spec->num_channel_mode = ARRAY_SIZE(cxt5047_modes),
	spec->channel_mode = cxt5047_modes,

	codec->patch_ops = conexant_patch_ops;

	board_config = snd_hda_check_board_config(codec, CXT5047_MODELS,
						  cxt5047_models,
						  cxt5047_cfg_tbl);
	switch (board_config) {
	case CXT5047_LAPTOP:
		codec->patch_ops.unsol_event = cxt5047_hp2_unsol_event;
		break;
	case CXT5047_LAPTOP_HP:
		spec->input_mux = &cxt5047_hp_capture_source;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = cxt5047_hp_init_verbs;
		spec->mixers[0] = cxt5047_hp_mixers;
		codec->patch_ops.unsol_event = cxt5047_hp_unsol_event;
		codec->patch_ops.init = cxt5047_hp_init;
		break;
	case CXT5047_LAPTOP_EAPD:
		spec->input_mux = &cxt5047_toshiba_capture_source;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = cxt5047_toshiba_init_verbs;
		spec->mixers[0] = cxt5047_toshiba_mixers;
		codec->patch_ops.unsol_event = cxt5047_hp_unsol_event;
		break;
#ifdef CONFIG_SND_DEBUG
	case CXT5047_TEST:
		spec->input_mux = &cxt5047_test_capture_source;
		spec->mixers[0] = cxt5047_test_mixer;
		spec->init_verbs[0] = cxt5047_test_init_verbs;
		codec->patch_ops.unsol_event = cxt5047_hp_unsol_event;
#endif	
	}
	return 0;
}

/* Conexant 5051 specific */
static hda_nid_t cxt5051_dac_nids[1] = { 0x10 };
static hda_nid_t cxt5051_adc_nids[2] = { 0x14, 0x15 };
#define CXT5051_SPDIF_OUT	0x1C
#define CXT5051_PORTB_EVENT	0x38
#define CXT5051_PORTC_EVENT	0x39

static struct hda_channel_mode cxt5051_modes[1] = {
	{ 2, NULL },
};

static void cxt5051_update_speaker(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int pinctl;
	pinctl = (!spec->hp_present && spec->cur_eapd) ? PIN_OUT : 0;
	snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    pinctl);
}

/* turn on/off EAPD (+ mute HP) as a master switch */
static int cxt5051_hp_master_sw_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);

	if (!cxt_eapd_put(kcontrol, ucontrol))
		return 0;
	cxt5051_update_speaker(codec);
	return 1;
}

/* toggle input of built-in and mic jack appropriately */
static void cxt5051_portb_automic(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x17, 0,
				     AC_VERB_GET_PIN_SENSE, 0) &
		AC_PINSENSE_PRESENCE;
	snd_hda_codec_write(codec, 0x14, 0,
			    AC_VERB_SET_CONNECT_SEL,
			    present ? 0x01 : 0x00);
}

/* switch the current ADC according to the jack state */
static void cxt5051_portc_automic(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int present;
	hda_nid_t new_adc;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0) &
		AC_PINSENSE_PRESENCE;
	if (present)
		spec->cur_adc_idx = 1;
	else
		spec->cur_adc_idx = 0;
	new_adc = spec->adc_nids[spec->cur_adc_idx];
	if (spec->cur_adc && spec->cur_adc != new_adc) {
		/* stream is running, let's swap the current ADC */
		snd_hda_codec_setup_stream(codec, spec->cur_adc, 0, 0, 0);
		spec->cur_adc = new_adc;
		snd_hda_codec_setup_stream(codec, new_adc,
					   spec->cur_adc_stream_tag, 0,
					   spec->cur_adc_format);
	}
}

/* mute internal speaker if HP is plugged */
static void cxt5051_hp_automute(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;

	spec->hp_present = snd_hda_codec_read(codec, 0x16, 0,
				     AC_VERB_GET_PIN_SENSE, 0) &
		AC_PINSENSE_PRESENCE;
	cxt5051_update_speaker(codec);
}

/* unsolicited event for HP jack sensing */
static void cxt5051_hp_unsol_event(struct hda_codec *codec,
				   unsigned int res)
{
	switch (res >> 26) {
	case CONEXANT_HP_EVENT:
		cxt5051_hp_automute(codec);
		break;
	case CXT5051_PORTB_EVENT:
		cxt5051_portb_automic(codec);
		break;
	case CXT5051_PORTC_EVENT:
		cxt5051_portc_automic(codec);
		break;
	}
}

static struct snd_kcontrol_new cxt5051_mixers[] = {
	HDA_CODEC_VOLUME("Internal Mic Volume", 0x14, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Switch", 0x14, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("External Mic Volume", 0x14, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("External Mic Switch", 0x14, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Docking Mic Volume", 0x15, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Docking Mic Switch", 0x15, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Master Playback Volume", 0x10, 0x00, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = cxt_eapd_info,
		.get = cxt_eapd_get,
		.put = cxt5051_hp_master_sw_put,
		.private_value = 0x1a,
	},

	{}
};

static struct snd_kcontrol_new cxt5051_hp_mixers[] = {
	HDA_CODEC_VOLUME("Internal Mic Volume", 0x14, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Switch", 0x14, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("External Mic Volume", 0x15, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("External Mic Switch", 0x15, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Master Playback Volume", 0x10, 0x00, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = cxt_eapd_info,
		.get = cxt_eapd_get,
		.put = cxt5051_hp_master_sw_put,
		.private_value = 0x1a,
	},

	{}
};

static struct hda_verb cxt5051_init_verbs[] = {
	/* Line in, Mic */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x03},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x03},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x03},
	/* SPK  */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* HP, Amp  */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* DAC1 */	
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Record selector: Int mic */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x44},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1) | 0x44},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x44},
	/* SPDIF route: PCM */
	{0x1c, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* EAPD */
	{0x1a, AC_VERB_SET_EAPD_BTLENABLE, 0x2}, /* default on */ 
	{0x16, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|CONEXANT_HP_EVENT},
	{0x17, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|CXT5051_PORTB_EVENT},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|CXT5051_PORTC_EVENT},
	{ } /* end */
};

/* initialize jack-sensing, too */
static int cxt5051_init(struct hda_codec *codec)
{
	conexant_init(codec);
	if (codec->patch_ops.unsol_event) {
		cxt5051_hp_automute(codec);
		cxt5051_portb_automic(codec);
		cxt5051_portc_automic(codec);
	}
	return 0;
}


enum {
	CXT5051_LAPTOP,	 /* Laptops w/ EAPD support */
	CXT5051_HP,	/* no docking */
	CXT5051_MODELS
};

static const char *cxt5051_models[CXT5051_MODELS] = {
	[CXT5051_LAPTOP]	= "laptop",
	[CXT5051_HP]		= "hp",
};

static struct snd_pci_quirk cxt5051_cfg_tbl[] = {
	SND_PCI_QUIRK(0x14f1, 0x0101, "Conexant Reference board",
		      CXT5051_LAPTOP),
	SND_PCI_QUIRK(0x14f1, 0x5051, "HP Spartan 1.1", CXT5051_HP),
	{}
};

static int patch_cxt5051(struct hda_codec *codec)
{
	struct conexant_spec *spec;
	int board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	mutex_init(&spec->amp_mutex);
	codec->spec = spec;

	codec->patch_ops = conexant_patch_ops;
	codec->patch_ops.init = cxt5051_init;

	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = ARRAY_SIZE(cxt5051_dac_nids);
	spec->multiout.dac_nids = cxt5051_dac_nids;
	spec->multiout.dig_out_nid = CXT5051_SPDIF_OUT;
	spec->num_adc_nids = 1; /* not 2; via auto-mic switch */
	spec->adc_nids = cxt5051_adc_nids;
	spec->num_mixers = 1;
	spec->mixers[0] = cxt5051_mixers;
	spec->num_init_verbs = 1;
	spec->init_verbs[0] = cxt5051_init_verbs;
	spec->spdif_route = 0;
	spec->num_channel_mode = ARRAY_SIZE(cxt5051_modes);
	spec->channel_mode = cxt5051_modes;
	spec->cur_adc = 0;
	spec->cur_adc_idx = 0;

	board_config = snd_hda_check_board_config(codec, CXT5051_MODELS,
						  cxt5051_models,
						  cxt5051_cfg_tbl);
	switch (board_config) {
	case CXT5051_HP:
		codec->patch_ops.unsol_event = cxt5051_hp_unsol_event;
		spec->mixers[0] = cxt5051_hp_mixers;
		break;
	default:
	case CXT5051_LAPTOP:
		codec->patch_ops.unsol_event = cxt5051_hp_unsol_event;
		break;
	}

	return 0;
}


/*
 */

struct hda_codec_preset snd_hda_preset_conexant[] = {
	{ .id = 0x14f15045, .name = "CX20549 (Venice)",
	  .patch = patch_cxt5045 },
	{ .id = 0x14f15047, .name = "CX20551 (Waikiki)",
	  .patch = patch_cxt5047 },
	{ .id = 0x14f15051, .name = "CX20561 (Hermosa)",
	  .patch = patch_cxt5051 },
	{} /* terminator */
};
