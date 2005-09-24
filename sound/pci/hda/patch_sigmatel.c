/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for SigmaTel STAC92xx
 *
 * Copyright (c) 2005 Embedded Alley Solutions, Inc.
 * <matt@embeddedalley.com>
 *
 * Based on patch_cmedia.c and patch_realtek.c
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
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

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/asoundef.h>
#include "hda_codec.h"
#include "hda_local.h"

#undef STAC_TEST

#define NUM_CONTROL_ALLOC	32
#define STAC_HP_EVENT		0x37
#define STAC_UNSOL_ENABLE 	(AC_USRSP_EN | STAC_HP_EVENT)

struct sigmatel_spec {
	snd_kcontrol_new_t *mixers[4];
	unsigned int num_mixers;

	unsigned int surr_switch: 1;

	/* playback */
	struct hda_multi_out multiout;
	hda_nid_t dac_nids[4];

	/* capture */
	hda_nid_t *adc_nids;
	unsigned int num_adcs;
	hda_nid_t *mux_nids;
	unsigned int num_muxes;
	hda_nid_t dig_in_nid;

#ifdef STAC_TEST
	/* pin widgets */
	hda_nid_t *pin_nids;
	unsigned int num_pins;
	unsigned int *pin_configs;
#endif

	/* codec specific stuff */
	struct hda_verb *init;
	snd_kcontrol_new_t *mixer;

	/* capture source */
	struct hda_input_mux *input_mux;
	unsigned int cur_mux[2];

	/* channel mode */
	unsigned int num_ch_modes;
	unsigned int cur_ch_mode;

	struct hda_pcm pcm_rec[2];	/* PCM information */

	/* dynamic controls and input_mux */
	struct auto_pin_cfg autocfg;
	unsigned int num_kctl_alloc, num_kctl_used;
	snd_kcontrol_new_t *kctl_alloc;
	struct hda_input_mux private_imux;
};

static hda_nid_t stac9200_adc_nids[1] = {
        0x03,
};

static hda_nid_t stac9200_mux_nids[1] = {
        0x0c,
};

static hda_nid_t stac9200_dac_nids[1] = {
        0x02,
};

static hda_nid_t stac922x_adc_nids[2] = {
        0x06, 0x07,
};

static hda_nid_t stac922x_mux_nids[2] = {
        0x12, 0x13,
};

#ifdef STAC_TEST
static hda_nid_t stac9200_pin_nids[8] = {
	0x08, 0x09, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
};

static hda_nid_t stac922x_pin_nids[10] = {
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
	0x0f, 0x10, 0x11, 0x15, 0x1b,
};
#endif

static int stac92xx_mux_enum_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_input_mux_info(spec->input_mux, uinfo);
}

static int stac92xx_mux_enum_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int stac92xx_mux_enum_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
				     spec->mux_nids[adc_idx], &spec->cur_mux[adc_idx]);
}

static struct hda_verb stac9200_core_init[] = {
	/* set dac0mux for dac converter */
	{ 0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{}
};

static struct hda_verb stac922x_core_init[] = {
	/* set master volume and direct control */	
	{ 0x16, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	{}
};

static int stac922x_channel_modes[3] = {2, 6, 8};

static int stac922x_ch_mode_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = spec->num_ch_modes;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	sprintf(uinfo->value.enumerated.name, "%dch",
		stac922x_channel_modes[uinfo->value.enumerated.item]);
	return 0;
}

static int stac922x_ch_mode_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->cur_ch_mode;
	return 0;
}

static int stac922x_ch_mode_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;

	if (ucontrol->value.enumerated.item[0] >= spec->num_ch_modes)
		ucontrol->value.enumerated.item[0] = spec->num_ch_modes;
	if (ucontrol->value.enumerated.item[0] == spec->cur_ch_mode &&
	    ! codec->in_resume)
		return 0;

	spec->cur_ch_mode = ucontrol->value.enumerated.item[0];
	spec->multiout.max_channels = stac922x_channel_modes[spec->cur_ch_mode];

	return 1;
}

