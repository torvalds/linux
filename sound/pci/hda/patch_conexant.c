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
#include <linux/module.h>
#include <sound/core.h>
#include <sound/jack.h>

#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_beep.h"
#include "hda_jack.h"
#include "hda_generic.h"

#define ENABLE_CXT_STATIC_QUIRKS

#define CXT_PIN_DIR_IN              0x00
#define CXT_PIN_DIR_OUT             0x01
#define CXT_PIN_DIR_INOUT           0x02
#define CXT_PIN_DIR_IN_NOMICBIAS    0x03
#define CXT_PIN_DIR_INOUT_NOMICBIAS 0x04

#define CONEXANT_HP_EVENT	0x37
#define CONEXANT_MIC_EVENT	0x38
#define CONEXANT_LINE_EVENT	0x39

/* Conexant 5051 specific */

#define CXT5051_SPDIF_OUT	0x12
#define CXT5051_PORTB_EVENT	0x38
#define CXT5051_PORTC_EVENT	0x39

#define AUTO_MIC_PORTB		(1 << 1)
#define AUTO_MIC_PORTC		(1 << 2)

struct conexant_spec {
	struct hda_gen_spec gen;

	unsigned int beep_amp;

	/* extra EAPD pins */
	unsigned int num_eapds;
	hda_nid_t eapds[4];
	bool dynamic_eapd;

	unsigned int parse_flags; /* flag for snd_hda_parse_pin_defcfg() */

#ifdef ENABLE_CXT_STATIC_QUIRKS
	const struct snd_kcontrol_new *mixers[5];
	int num_mixers;
	hda_nid_t vmaster_nid;

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
	unsigned int line_present;
	unsigned int auto_mic;

	/* capture */
	unsigned int num_adc_nids;
	const hda_nid_t *adc_nids;
	hda_nid_t dig_in_nid;		/* digital-in NID; optional */

	unsigned int cur_adc_idx;
	hda_nid_t cur_adc;
	unsigned int cur_adc_stream_tag;
	unsigned int cur_adc_format;

	const struct hda_pcm_stream *capture_stream;

	/* capture source */
	const struct hda_input_mux *input_mux;
	const hda_nid_t *capsrc_nids;
	unsigned int cur_mux[3];

	/* channel model */
	const struct hda_channel_mode *channel_mode;
	int num_channel_mode;

	/* PCM information */
	struct hda_pcm pcm_rec[2];	/* used in build_pcms() */

	unsigned int spdif_route;

	unsigned int port_d_mode;
	unsigned int dell_automute:1;
	unsigned int dell_vostro:1;
	unsigned int ideapad:1;
	unsigned int thinkpad:1;
	unsigned int hp_laptop:1;
	unsigned int asus:1;

	unsigned int ext_mic_present;
	unsigned int recording;
	void (*capture_prepare)(struct hda_codec *codec);
	void (*capture_cleanup)(struct hda_codec *codec);

	/* OLPC XO-1.5 supports DC input mode (e.g. for use with analog sensors)
	 * through the microphone jack.
	 * When the user enables this through a mixer switch, both internal and
	 * external microphones are disabled. Gain is fixed at 0dB. In this mode,
	 * we also allow the bias to be configured through a separate mixer
	 * control. */
	unsigned int dc_enable;
	unsigned int dc_input_bias; /* offset into cxt5066_olpc_dc_bias */
	unsigned int mic_boost; /* offset into cxt5066_analog_mic_boost */
#endif /* ENABLE_CXT_STATIC_QUIRKS */
};


#ifdef CONFIG_SND_HDA_INPUT_BEEP
static inline void set_beep_amp(struct conexant_spec *spec, hda_nid_t nid,
				int idx, int dir)
{
	spec->gen.beep_nid = nid;
	spec->beep_amp = HDA_COMPOSE_AMP_VAL(nid, 1, idx, dir);
}
/* additional beep mixers; the actual parameters are overwritten at build */
static const struct snd_kcontrol_new cxt_beep_mixer[] = {
	HDA_CODEC_VOLUME_MONO("Beep Playback Volume", 0, 1, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE_BEEP_MONO("Beep Playback Switch", 0, 1, 0, HDA_OUTPUT),
	{ } /* end */
};

/* create beep controls if needed */
static int add_beep_ctls(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	int err;

	if (spec->beep_amp) {
		const struct snd_kcontrol_new *knew;
		for (knew = cxt_beep_mixer; knew->name; knew++) {
			struct snd_kcontrol *kctl;
			kctl = snd_ctl_new1(knew, codec);
			if (!kctl)
				return -ENOMEM;
			kctl->private_value = spec->beep_amp;
			err = snd_hda_ctl_add(codec, 0, kctl);
			if (err < 0)
				return err;
		}
	}
	return 0;
}
#else
#define set_beep_amp(spec, nid, idx, dir) /* NOP */
#define add_beep_ctls(codec)	0
#endif


#ifdef ENABLE_CXT_STATIC_QUIRKS
static int conexant_playback_pcm_open(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream,
					     hinfo);
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
	if (spec->capture_prepare)
		spec->capture_prepare(codec);
	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
				   stream_tag, 0, format);
	return 0;
}

static int conexant_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct conexant_spec *spec = codec->spec;
	snd_hda_codec_cleanup_stream(codec, spec->adc_nids[substream->number]);
	if (spec->capture_cleanup)
		spec->capture_cleanup(codec);
	return 0;
}



static const struct hda_pcm_stream conexant_pcm_analog_playback = {
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

static const struct hda_pcm_stream conexant_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0, /* fill later */
	.ops = {
		.prepare = conexant_capture_pcm_prepare,
		.cleanup = conexant_capture_pcm_cleanup
	},
};


static const struct hda_pcm_stream conexant_pcm_digital_playback = {
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

static const struct hda_pcm_stream conexant_pcm_digital_capture = {
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
	snd_hda_codec_cleanup_stream(codec, spec->cur_adc);
	spec->cur_adc = 0;
	return 0;
}

static const struct hda_pcm_stream cx5051_pcm_analog_capture = {
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
	if (spec->capture_stream)
		info->stream[SNDRV_PCM_STREAM_CAPTURE] = *spec->capture_stream;
	else {
		if (codec->vendor_id == 0x14f15051)
			info->stream[SNDRV_PCM_STREAM_CAPTURE] =
				cx5051_pcm_analog_capture;
		else {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] =
				conexant_pcm_analog_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams =
				spec->num_adc_nids;
		}
	}
	info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adc_nids[0];

	if (spec->multiout.dig_out_nid) {
		info++;
		codec->num_pcms++;
		info->name = "Conexant Digital";
		info->pcm_type = HDA_PCM_TYPE_SPDIF;
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

static void conexant_set_power(struct hda_codec *codec, hda_nid_t fg,
			       unsigned int power_state)
{
	if (power_state == AC_PWRST_D3)
		msleep(100);
	snd_hda_codec_read(codec, fg, 0, AC_VERB_SET_POWER_STATE,
			    power_state);
	/* partial workaround for "azx_get_response timeout" */
	if (power_state == AC_PWRST_D0)
		msleep(10);
	snd_hda_codec_set_power_to_all(codec, fg, power_state);
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
	snd_hda_detach_beep_device(codec);
	kfree(spec);
}

static const struct snd_kcontrol_new cxt_capture_mixers[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = conexant_mux_enum_info,
		.get = conexant_mux_enum_get,
		.put = conexant_mux_enum_put
	},
	{}
};

static const char * const slave_pfxs[] = {
	"Headphone", "Speaker", "Bass Speaker", "Front", "Surround", "CLFE",
	NULL
};

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
						    spec->multiout.dig_out_nid,
						    spec->multiout.dig_out_nid);
		if (err < 0)
			return err;
		err = snd_hda_create_spdif_share_sw(codec,
						    &spec->multiout);
		if (err < 0)
			return err;
		spec->multiout.share_spdif = 1;
	} 
	if (spec->dig_in_nid) {
		err = snd_hda_create_spdif_in_ctls(codec,spec->dig_in_nid);
		if (err < 0)
			return err;
	}

	/* if we have no master control, let's create it */
	if (spec->vmaster_nid &&
	    !snd_hda_find_mixer_ctl(codec, "Master Playback Volume")) {
		unsigned int vmaster_tlv[4];
		snd_hda_set_vmaster_tlv(codec, spec->vmaster_nid,
					HDA_OUTPUT, vmaster_tlv);
		err = snd_hda_add_vmaster(codec, "Master Playback Volume",
					  vmaster_tlv, slave_pfxs,
					  "Playback Volume");
		if (err < 0)
			return err;
	}
	if (spec->vmaster_nid &&
	    !snd_hda_find_mixer_ctl(codec, "Master Playback Switch")) {
		err = snd_hda_add_vmaster(codec, "Master Playback Switch",
					  NULL, slave_pfxs,
					  "Playback Switch");
		if (err < 0)
			return err;
	}

	if (spec->input_mux) {
		err = snd_hda_add_new_ctls(codec, cxt_capture_mixers);
		if (err < 0)
			return err;
	}

	err = add_beep_ctls(codec);
	if (err < 0)
		return err;

	return 0;
}

static const struct hda_codec_ops conexant_patch_ops = {
	.build_controls = conexant_build_controls,
	.build_pcms = conexant_build_pcms,
	.init = conexant_init,
	.free = conexant_free,
	.set_power_state = conexant_set_power,
};

static int patch_conexant_auto(struct hda_codec *codec);
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

static const hda_nid_t cxt5045_dac_nids[1] = { 0x19 };
static const hda_nid_t cxt5045_adc_nids[1] = { 0x1a };
static const hda_nid_t cxt5045_capsrc_nids[1] = { 0x1a };
#define CXT5045_SPDIF_OUT	0x18

static const struct hda_channel_mode cxt5045_modes[1] = {
	{ 2, NULL },
};

static const struct hda_input_mux cxt5045_capture_source = {
	.num_items = 2,
	.items = {
		{ "Internal Mic", 0x1 },
		{ "Mic",          0x2 },
	}
};

static const struct hda_input_mux cxt5045_capture_source_benq = {
	.num_items = 4,
	.items = {
		{ "Internal Mic", 0x1 },
		{ "Mic",          0x2 },
		{ "Line",         0x3 },
		{ "Mixer",        0x0 },
	}
};

