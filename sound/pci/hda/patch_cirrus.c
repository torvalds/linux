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
#include <linux/module.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"
#include <sound/tlv.h>

/*
 */

struct cs_spec {
	struct hda_gen_spec gen;

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

	const struct hda_bind_ctls *capture_bind[2];

	unsigned int gpio_mask;
	unsigned int gpio_dir;
	unsigned int gpio_data;
	unsigned int gpio_eapd_hp; /* EAPD GPIO bit for headphones */
	unsigned int gpio_eapd_speaker; /* EAPD GPIO bit for speakers */

	struct hda_pcm pcm_rec[2];	/* PCM information */

	unsigned int hp_detect:1;
	unsigned int mic_detect:1;
	unsigned int speaker_2_1:1;
	/* CS421x */
	unsigned int spdif_detect:1;
	unsigned int sense_b:1;
	hda_nid_t vendor_nid;
	struct hda_input_mux input_mux;
	unsigned int last_input;
};

/* available models with CS420x */
enum {
	CS420X_MBP53,
	CS420X_MBP55,
	CS420X_IMAC27,
	CS420X_GPIO_13,
	CS420X_GPIO_23,
	CS420X_MBP101,
	CS420X_MBP101_COEF,
	CS420X_AUTO,
	/* aliases */
	CS420X_IMAC27_122 = CS420X_GPIO_23,
	CS420X_APPLE = CS420X_GPIO_13,
};

/* CS421x boards */
enum {
	CS421X_CDB4210,
	CS421X_SENSE_B,
};

/* Vendor-specific processing widget */
#define CS420X_VENDOR_NID	0x11
#define CS_DIG_OUT1_PIN_NID	0x10
#define CS_DIG_OUT2_PIN_NID	0x15
#define CS_DMIC1_PIN_NID	0x0e
#define CS_DMIC2_PIN_NID	0x12

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

/*
 * Cirrus Logic CS4210
 *
 * 1 DAC => HP(sense) / Speakers,
 * 1 ADC <= LineIn(sense) / MicIn / DMicIn,
 * 1 SPDIF OUT => SPDIF Trasmitter(sense)
*/
#define CS4210_DAC_NID		0x02
#define CS4210_ADC_NID		0x03
#define CS4210_VENDOR_NID	0x0B
#define CS421X_DMIC_PIN_NID	0x09 /* Port E */
#define CS421X_SPDIF_PIN_NID	0x0A /* Port H */

#define CS421X_IDX_DEV_CFG	0x01
#define CS421X_IDX_ADC_CFG	0x02
#define CS421X_IDX_DAC_CFG	0x03
#define CS421X_IDX_SPK_CTL	0x04

#define SPDIF_EVENT		0x04

/* Cirrus Logic CS4213 is like CS4210 but does not have SPDIF input/output */
#define CS4213_VENDOR_NID	0x09


static inline int cs_vendor_coef_get(struct hda_codec *codec, unsigned int idx)
{
	struct cs_spec *spec = codec->spec;
	snd_hda_codec_write(codec, spec->vendor_nid, 0,
			    AC_VERB_SET_COEF_INDEX, idx);
	return snd_hda_codec_read(codec, spec->vendor_nid, 0,
				  AC_VERB_GET_PROC_COEF, 0);
}

static inline void cs_vendor_coef_set(struct hda_codec *codec, unsigned int idx,
				      unsigned int coef)
{
	struct cs_spec *spec = codec->spec;
	snd_hda_codec_write(codec, spec->vendor_nid, 0,
			    AC_VERB_SET_COEF_INDEX, idx);
	snd_hda_codec_write(codec, spec->vendor_nid, 0,
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

static void cs_update_input_select(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	if (spec->cur_adc)
		snd_hda_codec_write(codec, spec->cur_adc, 0,
				    AC_VERB_SET_CONNECT_SEL,
				    spec->adc_idx[spec->cur_input]);
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
	cs_update_input_select(codec);
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
static const struct hda_pcm_stream cs_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.open = cs_playback_pcm_open,
		.prepare = cs_playback_pcm_prepare,
		.cleanup = cs_playback_pcm_cleanup
	},
};

static const struct hda_pcm_stream cs_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.prepare = cs_capture_pcm_prepare,
		.cleanup = cs_capture_pcm_cleanup
	},
};

static const struct hda_pcm_stream cs_pcm_digital_playback = {
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

static const struct hda_pcm_stream cs_pcm_digital_capture = {
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
	if (spec->speaker_2_1)
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].chmap =
			snd_pcm_2_1_chmaps;
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
	hda_nid_t pin = cfg->inputs[idx].pin;
	unsigned int val;
	if (!is_jack_detectable(codec, pin))
		return 0;
	val = snd_hda_codec_get_pincfg(codec, pin);
	return (snd_hda_get_input_pin_attr(val) != INPUT_PIN_ATTR_INT);
}

