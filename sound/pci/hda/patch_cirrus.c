/*
 * HD audio interface patch for Cirrus Logic CS420x chip
 *
 * Copyright (c) 2009 Takashi Iwai <tiwai@suse.de>
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

/*
 */

struct cs_spec {
	int board_config;
	struct auto_pin_cfg autocfg;
	struct hda_multi_out multiout;
	struct snd_kcontrol *vmaster_sw;
	struct snd_kcontrol *vmaster_vol;

	hda_nid_t dac_nid[AUTO_CFG_MAX_OUTS];
	hda_nid_t slave_dig_outs[2];

	unsigned int input_idx[AUTO_PIN_LAST];
	unsigned int capsrc_idx[AUTO_PIN_LAST];
	hda_nid_t adc_nid[AUTO_PIN_LAST];
	unsigned int adc_idx[AUTO_PIN_LAST];
	unsigned int num_inputs;
	unsigned int cur_input;
	unsigned int automic_idx;
	hda_nid_t cur_adc;
	unsigned int cur_adc_stream_tag;
	unsigned int cur_adc_format;
	hda_nid_t dig_in;

	struct hda_bind_ctls *capture_bind[2];

	unsigned int gpio_mask;
	unsigned int gpio_dir;
	unsigned int gpio_data;

	struct hda_pcm pcm_rec[2];	/* PCM information */

	unsigned int hp_detect:1;
	unsigned int mic_detect:1;
};

/* available models */
enum {
	CS420X_MBP55,
	CS420X_AUTO,
	CS420X_MODELS
};

/* Vendor-specific processing widget */
#define CS420X_VENDOR_NID	0x11
#define CS_DIG_OUT1_PIN_NID	0x10
#define CS_DIG_OUT2_PIN_NID	0x15
#define CS_DMIC1_PIN_NID	0x12
#define CS_DMIC2_PIN_NID	0x0e

/* coef indices */
#define IDX_SPDIF_STAT		0x0000
#define IDX_SPDIF_CTL		0x0001
#define IDX_ADC_CFG		0x0002
/* SZC bitmask, 4 modes below:
 * 0 = immediate,
 * 1 = digital immediate, analog zero-cross
 * 2 = digtail & analog soft-ramp
 * 3 = digital soft-ramp, analog zero-cross
 */
#define   CS_COEF_ADC_SZC_MASK		(3 << 0)
#define   CS_COEF_ADC_MIC_SZC_MODE	(3 << 0) /* SZC setup for mic */
#define   CS_COEF_ADC_LI_SZC_MODE	(3 << 0) /* SZC setup for line-in */
/* PGA mode: 0 = differential, 1 = signle-ended */
#define   CS_COEF_ADC_MIC_PGA_MODE	(1 << 5) /* PGA setup for mic */
#define   CS_COEF_ADC_LI_PGA_MODE	(1 << 6) /* PGA setup for line-in */
#define IDX_DAC_CFG		0x0003
/* SZC bitmask, 4 modes below:
 * 0 = Immediate
 * 1 = zero-cross
 * 2 = soft-ramp
 * 3 = soft-ramp on zero-cross
 */
#define   CS_COEF_DAC_HP_SZC_MODE	(3 << 0) /* nid 0x02 */
#define   CS_COEF_DAC_LO_SZC_MODE	(3 << 2) /* nid 0x03 */
#define   CS_COEF_DAC_SPK_SZC_MODE	(3 << 4) /* nid 0x04 */

#define IDX_BEEP_CFG		0x0004
/* 0x0008 - test reg key */
/* 0x0009 - 0x0014 -> 12 test regs */
/* 0x0015 - visibility reg */


static inline int cs_vendor_coef_get(struct hda_codec *codec, unsigned int idx)
{
	snd_hda_codec_write(codec, CS420X_VENDOR_NID, 0,
			    AC_VERB_SET_COEF_INDEX, idx);
	return snd_hda_codec_read(codec, CS420X_VENDOR_NID, 0,
				  AC_VERB_GET_PROC_COEF, 0);
}

static inline void cs_vendor_coef_set(struct hda_codec *codec, unsigned int idx,
				      unsigned int coef)
{
	snd_hda_codec_write(codec, CS420X_VENDOR_NID, 0,
			    AC_VERB_SET_COEF_INDEX, idx);
	snd_hda_codec_write(codec, CS420X_VENDOR_NID, 0,
			    AC_VERB_SET_PROC_COEF, coef);
}


