/*
 * HD audio interface patch for AD1882, AD1884, AD1981HD, AD1983, AD1984,
 *   AD1986A, AD1988
 *
 * Copyright (c) 2005-2007 Takashi Iwai <tiwai@suse.de>
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
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/module.h>

#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_beep.h"
#include "hda_jack.h"
#include "hda_generic.h"

#define ENABLE_AD_STATIC_QUIRKS

struct ad198x_spec {
	struct hda_gen_spec gen;

	/* for auto parser */
	int smux_paths[4];
	unsigned int cur_smux;
	hda_nid_t eapd_nid;

	unsigned int beep_amp;	/* beep amp value, set via set_beep_amp() */

#ifdef ENABLE_AD_STATIC_QUIRKS
	const struct snd_kcontrol_new *mixers[6];
	int num_mixers;
	const struct hda_verb *init_verbs[6];	/* initialization verbs
						 * don't forget NULL termination!
						 */
	unsigned int num_init_verbs;

	/* playback */
	struct hda_multi_out multiout;	/* playback set-up
					 * max_channels, dacs must be set
					 * dig_out_nid and hp_nid are optional
					 */
	unsigned int cur_eapd;
	unsigned int need_dac_fix;

	/* capture */
	unsigned int num_adc_nids;
	const hda_nid_t *adc_nids;
	hda_nid_t dig_in_nid;		/* digital-in NID; optional */

	/* capture source */
	const struct hda_input_mux *input_mux;
	const hda_nid_t *capsrc_nids;
	unsigned int cur_mux[3];

	/* channel model */
	const struct hda_channel_mode *channel_mode;
	int num_channel_mode;

	/* PCM information */
	struct hda_pcm pcm_rec[3];	/* used in alc_build_pcms() */

	unsigned int spdif_route;

	unsigned int jack_present: 1;
	unsigned int inv_jack_detect: 1;/* inverted jack-detection */
	unsigned int analog_beep: 1;	/* analog beep input present */
	unsigned int avoid_init_slave_vol:1;

#ifdef CONFIG_PM
	struct hda_loopback_check loopback;
#endif
	/* for virtual master */
	hda_nid_t vmaster_nid;
	const char * const *slave_vols;
	const char * const *slave_sws;
#endif /* ENABLE_AD_STATIC_QUIRKS */
};

#ifdef ENABLE_AD_STATIC_QUIRKS
/*
 * input MUX handling (common part)
 */
static int ad198x_mux_enum_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;

	return snd_hda_input_mux_info(spec->input_mux, uinfo);
}

static int ad198x_mux_enum_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int ad198x_mux_enum_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
				     spec->capsrc_nids[adc_idx],
				     &spec->cur_mux[adc_idx]);
}

/*
 * initialization (common callbacks)
 */
static int ad198x_init(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->num_init_verbs; i++)
		snd_hda_sequence_write(codec, spec->init_verbs[i]);
	return 0;
}

static const char * const ad_slave_pfxs[] = {
	"Front", "Surround", "Center", "LFE", "Side",
	"Headphone", "Mono", "Speaker", "IEC958",
	NULL
};

static const char * const ad1988_6stack_fp_slave_pfxs[] = {
	"Front", "Surround", "Center", "LFE", "Side", "IEC958",
	NULL
};
#endif /* ENABLE_AD_STATIC_QUIRKS */

#ifdef CONFIG_SND_HDA_INPUT_BEEP
/* additional beep mixers; the actual parameters are overwritten at build */
static const struct snd_kcontrol_new ad_beep_mixer[] = {
	HDA_CODEC_VOLUME("Beep Playback Volume", 0, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE_BEEP("Beep Playback Switch", 0, 0, HDA_OUTPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new ad_beep2_mixer[] = {
	HDA_CODEC_VOLUME("Digital Beep Playback Volume", 0, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE_BEEP("Digital Beep Playback Switch", 0, 0, HDA_OUTPUT),
	{ } /* end */
};

#define set_beep_amp(spec, nid, idx, dir) \
	((spec)->beep_amp = HDA_COMPOSE_AMP_VAL(nid, 1, idx, dir)) /* mono */
#else
#define set_beep_amp(spec, nid, idx, dir) /* NOP */
#endif

#ifdef CONFIG_SND_HDA_INPUT_BEEP
static int create_beep_ctls(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	const struct snd_kcontrol_new *knew;

	if (!spec->beep_amp)
		return 0;

	knew = spec->analog_beep ? ad_beep2_mixer : ad_beep_mixer;
	for ( ; knew->name; knew++) {
		int err;
		struct snd_kcontrol *kctl;
		kctl = snd_ctl_new1(knew, codec);
		if (!kctl)
			return -ENOMEM;
		kctl->private_value = spec->beep_amp;
		err = snd_hda_ctl_add(codec, 0, kctl);
		if (err < 0)
			return err;
	}
	return 0;
}
#else
#define create_beep_ctls(codec)		0
#endif

#ifdef ENABLE_AD_STATIC_QUIRKS
static int ad198x_build_controls(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	struct snd_kcontrol *kctl;
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
		err = snd_hda_create_spdif_in_ctls(codec, spec->dig_in_nid);
		if (err < 0)
			return err;
	}

	/* create beep controls if needed */
	err = create_beep_ctls(codec);
	if (err < 0)
		return err;

	/* if we have no master control, let's create it */
	if (!snd_hda_find_mixer_ctl(codec, "Master Playback Volume")) {
		unsigned int vmaster_tlv[4];
		snd_hda_set_vmaster_tlv(codec, spec->vmaster_nid,
					HDA_OUTPUT, vmaster_tlv);
		err = __snd_hda_add_vmaster(codec, "Master Playback Volume",
					  vmaster_tlv,
					  (spec->slave_vols ?
					   spec->slave_vols : ad_slave_pfxs),
					  "Playback Volume",
					  !spec->avoid_init_slave_vol, NULL);
		if (err < 0)
			return err;
	}
	if (!snd_hda_find_mixer_ctl(codec, "Master Playback Switch")) {
		err = snd_hda_add_vmaster(codec, "Master Playback Switch",
					  NULL,
					  (spec->slave_sws ?
					   spec->slave_sws : ad_slave_pfxs),
					  "Playback Switch");
		if (err < 0)
			return err;
	}

	/* assign Capture Source enums to NID */
	kctl = snd_hda_find_mixer_ctl(codec, "Capture Source");
	if (!kctl)
		kctl = snd_hda_find_mixer_ctl(codec, "Input Source");
	for (i = 0; kctl && i < kctl->count; i++) {
		err = snd_hda_add_nid(codec, kctl, i, spec->capsrc_nids[i]);
		if (err < 0)
			return err;
	}

	/* assign IEC958 enums to NID */
	kctl = snd_hda_find_mixer_ctl(codec,
			SNDRV_CTL_NAME_IEC958("",PLAYBACK,NONE) "Source");
	if (kctl) {
		err = snd_hda_add_nid(codec, kctl, 0,
				      spec->multiout.dig_out_nid);
		if (err < 0)
			return err;
	}

	return 0;
}

#ifdef CONFIG_PM
static int ad198x_check_power_status(struct hda_codec *codec, hda_nid_t nid)
{
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_check_amp_list_power(codec, &spec->loopback, nid);
}
#endif

/*
 * Analog playback callbacks
 */
static int ad198x_playback_pcm_open(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream,
					     hinfo);
}

static int ad198x_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       unsigned int stream_tag,
				       unsigned int format,
				       struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout, stream_tag,
						format, substream);
}

static int ad198x_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital out
 */
static int ad198x_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int ad198x_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int ad198x_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   unsigned int stream_tag,
					   unsigned int format,
					   struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout, stream_tag,
					     format, substream);
}

static int ad198x_dig_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_cleanup(codec, &spec->multiout);
}

/*
 * Analog capture
 */