static hda_nid_t get_adc(struct hda_codec *codec, hda_nid_t pin,
			 unsigned int *idxp)
{
	int i, idx;
	hda_nid_t nid;

	nid = codec->start_nid;
	for (i = 0; i < codec->num_nodes; i++, nid++) {
		unsigned int type;
		type = get_wcaps_type(get_wcaps(codec, nid));
		if (type != AC_WID_AUD_IN)
			continue;
		idx = snd_hda_get_conn_index(codec, nid, pin, false);
		if (idx >= 0) {
			*idxp = idx;
			return nid;
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

	if (cfg->line_out_type == AUTO_PIN_SPEAKER_OUT && i == 2)
		spec->speaker_2_1 = 1; /* assume 2.1 speakers */

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
		memset(cfg->line_out_pins, 0, sizeof(cfg->line_out_pins));
	}

	return 0;
}

static int parse_input(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t pin = cfg->inputs[i].pin;
		spec->input_idx[spec->num_inputs] = i;
		spec->capsrc_idx[i] = spec->num_inputs++;
		spec->cur_input = i;
		spec->adc_nid[i] = get_adc(codec, pin, &spec->adc_idx[i]);
	}
	if (!spec->num_inputs)
		return 0;

	/* check whether the automatic mic switch is available */
	if (spec->num_inputs == 2 &&
	    cfg->inputs[0].type == AUTO_PIN_MIC &&
	    cfg->inputs[1].type == AUTO_PIN_MIC) {
		if (is_ext_mic(codec, cfg->inputs[0].pin)) {
			if (!is_ext_mic(codec, cfg->inputs[1].pin)) {
				spec->mic_detect = 1;
				spec->automic_idx = 0;
			}
		} else {
			if (is_ext_mic(codec, cfg->inputs[1].pin)) {
				spec->mic_detect = 1;
				spec->automic_idx = 1;
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

static const char * const dir_sfx[2] = { "Playback", "Capture" };

static int add_mute(struct hda_codec *codec, const char *name, int index,
		    unsigned int pval, int dir, struct snd_kcontrol **kctlp)
{
	char tmp[44];
	struct snd_kcontrol_new knew =
		HDA_CODEC_MUTE_IDX(tmp, index, 0, 0, HDA_OUTPUT);
	knew.private_value = pval;
	snprintf(tmp, sizeof(tmp), "%s %s Switch", name, dir_sfx[dir]);
	*kctlp = snd_ctl_new1(&knew, codec);
	(*kctlp)->id.subdevice = HDA_SUBDEV_AMP_FLAG;
	return snd_hda_ctl_add(codec, 0, *kctlp);
}

static int add_volume(struct hda_codec *codec, const char *name,
		      int index, unsigned int pval, int dir,
		      struct snd_kcontrol **kctlp)
{
	char tmp[44];
	struct snd_kcontrol_new knew =
		HDA_CODEC_VOLUME_IDX(tmp, index, 0, 0, HDA_OUTPUT);
	knew.private_value = pval;
	snprintf(tmp, sizeof(tmp), "%s %s Volume", name, dir_sfx[dir]);
	*kctlp = snd_ctl_new1(&knew, codec);
	(*kctlp)->id.subdevice = HDA_SUBDEV_AMP_FLAG;
	return snd_hda_ctl_add(codec, 0, *kctlp);
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
	err = snd_hda_ctl_add(codec, dac, spec->vmaster_sw);
	if (err < 0)
		return err;

	snd_hda_set_vmaster_tlv(codec, dac, HDA_OUTPUT, tlv);
	spec->vmaster_vol =
		snd_ctl_make_virtual_master("Master Playback Volume", tlv);
	err = snd_hda_ctl_add(codec, dac, spec->vmaster_vol);
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
	static const char * const speakers[] = {
		"Front Speaker", "Surround Speaker", "Bass Speaker"
	};
	static const char * const line_outs[] = {
		"Front Line Out", "Surround Line Out", "Bass Line Out"
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
		if (spec->speaker_2_1)
			name = idx ? "Bass Speaker" : "Speaker";
		else if (num_ctls > 1)
			name = speakers[idx];
		else
			name = "Speaker";
		break;
	default:
		if (num_ctls > 1)
			name = line_outs[idx];
		else
			name = "Line Out";
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

static const struct snd_kcontrol_new cs_capture_ctls[] = {
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
		__snd_hda_codec_cleanup_stream(codec, spec->cur_adc, 1);
		spec->cur_adc = spec->adc_nid[idx];
		snd_hda_codec_setup_stream(codec, spec->cur_adc,
					   spec->cur_adc_stream_tag, 0,
					   spec->cur_adc_format);
	}
	spec->cur_input = idx;
	cs_update_input_select(codec);
	return 1;
}

static int cs_capture_source_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int idx;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = spec->num_inputs;
	if (uinfo->value.enumerated.item >= spec->num_inputs)
		uinfo->value.enumerated.item = spec->num_inputs - 1;
	idx = spec->input_idx[uinfo->value.enumerated.item];
	snd_hda_get_pin_label(codec, cfg->inputs[idx].pin, cfg,
			      uinfo->value.enumerated.name,
			      sizeof(uinfo->value.enumerated.name), NULL);
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

static const struct snd_kcontrol_new cs_capture_source = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Source",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = cs_capture_source_info,
	.get = cs_capture_source_get,
	.put = cs_capture_source_put,
};

static const struct hda_bind_ctls *make_bind_capture(struct hda_codec *codec,
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

/* add a (input-boost) volume control to the given input pin */
static int add_input_volume_control(struct hda_codec *codec,
				    struct auto_pin_cfg *cfg,
				    int item)
{
	hda_nid_t pin = cfg->inputs[item].pin;
	u32 caps;
	const char *label;
	struct snd_kcontrol *kctl;
		
	if (!(get_wcaps(codec, pin) & AC_WCAP_IN_AMP))
		return 0;
	caps = query_amp_caps(codec, pin, HDA_INPUT);
	caps = (caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT;
	if (caps <= 1)
		return 0;
	label = hda_get_autocfg_input_label(codec, cfg, item);
	return add_volume(codec, label, 0,
			  HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_INPUT), 1, &kctl);
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
		int n;
		if (!spec->capture_bind[i])
			return -ENOMEM;
		kctl = snd_ctl_new1(&cs_capture_ctls[i], codec);
		if (!kctl)
			return -ENOMEM;
		kctl->private_value = (long)spec->capture_bind[i];
		err = snd_hda_ctl_add(codec, 0, kctl);
		if (err < 0)
			return err;
		for (n = 0; n < AUTO_PIN_LAST; n++) {
			if (!spec->adc_nid[n])
				continue;
			err = snd_hda_add_nid(codec, kctl, 0, spec->adc_nid[n]);
			if (err < 0)
				return err;
		}
	}
	
	if (spec->num_inputs > 1 && !spec->mic_detect) {
		err = snd_hda_ctl_add(codec, 0,
				      snd_ctl_new1(&cs_capture_source, codec));
		if (err < 0)
			return err;
	}

	for (i = 0; i < spec->num_inputs; i++) {
		err = add_input_volume_control(codec, &spec->autocfg, i);
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

	err = snd_hda_create_dig_out_ctls(codec, spec->multiout.dig_out_nid,
					  spec->multiout.dig_out_nid,
					  spec->pcm_rec[1].pcm_type);
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
 * CS421x auto-output redirecting
 * HP/SPK/SPDIF
 */

static void cs_automute(struct hda_codec *codec, struct hda_jack_tbl *tbl)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int hp_present;
	unsigned int spdif_present;
	hda_nid_t nid;
	int i;

	spdif_present = 0;
	if (cfg->dig_outs) {
		nid = cfg->dig_out_pins[0];
		if (is_jack_detectable(codec, nid)) {
			/*
			TODO: SPDIF output redirect when SENSE_B is enabled.
			Shared (SENSE_A) jack (e.g HP/mini-TOSLINK)
			assumed.
			*/
			if (snd_hda_jack_detect(codec, nid)
				/* && spec->sense_b */)
				spdif_present = 1;
		}
	}

	hp_present = 0;
	for (i = 0; i < cfg->hp_outs; i++) {
		nid = cfg->hp_pins[i];
		if (!is_jack_detectable(codec, nid))
			continue;
		hp_present = snd_hda_jack_detect(codec, nid);
		if (hp_present)
			break;
	}

	/* mute speakers if spdif or hp jack is plugged in */
	for (i = 0; i < cfg->speaker_outs; i++) {
		int pin_ctl = hp_present ? 0 : PIN_OUT;
		/* detect on spdif is specific to CS4210 */
		if (spdif_present && (spec->vendor_nid == CS4210_VENDOR_NID))
			pin_ctl = 0;

		nid = cfg->speaker_pins[i];
		snd_hda_set_pin_ctl(codec, nid, pin_ctl);
	}
	if (spec->gpio_eapd_hp) {
		unsigned int gpio = hp_present ?
			spec->gpio_eapd_hp : spec->gpio_eapd_speaker;
		snd_hda_codec_write(codec, 0x01, 0,
				    AC_VERB_SET_GPIO_DATA, gpio);
	}

	/* specific to CS4210 */
	if (spec->vendor_nid == CS4210_VENDOR_NID) {
		/* mute HPs if spdif jack (SENSE_B) is present */
		for (i = 0; i < cfg->hp_outs; i++) {
			nid = cfg->hp_pins[i];
			snd_hda_set_pin_ctl(codec, nid,
				(spdif_present && spec->sense_b) ? 0 : PIN_HP);
		}

		/* SPDIF TX on/off */
		if (cfg->dig_outs) {
			nid = cfg->dig_out_pins[0];
			snd_hda_set_pin_ctl(codec, nid,
				spdif_present ? PIN_OUT : 0);

		}
		/* Update board GPIOs if neccessary ... */
	}
}

/*
 * Auto-input redirect for CS421x
 * Switch max 3 inputs of a single ADC (nid 3)
*/

static void cs_automic(struct hda_codec *codec, struct hda_jack_tbl *tbl)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	hda_nid_t nid;
	unsigned int present;

	nid = cfg->inputs[spec->automic_idx].pin;
	present = snd_hda_jack_detect(codec, nid);

	/* specific to CS421x, single ADC */
	if (spec->vendor_nid == CS420X_VENDOR_NID) {
		if (present)
			change_cur_input(codec, spec->automic_idx, 0);
		else
			change_cur_input(codec, !spec->automic_idx, 0);
	} else {
		if (present) {
			if (spec->cur_input != spec->automic_idx) {
				spec->last_input = spec->cur_input;
				spec->cur_input = spec->automic_idx;
			}
		} else  {
			spec->cur_input = spec->last_input;
		}
		cs_update_input_select(codec);
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
		snd_hda_set_pin_ctl(codec, cfg->line_out_pins[i], PIN_OUT);
	/* HP */
	for (i = 0; i < cfg->hp_outs; i++) {
		hda_nid_t nid = cfg->hp_pins[i];
		snd_hda_set_pin_ctl(codec, nid, PIN_HP);
		if (!cfg->speaker_outs)
			continue;
		if (get_wcaps(codec, nid) & AC_WCAP_UNSOL_CAP) {
			snd_hda_jack_detect_enable_callback(codec, nid, HP_EVENT, cs_automute);
			spec->hp_detect = 1;
		}
	}

	/* Speaker */
	for (i = 0; i < cfg->speaker_outs; i++)
		snd_hda_set_pin_ctl(codec, cfg->speaker_pins[i], PIN_OUT);

	/* SPDIF is enabled on presence detect for CS421x */
	if (spec->hp_detect || spec->spdif_detect)
		cs_automute(codec, NULL);
}

static void init_input(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int coef;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		unsigned int ctl;
		hda_nid_t pin = cfg->inputs[i].pin;
		if (!spec->adc_nid[i])
			continue;
		/* set appropriate pin control and mute first */
		ctl = PIN_IN;
		if (cfg->inputs[i].type == AUTO_PIN_MIC)
			ctl |= snd_hda_get_default_vref(codec, pin);
		snd_hda_set_pin_ctl(codec, pin, ctl);
		snd_hda_codec_write(codec, spec->adc_nid[i], 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_IN_MUTE(spec->adc_idx[i]));
		if (spec->mic_detect && spec->automic_idx == i)
			snd_hda_jack_detect_enable_callback(codec, pin, MIC_EVENT, cs_automic);
	}
	/* CS420x has multiple ADC, CS421x has single ADC */
	if (spec->vendor_nid == CS420X_VENDOR_NID) {
		change_cur_input(codec, spec->cur_input, 1);
		if (spec->mic_detect)
			cs_automic(codec, NULL);

		coef = cs_vendor_coef_get(codec, IDX_BEEP_CFG);
		if (is_active_pin(codec, CS_DMIC2_PIN_NID))
			coef |= 1 << 4; /* DMIC2 2 chan on, GPIO1 off */
		if (is_active_pin(codec, CS_DMIC1_PIN_NID))
			coef |= 1 << 3; /* DMIC1 2 chan on, GPIO0 off
					 * No effect if SPDIF_OUT2 is
					 * selected in IDX_SPDIF_CTL.
					*/

		cs_vendor_coef_set(codec, IDX_BEEP_CFG, coef);
	} else {
		if (spec->mic_detect)
			cs_automic(codec, NULL);
		else  {
			spec->cur_adc = spec->adc_nid[spec->cur_input];
			cs_update_input_select(codec);
		}
	}
}

static const struct hda_verb cs_coef_init_verbs[] = {
	{0x11, AC_VERB_SET_PROC_STATE, 1},
	{0x11, AC_VERB_SET_COEF_INDEX, IDX_DAC_CFG},
	{0x11, AC_VERB_SET_PROC_COEF,
	 (0x002a /* DAC1/2/3 SZCMode Soft Ramp */
	  | 0x0040 /* Mute DACs on FIFO error */
	  | 0x1000 /* Enable DACs High Pass Filter */
	  | 0x0400 /* Disable Coefficient Auto increment */
	  )},
	/* ADC1/2 - Digital and Analog Soft Ramp */
	{0x11, AC_VERB_SET_COEF_INDEX, IDX_ADC_CFG},
	{0x11, AC_VERB_SET_PROC_COEF, 0x000a},
	/* Beep */
	{0x11, AC_VERB_SET_COEF_INDEX, IDX_BEEP_CFG},
	{0x11, AC_VERB_SET_PROC_COEF, 0x0007}, /* Enable Beep thru DAC1/2/3 */

	{} /* terminator */
};

/* Errata: CS4207 rev C0/C1/C2 Silicon
 *
 * http://www.cirrus.com/en/pubs/errata/ER880C3.pdf
 *
 * 6. At high temperature (TA > +85°C), the digital supply current (IVD)
 * may be excessive (up to an additional 200 μA), which is most easily
 * observed while the part is being held in reset (RESET# active low).
 *
 * Root Cause: At initial powerup of the device, the logic that drives
 * the clock and write enable to the S/PDIF SRC RAMs is not properly
 * initialized.
 * Certain random patterns will cause a steady leakage current in those
 * RAM cells. The issue will resolve once the SRCs are used (turned on).
 *
 * Workaround: The following verb sequence briefly turns on the S/PDIF SRC
 * blocks, which will alleviate the issue.
 */

static const struct hda_verb cs_errata_init_verbs[] = {
	{0x01, AC_VERB_SET_POWER_STATE, 0x00}, /* AFG: D0 */
	{0x11, AC_VERB_SET_PROC_STATE, 0x01},  /* VPW: processing on */

	{0x11, AC_VERB_SET_COEF_INDEX, 0x0008},
	{0x11, AC_VERB_SET_PROC_COEF, 0x9999},
	{0x11, AC_VERB_SET_COEF_INDEX, 0x0017},
	{0x11, AC_VERB_SET_PROC_COEF, 0xa412},
	{0x11, AC_VERB_SET_COEF_INDEX, 0x0001},
	{0x11, AC_VERB_SET_PROC_COEF, 0x0009},

	{0x07, AC_VERB_SET_POWER_STATE, 0x00}, /* S/PDIF Rx: D0 */
	{0x08, AC_VERB_SET_POWER_STATE, 0x00}, /* S/PDIF Tx: D0 */

	{0x11, AC_VERB_SET_COEF_INDEX, 0x0017},
	{0x11, AC_VERB_SET_PROC_COEF, 0x2412},
	{0x11, AC_VERB_SET_COEF_INDEX, 0x0008},
	{0x11, AC_VERB_SET_PROC_COEF, 0x0000},
	{0x11, AC_VERB_SET_COEF_INDEX, 0x0001},
	{0x11, AC_VERB_SET_PROC_COEF, 0x0008},
	{0x11, AC_VERB_SET_PROC_STATE, 0x00},

#if 0 /* Don't to set to D3 as we are in power-up sequence */
	{0x07, AC_VERB_SET_POWER_STATE, 0x03}, /* S/PDIF Rx: D3 */
	{0x08, AC_VERB_SET_POWER_STATE, 0x03}, /* S/PDIF Tx: D3 */
	/*{0x01, AC_VERB_SET_POWER_STATE, 0x03},*/ /* AFG: D3 This is already handled */
#endif

	{} /* terminator */
};

static const struct hda_verb mbp101_init_verbs[] = {
	{0x11, AC_VERB_SET_COEF_INDEX, 0x0002},
	{0x11, AC_VERB_SET_PROC_COEF, 0x100a},
	{0x11, AC_VERB_SET_COEF_INDEX, 0x0004},
	{0x11, AC_VERB_SET_PROC_COEF, 0x000f},
	{}
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

	/* init_verb sequence for C0/C1/C2 errata*/
	snd_hda_sequence_write(codec, cs_errata_init_verbs);

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
	struct cs_spec *spec = codec->spec;
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
	err = cs_init(codec);
	if (err < 0)
		return err;

	err = snd_hda_jack_add_kctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	return 0;
}

static void cs_free(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	kfree(spec->capture_bind[0]);
	kfree(spec->capture_bind[1]);
	snd_hda_gen_free(&spec->gen);
	kfree(codec->spec);
}

static const struct hda_codec_ops cs_patch_ops = {
	.build_controls = cs_build_controls,
	.build_pcms = cs_build_pcms,
	.init = cs_init,
	.free = cs_free,
	.unsol_event = snd_hda_jack_unsol_event,
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

static const struct hda_model_fixup cs420x_models[] = {
	{ .id = CS420X_MBP53, .name = "mbp53" },
	{ .id = CS420X_MBP55, .name = "mbp55" },
	{ .id = CS420X_IMAC27, .name = "imac27" },
	{ .id = CS420X_IMAC27_122, .name = "imac27_122" },
	{ .id = CS420X_APPLE, .name = "apple" },
	{ .id = CS420X_MBP101, .name = "mbp101" },
	{}
};

static const struct snd_pci_quirk cs420x_fixup_tbl[] = {
	SND_PCI_QUIRK(0x10de, 0x0ac0, "MacBookPro 5,3", CS420X_MBP53),
	SND_PCI_QUIRK(0x10de, 0x0d94, "MacBookAir 3,1(2)", CS420X_MBP55),
	SND_PCI_QUIRK(0x10de, 0xcb79, "MacBookPro 5,5", CS420X_MBP55),
	SND_PCI_QUIRK(0x10de, 0xcb89, "MacBookPro 7,1", CS420X_MBP55),
	/* this conflicts with too many other models */
	/*SND_PCI_QUIRK(0x8086, 0x7270, "IMac 27 Inch", CS420X_IMAC27),*/

	/* codec SSID */
	SND_PCI_QUIRK(0x106b, 0x2000, "iMac 12,2", CS420X_IMAC27_122),
	SND_PCI_QUIRK(0x106b, 0x2800, "MacBookPro 10,1", CS420X_MBP101),
	SND_PCI_QUIRK_VENDOR(0x106b, "Apple", CS420X_APPLE),
	{} /* terminator */
};

static const struct hda_pintbl mbp53_pincfgs[] = {
	{ 0x09, 0x012b4050 },
	{ 0x0a, 0x90100141 },
	{ 0x0b, 0x90100140 },
	{ 0x0c, 0x018b3020 },
	{ 0x0d, 0x90a00110 },
	{ 0x0e, 0x400000f0 },
	{ 0x0f, 0x01cbe030 },
	{ 0x10, 0x014be060 },
	{ 0x12, 0x400000f0 },
	{ 0x15, 0x400000f0 },
	{} /* terminator */
};

static const struct hda_pintbl mbp55_pincfgs[] = {
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

static const struct hda_pintbl imac27_pincfgs[] = {
	{ 0x09, 0x012b4050 },
	{ 0x0a, 0x90100140 },
	{ 0x0b, 0x90100142 },
	{ 0x0c, 0x018b3020 },
	{ 0x0d, 0x90a00110 },
	{ 0x0e, 0x400000f0 },
	{ 0x0f, 0x01cbe030 },
	{ 0x10, 0x014be060 },
	{ 0x12, 0x01ab9070 },
	{ 0x15, 0x400000f0 },
	{} /* terminator */
};

static const struct hda_pintbl mbp101_pincfgs[] = {
	{ 0x0d, 0x40ab90f0 },
	{ 0x0e, 0x90a600f0 },
	{ 0x12, 0x50a600f0 },
	{} /* terminator */
};

static void cs420x_fixup_gpio_13(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct cs_spec *spec = codec->spec;
		spec->gpio_eapd_hp = 2; /* GPIO1 = headphones */
		spec->gpio_eapd_speaker = 8; /* GPIO3 = speakers */
		spec->gpio_mask = spec->gpio_dir =
			spec->gpio_eapd_hp | spec->gpio_eapd_speaker;
	}
}

static void cs420x_fixup_gpio_23(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		struct cs_spec *spec = codec->spec;
		spec->gpio_eapd_hp = 4; /* GPIO2 = headphones */
		spec->gpio_eapd_speaker = 8; /* GPIO3 = speakers */
		spec->gpio_mask = spec->gpio_dir =
			spec->gpio_eapd_hp | spec->gpio_eapd_speaker;
	}
}

static const struct hda_fixup cs420x_fixups[] = {
	[CS420X_MBP53] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = mbp53_pincfgs,
		.chained = true,
		.chain_id = CS420X_APPLE,
	},
	[CS420X_MBP55] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = mbp55_pincfgs,
		.chained = true,
		.chain_id = CS420X_GPIO_13,
	},
	[CS420X_IMAC27] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = imac27_pincfgs,
		.chained = true,
		.chain_id = CS420X_GPIO_13,
	},
	[CS420X_GPIO_13] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs420x_fixup_gpio_13,
	},
	[CS420X_GPIO_23] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs420x_fixup_gpio_23,
	},
	[CS420X_MBP101] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = mbp101_pincfgs,
		.chained = true,
		.chain_id = CS420X_MBP101_COEF,
	},
	[CS420X_MBP101_COEF] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = mbp101_init_verbs,
		.chained = true,
		.chain_id = CS420X_GPIO_13,
	},
};