#define HP_EVENT	1
#define MIC_EVENT	2

/*
 * PCM callbacks
 */
static int cs_playback_pcm_open(struct hda_pcm_stream *hinfo,
				struct hda_codec *codec,
				struct snd_pcm_substream *substream)
{
	struct cs_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream,
					     hinfo);
}

static int cs_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   unsigned int stream_tag,
				   unsigned int format,
				   struct snd_pcm_substream *substream)
{
	struct cs_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout,
						stream_tag, format, substream);
}

static int cs_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream)
{
	struct cs_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital out
 */
static int cs_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	struct cs_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int cs_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
				     struct hda_codec *codec,
				     struct snd_pcm_substream *substream)
{
	struct cs_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int cs_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       unsigned int stream_tag,
				       unsigned int format,
				       struct snd_pcm_substream *substream)
{
	struct cs_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout, stream_tag,
					     format, substream);
}

static int cs_dig_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       struct snd_pcm_substream *substream)
{
	struct cs_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_cleanup(codec, &spec->multiout);
}

/*
 * Analog capture
 */
static int cs_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  unsigned int stream_tag,
				  unsigned int format,
				  struct snd_pcm_substream *substream)
{
	struct cs_spec *spec = codec->spec;
	spec->cur_adc = spec->adc_nid[spec->cur_input];
	spec->cur_adc_stream_tag = stream_tag;
	spec->cur_adc_format = format;
	snd_hda_codec_setup_stream(codec, spec->cur_adc, stream_tag, 0, format);
	return 0;
}

static int cs_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream)
{
	struct cs_spec *spec = codec->spec;
	snd_hda_codec_cleanup_stream(codec, spec->cur_adc);
	spec->cur_adc = 0;
	return 0;
}

/*
 */
static struct hda_pcm_stream cs_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.open = cs_playback_pcm_open,
		.prepare = cs_playback_pcm_prepare,
		.cleanup = cs_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream cs_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.prepare = cs_capture_pcm_prepare,
		.cleanup = cs_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream cs_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.open = cs_dig_playback_pcm_open,
		.close = cs_dig_playback_pcm_close,
		.prepare = cs_dig_playback_pcm_prepare,
		.cleanup = cs_dig_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream cs_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
};

static int cs_build_pcms(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;

	codec->pcm_info = info;
	codec->num_pcms = 0;

	info->name = "Cirrus Analog";
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = cs_pcm_analog_playback;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->dac_nid[0];
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max =
		spec->multiout.max_channels;
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = cs_pcm_analog_capture;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].nid =
		spec->adc_nid[spec->cur_input];
	codec->num_pcms++;

	if (!spec->multiout.dig_out_nid && !spec->dig_in)
		return 0;

	info++;
	info->name = "Cirrus Digital";
	info->pcm_type = spec->autocfg.dig_out_type[0];
	if (!info->pcm_type)
		info->pcm_type = HDA_PCM_TYPE_SPDIF;
	if (spec->multiout.dig_out_nid) {
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
			cs_pcm_digital_playback;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
			spec->multiout.dig_out_nid;
	}
	if (spec->dig_in) {
		info->stream[SNDRV_PCM_STREAM_CAPTURE] =
			cs_pcm_digital_capture;
		info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->dig_in;
	}
	codec->num_pcms++;

	return 0;
}

/*
 * parse codec topology
 */

static hda_nid_t get_dac(struct hda_codec *codec, hda_nid_t pin)
{
	hda_nid_t dac;
	if (!pin)
		return 0;
	if (snd_hda_get_connections(codec, pin, &dac, 1) != 1)
		return 0;
	return dac;
}

static int is_ext_mic(struct hda_codec *codec, unsigned int idx)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	hda_nid_t pin = cfg->input_pins[idx];
	unsigned int val = snd_hda_query_pin_caps(codec, pin);
	if (!(val & AC_PINCAP_PRES_DETECT))
		return 0;
	val = snd_hda_codec_get_pincfg(codec, pin);
	return (get_defcfg_connect(val) == AC_JACK_PORT_COMPLEX);
}