static int ad198x_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
				   stream_tag, 0, format);
	return 0;
}

static int ad198x_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct ad198x_spec *spec = codec->spec;
	snd_hda_codec_cleanup_stream(codec, spec->adc_nids[substream->number]);
	return 0;
}

/*
 */
static const struct hda_pcm_stream ad198x_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 6, /* changed later */
	.nid = 0, /* fill later */
	.ops = {
		.open = ad198x_playback_pcm_open,
		.prepare = ad198x_playback_pcm_prepare,
		.cleanup = ad198x_playback_pcm_cleanup,
	},
};

static const struct hda_pcm_stream ad198x_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0, /* fill later */
	.ops = {
		.prepare = ad198x_capture_pcm_prepare,
		.cleanup = ad198x_capture_pcm_cleanup
	},
};

static const struct hda_pcm_stream ad198x_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0, /* fill later */
	.ops = {
		.open = ad198x_dig_playback_pcm_open,
		.close = ad198x_dig_playback_pcm_close,
		.prepare = ad198x_dig_playback_pcm_prepare,
		.cleanup = ad198x_dig_playback_pcm_cleanup
	},
};

static const struct hda_pcm_stream ad198x_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

static int ad198x_build_pcms(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "AD198x Analog";
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = ad198x_pcm_analog_playback;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max = spec->multiout.max_channels;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dac_nids[0];
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = ad198x_pcm_analog_capture;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams = spec->num_adc_nids;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adc_nids[0];

	if (spec->multiout.dig_out_nid) {
		info++;
		codec->num_pcms++;
		codec->spdif_status_reset = 1;
		info->name = "AD198x Digital";
		info->pcm_type = HDA_PCM_TYPE_SPDIF;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] = ad198x_pcm_digital_playback;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dig_out_nid;
		if (spec->dig_in_nid) {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] = ad198x_pcm_digital_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->dig_in_nid;
		}
	}

	return 0;
}
#endif /* ENABLE_AD_STATIC_QUIRKS */

static void ad198x_power_eapd_write(struct hda_codec *codec, hda_nid_t front,
				hda_nid_t hp)
{
	if (snd_hda_query_pin_caps(codec, front) & AC_PINCAP_EAPD)
		snd_hda_codec_write(codec, front, 0, AC_VERB_SET_EAPD_BTLENABLE,
			    !codec->inv_eapd ? 0x00 : 0x02);
	if (snd_hda_query_pin_caps(codec, hp) & AC_PINCAP_EAPD)
		snd_hda_codec_write(codec, hp, 0, AC_VERB_SET_EAPD_BTLENABLE,
			    !codec->inv_eapd ? 0x00 : 0x02);
}

static void ad198x_power_eapd(struct hda_codec *codec)
{
	/* We currently only handle front, HP */
	switch (codec->vendor_id) {
	case 0x11d41882:
	case 0x11d4882a:
	case 0x11d41884:
	case 0x11d41984:
	case 0x11d41883:
	case 0x11d4184a:
	case 0x11d4194a:
	case 0x11d4194b:
	case 0x11d41988:
	case 0x11d4198b:
	case 0x11d4989a:
	case 0x11d4989b:
		ad198x_power_eapd_write(codec, 0x12, 0x11);
		break;
	case 0x11d41981:
	case 0x11d41983:
		ad198x_power_eapd_write(codec, 0x05, 0x06);
		break;
	case 0x11d41986:
		ad198x_power_eapd_write(codec, 0x1b, 0x1a);
		break;
	}
}

static void ad198x_shutup(struct hda_codec *codec)
{
	snd_hda_shutup_pins(codec);
	ad198x_power_eapd(codec);
}

static void ad198x_free(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;

	if (!spec)
		return;

	snd_hda_gen_spec_free(&spec->gen);
	kfree(spec);
	snd_hda_detach_beep_device(codec);
}

#ifdef CONFIG_PM
static int ad198x_suspend(struct hda_codec *codec)
{
	ad198x_shutup(codec);
	return 0;
}
#endif

#ifdef ENABLE_AD_STATIC_QUIRKS
static const struct hda_codec_ops ad198x_patch_ops = {
	.build_controls = ad198x_build_controls,
	.build_pcms = ad198x_build_pcms,
	.init = ad198x_init,
	.free = ad198x_free,
#ifdef CONFIG_PM
	.check_power_status = ad198x_check_power_status,
	.suspend = ad198x_suspend,
#endif
	.reboot_notify = ad198x_shutup,
};


/*
 * EAPD control
 * the private value = nid
 */
#define ad198x_eapd_info	snd_ctl_boolean_mono_info

static int ad198x_eapd_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	if (codec->inv_eapd)
		ucontrol->value.integer.value[0] = ! spec->cur_eapd;
	else
		ucontrol->value.integer.value[0] = spec->cur_eapd;
	return 0;
}

static int ad198x_eapd_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	hda_nid_t nid = kcontrol->private_value & 0xff;
	unsigned int eapd;
	eapd = !!ucontrol->value.integer.value[0];
	if (codec->inv_eapd)
		eapd = !eapd;
	if (eapd == spec->cur_eapd)
		return 0;
	spec->cur_eapd = eapd;
	snd_hda_codec_write_cache(codec, nid,
				  0, AC_VERB_SET_EAPD_BTLENABLE,
				  eapd ? 0x02 : 0x00);
	return 1;
}

static int ad198x_ch_mode_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo);
static int ad198x_ch_mode_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol);
static int ad198x_ch_mode_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol);
#endif /* ENABLE_AD_STATIC_QUIRKS */


/*
 * Automatic parse of I/O pins from the BIOS configuration
 */

static int ad198x_auto_build_controls(struct hda_codec *codec)
{
	int err;

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;
	err = create_beep_ctls(codec);
	if (err < 0)
		return err;
	return 0;
}

static const struct hda_codec_ops ad198x_auto_patch_ops = {
	.build_controls = ad198x_auto_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = snd_hda_gen_init,
	.free = snd_hda_gen_free,
	.unsol_event = snd_hda_jack_unsol_event,
#ifdef CONFIG_PM
	.check_power_status = snd_hda_gen_check_power_status,
	.suspend = ad198x_suspend,
#endif
	.reboot_notify = ad198x_shutup,
};


static int ad198x_parse_auto_config(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->gen.autocfg;
	int err;

	codec->spdif_status_reset = 1;
	codec->no_trigger_sense = 1;
	codec->no_sticky_stream = 1;

	spec->gen.indep_hp = 1;

	err = snd_hda_parse_pin_defcfg(codec, cfg, NULL, 0);
	if (err < 0)
		return err;
	err = snd_hda_gen_parse_auto_config(codec, cfg);
	if (err < 0)
		return err;

	codec->patch_ops = ad198x_auto_patch_ops;

	return 0;
}

/*
 * AD1986A specific
 */

#ifdef ENABLE_AD_STATIC_QUIRKS
#define AD1986A_SPDIF_OUT	0x02
#define AD1986A_FRONT_DAC	0x03
#define AD1986A_SURR_DAC	0x04
#define AD1986A_CLFE_DAC	0x05
#define AD1986A_ADC		0x06

static const hda_nid_t ad1986a_dac_nids[3] = {
	AD1986A_FRONT_DAC, AD1986A_SURR_DAC, AD1986A_CLFE_DAC
};
static const hda_nid_t ad1986a_adc_nids[1] = { AD1986A_ADC };
static const hda_nid_t ad1986a_capsrc_nids[1] = { 0x12 };

static const struct hda_input_mux ad1986a_capture_source = {
	.num_items = 7,
	.items = {
		{ "Mic", 0x0 },
		{ "CD", 0x1 },
		{ "Aux", 0x3 },
		{ "Line", 0x4 },
		{ "Mix", 0x5 },
		{ "Mono", 0x6 },
		{ "Phone", 0x7 },
	},
};