static const struct hda_input_mux cxt5045_capture_source_hp530 = {
	.num_items = 2,
	.items = {
		{ "Mic",          0x1 },
		{ "Internal Mic", 0x2 },
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
static const struct hda_bind_ctls cxt5045_hp_bind_master_vol = {
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
	static const struct hda_verb mic_jack_on[] = {
		{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
		{0x12, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
		{}
	};
	static const struct hda_verb mic_jack_off[] = {
		{0x12, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
		{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
		{}
	};
	unsigned int present;

	present = snd_hda_jack_detect(codec, 0x12);
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

	spec->hp_present = snd_hda_jack_detect(codec, 0x11);

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

static const struct snd_kcontrol_new cxt5045_mixers[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x1a, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x1a, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x17, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x17, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x17, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x17, 0x2, HDA_INPUT),
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

static const struct snd_kcontrol_new cxt5045_benq_mixers[] = {
	HDA_CODEC_VOLUME("Line Playback Volume", 0x17, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x17, 0x3, HDA_INPUT),

	{}
};

static const struct snd_kcontrol_new cxt5045_mixers_hp530[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x1a, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x1a, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x17, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x17, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x17, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x17, 0x1, HDA_INPUT),
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

static const struct hda_verb cxt5045_init_verbs[] = {
	/* Line in, Mic */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN|AC_PINCTL_VREF_80 },
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
	/* Record selector: Internal mic */
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

static const struct hda_verb cxt5045_benq_init_verbs[] = {
	/* Internal Mic, Mic */
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
	/* Record selector: Internal mic */
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

static const struct hda_verb cxt5045_hp_sense_init_verbs[] = {
	/* pin sensing on HP jack */
	{0x11, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_HP_EVENT},
	{ } /* end */
};

static const struct hda_verb cxt5045_mic_sense_init_verbs[] = {
	/* pin sensing on HP jack */
	{0x12, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_MIC_EVENT},
	{ } /* end */
};

#ifdef CONFIG_SND_DEBUG
/* Test configuration for debugging, modelled after the ALC260 test
 * configuration.
 */
static const struct hda_input_mux cxt5045_test_capture_source = {
	.num_items = 5,
	.items = {
		{ "MIXER", 0x0 },
		{ "MIC1 pin", 0x1 },
		{ "LINE1 pin", 0x2 },
		{ "HP-OUT pin", 0x3 },
		{ "CD pin", 0x4 },
        },
};

static const struct snd_kcontrol_new cxt5045_test_mixer[] = {

	/* Output controls */
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x10, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x10, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("HP-OUT Playback Volume", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("HP-OUT Playback Switch", 0x11, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("LINE1 Playback Volume", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("LINE1 Playback Switch", 0x12, 0x0, HDA_OUTPUT),
	
	/* Modes for retasking pin widgets */
	CXT_PIN_MODE("HP-OUT pin mode", 0x11, CXT_PIN_DIR_INOUT),
	CXT_PIN_MODE("LINE1 pin mode", 0x12, CXT_PIN_DIR_INOUT),

	/* EAPD Switch Control */
	CXT_EAPD_SWITCH("External Amplifier", 0x10, 0x0),

	/* Loopback mixer controls */

	HDA_CODEC_VOLUME("PCM Volume", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("PCM Switch", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("MIC1 pin Volume", 0x17, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("MIC1 pin Switch", 0x17, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("LINE1 pin Volume", 0x17, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("LINE1 pin Switch", 0x17, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("HP-OUT pin Volume", 0x17, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("HP-OUT pin Switch", 0x17, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("CD pin Volume", 0x17, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("CD pin Switch", 0x17, 0x4, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Input Source",
		.info = conexant_mux_enum_info,
		.get = conexant_mux_enum_get,
		.put = conexant_mux_enum_put,
	},
	/* Audio input controls */
	HDA_CODEC_VOLUME("Capture Volume", 0x1a, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x1a, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct hda_verb cxt5045_test_init_verbs[] = {
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
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x01},

	/* Mute all inputs to mixer widget (even unconnected ones) */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)}, /* Mixer */
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
	CXT5045_LAPTOP_HP530,
#ifdef CONFIG_SND_DEBUG
	CXT5045_TEST,
#endif
	CXT5045_AUTO,
	CXT5045_MODELS
};

static const char * const cxt5045_models[CXT5045_MODELS] = {
	[CXT5045_LAPTOP_HPSENSE]	= "laptop-hpsense",
	[CXT5045_LAPTOP_MICSENSE]	= "laptop-micsense",
	[CXT5045_LAPTOP_HPMICSENSE]	= "laptop-hpmicsense",
	[CXT5045_BENQ]			= "benq",
	[CXT5045_LAPTOP_HP530]		= "laptop-hp530",
#ifdef CONFIG_SND_DEBUG
	[CXT5045_TEST]		= "test",
#endif
	[CXT5045_AUTO]			= "auto",
};

static const struct snd_pci_quirk cxt5045_cfg_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x30d5, "HP 530", CXT5045_LAPTOP_HP530),
	SND_PCI_QUIRK(0x1179, 0xff31, "Toshiba P105", CXT5045_LAPTOP_MICSENSE),
	SND_PCI_QUIRK(0x152d, 0x0753, "Benq R55E", CXT5045_BENQ),
	SND_PCI_QUIRK(0x1734, 0x10ad, "Fujitsu Si1520", CXT5045_LAPTOP_MICSENSE),
	SND_PCI_QUIRK(0x1734, 0x10cb, "Fujitsu Si3515", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x1734, 0x110e, "Fujitsu V5505",
		      CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x1509, 0x1e40, "FIC", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x1509, 0x2f05, "FIC", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x1509, 0x2f06, "FIC", CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK_MASK(0x1631, 0xff00, 0xc100, "Packard Bell",
			   CXT5045_LAPTOP_HPMICSENSE),
	SND_PCI_QUIRK(0x8086, 0x2111, "Conexant Reference board", CXT5045_LAPTOP_HPSENSE),
	{}
};

static int patch_cxt5045(struct hda_codec *codec)
{
	struct conexant_spec *spec;
	int board_config;

	board_config = snd_hda_check_board_config(codec, CXT5045_MODELS,
						  cxt5045_models,
						  cxt5045_cfg_tbl);
	if (board_config < 0)
		board_config = CXT5045_AUTO; /* model=auto as default */
	if (board_config == CXT5045_AUTO)
		return patch_conexant_auto(codec);

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	codec->spec = spec;
	codec->single_adc_amp = 1;

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
	spec->num_channel_mode = ARRAY_SIZE(cxt5045_modes);
	spec->channel_mode = cxt5045_modes;

	set_beep_amp(spec, 0x16, 0, 1);

	codec->patch_ops = conexant_patch_ops;

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
		codec->patch_ops.unsol_event = cxt5045_hp_unsol_event;
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
	case CXT5045_LAPTOP_HP530:
		codec->patch_ops.unsol_event = cxt5045_hp_unsol_event;
		spec->input_mux = &cxt5045_capture_source_hp530;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = cxt5045_hp_sense_init_verbs;
		spec->mixers[0] = cxt5045_mixers_hp530;
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

	switch (codec->subsystem_id >> 16) {
	case 0x103c:
	case 0x1631:
	case 0x1734:
	case 0x17aa:
		/* HP, Packard Bell, Fujitsu-Siemens & Lenovo laptops have
		 * really bad sound over 0dB on NID 0x17. Fix max PCM level to
		 * 0 dB (originally it has 0x2b steps with 0dB offset 0x14)
		 */
		snd_hda_override_amp_caps(codec, 0x17, HDA_INPUT,
					  (0x14 << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x14 << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x05 << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (1 << AC_AMPCAP_MUTE_SHIFT));
		break;
	}

	if (spec->beep_amp)
		snd_hda_attach_beep_device(codec, get_amp_nid_(spec->beep_amp));

	return 0;
}


/* Conexant 5047 specific */
#define CXT5047_SPDIF_OUT	0x11

static const hda_nid_t cxt5047_dac_nids[1] = { 0x10 }; /* 0x1c */
static const hda_nid_t cxt5047_adc_nids[1] = { 0x12 };
static const hda_nid_t cxt5047_capsrc_nids[1] = { 0x1a };

static const struct hda_channel_mode cxt5047_modes[1] = {
	{ 2, NULL },
};

static const struct hda_input_mux cxt5047_toshiba_capture_source = {
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
	/* NOTE: Conexat codec needs the index for *OUTPUT* amp of
	 * pin widgets unlike other codecs.  In this case, we need to
	 * set index 0x01 for the volume from the mixer amp 0x19.
	 */
	snd_hda_codec_amp_stereo(codec, 0x1d, HDA_OUTPUT, 0x01,
				 HDA_AMP_MUTE, bits);
	bits = spec->cur_eapd ? 0 : HDA_AMP_MUTE;
	snd_hda_codec_amp_stereo(codec, 0x13, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	return 1;
}

/* mute internal speaker if HP is plugged */
static void cxt5047_hp_automute(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int bits;

	spec->hp_present = snd_hda_jack_detect(codec, 0x13);

	bits = (spec->hp_present || !spec->cur_eapd) ? HDA_AMP_MUTE : 0;
	/* See the note in cxt5047_hp_master_sw_put */
	snd_hda_codec_amp_stereo(codec, 0x1d, HDA_OUTPUT, 0x01,
				 HDA_AMP_MUTE, bits);
}

/* toggle input of built-in and mic jack appropriately */
static void cxt5047_hp_automic(struct hda_codec *codec)
{
	static const struct hda_verb mic_jack_on[] = {
		{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
		{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
		{}
	};
	static const struct hda_verb mic_jack_off[] = {
		{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
		{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
		{}
	};
	unsigned int present;

	present = snd_hda_jack_detect(codec, 0x15);
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

static const struct snd_kcontrol_new cxt5047_base_mixers[] = {
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x19, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x19, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x1a, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("PCM Volume", 0x10, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Switch", 0x10, 0x00, HDA_OUTPUT),
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

static const struct snd_kcontrol_new cxt5047_hp_spk_mixers[] = {
	/* See the note in cxt5047_hp_master_sw_put */
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x1d, 0x01, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x13, 0x00, HDA_OUTPUT),
	{}
};

static const struct snd_kcontrol_new cxt5047_hp_only_mixers[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x13, 0x00, HDA_OUTPUT),
	{ } /* end */
};

static const struct hda_verb cxt5047_init_verbs[] = {
	/* Line in, Mic, Built-in Mic */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN|AC_PINCTL_VREF_50 },
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN|AC_PINCTL_VREF_50 },
	/* HP, Speaker  */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x0}, /* mixer(0x19) */
	{0x1d, AC_VERB_SET_CONNECT_SEL, 0x1}, /* mixer(0x19) */
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
static const struct hda_verb cxt5047_toshiba_init_verbs[] = {
	{0x13, AC_VERB_SET_EAPD_BTLENABLE, 0x0}, /* default off */
	{}
};

/* Test configuration for debugging, modelled after the ALC260 test
 * configuration.
 */
#ifdef CONFIG_SND_DEBUG
static const struct hda_input_mux cxt5047_test_capture_source = {
	.num_items = 4,
	.items = {
		{ "LINE1 pin", 0x0 },
		{ "MIC1 pin", 0x1 },
		{ "MIC2 pin", 0x2 },
		{ "CD pin", 0x3 },
        },
};

static const struct snd_kcontrol_new cxt5047_test_mixer[] = {

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
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x1a, 0x0, HDA_OUTPUT),

	{ } /* end */
};

static const struct hda_verb cxt5047_test_init_verbs[] = {
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
	CXT5047_AUTO,
	CXT5047_MODELS
};

static const char * const cxt5047_models[CXT5047_MODELS] = {
	[CXT5047_LAPTOP]	= "laptop",
	[CXT5047_LAPTOP_HP]	= "laptop-hp",
	[CXT5047_LAPTOP_EAPD]	= "laptop-eapd",
#ifdef CONFIG_SND_DEBUG
	[CXT5047_TEST]		= "test",
#endif
	[CXT5047_AUTO]		= "auto",
};

static const struct snd_pci_quirk cxt5047_cfg_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x30a5, "HP DV5200T/DV8000T", CXT5047_LAPTOP_HP),
	SND_PCI_QUIRK_MASK(0x103c, 0xff00, 0x3000, "HP DV Series",
			   CXT5047_LAPTOP),
	SND_PCI_QUIRK(0x1179, 0xff31, "Toshiba P100", CXT5047_LAPTOP_EAPD),
	{}
};

static int patch_cxt5047(struct hda_codec *codec)
{
	struct conexant_spec *spec;
	int board_config;

	board_config = snd_hda_check_board_config(codec, CXT5047_MODELS,
						  cxt5047_models,
						  cxt5047_cfg_tbl);
	if (board_config < 0)
		board_config = CXT5047_AUTO; /* model=auto as default */
	if (board_config == CXT5047_AUTO)
		return patch_conexant_auto(codec);

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	codec->spec = spec;
	codec->pin_amp_workaround = 1;

	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = ARRAY_SIZE(cxt5047_dac_nids);
	spec->multiout.dac_nids = cxt5047_dac_nids;
	spec->multiout.dig_out_nid = CXT5047_SPDIF_OUT;
	spec->num_adc_nids = 1;
	spec->adc_nids = cxt5047_adc_nids;
	spec->capsrc_nids = cxt5047_capsrc_nids;
	spec->num_mixers = 1;
	spec->mixers[0] = cxt5047_base_mixers;
	spec->num_init_verbs = 1;
	spec->init_verbs[0] = cxt5047_init_verbs;
	spec->spdif_route = 0;
	spec->num_channel_mode = ARRAY_SIZE(cxt5047_modes),
	spec->channel_mode = cxt5047_modes,

	codec->patch_ops = conexant_patch_ops;

	switch (board_config) {
	case CXT5047_LAPTOP:
		spec->num_mixers = 2;
		spec->mixers[1] = cxt5047_hp_spk_mixers;
		codec->patch_ops.unsol_event = cxt5047_hp_unsol_event;
		break;
	case CXT5047_LAPTOP_HP:
		spec->num_mixers = 2;
		spec->mixers[1] = cxt5047_hp_only_mixers;
		codec->patch_ops.unsol_event = cxt5047_hp_unsol_event;
		codec->patch_ops.init = cxt5047_hp_init;
		break;
	case CXT5047_LAPTOP_EAPD:
		spec->input_mux = &cxt5047_toshiba_capture_source;
		spec->num_mixers = 2;
		spec->mixers[1] = cxt5047_hp_spk_mixers;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = cxt5047_toshiba_init_verbs;
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
	spec->vmaster_nid = 0x13;

	switch (codec->subsystem_id >> 16) {
	case 0x103c:
		/* HP laptops have really bad sound over 0 dB on NID 0x10.
		 * Fix max PCM level to 0 dB (originally it has 0x1e steps
		 * with 0 dB offset 0x17)
		 */
		snd_hda_override_amp_caps(codec, 0x10, HDA_INPUT,
					  (0x17 << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x17 << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x05 << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (1 << AC_AMPCAP_MUTE_SHIFT));
		break;
	}

	return 0;
}

/* Conexant 5051 specific */
static const hda_nid_t cxt5051_dac_nids[1] = { 0x10 };
static const hda_nid_t cxt5051_adc_nids[2] = { 0x14, 0x15 };

static const struct hda_channel_mode cxt5051_modes[1] = {
	{ 2, NULL },
};

static void cxt5051_update_speaker(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int pinctl;
	/* headphone pin */
	pinctl = (spec->hp_present && spec->cur_eapd) ? PIN_HP : 0;
	snd_hda_set_pin_ctl(codec, 0x16, pinctl);
	/* speaker pin */
	pinctl = (!spec->hp_present && spec->cur_eapd) ? PIN_OUT : 0;
	snd_hda_set_pin_ctl(codec, 0x1a, pinctl);
	/* on ideapad there is an additional speaker (subwoofer) to mute */
	if (spec->ideapad)
		snd_hda_set_pin_ctl(codec, 0x1b, pinctl);
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
	struct conexant_spec *spec = codec->spec;
	unsigned int present;

	if (!(spec->auto_mic & AUTO_MIC_PORTB))
		return;
	present = snd_hda_jack_detect(codec, 0x17);
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

	if (!(spec->auto_mic & AUTO_MIC_PORTC))
		return;
	present = snd_hda_jack_detect(codec, 0x18);
	if (present)
		spec->cur_adc_idx = 1;
	else
		spec->cur_adc_idx = 0;
	new_adc = spec->adc_nids[spec->cur_adc_idx];
	if (spec->cur_adc && spec->cur_adc != new_adc) {
		/* stream is running, let's swap the current ADC */
		__snd_hda_codec_cleanup_stream(codec, spec->cur_adc, 1);
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

	spec->hp_present = snd_hda_jack_detect(codec, 0x16);
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

static const struct snd_kcontrol_new cxt5051_playback_mixers[] = {
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

static const struct snd_kcontrol_new cxt5051_capture_mixers[] = {
	HDA_CODEC_VOLUME("Internal Mic Volume", 0x14, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Switch", 0x14, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Volume", 0x14, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Switch", 0x14, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Dock Mic Volume", 0x15, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Dock Mic Switch", 0x15, 0x00, HDA_INPUT),
	{}
};

static const struct snd_kcontrol_new cxt5051_hp_mixers[] = {
	HDA_CODEC_VOLUME("Internal Mic Volume", 0x14, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Switch", 0x14, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Volume", 0x15, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Switch", 0x15, 0x00, HDA_INPUT),
	{}
};

static const struct snd_kcontrol_new cxt5051_hp_dv6736_mixers[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x14, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x14, 0x00, HDA_INPUT),
	{}
};

static const struct snd_kcontrol_new cxt5051_f700_mixers[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x14, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x14, 0x01, HDA_INPUT),
	{}
};

static const struct snd_kcontrol_new cxt5051_toshiba_mixers[] = {
	HDA_CODEC_VOLUME("Internal Mic Volume", 0x14, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Switch", 0x14, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Volume", 0x14, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Switch", 0x14, 0x01, HDA_INPUT),
	{}
};

static const struct hda_verb cxt5051_init_verbs[] = {
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
	/* Record selector: Internal mic */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x44},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1) | 0x44},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x44},
	/* SPDIF route: PCM */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1c, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* EAPD */
	{0x1a, AC_VERB_SET_EAPD_BTLENABLE, 0x2}, /* default on */ 
	{0x16, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|CONEXANT_HP_EVENT},
	{ } /* end */
};

static const struct hda_verb cxt5051_hp_dv6736_init_verbs[] = {
	/* Line in, Mic */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x03},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0},
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0},
	/* SPK  */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* HP, Amp  */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* DAC1 */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Record selector: Internal mic */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1) | 0x44},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* SPDIF route: PCM */
	{0x1c, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* EAPD */
	{0x1a, AC_VERB_SET_EAPD_BTLENABLE, 0x2}, /* default on */
	{0x16, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|CONEXANT_HP_EVENT},
	{ } /* end */
};

static const struct hda_verb cxt5051_f700_init_verbs[] = {
	/* Line in, Mic */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x03},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0},
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x0},
	/* SPK  */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* HP, Amp  */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* DAC1 */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Record selector: Internal mic */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1) | 0x44},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x1},
	/* SPDIF route: PCM */
	{0x1c, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* EAPD */
	{0x1a, AC_VERB_SET_EAPD_BTLENABLE, 0x2}, /* default on */
	{0x16, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|CONEXANT_HP_EVENT},
	{ } /* end */
};

static void cxt5051_init_mic_port(struct hda_codec *codec, hda_nid_t nid,
				 unsigned int event)
{
	snd_hda_codec_write(codec, nid, 0,
			    AC_VERB_SET_UNSOLICITED_ENABLE,
			    AC_USRSP_EN | event);
}

static const struct hda_verb cxt5051_ideapad_init_verbs[] = {
	/* Subwoofer */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{ } /* end */
};

/* initialize jack-sensing, too */
static int cxt5051_init(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;

	conexant_init(codec);

	if (spec->auto_mic & AUTO_MIC_PORTB)
		cxt5051_init_mic_port(codec, 0x17, CXT5051_PORTB_EVENT);
	if (spec->auto_mic & AUTO_MIC_PORTC)
		cxt5051_init_mic_port(codec, 0x18, CXT5051_PORTC_EVENT);

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
	CXT5051_HP_DV6736,	/* HP without mic switch */
	CXT5051_F700,       /* HP Compaq Presario F700 */
	CXT5051_TOSHIBA,	/* Toshiba M300 & co */
	CXT5051_IDEAPAD,	/* Lenovo IdeaPad Y430 */
	CXT5051_AUTO,		/* auto-parser */
	CXT5051_MODELS
};

static const char *const cxt5051_models[CXT5051_MODELS] = {
	[CXT5051_LAPTOP]	= "laptop",
	[CXT5051_HP]		= "hp",
	[CXT5051_HP_DV6736]	= "hp-dv6736",
	[CXT5051_F700]          = "hp-700",
	[CXT5051_TOSHIBA]	= "toshiba",
	[CXT5051_IDEAPAD]	= "ideapad",
	[CXT5051_AUTO]		= "auto",
};

static const struct snd_pci_quirk cxt5051_cfg_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x30cf, "HP DV6736", CXT5051_HP_DV6736),
	SND_PCI_QUIRK(0x103c, 0x360b, "Compaq Presario CQ60", CXT5051_HP),
	SND_PCI_QUIRK(0x103c, 0x30ea, "Compaq Presario F700", CXT5051_F700),
	SND_PCI_QUIRK(0x1179, 0xff50, "Toshiba M30x", CXT5051_TOSHIBA),
	SND_PCI_QUIRK(0x14f1, 0x0101, "Conexant Reference board",
		      CXT5051_LAPTOP),
	SND_PCI_QUIRK(0x14f1, 0x5051, "HP Spartan 1.1", CXT5051_HP),
	SND_PCI_QUIRK(0x17aa, 0x3a0d, "Lenovo IdeaPad", CXT5051_IDEAPAD),
	{}
};

static int patch_cxt5051(struct hda_codec *codec)
{
	struct conexant_spec *spec;
	int board_config;

	board_config = snd_hda_check_board_config(codec, CXT5051_MODELS,
						  cxt5051_models,
						  cxt5051_cfg_tbl);
	if (board_config < 0)
		board_config = CXT5051_AUTO; /* model=auto as default */
	if (board_config == CXT5051_AUTO)
		return patch_conexant_auto(codec);

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	codec->spec = spec;
	codec->pin_amp_workaround = 1;

	codec->patch_ops = conexant_patch_ops;
	codec->patch_ops.init = cxt5051_init;

	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = ARRAY_SIZE(cxt5051_dac_nids);
	spec->multiout.dac_nids = cxt5051_dac_nids;
	spec->multiout.dig_out_nid = CXT5051_SPDIF_OUT;
	spec->num_adc_nids = 1; /* not 2; via auto-mic switch */
	spec->adc_nids = cxt5051_adc_nids;
	spec->num_mixers = 2;
	spec->mixers[0] = cxt5051_capture_mixers;
	spec->mixers[1] = cxt5051_playback_mixers;
	spec->num_init_verbs = 1;
	spec->init_verbs[0] = cxt5051_init_verbs;
	spec->spdif_route = 0;
	spec->num_channel_mode = ARRAY_SIZE(cxt5051_modes);
	spec->channel_mode = cxt5051_modes;
	spec->cur_adc = 0;
	spec->cur_adc_idx = 0;

	set_beep_amp(spec, 0x13, 0, HDA_OUTPUT);

	codec->patch_ops.unsol_event = cxt5051_hp_unsol_event;

	spec->auto_mic = AUTO_MIC_PORTB | AUTO_MIC_PORTC;
	switch (board_config) {
	case CXT5051_HP:
		spec->mixers[0] = cxt5051_hp_mixers;
		break;
	case CXT5051_HP_DV6736:
		spec->init_verbs[0] = cxt5051_hp_dv6736_init_verbs;
		spec->mixers[0] = cxt5051_hp_dv6736_mixers;
		spec->auto_mic = 0;
		break;
	case CXT5051_F700:
		spec->init_verbs[0] = cxt5051_f700_init_verbs;
		spec->mixers[0] = cxt5051_f700_mixers;
		spec->auto_mic = 0;
		break;
	case CXT5051_TOSHIBA:
		spec->mixers[0] = cxt5051_toshiba_mixers;
		spec->auto_mic = AUTO_MIC_PORTB;
		break;
	case CXT5051_IDEAPAD:
		spec->init_verbs[spec->num_init_verbs++] =
			cxt5051_ideapad_init_verbs;
		spec->ideapad = 1;
		break;
	}

	if (spec->beep_amp)
		snd_hda_attach_beep_device(codec, get_amp_nid_(spec->beep_amp));

	return 0;
}

/* Conexant 5066 specific */

static const hda_nid_t cxt5066_dac_nids[1] = { 0x10 };
static const hda_nid_t cxt5066_adc_nids[3] = { 0x14, 0x15, 0x16 };
static const hda_nid_t cxt5066_capsrc_nids[1] = { 0x17 };
static const hda_nid_t cxt5066_digout_pin_nids[2] = { 0x20, 0x22 };

/* OLPC's microphone port is DC coupled for use with external sensors,
 * therefore we use a 50% mic bias in order to center the input signal with
 * the DC input range of the codec. */
#define CXT5066_OLPC_EXT_MIC_BIAS PIN_VREF50

static const struct hda_channel_mode cxt5066_modes[1] = {
	{ 2, NULL },
};

#define HP_PRESENT_PORT_A	(1 << 0)
#define HP_PRESENT_PORT_D	(1 << 1)
#define hp_port_a_present(spec)	((spec)->hp_present & HP_PRESENT_PORT_A)
#define hp_port_d_present(spec)	((spec)->hp_present & HP_PRESENT_PORT_D)

static void cxt5066_update_speaker(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int pinctl;

	snd_printdd("CXT5066: update speaker, hp_present=%d, cur_eapd=%d\n",
		    spec->hp_present, spec->cur_eapd);

	/* Port A (HP) */
	pinctl = (hp_port_a_present(spec) && spec->cur_eapd) ? PIN_HP : 0;
	snd_hda_set_pin_ctl(codec, 0x19, pinctl);

	/* Port D (HP/LO) */
	pinctl = spec->cur_eapd ? spec->port_d_mode : 0;
	if (spec->dell_automute || spec->thinkpad) {
		/* Mute if Port A is connected */
		if (hp_port_a_present(spec))
			pinctl = 0;
	} else {
		/* Thinkpad/Dell doesn't give pin-D status */
		if (!hp_port_d_present(spec))
			pinctl = 0;
	}
	snd_hda_set_pin_ctl(codec, 0x1c, pinctl);

	/* CLASS_D AMP */
	pinctl = (!spec->hp_present && spec->cur_eapd) ? PIN_OUT : 0;
	snd_hda_set_pin_ctl(codec, 0x1f, pinctl);
}

/* turn on/off EAPD (+ mute HP) as a master switch */
static int cxt5066_hp_master_sw_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);

	if (!cxt_eapd_put(kcontrol, ucontrol))
		return 0;

	cxt5066_update_speaker(codec);
	return 1;
}

static const struct hda_input_mux cxt5066_olpc_dc_bias = {
	.num_items = 3,
	.items = {
		{ "Off", PIN_IN },
		{ "50%", PIN_VREF50 },
		{ "80%", PIN_VREF80 },
	},
};

static int cxt5066_set_olpc_dc_bias(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	/* Even though port F is the DC input, the bias is controlled on port B.
	 * we also leave that port as an active input (but unselected) in DC mode
	 * just in case that is necessary to make the bias setting take effect. */
	return snd_hda_set_pin_ctl_cache(codec, 0x1a,
		cxt5066_olpc_dc_bias.items[spec->dc_input_bias].index);
}

/* OLPC defers mic widget control until when capture is started because the
 * microphone LED comes on as soon as these settings are put in place. if we
 * did this before recording, it would give the false indication that recording
 * is happening when it is not. */
static void cxt5066_olpc_select_mic(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	if (!spec->recording)
		return;

	if (spec->dc_enable) {
		/* in DC mode we ignore presence detection and just use the jack
		 * through our special DC port */
		const struct hda_verb enable_dc_mode[] = {
			/* disble internal mic, port C */
			{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

			/* enable DC capture, port F */
			{0x1e, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
			{},
		};

		snd_hda_sequence_write(codec, enable_dc_mode);
		/* port B input disabled (and bias set) through the following call */
		cxt5066_set_olpc_dc_bias(codec);
		return;
	}

	/* disable DC (port F) */
	snd_hda_set_pin_ctl(codec, 0x1e, 0);

	/* external mic, port B */
	snd_hda_set_pin_ctl(codec, 0x1a,
		spec->ext_mic_present ? CXT5066_OLPC_EXT_MIC_BIAS : 0);

	/* internal mic, port C */
	snd_hda_set_pin_ctl(codec, 0x1b,
		spec->ext_mic_present ? 0 : PIN_VREF80);
}

/* toggle input of built-in and mic jack appropriately */
static void cxt5066_olpc_automic(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int present;

	if (spec->dc_enable) /* don't do presence detection in DC mode */
		return;

	present = snd_hda_codec_read(codec, 0x1a, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	if (present)
		snd_printdd("CXT5066: external microphone detected\n");
	else
		snd_printdd("CXT5066: external microphone absent\n");

	snd_hda_codec_write(codec, 0x17, 0, AC_VERB_SET_CONNECT_SEL,
		present ? 0 : 1);
	spec->ext_mic_present = !!present;

	cxt5066_olpc_select_mic(codec);
}

/* toggle input of built-in digital mic and mic jack appropriately */
static void cxt5066_vostro_automic(struct hda_codec *codec)
{
	unsigned int present;

	struct hda_verb ext_mic_present[] = {
		/* enable external mic, port B */
		{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},

		/* switch to external mic input */
		{0x17, AC_VERB_SET_CONNECT_SEL, 0},
		{0x14, AC_VERB_SET_CONNECT_SEL, 0},

		/* disable internal digital mic */
		{0x23, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
		{}
	};
	static const struct hda_verb ext_mic_absent[] = {
		/* enable internal mic, port C */
		{0x23, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

		/* switch to internal mic input */
		{0x14, AC_VERB_SET_CONNECT_SEL, 2},

		/* disable external mic, port B */
		{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
		{}
	};

	present = snd_hda_jack_detect(codec, 0x1a);
	if (present) {
		snd_printdd("CXT5066: external microphone detected\n");
		snd_hda_sequence_write(codec, ext_mic_present);
	} else {
		snd_printdd("CXT5066: external microphone absent\n");
		snd_hda_sequence_write(codec, ext_mic_absent);
	}
}

/* toggle input of built-in digital mic and mic jack appropriately */
static void cxt5066_ideapad_automic(struct hda_codec *codec)
{
	unsigned int present;

	struct hda_verb ext_mic_present[] = {
		{0x14, AC_VERB_SET_CONNECT_SEL, 0},
		{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
		{0x23, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
		{}
	};
	static const struct hda_verb ext_mic_absent[] = {
		{0x14, AC_VERB_SET_CONNECT_SEL, 2},
		{0x23, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
		{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
		{}
	};

	present = snd_hda_jack_detect(codec, 0x1b);
	if (present) {
		snd_printdd("CXT5066: external microphone detected\n");
		snd_hda_sequence_write(codec, ext_mic_present);
	} else {
		snd_printdd("CXT5066: external microphone absent\n");
		snd_hda_sequence_write(codec, ext_mic_absent);
	}
}


/* toggle input of built-in digital mic and mic jack appropriately */
static void cxt5066_asus_automic(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_jack_detect(codec, 0x1b);
	snd_printdd("CXT5066: external microphone present=%d\n", present);
	snd_hda_codec_write(codec, 0x17, 0, AC_VERB_SET_CONNECT_SEL,
			    present ? 1 : 0);
}


/* toggle input of built-in digital mic and mic jack appropriately */
static void cxt5066_hp_laptop_automic(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_jack_detect(codec, 0x1b);
	snd_printdd("CXT5066: external microphone present=%d\n", present);
	snd_hda_codec_write(codec, 0x17, 0, AC_VERB_SET_CONNECT_SEL,
			    present ? 1 : 3);
}


/* toggle input of built-in digital mic and mic jack appropriately
   order is: external mic -> dock mic -> interal mic */
static void cxt5066_thinkpad_automic(struct hda_codec *codec)
{
	unsigned int ext_present, dock_present;

	static const struct hda_verb ext_mic_present[] = {
		{0x14, AC_VERB_SET_CONNECT_SEL, 0},
		{0x17, AC_VERB_SET_CONNECT_SEL, 1},
		{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
		{0x23, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
		{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
		{}
	};
	static const struct hda_verb dock_mic_present[] = {
		{0x14, AC_VERB_SET_CONNECT_SEL, 0},
		{0x17, AC_VERB_SET_CONNECT_SEL, 0},
		{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
		{0x23, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
		{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
		{}
	};
	static const struct hda_verb ext_mic_absent[] = {
		{0x14, AC_VERB_SET_CONNECT_SEL, 2},
		{0x23, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
		{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
		{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
		{}
	};

	ext_present = snd_hda_jack_detect(codec, 0x1b);
	dock_present = snd_hda_jack_detect(codec, 0x1a);
	if (ext_present) {
		snd_printdd("CXT5066: external microphone detected\n");
		snd_hda_sequence_write(codec, ext_mic_present);
	} else if (dock_present) {
		snd_printdd("CXT5066: dock microphone detected\n");
		snd_hda_sequence_write(codec, dock_mic_present);
	} else {
		snd_printdd("CXT5066: external microphone absent\n");
		snd_hda_sequence_write(codec, ext_mic_absent);
	}
}

/* mute internal speaker if HP is plugged */
static void cxt5066_hp_automute(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	unsigned int portA, portD;

	/* Port A */
	portA = snd_hda_jack_detect(codec, 0x19);

	/* Port D */
	portD = snd_hda_jack_detect(codec, 0x1c);

	spec->hp_present = portA ? HP_PRESENT_PORT_A : 0;
	spec->hp_present |= portD ? HP_PRESENT_PORT_D : 0;
	snd_printdd("CXT5066: hp automute portA=%x portD=%x present=%d\n",
		portA, portD, spec->hp_present);
	cxt5066_update_speaker(codec);
}

/* Dispatch the right mic autoswitch function */
static void cxt5066_automic(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;

	if (spec->dell_vostro)
		cxt5066_vostro_automic(codec);
	else if (spec->ideapad)
		cxt5066_ideapad_automic(codec);
	else if (spec->thinkpad)
		cxt5066_thinkpad_automic(codec);
	else if (spec->hp_laptop)
		cxt5066_hp_laptop_automic(codec);
	else if (spec->asus)
		cxt5066_asus_automic(codec);
}

/* unsolicited event for jack sensing */
static void cxt5066_olpc_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct conexant_spec *spec = codec->spec;
	snd_printdd("CXT5066: unsol event %x (%x)\n", res, res >> 26);
	switch (res >> 26) {
	case CONEXANT_HP_EVENT:
		cxt5066_hp_automute(codec);
		break;
	case CONEXANT_MIC_EVENT:
		/* ignore mic events in DC mode; we're always using the jack */
		if (!spec->dc_enable)
			cxt5066_olpc_automic(codec);
		break;
	}
}

/* unsolicited event for jack sensing */
static void cxt5066_unsol_event(struct hda_codec *codec, unsigned int res)
{
	snd_printdd("CXT5066: unsol event %x (%x)\n", res, res >> 26);
	switch (res >> 26) {
	case CONEXANT_HP_EVENT:
		cxt5066_hp_automute(codec);
		break;
	case CONEXANT_MIC_EVENT:
		cxt5066_automic(codec);
		break;
	}
}


static const struct hda_input_mux cxt5066_analog_mic_boost = {
	.num_items = 5,
	.items = {
		{ "0dB",  0 },
		{ "10dB", 1 },
		{ "20dB", 2 },
		{ "30dB", 3 },
		{ "40dB", 4 },
	},
};

static void cxt5066_set_mic_boost(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	snd_hda_codec_write_cache(codec, 0x17, 0,
		AC_VERB_SET_AMP_GAIN_MUTE,
		AC_AMP_SET_RIGHT | AC_AMP_SET_LEFT | AC_AMP_SET_OUTPUT |
			cxt5066_analog_mic_boost.items[spec->mic_boost].index);
	if (spec->ideapad || spec->thinkpad) {
		/* adjust the internal mic as well...it is not through 0x17 */
		snd_hda_codec_write_cache(codec, 0x23, 0,
			AC_VERB_SET_AMP_GAIN_MUTE,
			AC_AMP_SET_RIGHT | AC_AMP_SET_LEFT | AC_AMP_SET_INPUT |
				cxt5066_analog_mic_boost.
					items[spec->mic_boost].index);
	}
}

static int cxt5066_mic_boost_mux_enum_info(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_info *uinfo)
{
	return snd_hda_input_mux_info(&cxt5066_analog_mic_boost, uinfo);
}

static int cxt5066_mic_boost_mux_enum_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	ucontrol->value.enumerated.item[0] = spec->mic_boost;
	return 0;
}

static int cxt5066_mic_boost_mux_enum_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	const struct hda_input_mux *imux = &cxt5066_analog_mic_boost;
	unsigned int idx;
	idx = ucontrol->value.enumerated.item[0];
	if (idx >= imux->num_items)
		idx = imux->num_items - 1;

	spec->mic_boost = idx;
	if (!spec->dc_enable)
		cxt5066_set_mic_boost(codec);
	return 1;
}

static void cxt5066_enable_dc(struct hda_codec *codec)
{
	const struct hda_verb enable_dc_mode[] = {
		/* disable gain */
		{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

		/* switch to DC input */
		{0x17, AC_VERB_SET_CONNECT_SEL, 3},
		{}
	};

	/* configure as input source */
	snd_hda_sequence_write(codec, enable_dc_mode);
	cxt5066_olpc_select_mic(codec); /* also sets configured bias */
}

static void cxt5066_disable_dc(struct hda_codec *codec)
{
	/* reconfigure input source */
	cxt5066_set_mic_boost(codec);
	/* automic also selects the right mic if we're recording */
	cxt5066_olpc_automic(codec);
}

static int cxt5066_olpc_dc_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	ucontrol->value.integer.value[0] = spec->dc_enable;
	return 0;
}

static int cxt5066_olpc_dc_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	int dc_enable = !!ucontrol->value.integer.value[0];

	if (dc_enable == spec->dc_enable)
		return 0;

	spec->dc_enable = dc_enable;
	if (dc_enable)
		cxt5066_enable_dc(codec);
	else
		cxt5066_disable_dc(codec);

	return 1;
}

static int cxt5066_olpc_dc_bias_enum_info(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_info *uinfo)
{
	return snd_hda_input_mux_info(&cxt5066_olpc_dc_bias, uinfo);
}

static int cxt5066_olpc_dc_bias_enum_get(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	ucontrol->value.enumerated.item[0] = spec->dc_input_bias;
	return 0;
}

static int cxt5066_olpc_dc_bias_enum_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct conexant_spec *spec = codec->spec;
	const struct hda_input_mux *imux = &cxt5066_analog_mic_boost;
	unsigned int idx;

	idx = ucontrol->value.enumerated.item[0];
	if (idx >= imux->num_items)
		idx = imux->num_items - 1;

	spec->dc_input_bias = idx;
	if (spec->dc_enable)
		cxt5066_set_olpc_dc_bias(codec);
	return 1;
}

static void cxt5066_olpc_capture_prepare(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	/* mark as recording and configure the microphone widget so that the
	 * recording LED comes on. */
	spec->recording = 1;
	cxt5066_olpc_select_mic(codec);
}

static void cxt5066_olpc_capture_cleanup(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	const struct hda_verb disable_mics[] = {
		/* disable external mic, port B */
		{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

		/* disble internal mic, port C */
		{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

		/* disable DC capture, port F */
		{0x1e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
		{},
	};

	snd_hda_sequence_write(codec, disable_mics);
	spec->recording = 0;
}

static void conexant_check_dig_outs(struct hda_codec *codec,
				    const hda_nid_t *dig_pins,
				    int num_pins)
{
	struct conexant_spec *spec = codec->spec;
	hda_nid_t *nid_loc = &spec->multiout.dig_out_nid;
	int i;

	for (i = 0; i < num_pins; i++, dig_pins++) {
		unsigned int cfg = snd_hda_codec_get_pincfg(codec, *dig_pins);
		if (get_defcfg_connect(cfg) == AC_JACK_PORT_NONE)
			continue;
		if (snd_hda_get_connections(codec, *dig_pins, nid_loc, 1) != 1)
			continue;
	}
}

static const struct hda_input_mux cxt5066_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic B", 0 },
		{ "Mic C", 1 },
		{ "Mic E", 2 },
		{ "Mic F", 3 },
	},
};

static const struct hda_bind_ctls cxt5066_bind_capture_vol_others = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x14, 3, 2, HDA_INPUT),
		0
	},
};

static const struct hda_bind_ctls cxt5066_bind_capture_sw_others = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x14, 3, 2, HDA_INPUT),
		0
	},
};

static const struct snd_kcontrol_new cxt5066_mixer_master[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x10, 0x00, HDA_OUTPUT),
	{}
};

static const struct snd_kcontrol_new cxt5066_mixer_master_olpc[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Volume",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
				  SNDRV_CTL_ELEM_ACCESS_TLV_READ |
				  SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK,
		.subdevice = HDA_SUBDEV_AMP_FLAG,
		.info = snd_hda_mixer_amp_volume_info,
		.get = snd_hda_mixer_amp_volume_get,
		.put = snd_hda_mixer_amp_volume_put,
		.tlv = { .c = snd_hda_mixer_amp_tlv },
		/* offset by 28 volume steps to limit minimum gain to -46dB */
		.private_value =
			HDA_COMPOSE_AMP_VAL_OFS(0x10, 3, 0, HDA_OUTPUT, 28),
	},
	{}
};

static const struct snd_kcontrol_new cxt5066_mixer_olpc_dc[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "DC Mode Enable Switch",
		.info = snd_ctl_boolean_mono_info,
		.get = cxt5066_olpc_dc_get,
		.put = cxt5066_olpc_dc_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "DC Input Bias Enum",
		.info = cxt5066_olpc_dc_bias_enum_info,
		.get = cxt5066_olpc_dc_bias_enum_get,
		.put = cxt5066_olpc_dc_bias_enum_put,
	},
	{}
};

static const struct snd_kcontrol_new cxt5066_mixers[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = cxt_eapd_info,
		.get = cxt_eapd_get,
		.put = cxt5066_hp_master_sw_put,
		.private_value = 0x1d,
	},

	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog Mic Boost Capture Enum",
		.info = cxt5066_mic_boost_mux_enum_info,
		.get = cxt5066_mic_boost_mux_enum_get,
		.put = cxt5066_mic_boost_mux_enum_put,
	},

	HDA_BIND_VOL("Capture Volume", &cxt5066_bind_capture_vol_others),
	HDA_BIND_SW("Capture Switch", &cxt5066_bind_capture_sw_others),
	{}
};

static const struct snd_kcontrol_new cxt5066_vostro_mixers[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Internal Mic Boost Capture Enum",
		.info = cxt5066_mic_boost_mux_enum_info,
		.get = cxt5066_mic_boost_mux_enum_get,
		.put = cxt5066_mic_boost_mux_enum_put,
		.private_value = 0x23 | 0x100,
	},
	{}
};

static const struct hda_verb cxt5066_init_verbs[] = {
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80}, /* Port B */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80}, /* Port C */
	{0x1e, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN}, /* Port F */
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN}, /* Port E */

	/* Speakers  */
	{0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1f, AC_VERB_SET_CONNECT_SEL, 0x00}, /* DAC1 */

	/* HP, Amp  */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x19, AC_VERB_SET_CONNECT_SEL, 0x00}, /* DAC1 */

	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1c, AC_VERB_SET_CONNECT_SEL, 0x00}, /* DAC1 */

	/* DAC1 */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Node 14 connections: 0x17 0x18 0x23 0x24 0x27 */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x50},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2) | 0x50},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/* no digital microphone support yet */
	{0x23, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

	/* Audio input selector */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE | 0x3},

	/* SPDIF route: PCM */
	{0x20, AC_VERB_SET_CONNECT_SEL, 0x0},
	{0x22, AC_VERB_SET_CONNECT_SEL, 0x0},

	{0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x22, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* EAPD */
	{0x1d, AC_VERB_SET_EAPD_BTLENABLE, 0x2}, /* default on */

	/* not handling these yet */
	{0x19, AC_VERB_SET_UNSOLICITED_ENABLE, 0},
	{0x1a, AC_VERB_SET_UNSOLICITED_ENABLE, 0},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, 0},
	{0x1c, AC_VERB_SET_UNSOLICITED_ENABLE, 0},
	{0x1d, AC_VERB_SET_UNSOLICITED_ENABLE, 0},
	{0x1e, AC_VERB_SET_UNSOLICITED_ENABLE, 0},
	{0x20, AC_VERB_SET_UNSOLICITED_ENABLE, 0},
	{0x22, AC_VERB_SET_UNSOLICITED_ENABLE, 0},
	{ } /* end */
};

static const struct hda_verb cxt5066_init_verbs_olpc[] = {
	/* Port A: headphones */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x19, AC_VERB_SET_CONNECT_SEL, 0x00}, /* DAC1 */

	/* Port B: external microphone */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

	/* Port C: internal microphone */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

	/* Port D: unused */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

	/* Port E: unused, but has primary EAPD */
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x1d, AC_VERB_SET_EAPD_BTLENABLE, 0x2}, /* default on */

	/* Port F: external DC input through microphone port */
	{0x1e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

	/* Port G: internal speakers */
	{0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1f, AC_VERB_SET_CONNECT_SEL, 0x00}, /* DAC1 */

	/* DAC1 */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* DAC2: unused */
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x50},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},

	/* Disable digital microphone port */
	{0x23, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

	/* Audio input selectors */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE | 0x3},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },

	/* Disable SPDIF */
	{0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x22, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

	/* enable unsolicited events for Port A and B */
	{0x19, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_HP_EVENT},
	{0x1a, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_MIC_EVENT},
	{ } /* end */
};

static const struct hda_verb cxt5066_init_verbs_vostro[] = {
	/* Port A: headphones */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x19, AC_VERB_SET_CONNECT_SEL, 0x00}, /* DAC1 */

	/* Port B: external microphone */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

	/* Port C: unused */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

	/* Port D: unused */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

	/* Port E: unused, but has primary EAPD */
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x1d, AC_VERB_SET_EAPD_BTLENABLE, 0x2}, /* default on */

	/* Port F: unused */
	{0x1e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

	/* Port G: internal speakers */
	{0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1f, AC_VERB_SET_CONNECT_SEL, 0x00}, /* DAC1 */

	/* DAC1 */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* DAC2: unused */
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},

	/* Digital microphone port */
	{0x23, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	/* Audio input selectors */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE | 0x3},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },

	/* Disable SPDIF */
	{0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x22, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

	/* enable unsolicited events for Port A and B */
	{0x19, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_HP_EVENT},
	{0x1a, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_MIC_EVENT},
	{ } /* end */
};