static hda_nid_t get_adc(struct hda_codec *codec, hda_nid_t pin,
			 unsigned int *idxp)
{
	int i;
	hda_nid_t nid;

	nid = codec->start_nid;
	for (i = 0; i < codec->num_nodes; i++, nid++) {
		hda_nid_t pins[2];
		unsigned int type;
		int j, nums;
		type = (get_wcaps(codec, nid) & AC_WCAP_TYPE)
			>> AC_WCAP_TYPE_SHIFT;
		if (type != AC_WID_AUD_IN)
			continue;
		nums = snd_hda_get_connections(codec, nid, pins,
					       ARRAY_SIZE(pins));
		if (nums <= 0)
			continue;
		for (j = 0; j < nums; j++) {
			if (pins[j] == pin) {
				*idxp = j;
				return nid;
			}
		}
	}
	return 0;
}

static int is_active_pin(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int val;
	val = snd_hda_codec_get_pincfg(codec, nid);
	return (get_defcfg_connect(val) != AC_JACK_PORT_NONE);
}

static int parse_output(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i, extra_nids;
	hda_nid_t dac;

	for (i = 0; i < cfg->line_outs; i++) {
		dac = get_dac(codec, cfg->line_out_pins[i]);
		if (!dac)
			break;
		spec->dac_nid[i] = dac;
	}
	spec->multiout.num_dacs = i;
	spec->multiout.dac_nids = spec->dac_nid;
	spec->multiout.max_channels = i * 2;

	/* add HP and speakers */
	extra_nids = 0;
	for (i = 0; i < cfg->hp_outs; i++) {
		dac = get_dac(codec, cfg->hp_pins[i]);
		if (!dac)
			break;
		if (!i)
			spec->multiout.hp_nid = dac;
		else
			spec->multiout.extra_out_nid[extra_nids++] = dac;
	}
	for (i = 0; i < cfg->speaker_outs; i++) {
		dac = get_dac(codec, cfg->speaker_pins[i]);
		if (!dac)
			break;
		spec->multiout.extra_out_nid[extra_nids++] = dac;
	}

	if (cfg->line_out_type == AUTO_PIN_SPEAKER_OUT) {
		cfg->speaker_outs = cfg->line_outs;
		memcpy(cfg->speaker_pins, cfg->line_out_pins,
		       sizeof(cfg->speaker_pins));
		cfg->line_outs = 0;
	}

	return 0;
}

static int parse_input(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t pin = cfg->input_pins[i];
		if (!pin)
			continue;
		spec->input_idx[spec->num_inputs] = i;
		spec->capsrc_idx[i] = spec->num_inputs++;
		spec->cur_input = i;
		spec->adc_nid[i] = get_adc(codec, pin, &spec->adc_idx[i]);
	}
	if (!spec->num_inputs)
		return 0;

	/* check whether the automatic mic switch is available */
	if (spec->num_inputs == 2 &&
	    spec->adc_nid[AUTO_PIN_MIC] && spec->adc_nid[AUTO_PIN_FRONT_MIC]) {
		if (is_ext_mic(codec, cfg->input_pins[AUTO_PIN_FRONT_MIC])) {
			if (!is_ext_mic(codec, cfg->input_pins[AUTO_PIN_MIC])) {
				spec->mic_detect = 1;
				spec->automic_idx = AUTO_PIN_FRONT_MIC;
			}
		} else {
			if (is_ext_mic(codec, cfg->input_pins[AUTO_PIN_MIC])) {
				spec->mic_detect = 1;
				spec->automic_idx = AUTO_PIN_MIC;
			}
		}
	}
	return 0;
}


static int parse_digital_output(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	hda_nid_t nid;

	if (!cfg->dig_outs)
		return 0;
	if (snd_hda_get_connections(codec, cfg->dig_out_pins[0], &nid, 1) < 1)
		return 0;
	spec->multiout.dig_out_nid = nid;
	spec->multiout.share_spdif = 1;
	if (cfg->dig_outs > 1 &&
	    snd_hda_get_connections(codec, cfg->dig_out_pins[1], &nid, 1) > 0) {
		spec->slave_dig_outs[0] = nid;
		codec->slave_dig_outs = spec->slave_dig_outs;
	}
	return 0;
}

static int parse_digital_input(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int idx;

	if (cfg->dig_in_pin)
		spec->dig_in = get_adc(codec, cfg->dig_in_pin, &idx);
	return 0;
}