static const struct hda_bind_ctls ad1986a_bind_pcm_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(AD1986A_FRONT_DAC, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(AD1986A_SURR_DAC, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(AD1986A_CLFE_DAC, 3, 0, HDA_OUTPUT),
		0
	},
};

static const struct hda_bind_ctls ad1986a_bind_pcm_sw = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(AD1986A_FRONT_DAC, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(AD1986A_SURR_DAC, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(AD1986A_CLFE_DAC, 3, 0, HDA_OUTPUT),
		0
	},
};

/*
 * mixers
 */
static const struct snd_kcontrol_new ad1986a_mixers[] = {
	/*
	 * bind volumes/mutes of 3 DACs as a single PCM control for simplicity
	 */
	HDA_BIND_VOL("PCM Playback Volume", &ad1986a_bind_pcm_vol),
	HDA_BIND_SW("PCM Playback Switch", &ad1986a_bind_pcm_sw),
	HDA_CODEC_VOLUME("Front Playback Volume", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x1d, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x1d, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x1d, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x1d, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x1a, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x17, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x17, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Aux Playback Volume", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Aux Playback Switch", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mono Playback Volume", 0x1e, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mono Playback Switch", 0x1e, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = ad198x_mux_enum_info,
		.get = ad198x_mux_enum_get,
		.put = ad198x_mux_enum_put,
	},
	HDA_CODEC_MUTE("Stereo Downmix Switch", 0x09, 0x0, HDA_OUTPUT),
	{ } /* end */
};

/* additional mixers for 3stack mode */
static const struct snd_kcontrol_new ad1986a_3st_mixers[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = ad198x_ch_mode_info,
		.get = ad198x_ch_mode_get,
		.put = ad198x_ch_mode_put,
	},
	{ } /* end */
};

/* laptop model - 2ch only */
static const hda_nid_t ad1986a_laptop_dac_nids[1] = { AD1986A_FRONT_DAC };

/* master controls both pins 0x1a and 0x1b */
static const struct hda_bind_ctls ad1986a_laptop_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x1a, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x1b, 3, 0, HDA_OUTPUT),
		0,
	},
};

static const struct hda_bind_ctls ad1986a_laptop_master_sw = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x1a, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x1b, 3, 0, HDA_OUTPUT),
		0,
	},
};

static const struct snd_kcontrol_new ad1986a_laptop_mixers[] = {
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_BIND_VOL("Master Playback Volume", &ad1986a_laptop_master_vol),
	HDA_BIND_SW("Master Playback Switch", &ad1986a_laptop_master_sw),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x17, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x17, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Aux Playback Volume", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Aux Playback Switch", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x0f, 0x0, HDA_OUTPUT),
	/* 
	   HDA_CODEC_VOLUME("Mono Playback Volume", 0x1e, 0x0, HDA_OUTPUT),
	   HDA_CODEC_MUTE("Mono Playback Switch", 0x1e, 0x0, HDA_OUTPUT), */
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = ad198x_mux_enum_info,
		.get = ad198x_mux_enum_get,
		.put = ad198x_mux_enum_put,
	},
	{ } /* end */
};

/* laptop-eapd model - 2ch only */

static const struct hda_input_mux ad1986a_laptop_eapd_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x4 },
		{ "Mix", 0x5 },
	},
};

static const struct hda_input_mux ad1986a_automic_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x0 },
		{ "Mix", 0x5 },
	},
};

static const struct snd_kcontrol_new ad1986a_laptop_master_mixers[] = {
	HDA_BIND_VOL("Master Playback Volume", &ad1986a_laptop_master_vol),
	HDA_BIND_SW("Master Playback Switch", &ad1986a_laptop_master_sw),
	{ } /* end */
};

static const struct snd_kcontrol_new ad1986a_laptop_eapd_mixers[] = {
	HDA_CODEC_VOLUME("PCM Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x13, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x12, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x12, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = ad198x_mux_enum_info,
		.get = ad198x_mux_enum_get,
		.put = ad198x_mux_enum_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "External Amplifier",
		.subdevice = HDA_SUBDEV_NID_FLAG | 0x1b,
		.info = ad198x_eapd_info,
		.get = ad198x_eapd_get,
		.put = ad198x_eapd_put,
		.private_value = 0x1b, /* port-D */
	},
	{ } /* end */
};

static const struct snd_kcontrol_new ad1986a_laptop_intmic_mixers[] = {
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x17, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x17, 0, HDA_OUTPUT),
	{ } /* end */
};

/* re-connect the mic boost input according to the jack sensing */
static void ad1986a_automic(struct hda_codec *codec)
{
	unsigned int present;
	present = snd_hda_jack_detect(codec, 0x1f);
	/* 0 = 0x1f, 2 = 0x1d, 4 = mixed */
	snd_hda_codec_write(codec, 0x0f, 0, AC_VERB_SET_CONNECT_SEL,
			    present ? 0 : 2);
}

#define AD1986A_MIC_EVENT		0x36

static void ad1986a_automic_unsol_event(struct hda_codec *codec,
					    unsigned int res)
{
	if ((res >> 26) != AD1986A_MIC_EVENT)
		return;
	ad1986a_automic(codec);
}

static int ad1986a_automic_init(struct hda_codec *codec)
{
	ad198x_init(codec);
	ad1986a_automic(codec);
	return 0;
}

/* laptop-automute - 2ch only */

static void ad1986a_update_hp(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	unsigned int mute;

	if (spec->jack_present)
		mute = HDA_AMP_MUTE; /* mute internal speaker */
	else
		/* unmute internal speaker if necessary */
		mute = snd_hda_codec_amp_read(codec, 0x1a, 0, HDA_OUTPUT, 0);
	snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, mute);
}

static void ad1986a_hp_automute(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;

	spec->jack_present = snd_hda_jack_detect(codec, 0x1a);
	if (spec->inv_jack_detect)
		spec->jack_present = !spec->jack_present;
	ad1986a_update_hp(codec);
}

#define AD1986A_HP_EVENT		0x37

static void ad1986a_hp_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if ((res >> 26) != AD1986A_HP_EVENT)
		return;
	ad1986a_hp_automute(codec);
}

static int ad1986a_hp_init(struct hda_codec *codec)
{
	ad198x_init(codec);
	ad1986a_hp_automute(codec);
	return 0;
}

/* bind hp and internal speaker mute (with plug check) */
static int ad1986a_hp_master_sw_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	int change = snd_hda_mixer_amp_switch_put(kcontrol, ucontrol);
	if (change)
		ad1986a_update_hp(codec);
	return change;
}

static const struct snd_kcontrol_new ad1986a_automute_master_mixers[] = {
	HDA_BIND_VOL("Master Playback Volume", &ad1986a_laptop_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.subdevice = HDA_SUBDEV_AMP_FLAG,
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = ad1986a_hp_master_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x1a, 3, 0, HDA_OUTPUT),
	},
	{ } /* end */
};


/*
 * initialization verbs
 */