static snd_kcontrol_new_t stac9200_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0xb, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Master Playback Switch", 0xb, 0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Input Source",
		.count = 1,
		.info = stac92xx_mux_enum_info,
		.get = stac92xx_mux_enum_get,
		.put = stac92xx_mux_enum_put,
	},
	HDA_CODEC_VOLUME("Capture Volume", 0x0a, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x0a, 0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Mux Volume", 0x0c, 0, HDA_OUTPUT),
	{ } /* end */
};

/* This needs to be generated dynamically based on sequence */
static snd_kcontrol_new_t stac922x_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Input Source",
		.count = 1,
		.info = stac92xx_mux_enum_info,
		.get = stac92xx_mux_enum_get,
		.put = stac92xx_mux_enum_put,
	},
	HDA_CODEC_VOLUME("Capture Volume", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mux Capture Volume", 0x12, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static snd_kcontrol_new_t stac922x_ch_mode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = stac922x_ch_mode_info,
		.get = stac922x_ch_mode_get,
		.put = stac922x_ch_mode_put,
	},
	{ } /* end */
};

static int stac92xx_build_controls(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int err;
	int i;

	err = snd_hda_add_new_ctls(codec, spec->mixer);
	if (err < 0)
		return err;

	for (i = 0; i < spec->num_mixers; i++) {
		err = snd_hda_add_new_ctls(codec, spec->mixers[i]);
		if (err < 0)
			return err;
	}

	if (spec->surr_switch) {
		err = snd_hda_add_new_ctls(codec, stac922x_ch_mode_mixer);
		if (err < 0)
			return err;
	}
	if (spec->multiout.dig_out_nid) {
		err = snd_hda_create_spdif_out_ctls(codec, spec->multiout.dig_out_nid);
		if (err < 0)
			return err;
	}
	if (spec->dig_in_nid) {
		err = snd_hda_create_spdif_in_ctls(codec, spec->dig_in_nid);
		if (err < 0)
			return err;
	}
	return 0;	
}

#ifdef STAC_TEST
static unsigned int stac9200_pin_configs[8] = {
	0x01c47010, 0x01447010, 0x0221401f, 0x01114010,
	0x02a19020, 0x01a19021, 0x90100140, 0x01813122,
};

static unsigned int stac922x_pin_configs[10] = {
	0x01014010, 0x01014011, 0x01014012, 0x0221401f,
	0x01813122, 0x01014014, 0x01441030, 0x01c41030,
	0x40000100, 0x40000100,
};

static void stac92xx_set_config_regs(struct hda_codec *codec)
{
	int i;
	struct sigmatel_spec *spec = codec->spec;
	unsigned int pin_cfg;

	for (i=0; i < spec->num_pins; i++) {
		snd_hda_codec_write(codec, spec->pin_nids[i], 0,
				    AC_VERB_SET_CONFIG_DEFAULT_BYTES_0,
				    spec->pin_configs[i] & 0x000000ff);
		snd_hda_codec_write(codec, spec->pin_nids[i], 0,
				    AC_VERB_SET_CONFIG_DEFAULT_BYTES_1,
				    (spec->pin_configs[i] & 0x0000ff00) >> 8);
		snd_hda_codec_write(codec, spec->pin_nids[i], 0,
				    AC_VERB_SET_CONFIG_DEFAULT_BYTES_2,
				    (spec->pin_configs[i] & 0x00ff0000) >> 16);
		snd_hda_codec_write(codec, spec->pin_nids[i], 0,
				    AC_VERB_SET_CONFIG_DEFAULT_BYTES_3,
				    spec->pin_configs[i] >> 24);
		pin_cfg = snd_hda_codec_read(codec, spec->pin_nids[i], 0,
					     AC_VERB_GET_CONFIG_DEFAULT,
					     0x00);	
		printk("pin nid %2.2x pin config %8.8x\n", spec->pin_nids[i], pin_cfg);
	}
}
#endif