static int patch_cs420x(struct hda_codec *codec)
{
	struct cs_spec *spec;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	codec->spec = spec;
	snd_hda_gen_init(&spec->gen);

	spec->vendor_nid = CS420X_VENDOR_NID;

	snd_hda_pick_fixup(codec, cs420x_models, cs420x_fixup_tbl,
			   cs420x_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = cs_parse_auto_config(codec);
	if (err < 0)
		goto error;

	codec->patch_ops = cs_patch_ops;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	cs_free(codec);
	codec->spec = NULL;
	return err;
}

/*
 * Cirrus Logic CS4210
 *
 * 1 DAC => HP(sense) / Speakers,
 * 1 ADC <= LineIn(sense) / MicIn / DMicIn,
 * 1 SPDIF OUT => SPDIF Trasmitter(sense)
*/

/* CS4210 board names */
static const struct hda_model_fixup cs421x_models[] = {
	{ .id = CS421X_CDB4210, .name = "cdb4210" },
	{}
};

static const struct snd_pci_quirk cs421x_fixup_tbl[] = {
	/* Test Intel board + CDB2410  */
	SND_PCI_QUIRK(0x8086, 0x5001, "DP45SG/CDB4210", CS421X_CDB4210),
	{} /* terminator */
};

/* CS4210 board pinconfigs */
/* Default CS4210 (CDB4210)*/
static const struct hda_pintbl cdb4210_pincfgs[] = {
	{ 0x05, 0x0321401f },
	{ 0x06, 0x90170010 },
	{ 0x07, 0x03813031 },
	{ 0x08, 0xb7a70037 },
	{ 0x09, 0xb7a6003e },
	{ 0x0a, 0x034510f0 },
	{} /* terminator */
};

/* Setup GPIO/SENSE for each board (if used) */
static void cs421x_fixup_sense_b(struct hda_codec *codec,
				 const struct hda_fixup *fix, int action)
{
	struct cs_spec *spec = codec->spec;
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->sense_b = 1;
}

static const struct hda_fixup cs421x_fixups[] = {
	[CS421X_CDB4210] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = cdb4210_pincfgs,
		.chained = true,
		.chain_id = CS421X_SENSE_B,
	},
	[CS421X_SENSE_B] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = cs421x_fixup_sense_b,
	}
};

