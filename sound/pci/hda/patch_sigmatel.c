/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for SigmaTel STAC92xx
 *
 * Copyright (c) 2005 Embedded Alley Solutions, Inc.
 * Matt Porter <mporter@embeddedalley.com>
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

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/asoundef.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_patch.h"

#define NUM_CONTROL_ALLOC	32
#define STAC_PWR_EVENT		0x20
#define STAC_HP_EVENT		0x30

enum {
	STAC_REF,
	STAC_9200_OQO,
	STAC_9200_DELL_D21,
	STAC_9200_DELL_D22,
	STAC_9200_DELL_D23,
	STAC_9200_DELL_M21,
	STAC_9200_DELL_M22,
	STAC_9200_DELL_M23,
	STAC_9200_DELL_M24,
	STAC_9200_DELL_M25,
	STAC_9200_DELL_M26,
	STAC_9200_DELL_M27,
	STAC_9200_GATEWAY,
	STAC_9200_PANASONIC,
	STAC_9200_MODELS
};

enum {
	STAC_9205_REF,
	STAC_9205_DELL_M42,
	STAC_9205_DELL_M43,
	STAC_9205_DELL_M44,
	STAC_9205_MODELS
};

enum {
	STAC_92HD73XX_REF,
	STAC_DELL_M6,
	STAC_92HD73XX_MODELS
};

enum {
	STAC_92HD71BXX_REF,
	STAC_DELL_M4_1,
	STAC_DELL_M4_2,
	STAC_92HD71BXX_MODELS
};

enum {
	STAC_925x_REF,
	STAC_M2_2,
	STAC_MA6,
	STAC_PA6,
	STAC_925x_MODELS
};

enum {
	STAC_D945_REF,
	STAC_D945GTP3,
	STAC_D945GTP5,
	STAC_INTEL_MAC_V1,
	STAC_INTEL_MAC_V2,
	STAC_INTEL_MAC_V3,
	STAC_INTEL_MAC_V4,
	STAC_INTEL_MAC_V5,
	STAC_INTEL_MAC_AUTO, /* This model is selected if no module parameter
			      * is given, one of the above models will be
			      * chosen according to the subsystem id. */
	/* for backward compatibility */
	STAC_MACMINI,
	STAC_MACBOOK,
	STAC_MACBOOK_PRO_V1,
	STAC_MACBOOK_PRO_V2,
	STAC_IMAC_INTEL,
	STAC_IMAC_INTEL_20,
	STAC_922X_DELL_D81,
	STAC_922X_DELL_D82,
	STAC_922X_DELL_M81,
	STAC_922X_DELL_M82,
	STAC_922X_MODELS
};

enum {
	STAC_D965_REF,
	STAC_D965_3ST,
	STAC_D965_5ST,
	STAC_DELL_3ST,
	STAC_DELL_BIOS,
	STAC_927X_MODELS
};

struct sigmatel_spec {
	struct snd_kcontrol_new *mixers[4];
	unsigned int num_mixers;

	int board_config;
	unsigned int surr_switch: 1;
	unsigned int line_switch: 1;
	unsigned int mic_switch: 1;
	unsigned int alt_switch: 1;
	unsigned int hp_detect: 1;

	/* gpio lines */
	unsigned int eapd_mask;
	unsigned int gpio_mask;
	unsigned int gpio_dir;
	unsigned int gpio_data;
	unsigned int gpio_mute;

	/* analog loopback */
	unsigned char aloopback_mask;
	unsigned char aloopback_shift;

	/* power management */
	unsigned int num_pwrs;
	hda_nid_t *pwr_nids;
	hda_nid_t *dac_list;

	/* playback */
	struct hda_input_mux *mono_mux;
	unsigned int cur_mmux;
	struct hda_multi_out multiout;
	hda_nid_t dac_nids[5];

	/* capture */
	hda_nid_t *adc_nids;
	unsigned int num_adcs;
	hda_nid_t *mux_nids;
	unsigned int num_muxes;
	hda_nid_t *dmic_nids;
	unsigned int num_dmics;
	hda_nid_t *dmux_nids;
	unsigned int num_dmuxes;
	hda_nid_t dig_in_nid;
	hda_nid_t mono_nid;

	/* pin widgets */
	hda_nid_t *pin_nids;
	unsigned int num_pins;
	unsigned int *pin_configs;
	unsigned int *bios_pin_configs;

	/* codec specific stuff */
	struct hda_verb *init;
	struct snd_kcontrol_new *mixer;

	/* capture source */
	struct hda_input_mux *dinput_mux;
	unsigned int cur_dmux[2];
	struct hda_input_mux *input_mux;
	unsigned int cur_mux[3];

	/* i/o switches */
	unsigned int io_switch[2];
	unsigned int clfe_swap;
	unsigned int hp_switch;
	unsigned int aloopback;

	struct hda_pcm pcm_rec[2];	/* PCM information */

	/* dynamic controls and input_mux */
	struct auto_pin_cfg autocfg;
	unsigned int num_kctl_alloc, num_kctl_used;
	struct snd_kcontrol_new *kctl_alloc;
	struct hda_input_mux private_dimux;
	struct hda_input_mux private_imux;
	struct hda_input_mux private_mono_mux;
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

static hda_nid_t stac92hd73xx_pwr_nids[8] = {
	0x0a, 0x0b, 0x0c, 0xd, 0x0e,
	0x0f, 0x10, 0x11
};

static hda_nid_t stac92hd73xx_adc_nids[2] = {
	0x1a, 0x1b
};

#define STAC92HD73XX_NUM_DMICS	2
static hda_nid_t stac92hd73xx_dmic_nids[STAC92HD73XX_NUM_DMICS + 1] = {
	0x13, 0x14, 0
};

#define STAC92HD73_DAC_COUNT 5
static hda_nid_t stac92hd73xx_dac_nids[STAC92HD73_DAC_COUNT] = {
	0x15, 0x16, 0x17, 0x18, 0x19,
};

static hda_nid_t stac92hd73xx_mux_nids[4] = {
	0x28, 0x29, 0x2a, 0x2b,
};

static hda_nid_t stac92hd73xx_dmux_nids[2] = {
	0x20, 0x21,
};

static hda_nid_t stac92hd71bxx_pwr_nids[3] = {
	0x0a, 0x0d, 0x0f
};

static hda_nid_t stac92hd71bxx_adc_nids[2] = {
	0x12, 0x13,
};

static hda_nid_t stac92hd71bxx_mux_nids[2] = {
	0x1a, 0x1b
};

static hda_nid_t stac92hd71bxx_dmux_nids[1] = {
	0x1c,
};

static hda_nid_t stac92hd71bxx_dac_nids[1] = {
	0x10, /*0x11, */
};

#define STAC92HD71BXX_NUM_DMICS	2
static hda_nid_t stac92hd71bxx_dmic_nids[STAC92HD71BXX_NUM_DMICS + 1] = {
	0x18, 0x19, 0
};

static hda_nid_t stac925x_adc_nids[1] = {
        0x03,
};

static hda_nid_t stac925x_mux_nids[1] = {
        0x0f,
};

static hda_nid_t stac925x_dac_nids[1] = {
        0x02,
};

#define STAC925X_NUM_DMICS	1
static hda_nid_t stac925x_dmic_nids[STAC925X_NUM_DMICS + 1] = {
	0x15, 0
};

static hda_nid_t stac925x_dmux_nids[1] = {
	0x14,
};

static hda_nid_t stac922x_adc_nids[2] = {
        0x06, 0x07,
};

static hda_nid_t stac922x_mux_nids[2] = {
        0x12, 0x13,
};

static hda_nid_t stac927x_adc_nids[3] = {
        0x07, 0x08, 0x09
};

static hda_nid_t stac927x_mux_nids[3] = {
        0x15, 0x16, 0x17
};

static hda_nid_t stac927x_dac_nids[6] = {
	0x02, 0x03, 0x04, 0x05, 0x06, 0
};

static hda_nid_t stac927x_dmux_nids[1] = {
	0x1b,
};

#define STAC927X_NUM_DMICS 2
static hda_nid_t stac927x_dmic_nids[STAC927X_NUM_DMICS + 1] = {
	0x13, 0x14, 0
};

static hda_nid_t stac9205_adc_nids[2] = {
        0x12, 0x13
};

static hda_nid_t stac9205_mux_nids[2] = {
        0x19, 0x1a
};

static hda_nid_t stac9205_dmux_nids[1] = {
	0x1d,
};

#define STAC9205_NUM_DMICS	2
static hda_nid_t stac9205_dmic_nids[STAC9205_NUM_DMICS + 1] = {
        0x17, 0x18, 0
};

static hda_nid_t stac9200_pin_nids[8] = {
	0x08, 0x09, 0x0d, 0x0e, 
	0x0f, 0x10, 0x11, 0x12,
};

static hda_nid_t stac925x_pin_nids[8] = {
	0x07, 0x08, 0x0a, 0x0b, 
	0x0c, 0x0d, 0x10, 0x11,
};

static hda_nid_t stac922x_pin_nids[10] = {
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
	0x0f, 0x10, 0x11, 0x15, 0x1b,
};

static hda_nid_t stac92hd73xx_pin_nids[13] = {
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
	0x0f, 0x10, 0x11, 0x12, 0x13,
	0x14, 0x1e, 0x22
};

static hda_nid_t stac92hd71bxx_pin_nids[10] = {
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
	0x0f, 0x14, 0x18, 0x19, 0x1e,
};

static hda_nid_t stac927x_pin_nids[14] = {
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
	0x0f, 0x10, 0x11, 0x12, 0x13,
	0x14, 0x21, 0x22, 0x23,
};

static hda_nid_t stac9205_pin_nids[12] = {
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
	0x0f, 0x14, 0x16, 0x17, 0x18,
	0x21, 0x22,
};

static int stac92xx_dmux_enum_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_input_mux_info(spec->dinput_mux, uinfo);
}

static int stac92xx_dmux_enum_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	unsigned int dmux_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_dmux[dmux_idx];
	return 0;
}

static int stac92xx_dmux_enum_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	unsigned int dmux_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	return snd_hda_input_mux_put(codec, spec->dinput_mux, ucontrol,
			spec->dmux_nids[dmux_idx], &spec->cur_dmux[dmux_idx]);
}

static int stac92xx_mux_enum_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_input_mux_info(spec->input_mux, uinfo);
}

static int stac92xx_mux_enum_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int stac92xx_mux_enum_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	return snd_hda_input_mux_put(codec, spec->input_mux, ucontrol,
				     spec->mux_nids[adc_idx], &spec->cur_mux[adc_idx]);
}

static int stac92xx_mono_mux_enum_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_input_mux_info(spec->mono_mux, uinfo);
}

static int stac92xx_mono_mux_enum_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->cur_mmux;
	return 0;
}

static int stac92xx_mono_mux_enum_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;

	return snd_hda_input_mux_put(codec, spec->mono_mux, ucontrol,
				     spec->mono_nid, &spec->cur_mmux);
}

#define stac92xx_aloopback_info snd_ctl_boolean_mono_info

static int stac92xx_aloopback_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	struct sigmatel_spec *spec = codec->spec;

	ucontrol->value.integer.value[0] = !!(spec->aloopback &
					      (spec->aloopback_mask << idx));
	return 0;
}

static int stac92xx_aloopback_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	unsigned int dac_mode;
	unsigned int val, idx_val;

	idx_val = spec->aloopback_mask << idx;
	if (ucontrol->value.integer.value[0])
		val = spec->aloopback | idx_val;
	else
		val = spec->aloopback & ~idx_val;
	if (spec->aloopback == val)
		return 0;

	spec->aloopback = val;

	/* Only return the bits defined by the shift value of the
	 * first two bytes of the mask
	 */
	dac_mode = snd_hda_codec_read(codec, codec->afg, 0,
				      kcontrol->private_value & 0xFFFF, 0x0);
	dac_mode >>= spec->aloopback_shift;

	if (spec->aloopback & idx_val) {
		snd_hda_power_up(codec);
		dac_mode |= idx_val;
	} else {
		snd_hda_power_down(codec);
		dac_mode &= ~idx_val;
	}

	snd_hda_codec_write_cache(codec, codec->afg, 0,
		kcontrol->private_value >> 16, dac_mode);

	return 1;
}

static struct hda_verb stac9200_core_init[] = {
	/* set dac0mux for dac converter */
	{ 0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{}
};

static struct hda_verb stac9200_eapd_init[] = {
	/* set dac0mux for dac converter */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_EAPD_BTLENABLE, 0x02},
	{}
};