/*
 * Analog playback callbacks
 */
static int stac92xx_playback_pcm_open(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      snd_pcm_substream_t *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream);
}

/*
 * set up the i/o for analog out
 * when the digital out is available, copy the front out to digital out, too.
 */
static int stac92xx_multi_out_analog_prepare(struct hda_codec *codec, struct hda_multi_out *mout,
				     unsigned int stream_tag,
				     unsigned int format,
				     snd_pcm_substream_t *substream)
{
	hda_nid_t *nids = mout->dac_nids;
	int chs = substream->runtime->channels;
	int i;

	down(&codec->spdif_mutex);
	if (mout->dig_out_nid && mout->dig_out_used != HDA_DIG_EXCLUSIVE) {
		if (chs == 2 &&
		    snd_hda_is_supported_format(codec, mout->dig_out_nid, format) &&
		    ! (codec->spdif_status & IEC958_AES0_NONAUDIO)) {
			mout->dig_out_used = HDA_DIG_ANALOG_DUP;
			/* setup digital receiver */
			snd_hda_codec_setup_stream(codec, mout->dig_out_nid,
						   stream_tag, 0, format);
		} else {
			mout->dig_out_used = 0;
			snd_hda_codec_setup_stream(codec, mout->dig_out_nid, 0, 0, 0);
		}
	}
	up(&codec->spdif_mutex);

	/* front */
	snd_hda_codec_setup_stream(codec, nids[HDA_FRONT], stream_tag, 0, format);
	if (mout->hp_nid)
		/* headphone out will just decode front left/right (stereo) */
		snd_hda_codec_setup_stream(codec, mout->hp_nid, stream_tag, 0, format);
	/* surrounds */
	if (mout->max_channels > 2)
		for (i = 1; i < mout->num_dacs; i++) {
			if ((mout->max_channels == 6) && (i == 3))
				break;
			if (chs >= (i + 1) * 2) /* independent out */
				snd_hda_codec_setup_stream(codec, nids[i], stream_tag, i * 2,
						format);
			else /* copy front */
				snd_hda_codec_setup_stream(codec, nids[i], stream_tag, 0,
						format);
		}
	return 0;
}


static int stac92xx_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 unsigned int stream_tag,
					 unsigned int format,
					 snd_pcm_substream_t *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return stac92xx_multi_out_analog_prepare(codec, &spec->multiout, stream_tag,
						format, substream);
}

static int stac92xx_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					snd_pcm_substream_t *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital playback callbacks
 */
static int stac92xx_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					  struct hda_codec *codec,
					  snd_pcm_substream_t *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int stac92xx_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   snd_pcm_substream_t *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}


/*
 * Analog capture callbacks
 */
static int stac92xx_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					unsigned int stream_tag,
					unsigned int format,
					snd_pcm_substream_t *substream)
{
	struct sigmatel_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
                                   stream_tag, 0, format);
	return 0;
}

static int stac92xx_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					snd_pcm_substream_t *substream)
{
	struct sigmatel_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number], 0, 0, 0);
	return 0;
}

static struct hda_pcm_stream stac92xx_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in stac92xx_build_pcms */
	.ops = {
		.open = stac92xx_dig_playback_pcm_open,
		.close = stac92xx_dig_playback_pcm_close
	},
};

static struct hda_pcm_stream stac92xx_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in stac92xx_build_pcms */
};