static const struct hda_verb cs421x_coef_init_verbs[] = {
	{0x0B, AC_VERB_SET_PROC_STATE, 1},
	{0x0B, AC_VERB_SET_COEF_INDEX, CS421X_IDX_DEV_CFG},
	/*
	    Disable Coefficient Index Auto-Increment(DAI)=1,
	    PDREF=0
	*/
	{0x0B, AC_VERB_SET_PROC_COEF, 0x0001 },

	{0x0B, AC_VERB_SET_COEF_INDEX, CS421X_IDX_ADC_CFG},
	/* ADC SZCMode = Digital Soft Ramp */
	{0x0B, AC_VERB_SET_PROC_COEF, 0x0002 },

	{0x0B, AC_VERB_SET_COEF_INDEX, CS421X_IDX_DAC_CFG},
	{0x0B, AC_VERB_SET_PROC_COEF,
	 (0x0002 /* DAC SZCMode = Digital Soft Ramp */
	  | 0x0004 /* Mute DAC on FIFO error */
	  | 0x0008 /* Enable DAC High Pass Filter */
	  )},
	{} /* terminator */
};

/* Errata: CS4210 rev A1 Silicon
 *
 * http://www.cirrus.com/en/pubs/errata/
 *
 * Description:
 * 1. Performance degredation is present in the ADC.
 * 2. Speaker output is not completely muted upon HP detect.
 * 3. Noise is present when clipping occurs on the amplified
 *    speaker outputs.
 *
 * Workaround:
 * The following verb sequence written to the registers during
 * initialization will correct the issues listed above.
 */

