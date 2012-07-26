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
#include <linux/dmi.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/asoundef.h>
#include <sound/jack.h>
#include <sound/tlv.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_beep.h"
#include "hda_jack.h"

enum {
	STAC_VREF_EVENT	= 1,
	STAC_INSERT_EVENT,
	STAC_PWR_EVENT,
	STAC_HP_EVENT,
	STAC_LO_EVENT,
	STAC_MIC_EVENT,
};

enum {
	STAC_AUTO,
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
	STAC_9200_M4,
	STAC_9200_M4_2,
	STAC_9200_PANASONIC,
	STAC_9200_MODELS
};

enum {
	STAC_9205_AUTO,
	STAC_9205_REF,
	STAC_9205_DELL_M42,
	STAC_9205_DELL_M43,
	STAC_9205_DELL_M44,
	STAC_9205_EAPD,
	STAC_9205_MODELS
};

enum {
	STAC_92HD73XX_AUTO,
	STAC_92HD73XX_NO_JD, /* no jack-detection */
	STAC_92HD73XX_REF,
	STAC_92HD73XX_INTEL,
	STAC_DELL_M6_AMIC,
	STAC_DELL_M6_DMIC,
	STAC_DELL_M6_BOTH,
	STAC_DELL_EQ,
	STAC_ALIENWARE_M17X,
	STAC_92HD73XX_MODELS
};

enum {
	STAC_92HD83XXX_AUTO,
	STAC_92HD83XXX_REF,
	STAC_92HD83XXX_PWR_REF,
	STAC_DELL_S14,
	STAC_DELL_VOSTRO_3500,
	STAC_92HD83XXX_HP_cNB11_INTQUAD,
	STAC_HP_DV7_4000,
	STAC_HP_ZEPHYR,
	STAC_92HD83XXX_HP_LED,
	STAC_92HD83XXX_MODELS
};

enum {
	STAC_92HD71BXX_AUTO,
	STAC_92HD71BXX_REF,
	STAC_DELL_M4_1,
	STAC_DELL_M4_2,
	STAC_DELL_M4_3,
	STAC_HP_M4,
	STAC_HP_DV4,
	STAC_HP_DV5,
	STAC_HP_HDX,
	STAC_HP_DV4_1222NR,
	STAC_92HD71BXX_MODELS
};

enum {
	STAC_925x_AUTO,
	STAC_925x_REF,
	STAC_M1,
	STAC_M1_2,
	STAC_M2,
	STAC_M2_2,
	STAC_M3,
	STAC_M5,
	STAC_M6,
	STAC_925x_MODELS
};

enum {
	STAC_922X_AUTO,
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
	STAC_ECS_202,
	STAC_922X_DELL_D81,
	STAC_922X_DELL_D82,
	STAC_922X_DELL_M81,
	STAC_922X_DELL_M82,
	STAC_922X_MODELS
};

enum {
	STAC_927X_AUTO,
	STAC_D965_REF_NO_JD, /* no jack-detection */
	STAC_D965_REF,
	STAC_D965_3ST,
	STAC_D965_5ST,
	STAC_D965_5ST_NO_FP,
	STAC_DELL_3ST,
	STAC_DELL_BIOS,
	STAC_927X_VOLKNOB,
	STAC_927X_MODELS
};

enum {
	STAC_9872_AUTO,
	STAC_9872_VAIO,
	STAC_9872_MODELS
};

struct sigmatel_mic_route {
	hda_nid_t pin;
	signed char mux_idx;
	signed char dmux_idx;
};

#define MAX_PINS_NUM 16
#define MAX_ADCS_NUM 4
#define MAX_DMICS_NUM 4

struct sigmatel_spec {
	struct snd_kcontrol_new *mixers[4];
	unsigned int num_mixers;

	int board_config;
	unsigned int eapd_switch: 1;
	unsigned int surr_switch: 1;
	unsigned int alt_switch: 1;
	unsigned int hp_detect: 1;
	unsigned int spdif_mute: 1;
	unsigned int check_volume_offset:1;
	unsigned int auto_mic:1;
	unsigned int linear_tone_beep:1;

	/* gpio lines */
	unsigned int eapd_mask;
	unsigned int gpio_mask;
	unsigned int gpio_dir;
	unsigned int gpio_data;
	unsigned int gpio_mute;
	unsigned int gpio_led;
	unsigned int gpio_led_polarity;
	unsigned int vref_mute_led_nid; /* pin NID for mute-LED vref control */
	unsigned int vref_led;

	/* stream */
	unsigned int stream_delay;

	/* analog loopback */
	const struct snd_kcontrol_new *aloopback_ctl;
	unsigned char aloopback_mask;
	unsigned char aloopback_shift;

	/* power management */
	unsigned int num_pwrs;
	const hda_nid_t *pwr_nids;
	const hda_nid_t *dac_list;

	/* playback */
	struct hda_input_mux *mono_mux;
	unsigned int cur_mmux;
	struct hda_multi_out multiout;
	hda_nid_t dac_nids[5];
	hda_nid_t hp_dacs[5];
	hda_nid_t speaker_dacs[5];

	int volume_offset;

	/* capture */
	const hda_nid_t *adc_nids;
	unsigned int num_adcs;
	const hda_nid_t *mux_nids;
	unsigned int num_muxes;
	const hda_nid_t *dmic_nids;
	unsigned int num_dmics;
	const hda_nid_t *dmux_nids;
	unsigned int num_dmuxes;
	const hda_nid_t *smux_nids;
	unsigned int num_smuxes;
	unsigned int num_analog_muxes;

	const unsigned long *capvols; /* amp-volume attr: HDA_COMPOSE_AMP_VAL() */
	const unsigned long *capsws; /* amp-mute attr: HDA_COMPOSE_AMP_VAL() */
	unsigned int num_caps; /* number of capture volume/switch elements */

	struct sigmatel_mic_route ext_mic;
	struct sigmatel_mic_route int_mic;
	struct sigmatel_mic_route dock_mic;

	const char * const *spdif_labels;

	hda_nid_t dig_in_nid;
	hda_nid_t mono_nid;
	hda_nid_t anabeep_nid;
	hda_nid_t digbeep_nid;

	/* pin widgets */
	const hda_nid_t *pin_nids;
	unsigned int num_pins;

	/* codec specific stuff */
	const struct hda_verb *init;
	const struct snd_kcontrol_new *mixer;

	/* capture source */
	struct hda_input_mux *dinput_mux;
	unsigned int cur_dmux[2];
	struct hda_input_mux *input_mux;
	unsigned int cur_mux[3];
	struct hda_input_mux *sinput_mux;
	unsigned int cur_smux[2];
	unsigned int cur_amux;
	hda_nid_t *amp_nids;
	unsigned int powerdown_adcs;

	/* i/o switches */
	unsigned int io_switch[2];
	unsigned int clfe_swap;
	hda_nid_t line_switch;	/* shared line-in for input and output */
	hda_nid_t mic_switch;	/* shared mic-in for input and output */
	hda_nid_t hp_switch; /* NID of HP as line-out */
	unsigned int aloopback;

	struct hda_pcm pcm_rec[2];	/* PCM information */

	/* dynamic controls and input_mux */
	struct auto_pin_cfg autocfg;
	struct snd_array kctls;
	struct hda_input_mux private_dimux;
	struct hda_input_mux private_imux;
	struct hda_input_mux private_smux;
	struct hda_input_mux private_mono_mux;

	/* auto spec */
	unsigned auto_pin_cnt;
	hda_nid_t auto_pin_nids[MAX_PINS_NUM];
	unsigned auto_adc_cnt;
	hda_nid_t auto_adc_nids[MAX_ADCS_NUM];
	hda_nid_t auto_mux_nids[MAX_ADCS_NUM];
	hda_nid_t auto_dmux_nids[MAX_ADCS_NUM];
	unsigned long auto_capvols[MAX_ADCS_NUM];
	unsigned auto_dmic_cnt;
	hda_nid_t auto_dmic_nids[MAX_DMICS_NUM];

	struct hda_vmaster_mute_hook vmaster_mute;
};

static const hda_nid_t stac9200_adc_nids[1] = {
        0x03,
};

static const hda_nid_t stac9200_mux_nids[1] = {
        0x0c,
};

static const hda_nid_t stac9200_dac_nids[1] = {
        0x02,
};

static const hda_nid_t stac92hd73xx_pwr_nids[8] = {
	0x0a, 0x0b, 0x0c, 0xd, 0x0e,
	0x0f, 0x10, 0x11
};

static const hda_nid_t stac92hd73xx_slave_dig_outs[2] = {
	0x26, 0,
};

static const hda_nid_t stac92hd73xx_adc_nids[2] = {
	0x1a, 0x1b
};

#define STAC92HD73XX_NUM_DMICS	2
static const hda_nid_t stac92hd73xx_dmic_nids[STAC92HD73XX_NUM_DMICS + 1] = {
	0x13, 0x14, 0
};

#define STAC92HD73_DAC_COUNT 5

static const hda_nid_t stac92hd73xx_mux_nids[2] = {
	0x20, 0x21,
};

static const hda_nid_t stac92hd73xx_dmux_nids[2] = {
	0x20, 0x21,
};

static const hda_nid_t stac92hd73xx_smux_nids[2] = {
	0x22, 0x23,
};

#define STAC92HD73XX_NUM_CAPS	2
static const unsigned long stac92hd73xx_capvols[] = {
	HDA_COMPOSE_AMP_VAL(0x20, 3, 0, HDA_OUTPUT),
	HDA_COMPOSE_AMP_VAL(0x21, 3, 0, HDA_OUTPUT),
};
#define stac92hd73xx_capsws	stac92hd73xx_capvols

#define STAC92HD83_DAC_COUNT 3

static const hda_nid_t stac92hd83xxx_pwr_nids[7] = {
	0x0a, 0x0b, 0x0c, 0xd, 0x0e,
	0x0f, 0x10
};

static const hda_nid_t stac92hd83xxx_slave_dig_outs[2] = {
	0x1e, 0,
};

static const hda_nid_t stac92hd83xxx_dmic_nids[] = {
		0x11, 0x20,
};

static const hda_nid_t stac92hd71bxx_pwr_nids[3] = {
	0x0a, 0x0d, 0x0f
};

static const hda_nid_t stac92hd71bxx_adc_nids[2] = {
	0x12, 0x13,
};

static const hda_nid_t stac92hd71bxx_mux_nids[2] = {
	0x1a, 0x1b
};

static const hda_nid_t stac92hd71bxx_dmux_nids[2] = {
	0x1c, 0x1d,
};

static const hda_nid_t stac92hd71bxx_smux_nids[2] = {
	0x24, 0x25,
};

#define STAC92HD71BXX_NUM_DMICS	2
static const hda_nid_t stac92hd71bxx_dmic_nids[STAC92HD71BXX_NUM_DMICS + 1] = {
	0x18, 0x19, 0
};

static const hda_nid_t stac92hd71bxx_dmic_5port_nids[STAC92HD71BXX_NUM_DMICS] = {
	0x18, 0
};

static const hda_nid_t stac92hd71bxx_slave_dig_outs[2] = {
	0x22, 0
};

#define STAC92HD71BXX_NUM_CAPS		2
static const unsigned long stac92hd71bxx_capvols[] = {
	HDA_COMPOSE_AMP_VAL(0x1c, 3, 0, HDA_OUTPUT),
	HDA_COMPOSE_AMP_VAL(0x1d, 3, 0, HDA_OUTPUT),
};
#define stac92hd71bxx_capsws	stac92hd71bxx_capvols

static const hda_nid_t stac925x_adc_nids[1] = {
        0x03,
};

static const hda_nid_t stac925x_mux_nids[1] = {
        0x0f,
};

static const hda_nid_t stac925x_dac_nids[1] = {
        0x02,
};

#define STAC925X_NUM_DMICS	1
static const hda_nid_t stac925x_dmic_nids[STAC925X_NUM_DMICS + 1] = {
	0x15, 0
};

static const hda_nid_t stac925x_dmux_nids[1] = {
	0x14,
};

static const unsigned long stac925x_capvols[] = {
	HDA_COMPOSE_AMP_VAL(0x09, 3, 0, HDA_OUTPUT),
};
static const unsigned long stac925x_capsws[] = {
	HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
};

static const hda_nid_t stac922x_adc_nids[2] = {
        0x06, 0x07,
};

static const hda_nid_t stac922x_mux_nids[2] = {
        0x12, 0x13,
};

#define STAC922X_NUM_CAPS	2
static const unsigned long stac922x_capvols[] = {
	HDA_COMPOSE_AMP_VAL(0x17, 3, 0, HDA_INPUT),
	HDA_COMPOSE_AMP_VAL(0x18, 3, 0, HDA_INPUT),
};
#define stac922x_capsws		stac922x_capvols

static const hda_nid_t stac927x_slave_dig_outs[2] = {
	0x1f, 0,
};

static const hda_nid_t stac927x_adc_nids[3] = {
        0x07, 0x08, 0x09
};

static const hda_nid_t stac927x_mux_nids[3] = {
        0x15, 0x16, 0x17
};

static const hda_nid_t stac927x_smux_nids[1] = {
	0x21,
};

static const hda_nid_t stac927x_dac_nids[6] = {
	0x02, 0x03, 0x04, 0x05, 0x06, 0
};

static const hda_nid_t stac927x_dmux_nids[1] = {
	0x1b,
};

#define STAC927X_NUM_DMICS 2
static const hda_nid_t stac927x_dmic_nids[STAC927X_NUM_DMICS + 1] = {
	0x13, 0x14, 0
};

#define STAC927X_NUM_CAPS	3
static const unsigned long stac927x_capvols[] = {
	HDA_COMPOSE_AMP_VAL(0x18, 3, 0, HDA_INPUT),
	HDA_COMPOSE_AMP_VAL(0x19, 3, 0, HDA_INPUT),
	HDA_COMPOSE_AMP_VAL(0x1a, 3, 0, HDA_INPUT),
};
static const unsigned long stac927x_capsws[] = {
	HDA_COMPOSE_AMP_VAL(0x1b, 3, 0, HDA_OUTPUT),
	HDA_COMPOSE_AMP_VAL(0x1c, 3, 0, HDA_OUTPUT),
	HDA_COMPOSE_AMP_VAL(0x1d, 3, 0, HDA_OUTPUT),
};

static const char * const stac927x_spdif_labels[5] = {
	"Digital Playback", "ADAT", "Analog Mux 1",
	"Analog Mux 2", "Analog Mux 3"
};

static const hda_nid_t stac9205_adc_nids[2] = {
        0x12, 0x13
};

static const hda_nid_t stac9205_mux_nids[2] = {
        0x19, 0x1a
};

static const hda_nid_t stac9205_dmux_nids[1] = {
	0x1d,
};

static const hda_nid_t stac9205_smux_nids[1] = {
	0x21,
};

#define STAC9205_NUM_DMICS	2
static const hda_nid_t stac9205_dmic_nids[STAC9205_NUM_DMICS + 1] = {
        0x17, 0x18, 0
};

#define STAC9205_NUM_CAPS	2
static const unsigned long stac9205_capvols[] = {
	HDA_COMPOSE_AMP_VAL(0x1b, 3, 0, HDA_INPUT),
	HDA_COMPOSE_AMP_VAL(0x1c, 3, 0, HDA_INPUT),
};
static const unsigned long stac9205_capsws[] = {
	HDA_COMPOSE_AMP_VAL(0x1d, 3, 0, HDA_OUTPUT),
	HDA_COMPOSE_AMP_VAL(0x1e, 3, 0, HDA_OUTPUT),
};

static const hda_nid_t stac9200_pin_nids[8] = {
	0x08, 0x09, 0x0d, 0x0e, 
	0x0f, 0x10, 0x11, 0x12,
};

static const hda_nid_t stac925x_pin_nids[8] = {
	0x07, 0x08, 0x0a, 0x0b, 
	0x0c, 0x0d, 0x10, 0x11,
};

static const hda_nid_t stac922x_pin_nids[10] = {
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
	0x0f, 0x10, 0x11, 0x15, 0x1b,
};

static const hda_nid_t stac92hd73xx_pin_nids[13] = {
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
	0x0f, 0x10, 0x11, 0x12, 0x13,
	0x14, 0x22, 0x23
};

#define STAC92HD71BXX_NUM_PINS 13
static const hda_nid_t stac92hd71bxx_pin_nids_4port[STAC92HD71BXX_NUM_PINS] = {
	0x0a, 0x0b, 0x0c, 0x0d, 0x00,
	0x00, 0x14, 0x18, 0x19, 0x1e,
	0x1f, 0x20, 0x27
};
static const hda_nid_t stac92hd71bxx_pin_nids_6port[STAC92HD71BXX_NUM_PINS] = {
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
	0x0f, 0x14, 0x18, 0x19, 0x1e,
	0x1f, 0x20, 0x27
};

static const hda_nid_t stac927x_pin_nids[14] = {
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
	0x0f, 0x10, 0x11, 0x12, 0x13,
	0x14, 0x21, 0x22, 0x23,
};

static const hda_nid_t stac9205_pin_nids[12] = {
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

static int stac92xx_smux_enum_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_input_mux_info(spec->sinput_mux, uinfo);
}

static int stac92xx_smux_enum_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	unsigned int smux_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_smux[smux_idx];
	return 0;
}

static int stac92xx_smux_enum_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	struct hda_input_mux *smux = &spec->private_smux;
	unsigned int smux_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	int err, val;
	hda_nid_t nid;

	err = snd_hda_input_mux_put(codec, spec->sinput_mux, ucontrol,
			spec->smux_nids[smux_idx], &spec->cur_smux[smux_idx]);
	if (err < 0)
		return err;

	if (spec->spdif_mute) {
		if (smux_idx == 0)
			nid = spec->multiout.dig_out_nid;
		else
			nid = codec->slave_dig_outs[smux_idx - 1];
		if (spec->cur_smux[smux_idx] == smux->num_items - 1)
			val = HDA_AMP_MUTE;
		else
			val = 0;
		/* un/mute SPDIF out */
		snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, val);
	}
	return 0;
}

static int stac_vrefout_set(struct hda_codec *codec,
					hda_nid_t nid, unsigned int new_vref)
{
	int error, pinctl;

	snd_printdd("%s, nid %x ctl %x\n", __func__, nid, new_vref);
	pinctl = snd_hda_codec_read(codec, nid, 0,
				AC_VERB_GET_PIN_WIDGET_CONTROL, 0);

	if (pinctl < 0)
		return pinctl;

	pinctl &= 0xff;
	pinctl &= ~AC_PINCTL_VREFEN;
	pinctl |= (new_vref & AC_PINCTL_VREFEN);

	error = snd_hda_codec_write_cache(codec, nid, 0,
					AC_VERB_SET_PIN_WIDGET_CONTROL, pinctl);
	if (error < 0)
		return error;

	return 1;
}

static unsigned int stac92xx_vref_set(struct hda_codec *codec,
					hda_nid_t nid, unsigned int new_vref)
{
	int error;
	unsigned int pincfg;
	pincfg = snd_hda_codec_read(codec, nid, 0,
				AC_VERB_GET_PIN_WIDGET_CONTROL, 0);

	pincfg &= 0xff;
	pincfg &= ~(AC_PINCTL_VREFEN | AC_PINCTL_IN_EN | AC_PINCTL_OUT_EN);
	pincfg |= new_vref;

	if (new_vref == AC_PINCTL_VREF_HIZ)
		pincfg |= AC_PINCTL_OUT_EN;
	else
		pincfg |= AC_PINCTL_IN_EN;

	error = snd_hda_codec_write_cache(codec, nid, 0,
					AC_VERB_SET_PIN_WIDGET_CONTROL, pincfg);
	if (error < 0)
		return error;
	else
		return 1;
}