static const struct hda_verb cxt5066_init_verbs_ideapad[] = {
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80}, /* Port B */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80}, /* Port C */
	{0x1e, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN}, /* Port F */
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN}, /* Port E */

	/* Speakers  */
	{0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1f, AC_VERB_SET_CONNECT_SEL, 0x00}, /* DAC1 */

	/* HP, Amp  */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x19, AC_VERB_SET_CONNECT_SEL, 0x00}, /* DAC1 */

	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1c, AC_VERB_SET_CONNECT_SEL, 0x00}, /* DAC1 */

	/* DAC1 */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Node 14 connections: 0x17 0x18 0x23 0x24 0x27 */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x50},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2) | 0x50},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{0x14, AC_VERB_SET_CONNECT_SEL, 2},	/* default to internal mic */

	/* Audio input selector */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE | 0x2},
	{0x17, AC_VERB_SET_CONNECT_SEL, 1},	/* route ext mic */

	/* SPDIF route: PCM */
	{0x20, AC_VERB_SET_CONNECT_SEL, 0x0},
	{0x22, AC_VERB_SET_CONNECT_SEL, 0x0},

	{0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x22, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* internal microphone */
	{0x23, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN}, /* enable internal mic */

	/* EAPD */
	{0x1d, AC_VERB_SET_EAPD_BTLENABLE, 0x2}, /* default on */

	{0x19, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_HP_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_MIC_EVENT},
	{ } /* end */
};