static const struct hda_verb ad1986a_init_verbs[] = {
	/* Front, Surround, CLFE DAC; mute as default */
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* Downmix - off */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* HP, Line-Out, Surround, CLFE selectors */
	{0x0a, AC_VERB_SET_CONNECT_SEL, 0x0},
	{0x0b, AC_VERB_SET_CONNECT_SEL, 0x0},
	{0x0c, AC_VERB_SET_CONNECT_SEL, 0x0},
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Mono selector */
	{0x0e, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Mic selector: Mic 1/2 pin */
	{0x0f, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Line-in selector: Line-in */
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Mic 1/2 swap */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Record selector: mic */
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* Mic, Phone, CD, Aux, Line-In amp; mute as default */
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* PC beep */
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x0},
	/* HP, Line-Out, Surround, CLFE, Mono pins; mute as default */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	{0x1e, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080},
	/* HP Pin */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* Front, Surround, CLFE Pins */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* Mono Pin */
	{0x1e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* Mic Pin */
	{0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* Line, Aux, CD, Beep-In Pin */
	{0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	{0x22, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	{0x23, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	{0x24, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	{ } /* end */
};

static const struct hda_verb ad1986a_ch2_init[] = {
	/* Surround out -> Line In */
	{ 0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
 	/* Line-in selectors */
	{ 0x10, AC_VERB_SET_CONNECT_SEL, 0x1 },
	/* CLFE -> Mic in */
	{ 0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	/* Mic selector, mix C/LFE (backmic) and Mic (frontmic) */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x4 },
	{ } /* end */
};

static const struct hda_verb ad1986a_ch4_init[] = {
	/* Surround out -> Surround */
	{ 0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x10, AC_VERB_SET_CONNECT_SEL, 0x0 },
	/* CLFE -> Mic in */
	{ 0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x4 },
	{ } /* end */
};

static const struct hda_verb ad1986a_ch6_init[] = {
	/* Surround out -> Surround out */
	{ 0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x10, AC_VERB_SET_CONNECT_SEL, 0x0 },
	/* CLFE -> CLFE */
	{ 0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x0 },
	{ } /* end */
};

static const struct hda_channel_mode ad1986a_modes[3] = {
	{ 2, ad1986a_ch2_init },
	{ 4, ad1986a_ch4_init },
	{ 6, ad1986a_ch6_init },
};

/* eapd initialization */
static const struct hda_verb ad1986a_eapd_init_verbs[] = {
	{0x1b, AC_VERB_SET_EAPD_BTLENABLE, 0x00 },
	{}
};

static const struct hda_verb ad1986a_automic_verbs[] = {
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	/*{0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},*/
	{0x0f, AC_VERB_SET_CONNECT_SEL, 0x0},
	{0x1f, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | AD1986A_MIC_EVENT},
	{}
};

/* pin sensing on HP jack */
static const struct hda_verb ad1986a_hp_init_verbs[] = {
	{0x1a, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | AD1986A_HP_EVENT},
	{}
};

static void ad1986a_samsung_p50_unsol_event(struct hda_codec *codec,
					    unsigned int res)
{
	switch (res >> 26) {
	case AD1986A_HP_EVENT:
		ad1986a_hp_automute(codec);
		break;
	case AD1986A_MIC_EVENT:
		ad1986a_automic(codec);
		break;
	}
}

static int ad1986a_samsung_p50_init(struct hda_codec *codec)
{
	ad198x_init(codec);
	ad1986a_hp_automute(codec);
	ad1986a_automic(codec);
	return 0;
}


/* models */
enum {
	AD1986A_AUTO,
	AD1986A_6STACK,
	AD1986A_3STACK,
	AD1986A_LAPTOP,
	AD1986A_LAPTOP_EAPD,
	AD1986A_LAPTOP_AUTOMUTE,
	AD1986A_SAMSUNG,
	AD1986A_SAMSUNG_P50,
	AD1986A_MODELS
};

static const char * const ad1986a_models[AD1986A_MODELS] = {
	[AD1986A_AUTO]		= "auto",
	[AD1986A_6STACK]	= "6stack",
	[AD1986A_3STACK]	= "3stack",
	[AD1986A_LAPTOP]	= "laptop",
	[AD1986A_LAPTOP_EAPD]	= "laptop-eapd",
	[AD1986A_LAPTOP_AUTOMUTE] = "laptop-automute",
	[AD1986A_SAMSUNG]	= "samsung",
	[AD1986A_SAMSUNG_P50]	= "samsung-p50",
};

static const struct snd_pci_quirk ad1986a_cfg_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x30af, "HP B2800", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1153, "ASUS M9", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x11f7, "ASUS U5A", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1213, "ASUS A6J", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1263, "ASUS U5F", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1297, "ASUS Z62F", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x12b3, "ASUS V1j", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1302, "ASUS W3j", AD1986A_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x1043, 0x1443, "ASUS VX1", AD1986A_LAPTOP),
	SND_PCI_QUIRK(0x1043, 0x1447, "ASUS A8J", AD1986A_3STACK),
	SND_PCI_QUIRK(0x1043, 0x817f, "ASUS P5", AD1986A_3STACK),
	SND_PCI_QUIRK(0x1043, 0x818f, "ASUS P5", AD1986A_LAPTOP),
	SND_PCI_QUIRK(0x1043, 0x81b3, "ASUS P5", AD1986A_3STACK),
	SND_PCI_QUIRK(0x1043, 0x81cb, "ASUS M2N", AD1986A_3STACK),
	SND_PCI_QUIRK(0x1043, 0x8234, "ASUS M2N", AD1986A_3STACK),
	SND_PCI_QUIRK(0x10de, 0xcb84, "ASUS A8N-VM", AD1986A_3STACK),
	SND_PCI_QUIRK(0x1179, 0xff40, "Toshiba Satellite L40-10Q", AD1986A_3STACK),
	SND_PCI_QUIRK(0x144d, 0xb03c, "Samsung R55", AD1986A_3STACK),
	SND_PCI_QUIRK(0x144d, 0xc01e, "FSC V2060", AD1986A_LAPTOP),
	SND_PCI_QUIRK(0x144d, 0xc024, "Samsung P50", AD1986A_SAMSUNG_P50),
	SND_PCI_QUIRK_MASK(0x144d, 0xff00, 0xc000, "Samsung", AD1986A_SAMSUNG),
	SND_PCI_QUIRK(0x144d, 0xc504, "Samsung Q35", AD1986A_3STACK),
	SND_PCI_QUIRK(0x17aa, 0x1011, "Lenovo M55", AD1986A_LAPTOP),
	SND_PCI_QUIRK(0x17aa, 0x1017, "Lenovo A60", AD1986A_3STACK),
	SND_PCI_QUIRK(0x17aa, 0x2066, "Lenovo N100", AD1986A_LAPTOP_AUTOMUTE),
	SND_PCI_QUIRK(0x17c0, 0x2017, "Samsung M50", AD1986A_LAPTOP),
	{}
};

#ifdef CONFIG_PM
static const struct hda_amp_list ad1986a_loopbacks[] = {
	{ 0x13, HDA_OUTPUT, 0 }, /* Mic */
	{ 0x14, HDA_OUTPUT, 0 }, /* Phone */
	{ 0x15, HDA_OUTPUT, 0 }, /* CD */
	{ 0x16, HDA_OUTPUT, 0 }, /* Aux */
	{ 0x17, HDA_OUTPUT, 0 }, /* Line */
	{ } /* end */
};
#endif

static int is_jack_available(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int conf = snd_hda_codec_get_pincfg(codec, nid);
	return get_defcfg_connect(conf) != AC_JACK_PORT_NONE;
}
#endif /* ENABLE_AD_STATIC_QUIRKS */

static int alloc_ad_spec(struct hda_codec *codec)
{
	struct ad198x_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	codec->spec = spec;
	snd_hda_gen_spec_init(&spec->gen);
	return 0;
}

/*
 * AD1986A fixup codes
 */

/* Lenovo N100 seems to report the reversed bit for HP jack-sensing */
static void ad_fixup_inv_jack_detect(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		codec->inv_jack_detect = 1;
}

enum {
	AD1986A_FIXUP_INV_JACK_DETECT,
	AD1986A_FIXUP_ULTRA,
};

static const struct hda_fixup ad1986a_fixups[] = {
	[AD1986A_FIXUP_INV_JACK_DETECT] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = ad_fixup_inv_jack_detect,
	},
	[AD1986A_FIXUP_ULTRA] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x1b, 0x90170110 }, /* speaker */
			{ 0x1d, 0x90a7013e }, /* int mic */
			{}
		},
	},
};

static const struct snd_pci_quirk ad1986a_fixup_tbl[] = {
	SND_PCI_QUIRK(0x144d, 0xc027, "Samsung Q1", AD1986A_FIXUP_ULTRA),
	SND_PCI_QUIRK(0x17aa, 0x2066, "Lenovo N100", AD1986A_FIXUP_INV_JACK_DETECT),
	{}
};

/*
 */
static int ad1986a_parse_auto_config(struct hda_codec *codec)
{
	int err;
	struct ad198x_spec *spec;

	err = alloc_ad_spec(codec);
	if (err < 0)
		return err;
	spec = codec->spec;

	/* AD1986A has the inverted EAPD implementation */
	codec->inv_eapd = 1;

	spec->gen.mixer_nid = 0x07;
	spec->gen.beep_nid = 0x19;
	set_beep_amp(spec, 0x18, 0, HDA_OUTPUT);

	/* AD1986A has a hardware problem that it can't share a stream
	 * with multiple output pins.  The copy of front to surrounds
	 * causes noisy or silent outputs at a certain timing, e.g.
	 * changing the volume.
	 * So, let's disable the shared stream.
	 */
	spec->gen.multiout.no_share_stream = 1;

	snd_hda_pick_fixup(codec, NULL, ad1986a_fixup_tbl, ad1986a_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = ad198x_parse_auto_config(codec);
	if (err < 0) {
		snd_hda_gen_free(codec);
		return err;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;
}

#ifdef ENABLE_AD_STATIC_QUIRKS
static int patch_ad1986a(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	int err, board_config;

	board_config = snd_hda_check_board_config(codec, AD1986A_MODELS,
						  ad1986a_models,
						  ad1986a_cfg_tbl);
	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = AD1986A_AUTO;
	}

	if (board_config == AD1986A_AUTO)
		return ad1986a_parse_auto_config(codec);

	err = alloc_ad_spec(codec);
	if (err < 0)
		return err;
	spec = codec->spec;

	err = snd_hda_attach_beep_device(codec, 0x19);
	if (err < 0) {
		ad198x_free(codec);
		return err;
	}
	set_beep_amp(spec, 0x18, 0, HDA_OUTPUT);

	spec->multiout.max_channels = 6;
	spec->multiout.num_dacs = ARRAY_SIZE(ad1986a_dac_nids);
	spec->multiout.dac_nids = ad1986a_dac_nids;
	spec->multiout.dig_out_nid = AD1986A_SPDIF_OUT;
	spec->num_adc_nids = 1;
	spec->adc_nids = ad1986a_adc_nids;
	spec->capsrc_nids = ad1986a_capsrc_nids;
	spec->input_mux = &ad1986a_capture_source;
	spec->num_mixers = 1;
	spec->mixers[0] = ad1986a_mixers;
	spec->num_init_verbs = 1;
	spec->init_verbs[0] = ad1986a_init_verbs;
#ifdef CONFIG_PM
	spec->loopback.amplist = ad1986a_loopbacks;
#endif
	spec->vmaster_nid = 0x1b;
	codec->inv_eapd = 1; /* AD1986A has the inverted EAPD implementation */

	codec->patch_ops = ad198x_patch_ops;

	/* override some parameters */
	switch (board_config) {
	case AD1986A_3STACK:
		spec->num_mixers = 2;
		spec->mixers[1] = ad1986a_3st_mixers;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = ad1986a_ch2_init;
		spec->channel_mode = ad1986a_modes;
		spec->num_channel_mode = ARRAY_SIZE(ad1986a_modes);
		spec->need_dac_fix = 1;
		spec->multiout.max_channels = 2;
		spec->multiout.num_dacs = 1;
		break;
	case AD1986A_LAPTOP:
		spec->mixers[0] = ad1986a_laptop_mixers;
		spec->multiout.max_channels = 2;
		spec->multiout.num_dacs = 1;
		spec->multiout.dac_nids = ad1986a_laptop_dac_nids;
		break;
	case AD1986A_LAPTOP_EAPD:
		spec->num_mixers = 3;
		spec->mixers[0] = ad1986a_laptop_master_mixers;
		spec->mixers[1] = ad1986a_laptop_eapd_mixers;
		spec->mixers[2] = ad1986a_laptop_intmic_mixers;
		spec->num_init_verbs = 2;
		spec->init_verbs[1] = ad1986a_eapd_init_verbs;
		spec->multiout.max_channels = 2;
		spec->multiout.num_dacs = 1;
		spec->multiout.dac_nids = ad1986a_laptop_dac_nids;
		if (!is_jack_available(codec, 0x25))
			spec->multiout.dig_out_nid = 0;
		spec->input_mux = &ad1986a_laptop_eapd_capture_source;
		break;
	case AD1986A_SAMSUNG:
		spec->num_mixers = 2;
		spec->mixers[0] = ad1986a_laptop_master_mixers;
		spec->mixers[1] = ad1986a_laptop_eapd_mixers;
		spec->num_init_verbs = 3;
		spec->init_verbs[1] = ad1986a_eapd_init_verbs;
		spec->init_verbs[2] = ad1986a_automic_verbs;
		spec->multiout.max_channels = 2;
		spec->multiout.num_dacs = 1;
		spec->multiout.dac_nids = ad1986a_laptop_dac_nids;
		if (!is_jack_available(codec, 0x25))
			spec->multiout.dig_out_nid = 0;
		spec->input_mux = &ad1986a_automic_capture_source;
		codec->patch_ops.unsol_event = ad1986a_automic_unsol_event;
		codec->patch_ops.init = ad1986a_automic_init;
		break;
	case AD1986A_SAMSUNG_P50:
		spec->num_mixers = 2;
		spec->mixers[0] = ad1986a_automute_master_mixers;
		spec->mixers[1] = ad1986a_laptop_eapd_mixers;
		spec->num_init_verbs = 4;
		spec->init_verbs[1] = ad1986a_eapd_init_verbs;
		spec->init_verbs[2] = ad1986a_automic_verbs;
		spec->init_verbs[3] = ad1986a_hp_init_verbs;
		spec->multiout.max_channels = 2;
		spec->multiout.num_dacs = 1;
		spec->multiout.dac_nids = ad1986a_laptop_dac_nids;
		if (!is_jack_available(codec, 0x25))
			spec->multiout.dig_out_nid = 0;
		spec->input_mux = &ad1986a_automic_capture_source;
		codec->patch_ops.unsol_event = ad1986a_samsung_p50_unsol_event;
		codec->patch_ops.init = ad1986a_samsung_p50_init;
		break;
	case AD1986A_LAPTOP_AUTOMUTE:
		spec->num_mixers = 3;
		spec->mixers[0] = ad1986a_automute_master_mixers;
		spec->mixers[1] = ad1986a_laptop_eapd_mixers;
		spec->mixers[2] = ad1986a_laptop_intmic_mixers;
		spec->num_init_verbs = 3;
		spec->init_verbs[1] = ad1986a_eapd_init_verbs;
		spec->init_verbs[2] = ad1986a_hp_init_verbs;
		spec->multiout.max_channels = 2;
		spec->multiout.num_dacs = 1;
		spec->multiout.dac_nids = ad1986a_laptop_dac_nids;
		if (!is_jack_available(codec, 0x25))
			spec->multiout.dig_out_nid = 0;
		spec->input_mux = &ad1986a_laptop_eapd_capture_source;
		codec->patch_ops.unsol_event = ad1986a_hp_unsol_event;
		codec->patch_ops.init = ad1986a_hp_init;
		/* Lenovo N100 seems to report the reversed bit
		 * for HP jack-sensing
		 */
		spec->inv_jack_detect = 1;
		break;
	}

	/* AD1986A has a hardware problem that it can't share a stream
	 * with multiple output pins.  The copy of front to surrounds
	 * causes noisy or silent outputs at a certain timing, e.g.
	 * changing the volume.
	 * So, let's disable the shared stream.
	 */
	spec->multiout.no_share_stream = 1;

	codec->no_trigger_sense = 1;
	codec->no_sticky_stream = 1;

	return 0;
}
#else /* ENABLE_AD_STATIC_QUIRKS */
#define patch_ad1986a	ad1986a_parse_auto_config
#endif /* ENABLE_AD_STATIC_QUIRKS */

/*
 * AD1983 specific
 */

/*
 * SPDIF mux control for AD1983 auto-parser
 */
static int ad1983_auto_smux_enum_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	static const char * const texts2[] = { "PCM", "ADC" };
	static const char * const texts3[] = { "PCM", "ADC1", "ADC2" };
	hda_nid_t dig_out = spec->gen.multiout.dig_out_nid;
	int num_conns = snd_hda_get_num_conns(codec, dig_out);

	if (num_conns == 2)
		return snd_hda_enum_helper_info(kcontrol, uinfo, 2, texts2);
	else if (num_conns == 3)
		return snd_hda_enum_helper_info(kcontrol, uinfo, 3, texts3);
	else
		return -EINVAL;
}

static int ad1983_auto_smux_enum_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->cur_smux;
	return 0;
}

static int ad1983_auto_smux_enum_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	unsigned int val = ucontrol->value.enumerated.item[0];
	hda_nid_t dig_out = spec->gen.multiout.dig_out_nid;
	int num_conns = snd_hda_get_num_conns(codec, dig_out);

	if (val >= num_conns)
		return -EINVAL;
	if (spec->cur_smux == val)
		return 0;
	spec->cur_smux = val;
	snd_hda_codec_write_cache(codec, dig_out, 0,
				  AC_VERB_SET_CONNECT_SEL, val);
	return 1;
}