static unsigned int stac92xx_vref_get(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int vref;
	vref = snd_hda_codec_read(codec, nid, 0,
				AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
	vref &= AC_PINCTL_VREFEN;
	return vref;
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
	const struct hda_input_mux *imux = spec->input_mux;
	unsigned int idx, prev_idx, didx;

	idx = ucontrol->value.enumerated.item[0];
	if (idx >= imux->num_items)
		idx = imux->num_items - 1;
	prev_idx = spec->cur_mux[adc_idx];
	if (prev_idx == idx)
		return 0;
	if (idx < spec->num_analog_muxes) {
		snd_hda_codec_write_cache(codec, spec->mux_nids[adc_idx], 0,
					  AC_VERB_SET_CONNECT_SEL,
					  imux->items[idx].index);
		if (prev_idx >= spec->num_analog_muxes &&
		    spec->mux_nids[adc_idx] != spec->dmux_nids[adc_idx]) {
			imux = spec->dinput_mux;
			/* 0 = analog */
			snd_hda_codec_write_cache(codec,
						  spec->dmux_nids[adc_idx], 0,
						  AC_VERB_SET_CONNECT_SEL,
						  imux->items[0].index);
		}
	} else {
		imux = spec->dinput_mux;
		/* first dimux item is hardcoded to select analog imux,
		 * so lets skip it
		 */
		didx = idx - spec->num_analog_muxes + 1;
		snd_hda_codec_write_cache(codec, spec->dmux_nids[adc_idx], 0,
					  AC_VERB_SET_CONNECT_SEL,
					  imux->items[didx].index);
	}
	spec->cur_mux[adc_idx] = idx;
	return 1;
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

static const struct hda_verb stac9200_core_init[] = {
	/* set dac0mux for dac converter */
	{ 0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{}
};

static const struct hda_verb stac9200_eapd_init[] = {
	/* set dac0mux for dac converter */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_EAPD_BTLENABLE, 0x02},
	{}
};

static const struct hda_verb dell_eq_core_init[] = {
	/* set master volume to max value without distortion
	 * and direct control */
	{ 0x1f, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xec},
	{}
};

static const struct hda_verb stac92hd73xx_core_init[] = {
	/* set master volume and direct control */
	{ 0x1f, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	{}
};

static const struct hda_verb stac92hd83xxx_core_init[] = {
	/* power state controls amps */
	{ 0x01, AC_VERB_SET_EAPD, 1 << 2},
	{}
};

static const struct hda_verb stac92hd83xxx_hp_zephyr_init[] = {
	{ 0x22, 0x785, 0x43 },
	{ 0x22, 0x782, 0xe0 },
	{ 0x22, 0x795, 0x00 },
	{}
};

static const struct hda_verb stac92hd71bxx_core_init[] = {
	/* set master volume and direct control */
	{ 0x28, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	{}
};

static const struct hda_verb stac92hd71bxx_unmute_core_init[] = {
	/* unmute right and left channels for nodes 0x0f, 0xa, 0x0d */
	{ 0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{ 0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{}
};

static const struct hda_verb stac925x_core_init[] = {
	/* set dac0mux for dac converter */
	{ 0x06, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* mute the master volume */
	{ 0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{}
};

static const struct hda_verb stac922x_core_init[] = {
	/* set master volume and direct control */	
	{ 0x16, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	{}
};

static const struct hda_verb d965_core_init[] = {
	/* set master volume and direct control */	
	{ 0x24, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	/* unmute node 0x1b */
	{ 0x1b, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* select node 0x03 as DAC */	
	{ 0x0b, AC_VERB_SET_CONNECT_SEL, 0x01},
	{}
};

static const struct hda_verb dell_3st_core_init[] = {
	/* don't set delta bit */
	{0x24, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0x7f},
	/* unmute node 0x1b */
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* select node 0x03 as DAC */
	{0x0b, AC_VERB_SET_CONNECT_SEL, 0x01},
	{}
};

static const struct hda_verb stac927x_core_init[] = {
	/* set master volume and direct control */	
	{ 0x24, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	/* enable analog pc beep path */
	{ 0x01, AC_VERB_SET_DIGI_CONVERT_2, 1 << 5},
	{}
};

static const struct hda_verb stac927x_volknob_core_init[] = {
	/* don't set delta bit */
	{0x24, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0x7f},
	/* enable analog pc beep path */
	{0x01, AC_VERB_SET_DIGI_CONVERT_2, 1 << 5},
	{}
};

static const struct hda_verb stac9205_core_init[] = {
	/* set master volume and direct control */	
	{ 0x24, AC_VERB_SET_VOLUME_KNOB_CONTROL, 0xff},
	/* enable analog pc beep path */
	{ 0x01, AC_VERB_SET_DIGI_CONVERT_2, 1 << 5},
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

#define DC_BIAS(xname, idx, nid) \
	{ \
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name = xname, \
		.index = idx, \
		.info = stac92xx_dc_bias_info, \
		.get = stac92xx_dc_bias_get, \
		.put = stac92xx_dc_bias_put, \
		.private_value = nid, \
	}

static const struct snd_kcontrol_new stac9200_mixer[] = {
	HDA_CODEC_VOLUME_MIN_MUTE("PCM Playback Volume", 0xb, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0xb, 0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x0a, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x0a, 0, HDA_OUTPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new stac92hd73xx_6ch_loopback[] = {
	STAC_ANALOG_LOOPBACK(0xFA0, 0x7A1, 3),
	{}
};

static const struct snd_kcontrol_new stac92hd73xx_8ch_loopback[] = {
	STAC_ANALOG_LOOPBACK(0xFA0, 0x7A1, 4),
	{}
};

static const struct snd_kcontrol_new stac92hd73xx_10ch_loopback[] = {
	STAC_ANALOG_LOOPBACK(0xFA0, 0x7A1, 5),
	{}
};


static const struct snd_kcontrol_new stac92hd71bxx_loopback[] = {
	STAC_ANALOG_LOOPBACK(0xFA0, 0x7A0, 2)
};

static const struct snd_kcontrol_new stac925x_mixer[] = {
	HDA_CODEC_VOLUME_MIN_MUTE("PCM Playback Volume", 0xe, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PCM Playback Switch", 0x0e, 0, HDA_OUTPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new stac9205_loopback[] = {
	STAC_ANALOG_LOOPBACK(0xFE0, 0x7E0, 1),
	{}
};

static const struct snd_kcontrol_new stac927x_loopback[] = {
	STAC_ANALOG_LOOPBACK(0xFEB, 0x7EB, 1),
	{}
};

static struct snd_kcontrol_new stac_dmux_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Digital Input Source",
	/* count set later */
	.info = stac92xx_dmux_enum_info,
	.get = stac92xx_dmux_enum_get,
	.put = stac92xx_dmux_enum_put,
};

static struct snd_kcontrol_new stac_smux_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "IEC958 Playback Source",
	/* count set later */
	.info = stac92xx_smux_enum_info,
	.get = stac92xx_smux_enum_get,
	.put = stac92xx_smux_enum_put,
};

static const char * const slave_pfxs[] = {
	"Front", "Surround", "Center", "LFE", "Side",
	"Headphone", "Speaker", "IEC958",
	NULL
};

static void stac92xx_update_led_status(struct hda_codec *codec, int enabled);

static void stac92xx_vmaster_hook(void *private_data, int val)
{
	stac92xx_update_led_status(private_data, val);
}

static void stac92xx_free_kctls(struct hda_codec *codec);

static int stac92xx_build_controls(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	unsigned int vmaster_tlv[4];
	int err;
	int i;

	if (spec->mixer) {
		err = snd_hda_add_new_ctls(codec, spec->mixer);
		if (err < 0)
			return err;
	}

	for (i = 0; i < spec->num_mixers; i++) {
		err = snd_hda_add_new_ctls(codec, spec->mixers[i]);
		if (err < 0)
			return err;
	}
	if (!spec->auto_mic && spec->num_dmuxes > 0 &&
	    snd_hda_get_bool_hint(codec, "separate_dmux") == 1) {
		stac_dmux_mixer.count = spec->num_dmuxes;
		err = snd_hda_ctl_add(codec, 0,
				  snd_ctl_new1(&stac_dmux_mixer, codec));
		if (err < 0)
			return err;
	}
	if (spec->num_smuxes > 0) {
		int wcaps = get_wcaps(codec, spec->multiout.dig_out_nid);
		struct hda_input_mux *smux = &spec->private_smux;
		/* check for mute support on SPDIF out */
		if (wcaps & AC_WCAP_OUT_AMP) {
			snd_hda_add_imux_item(smux, "Off", 0, NULL);
			spec->spdif_mute = 1;
		}
		stac_smux_mixer.count = spec->num_smuxes;
		err = snd_hda_ctl_add(codec, 0,
				  snd_ctl_new1(&stac_smux_mixer, codec));
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
	if (spec->dig_in_nid && !(spec->gpio_dir & 0x01)) {
		err = snd_hda_create_spdif_in_ctls(codec, spec->dig_in_nid);
		if (err < 0)
			return err;
	}

	/* if we have no master control, let's create it */
	snd_hda_set_vmaster_tlv(codec, spec->multiout.dac_nids[0],
				HDA_OUTPUT, vmaster_tlv);
	/* correct volume offset */
	vmaster_tlv[2] += vmaster_tlv[3] * spec->volume_offset;
	/* minimum value is actually mute */
	vmaster_tlv[3] |= TLV_DB_SCALE_MUTE;
	err = snd_hda_add_vmaster(codec, "Master Playback Volume",
				  vmaster_tlv, slave_pfxs,
				  "Playback Volume");
	if (err < 0)
		return err;

	err = __snd_hda_add_vmaster(codec, "Master Playback Switch",
				    NULL, slave_pfxs,
				    "Playback Switch", true,
				    &spec->vmaster_mute.sw_kctl);
	if (err < 0)
		return err;

	if (spec->gpio_led) {
		spec->vmaster_mute.hook = stac92xx_vmaster_hook;
		err = snd_hda_add_vmaster_hook(codec, &spec->vmaster_mute, true);
		if (err < 0)
			return err;
	}

	if (spec->aloopback_ctl &&
	    snd_hda_get_bool_hint(codec, "loopback") == 1) {
		err = snd_hda_add_new_ctls(codec, spec->aloopback_ctl);
		if (err < 0)
			return err;
	}

	stac92xx_free_kctls(codec); /* no longer needed */

	err = snd_hda_jack_add_kctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	return 0;	
}

static const unsigned int ref9200_pin_configs[8] = {
	0x01c47010, 0x01447010, 0x0221401f, 0x01114010,
	0x02a19020, 0x01a19021, 0x90100140, 0x01813122,
};

static const unsigned int gateway9200_m4_pin_configs[8] = {
	0x400000fe, 0x404500f4, 0x400100f0, 0x90110010,
	0x400100f1, 0x02a1902e, 0x500000f2, 0x500000f3,
};
static const unsigned int gateway9200_m4_2_pin_configs[8] = {
	0x400000fe, 0x404500f4, 0x400100f0, 0x90110010,
	0x400100f1, 0x02a1902e, 0x500000f2, 0x500000f3,
};

/*
    STAC 9200 pin configs for
    102801A8
    102801DE
    102801E8
*/
static const unsigned int dell9200_d21_pin_configs[8] = {
	0x400001f0, 0x400001f1, 0x02214030, 0x01014010, 
	0x02a19020, 0x01a19021, 0x90100140, 0x01813122,
};

/* 
    STAC 9200 pin configs for
    102801C0
    102801C1
*/
static const unsigned int dell9200_d22_pin_configs[8] = {
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
static const unsigned int dell9200_d23_pin_configs[8] = {
	0x400001f0, 0x400001f1, 0x0221401f, 0x01014010, 
	0x01813020, 0x01a19021, 0x90100140, 0x400001f2, 
};


/* 
    STAC 9200-32 pin configs for
    102801B5 (Dell Inspiron 630m)
    102801D8 (Dell Inspiron 640m)
*/
static const unsigned int dell9200_m21_pin_configs[8] = {
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
static const unsigned int dell9200_m22_pin_configs[8] = {
	0x40c003fa, 0x0144131f, 0x0321121f, 0x90170310, 
	0x90a70321, 0x03a11020, 0x401003fb, 0x40f000fc,
};

/* 
    STAC 9200-32 pin configs for
    102801CE (Dell XPS M1710)
    102801CF (Dell Precision M90)
*/
static const unsigned int dell9200_m23_pin_configs[8] = {
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
static const unsigned int dell9200_m24_pin_configs[8] = {
	0x40c003fa, 0x404003fb, 0x0321121f, 0x90170310, 
	0x408003fc, 0x03a11020, 0x401003fd, 0x403003fe, 
};

/*
    STAC 9200-32 pin configs for
    102801BD (Dell Inspiron E1505n)
    102801EE
    102801EF
*/
static const unsigned int dell9200_m25_pin_configs[8] = {
	0x40c003fa, 0x01441340, 0x0421121f, 0x90170310, 
	0x408003fb, 0x04a11020, 0x401003fc, 0x403003fd,
};

/*
    STAC 9200-32 pin configs for
    102801F5 (Dell Inspiron 1501)
    102801F6
*/
static const unsigned int dell9200_m26_pin_configs[8] = {
	0x40c003fa, 0x404003fb, 0x0421121f, 0x90170310, 
	0x408003fc, 0x04a11020, 0x401003fd, 0x403003fe,
};

/*
    STAC 9200-32
    102801CD (Dell Inspiron E1705/9400)
*/
static const unsigned int dell9200_m27_pin_configs[8] = {
	0x40c003fa, 0x01441340, 0x0421121f, 0x90170310,
	0x90170310, 0x04a11020, 0x90170310, 0x40f003fc,
};

static const unsigned int oqo9200_pin_configs[8] = {
	0x40c000f0, 0x404000f1, 0x0221121f, 0x02211210,
	0x90170111, 0x90a70120, 0x400000f2, 0x400000f3,
};


static const unsigned int *stac9200_brd_tbl[STAC_9200_MODELS] = {
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
	[STAC_9200_M4] = gateway9200_m4_pin_configs,
	[STAC_9200_M4_2] = gateway9200_m4_2_pin_configs,
	[STAC_9200_PANASONIC] = ref9200_pin_configs,
};

static const char * const stac9200_models[STAC_9200_MODELS] = {
	[STAC_AUTO] = "auto",
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
	[STAC_9200_M4] = "gateway-m4",
	[STAC_9200_M4_2] = "gateway-m4-2",
	[STAC_9200_PANASONIC] = "panasonic",
};

static const struct snd_pci_quirk stac9200_cfg_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668,
		      "DFI LanParty", STAC_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DFI, 0x3101,
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
	SND_PCI_QUIRK(0x107b, 0x0205, "Gateway S-7110M", STAC_9200_M4),
	SND_PCI_QUIRK(0x107b, 0x0317, "Gateway MT3423, MX341*", STAC_9200_M4_2),
	SND_PCI_QUIRK(0x107b, 0x0318, "Gateway ML3019, MT3707", STAC_9200_M4_2),
	/* OQO Mobile */
	SND_PCI_QUIRK(0x1106, 0x3288, "OQO Model 2", STAC_9200_OQO),
	{} /* terminator */
};

static const unsigned int ref925x_pin_configs[8] = {
	0x40c003f0, 0x424503f2, 0x01813022, 0x02a19021,
	0x90a70320, 0x02214210, 0x01019020, 0x9033032e,
};

static const unsigned int stac925xM1_pin_configs[8] = {
	0x40c003f4, 0x424503f2, 0x400000f3, 0x02a19020,
	0x40a000f0, 0x90100210, 0x400003f1, 0x9033032e,
};

static const unsigned int stac925xM1_2_pin_configs[8] = {
	0x40c003f4, 0x424503f2, 0x400000f3, 0x02a19020,
	0x40a000f0, 0x90100210, 0x400003f1, 0x9033032e,
};

static const unsigned int stac925xM2_pin_configs[8] = {
	0x40c003f4, 0x424503f2, 0x400000f3, 0x02a19020,
	0x40a000f0, 0x90100210, 0x400003f1, 0x9033032e,
};

static const unsigned int stac925xM2_2_pin_configs[8] = {
	0x40c003f4, 0x424503f2, 0x400000f3, 0x02a19020,
	0x40a000f0, 0x90100210, 0x400003f1, 0x9033032e,
};

static const unsigned int stac925xM3_pin_configs[8] = {
	0x40c003f4, 0x424503f2, 0x400000f3, 0x02a19020,
	0x40a000f0, 0x90100210, 0x400003f1, 0x503303f3,
};

static const unsigned int stac925xM5_pin_configs[8] = {
	0x40c003f4, 0x424503f2, 0x400000f3, 0x02a19020,
	0x40a000f0, 0x90100210, 0x400003f1, 0x9033032e,
};

static const unsigned int stac925xM6_pin_configs[8] = {
	0x40c003f4, 0x424503f2, 0x400000f3, 0x02a19020,
	0x40a000f0, 0x90100210, 0x400003f1, 0x90330320,
};

static const unsigned int *stac925x_brd_tbl[STAC_925x_MODELS] = {
	[STAC_REF] = ref925x_pin_configs,
	[STAC_M1] = stac925xM1_pin_configs,
	[STAC_M1_2] = stac925xM1_2_pin_configs,
	[STAC_M2] = stac925xM2_pin_configs,
	[STAC_M2_2] = stac925xM2_2_pin_configs,
	[STAC_M3] = stac925xM3_pin_configs,
	[STAC_M5] = stac925xM5_pin_configs,
	[STAC_M6] = stac925xM6_pin_configs,
};

static const char * const stac925x_models[STAC_925x_MODELS] = {
	[STAC_925x_AUTO] = "auto",
	[STAC_REF] = "ref",
	[STAC_M1] = "m1",
	[STAC_M1_2] = "m1-2",
	[STAC_M2] = "m2",
	[STAC_M2_2] = "m2-2",
	[STAC_M3] = "m3",
	[STAC_M5] = "m5",
	[STAC_M6] = "m6",
};

static const struct snd_pci_quirk stac925x_codec_id_cfg_tbl[] = {
	SND_PCI_QUIRK(0x107b, 0x0316, "Gateway M255", STAC_M2),
	SND_PCI_QUIRK(0x107b, 0x0366, "Gateway MP6954", STAC_M5),
	SND_PCI_QUIRK(0x107b, 0x0461, "Gateway NX560XL", STAC_M1),
	SND_PCI_QUIRK(0x107b, 0x0681, "Gateway NX860", STAC_M2),
	SND_PCI_QUIRK(0x107b, 0x0367, "Gateway MX6453", STAC_M1_2),
	/* Not sure about the brand name for those */
	SND_PCI_QUIRK(0x107b, 0x0281, "Gateway mobile", STAC_M1),
	SND_PCI_QUIRK(0x107b, 0x0507, "Gateway mobile", STAC_M3),
	SND_PCI_QUIRK(0x107b, 0x0281, "Gateway mobile", STAC_M6),
	SND_PCI_QUIRK(0x107b, 0x0685, "Gateway mobile", STAC_M2_2),
	{} /* terminator */
};

static const struct snd_pci_quirk stac925x_cfg_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668, "DFI LanParty", STAC_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DFI, 0x3101, "DFI LanParty", STAC_REF),
	SND_PCI_QUIRK(0x8384, 0x7632, "Stac9202 Reference Board", STAC_REF),

	/* Default table for unknown ID */
	SND_PCI_QUIRK(0x1002, 0x437b, "Gateway mobile", STAC_M2_2),

	{} /* terminator */
};

static const unsigned int ref92hd73xx_pin_configs[13] = {
	0x02214030, 0x02a19040, 0x01a19020, 0x02214030,
	0x0181302e, 0x01014010, 0x01014020, 0x01014030,
	0x02319040, 0x90a000f0, 0x90a000f0, 0x01452050,
	0x01452050,
};

static const unsigned int dell_m6_pin_configs[13] = {
	0x0321101f, 0x4f00000f, 0x4f0000f0, 0x90170110,
	0x03a11020, 0x0321101f, 0x4f0000f0, 0x4f0000f0,
	0x4f0000f0, 0x90a60160, 0x4f0000f0, 0x4f0000f0,
	0x4f0000f0,
};

static const unsigned int alienware_m17x_pin_configs[13] = {
	0x0321101f, 0x0321101f, 0x03a11020, 0x03014020,
	0x90170110, 0x4f0000f0, 0x4f0000f0, 0x4f0000f0,
	0x4f0000f0, 0x90a60160, 0x4f0000f0, 0x4f0000f0,
	0x904601b0,
};

static const unsigned int intel_dg45id_pin_configs[13] = {
	0x02214230, 0x02A19240, 0x01013214, 0x01014210,
	0x01A19250, 0x01011212, 0x01016211
};

static const unsigned int *stac92hd73xx_brd_tbl[STAC_92HD73XX_MODELS] = {
	[STAC_92HD73XX_REF]	= ref92hd73xx_pin_configs,
	[STAC_DELL_M6_AMIC]	= dell_m6_pin_configs,
	[STAC_DELL_M6_DMIC]	= dell_m6_pin_configs,
	[STAC_DELL_M6_BOTH]	= dell_m6_pin_configs,
	[STAC_DELL_EQ]	= dell_m6_pin_configs,
	[STAC_ALIENWARE_M17X]	= alienware_m17x_pin_configs,
	[STAC_92HD73XX_INTEL]	= intel_dg45id_pin_configs,
};

static const char * const stac92hd73xx_models[STAC_92HD73XX_MODELS] = {
	[STAC_92HD73XX_AUTO] = "auto",
	[STAC_92HD73XX_NO_JD] = "no-jd",
	[STAC_92HD73XX_REF] = "ref",
	[STAC_92HD73XX_INTEL] = "intel",
	[STAC_DELL_M6_AMIC] = "dell-m6-amic",
	[STAC_DELL_M6_DMIC] = "dell-m6-dmic",
	[STAC_DELL_M6_BOTH] = "dell-m6",
	[STAC_DELL_EQ] = "dell-eq",
	[STAC_ALIENWARE_M17X] = "alienware",
};

static const struct snd_pci_quirk stac92hd73xx_cfg_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668,
				"DFI LanParty", STAC_92HD73XX_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DFI, 0x3101,
				"DFI LanParty", STAC_92HD73XX_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x5002,
				"Intel DG45ID", STAC_92HD73XX_INTEL),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x5003,
				"Intel DG45FC", STAC_92HD73XX_INTEL),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0254,
				"Dell Studio 1535", STAC_DELL_M6_DMIC),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0255,
				"unknown Dell", STAC_DELL_M6_DMIC),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0256,
				"unknown Dell", STAC_DELL_M6_BOTH),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0257,
				"unknown Dell", STAC_DELL_M6_BOTH),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x025e,
				"unknown Dell", STAC_DELL_M6_AMIC),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x025f,
				"unknown Dell", STAC_DELL_M6_AMIC),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0271,
				"unknown Dell", STAC_DELL_M6_DMIC),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0272,
				"unknown Dell", STAC_DELL_M6_DMIC),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x029f,
				"Dell Studio 1537", STAC_DELL_M6_DMIC),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x02a0,
				"Dell Studio 17", STAC_DELL_M6_DMIC),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x02be,
				"Dell Studio 1555", STAC_DELL_M6_DMIC),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x02bd,
				"Dell Studio 1557", STAC_DELL_M6_DMIC),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x02fe,
				"Dell Studio XPS 1645", STAC_DELL_M6_DMIC),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0413,
				"Dell Studio 1558", STAC_DELL_M6_DMIC),
	{} /* terminator */
};

static const struct snd_pci_quirk stac92hd73xx_codec_id_cfg_tbl[] = {
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x02a1,
		      "Alienware M17x", STAC_ALIENWARE_M17X),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x043a,
		      "Alienware M17x", STAC_ALIENWARE_M17X),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0490,
		      "Alienware M17x R3", STAC_DELL_EQ),
	{} /* terminator */
};

static const unsigned int ref92hd83xxx_pin_configs[10] = {
	0x02214030, 0x02211010, 0x02a19020, 0x02170130,
	0x01014050, 0x01819040, 0x01014020, 0x90a3014e,
	0x01451160, 0x98560170,
};

static const unsigned int dell_s14_pin_configs[10] = {
	0x0221403f, 0x0221101f, 0x02a19020, 0x90170110,
	0x40f000f0, 0x40f000f0, 0x40f000f0, 0x90a60160,
	0x40f000f0, 0x40f000f0,
};

static const unsigned int dell_vostro_3500_pin_configs[10] = {
	0x02a11020, 0x0221101f, 0x400000f0, 0x90170110,
	0x400000f1, 0x400000f2, 0x400000f3, 0x90a60160,
	0x400000f4, 0x400000f5,
};

static const unsigned int hp_dv7_4000_pin_configs[10] = {
	0x03a12050, 0x0321201f, 0x40f000f0, 0x90170110,
	0x40f000f0, 0x40f000f0, 0x90170110, 0xd5a30140,
	0x40f000f0, 0x40f000f0,
};

static const unsigned int hp_zephyr_pin_configs[10] = {
	0x01813050, 0x0421201f, 0x04a1205e, 0x96130310,
	0x96130310, 0x0101401f, 0x1111611f, 0xd5a30130,
	0, 0,
};

static const unsigned int hp_cNB11_intquad_pin_configs[10] = {
	0x40f000f0, 0x0221101f, 0x02a11020, 0x92170110,
	0x40f000f0, 0x92170110, 0x40f000f0, 0xd5a30130,
	0x40f000f0, 0x40f000f0,
};