static const struct hda_verb cxt5066_init_verbs_thinkpad[] = {
	{0x1e, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN}, /* Port F */
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN}, /* Port E */

	/* Port G: internal speakers  */
	{0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1f, AC_VERB_SET_CONNECT_SEL, 0x00}, /* DAC1 */

	/* Port A: HP, Amp  */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x19, AC_VERB_SET_CONNECT_SEL, 0x00}, /* DAC1 */

	/* Port B: Mic Dock */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

	/* Port C: Mic */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},

	/* Port D: HP Dock, Amp */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x1c, AC_VERB_SET_CONNECT_SEL, 0x00}, /* DAC1 */

	/* DAC1 */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Node 14 connections: 0x17 0x18 0x23 0x24 0x27 */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) | 0x50},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2) | 0x50},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{0x14, AC_VERB_SET_CONNECT_SEL, 2},	/* default to internal mic */

	/* Audio input selector */
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE | 0x2},
	{0x17, AC_VERB_SET_CONNECT_SEL, 1},	/* route ext mic */

	/* SPDIF route: PCM */
	{0x20, AC_VERB_SET_CONNECT_SEL, 0x0},
	{0x22, AC_VERB_SET_CONNECT_SEL, 0x0},

	{0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x22, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* internal microphone */
	{0x23, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN}, /* enable internal mic */

	/* EAPD */
	{0x1d, AC_VERB_SET_EAPD_BTLENABLE, 0x2}, /* default on */

	/* enable unsolicited events for Port A, B, C and D */
	{0x19, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_HP_EVENT},
	{0x1c, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_HP_EVENT},
	{0x1a, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_MIC_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_MIC_EVENT},
	{ } /* end */
};