static struct snd_kcontrol_new ad1983_auto_smux_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "IEC958 Playback Source",
	.info = ad1983_auto_smux_enum_info,
	.get = ad1983_auto_smux_enum_get,
	.put = ad1983_auto_smux_enum_put,
};

static int ad1983_add_spdif_mux_ctl(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	hda_nid_t dig_out = spec->gen.multiout.dig_out_nid;
	int num_conns;

	if (!dig_out)
		return 0;
	num_conns = snd_hda_get_num_conns(codec, dig_out);
	if (num_conns != 2 && num_conns != 3)
		return 0;
	if (!snd_hda_gen_add_kctl(&spec->gen, NULL, &ad1983_auto_smux_mixer))
		return -ENOMEM;
	return 0;
}

static int patch_ad1983(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	int err;

	err = alloc_ad_spec(codec);
	if (err < 0)
		return err;
	spec = codec->spec;

	spec->gen.beep_nid = 0x10;
	set_beep_amp(spec, 0x10, 0, HDA_OUTPUT);
	err = ad198x_parse_auto_config(codec);
	if (err < 0)
		goto error;
	err = ad1983_add_spdif_mux_ctl(codec);
	if (err < 0)
		goto error;
	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}


/*
 * AD1981 HD specific
 */

/* follow EAPD via vmaster hook */
static void ad_vmaster_eapd_hook(void *private_data, int enabled)
{
	struct hda_codec *codec = private_data;
	struct ad198x_spec *spec = codec->spec;

	if (!spec->eapd_nid)
		return;
	snd_hda_codec_update_cache(codec, spec->eapd_nid, 0,
				   AC_VERB_SET_EAPD_BTLENABLE,
				   enabled ? 0x02 : 0x00);
}