static const unsigned int *stac92hd83xxx_brd_tbl[STAC_92HD83XXX_MODELS] = {
	[STAC_92HD83XXX_REF] = ref92hd83xxx_pin_configs,
	[STAC_92HD83XXX_PWR_REF] = ref92hd83xxx_pin_configs,
	[STAC_DELL_S14] = dell_s14_pin_configs,
	[STAC_DELL_VOSTRO_3500] = dell_vostro_3500_pin_configs,
	[STAC_92HD83XXX_HP_cNB11_INTQUAD] = hp_cNB11_intquad_pin_configs,
	[STAC_HP_DV7_4000] = hp_dv7_4000_pin_configs,
	[STAC_HP_ZEPHYR] = hp_zephyr_pin_configs,
};

static const char * const stac92hd83xxx_models[STAC_92HD83XXX_MODELS] = {
	[STAC_92HD83XXX_AUTO] = "auto",
	[STAC_92HD83XXX_REF] = "ref",
	[STAC_92HD83XXX_PWR_REF] = "mic-ref",
	[STAC_DELL_S14] = "dell-s14",
	[STAC_DELL_VOSTRO_3500] = "dell-vostro-3500",
	[STAC_92HD83XXX_HP_cNB11_INTQUAD] = "hp_cNB11_intquad",
	[STAC_HP_DV7_4000] = "hp-dv7-4000",
	[STAC_HP_ZEPHYR] = "hp-zephyr",
	[STAC_92HD83XXX_HP_LED] = "hp-led",
};

static const struct snd_pci_quirk stac92hd83xxx_cfg_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668,
		      "DFI LanParty", STAC_92HD83XXX_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DFI, 0x3101,
		      "DFI LanParty", STAC_92HD83XXX_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x02ba,
		      "unknown Dell", STAC_DELL_S14),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x1028,
		      "Dell Vostro 3500", STAC_DELL_VOSTRO_3500),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x1656,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x1657,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x1658,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x1659,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x165A,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x165B,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x3388,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x3389,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x355B,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x355C,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x355D,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x355E,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x355F,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x3560,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x358B,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x358C,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x358D,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x3591,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x3592,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x3593,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x3561,
			  "HP", STAC_HP_ZEPHYR),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x3660,
			  "HP Mini", STAC_92HD83XXX_HP_LED),
	{} /* terminator */
};

static const struct snd_pci_quirk stac92hd83xxx_codec_id_cfg_tbl[] = {
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x3561,
			  "HP", STAC_HP_ZEPHYR),
	{} /* terminator */
};

static const unsigned int ref92hd71bxx_pin_configs[STAC92HD71BXX_NUM_PINS] = {
	0x02214030, 0x02a19040, 0x01a19020, 0x01014010,
	0x0181302e, 0x01014010, 0x01019020, 0x90a000f0,
	0x90a000f0, 0x01452050, 0x01452050, 0x00000000,
	0x00000000
};

static const unsigned int dell_m4_1_pin_configs[STAC92HD71BXX_NUM_PINS] = {
	0x0421101f, 0x04a11221, 0x40f000f0, 0x90170110,
	0x23a1902e, 0x23014250, 0x40f000f0, 0x90a000f0,
	0x40f000f0, 0x4f0000f0, 0x4f0000f0, 0x00000000,
	0x00000000
};

static const unsigned int dell_m4_2_pin_configs[STAC92HD71BXX_NUM_PINS] = {
	0x0421101f, 0x04a11221, 0x90a70330, 0x90170110,
	0x23a1902e, 0x23014250, 0x40f000f0, 0x40f000f0,
	0x40f000f0, 0x044413b0, 0x044413b0, 0x00000000,
	0x00000000
};

static const unsigned int dell_m4_3_pin_configs[STAC92HD71BXX_NUM_PINS] = {
	0x0421101f, 0x04a11221, 0x90a70330, 0x90170110,
	0x40f000f0, 0x40f000f0, 0x40f000f0, 0x90a000f0,
	0x40f000f0, 0x044413b0, 0x044413b0, 0x00000000,
	0x00000000
};

static const unsigned int *stac92hd71bxx_brd_tbl[STAC_92HD71BXX_MODELS] = {
	[STAC_92HD71BXX_REF] = ref92hd71bxx_pin_configs,
	[STAC_DELL_M4_1]	= dell_m4_1_pin_configs,
	[STAC_DELL_M4_2]	= dell_m4_2_pin_configs,
	[STAC_DELL_M4_3]	= dell_m4_3_pin_configs,
	[STAC_HP_M4]		= NULL,
	[STAC_HP_DV4]		= NULL,
	[STAC_HP_DV5]		= NULL,
	[STAC_HP_HDX]           = NULL,
	[STAC_HP_DV4_1222NR]	= NULL,
};

static const char * const stac92hd71bxx_models[STAC_92HD71BXX_MODELS] = {
	[STAC_92HD71BXX_AUTO] = "auto",
	[STAC_92HD71BXX_REF] = "ref",
	[STAC_DELL_M4_1] = "dell-m4-1",
	[STAC_DELL_M4_2] = "dell-m4-2",
	[STAC_DELL_M4_3] = "dell-m4-3",
	[STAC_HP_M4] = "hp-m4",
	[STAC_HP_DV4] = "hp-dv4",
	[STAC_HP_DV5] = "hp-dv5",
	[STAC_HP_HDX] = "hp-hdx",
	[STAC_HP_DV4_1222NR] = "hp-dv4-1222nr",
};

static const struct snd_pci_quirk stac92hd71bxx_cfg_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668,
		      "DFI LanParty", STAC_92HD71BXX_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DFI, 0x3101,
		      "DFI LanParty", STAC_92HD71BXX_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x30fb,
		      "HP dv4-1222nr", STAC_HP_DV4_1222NR),
	SND_PCI_QUIRK_MASK(PCI_VENDOR_ID_HP, 0xfff0, 0x1720,
			  "HP", STAC_HP_DV5),
	SND_PCI_QUIRK_MASK(PCI_VENDOR_ID_HP, 0xfff0, 0x3080,
		      "HP", STAC_HP_DV5),
	SND_PCI_QUIRK_MASK(PCI_VENDOR_ID_HP, 0xfff0, 0x30f0,
		      "HP dv4-7", STAC_HP_DV4),
	SND_PCI_QUIRK_MASK(PCI_VENDOR_ID_HP, 0xfff0, 0x3600,
		      "HP dv4-7", STAC_HP_DV5),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x3610,
		      "HP HDX", STAC_HP_HDX),  /* HDX18 */
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x361a,
		      "HP mini 1000", STAC_HP_M4),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x361b,
		      "HP HDX", STAC_HP_HDX),  /* HDX16 */
	SND_PCI_QUIRK_MASK(PCI_VENDOR_ID_HP, 0xfff0, 0x3620,
		      "HP dv6", STAC_HP_DV5),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x3061,
		      "HP dv6", STAC_HP_DV5), /* HP dv6-1110ax */
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x363e,
		      "HP DV6", STAC_HP_DV5),
	SND_PCI_QUIRK_MASK(PCI_VENDOR_ID_HP, 0xfff0, 0x7010,
		      "HP", STAC_HP_DV5),
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
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x02aa,
				"unknown Dell", STAC_DELL_M4_3),
	{} /* terminator */
};

static const unsigned int ref922x_pin_configs[10] = {
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
static const unsigned int dell_922x_d81_pin_configs[10] = {
	0x02214030, 0x01a19021, 0x01111012, 0x01114010,
	0x02a19020, 0x01117011, 0x400001f0, 0x400001f1,
	0x01813122, 0x400001f2,
};

/*
    STAC 922X pin configs for
    102801AC
    102801D0
*/
static const unsigned int dell_922x_d82_pin_configs[10] = {
	0x02214030, 0x01a19021, 0x01111012, 0x01114010,
	0x02a19020, 0x01117011, 0x01451140, 0x400001f0,
	0x01813122, 0x400001f1,
};

/*
    STAC 922X pin configs for
    102801BF
*/
static const unsigned int dell_922x_m81_pin_configs[10] = {
	0x0321101f, 0x01112024, 0x01111222, 0x91174220,
	0x03a11050, 0x01116221, 0x90a70330, 0x01452340, 
	0x40C003f1, 0x405003f0,
};

/*
    STAC 9221 A1 pin configs for
    102801D7 (Dell XPS M1210)
*/
static const unsigned int dell_922x_m82_pin_configs[10] = {
	0x02211211, 0x408103ff, 0x02a1123e, 0x90100310, 
	0x408003f1, 0x0221121f, 0x03451340, 0x40c003f2, 
	0x508003f3, 0x405003f4, 
};

static const unsigned int d945gtp3_pin_configs[10] = {
	0x0221401f, 0x01a19022, 0x01813021, 0x01014010,
	0x40000100, 0x40000100, 0x40000100, 0x40000100,
	0x02a19120, 0x40000100,
};

static const unsigned int d945gtp5_pin_configs[10] = {
	0x0221401f, 0x01011012, 0x01813024, 0x01014010,
	0x01a19021, 0x01016011, 0x01452130, 0x40000100,
	0x02a19320, 0x40000100,
};

static const unsigned int intel_mac_v1_pin_configs[10] = {
	0x0121e21f, 0x400000ff, 0x9017e110, 0x400000fd,
	0x400000fe, 0x0181e020, 0x1145e030, 0x11c5e240,
	0x400000fc, 0x400000fb,
};

static const unsigned int intel_mac_v2_pin_configs[10] = {
	0x0121e21f, 0x90a7012e, 0x9017e110, 0x400000fd,
	0x400000fe, 0x0181e020, 0x1145e230, 0x500000fa,
	0x400000fc, 0x400000fb,
};

static const unsigned int intel_mac_v3_pin_configs[10] = {
	0x0121e21f, 0x90a7012e, 0x9017e110, 0x400000fd,
	0x400000fe, 0x0181e020, 0x1145e230, 0x11c5e240,
	0x400000fc, 0x400000fb,
};

static const unsigned int intel_mac_v4_pin_configs[10] = {
	0x0321e21f, 0x03a1e02e, 0x9017e110, 0x9017e11f,
	0x400000fe, 0x0381e020, 0x1345e230, 0x13c5e240,
	0x400000fc, 0x400000fb,
};

static const unsigned int intel_mac_v5_pin_configs[10] = {
	0x0321e21f, 0x03a1e02e, 0x9017e110, 0x9017e11f,
	0x400000fe, 0x0381e020, 0x1345e230, 0x13c5e240,
	0x400000fc, 0x400000fb,
};

static const unsigned int ecs202_pin_configs[10] = {
	0x0221401f, 0x02a19020, 0x01a19020, 0x01114010,
	0x408000f0, 0x01813022, 0x074510a0, 0x40c400f1,
	0x9037012e, 0x40e000f2,
};

static const unsigned int *stac922x_brd_tbl[STAC_922X_MODELS] = {
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
	[STAC_ECS_202] = ecs202_pin_configs,
	[STAC_922X_DELL_D81] = dell_922x_d81_pin_configs,
	[STAC_922X_DELL_D82] = dell_922x_d82_pin_configs,	
	[STAC_922X_DELL_M81] = dell_922x_m81_pin_configs,
	[STAC_922X_DELL_M82] = dell_922x_m82_pin_configs,	
};

static const char * const stac922x_models[STAC_922X_MODELS] = {
	[STAC_922X_AUTO] = "auto",
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
	[STAC_ECS_202] = "ecs202",
	[STAC_922X_DELL_D81] = "dell-d81",
	[STAC_922X_DELL_D82] = "dell-d82",
	[STAC_922X_DELL_M81] = "dell-m81",
	[STAC_922X_DELL_M82] = "dell-m82",
};

static const struct snd_pci_quirk stac922x_cfg_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668,
		      "DFI LanParty", STAC_D945_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DFI, 0x3101,
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
	/* other intel */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x0204,
		      "Intel D945", STAC_D945_REF),
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
	/* ECS/PC Chips boards */
	SND_PCI_QUIRK_MASK(0x1019, 0xf000, 0x2000,
		      "ECS/PC chips", STAC_ECS_202),
	{} /* terminator */
};

static const unsigned int ref927x_pin_configs[14] = {
	0x02214020, 0x02a19080, 0x0181304e, 0x01014010,
	0x01a19040, 0x01011012, 0x01016011, 0x0101201f, 
	0x183301f0, 0x18a001f0, 0x18a001f0, 0x01442070,
	0x01c42190, 0x40000100,
};

static const unsigned int d965_3st_pin_configs[14] = {
	0x0221401f, 0x02a19120, 0x40000100, 0x01014011,
	0x01a19021, 0x01813024, 0x40000100, 0x40000100,
	0x40000100, 0x40000100, 0x40000100, 0x40000100,
	0x40000100, 0x40000100
};

static const unsigned int d965_5st_pin_configs[14] = {
	0x02214020, 0x02a19080, 0x0181304e, 0x01014010,
	0x01a19040, 0x01011012, 0x01016011, 0x40000100,
	0x40000100, 0x40000100, 0x40000100, 0x01442070,
	0x40000100, 0x40000100
};

static const unsigned int d965_5st_no_fp_pin_configs[14] = {
	0x40000100, 0x40000100, 0x0181304e, 0x01014010,
	0x01a19040, 0x01011012, 0x01016011, 0x40000100,
	0x40000100, 0x40000100, 0x40000100, 0x01442070,
	0x40000100, 0x40000100
};

static const unsigned int dell_3st_pin_configs[14] = {
	0x02211230, 0x02a11220, 0x01a19040, 0x01114210,
	0x01111212, 0x01116211, 0x01813050, 0x01112214,
	0x403003fa, 0x90a60040, 0x90a60040, 0x404003fb,
	0x40c003fc, 0x40000100
};

static const unsigned int *stac927x_brd_tbl[STAC_927X_MODELS] = {
	[STAC_D965_REF_NO_JD] = ref927x_pin_configs,
	[STAC_D965_REF]  = ref927x_pin_configs,
	[STAC_D965_3ST]  = d965_3st_pin_configs,
	[STAC_D965_5ST]  = d965_5st_pin_configs,
	[STAC_D965_5ST_NO_FP]  = d965_5st_no_fp_pin_configs,
	[STAC_DELL_3ST]  = dell_3st_pin_configs,
	[STAC_DELL_BIOS] = NULL,
	[STAC_927X_VOLKNOB] = NULL,
};

static const char * const stac927x_models[STAC_927X_MODELS] = {
	[STAC_927X_AUTO]	= "auto",
	[STAC_D965_REF_NO_JD]	= "ref-no-jd",
	[STAC_D965_REF]		= "ref",
	[STAC_D965_3ST]		= "3stack",
	[STAC_D965_5ST]		= "5stack",
	[STAC_D965_5ST_NO_FP]	= "5stack-no-fp",
	[STAC_DELL_3ST]		= "dell-3stack",
	[STAC_DELL_BIOS]	= "dell-bios",
	[STAC_927X_VOLKNOB]	= "volknob",
};

static const struct snd_pci_quirk stac927x_cfg_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668,
		      "DFI LanParty", STAC_D965_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DFI, 0x3101,
		      "DFI LanParty", STAC_D965_REF),
	 /* Intel 946 based systems */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x3d01, "Intel D946", STAC_D965_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0xa301, "Intel D946", STAC_D965_3ST),
	/* 965 based 3 stack systems */
	SND_PCI_QUIRK_MASK(PCI_VENDOR_ID_INTEL, 0xff00, 0x2100,
			   "Intel D965", STAC_D965_3ST),
	SND_PCI_QUIRK_MASK(PCI_VENDOR_ID_INTEL, 0xff00, 0x2000,
			   "Intel D965", STAC_D965_3ST),
	/* Dell 3 stack systems */
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x01dd, "Dell Dimension E520", STAC_DELL_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x01ed, "Dell     ", STAC_DELL_3ST),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x01f4, "Dell     ", STAC_DELL_3ST),
	/* Dell 3 stack systems with verb table in BIOS */
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x01f3, "Dell Inspiron 1420", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x01f7, "Dell XPS M1730", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x0227, "Dell Vostro 1400  ", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x022e, "Dell     ", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x022f, "Dell Inspiron 1525", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x0242, "Dell     ", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x0243, "Dell     ", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x02ff, "Dell     ", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x0209, "Dell XPS 1330", STAC_DELL_BIOS),
	/* 965 based 5 stack systems */
	SND_PCI_QUIRK_MASK(PCI_VENDOR_ID_INTEL, 0xff00, 0x2300,
			   "Intel D965", STAC_D965_5ST),
	SND_PCI_QUIRK_MASK(PCI_VENDOR_ID_INTEL, 0xff00, 0x2500,
			   "Intel D965", STAC_D965_5ST),
	/* volume-knob fixes */
	SND_PCI_QUIRK_VENDOR(0x10cf, "FSC", STAC_927X_VOLKNOB),
	{} /* terminator */
};

static const unsigned int ref9205_pin_configs[12] = {
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
    10280229 (Dell Vostro 1700)
*/
static const unsigned int dell_9205_m42_pin_configs[12] = {
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
static const unsigned int dell_9205_m43_pin_configs[12] = {
	0x0321101f, 0x03a11020, 0x90a70330, 0x90170310,
	0x400000fe, 0x400000ff, 0x400000fd, 0x40f000f9,
	0x400000fa, 0x400000fc, 0x0144131f, 0x40c003f8,
};

static const unsigned int dell_9205_m44_pin_configs[12] = {
	0x0421101f, 0x04a11020, 0x400003fa, 0x90170310,
	0x400003fb, 0x400003fc, 0x400003fd, 0x400003f9,
	0x90a60330, 0x400003ff, 0x01441340, 0x40c003fe,
};

static const unsigned int *stac9205_brd_tbl[STAC_9205_MODELS] = {
	[STAC_9205_REF] = ref9205_pin_configs,
	[STAC_9205_DELL_M42] = dell_9205_m42_pin_configs,
	[STAC_9205_DELL_M43] = dell_9205_m43_pin_configs,
	[STAC_9205_DELL_M44] = dell_9205_m44_pin_configs,
	[STAC_9205_EAPD] = NULL,
};

static const char * const stac9205_models[STAC_9205_MODELS] = {
	[STAC_9205_AUTO] = "auto",
	[STAC_9205_REF] = "ref",
	[STAC_9205_DELL_M42] = "dell-m42",
	[STAC_9205_DELL_M43] = "dell-m43",
	[STAC_9205_DELL_M44] = "dell-m44",
	[STAC_9205_EAPD] = "eapd",
};

static const struct snd_pci_quirk stac9205_cfg_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668,
		      "DFI LanParty", STAC_9205_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0xfb30,
		      "SigmaTel", STAC_9205_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DFI, 0x3101,
		      "DFI LanParty", STAC_9205_REF),
	/* Dell */
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
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0229,
		      "Dell Vostro 1700", STAC_9205_DELL_M42),
	/* Gateway */
	SND_PCI_QUIRK(0x107b, 0x0560, "Gateway T6834c", STAC_9205_EAPD),
	SND_PCI_QUIRK(0x107b, 0x0565, "Gateway T1616", STAC_9205_EAPD),
	{} /* terminator */
};

static void stac92xx_set_config_regs(struct hda_codec *codec,
				     const unsigned int *pincfgs)
{
	int i;
	struct sigmatel_spec *spec = codec->spec;

	if (!pincfgs)
		return;

	for (i = 0; i < spec->num_pins; i++)
		if (spec->pin_nids[i] && pincfgs[i])
			snd_hda_codec_set_pincfg(codec, spec->pin_nids[i],
						 pincfgs[i]);
}

/*
 * Analog playback callbacks
 */
static int stac92xx_playback_pcm_open(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	if (spec->stream_delay)
		msleep(spec->stream_delay);
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

static int stac92xx_dig_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_cleanup(codec, &spec->multiout);
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
	hda_nid_t nid = spec->adc_nids[substream->number];

	if (spec->powerdown_adcs) {
		msleep(40);
		snd_hda_codec_write(codec, nid, 0,
			AC_VERB_SET_POWER_STATE, AC_PWRST_D0);
	}
	snd_hda_codec_setup_stream(codec, nid, stream_tag, 0, format);
	return 0;
}

static int stac92xx_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t nid = spec->adc_nids[substream->number];

	snd_hda_codec_cleanup_stream(codec, nid);
	if (spec->powerdown_adcs)
		snd_hda_codec_write(codec, nid, 0,
			AC_VERB_SET_POWER_STATE, AC_PWRST_D3);
	return 0;
}

static const struct hda_pcm_stream stac92xx_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in stac92xx_build_pcms */
	.ops = {
		.open = stac92xx_dig_playback_pcm_open,
		.close = stac92xx_dig_playback_pcm_close,
		.prepare = stac92xx_dig_playback_pcm_prepare,
		.cleanup = stac92xx_dig_playback_pcm_cleanup
	},
};

static const struct hda_pcm_stream stac92xx_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in stac92xx_build_pcms */
};

static const struct hda_pcm_stream stac92xx_pcm_analog_playback = {
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

static const struct hda_pcm_stream stac92xx_pcm_analog_alt_playback = {
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

static const struct hda_pcm_stream stac92xx_pcm_analog_capture = {
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
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
		spec->multiout.dac_nids[0];
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
		info->pcm_type = spec->autocfg.dig_out_type[0];
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

static unsigned int stac92xx_get_default_vref(struct hda_codec *codec,
					hda_nid_t nid)
{
	unsigned int pincap = snd_hda_query_pin_caps(codec, nid);
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

	ucontrol->value.integer.value[0] = !!spec->hp_switch;
	return 0;
}

static void stac_issue_unsol_event(struct hda_codec *codec, hda_nid_t nid);

static int stac92xx_hp_switch_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	int nid = kcontrol->private_value;
 
	spec->hp_switch = ucontrol->value.integer.value[0] ? nid : 0;

	/* check to be sure that the ports are up to date with
	 * switch changes
	 */
	stac_issue_unsol_event(codec, nid);

	return 1;
}

static int stac92xx_dc_bias_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	int i;
	static const char * const texts[] = {
		"Mic In", "Line In", "Line Out"
	};

	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t nid = kcontrol->private_value;

	if (nid == spec->mic_switch || nid == spec->line_switch)
		i = 3;
	else
		i = 2;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->value.enumerated.items = i;
	uinfo->count = 1;
	if (uinfo->value.enumerated.item >= i)
		uinfo->value.enumerated.item = i-1;
	strcpy(uinfo->value.enumerated.name,
		texts[uinfo->value.enumerated.item]);

	return 0;
}

static int stac92xx_dc_bias_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value;
	unsigned int vref = stac92xx_vref_get(codec, nid);

	if (vref == stac92xx_get_default_vref(codec, nid))
		ucontrol->value.enumerated.item[0] = 0;
	else if (vref == AC_PINCTL_VREF_GRD)
		ucontrol->value.enumerated.item[0] = 1;
	else if (vref == AC_PINCTL_VREF_HIZ)
		ucontrol->value.enumerated.item[0] = 2;

	return 0;
}