static struct hda_pcm_stream stac92xx_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.nid = 0x02, /* NID to query formats and rates */
	.ops = {
		.open = stac92xx_playback_pcm_open,
		.prepare = stac92xx_playback_pcm_prepare,
		.cleanup = stac92xx_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream stac92xx_pcm_analog_capture = {
	.substreams = 2,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x06, /* NID to query formats and rates */
	.ops = {
		.prepare = stac92xx_capture_pcm_prepare,
		.cleanup = stac92xx_capture_pcm_cleanup
	},
};

static int stac92xx_build_pcms(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "STAC92xx Analog";
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = stac92xx_pcm_analog_playback;
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = stac92xx_pcm_analog_capture;

	if (spec->multiout.dig_out_nid || spec->dig_in_nid) {
		codec->num_pcms++;
		info++;
		info->name = "STAC92xx Digital";
		if (spec->multiout.dig_out_nid) {
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] = stac92xx_pcm_digital_playback;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dig_out_nid;
		}
		if (spec->dig_in_nid) {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] = stac92xx_pcm_digital_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->dig_in_nid;
		}
	}

	return 0;
}

enum {
	STAC_CTL_WIDGET_VOL,
	STAC_CTL_WIDGET_MUTE,
};

static snd_kcontrol_new_t stac92xx_control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	HDA_CODEC_MUTE(NULL, 0, 0, 0),
};

/* add dynamic controls */
static int stac92xx_add_control(struct sigmatel_spec *spec, int type, const char *name, unsigned long val)
{
	snd_kcontrol_new_t *knew;

	if (spec->num_kctl_used >= spec->num_kctl_alloc) {
		int num = spec->num_kctl_alloc + NUM_CONTROL_ALLOC;

		knew = kcalloc(num + 1, sizeof(*knew), GFP_KERNEL); /* array + terminator */
		if (! knew)
			return -ENOMEM;
		if (spec->kctl_alloc) {
			memcpy(knew, spec->kctl_alloc, sizeof(*knew) * spec->num_kctl_alloc);
			kfree(spec->kctl_alloc);
		}
		spec->kctl_alloc = knew;
		spec->num_kctl_alloc = num;
	}

	knew = &spec->kctl_alloc[spec->num_kctl_used];
	*knew = stac92xx_control_templates[type];
	knew->name = kstrdup(name, GFP_KERNEL);
	if (! knew->name)
		return -ENOMEM;
	knew->private_value = val;
	spec->num_kctl_used++;
	return 0;
}

/* fill in the dac_nids table from the parsed pin configuration */
static int stac92xx_auto_fill_dac_nids(struct hda_codec *codec, const struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t nid;
	int i;

	/* check the pins hardwired to audio widget */
	for (i = 0; i < cfg->line_outs; i++) {
		nid = cfg->line_out_pins[i];
		spec->multiout.dac_nids[i] = snd_hda_codec_read(codec, nid, 0,
					AC_VERB_GET_CONNECT_LIST, 0) & 0xff;
	}

	spec->multiout.num_dacs = cfg->line_outs;

	return 0;
}

/* add playback controls from the parsed DAC table */
static int stac92xx_auto_create_multi_out_ctls(struct sigmatel_spec *spec, const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = { "Front", "Surround", NULL /*CLFE*/, "Side" };
	hda_nid_t nid;
	int i, err;

	for (i = 0; i < cfg->line_outs; i++) {
		if (! spec->multiout.dac_nids[i])
			continue;

		nid = spec->multiout.dac_nids[i];

		if (i == 2) {
			/* Center/LFE */
			if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_VOL, "Center Playback Volume",
					       HDA_COMPOSE_AMP_VAL(nid, 1, 0, HDA_OUTPUT))) < 0)
				return err;
			if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_VOL, "LFE Playback Volume",
					       HDA_COMPOSE_AMP_VAL(nid, 2, 0, HDA_OUTPUT))) < 0)
				return err;
			if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_MUTE, "Center Playback Switch",
					       HDA_COMPOSE_AMP_VAL(nid, 1, 0, HDA_OUTPUT))) < 0)
				return err;
			if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_MUTE, "LFE Playback Switch",
					       HDA_COMPOSE_AMP_VAL(nid, 2, 0, HDA_OUTPUT))) < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_VOL, name,
					       HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT))) < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_MUTE, name,
					       HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT))) < 0)
				return err;
		}
	}

	return 0;
}