static void ad1981_fixup_hp_eapd(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	struct ad198x_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gen.vmaster_mute.hook = ad_vmaster_eapd_hook;
		spec->eapd_nid = 0x05;
	}
}

/* set the upper-limit for mixer amp to 0dB for avoiding the possible
 * damage by overloading
 */
static void ad1981_fixup_amp_override(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		snd_hda_override_amp_caps(codec, 0x11, HDA_INPUT,
					  (0x17 << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x17 << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x05 << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (1 << AC_AMPCAP_MUTE_SHIFT));
}

enum {
	AD1981_FIXUP_AMP_OVERRIDE,
	AD1981_FIXUP_HP_EAPD,
};

static const struct hda_fixup ad1981_fixups[] = {
	[AD1981_FIXUP_AMP_OVERRIDE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = ad1981_fixup_amp_override,
	},
	[AD1981_FIXUP_HP_EAPD] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = ad1981_fixup_hp_eapd,
		.chained = true,
		.chain_id = AD1981_FIXUP_AMP_OVERRIDE,
	},
};

static const struct snd_pci_quirk ad1981_fixup_tbl[] = {
	SND_PCI_QUIRK_VENDOR(0x1014, "Lenovo", AD1981_FIXUP_AMP_OVERRIDE),
	SND_PCI_QUIRK_VENDOR(0x103c, "HP", AD1981_FIXUP_HP_EAPD),
	SND_PCI_QUIRK_VENDOR(0x17aa, "Lenovo", AD1981_FIXUP_AMP_OVERRIDE),
	/* HP nx6320 (reversed SSID, H/W bug) */
	SND_PCI_QUIRK(0x30b0, 0x103c, "HP nx6320", AD1981_FIXUP_HP_EAPD),
	{}
};

static int patch_ad1981(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	int err;

	err = alloc_ad_spec(codec);
	if (err < 0)
		return -ENOMEM;
	spec = codec->spec;

	spec->gen.mixer_nid = 0x0e;
	spec->gen.beep_nid = 0x10;
	set_beep_amp(spec, 0x0d, 0, HDA_OUTPUT);

	snd_hda_pick_fixup(codec, NULL, ad1981_fixup_tbl, ad1981_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = ad198x_parse_auto_config(codec);
	if (err < 0)
		goto error;
	err = ad1983_add_spdif_mux_ctl(codec);
	if (err < 0)
		goto error;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}


/*
 * AD1988
 *
 * Output pins and routes
 *
 *        Pin               Mix     Sel     DAC (*)
 * port-A 0x11 (mute/hp) <- 0x22 <- 0x37 <- 03/04/06
 * port-B 0x14 (mute/hp) <- 0x2b <- 0x30 <- 03/04/06
 * port-C 0x15 (mute)    <- 0x2c <- 0x31 <- 05/0a
 * port-D 0x12 (mute/hp) <- 0x29         <- 04
 * port-E 0x17 (mute/hp) <- 0x26 <- 0x32 <- 05/0a
 * port-F 0x16 (mute)    <- 0x2a         <- 06
 * port-G 0x24 (mute)    <- 0x27         <- 05
 * port-H 0x25 (mute)    <- 0x28         <- 0a
 * mono   0x13 (mute/amp)<- 0x1e <- 0x36 <- 03/04/06
 *
 * DAC0 = 03h, DAC1 = 04h, DAC2 = 05h, DAC3 = 06h, DAC4 = 0ah
 * (*) DAC2/3/4 are swapped to DAC3/4/2 on AD198A rev.2 due to a h/w bug.
 *
 * Input pins and routes
 *
 *        pin     boost   mix input # / adc input #
 * port-A 0x11 -> 0x38 -> mix 2, ADC 0
 * port-B 0x14 -> 0x39 -> mix 0, ADC 1
 * port-C 0x15 -> 0x3a -> 33:0 - mix 1, ADC 2
 * port-D 0x12 -> 0x3d -> mix 3, ADC 8
 * port-E 0x17 -> 0x3c -> 34:0 - mix 4, ADC 4
 * port-F 0x16 -> 0x3b -> mix 5, ADC 3
 * port-G 0x24 -> N/A  -> 33:1 - mix 1, 34:1 - mix 4, ADC 6
 * port-H 0x25 -> N/A  -> 33:2 - mix 1, 34:2 - mix 4, ADC 7
 *
 *
 * DAC assignment
 *   6stack - front/surr/CLFE/side/opt DACs - 04/06/05/0a/03
 *   3stack - front/surr/CLFE/opt DACs - 04/05/0a/03
 *
 * Inputs of Analog Mix (0x20)
 *   0:Port-B (front mic)
 *   1:Port-C/G/H (line-in)
 *   2:Port-A
 *   3:Port-D (line-in/2)
 *   4:Port-E/G/H (mic-in)
 *   5:Port-F (mic2-in)
 *   6:CD
 *   7:Beep
 *
 * ADC selection
 *   0:Port-A
 *   1:Port-B (front mic-in)
 *   2:Port-C (line-in)
 *   3:Port-F (mic2-in)
 *   4:Port-E (mic-in)
 *   5:CD
 *   6:Port-G
 *   7:Port-H
 *   8:Port-D (line-in/2)
 *   9:Mix
 *
 * Proposed pin assignments by the datasheet
 *
 * 6-stack
 * Port-A front headphone
 *      B front mic-in
 *      C rear line-in
 *      D rear front-out
 *      E rear mic-in
 *      F rear surround
 *      G rear CLFE
 *      H rear side
 *
 * 3-stack
 * Port-A front headphone
 *      B front mic
 *      C rear line-in/surround
 *      D rear front-out
 *      E rear mic-in/CLFE
 *
 * laptop
 * Port-A headphone
 *      B mic-in
 *      C docking station
 *      D internal speaker (with EAPD)
 *      E/F quad mic array
 */

#ifdef ENABLE_AD_STATIC_QUIRKS
static int ad198x_ch_mode_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_ch_mode_info(codec, uinfo, spec->channel_mode,
				    spec->num_channel_mode);
}