static int stac92xx_dc_bias_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int new_vref = 0;
	int error;
	hda_nid_t nid = kcontrol->private_value;

	if (ucontrol->value.enumerated.item[0] == 0)
		new_vref = stac92xx_get_default_vref(codec, nid);
	else if (ucontrol->value.enumerated.item[0] == 1)
		new_vref = AC_PINCTL_VREF_GRD;
	else if (ucontrol->value.enumerated.item[0] == 2)
		new_vref = AC_PINCTL_VREF_HIZ;
	else
		return 0;

	if (new_vref != stac92xx_vref_get(codec, nid)) {
		error = stac92xx_vref_set(codec, nid, new_vref);
		return error;
	}

	return 0;
}

static int stac92xx_io_switch_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	char *texts[2];
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;

	if (kcontrol->private_value == spec->line_switch)
		texts[0] = "Line In";
	else
		texts[0] = "Mic In";
	texts[1] = "Line Out";
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->value.enumerated.items = 2;
	uinfo->count = 1;

	if (uinfo->value.enumerated.item >= 2)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name,
		texts[uinfo->value.enumerated.item]);

	return 0;
}

static int stac92xx_io_switch_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t nid = kcontrol->private_value;
	int io_idx = (nid == spec->mic_switch) ? 1 : 0;

	ucontrol->value.enumerated.item[0] = spec->io_switch[io_idx];
	return 0;
}

static int stac92xx_io_switch_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
        struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t nid = kcontrol->private_value;
	int io_idx = (nid == spec->mic_switch) ? 1 : 0;
	unsigned short val = !!ucontrol->value.enumerated.item[0];

	spec->io_switch[io_idx] = val;

	if (val)
		stac92xx_auto_set_pinctl(codec, nid, AC_PINCTL_OUT_EN);
	else {
		unsigned int pinctl = AC_PINCTL_IN_EN;
		if (io_idx) /* set VREF for mic */
			pinctl |= stac92xx_get_default_vref(codec, nid);
		stac92xx_auto_set_pinctl(codec, nid, pinctl);
	}

	/* check the auto-mute again: we need to mute/unmute the speaker
	 * appropriately according to the pin direction
	 */
	if (spec->hp_detect)
		stac_issue_unsol_event(codec, nid);

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
	STAC_CTL_WIDGET_MUTE_BEEP,
	STAC_CTL_WIDGET_MONO_MUX,
	STAC_CTL_WIDGET_HP_SWITCH,
	STAC_CTL_WIDGET_IO_SWITCH,
	STAC_CTL_WIDGET_CLFE_SWITCH,
	STAC_CTL_WIDGET_DC_BIAS
};

static const struct snd_kcontrol_new stac92xx_control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	HDA_CODEC_MUTE(NULL, 0, 0, 0),
	HDA_CODEC_MUTE_BEEP(NULL, 0, 0, 0),
	STAC_MONO_MUX,
	STAC_CODEC_HP_SWITCH(NULL),
	STAC_CODEC_IO_SWITCH(NULL, 0),
	STAC_CODEC_CLFE_SWITCH(NULL, 0),
	DC_BIAS(NULL, 0, 0),
};

/* add dynamic controls */
static struct snd_kcontrol_new *
stac_control_new(struct sigmatel_spec *spec,
		 const struct snd_kcontrol_new *ktemp,
		 const char *name,
		 unsigned int subdev)
{
	struct snd_kcontrol_new *knew;

	snd_array_init(&spec->kctls, sizeof(*knew), 32);
	knew = snd_array_new(&spec->kctls);
	if (!knew)
		return NULL;
	*knew = *ktemp;
	knew->name = kstrdup(name, GFP_KERNEL);
	if (!knew->name) {
		/* roolback */
		memset(knew, 0, sizeof(*knew));
		spec->kctls.alloced--;
		return NULL;
	}
	knew->subdevice = subdev;
	return knew;
}

static int stac92xx_add_control_temp(struct sigmatel_spec *spec,
				     const struct snd_kcontrol_new *ktemp,
				     int idx, const char *name,
				     unsigned long val)
{
	struct snd_kcontrol_new *knew = stac_control_new(spec, ktemp, name,
							 HDA_SUBDEV_AMP_FLAG);
	if (!knew)
		return -ENOMEM;
	knew->index = idx;
	knew->private_value = val;
	return 0;
}

static inline int stac92xx_add_control_idx(struct sigmatel_spec *spec,
					   int type, int idx, const char *name,
					   unsigned long val)
{
	return stac92xx_add_control_temp(spec,
					 &stac92xx_control_templates[type],
					 idx, name, val);
}


/* add dynamic controls */
static inline int stac92xx_add_control(struct sigmatel_spec *spec, int type,
				       const char *name, unsigned long val)
{
	return stac92xx_add_control_idx(spec, type, 0, name, val);
}

static const struct snd_kcontrol_new stac_input_src_temp = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Input Source",
	.info = stac92xx_mux_enum_info,
	.get = stac92xx_mux_enum_get,
	.put = stac92xx_mux_enum_put,
};

static inline int stac92xx_add_jack_mode_control(struct hda_codec *codec,
						hda_nid_t nid, int idx)
{
	int def_conf = snd_hda_codec_get_pincfg(codec, nid);
	int control = 0;
	struct sigmatel_spec *spec = codec->spec;
	char name[22];

	if (snd_hda_get_input_pin_attr(def_conf) != INPUT_PIN_ATTR_INT) {
		if (stac92xx_get_default_vref(codec, nid) == AC_PINCTL_VREF_GRD
			&& nid == spec->line_switch)
			control = STAC_CTL_WIDGET_IO_SWITCH;
		else if (snd_hda_query_pin_caps(codec, nid)
			& (AC_PINCAP_VREF_GRD << AC_PINCAP_VREF_SHIFT))
			control = STAC_CTL_WIDGET_DC_BIAS;
		else if (nid == spec->mic_switch)
			control = STAC_CTL_WIDGET_IO_SWITCH;
	}

	if (control) {
		snd_hda_get_pin_label(codec, nid, &spec->autocfg,
				      name, sizeof(name), NULL);
		return stac92xx_add_control(codec->spec, control,
					strcat(name, " Jack Mode"), nid);
	}

	return 0;
}

static int stac92xx_add_input_source(struct sigmatel_spec *spec)
{
	struct snd_kcontrol_new *knew;
	struct hda_input_mux *imux = &spec->private_imux;

	if (spec->auto_mic)
		return 0; /* no need for input source */
	if (!spec->num_adcs || imux->num_items <= 1)
		return 0; /* no need for input source control */
	knew = stac_control_new(spec, &stac_input_src_temp,
				stac_input_src_temp.name, 0);
	if (!knew)
		return -ENOMEM;
	knew->count = spec->num_adcs;
	return 0;
}

/* check whether the line-input can be used as line-out */
static hda_nid_t check_line_out_switch(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	hda_nid_t nid;
	unsigned int pincap;
	int i;

	if (cfg->line_out_type != AUTO_PIN_LINE_OUT)
		return 0;
	for (i = 0; i < cfg->num_inputs; i++) {
		if (cfg->inputs[i].type == AUTO_PIN_LINE_IN) {
			nid = cfg->inputs[i].pin;
			pincap = snd_hda_query_pin_caps(codec, nid);
			if (pincap & AC_PINCAP_OUT)
				return nid;
		}
	}
	return 0;
}

static hda_nid_t get_unassigned_dac(struct hda_codec *codec, hda_nid_t nid);

/* check whether the mic-input can be used as line-out */
static hda_nid_t check_mic_out_switch(struct hda_codec *codec, hda_nid_t *dac)
{
	struct sigmatel_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int def_conf, pincap;
	int i;

	*dac = 0;
	if (cfg->line_out_type != AUTO_PIN_LINE_OUT)
		return 0;
	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		if (cfg->inputs[i].type != AUTO_PIN_MIC)
			continue;
		def_conf = snd_hda_codec_get_pincfg(codec, nid);
		/* some laptops have an internal analog microphone
		 * which can't be used as a output */
		if (snd_hda_get_input_pin_attr(def_conf) != INPUT_PIN_ATTR_INT) {
			pincap = snd_hda_query_pin_caps(codec, nid);
			if (pincap & AC_PINCAP_OUT) {
				*dac = get_unassigned_dac(codec, nid);
				if (*dac)
					return nid;
			}
		}
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

static int check_all_dac_nids(struct sigmatel_spec *spec, hda_nid_t nid)
{
	int i;
	if (is_in_dac_nids(spec, nid))
		return 1;
	for (i = 0; i < spec->autocfg.hp_outs; i++)
		if (spec->hp_dacs[i] == nid)
			return 1;
	for (i = 0; i < spec->autocfg.speaker_outs; i++)
		if (spec->speaker_dacs[i] == nid)
			return 1;
	return 0;
}

static hda_nid_t get_unassigned_dac(struct hda_codec *codec, hda_nid_t nid)
{
	struct sigmatel_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int j, conn_len;
	hda_nid_t conn[HDA_MAX_CONNECTIONS], fallback_dac;
	unsigned int wcaps, wtype;

	conn_len = snd_hda_get_connections(codec, nid, conn,
					   HDA_MAX_CONNECTIONS);
	/* 92HD88: trace back up the link of nids to find the DAC */
	while (conn_len == 1 && (get_wcaps_type(get_wcaps(codec, conn[0]))
					!= AC_WID_AUD_OUT)) {
		nid = conn[0];
		conn_len = snd_hda_get_connections(codec, nid, conn,
			HDA_MAX_CONNECTIONS);
	}
	for (j = 0; j < conn_len; j++) {
		wcaps = get_wcaps(codec, conn[j]);
		wtype = get_wcaps_type(wcaps);
		/* we check only analog outputs */
		if (wtype != AC_WID_AUD_OUT || (wcaps & AC_WCAP_DIGITAL))
			continue;
		/* if this route has a free DAC, assign it */
		if (!check_all_dac_nids(spec, conn[j])) {
			if (conn_len > 1) {
				/* select this DAC in the pin's input mux */
				snd_hda_codec_write_cache(codec, nid, 0,
						  AC_VERB_SET_CONNECT_SEL, j);
			}
			return conn[j];
		}
	}

	/* if all DACs are already assigned, connect to the primary DAC,
	   unless we're assigning a secondary headphone */
	fallback_dac = spec->multiout.dac_nids[0];
	if (spec->multiout.hp_nid) {
		for (j = 0; j < cfg->hp_outs; j++)
			if (cfg->hp_pins[j] == nid) {
				fallback_dac = spec->multiout.hp_nid;
				break;
			}
	}

	if (conn_len > 1) {
		for (j = 0; j < conn_len; j++) {
			if (conn[j] == fallback_dac) {
				snd_hda_codec_write_cache(codec, nid, 0,
						  AC_VERB_SET_CONNECT_SEL, j);
				break;
			}
		}
	}
	return 0;
}

static int add_spec_dacs(struct sigmatel_spec *spec, hda_nid_t nid);
static int add_spec_extra_dacs(struct sigmatel_spec *spec, hda_nid_t nid);

/*
 * Fill in the dac_nids table from the parsed pin configuration
 * This function only works when every pin in line_out_pins[]
 * contains atleast one DAC in its connection list. Some 92xx
 * codecs are not connected directly to a DAC, such as the 9200
 * and 9202/925x. For those, dac_nids[] must be hard-coded.
 */
static int stac92xx_auto_fill_dac_nids(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;
	hda_nid_t nid, dac;
	
	for (i = 0; i < cfg->line_outs; i++) {
		nid = cfg->line_out_pins[i];
		dac = get_unassigned_dac(codec, nid);
		if (!dac) {
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
		add_spec_dacs(spec, dac);
	}

	for (i = 0; i < cfg->hp_outs; i++) {
		nid = cfg->hp_pins[i];
		dac = get_unassigned_dac(codec, nid);
		if (dac) {
			if (!spec->multiout.hp_nid)
				spec->multiout.hp_nid = dac;
			else
				add_spec_extra_dacs(spec, dac);
		}
		spec->hp_dacs[i] = dac;
	}

	for (i = 0; i < cfg->speaker_outs; i++) {
		nid = cfg->speaker_pins[i];
		dac = get_unassigned_dac(codec, nid);
		if (dac)
			add_spec_extra_dacs(spec, dac);
		spec->speaker_dacs[i] = dac;
	}

	/* add line-in as output */
	nid = check_line_out_switch(codec);
	if (nid) {
		dac = get_unassigned_dac(codec, nid);
		if (dac) {
			snd_printdd("STAC: Add line-in 0x%x as output %d\n",
				    nid, cfg->line_outs);
			cfg->line_out_pins[cfg->line_outs] = nid;
			cfg->line_outs++;
			spec->line_switch = nid;
			add_spec_dacs(spec, dac);
		}
	}
	/* add mic as output */
	nid = check_mic_out_switch(codec, &dac);
	if (nid && dac) {
		snd_printdd("STAC: Add mic-in 0x%x as output %d\n",
			    nid, cfg->line_outs);
		cfg->line_out_pins[cfg->line_outs] = nid;
		cfg->line_outs++;
		spec->mic_switch = nid;
		add_spec_dacs(spec, dac);
	}

	snd_printd("stac92xx: dac_nids=%d (0x%x/0x%x/0x%x/0x%x/0x%x)\n",
		   spec->multiout.num_dacs,
		   spec->multiout.dac_nids[0],
		   spec->multiout.dac_nids[1],
		   spec->multiout.dac_nids[2],
		   spec->multiout.dac_nids[3],
		   spec->multiout.dac_nids[4]);

	return 0;
}

/* create volume control/switch for the given prefx type */
static int create_controls_idx(struct hda_codec *codec, const char *pfx,
			       int idx, hda_nid_t nid, int chs)
{
	struct sigmatel_spec *spec = codec->spec;
	char name[32];
	int err;

	if (!spec->check_volume_offset) {
		unsigned int caps, step, nums, db_scale;
		caps = query_amp_caps(codec, nid, HDA_OUTPUT);
		step = (caps & AC_AMPCAP_STEP_SIZE) >>
			AC_AMPCAP_STEP_SIZE_SHIFT;
		step = (step + 1) * 25; /* in .01dB unit */
		nums = (caps & AC_AMPCAP_NUM_STEPS) >>
			AC_AMPCAP_NUM_STEPS_SHIFT;
		db_scale = nums * step;
		/* if dB scale is over -64dB, and finer enough,
		 * let's reduce it to half
		 */
		if (db_scale > 6400 && nums >= 0x1f)
			spec->volume_offset = nums / 2;
		spec->check_volume_offset = 1;
	}

	sprintf(name, "%s Playback Volume", pfx);
	err = stac92xx_add_control_idx(spec, STAC_CTL_WIDGET_VOL, idx, name,
		HDA_COMPOSE_AMP_VAL_OFS(nid, chs, 0, HDA_OUTPUT,
					spec->volume_offset));
	if (err < 0)
		return err;
	sprintf(name, "%s Playback Switch", pfx);
	err = stac92xx_add_control_idx(spec, STAC_CTL_WIDGET_MUTE, idx, name,
				   HDA_COMPOSE_AMP_VAL(nid, chs, 0, HDA_OUTPUT));
	if (err < 0)
		return err;
	return 0;
}

#define create_controls(codec, pfx, nid, chs) \
	create_controls_idx(codec, pfx, 0, nid, chs)

static int add_spec_dacs(struct sigmatel_spec *spec, hda_nid_t nid)
{
	if (spec->multiout.num_dacs > 4) {
		printk(KERN_WARNING "stac92xx: No space for DAC 0x%x\n", nid);
		return 1;
	} else {
		snd_BUG_ON(spec->multiout.dac_nids != spec->dac_nids);
		spec->dac_nids[spec->multiout.num_dacs] = nid;
		spec->multiout.num_dacs++;
	}
	return 0;
}

static int add_spec_extra_dacs(struct sigmatel_spec *spec, hda_nid_t nid)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(spec->multiout.extra_out_nid); i++) {
		if (!spec->multiout.extra_out_nid[i]) {
			spec->multiout.extra_out_nid[i] = nid;
			return 0;
		}
	}
	printk(KERN_WARNING "stac92xx: No space for extra DAC 0x%x\n", nid);
	return 1;
}

/* Create output controls
 * The mixer elements are named depending on the given type (AUTO_PIN_XXX_OUT)
 */
static int create_multi_out_ctls(struct hda_codec *codec, int num_outs,
				 const hda_nid_t *pins,
				 const hda_nid_t *dac_nids,
				 int type)
{
	struct sigmatel_spec *spec = codec->spec;
	static const char * const chname[4] = {
		"Front", "Surround", NULL /*CLFE*/, "Side"
	};
	hda_nid_t nid;
	int i, err;
	unsigned int wid_caps;

	for (i = 0; i < num_outs && i < ARRAY_SIZE(chname); i++) {
		if (type == AUTO_PIN_HP_OUT && !spec->hp_detect) {
			if (is_jack_detectable(codec, pins[i]))
				spec->hp_detect = 1;
		}
		nid = dac_nids[i];
		if (!nid)
			continue;
		if (type != AUTO_PIN_HP_OUT && i == 2) {
			/* Center/LFE */
			err = create_controls(codec, "Center", nid, 1);
			if (err < 0)
				return err;
			err = create_controls(codec, "LFE", nid, 2);
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
			const char *name;
			int idx;
			switch (type) {
			case AUTO_PIN_HP_OUT:
				name = "Headphone";
				idx = i;
				break;
			case AUTO_PIN_SPEAKER_OUT:
				name = "Speaker";
				idx = i;
				break;
			default:
				name = chname[i];
				idx = 0;
				break;
			}
			err = create_controls_idx(codec, name, idx, nid, 3);
			if (err < 0)
				return err;
		}
	}
	return 0;
}

static int stac92xx_add_capvol_ctls(struct hda_codec *codec, unsigned long vol,
				    unsigned long sw, int idx)
{
	int err;
	err = stac92xx_add_control_idx(codec->spec, STAC_CTL_WIDGET_VOL, idx,
				       "Capture Volume", vol);
	if (err < 0)
		return err;
	err = stac92xx_add_control_idx(codec->spec, STAC_CTL_WIDGET_MUTE, idx,
				       "Capture Switch", sw);
	if (err < 0)
		return err;
	return 0;
}

/* add playback controls from the parsed DAC table */
static int stac92xx_auto_create_multi_out_ctls(struct hda_codec *codec,
					       const struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t nid;
	int err;
	int idx;

	err = create_multi_out_ctls(codec, cfg->line_outs, cfg->line_out_pins,
				    spec->multiout.dac_nids,
				    cfg->line_out_type);
	if (err < 0)
		return err;

	if (cfg->hp_outs > 1 && cfg->line_out_type == AUTO_PIN_LINE_OUT) {
		err = stac92xx_add_control(spec,
			STAC_CTL_WIDGET_HP_SWITCH,
			"Headphone as Line Out Switch",
			cfg->hp_pins[cfg->hp_outs - 1]);
		if (err < 0)
			return err;
	}

	for (idx = 0; idx < cfg->num_inputs; idx++) {
		if (cfg->inputs[idx].type > AUTO_PIN_LINE_IN)
			break;
		nid = cfg->inputs[idx].pin;
		err = stac92xx_add_jack_mode_control(codec, nid, idx);
		if (err < 0)
			return err;
	}

	return 0;
}

/* add playback controls for Speaker and HP outputs */
static int stac92xx_auto_create_hp_ctls(struct hda_codec *codec,
					struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	int err;

	err = create_multi_out_ctls(codec, cfg->hp_outs, cfg->hp_pins,
				    spec->hp_dacs, AUTO_PIN_HP_OUT);
	if (err < 0)
		return err;

	err = create_multi_out_ctls(codec, cfg->speaker_outs, cfg->speaker_pins,
				    spec->speaker_dacs, AUTO_PIN_SPEAKER_OUT);
	if (err < 0)
		return err;

	return 0;
}

/* labels for mono mux outputs */
static const char * const stac92xx_mono_labels[4] = {
	"DAC0", "DAC1", "Mixer", "DAC2"
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
	if (num_cons <= 0 || num_cons > ARRAY_SIZE(stac92xx_mono_labels))
		return -EINVAL;

	for (i = 0; i < num_cons; i++)
		snd_hda_add_imux_item(mono_mux, stac92xx_mono_labels[i], i,
				      NULL);

	return stac92xx_add_control(spec, STAC_CTL_WIDGET_MONO_MUX,
				"Mono Mux", spec->mono_nid);
}

/* create PC beep volume controls */
static int stac92xx_auto_create_beep_ctls(struct hda_codec *codec,
						hda_nid_t nid)
{
	struct sigmatel_spec *spec = codec->spec;
	u32 caps = query_amp_caps(codec, nid, HDA_OUTPUT);
	int err, type = STAC_CTL_WIDGET_MUTE_BEEP;

	if (spec->anabeep_nid == nid)
		type = STAC_CTL_WIDGET_MUTE;

	/* check for mute support for the the amp */
	if ((caps & AC_AMPCAP_MUTE) >> AC_AMPCAP_MUTE_SHIFT) {
		err = stac92xx_add_control(spec, type,
			"Beep Playback Switch",
			HDA_COMPOSE_AMP_VAL(nid, 1, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
	}

	/* check to see if there is volume support for the amp */
	if ((caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT) {
		err = stac92xx_add_control(spec, STAC_CTL_WIDGET_VOL,
			"Beep Playback Volume",
			HDA_COMPOSE_AMP_VAL(nid, 1, 0, HDA_OUTPUT));
			if (err < 0)
				return err;
	}
	return 0;
}

#ifdef CONFIG_SND_HDA_INPUT_BEEP
#define stac92xx_dig_beep_switch_info snd_ctl_boolean_mono_info

static int stac92xx_dig_beep_switch_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = codec->beep->enabled;
	return 0;
}

static int stac92xx_dig_beep_switch_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	return snd_hda_enable_beep_device(codec, ucontrol->value.integer.value[0]);
}

static const struct snd_kcontrol_new stac92xx_dig_beep_ctrl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = stac92xx_dig_beep_switch_info,
	.get = stac92xx_dig_beep_switch_get,
	.put = stac92xx_dig_beep_switch_put,
};

static int stac92xx_beep_switch_ctl(struct hda_codec *codec)
{
	return stac92xx_add_control_temp(codec->spec, &stac92xx_dig_beep_ctrl,
					 0, "Beep Playback Switch", 0);
}
#endif