static struct hda_verb stac92hd73xx_6ch_core_init[] = {
	/* set master volume and direct control */
	{ 0x1f, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	/* setup audio connections */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00},
	{ 0x10, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x11, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* setup adcs to point to mixer */
	{ 0x20, AC_VERB_SET_CONNECT_SEL, 0x0b},
	{ 0x21, AC_VERB_SET_CONNECT_SEL, 0x0b},
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* setup import muxs */
	{ 0x28, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x29, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x2a, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x2b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{}
};

static struct hda_verb dell_eq_core_init[] = {
	/* set master volume to max value without distortion
	 * and direct control */
	{ 0x1f, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xec},
	/* setup audio connections */
	{ 0x0d, AC_VERB_SET_CONNECT_SEL, 0x00},
	{ 0x0a, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* setup adcs to point to mixer */
	{ 0x20, AC_VERB_SET_CONNECT_SEL, 0x0b},
	{ 0x21, AC_VERB_SET_CONNECT_SEL, 0x0b},
	/* setup import muxs */
	{ 0x28, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x29, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x2a, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x2b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{}
};

static struct hda_verb dell_m6_core_init[] = {
	/* set master volume to max value without distortion
	 * and direct control */
	{ 0x1f, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xec},
	/* setup audio connections */
	{ 0x0d, AC_VERB_SET_CONNECT_SEL, 0x00},
	{ 0x0a, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* setup adcs to point to mixer */
	{ 0x20, AC_VERB_SET_CONNECT_SEL, 0x0b},
	{ 0x21, AC_VERB_SET_CONNECT_SEL, 0x0b},
	/* setup import muxs */
	{ 0x28, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x29, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x2a, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x2b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{}
};

static struct hda_verb stac92hd73xx_8ch_core_init[] = {
	/* set master volume and direct control */
	{ 0x1f, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	/* setup audio connections */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00},
	{ 0x10, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x11, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* connect hp ports to dac3 */
	{ 0x0a, AC_VERB_SET_CONNECT_SEL, 0x03},
	{ 0x0d, AC_VERB_SET_CONNECT_SEL, 0x03},
	/* setup adcs to point to mixer */
	{ 0x20, AC_VERB_SET_CONNECT_SEL, 0x0b},
	{ 0x21, AC_VERB_SET_CONNECT_SEL, 0x0b},
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* setup import muxs */
	{ 0x28, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x29, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x2a, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x2b, AC_VERB_SET_CONNECT_SEL, 0x03},
	{}
};

static struct hda_verb stac92hd73xx_10ch_core_init[] = {
	/* set master volume and direct control */
	{ 0x1f, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	/* setup audio connections */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x10, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ 0x11, AC_VERB_SET_CONNECT_SEL, 0x02 },
	/* dac3 is connected to import3 mux */
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, 0xb07f},
	/* connect hp ports to dac4 */
	{ 0x0a, AC_VERB_SET_CONNECT_SEL, 0x04},
	{ 0x0d, AC_VERB_SET_CONNECT_SEL, 0x04},
	/* setup adcs to point to mixer */
	{ 0x20, AC_VERB_SET_CONNECT_SEL, 0x0b},
	{ 0x21, AC_VERB_SET_CONNECT_SEL, 0x0b},
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* setup import muxs */
	{ 0x28, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x29, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x2a, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x2b, AC_VERB_SET_CONNECT_SEL, 0x03},
	{}
};

static struct hda_verb stac92hd71bxx_core_init[] = {
	/* set master volume and direct control */
	{ 0x28, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	/* connect headphone jack to dac1 */
	{ 0x0a, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT}, /* Speaker */
	/* unmute right and left channels for nodes 0x0a, 0xd, 0x0f */
	{ 0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{ 0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
};

#define HD_DISABLE_PORTF 3
static struct hda_verb stac92hd71bxx_analog_core_init[] = {
	/* start of config #1 */

	/* connect port 0f to audio mixer */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x2},
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT}, /* Speaker */
	/* unmute right and left channels for node 0x0f */
	{ 0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* start of config #2 */

	/* set master volume and direct control */
	{ 0x28, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	/* connect headphone jack to dac1 */
	{ 0x0a, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* connect port 0d to audio mixer */
	{ 0x0d, AC_VERB_SET_CONNECT_SEL, 0x2},
	/* unmute dac0 input in audio mixer */
	{ 0x17, AC_VERB_SET_AMP_GAIN_MUTE, 0x701f},
	/* unmute right and left channels for nodes 0x0a, 0xd */
	{ 0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{}
};

static struct hda_verb stac925x_core_init[] = {
	/* set dac0mux for dac converter */
	{ 0x06, AC_VERB_SET_CONNECT_SEL, 0x00},
	{}
};

static struct hda_verb stac922x_core_init[] = {
	/* set master volume and direct control */	
	{ 0x16, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	{}
};

static struct hda_verb d965_core_init[] = {
	/* set master volume and direct control */	
	{ 0x24, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	/* unmute node 0x1b */
	{ 0x1b, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* select node 0x03 as DAC */	
	{ 0x0b, AC_VERB_SET_CONNECT_SEL, 0x01},
	{}
};

static struct hda_verb stac927x_core_init[] = {
	/* set master volume and direct control */	
	{ 0x24, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	{}
};

static struct hda_verb stac9205_core_init[] = {
	/* set master volume and direct control */	
	{ 0x24, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	{}
};

#define STAC_MONO_MUX \
	{ \
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name = "Mono Mux", \
		.count = 1, \
		.info = stac92xx_mono_mux_enum_info, \
		.get = stac92xx_mono_mux_enum_get, \
		.put = stac92xx_mono_mux_enum_put, \
	}

#define STAC_INPUT_SOURCE(cnt) \
	{ \
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name = "Input Source", \
		.count = cnt, \
		.info = stac92xx_mux_enum_info, \
		.get = stac92xx_mux_enum_get, \
		.put = stac92xx_mux_enum_put, \
	}

#define STAC_ANALOG_LOOPBACK(verb_read, verb_write, cnt) \
	{ \
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name  = "Analog Loopback", \
		.count = cnt, \
		.info  = stac92xx_aloopback_info, \
		.get   = stac92xx_aloopback_get, \
		.put   = stac92xx_aloopback_put, \
		.private_value = verb_read | (verb_write << 16), \
	}

static struct snd_kcontrol_new stac9200_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0xb, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Master Playback Switch", 0xb, 0, HDA_OUTPUT),
	STAC_INPUT_SOURCE(1),
	HDA_CODEC_VOLUME("Capture Volume", 0x0a, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x0a, 0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Mux Volume", 0x0c, 0, HDA_OUTPUT),
	{ } /* end */
};

static struct snd_kcontrol_new stac92hd73xx_6ch_mixer[] = {
	STAC_ANALOG_LOOPBACK(0xFA0, 0x7A1, 3),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x0, 0x20, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x0, 0x20, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x1, 0x21, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x1, 0x21, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Front Mic Mixer Capture Volume", 0x1d, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Mixer Capture Switch", 0x1d, 0, HDA_INPUT),

	HDA_CODEC_VOLUME("Mic Mixer Capture Volume", 0x1d, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Mixer Capture Switch", 0x1d, 0x1, HDA_INPUT),

	HDA_CODEC_VOLUME("Line In Mixer Capture Volume", 0x1d, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Line In Mixer Capture Switch", 0x1d, 0x2, HDA_INPUT),

	HDA_CODEC_VOLUME("DAC Mixer Capture Volume", 0x1d, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("DAC Mixer Capture Switch", 0x1d, 0x3, HDA_INPUT),

	HDA_CODEC_VOLUME("CD Mixer Capture Volume", 0x1d, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("CD Mixer Capture Switch", 0x1d, 0x4, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new stac92hd73xx_8ch_mixer[] = {
	STAC_ANALOG_LOOPBACK(0xFA0, 0x7A1, 4),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x0, 0x20, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x0, 0x20, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x1, 0x21, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x1, 0x21, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Front Mic Mixer Capture Volume", 0x1d, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Mixer Capture Switch", 0x1d, 0, HDA_INPUT),

	HDA_CODEC_VOLUME("Mic Mixer Capture Volume", 0x1d, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Mixer Capture Switch", 0x1d, 0x1, HDA_INPUT),

	HDA_CODEC_VOLUME("Line In Mixer Capture Volume", 0x1d, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Line In Mixer Capture Switch", 0x1d, 0x2, HDA_INPUT),

	HDA_CODEC_VOLUME("DAC Mixer Capture Volume", 0x1d, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("DAC Mixer Capture Switch", 0x1d, 0x3, HDA_INPUT),

	HDA_CODEC_VOLUME("CD Mixer Capture Volume", 0x1d, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("CD Mixer Capture Switch", 0x1d, 0x4, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new stac92hd73xx_10ch_mixer[] = {
	STAC_ANALOG_LOOPBACK(0xFA0, 0x7A1, 5),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x0, 0x20, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x0, 0x20, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x1, 0x21, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x1, 0x21, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Front Mic Mixer Capture Volume", 0x1d, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Mixer Capture Switch", 0x1d, 0, HDA_INPUT),

	HDA_CODEC_VOLUME("Mic Mixer Capture Volume", 0x1d, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Mixer Capture Switch", 0x1d, 0x1, HDA_INPUT),

	HDA_CODEC_VOLUME("Line In Mixer Capture Volume", 0x1d, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("Line In Mixer Capture Switch", 0x1d, 0x2, HDA_INPUT),

	HDA_CODEC_VOLUME("DAC Mixer Capture Volume", 0x1d, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("DAC Mixer Capture Switch", 0x1d, 0x3, HDA_INPUT),

	HDA_CODEC_VOLUME("CD Mixer Capture Volume", 0x1d, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("CD Mixer Capture Switch", 0x1d, 0x4, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new stac92hd71bxx_analog_mixer[] = {
	STAC_INPUT_SOURCE(2),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x0, 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x0, 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Capture Mux Volume", 0x0, 0x1a, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x1, 0x1d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x1, 0x1d, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Capture Mux Volume", 0x1, 0x1b, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("PC Beep Volume", 0x17, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("PC Beep Switch", 0x17, 0x2, HDA_INPUT),

	HDA_CODEC_MUTE("Analog Loopback 1", 0x17, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Analog Loopback 2", 0x17, 0x4, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new stac92hd71bxx_mixer[] = {
	STAC_INPUT_SOURCE(2),
	STAC_ANALOG_LOOPBACK(0xFA0, 0x7A0, 2),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x0, 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x0, 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Capture Mux Volume", 0x0, 0x1a, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x1, 0x1d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x1, 0x1d, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Capture Mux Volume", 0x1, 0x1b, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static struct snd_kcontrol_new stac925x_mixer[] = {
	STAC_INPUT_SOURCE(1),
	HDA_CODEC_VOLUME("Capture Volume", 0x09, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x14, 0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Mux Volume", 0x0f, 0, HDA_OUTPUT),
	{ } /* end */
};

static struct snd_kcontrol_new stac9205_mixer[] = {
	STAC_INPUT_SOURCE(2),
	STAC_ANALOG_LOOPBACK(0xFE0, 0x7E0, 1),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x0, 0x1b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x0, 0x1d, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Mux Capture Volume", 0x0, 0x19, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x1, 0x1c, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x1, 0x1e, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Mux Capture Volume", 0x1, 0x1A, 0x0, HDA_OUTPUT),

	{ } /* end */
};

/* This needs to be generated dynamically based on sequence */
static struct snd_kcontrol_new stac922x_mixer[] = {
	STAC_INPUT_SOURCE(2),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x0, 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x0, 0x17, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Mux Capture Volume", 0x0, 0x12, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x1, 0x18, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x1, 0x18, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Mux Capture Volume", 0x1, 0x13, 0x0, HDA_OUTPUT),
	{ } /* end */
};


static struct snd_kcontrol_new stac927x_mixer[] = {
	STAC_INPUT_SOURCE(3),
	STAC_ANALOG_LOOPBACK(0xFEB, 0x7EB, 1),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x0, 0x18, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x0, 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Mux Capture Volume", 0x0, 0x15, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x1, 0x19, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x1, 0x1c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Mux Capture Volume", 0x1, 0x16, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME_IDX("Capture Volume", 0x2, 0x1A, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 0x2, 0x1d, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Mux Capture Volume", 0x2, 0x17, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static struct snd_kcontrol_new stac_dmux_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Digital Input Source",
	/* count set later */
	.info = stac92xx_dmux_enum_info,
	.get = stac92xx_dmux_enum_get,
	.put = stac92xx_dmux_enum_put,
};

static const char *slave_vols[] = {
	"Front Playback Volume",
	"Surround Playback Volume",
	"Center Playback Volume",
	"LFE Playback Volume",
	"Side Playback Volume",
	"Headphone Playback Volume",
	"Headphone Playback Volume",
	"Speaker Playback Volume",
	"External Speaker Playback Volume",
	"Speaker2 Playback Volume",
	NULL
};

static const char *slave_sws[] = {
	"Front Playback Switch",
	"Surround Playback Switch",
	"Center Playback Switch",
	"LFE Playback Switch",
	"Side Playback Switch",
	"Headphone Playback Switch",
	"Headphone Playback Switch",
	"Speaker Playback Switch",
	"External Speaker Playback Switch",
	"Speaker2 Playback Switch",
	"IEC958 Playback Switch",
	NULL
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
	if (spec->num_dmuxes > 0) {
		stac_dmux_mixer.count = spec->num_dmuxes;
		err = snd_ctl_add(codec->bus->card,
				  snd_ctl_new1(&stac_dmux_mixer, codec));
		if (err < 0)
			return err;
	}

	if (spec->multiout.dig_out_nid) {
		err = snd_hda_create_spdif_out_ctls(codec, spec->multiout.dig_out_nid);
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

	/* if we have no master control, let's create it */
	if (!snd_hda_find_mixer_ctl(codec, "Master Playback Volume")) {
		unsigned int vmaster_tlv[4];
		snd_hda_set_vmaster_tlv(codec, spec->multiout.dac_nids[0],
					HDA_OUTPUT, vmaster_tlv);
		err = snd_hda_add_vmaster(codec, "Master Playback Volume",
					  vmaster_tlv, slave_vols);
		if (err < 0)
			return err;
	}
	if (!snd_hda_find_mixer_ctl(codec, "Master Playback Switch")) {
		err = snd_hda_add_vmaster(codec, "Master Playback Switch",
					  NULL, slave_sws);
		if (err < 0)
			return err;
	}

	return 0;	
}

static unsigned int ref9200_pin_configs[8] = {
	0x01c47010, 0x01447010, 0x0221401f, 0x01114010,
	0x02a19020, 0x01a19021, 0x90100140, 0x01813122,
};

/* 
    STAC 9200 pin configs for
    102801A8
    102801DE
    102801E8
*/
static unsigned int dell9200_d21_pin_configs[8] = {
	0x400001f0, 0x400001f1, 0x02214030, 0x01014010, 
	0x02a19020, 0x01a19021, 0x90100140, 0x01813122,
};

/* 
    STAC 9200 pin configs for
    102801C0
    102801C1
*/
static unsigned int dell9200_d22_pin_configs[8] = {
	0x400001f0, 0x400001f1, 0x0221401f, 0x01014010, 
	0x01813020, 0x02a19021, 0x90100140, 0x400001f2,
};

/* 
    STAC 9200 pin configs for
    102801C4 (Dell Dimension E310)
    102801C5
    102801C7
    102801D9
    102801DA
    102801E3
*/
static unsigned int dell9200_d23_pin_configs[8] = {
	0x400001f0, 0x400001f1, 0x0221401f, 0x01014010, 
	0x01813020, 0x01a19021, 0x90100140, 0x400001f2, 
};


/* 
    STAC 9200-32 pin configs for
    102801B5 (Dell Inspiron 630m)
    102801D8 (Dell Inspiron 640m)
*/
static unsigned int dell9200_m21_pin_configs[8] = {
	0x40c003fa, 0x03441340, 0x0321121f, 0x90170310,
	0x408003fb, 0x03a11020, 0x401003fc, 0x403003fd,
};

/* 
    STAC 9200-32 pin configs for
    102801C2 (Dell Latitude D620)
    102801C8 
    102801CC (Dell Latitude D820)
    102801D4 
    102801D6 
*/
static unsigned int dell9200_m22_pin_configs[8] = {
	0x40c003fa, 0x0144131f, 0x0321121f, 0x90170310, 
	0x90a70321, 0x03a11020, 0x401003fb, 0x40f000fc,
};

/* 
    STAC 9200-32 pin configs for
    102801CE (Dell XPS M1710)
    102801CF (Dell Precision M90)
*/
static unsigned int dell9200_m23_pin_configs[8] = {
	0x40c003fa, 0x01441340, 0x0421421f, 0x90170310,
	0x408003fb, 0x04a1102e, 0x90170311, 0x403003fc,
};

/*
    STAC 9200-32 pin configs for 
    102801C9
    102801CA
    102801CB (Dell Latitude 120L)
    102801D3
*/
static unsigned int dell9200_m24_pin_configs[8] = {
	0x40c003fa, 0x404003fb, 0x0321121f, 0x90170310, 
	0x408003fc, 0x03a11020, 0x401003fd, 0x403003fe, 
};

/*
    STAC 9200-32 pin configs for
    102801BD (Dell Inspiron E1505n)
    102801EE
    102801EF
*/
static unsigned int dell9200_m25_pin_configs[8] = {
	0x40c003fa, 0x01441340, 0x0421121f, 0x90170310, 
	0x408003fb, 0x04a11020, 0x401003fc, 0x403003fd,
};

/*
    STAC 9200-32 pin configs for
    102801F5 (Dell Inspiron 1501)
    102801F6
*/
static unsigned int dell9200_m26_pin_configs[8] = {
	0x40c003fa, 0x404003fb, 0x0421121f, 0x90170310, 
	0x408003fc, 0x04a11020, 0x401003fd, 0x403003fe,
};

/*
    STAC 9200-32
    102801CD (Dell Inspiron E1705/9400)
*/
static unsigned int dell9200_m27_pin_configs[8] = {
	0x40c003fa, 0x01441340, 0x0421121f, 0x90170310,
	0x90170310, 0x04a11020, 0x90170310, 0x40f003fc,
};

static unsigned int oqo9200_pin_configs[8] = {
	0x40c000f0, 0x404000f1, 0x0221121f, 0x02211210,
	0x90170111, 0x90a70120, 0x400000f2, 0x400000f3,
};


static unsigned int *stac9200_brd_tbl[STAC_9200_MODELS] = {
	[STAC_REF] = ref9200_pin_configs,
	[STAC_9200_OQO] = oqo9200_pin_configs,
	[STAC_9200_DELL_D21] = dell9200_d21_pin_configs,
	[STAC_9200_DELL_D22] = dell9200_d22_pin_configs,
	[STAC_9200_DELL_D23] = dell9200_d23_pin_configs,
	[STAC_9200_DELL_M21] = dell9200_m21_pin_configs,
	[STAC_9200_DELL_M22] = dell9200_m22_pin_configs,
	[STAC_9200_DELL_M23] = dell9200_m23_pin_configs,
	[STAC_9200_DELL_M24] = dell9200_m24_pin_configs,
	[STAC_9200_DELL_M25] = dell9200_m25_pin_configs,
	[STAC_9200_DELL_M26] = dell9200_m26_pin_configs,
	[STAC_9200_DELL_M27] = dell9200_m27_pin_configs,
	[STAC_9200_PANASONIC] = ref9200_pin_configs,
};

static const char *stac9200_models[STAC_9200_MODELS] = {
	[STAC_REF] = "ref",
	[STAC_9200_OQO] = "oqo",
	[STAC_9200_DELL_D21] = "dell-d21",
	[STAC_9200_DELL_D22] = "dell-d22",
	[STAC_9200_DELL_D23] = "dell-d23",
	[STAC_9200_DELL_M21] = "dell-m21",
	[STAC_9200_DELL_M22] = "dell-m22",
	[STAC_9200_DELL_M23] = "dell-m23",
	[STAC_9200_DELL_M24] = "dell-m24",
	[STAC_9200_DELL_M25] = "dell-m25",
	[STAC_9200_DELL_M26] = "dell-m26",
	[STAC_9200_DELL_M27] = "dell-m27",
	[STAC_9200_GATEWAY] = "gateway",
	[STAC_9200_PANASONIC] = "panasonic",
};

static struct snd_pci_quirk stac9200_cfg_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668,
		      "DFI LanParty", STAC_REF),
	/* Dell laptops have BIOS problem */
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01a8,
		      "unknown Dell", STAC_9200_DELL_D21),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01b5,
		      "Dell Inspiron 630m", STAC_9200_DELL_M21),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01bd,
		      "Dell Inspiron E1505n", STAC_9200_DELL_M25),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01c0,
		      "unknown Dell", STAC_9200_DELL_D22),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01c1,
		      "unknown Dell", STAC_9200_DELL_D22),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01c2,
		      "Dell Latitude D620", STAC_9200_DELL_M22),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01c5,
		      "unknown Dell", STAC_9200_DELL_D23),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01c7,
		      "unknown Dell", STAC_9200_DELL_D23),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01c8,
		      "unknown Dell", STAC_9200_DELL_M22),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01c9,
		      "unknown Dell", STAC_9200_DELL_M24),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01ca,
		      "unknown Dell", STAC_9200_DELL_M24),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01cb,
		      "Dell Latitude 120L", STAC_9200_DELL_M24),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01cc,
		      "Dell Latitude D820", STAC_9200_DELL_M22),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01cd,
		      "Dell Inspiron E1705/9400", STAC_9200_DELL_M27),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01ce,
		      "Dell XPS M1710", STAC_9200_DELL_M23),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01cf,
		      "Dell Precision M90", STAC_9200_DELL_M23),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01d3,
		      "unknown Dell", STAC_9200_DELL_M22),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01d4,
		      "unknown Dell", STAC_9200_DELL_M22),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01d6,
		      "unknown Dell", STAC_9200_DELL_M22),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01d8,
		      "Dell Inspiron 640m", STAC_9200_DELL_M21),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01d9,
		      "unknown Dell", STAC_9200_DELL_D23),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01da,
		      "unknown Dell", STAC_9200_DELL_D23),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01de,
		      "unknown Dell", STAC_9200_DELL_D21),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01e3,
		      "unknown Dell", STAC_9200_DELL_D23),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01e8,
		      "unknown Dell", STAC_9200_DELL_D21),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01ee,
		      "unknown Dell", STAC_9200_DELL_M25),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01ef,
		      "unknown Dell", STAC_9200_DELL_M25),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01f5,
		      "Dell Inspiron 1501", STAC_9200_DELL_M26),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01f6,
		      "unknown Dell", STAC_9200_DELL_M26),
	/* Panasonic */
	SND_PCI_QUIRK(0x10f7, 0x8338, "Panasonic CF-74", STAC_9200_PANASONIC),
	/* Gateway machines needs EAPD to be set on resume */
	SND_PCI_QUIRK(0x107b, 0x0205, "Gateway S-7110M", STAC_9200_GATEWAY),
	SND_PCI_QUIRK(0x107b, 0x0317, "Gateway MT3423, MX341*",
		      STAC_9200_GATEWAY),
	SND_PCI_QUIRK(0x107b, 0x0318, "Gateway ML3019, MT3707",
		      STAC_9200_GATEWAY),
	/* OQO Mobile */
	SND_PCI_QUIRK(0x1106, 0x3288, "OQO Model 2", STAC_9200_OQO),
	{} /* terminator */
};

static unsigned int ref925x_pin_configs[8] = {
	0x40c003f0, 0x424503f2, 0x01813022, 0x02a19021,
	0x90a70320, 0x02214210, 0x01019020, 0x9033032e,
};

static unsigned int stac925x_MA6_pin_configs[8] = {
	0x40c003f0, 0x424503f2, 0x01813022, 0x02a19021,
	0x90a70320, 0x90100211, 0x400003f1, 0x9033032e,
};

static unsigned int stac925x_PA6_pin_configs[8] = {
	0x40c003f0, 0x424503f2, 0x01813022, 0x02a19021,
	0x50a103f0, 0x90100211, 0x400003f1, 0x9033032e,
};

static unsigned int stac925xM2_2_pin_configs[8] = {
	0x40c003f3, 0x424503f2, 0x04180011, 0x02a19020,
	0x50a103f0, 0x90100212, 0x400003f1, 0x9033032e,
};

static unsigned int *stac925x_brd_tbl[STAC_925x_MODELS] = {
	[STAC_REF] = ref925x_pin_configs,
	[STAC_M2_2] = stac925xM2_2_pin_configs,
	[STAC_MA6] = stac925x_MA6_pin_configs,
	[STAC_PA6] = stac925x_PA6_pin_configs,
};

static const char *stac925x_models[STAC_925x_MODELS] = {
	[STAC_REF] = "ref",
	[STAC_M2_2] = "m2-2",
	[STAC_MA6] = "m6",
	[STAC_PA6] = "pa6",
};

static struct snd_pci_quirk stac925x_cfg_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668, "DFI LanParty", STAC_REF),
	SND_PCI_QUIRK(0x8384, 0x7632, "Stac9202 Reference Board", STAC_REF),
	SND_PCI_QUIRK(0x107b, 0x0316, "Gateway M255", STAC_REF),
	SND_PCI_QUIRK(0x107b, 0x0366, "Gateway MP6954", STAC_REF),
	SND_PCI_QUIRK(0x107b, 0x0461, "Gateway NX560XL", STAC_MA6),
	SND_PCI_QUIRK(0x107b, 0x0681, "Gateway NX860", STAC_PA6),
	SND_PCI_QUIRK(0x1002, 0x437b, "Gateway MX6453", STAC_M2_2),
	{} /* terminator */
};

static unsigned int ref92hd73xx_pin_configs[13] = {
	0x02214030, 0x02a19040, 0x01a19020, 0x02214030,
	0x0181302e, 0x01014010, 0x01014020, 0x01014030,
	0x02319040, 0x90a000f0, 0x90a000f0, 0x01452050,
	0x01452050,
};

static unsigned int dell_m6_pin_configs[13] = {
	0x0321101f, 0x4f00000f, 0x4f0000f0, 0x90170110,
	0x03a11020, 0x0321101f, 0x4f0000f0, 0x4f0000f0,
	0x4f0000f0, 0x90a60160, 0x4f0000f0, 0x4f0000f0,
	0x4f0000f0,
};

static unsigned int *stac92hd73xx_brd_tbl[STAC_92HD73XX_MODELS] = {
	[STAC_92HD73XX_REF]	= ref92hd73xx_pin_configs,
	[STAC_DELL_M6]	= dell_m6_pin_configs,
};

static const char *stac92hd73xx_models[STAC_92HD73XX_MODELS] = {
	[STAC_92HD73XX_REF] = "ref",
	[STAC_DELL_M6] = "dell-m6",
};

static struct snd_pci_quirk stac92hd73xx_cfg_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668,
				"DFI LanParty", STAC_92HD73XX_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0254,
				"unknown Dell", STAC_DELL_M6),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0255,
				"unknown Dell", STAC_DELL_M6),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0256,
				"unknown Dell", STAC_DELL_M6),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0257,
				"unknown Dell", STAC_DELL_M6),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x025e,
				"unknown Dell", STAC_DELL_M6),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x025f,
				"unknown Dell", STAC_DELL_M6),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0271,
				"unknown Dell", STAC_DELL_M6),
	{} /* terminator */
};