/*
 * create mixer controls
 */

static const char *dir_sfx[2] = { "Playback", "Capture" };

static int add_mute(struct hda_codec *codec, const char *name, int index,
		    unsigned int pval, int dir, struct snd_kcontrol **kctlp)
{
	char tmp[44];
	struct snd_kcontrol_new knew =
		HDA_CODEC_MUTE_IDX(tmp, index, 0, 0, HDA_OUTPUT);
	knew.private_value = pval;
	snprintf(tmp, sizeof(tmp), "%s %s Switch", name, dir_sfx[dir]);
	*kctlp = snd_ctl_new1(&knew, codec);
	return snd_hda_ctl_add(codec, *kctlp);
}

static int add_volume(struct hda_codec *codec, const char *name,
		      int index, unsigned int pval, int dir,
		      struct snd_kcontrol **kctlp)
{
	char tmp[32];
	struct snd_kcontrol_new knew =
		HDA_CODEC_VOLUME_IDX(tmp, index, 0, 0, HDA_OUTPUT);
	knew.private_value = pval;
	snprintf(tmp, sizeof(tmp), "%s %s Volume", name, dir_sfx[dir]);
	*kctlp = snd_ctl_new1(&knew, codec);
	return snd_hda_ctl_add(codec, *kctlp);
}

static void fix_volume_caps(struct hda_codec *codec, hda_nid_t dac)
{
	unsigned int caps;

	/* set the upper-limit for mixer amp to 0dB */
	caps = query_amp_caps(codec, dac, HDA_OUTPUT);
	caps &= ~(0x7f << AC_AMPCAP_NUM_STEPS_SHIFT);
	caps |= ((caps >> AC_AMPCAP_OFFSET_SHIFT) & 0x7f)
		<< AC_AMPCAP_NUM_STEPS_SHIFT;
	snd_hda_override_amp_caps(codec, dac, HDA_OUTPUT, caps);
}

static int add_vmaster(struct hda_codec *codec, hda_nid_t dac)
{
	struct cs_spec *spec = codec->spec;
	unsigned int tlv[4];
	int err;

	spec->vmaster_sw =
		snd_ctl_make_virtual_master("Master Playback Switch", NULL);
	err = snd_hda_ctl_add(codec, spec->vmaster_sw);
	if (err < 0)
		return err;

	snd_hda_set_vmaster_tlv(codec, dac, HDA_OUTPUT, tlv);
	spec->vmaster_vol =
		snd_ctl_make_virtual_master("Master Playback Volume", tlv);
	err = snd_hda_ctl_add(codec, spec->vmaster_vol);
	if (err < 0)
		return err;
	return 0;
}

static int add_output(struct hda_codec *codec, hda_nid_t dac, int idx,
		      int num_ctls, int type)
{
	struct cs_spec *spec = codec->spec;
	const char *name;
	int err, index;
	struct snd_kcontrol *kctl;
	static char *speakers[] = {
		"Front Speaker", "Surround Speaker", "Bass Speaker"
	};
	static char *line_outs[] = {
		"Front Line-Out", "Surround Line-Out", "Bass Line-Out"
	};

	fix_volume_caps(codec, dac);
	if (!spec->vmaster_sw) {
		err = add_vmaster(codec, dac);
		if (err < 0)
			return err;
	}

	index = 0;
	switch (type) {
	case AUTO_PIN_HP_OUT:
		name = "Headphone";
		index = idx;
		break;
	case AUTO_PIN_SPEAKER_OUT:
		if (num_ctls > 1)
			name = speakers[idx];
		else
			name = "Speaker";
		break;
	default:
		if (num_ctls > 1)
			name = line_outs[idx];
		else
			name = "Line-Out";
		break;
	}

	err = add_mute(codec, name, index,
		       HDA_COMPOSE_AMP_VAL(dac, 3, 0, HDA_OUTPUT), 0, &kctl);
	if (err < 0)
		return err;
	err = snd_ctl_add_slave(spec->vmaster_sw, kctl);
	if (err < 0)
		return err;

	err = add_volume(codec, name, index,
			 HDA_COMPOSE_AMP_VAL(dac, 3, 0, HDA_OUTPUT), 0, &kctl);
	if (err < 0)
		return err;
	err = snd_ctl_add_slave(spec->vmaster_vol, kctl);
	if (err < 0)
		return err;

	return 0;
}		