static int stac92xx_auto_create_mux_input_ctls(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int i, j, err = 0;

	for (i = 0; i < spec->num_muxes; i++) {
		hda_nid_t nid;
		unsigned int wcaps;
		unsigned long val;

		nid = spec->mux_nids[i];
		wcaps = get_wcaps(codec, nid);
		if (!(wcaps & AC_WCAP_OUT_AMP))
			continue;

		/* check whether already the same control was created as
		 * normal Capture Volume.
		 */
		val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
		for (j = 0; j < spec->num_caps; j++) {
			if (spec->capvols[j] == val)
				break;
		}
		if (j < spec->num_caps)
			continue;

		err = stac92xx_add_control_idx(spec, STAC_CTL_WIDGET_VOL, i,
					       "Mux Capture Volume", val);
		if (err < 0)
			return err;
	}
	return 0;
};

static const char * const stac92xx_spdif_labels[3] = {
	"Digital Playback", "Analog Mux 1", "Analog Mux 2",
};

static int stac92xx_auto_create_spdif_mux_ctls(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	struct hda_input_mux *spdif_mux = &spec->private_smux;
	const char * const *labels = spec->spdif_labels;
	int i, num_cons;
	hda_nid_t con_lst[HDA_MAX_NUM_INPUTS];

	num_cons = snd_hda_get_connections(codec,
				spec->smux_nids[0],
				con_lst,
				HDA_MAX_NUM_INPUTS);
	if (num_cons <= 0)
		return -EINVAL;

	if (!labels)
		labels = stac92xx_spdif_labels;

	for (i = 0; i < num_cons; i++)
		snd_hda_add_imux_item(spdif_mux, labels[i], i, NULL);

	return 0;
}

/* labels for dmic mux inputs */
static const char * const stac92xx_dmic_labels[5] = {
	"Analog Inputs", "Digital Mic 1", "Digital Mic 2",
	"Digital Mic 3", "Digital Mic 4"
};

static hda_nid_t get_connected_node(struct hda_codec *codec, hda_nid_t mux,
				    int idx)
{
	hda_nid_t conn[HDA_MAX_NUM_INPUTS];
	int nums;
	nums = snd_hda_get_connections(codec, mux, conn, ARRAY_SIZE(conn));
	if (idx >= 0 && idx < nums)
		return conn[idx];
	return 0;
}

/* look for NID recursively */
#define get_connection_index(codec, mux, nid) \
	snd_hda_get_conn_index(codec, mux, nid, 1)