static unsigned int ref92hd71bxx_pin_configs[10] = {
	0x02214030, 0x02a19040, 0x01a19020, 0x01014010,
	0x0181302e, 0x01114010, 0x01019020, 0x90a000f0,
	0x90a000f0, 0x01452050,
};

static unsigned int dell_m4_1_pin_configs[10] = {
	0x0421101f, 0x04a11221, 0x40f000f0, 0x90170110,
	0x23a1902e, 0x23014250, 0x40f000f0, 0x90a000f0,
	0x40f000f0, 0x4f0000f0,
};

static unsigned int dell_m4_2_pin_configs[10] = {
	0x0421101f, 0x04a11221, 0x90a70330, 0x90170110,
	0x23a1902e, 0x23014250, 0x40f000f0, 0x40f000f0,
	0x40f000f0, 0x044413b0,
};

static unsigned int *stac92hd71bxx_brd_tbl[STAC_92HD71BXX_MODELS] = {
	[STAC_92HD71BXX_REF] = ref92hd71bxx_pin_configs,
	[STAC_DELL_M4_1]	= dell_m4_1_pin_configs,
	[STAC_DELL_M4_2]	= dell_m4_2_pin_configs,
};

static const char *stac92hd71bxx_models[STAC_92HD71BXX_MODELS] = {
	[STAC_92HD71BXX_REF] = "ref",
	[STAC_DELL_M4_1] = "dell-m4-1",
	[STAC_DELL_M4_2] = "dell-m4-2",
};

static struct snd_pci_quirk stac92hd71bxx_cfg_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668,
		      "DFI LanParty", STAC_92HD71BXX_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0233,
				"unknown Dell", STAC_DELL_M4_1),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0234,
				"unknown Dell", STAC_DELL_M4_1),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0250,
				"unknown Dell", STAC_DELL_M4_1),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x024f,
				"unknown Dell", STAC_DELL_M4_1),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x024d,
				"unknown Dell", STAC_DELL_M4_1),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0251,
				"unknown Dell", STAC_DELL_M4_1),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0277,
				"unknown Dell", STAC_DELL_M4_1),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0263,
				"unknown Dell", STAC_DELL_M4_2),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0265,
				"unknown Dell", STAC_DELL_M4_2),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0262,
				"unknown Dell", STAC_DELL_M4_2),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0264,
				"unknown Dell", STAC_DELL_M4_2),
	{} /* terminator */
};

static unsigned int ref922x_pin_configs[10] = {
	0x01014010, 0x01016011, 0x01012012, 0x0221401f,
	0x01813122, 0x01011014, 0x01441030, 0x01c41030,
	0x40000100, 0x40000100,
};

/*
    STAC 922X pin configs for
    102801A7
    102801AB
    102801A9
    102801D1
    102801D2
*/
static unsigned int dell_922x_d81_pin_configs[10] = {
	0x02214030, 0x01a19021, 0x01111012, 0x01114010,
	0x02a19020, 0x01117011, 0x400001f0, 0x400001f1,
	0x01813122, 0x400001f2,
};

/*
    STAC 922X pin configs for
    102801AC
    102801D0
*/
static unsigned int dell_922x_d82_pin_configs[10] = {
	0x02214030, 0x01a19021, 0x01111012, 0x01114010,
	0x02a19020, 0x01117011, 0x01451140, 0x400001f0,
	0x01813122, 0x400001f1,
};

/*
    STAC 922X pin configs for
    102801BF
*/
static unsigned int dell_922x_m81_pin_configs[10] = {
	0x0321101f, 0x01112024, 0x01111222, 0x91174220,
	0x03a11050, 0x01116221, 0x90a70330, 0x01452340, 
	0x40C003f1, 0x405003f0,
};

/*
    STAC 9221 A1 pin configs for
    102801D7 (Dell XPS M1210)
*/
static unsigned int dell_922x_m82_pin_configs[10] = {
	0x02211211, 0x408103ff, 0x02a1123e, 0x90100310, 
	0x408003f1, 0x0221121f, 0x03451340, 0x40c003f2, 
	0x508003f3, 0x405003f4, 
};

static unsigned int d945gtp3_pin_configs[10] = {
	0x0221401f, 0x01a19022, 0x01813021, 0x01014010,
	0x40000100, 0x40000100, 0x40000100, 0x40000100,
	0x02a19120, 0x40000100,
};

static unsigned int d945gtp5_pin_configs[10] = {
	0x0221401f, 0x01011012, 0x01813024, 0x01014010,
	0x01a19021, 0x01016011, 0x01452130, 0x40000100,
	0x02a19320, 0x40000100,
};

static unsigned int intel_mac_v1_pin_configs[10] = {
	0x0121e21f, 0x400000ff, 0x9017e110, 0x400000fd,
	0x400000fe, 0x0181e020, 0x1145e030, 0x11c5e240,
	0x400000fc, 0x400000fb,
};

static unsigned int intel_mac_v2_pin_configs[10] = {
	0x0121e21f, 0x90a7012e, 0x9017e110, 0x400000fd,
	0x400000fe, 0x0181e020, 0x1145e230, 0x500000fa,
	0x400000fc, 0x400000fb,
};

static unsigned int intel_mac_v3_pin_configs[10] = {
	0x0121e21f, 0x90a7012e, 0x9017e110, 0x400000fd,
	0x400000fe, 0x0181e020, 0x1145e230, 0x11c5e240,
	0x400000fc, 0x400000fb,
};

static unsigned int intel_mac_v4_pin_configs[10] = {
	0x0321e21f, 0x03a1e02e, 0x9017e110, 0x9017e11f,
	0x400000fe, 0x0381e020, 0x1345e230, 0x13c5e240,
	0x400000fc, 0x400000fb,
};

static unsigned int intel_mac_v5_pin_configs[10] = {
	0x0321e21f, 0x03a1e02e, 0x9017e110, 0x9017e11f,
	0x400000fe, 0x0381e020, 0x1345e230, 0x13c5e240,
	0x400000fc, 0x400000fb,
};


static unsigned int *stac922x_brd_tbl[STAC_922X_MODELS] = {
	[STAC_D945_REF] = ref922x_pin_configs,
	[STAC_D945GTP3] = d945gtp3_pin_configs,
	[STAC_D945GTP5] = d945gtp5_pin_configs,
	[STAC_INTEL_MAC_V1] = intel_mac_v1_pin_configs,
	[STAC_INTEL_MAC_V2] = intel_mac_v2_pin_configs,
	[STAC_INTEL_MAC_V3] = intel_mac_v3_pin_configs,
	[STAC_INTEL_MAC_V4] = intel_mac_v4_pin_configs,
	[STAC_INTEL_MAC_V5] = intel_mac_v5_pin_configs,
	[STAC_INTEL_MAC_AUTO] = intel_mac_v3_pin_configs,
	/* for backward compatibility */
	[STAC_MACMINI] = intel_mac_v3_pin_configs,
	[STAC_MACBOOK] = intel_mac_v5_pin_configs,
	[STAC_MACBOOK_PRO_V1] = intel_mac_v3_pin_configs,
	[STAC_MACBOOK_PRO_V2] = intel_mac_v3_pin_configs,
	[STAC_IMAC_INTEL] = intel_mac_v2_pin_configs,
	[STAC_IMAC_INTEL_20] = intel_mac_v3_pin_configs,
	[STAC_922X_DELL_D81] = dell_922x_d81_pin_configs,
	[STAC_922X_DELL_D82] = dell_922x_d82_pin_configs,	
	[STAC_922X_DELL_M81] = dell_922x_m81_pin_configs,
	[STAC_922X_DELL_M82] = dell_922x_m82_pin_configs,	
};

static const char *stac922x_models[STAC_922X_MODELS] = {
	[STAC_D945_REF]	= "ref",
	[STAC_D945GTP5]	= "5stack",
	[STAC_D945GTP3]	= "3stack",
	[STAC_INTEL_MAC_V1] = "intel-mac-v1",
	[STAC_INTEL_MAC_V2] = "intel-mac-v2",
	[STAC_INTEL_MAC_V3] = "intel-mac-v3",
	[STAC_INTEL_MAC_V4] = "intel-mac-v4",
	[STAC_INTEL_MAC_V5] = "intel-mac-v5",
	[STAC_INTEL_MAC_AUTO] = "intel-mac-auto",
	/* for backward compatibility */
	[STAC_MACMINI]	= "macmini",
	[STAC_MACBOOK]	= "macbook",
	[STAC_MACBOOK_PRO_V1]	= "macbook-pro-v1",
	[STAC_MACBOOK_PRO_V2]	= "macbook-pro",
	[STAC_IMAC_INTEL] = "imac-intel",
	[STAC_IMAC_INTEL_20] = "imac-intel-20",
	[STAC_922X_DELL_D81] = "dell-d81",
	[STAC_922X_DELL_D82] = "dell-d82",
	[STAC_922X_DELL_M81] = "dell-m81",
	[STAC_922X_DELL_M82] = "dell-m82",
};

static struct snd_pci_quirk stac922x_cfg_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668,
		      "DFI LanParty", STAC_D945_REF),
	/* Intel 945G based systems */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0101,
		      "Intel D945G", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0202,
		      "Intel D945G", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0606,
		      "Intel D945G", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0601,
		      "Intel D945G", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0111,
		      "Intel D945G", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x1115,
		      "Intel D945G", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x1116,
		      "Intel D945G", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x1117,
		      "Intel D945G", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x1118,
		      "Intel D945G", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x1119,
		      "Intel D945G", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x8826,
		      "Intel D945G", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x5049,
		      "Intel D945G", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x5055,
		      "Intel D945G", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x5048,
		      "Intel D945G", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0110,
		      "Intel D945G", STAC_D945GTP3),
	/* Intel D945G 5-stack systems */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0404,
		      "Intel D945G", STAC_D945GTP5),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0303,
		      "Intel D945G", STAC_D945GTP5),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0013,
		      "Intel D945G", STAC_D945GTP5),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0417,
		      "Intel D945G", STAC_D945GTP5),
	/* Intel 945P based systems */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0b0b,
		      "Intel D945P", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0112,
		      "Intel D945P", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0d0d,
		      "Intel D945P", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0909,
		      "Intel D945P", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0505,
		      "Intel D945P", STAC_D945GTP3),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0707,
		      "Intel D945P", STAC_D945GTP5),
	/* other systems  */
	/* Apple Intel Mac (Mac Mini, MacBook, MacBook Pro...) */
	SND_PCI_QUIRK(0x8384, 0x7680,
		      "Mac", STAC_INTEL_MAC_AUTO),
	/* Dell systems  */
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01a7,
		      "unknown Dell", STAC_922X_DELL_D81),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01a9,
		      "unknown Dell", STAC_922X_DELL_D81),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01ab,
		      "unknown Dell", STAC_922X_DELL_D81),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01ac,
		      "unknown Dell", STAC_922X_DELL_D82),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01bf,
		      "unknown Dell", STAC_922X_DELL_M81),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01d0,
		      "unknown Dell", STAC_922X_DELL_D82),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01d1,
		      "unknown Dell", STAC_922X_DELL_D81),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01d2,
		      "unknown Dell", STAC_922X_DELL_D81),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01d7,
		      "Dell XPS M1210", STAC_922X_DELL_M82),
	{} /* terminator */
};

static unsigned int ref927x_pin_configs[14] = {
	0x02214020, 0x02a19080, 0x0181304e, 0x01014010,
	0x01a19040, 0x01011012, 0x01016011, 0x0101201f, 
	0x183301f0, 0x18a001f0, 0x18a001f0, 0x01442070,
	0x01c42190, 0x40000100,
};

static unsigned int d965_3st_pin_configs[14] = {
	0x0221401f, 0x02a19120, 0x40000100, 0x01014011,
	0x01a19021, 0x01813024, 0x40000100, 0x40000100,
	0x40000100, 0x40000100, 0x40000100, 0x40000100,
	0x40000100, 0x40000100
};

static unsigned int d965_5st_pin_configs[14] = {
	0x02214020, 0x02a19080, 0x0181304e, 0x01014010,
	0x01a19040, 0x01011012, 0x01016011, 0x40000100,
	0x40000100, 0x40000100, 0x40000100, 0x01442070,
	0x40000100, 0x40000100
};

static unsigned int dell_3st_pin_configs[14] = {
	0x02211230, 0x02a11220, 0x01a19040, 0x01114210,
	0x01111212, 0x01116211, 0x01813050, 0x01112214,
	0x403003fa, 0x90a60040, 0x90a60040, 0x404003fb,
	0x40c003fc, 0x40000100
};

static unsigned int *stac927x_brd_tbl[STAC_927X_MODELS] = {
	[STAC_D965_REF]  = ref927x_pin_configs,
	[STAC_D965_3ST]  = d965_3st_pin_configs,
	[STAC_D965_5ST]  = d965_5st_pin_configs,
	[STAC_DELL_3ST]  = dell_3st_pin_configs,
	[STAC_DELL_BIOS] = NULL,
};

static const char *stac927x_models[STAC_927X_MODELS] = {
	[STAC_D965_REF]		= "ref",
	[STAC_D965_3ST]		= "3stack",
	[STAC_D965_5ST]		= "5stack",
	[STAC_DELL_3ST]		= "dell-3stack",
	[STAC_DELL_BIOS]	= "dell-bios",
};