static const struct hda_verb cxt5066_init_verbs_portd_lo[] = {
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{ } /* end */
};


static const struct hda_verb cxt5066_init_verbs_hp_laptop[] = {
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x0},
	{0x19, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_HP_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | CONEXANT_MIC_EVENT},
	{ } /* end */
};

/* initialize jack-sensing, too */
static int cxt5066_init(struct hda_codec *codec)
{
	snd_printdd("CXT5066: init\n");
	conexant_init(codec);
	if (codec->patch_ops.unsol_event) {
		cxt5066_hp_automute(codec);
		cxt5066_automic(codec);
	}
	cxt5066_set_mic_boost(codec);
	return 0;
}

static int cxt5066_olpc_init(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	snd_printdd("CXT5066: init\n");
	conexant_init(codec);
	cxt5066_hp_automute(codec);
	if (!spec->dc_enable) {
		cxt5066_set_mic_boost(codec);
		cxt5066_olpc_automic(codec);
	} else {
		cxt5066_enable_dc(codec);
	}
	return 0;
}

enum {
	CXT5066_LAPTOP,		/* Laptops w/ EAPD support */
	CXT5066_DELL_LAPTOP,	/* Dell Laptop */
	CXT5066_OLPC_XO_1_5,	/* OLPC XO 1.5 */
	CXT5066_DELL_VOSTRO,	/* Dell Vostro 1015i */
	CXT5066_IDEAPAD,	/* Lenovo IdeaPad U150 */
	CXT5066_THINKPAD,	/* Lenovo ThinkPad T410s, others? */
	CXT5066_ASUS,		/* Asus K52JU, Lenovo G560 - Int mic at 0x1a and Ext mic at 0x1b */
	CXT5066_HP_LAPTOP,      /* HP Laptop */
	CXT5066_AUTO,		/* BIOS auto-parser */
	CXT5066_MODELS
};