/* create a volume assigned to the given pin (only if supported) */
/* return 1 if the volume control is created */
static int create_elem_capture_vol(struct hda_codec *codec, hda_nid_t nid,
				   const char *label, int idx, int direction)
{
	unsigned int caps, nums;
	char name[32];
	int err;

	if (direction == HDA_OUTPUT)
		caps = AC_WCAP_OUT_AMP;
	else
		caps = AC_WCAP_IN_AMP;
	if (!(get_wcaps(codec, nid) & caps))
		return 0;
	caps = query_amp_caps(codec, nid, direction);
	nums = (caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT;
	if (!nums)
		return 0;
	snprintf(name, sizeof(name), "%s Capture Volume", label);
	err = stac92xx_add_control_idx(codec->spec, STAC_CTL_WIDGET_VOL, idx, name,
				       HDA_COMPOSE_AMP_VAL(nid, 3, 0, direction));
	if (err < 0)
		return err;
	return 1;
}

/* create playback/capture controls for input pins on dmic capable codecs */
static int stac92xx_auto_create_dmic_input_ctls(struct hda_codec *codec,
						const struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->private_imux;
	struct hda_input_mux *dimux = &spec->private_dimux;
	int err, i;
	unsigned int def_conf;

	snd_hda_add_imux_item(dimux, stac92xx_dmic_labels[0], 0, NULL);

	for (i = 0; i < spec->num_dmics; i++) {
		hda_nid_t nid;
		int index, type_idx;
		char label[32];

		nid = spec->dmic_nids[i];
		if (get_wcaps_type(get_wcaps(codec, nid)) != AC_WID_PIN)
			continue;
		def_conf = snd_hda_codec_get_pincfg(codec, nid);
		if (get_defcfg_connect(def_conf) == AC_JACK_PORT_NONE)
			continue;

		index = get_connection_index(codec, spec->dmux_nids[0], nid);
		if (index < 0)
			continue;

		snd_hda_get_pin_label(codec, nid, &spec->autocfg,
				      label, sizeof(label), NULL);
		snd_hda_add_imux_item(dimux, label, index, &type_idx);
		if (snd_hda_get_bool_hint(codec, "separate_dmux") != 1)
			snd_hda_add_imux_item(imux, label, index, &type_idx);

		err = create_elem_capture_vol(codec, nid, label, type_idx,
					      HDA_INPUT);
		if (err < 0)
			return err;
		if (!err) {
			err = create_elem_capture_vol(codec, nid, label,
						      type_idx, HDA_OUTPUT);
			if (err < 0)
				return err;
			if (!err) {
				nid = get_connected_node(codec,
						spec->dmux_nids[0], index);
				if (nid)
					err = create_elem_capture_vol(codec,
							nid, label,
							type_idx, HDA_INPUT);
				if (err < 0)
					return err;
			}
		}
	}

	return 0;
}

static int check_mic_pin(struct hda_codec *codec, hda_nid_t nid,
			 hda_nid_t *fixed, hda_nid_t *ext, hda_nid_t *dock)
{
	unsigned int cfg;
	unsigned int type;

	if (!nid)
		return 0;
	cfg = snd_hda_codec_get_pincfg(codec, nid);
	type = get_defcfg_device(cfg);
	switch (snd_hda_get_input_pin_attr(cfg)) {
	case INPUT_PIN_ATTR_INT:
		if (*fixed)
			return 1; /* already occupied */
		if (type != AC_JACK_MIC_IN)
			return 1; /* invalid type */
		*fixed = nid;
		break;
	case INPUT_PIN_ATTR_UNUSED:
		break;
	case INPUT_PIN_ATTR_DOCK:
		if (*dock)
			return 1; /* already occupied */
		if (type != AC_JACK_MIC_IN && type != AC_JACK_LINE_IN)
			return 1; /* invalid type */
		*dock = nid;
		break;
	default:
		if (*ext)
			return 1; /* already occupied */
		if (type != AC_JACK_MIC_IN)
			return 1; /* invalid type */
		*ext = nid;
		break;
	}
	return 0;
}

static int set_mic_route(struct hda_codec *codec,
			 struct sigmatel_mic_route *mic,
			 hda_nid_t pin)
{
	struct sigmatel_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	mic->pin = pin;
	if (pin == 0)
		return 0;
	for (i = 0; i < cfg->num_inputs; i++) {
		if (pin == cfg->inputs[i].pin)
			break;
	}
	if (i < cfg->num_inputs && cfg->inputs[i].type == AUTO_PIN_MIC) {
		/* analog pin */
		i = get_connection_index(codec, spec->mux_nids[0], pin);
		if (i < 0)
			return -1;
		mic->mux_idx = i;
		mic->dmux_idx = -1;
		if (spec->dmux_nids)
			mic->dmux_idx = get_connection_index(codec,
							     spec->dmux_nids[0],
							     spec->mux_nids[0]);
	}  else if (spec->dmux_nids) {
		/* digital pin */
		i = get_connection_index(codec, spec->dmux_nids[0], pin);
		if (i < 0)
			return -1;
		mic->dmux_idx = i;
		mic->mux_idx = -1;
		if (spec->mux_nids)
			mic->mux_idx = get_connection_index(codec,
							    spec->mux_nids[0],
							    spec->dmux_nids[0]);
	}
	return 0;
}

/* return non-zero if the device is for automatic mic switch */
static int stac_check_auto_mic(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	hda_nid_t fixed, ext, dock;
	int i;

	fixed = ext = dock = 0;
	for (i = 0; i < cfg->num_inputs; i++)
		if (check_mic_pin(codec, cfg->inputs[i].pin,
		    &fixed, &ext, &dock))
			return 0;
	for (i = 0; i < spec->num_dmics; i++)
		if (check_mic_pin(codec, spec->dmic_nids[i],
		    &fixed, &ext, &dock))
			return 0;
	if (!fixed || (!ext && !dock))
		return 0; /* no input to switch */
	if (!is_jack_detectable(codec, ext))
		return 0; /* no unsol support */
	if (set_mic_route(codec, &spec->ext_mic, ext) ||
	    set_mic_route(codec, &spec->int_mic, fixed) ||
	    set_mic_route(codec, &spec->dock_mic, dock))
		return 0; /* something is wrong */
	return 1;
}

/* create playback/capture controls for input pins */
static int stac92xx_auto_create_analog_input_ctls(struct hda_codec *codec, const struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->private_imux;
	int i, j;
	const char *label;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		int index, err, type_idx;

		index = -1;
		for (j = 0; j < spec->num_muxes; j++) {
			index = get_connection_index(codec, spec->mux_nids[j],
						     nid);
			if (index >= 0)
				break;
		}
		if (index < 0)
			continue;

		label = hda_get_autocfg_input_label(codec, cfg, i);
		snd_hda_add_imux_item(imux, label, index, &type_idx);

		err = create_elem_capture_vol(codec, nid,
					      label, type_idx,
					      HDA_INPUT);
		if (err < 0)
			return err;
	}
	spec->num_analog_muxes = imux->num_items;

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

static int is_dual_headphones(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int i, valid_hps;

	if (spec->autocfg.line_out_type != AUTO_PIN_SPEAKER_OUT ||
	    spec->autocfg.hp_outs <= 1)
		return 0;
	valid_hps = 0;
	for (i = 0; i < spec->autocfg.hp_outs; i++) {
		hda_nid_t nid = spec->autocfg.hp_pins[i];
		unsigned int cfg = snd_hda_codec_get_pincfg(codec, nid);
		if (get_defcfg_location(cfg) & AC_JACK_LOC_SEPARATE)
			continue;
		valid_hps++;
	}
	return (valid_hps > 1);
}


static int stac92xx_parse_auto_config(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t dig_out = 0, dig_in = 0;
	int hp_swap = 0;
	int i, err;

	if ((err = snd_hda_parse_pin_def_config(codec,
						&spec->autocfg,
						spec->dmic_nids)) < 0)
		return err;
	if (! spec->autocfg.line_outs)
		return 0; /* can't find valid pin config */

	/* If we have no real line-out pin and multiple hp-outs, HPs should
	 * be set up as multi-channel outputs.
	 */
	if (is_dual_headphones(codec)) {
		/* Copy hp_outs to line_outs, backup line_outs in
		 * speaker_outs so that the following routines can handle
		 * HP pins as primary outputs.
		 */
		snd_printdd("stac92xx: Enabling multi-HPs workaround\n");
		memcpy(spec->autocfg.speaker_pins, spec->autocfg.line_out_pins,
		       sizeof(spec->autocfg.line_out_pins));
		spec->autocfg.speaker_outs = spec->autocfg.line_outs;
		memcpy(spec->autocfg.line_out_pins, spec->autocfg.hp_pins,
		       sizeof(spec->autocfg.hp_pins));
		spec->autocfg.line_outs = spec->autocfg.hp_outs;
		spec->autocfg.line_out_type = AUTO_PIN_HP_OUT;
		spec->autocfg.hp_outs = 0;
		hp_swap = 1;
	}
	if (spec->autocfg.mono_out_pin) {
		int dir = get_wcaps(codec, spec->autocfg.mono_out_pin) &
			(AC_WCAP_OUT_AMP | AC_WCAP_IN_AMP);
		u32 caps = query_amp_caps(codec,
				spec->autocfg.mono_out_pin, dir);
		hda_nid_t conn_list[1];

		/* get the mixer node and then the mono mux if it exists */
		if (snd_hda_get_connections(codec,
				spec->autocfg.mono_out_pin, conn_list, 1) &&
				snd_hda_get_connections(codec, conn_list[0],
				conn_list, 1) > 0) {

				int wcaps = get_wcaps(codec, conn_list[0]);
				int wid_type = get_wcaps_type(wcaps);
				/* LR swap check, some stac925x have a mux that
 				 * changes the DACs output path instead of the
 				 * mono-mux path.
 				 */
				if (wid_type == AC_WID_AUD_SEL &&
						!(wcaps & AC_WCAP_LR_SWAP))
					spec->mono_nid = conn_list[0];
		}
		if (dir) {
			hda_nid_t nid = spec->autocfg.mono_out_pin;

			/* most mono outs have a least a mute/unmute switch */
			dir = (dir & AC_WCAP_OUT_AMP) ? HDA_OUTPUT : HDA_INPUT;
			err = stac92xx_add_control(spec, STAC_CTL_WIDGET_MUTE,
				"Mono Playback Switch",
				HDA_COMPOSE_AMP_VAL(nid, 1, 0, dir));
			if (err < 0)
				return err;
			/* check for volume support for the amp */
			if ((caps & AC_AMPCAP_NUM_STEPS)
					>> AC_AMPCAP_NUM_STEPS_SHIFT) {
				err = stac92xx_add_control(spec,
					STAC_CTL_WIDGET_VOL,
					"Mono Playback Volume",
				HDA_COMPOSE_AMP_VAL(nid, 1, 0, dir));
				if (err < 0)
					return err;
			}
		}

		stac92xx_auto_set_pinctl(codec, spec->autocfg.mono_out_pin,
					 AC_PINCTL_OUT_EN);
	}

	if (!spec->multiout.num_dacs) {
		err = stac92xx_auto_fill_dac_nids(codec);
		if (err < 0)
			return err;
		err = stac92xx_auto_create_multi_out_ctls(codec,
							  &spec->autocfg);
		if (err < 0)
			return err;
	}

	/* setup analog beep controls */
	if (spec->anabeep_nid > 0) {
		err = stac92xx_auto_create_beep_ctls(codec,
			spec->anabeep_nid);
		if (err < 0)
			return err;
	}

	/* setup digital beep controls and input device */
#ifdef CONFIG_SND_HDA_INPUT_BEEP
	if (spec->digbeep_nid > 0) {
		hda_nid_t nid = spec->digbeep_nid;
		unsigned int caps;

		err = stac92xx_auto_create_beep_ctls(codec, nid);
		if (err < 0)
			return err;
		err = snd_hda_attach_beep_device(codec, nid);
		if (err < 0)
			return err;
		if (codec->beep) {
			/* IDT/STAC codecs have linear beep tone parameter */
			codec->beep->linear_tone = spec->linear_tone_beep;
			/* if no beep switch is available, make its own one */
			caps = query_amp_caps(codec, nid, HDA_OUTPUT);
			if (!(caps & AC_AMPCAP_MUTE)) {
				err = stac92xx_beep_switch_ctl(codec);
				if (err < 0)
					return err;
			}
		}
	}
#endif

	err = stac92xx_auto_create_hp_ctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	/* All output parsing done, now restore the swapped hp pins */
	if (hp_swap) {
		memcpy(spec->autocfg.hp_pins, spec->autocfg.line_out_pins,
		       sizeof(spec->autocfg.hp_pins));
		spec->autocfg.hp_outs = spec->autocfg.line_outs;
		spec->autocfg.line_out_type = AUTO_PIN_HP_OUT;
		spec->autocfg.line_outs = 0;
	}

	if (stac_check_auto_mic(codec)) {
		spec->auto_mic = 1;
		/* only one capture for auto-mic */
		spec->num_adcs = 1;
		spec->num_caps = 1;
		spec->num_muxes = 1;
	}

	for (i = 0; i < spec->num_caps; i++) {
		err = stac92xx_add_capvol_ctls(codec, spec->capvols[i],
					       spec->capsws[i], i);
		if (err < 0)
			return err;
	}

	err = stac92xx_auto_create_analog_input_ctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	if (spec->mono_nid > 0) {
		err = stac92xx_auto_create_mono_output_ctls(codec);
		if (err < 0)
			return err;
	}
	if (spec->num_dmics > 0 && !spec->dinput_mux)
		if ((err = stac92xx_auto_create_dmic_input_ctls(codec,
						&spec->autocfg)) < 0)
			return err;
	if (spec->num_muxes > 0) {
		err = stac92xx_auto_create_mux_input_ctls(codec);
		if (err < 0)
			return err;
	}
	if (spec->num_smuxes > 0) {
		err = stac92xx_auto_create_spdif_mux_ctls(codec);
		if (err < 0)
			return err;
	}

	err = stac92xx_add_input_source(spec);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;
	if (spec->multiout.max_channels > 2)
		spec->surr_switch = 1;

	/* find digital out and in converters */
	for (i = codec->start_nid; i < codec->start_nid + codec->num_nodes; i++) {
		unsigned int wid_caps = get_wcaps(codec, i);
		if (wid_caps & AC_WCAP_DIGITAL) {
			switch (get_wcaps_type(wid_caps)) {
			case AC_WID_AUD_OUT:
				if (!dig_out)
					dig_out = i;
				break;
			case AC_WID_AUD_IN:
				if (!dig_in)
					dig_in = i;
				break;
			}
		}
	}
	if (spec->autocfg.dig_outs)
		spec->multiout.dig_out_nid = dig_out;
	if (dig_in && spec->autocfg.dig_in_pin)
		spec->dig_in_nid = dig_in;

	if (spec->kctls.list)
		spec->mixers[spec->num_mixers++] = spec->kctls.list;

	spec->input_mux = &spec->private_imux;
	if (!spec->dinput_mux)
		spec->dinput_mux = &spec->private_dimux;
	spec->sinput_mux = &spec->private_smux;
	spec->mono_mux = &spec->private_mono_mux;
	return 1;
}

/* add playback controls for HP output */
static int stac9200_auto_create_hp_ctls(struct hda_codec *codec,
					struct auto_pin_cfg *cfg)
{
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t pin = cfg->hp_pins[0];

	if (! pin)
		return 0;

	if (is_jack_detectable(codec, pin))
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
			defcfg = snd_hda_codec_get_pincfg(codec, pin);
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
		err = create_controls(codec, "LFE", lfe_pin, 1);
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

	if (spec->num_muxes > 0) {
		err = stac92xx_auto_create_mux_input_ctls(codec);
		if (err < 0)
			return err;
	}

	err = stac92xx_add_input_source(spec);
	if (err < 0)
		return err;

	if (spec->autocfg.dig_outs)
		spec->multiout.dig_out_nid = 0x05;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = 0x04;

	if (spec->kctls.list)
		spec->mixers[spec->num_mixers++] = spec->kctls.list;

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

	snd_printdd("%s msk %x dir %x gpio %x\n", __func__, mask, dir_mask, data);

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

static int stac_add_event(struct hda_codec *codec, hda_nid_t nid,
			  unsigned char type, int data)
{
	struct hda_jack_tbl *event;

	event = snd_hda_jack_tbl_new(codec, nid);
	if (!event)
		return -ENOMEM;
	event->action = type;
	event->private_data = data;

	return 0;
}

/* check if given nid is a valid pin and no other events are assigned
 * to it.  If OK, assign the event, set the unsol flag, and returns 1.
 * Otherwise, returns zero.
 */
static int enable_pin_detect(struct hda_codec *codec, hda_nid_t nid,
			     unsigned int type)
{
	struct hda_jack_tbl *event;

	if (!is_jack_detectable(codec, nid))
		return 0;
	event = snd_hda_jack_tbl_new(codec, nid);
	if (!event)
		return -ENOMEM;
	if (event->action && event->action != type)
		return 0;
	event->action = type;
	snd_hda_jack_detect_enable(codec, nid, 0);
	return 1;
}

static int is_nid_out_jack_pin(struct auto_pin_cfg *cfg, hda_nid_t nid)
{
	int i;
	for (i = 0; i < cfg->hp_outs; i++)
		if (cfg->hp_pins[i] == nid)
			return 1; /* nid is a HP-Out */
	for (i = 0; i < cfg->line_outs; i++)
		if (cfg->line_out_pins[i] == nid)
			return 1; /* nid is a line-Out */
	return 0; /* nid is not a HP-Out */
};

static void stac92xx_power_down(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	/* power down inactive DACs */
	const hda_nid_t *dac;
	for (dac = spec->dac_list; *dac; dac++)
		if (!check_all_dac_nids(spec, *dac))
			snd_hda_codec_write(codec, *dac, 0,
					AC_VERB_SET_POWER_STATE, AC_PWRST_D3);
}

static void stac_toggle_power_map(struct hda_codec *codec, hda_nid_t nid,
				  int enable);

static inline int get_int_hint(struct hda_codec *codec, const char *key,
			       int *valp)
{
	const char *p;
	p = snd_hda_get_hint(codec, key);
	if (p) {
		unsigned long val;
		if (!strict_strtoul(p, 0, &val)) {
			*valp = val;
			return 1;
		}
	}
	return 0;
}

/* override some hints from the hwdep entry */
static void stac_store_hints(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int val;

	val = snd_hda_get_bool_hint(codec, "hp_detect");
	if (val >= 0)
		spec->hp_detect = val;
	if (get_int_hint(codec, "gpio_mask", &spec->gpio_mask)) {
		spec->eapd_mask = spec->gpio_dir = spec->gpio_data =
			spec->gpio_mask;
	}
	if (get_int_hint(codec, "gpio_dir", &spec->gpio_dir))
		spec->gpio_mask &= spec->gpio_mask;
	if (get_int_hint(codec, "gpio_data", &spec->gpio_data))
		spec->gpio_dir &= spec->gpio_mask;
	if (get_int_hint(codec, "eapd_mask", &spec->eapd_mask))
		spec->eapd_mask &= spec->gpio_mask;
	if (get_int_hint(codec, "gpio_mute", &spec->gpio_mute))
		spec->gpio_mute &= spec->gpio_mask;
	val = snd_hda_get_bool_hint(codec, "eapd_switch");
	if (val >= 0)
		spec->eapd_switch = val;
	get_int_hint(codec, "gpio_led_polarity", &spec->gpio_led_polarity);
	if (get_int_hint(codec, "gpio_led", &spec->gpio_led)) {
		spec->gpio_mask |= spec->gpio_led;
		spec->gpio_dir |= spec->gpio_led;
		if (spec->gpio_led_polarity)
			spec->gpio_data |= spec->gpio_led;
	}
}

static void stac_issue_unsol_events(struct hda_codec *codec, int num_pins,
				    const hda_nid_t *pins)
{
	while (num_pins--)
		stac_issue_unsol_event(codec, *pins++);
}

/* fake event to set up pins */
static void stac_fake_hp_events(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	if (spec->autocfg.hp_outs)
		stac_issue_unsol_events(codec, spec->autocfg.hp_outs,
					spec->autocfg.hp_pins);
	if (spec->autocfg.line_outs &&
	    spec->autocfg.line_out_pins[0] != spec->autocfg.hp_pins[0])
		stac_issue_unsol_events(codec, spec->autocfg.line_outs,
					spec->autocfg.line_out_pins);
}

static int stac92xx_init(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int gpio;
	int i;

	snd_hda_sequence_write(codec, spec->init);

	/* power down adcs initially */
	if (spec->powerdown_adcs)
		for (i = 0; i < spec->num_adcs; i++)
			snd_hda_codec_write(codec,
				spec->adc_nids[i], 0,
				AC_VERB_SET_POWER_STATE, AC_PWRST_D3);

	/* override some hints */
	stac_store_hints(codec);

	/* set up GPIO */
	gpio = spec->gpio_data;
	/* turn on EAPD statically when spec->eapd_switch isn't set.
	 * otherwise, unsol event will turn it on/off dynamically
	 */
	if (!spec->eapd_switch)
		gpio |= spec->eapd_mask;
	stac_gpio_set(codec, spec->gpio_mask, spec->gpio_dir, gpio);

	/* set up pins */
	if (spec->hp_detect) {
		/* Enable unsolicited responses on the HP widget */
		for (i = 0; i < cfg->hp_outs; i++) {
			hda_nid_t nid = cfg->hp_pins[i];
			enable_pin_detect(codec, nid, STAC_HP_EVENT);
		}
		if (cfg->line_out_type == AUTO_PIN_LINE_OUT &&
		    cfg->speaker_outs > 0) {
			/* enable pin-detect for line-outs as well */
			for (i = 0; i < cfg->line_outs; i++) {
				hda_nid_t nid = cfg->line_out_pins[i];
				enable_pin_detect(codec, nid, STAC_LO_EVENT);
			}
		}

		/* force to enable the first line-out; the others are set up
		 * in unsol_event
		 */
		stac92xx_auto_set_pinctl(codec, spec->autocfg.line_out_pins[0],
				AC_PINCTL_OUT_EN);
		/* fake event to set up pins */
		stac_fake_hp_events(codec);
	} else {
		stac92xx_auto_init_multi_out(codec);
		stac92xx_auto_init_hp_out(codec);
		for (i = 0; i < cfg->hp_outs; i++)
			stac_toggle_power_map(codec, cfg->hp_pins[i], 1);
	}
	if (spec->auto_mic) {
		/* initialize connection to analog input */
		if (spec->dmux_nids)
			snd_hda_codec_write_cache(codec, spec->dmux_nids[0], 0,
					  AC_VERB_SET_CONNECT_SEL, 0);
		if (enable_pin_detect(codec, spec->ext_mic.pin, STAC_MIC_EVENT))
			stac_issue_unsol_event(codec, spec->ext_mic.pin);
		if (enable_pin_detect(codec, spec->dock_mic.pin,
		    STAC_MIC_EVENT))
			stac_issue_unsol_event(codec, spec->dock_mic.pin);
	}
	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		int type = cfg->inputs[i].type;
		unsigned int pinctl, conf;
		if (type == AUTO_PIN_MIC) {
			/* for mic pins, force to initialize */
			pinctl = stac92xx_get_default_vref(codec, nid);
			pinctl |= AC_PINCTL_IN_EN;
			stac92xx_auto_set_pinctl(codec, nid, pinctl);
		} else {
			pinctl = snd_hda_codec_read(codec, nid, 0,
					AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
			/* if PINCTL already set then skip */
			/* Also, if both INPUT and OUTPUT are set,
			 * it must be a BIOS bug; need to override, too
			 */
			if (!(pinctl & AC_PINCTL_IN_EN) ||
			    (pinctl & AC_PINCTL_OUT_EN)) {
				pinctl &= ~AC_PINCTL_OUT_EN;
				pinctl |= AC_PINCTL_IN_EN;
				stac92xx_auto_set_pinctl(codec, nid, pinctl);
			}
		}
		conf = snd_hda_codec_get_pincfg(codec, nid);
		if (get_defcfg_connect(conf) != AC_JACK_PORT_FIXED) {
			if (enable_pin_detect(codec, nid, STAC_INSERT_EVENT))
				stac_issue_unsol_event(codec, nid);
		}
	}
	for (i = 0; i < spec->num_dmics; i++)
		stac92xx_auto_set_pinctl(codec, spec->dmic_nids[i],
					AC_PINCTL_IN_EN);
	if (cfg->dig_out_pins[0])
		stac92xx_auto_set_pinctl(codec, cfg->dig_out_pins[0],
					 AC_PINCTL_OUT_EN);
	if (cfg->dig_in_pin)
		stac92xx_auto_set_pinctl(codec, cfg->dig_in_pin,
					 AC_PINCTL_IN_EN);
	for (i = 0; i < spec->num_pwrs; i++)  {
		hda_nid_t nid = spec->pwr_nids[i];
		unsigned int pinctl, def_conf;

		/* power on when no jack detection is available */
		/* or when the VREF is used for controlling LED */
		if (!spec->hp_detect ||
		    spec->vref_mute_led_nid == nid) {
			stac_toggle_power_map(codec, nid, 1);
			continue;
		}

		if (is_nid_out_jack_pin(cfg, nid))
			continue; /* already has an unsol event */

		pinctl = snd_hda_codec_read(codec, nid, 0,
					    AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		/* outputs are only ports capable of power management
		 * any attempts on powering down a input port cause the
		 * referenced VREF to act quirky.
		 */
		if (pinctl & AC_PINCTL_IN_EN) {
			stac_toggle_power_map(codec, nid, 1);
			continue;
		}
		def_conf = snd_hda_codec_get_pincfg(codec, nid);
		def_conf = get_defcfg_connect(def_conf);
		/* skip any ports that don't have jacks since presence
 		 * detection is useless */
		if (def_conf != AC_JACK_PORT_COMPLEX ||
		    !is_jack_detectable(codec, nid)) {
			stac_toggle_power_map(codec, nid, 1);
			continue;
		}
		if (enable_pin_detect(codec, nid, STAC_PWR_EVENT)) {
			stac_issue_unsol_event(codec, nid);
			continue;
		}
		/* none of the above, turn the port OFF */
		stac_toggle_power_map(codec, nid, 0);
	}

	snd_hda_jack_report_sync(codec);

	/* sync mute LED */
	snd_hda_sync_vmaster_hook(&spec->vmaster_mute);
	if (spec->dac_list)
		stac92xx_power_down(codec);
	return 0;
}

static void stac92xx_free_kctls(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	if (spec->kctls.list) {
		struct snd_kcontrol_new *kctl = spec->kctls.list;
		int i;
		for (i = 0; i < spec->kctls.used; i++)
			kfree(kctl[i].name);
	}
	snd_array_free(&spec->kctls);
}

static void stac92xx_shutup_pins(struct hda_codec *codec)
{
	unsigned int i, def_conf;

	if (codec->bus->shutdown)
		return;
	for (i = 0; i < codec->init_pins.used; i++) {
		struct hda_pincfg *pin = snd_array_elem(&codec->init_pins, i);
		def_conf = snd_hda_codec_get_pincfg(codec, pin->nid);
		if (get_defcfg_connect(def_conf) != AC_JACK_PORT_NONE)
			snd_hda_codec_write(codec, pin->nid, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, 0);
	}
}

static void stac92xx_shutup(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	stac92xx_shutup_pins(codec);

	if (spec->eapd_mask)
		stac_gpio_set(codec, spec->gpio_mask,
				spec->gpio_dir, spec->gpio_data &
				~spec->eapd_mask);
}

static void stac92xx_free(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	if (! spec)
		return;

	stac92xx_shutup(codec);

	kfree(spec);
	snd_hda_detach_beep_device(codec);
}

static void stac92xx_set_pinctl(struct hda_codec *codec, hda_nid_t nid,
				unsigned int flag)
{
	unsigned int old_ctl, pin_ctl;

	pin_ctl = snd_hda_codec_read(codec, nid,
			0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00);

	if (pin_ctl & AC_PINCTL_IN_EN) {
		/*
		 * we need to check the current set-up direction of
		 * shared input pins since they can be switched via
		 * "xxx as Output" mixer switch
		 */
		struct sigmatel_spec *spec = codec->spec;
		if (nid == spec->line_switch || nid == spec->mic_switch)
			return;
	}

	old_ctl = pin_ctl;
	/* if setting pin direction bits, clear the current
	   direction bits first */
	if (flag & (AC_PINCTL_IN_EN | AC_PINCTL_OUT_EN))
		pin_ctl &= ~(AC_PINCTL_IN_EN | AC_PINCTL_OUT_EN);
	
	pin_ctl |= flag;
	if (old_ctl != pin_ctl)
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_PIN_WIDGET_CONTROL,
					  pin_ctl);
}

static void stac92xx_reset_pinctl(struct hda_codec *codec, hda_nid_t nid,
				  unsigned int flag)
{
	unsigned int pin_ctl = snd_hda_codec_read(codec, nid,
			0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0x00);
	if (pin_ctl & flag)
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_PIN_WIDGET_CONTROL,
					  pin_ctl & ~flag);
}

static inline int get_pin_presence(struct hda_codec *codec, hda_nid_t nid)
{
	if (!nid)
		return 0;
	return snd_hda_jack_detect(codec, nid);
}

static void stac92xx_line_out_detect(struct hda_codec *codec,
				     int presence)
{
	struct sigmatel_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	for (i = 0; i < cfg->line_outs; i++) {
		if (presence)
			break;
		presence = get_pin_presence(codec, cfg->line_out_pins[i]);
		if (presence) {
			unsigned int pinctl;
			pinctl = snd_hda_codec_read(codec,
						    cfg->line_out_pins[i], 0,
					    AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
			if (pinctl & AC_PINCTL_IN_EN)
				presence = 0; /* mic- or line-input */
		}
	}

	if (presence) {
		/* disable speakers */
		for (i = 0; i < cfg->speaker_outs; i++)
			stac92xx_reset_pinctl(codec, cfg->speaker_pins[i],
						AC_PINCTL_OUT_EN);
		if (spec->eapd_mask && spec->eapd_switch)
			stac_gpio_set(codec, spec->gpio_mask,
				spec->gpio_dir, spec->gpio_data &
				~spec->eapd_mask);
	} else {
		/* enable speakers */
		for (i = 0; i < cfg->speaker_outs; i++)
			stac92xx_set_pinctl(codec, cfg->speaker_pins[i],
						AC_PINCTL_OUT_EN);
		if (spec->eapd_mask && spec->eapd_switch)
			stac_gpio_set(codec, spec->gpio_mask,
				spec->gpio_dir, spec->gpio_data |
				spec->eapd_mask);
	}
} 

/* return non-zero if the hp-pin of the given array index isn't
 * a jack-detection target
 */
static int no_hp_sensing(struct sigmatel_spec *spec, int i)
{
	struct auto_pin_cfg *cfg = &spec->autocfg;

	/* ignore sensing of shared line and mic jacks */
	if (cfg->hp_pins[i] == spec->line_switch)
		return 1;
	if (cfg->hp_pins[i] == spec->mic_switch)
		return 1;
	/* ignore if the pin is set as line-out */
	if (cfg->hp_pins[i] == spec->hp_switch)
		return 1;
	return 0;
}

static void stac92xx_hp_detect(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i, presence;

	presence = 0;
	if (spec->gpio_mute)
		presence = !(snd_hda_codec_read(codec, codec->afg, 0,
			AC_VERB_GET_GPIO_DATA, 0) & spec->gpio_mute);

	for (i = 0; i < cfg->hp_outs; i++) {
		if (presence)
			break;
		if (no_hp_sensing(spec, i))
			continue;
		presence = get_pin_presence(codec, cfg->hp_pins[i]);
		if (presence) {
			unsigned int pinctl;
			pinctl = snd_hda_codec_read(codec, cfg->hp_pins[i], 0,
					    AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
			if (pinctl & AC_PINCTL_IN_EN)
				presence = 0; /* mic- or line-input */
		}
	}

	if (presence) {
		/* disable lineouts */
		if (spec->hp_switch)
			stac92xx_reset_pinctl(codec, spec->hp_switch,
					      AC_PINCTL_OUT_EN);
		for (i = 0; i < cfg->line_outs; i++)
			stac92xx_reset_pinctl(codec, cfg->line_out_pins[i],
						AC_PINCTL_OUT_EN);
	} else {
		/* enable lineouts */
		if (spec->hp_switch)
			stac92xx_set_pinctl(codec, spec->hp_switch,
					    AC_PINCTL_OUT_EN);
		for (i = 0; i < cfg->line_outs; i++)
			stac92xx_set_pinctl(codec, cfg->line_out_pins[i],
						AC_PINCTL_OUT_EN);
	}
	stac92xx_line_out_detect(codec, presence);
	/* toggle hp outs */
	for (i = 0; i < cfg->hp_outs; i++) {
		unsigned int val = AC_PINCTL_OUT_EN | AC_PINCTL_HP_EN;
		if (no_hp_sensing(spec, i))
			continue;
		if (1 /*presence*/)
			stac92xx_set_pinctl(codec, cfg->hp_pins[i], val);
#if 0 /* FIXME */
/* Resetting the pinctl like below may lead to (a sort of) regressions
 * on some devices since they use the HP pin actually for line/speaker
 * outs although the default pin config shows a different pin (that is
 * wrong and useless).
 *
 * So, it's basically a problem of default pin configs, likely a BIOS issue.
 * But, disabling the code below just works around it, and I'm too tired of
 * bug reports with such devices... 
 */
		else
			stac92xx_reset_pinctl(codec, cfg->hp_pins[i], val);
#endif /* FIXME */
	}
} 

static void stac_toggle_power_map(struct hda_codec *codec, hda_nid_t nid,
				  int enable)
{
	struct sigmatel_spec *spec = codec->spec;
	unsigned int idx, val;

	for (idx = 0; idx < spec->num_pwrs; idx++) {
		if (spec->pwr_nids[idx] == nid)
			break;
	}
	if (idx >= spec->num_pwrs)
		return;

	idx = 1 << idx;

	val = snd_hda_codec_read(codec, codec->afg, 0, 0x0fec, 0x0) & 0xff;
	if (enable)
		val &= ~idx;
	else
		val |= idx;

	/* power down unused output ports */
	snd_hda_codec_write(codec, codec->afg, 0, 0x7ec, val);
}

static void stac92xx_pin_sense(struct hda_codec *codec, hda_nid_t nid)
{
	stac_toggle_power_map(codec, nid, get_pin_presence(codec, nid));
}

/* get the pin connection (fixed, none, etc) */
static unsigned int stac_get_defcfg_connect(struct hda_codec *codec, int idx)
{
	struct sigmatel_spec *spec = codec->spec;
	unsigned int cfg;

	cfg = snd_hda_codec_get_pincfg(codec, spec->pin_nids[idx]);
	return get_defcfg_connect(cfg);
}

static int stac92xx_connected_ports(struct hda_codec *codec,
				    const hda_nid_t *nids, int num_nids)
{
	struct sigmatel_spec *spec = codec->spec;
	int idx, num;
	unsigned int def_conf;

	for (num = 0; num < num_nids; num++) {
		for (idx = 0; idx < spec->num_pins; idx++)
			if (spec->pin_nids[idx] == nids[num])
				break;
		if (idx >= spec->num_pins)
			break;
		def_conf = stac_get_defcfg_connect(codec, idx);
		if (def_conf == AC_JACK_PORT_NONE)
			break;
	}
	return num;
}

static void stac92xx_mic_detect(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	struct sigmatel_mic_route *mic;

	if (get_pin_presence(codec, spec->ext_mic.pin))
		mic = &spec->ext_mic;
	else if (get_pin_presence(codec, spec->dock_mic.pin))
		mic = &spec->dock_mic;
	else
		mic = &spec->int_mic;
	if (mic->dmux_idx >= 0)
		snd_hda_codec_write_cache(codec, spec->dmux_nids[0], 0,
					  AC_VERB_SET_CONNECT_SEL,
					  mic->dmux_idx);
	if (mic->mux_idx >= 0)
		snd_hda_codec_write_cache(codec, spec->mux_nids[0], 0,
					  AC_VERB_SET_CONNECT_SEL,
					  mic->mux_idx);
}

static void handle_unsol_event(struct hda_codec *codec,
			       struct hda_jack_tbl *event)
{
	struct sigmatel_spec *spec = codec->spec;
	int data;

	switch (event->action) {
	case STAC_HP_EVENT:
	case STAC_LO_EVENT:
		stac92xx_hp_detect(codec);
		break;
	case STAC_MIC_EVENT:
		stac92xx_mic_detect(codec);
		break;
	}

	switch (event->action) {
	case STAC_HP_EVENT:
	case STAC_LO_EVENT:
	case STAC_MIC_EVENT:
	case STAC_INSERT_EVENT:
	case STAC_PWR_EVENT:
		if (spec->num_pwrs > 0)
			stac92xx_pin_sense(codec, event->nid);

		switch (codec->subsystem_id) {
		case 0x103c308f:
			if (event->nid == 0xb) {
				int pin = AC_PINCTL_IN_EN;

				if (get_pin_presence(codec, 0xa)
						&& get_pin_presence(codec, 0xb))
					pin |= AC_PINCTL_VREF_80;
				if (!get_pin_presence(codec, 0xb))
					pin |= AC_PINCTL_VREF_80;

				/* toggle VREF state based on mic + hp pin
				 * status
				 */
				stac92xx_auto_set_pinctl(codec, 0x0a, pin);
			}
		}
		break;
	case STAC_VREF_EVENT:
		data = snd_hda_codec_read(codec, codec->afg, 0,
					  AC_VERB_GET_GPIO_DATA, 0);
		/* toggle VREF state based on GPIOx status */
		snd_hda_codec_write(codec, codec->afg, 0, 0x7e0,
				    !!(data & (1 << event->private_data)));
		break;
	}
}

static void stac_issue_unsol_event(struct hda_codec *codec, hda_nid_t nid)
{
	struct hda_jack_tbl *event = snd_hda_jack_tbl_get(codec, nid);
	if (!event)
		return;
	handle_unsol_event(codec, event);
}

static void stac92xx_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct hda_jack_tbl *event;
	int tag;

	tag = (res >> 26) & 0x7f;
	event = snd_hda_jack_tbl_get_from_tag(codec, tag);
	if (!event)
		return;
	event->jack_dirty = 1;
	handle_unsol_event(codec, event);
	snd_hda_jack_report_sync(codec);
}

static int hp_blike_system(u32 subsystem_id);

static void set_hp_led_gpio(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	unsigned int gpio;

	if (spec->gpio_led)
		return;

	gpio = snd_hda_param_read(codec, codec->afg, AC_PAR_GPIO_CAP);
	gpio &= AC_GPIO_IO_COUNT;
	if (gpio > 3)
		spec->gpio_led = 0x08; /* GPIO 3 */
	else
		spec->gpio_led = 0x01; /* GPIO 0 */
}

/*
 * This method searches for the mute LED GPIO configuration
 * provided as OEM string in SMBIOS. The format of that string
 * is HP_Mute_LED_P_G or HP_Mute_LED_P
 * where P can be 0 or 1 and defines mute LED GPIO control state (low/high)
 * that corresponds to the NOT muted state of the master volume
 * and G is the index of the GPIO to use as the mute LED control (0..9)
 * If _G portion is missing it is assigned based on the codec ID
 *
 * So, HP B-series like systems may have HP_Mute_LED_0 (current models)
 * or  HP_Mute_LED_0_3 (future models) OEM SMBIOS strings
 *
 *
 * The dv-series laptops don't seem to have the HP_Mute_LED* strings in
 * SMBIOS - at least the ones I have seen do not have them - which include
 * my own system (HP Pavilion dv6-1110ax) and my cousin's
 * HP Pavilion dv9500t CTO.
 * Need more information on whether it is true across the entire series.
 * -- kunal
 */
static int find_mute_led_cfg(struct hda_codec *codec, int default_polarity)
{
	struct sigmatel_spec *spec = codec->spec;
	const struct dmi_device *dev = NULL;

	if ((codec->subsystem_id >> 16) == PCI_VENDOR_ID_HP) {
		while ((dev = dmi_find_device(DMI_DEV_TYPE_OEM_STRING,
								NULL, dev))) {
			if (sscanf(dev->name, "HP_Mute_LED_%d_%x",
				  &spec->gpio_led_polarity,
				  &spec->gpio_led) == 2) {
				unsigned int max_gpio;
				max_gpio = snd_hda_param_read(codec, codec->afg,
							      AC_PAR_GPIO_CAP);
				max_gpio &= AC_GPIO_IO_COUNT;
				if (spec->gpio_led < max_gpio)
					spec->gpio_led = 1 << spec->gpio_led;
				else
					spec->vref_mute_led_nid = spec->gpio_led;
				return 1;
			}
			if (sscanf(dev->name, "HP_Mute_LED_%d",
				  &spec->gpio_led_polarity) == 1) {
				set_hp_led_gpio(codec);
				return 1;
			}
			/* BIOS bug: unfilled OEM string */
			if (strstr(dev->name, "HP_Mute_LED_P_G")) {
				set_hp_led_gpio(codec);
				switch (codec->subsystem_id) {
				case 0x103c148a:
					spec->gpio_led_polarity = 0;
					break;
				default:
					spec->gpio_led_polarity = 1;
					break;
				}
				return 1;
			}
		}

		/*
		 * Fallback case - if we don't find the DMI strings,
		 * we statically set the GPIO - if not a B-series system
		 * and default polarity is provided
		 */
		if (!hp_blike_system(codec->subsystem_id) &&
			(default_polarity == 0 || default_polarity == 1)) {
			set_hp_led_gpio(codec);
			spec->gpio_led_polarity = default_polarity;
			return 1;
		}
	}
	return 0;
}

static int hp_blike_system(u32 subsystem_id)
{
	switch (subsystem_id) {
	case 0x103c1520:
	case 0x103c1521:
	case 0x103c1523:
	case 0x103c1524:
	case 0x103c1525:
	case 0x103c1722:
	case 0x103c1723:
	case 0x103c1724:
	case 0x103c1725:
	case 0x103c1726:
	case 0x103c1727:
	case 0x103c1728:
	case 0x103c1729:
	case 0x103c172a:
	case 0x103c172b:
	case 0x103c307e:
	case 0x103c307f:
	case 0x103c3080:
	case 0x103c3081:
	case 0x103c7007:
	case 0x103c7008:
		return 1;
	}
	return 0;
}

#ifdef CONFIG_PROC_FS
static void stac92hd_proc_hook(struct snd_info_buffer *buffer,
			       struct hda_codec *codec, hda_nid_t nid)
{
	if (nid == codec->afg)
		snd_iprintf(buffer, "Power-Map: 0x%02x\n", 
			    snd_hda_codec_read(codec, nid, 0, 0x0fec, 0x0));
}

static void analog_loop_proc_hook(struct snd_info_buffer *buffer,
				  struct hda_codec *codec,
				  unsigned int verb)
{
	snd_iprintf(buffer, "Analog Loopback: 0x%02x\n",
		    snd_hda_codec_read(codec, codec->afg, 0, verb, 0));
}

/* stac92hd71bxx, stac92hd73xx */
static void stac92hd7x_proc_hook(struct snd_info_buffer *buffer,
				 struct hda_codec *codec, hda_nid_t nid)
{
	stac92hd_proc_hook(buffer, codec, nid);
	if (nid == codec->afg)
		analog_loop_proc_hook(buffer, codec, 0xfa0);
}

static void stac9205_proc_hook(struct snd_info_buffer *buffer,
			       struct hda_codec *codec, hda_nid_t nid)
{
	if (nid == codec->afg)
		analog_loop_proc_hook(buffer, codec, 0xfe0);
}

static void stac927x_proc_hook(struct snd_info_buffer *buffer,
			       struct hda_codec *codec, hda_nid_t nid)
{
	if (nid == codec->afg)
		analog_loop_proc_hook(buffer, codec, 0xfeb);
}
#else
#define stac92hd_proc_hook	NULL
#define stac92hd7x_proc_hook	NULL
#define stac9205_proc_hook	NULL
#define stac927x_proc_hook	NULL
#endif

#ifdef CONFIG_PM
static int stac92xx_resume(struct hda_codec *codec)
{
	stac92xx_init(codec);
	snd_hda_codec_resume_amp(codec);
	snd_hda_codec_resume_cache(codec);
	/* fake event to set up pins again to override cached values */
	stac_fake_hp_events(codec);
	return 0;
}

static int stac92xx_suspend(struct hda_codec *codec, pm_message_t state)
{
	stac92xx_shutup(codec);
	return 0;
}

static int stac92xx_pre_resume(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	/* sync mute LED */
	if (spec->vref_mute_led_nid)
		stac_vrefout_set(codec, spec->vref_mute_led_nid,
				 spec->vref_led);
	else if (spec->gpio_led)
		stac_gpio_set(codec, spec->gpio_mask,
			      spec->gpio_dir, spec->gpio_data);
	return 0;
}

static void stac92xx_set_power_state(struct hda_codec *codec, hda_nid_t fg,
				unsigned int power_state)
{
	unsigned int afg_power_state = power_state;
	struct sigmatel_spec *spec = codec->spec;

	if (power_state == AC_PWRST_D3) {
		if (spec->vref_mute_led_nid) {
			/* with vref-out pin used for mute led control
			 * codec AFG is prevented from D3 state
			 */
			afg_power_state = AC_PWRST_D1;
		}
		/* this delay seems necessary to avoid click noise at power-down */
		msleep(100);
	}
	snd_hda_codec_read(codec, fg, 0, AC_VERB_SET_POWER_STATE,
			afg_power_state);
	snd_hda_codec_set_power_to_all(codec, fg, power_state, true);
}
#else
#define stac92xx_suspend	NULL
#define stac92xx_resume		NULL
#define stac92xx_pre_resume	NULL
#define stac92xx_set_power_state NULL
#endif /* CONFIG_PM */

/* update mute-LED accoring to the master switch */
static void stac92xx_update_led_status(struct hda_codec *codec, int enabled)
{
	struct sigmatel_spec *spec = codec->spec;
	int muted = !enabled;

	if (!spec->gpio_led)
		return;

	/* LED state is inverted on these systems */
	if (spec->gpio_led_polarity)
		muted = !muted;

	if (!spec->vref_mute_led_nid) {
		if (muted)
			spec->gpio_data |= spec->gpio_led;
		else
			spec->gpio_data &= ~spec->gpio_led;
		stac_gpio_set(codec, spec->gpio_mask,
				spec->gpio_dir, spec->gpio_data);
	} else {
		spec->vref_led = muted ? AC_PINCTL_VREF_50 : AC_PINCTL_VREF_GRD;
		stac_vrefout_set(codec,	spec->vref_mute_led_nid,
				 spec->vref_led);
	}
}

static const struct hda_codec_ops stac92xx_patch_ops = {
	.build_controls = stac92xx_build_controls,
	.build_pcms = stac92xx_build_pcms,
	.init = stac92xx_init,
	.free = stac92xx_free,
	.unsol_event = stac92xx_unsol_event,
#ifdef CONFIG_PM
	.suspend = stac92xx_suspend,
	.resume = stac92xx_resume,
#endif
	.reboot_notify = stac92xx_shutup,
};

static int patch_stac9200(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->no_trigger_sense = 1;
	codec->spec = spec;
	spec->linear_tone_beep = 1;
	spec->num_pins = ARRAY_SIZE(stac9200_pin_nids);
	spec->pin_nids = stac9200_pin_nids;
	spec->board_config = snd_hda_check_board_config(codec, STAC_9200_MODELS,
							stac9200_models,
							stac9200_cfg_tbl);
	if (spec->board_config < 0)
		snd_printdd(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
			    codec->chip_name);
	else
		stac92xx_set_config_regs(codec,
					 stac9200_brd_tbl[spec->board_config]);

	spec->multiout.max_channels = 2;
	spec->multiout.num_dacs = 1;
	spec->multiout.dac_nids = stac9200_dac_nids;
	spec->adc_nids = stac9200_adc_nids;
	spec->mux_nids = stac9200_mux_nids;
	spec->num_muxes = 1;
	spec->num_dmics = 0;
	spec->num_adcs = 1;
	spec->num_pwrs = 0;

	if (spec->board_config == STAC_9200_M4 ||
	    spec->board_config == STAC_9200_M4_2 ||
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

	/* CF-74 has no headphone detection, and the driver should *NOT*
	 * do detection and HP/speaker toggle because the hardware does it.
	 */
	if (spec->board_config == STAC_9200_PANASONIC)
		spec->hp_detect = 0;

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

	codec->no_trigger_sense = 1;
	codec->spec = spec;
	spec->linear_tone_beep = 1;
	spec->num_pins = ARRAY_SIZE(stac925x_pin_nids);
	spec->pin_nids = stac925x_pin_nids;

	/* Check first for codec ID */
	spec->board_config = snd_hda_check_board_codec_sid_config(codec,
							STAC_925x_MODELS,
							stac925x_models,
							stac925x_codec_id_cfg_tbl);

	/* Now checks for PCI ID, if codec ID is not found */
	if (spec->board_config < 0)
		spec->board_config = snd_hda_check_board_config(codec,
							STAC_925x_MODELS,
							stac925x_models,
							stac925x_cfg_tbl);
 again:
	if (spec->board_config < 0)
		snd_printdd(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
			    codec->chip_name);
	else
		stac92xx_set_config_regs(codec,
					 stac925x_brd_tbl[spec->board_config]);

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
	spec->num_caps = 1;
	spec->capvols = stac925x_capvols;
	spec->capsws = stac925x_capsws;

	err = stac92xx_parse_auto_config(codec);
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

static int patch_stac92hd73xx(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	hda_nid_t conn[STAC92HD73_DAC_COUNT + 2];
	int err = 0;
	int num_dacs;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->no_trigger_sense = 1;
	codec->spec = spec;
	spec->linear_tone_beep = 0;
	codec->slave_dig_outs = stac92hd73xx_slave_dig_outs;
	spec->num_pins = ARRAY_SIZE(stac92hd73xx_pin_nids);
	spec->pin_nids = stac92hd73xx_pin_nids;
	spec->board_config = snd_hda_check_board_config(codec,
							STAC_92HD73XX_MODELS,
							stac92hd73xx_models,
							stac92hd73xx_cfg_tbl);
	/* check codec subsystem id if not found */
	if (spec->board_config < 0)
		spec->board_config =
			snd_hda_check_board_codec_sid_config(codec,
				STAC_92HD73XX_MODELS, stac92hd73xx_models,
				stac92hd73xx_codec_id_cfg_tbl);
again:
	if (spec->board_config < 0)
		snd_printdd(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
			    codec->chip_name);
	else
		stac92xx_set_config_regs(codec,
				stac92hd73xx_brd_tbl[spec->board_config]);

	num_dacs = snd_hda_get_connections(codec, 0x0a,
			conn, STAC92HD73_DAC_COUNT + 2) - 1;

	if (num_dacs < 3 || num_dacs > 5) {
		printk(KERN_WARNING "hda_codec: Could not determine "
		       "number of channels defaulting to DAC count\n");
		num_dacs = STAC92HD73_DAC_COUNT;
	}
	spec->init = stac92hd73xx_core_init;
	switch (num_dacs) {
	case 0x3: /* 6 Channel */
		spec->aloopback_ctl = stac92hd73xx_6ch_loopback;
		break;
	case 0x4: /* 8 Channel */
		spec->aloopback_ctl = stac92hd73xx_8ch_loopback;
		break;
	case 0x5: /* 10 Channel */
		spec->aloopback_ctl = stac92hd73xx_10ch_loopback;
		break;
	}
	spec->multiout.dac_nids = spec->dac_nids;

	spec->aloopback_mask = 0x01;
	spec->aloopback_shift = 8;

	spec->digbeep_nid = 0x1c;
	spec->mux_nids = stac92hd73xx_mux_nids;
	spec->adc_nids = stac92hd73xx_adc_nids;
	spec->dmic_nids = stac92hd73xx_dmic_nids;
	spec->dmux_nids = stac92hd73xx_dmux_nids;
	spec->smux_nids = stac92hd73xx_smux_nids;

	spec->num_muxes = ARRAY_SIZE(stac92hd73xx_mux_nids);
	spec->num_adcs = ARRAY_SIZE(stac92hd73xx_adc_nids);
	spec->num_dmuxes = ARRAY_SIZE(stac92hd73xx_dmux_nids);

	spec->num_caps = STAC92HD73XX_NUM_CAPS;
	spec->capvols = stac92hd73xx_capvols;
	spec->capsws = stac92hd73xx_capsws;

	switch (spec->board_config) {
	case STAC_DELL_EQ:
		spec->init = dell_eq_core_init;
		/* fallthru */
	case STAC_DELL_M6_AMIC:
	case STAC_DELL_M6_DMIC:
	case STAC_DELL_M6_BOTH:
		spec->num_smuxes = 0;
		spec->eapd_switch = 0;

		switch (spec->board_config) {
		case STAC_DELL_M6_AMIC: /* Analog Mics */
			snd_hda_codec_set_pincfg(codec, 0x0b, 0x90A70170);
			spec->num_dmics = 0;
			break;
		case STAC_DELL_M6_DMIC: /* Digital Mics */
			snd_hda_codec_set_pincfg(codec, 0x13, 0x90A60160);
			spec->num_dmics = 1;
			break;
		case STAC_DELL_M6_BOTH: /* Both */
			snd_hda_codec_set_pincfg(codec, 0x0b, 0x90A70170);
			snd_hda_codec_set_pincfg(codec, 0x13, 0x90A60160);
			spec->num_dmics = 1;
			break;
		}
		break;
	case STAC_ALIENWARE_M17X:
		spec->num_dmics = STAC92HD73XX_NUM_DMICS;
		spec->num_smuxes = ARRAY_SIZE(stac92hd73xx_smux_nids);
		spec->eapd_switch = 0;
		break;
	default:
		spec->num_dmics = STAC92HD73XX_NUM_DMICS;
		spec->num_smuxes = ARRAY_SIZE(stac92hd73xx_smux_nids);
		spec->eapd_switch = 1;
		break;
	}
	if (spec->board_config != STAC_92HD73XX_REF) {
		/* GPIO0 High = Enable EAPD */
		spec->eapd_mask = spec->gpio_mask = spec->gpio_dir = 0x1;
		spec->gpio_data = 0x01;
	}

	spec->num_pwrs = ARRAY_SIZE(stac92hd73xx_pwr_nids);
	spec->pwr_nids = stac92hd73xx_pwr_nids;

	err = stac92xx_parse_auto_config(codec);

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

	if (spec->board_config == STAC_92HD73XX_NO_JD)
		spec->hp_detect = 0;

	codec->patch_ops = stac92xx_patch_ops;

	codec->proc_widget_hook = stac92hd7x_proc_hook;

	return 0;
}

static int hp_bnb2011_with_dock(struct hda_codec *codec)
{
	if (codec->vendor_id != 0x111d7605 &&
	    codec->vendor_id != 0x111d76d1)
		return 0;

	switch (codec->subsystem_id) {
	case 0x103c1618:
	case 0x103c1619:
	case 0x103c161a:
	case 0x103c161b:
	case 0x103c161c:
	case 0x103c161d:
	case 0x103c161e:
	case 0x103c161f:

	case 0x103c162a:
	case 0x103c162b:

	case 0x103c1630:
	case 0x103c1631:

	case 0x103c1633:
	case 0x103c1634:
	case 0x103c1635:

	case 0x103c3587:
	case 0x103c3588:
	case 0x103c3589:
	case 0x103c358a:

	case 0x103c3667:
	case 0x103c3668:
	case 0x103c3669:

		return 1;
	}
	return 0;
}

static void stac92hd8x_add_pin(struct hda_codec *codec, hda_nid_t nid)
{
	struct sigmatel_spec *spec = codec->spec;
	unsigned int def_conf = snd_hda_codec_get_pincfg(codec, nid);
	int i;

	spec->auto_pin_nids[spec->auto_pin_cnt] = nid;
	spec->auto_pin_cnt++;

	if (get_defcfg_device(def_conf) == AC_JACK_MIC_IN &&
	    get_defcfg_connect(def_conf) != AC_JACK_PORT_NONE) {
		for (i = 0; i < ARRAY_SIZE(stac92hd83xxx_dmic_nids); i++) {
			if (nid == stac92hd83xxx_dmic_nids[i]) {
				spec->auto_dmic_nids[spec->auto_dmic_cnt] = nid;
				spec->auto_dmic_cnt++;
			}
		}
	}
}

static void stac92hd8x_add_adc(struct hda_codec *codec, hda_nid_t nid)
{
	struct sigmatel_spec *spec = codec->spec;

	spec->auto_adc_nids[spec->auto_adc_cnt] = nid;
	spec->auto_adc_cnt++;
}

static void stac92hd8x_add_mux(struct hda_codec *codec, hda_nid_t nid)
{
	int i, j;
	struct sigmatel_spec *spec = codec->spec;

	for (i = 0; i < spec->auto_adc_cnt; i++) {
		if (get_connection_index(codec,
				spec->auto_adc_nids[i], nid) >= 0) {
			/* mux and volume for adc_nids[i] */
			if (!spec->auto_mux_nids[i]) {
				spec->auto_mux_nids[i] = nid;
				/* 92hd codecs capture volume is in mux */
				spec->auto_capvols[i] = HDA_COMPOSE_AMP_VAL(nid,
							3, 0, HDA_OUTPUT);
			}
			for (j = 0; j < spec->auto_dmic_cnt; j++) {
				if (get_connection_index(codec, nid,
						spec->auto_dmic_nids[j]) >= 0) {
					/* dmux for adc_nids[i] */
					if (!spec->auto_dmux_nids[i])
						spec->auto_dmux_nids[i] = nid;
					break;
				}
			}
			break;
		}
	}
}

static void stac92hd8x_fill_auto_spec(struct hda_codec *codec)
{
	hda_nid_t nid, end_nid;
	unsigned int wid_caps, wid_type;
	struct sigmatel_spec *spec = codec->spec;

	end_nid = codec->start_nid + codec->num_nodes;

	for (nid = codec->start_nid; nid < end_nid; nid++) {
		wid_caps = get_wcaps(codec, nid);
		wid_type = get_wcaps_type(wid_caps);

		if (wid_type == AC_WID_PIN)
			stac92hd8x_add_pin(codec, nid);

		if (wid_type == AC_WID_AUD_IN && !(wid_caps & AC_WCAP_DIGITAL))
			stac92hd8x_add_adc(codec, nid);
	}

	for (nid = codec->start_nid; nid < end_nid; nid++) {
		wid_caps = get_wcaps(codec, nid);
		wid_type = get_wcaps_type(wid_caps);

		if (wid_type == AC_WID_AUD_SEL)
			stac92hd8x_add_mux(codec, nid);
	}

	spec->pin_nids = spec->auto_pin_nids;
	spec->num_pins = spec->auto_pin_cnt;
	spec->adc_nids = spec->auto_adc_nids;
	spec->num_adcs = spec->auto_adc_cnt;
	spec->capvols = spec->auto_capvols;
	spec->capsws = spec->auto_capvols;
	spec->num_caps = spec->auto_adc_cnt;
	spec->mux_nids = spec->auto_mux_nids;
	spec->num_muxes = spec->auto_adc_cnt;
	spec->dmux_nids = spec->auto_dmux_nids;
	spec->num_dmuxes = spec->auto_adc_cnt;
	spec->dmic_nids = spec->auto_dmic_nids;
	spec->num_dmics = spec->auto_dmic_cnt;
}

static int patch_stac92hd83xxx(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int default_polarity = -1; /* no default cfg */
	int err;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	if (hp_bnb2011_with_dock(codec)) {
		snd_hda_codec_set_pincfg(codec, 0xa, 0x2101201f);
		snd_hda_codec_set_pincfg(codec, 0xf, 0x2181205e);
	}

	codec->no_trigger_sense = 1;
	codec->spec = spec;

	stac92hd8x_fill_auto_spec(codec);

	spec->linear_tone_beep = 0;
	codec->slave_dig_outs = stac92hd83xxx_slave_dig_outs;
	spec->digbeep_nid = 0x21;
	spec->pwr_nids = stac92hd83xxx_pwr_nids;
	spec->num_pwrs = ARRAY_SIZE(stac92hd83xxx_pwr_nids);
	spec->multiout.dac_nids = spec->dac_nids;
	spec->init = stac92hd83xxx_core_init;

	spec->board_config = snd_hda_check_board_config(codec,
							STAC_92HD83XXX_MODELS,
							stac92hd83xxx_models,
							stac92hd83xxx_cfg_tbl);
	/* check codec subsystem id if not found */
	if (spec->board_config < 0)
		spec->board_config =
			snd_hda_check_board_codec_sid_config(codec,
				STAC_92HD83XXX_MODELS, stac92hd83xxx_models,
				stac92hd83xxx_codec_id_cfg_tbl);
again:
	if (spec->board_config < 0)
		snd_printdd(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
			    codec->chip_name);
	else
		stac92xx_set_config_regs(codec,
				stac92hd83xxx_brd_tbl[spec->board_config]);

	codec->patch_ops = stac92xx_patch_ops;

	switch (spec->board_config) {
	case STAC_HP_ZEPHYR:
		spec->init = stac92hd83xxx_hp_zephyr_init;
		break;
	case STAC_92HD83XXX_HP_LED:
		default_polarity = 1;
		break;
	}

	if (find_mute_led_cfg(codec, default_polarity))
		snd_printd("mute LED gpio %d polarity %d\n",
				spec->gpio_led,
				spec->gpio_led_polarity);

	if (spec->gpio_led) {
		if (!spec->vref_mute_led_nid) {
			spec->gpio_mask |= spec->gpio_led;
			spec->gpio_dir |= spec->gpio_led;
			spec->gpio_data |= spec->gpio_led;
		} else {
			codec->patch_ops.set_power_state =
					stac92xx_set_power_state;
		}
#ifdef CONFIG_PM
		codec->patch_ops.pre_resume = stac92xx_pre_resume;
#endif
	}

	err = stac92xx_parse_auto_config(codec);
	if (!err) {
		if (spec->board_config < 0) {
			printk(KERN_WARNING "hda_codec: No auto-config is "
			       "available, default to model=ref\n");
			spec->board_config = STAC_92HD83XXX_REF;
			goto again;
		}
		err = -EINVAL;
	}

	if (err < 0) {
		stac92xx_free(codec);
		return err;
	}

	codec->proc_widget_hook = stac92hd_proc_hook;

	return 0;
}

static int stac92hd71bxx_connected_smuxes(struct hda_codec *codec,
					  hda_nid_t dig0pin)
{
	struct sigmatel_spec *spec = codec->spec;
	int idx;

	for (idx = 0; idx < spec->num_pins; idx++)
		if (spec->pin_nids[idx] == dig0pin)
			break;
	if ((idx + 2) >= spec->num_pins)
		return 0;

	/* dig1pin case */
	if (stac_get_defcfg_connect(codec, idx + 1) != AC_JACK_PORT_NONE)
		return 2;

	/* dig0pin + dig2pin case */
	if (stac_get_defcfg_connect(codec, idx + 2) != AC_JACK_PORT_NONE)
		return 2;
	if (stac_get_defcfg_connect(codec, idx) != AC_JACK_PORT_NONE)
		return 1;
	else
		return 0;
}

/* HP dv7 bass switch - GPIO5 */
#define stac_hp_bass_gpio_info	snd_ctl_boolean_mono_info
static int stac_hp_bass_gpio_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	ucontrol->value.integer.value[0] = !!(spec->gpio_data & 0x20);
	return 0;
}

static int stac_hp_bass_gpio_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	unsigned int gpio_data;

	gpio_data = (spec->gpio_data & ~0x20) |
		(ucontrol->value.integer.value[0] ? 0x20 : 0);
	if (gpio_data == spec->gpio_data)
		return 0;
	spec->gpio_data = gpio_data;
	stac_gpio_set(codec, spec->gpio_mask, spec->gpio_dir, spec->gpio_data);
	return 1;
}

static const struct snd_kcontrol_new stac_hp_bass_sw_ctrl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = stac_hp_bass_gpio_info,
	.get = stac_hp_bass_gpio_get,
	.put = stac_hp_bass_gpio_put,
};

static int stac_add_hp_bass_switch(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	if (!stac_control_new(spec, &stac_hp_bass_sw_ctrl,
			      "Bass Speaker Playback Switch", 0))
		return -ENOMEM;

	spec->gpio_mask |= 0x20;
	spec->gpio_dir |= 0x20;
	spec->gpio_data |= 0x20;
	return 0;
}

static int patch_stac92hd71bxx(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	const struct hda_verb *unmute_init = stac92hd71bxx_unmute_core_init;
	unsigned int pin_cfg;
	int err = 0;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->no_trigger_sense = 1;
	codec->spec = spec;
	spec->linear_tone_beep = 0;
	codec->patch_ops = stac92xx_patch_ops;
	spec->num_pins = STAC92HD71BXX_NUM_PINS;
	switch (codec->vendor_id) {
	case 0x111d76b6:
	case 0x111d76b7:
		spec->pin_nids = stac92hd71bxx_pin_nids_4port;
		break;
	case 0x111d7603:
	case 0x111d7608:
		/* On 92HD75Bx 0x27 isn't a pin nid */
		spec->num_pins--;
		/* fallthrough */
	default:
		spec->pin_nids = stac92hd71bxx_pin_nids_6port;
	}
	spec->num_pwrs = ARRAY_SIZE(stac92hd71bxx_pwr_nids);
	spec->board_config = snd_hda_check_board_config(codec,
							STAC_92HD71BXX_MODELS,
							stac92hd71bxx_models,
							stac92hd71bxx_cfg_tbl);
again:
	if (spec->board_config < 0)
		snd_printdd(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
			    codec->chip_name);
	else
		stac92xx_set_config_regs(codec,
				stac92hd71bxx_brd_tbl[spec->board_config]);

	if (spec->board_config != STAC_92HD71BXX_REF) {
		/* GPIO0 = EAPD */
		spec->gpio_mask = 0x01;
		spec->gpio_dir = 0x01;
		spec->gpio_data = 0x01;
	}

	spec->dmic_nids = stac92hd71bxx_dmic_nids;
	spec->dmux_nids = stac92hd71bxx_dmux_nids;

	spec->num_caps = STAC92HD71BXX_NUM_CAPS;
	spec->capvols = stac92hd71bxx_capvols;
	spec->capsws = stac92hd71bxx_capsws;

	switch (codec->vendor_id) {
	case 0x111d76b6: /* 4 Port without Analog Mixer */
	case 0x111d76b7:
		unmute_init++;
		/* fallthru */
	case 0x111d76b4: /* 6 Port without Analog Mixer */
	case 0x111d76b5:
		spec->init = stac92hd71bxx_core_init;
		codec->slave_dig_outs = stac92hd71bxx_slave_dig_outs;
		spec->num_dmics = stac92xx_connected_ports(codec,
					stac92hd71bxx_dmic_nids,
					STAC92HD71BXX_NUM_DMICS);
		break;
	case 0x111d7608: /* 5 Port with Analog Mixer */
		switch (spec->board_config) {
		case STAC_HP_M4:
			/* Enable VREF power saving on GPIO1 detect */
			err = stac_add_event(codec, codec->afg,
					     STAC_VREF_EVENT, 0x02);
			if (err < 0)
				return err;
			snd_hda_codec_write_cache(codec, codec->afg, 0,
				AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK, 0x02);
			snd_hda_jack_detect_enable(codec, codec->afg, 0);
			spec->gpio_mask |= 0x02;
			break;
		}
		if ((codec->revision_id & 0xf) == 0 ||
		    (codec->revision_id & 0xf) == 1)
			spec->stream_delay = 40; /* 40 milliseconds */

		/* disable VSW */
		spec->init = stac92hd71bxx_core_init;
		unmute_init++;
		snd_hda_codec_set_pincfg(codec, 0x0f, 0x40f000f0);
		snd_hda_codec_set_pincfg(codec, 0x19, 0x40f000f3);
		spec->dmic_nids = stac92hd71bxx_dmic_5port_nids;
		spec->num_dmics = stac92xx_connected_ports(codec,
					stac92hd71bxx_dmic_5port_nids,
					STAC92HD71BXX_NUM_DMICS - 1);
		break;
	case 0x111d7603: /* 6 Port with Analog Mixer */
		if ((codec->revision_id & 0xf) == 1)
			spec->stream_delay = 40; /* 40 milliseconds */

		/* fallthru */
	default:
		spec->init = stac92hd71bxx_core_init;
		codec->slave_dig_outs = stac92hd71bxx_slave_dig_outs;
		spec->num_dmics = stac92xx_connected_ports(codec,
					stac92hd71bxx_dmic_nids,
					STAC92HD71BXX_NUM_DMICS);
		break;
	}

	if (get_wcaps(codec, 0xa) & AC_WCAP_IN_AMP)
		snd_hda_sequence_write_cache(codec, unmute_init);

	spec->aloopback_ctl = stac92hd71bxx_loopback;
	spec->aloopback_mask = 0x50;
	spec->aloopback_shift = 0;

	spec->powerdown_adcs = 1;
	spec->digbeep_nid = 0x26;
	spec->mux_nids = stac92hd71bxx_mux_nids;
	spec->adc_nids = stac92hd71bxx_adc_nids;
	spec->smux_nids = stac92hd71bxx_smux_nids;
	spec->pwr_nids = stac92hd71bxx_pwr_nids;

	spec->num_muxes = ARRAY_SIZE(stac92hd71bxx_mux_nids);
	spec->num_adcs = ARRAY_SIZE(stac92hd71bxx_adc_nids);
	spec->num_dmuxes = ARRAY_SIZE(stac92hd71bxx_dmux_nids);
	spec->num_smuxes = stac92hd71bxx_connected_smuxes(codec, 0x1e);

	snd_printdd("Found board config: %d\n", spec->board_config);

	switch (spec->board_config) {
	case STAC_HP_M4:
		/* enable internal microphone */
		snd_hda_codec_set_pincfg(codec, 0x0e, 0x01813040);
		stac92xx_auto_set_pinctl(codec, 0x0e,
			AC_PINCTL_IN_EN | AC_PINCTL_VREF_80);
		/* fallthru */
	case STAC_DELL_M4_2:
		spec->num_dmics = 0;
		spec->num_smuxes = 0;
		spec->num_dmuxes = 0;
		break;
	case STAC_DELL_M4_1:
	case STAC_DELL_M4_3:
		spec->num_dmics = 1;
		spec->num_smuxes = 0;
		spec->num_dmuxes = 1;
		break;
	case STAC_HP_DV4_1222NR:
		spec->num_dmics = 1;
		/* I don't know if it needs 1 or 2 smuxes - will wait for
		 * bug reports to fix if needed
		 */
		spec->num_smuxes = 1;
		spec->num_dmuxes = 1;
		/* fallthrough */
	case STAC_HP_DV4:
		spec->gpio_led = 0x01;
		/* fallthrough */
	case STAC_HP_DV5:
		snd_hda_codec_set_pincfg(codec, 0x0d, 0x90170010);
		stac92xx_auto_set_pinctl(codec, 0x0d, AC_PINCTL_OUT_EN);
		/* HP dv6 gives the headphone pin as a line-out.  Thus we
		 * need to set hp_detect flag here to force to enable HP
		 * detection.
		 */
		spec->hp_detect = 1;
		break;
	case STAC_HP_HDX:
		spec->num_dmics = 1;
		spec->num_dmuxes = 1;
		spec->num_smuxes = 1;
		spec->gpio_led = 0x08;
		break;
	}

	if (hp_blike_system(codec->subsystem_id)) {
		pin_cfg = snd_hda_codec_get_pincfg(codec, 0x0f);
		if (get_defcfg_device(pin_cfg) == AC_JACK_LINE_OUT ||
			get_defcfg_device(pin_cfg) == AC_JACK_SPEAKER  ||
			get_defcfg_device(pin_cfg) == AC_JACK_HP_OUT) {
			/* It was changed in the BIOS to just satisfy MS DTM.
			 * Lets turn it back into slaved HP
			 */
			pin_cfg = (pin_cfg & (~AC_DEFCFG_DEVICE))
					| (AC_JACK_HP_OUT <<
						AC_DEFCFG_DEVICE_SHIFT);
			pin_cfg = (pin_cfg & (~(AC_DEFCFG_DEF_ASSOC
							| AC_DEFCFG_SEQUENCE)))
								| 0x1f;
			snd_hda_codec_set_pincfg(codec, 0x0f, pin_cfg);
		}
	}

	if (find_mute_led_cfg(codec, 1))
		snd_printd("mute LED gpio %d polarity %d\n",
				spec->gpio_led,
				spec->gpio_led_polarity);

	if (spec->gpio_led) {
		if (!spec->vref_mute_led_nid) {
			spec->gpio_mask |= spec->gpio_led;
			spec->gpio_dir |= spec->gpio_led;
			spec->gpio_data |= spec->gpio_led;
		} else {
			codec->patch_ops.set_power_state =
					stac92xx_set_power_state;
		}
#ifdef CONFIG_PM
		codec->patch_ops.pre_resume = stac92xx_pre_resume;
#endif
	}

	spec->multiout.dac_nids = spec->dac_nids;

	err = stac92xx_parse_auto_config(codec);
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

	/* enable bass on HP dv7 */
	if (spec->board_config == STAC_HP_DV4 ||
	    spec->board_config == STAC_HP_DV5) {
		unsigned int cap;
		cap = snd_hda_param_read(codec, 0x1, AC_PAR_GPIO_CAP);
		cap &= AC_GPIO_IO_COUNT;
		if (cap >= 6)
			stac_add_hp_bass_switch(codec);
	}

	codec->proc_widget_hook = stac92hd7x_proc_hook;

	return 0;
}

static int patch_stac922x(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->no_trigger_sense = 1;
	codec->spec = spec;
	spec->linear_tone_beep = 1;
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
	if (spec->board_config < 0)
		snd_printdd(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
			    codec->chip_name);
	else
		stac92xx_set_config_regs(codec,
				stac922x_brd_tbl[spec->board_config]);

	spec->adc_nids = stac922x_adc_nids;
	spec->mux_nids = stac922x_mux_nids;
	spec->num_muxes = ARRAY_SIZE(stac922x_mux_nids);
	spec->num_adcs = ARRAY_SIZE(stac922x_adc_nids);
	spec->num_dmics = 0;
	spec->num_pwrs = 0;

	spec->init = stac922x_core_init;

	spec->num_caps = STAC922X_NUM_CAPS;
	spec->capvols = stac922x_capvols;
	spec->capsws = stac922x_capsws;

	spec->multiout.dac_nids = spec->dac_nids;
	
	err = stac92xx_parse_auto_config(codec);
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

	codec->no_trigger_sense = 1;
	codec->spec = spec;
	spec->linear_tone_beep = 1;
	codec->slave_dig_outs = stac927x_slave_dig_outs;
	spec->num_pins = ARRAY_SIZE(stac927x_pin_nids);
	spec->pin_nids = stac927x_pin_nids;
	spec->board_config = snd_hda_check_board_config(codec, STAC_927X_MODELS,
							stac927x_models,
							stac927x_cfg_tbl);
 again:
	if (spec->board_config < 0)
		snd_printdd(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
			    codec->chip_name);
	else
		stac92xx_set_config_regs(codec,
				stac927x_brd_tbl[spec->board_config]);

	spec->digbeep_nid = 0x23;
	spec->adc_nids = stac927x_adc_nids;
	spec->num_adcs = ARRAY_SIZE(stac927x_adc_nids);
	spec->mux_nids = stac927x_mux_nids;
	spec->num_muxes = ARRAY_SIZE(stac927x_mux_nids);
	spec->smux_nids = stac927x_smux_nids;
	spec->num_smuxes = ARRAY_SIZE(stac927x_smux_nids);
	spec->spdif_labels = stac927x_spdif_labels;
	spec->dac_list = stac927x_dac_nids;
	spec->multiout.dac_nids = spec->dac_nids;

	if (spec->board_config != STAC_D965_REF) {
		/* GPIO0 High = Enable EAPD */
		spec->eapd_mask = spec->gpio_mask = 0x01;
		spec->gpio_dir = spec->gpio_data = 0x01;
	}

	switch (spec->board_config) {
	case STAC_D965_3ST:
	case STAC_D965_5ST:
		/* GPIO0 High = Enable EAPD */
		spec->num_dmics = 0;
		spec->init = d965_core_init;
		break;
	case STAC_DELL_BIOS:
		switch (codec->subsystem_id) {
		case 0x10280209:
		case 0x1028022e:
			/* correct the device field to SPDIF out */
			snd_hda_codec_set_pincfg(codec, 0x21, 0x01442070);
			break;
		}
		/* configure the analog microphone on some laptops */
		snd_hda_codec_set_pincfg(codec, 0x0c, 0x90a79130);
		/* correct the front output jack as a hp out */
		snd_hda_codec_set_pincfg(codec, 0x0f, 0x0227011f);
		/* correct the front input jack as a mic */
		snd_hda_codec_set_pincfg(codec, 0x0e, 0x02a79130);
		/* fallthru */
	case STAC_DELL_3ST:
		if (codec->subsystem_id != 0x1028022f) {
			/* GPIO2 High = Enable EAPD */
			spec->eapd_mask = spec->gpio_mask = 0x04;
			spec->gpio_dir = spec->gpio_data = 0x04;
		}
		spec->dmic_nids = stac927x_dmic_nids;
		spec->num_dmics = STAC927X_NUM_DMICS;

		spec->init = dell_3st_core_init;
		spec->dmux_nids = stac927x_dmux_nids;
		spec->num_dmuxes = ARRAY_SIZE(stac927x_dmux_nids);
		break;
	case STAC_927X_VOLKNOB:
		spec->num_dmics = 0;
		spec->init = stac927x_volknob_core_init;
		break;
	default:
		spec->num_dmics = 0;
		spec->init = stac927x_core_init;
		break;
	}

	spec->num_caps = STAC927X_NUM_CAPS;
	spec->capvols = stac927x_capvols;
	spec->capsws = stac927x_capsws;

	spec->num_pwrs = 0;
	spec->aloopback_ctl = stac927x_loopback;
	spec->aloopback_mask = 0x40;
	spec->aloopback_shift = 0;
	spec->eapd_switch = 1;

	err = stac92xx_parse_auto_config(codec);
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

	codec->proc_widget_hook = stac927x_proc_hook;

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

	/* no jack detecion for ref-no-jd model */
	if (spec->board_config == STAC_D965_REF_NO_JD)
		spec->hp_detect = 0;

	return 0;
}

static int patch_stac9205(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->no_trigger_sense = 1;
	codec->spec = spec;
	spec->linear_tone_beep = 1;
	spec->num_pins = ARRAY_SIZE(stac9205_pin_nids);
	spec->pin_nids = stac9205_pin_nids;
	spec->board_config = snd_hda_check_board_config(codec, STAC_9205_MODELS,
							stac9205_models,
							stac9205_cfg_tbl);
 again:
	if (spec->board_config < 0)
		snd_printdd(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
			    codec->chip_name);
	else
		stac92xx_set_config_regs(codec,
					 stac9205_brd_tbl[spec->board_config]);

	spec->digbeep_nid = 0x23;
	spec->adc_nids = stac9205_adc_nids;
	spec->num_adcs = ARRAY_SIZE(stac9205_adc_nids);
	spec->mux_nids = stac9205_mux_nids;
	spec->num_muxes = ARRAY_SIZE(stac9205_mux_nids);
	spec->smux_nids = stac9205_smux_nids;
	spec->num_smuxes = ARRAY_SIZE(stac9205_smux_nids);
	spec->dmic_nids = stac9205_dmic_nids;
	spec->num_dmics = STAC9205_NUM_DMICS;
	spec->dmux_nids = stac9205_dmux_nids;
	spec->num_dmuxes = ARRAY_SIZE(stac9205_dmux_nids);
	spec->num_pwrs = 0;

	spec->init = stac9205_core_init;
	spec->aloopback_ctl = stac9205_loopback;

	spec->num_caps = STAC9205_NUM_CAPS;
	spec->capvols = stac9205_capvols;
	spec->capsws = stac9205_capsws;

	spec->aloopback_mask = 0x40;
	spec->aloopback_shift = 0;
	/* Turn on/off EAPD per HP plugging */
	if (spec->board_config != STAC_9205_EAPD)
		spec->eapd_switch = 1;
	spec->multiout.dac_nids = spec->dac_nids;
	
	switch (spec->board_config){
	case STAC_9205_DELL_M43:
		/* Enable SPDIF in/out */
		snd_hda_codec_set_pincfg(codec, 0x1f, 0x01441030);
		snd_hda_codec_set_pincfg(codec, 0x20, 0x1c410030);

		/* Enable unsol response for GPIO4/Dock HP connection */
		err = stac_add_event(codec, codec->afg, STAC_VREF_EVENT, 0x01);
		if (err < 0)
			return err;
		snd_hda_codec_write_cache(codec, codec->afg, 0,
			AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK, 0x10);
		snd_hda_jack_detect_enable(codec, codec->afg, 0);

		spec->gpio_dir = 0x0b;
		spec->eapd_mask = 0x01;
		spec->gpio_mask = 0x1b;
		spec->gpio_mute = 0x10;
		/* GPIO0 High = EAPD, GPIO1 Low = Headphone Mute,
		 * GPIO3 Low = DRM
		 */
		spec->gpio_data = 0x01;
		break;
	case STAC_9205_REF:
		/* SPDIF-In enabled */
		break;
	default:
		/* GPIO0 High = EAPD */
		spec->eapd_mask = spec->gpio_mask = spec->gpio_dir = 0x1;
		spec->gpio_data = 0x01;
		break;
	}

	err = stac92xx_parse_auto_config(codec);
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

	codec->proc_widget_hook = stac9205_proc_hook;

	return 0;
}

/*
 * STAC9872 hack
 */

static const struct hda_verb stac9872_core_init[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x1}, /* mic-sel: 0a,0d,14,02 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, /* Mic-in -> 0x9 */
	{}
};