static struct snd_pci_quirk stac927x_cfg_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668,
		      "DFI LanParty", STAC_D965_REF),
	 /* Intel 946 based systems */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x3d01, "Intel D946", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0xa301, "Intel D946", STAC_D965_3ST),
	/* 965 based 3 stack systems */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2116, "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2115, "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2114, "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2113, "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2112, "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2111, "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2110, "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2009, "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2008, "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2007, "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2006, "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2005, "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2004, "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2003, "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2002, "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2001, "Intel D965", STAC_D965_3ST),
	/* Dell 3 stack systems */
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x01f7, "Dell XPS M1730", STAC_DELL_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x01dd, "Dell Dimension E520", STAC_DELL_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x01ed, "Dell     ", STAC_DELL_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x01f4, "Dell     ", STAC_DELL_3ST),
	/* Dell 3 stack systems with verb table in BIOS */
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x01f3, "Dell Inspiron 1420", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x0227, "Dell Vostro 1400  ", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x022f, "Dell     ", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x022e, "Dell     ", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x0242, "Dell     ", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x0243, "Dell     ", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x02ff, "Dell     ", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x0209, "Dell XPS 1330", STAC_DELL_BIOS),
	/* 965 based 5 stack systems */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2301, "Intel D965", STAC_D965_5ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2302, "Intel D965", STAC_D965_5ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2303, "Intel D965", STAC_D965_5ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2304, "Intel D965", STAC_D965_5ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2305, "Intel D965", STAC_D965_5ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2501, "Intel D965", STAC_D965_5ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2502, "Intel D965", STAC_D965_5ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2503, "Intel D965", STAC_D965_5ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2504, "Intel D965", STAC_D965_5ST),
	{} /* terminator */
};

static unsigned int ref9205_pin_configs[12] = {
	0x40000100, 0x40000100, 0x01016011, 0x01014010,
	0x01813122, 0x01a19021, 0x01019020, 0x40000100,
	0x90a000f0, 0x90a000f0, 0x01441030, 0x01c41030
};

/*
    STAC 9205 pin configs for
    102801F1
    102801F2
    102801FC
    102801FD
    10280204
    1028021F
    10280228 (Dell Vostro 1500)
*/
static unsigned int dell_9205_m42_pin_configs[12] = {
	0x0321101F, 0x03A11020, 0x400003FA, 0x90170310,
	0x400003FB, 0x400003FC, 0x400003FD, 0x40F000F9,
	0x90A60330, 0x400003FF, 0x0144131F, 0x40C003FE,
};

/*
    STAC 9205 pin configs for
    102801F9
    102801FA
    102801FE
    102801FF (Dell Precision M4300)
    10280206
    10280200
    10280201
*/
static unsigned int dell_9205_m43_pin_configs[12] = {
	0x0321101f, 0x03a11020, 0x90a70330, 0x90170310,
	0x400000fe, 0x400000ff, 0x400000fd, 0x40f000f9,
	0x400000fa, 0x400000fc, 0x0144131f, 0x40c003f8,
};

static unsigned int dell_9205_m44_pin_configs[12] = {
	0x0421101f, 0x04a11020, 0x400003fa, 0x90170310,
	0x400003fb, 0x400003fc, 0x400003fd, 0x400003f9,
	0x90a60330, 0x400003ff, 0x01441340, 0x40c003fe,
};

static unsigned int *stac9205_brd_tbl[STAC_9205_MODELS] = {
	[STAC_9205_REF] = ref9205_pin_configs,
	[STAC_9205_DELL_M42] = dell_9205_m42_pin_configs,
	[STAC_9205_DELL_M43] = dell_9205_m43_pin_configs,
	[STAC_9205_DELL_M44] = dell_9205_m44_pin_configs,
};

static const char *stac9205_models[STAC_9205_MODELS] = {
	[STAC_9205_REF] = "ref",
	[STAC_9205_DELL_M42] = "dell-m42",
	[STAC_9205_DELL_M43] = "dell-m43",
	[STAC_9205_DELL_M44] = "dell-m44",
};

static struct snd_pci_quirk stac9205_cfg_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668,
		      "DFI LanParty", STAC_9205_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01f1,
		      "unknown Dell", STAC_9205_DELL_M42),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01f2,
		      "unknown Dell", STAC_9205_DELL_M42),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01f8,
		      "Dell Precision", STAC_9205_DELL_M43),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01f9,
		      "Dell Precision", STAC_9205_DELL_M43),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01fa,
		      "Dell Precision", STAC_9205_DELL_M43),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01fc,
		      "unknown Dell", STAC_9205_DELL_M42),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01fd,
		      "unknown Dell", STAC_9205_DELL_M42),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01fe,
		      "Dell Precision", STAC_9205_DELL_M43),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x01ff,
		      "Dell Precision M4300", STAC_9205_DELL_M43),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0204,
		      "unknown Dell", STAC_9205_DELL_M42),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0206,
		      "Dell Precision", STAC_9205_DELL_M43),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x021b,
		      "Dell Precision", STAC_9205_DELL_M43),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x021c,
		      "Dell Precision", STAC_9205_DELL_M43),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x021f,
		      "Dell Inspiron", STAC_9205_DELL_M44),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0228,
		      "Dell Vostro 1500", STAC_9205_DELL_M42),
	{} /* terminator */
};

static int stac92xx_save_bios_config_regs(struct hda_codec *codec)
{
	int i;
	struct sigmatel_spec *spec = codec->spec;
	
	if (! spec->bios_pin_configs) {
		spec->bios_pin_configs = kcalloc(spec->num_pins,
		                                 sizeof(*spec->bios_pin_configs), GFP_KERNEL);
		if (! spec->bios_pin_configs)
			return -ENOMEM;
	}
	
	for (i = 0; i < spec->num_pins; i++) {
		hda_nid_t nid = spec->pin_nids[i];
		unsigned int pin_cfg;
		
		pin_cfg = snd_hda_codec_read(codec, nid, 0, 
			AC_VERB_GET_CONFIG_DEFAULT, 0x00);	
		snd_printdd(KERN_INFO "hda_codec: pin nid %2.2x bios pin config %8.8x\n",
					nid, pin_cfg);
		spec->bios_pin_configs[i] = pin_cfg;
	}
	
	return 0;
}

static void stac92xx_set_config_reg(struct hda_codec *codec,
				    hda_nid_t pin_nid, unsigned int pin_config)
{
	int i;
	snd_hda_codec_write(codec, pin_nid, 0,
			    AC_VERB_SET_CONFIG_DEFAULT_BYTES_0,
			    pin_config & 0x000000ff);
	snd_hda_codec_write(codec, pin_nid, 0,
			    AC_VERB_SET_CONFIG_DEFAULT_BYTES_1,
			    (pin_config & 0x0000ff00) >> 8);
	snd_hda_codec_write(codec, pin_nid, 0,
			    AC_VERB_SET_CONFIG_DEFAULT_BYTES_2,
			    (pin_config & 0x00ff0000) >> 16);
	snd_hda_codec_write(codec, pin_nid, 0,
			    AC_VERB_SET_CONFIG_DEFAULT_BYTES_3,
			    pin_config >> 24);
	i = snd_hda_codec_read(codec, pin_nid, 0,
			       AC_VERB_GET_CONFIG_DEFAULT,
			       0x00);	
	snd_printdd(KERN_INFO "hda_codec: pin nid %2.2x pin config %8.8x\n",
		    pin_nid, i);
}

static void stac92xx_set_config_regs(struct hda_codec *codec)
{
	int i;
	struct sigmatel_spec *spec = codec->spec;

 	if (!spec->pin_configs)
 		return;

	for (i = 0; i < spec->num_pins; i++)
		stac92xx_set_config_reg(codec, spec->pin_nids[i],
					spec->pin_configs[i]);
}

/*
 * Analog playback callbacks
 */
static int stac92xx_playback_pcm_open(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream,
					     hinfo);
}

static int stac92xx_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 unsigned int stream_tag,
					 unsigned int format,
					 struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout, stream_tag, format, substream);
}

static int stac92xx_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital playback callbacks
 */
static int stac92xx_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					  struct hda_codec *codec,
					  struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int stac92xx_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

static int stac92xx_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 unsigned int stream_tag,
					 unsigned int format,
					 struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout,
					     stream_tag, format, substream);
}


/*
 * Analog capture callbacks
 */
static int stac92xx_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					unsigned int stream_tag,
					unsigned int format,
					struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number],
                                   stream_tag, 0, format);
	return 0;
}

static int stac92xx_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;

	snd_hda_codec_cleanup_stream(codec, spec->adc_nids[substream->number]);
	return 0;
}

static struct hda_pcm_stream stac92xx_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in stac92xx_build_pcms */
	.ops = {
		.open = stac92xx_dig_playback_pcm_open,
		.close = stac92xx_dig_playback_pcm_close,
		.prepare = stac92xx_dig_playback_pcm_prepare
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

static struct hda_pcm_stream stac92xx_pcm_analog_alt_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0x06, /* NID to query formats and rates */
	.ops = {
		.open = stac92xx_playback_pcm_open,
		.prepare = stac92xx_playback_pcm_prepare,
		.cleanup = stac92xx_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream stac92xx_pcm_analog_capture = {
	.channels_min = 2,
	.channels_max = 2,
	/* NID + .substreams is set in stac92xx_build_pcms */
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
	info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adc_nids[0];
	info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams = spec->num_adcs;

	if (spec->alt_switch) {
		codec->num_pcms++;
		info++;
		info->name = "STAC92xx Analog Alt";
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] = stac92xx_pcm_analog_alt_playback;
	}

	if (spec->multiout.dig_out_nid || spec->dig_in_nid) {
		codec->num_pcms++;
		info++;
		info->name = "STAC92xx Digital";
		info->pcm_type = HDA_PCM_TYPE_SPDIF;
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

static unsigned int stac92xx_get_vref(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int pincap = snd_hda_param_read(codec, nid,
						 AC_PAR_PIN_CAP);
	pincap = (pincap & AC_PINCAP_VREF) >> AC_PINCAP_VREF_SHIFT;
	if (pincap & AC_PINCAP_VREF_100)
		return AC_PINCTL_VREF_100;
	if (pincap & AC_PINCAP_VREF_80)
		return AC_PINCTL_VREF_80;
	if (pincap & AC_PINCAP_VREF_50)
		return AC_PINCTL_VREF_50;
	if (pincap & AC_PINCAP_VREF_GRD)
		return AC_PINCTL_VREF_GRD;
	return 0;
}

static void stac92xx_auto_set_pinctl(struct hda_codec *codec, hda_nid_t nid, int pin_type)

{
	snd_hda_codec_write_cache(codec, nid, 0,
				  AC_VERB_SET_PIN_WIDGET_CONTROL, pin_type);
}

#define stac92xx_hp_switch_info		snd_ctl_boolean_mono_info

static int stac92xx_hp_switch_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;

	ucontrol->value.integer.value[0] = spec->hp_switch;
	return 0;
}

static int stac92xx_hp_switch_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;

	spec->hp_switch = ucontrol->value.integer.value[0];

	/* check to be sure that the ports are upto date with
	 * switch changes
	 */
	codec->patch_ops.unsol_event(codec, STAC_HP_EVENT << 26);

	return 1;
}

#define stac92xx_io_switch_info		snd_ctl_boolean_mono_info

static int stac92xx_io_switch_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	int io_idx = kcontrol-> private_value & 0xff;

	ucontrol->value.integer.value[0] = spec->io_switch[io_idx];
	return 0;
}

static int stac92xx_io_switch_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
        struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
        hda_nid_t nid = kcontrol->private_value >> 8;
	int io_idx = kcontrol-> private_value & 0xff;
	unsigned short val = !!ucontrol->value.integer.value[0];

	spec->io_switch[io_idx] = val;

	if (val)
		stac92xx_auto_set_pinctl(codec, nid, AC_PINCTL_OUT_EN);
	else {
		unsigned int pinctl = AC_PINCTL_IN_EN;
		if (io_idx) /* set VREF for mic */
			pinctl |= stac92xx_get_vref(codec, nid);
		stac92xx_auto_set_pinctl(codec, nid, pinctl);
	}

	/* check the auto-mute again: we need to mute/unmute the speaker
	 * appropriately according to the pin direction
	 */
	if (spec->hp_detect)
		codec->patch_ops.unsol_event(codec, STAC_HP_EVENT << 26);

        return 1;
}

#define stac92xx_clfe_switch_info snd_ctl_boolean_mono_info

static int stac92xx_clfe_switch_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;

	ucontrol->value.integer.value[0] = spec->clfe_swap;
	return 0;
}

static int stac92xx_clfe_switch_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t nid = kcontrol->private_value & 0xff;
	unsigned int val = !!ucontrol->value.integer.value[0];

	if (spec->clfe_swap == val)
		return 0;

	spec->clfe_swap = val;

	snd_hda_codec_write_cache(codec, nid, 0, AC_VERB_SET_EAPD_BTLENABLE,
		spec->clfe_swap ? 0x4 : 0x0);

	return 1;
}

#define STAC_CODEC_HP_SWITCH(xname) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	  .name = xname, \
	  .index = 0, \
	  .info = stac92xx_hp_switch_info, \
	  .get = stac92xx_hp_switch_get, \
	  .put = stac92xx_hp_switch_put, \
	}

#define STAC_CODEC_IO_SWITCH(xname, xpval) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	  .name = xname, \
	  .index = 0, \
          .info = stac92xx_io_switch_info, \
          .get = stac92xx_io_switch_get, \
          .put = stac92xx_io_switch_put, \
          .private_value = xpval, \
	}

#define STAC_CODEC_CLFE_SWITCH(xname, xpval) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	  .name = xname, \
	  .index = 0, \
	  .info = stac92xx_clfe_switch_info, \
	  .get = stac92xx_clfe_switch_get, \
	  .put = stac92xx_clfe_switch_put, \
	  .private_value = xpval, \
	}

enum {
	STAC_CTL_WIDGET_VOL,
	STAC_CTL_WIDGET_MUTE,
	STAC_CTL_WIDGET_MONO_MUX,
	STAC_CTL_WIDGET_HP_SWITCH,
	STAC_CTL_WIDGET_IO_SWITCH,
	STAC_CTL_WIDGET_CLFE_SWITCH
};

static struct snd_kcontrol_new stac92xx_control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	HDA_CODEC_MUTE(NULL, 0, 0, 0),
	STAC_MONO_MUX,
	STAC_CODEC_HP_SWITCH(NULL),
	STAC_CODEC_IO_SWITCH(NULL, 0),
	STAC_CODEC_CLFE_SWITCH(NULL, 0),
};