static const struct hda_verb cs421x_coef_init_verbs_A1_silicon_fixes[] = {
	{0x0B, AC_VERB_SET_PROC_STATE, 0x01},  /* VPW: processing on */

	{0x0B, AC_VERB_SET_COEF_INDEX, 0x0006},
	{0x0B, AC_VERB_SET_PROC_COEF, 0x9999}, /* Test mode: on */

	{0x0B, AC_VERB_SET_COEF_INDEX, 0x000A},
	{0x0B, AC_VERB_SET_PROC_COEF, 0x14CB}, /* Chop double */

	{0x0B, AC_VERB_SET_COEF_INDEX, 0x0011},
	{0x0B, AC_VERB_SET_PROC_COEF, 0xA2D0}, /* Increase ADC current */

	{0x0B, AC_VERB_SET_COEF_INDEX, 0x001A},
	{0x0B, AC_VERB_SET_PROC_COEF, 0x02A9}, /* Mute speaker */

	{0x0B, AC_VERB_SET_COEF_INDEX, 0x001B},
	{0x0B, AC_VERB_SET_PROC_COEF, 0X1006}, /* Remove noise */

	{} /* terminator */
};

/* Speaker Amp Gain is controlled by the vendor widget's coef 4 */
static const DECLARE_TLV_DB_SCALE(cs421x_speaker_boost_db_scale, 900, 300, 0);