static int ad198x_ch_mode_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	return snd_hda_ch_mode_get(codec, ucontrol, spec->channel_mode,
				   spec->num_channel_mode, spec->multiout.max_channels);
}

static int ad198x_ch_mode_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	int err = snd_hda_ch_mode_put(codec, ucontrol, spec->channel_mode,
				      spec->num_channel_mode,
				      &spec->multiout.max_channels);
	if (err >= 0 && spec->need_dac_fix)
		spec->multiout.num_dacs = spec->multiout.max_channels / 2;
	return err;
}
#endif /* ENABLE_AD_STATIC_QUIRKS */

static int ad1988_auto_smux_enum_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	static const char * const texts[] = {
		"PCM", "ADC1", "ADC2", "ADC3",
	};
	int num_conns = snd_hda_get_num_conns(codec, 0x0b) + 1;
	if (num_conns > 4)
		num_conns = 4;
	return snd_hda_enum_helper_info(kcontrol, uinfo, num_conns, texts);
}

static int ad1988_auto_smux_enum_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->cur_smux;
	return 0;
}

static int ad1988_auto_smux_enum_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ad198x_spec *spec = codec->spec;
	unsigned int val = ucontrol->value.enumerated.item[0];
	struct nid_path *path;
	int num_conns = snd_hda_get_num_conns(codec, 0x0b) + 1;

	if (val >= num_conns)
		return -EINVAL;
	if (spec->cur_smux == val)
		return 0;

	mutex_lock(&codec->control_mutex);
	codec->cached_write = 1;
	path = snd_hda_get_path_from_idx(codec,
					 spec->smux_paths[spec->cur_smux]);
	if (path)
		snd_hda_activate_path(codec, path, false, true);
	path = snd_hda_get_path_from_idx(codec, spec->smux_paths[val]);
	if (path)
		snd_hda_activate_path(codec, path, true, true);
	spec->cur_smux = val;
	codec->cached_write = 0;
	mutex_unlock(&codec->control_mutex);
	snd_hda_codec_flush_cache(codec); /* flush the updates */
	return 1;
}

static struct snd_kcontrol_new ad1988_auto_smux_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "IEC958 Playback Source",
	.info = ad1988_auto_smux_enum_info,
	.get = ad1988_auto_smux_enum_get,
	.put = ad1988_auto_smux_enum_put,
};

static int ad1988_auto_init(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	int i, err;

	err = snd_hda_gen_init(codec);
	if (err < 0)
		return err;
	if (!spec->gen.autocfg.dig_outs)
		return 0;

	for (i = 0; i < 4; i++) {
		struct nid_path *path;
		path = snd_hda_get_path_from_idx(codec, spec->smux_paths[i]);
		if (path)
			snd_hda_activate_path(codec, path, path->active, false);
	}

	return 0;
}

static int ad1988_add_spdif_mux_ctl(struct hda_codec *codec)
{
	struct ad198x_spec *spec = codec->spec;
	int i, num_conns;
	/* we create four static faked paths, since AD codecs have odd
	 * widget connections regarding the SPDIF out source
	 */
	static struct nid_path fake_paths[4] = {
		{
			.depth = 3,
			.path = { 0x02, 0x1d, 0x1b },
			.idx = { 0, 0, 0 },
			.multi = { 0, 0, 0 },
		},
		{
			.depth = 4,
			.path = { 0x08, 0x0b, 0x1d, 0x1b },
			.idx = { 0, 0, 1, 0 },
			.multi = { 0, 1, 0, 0 },
		},
		{
			.depth = 4,
			.path = { 0x09, 0x0b, 0x1d, 0x1b },
			.idx = { 0, 1, 1, 0 },
			.multi = { 0, 1, 0, 0 },
		},
		{
			.depth = 4,
			.path = { 0x0f, 0x0b, 0x1d, 0x1b },
			.idx = { 0, 2, 1, 0 },
			.multi = { 0, 1, 0, 0 },
		},
	};

	/* SPDIF source mux appears to be present only on AD1988A */
	if (!spec->gen.autocfg.dig_outs ||
	    get_wcaps_type(get_wcaps(codec, 0x1d)) != AC_WID_AUD_MIX)
		return 0;

	num_conns = snd_hda_get_num_conns(codec, 0x0b) + 1;
	if (num_conns != 3 && num_conns != 4)
		return 0;

	for (i = 0; i < num_conns; i++) {
		struct nid_path *path = snd_array_new(&spec->gen.paths);
		if (!path)
			return -ENOMEM;
		*path = fake_paths[i];
		if (!i)
			path->active = 1;
		spec->smux_paths[i] = snd_hda_get_path_idx(codec, path);
	}

	if (!snd_hda_gen_add_kctl(&spec->gen, NULL, &ad1988_auto_smux_mixer))
		return -ENOMEM;

	codec->patch_ops.init = ad1988_auto_init;

	return 0;
}

/*
 */

enum {
	AD1988_FIXUP_6STACK_DIG,
};

static const struct hda_fixup ad1988_fixups[] = {
	[AD1988_FIXUP_6STACK_DIG] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x11, 0x02214130 }, /* front-hp */
			{ 0x12, 0x01014010 }, /* line-out */
			{ 0x14, 0x02a19122 }, /* front-mic */
			{ 0x15, 0x01813021 }, /* line-in */
			{ 0x16, 0x01011012 }, /* line-out */
			{ 0x17, 0x01a19020 }, /* mic */
			{ 0x1b, 0x0145f1f0 }, /* SPDIF */
			{ 0x24, 0x01016011 }, /* line-out */
			{ 0x25, 0x01012013 }, /* line-out */
			{ }
		}
	},
};

static const struct hda_model_fixup ad1988_fixup_models[] = {
	{ .id = AD1988_FIXUP_6STACK_DIG, .name = "6stack-dig" },
	{}
};