/* add dynamic controls */
static int stac92xx_add_control(struct sigmatel_spec *spec, int type, const char *name, unsigned long val)
{
	struct snd_kcontrol_new *knew;

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

/* flag inputs as additional dynamic lineouts */
static int stac92xx_add_dyn_out_pins(struct hda_codec *codec, struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	unsigned int wcaps, wtype;
	int i, num_dacs = 0;
	
	/* use the wcaps cache to count all DACs available for line-outs */
	for (i = 0; i < codec->num_nodes; i++) {
		wcaps = codec->wcaps[i];
		wtype = (wcaps & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT;

		if (wtype == AC_WID_AUD_OUT && !(wcaps & AC_WCAP_DIGITAL))
			num_dacs++;
	}

	snd_printdd("%s: total dac count=%d\n", __func__, num_dacs);
	
	switch (cfg->line_outs) {
	case 3:
		/* add line-in as side */
		if (cfg->input_pins[AUTO_PIN_LINE] && num_dacs > 3) {
			cfg->line_out_pins[cfg->line_outs] =
				cfg->input_pins[AUTO_PIN_LINE];
			spec->line_switch = 1;
			cfg->line_outs++;
		}
		break;
	case 2:
		/* add line-in as clfe and mic as side */
		if (cfg->input_pins[AUTO_PIN_LINE] && num_dacs > 2) {
			cfg->line_out_pins[cfg->line_outs] =
				cfg->input_pins[AUTO_PIN_LINE];
			spec->line_switch = 1;
			cfg->line_outs++;
		}
		if (cfg->input_pins[AUTO_PIN_MIC] && num_dacs > 3) {
			cfg->line_out_pins[cfg->line_outs] =
				cfg->input_pins[AUTO_PIN_MIC];
			spec->mic_switch = 1;
			cfg->line_outs++;
		}
		break;
	case 1:
		/* add line-in as surr and mic as clfe */
		if (cfg->input_pins[AUTO_PIN_LINE] && num_dacs > 1) {
			cfg->line_out_pins[cfg->line_outs] =
				cfg->input_pins[AUTO_PIN_LINE];
			spec->line_switch = 1;
			cfg->line_outs++;
		}
		if (cfg->input_pins[AUTO_PIN_MIC] && num_dacs > 2) {
			cfg->line_out_pins[cfg->line_outs] =
				cfg->input_pins[AUTO_PIN_MIC];
			spec->mic_switch = 1;
			cfg->line_outs++;
		}
		break;
	}

	return 0;
}


static int is_in_dac_nids(struct sigmatel_spec *spec, hda_nid_t nid)
{
	int i;
	
	for (i = 0; i < spec->multiout.num_dacs; i++) {
		if (spec->multiout.dac_nids[i] == nid)
			return 1;
	}

	return 0;
}

/*
 * Fill in the dac_nids table from the parsed pin configuration
 * This function only works when every pin in line_out_pins[]
 * contains atleast one DAC in its connection list. Some 92xx
 * codecs are not connected directly to a DAC, such as the 9200
 * and 9202/925x. For those, dac_nids[] must be hard-coded.
 */
static int stac92xx_auto_fill_dac_nids(struct hda_codec *codec,
				       struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	int i, j, conn_len = 0; 
	hda_nid_t nid, conn[HDA_MAX_CONNECTIONS];
	unsigned int wcaps, wtype;
	
	for (i = 0; i < cfg->line_outs; i++) {
		nid = cfg->line_out_pins[i];
		conn_len = snd_hda_get_connections(codec, nid, conn,
						   HDA_MAX_CONNECTIONS);
		for (j = 0; j < conn_len; j++) {
			wcaps = snd_hda_param_read(codec, conn[j],
						   AC_PAR_AUDIO_WIDGET_CAP);
			wtype = (wcaps & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT;
			if (wtype != AC_WID_AUD_OUT ||
			    (wcaps & AC_WCAP_DIGITAL))
				continue;
			/* conn[j] is a DAC routed to this line-out */
			if (!is_in_dac_nids(spec, conn[j]))
				break;
		}

		if (j == conn_len) {
			if (spec->multiout.num_dacs > 0) {
				/* we have already working output pins,
				 * so let's drop the broken ones again
				 */
				cfg->line_outs = spec->multiout.num_dacs;
				break;
			}
			/* error out, no available DAC found */
			snd_printk(KERN_ERR
				   "%s: No available DAC for pin 0x%x\n",
				   __func__, nid);
			return -ENODEV;
		}

		spec->multiout.dac_nids[i] = conn[j];
		spec->multiout.num_dacs++;
		if (conn_len > 1) {
			/* select this DAC in the pin's input mux */
			snd_hda_codec_write_cache(codec, nid, 0,
						  AC_VERB_SET_CONNECT_SEL, j);

		}
	}

	snd_printd("dac_nids=%d (0x%x/0x%x/0x%x/0x%x/0x%x)\n",
		   spec->multiout.num_dacs,
		   spec->multiout.dac_nids[0],
		   spec->multiout.dac_nids[1],
		   spec->multiout.dac_nids[2],
		   spec->multiout.dac_nids[3],
		   spec->multiout.dac_nids[4]);
	return 0;
}

/* create volume control/switch for the given prefx type */
static int create_controls(struct sigmatel_spec *spec, const char *pfx, hda_nid_t nid, int chs)
{
	char name[32];
	int err;

	sprintf(name, "%s Playback Volume", pfx);
	err = stac92xx_add_control(spec, STAC_CTL_WIDGET_VOL, name,
				   HDA_COMPOSE_AMP_VAL(nid, chs, 0, HDA_OUTPUT));
	if (err < 0)
		return err;
	sprintf(name, "%s Playback Switch", pfx);
	err = stac92xx_add_control(spec, STAC_CTL_WIDGET_MUTE, name,
				   HDA_COMPOSE_AMP_VAL(nid, chs, 0, HDA_OUTPUT));
	if (err < 0)
		return err;
	return 0;
}

static int add_spec_dacs(struct sigmatel_spec *spec, hda_nid_t nid)
{
	if (!spec->multiout.hp_nid)
		spec->multiout.hp_nid = nid;
	else if (spec->multiout.num_dacs > 4) {
		printk(KERN_WARNING "stac92xx: No space for DAC 0x%x\n", nid);
		return 1;
	} else {
		spec->multiout.dac_nids[spec->multiout.num_dacs] = nid;
		spec->multiout.num_dacs++;
	}
	return 0;
}

static int check_in_dac_nids(struct sigmatel_spec *spec, hda_nid_t nid)
{
	if (is_in_dac_nids(spec, nid))
		return 1;
	if (spec->multiout.hp_nid == nid)
		return 1;
	return 0;
}

/* add playback controls from the parsed DAC table */
static int stac92xx_auto_create_multi_out_ctls(struct hda_codec *codec,
					       const struct auto_pin_cfg *cfg)
{
	static const char *chname[4] = {
		"Front", "Surround", NULL /*CLFE*/, "Side"
	};
	hda_nid_t nid;
	int i, err;

	struct sigmatel_spec *spec = codec->spec;
	unsigned int wid_caps, pincap;


	for (i = 0; i < cfg->line_outs && i < spec->multiout.num_dacs; i++) {
		if (!spec->multiout.dac_nids[i])
			continue;

		nid = spec->multiout.dac_nids[i];

		if (i == 2) {
			/* Center/LFE */
			err = create_controls(spec, "Center", nid, 1);
			if (err < 0)
				return err;
			err = create_controls(spec, "LFE", nid, 2);
			if (err < 0)
				return err;

			wid_caps = get_wcaps(codec, nid);

			if (wid_caps & AC_WCAP_LR_SWAP) {
				err = stac92xx_add_control(spec,
					STAC_CTL_WIDGET_CLFE_SWITCH,
					"Swap Center/LFE Playback Switch", nid);

				if (err < 0)
					return err;
			}

		} else {
			err = create_controls(spec, chname[i], nid, 3);
			if (err < 0)
				return err;
		}
	}

	if (cfg->hp_outs > 1) {
		err = stac92xx_add_control(spec,
			STAC_CTL_WIDGET_HP_SWITCH,
			"Headphone as Line Out Switch", 0);
		if (err < 0)
			return err;
	}

	if (spec->line_switch) {
		nid = cfg->input_pins[AUTO_PIN_LINE];
		pincap = snd_hda_param_read(codec, nid,
						AC_PAR_PIN_CAP);
		if (pincap & AC_PINCAP_OUT) {
			err = stac92xx_add_control(spec,
				STAC_CTL_WIDGET_IO_SWITCH,
				"Line In as Output Switch", nid << 8);
			if (err < 0)
				return err;
		}
	}

	if (spec->mic_switch) {
		unsigned int def_conf;
		unsigned int mic_pin = AUTO_PIN_MIC;
again:
		nid = cfg->input_pins[mic_pin];
		def_conf = snd_hda_codec_read(codec, nid, 0,
						AC_VERB_GET_CONFIG_DEFAULT, 0);
		/* some laptops have an internal analog microphone
		 * which can't be used as a output */
		if (get_defcfg_connect(def_conf) != AC_JACK_PORT_FIXED) {
			pincap = snd_hda_param_read(codec, nid,
							AC_PAR_PIN_CAP);
			if (pincap & AC_PINCAP_OUT) {
				err = stac92xx_add_control(spec,
					STAC_CTL_WIDGET_IO_SWITCH,
					"Mic as Output Switch", (nid << 8) | 1);
				nid = snd_hda_codec_read(codec, nid, 0,
					 AC_VERB_GET_CONNECT_LIST, 0) & 0xff;
				if (!check_in_dac_nids(spec, nid))
					add_spec_dacs(spec, nid);
				if (err < 0)
					return err;
			}
		} else if (mic_pin == AUTO_PIN_MIC) {
			mic_pin = AUTO_PIN_FRONT_MIC;
			goto again;
		}
	}

	return 0;
}

/* add playback controls for Speaker and HP outputs */
static int stac92xx_auto_create_hp_ctls(struct hda_codec *codec,
					struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t nid;
	int i, old_num_dacs, err;

	old_num_dacs = spec->multiout.num_dacs;
	for (i = 0; i < cfg->hp_outs; i++) {
		unsigned int wid_caps = get_wcaps(codec, cfg->hp_pins[i]);
		if (wid_caps & AC_WCAP_UNSOL_CAP)
			spec->hp_detect = 1;
		nid = snd_hda_codec_read(codec, cfg->hp_pins[i], 0,
					 AC_VERB_GET_CONNECT_LIST, 0) & 0xff;
		if (check_in_dac_nids(spec, nid))
			nid = 0;
		if (! nid)
			continue;
		add_spec_dacs(spec, nid);
	}
	for (i = 0; i < cfg->speaker_outs; i++) {
		nid = snd_hda_codec_read(codec, cfg->speaker_pins[i], 0,
					 AC_VERB_GET_CONNECT_LIST, 0) & 0xff;
		if (check_in_dac_nids(spec, nid))
			nid = 0;
		if (! nid)
			continue;
		add_spec_dacs(spec, nid);
	}
	for (i = 0; i < cfg->line_outs; i++) {
		nid = snd_hda_codec_read(codec, cfg->line_out_pins[i], 0,
					AC_VERB_GET_CONNECT_LIST, 0) & 0xff;
		if (check_in_dac_nids(spec, nid))
			nid = 0;
		if (! nid)
			continue;
		add_spec_dacs(spec, nid);
	}
	for (i = old_num_dacs; i < spec->multiout.num_dacs; i++) {
		static const char *pfxs[] = {
			"Speaker", "External Speaker", "Speaker2",
		};
		err = create_controls(spec, pfxs[i - old_num_dacs],
				      spec->multiout.dac_nids[i], 3);
		if (err < 0)
			return err;
	}
	if (spec->multiout.hp_nid) {
		err = create_controls(spec, "Headphone",
				      spec->multiout.hp_nid, 3);
		if (err < 0)
			return err;
	}

	return 0;
}

/* labels for mono mux outputs */
static const char *stac92xx_mono_labels[3] = {
	"DAC0", "DAC1", "Mixer"
};

/* create mono mux for mono out on capable codecs */
static int stac92xx_auto_create_mono_output_ctls(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	struct hda_input_mux *mono_mux = &spec->private_mono_mux;
	int i, num_cons;
	hda_nid_t con_lst[ARRAY_SIZE(stac92xx_mono_labels)];

	num_cons = snd_hda_get_connections(codec,
				spec->mono_nid,
				con_lst,
				HDA_MAX_NUM_INPUTS);
	if (!num_cons || num_cons > ARRAY_SIZE(stac92xx_mono_labels))
		return -EINVAL;

	for (i = 0; i < num_cons; i++) {
		mono_mux->items[mono_mux->num_items].label =
					stac92xx_mono_labels[i];
		mono_mux->items[mono_mux->num_items].index = i;
		mono_mux->num_items++;
	}

	return stac92xx_add_control(spec, STAC_CTL_WIDGET_MONO_MUX,
				"Mono Mux", spec->mono_nid);
}

/* labels for dmic mux inputs */
static const char *stac92xx_dmic_labels[5] = {
	"Analog Inputs", "Digital Mic 1", "Digital Mic 2",
	"Digital Mic 3", "Digital Mic 4"
};

/* create playback/capture controls for input pins on dmic capable codecs */
static int stac92xx_auto_create_dmic_input_ctls(struct hda_codec *codec,
						const struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	struct hda_input_mux *dimux = &spec->private_dimux;
	hda_nid_t con_lst[HDA_MAX_NUM_INPUTS];
	int err, i, j;
	char name[32];

	dimux->items[dimux->num_items].label = stac92xx_dmic_labels[0];
	dimux->items[dimux->num_items].index = 0;
	dimux->num_items++;

	for (i = 0; i < spec->num_dmics; i++) {
		hda_nid_t nid;
		int index;
		int num_cons;
		unsigned int wcaps;
		unsigned int def_conf;

		def_conf = snd_hda_codec_read(codec,
					      spec->dmic_nids[i],
					      0,
					      AC_VERB_GET_CONFIG_DEFAULT,
					      0);
		if (get_defcfg_connect(def_conf) == AC_JACK_PORT_NONE)
			continue;

		nid = spec->dmic_nids[i];
		num_cons = snd_hda_get_connections(codec,
				spec->dmux_nids[0],
				con_lst,
				HDA_MAX_NUM_INPUTS);
		for (j = 0; j < num_cons; j++)
			if (con_lst[j] == nid) {
				index = j;
				goto found;
			}
		continue;
found:
		wcaps = get_wcaps(codec, nid);

		if (wcaps & AC_WCAP_OUT_AMP) {
			sprintf(name, "%s Capture Volume",
				stac92xx_dmic_labels[dimux->num_items]);

			err = stac92xx_add_control(spec,
				STAC_CTL_WIDGET_VOL,
				name,
				HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
		}

		dimux->items[dimux->num_items].label =
			stac92xx_dmic_labels[dimux->num_items];
		dimux->items[dimux->num_items].index = index;
		dimux->num_items++;
	}

	return 0;
}

/* create playback/capture controls for input pins */
static int stac92xx_auto_create_analog_input_ctls(struct hda_codec *codec, const struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->private_imux;
	hda_nid_t con_lst[HDA_MAX_NUM_INPUTS];
	int i, j, k;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		int index;

		if (!cfg->input_pins[i])
			continue;
		index = -1;
		for (j = 0; j < spec->num_muxes; j++) {
			int num_cons;
			num_cons = snd_hda_get_connections(codec,
							   spec->mux_nids[j],
							   con_lst,
							   HDA_MAX_NUM_INPUTS);
			for (k = 0; k < num_cons; k++)
				if (con_lst[k] == cfg->input_pins[i]) {
					index = k;
					goto found;
				}
		}
		continue;
	found:
		imux->items[imux->num_items].label = auto_pin_cfg_labels[i];
		imux->items[imux->num_items].index = index;
		imux->num_items++;
	}

	if (imux->num_items) {
		/*
		 * Set the current input for the muxes.
		 * The STAC9221 has two input muxes with identical source
		 * NID lists.  Hopefully this won't get confused.
		 */
		for (i = 0; i < spec->num_muxes; i++) {
			snd_hda_codec_write_cache(codec, spec->mux_nids[i], 0,
						  AC_VERB_SET_CONNECT_SEL,
						  imux->items[0].index);
		}
	}

	return 0;
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
	int i;

	for (i = 0; i < spec->autocfg.hp_outs; i++) {
		hda_nid_t pin;
		pin = spec->autocfg.hp_pins[i];
		if (pin) /* connect to front */
			stac92xx_auto_set_pinctl(codec, pin, AC_PINCTL_OUT_EN | AC_PINCTL_HP_EN);
	}
	for (i = 0; i < spec->autocfg.speaker_outs; i++) {
		hda_nid_t pin;
		pin = spec->autocfg.speaker_pins[i];
		if (pin) /* connect to front */
			stac92xx_auto_set_pinctl(codec, pin, AC_PINCTL_OUT_EN);
	}
}

static int stac92xx_parse_auto_config(struct hda_codec *codec, hda_nid_t dig_out, hda_nid_t dig_in)
{
	struct sigmatel_spec *spec = codec->spec;
	int err;
	int hp_speaker_swap = 0;

	if ((err = snd_hda_parse_pin_def_config(codec,
						&spec->autocfg,
						spec->dmic_nids)) < 0)
		return err;
	if (! spec->autocfg.line_outs)
		return 0; /* can't find valid pin config */

	/* If we have no real line-out pin and multiple hp-outs, HPs should
	 * be set up as multi-channel outputs.
	 */
	if (spec->autocfg.line_out_type == AUTO_PIN_SPEAKER_OUT &&
	    spec->autocfg.hp_outs > 1) {
		/* Copy hp_outs to line_outs, backup line_outs in
		 * speaker_outs so that the following routines can handle
		 * HP pins as primary outputs.
		 */
		memcpy(spec->autocfg.speaker_pins, spec->autocfg.line_out_pins,
		       sizeof(spec->autocfg.line_out_pins));
		spec->autocfg.speaker_outs = spec->autocfg.line_outs;
		memcpy(spec->autocfg.line_out_pins, spec->autocfg.hp_pins,
		       sizeof(spec->autocfg.hp_pins));
		spec->autocfg.line_outs = spec->autocfg.hp_outs;
		hp_speaker_swap = 1;
	}
	if (spec->autocfg.mono_out_pin) {
		int dir = (get_wcaps(codec, spec->autocfg.mono_out_pin)
				& AC_WCAP_OUT_AMP) ? HDA_OUTPUT : HDA_INPUT;
		u32 caps = query_amp_caps(codec,
				spec->autocfg.mono_out_pin, dir);
		hda_nid_t conn_list[1];

		/* get the mixer node and then the mono mux if it exists */
		if (snd_hda_get_connections(codec,
				spec->autocfg.mono_out_pin, conn_list, 1) &&
				snd_hda_get_connections(codec, conn_list[0],
				conn_list, 1)) {

				int wcaps = get_wcaps(codec, conn_list[0]);
				int wid_type = (wcaps & AC_WCAP_TYPE)
					>> AC_WCAP_TYPE_SHIFT;
				/* LR swap check, some stac925x have a mux that
 				 * changes the DACs output path instead of the
 				 * mono-mux path.
 				 */
				if (wid_type == AC_WID_AUD_SEL &&
						!(wcaps & AC_WCAP_LR_SWAP))
					spec->mono_nid = conn_list[0];
		}
		/* all mono outs have a least a mute/unmute switch */
		err = stac92xx_add_control(spec, STAC_CTL_WIDGET_MUTE,
			"Mono Playback Switch",
			HDA_COMPOSE_AMP_VAL(spec->autocfg.mono_out_pin,
					1, 0, dir));
		if (err < 0)
			return err;
		/* check to see if there is volume support for the amp */
		if ((caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT) {
			err = stac92xx_add_control(spec, STAC_CTL_WIDGET_VOL,
				"Mono Playback Volume",
				HDA_COMPOSE_AMP_VAL(spec->autocfg.mono_out_pin,
					1, 0, dir));
			if (err < 0)
				return err;
		}

		stac92xx_auto_set_pinctl(codec, spec->autocfg.mono_out_pin,
					 AC_PINCTL_OUT_EN);
	}

	if ((err = stac92xx_add_dyn_out_pins(codec, &spec->autocfg)) < 0)
		return err;
	if (spec->multiout.num_dacs == 0)
		if ((err = stac92xx_auto_fill_dac_nids(codec, &spec->autocfg)) < 0)
			return err;

	err = stac92xx_auto_create_multi_out_ctls(codec, &spec->autocfg);

	if (err < 0)
		return err;

	if (hp_speaker_swap == 1) {
		/* Restore the hp_outs and line_outs */
		memcpy(spec->autocfg.hp_pins, spec->autocfg.line_out_pins,
		       sizeof(spec->autocfg.line_out_pins));
		spec->autocfg.hp_outs = spec->autocfg.line_outs;
		memcpy(spec->autocfg.line_out_pins, spec->autocfg.speaker_pins,
		       sizeof(spec->autocfg.speaker_pins));
		spec->autocfg.line_outs = spec->autocfg.speaker_outs;
		memset(spec->autocfg.speaker_pins, 0,
		       sizeof(spec->autocfg.speaker_pins));
		spec->autocfg.speaker_outs = 0;
	}

	err = stac92xx_auto_create_hp_ctls(codec, &spec->autocfg);

	if (err < 0)
		return err;

	err = stac92xx_auto_create_analog_input_ctls(codec, &spec->autocfg);

	if (err < 0)
		return err;

	if (spec->mono_nid > 0) {
		err = stac92xx_auto_create_mono_output_ctls(codec);
		if (err < 0)
			return err;
	}

	if (spec->num_dmics > 0)
		if ((err = stac92xx_auto_create_dmic_input_ctls(codec,
						&spec->autocfg)) < 0)
			return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;
	if (spec->multiout.max_channels > 2)
		spec->surr_switch = 1;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = dig_out;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = dig_in;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->input_mux = &spec->private_imux;
	if (!spec->dinput_mux)
		spec->dinput_mux = &spec->private_dimux;
	spec->mono_mux = &spec->private_mono_mux;

	return 1;
}

/* add playback controls for HP output */
static int stac9200_auto_create_hp_ctls(struct hda_codec *codec,
					struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t pin = cfg->hp_pins[0];
	unsigned int wid_caps;

	if (! pin)
		return 0;

	wid_caps = get_wcaps(codec, pin);
	if (wid_caps & AC_WCAP_UNSOL_CAP)
		spec->hp_detect = 1;

	return 0;
}

/* add playback controls for LFE output */
static int stac9200_auto_create_lfe_ctls(struct hda_codec *codec,
					struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	int err;
	hda_nid_t lfe_pin = 0x0;
	int i;

	/*
	 * search speaker outs and line outs for a mono speaker pin
	 * with an amp.  If one is found, add LFE controls
	 * for it.
	 */
	for (i = 0; i < spec->autocfg.speaker_outs && lfe_pin == 0x0; i++) {
		hda_nid_t pin = spec->autocfg.speaker_pins[i];
		unsigned int wcaps = get_wcaps(codec, pin);
		wcaps &= (AC_WCAP_STEREO | AC_WCAP_OUT_AMP);
		if (wcaps == AC_WCAP_OUT_AMP)
			/* found a mono speaker with an amp, must be lfe */
			lfe_pin = pin;
	}

	/* if speaker_outs is 0, then speakers may be in line_outs */
	if (lfe_pin == 0 && spec->autocfg.speaker_outs == 0) {
		for (i = 0; i < spec->autocfg.line_outs && lfe_pin == 0x0; i++) {
			hda_nid_t pin = spec->autocfg.line_out_pins[i];
			unsigned int defcfg;
			defcfg = snd_hda_codec_read(codec, pin, 0,
						 AC_VERB_GET_CONFIG_DEFAULT,
						 0x00);
			if (get_defcfg_device(defcfg) == AC_JACK_SPEAKER) {
				unsigned int wcaps = get_wcaps(codec, pin);
				wcaps &= (AC_WCAP_STEREO | AC_WCAP_OUT_AMP);
				if (wcaps == AC_WCAP_OUT_AMP)
					/* found a mono speaker with an amp,
					   must be lfe */
					lfe_pin = pin;
			}
		}
	}

	if (lfe_pin) {
		err = create_controls(spec, "LFE", lfe_pin, 1);
		if (err < 0)
			return err;
	}

	return 0;
}

static int stac9200_parse_auto_config(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int err;

	if ((err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL)) < 0)
		return err;

	if ((err = stac92xx_auto_create_analog_input_ctls(codec, &spec->autocfg)) < 0)
		return err;

	if ((err = stac9200_auto_create_hp_ctls(codec, &spec->autocfg)) < 0)
		return err;

	if ((err = stac9200_auto_create_lfe_ctls(codec, &spec->autocfg)) < 0)
		return err;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = 0x05;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = 0x04;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->input_mux = &spec->private_imux;
	spec->dinput_mux = &spec->private_dimux;

	return 1;
}

/*
 * Early 2006 Intel Macintoshes with STAC9220X5 codecs seem to have a
 * funky external mute control using GPIO pins.
 */

static void stac_gpio_set(struct hda_codec *codec, unsigned int mask,
			  unsigned int dir_mask, unsigned int data)
{
	unsigned int gpiostate, gpiomask, gpiodir;

	gpiostate = snd_hda_codec_read(codec, codec->afg, 0,
				       AC_VERB_GET_GPIO_DATA, 0);
	gpiostate = (gpiostate & ~dir_mask) | (data & dir_mask);

	gpiomask = snd_hda_codec_read(codec, codec->afg, 0,
				      AC_VERB_GET_GPIO_MASK, 0);
	gpiomask |= mask;

	gpiodir = snd_hda_codec_read(codec, codec->afg, 0,
				     AC_VERB_GET_GPIO_DIRECTION, 0);
	gpiodir |= dir_mask;

	/* Configure GPIOx as CMOS */
	snd_hda_codec_write(codec, codec->afg, 0, 0x7e7, 0);

	snd_hda_codec_write(codec, codec->afg, 0,
			    AC_VERB_SET_GPIO_MASK, gpiomask);
	snd_hda_codec_read(codec, codec->afg, 0,
			   AC_VERB_SET_GPIO_DIRECTION, gpiodir); /* sync */

	msleep(1);

	snd_hda_codec_read(codec, codec->afg, 0,
			   AC_VERB_SET_GPIO_DATA, gpiostate); /* sync */
}

static void enable_pin_detect(struct hda_codec *codec, hda_nid_t nid,
			      unsigned int event)
{
	if (get_wcaps(codec, nid) & AC_WCAP_UNSOL_CAP)
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_UNSOLICITED_ENABLE,
					  (AC_USRSP_EN | event));
}

static int is_nid_hp_pin(struct auto_pin_cfg *cfg, hda_nid_t nid)
{
	int i;
	for (i = 0; i < cfg->hp_outs; i++)
		if (cfg->hp_pins[i] == nid)
			return 1; /* nid is a HP-Out */

	return 0; /* nid is not a HP-Out */
};

static void stac92xx_power_down(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	/* power down inactive DACs */
	hda_nid_t *dac;
	for (dac = spec->dac_list; *dac; dac++)
		if (!is_in_dac_nids(spec, *dac) &&
			spec->multiout.hp_nid != *dac)
			snd_hda_codec_write_cache(codec, *dac, 0,
					AC_VERB_SET_POWER_STATE, AC_PWRST_D3);
}

static int stac92xx_init(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	snd_hda_sequence_write(codec, spec->init);

	/* set up pins */
	if (spec->hp_detect) {
		/* Enable unsolicited responses on the HP widget */
		for (i = 0; i < cfg->hp_outs; i++)
			enable_pin_detect(codec, cfg->hp_pins[i],
					  STAC_HP_EVENT);
		/* force to enable the first line-out; the others are set up
		 * in unsol_event
		 */
		stac92xx_auto_set_pinctl(codec, spec->autocfg.line_out_pins[0],
					 AC_PINCTL_OUT_EN);
		stac92xx_auto_init_hp_out(codec);
		/* fake event to set up pins */
		codec->patch_ops.unsol_event(codec, STAC_HP_EVENT << 26);
	} else {
		stac92xx_auto_init_multi_out(codec);
		stac92xx_auto_init_hp_out(codec);
	}
	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = cfg->input_pins[i];
		if (nid) {
			unsigned int pinctl = AC_PINCTL_IN_EN;
			if (i == AUTO_PIN_MIC || i == AUTO_PIN_FRONT_MIC)
				pinctl |= stac92xx_get_vref(codec, nid);
			stac92xx_auto_set_pinctl(codec, nid, pinctl);
		}
	}
	for (i = 0; i < spec->num_dmics; i++)
		stac92xx_auto_set_pinctl(codec, spec->dmic_nids[i],
					AC_PINCTL_IN_EN);
	for (i = 0; i < spec->num_pwrs; i++)  {
		int event = is_nid_hp_pin(cfg, spec->pwr_nids[i])
					? STAC_HP_EVENT : STAC_PWR_EVENT;
		int pinctl = snd_hda_codec_read(codec, spec->pwr_nids[i],
					0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		int def_conf = snd_hda_codec_read(codec, spec->pwr_nids[i],
					0, AC_VERB_GET_CONFIG_DEFAULT, 0);
		def_conf = get_defcfg_connect(def_conf);
		/* outputs are only ports capable of power management
		 * any attempts on powering down a input port cause the
		 * referenced VREF to act quirky.
		 */
		if (pinctl & AC_PINCTL_IN_EN)
			continue;
		/* skip any ports that don't have jacks since presence
 		 * detection is useless */
		if (def_conf && def_conf != AC_JACK_PORT_FIXED)
			continue;
		enable_pin_detect(codec, spec->pwr_nids[i], event | i);
		codec->patch_ops.unsol_event(codec, (event | i) << 26);
	}
	if (spec->dac_list)
		stac92xx_power_down(codec);
	if (cfg->dig_out_pin)
		stac92xx_auto_set_pinctl(codec, cfg->dig_out_pin,
					 AC_PINCTL_OUT_EN);
	if (cfg->dig_in_pin)
		stac92xx_auto_set_pinctl(codec, cfg->dig_in_pin,
					 AC_PINCTL_IN_EN);

	stac_gpio_set(codec, spec->gpio_mask,
					spec->gpio_dir, spec->gpio_data);

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

	if (spec->bios_pin_configs)
		kfree(spec->bios_pin_configs);

	kfree(spec);
}

static void stac92xx_set_pinctl(struct hda_codec *codec, hda_nid_t nid,
				unsigned int flag)
{
	unsigned int pin_ctl = snd_hda_codec_read(codec, nid,
			0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00);

	if (pin_ctl & AC_PINCTL_IN_EN) {
		/*
		 * we need to check the current set-up direction of
		 * shared input pins since they can be switched via
		 * "xxx as Output" mixer switch
		 */
		struct sigmatel_spec *spec = codec->spec;
		struct auto_pin_cfg *cfg = &spec->autocfg;
		if ((nid == cfg->input_pins[AUTO_PIN_LINE] &&
		     spec->line_switch) ||
		    (nid == cfg->input_pins[AUTO_PIN_MIC] &&
		     spec->mic_switch))
			return;
	}

	/* if setting pin direction bits, clear the current
	   direction bits first */
	if (flag & (AC_PINCTL_IN_EN | AC_PINCTL_OUT_EN))
		pin_ctl &= ~(AC_PINCTL_IN_EN | AC_PINCTL_OUT_EN);
	
	snd_hda_codec_write_cache(codec, nid, 0,
			AC_VERB_SET_PIN_WIDGET_CONTROL,
			pin_ctl | flag);
}

static void stac92xx_reset_pinctl(struct hda_codec *codec, hda_nid_t nid,
				  unsigned int flag)
{
	unsigned int pin_ctl = snd_hda_codec_read(codec, nid,
			0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00);
	snd_hda_codec_write_cache(codec, nid, 0,
			AC_VERB_SET_PIN_WIDGET_CONTROL,
			pin_ctl & ~flag);
}

static int get_hp_pin_presence(struct hda_codec *codec, hda_nid_t nid)
{
	if (!nid)
		return 0;
	if (snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_PIN_SENSE, 0x00)
	    & (1 << 31)) {
		unsigned int pinctl;
		pinctl = snd_hda_codec_read(codec, nid, 0,
					    AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		if (pinctl & AC_PINCTL_IN_EN)
			return 0; /* mic- or line-input */
		else
			return 1; /* HP-output */
	}
	return 0;
}

static void stac92xx_hp_detect(struct hda_codec *codec, unsigned int res)
{
	struct sigmatel_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int nid = cfg->hp_pins[cfg->hp_outs - 1];
	int i, presence;

	presence = 0;
	if (spec->gpio_mute)
		presence = !(snd_hda_codec_read(codec, codec->afg, 0,
			AC_VERB_GET_GPIO_DATA, 0) & spec->gpio_mute);

	for (i = 0; i < cfg->hp_outs; i++) {
		if (presence)
			break;
		if (spec->hp_switch && cfg->hp_pins[i] == nid)
			break;
		presence = get_hp_pin_presence(codec, cfg->hp_pins[i]);
	}

	if (presence) {
		/* disable lineouts, enable hp */
		if (spec->hp_switch)
			stac92xx_reset_pinctl(codec, nid, AC_PINCTL_OUT_EN);
		for (i = 0; i < cfg->line_outs; i++)
			stac92xx_reset_pinctl(codec, cfg->line_out_pins[i],
						AC_PINCTL_OUT_EN);
		for (i = 0; i < cfg->speaker_outs; i++)
			stac92xx_reset_pinctl(codec, cfg->speaker_pins[i],
						AC_PINCTL_OUT_EN);
		if (spec->eapd_mask)
			stac_gpio_set(codec, spec->gpio_mask,
				spec->gpio_dir, spec->gpio_data &
				~spec->eapd_mask);
	} else {
		/* enable lineouts, disable hp */
		if (spec->hp_switch)
			stac92xx_set_pinctl(codec, nid, AC_PINCTL_OUT_EN);
		for (i = 0; i < cfg->line_outs; i++)
			stac92xx_set_pinctl(codec, cfg->line_out_pins[i],
						AC_PINCTL_OUT_EN);
		for (i = 0; i < cfg->speaker_outs; i++)
			stac92xx_set_pinctl(codec, cfg->speaker_pins[i],
						AC_PINCTL_OUT_EN);
		if (spec->eapd_mask)
			stac_gpio_set(codec, spec->gpio_mask,
				spec->gpio_dir, spec->gpio_data |
				spec->eapd_mask);
	}
	if (!spec->hp_switch && cfg->hp_outs > 1 && presence)
		stac92xx_set_pinctl(codec, nid, AC_PINCTL_OUT_EN);
} 

static void stac92xx_pin_sense(struct hda_codec *codec, int idx)
{
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t nid = spec->pwr_nids[idx];
	int presence, val;
	val = snd_hda_codec_read(codec, codec->afg, 0, 0x0fec, 0x0)
							& 0x000000ff;
	presence = get_hp_pin_presence(codec, nid);
	idx = 1 << idx;

	if (presence)
		val &= ~idx;
	else
		val |= idx;

	/* power down unused output ports */
	snd_hda_codec_write(codec, codec->afg, 0, 0x7ec, val);
};

static void stac92xx_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct sigmatel_spec *spec = codec->spec;
	int idx = res >> 26 & 0x0f;

	switch ((res >> 26) & 0x30) {
	case STAC_HP_EVENT:
		stac92xx_hp_detect(codec, res);
		/* fallthru */
	case STAC_PWR_EVENT:
		if (spec->num_pwrs > 0)
			stac92xx_pin_sense(codec, idx);
	}
}

#ifdef SND_HDA_NEEDS_RESUME
static int stac92xx_resume(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	stac92xx_set_config_regs(codec);
	snd_hda_sequence_write(codec, spec->init);
	stac_gpio_set(codec, spec->gpio_mask,
		spec->gpio_dir, spec->gpio_data);
	snd_hda_codec_resume_amp(codec);
	snd_hda_codec_resume_cache(codec);
	/* power down inactive DACs */
	if (spec->dac_list)
		stac92xx_power_down(codec);
	/* invoke unsolicited event to reset the HP state */
	if (spec->hp_detect)
		codec->patch_ops.unsol_event(codec, STAC_HP_EVENT << 26);
	return 0;
}
#endif

static struct hda_codec_ops stac92xx_patch_ops = {
	.build_controls = stac92xx_build_controls,
	.build_pcms = stac92xx_build_pcms,
	.init = stac92xx_init,
	.free = stac92xx_free,
	.unsol_event = stac92xx_unsol_event,
#ifdef SND_HDA_NEEDS_RESUME
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
	spec->num_pins = ARRAY_SIZE(stac9200_pin_nids);
	spec->pin_nids = stac9200_pin_nids;
	spec->board_config = snd_hda_check_board_config(codec, STAC_9200_MODELS,
							stac9200_models,
							stac9200_cfg_tbl);
	if (spec->board_config < 0) {
		snd_printdd(KERN_INFO "hda_codec: Unknown model for STAC9200, using BIOS defaults\n");
		err = stac92xx_save_bios_config_regs(codec);
		if (err < 0) {
			stac92xx_free(codec);
			return err;
		}
		spec->pin_configs = spec->bios_pin_configs;
	} else {
		spec->pin_configs = stac9200_brd_tbl[spec->board_config];
		stac92xx_set_config_regs(codec);
	}

	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = 1;
	spec->multiout.dac_nids = stac9200_dac_nids;
	spec->adc_nids = stac9200_adc_nids;
	spec->mux_nids = stac9200_mux_nids;
	spec->num_muxes = 1;
	spec->num_dmics = 0;
	spec->num_adcs = 1;
	spec->num_pwrs = 0;

	if (spec->board_config == STAC_9200_GATEWAY ||
	    spec->board_config == STAC_9200_OQO)
		spec->init = stac9200_eapd_init;
	else
		spec->init = stac9200_core_init;
	spec->mixer = stac9200_mixer;

	if (spec->board_config == STAC_9200_PANASONIC) {
		spec->gpio_mask = spec->gpio_dir = 0x09;
		spec->gpio_data = 0x00;
	}

	err = stac9200_parse_auto_config(codec);
	if (err < 0) {
		stac92xx_free(codec);
		return err;
	}

	codec->patch_ops = stac92xx_patch_ops;

	return 0;
}

static int patch_stac925x(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	spec->num_pins = ARRAY_SIZE(stac925x_pin_nids);
	spec->pin_nids = stac925x_pin_nids;
	spec->board_config = snd_hda_check_board_config(codec, STAC_925x_MODELS,
							stac925x_models,
							stac925x_cfg_tbl);
 again:
	if (spec->board_config < 0) {
		snd_printdd(KERN_INFO "hda_codec: Unknown model for STAC925x," 
				      "using BIOS defaults\n");
		err = stac92xx_save_bios_config_regs(codec);
		if (err < 0) {
			stac92xx_free(codec);
			return err;
		}
		spec->pin_configs = spec->bios_pin_configs;
	} else if (stac925x_brd_tbl[spec->board_config] != NULL){
		spec->pin_configs = stac925x_brd_tbl[spec->board_config];
		stac92xx_set_config_regs(codec);
	}

	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = 1;
	spec->multiout.dac_nids = stac925x_dac_nids;
	spec->adc_nids = stac925x_adc_nids;
	spec->mux_nids = stac925x_mux_nids;
	spec->num_muxes = 1;
	spec->num_adcs = 1;
	spec->num_pwrs = 0;
	switch (codec->vendor_id) {
	case 0x83847632: /* STAC9202  */
	case 0x83847633: /* STAC9202D */
	case 0x83847636: /* STAC9251  */
	case 0x83847637: /* STAC9251D */
		spec->num_dmics = STAC925X_NUM_DMICS;
		spec->dmic_nids = stac925x_dmic_nids;
		spec->num_dmuxes = ARRAY_SIZE(stac925x_dmux_nids);
		spec->dmux_nids = stac925x_dmux_nids;
		break;
	default:
		spec->num_dmics = 0;
		break;
	}

	spec->init = stac925x_core_init;
	spec->mixer = stac925x_mixer;

	err = stac92xx_parse_auto_config(codec, 0x8, 0x7);
	if (!err) {
		if (spec->board_config < 0) {
			printk(KERN_WARNING "hda_codec: No auto-config is "
			       "available, default to model=ref\n");
			spec->board_config = STAC_925x_REF;
			goto again;
		}
		err = -EINVAL;
	}
	if (err < 0) {
		stac92xx_free(codec);
		return err;
	}

	codec->patch_ops = stac92xx_patch_ops;

	return 0;
}

static struct hda_input_mux stac92hd73xx_dmux = {
	.num_items = 4,
	.items = {
		{ "Analog Inputs", 0x0b },
		{ "CD", 0x08 },
		{ "Digital Mic 1", 0x09 },
		{ "Digital Mic 2", 0x0a },
	}
};

static int patch_stac92hd73xx(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	hda_nid_t conn[STAC92HD73_DAC_COUNT + 2];
	int err = 0;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	spec->num_pins = ARRAY_SIZE(stac92hd73xx_pin_nids);
	spec->pin_nids = stac92hd73xx_pin_nids;
	spec->board_config = snd_hda_check_board_config(codec,
							STAC_92HD73XX_MODELS,
							stac92hd73xx_models,
							stac92hd73xx_cfg_tbl);
again:
	if (spec->board_config < 0) {
		snd_printdd(KERN_INFO "hda_codec: Unknown model for"
			" STAC92HD73XX, using BIOS defaults\n");
		err = stac92xx_save_bios_config_regs(codec);
		if (err < 0) {
			stac92xx_free(codec);
			return err;
		}
		spec->pin_configs = spec->bios_pin_configs;
	} else {
		spec->pin_configs = stac92hd73xx_brd_tbl[spec->board_config];
		stac92xx_set_config_regs(codec);
	}

	spec->multiout.num_dacs = snd_hda_get_connections(codec, 0x0a,
			conn, STAC92HD73_DAC_COUNT + 2) - 1;

	if (spec->multiout.num_dacs < 0) {
		printk(KERN_WARNING "hda_codec: Could not determine "
		       "number of channels defaulting to DAC count\n");
		spec->multiout.num_dacs = STAC92HD73_DAC_COUNT;
	}

	switch (spec->multiout.num_dacs) {
	case 0x3: /* 6 Channel */
		spec->multiout.hp_nid = 0x17;
		spec->mixer = stac92hd73xx_6ch_mixer;
		spec->init = stac92hd73xx_6ch_core_init;
		break;
	case 0x4: /* 8 Channel */
		spec->multiout.hp_nid = 0x18;
		spec->mixer = stac92hd73xx_8ch_mixer;
		spec->init = stac92hd73xx_8ch_core_init;
		break;
	case 0x5: /* 10 Channel */
		spec->multiout.hp_nid = 0x19;
		spec->mixer = stac92hd73xx_10ch_mixer;
		spec->init = stac92hd73xx_10ch_core_init;
	};

	spec->multiout.dac_nids = stac92hd73xx_dac_nids;
	spec->aloopback_mask = 0x01;
	spec->aloopback_shift = 8;

	spec->mux_nids = stac92hd73xx_mux_nids;
	spec->adc_nids = stac92hd73xx_adc_nids;
	spec->dmic_nids = stac92hd73xx_dmic_nids;
	spec->dmux_nids = stac92hd73xx_dmux_nids;

	spec->num_muxes = ARRAY_SIZE(stac92hd73xx_mux_nids);
	spec->num_adcs = ARRAY_SIZE(stac92hd73xx_adc_nids);
	spec->num_dmuxes = ARRAY_SIZE(stac92hd73xx_dmux_nids);
	spec->dinput_mux = &stac92hd73xx_dmux;
	/* GPIO0 High = Enable EAPD */
	spec->eapd_mask = spec->gpio_mask = spec->gpio_dir = 0x1;
	spec->gpio_data = 0x01;

	switch (spec->board_config) {
	case STAC_DELL_M6:
		spec->init = dell_eq_core_init;
		switch (codec->subsystem_id) {
		case 0x1028025e: /* Analog Mics */
		case 0x1028025f:
			stac92xx_set_config_reg(codec, 0x0b, 0x90A70170);
			spec->num_dmics = 0;
			break;
		case 0x10280271: /* Digital Mics */
		case 0x10280272:
			spec->init = dell_m6_core_init;
			/* fall-through */
		case 0x10280254:
		case 0x10280255:
			stac92xx_set_config_reg(codec, 0x13, 0x90A60160);
			spec->num_dmics = 1;
			break;
		case 0x10280256: /* Both */
		case 0x10280057:
			stac92xx_set_config_reg(codec, 0x0b, 0x90A70170);
			stac92xx_set_config_reg(codec, 0x13, 0x90A60160);
			spec->num_dmics = 1;
			break;
		}
		break;
	default:
		spec->num_dmics = STAC92HD73XX_NUM_DMICS;
	}

	spec->num_pwrs = ARRAY_SIZE(stac92hd73xx_pwr_nids);
	spec->pwr_nids = stac92hd73xx_pwr_nids;

	err = stac92xx_parse_auto_config(codec, 0x22, 0x24);

	if (!err) {
		if (spec->board_config < 0) {
			printk(KERN_WARNING "hda_codec: No auto-config is "
			       "available, default to model=ref\n");
			spec->board_config = STAC_92HD73XX_REF;
			goto again;
		}
		err = -EINVAL;
	}

	if (err < 0) {
		stac92xx_free(codec);
		return err;
	}

	codec->patch_ops = stac92xx_patch_ops;

	return 0;
}

static int patch_stac92hd71bxx(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err = 0;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	spec->num_pins = ARRAY_SIZE(stac92hd71bxx_pin_nids);
	spec->num_pwrs = ARRAY_SIZE(stac92hd71bxx_pwr_nids);
	spec->pin_nids = stac92hd71bxx_pin_nids;
	spec->board_config = snd_hda_check_board_config(codec,
							STAC_92HD71BXX_MODELS,
							stac92hd71bxx_models,
							stac92hd71bxx_cfg_tbl);
again:
	if (spec->board_config < 0) {
		snd_printdd(KERN_INFO "hda_codec: Unknown model for"
			" STAC92HD71BXX, using BIOS defaults\n");
		err = stac92xx_save_bios_config_regs(codec);
		if (err < 0) {
			stac92xx_free(codec);
			return err;
		}
		spec->pin_configs = spec->bios_pin_configs;
	} else {
		spec->pin_configs = stac92hd71bxx_brd_tbl[spec->board_config];
		stac92xx_set_config_regs(codec);
	}

	switch (codec->vendor_id) {
	case 0x111d76b6: /* 4 Port without Analog Mixer */
	case 0x111d76b7:
	case 0x111d76b4: /* 6 Port without Analog Mixer */
	case 0x111d76b5:
		spec->mixer = stac92hd71bxx_mixer;
		spec->init = stac92hd71bxx_core_init;
		break;
	case 0x111d7608: /* 5 Port with Analog Mixer */
		/* no output amps */
		spec->num_pwrs = 0;
		spec->mixer = stac92hd71bxx_analog_mixer;

		/* disable VSW */
		spec->init = &stac92hd71bxx_analog_core_init[HD_DISABLE_PORTF];
		stac92xx_set_config_reg(codec, 0xf, 0x40f000f0);
		break;
	case 0x111d7603: /* 6 Port with Analog Mixer */
		/* no output amps */
		spec->num_pwrs = 0;
		/* fallthru */
	default:
		spec->mixer = stac92hd71bxx_analog_mixer;
		spec->init = stac92hd71bxx_analog_core_init;
	}

	spec->aloopback_mask = 0x20;
	spec->aloopback_shift = 0;

	/* GPIO0 High = EAPD */
	spec->gpio_mask = 0x01;
	spec->gpio_dir = 0x01;
	spec->gpio_data = 0x01;

	spec->mux_nids = stac92hd71bxx_mux_nids;
	spec->adc_nids = stac92hd71bxx_adc_nids;
	spec->dmic_nids = stac92hd71bxx_dmic_nids;
	spec->dmux_nids = stac92hd71bxx_dmux_nids;
	spec->pwr_nids = stac92hd71bxx_pwr_nids;

	spec->num_muxes = ARRAY_SIZE(stac92hd71bxx_mux_nids);
	spec->num_adcs = ARRAY_SIZE(stac92hd71bxx_adc_nids);
	spec->num_dmics = STAC92HD71BXX_NUM_DMICS;
	spec->num_dmuxes = ARRAY_SIZE(stac92hd71bxx_dmux_nids);

	spec->multiout.num_dacs = 1;
	spec->multiout.hp_nid = 0x11;
	spec->multiout.dac_nids = stac92hd71bxx_dac_nids;

	err = stac92xx_parse_auto_config(codec, 0x21, 0x23);
	if (!err) {
		if (spec->board_config < 0) {
			printk(KERN_WARNING "hda_codec: No auto-config is "
			       "available, default to model=ref\n");
			spec->board_config = STAC_92HD71BXX_REF;
			goto again;
		}
		err = -EINVAL;
	}

	if (err < 0) {
		stac92xx_free(codec);
		return err;
	}

	codec->patch_ops = stac92xx_patch_ops;

	return 0;
};

static int patch_stac922x(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	spec->num_pins = ARRAY_SIZE(stac922x_pin_nids);
	spec->pin_nids = stac922x_pin_nids;
	spec->board_config = snd_hda_check_board_config(codec, STAC_922X_MODELS,
							stac922x_models,
							stac922x_cfg_tbl);
	if (spec->board_config == STAC_INTEL_MAC_AUTO) {
		spec->gpio_mask = spec->gpio_dir = 0x03;
		spec->gpio_data = 0x03;
		/* Intel Macs have all same PCI SSID, so we need to check
		 * codec SSID to distinguish the exact models
		 */
		printk(KERN_INFO "hda_codec: STAC922x, Apple subsys_id=%x\n", codec->subsystem_id);
		switch (codec->subsystem_id) {

		case 0x106b0800:
			spec->board_config = STAC_INTEL_MAC_V1;
			break;
		case 0x106b0600:
		case 0x106b0700:
			spec->board_config = STAC_INTEL_MAC_V2;
			break;
		case 0x106b0e00:
		case 0x106b0f00:
		case 0x106b1600:
		case 0x106b1700:
		case 0x106b0200:
		case 0x106b1e00:
			spec->board_config = STAC_INTEL_MAC_V3;
			break;
		case 0x106b1a00:
		case 0x00000100:
			spec->board_config = STAC_INTEL_MAC_V4;
			break;
		case 0x106b0a00:
		case 0x106b2200:
			spec->board_config = STAC_INTEL_MAC_V5;
			break;
		default:
			spec->board_config = STAC_INTEL_MAC_V3;
			break;
		}
	}

 again:
	if (spec->board_config < 0) {
		snd_printdd(KERN_INFO "hda_codec: Unknown model for STAC922x, "
			"using BIOS defaults\n");
		err = stac92xx_save_bios_config_regs(codec);
		if (err < 0) {
			stac92xx_free(codec);
			return err;
		}
		spec->pin_configs = spec->bios_pin_configs;
	} else if (stac922x_brd_tbl[spec->board_config] != NULL) {
		spec->pin_configs = stac922x_brd_tbl[spec->board_config];
		stac92xx_set_config_regs(codec);
	}

	spec->adc_nids = stac922x_adc_nids;
	spec->mux_nids = stac922x_mux_nids;
	spec->num_muxes = ARRAY_SIZE(stac922x_mux_nids);
	spec->num_adcs = ARRAY_SIZE(stac922x_adc_nids);
	spec->num_dmics = 0;
	spec->num_pwrs = 0;

	spec->init = stac922x_core_init;
	spec->mixer = stac922x_mixer;

	spec->multiout.dac_nids = spec->dac_nids;
	
	err = stac92xx_parse_auto_config(codec, 0x08, 0x09);
	if (!err) {
		if (spec->board_config < 0) {
			printk(KERN_WARNING "hda_codec: No auto-config is "
			       "available, default to model=ref\n");
			spec->board_config = STAC_D945_REF;
			goto again;
		}
		err = -EINVAL;
	}
	if (err < 0) {
		stac92xx_free(codec);
		return err;
	}

	codec->patch_ops = stac92xx_patch_ops;

	/* Fix Mux capture level; max to 2 */
	snd_hda_override_amp_caps(codec, 0x12, HDA_OUTPUT,
				  (0 << AC_AMPCAP_OFFSET_SHIFT) |
				  (2 << AC_AMPCAP_NUM_STEPS_SHIFT) |
				  (0x27 << AC_AMPCAP_STEP_SIZE_SHIFT) |
				  (0 << AC_AMPCAP_MUTE_SHIFT));

	return 0;
}

static int patch_stac927x(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	spec->num_pins = ARRAY_SIZE(stac927x_pin_nids);
	spec->pin_nids = stac927x_pin_nids;
	spec->board_config = snd_hda_check_board_config(codec, STAC_927X_MODELS,
							stac927x_models,
							stac927x_cfg_tbl);
 again:
	if (spec->board_config < 0 || !stac927x_brd_tbl[spec->board_config]) {
		if (spec->board_config < 0)
			snd_printdd(KERN_INFO "hda_codec: Unknown model for"
				    "STAC927x, using BIOS defaults\n");
		err = stac92xx_save_bios_config_regs(codec);
		if (err < 0) {
			stac92xx_free(codec);
			return err;
		}
		spec->pin_configs = spec->bios_pin_configs;
	} else {
		spec->pin_configs = stac927x_brd_tbl[spec->board_config];
		stac92xx_set_config_regs(codec);
	}

	spec->adc_nids = stac927x_adc_nids;
	spec->num_adcs = ARRAY_SIZE(stac927x_adc_nids);
	spec->mux_nids = stac927x_mux_nids;
	spec->num_muxes = ARRAY_SIZE(stac927x_mux_nids);
	spec->dac_list = stac927x_dac_nids;
	spec->multiout.dac_nids = spec->dac_nids;

	switch (spec->board_config) {
	case STAC_D965_3ST:
	case STAC_D965_5ST:
		/* GPIO0 High = Enable EAPD */
		spec->eapd_mask = spec->gpio_mask = spec->gpio_dir = 0x01;
		spec->gpio_data = 0x01;
		spec->num_dmics = 0;

		spec->init = d965_core_init;
		spec->mixer = stac927x_mixer;
		break;
	case STAC_DELL_BIOS:
		switch (codec->subsystem_id) {
		case 0x10280209:
		case 0x1028022e:
			/* correct the device field to SPDIF out */
			stac92xx_set_config_reg(codec, 0x21, 0x01442070);
			break;
		};
		/* configure the analog microphone on some laptops */
		stac92xx_set_config_reg(codec, 0x0c, 0x90a79130);
		/* correct the front output jack as a hp out */
		stac92xx_set_config_reg(codec, 0x0f, 0x0227011f);
		/* correct the front input jack as a mic */
		stac92xx_set_config_reg(codec, 0x0e, 0x02a79130);
		/* fallthru */
	case STAC_DELL_3ST:
		/* GPIO2 High = Enable EAPD */
		spec->eapd_mask = spec->gpio_mask = spec->gpio_dir = 0x04;
		spec->gpio_data = 0x04;
		spec->dmic_nids = stac927x_dmic_nids;
		spec->num_dmics = STAC927X_NUM_DMICS;

		spec->init = d965_core_init;
		spec->mixer = stac927x_mixer;
		spec->dmux_nids = stac927x_dmux_nids;
		spec->num_dmuxes = ARRAY_SIZE(stac927x_dmux_nids);
		break;
	default:
		/* GPIO0 High = Enable EAPD */
		spec->eapd_mask = spec->gpio_mask = spec->gpio_dir = 0x1;
		spec->gpio_data = 0x01;
		spec->num_dmics = 0;

		spec->init = stac927x_core_init;
		spec->mixer = stac927x_mixer;
	}

	spec->num_pwrs = 0;
	spec->aloopback_mask = 0x40;
	spec->aloopback_shift = 0;

	err = stac92xx_parse_auto_config(codec, 0x1e, 0x20);
	if (!err) {
		if (spec->board_config < 0) {
			printk(KERN_WARNING "hda_codec: No auto-config is "
			       "available, default to model=ref\n");
			spec->board_config = STAC_D965_REF;
			goto again;
		}
		err = -EINVAL;
	}
	if (err < 0) {
		stac92xx_free(codec);
		return err;
	}

	codec->patch_ops = stac92xx_patch_ops;

	/*
	 * !!FIXME!!
	 * The STAC927x seem to require fairly long delays for certain
	 * command sequences.  With too short delays (even if the answer
	 * is set to RIRB properly), it results in the silence output
	 * on some hardwares like Dell.
	 *
	 * The below flag enables the longer delay (see get_response
	 * in hda_intel.c).
	 */
	codec->bus->needs_damn_long_delay = 1;

	return 0;
}

static int patch_stac9205(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	spec->num_pins = ARRAY_SIZE(stac9205_pin_nids);
	spec->pin_nids = stac9205_pin_nids;
	spec->board_config = snd_hda_check_board_config(codec, STAC_9205_MODELS,
							stac9205_models,
							stac9205_cfg_tbl);
 again:
	if (spec->board_config < 0) {
		snd_printdd(KERN_INFO "hda_codec: Unknown model for STAC9205, using BIOS defaults\n");
		err = stac92xx_save_bios_config_regs(codec);
		if (err < 0) {
			stac92xx_free(codec);
			return err;
		}
		spec->pin_configs = spec->bios_pin_configs;
	} else {
		spec->pin_configs = stac9205_brd_tbl[spec->board_config];
		stac92xx_set_config_regs(codec);
	}

	spec->adc_nids = stac9205_adc_nids;
	spec->num_adcs = ARRAY_SIZE(stac9205_adc_nids);
	spec->mux_nids = stac9205_mux_nids;
	spec->num_muxes = ARRAY_SIZE(stac9205_mux_nids);
	spec->dmic_nids = stac9205_dmic_nids;
	spec->num_dmics = STAC9205_NUM_DMICS;
	spec->dmux_nids = stac9205_dmux_nids;
	spec->num_dmuxes = ARRAY_SIZE(stac9205_dmux_nids);
	spec->num_pwrs = 0;

	spec->init = stac9205_core_init;
	spec->mixer = stac9205_mixer;

	spec->aloopback_mask = 0x40;
	spec->aloopback_shift = 0;
	spec->multiout.dac_nids = spec->dac_nids;
	
	switch (spec->board_config){
	case STAC_9205_DELL_M43:
		/* Enable SPDIF in/out */
		stac92xx_set_config_reg(codec, 0x1f, 0x01441030);
		stac92xx_set_config_reg(codec, 0x20, 0x1c410030);

		/* Enable unsol response for GPIO4/Dock HP connection */
		snd_hda_codec_write(codec, codec->afg, 0,
			AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK, 0x10);
		snd_hda_codec_write_cache(codec, codec->afg, 0,
					  AC_VERB_SET_UNSOLICITED_ENABLE,
					  (AC_USRSP_EN | STAC_HP_EVENT));

		spec->gpio_dir = 0x0b;
		spec->eapd_mask = 0x01;
		spec->gpio_mask = 0x1b;
		spec->gpio_mute = 0x10;
		/* GPIO0 High = EAPD, GPIO1 Low = Headphone Mute,
		 * GPIO3 Low = DRM
		 */
		spec->gpio_data = 0x01;
		break;
	default:
		/* GPIO0 High = EAPD */
		spec->eapd_mask = spec->gpio_mask = spec->gpio_dir = 0x1;
		spec->gpio_data = 0x01;
		break;
	}

	err = stac92xx_parse_auto_config(codec, 0x1f, 0x20);
	if (!err) {
		if (spec->board_config < 0) {
			printk(KERN_WARNING "hda_codec: No auto-config is "
			       "available, default to model=ref\n");
			spec->board_config = STAC_9205_REF;
			goto again;
		}
		err = -EINVAL;
	}
	if (err < 0) {
		stac92xx_free(codec);
		return err;
	}

	codec->patch_ops = stac92xx_patch_ops;

	return 0;
}

/*
 * STAC9872 hack
 */

/* static config for Sony VAIO FE550G and Sony VAIO AR */
static hda_nid_t vaio_dacs[] = { 0x2 };
#define VAIO_HP_DAC	0x5
static hda_nid_t vaio_adcs[] = { 0x8 /*,0x6*/ };
static hda_nid_t vaio_mux_nids[] = { 0x15 };

static struct hda_input_mux vaio_mux = {
	.num_items = 3,
	.items = {
		/* { "HP", 0x0 }, */
		{ "Mic Jack", 0x1 },
		{ "Internal Mic", 0x2 },
		{ "PCM", 0x3 },
	}
};

static struct hda_verb vaio_init[] = {
	{0x0a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP }, /* HP <- 0x2 */
	{0x0a, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | STAC_HP_EVENT},
	{0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT }, /* Speaker <- 0x5 */
	{0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 }, /* Mic? (<- 0x2) */
	{0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN }, /* CD */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 }, /* Mic? */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x1}, /* mic-sel: 0a,0d,14,02 */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE}, /* HP */
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE}, /* Speaker */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)}, /* capture sw/vol -> 0x8 */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)}, /* CD-in -> 0x6 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, /* Mic-in -> 0x9 */
	{}
};