static int cs421x_boost_vol_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 3;
	return 0;
}

static int cs421x_boost_vol_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] =
		cs_vendor_coef_get(codec, CS421X_IDX_SPK_CTL) & 0x0003;
	return 0;
}

static int cs421x_boost_vol_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);

	unsigned int vol = ucontrol->value.integer.value[0];
	unsigned int coef =
		cs_vendor_coef_get(codec, CS421X_IDX_SPK_CTL);
	unsigned int original_coef = coef;

	coef &= ~0x0003;
	coef |= (vol & 0x0003);
	if (original_coef == coef)
		return 0;
	else {
		cs_vendor_coef_set(codec, CS421X_IDX_SPK_CTL, coef);
		return 1;
	}
}

static const struct snd_kcontrol_new cs421x_speaker_bost_ctl = {

	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.name = "Speaker Boost Playback Volume",
	.info = cs421x_boost_vol_info,
	.get = cs421x_boost_vol_get,
	.put = cs421x_boost_vol_put,
	.tlv = { .p = cs421x_speaker_boost_db_scale },
};

static void cs4210_pinmux_init(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	unsigned int def_conf, coef;

	/* GPIO, DMIC_SCL, DMIC_SDA and SENSE_B are multiplexed */
	coef = cs_vendor_coef_get(codec, CS421X_IDX_DEV_CFG);

	if (spec->gpio_mask)
		coef |= 0x0008; /* B1,B2 are GPIOs */
	else
		coef &= ~0x0008;

	if (spec->sense_b)
		coef |= 0x0010; /* B2 is SENSE_B, not inverted  */
	else
		coef &= ~0x0010;

	cs_vendor_coef_set(codec, CS421X_IDX_DEV_CFG, coef);

	if ((spec->gpio_mask || spec->sense_b) &&
	    is_active_pin(codec, CS421X_DMIC_PIN_NID)) {

		/*
		    GPIO or SENSE_B forced - disconnect the DMIC pin.
		*/
		def_conf = snd_hda_codec_get_pincfg(codec, CS421X_DMIC_PIN_NID);
		def_conf &= ~AC_DEFCFG_PORT_CONN;
		def_conf |= (AC_JACK_PORT_NONE << AC_DEFCFG_PORT_CONN_SHIFT);
		snd_hda_codec_set_pincfg(codec, CS421X_DMIC_PIN_NID, def_conf);
	}
}