static int patch_ad1988(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	int err;

	err = alloc_ad_spec(codec);
	if (err < 0)
		return err;
	spec = codec->spec;

	spec->gen.mixer_nid = 0x20;
	spec->gen.mixer_merge_nid = 0x21;
	spec->gen.beep_nid = 0x10;
	set_beep_amp(spec, 0x10, 0, HDA_OUTPUT);

	snd_hda_pick_fixup(codec, ad1988_fixup_models, NULL, ad1988_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = ad198x_parse_auto_config(codec);
	if (err < 0)
		goto error;
	err = ad1988_add_spdif_mux_ctl(codec);
	if (err < 0)
		goto error;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}


/*
 * AD1884 / AD1984
 *
 * port-B - front line/mic-in
 * port-E - aux in/out
 * port-F - aux in/out
 * port-C - rear line/mic-in
 * port-D - rear line/hp-out
 * port-A - front line/hp-out
 *
 * AD1984 = AD1884 + two digital mic-ins
 *
 * AD1883 / AD1884A / AD1984A / AD1984B
 *
 * port-B (0x14) - front mic-in
 * port-E (0x1c) - rear mic-in
 * port-F (0x16) - CD / ext out
 * port-C (0x15) - rear line-in
 * port-D (0x12) - rear line-out
 * port-A (0x11) - front hp-out
 *
 * AD1984A = AD1884A + digital-mic
 * AD1883 = equivalent with AD1984A
 * AD1984B = AD1984A + extra SPDIF-out
 */

/* set the upper-limit for mixer amp to 0dB for avoiding the possible
 * damage by overloading
 */
static void ad1884_fixup_amp_override(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		snd_hda_override_amp_caps(codec, 0x20, HDA_INPUT,
					  (0x17 << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x17 << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x05 << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (1 << AC_AMPCAP_MUTE_SHIFT));
}

/* toggle GPIO1 according to the mute state */
static void ad1884_vmaster_hp_gpio_hook(void *private_data, int enabled)
{
	struct hda_codec *codec = private_data;
	struct ad198x_spec *spec = codec->spec;

	if (spec->eapd_nid)
		ad_vmaster_eapd_hook(private_data, enabled);
	snd_hda_codec_update_cache(codec, 0x01, 0,
				   AC_VERB_SET_GPIO_DATA,
				   enabled ? 0x00 : 0x02);
}

static void ad1884_fixup_hp_eapd(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	struct ad198x_spec *spec = codec->spec;
	static const struct hda_verb gpio_init_verbs[] = {
		{0x01, AC_VERB_SET_GPIO_MASK, 0x02},
		{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x02},
		{0x01, AC_VERB_SET_GPIO_DATA, 0x02},
		{},
	};

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		spec->gen.vmaster_mute.hook = ad1884_vmaster_hp_gpio_hook;
		snd_hda_sequence_write_cache(codec, gpio_init_verbs);
		break;
	case HDA_FIXUP_ACT_PROBE:
		if (spec->gen.autocfg.line_out_type == AUTO_PIN_SPEAKER_OUT)
			spec->eapd_nid = spec->gen.autocfg.line_out_pins[0];
		else
			spec->eapd_nid = spec->gen.autocfg.speaker_pins[0];
		break;
	}
}

/* set magic COEFs for dmic */
static const struct hda_verb ad1884_dmic_init_verbs[] = {
	{0x01, AC_VERB_SET_COEF_INDEX, 0x13f7},
	{0x01, AC_VERB_SET_PROC_COEF, 0x08},
	{}
};

enum {
	AD1884_FIXUP_AMP_OVERRIDE,
	AD1884_FIXUP_HP_EAPD,
	AD1884_FIXUP_DMIC_COEF,
	AD1884_FIXUP_HP_TOUCHSMART,
};

static const struct hda_fixup ad1884_fixups[] = {
	[AD1884_FIXUP_AMP_OVERRIDE] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = ad1884_fixup_amp_override,
	},
	[AD1884_FIXUP_HP_EAPD] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = ad1884_fixup_hp_eapd,
		.chained = true,
		.chain_id = AD1884_FIXUP_AMP_OVERRIDE,
	},
	[AD1884_FIXUP_DMIC_COEF] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = ad1884_dmic_init_verbs,
	},
	[AD1884_FIXUP_HP_TOUCHSMART] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = ad1884_dmic_init_verbs,
		.chained = true,
		.chain_id = AD1884_FIXUP_HP_EAPD,
	},
};

static const struct snd_pci_quirk ad1884_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x2a82, "HP Touchsmart", AD1884_FIXUP_HP_TOUCHSMART),
	SND_PCI_QUIRK_VENDOR(0x103c, "HP", AD1884_FIXUP_HP_EAPD),
	SND_PCI_QUIRK_VENDOR(0x17aa, "Lenovo Thinkpad", AD1884_FIXUP_DMIC_COEF),
	{}
};


static int patch_ad1884(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	int err;

	err = alloc_ad_spec(codec);
	if (err < 0)
		return err;
	spec = codec->spec;

	spec->gen.mixer_nid = 0x20;
	spec->gen.beep_nid = 0x10;
	set_beep_amp(spec, 0x10, 0, HDA_OUTPUT);

	snd_hda_pick_fixup(codec, NULL, ad1884_fixup_tbl, ad1884_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = ad198x_parse_auto_config(codec);
	if (err < 0)
		goto error;
	err = ad1983_add_spdif_mux_ctl(codec);
	if (err < 0)
		goto error;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}

/*
 * AD1882 / AD1882A
 *
 * port-A - front hp-out
 * port-B - front mic-in
 * port-C - rear line-in, shared surr-out (3stack)
 * port-D - rear line-out
 * port-E - rear mic-in, shared clfe-out (3stack)
 * port-F - rear surr-out (6stack)
 * port-G - rear clfe-out (6stack)
 */

static int patch_ad1882(struct hda_codec *codec)
{
	struct ad198x_spec *spec;
	int err;

	err = alloc_ad_spec(codec);
	if (err < 0)
		return err;
	spec = codec->spec;

	spec->gen.mixer_nid = 0x20;
	spec->gen.mixer_merge_nid = 0x21;
	spec->gen.beep_nid = 0x10;
	set_beep_amp(spec, 0x10, 0, HDA_OUTPUT);
	err = ad198x_parse_auto_config(codec);
	if (err < 0)
		goto error;
	err = ad1988_add_spdif_mux_ctl(codec);
	if (err < 0)
		goto error;
	return 0;

 error:
	snd_hda_gen_free(codec);
	return err;
}


/*
 * patch entries
 */
static const struct hda_codec_preset snd_hda_preset_analog[] = {
	{ .id = 0x11d4184a, .name = "AD1884A", .patch = patch_ad1884 },
	{ .id = 0x11d41882, .name = "AD1882", .patch = patch_ad1882 },
	{ .id = 0x11d41883, .name = "AD1883", .patch = patch_ad1884 },
	{ .id = 0x11d41884, .name = "AD1884", .patch = patch_ad1884 },
	{ .id = 0x11d4194a, .name = "AD1984A", .patch = patch_ad1884 },
	{ .id = 0x11d4194b, .name = "AD1984B", .patch = patch_ad1884 },
	{ .id = 0x11d41981, .name = "AD1981", .patch = patch_ad1981 },
	{ .id = 0x11d41983, .name = "AD1983", .patch = patch_ad1983 },
	{ .id = 0x11d41984, .name = "AD1984", .patch = patch_ad1884 },
	{ .id = 0x11d41986, .name = "AD1986A", .patch = patch_ad1986a },
	{ .id = 0x11d41988, .name = "AD1988", .patch = patch_ad1988 },
	{ .id = 0x11d4198b, .name = "AD1988B", .patch = patch_ad1988 },
	{ .id = 0x11d4882a, .name = "AD1882A", .patch = patch_ad1882 },
	{ .id = 0x11d4989a, .name = "AD1989A", .patch = patch_ad1988 },
	{ .id = 0x11d4989b, .name = "AD1989B", .patch = patch_ad1988 },
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:11d4*");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Analog Devices HD-audio codec");

static struct hda_codec_preset_list analog_list = {
	.preset = snd_hda_preset_analog,
	.owner = THIS_MODULE,
};

static int __init patch_analog_init(void)
{
	return snd_hda_add_codec_preset(&analog_list);
}

static void __exit patch_analog_exit(void)
{
	snd_hda_delete_codec_preset(&analog_list);
}

module_init(patch_analog_init)
module_exit(patch_analog_exit)