static const char * const cxt5066_models[CXT5066_MODELS] = {
	[CXT5066_LAPTOP]	= "laptop",
	[CXT5066_DELL_LAPTOP]	= "dell-laptop",
	[CXT5066_OLPC_XO_1_5]	= "olpc-xo-1_5",
	[CXT5066_DELL_VOSTRO]	= "dell-vostro",
	[CXT5066_IDEAPAD]	= "ideapad",
	[CXT5066_THINKPAD]	= "thinkpad",
	[CXT5066_ASUS]		= "asus",
	[CXT5066_HP_LAPTOP]	= "hp-laptop",
	[CXT5066_AUTO]		= "auto",
};

static const struct snd_pci_quirk cxt5066_cfg_tbl[] = {
	SND_PCI_QUIRK_MASK(0x1025, 0xff00, 0x0400, "Acer", CXT5066_IDEAPAD),
	SND_PCI_QUIRK(0x1028, 0x02d8, "Dell Vostro", CXT5066_DELL_VOSTRO),
	SND_PCI_QUIRK(0x1028, 0x02f5, "Dell Vostro 320", CXT5066_IDEAPAD),
	SND_PCI_QUIRK(0x1028, 0x0401, "Dell Vostro 1014", CXT5066_DELL_VOSTRO),
	SND_PCI_QUIRK(0x1028, 0x0408, "Dell Inspiron One 19T", CXT5066_IDEAPAD),
	SND_PCI_QUIRK(0x1028, 0x050f, "Dell Inspiron", CXT5066_IDEAPAD),
	SND_PCI_QUIRK(0x1028, 0x0510, "Dell Vostro", CXT5066_IDEAPAD),
	SND_PCI_QUIRK(0x103c, 0x360b, "HP G60", CXT5066_HP_LAPTOP),
	SND_PCI_QUIRK(0x1043, 0x13f3, "Asus A52J", CXT5066_ASUS),
	SND_PCI_QUIRK(0x1043, 0x1643, "Asus K52JU", CXT5066_ASUS),
	SND_PCI_QUIRK(0x1043, 0x1993, "Asus U50F", CXT5066_ASUS),
	SND_PCI_QUIRK(0x1179, 0xff1e, "Toshiba Satellite C650D", CXT5066_IDEAPAD),
	SND_PCI_QUIRK(0x1179, 0xff50, "Toshiba Satellite P500-PSPGSC-01800T", CXT5066_OLPC_XO_1_5),
	SND_PCI_QUIRK(0x14f1, 0x0101, "Conexant Reference board",
		      CXT5066_LAPTOP),
	SND_PCI_QUIRK(0x152d, 0x0833, "OLPC XO-1.5", CXT5066_OLPC_XO_1_5),
	SND_PCI_QUIRK(0x17aa, 0x20f2, "Lenovo T400s", CXT5066_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x21c5, "Thinkpad Edge 13", CXT5066_THINKPAD),
	SND_PCI_QUIRK(0x17aa, 0x21c6, "Thinkpad Edge 13", CXT5066_ASUS),
	SND_PCI_QUIRK(0x17aa, 0x3a0d, "Lenovo U350", CXT5066_ASUS),
	SND_PCI_QUIRK(0x17aa, 0x38af, "Lenovo G560", CXT5066_ASUS),
	{}
};

static int patch_cxt5066(struct hda_codec *codec)
{
	struct conexant_spec *spec;
	int board_config;

	board_config = snd_hda_check_board_config(codec, CXT5066_MODELS,
						  cxt5066_models, cxt5066_cfg_tbl);
	if (board_config < 0)
		board_config = CXT5066_AUTO; /* model=auto as default */
	if (board_config == CXT5066_AUTO)
		return patch_conexant_auto(codec);

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	codec->spec = spec;

	codec->patch_ops = conexant_patch_ops;
	codec->patch_ops.init = conexant_init;

	spec->dell_automute = 0;
	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = ARRAY_SIZE(cxt5066_dac_nids);
	spec->multiout.dac_nids = cxt5066_dac_nids;
	conexant_check_dig_outs(codec, cxt5066_digout_pin_nids,
	    ARRAY_SIZE(cxt5066_digout_pin_nids));
	spec->num_adc_nids = 1;
	spec->adc_nids = cxt5066_adc_nids;
	spec->capsrc_nids = cxt5066_capsrc_nids;
	spec->input_mux = &cxt5066_capture_source;

	spec->port_d_mode = PIN_HP;

	spec->num_init_verbs = 1;
	spec->init_verbs[0] = cxt5066_init_verbs;
	spec->num_channel_mode = ARRAY_SIZE(cxt5066_modes);
	spec->channel_mode = cxt5066_modes;
	spec->cur_adc = 0;
	spec->cur_adc_idx = 0;

	set_beep_amp(spec, 0x13, 0, HDA_OUTPUT);

	switch (board_config) {
	default:
	case CXT5066_LAPTOP:
		spec->mixers[spec->num_mixers++] = cxt5066_mixer_master;
		spec->mixers[spec->num_mixers++] = cxt5066_mixers;
		break;
	case CXT5066_DELL_LAPTOP:
		spec->mixers[spec->num_mixers++] = cxt5066_mixer_master;
		spec->mixers[spec->num_mixers++] = cxt5066_mixers;

		spec->port_d_mode = PIN_OUT;
		spec->init_verbs[spec->num_init_verbs] = cxt5066_init_verbs_portd_lo;
		spec->num_init_verbs++;
		spec->dell_automute = 1;
		break;
	case CXT5066_ASUS:
	case CXT5066_HP_LAPTOP:
		codec->patch_ops.init = cxt5066_init;
		codec->patch_ops.unsol_event = cxt5066_unsol_event;
		spec->init_verbs[spec->num_init_verbs] =
			cxt5066_init_verbs_hp_laptop;
		spec->num_init_verbs++;
		spec->hp_laptop = board_config == CXT5066_HP_LAPTOP;
		spec->asus = board_config == CXT5066_ASUS;
		spec->mixers[spec->num_mixers++] = cxt5066_mixer_master;
		spec->mixers[spec->num_mixers++] = cxt5066_mixers;
		/* no S/PDIF out */
		if (board_config == CXT5066_HP_LAPTOP)
			spec->multiout.dig_out_nid = 0;
		/* input source automatically selected */
		spec->input_mux = NULL;
		spec->port_d_mode = 0;
		spec->mic_boost = 3; /* default 30dB gain */
		break;

	case CXT5066_OLPC_XO_1_5:
		codec->patch_ops.init = cxt5066_olpc_init;
		codec->patch_ops.unsol_event = cxt5066_olpc_unsol_event;
		spec->init_verbs[0] = cxt5066_init_verbs_olpc;
		spec->mixers[spec->num_mixers++] = cxt5066_mixer_master_olpc;
		spec->mixers[spec->num_mixers++] = cxt5066_mixer_olpc_dc;
		spec->mixers[spec->num_mixers++] = cxt5066_mixers;
		spec->port_d_mode = 0;
		spec->mic_boost = 3; /* default 30dB gain */

		/* no S/PDIF out */
		spec->multiout.dig_out_nid = 0;

		/* input source automatically selected */
		spec->input_mux = NULL;

		/* our capture hooks which allow us to turn on the microphone LED
		 * at the right time */
		spec->capture_prepare = cxt5066_olpc_capture_prepare;
		spec->capture_cleanup = cxt5066_olpc_capture_cleanup;
		break;
	case CXT5066_DELL_VOSTRO:
		codec->patch_ops.init = cxt5066_init;
		codec->patch_ops.unsol_event = cxt5066_unsol_event;
		spec->init_verbs[0] = cxt5066_init_verbs_vostro;
		spec->mixers[spec->num_mixers++] = cxt5066_mixer_master_olpc;
		spec->mixers[spec->num_mixers++] = cxt5066_mixers;
		spec->mixers[spec->num_mixers++] = cxt5066_vostro_mixers;
		spec->port_d_mode = 0;
		spec->dell_vostro = 1;
		spec->mic_boost = 3; /* default 30dB gain */

		/* no S/PDIF out */
		spec->multiout.dig_out_nid = 0;

		/* input source automatically selected */
		spec->input_mux = NULL;
		break;
	case CXT5066_IDEAPAD:
		codec->patch_ops.init = cxt5066_init;
		codec->patch_ops.unsol_event = cxt5066_unsol_event;
		spec->mixers[spec->num_mixers++] = cxt5066_mixer_master;
		spec->mixers[spec->num_mixers++] = cxt5066_mixers;
		spec->init_verbs[0] = cxt5066_init_verbs_ideapad;
		spec->port_d_mode = 0;
		spec->ideapad = 1;
		spec->mic_boost = 2;	/* default 20dB gain */

		/* no S/PDIF out */
		spec->multiout.dig_out_nid = 0;

		/* input source automatically selected */
		spec->input_mux = NULL;
		break;
	case CXT5066_THINKPAD:
		codec->patch_ops.init = cxt5066_init;
		codec->patch_ops.unsol_event = cxt5066_unsol_event;
		spec->mixers[spec->num_mixers++] = cxt5066_mixer_master;
		spec->mixers[spec->num_mixers++] = cxt5066_mixers;
		spec->init_verbs[0] = cxt5066_init_verbs_thinkpad;
		spec->thinkpad = 1;
		spec->port_d_mode = PIN_OUT;
		spec->mic_boost = 2;	/* default 20dB gain */

		/* no S/PDIF out */
		spec->multiout.dig_out_nid = 0;

		/* input source automatically selected */
		spec->input_mux = NULL;
		break;
	}

	if (spec->beep_amp)
		snd_hda_attach_beep_device(codec, get_amp_nid_(spec->beep_amp));

	return 0;
}