static int build_output(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i, err;

	for (i = 0; i < cfg->line_outs; i++) {
		err = add_output(codec, get_dac(codec, cfg->line_out_pins[i]),
				 i, cfg->line_outs, cfg->line_out_type);
		if (err < 0)
			return err;
	}
	for (i = 0; i < cfg->hp_outs; i++) {
		err = add_output(codec, get_dac(codec, cfg->hp_pins[i]),
				 i, cfg->hp_outs, AUTO_PIN_HP_OUT);
		if (err < 0)
			return err;
	}
	for (i = 0; i < cfg->speaker_outs; i++) {
		err = add_output(codec, get_dac(codec, cfg->speaker_pins[i]),
				 i, cfg->speaker_outs, AUTO_PIN_SPEAKER_OUT);
		if (err < 0)
			return err;
	}
	return 0;
}

/*
 */

static struct snd_kcontrol_new cs_capture_ctls[] = {
	HDA_BIND_SW("Capture Switch", 0),
	HDA_BIND_VOL("Capture Volume", 0),
};

static int change_cur_input(struct hda_codec *codec, unsigned int idx,
			    int force)
{
	struct cs_spec *spec = codec->spec;
	
	if (spec->cur_input == idx && !force)
		return 0;
	if (spec->cur_adc && spec->cur_adc != spec->adc_nid[idx]) {
		/* stream is running, let's swap the current ADC */
		snd_hda_codec_cleanup_stream(codec, spec->cur_adc);
		spec->cur_adc = spec->adc_nid[idx];
		snd_hda_codec_setup_stream(codec, spec->cur_adc,
					   spec->cur_adc_stream_tag, 0,
					   spec->cur_adc_format);
	}
	snd_hda_codec_write(codec, spec->cur_adc, 0,
			    AC_VERB_SET_CONNECT_SEL,
			    spec->adc_idx[idx]);
	spec->cur_input = idx;
	return 1;
}

static int cs_capture_source_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cs_spec *spec = codec->spec;
	unsigned int idx;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = spec->num_inputs;
	if (uinfo->value.enumerated.item >= spec->num_inputs)
		uinfo->value.enumerated.item = spec->num_inputs - 1;
	idx = spec->input_idx[uinfo->value.enumerated.item];
	strcpy(uinfo->value.enumerated.name, auto_pin_cfg_labels[idx]);
	return 0;
}

static int cs_capture_source_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cs_spec *spec = codec->spec;
	ucontrol->value.enumerated.item[0] = spec->capsrc_idx[spec->cur_input];
	return 0;
}

static int cs_capture_source_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cs_spec *spec = codec->spec;
	unsigned int idx = ucontrol->value.enumerated.item[0];

	if (idx >= spec->num_inputs)
		return -EINVAL;
	idx = spec->input_idx[idx];
	return change_cur_input(codec, idx, 0);
}

static struct snd_kcontrol_new cs_capture_source = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Source",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = cs_capture_source_info,
	.get = cs_capture_source_get,
	.put = cs_capture_source_put,
};

static struct hda_bind_ctls *make_bind_capture(struct hda_codec *codec,
					       struct hda_ctl_ops *ops)
{
	struct cs_spec *spec = codec->spec;
	struct hda_bind_ctls *bind;
	int i, n;

	bind = kzalloc(sizeof(*bind) + sizeof(long) * (spec->num_inputs + 1),
		       GFP_KERNEL);
	if (!bind)
		return NULL;
	bind->ops = ops;
	n = 0;
	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (!spec->adc_nid[i])
			continue;
		bind->values[n++] =
			HDA_COMPOSE_AMP_VAL(spec->adc_nid[i], 3,
					    spec->adc_idx[i], HDA_INPUT);
	}
	return bind;
}