/* add playback controls for HP output */
static int stac92xx_auto_create_hp_ctls(struct hda_codec *codec, struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t pin = cfg->hp_pin;
	hda_nid_t nid;
	int i, err;
	unsigned int wid_caps;

	if (! pin)
		return 0;

	wid_caps = snd_hda_param_read(codec, pin, AC_PAR_AUDIO_WIDGET_CAP);
	if (wid_caps & AC_WCAP_UNSOL_CAP)
		/* Enable unsolicited responses on the HP widget */
		snd_hda_codec_write(codec, pin, 0,
				AC_VERB_SET_UNSOLICITED_ENABLE,
				STAC_UNSOL_ENABLE);

	nid = snd_hda_codec_read(codec, pin, 0, AC_VERB_GET_CONNECT_LIST, 0) & 0xff;
	for (i = 0; i < cfg->line_outs; i++) {
		if (! spec->multiout.dac_nids[i])
			continue;
		if (spec->multiout.dac_nids[i] == nid)
			return 0;
	}

	spec->multiout.hp_nid = nid;

	/* control HP volume/switch on the output mixer amp */
	if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_VOL, "Headphone Playback Volume",
					HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT))) < 0)
		return err;
	if ((err = stac92xx_add_control(spec, STAC_CTL_WIDGET_MUTE, "Headphone Playback Switch",
					HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT))) < 0)
		return err;

	return 0;
}

/* create playback/capture controls for input pins */
static int stac92xx_auto_create_analog_input_ctls(struct hda_codec *codec, const struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	static char *labels[AUTO_PIN_LAST] = {
		"Mic", "Front Mic", "Line", "Front Line", "CD", "Aux"
	};
	struct hda_input_mux *imux = &spec->private_imux;
	hda_nid_t con_lst[HDA_MAX_NUM_INPUTS];
	int i, j, k;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		int index = -1;
		if (cfg->input_pins[i]) {
			imux->items[imux->num_items].label = labels[i];

			for (j=0; j<spec->num_muxes; j++) {
				int num_cons = snd_hda_get_connections(codec, spec->mux_nids[j], con_lst, HDA_MAX_NUM_INPUTS);
				for (k=0; k<num_cons; k++)
					if (con_lst[k] == cfg->input_pins[i]) {
						index = k;
					 	break;
					}
				if (index >= 0)
					break;
			}
			imux->items[imux->num_items].index = index;
			imux->num_items++;
		}
	}

	return 0;
}

static void stac92xx_auto_set_pinctl(struct hda_codec *codec, hda_nid_t nid, int pin_type)

{
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, pin_type);
}

static void stac92xx_auto_init_multi_out(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->autocfg.line_outs; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		stac92xx_auto_set_pinctl(codec, nid, AC_PINCTL_OUT_EN);
	}
}

static void stac92xx_auto_init_hp_out(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pin;
	if (pin) /* connect to front */
		stac92xx_auto_set_pinctl(codec, pin, AC_PINCTL_OUT_EN | AC_PINCTL_HP_EN);
}