static const hda_nid_t stac9872_pin_nids[] = {
	0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x11, 0x13, 0x14,
};

static const hda_nid_t stac9872_adc_nids[] = {
	0x8 /*,0x6*/
};

static const hda_nid_t stac9872_mux_nids[] = {
	0x15
};

static const unsigned long stac9872_capvols[] = {
	HDA_COMPOSE_AMP_VAL(0x09, 3, 0, HDA_INPUT),
};
#define stac9872_capsws		stac9872_capvols

static const unsigned int stac9872_vaio_pin_configs[9] = {
	0x03211020, 0x411111f0, 0x411111f0, 0x03a15030,
	0x411111f0, 0x90170110, 0x411111f0, 0x411111f0,
	0x90a7013e
};

static const char * const stac9872_models[STAC_9872_MODELS] = {
	[STAC_9872_AUTO] = "auto",
	[STAC_9872_VAIO] = "vaio",
};

static const unsigned int *stac9872_brd_tbl[STAC_9872_MODELS] = {
	[STAC_9872_VAIO] = stac9872_vaio_pin_configs,
};

static const struct snd_pci_quirk stac9872_cfg_tbl[] = {
	SND_PCI_QUIRK_MASK(0x104d, 0xfff0, 0x81e0,
			   "Sony VAIO F/S", STAC_9872_VAIO),
	{} /* terminator */
};