static void init_cs421x_digital(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;


	for (i = 0; i < cfg->dig_outs; i++) {
		hda_nid_t nid = cfg->dig_out_pins[i];
		if (!cfg->speaker_outs)
			continue;
		if (get_wcaps(codec, nid) & AC_WCAP_UNSOL_CAP) {
			snd_hda_jack_detect_enable_callback(codec, nid, SPDIF_EVENT, cs_automute);
			spec->spdif_detect = 1;
		}
	}
}

static int cs421x_init(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;

	if (spec->vendor_nid == CS4210_VENDOR_NID) {
		snd_hda_sequence_write(codec, cs421x_coef_init_verbs);
		snd_hda_sequence_write(codec, cs421x_coef_init_verbs_A1_silicon_fixes);
		cs4210_pinmux_init(codec);
	}

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
	init_cs421x_digital(codec);

	return 0;
}

/*
 * CS4210 Input MUX (1 ADC)
 */
static int cs421x_mux_enum_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cs_spec *spec = codec->spec;

	return snd_hda_input_mux_info(&spec->input_mux, uinfo);
}

static int cs421x_mux_enum_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cs_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->cur_input;
	return 0;
}

static int cs421x_mux_enum_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct cs_spec *spec = codec->spec;

	return snd_hda_input_mux_put(codec, &spec->input_mux, ucontrol,
				spec->adc_nid[0], &spec->cur_input);

}

static const struct snd_kcontrol_new cs421x_capture_source = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Source",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.info = cs421x_mux_enum_info,
	.get = cs421x_mux_enum_get,
	.put = cs421x_mux_enum_put,
};

static int cs421x_add_input_volume_control(struct hda_codec *codec, int item)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	const struct hda_input_mux *imux = &spec->input_mux;
	hda_nid_t pin = cfg->inputs[item].pin;
	struct snd_kcontrol *kctl;
	u32 caps;

	if (!(get_wcaps(codec, pin) & AC_WCAP_IN_AMP))
		return 0;

	caps = query_amp_caps(codec, pin, HDA_INPUT);
	caps = (caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT;
	if (caps <= 1)
		return 0;

	return add_volume(codec,  imux->items[item].label, 0,
			  HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_INPUT), 1, &kctl);
}

/* add a (input-boost) volume control to the given input pin */
static int build_cs421x_input(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	struct hda_input_mux *imux = &spec->input_mux;
	int i, err, type_idx;
	const char *label;

	if (!spec->num_inputs)
		return 0;

	/* make bind-capture */
	spec->capture_bind[0] = make_bind_capture(codec, &snd_hda_bind_sw);
	spec->capture_bind[1] = make_bind_capture(codec, &snd_hda_bind_vol);
	for (i = 0; i < 2; i++) {
		struct snd_kcontrol *kctl;
		int n;
		if (!spec->capture_bind[i])
			return -ENOMEM;
		kctl = snd_ctl_new1(&cs_capture_ctls[i], codec);
		if (!kctl)
			return -ENOMEM;
		kctl->private_value = (long)spec->capture_bind[i];
		err = snd_hda_ctl_add(codec, 0, kctl);
		if (err < 0)
			return err;
		for (n = 0; n < AUTO_PIN_LAST; n++) {
			if (!spec->adc_nid[n])
				continue;
			err = snd_hda_add_nid(codec, kctl, 0, spec->adc_nid[n]);
			if (err < 0)
				return err;
		}
	}

	/* Add Input MUX Items + Capture Volume/Switch */
	for (i = 0; i < spec->num_inputs; i++) {
		label = hda_get_autocfg_input_label(codec, cfg, i);
		snd_hda_add_imux_item(imux, label, spec->adc_idx[i], &type_idx);

		err = cs421x_add_input_volume_control(codec, i);
		if (err < 0)
			return err;
	}

	/*
	    Add 'Capture Source' Switch if
		* 2 inputs and no mic detec
		* 3 inputs
	*/
	if ((spec->num_inputs == 2 && !spec->mic_detect) ||
	    (spec->num_inputs == 3)) {

		err = snd_hda_ctl_add(codec, spec->adc_nid[0],
			      snd_ctl_new1(&cs421x_capture_source, codec));
		if (err < 0)
			return err;
	}

	return 0;
}