static int stac922x_parse_auto_config(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int err;

	if ((err = snd_hda_parse_pin_def_config(codec, &spec->autocfg)) < 0)
		return err;
	if ((err = stac92xx_auto_fill_dac_nids(codec, &spec->autocfg)) < 0)
		return err;
	if (! spec->autocfg.line_outs && ! spec->autocfg.hp_pin)
		return 0; /* can't find valid pin config */

	if ((err = stac92xx_auto_create_multi_out_ctls(spec, &spec->autocfg)) < 0 ||
	    (err = stac92xx_auto_create_hp_ctls(codec, &spec->autocfg)) < 0 ||
	    (err = stac92xx_auto_create_analog_input_ctls(codec, &spec->autocfg)) < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;
	if (spec->multiout.max_channels > 2) {
		spec->surr_switch = 1;
		spec->cur_ch_mode = 1;
		spec->num_ch_modes = 2;
		if (spec->multiout.max_channels == 8) {
			spec->cur_ch_mode++;
			spec->num_ch_modes++;
		}
	}

	if (spec->autocfg.dig_out_pin) {
		spec->multiout.dig_out_nid = 0x08;
		stac92xx_auto_set_pinctl(codec, spec->autocfg.dig_out_pin, AC_PINCTL_OUT_EN);
	}
	if (spec->autocfg.dig_in_pin) {
		spec->dig_in_nid = 0x09;
		stac92xx_auto_set_pinctl(codec, spec->autocfg.dig_in_pin, AC_PINCTL_IN_EN);
	}

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->input_mux = &spec->private_imux;

	return 1;
}

static int stac9200_parse_auto_config(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int err;

	if ((err = snd_hda_parse_pin_def_config(codec, &spec->autocfg)) < 0)
		return err;

	if ((err = stac92xx_auto_create_analog_input_ctls(codec, &spec->autocfg)) < 0)
		return err;

	if (spec->autocfg.dig_out_pin) {
		spec->multiout.dig_out_nid = 0x05;
		stac92xx_auto_set_pinctl(codec, spec->autocfg.dig_out_pin, AC_PINCTL_OUT_EN);
	}
	if (spec->autocfg.dig_in_pin) {
		spec->dig_in_nid = 0x04;
		stac92xx_auto_set_pinctl(codec, spec->autocfg.dig_in_pin, AC_PINCTL_IN_EN);
	}

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->input_mux = &spec->private_imux;

	return 1;
}

static int stac92xx_init_pstate(struct hda_codec *codec)
{
       hda_nid_t nid, nid_start;
       int nodes;

	snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_POWER_STATE, 0x00);

       nodes = snd_hda_get_sub_nodes(codec, codec->afg, &nid_start);
       for (nid = nid_start; nid < nodes + nid_start; nid++) {
               unsigned int wid_caps = snd_hda_param_read(codec, nid,
                                                  AC_PAR_AUDIO_WIDGET_CAP);
		if (wid_caps & AC_WCAP_POWER)
			snd_hda_codec_write(codec, nid, 0,
                                    AC_VERB_SET_POWER_STATE, 0x00);
	}

	mdelay(100);

	return 0;
}

static int stac92xx_init(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	stac92xx_init_pstate(codec);

	snd_hda_sequence_write(codec, spec->init);

	stac92xx_auto_init_multi_out(codec);
	stac92xx_auto_init_hp_out(codec);

	return 0;
}

static void stac92xx_free(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int i;

	if (! spec)
		return;

	if (spec->kctl_alloc) {
		for (i = 0; i < spec->num_kctl_used; i++)
			kfree(spec->kctl_alloc[i].name);
		kfree(spec->kctl_alloc);
	}

	kfree(spec);
}

static void stac92xx_set_pinctl(struct hda_codec *codec, hda_nid_t nid,
				unsigned int flag)
{
	unsigned int pin_ctl = snd_hda_codec_read(codec, nid,
			0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00);
	snd_hda_codec_write(codec, nid, 0,
			AC_VERB_SET_PIN_WIDGET_CONTROL,
			pin_ctl | flag);
}

static void stac92xx_reset_pinctl(struct hda_codec *codec, hda_nid_t nid,
				  unsigned int flag)
{
	unsigned int pin_ctl = snd_hda_codec_read(codec, nid,
			0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00);
	snd_hda_codec_write(codec, nid, 0,
			AC_VERB_SET_PIN_WIDGET_CONTROL,
			pin_ctl & ~flag);
}