static int build_input(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	int i, err;

	if (!spec->num_inputs)
		return 0;

	/* make bind-capture */
	spec->capture_bind[0] = make_bind_capture(codec, &snd_hda_bind_sw);
	spec->capture_bind[1] = make_bind_capture(codec, &snd_hda_bind_vol);
	for (i = 0; i < 2; i++) {
		struct snd_kcontrol *kctl;
		if (!spec->capture_bind[i])
			return -ENOMEM;
		kctl = snd_ctl_new1(&cs_capture_ctls[i], codec);
		if (!kctl)
			return -ENOMEM;
		kctl->private_value = (long)spec->capture_bind[i];
		err = snd_hda_ctl_add(codec, kctl);
		if (err < 0)
			return err;
	}
	
	if (spec->num_inputs > 1 && !spec->mic_detect) {
		err = snd_hda_ctl_add(codec,
				      snd_ctl_new1(&cs_capture_source, codec));
		if (err < 0)
			return err;
	}

	return 0;
}

/*
 */

static int build_digital_output(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	int err;

	if (!spec->multiout.dig_out_nid)
		return 0;

	err = snd_hda_create_spdif_out_ctls(codec, spec->multiout.dig_out_nid);
	if (err < 0)
		return err;
	err = snd_hda_create_spdif_share_sw(codec, &spec->multiout);
	if (err < 0)
		return err;
	return 0;
}

static int build_digital_input(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	if (spec->dig_in)
		return snd_hda_create_spdif_in_ctls(codec, spec->dig_in);
	return 0;
}

/*
 * auto-mute and auto-mic switching
 */

static void cs_automute(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int caps, present, hp_present;
	hda_nid_t nid;
	int i;

	hp_present = 0;
	for (i = 0; i < cfg->hp_outs; i++) {
		nid = cfg->hp_pins[i];
		caps = snd_hda_query_pin_caps(codec, nid);
		if (!(caps & AC_PINCAP_PRES_DETECT))
			continue;
		if (caps & AC_PINCAP_TRIG_REQ)
			snd_hda_codec_read(codec, nid, 0,
					   AC_VERB_SET_PIN_SENSE, 0);
		present = snd_hda_codec_read(codec, nid, 0,
					     AC_VERB_GET_PIN_SENSE, 0);
		hp_present |= (present & AC_PINSENSE_PRESENCE) != 0;
		if (hp_present)
			break;
	}
	for (i = 0; i < cfg->speaker_outs; i++) {
		nid = cfg->speaker_pins[i];
		snd_hda_codec_write(codec, nid, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL,
				    hp_present ? 0 : PIN_OUT);
	}
	if (spec->board_config == CS420X_MBP55) {
		unsigned int gpio = hp_present ? 0x02 : 0x08;
		snd_hda_codec_write(codec, 0x01, 0,
				    AC_VERB_SET_GPIO_DATA, gpio);
	}
}

static void cs_automic(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	hda_nid_t nid;
	unsigned int caps, present;
	
	nid = cfg->input_pins[spec->automic_idx];
	caps = snd_hda_query_pin_caps(codec, nid);
	if (caps & AC_PINCAP_TRIG_REQ)
		snd_hda_codec_read(codec, nid, 0, AC_VERB_SET_PIN_SENSE, 0);
	present = snd_hda_codec_read(codec, nid, 0,
				     AC_VERB_GET_PIN_SENSE, 0);
	if (present & AC_PINSENSE_PRESENCE)
		change_cur_input(codec, spec->automic_idx, 0);
	else {
		unsigned int imic = (spec->automic_idx == AUTO_PIN_MIC) ?
			AUTO_PIN_FRONT_MIC : AUTO_PIN_MIC;
		change_cur_input(codec, imic, 0);
	}
}

/*
 */