#endif /* ENABLE_CXT_STATIC_QUIRKS */


/*
 * Automatic parser for CX20641 & co
 */

#ifdef CONFIG_SND_HDA_INPUT_BEEP
static void cx_auto_parse_beep(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	hda_nid_t nid, end_nid;

	end_nid = codec->start_nid + codec->num_nodes;
	for (nid = codec->start_nid; nid < end_nid; nid++)
		if (get_wcaps_type(get_wcaps(codec, nid)) == AC_WID_BEEP) {
			set_beep_amp(spec, nid, 0, HDA_OUTPUT);
			break;
		}
}
#else
#define cx_auto_parse_beep(codec)
#endif

/* parse EAPDs */
static void cx_auto_parse_eapd(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	hda_nid_t nid, end_nid;

	end_nid = codec->start_nid + codec->num_nodes;
	for (nid = codec->start_nid; nid < end_nid; nid++) {
		if (get_wcaps_type(get_wcaps(codec, nid)) != AC_WID_PIN)
			continue;
		if (!(snd_hda_query_pin_caps(codec, nid) & AC_PINCAP_EAPD))
			continue;
		spec->eapds[spec->num_eapds++] = nid;
		if (spec->num_eapds >= ARRAY_SIZE(spec->eapds))
			break;
	}

	/* NOTE: below is a wild guess; if we have more than two EAPDs,
	 * it's a new chip, where EAPDs are supposed to be associated to
	 * pins, and we can control EAPD per pin.
	 * OTOH, if only one or two EAPDs are found, it's an old chip,
	 * thus it might control over all pins.
	 */
	if (spec->num_eapds > 2)
		spec->dynamic_eapd = 1;
}

static void cx_auto_turn_eapd(struct hda_codec *codec, int num_pins,
			      hda_nid_t *pins, bool on)
{
	int i;
	for (i = 0; i < num_pins; i++) {
		if (snd_hda_query_pin_caps(codec, pins[i]) & AC_PINCAP_EAPD)
			snd_hda_codec_write(codec, pins[i], 0,
					    AC_VERB_SET_EAPD_BTLENABLE,
					    on ? 0x02 : 0);
	}
}

/* turn on/off EAPD according to Master switch */
static void cx_auto_vmaster_hook(void *private_data, int enabled)
{
	struct hda_codec *codec = private_data;
	struct conexant_spec *spec = codec->spec;

	cx_auto_turn_eapd(codec, spec->num_eapds, spec->eapds, enabled);
}

static int cx_auto_build_controls(struct hda_codec *codec)
{
	int err;

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;

	err = add_beep_ctls(codec);
	if (err < 0)
		return err;

	return 0;
}

static int cx_auto_init(struct hda_codec *codec)
{
	struct conexant_spec *spec = codec->spec;
	snd_hda_gen_init(codec);
	if (!spec->dynamic_eapd)
		cx_auto_turn_eapd(codec, spec->num_eapds, spec->eapds, true);

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_INIT);

	return 0;
}

static void cx_auto_free(struct hda_codec *codec)
{
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_FREE);
	snd_hda_gen_free(codec);
}

static const struct hda_codec_ops cx_auto_patch_ops = {
	.build_controls = cx_auto_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = cx_auto_init,
	.free = cx_auto_free,
	.unsol_event = snd_hda_jack_unsol_event,
#ifdef CONFIG_PM
	.check_power_status = snd_hda_gen_check_power_status,
#endif
};

/*
 * pin fix-up
 */
enum {
	CXT_PINCFG_LENOVO_X200,
	CXT_PINCFG_LENOVO_TP410,
	CXT_PINCFG_LEMOTE_A1004,
	CXT_PINCFG_LEMOTE_A1205,
	CXT_FIXUP_STEREO_DMIC,
	CXT_FIXUP_INC_MIC_BOOST,
	CXT_FIXUP_HEADPHONE_MIC_PIN,
	CXT_FIXUP_HEADPHONE_MIC,
	CXT_FIXUP_GPIO1,
	CXT_FIXUP_THINKPAD_ACPI,
};

#if IS_ENABLED(CONFIG_THINKPAD_ACPI)

#include <linux/thinkpad_acpi.h>
#include <acpi/acpi.h>

static int (*led_set_func)(int, bool);

static acpi_status acpi_check_cb(acpi_handle handle, u32 lvl, void *context,
				 void **rv)
{
	bool *found = context;
	*found = true;
	return AE_OK;
}

static bool is_thinkpad(struct hda_codec *codec)
{
	bool found = false;
	if (codec->subsystem_id >> 16 != 0x17aa)
		return false;
	if (ACPI_SUCCESS(acpi_get_devices("LEN0068", acpi_check_cb, &found, NULL)) && found)
		return true;
	found = false;
	return ACPI_SUCCESS(acpi_get_devices("IBM0068", acpi_check_cb, &found, NULL)) && found;
}

static void update_tpacpi_mute_led(void *private_data, int enabled)
{
	struct hda_codec *codec = private_data;
	struct conexant_spec *spec = codec->spec;

	if (spec->dynamic_eapd)
		cx_auto_vmaster_hook(private_data, enabled);

	if (led_set_func)
		led_set_func(TPACPI_LED_MUTE, !enabled);
}

static void update_tpacpi_micmute_led(struct hda_codec *codec,
				      struct snd_ctl_elem_value *ucontrol)
{
	if (!ucontrol || !led_set_func)
		return;
	if (strcmp("Capture Switch", ucontrol->id.name) == 0 && ucontrol->id.index == 0) {
		/* TODO: How do I verify if it's a mono or stereo here? */
		bool val = ucontrol->value.integer.value[0] || ucontrol->value.integer.value[1];
		led_set_func(TPACPI_LED_MICMUTE, !val);
	}
}

static void cxt_fixup_thinkpad_acpi(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	struct conexant_spec *spec = codec->spec;

	bool removefunc = false;

	if (action == HDA_FIXUP_ACT_PROBE) {
		if (!is_thinkpad(codec))
			return;
		if (!led_set_func)
			led_set_func = symbol_request(tpacpi_led_set);
		if (!led_set_func) {
			snd_printk(KERN_WARNING "Failed to find thinkpad-acpi symbol tpacpi_led_set\n");
			return;
		}

		removefunc = true;
		if (led_set_func(TPACPI_LED_MUTE, false) >= 0) {
			spec->gen.vmaster_mute.hook = update_tpacpi_mute_led;
			removefunc = false;
		}
		if (led_set_func(TPACPI_LED_MICMUTE, false) >= 0) {
			if (spec->gen.num_adc_nids > 1)
				snd_printdd("Skipping micmute LED control due to several ADCs");
			else {
				spec->gen.cap_sync_hook = update_tpacpi_micmute_led;
				removefunc = false;
			}
		}
	}

	if (led_set_func && (action == HDA_FIXUP_ACT_FREE || removefunc)) {
		symbol_put(tpacpi_led_set);
		led_set_func = NULL;
	}
}

#else

static void cxt_fixup_thinkpad_acpi(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
}

#endif

static void cxt_fixup_stereo_dmic(struct hda_codec *codec,
				  const struct hda_fixup *fix, int action)
{
	struct conexant_spec *spec = codec->spec;
	spec->gen.inv_dmic_split = 1;
}

static void cxt5066_increase_mic_boost(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	snd_hda_override_amp_caps(codec, 0x17, HDA_OUTPUT,
				  (0x3 << AC_AMPCAP_OFFSET_SHIFT) |
				  (0x4 << AC_AMPCAP_NUM_STEPS_SHIFT) |
				  (0x27 << AC_AMPCAP_STEP_SIZE_SHIFT) |
				  (0 << AC_AMPCAP_MUTE_SHIFT));
}

static void cxt_update_headset_mode(struct hda_codec *codec)
{
	/* The verbs used in this function were tested on a Conexant CX20751/2 codec. */
	int i;
	bool mic_mode = false;
	struct conexant_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->gen.autocfg;

	hda_nid_t mux_pin = spec->gen.imux_pins[spec->gen.cur_mux[0]];

	for (i = 0; i < cfg->num_inputs; i++)
		if (cfg->inputs[i].pin == mux_pin) {
			mic_mode = !!cfg->inputs[i].is_headphone_mic;
			break;
		}

	if (mic_mode) {
		snd_hda_codec_write_cache(codec, 0x1c, 0, 0x410, 0x7c); /* enable merged mode for analog int-mic */
		spec->gen.hp_jack_present = false;
	} else {
		snd_hda_codec_write_cache(codec, 0x1c, 0, 0x410, 0x54); /* disable merged mode for analog int-mic */
		spec->gen.hp_jack_present = snd_hda_jack_detect(codec, spec->gen.autocfg.hp_pins[0]);
	}

	snd_hda_gen_update_outputs(codec);
}

static void cxt_update_headset_mode_hook(struct hda_codec *codec,
			     struct snd_ctl_elem_value *ucontrol)
{
	cxt_update_headset_mode(codec);
}

static void cxt_fixup_headphone_mic(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct conexant_spec *spec = codec->spec;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->parse_flags |= HDA_PINCFG_HEADPHONE_MIC;
		break;
	case HDA_FIXUP_ACT_PROBE:
		spec->gen.cap_sync_hook = cxt_update_headset_mode_hook;
		spec->gen.automute_hook = cxt_update_headset_mode;
		break;
	case HDA_FIXUP_ACT_INIT:
		cxt_update_headset_mode(codec);
		break;
	}
}


/* ThinkPad X200 & co with cxt5051 */
static const struct hda_pintbl cxt_pincfg_lenovo_x200[] = {
	{ 0x16, 0x042140ff }, /* HP (seq# overridden) */
	{ 0x17, 0x21a11000 }, /* dock-mic */
	{ 0x19, 0x2121103f }, /* dock-HP */
	{ 0x1c, 0x21440100 }, /* dock SPDIF out */
	{}
};

/* ThinkPad 410/420/510/520, X201 & co with cxt5066 */
static const struct hda_pintbl cxt_pincfg_lenovo_tp410[] = {
	{ 0x19, 0x042110ff }, /* HP (seq# overridden) */
	{ 0x1a, 0x21a190f0 }, /* dock-mic */
	{ 0x1c, 0x212140ff }, /* dock-HP */
	{}
};

/* Lemote A1004/A1205 with cxt5066 */
static const struct hda_pintbl cxt_pincfg_lemote[] = {
	{ 0x1a, 0x90a10020 }, /* Internal mic */
	{ 0x1b, 0x03a11020 }, /* External mic */
	{ 0x1d, 0x400101f0 }, /* Not used */
	{ 0x1e, 0x40a701f0 }, /* Not used */
	{ 0x20, 0x404501f0 }, /* Not used */
	{ 0x22, 0x404401f0 }, /* Not used */
	{ 0x23, 0x40a701f0 }, /* Not used */
	{}
};