/* Single DAC (Mute/Gain) */
static int build_cs421x_output(struct hda_codec *codec)
{
	hda_nid_t dac = CS4210_DAC_NID;
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	struct snd_kcontrol *kctl;
	int err;
	char *name = "Master";

	fix_volume_caps(codec, dac);

	err = add_mute(codec, name, 0,
			HDA_COMPOSE_AMP_VAL(dac, 3, 0, HDA_OUTPUT), 0, &kctl);
	if (err < 0)
		return err;

	err = add_volume(codec, name, 0,
			HDA_COMPOSE_AMP_VAL(dac, 3, 0, HDA_OUTPUT), 0, &kctl);
	if (err < 0)
		return err;

	if (cfg->speaker_outs && (spec->vendor_nid == CS4210_VENDOR_NID)) {
		err = snd_hda_ctl_add(codec, 0,
			snd_ctl_new1(&cs421x_speaker_bost_ctl, codec));
		if (err < 0)
			return err;
	}
	return err;
}

static int cs421x_build_controls(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	int err;

	err = build_cs421x_output(codec);
	if (err < 0)
		return err;
	err = build_cs421x_input(codec);
	if (err < 0)
		return err;
	err = build_digital_output(codec);
	if (err < 0)
		return err;
	err =  cs421x_init(codec);
	if (err < 0)
		return err;

	err = snd_hda_jack_add_kctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	return 0;
}

static int parse_cs421x_input(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t pin = cfg->inputs[i].pin;
		spec->adc_nid[i] = get_adc(codec, pin, &spec->adc_idx[i]);
		spec->cur_input = spec->last_input = i;
		spec->num_inputs++;

		/* check whether the automatic mic switch is available */
		if (is_ext_mic(codec, i) && cfg->num_inputs >= 2) {
			spec->mic_detect = 1;
			spec->automic_idx = i;
		}
	}
	return 0;
}

static int cs421x_parse_auto_config(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	int err;

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);
	if (err < 0)
		return err;
	err = parse_output(codec);
	if (err < 0)
		return err;
	err = parse_cs421x_input(codec);
	if (err < 0)
		return err;
	err = parse_digital_output(codec);
	if (err < 0)
		return err;
	return 0;
}

#ifdef CONFIG_PM
/*
	Manage PDREF, when transitioning to D3hot
	(DAC,ADC) -> D3, PDREF=1, AFG->D3
*/
static int cs421x_suspend(struct hda_codec *codec)
{
	struct cs_spec *spec = codec->spec;
	unsigned int coef;

	snd_hda_shutup_pins(codec);

	snd_hda_codec_write(codec, CS4210_DAC_NID, 0,
			    AC_VERB_SET_POWER_STATE,  AC_PWRST_D3);
	snd_hda_codec_write(codec, CS4210_ADC_NID, 0,
			    AC_VERB_SET_POWER_STATE,  AC_PWRST_D3);

	if (spec->vendor_nid == CS4210_VENDOR_NID) {
		coef = cs_vendor_coef_get(codec, CS421X_IDX_DEV_CFG);
		coef |= 0x0004; /* PDREF */
		cs_vendor_coef_set(codec, CS421X_IDX_DEV_CFG, coef);
	}

	return 0;
}
#endif

static const struct hda_codec_ops cs421x_patch_ops = {
	.build_controls = cs421x_build_controls,
	.build_pcms = cs_build_pcms,
	.init = cs421x_init,
	.free = cs_free,
	.unsol_event = snd_hda_jack_unsol_event,
#ifdef CONFIG_PM
	.suspend = cs421x_suspend,
#endif
};

static int patch_cs4210(struct hda_codec *codec)
{
	struct cs_spec *spec;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	codec->spec = spec;
	snd_hda_gen_init(&spec->gen);

	spec->vendor_nid = CS4210_VENDOR_NID;

	snd_hda_pick_fixup(codec, cs421x_models, cs421x_fixup_tbl,
			   cs421x_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	/*
	    Update the GPIO/DMIC/SENSE_B pinmux before the configuration
	    is auto-parsed. If GPIO or SENSE_B is forced, DMIC input
	    is disabled.
	*/
	cs4210_pinmux_init(codec);

	err = cs421x_parse_auto_config(codec);
	if (err < 0)
		goto error;

	codec->patch_ops = cs421x_patch_ops;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;

 error:
	cs_free(codec);
	codec->spec = NULL;
	return err;
}

static int patch_cs4213(struct hda_codec *codec)
{
	struct cs_spec *spec;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	codec->spec = spec;
	snd_hda_gen_init(&spec->gen);

	spec->vendor_nid = CS4213_VENDOR_NID;

	err = cs421x_parse_auto_config(codec);
	if (err < 0)
		goto error;

	codec->patch_ops = cs421x_patch_ops;
	return 0;

 error:
	cs_free(codec);
	codec->spec = NULL;
	return err;
}


/*
 * patch entries
 */
static const struct hda_codec_preset snd_hda_preset_cirrus[] = {
	{ .id = 0x10134206, .name = "CS4206", .patch = patch_cs420x },
	{ .id = 0x10134207, .name = "CS4207", .patch = patch_cs420x },
	{ .id = 0x10134210, .name = "CS4210", .patch = patch_cs4210 },
	{ .id = 0x10134213, .name = "CS4213", .patch = patch_cs4213 },
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:10134206");
MODULE_ALIAS("snd-hda-codec-id:10134207");
MODULE_ALIAS("snd-hda-codec-id:10134210");
MODULE_ALIAS("snd-hda-codec-id:10134213");

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