static void init_output(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	/* mute first */
	for (i = 0; i < spec->multiout.num_dacs; i++)
		snd_hda_codec_write(codec, spec->multiout.dac_nids[i], 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);
	if (spec->multiout.hp_nid)
		snd_hda_codec_write(codec, spec->multiout.hp_nid, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);
	for (i = 0; i < ARRAY_SIZE(spec->multiout.extra_out_nid); i++) {
		if (!spec->multiout.extra_out_nid[i])
			break;
		snd_hda_codec_write(codec, spec->multiout.extra_out_nid[i], 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE);
	}

	/* set appropriate pin controls */
	for (i = 0; i < cfg->line_outs; i++)
		snd_hda_codec_write(codec, cfg->line_out_pins[i], 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
	for (i = 0; i < cfg->hp_outs; i++) {
		hda_nid_t nid = cfg->hp_pins[i];
		snd_hda_codec_write(codec, nid, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP);
		if (!cfg->speaker_outs)
			continue;
		if (get_wcaps(codec, nid) & AC_WCAP_UNSOL_CAP) {
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_UNSOLICITED_ENABLE,
					    AC_USRSP_EN | HP_EVENT);
			spec->hp_detect = 1;
		}
	}
	for (i = 0; i < cfg->speaker_outs; i++)
		snd_hda_codec_write(codec, cfg->speaker_pins[i], 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
	if (spec->hp_detect)
		cs_automute(codec);
}

static void init_input(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int coef;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		unsigned int ctl;
		hda_nid_t pin = cfg->input_pins[i];
		if (!pin || !spec->adc_nid[i])
			continue;
		/* set appropriate pin control and mute first */
		ctl = PIN_IN;
		if (i <= AUTO_PIN_FRONT_MIC) {
			unsigned int caps = snd_hda_query_pin_caps(codec, pin);
			caps >>= AC_PINCAP_VREF_SHIFT;
			if (caps & AC_PINCAP_VREF_80)
				ctl = PIN_VREF80;
		}
		snd_hda_codec_write(codec, pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, ctl);
		snd_hda_codec_write(codec, spec->adc_nid[i], 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_IN_MUTE(spec->adc_idx[i]));
		if (spec->mic_detect && spec->automic_idx == i)
			snd_hda_codec_write(codec, pin, 0,
					    AC_VERB_SET_UNSOLICITED_ENABLE,
					    AC_USRSP_EN | MIC_EVENT);
	}
	change_cur_input(codec, spec->cur_input, 1);
	if (spec->mic_detect)
		cs_automic(codec);

	coef = 0x000a; /* ADC1/2 - Digital and Analog Soft Ramp */
	if (is_active_pin(codec, CS_DMIC2_PIN_NID))
		coef |= 0x0500; /* DMIC2 enable 2 channels, disable GPIO1 */
	if (is_active_pin(codec, CS_DMIC1_PIN_NID))
		coef |= 0x1800; /* DMIC1 enable 2 channels, disable GPIO0 
				 * No effect if SPDIF_OUT2 is slected in 
				 * IDX_SPDIF_CTL.
				  */
	cs_vendor_coef_set(codec, IDX_ADC_CFG, coef);
}

static struct hda_verb cs_coef_init_verbs[] = {
	{0x11, AC_VERB_SET_PROC_STATE, 1},
	{0x11, AC_VERB_SET_COEF_INDEX, IDX_DAC_CFG},
	{0x11, AC_VERB_SET_PROC_COEF,
	 (0x002a /* DAC1/2/3 SZCMode Soft Ramp */
	  | 0x0040 /* Mute DACs on FIFO error */
	  | 0x1000 /* Enable DACs High Pass Filter */
	  | 0x0400 /* Disable Coefficient Auto increment */
	  )},
	/* Beep */
	{0x11, AC_VERB_SET_COEF_INDEX, IDX_DAC_CFG},
	{0x11, AC_VERB_SET_PROC_COEF, 0x0007}, /* Enable Beep thru DAC1/2/3 */

	{} /* terminator */
};

/* SPDIF setup */
static void init_digital(struct hda_codec *codec)
{
	unsigned int coef;

	coef = 0x0002; /* SRC_MUTE soft-mute on SPDIF (if no lock) */
	coef |= 0x0008; /* Replace with mute on error */
	if (is_active_pin(codec, CS_DIG_OUT2_PIN_NID))
		coef |= 0x4000; /* RX to TX1 or TX2 Loopthru / SPDIF2
				 * SPDIF_OUT2 is shared with GPIO1 and
				 * DMIC_SDA2.
				 */
	cs_vendor_coef_set(codec, IDX_SPDIF_CTL, coef);
}

static int cs_init(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;

	snd_hda_sequence_write(codec, cs_coef_init_verbs);

	if (spec->gpio_mask) {
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_MASK,
				    spec->gpio_mask);
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DIRECTION,
				    spec->gpio_dir);
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA,
				    spec->gpio_data);
	}

	init_output(codec);
	init_input(codec);
	init_digital(codec);
	return 0;
}

static int cs_build_controls(struct hda_codec *codec)
{
	int err;

	err = build_output(codec);
	if (err < 0)
		return err;
	err = build_input(codec);
	if (err < 0)
		return err;
	err = build_digital_output(codec);
	if (err < 0)
		return err;
	err = build_digital_input(codec);
	if (err < 0)
		return err;
	return cs_init(codec);
}