static void stac92xx_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct sigmatel_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i, presence;

	if ((res >> 26) != STAC_HP_EVENT)
		return;

	presence = snd_hda_codec_read(codec, cfg->hp_pin, 0,
			AC_VERB_GET_PIN_SENSE, 0x00) >> 31;

	if (presence) {
		/* disable lineouts, enable hp */
		for (i = 0; i < cfg->line_outs; i++)
			stac92xx_reset_pinctl(codec, cfg->line_out_pins[i],
						AC_PINCTL_OUT_EN);
		stac92xx_set_pinctl(codec, cfg->hp_pin, AC_PINCTL_OUT_EN);
	} else {
		/* enable lineouts, disable hp */
		for (i = 0; i < cfg->line_outs; i++)
			stac92xx_set_pinctl(codec, cfg->line_out_pins[i],
						AC_PINCTL_OUT_EN);
		stac92xx_reset_pinctl(codec, cfg->hp_pin, AC_PINCTL_OUT_EN);
	}
} 

#ifdef CONFIG_PM
static int stac92xx_resume(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int i;

	stac92xx_init(codec);
	for (i = 0; i < spec->num_mixers; i++)
		snd_hda_resume_ctls(codec, spec->mixers[i]);
	if (spec->multiout.dig_out_nid)
		snd_hda_resume_spdif_out(codec);
	if (spec->dig_in_nid)
		snd_hda_resume_spdif_in(codec);

	return 0;
}
#endif

static struct hda_codec_ops stac92xx_patch_ops = {
	.build_controls = stac92xx_build_controls,
	.build_pcms = stac92xx_build_pcms,
	.init = stac92xx_init,
	.free = stac92xx_free,
	.unsol_event = stac92xx_unsol_event,
#ifdef CONFIG_PM
	.resume = stac92xx_resume,
#endif
};

static int patch_stac9200(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

#ifdef STAC_TEST
	spec->pin_nids = stac9200_pin_nids;
	spec->num_pins = 8;
	spec->pin_configs = stac9200_pin_configs;
	stac92xx_set_config_regs(codec);
#endif
	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = 1;
	spec->multiout.dac_nids = stac9200_dac_nids;
	spec->adc_nids = stac9200_adc_nids;
	spec->mux_nids = stac9200_mux_nids;
	spec->num_muxes = 1;

	spec->init = stac9200_core_init;
	spec->mixer = stac9200_mixer;

	err = stac9200_parse_auto_config(codec);
	if (err < 0) {
		stac92xx_free(codec);
		return err;
	}

	codec->patch_ops = stac92xx_patch_ops;

	return 0;
}

static int patch_stac922x(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

#ifdef STAC_TEST
	spec->num_pins = 10;
	spec->pin_nids = stac922x_pin_nids;
	spec->pin_configs = stac922x_pin_configs;
	stac92xx_set_config_regs(codec);
#endif
	spec->adc_nids = stac922x_adc_nids;
	spec->mux_nids = stac922x_mux_nids;
	spec->num_muxes = 2;

	spec->init = stac922x_core_init;
	spec->mixer = stac922x_mixer;

	spec->multiout.dac_nids = spec->dac_nids;

	err = stac922x_parse_auto_config(codec);
	if (err < 0) {
		stac92xx_free(codec);
		return err;
	}

	codec->patch_ops = stac92xx_patch_ops;

	return 0;
}

/*
 * patch entries
 */
struct hda_codec_preset snd_hda_preset_sigmatel[] = {
 	{ .id = 0x83847690, .name = "STAC9200", .patch = patch_stac9200 },
 	{ .id = 0x83847882, .name = "STAC9220 A1", .patch = patch_stac922x },
 	{ .id = 0x83847680, .name = "STAC9221 A1", .patch = patch_stac922x },
 	{ .id = 0x83847880, .name = "STAC9220 A2", .patch = patch_stac922x },
 	{ .id = 0x83847681, .name = "STAC9220D/9223D A2", .patch = patch_stac922x },
 	{ .id = 0x83847682, .name = "STAC9221 A2", .patch = patch_stac922x },
 	{ .id = 0x83847683, .name = "STAC9221D A2", .patch = patch_stac922x },
	{} /* terminator */
};