static struct hda_verb vaio_ar_init[] = {
	{0x0a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP }, /* HP <- 0x2 */
	{0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT }, /* Speaker <- 0x5 */
	{0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 }, /* Mic? (<- 0x2) */
	{0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN }, /* CD */
/*	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },*/ /* Optical Out */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 }, /* Mic? */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x1}, /* mic-sel: 0a,0d,14,02 */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE}, /* HP */
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE}, /* Speaker */
/*	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},*/ /* Optical Out */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)}, /* capture sw/vol -> 0x8 */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)}, /* CD-in -> 0x6 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, /* Mic-in -> 0x9 */
	{}
};

/* bind volumes of both NID 0x02 and 0x05 */
static struct hda_bind_ctls vaio_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x02, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x05, 3, 0, HDA_OUTPUT),
		0
	},
};

/* bind volumes of both NID 0x02 and 0x05 */
static struct hda_bind_ctls vaio_bind_master_sw = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x02, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x05, 3, 0, HDA_OUTPUT),
		0,
	},
};

static struct snd_kcontrol_new vaio_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &vaio_bind_master_vol),
	HDA_BIND_SW("Master Playback Switch", &vaio_bind_master_sw),
	/* HDA_CODEC_VOLUME("CD Capture Volume", 0x07, 0, HDA_INPUT), */
	HDA_CODEC_VOLUME("Capture Volume", 0x09, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x09, 0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.count = 1,
		.info = stac92xx_mux_enum_info,
		.get = stac92xx_mux_enum_get,
		.put = stac92xx_mux_enum_put,
	},
	{}
};