static void cs_free(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	kfree(spec->capture_bind[0]);
	kfree(spec->capture_bind[1]);
	kfree(codec->spec);
}

static void cs_unsol_event(struct hda_codec *codec, unsigned int res)
{
	switch ((res >> 26) & 0x7f) {
	case HP_EVENT:
		cs_automute(codec);
		break;
	case MIC_EVENT:
		cs_automic(codec);
		break;
	}
}

static struct hda_codec_ops cs_patch_ops = {
	.build_controls = cs_build_controls,
	.build_pcms = cs_build_pcms,
	.init = cs_init,
	.free = cs_free,
	.unsol_event = cs_unsol_event,
};

static int cs_parse_auto_config(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	int err;

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);
	if (err < 0)
		return err;

	err = parse_output(codec);
	if (err < 0)
		return err;
	err = parse_input(codec);
	if (err < 0)
		return err;
	err = parse_digital_output(codec);
	if (err < 0)
		return err;
	err = parse_digital_input(codec);
	if (err < 0)
		return err;
	return 0;
}

static const char *cs420x_models[CS420X_MODELS] = {
	[CS420X_MBP55] = "mbp55",
	[CS420X_AUTO] = "auto",
};


static struct snd_pci_quirk cs420x_cfg_tbl[] = {
	SND_PCI_QUIRK(0x10de, 0xcb79, "MacBookPro 5,5", CS420X_MBP55),
	{} /* terminator */
};

struct cs_pincfg {
	hda_nid_t nid;
	u32 val;
};

static struct cs_pincfg mbp55_pincfgs[] = {
	{ 0x09, 0x012b4030 },
	{ 0x0a, 0x90100121 },
	{ 0x0b, 0x90100120 },
	{ 0x0c, 0x400000f0 },
	{ 0x0d, 0x90a00110 },
	{ 0x0e, 0x400000f0 },
	{ 0x0f, 0x400000f0 },
	{ 0x10, 0x014be040 },
	{ 0x12, 0x400000f0 },
	{ 0x15, 0x400000f0 },
	{} /* terminator */
};

static struct cs_pincfg *cs_pincfgs[CS420X_MODELS] = {
	[CS420X_MBP55] = mbp55_pincfgs,
};

static void fix_pincfg(struct hda_codec *codec, int model)
{
	const struct cs_pincfg *cfg = cs_pincfgs[model];
	if (!cfg)
		return;
	for (; cfg->nid; cfg++)
		snd_hda_codec_set_pincfg(codec, cfg->nid, cfg->val);
}


static int patch_cs420x(struct hda_codec *codec)
{
	struct cs_spec *spec;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	codec->spec = spec;

	spec->board_config =
		snd_hda_check_board_config(codec, CS420X_MODELS,
					   cs420x_models, cs420x_cfg_tbl);
	if (spec->board_config >= 0)
		fix_pincfg(codec, spec->board_config);

	switch (spec->board_config) {
	case CS420X_MBP55:
		/* GPIO1 = headphones */
		/* GPIO3 = speakers */
		spec->gpio_mask = 0x0a;
		spec->gpio_dir = 0x0a;
		break;
	}

	err = cs_parse_auto_config(codec);
	if (err < 0)
		goto error;

	codec->patch_ops = cs_patch_ops;

	return 0;

 error:
	kfree(codec->spec);
	codec->spec = NULL;
	return err;
}


/*
 * patch entries
 */
static struct hda_codec_preset snd_hda_preset_cirrus[] = {
	{ .id = 0x10134206, .name = "CS4206", .patch = patch_cs420x },
	{ .id = 0x10134207, .name = "CS4207", .patch = patch_cs420x },
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:10134206");
MODULE_ALIAS("snd-hda-codec-id:10134207");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cirrus Logic HD-audio codec");

static struct hda_codec_preset_list cirrus_list = {
	.preset = snd_hda_preset_cirrus,
	.owner = THIS_MODULE,
};

static int __init patch_cirrus_init(void)
{
	return snd_hda_add_codec_preset(&cirrus_list);
}

static void __exit patch_cirrus_exit(void)
{
	snd_hda_delete_codec_preset(&cirrus_list);
}

module_init(patch_cirrus_init)
module_exit(patch_cirrus_exit)