static const struct hda_fixup cxt_fixups[] = {
	[CXT_PINCFG_LENOVO_X200] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = cxt_pincfg_lenovo_x200,
	},
	[CXT_PINCFG_LENOVO_TP410] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = cxt_pincfg_lenovo_tp410,
		.chained = true,
		.chain_id = CXT_FIXUP_THINKPAD_ACPI,
	},
	[CXT_PINCFG_LEMOTE_A1004] = {
		.type = HDA_FIXUP_PINS,
		.chained = true,
		.chain_id = CXT_FIXUP_INC_MIC_BOOST,
		.v.pins = cxt_pincfg_lemote,
	},
	[CXT_PINCFG_LEMOTE_A1205] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = cxt_pincfg_lemote,
	},
	[CXT_FIXUP_STEREO_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cxt_fixup_stereo_dmic,
	},
	[CXT_FIXUP_INC_MIC_BOOST] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cxt5066_increase_mic_boost,
	},
	[CXT_FIXUP_HEADPHONE_MIC_PIN] = {
		.type = HDA_FIXUP_PINS,
		.chained = true,
		.chain_id = CXT_FIXUP_HEADPHONE_MIC,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x18, 0x03a1913d }, /* use as headphone mic, without its own jack detect */
			{ }
		}
	},
	[CXT_FIXUP_HEADPHONE_MIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cxt_fixup_headphone_mic,
	},
	[CXT_FIXUP_GPIO1] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x01, AC_VERB_SET_GPIO_MASK, 0x01 },
			{ 0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01 },
			{ 0x01, AC_VERB_SET_GPIO_DATA, 0x01 },
			{ }
		},
	},
	[CXT_FIXUP_THINKPAD_ACPI] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cxt_fixup_thinkpad_acpi,
	},
};

static const struct snd_pci_quirk cxt5051_fixups[] = {
	SND_PCI_QUIRK(0x17aa, 0x20f2, "Lenovo X200", CXT_PINCFG_LENOVO_X200),
	{}
};

static const struct snd_pci_quirk cxt5066_fixups[] = {
	SND_PCI_QUIRK(0x1025, 0x0543, "Acer Aspire One 522", CXT_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x1025, 0x054c, "Acer Aspire 3830TG", CXT_FIXUP_GPIO1),
	SND_PCI_QUIRK(0x1043, 0x138d, "Asus", CXT_FIXUP_HEADPHONE_MIC_PIN),
	SND_PCI_QUIRK(0x17aa, 0x20f2, "Lenovo T400", CXT_PINCFG_LENOVO_TP410),
	SND_PCI_QUIRK(0x17aa, 0x215e, "Lenovo T410", CXT_PINCFG_LENOVO_TP410),
	SND_PCI_QUIRK(0x17aa, 0x215f, "Lenovo T510", CXT_PINCFG_LENOVO_TP410),
	SND_PCI_QUIRK(0x17aa, 0x21ce, "Lenovo T420", CXT_PINCFG_LENOVO_TP410),
	SND_PCI_QUIRK(0x17aa, 0x21cf, "Lenovo T520", CXT_PINCFG_LENOVO_TP410),
	SND_PCI_QUIRK(0x17aa, 0x21da, "Lenovo X220", CXT_PINCFG_LENOVO_TP410),
	SND_PCI_QUIRK(0x17aa, 0x21db, "Lenovo X220-tablet", CXT_PINCFG_LENOVO_TP410),
	SND_PCI_QUIRK(0x17aa, 0x3975, "Lenovo U300s", CXT_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x17aa, 0x3977, "Lenovo IdeaPad U310", CXT_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK(0x17aa, 0x397b, "Lenovo S205", CXT_FIXUP_STEREO_DMIC),
	SND_PCI_QUIRK_VENDOR(0x17aa, "Thinkpad", CXT_FIXUP_THINKPAD_ACPI),
	SND_PCI_QUIRK(0x1c06, 0x2011, "Lemote A1004", CXT_PINCFG_LEMOTE_A1004),
	SND_PCI_QUIRK(0x1c06, 0x2012, "Lemote A1205", CXT_PINCFG_LEMOTE_A1205),
	{}
};

/* add "fake" mute amp-caps to DACs on cx5051 so that mixer mute switches
 * can be created (bko#42825)
 */
static void add_cx5051_fake_mutes(struct hda_codec *codec)
{
	static hda_nid_t out_nids[] = {
		0x10, 0x11, 0
	};
	hda_nid_t *p;

	for (p = out_nids; *p; p++)
		snd_hda_override_amp_caps(codec, *p, HDA_OUTPUT,
					  AC_AMPCAP_MIN_MUTE |
					  query_amp_caps(codec, *p, HDA_OUTPUT));
}

static int patch_conexant_auto(struct hda_codec *codec)
{
	struct conexant_spec *spec;
	int err;

	printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
	       codec->chip_name);

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	snd_hda_gen_spec_init(&spec->gen);
	codec->spec = spec;

	cx_auto_parse_beep(codec);
	cx_auto_parse_eapd(codec);
	spec->gen.own_eapd_ctl = 1;
	if (spec->dynamic_eapd)
		spec->gen.vmaster_mute.hook = cx_auto_vmaster_hook;

	switch (codec->vendor_id) {
	case 0x14f15045:
		codec->single_adc_amp = 1;
		break;
	case 0x14f15047:
		codec->pin_amp_workaround = 1;
		spec->gen.mixer_nid = 0x19;
		break;
	case 0x14f15051:
		add_cx5051_fake_mutes(codec);
		codec->pin_amp_workaround = 1;
		snd_hda_pick_fixup(codec, NULL, cxt5051_fixups, cxt_fixups);
		break;
	default:
		codec->pin_amp_workaround = 1;
		snd_hda_pick_fixup(codec, NULL, cxt5066_fixups, cxt_fixups);
		break;
	}

	/* Show mute-led control only on HP laptops
	 * This is a sort of white-list: on HP laptops, EAPD corresponds
	 * only to the mute-LED without actualy amp function.  Meanwhile,
	 * others may use EAPD really as an amp switch, so it might be
	 * not good to expose it blindly.
	 */
	switch (codec->subsystem_id >> 16) {
	case 0x103c:
		spec->gen.vmaster_mute_enum = 1;
		break;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = snd_hda_parse_pin_defcfg(codec, &spec->gen.autocfg, NULL,
				       spec->parse_flags);
	if (err < 0)
		goto error;

	err = snd_hda_gen_parse_auto_config(codec, &spec->gen.autocfg);
	if (err < 0)
		goto error;

	codec->patch_ops = cx_auto_patch_ops;

	/* Some laptops with Conexant chips show stalls in S3 resume,
	 * which falls into the single-cmd mode.
	 * Better to make reset, then.
	 */
	if (!codec->bus->sync_write) {
		snd_printd("hda_codec: "
			   "Enable sync_write for stable communication\n");
		codec->bus->sync_write = 1;
		codec->bus->allow_bus_reset = 1;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	cx_auto_free(codec);
	return err;
}

#ifndef ENABLE_CXT_STATIC_QUIRKS
#define patch_cxt5045	patch_conexant_auto
#define patch_cxt5047	patch_conexant_auto
#define patch_cxt5051	patch_conexant_auto
#define patch_cxt5066	patch_conexant_auto
#endif

/*
 */

static const struct hda_codec_preset snd_hda_preset_conexant[] = {
	{ .id = 0x14f15045, .name = "CX20549 (Venice)",
	  .patch = patch_cxt5045 },
	{ .id = 0x14f15047, .name = "CX20551 (Waikiki)",
	  .patch = patch_cxt5047 },
	{ .id = 0x14f15051, .name = "CX20561 (Hermosa)",
	  .patch = patch_cxt5051 },
	{ .id = 0x14f15066, .name = "CX20582 (Pebble)",
	  .patch = patch_cxt5066 },
	{ .id = 0x14f15067, .name = "CX20583 (Pebble HSF)",
	  .patch = patch_cxt5066 },
	{ .id = 0x14f15068, .name = "CX20584",
	  .patch = patch_cxt5066 },
	{ .id = 0x14f15069, .name = "CX20585",
	  .patch = patch_cxt5066 },
	{ .id = 0x14f1506c, .name = "CX20588",
	  .patch = patch_cxt5066 },
	{ .id = 0x14f1506e, .name = "CX20590",
	  .patch = patch_cxt5066 },
	{ .id = 0x14f15097, .name = "CX20631",
	  .patch = patch_conexant_auto },
	{ .id = 0x14f15098, .name = "CX20632",
	  .patch = patch_conexant_auto },
	{ .id = 0x14f150a1, .name = "CX20641",
	  .patch = patch_conexant_auto },
	{ .id = 0x14f150a2, .name = "CX20642",
	  .patch = patch_conexant_auto },
	{ .id = 0x14f150ab, .name = "CX20651",
	  .patch = patch_conexant_auto },
	{ .id = 0x14f150ac, .name = "CX20652",
	  .patch = patch_conexant_auto },
	{ .id = 0x14f150b8, .name = "CX20664",
	  .patch = patch_conexant_auto },
	{ .id = 0x14f150b9, .name = "CX20665",
	  .patch = patch_conexant_auto },
	{ .id = 0x14f1510f, .name = "CX20751/2",
	  .patch = patch_conexant_auto },
	{ .id = 0x14f15110, .name = "CX20751/2",
	  .patch = patch_conexant_auto },
	{ .id = 0x14f15111, .name = "CX20753/4",
	  .patch = patch_conexant_auto },
	{ .id = 0x14f15113, .name = "CX20755",
	  .patch = patch_conexant_auto },
	{ .id = 0x14f15114, .name = "CX20756",
	  .patch = patch_conexant_auto },
	{ .id = 0x14f15115, .name = "CX20757",
	  .patch = patch_conexant_auto },
	{ .id = 0x14f151d7, .name = "CX20952",
	  .patch = patch_conexant_auto },
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:14f15045");
MODULE_ALIAS("snd-hda-codec-id:14f15047");
MODULE_ALIAS("snd-hda-codec-id:14f15051");
MODULE_ALIAS("snd-hda-codec-id:14f15066");
MODULE_ALIAS("snd-hda-codec-id:14f15067");
MODULE_ALIAS("snd-hda-codec-id:14f15068");
MODULE_ALIAS("snd-hda-codec-id:14f15069");
MODULE_ALIAS("snd-hda-codec-id:14f1506c");
MODULE_ALIAS("snd-hda-codec-id:14f1506e");
MODULE_ALIAS("snd-hda-codec-id:14f15097");
MODULE_ALIAS("snd-hda-codec-id:14f15098");
MODULE_ALIAS("snd-hda-codec-id:14f150a1");
MODULE_ALIAS("snd-hda-codec-id:14f150a2");
MODULE_ALIAS("snd-hda-codec-id:14f150ab");
MODULE_ALIAS("snd-hda-codec-id:14f150ac");
MODULE_ALIAS("snd-hda-codec-id:14f150b8");
MODULE_ALIAS("snd-hda-codec-id:14f150b9");
MODULE_ALIAS("snd-hda-codec-id:14f1510f");
MODULE_ALIAS("snd-hda-codec-id:14f15110");
MODULE_ALIAS("snd-hda-codec-id:14f15111");
MODULE_ALIAS("snd-hda-codec-id:14f15113");
MODULE_ALIAS("snd-hda-codec-id:14f15114");
MODULE_ALIAS("snd-hda-codec-id:14f15115");
MODULE_ALIAS("snd-hda-codec-id:14f151d7");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Conexant HD-audio codec");

static struct hda_codec_preset_list conexant_list = {
	.preset = snd_hda_preset_conexant,
	.owner = THIS_MODULE,
};

static int __init patch_conexant_init(void)
{
	return snd_hda_add_codec_preset(&conexant_list);
}

static void __exit patch_conexant_exit(void)
{
	snd_hda_delete_codec_preset(&conexant_list);
}

module_init(patch_conexant_init)
module_exit(patch_conexant_exit)