static struct snd_kcontrol_new vaio_ar_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &vaio_bind_master_vol),
	HDA_BIND_SW("Master Playback Switch", &vaio_bind_master_sw),
	/* HDA_CODEC_VOLUME("CD Capture Volume", 0x07, 0, HDA_INPUT), */
	HDA_CODEC_VOLUME("Capture Volume", 0x09, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x09, 0, HDA_INPUT),
	/*HDA_CODEC_MUTE("Optical Out Switch", 0x10, 0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Optical Out Volume", 0x10, 0, HDA_OUTPUT),*/
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.count = 1,
		.info = stac92xx_mux_enum_info,
		.get = stac92xx_mux_enum_get,
		.put = stac92xx_mux_enum_put,
	},
	{}
};

static struct hda_codec_ops stac9872_patch_ops = {
	.build_controls = stac92xx_build_controls,
	.build_pcms = stac92xx_build_pcms,
	.init = stac92xx_init,
	.free = stac92xx_free,
#ifdef SND_HDA_NEEDS_RESUME
	.resume = stac92xx_resume,
#endif
};

static int stac9872_vaio_init(struct hda_codec *codec)
{
	int err;

	err = stac92xx_init(codec);
	if (err < 0)
		return err;
	if (codec->patch_ops.unsol_event)
		codec->patch_ops.unsol_event(codec, STAC_HP_EVENT << 26);
	return 0;
}