static int patch_stac9872(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	spec  = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;
	codec->no_trigger_sense = 1;
	codec->spec = spec;
	spec->linear_tone_beep = 1;
	spec->num_pins = ARRAY_SIZE(stac9872_pin_nids);
	spec->pin_nids = stac9872_pin_nids;

	spec->board_config = snd_hda_check_board_config(codec, STAC_9872_MODELS,
							stac9872_models,
							stac9872_cfg_tbl);
	if (spec->board_config < 0)
		snd_printdd(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
			    codec->chip_name);
	else
		stac92xx_set_config_regs(codec,
					 stac9872_brd_tbl[spec->board_config]);

	spec->multiout.dac_nids = spec->dac_nids;
	spec->num_adcs = ARRAY_SIZE(stac9872_adc_nids);
	spec->adc_nids = stac9872_adc_nids;
	spec->num_muxes = ARRAY_SIZE(stac9872_mux_nids);
	spec->mux_nids = stac9872_mux_nids;
	spec->init = stac9872_core_init;
	spec->num_caps = 1;
	spec->capvols = stac9872_capvols;
	spec->capsws = stac9872_capsws;

	err = stac92xx_parse_auto_config(codec);
	if (err < 0) {
		stac92xx_free(codec);
		return -EINVAL;
	}
	spec->input_mux = &spec->private_imux;
	codec->patch_ops = stac92xx_patch_ops;
	return 0;
}


/*
 * patch entries
 */
static const struct hda_codec_preset snd_hda_preset_sigmatel[] = {
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
	{ .id = 0x83847698, .name = "STAC9205", .patch = patch_stac9205 },
 	{ .id = 0x838476a0, .name = "STAC9205", .patch = patch_stac9205 },
 	{ .id = 0x838476a1, .name = "STAC9205D", .patch = patch_stac9205 },
 	{ .id = 0x838476a2, .name = "STAC9204", .patch = patch_stac9205 },
 	{ .id = 0x838476a3, .name = "STAC9204D", .patch = patch_stac9205 },
 	{ .id = 0x838476a4, .name = "STAC9255", .patch = patch_stac9205 },
 	{ .id = 0x838476a5, .name = "STAC9255D", .patch = patch_stac9205 },
 	{ .id = 0x838476a6, .name = "STAC9254", .patch = patch_stac9205 },
 	{ .id = 0x838476a7, .name = "STAC9254D", .patch = patch_stac9205 },
	{ .id = 0x111d7603, .name = "92HD75B3X5", .patch = patch_stac92hd71bxx},
	{ .id = 0x111d7604, .name = "92HD83C1X5", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76d4, .name = "92HD83C1C5", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d7605, .name = "92HD81B1X5", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76d5, .name = "92HD81B1C5", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76d1, .name = "92HD87B1/3", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76d9, .name = "92HD87B2/4", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d7666, .name = "92HD88B3", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d7667, .name = "92HD88B1", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d7668, .name = "92HD88B2", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d7669, .name = "92HD88B4", .patch = patch_stac92hd83xxx},
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
	{ .id = 0x111d76c0, .name = "92HD89C3", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76c1, .name = "92HD89C2", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76c2, .name = "92HD89C1", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76c3, .name = "92HD89B3", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76c4, .name = "92HD89B2", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76c5, .name = "92HD89B1", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76c6, .name = "92HD89E3", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76c7, .name = "92HD89E2", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76c8, .name = "92HD89E1", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76c9, .name = "92HD89D3", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76ca, .name = "92HD89D2", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76cb, .name = "92HD89D1", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76cc, .name = "92HD89F3", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76cd, .name = "92HD89F2", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76ce, .name = "92HD89F1", .patch = patch_stac92hd73xx },
	{ .id = 0x111d76df, .name = "92HD93BXX", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76e0, .name = "92HD91BXX", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76e3, .name = "92HD98BXX", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76e5, .name = "92HD99BXX", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76e7, .name = "92HD90BXX", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76e8, .name = "92HD66B1X5", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76e9, .name = "92HD66B2X5", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76ea, .name = "92HD66B3X5", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76eb, .name = "92HD66C1X5", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76ec, .name = "92HD66C2X5", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76ed, .name = "92HD66C3X5", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76ee, .name = "92HD66B1X3", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76ef, .name = "92HD66B2X3", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76f0, .name = "92HD66B3X3", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76f1, .name = "92HD66C1X3", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76f2, .name = "92HD66C2X3", .patch = patch_stac92hd83xxx},
	{ .id = 0x111d76f3, .name = "92HD66C3/65", .patch = patch_stac92hd83xxx},
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:8384*");
MODULE_ALIAS("snd-hda-codec-id:111d*");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IDT/Sigmatel HD-audio codec");

static struct hda_codec_preset_list sigmatel_list = {
	.preset = snd_hda_preset_sigmatel,
	.owner = THIS_MODULE,
};

static int __init patch_sigmatel_init(void)
{
	return snd_hda_add_codec_preset(&sigmatel_list);
}

static void __exit patch_sigmatel_exit(void)
{
	snd_hda_delete_codec_preset(&sigmatel_list);
}

module_init(patch_sigmatel_init)
module_exit(patch_sigmatel_exit)