static void stac9872_vaio_hp_detect(struct hda_codec *codec, unsigned int res)
{
	if (get_hp_pin_presence(codec, 0x0a)) {
		stac92xx_reset_pinctl(codec, 0x0f, AC_PINCTL_OUT_EN);
		stac92xx_set_pinctl(codec, 0x0a, AC_PINCTL_OUT_EN);
	} else {
		stac92xx_reset_pinctl(codec, 0x0a, AC_PINCTL_OUT_EN);
		stac92xx_set_pinctl(codec, 0x0f, AC_PINCTL_OUT_EN);
	}
} 

static void stac9872_vaio_unsol_event(struct hda_codec *codec, unsigned int res)
{
	switch (res >> 26) {
	case STAC_HP_EVENT:
		stac9872_vaio_hp_detect(codec, res);
		break;
	}
}

static struct hda_codec_ops stac9872_vaio_patch_ops = {
	.build_controls = stac92xx_build_controls,
	.build_pcms = stac92xx_build_pcms,
	.init = stac9872_vaio_init,
	.free = stac92xx_free,
	.unsol_event = stac9872_vaio_unsol_event,
#ifdef CONFIG_PM
	.resume = stac92xx_resume,
#endif
};

enum { /* FE and SZ series. id=0x83847661 and subsys=0x104D0700 or 104D1000. */
       CXD9872RD_VAIO,
       /* Unknown. id=0x83847662 and subsys=0x104D1200 or 104D1000. */
       STAC9872AK_VAIO, 
       /* Unknown. id=0x83847661 and subsys=0x104D1200. */
       STAC9872K_VAIO,
       /* AR Series. id=0x83847664 and subsys=104D1300 */
       CXD9872AKD_VAIO,
       STAC_9872_MODELS,
};

static const char *stac9872_models[STAC_9872_MODELS] = {
	[CXD9872RD_VAIO]	= "vaio",
	[CXD9872AKD_VAIO]	= "vaio-ar",
};

static struct snd_pci_quirk stac9872_cfg_tbl[] = {
	SND_PCI_QUIRK(0x104d, 0x81e6, "Sony VAIO F/S", CXD9872RD_VAIO),
	SND_PCI_QUIRK(0x104d, 0x81ef, "Sony VAIO F/S", CXD9872RD_VAIO),
	SND_PCI_QUIRK(0x104d, 0x81fd, "Sony VAIO AR", CXD9872AKD_VAIO),
	SND_PCI_QUIRK(0x104d, 0x8205, "Sony VAIO AR", CXD9872AKD_VAIO),
	{}
};

static int patch_stac9872(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int board_config;

	board_config = snd_hda_check_board_config(codec, STAC_9872_MODELS,
						  stac9872_models,
						  stac9872_cfg_tbl);
	if (board_config < 0)
		/* unknown config, let generic-parser do its job... */
		return snd_hda_parse_generic_codec(codec);
	
	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
	switch (board_config) {
	case CXD9872RD_VAIO:
	case STAC9872AK_VAIO:
	case STAC9872K_VAIO:
		spec->mixer = vaio_mixer;
		spec->init = vaio_init;
		spec->multiout.max_channels = 2;
		spec->multiout.num_dacs = ARRAY_SIZE(vaio_dacs);
		spec->multiout.dac_nids = vaio_dacs;
		spec->multiout.hp_nid = VAIO_HP_DAC;
		spec->num_adcs = ARRAY_SIZE(vaio_adcs);
		spec->adc_nids = vaio_adcs;
		spec->num_pwrs = 0;
		spec->input_mux = &vaio_mux;
		spec->mux_nids = vaio_mux_nids;
		codec->patch_ops = stac9872_vaio_patch_ops;
		break;
	
	case CXD9872AKD_VAIO:
		spec->mixer = vaio_ar_mixer;
		spec->init = vaio_ar_init;
		spec->multiout.max_channels = 2;
		spec->multiout.num_dacs = ARRAY_SIZE(vaio_dacs);
		spec->multiout.dac_nids = vaio_dacs;
		spec->multiout.hp_nid = VAIO_HP_DAC;
		spec->num_adcs = ARRAY_SIZE(vaio_adcs);
		spec->num_pwrs = 0;
		spec->adc_nids = vaio_adcs;
		spec->input_mux = &vaio_mux;
		spec->mux_nids = vaio_mux_nids;
		codec->patch_ops = stac9872_patch_ops;
		break;
	}

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
 	{ .id = 0x83847618, .name = "STAC9227", .patch = patch_stac927x },
 	{ .id = 0x83847619, .name = "STAC9227", .patch = patch_stac927x },
 	{ .id = 0x83847616, .name = "STAC9228", .patch = patch_stac927x },
 	{ .id = 0x83847617, .name = "STAC9228", .patch = patch_stac927x },
 	{ .id = 0x83847614, .name = "STAC9229", .patch = patch_stac927x },
 	{ .id = 0x83847615, .name = "STAC9229", .patch = patch_stac927x },
 	{ .id = 0x83847620, .name = "STAC9274", .patch = patch_stac927x },
 	{ .id = 0x83847621, .name = "STAC9274D", .patch = patch_stac927x },
 	{ .id = 0x83847622, .name = "STAC9273X", .patch = patch_stac927x },
 	{ .id = 0x83847623, .name = "STAC9273D", .patch = patch_stac927x },
 	{ .id = 0x83847624, .name = "STAC9272X", .patch = patch_stac927x },
 	{ .id = 0x83847625, .name = "STAC9272D", .patch = patch_stac927x },
 	{ .id = 0x83847626, .name = "STAC9271X", .patch = patch_stac927x },
 	{ .id = 0x83847627, .name = "STAC9271D", .patch = patch_stac927x },
 	{ .id = 0x83847628, .name = "STAC9274X5NH", .patch = patch_stac927x },
 	{ .id = 0x83847629, .name = "STAC9274D5NH", .patch = patch_stac927x },
	{ .id = 0x83847632, .name = "STAC9202",  .patch = patch_stac925x },
	{ .id = 0x83847633, .name = "STAC9202D", .patch = patch_stac925x },
	{ .id = 0x83847634, .name = "STAC9250", .patch = patch_stac925x },
	{ .id = 0x83847635, .name = "STAC9250D", .patch = patch_stac925x },
	{ .id = 0x83847636, .name = "STAC9251", .patch = patch_stac925x },
	{ .id = 0x83847637, .name = "STAC9250D", .patch = patch_stac925x },
	{ .id = 0x83847645, .name = "92HD206X", .patch = patch_stac927x },
	{ .id = 0x83847646, .name = "92HD206D", .patch = patch_stac927x },
 	/* The following does not take into account .id=0x83847661 when subsys =
 	 * 104D0C00 which is STAC9225s. Because of this, some SZ Notebooks are
 	 * currently not fully supported.
 	 */
 	{ .id = 0x83847661, .name = "CXD9872RD/K", .patch = patch_stac9872 },
 	{ .id = 0x83847662, .name = "STAC9872AK", .patch = patch_stac9872 },
 	{ .id = 0x83847664, .name = "CXD9872AKD", .patch = patch_stac9872 },
 	{ .id = 0x838476a0, .name = "STAC9205", .patch = patch_stac9205 },
 	{ .id = 0x838476a1, .name = "STAC9205D", .patch = patch_stac9205 },
 	{ .id = 0x838476a2, .name = "STAC9204", .patch = patch_stac9205 },
 	{ .id = 0x838476a3, .name = "STAC9204D", .patch = patch_stac9205 },
 	{ .id = 0x838476a4, .name = "STAC9255", .patch = patch_stac9205 },
 	{ .id = 0x838476a5, .name = "STAC9255D", .patch = patch_stac9205 },
 	{ .id = 0x838476a6, .name = "STAC9254", .patch = patch_stac9205 },
 	{ .id = 0x838476a7, .name = "STAC9254D", .patch = patch_stac9205 },
	{ .id = 0x111d7603, .name = "92HD75B3X5", .patch = patch_stac92hd71bxx},
	{ .id = 0x111d7608, .name = "92HD75B2X5", .patch = patch_stac92hd71bxx},
	{ .id = 0x111d7674, .name = "92HD73D1X5", .patch = patch_stac92hd73xx },
	{ .id = 0x111d7675, .name = "92HD73C1X5", .patch = patch_stac92hd73xx },
	{ .id = 0x111d7676, .name = "92HD73E1X5", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76b0, .name = "92HD71B8X", .patch = patch_stac92hd71bxx },
	{ .id = 0x111d76b1, .name = "92HD71B8X", .patch = patch_stac92hd71bxx },
	{ .id = 0x111d76b2, .name = "92HD71B7X", .patch = patch_stac92hd71bxx },
	{ .id = 0x111d76b3, .name = "92HD71B7X", .patch = patch_stac92hd71bxx },
	{ .id = 0x111d76b4, .name = "92HD71B6X", .patch = patch_stac92hd71bxx },
	{ .id = 0x111d76b5, .name = "92HD71B6X", .patch = patch_stac92hd71bxx },
	{ .id = 0x111d76b6, .name = "92HD71B5X", .patch = patch_stac92hd71bxx },
	{ .id = 0x111d76b7, .name = "92HD71B5X", .patch = patch_stac92hd71bxx },
	{} /* terminator */
};
