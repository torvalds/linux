/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * HD audio interface patch for ALC 260/880/882 codecs
 *
 * Copyright (c) 2004 Kailang Yang <kailang@realtek.com.tw>
 *                    PeiSen Hou <pshou@realtek.com.tw>
 *                    Takashi Iwai <tiwai@suse.de>
 *                    Jonathan Woithe <jwoithe@physics.adelaide.edu.au>
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
#include "hda_patch.h"

#define ALC880_FRONT_EVENT		0x01
#define ALC880_DCVOL_EVENT		0x02
#define ALC880_HP_EVENT			0x04
#define ALC880_MIC_EVENT		0x08

/* ALC880 board config type */
enum {
	ALC880_3ST,
	ALC880_3ST_DIG,
	ALC880_5ST,
	ALC880_5ST_DIG,
	ALC880_W810,
	ALC880_Z71V,
	ALC880_6ST,
	ALC880_6ST_DIG,
	ALC880_F1734,
	ALC880_ASUS,
	ALC880_ASUS_DIG,
	ALC880_ASUS_W1V,
	ALC880_ASUS_DIG2,
	ALC880_FUJITSU,
	ALC880_UNIWILL_DIG,
	ALC880_UNIWILL,
	ALC880_UNIWILL_P53,
	ALC880_CLEVO,
	ALC880_TCL_S700,
	ALC880_LG,
	ALC880_LG_LW,
	ALC880_MEDION_RIM,
#ifdef CONFIG_SND_DEBUG
	ALC880_TEST,
#endif
	ALC880_AUTO,
	ALC880_MODEL_LAST /* last tag */
};

/* ALC260 models */
enum {
	ALC260_BASIC,
	ALC260_HP,
	ALC260_HP_DC7600,
	ALC260_HP_3013,
	ALC260_FUJITSU_S702X,
	ALC260_ACER,
	ALC260_WILL,
	ALC260_REPLACER_672V,
#ifdef CONFIG_SND_DEBUG
	ALC260_TEST,
#endif
	ALC260_AUTO,
	ALC260_MODEL_LAST /* last tag */
};

/* ALC262 models */
enum {
	ALC262_BASIC,
	ALC262_HIPPO,
	ALC262_HIPPO_1,
	ALC262_FUJITSU,
	ALC262_HP_BPC,
	ALC262_HP_BPC_D7000_WL,
	ALC262_HP_BPC_D7000_WF,
	ALC262_HP_TC_T5735,
	ALC262_HP_RP5700,
	ALC262_BENQ_ED8,
	ALC262_SONY_ASSAMD,
	ALC262_BENQ_T31,
	ALC262_ULTRA,
	ALC262_LENOVO_3000,
	ALC262_NEC,
	ALC262_TOSHIBA_S06,
	ALC262_AUTO,
	ALC262_MODEL_LAST /* last tag */
};

/* ALC268 models */
enum {
	ALC267_QUANTA_IL1,
	ALC268_3ST,
	ALC268_TOSHIBA,
	ALC268_ACER,
	ALC268_ACER_ASPIRE_ONE,
	ALC268_DELL,
	ALC268_ZEPTO,
#ifdef CONFIG_SND_DEBUG
	ALC268_TEST,
#endif
	ALC268_AUTO,
	ALC268_MODEL_LAST /* last tag */
};

/* ALC269 models */
enum {
	ALC269_BASIC,
	ALC269_QUANTA_FL1,
	ALC269_ASUS_EEEPC_P703,
	ALC269_ASUS_EEEPC_P901,
	ALC269_AUTO,
	ALC269_MODEL_LAST /* last tag */
};

/* ALC861 models */
enum {
	ALC861_3ST,
	ALC660_3ST,
	ALC861_3ST_DIG,
	ALC861_6ST_DIG,
	ALC861_UNIWILL_M31,
	ALC861_TOSHIBA,
	ALC861_ASUS,
	ALC861_ASUS_LAPTOP,
	ALC861_AUTO,
	ALC861_MODEL_LAST,
};

/* ALC861-VD models */
enum {
	ALC660VD_3ST,
	ALC660VD_3ST_DIG,
	ALC861VD_3ST,
	ALC861VD_3ST_DIG,
	ALC861VD_6ST_DIG,
	ALC861VD_LENOVO,
	ALC861VD_DALLAS,
	ALC861VD_HP,
	ALC861VD_AUTO,
	ALC861VD_MODEL_LAST,
};

/* ALC662 models */
enum {
	ALC662_3ST_2ch_DIG,
	ALC662_3ST_6ch_DIG,
	ALC662_3ST_6ch,
	ALC662_5ST_DIG,
	ALC662_LENOVO_101E,
	ALC662_ASUS_EEEPC_P701,
	ALC662_ASUS_EEEPC_EP20,
	ALC663_ASUS_M51VA,
	ALC663_ASUS_G71V,
	ALC663_ASUS_H13,
	ALC663_ASUS_G50V,
	ALC662_ECS,
	ALC663_ASUS_MODE1,
	ALC662_ASUS_MODE2,
	ALC663_ASUS_MODE3,
	ALC663_ASUS_MODE4,
	ALC663_ASUS_MODE5,
	ALC663_ASUS_MODE6,
	ALC662_AUTO,
	ALC662_MODEL_LAST,
};

/* ALC882 models */
enum {
	ALC882_3ST_DIG,
	ALC882_6ST_DIG,
	ALC882_ARIMA,
	ALC882_W2JC,
	ALC882_TARGA,
	ALC882_ASUS_A7J,
	ALC882_ASUS_A7M,
	ALC885_MACPRO,
	ALC885_MBP3,
	ALC885_IMAC24,
	ALC882_AUTO,
	ALC882_MODEL_LAST,
};

/* ALC883 models */
enum {
	ALC883_3ST_2ch_DIG,
	ALC883_3ST_6ch_DIG,
	ALC883_3ST_6ch,
	ALC883_6ST_DIG,
	ALC883_TARGA_DIG,
	ALC883_TARGA_2ch_DIG,
	ALC883_ACER,
	ALC883_ACER_ASPIRE,
	ALC883_MEDION,
	ALC883_MEDION_MD2,
	ALC883_LAPTOP_EAPD,
	ALC883_LENOVO_101E_2ch,
	ALC883_LENOVO_NB0763,
	ALC888_LENOVO_MS7195_DIG,
	ALC888_LENOVO_SKY,
	ALC883_HAIER_W66,
	ALC888_3ST_HP,
	ALC888_6ST_DELL,
	ALC883_MITAC,
	ALC883_CLEVO_M720,
	ALC883_FUJITSU_PI2515,
	ALC883_3ST_6ch_INTEL,
	ALC888_ASUS_M90V,
	ALC888_ASUS_EEE1601,
	ALC883_AUTO,
	ALC883_MODEL_LAST,
};

/* for GPIO Poll */
#define GPIO_MASK	0x03

struct alc_spec {
	/* codec parameterization */
	struct snd_kcontrol_new *mixers[5];	/* mixer arrays */
	unsigned int num_mixers;

	const struct hda_verb *init_verbs[5];	/* initialization verbs
						 * don't forget NULL
						 * termination!
						 */
	unsigned int num_init_verbs;

	char *stream_name_analog;	/* analog PCM stream */
	struct hda_pcm_stream *stream_analog_playback;
	struct hda_pcm_stream *stream_analog_capture;
	struct hda_pcm_stream *stream_analog_alt_playback;
	struct hda_pcm_stream *stream_analog_alt_capture;

	char *stream_name_digital;	/* digital PCM stream */
	struct hda_pcm_stream *stream_digital_playback;
	struct hda_pcm_stream *stream_digital_capture;

	/* playback */
	struct hda_multi_out multiout;	/* playback set-up
					 * max_channels, dacs must be set
					 * dig_out_nid and hp_nid are optional
					 */
	hda_nid_t alt_dac_nid;

	/* capture */
	unsigned int num_adc_nids;
	hda_nid_t *adc_nids;
	hda_nid_t *capsrc_nids;
	hda_nid_t dig_in_nid;		/* digital-in NID; optional */

	/* capture source */
	unsigned int num_mux_defs;
	const struct hda_input_mux *input_mux;
	unsigned int cur_mux[3];

	/* channel model */
	const struct hda_channel_mode *channel_mode;
	int num_channel_mode;
	int need_dac_fix;

	/* PCM information */
	struct hda_pcm pcm_rec[3];	/* used in alc_build_pcms() */

	/* dynamic controls, init_verbs and input_mux */
	struct auto_pin_cfg autocfg;
	unsigned int num_kctl_alloc, num_kctl_used;
	struct snd_kcontrol_new *kctl_alloc;
	struct hda_input_mux private_imux;
	hda_nid_t private_dac_nids[AUTO_CFG_MAX_OUTS];

	/* hooks */
	void (*init_hook)(struct hda_codec *codec);
	void (*unsol_event)(struct hda_codec *codec, unsigned int res);

	/* for pin sensing */
	unsigned int sense_updated: 1;
	unsigned int jack_present: 1;
	unsigned int master_sw: 1;

	/* for virtual master */
	hda_nid_t vmaster_nid;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	struct hda_loopback_check loopback;
#endif

	/* for PLL fix */
	hda_nid_t pll_nid;
	unsigned int pll_coef_idx, pll_coef_bit;
};

/*
 * configuration template - to be copied to the spec instance
 */
struct alc_config_preset {
	struct snd_kcontrol_new *mixers[5]; /* should be identical size
					     * with spec
					     */
	const struct hda_verb *init_verbs[5];
	unsigned int num_dacs;
	hda_nid_t *dac_nids;
	hda_nid_t dig_out_nid;		/* optional */
	hda_nid_t hp_nid;		/* optional */
	unsigned int num_adc_nids;
	hda_nid_t *adc_nids;
	hda_nid_t *capsrc_nids;
	hda_nid_t dig_in_nid;
	unsigned int num_channel_mode;
	const struct hda_channel_mode *channel_mode;
	int need_dac_fix;
	unsigned int num_mux_defs;
	const struct hda_input_mux *input_mux;
	void (*unsol_event)(struct hda_codec *, unsigned int);
	void (*init_hook)(struct hda_codec *);
#ifdef CONFIG_SND_HDA_POWER_SAVE
	struct hda_amp_list *loopbacks;
#endif
};


/*
 * input MUX handling
 */
static int alc_mux_enum_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	unsigned int mux_idx = snd_ctl_get_ioffidx(kcontrol, &uinfo->id);
	if (mux_idx >= spec->num_mux_defs)
		mux_idx = 0;
	return snd_hda_input_mux_info(&spec->input_mux[mux_idx], uinfo);
}

static int alc_mux_enum_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_mux[adc_idx];
	return 0;
}

static int alc_mux_enum_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	unsigned int mux_idx = adc_idx >= spec->num_mux_defs ? 0 : adc_idx;
	hda_nid_t nid = spec->capsrc_nids ?
		spec->capsrc_nids[adc_idx] : spec->adc_nids[adc_idx];
	return snd_hda_input_mux_put(codec, &spec->input_mux[mux_idx], ucontrol,
				     nid, &spec->cur_mux[adc_idx]);
}


/*
 * channel mode setting
 */
static int alc_ch_mode_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	return snd_hda_ch_mode_info(codec, uinfo, spec->channel_mode,
				    spec->num_channel_mode);
}

static int alc_ch_mode_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	return snd_hda_ch_mode_get(codec, ucontrol, spec->channel_mode,
				   spec->num_channel_mode,
				   spec->multiout.max_channels);
}

static int alc_ch_mode_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int err = snd_hda_ch_mode_put(codec, ucontrol, spec->channel_mode,
				      spec->num_channel_mode,
				      &spec->multiout.max_channels);
	if (err >= 0 && spec->need_dac_fix)
		spec->multiout.num_dacs = spec->multiout.max_channels / 2;
	return err;
}

/*
 * Control the mode of pin widget settings via the mixer.  "pc" is used
 * instead of "%" to avoid consequences of accidently treating the % as
 * being part of a format specifier.  Maximum allowed length of a value is
 * 63 characters plus NULL terminator.
 *
 * Note: some retasking pin complexes seem to ignore requests for input
 * states other than HiZ (eg: PIN_VREFxx) and revert to HiZ if any of these
 * are requested.  Therefore order this list so that this behaviour will not
 * cause problems when mixer clients move through the enum sequentially.
 * NIDs 0x0f and 0x10 have been observed to have this behaviour as of
 * March 2006.
 */
static char *alc_pin_mode_names[] = {
	"Mic 50pc bias", "Mic 80pc bias",
	"Line in", "Line out", "Headphone out",
};
static unsigned char alc_pin_mode_values[] = {
	PIN_VREF50, PIN_VREF80, PIN_IN, PIN_OUT, PIN_HP,
};
/* The control can present all 5 options, or it can limit the options based
 * in the pin being assumed to be exclusively an input or an output pin.  In
 * addition, "input" pins may or may not process the mic bias option
 * depending on actual widget capability (NIDs 0x0f and 0x10 don't seem to
 * accept requests for bias as of chip versions up to March 2006) and/or
 * wiring in the computer.
 */
#define ALC_PIN_DIR_IN              0x00
#define ALC_PIN_DIR_OUT             0x01
#define ALC_PIN_DIR_INOUT           0x02
#define ALC_PIN_DIR_IN_NOMICBIAS    0x03
#define ALC_PIN_DIR_INOUT_NOMICBIAS 0x04

/* Info about the pin modes supported by the different pin direction modes.
 * For each direction the minimum and maximum values are given.
 */
static signed char alc_pin_mode_dir_info[5][2] = {
	{ 0, 2 },    /* ALC_PIN_DIR_IN */
	{ 3, 4 },    /* ALC_PIN_DIR_OUT */
	{ 0, 4 },    /* ALC_PIN_DIR_INOUT */
	{ 2, 2 },    /* ALC_PIN_DIR_IN_NOMICBIAS */
	{ 2, 4 },    /* ALC_PIN_DIR_INOUT_NOMICBIAS */
};
#define alc_pin_mode_min(_dir) (alc_pin_mode_dir_info[_dir][0])
#define alc_pin_mode_max(_dir) (alc_pin_mode_dir_info[_dir][1])
#define alc_pin_mode_n_items(_dir) \
	(alc_pin_mode_max(_dir)-alc_pin_mode_min(_dir)+1)

static int alc_pin_mode_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	unsigned int item_num = uinfo->value.enumerated.item;
	unsigned char dir = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = alc_pin_mode_n_items(dir);

	if (item_num<alc_pin_mode_min(dir) || item_num>alc_pin_mode_max(dir))
		item_num = alc_pin_mode_min(dir);
	strcpy(uinfo->value.enumerated.name, alc_pin_mode_names[item_num]);
	return 0;
}

static int alc_pin_mode_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	unsigned int i;
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char dir = (kcontrol->private_value >> 16) & 0xff;
	long *valp = ucontrol->value.integer.value;
	unsigned int pinctl = snd_hda_codec_read(codec, nid, 0,
						 AC_VERB_GET_PIN_WIDGET_CONTROL,
						 0x00);

	/* Find enumerated value for current pinctl setting */
	i = alc_pin_mode_min(dir);
	while (alc_pin_mode_values[i] != pinctl && i <= alc_pin_mode_max(dir))
		i++;
	*valp = i <= alc_pin_mode_max(dir) ? i: alc_pin_mode_min(dir);
	return 0;
}

static int alc_pin_mode_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	signed int change;
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char dir = (kcontrol->private_value >> 16) & 0xff;
	long val = *ucontrol->value.integer.value;
	unsigned int pinctl = snd_hda_codec_read(codec, nid, 0,
						 AC_VERB_GET_PIN_WIDGET_CONTROL,
						 0x00);

	if (val < alc_pin_mode_min(dir) || val > alc_pin_mode_max(dir))
		val = alc_pin_mode_min(dir);

	change = pinctl != alc_pin_mode_values[val];
	if (change) {
		/* Set pin mode to that requested */
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_PIN_WIDGET_CONTROL,
					  alc_pin_mode_values[val]);

		/* Also enable the retasking pin's input/output as required
		 * for the requested pin mode.  Enum values of 2 or less are
		 * input modes.
		 *
		 * Dynamically switching the input/output buffers probably
		 * reduces noise slightly (particularly on input) so we'll
		 * do it.  However, having both input and output buffers
		 * enabled simultaneously doesn't seem to be problematic if
		 * this turns out to be necessary in the future.
		 */
		if (val <= 2) {
			snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE, HDA_AMP_MUTE);
			snd_hda_codec_amp_stereo(codec, nid, HDA_INPUT, 0,
						 HDA_AMP_MUTE, 0);
		} else {
			snd_hda_codec_amp_stereo(codec, nid, HDA_INPUT, 0,
						 HDA_AMP_MUTE, HDA_AMP_MUTE);
			snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE, 0);
		}
	}
	return change;
}

#define ALC_PIN_MODE(xname, nid, dir) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0,  \
	  .info = alc_pin_mode_info, \
	  .get = alc_pin_mode_get, \
	  .put = alc_pin_mode_put, \
	  .private_value = nid | (dir<<16) }

/* A switch control for ALC260 GPIO pins.  Multiple GPIOs can be ganged
 * together using a mask with more than one bit set.  This control is
 * currently used only by the ALC260 test model.  At this stage they are not
 * needed for any "production" models.
 */
#ifdef CONFIG_SND_DEBUG
#define alc_gpio_data_info	snd_ctl_boolean_mono_info

static int alc_gpio_data_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char mask = (kcontrol->private_value >> 16) & 0xff;
	long *valp = ucontrol->value.integer.value;
	unsigned int val = snd_hda_codec_read(codec, nid, 0,
					      AC_VERB_GET_GPIO_DATA, 0x00);

	*valp = (val & mask) != 0;
	return 0;
}
static int alc_gpio_data_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	signed int change;
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char mask = (kcontrol->private_value >> 16) & 0xff;
	long val = *ucontrol->value.integer.value;
	unsigned int gpio_data = snd_hda_codec_read(codec, nid, 0,
						    AC_VERB_GET_GPIO_DATA,
						    0x00);

	/* Set/unset the masked GPIO bit(s) as needed */
	change = (val == 0 ? 0 : mask) != (gpio_data & mask);
	if (val == 0)
		gpio_data &= ~mask;
	else
		gpio_data |= mask;
	snd_hda_codec_write_cache(codec, nid, 0,
				  AC_VERB_SET_GPIO_DATA, gpio_data);

	return change;
}
#define ALC_GPIO_DATA_SWITCH(xname, nid, mask) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0,  \
	  .info = alc_gpio_data_info, \
	  .get = alc_gpio_data_get, \
	  .put = alc_gpio_data_put, \
	  .private_value = nid | (mask<<16) }
#endif   /* CONFIG_SND_DEBUG */

/* A switch control to allow the enabling of the digital IO pins on the
 * ALC260.  This is incredibly simplistic; the intention of this control is
 * to provide something in the test model allowing digital outputs to be
 * identified if present.  If models are found which can utilise these
 * outputs a more complete mixer control can be devised for those models if
 * necessary.
 */
#ifdef CONFIG_SND_DEBUG
#define alc_spdif_ctrl_info	snd_ctl_boolean_mono_info

static int alc_spdif_ctrl_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char mask = (kcontrol->private_value >> 16) & 0xff;
	long *valp = ucontrol->value.integer.value;
	unsigned int val = snd_hda_codec_read(codec, nid, 0,
					      AC_VERB_GET_DIGI_CONVERT_1, 0x00);

	*valp = (val & mask) != 0;
	return 0;
}
static int alc_spdif_ctrl_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	signed int change;
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char mask = (kcontrol->private_value >> 16) & 0xff;
	long val = *ucontrol->value.integer.value;
	unsigned int ctrl_data = snd_hda_codec_read(codec, nid, 0,
						    AC_VERB_GET_DIGI_CONVERT_1,
						    0x00);

	/* Set/unset the masked control bit(s) as needed */
	change = (val == 0 ? 0 : mask) != (ctrl_data & mask);
	if (val==0)
		ctrl_data &= ~mask;
	else
		ctrl_data |= mask;
	snd_hda_codec_write_cache(codec, nid, 0, AC_VERB_SET_DIGI_CONVERT_1,
				  ctrl_data);

	return change;
}
#define ALC_SPDIF_CTRL_SWITCH(xname, nid, mask) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0,  \
	  .info = alc_spdif_ctrl_info, \
	  .get = alc_spdif_ctrl_get, \
	  .put = alc_spdif_ctrl_put, \
	  .private_value = nid | (mask<<16) }
#endif   /* CONFIG_SND_DEBUG */

/* A switch control to allow the enabling EAPD digital outputs on the ALC26x.
 * Again, this is only used in the ALC26x test models to help identify when
 * the EAPD line must be asserted for features to work.
 */
#ifdef CONFIG_SND_DEBUG
#define alc_eapd_ctrl_info	snd_ctl_boolean_mono_info

static int alc_eapd_ctrl_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char mask = (kcontrol->private_value >> 16) & 0xff;
	long *valp = ucontrol->value.integer.value;
	unsigned int val = snd_hda_codec_read(codec, nid, 0,
					      AC_VERB_GET_EAPD_BTLENABLE, 0x00);

	*valp = (val & mask) != 0;
	return 0;
}

static int alc_eapd_ctrl_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	int change;
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = kcontrol->private_value & 0xffff;
	unsigned char mask = (kcontrol->private_value >> 16) & 0xff;
	long val = *ucontrol->value.integer.value;
	unsigned int ctrl_data = snd_hda_codec_read(codec, nid, 0,
						    AC_VERB_GET_EAPD_BTLENABLE,
						    0x00);

	/* Set/unset the masked control bit(s) as needed */
	change = (!val ? 0 : mask) != (ctrl_data & mask);
	if (!val)
		ctrl_data &= ~mask;
	else
		ctrl_data |= mask;
	snd_hda_codec_write_cache(codec, nid, 0, AC_VERB_SET_EAPD_BTLENABLE,
				  ctrl_data);

	return change;
}

#define ALC_EAPD_CTRL_SWITCH(xname, nid, mask) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0,  \
	  .info = alc_eapd_ctrl_info, \
	  .get = alc_eapd_ctrl_get, \
	  .put = alc_eapd_ctrl_put, \
	  .private_value = nid | (mask<<16) }
#endif   /* CONFIG_SND_DEBUG */

/*
 * set up from the preset table
 */
static void setup_preset(struct alc_spec *spec,
			 const struct alc_config_preset *preset)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(preset->mixers) && preset->mixers[i]; i++)
		spec->mixers[spec->num_mixers++] = preset->mixers[i];
	for (i = 0; i < ARRAY_SIZE(preset->init_verbs) && preset->init_verbs[i];
	     i++)
		spec->init_verbs[spec->num_init_verbs++] =
			preset->init_verbs[i];

	spec->channel_mode = preset->channel_mode;
	spec->num_channel_mode = preset->num_channel_mode;
	spec->need_dac_fix = preset->need_dac_fix;

	spec->multiout.max_channels = spec->channel_mode[0].channels;

	spec->multiout.num_dacs = preset->num_dacs;
	spec->multiout.dac_nids = preset->dac_nids;
	spec->multiout.dig_out_nid = preset->dig_out_nid;
	spec->multiout.hp_nid = preset->hp_nid;

	spec->num_mux_defs = preset->num_mux_defs;
	if (!spec->num_mux_defs)
		spec->num_mux_defs = 1;
	spec->input_mux = preset->input_mux;

	spec->num_adc_nids = preset->num_adc_nids;
	spec->adc_nids = preset->adc_nids;
	spec->capsrc_nids = preset->capsrc_nids;
	spec->dig_in_nid = preset->dig_in_nid;

	spec->unsol_event = preset->unsol_event;
	spec->init_hook = preset->init_hook;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	spec->loopback.amplist = preset->loopbacks;
#endif
}

/* Enable GPIO mask and set output */
static struct hda_verb alc_gpio1_init_verbs[] = {
	{0x01, AC_VERB_SET_GPIO_MASK, 0x01},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x01},
	{ }
};

static struct hda_verb alc_gpio2_init_verbs[] = {
	{0x01, AC_VERB_SET_GPIO_MASK, 0x02},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x02},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x02},
	{ }
};

static struct hda_verb alc_gpio3_init_verbs[] = {
	{0x01, AC_VERB_SET_GPIO_MASK, 0x03},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x03},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x03},
	{ }
};

/*
 * Fix hardware PLL issue
 * On some codecs, the analog PLL gating control must be off while
 * the default value is 1.
 */
static void alc_fix_pll(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int val;

	if (!spec->pll_nid)
		return;
	snd_hda_codec_write(codec, spec->pll_nid, 0, AC_VERB_SET_COEF_INDEX,
			    spec->pll_coef_idx);
	val = snd_hda_codec_read(codec, spec->pll_nid, 0,
				 AC_VERB_GET_PROC_COEF, 0);
	snd_hda_codec_write(codec, spec->pll_nid, 0, AC_VERB_SET_COEF_INDEX,
			    spec->pll_coef_idx);
	snd_hda_codec_write(codec, spec->pll_nid, 0, AC_VERB_SET_PROC_COEF,
			    val & ~(1 << spec->pll_coef_bit));
}

static void alc_fix_pll_init(struct hda_codec *codec, hda_nid_t nid,
			     unsigned int coef_idx, unsigned int coef_bit)
{
	struct alc_spec *spec = codec->spec;
	spec->pll_nid = nid;
	spec->pll_coef_idx = coef_idx;
	spec->pll_coef_bit = coef_bit;
	alc_fix_pll(codec);
}

static void alc_sku_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int present;
	unsigned int hp_nid = spec->autocfg.hp_pins[0];
	unsigned int sp_nid = spec->autocfg.speaker_pins[0];

	/* need to execute and sync at first */
	snd_hda_codec_read(codec, hp_nid, 0, AC_VERB_SET_PIN_SENSE, 0);
	present = snd_hda_codec_read(codec, hp_nid, 0,
				     AC_VERB_GET_PIN_SENSE, 0);
	spec->jack_present = (present & 0x80000000) != 0;
	snd_hda_codec_write(codec, sp_nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    spec->jack_present ? 0 : PIN_OUT);
}

/* unsolicited event for HP jack sensing */
static void alc_sku_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if (codec->vendor_id == 0x10ec0880)
		res >>= 28;
	else
		res >>= 26;
	if (res != ALC880_HP_EVENT)
		return;

	alc_sku_automute(codec);
}

/* additional initialization for ALC888 variants */
static void alc888_coef_init(struct hda_codec *codec)
{
	unsigned int tmp;

	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX, 0);
	tmp = snd_hda_codec_read(codec, 0x20, 0, AC_VERB_GET_PROC_COEF, 0);
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX, 7);
	if ((tmp & 0xf0) == 2)
		/* alc888S-VC */
		snd_hda_codec_read(codec, 0x20, 0,
				   AC_VERB_SET_PROC_COEF, 0x830);
	 else
		 /* alc888-VB */
		 snd_hda_codec_read(codec, 0x20, 0,
				    AC_VERB_SET_PROC_COEF, 0x3030);
}

/* 32-bit subsystem ID for BIOS loading in HD Audio codec.
 *	31 ~ 16 :	Manufacture ID
 *	15 ~ 8	:	SKU ID
 *	7  ~ 0	:	Assembly ID
 *	port-A --> pin 39/41, port-E --> pin 14/15, port-D --> pin 35/36
 */
static void alc_subsystem_id(struct hda_codec *codec,
			     unsigned int porta, unsigned int porte,
			     unsigned int portd)
{
	unsigned int ass, tmp, i;
	unsigned nid;
	struct alc_spec *spec = codec->spec;

	ass = codec->subsystem_id & 0xffff;
	if ((ass != codec->bus->pci->subsystem_device) && (ass & 1))
		goto do_sku;

	/*
	 * 31~30	: port conetcivity
	 * 29~21	: reserve
	 * 20		: PCBEEP input
	 * 19~16	: Check sum (15:1)
	 * 15~1		: Custom
	 * 0		: override
	*/
	nid = 0x1d;
	if (codec->vendor_id == 0x10ec0260)
		nid = 0x17;
	ass = snd_hda_codec_read(codec, nid, 0,
				 AC_VERB_GET_CONFIG_DEFAULT, 0);
	if (!(ass & 1) && !(ass & 0x100000))
		return;
	if ((ass >> 30) != 1)	/* no physical connection */
		return;

	/* check sum */
	tmp = 0;
	for (i = 1; i < 16; i++) {
		if ((ass >> i) & 1)
			tmp++;
	}
	if (((ass >> 16) & 0xf) != tmp)
		return;
do_sku:
	/*
	 * 0 : override
	 * 1 :	Swap Jack
	 * 2 : 0 --> Desktop, 1 --> Laptop
	 * 3~5 : External Amplifier control
	 * 7~6 : Reserved
	*/
	tmp = (ass & 0x38) >> 3;	/* external Amp control */
	switch (tmp) {
	case 1:
		snd_hda_sequence_write(codec, alc_gpio1_init_verbs);
		break;
	case 3:
		snd_hda_sequence_write(codec, alc_gpio2_init_verbs);
		break;
	case 7:
		snd_hda_sequence_write(codec, alc_gpio3_init_verbs);
		break;
	case 5:	/* set EAPD output high */
		switch (codec->vendor_id) {
		case 0x10ec0260:
			snd_hda_codec_write(codec, 0x0f, 0,
					    AC_VERB_SET_EAPD_BTLENABLE, 2);
			snd_hda_codec_write(codec, 0x10, 0,
					    AC_VERB_SET_EAPD_BTLENABLE, 2);
			break;
		case 0x10ec0262:
		case 0x10ec0267:
		case 0x10ec0268:
		case 0x10ec0269:
		case 0x10ec0660:
		case 0x10ec0662:
		case 0x10ec0663:
		case 0x10ec0862:
		case 0x10ec0889:
			snd_hda_codec_write(codec, 0x14, 0,
					    AC_VERB_SET_EAPD_BTLENABLE, 2);
			snd_hda_codec_write(codec, 0x15, 0,
					    AC_VERB_SET_EAPD_BTLENABLE, 2);
			break;
		}
		switch (codec->vendor_id) {
		case 0x10ec0260:
			snd_hda_codec_write(codec, 0x1a, 0,
					    AC_VERB_SET_COEF_INDEX, 7);
			tmp = snd_hda_codec_read(codec, 0x1a, 0,
						 AC_VERB_GET_PROC_COEF, 0);
			snd_hda_codec_write(codec, 0x1a, 0,
					    AC_VERB_SET_COEF_INDEX, 7);
			snd_hda_codec_write(codec, 0x1a, 0,
					    AC_VERB_SET_PROC_COEF,
					    tmp | 0x2010);
			break;
		case 0x10ec0262:
		case 0x10ec0880:
		case 0x10ec0882:
		case 0x10ec0883:
		case 0x10ec0885:
		case 0x10ec0889:
			snd_hda_codec_write(codec, 0x20, 0,
					    AC_VERB_SET_COEF_INDEX, 7);
			tmp = snd_hda_codec_read(codec, 0x20, 0,
						 AC_VERB_GET_PROC_COEF, 0);
			snd_hda_codec_write(codec, 0x20, 0,
					    AC_VERB_SET_COEF_INDEX, 7);
			snd_hda_codec_write(codec, 0x20, 0,
					    AC_VERB_SET_PROC_COEF,
					    tmp | 0x2010);
			break;
		case 0x10ec0888:
			/*alc888_coef_init(codec);*/ /* called in alc_init() */
			break;
		case 0x10ec0267:
		case 0x10ec0268:
			snd_hda_codec_write(codec, 0x20, 0,
					    AC_VERB_SET_COEF_INDEX, 7);
			tmp = snd_hda_codec_read(codec, 0x20, 0,
						 AC_VERB_GET_PROC_COEF, 0);
			snd_hda_codec_write(codec, 0x20, 0,
					    AC_VERB_SET_COEF_INDEX, 7);
			snd_hda_codec_write(codec, 0x20, 0,
					    AC_VERB_SET_PROC_COEF,
					    tmp | 0x3000);
			break;
		}
	default:
		break;
	}

	/* is laptop or Desktop and enable the function "Mute internal speaker
	 * when the external headphone out jack is plugged"
	 */
	if (!(ass & 0x8000))
		return;
	/*
	 * 10~8 : Jack location
	 * 12~11: Headphone out -> 00: PortA, 01: PortE, 02: PortD, 03: Resvered
	 * 14~13: Resvered
	 * 15   : 1 --> enable the function "Mute internal speaker
	 *	        when the external headphone out jack is plugged"
	 */
	if (!spec->autocfg.speaker_pins[0]) {
		if (spec->autocfg.line_out_pins[0])
			spec->autocfg.speaker_pins[0] =
				spec->autocfg.line_out_pins[0];
		else
			return;
	}

	if (!spec->autocfg.hp_pins[0]) {
		tmp = (ass >> 11) & 0x3;	/* HP to chassis */
		if (tmp == 0)
			spec->autocfg.hp_pins[0] = porta;
		else if (tmp == 1)
			spec->autocfg.hp_pins[0] = porte;
		else if (tmp == 2)
			spec->autocfg.hp_pins[0] = portd;
		else
			return;
	}

	snd_hda_codec_write(codec, spec->autocfg.hp_pins[0], 0,
			    AC_VERB_SET_UNSOLICITED_ENABLE,
			    AC_USRSP_EN | ALC880_HP_EVENT);

	spec->unsol_event = alc_sku_unsol_event;
}

/*
 * Fix-up pin default configurations
 */

struct alc_pincfg {
	hda_nid_t nid;
	u32 val;
};

static void alc_fix_pincfg(struct hda_codec *codec,
			   const struct snd_pci_quirk *quirk,
			   const struct alc_pincfg **pinfix)
{
	const struct alc_pincfg *cfg;

	quirk = snd_pci_quirk_lookup(codec->bus->pci, quirk);
	if (!quirk)
		return;

	cfg = pinfix[quirk->value];
	for (; cfg->nid; cfg++) {
		int i;
		u32 val = cfg->val;
		for (i = 0; i < 4; i++) {
			snd_hda_codec_write(codec, cfg->nid, 0,
				    AC_VERB_SET_CONFIG_DEFAULT_BYTES_0 + i,
				    val & 0xff);
			val >>= 8;
		}
	}
}

/*
 * ALC880 3-stack model
 *
 * DAC: Front = 0x02 (0x0c), Surr = 0x05 (0x0f), CLFE = 0x04 (0x0e)
 * Pin assignment: Front = 0x14, Line-In/Surr = 0x1a, Mic/CLFE = 0x18,
 *                 F-Mic = 0x1b, HP = 0x19
 */

static hda_nid_t alc880_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x05, 0x04, 0x03
};

static hda_nid_t alc880_adc_nids[3] = {
	/* ADC0-2 */
	0x07, 0x08, 0x09,
};

/* The datasheet says the node 0x07 is connected from inputs,
 * but it shows zero connection in the real implementation on some devices.
 * Note: this is a 915GAV bug, fixed on 915GLV
 */
static hda_nid_t alc880_adc_nids_alt[2] = {
	/* ADC1-2 */
	0x08, 0x09,
};

#define ALC880_DIGOUT_NID	0x06
#define ALC880_DIGIN_NID	0x0a

static struct hda_input_mux alc880_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x3 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

/* channel source setting (2/6 channel selection for 3-stack) */
/* 2ch mode */
static struct hda_verb alc880_threestack_ch2_init[] = {
	/* set line-in to input, mute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	/* set mic-in to input vref 80%, mute it */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/* 6ch mode */
static struct hda_verb alc880_threestack_ch6_init[] = {
	/* set line-in to output, unmute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	/* set mic-in to output, unmute it */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ } /* end */
};

static struct hda_channel_mode alc880_threestack_modes[2] = {
	{ 2, alc880_threestack_ch2_init },
	{ 6, alc880_threestack_ch6_init },
};

static struct snd_kcontrol_new alc880_three_stack_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x19, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

/* capture mixer elements */
static struct snd_kcontrol_new alc880_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 2, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 2, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 3,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

/* capture mixer elements (in case NID 0x07 not available) */
static struct snd_kcontrol_new alc880_capture_alt_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};



/*
 * ALC880 5-stack model
 *
 * DAC: Front = 0x02 (0x0c), Surr = 0x05 (0x0f), CLFE = 0x04 (0x0d),
 *      Side = 0x02 (0xd)
 * Pin assignment: Front = 0x14, Surr = 0x17, CLFE = 0x16
 *                 Line-In/Side = 0x1a, Mic = 0x18, F-Mic = 0x1b, HP = 0x19
 */

/* additional mixers to alc880_three_stack_mixer */
static struct snd_kcontrol_new alc880_five_stack_mixer[] = {
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0d, 2, HDA_INPUT),
	{ } /* end */
};

/* channel source setting (6/8 channel selection for 5-stack) */
/* 6ch mode */
static struct hda_verb alc880_fivestack_ch6_init[] = {
	/* set line-in to input, mute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/* 8ch mode */
static struct hda_verb alc880_fivestack_ch8_init[] = {
	/* set line-in to output, unmute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ } /* end */
};

static struct hda_channel_mode alc880_fivestack_modes[2] = {
	{ 6, alc880_fivestack_ch6_init },
	{ 8, alc880_fivestack_ch8_init },
};


/*
 * ALC880 6-stack model
 *
 * DAC: Front = 0x02 (0x0c), Surr = 0x03 (0x0d), CLFE = 0x04 (0x0e),
 *      Side = 0x05 (0x0f)
 * Pin assignment: Front = 0x14, Surr = 0x15, CLFE = 0x16, Side = 0x17,
 *   Mic = 0x18, F-Mic = 0x19, Line = 0x1a, HP = 0x1b
 */

static hda_nid_t alc880_6st_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x03, 0x04, 0x05
};

static struct hda_input_mux alc880_6stack_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

/* fixed 8-channels */
static struct hda_channel_mode alc880_sixstack_modes[1] = {
	{ 8, NULL },
};

static struct snd_kcontrol_new alc880_six_stack_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};


/*
 * ALC880 W810 model
 *
 * W810 has rear IO for:
 * Front (DAC 02)
 * Surround (DAC 03)
 * Center/LFE (DAC 04)
 * Digital out (06)
 *
 * The system also has a pair of internal speakers, and a headphone jack.
 * These are both connected to Line2 on the codec, hence to DAC 02.
 *
 * There is a variable resistor to control the speaker or headphone
 * volume. This is a hardware-only device without a software API.
 *
 * Plugging headphones in will disable the internal speakers. This is
 * implemented in hardware, not via the driver using jack sense. In
 * a similar fashion, plugging into the rear socket marked "front" will
 * disable both the speakers and headphones.
 *
 * For input, there's a microphone jack, and an "audio in" jack.
 * These may not do anything useful with this driver yet, because I
 * haven't setup any initialization verbs for these yet...
 */

static hda_nid_t alc880_w810_dac_nids[3] = {
	/* front, rear/surround, clfe */
	0x02, 0x03, 0x04
};

/* fixed 6 channels */
static struct hda_channel_mode alc880_w810_modes[1] = {
	{ 6, NULL }
};

/* Pin assignment: Front = 0x14, Surr = 0x15, CLFE = 0x16, HP = 0x1b */
static struct snd_kcontrol_new alc880_w810_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	{ } /* end */
};


/*
 * Z710V model
 *
 * DAC: Front = 0x02 (0x0c), HP = 0x03 (0x0d)
 * Pin assignment: Front = 0x14, HP = 0x15, Mic = 0x18, Mic2 = 0x19(?),
 *                 Line = 0x1a
 */

static hda_nid_t alc880_z71v_dac_nids[1] = {
	0x02
};
#define ALC880_Z71V_HP_DAC	0x03

/* fixed 2 channels */
static struct hda_channel_mode alc880_2_jack_modes[1] = {
	{ 2, NULL }
};

static struct snd_kcontrol_new alc880_z71v_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};


/*
 * ALC880 F1734 model
 *
 * DAC: HP = 0x02 (0x0c), Front = 0x03 (0x0d)
 * Pin assignment: HP = 0x14, Front = 0x15, Mic = 0x18
 */

static hda_nid_t alc880_f1734_dac_nids[1] = {
	0x03
};
#define ALC880_F1734_HP_DAC	0x02

static struct snd_kcontrol_new alc880_f1734_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static struct hda_input_mux alc880_f1734_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x1 },
		{ "CD", 0x4 },
	},
};


/*
 * ALC880 ASUS model
 *
 * DAC: HP/Front = 0x02 (0x0c), Surr = 0x03 (0x0d), CLFE = 0x04 (0x0e)
 * Pin assignment: HP/Front = 0x14, Surr = 0x15, CLFE = 0x16,
 *  Mic = 0x18, Line = 0x1a
 */

#define alc880_asus_dac_nids	alc880_w810_dac_nids	/* identical with w810 */
#define alc880_asus_modes	alc880_threestack_modes	/* 2/6 channel mode */

static struct snd_kcontrol_new alc880_asus_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

/*
 * ALC880 ASUS W1V model
 *
 * DAC: HP/Front = 0x02 (0x0c), Surr = 0x03 (0x0d), CLFE = 0x04 (0x0e)
 * Pin assignment: HP/Front = 0x14, Surr = 0x15, CLFE = 0x16,
 *  Mic = 0x18, Line = 0x1a, Line2 = 0x1b
 */

/* additional mixers to alc880_asus_mixer */
static struct snd_kcontrol_new alc880_asus_w1v_mixer[] = {
	HDA_CODEC_VOLUME("Line2 Playback Volume", 0x0b, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Line2 Playback Switch", 0x0b, 0x03, HDA_INPUT),
	{ } /* end */
};

/* additional mixers to alc880_asus_mixer */
static struct snd_kcontrol_new alc880_pcbeep_mixer[] = {
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

/* TCL S700 */
static struct snd_kcontrol_new alc880_tcl_s700_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0B, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0B, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0B, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0B, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

/* Uniwill */
static struct snd_kcontrol_new alc880_uniwill_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc880_fujitsu_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Ext Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Ext Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc880_uniwill_p53_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

/*
 * virtual master controls
 */

/*
 * slave controls for virtual master
 */
static const char *alc_slave_vols[] = {
	"Front Playback Volume",
	"Surround Playback Volume",
	"Center Playback Volume",
	"LFE Playback Volume",
	"Side Playback Volume",
	"Headphone Playback Volume",
	"Speaker Playback Volume",
	"Mono Playback Volume",
	"Line-Out Playback Volume",
	NULL,
};

static const char *alc_slave_sws[] = {
	"Front Playback Switch",
	"Surround Playback Switch",
	"Center Playback Switch",
	"LFE Playback Switch",
	"Side Playback Switch",
	"Headphone Playback Switch",
	"Speaker Playback Switch",
	"Mono Playback Switch",
	"IEC958 Playback Switch",
	NULL,
};

/*
 * build control elements
 */
static int alc_build_controls(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	int i;

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
		snd_hda_set_vmaster_tlv(codec, spec->vmaster_nid,
					HDA_OUTPUT, vmaster_tlv);
		err = snd_hda_add_vmaster(codec, "Master Playback Volume",
					  vmaster_tlv, alc_slave_vols);
		if (err < 0)
			return err;
	}
	if (!snd_hda_find_mixer_ctl(codec, "Master Playback Switch")) {
		err = snd_hda_add_vmaster(codec, "Master Playback Switch",
					  NULL, alc_slave_sws);
		if (err < 0)
			return err;
	}

	return 0;
}


/*
 * initialize the codec volumes, etc
 */

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc880_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for front
	 * panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},

	/*
	 * Set up output mixers (0x0c - 0x0f)
	 */
	/* set vol=0 to output mixers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	{ }
};

/*
 * 3-stack pin configuration:
 * front = 0x14, mic/clfe = 0x18, HP = 0x19, line/surr = 0x1a, f-mic = 0x1b
 */
static struct hda_verb alc880_pin_3stack_init_verbs[] = {
	/*
	 * preset connection lists of input pins
	 * 0 = front, 1 = rear_surr, 2 = CLFE, 3 = surround
	 */
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x03}, /* line/surround */

	/*
	 * Set pin mode and muting
	 */
	/* set front pin widgets 0x14 for output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Mic2 (as headphone out) for HP output */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Line In pin widget for input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line2 (as front mic) pin widget for input and vref at 80% */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

/*
 * 5-stack pin configuration:
 * front = 0x14, surround = 0x17, clfe = 0x16, mic = 0x18, HP = 0x19,
 * line-in/side = 0x1a, f-mic = 0x1b
 */
static struct hda_verb alc880_pin_5stack_init_verbs[] = {
	/*
	 * preset connection lists of input pins
	 * 0 = front, 1 = rear_surr, 2 = CLFE, 3 = surround
	 */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x01}, /* line/side */

	/*
	 * Set pin mode and muting
	 */
	/* set pin widgets 0x14-0x17 for output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* unmute pins for output (no gain on this amp) */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Mic2 (as headphone out) for HP output */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Line In pin widget for input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line2 (as front mic) pin widget for input and vref at 80% */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

/*
 * W810 pin configuration:
 * front = 0x14, surround = 0x15, clfe = 0x16, HP = 0x1b
 */
static struct hda_verb alc880_pin_w810_init_verbs[] = {
	/* hphone/speaker input selector: front DAC */
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x0},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{ }
};

/*
 * Z71V pin configuration:
 * Speaker-out = 0x14, HP = 0x15, Mic = 0x18, Line-in = 0x1a, Mic2 = 0x1b (?)
 */
static struct hda_verb alc880_pin_z71v_init_verbs[] = {
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

/*
 * 6-stack pin configuration:
 * front = 0x14, surr = 0x15, clfe = 0x16, side = 0x17, mic = 0x18,
 * f-mic = 0x19, line = 0x1a, HP = 0x1b
 */
static struct hda_verb alc880_pin_6stack_init_verbs[] = {
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

/*
 * Uniwill pin configuration:
 * HP = 0x14, InternalSpeaker = 0x15, mic = 0x18, internal mic = 0x19,
 * line = 0x1a
 */
static struct hda_verb alc880_uniwill_init_verbs[] = {
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* {0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP}, */
	/* {0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},

	{ }
};

/*
* Uniwill P53
* HP = 0x14, InternalSpeaker = 0x15, mic = 0x19,
 */
static struct hda_verb alc880_uniwill_p53_init_verbs[] = {
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_DCVOL_EVENT},

	{ }
};

static struct hda_verb alc880_beep_init_verbs[] = {
	{ 0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(5) },
	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc880_uniwill_hp_automute(struct hda_codec *codec)
{
 	unsigned int present;
	unsigned char bits;

 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	snd_hda_codec_amp_stereo(codec, 0x16, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

/* auto-toggle front mic */
static void alc880_uniwill_mic_automute(struct hda_codec *codec)
{
 	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x0b, HDA_INPUT, 1, HDA_AMP_MUTE, bits);
}

static void alc880_uniwill_automute(struct hda_codec *codec)
{
	alc880_uniwill_hp_automute(codec);
	alc880_uniwill_mic_automute(codec);
}

static void alc880_uniwill_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	/* Looks like the unsol event is incompatible with the standard
	 * definition.  4bit tag is placed at 28 bit!
	 */
	switch (res >> 28) {
	case ALC880_HP_EVENT:
		alc880_uniwill_hp_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc880_uniwill_mic_automute(codec);
		break;
	}
}

static void alc880_uniwill_p53_hp_automute(struct hda_codec *codec)
{
 	unsigned int present;
	unsigned char bits;

 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0, HDA_AMP_MUTE, bits);
}

static void alc880_uniwill_p53_dcvol_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x21, 0,
				     AC_VERB_GET_VOLUME_KNOB_CONTROL, 0);
	present &= HDA_AMP_VOLMASK;
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_OUTPUT, 0,
				 HDA_AMP_VOLMASK, present);
	snd_hda_codec_amp_stereo(codec, 0x0d, HDA_OUTPUT, 0,
				 HDA_AMP_VOLMASK, present);
}

static void alc880_uniwill_p53_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	/* Looks like the unsol event is incompatible with the standard
	 * definition.  4bit tag is placed at 28 bit!
	 */
	if ((res >> 28) == ALC880_HP_EVENT)
		alc880_uniwill_p53_hp_automute(codec);
	if ((res >> 28) == ALC880_DCVOL_EVENT)
		alc880_uniwill_p53_dcvol_automute(codec);
}

/*
 * F1734 pin configuration:
 * HP = 0x14, speaker-out = 0x15, mic = 0x18
 */
static struct hda_verb alc880_pin_f1734_init_verbs[] = {
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x02},
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_HP_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_DCVOL_EVENT},

	{ }
};

/*
 * ASUS pin configuration:
 * HP/front = 0x14, surr = 0x15, clfe = 0x16, mic = 0x18, line = 0x1a
 */
static struct hda_verb alc880_pin_asus_init_verbs[] = {
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x02},
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

/* Enable GPIO mask and set output */
#define alc880_gpio1_init_verbs	alc_gpio1_init_verbs
#define alc880_gpio2_init_verbs	alc_gpio2_init_verbs

/* Clevo m520g init */
static struct hda_verb alc880_pin_clevo_init_verbs[] = {
	/* headphone output */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* line-out */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Line-in */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* CD */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Mic1 (rear panel) */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Mic2 (front panel) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* headphone */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
        /* change to EAPD mode */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3060},

	{ }
};

static struct hda_verb alc880_pin_tcl_S700_init_verbs[] = {
	/* change to EAPD mode */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3060},

	/* Headphone output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Front output*/
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Line In pin widget for input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},

	/* change to EAPD mode */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3070},

	{ }
};

/*
 * LG m1 express dual
 *
 * Pin assignment:
 *   Rear Line-In/Out (blue): 0x14
 *   Build-in Mic-In: 0x15
 *   Speaker-out: 0x17
 *   HP-Out (green): 0x1b
 *   Mic-In/Out (red): 0x19
 *   SPDIF-Out: 0x1e
 */

/* To make 5.1 output working (green=Front, blue=Surr, red=CLFE) */
static hda_nid_t alc880_lg_dac_nids[3] = {
	0x05, 0x02, 0x03
};

/* seems analog CD is not working */
static struct hda_input_mux alc880_lg_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x1 },
		{ "Line", 0x5 },
		{ "Internal Mic", 0x6 },
	},
};

/* 2,4,6 channel modes */
static struct hda_verb alc880_lg_ch2_init[] = {
	/* set line-in and mic-in to input */
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ }
};

static struct hda_verb alc880_lg_ch4_init[] = {
	/* set line-in to out and mic-in to input */
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ }
};

static struct hda_verb alc880_lg_ch6_init[] = {
	/* set line-in and mic-in to output */
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ }
};

static struct hda_channel_mode alc880_lg_ch_modes[3] = {
	{ 2, alc880_lg_ch2_init },
	{ 4, alc880_lg_ch4_init },
	{ 6, alc880_lg_ch6_init },
};

static struct snd_kcontrol_new alc880_lg_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0d, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0d, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0d, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0d, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x06, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x06, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x07, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x07, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct hda_verb alc880_lg_init_verbs[] = {
	/* set capture source to mic-in */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* mute all amp mixer inputs */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(5)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},
	/* line-in to input */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* built-in mic */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* speaker-out */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* mic-in to input */
	{0x11, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* HP-out */
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x03},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* jack sense */
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | 0x1},
	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc880_lg_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x17, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc880_lg_unsol_event(struct hda_codec *codec, unsigned int res)
{
	/* Looks like the unsol event is incompatible with the standard
	 * definition.  4bit tag is placed at 28 bit!
	 */
	if ((res >> 28) == 0x01)
		alc880_lg_automute(codec);
}

/*
 * LG LW20
 *
 * Pin assignment:
 *   Speaker-out: 0x14
 *   Mic-In: 0x18
 *   Built-in Mic-In: 0x19
 *   Line-In: 0x1b
 *   HP-Out: 0x1a
 *   SPDIF-Out: 0x1e
 */

static struct hda_input_mux alc880_lg_lw_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x1 },
		{ "Line In", 0x2 },
	},
};

#define alc880_lg_lw_modes alc880_threestack_modes

static struct snd_kcontrol_new alc880_lg_lw_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct hda_verb alc880_lg_lw_init_verbs[] = {
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{0x10, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x12, AC_VERB_SET_CONNECT_SEL, 0x03}, /* line/surround */

	/* set capture source to mic-in */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},
	/* speaker-out */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* HP-out */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* mic-in to input */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* built-in mic */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* jack sense */
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | 0x1},
	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc880_lg_lw_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc880_lg_lw_unsol_event(struct hda_codec *codec, unsigned int res)
{
	/* Looks like the unsol event is incompatible with the standard
	 * definition.  4bit tag is placed at 28 bit!
	 */
	if ((res >> 28) == 0x01)
		alc880_lg_lw_automute(codec);
}

static struct snd_kcontrol_new alc880_medion_rim_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Master Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static struct hda_input_mux alc880_medion_rim_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x1 },
	},
};

static struct hda_verb alc880_medion_rim_init_verbs[] = {
	{0x13, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Mic2 (as headphone out) for HP output */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Internal Speaker */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3060},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc880_medion_rim_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	if (present)
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA, 0);
	else
		snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA, 2);
}

static void alc880_medion_rim_unsol_event(struct hda_codec *codec,
					  unsigned int res)
{
	/* Looks like the unsol event is incompatible with the standard
	 * definition.  4bit tag is placed at 28 bit!
	 */
	if ((res >> 28) == ALC880_HP_EVENT)
		alc880_medion_rim_automute(codec);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list alc880_loopbacks[] = {
	{ 0x0b, HDA_INPUT, 0 },
	{ 0x0b, HDA_INPUT, 1 },
	{ 0x0b, HDA_INPUT, 2 },
	{ 0x0b, HDA_INPUT, 3 },
	{ 0x0b, HDA_INPUT, 4 },
	{ } /* end */
};

static struct hda_amp_list alc880_lg_loopbacks[] = {
	{ 0x0b, HDA_INPUT, 1 },
	{ 0x0b, HDA_INPUT, 6 },
	{ 0x0b, HDA_INPUT, 7 },
	{ } /* end */
};
#endif

/*
 * Common callbacks
 */

static int alc_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int i;

	alc_fix_pll(codec);
	if (codec->vendor_id == 0x10ec0888)
		alc888_coef_init(codec);

	for (i = 0; i < spec->num_init_verbs; i++)
		snd_hda_sequence_write(codec, spec->init_verbs[i]);

	if (spec->init_hook)
		spec->init_hook(codec);

	return 0;
}

static void alc_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct alc_spec *spec = codec->spec;

	if (spec->unsol_event)
		spec->unsol_event(codec, res);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static int alc_check_power_status(struct hda_codec *codec, hda_nid_t nid)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_check_amp_list_power(codec, &spec->loopback, nid);
}
#endif

/*
 * Analog playback callbacks
 */
static int alc880_playback_pcm_open(struct hda_pcm_stream *hinfo,
				    struct hda_codec *codec,
				    struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_open(codec, &spec->multiout, substream,
					     hinfo);
}

static int alc880_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       unsigned int stream_tag,
				       unsigned int format,
				       struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_prepare(codec, &spec->multiout,
						stream_tag, format, substream);
}

static int alc880_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_analog_cleanup(codec, &spec->multiout);
}

/*
 * Digital out
 */
static int alc880_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int alc880_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   unsigned int stream_tag,
					   unsigned int format,
					   struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout,
					     stream_tag, format, substream);
}

static int alc880_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

/*
 * Analog capture
 */
static int alc880_alt_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->adc_nids[substream->number + 1],
				   stream_tag, 0, format);
	return 0;
}

static int alc880_alt_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;

	snd_hda_codec_cleanup_stream(codec,
				     spec->adc_nids[substream->number + 1]);
	return 0;
}


/*
 */
static struct hda_pcm_stream alc880_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	/* NID is set in alc_build_pcms */
	.ops = {
		.open = alc880_playback_pcm_open,
		.prepare = alc880_playback_pcm_prepare,
		.cleanup = alc880_playback_pcm_cleanup
	},
};

static struct hda_pcm_stream alc880_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

static struct hda_pcm_stream alc880_pcm_analog_alt_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

static struct hda_pcm_stream alc880_pcm_analog_alt_capture = {
	.substreams = 2, /* can be overridden */
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
	.ops = {
		.prepare = alc880_alt_capture_pcm_prepare,
		.cleanup = alc880_alt_capture_pcm_cleanup
	},
};

static struct hda_pcm_stream alc880_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
	.ops = {
		.open = alc880_dig_playback_pcm_open,
		.close = alc880_dig_playback_pcm_close,
		.prepare = alc880_dig_playback_pcm_prepare
	},
};

static struct hda_pcm_stream alc880_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

/* Used by alc_build_pcms to flag that a PCM has no playback stream */
static struct hda_pcm_stream alc_pcm_null_stream = {
	.substreams = 0,
	.channels_min = 0,
	.channels_max = 0,
};

static int alc_build_pcms(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct hda_pcm *info = spec->pcm_rec;
	int i;

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = spec->stream_name_analog;
	if (spec->stream_analog_playback) {
		if (snd_BUG_ON(!spec->multiout.dac_nids))
			return -EINVAL;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] = *(spec->stream_analog_playback);
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dac_nids[0];
	}
	if (spec->stream_analog_capture) {
		if (snd_BUG_ON(!spec->adc_nids))
			return -EINVAL;
		info->stream[SNDRV_PCM_STREAM_CAPTURE] = *(spec->stream_analog_capture);
		info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adc_nids[0];
	}

	if (spec->channel_mode) {
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max = 0;
		for (i = 0; i < spec->num_channel_mode; i++) {
			if (spec->channel_mode[i].channels > info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max) {
				info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max = spec->channel_mode[i].channels;
			}
		}
	}

	/* SPDIF for stream index #1 */
	if (spec->multiout.dig_out_nid || spec->dig_in_nid) {
		codec->num_pcms = 2;
		info = spec->pcm_rec + 1;
		info->name = spec->stream_name_digital;
		info->pcm_type = HDA_PCM_TYPE_SPDIF;
		if (spec->multiout.dig_out_nid &&
		    spec->stream_digital_playback) {
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] = *(spec->stream_digital_playback);
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->multiout.dig_out_nid;
		}
		if (spec->dig_in_nid &&
		    spec->stream_digital_capture) {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] = *(spec->stream_digital_capture);
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->dig_in_nid;
		}
		/* FIXME: do we need this for all Realtek codec models? */
		codec->spdif_status_reset = 1;
	}

	/* If the use of more than one ADC is requested for the current
	 * model, configure a second analog capture-only PCM.
	 */
	/* Additional Analaog capture for index #2 */
	if ((spec->alt_dac_nid && spec->stream_analog_alt_playback) ||
	    (spec->num_adc_nids > 1 && spec->stream_analog_alt_capture)) {
		codec->num_pcms = 3;
		info = spec->pcm_rec + 2;
		info->name = spec->stream_name_analog;
		if (spec->alt_dac_nid) {
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
				*spec->stream_analog_alt_playback;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid =
				spec->alt_dac_nid;
		} else {
			info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
				alc_pcm_null_stream;
			info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = 0;
		}
		if (spec->num_adc_nids > 1) {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] =
				*spec->stream_analog_alt_capture;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid =
				spec->adc_nids[1];
			info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams =
				spec->num_adc_nids - 1;
		} else {
			info->stream[SNDRV_PCM_STREAM_CAPTURE] =
				alc_pcm_null_stream;
			info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = 0;
		}
	}

	return 0;
}

static void alc_free(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int i;

	if (!spec)
		return;

	if (spec->kctl_alloc) {
		for (i = 0; i < spec->num_kctl_used; i++)
			kfree(spec->kctl_alloc[i].name);
		kfree(spec->kctl_alloc);
	}
	kfree(spec);
	codec->spec = NULL; /* to be sure */
}

/*
 */
static struct hda_codec_ops alc_patch_ops = {
	.build_controls = alc_build_controls,
	.build_pcms = alc_build_pcms,
	.init = alc_init,
	.free = alc_free,
	.unsol_event = alc_unsol_event,
#ifdef CONFIG_SND_HDA_POWER_SAVE
	.check_power_status = alc_check_power_status,
#endif
};


/*
 * Test configuration for debugging
 *
 * Almost all inputs/outputs are enabled.  I/O pins can be configured via
 * enum controls.
 */
#ifdef CONFIG_SND_DEBUG
static hda_nid_t alc880_test_dac_nids[4] = {
	0x02, 0x03, 0x04, 0x05
};

static struct hda_input_mux alc880_test_capture_source = {
	.num_items = 7,
	.items = {
		{ "In-1", 0x0 },
		{ "In-2", 0x1 },
		{ "In-3", 0x2 },
		{ "In-4", 0x3 },
		{ "CD", 0x4 },
		{ "Front", 0x5 },
		{ "Surround", 0x6 },
	},
};

static struct hda_channel_mode alc880_test_modes[4] = {
	{ 2, NULL },
	{ 4, NULL },
	{ 6, NULL },
	{ 8, NULL },
};

static int alc_test_pin_ctl_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {
		"N/A", "Line Out", "HP Out",
		"In Hi-Z", "In 50%", "In Grd", "In 80%", "In 100%"
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 8;
	if (uinfo->value.enumerated.item >= 8)
		uinfo->value.enumerated.item = 7;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int alc_test_pin_ctl_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = (hda_nid_t)kcontrol->private_value;
	unsigned int pin_ctl, item = 0;

	pin_ctl = snd_hda_codec_read(codec, nid, 0,
				     AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
	if (pin_ctl & AC_PINCTL_OUT_EN) {
		if (pin_ctl & AC_PINCTL_HP_EN)
			item = 2;
		else
			item = 1;
	} else if (pin_ctl & AC_PINCTL_IN_EN) {
		switch (pin_ctl & AC_PINCTL_VREFEN) {
		case AC_PINCTL_VREF_HIZ: item = 3; break;
		case AC_PINCTL_VREF_50:  item = 4; break;
		case AC_PINCTL_VREF_GRD: item = 5; break;
		case AC_PINCTL_VREF_80:  item = 6; break;
		case AC_PINCTL_VREF_100: item = 7; break;
		}
	}
	ucontrol->value.enumerated.item[0] = item;
	return 0;
}

static int alc_test_pin_ctl_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = (hda_nid_t)kcontrol->private_value;
	static unsigned int ctls[] = {
		0, AC_PINCTL_OUT_EN, AC_PINCTL_OUT_EN | AC_PINCTL_HP_EN,
		AC_PINCTL_IN_EN | AC_PINCTL_VREF_HIZ,
		AC_PINCTL_IN_EN | AC_PINCTL_VREF_50,
		AC_PINCTL_IN_EN | AC_PINCTL_VREF_GRD,
		AC_PINCTL_IN_EN | AC_PINCTL_VREF_80,
		AC_PINCTL_IN_EN | AC_PINCTL_VREF_100,
	};
	unsigned int old_ctl, new_ctl;

	old_ctl = snd_hda_codec_read(codec, nid, 0,
				     AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
	new_ctl = ctls[ucontrol->value.enumerated.item[0]];
	if (old_ctl != new_ctl) {
		int val;
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_PIN_WIDGET_CONTROL,
					  new_ctl);
		val = ucontrol->value.enumerated.item[0] >= 3 ?
			HDA_AMP_MUTE : 0;
		snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, val);
		return 1;
	}
	return 0;
}

static int alc_test_pin_src_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	static char *texts[] = {
		"Front", "Surround", "CLFE", "Side"
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item >= 4)
		uinfo->value.enumerated.item = 3;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int alc_test_pin_src_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = (hda_nid_t)kcontrol->private_value;
	unsigned int sel;

	sel = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_CONNECT_SEL, 0);
	ucontrol->value.enumerated.item[0] = sel & 3;
	return 0;
}

static int alc_test_pin_src_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = (hda_nid_t)kcontrol->private_value;
	unsigned int sel;

	sel = snd_hda_codec_read(codec, nid, 0, AC_VERB_GET_CONNECT_SEL, 0) & 3;
	if (ucontrol->value.enumerated.item[0] != sel) {
		sel = ucontrol->value.enumerated.item[0] & 3;
		snd_hda_codec_write_cache(codec, nid, 0,
					  AC_VERB_SET_CONNECT_SEL, sel);
		return 1;
	}
	return 0;
}

#define PIN_CTL_TEST(xname,nid) {			\
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,	\
			.name = xname,		       \
			.info = alc_test_pin_ctl_info, \
			.get = alc_test_pin_ctl_get,   \
			.put = alc_test_pin_ctl_put,   \
			.private_value = nid	       \
			}

#define PIN_SRC_TEST(xname,nid) {			\
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,	\
			.name = xname,		       \
			.info = alc_test_pin_src_info, \
			.get = alc_test_pin_src_get,   \
			.put = alc_test_pin_src_put,   \
			.private_value = nid	       \
			}

static struct snd_kcontrol_new alc880_test_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CLFE Playback Volume", 0x0e, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_BIND_MUTE("CLFE Playback Switch", 0x0e, 2, HDA_INPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),
	PIN_CTL_TEST("Front Pin Mode", 0x14),
	PIN_CTL_TEST("Surround Pin Mode", 0x15),
	PIN_CTL_TEST("CLFE Pin Mode", 0x16),
	PIN_CTL_TEST("Side Pin Mode", 0x17),
	PIN_CTL_TEST("In-1 Pin Mode", 0x18),
	PIN_CTL_TEST("In-2 Pin Mode", 0x19),
	PIN_CTL_TEST("In-3 Pin Mode", 0x1a),
	PIN_CTL_TEST("In-4 Pin Mode", 0x1b),
	PIN_SRC_TEST("In-1 Pin Source", 0x18),
	PIN_SRC_TEST("In-2 Pin Source", 0x19),
	PIN_SRC_TEST("In-3 Pin Source", 0x1a),
	PIN_SRC_TEST("In-4 Pin Source", 0x1b),
	HDA_CODEC_VOLUME("In-1 Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("In-1 Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("In-2 Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("In-2 Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("In-3 Playback Volume", 0x0b, 0x2, HDA_INPUT),
	HDA_CODEC_MUTE("In-3 Playback Switch", 0x0b, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("In-4 Playback Volume", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_MUTE("In-4 Playback Switch", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x4, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct hda_verb alc880_test_init_verbs[] = {
	/* Unmute inputs of 0x0c - 0x0f */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* Vol output for 0x0c-0x0f */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* Set output pins 0x14-0x17 */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* Unmute output pins 0x14-0x17 */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Set input pins 0x18-0x1c */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* Mute input pins 0x18-0x1b */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* ADC set up */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Analog input/passthru */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{ }
};
#endif

/*
 */

static const char *alc880_models[ALC880_MODEL_LAST] = {
	[ALC880_3ST]		= "3stack",
	[ALC880_TCL_S700]	= "tcl",
	[ALC880_3ST_DIG]	= "3stack-digout",
	[ALC880_CLEVO]		= "clevo",
	[ALC880_5ST]		= "5stack",
	[ALC880_5ST_DIG]	= "5stack-digout",
	[ALC880_W810]		= "w810",
	[ALC880_Z71V]		= "z71v",
	[ALC880_6ST]		= "6stack",
	[ALC880_6ST_DIG]	= "6stack-digout",
	[ALC880_ASUS]		= "asus",
	[ALC880_ASUS_W1V]	= "asus-w1v",
	[ALC880_ASUS_DIG]	= "asus-dig",
	[ALC880_ASUS_DIG2]	= "asus-dig2",
	[ALC880_UNIWILL_DIG]	= "uniwill",
	[ALC880_UNIWILL_P53]	= "uniwill-p53",
	[ALC880_FUJITSU]	= "fujitsu",
	[ALC880_F1734]		= "F1734",
	[ALC880_LG]		= "lg",
	[ALC880_LG_LW]		= "lg-lw",
	[ALC880_MEDION_RIM]	= "medion",
#ifdef CONFIG_SND_DEBUG
	[ALC880_TEST]		= "test",
#endif
	[ALC880_AUTO]		= "auto",
};

static struct snd_pci_quirk alc880_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x0f69, "Coeus G610P", ALC880_W810),
	SND_PCI_QUIRK(0x1019, 0xa880, "ECS", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x1019, 0xa884, "Acer APFV", ALC880_6ST),
	SND_PCI_QUIRK(0x1025, 0x0070, "ULI", ALC880_3ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0077, "ULI", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0078, "ULI", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0x0087, "ULI", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1025, 0xe309, "ULI", ALC880_3ST_DIG),
	SND_PCI_QUIRK(0x1025, 0xe310, "ULI", ALC880_3ST),
	SND_PCI_QUIRK(0x1039, 0x1234, NULL, ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x103c, 0x2a09, "HP", ALC880_5ST),
	SND_PCI_QUIRK(0x1043, 0x10b3, "ASUS W1V", ALC880_ASUS_W1V),
	SND_PCI_QUIRK(0x1043, 0x10c2, "ASUS W6A", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x10c3, "ASUS Wxx", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x1113, "ASUS", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x1123, "ASUS", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x1173, "ASUS", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x1964, "ASUS Z71V", ALC880_Z71V),
	/* SND_PCI_QUIRK(0x1043, 0x1964, "ASUS", ALC880_ASUS_DIG), */
	SND_PCI_QUIRK(0x1043, 0x1973, "ASUS", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x19b3, "ASUS", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x814e, "ASUS P5GD1 w/SPDIF", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x8181, "ASUS P4GPL", ALC880_ASUS_DIG),
	SND_PCI_QUIRK(0x1043, 0x8196, "ASUS P5GD1", ALC880_6ST),
	SND_PCI_QUIRK(0x1043, 0x81b4, "ASUS", ALC880_6ST),
	SND_PCI_QUIRK(0x1043, 0, "ASUS", ALC880_ASUS), /* default ASUS */
	SND_PCI_QUIRK(0x104d, 0x81a0, "Sony", ALC880_3ST),
	SND_PCI_QUIRK(0x104d, 0x81d6, "Sony", ALC880_3ST),
	SND_PCI_QUIRK(0x107b, 0x3032, "Gateway", ALC880_5ST),
	SND_PCI_QUIRK(0x107b, 0x3033, "Gateway", ALC880_5ST),
	SND_PCI_QUIRK(0x107b, 0x4039, "Gateway", ALC880_5ST),
	SND_PCI_QUIRK(0x1297, 0xc790, "Shuttle ST20G5", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1458, 0xa102, "Gigabyte K8", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x1150, "MSI", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1509, 0x925d, "FIC P4M", ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x1558, 0x0520, "Clevo m520G", ALC880_CLEVO),
	SND_PCI_QUIRK(0x1558, 0x0660, "Clevo m655n", ALC880_CLEVO),
	SND_PCI_QUIRK(0x1558, 0x5401, "ASUS", ALC880_ASUS_DIG2),
	SND_PCI_QUIRK(0x1565, 0x8202, "Biostar", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x1584, 0x9050, "Uniwill", ALC880_UNIWILL_DIG),
	SND_PCI_QUIRK(0x1584, 0x9054, "Uniwlll", ALC880_F1734),
	SND_PCI_QUIRK(0x1584, 0x9070, "Uniwill", ALC880_UNIWILL),
	SND_PCI_QUIRK(0x1584, 0x9077, "Uniwill P53", ALC880_UNIWILL_P53),
	SND_PCI_QUIRK(0x161f, 0x203d, "W810", ALC880_W810),
	SND_PCI_QUIRK(0x161f, 0x205d, "Medion Rim 2150", ALC880_MEDION_RIM),
	SND_PCI_QUIRK(0x1695, 0x400d, "EPoX", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x1695, 0x4012, "EPox EP-5LDA", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x1734, 0x107c, "FSC F1734", ALC880_F1734),
	SND_PCI_QUIRK(0x1734, 0x1094, "FSC Amilo M1451G", ALC880_FUJITSU),
	SND_PCI_QUIRK(0x1734, 0x10ac, "FSC", ALC880_UNIWILL),
	SND_PCI_QUIRK(0x1734, 0x10b0, "Fujitsu", ALC880_FUJITSU),
	SND_PCI_QUIRK(0x1854, 0x0018, "LG LW20", ALC880_LG_LW),
	SND_PCI_QUIRK(0x1854, 0x003b, "LG", ALC880_LG),
	SND_PCI_QUIRK(0x1854, 0x0068, "LG w1", ALC880_LG),
	SND_PCI_QUIRK(0x1854, 0x0077, "LG LW25", ALC880_LG_LW),
	SND_PCI_QUIRK(0x19db, 0x4188, "TCL S700", ALC880_TCL_S700),
	SND_PCI_QUIRK(0x2668, 0x8086, NULL, ALC880_6ST_DIG), /* broken BIOS */
	SND_PCI_QUIRK(0x8086, 0x2668, NULL, ALC880_6ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xa100, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd400, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd401, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xd402, "Intel mobo", ALC880_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe224, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe305, "Intel mobo", ALC880_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe308, "Intel mobo", ALC880_3ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe400, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe401, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0xe402, "Intel mobo", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x8086, 0, "Intel mobo", ALC880_3ST), /* default Intel */
	SND_PCI_QUIRK(0xa0a0, 0x0560, "AOpen i915GMm-HFS", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0xe803, 0x1019, NULL, ALC880_6ST_DIG),
	{}
};

/*
 * ALC880 codec presets
 */
static struct alc_config_preset alc880_presets[] = {
	[ALC880_3ST] = {
		.mixers = { alc880_three_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_3stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_3ST_DIG] = {
		.mixers = { alc880_three_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_3stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_TCL_S700] = {
		.mixers = { alc880_tcl_s700_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_tcl_S700_init_verbs,
				alc880_gpio2_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc880_2_jack_modes),
		.channel_mode = alc880_2_jack_modes,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_5ST] = {
		.mixers = { alc880_three_stack_mixer,
			    alc880_five_stack_mixer},
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_5stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_fivestack_modes),
		.channel_mode = alc880_fivestack_modes,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_5ST_DIG] = {
		.mixers = { alc880_three_stack_mixer,
			    alc880_five_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_5stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_fivestack_modes),
		.channel_mode = alc880_fivestack_modes,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_6ST] = {
		.mixers = { alc880_six_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_6stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_6st_dac_nids),
		.dac_nids = alc880_6st_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_sixstack_modes),
		.channel_mode = alc880_sixstack_modes,
		.input_mux = &alc880_6stack_capture_source,
	},
	[ALC880_6ST_DIG] = {
		.mixers = { alc880_six_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_6stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_6st_dac_nids),
		.dac_nids = alc880_6st_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_sixstack_modes),
		.channel_mode = alc880_sixstack_modes,
		.input_mux = &alc880_6stack_capture_source,
	},
	[ALC880_W810] = {
		.mixers = { alc880_w810_base_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_w810_init_verbs,
				alc880_gpio2_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_w810_dac_nids),
		.dac_nids = alc880_w810_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_w810_modes),
		.channel_mode = alc880_w810_modes,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_Z71V] = {
		.mixers = { alc880_z71v_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_z71v_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_z71v_dac_nids),
		.dac_nids = alc880_z71v_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc880_2_jack_modes),
		.channel_mode = alc880_2_jack_modes,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_F1734] = {
		.mixers = { alc880_f1734_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_f1734_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_f1734_dac_nids),
		.dac_nids = alc880_f1734_dac_nids,
		.hp_nid = 0x02,
		.num_channel_mode = ARRAY_SIZE(alc880_2_jack_modes),
		.channel_mode = alc880_2_jack_modes,
		.input_mux = &alc880_f1734_capture_source,
		.unsol_event = alc880_uniwill_p53_unsol_event,
		.init_hook = alc880_uniwill_p53_hp_automute,
	},
	[ALC880_ASUS] = {
		.mixers = { alc880_asus_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_asus_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_asus_modes),
		.channel_mode = alc880_asus_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_ASUS_DIG] = {
		.mixers = { alc880_asus_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_asus_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_asus_modes),
		.channel_mode = alc880_asus_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_ASUS_DIG2] = {
		.mixers = { alc880_asus_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_asus_init_verbs,
				alc880_gpio2_init_verbs }, /* use GPIO2 */
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_asus_modes),
		.channel_mode = alc880_asus_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_ASUS_W1V] = {
		.mixers = { alc880_asus_mixer, alc880_asus_w1v_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_asus_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_asus_modes),
		.channel_mode = alc880_asus_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_UNIWILL_DIG] = {
		.mixers = { alc880_asus_mixer, alc880_pcbeep_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_asus_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_asus_modes),
		.channel_mode = alc880_asus_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_UNIWILL] = {
		.mixers = { alc880_uniwill_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_uniwill_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
		.unsol_event = alc880_uniwill_unsol_event,
		.init_hook = alc880_uniwill_automute,
	},
	[ALC880_UNIWILL_P53] = {
		.mixers = { alc880_uniwill_p53_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_uniwill_p53_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_asus_dac_nids),
		.dac_nids = alc880_asus_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_w810_modes),
		.channel_mode = alc880_threestack_modes,
		.input_mux = &alc880_capture_source,
		.unsol_event = alc880_uniwill_p53_unsol_event,
		.init_hook = alc880_uniwill_p53_hp_automute,
	},
	[ALC880_FUJITSU] = {
		.mixers = { alc880_fujitsu_mixer,
			    alc880_pcbeep_mixer, },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_uniwill_p53_init_verbs,
	       			alc880_beep_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_2_jack_modes),
		.channel_mode = alc880_2_jack_modes,
		.input_mux = &alc880_capture_source,
		.unsol_event = alc880_uniwill_p53_unsol_event,
		.init_hook = alc880_uniwill_p53_hp_automute,
	},
	[ALC880_CLEVO] = {
		.mixers = { alc880_three_stack_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_pin_clevo_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_capture_source,
	},
	[ALC880_LG] = {
		.mixers = { alc880_lg_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_lg_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_lg_dac_nids),
		.dac_nids = alc880_lg_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_lg_ch_modes),
		.channel_mode = alc880_lg_ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc880_lg_capture_source,
		.unsol_event = alc880_lg_unsol_event,
		.init_hook = alc880_lg_automute,
#ifdef CONFIG_SND_HDA_POWER_SAVE
		.loopbacks = alc880_lg_loopbacks,
#endif
	},
	[ALC880_LG_LW] = {
		.mixers = { alc880_lg_lw_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_lg_lw_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_lg_lw_modes),
		.channel_mode = alc880_lg_lw_modes,
		.input_mux = &alc880_lg_lw_capture_source,
		.unsol_event = alc880_lg_lw_unsol_event,
		.init_hook = alc880_lg_lw_automute,
	},
	[ALC880_MEDION_RIM] = {
		.mixers = { alc880_medion_rim_mixer },
		.init_verbs = { alc880_volume_init_verbs,
				alc880_medion_rim_init_verbs,
				alc_gpio2_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_dac_nids),
		.dac_nids = alc880_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_2_jack_modes),
		.channel_mode = alc880_2_jack_modes,
		.input_mux = &alc880_medion_rim_capture_source,
		.unsol_event = alc880_medion_rim_unsol_event,
		.init_hook = alc880_medion_rim_automute,
	},
#ifdef CONFIG_SND_DEBUG
	[ALC880_TEST] = {
		.mixers = { alc880_test_mixer },
		.init_verbs = { alc880_test_init_verbs },
		.num_dacs = ARRAY_SIZE(alc880_test_dac_nids),
		.dac_nids = alc880_test_dac_nids,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_test_modes),
		.channel_mode = alc880_test_modes,
		.input_mux = &alc880_test_capture_source,
	},
#endif
};

/*
 * Automatic parse of I/O pins from the BIOS configuration
 */

#define NUM_CONTROL_ALLOC	32
#define NUM_VERB_ALLOC		32

enum {
	ALC_CTL_WIDGET_VOL,
	ALC_CTL_WIDGET_MUTE,
	ALC_CTL_BIND_MUTE,
};
static struct snd_kcontrol_new alc880_control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	HDA_CODEC_MUTE(NULL, 0, 0, 0),
	HDA_BIND_MUTE(NULL, 0, 0, 0),
};

/* add dynamic controls */
static int add_control(struct alc_spec *spec, int type, const char *name,
		       unsigned long val)
{
	struct snd_kcontrol_new *knew;

	if (spec->num_kctl_used >= spec->num_kctl_alloc) {
		int num = spec->num_kctl_alloc + NUM_CONTROL_ALLOC;

		/* array + terminator */
		knew = kcalloc(num + 1, sizeof(*knew), GFP_KERNEL);
		if (!knew)
			return -ENOMEM;
		if (spec->kctl_alloc) {
			memcpy(knew, spec->kctl_alloc,
			       sizeof(*knew) * spec->num_kctl_alloc);
			kfree(spec->kctl_alloc);
		}
		spec->kctl_alloc = knew;
		spec->num_kctl_alloc = num;
	}

	knew = &spec->kctl_alloc[spec->num_kctl_used];
	*knew = alc880_control_templates[type];
	knew->name = kstrdup(name, GFP_KERNEL);
	if (!knew->name)
		return -ENOMEM;
	knew->private_value = val;
	spec->num_kctl_used++;
	return 0;
}

#define alc880_is_fixed_pin(nid)	((nid) >= 0x14 && (nid) <= 0x17)
#define alc880_fixed_pin_idx(nid)	((nid) - 0x14)
#define alc880_is_multi_pin(nid)	((nid) >= 0x18)
#define alc880_multi_pin_idx(nid)	((nid) - 0x18)
#define alc880_is_input_pin(nid)	((nid) >= 0x18)
#define alc880_input_pin_idx(nid)	((nid) - 0x18)
#define alc880_idx_to_dac(nid)		((nid) + 0x02)
#define alc880_dac_to_idx(nid)		((nid) - 0x02)
#define alc880_idx_to_mixer(nid)	((nid) + 0x0c)
#define alc880_idx_to_selector(nid)	((nid) + 0x10)
#define ALC880_PIN_CD_NID		0x1c

/* fill in the dac_nids table from the parsed pin configuration */
static int alc880_auto_fill_dac_nids(struct alc_spec *spec,
				     const struct auto_pin_cfg *cfg)
{
	hda_nid_t nid;
	int assigned[4];
	int i, j;

	memset(assigned, 0, sizeof(assigned));
	spec->multiout.dac_nids = spec->private_dac_nids;

	/* check the pins hardwired to audio widget */
	for (i = 0; i < cfg->line_outs; i++) {
		nid = cfg->line_out_pins[i];
		if (alc880_is_fixed_pin(nid)) {
			int idx = alc880_fixed_pin_idx(nid);
			spec->multiout.dac_nids[i] = alc880_idx_to_dac(idx);
			assigned[idx] = 1;
		}
	}
	/* left pins can be connect to any audio widget */
	for (i = 0; i < cfg->line_outs; i++) {
		nid = cfg->line_out_pins[i];
		if (alc880_is_fixed_pin(nid))
			continue;
		/* search for an empty channel */
		for (j = 0; j < cfg->line_outs; j++) {
			if (!assigned[j]) {
				spec->multiout.dac_nids[i] =
					alc880_idx_to_dac(j);
				assigned[j] = 1;
				break;
			}
		}
	}
	spec->multiout.num_dacs = cfg->line_outs;
	return 0;
}

/* add playback controls from the parsed DAC table */
static int alc880_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = {
		"Front", "Surround", NULL /*CLFE*/, "Side"
	};
	hda_nid_t nid;
	int i, err;

	for (i = 0; i < cfg->line_outs; i++) {
		if (!spec->multiout.dac_nids[i])
			continue;
		nid = alc880_idx_to_mixer(alc880_dac_to_idx(spec->multiout.dac_nids[i]));
		if (i == 2) {
			/* Center/LFE */
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "Center Playback Volume",
					  HDA_COMPOSE_AMP_VAL(nid, 1, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "LFE Playback Volume",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "Center Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 1, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "LFE Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
					  HDA_COMPOSE_AMP_VAL(nid, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = add_control(spec, ALC_CTL_BIND_MUTE, name,
					  HDA_COMPOSE_AMP_VAL(nid, 3, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

/* add playback controls for speaker and HP outputs */
static int alc880_auto_create_extra_out(struct alc_spec *spec, hda_nid_t pin,
					const char *pfx)
{
	hda_nid_t nid;
	int err;
	char name[32];

	if (!pin)
		return 0;

	if (alc880_is_fixed_pin(pin)) {
		nid = alc880_idx_to_dac(alc880_fixed_pin_idx(pin));
		/* specify the DAC as the extra output */
		if (!spec->multiout.hp_nid)
			spec->multiout.hp_nid = nid;
		else
			spec->multiout.extra_out_nid[0] = nid;
		/* control HP volume/switch on the output mixer amp */
		nid = alc880_idx_to_mixer(alc880_fixed_pin_idx(pin));
		sprintf(name, "%s Playback Volume", pfx);
		err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		sprintf(name, "%s Playback Switch", pfx);
		err = add_control(spec, ALC_CTL_BIND_MUTE, name,
				  HDA_COMPOSE_AMP_VAL(nid, 3, 2, HDA_INPUT));
		if (err < 0)
			return err;
	} else if (alc880_is_multi_pin(pin)) {
		/* set manual connection */
		/* we have only a switch on HP-out PIN */
		sprintf(name, "%s Playback Switch", pfx);
		err = add_control(spec, ALC_CTL_WIDGET_MUTE, name,
				  HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
	}
	return 0;
}

/* create input playback/capture controls for the given pin */
static int new_analog_input(struct alc_spec *spec, hda_nid_t pin,
			    const char *ctlname,
			    int idx, hda_nid_t mix_nid)
{
	char name[32];
	int err;

	sprintf(name, "%s Playback Volume", ctlname);
	err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
			  HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT));
	if (err < 0)
		return err;
	sprintf(name, "%s Playback Switch", ctlname);
	err = add_control(spec, ALC_CTL_WIDGET_MUTE, name,
			  HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT));
	if (err < 0)
		return err;
	return 0;
}

/* create playback/capture controls for input pins */
static int alc880_auto_create_analog_input_ctls(struct alc_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	struct hda_input_mux *imux = &spec->private_imux;
	int i, err, idx;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (alc880_is_input_pin(cfg->input_pins[i])) {
			idx = alc880_input_pin_idx(cfg->input_pins[i]);
			err = new_analog_input(spec, cfg->input_pins[i],
					       auto_pin_cfg_labels[i],
					       idx, 0x0b);
			if (err < 0)
				return err;
			imux->items[imux->num_items].label =
				auto_pin_cfg_labels[i];
			imux->items[imux->num_items].index =
				alc880_input_pin_idx(cfg->input_pins[i]);
			imux->num_items++;
		}
	}
	return 0;
}

static void alc_set_pin_output(struct hda_codec *codec, hda_nid_t nid,
			       unsigned int pin_type)
{
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    pin_type);
	/* unmute pin */
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    AMP_OUT_UNMUTE);
}

static void alc880_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid, int pin_type,
					      int dac_idx)
{
	alc_set_pin_output(codec, nid, pin_type);
	/* need the manual connection? */
	if (alc880_is_multi_pin(nid)) {
		struct alc_spec *spec = codec->spec;
		int idx = alc880_multi_pin_idx(nid);
		snd_hda_codec_write(codec, alc880_idx_to_selector(idx), 0,
				    AC_VERB_SET_CONNECT_SEL,
				    alc880_dac_to_idx(spec->multiout.dac_nids[dac_idx]));
	}
}

static int get_pin_type(int line_out_type)
{
	if (line_out_type == AUTO_PIN_HP_OUT)
		return PIN_HP;
	else
		return PIN_OUT;
}

static void alc880_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	alc_subsystem_id(codec, 0x15, 0x1b, 0x14);
	for (i = 0; i < spec->autocfg.line_outs; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		alc880_auto_set_output_and_unmute(codec, nid, pin_type, i);
	}
}

static void alc880_auto_init_extra_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.speaker_pins[0];
	if (pin) /* connect to front */
		alc880_auto_set_output_and_unmute(codec, pin, PIN_OUT, 0);
	pin = spec->autocfg.hp_pins[0];
	if (pin) /* connect to front */
		alc880_auto_set_output_and_unmute(codec, pin, PIN_HP, 0);
}

static void alc880_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];
		if (alc880_is_input_pin(nid)) {
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    i <= AUTO_PIN_FRONT_MIC ?
					    PIN_VREF80 : PIN_IN);
			if (nid != ALC880_PIN_CD_NID)
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_AMP_GAIN_MUTE,
						    AMP_OUT_MUTE);
		}
	}
}

/* parse the BIOS configuration and set up the alc_spec */
/* return 1 if successful, 0 if the proper config is not found,
 * or a negative error code
 */
static int alc880_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static hda_nid_t alc880_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc880_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */

	err = alc880_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc880_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc880_auto_create_extra_out(spec,
					   spec->autocfg.speaker_pins[0],
					   "Speaker");
	if (err < 0)
		return err;
	err = alc880_auto_create_extra_out(spec, spec->autocfg.hp_pins[0],
					   "Headphone");
	if (err < 0)
		return err;
	err = alc880_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC880_DIGOUT_NID;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = ALC880_DIGIN_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->init_verbs[spec->num_init_verbs++] = alc880_volume_init_verbs;

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;

	return 1;
}

/* additional initialization for auto-configuration model */
static void alc880_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc880_auto_init_multi_out(codec);
	alc880_auto_init_extra_out(codec);
	alc880_auto_init_analog_input(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

/*
 * OK, here we have finally the patch for ALC880
 */

static int patch_alc880(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	board_config = snd_hda_check_board_config(codec, ALC880_MODEL_LAST,
						  alc880_models,
						  alc880_cfg_tbl);
	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC880, "
		       "trying auto-probe from BIOS...\n");
		board_config = ALC880_AUTO;
	}

	if (board_config == ALC880_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc880_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using 3-stack mode...\n");
			board_config = ALC880_3ST;
		}
	}

	if (board_config != ALC880_AUTO)
		setup_preset(spec, &alc880_presets[board_config]);

	spec->stream_name_analog = "ALC880 Analog";
	spec->stream_analog_playback = &alc880_pcm_analog_playback;
	spec->stream_analog_capture = &alc880_pcm_analog_capture;
	spec->stream_analog_alt_capture = &alc880_pcm_analog_alt_capture;

	spec->stream_name_digital = "ALC880 Digital";
	spec->stream_digital_playback = &alc880_pcm_digital_playback;
	spec->stream_digital_capture = &alc880_pcm_digital_capture;

	if (!spec->adc_nids && spec->input_mux) {
		/* check whether NID 0x07 is valid */
		unsigned int wcap = get_wcaps(codec, alc880_adc_nids[0]);
		/* get type */
		wcap = (wcap & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT;
		if (wcap != AC_WID_AUD_IN) {
			spec->adc_nids = alc880_adc_nids_alt;
			spec->num_adc_nids = ARRAY_SIZE(alc880_adc_nids_alt);
			spec->mixers[spec->num_mixers] =
				alc880_capture_alt_mixer;
			spec->num_mixers++;
		} else {
			spec->adc_nids = alc880_adc_nids;
			spec->num_adc_nids = ARRAY_SIZE(alc880_adc_nids);
			spec->mixers[spec->num_mixers] = alc880_capture_mixer;
			spec->num_mixers++;
		}
	}

	spec->vmaster_nid = 0x0c;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC880_AUTO)
		spec->init_hook = alc880_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc880_loopbacks;
#endif

	return 0;
}


/*
 * ALC260 support
 */

static hda_nid_t alc260_dac_nids[1] = {
	/* front */
	0x02,
};

static hda_nid_t alc260_adc_nids[1] = {
	/* ADC0 */
	0x04,
};

static hda_nid_t alc260_adc_nids_alt[1] = {
	/* ADC1 */
	0x05,
};

static hda_nid_t alc260_hp_adc_nids[2] = {
	/* ADC1, 0 */
	0x05, 0x04
};

/* NIDs used when simultaneous access to both ADCs makes sense.  Note that
 * alc260_capture_mixer assumes ADC0 (nid 0x04) is the first ADC.
 */
static hda_nid_t alc260_dual_adc_nids[2] = {
	/* ADC0, ADC1 */
	0x04, 0x05
};

#define ALC260_DIGOUT_NID	0x03
#define ALC260_DIGIN_NID	0x06

static struct hda_input_mux alc260_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

/* On Fujitsu S702x laptops capture only makes sense from Mic/LineIn jack,
 * headphone jack and the internal CD lines since these are the only pins at
 * which audio can appear.  For flexibility, also allow the option of
 * recording the mixer output on the second ADC (ADC0 doesn't have a
 * connection to the mixer output).
 */
static struct hda_input_mux alc260_fujitsu_capture_sources[2] = {
	{
		.num_items = 3,
		.items = {
			{ "Mic/Line", 0x0 },
			{ "CD", 0x4 },
			{ "Headphone", 0x2 },
		},
	},
	{
		.num_items = 4,
		.items = {
			{ "Mic/Line", 0x0 },
			{ "CD", 0x4 },
			{ "Headphone", 0x2 },
			{ "Mixer", 0x5 },
		},
	},

};

/* Acer TravelMate(/Extensa/Aspire) notebooks have similar configuration to
 * the Fujitsu S702x, but jacks are marked differently.
 */
static struct hda_input_mux alc260_acer_capture_sources[2] = {
	{
		.num_items = 4,
		.items = {
			{ "Mic", 0x0 },
			{ "Line", 0x2 },
			{ "CD", 0x4 },
			{ "Headphone", 0x5 },
		},
	},
	{
		.num_items = 5,
		.items = {
			{ "Mic", 0x0 },
			{ "Line", 0x2 },
			{ "CD", 0x4 },
			{ "Headphone", 0x6 },
			{ "Mixer", 0x5 },
		},
	},
};
/*
 * This is just place-holder, so there's something for alc_build_pcms to look
 * at when it calculates the maximum number of channels. ALC260 has no mixer
 * element which allows changing the channel mode, so the verb list is
 * never used.
 */
static struct hda_channel_mode alc260_modes[1] = {
	{ 2, NULL },
};


/* Mixer combinations
 *
 * basic: base_output + input + pc_beep + capture
 * HP: base_output + input + capture_alt
 * HP_3013: hp_3013 + input + capture
 * fujitsu: fujitsu + capture
 * acer: acer + capture
 */

static struct snd_kcontrol_new alc260_base_output_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x08, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x09, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x09, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Mono Playback Volume", 0x0a, 1, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Mono Playback Switch", 0x0a, 1, 2, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc260_input_mixer[] = {
	HDA_CODEC_VOLUME("CD Playback Volume", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x07, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x07, 0x01, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc260_pc_beep_mixer[] = {
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x07, 0x05, HDA_INPUT),
	{ } /* end */
};

/* update HP, line and mono out pins according to the master switch */
static void alc260_hp_master_update(struct hda_codec *codec,
				    hda_nid_t hp, hda_nid_t line,
				    hda_nid_t mono)
{
	struct alc_spec *spec = codec->spec;
	unsigned int val = spec->master_sw ? PIN_HP : 0;
	/* change HP and line-out pins */
	snd_hda_codec_write(codec, 0x0f, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    val);
	snd_hda_codec_write(codec, 0x10, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    val);
	/* mono (speaker) depending on the HP jack sense */
	val = (val && !spec->jack_present) ? PIN_OUT : 0;
	snd_hda_codec_write(codec, 0x11, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    val);
}

static int alc260_hp_master_sw_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	*ucontrol->value.integer.value = spec->master_sw;
	return 0;
}

static int alc260_hp_master_sw_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int val = !!*ucontrol->value.integer.value;
	hda_nid_t hp, line, mono;

	if (val == spec->master_sw)
		return 0;
	spec->master_sw = val;
	hp = (kcontrol->private_value >> 16) & 0xff;
	line = (kcontrol->private_value >> 8) & 0xff;
	mono = kcontrol->private_value & 0xff;
	alc260_hp_master_update(codec, hp, line, mono);
	return 1;
}

static struct snd_kcontrol_new alc260_hp_output_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_ctl_boolean_mono_info,
		.get = alc260_hp_master_sw_get,
		.put = alc260_hp_master_sw_put,
		.private_value = (0x0f << 16) | (0x10 << 8) | 0x11
	},
	HDA_CODEC_VOLUME("Front Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x08, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x09, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x09, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Speaker Playback Volume", 0x0a, 1, 0x0,
			      HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Speaker Playback Switch", 0x0a, 1, 2, HDA_INPUT),
	{ } /* end */
};

static struct hda_verb alc260_hp_unsol_verbs[] = {
	{0x10, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{},
};

static void alc260_hp_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x10, 0,
				     AC_VERB_GET_PIN_SENSE, 0);
	spec->jack_present = (present & AC_PINSENSE_PRESENCE) != 0;
	alc260_hp_master_update(codec, 0x0f, 0x10, 0x11);
}

static void alc260_hp_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc260_hp_automute(codec);
}

static struct snd_kcontrol_new alc260_hp_3013_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_ctl_boolean_mono_info,
		.get = alc260_hp_master_sw_get,
		.put = alc260_hp_master_sw_put,
		.private_value = (0x10 << 16) | (0x15 << 8) | 0x11
	},
	HDA_CODEC_VOLUME("Front Playback Volume", 0x09, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x10, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Aux-In Playback Volume", 0x07, 0x06, HDA_INPUT),
	HDA_CODEC_MUTE("Aux-In Playback Switch", 0x07, 0x06, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Speaker Playback Volume", 0x0a, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Speaker Playback Switch", 0x11, 1, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static struct hda_bind_ctls alc260_dc7600_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x08, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x09, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x0a, 3, 0, HDA_OUTPUT),
		0
	},
};

static struct hda_bind_ctls alc260_dc7600_bind_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x11, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x15, 3, 0, HDA_OUTPUT),
		0
	},
};

static struct snd_kcontrol_new alc260_hp_dc7600_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc260_dc7600_bind_master_vol),
	HDA_BIND_SW("LineOut Playback Switch", &alc260_dc7600_bind_switch),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x0f, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x10, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static struct hda_verb alc260_hp_3013_unsol_verbs[] = {
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{},
};

static void alc260_hp_3013_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x15, 0,
				     AC_VERB_GET_PIN_SENSE, 0);
	spec->jack_present = (present & AC_PINSENSE_PRESENCE) != 0;
	alc260_hp_master_update(codec, 0x10, 0x15, 0x11);
}

static void alc260_hp_3013_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc260_hp_3013_automute(codec);
}

static void alc260_hp_3012_automute(struct hda_codec *codec)
{
	unsigned int present, bits;

	present = snd_hda_codec_read(codec, 0x10, 0,
			AC_VERB_GET_PIN_SENSE, 0) & AC_PINSENSE_PRESENCE;

	bits = present ? 0 : PIN_OUT;
	snd_hda_codec_write(codec, 0x0f, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    bits);
	snd_hda_codec_write(codec, 0x11, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    bits);
	snd_hda_codec_write(codec, 0x15, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    bits);
}

static void alc260_hp_3012_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc260_hp_3012_automute(codec);
}

/* Fujitsu S702x series laptops.  ALC260 pin usage: Mic/Line jack = 0x12,
 * HP jack = 0x14, CD audio =  0x16, internal speaker = 0x10.
 */
static struct snd_kcontrol_new alc260_fujitsu_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x08, 2, HDA_INPUT),
	ALC_PIN_MODE("Headphone Jack Mode", 0x14, ALC_PIN_DIR_INOUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic/Line Playback Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic/Line Playback Switch", 0x07, 0x0, HDA_INPUT),
	ALC_PIN_MODE("Mic/Line Jack Mode", 0x12, ALC_PIN_DIR_IN),
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x09, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x09, 2, HDA_INPUT),
	{ } /* end */
};

/* Mixer for Acer TravelMate(/Extensa/Aspire) notebooks.  Note that current
 * versions of the ALC260 don't act on requests to enable mic bias from NID
 * 0x0f (used to drive the headphone jack in these laptops).  The ALC260
 * datasheet doesn't mention this restriction.  At this stage it's not clear
 * whether this behaviour is intentional or is a hardware bug in chip
 * revisions available in early 2006.  Therefore for now allow the
 * "Headphone Jack Mode" control to span all choices, but if it turns out
 * that the lack of mic bias for this NID is intentional we could change the
 * mode from ALC_PIN_DIR_INOUT to ALC_PIN_DIR_INOUT_NOMICBIAS.
 *
 * In addition, Acer TravelMate(/Extensa/Aspire) notebooks in early 2006
 * don't appear to make the mic bias available from the "line" jack, even
 * though the NID used for this jack (0x14) can supply it.  The theory is
 * that perhaps Acer have included blocking capacitors between the ALC260
 * and the output jack.  If this turns out to be the case for all such
 * models the "Line Jack Mode" mode could be changed from ALC_PIN_DIR_INOUT
 * to ALC_PIN_DIR_INOUT_NOMICBIAS.
 *
 * The C20x Tablet series have a mono internal speaker which is controlled
 * via the chip's Mono sum widget and pin complex, so include the necessary
 * controls for such models.  On models without a "mono speaker" the control
 * won't do anything.
 */
static struct snd_kcontrol_new alc260_acer_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Master Playback Switch", 0x08, 2, HDA_INPUT),
	ALC_PIN_MODE("Headphone Jack Mode", 0x0f, ALC_PIN_DIR_INOUT),
	HDA_CODEC_VOLUME_MONO("Speaker Playback Volume", 0x0a, 1, 0x0,
			      HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Speaker Playback Switch", 0x0a, 1, 2,
			   HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x07, 0x0, HDA_INPUT),
	ALC_PIN_MODE("Mic Jack Mode", 0x12, ALC_PIN_DIR_IN),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x07, 0x02, HDA_INPUT),
	ALC_PIN_MODE("Line Jack Mode", 0x14, ALC_PIN_DIR_INOUT),
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x07, 0x05, HDA_INPUT),
	{ } /* end */
};

/* Packard bell V7900  ALC260 pin usage: HP = 0x0f, Mic jack = 0x12,
 * Line In jack = 0x14, CD audio =  0x16, pc beep = 0x17.
 */
static struct snd_kcontrol_new alc260_will_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Master Playback Switch", 0x08, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x07, 0x0, HDA_INPUT),
	ALC_PIN_MODE("Mic Jack Mode", 0x12, ALC_PIN_DIR_IN),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x07, 0x02, HDA_INPUT),
	ALC_PIN_MODE("Line Jack Mode", 0x14, ALC_PIN_DIR_INOUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x07, 0x05, HDA_INPUT),
	{ } /* end */
};

/* Replacer 672V ALC260 pin usage: Mic jack = 0x12,
 * Line In jack = 0x14, ATAPI Mic = 0x13, speaker = 0x0f.
 */
static struct snd_kcontrol_new alc260_replacer_672v_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Master Playback Switch", 0x08, 0x2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x07, 0x0, HDA_INPUT),
	ALC_PIN_MODE("Mic Jack Mode", 0x12, ALC_PIN_DIR_IN),
	HDA_CODEC_VOLUME("ATAPI Mic Playback Volume", 0x07, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("ATATI Mic Playback Switch", 0x07, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x07, 0x02, HDA_INPUT),
	ALC_PIN_MODE("Line Jack Mode", 0x14, ALC_PIN_DIR_INOUT),
	{ } /* end */
};

/* capture mixer elements */
static struct snd_kcontrol_new alc260_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x04, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x04, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x05, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x05, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc260_capture_alt_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x05, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x05, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

/*
 * initialization verbs
 */
static struct hda_verb alc260_init_verbs[] = {
	/* Line In pin widget for input */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* CD pin widget for input */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	/* Mic2 (front panel) pin widget for input and vref at 80% */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	/* LINE-2 is used for line-out in rear */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* select line-out */
	{0x0e, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* LINE-OUT pin */
	{0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* enable HP */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* enable Mono */
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* mute capture amp left and right */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* set connection select to line in (default select for this ADC) */
	{0x04, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* mute capture amp left and right */
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* set connection select to line in (default select for this ADC) */
	{0x05, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* set vol=0 Line-Out mixer amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* unmute pin widget amp left and right (no gain on this amp) */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* set vol=0 HP mixer amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* unmute pin widget amp left and right (no gain on this amp) */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* set vol=0 Mono mixer amp left and right */
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* unmute pin widget amp left and right (no gain on this amp) */
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* unmute LINE-2 out pin */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Amp Indexes: CD = 0x04, Line In 1 = 0x02, Mic 1 = 0x00 &
	 * Line In 2 = 0x03
	 */
	/* mute analog inputs */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Amp Indexes: DAC = 0x01 & mixer = 0x00 */
	/* mute Front out path */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* mute Headphone out path */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* mute Mono out path */
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{ }
};

#if 0 /* should be identical with alc260_init_verbs? */
static struct hda_verb alc260_hp_init_verbs[] = {
	/* Headphone and output */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0},
	/* mono output */
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* Mic2 (front panel) pin widget for input and vref at 80% */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* Line In pin widget for input */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	/* Line-2 pin widget for output */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* CD pin widget for input */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	/* unmute amp left and right */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000},
	/* set connection select to line in (default select for this ADC) */
	{0x04, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* unmute Line-Out mixer amp left and right (volume = 0) */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* unmute HP mixer amp left and right (volume = 0) */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* Amp Indexes: CD = 0x04, Line In 1 = 0x02, Mic 1 = 0x00 &
	 * Line In 2 = 0x03
	 */
	/* mute analog inputs */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Amp Indexes: DAC = 0x01 & mixer = 0x00 */
	/* Unmute Front out path */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Unmute Headphone out path */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Unmute Mono out path */
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{ }
};
#endif

static struct hda_verb alc260_hp_3013_init_verbs[] = {
	/* Line out and output */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* mono output */
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	/* Mic1 (rear panel) pin widget for input and vref at 80% */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* Mic2 (front panel) pin widget for input and vref at 80% */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	/* Line In pin widget for input */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	/* Headphone pin widget for output */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0},
	/* CD pin widget for input */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	/* unmute amp left and right */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000},
	/* set connection select to line in (default select for this ADC) */
	{0x04, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* unmute Line-Out mixer amp left and right (volume = 0) */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* unmute HP mixer amp left and right (volume = 0) */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, 0xb000},
	/* mute pin widget amp left and right (no gain on this amp) */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	/* Amp Indexes: CD = 0x04, Line In 1 = 0x02, Mic 1 = 0x00 &
	 * Line In 2 = 0x03
	 */
	/* mute analog inputs */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Amp Indexes: DAC = 0x01 & mixer = 0x00 */
	/* Unmute Front out path */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Unmute Headphone out path */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	/* Unmute Mono out path */
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{ }
};

/* Initialisation sequence for ALC260 as configured in Fujitsu S702x
 * laptops.  ALC260 pin usage: Mic/Line jack = 0x12, HP jack = 0x14, CD
 * audio = 0x16, internal speaker = 0x10.
 */
static struct hda_verb alc260_fujitsu_init_verbs[] = {
	/* Disable all GPIOs */
	{0x01, AC_VERB_SET_GPIO_MASK, 0},
	/* Internal speaker is connected to headphone pin */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Headphone/Line-out jack connects to Line1 pin; make it an output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	/* Mic/Line-in jack is connected to mic1 pin, so make it an input */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* Ensure all other unused pins are disabled and muted. */
	{0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},

	/* Disable digital (SPDIF) pins */
	{0x03, AC_VERB_SET_DIGI_CONVERT_1, 0},
	{0x06, AC_VERB_SET_DIGI_CONVERT_1, 0},

	/* Ensure Line1 pin widget takes its input from the OUT1 sum bus
	 * when acting as an output.
	 */
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0},

	/* Start with output sum widgets muted and their output gains at min */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* Unmute HP pin widget amp left and right (no equiv mixer ctrl) */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Unmute Line1 pin widget output buffer since it starts as an output.
	 * If the pin mode is changed by the user the pin mode control will
	 * take care of enabling the pin's input/output buffers as needed.
	 * Therefore there's no need to enable the input buffer at this
	 * stage.
	 */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Unmute input buffer of pin widget used for Line-in (no equiv
	 * mixer ctrl)
	 */
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute capture amp left and right */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	/* Set ADC connection select to match default mixer setting - line
	 * in (on mic1 pin)
	 */
	{0x04, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Do the same for the second ADC: mute capture input amp and
	 * set ADC connection to line in (on mic1 pin)
	 */
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x05, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Mute all inputs to mixer widget (even unconnected ones) */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)}, /* mic1 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)}, /* mic2 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)}, /* line1 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)}, /* line2 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)}, /* CD pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)}, /* Beep-gen pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)}, /* Line-out pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)}, /* HP-pin pin */

	{ }
};

/* Initialisation sequence for ALC260 as configured in Acer TravelMate and
 * similar laptops (adapted from Fujitsu init verbs).
 */
static struct hda_verb alc260_acer_init_verbs[] = {
	/* On TravelMate laptops, GPIO 0 enables the internal speaker and
	 * the headphone jack.  Turn this on and rely on the standard mute
	 * methods whenever the user wants to turn these outputs off.
	 */
	{0x01, AC_VERB_SET_GPIO_MASK, 0x01},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x01},
	/* Internal speaker/Headphone jack is connected to Line-out pin */
	{0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Internal microphone/Mic jack is connected to Mic1 pin */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50},
	/* Line In jack is connected to Line1 pin */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	/* Some Acers (eg: C20x Tablets) use Mono pin for internal speaker */
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Ensure all other unused pins are disabled and muted. */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	/* Disable digital (SPDIF) pins */
	{0x03, AC_VERB_SET_DIGI_CONVERT_1, 0},
	{0x06, AC_VERB_SET_DIGI_CONVERT_1, 0},

	/* Ensure Mic1 and Line1 pin widgets take input from the OUT1 sum
	 * bus when acting as outputs.
	 */
	{0x0b, AC_VERB_SET_CONNECT_SEL, 0},
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0},

	/* Start with output sum widgets muted and their output gains at min */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* Unmute Line-out pin widget amp left and right
	 * (no equiv mixer ctrl)
	 */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Unmute mono pin widget amp output (no equiv mixer ctrl) */
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Unmute Mic1 and Line1 pin widget input buffers since they start as
	 * inputs. If the pin mode is changed by the user the pin mode control
	 * will take care of enabling the pin's input/output buffers as needed.
	 * Therefore there's no need to enable the input buffer at this
	 * stage.
	 */
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute capture amp left and right */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	/* Set ADC connection select to match default mixer setting - mic
	 * (on mic1 pin)
	 */
	{0x04, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Do similar with the second ADC: mute capture input amp and
	 * set ADC connection to mic to match ALSA's default state.
	 */
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x05, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Mute all inputs to mixer widget (even unconnected ones) */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)}, /* mic1 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)}, /* mic2 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)}, /* line1 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)}, /* line2 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)}, /* CD pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)}, /* Beep-gen pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)}, /* Line-out pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)}, /* HP-pin pin */

	{ }
};

static struct hda_verb alc260_will_verbs[] = {
	{0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x0b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x0f, AC_VERB_SET_EAPD_BTLENABLE, 0x02},
	{0x1a, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x1a, AC_VERB_SET_PROC_COEF, 0x3040},
	{}
};

static struct hda_verb alc260_replacer_672v_verbs[] = {
	{0x0f, AC_VERB_SET_EAPD_BTLENABLE, 0x02},
	{0x1a, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x1a, AC_VERB_SET_PROC_COEF, 0x3050},

	{0x01, AC_VERB_SET_GPIO_MASK, 0x01},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x00},

	{0x0f, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

/* toggle speaker-output according to the hp-jack state */
static void alc260_replacer_672v_automute(struct hda_codec *codec)
{
        unsigned int present;

	/* speaker --> GPIO Data 0, hp or spdif --> GPIO data 1 */
        present = snd_hda_codec_read(codec, 0x0f, 0,
                                     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	if (present) {
		snd_hda_codec_write_cache(codec, 0x01, 0,
					  AC_VERB_SET_GPIO_DATA, 1);
		snd_hda_codec_write_cache(codec, 0x0f, 0,
					  AC_VERB_SET_PIN_WIDGET_CONTROL,
					  PIN_HP);
	} else {
		snd_hda_codec_write_cache(codec, 0x01, 0,
					  AC_VERB_SET_GPIO_DATA, 0);
		snd_hda_codec_write_cache(codec, 0x0f, 0,
					  AC_VERB_SET_PIN_WIDGET_CONTROL,
					  PIN_OUT);
	}
}

static void alc260_replacer_672v_unsol_event(struct hda_codec *codec,
                                       unsigned int res)
{
        if ((res >> 26) == ALC880_HP_EVENT)
                alc260_replacer_672v_automute(codec);
}

static struct hda_verb alc260_hp_dc7600_verbs[] = {
	{0x05, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x10, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x11, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

/* Test configuration for debugging, modelled after the ALC880 test
 * configuration.
 */
#ifdef CONFIG_SND_DEBUG
static hda_nid_t alc260_test_dac_nids[1] = {
	0x02,
};
static hda_nid_t alc260_test_adc_nids[2] = {
	0x04, 0x05,
};
/* For testing the ALC260, each input MUX needs its own definition since
 * the signal assignments are different.  This assumes that the first ADC
 * is NID 0x04.
 */
static struct hda_input_mux alc260_test_capture_sources[2] = {
	{
		.num_items = 7,
		.items = {
			{ "MIC1 pin", 0x0 },
			{ "MIC2 pin", 0x1 },
			{ "LINE1 pin", 0x2 },
			{ "LINE2 pin", 0x3 },
			{ "CD pin", 0x4 },
			{ "LINE-OUT pin", 0x5 },
			{ "HP-OUT pin", 0x6 },
		},
        },
	{
		.num_items = 8,
		.items = {
			{ "MIC1 pin", 0x0 },
			{ "MIC2 pin", 0x1 },
			{ "LINE1 pin", 0x2 },
			{ "LINE2 pin", 0x3 },
			{ "CD pin", 0x4 },
			{ "Mixer", 0x5 },
			{ "LINE-OUT pin", 0x6 },
			{ "HP-OUT pin", 0x7 },
		},
        },
};
static struct snd_kcontrol_new alc260_test_mixer[] = {
	/* Output driver widgets */
	HDA_CODEC_VOLUME_MONO("Mono Playback Volume", 0x0a, 1, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Mono Playback Switch", 0x0a, 1, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("LOUT2 Playback Volume", 0x09, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("LOUT2 Playback Switch", 0x09, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("LOUT1 Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("LOUT1 Playback Switch", 0x08, 2, HDA_INPUT),

	/* Modes for retasking pin widgets
	 * Note: the ALC260 doesn't seem to act on requests to enable mic
         * bias from NIDs 0x0f and 0x10.  The ALC260 datasheet doesn't
         * mention this restriction.  At this stage it's not clear whether
         * this behaviour is intentional or is a hardware bug in chip
         * revisions available at least up until early 2006.  Therefore for
         * now allow the "HP-OUT" and "LINE-OUT" Mode controls to span all
         * choices, but if it turns out that the lack of mic bias for these
         * NIDs is intentional we could change their modes from
         * ALC_PIN_DIR_INOUT to ALC_PIN_DIR_INOUT_NOMICBIAS.
	 */
	ALC_PIN_MODE("HP-OUT pin mode", 0x10, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("LINE-OUT pin mode", 0x0f, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("LINE2 pin mode", 0x15, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("LINE1 pin mode", 0x14, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("MIC2 pin mode", 0x13, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("MIC1 pin mode", 0x12, ALC_PIN_DIR_INOUT),

	/* Loopback mixer controls */
	HDA_CODEC_VOLUME("MIC1 Playback Volume", 0x07, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("MIC1 Playback Switch", 0x07, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("MIC2 Playback Volume", 0x07, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("MIC2 Playback Switch", 0x07, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("LINE1 Playback Volume", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("LINE1 Playback Switch", 0x07, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("LINE2 Playback Volume", 0x07, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("LINE2 Playback Switch", 0x07, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x07, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("LINE-OUT loopback Playback Volume", 0x07, 0x06, HDA_INPUT),
	HDA_CODEC_MUTE("LINE-OUT loopback Playback Switch", 0x07, 0x06, HDA_INPUT),
	HDA_CODEC_VOLUME("HP-OUT loopback Playback Volume", 0x07, 0x7, HDA_INPUT),
	HDA_CODEC_MUTE("HP-OUT loopback Playback Switch", 0x07, 0x7, HDA_INPUT),

	/* Controls for GPIO pins, assuming they are configured as outputs */
	ALC_GPIO_DATA_SWITCH("GPIO pin 0", 0x01, 0x01),
	ALC_GPIO_DATA_SWITCH("GPIO pin 1", 0x01, 0x02),
	ALC_GPIO_DATA_SWITCH("GPIO pin 2", 0x01, 0x04),
	ALC_GPIO_DATA_SWITCH("GPIO pin 3", 0x01, 0x08),

	/* Switches to allow the digital IO pins to be enabled.  The datasheet
	 * is ambigious as to which NID is which; testing on laptops which
	 * make this output available should provide clarification.
	 */
	ALC_SPDIF_CTRL_SWITCH("SPDIF Playback Switch", 0x03, 0x01),
	ALC_SPDIF_CTRL_SWITCH("SPDIF Capture Switch", 0x06, 0x01),

	/* A switch allowing EAPD to be enabled.  Some laptops seem to use
	 * this output to turn on an external amplifier.
	 */
	ALC_EAPD_CTRL_SWITCH("LINE-OUT EAPD Enable Switch", 0x0f, 0x02),
	ALC_EAPD_CTRL_SWITCH("HP-OUT EAPD Enable Switch", 0x10, 0x02),

	{ } /* end */
};
static struct hda_verb alc260_test_init_verbs[] = {
	/* Enable all GPIOs as outputs with an initial value of 0 */
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x0f},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x00},
	{0x01, AC_VERB_SET_GPIO_MASK, 0x0f},

	/* Enable retasking pins as output, initially without power amp */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* Disable digital (SPDIF) pins initially, but users can enable
	 * them via a mixer switch.  In the case of SPDIF-out, this initverb
	 * payload also sets the generation to 0, output to be in "consumer"
	 * PCM format, copyright asserted, no pre-emphasis and no validity
	 * control.
	 */
	{0x03, AC_VERB_SET_DIGI_CONVERT_1, 0},
	{0x06, AC_VERB_SET_DIGI_CONVERT_1, 0},

	/* Ensure mic1, mic2, line1 and line2 pin widgets take input from the
	 * OUT1 sum bus when acting as an output.
	 */
	{0x0b, AC_VERB_SET_CONNECT_SEL, 0},
	{0x0c, AC_VERB_SET_CONNECT_SEL, 0},
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0},
	{0x0e, AC_VERB_SET_CONNECT_SEL, 0},

	/* Start with output sum widgets muted and their output gains at min */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* Unmute retasking pin widget output buffers since the default
	 * state appears to be output.  As the pin mode is changed by the
	 * user the pin mode control will take care of enabling the pin's
	 * input/output buffers as needed.
	 */
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Also unmute the mono-out pin widget */
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Mute capture amp left and right */
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	/* Set ADC connection select to match default mixer setting (mic1
	 * pin)
	 */
	{0x04, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Do the same for the second ADC: mute capture input amp and
	 * set ADC connection to mic1 pin
	 */
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x05, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Mute all inputs to mixer widget (even unconnected ones) */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)}, /* mic1 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)}, /* mic2 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)}, /* line1 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)}, /* line2 pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)}, /* CD pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)}, /* Beep-gen pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)}, /* Line-out pin */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)}, /* HP-pin pin */

	{ }
};
#endif

#define alc260_pcm_analog_playback	alc880_pcm_analog_alt_playback
#define alc260_pcm_analog_capture	alc880_pcm_analog_capture

#define alc260_pcm_digital_playback	alc880_pcm_digital_playback
#define alc260_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * for BIOS auto-configuration
 */

static int alc260_add_playback_controls(struct alc_spec *spec, hda_nid_t nid,
					const char *pfx)
{
	hda_nid_t nid_vol;
	unsigned long vol_val, sw_val;
	char name[32];
	int err;

	if (nid >= 0x0f && nid < 0x11) {
		nid_vol = nid - 0x7;
		vol_val = HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0, HDA_OUTPUT);
		sw_val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
	} else if (nid == 0x11) {
		nid_vol = nid - 0x7;
		vol_val = HDA_COMPOSE_AMP_VAL(nid_vol, 2, 0, HDA_OUTPUT);
		sw_val = HDA_COMPOSE_AMP_VAL(nid, 2, 0, HDA_OUTPUT);
	} else if (nid >= 0x12 && nid <= 0x15) {
		nid_vol = 0x08;
		vol_val = HDA_COMPOSE_AMP_VAL(nid_vol, 3, 0, HDA_OUTPUT);
		sw_val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
	} else
		return 0; /* N/A */

	snprintf(name, sizeof(name), "%s Playback Volume", pfx);
	err = add_control(spec, ALC_CTL_WIDGET_VOL, name, vol_val);
	if (err < 0)
		return err;
	snprintf(name, sizeof(name), "%s Playback Switch", pfx);
	err = add_control(spec, ALC_CTL_WIDGET_MUTE, name, sw_val);
	if (err < 0)
		return err;
	return 1;
}

/* add playback controls from the parsed DAC table */
static int alc260_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	hda_nid_t nid;
	int err;

	spec->multiout.num_dacs = 1;
	spec->multiout.dac_nids = spec->private_dac_nids;
	spec->multiout.dac_nids[0] = 0x02;

	nid = cfg->line_out_pins[0];
	if (nid) {
		err = alc260_add_playback_controls(spec, nid, "Front");
		if (err < 0)
			return err;
	}

	nid = cfg->speaker_pins[0];
	if (nid) {
		err = alc260_add_playback_controls(spec, nid, "Speaker");
		if (err < 0)
			return err;
	}

	nid = cfg->hp_pins[0];
	if (nid) {
		err = alc260_add_playback_controls(spec, nid, "Headphone");
		if (err < 0)
			return err;
	}
	return 0;
}

/* create playback/capture controls for input pins */
static int alc260_auto_create_analog_input_ctls(struct alc_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	struct hda_input_mux *imux = &spec->private_imux;
	int i, err, idx;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (cfg->input_pins[i] >= 0x12) {
			idx = cfg->input_pins[i] - 0x12;
			err = new_analog_input(spec, cfg->input_pins[i],
					       auto_pin_cfg_labels[i], idx,
					       0x07);
			if (err < 0)
				return err;
			imux->items[imux->num_items].label =
				auto_pin_cfg_labels[i];
			imux->items[imux->num_items].index = idx;
			imux->num_items++;
		}
		if (cfg->input_pins[i] >= 0x0f && cfg->input_pins[i] <= 0x10){
			idx = cfg->input_pins[i] - 0x09;
			err = new_analog_input(spec, cfg->input_pins[i],
					       auto_pin_cfg_labels[i], idx,
					       0x07);
			if (err < 0)
				return err;
			imux->items[imux->num_items].label =
				auto_pin_cfg_labels[i];
			imux->items[imux->num_items].index = idx;
			imux->num_items++;
		}
	}
	return 0;
}

static void alc260_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid, int pin_type,
					      int sel_idx)
{
	alc_set_pin_output(codec, nid, pin_type);
	/* need the manual connection? */
	if (nid >= 0x12) {
		int idx = nid - 0x12;
		snd_hda_codec_write(codec, idx + 0x0b, 0,
				    AC_VERB_SET_CONNECT_SEL, sel_idx);
	}
}

static void alc260_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t nid;

	alc_subsystem_id(codec, 0x10, 0x15, 0x0f);
	nid = spec->autocfg.line_out_pins[0];
	if (nid) {
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		alc260_auto_set_output_and_unmute(codec, nid, pin_type, 0);
	}

	nid = spec->autocfg.speaker_pins[0];
	if (nid)
		alc260_auto_set_output_and_unmute(codec, nid, PIN_OUT, 0);

	nid = spec->autocfg.hp_pins[0];
	if (nid)
		alc260_auto_set_output_and_unmute(codec, nid, PIN_HP, 0);
}

#define ALC260_PIN_CD_NID		0x16
static void alc260_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];
		if (nid >= 0x12) {
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    i <= AUTO_PIN_FRONT_MIC ?
					    PIN_VREF80 : PIN_IN);
			if (nid != ALC260_PIN_CD_NID)
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_AMP_GAIN_MUTE,
						    AMP_OUT_MUTE);
		}
	}
}

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc260_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-1 and set the default input to mic-in
	 */
	{0x04, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x05, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for
	 * front panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	/* mute analog inputs */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/*
	 * Set up output mixers (0x08 - 0x0a)
	 */
	/* set vol=0 to output mixers */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{ }
};

static int alc260_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int wcap;
	int err;
	static hda_nid_t alc260_ignore[] = { 0x17, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc260_ignore);
	if (err < 0)
		return err;
	err = alc260_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	if (!spec->kctl_alloc)
		return 0; /* can't find valid BIOS pin config */
	err = alc260_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC260_DIGOUT_NID;
	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->init_verbs[spec->num_init_verbs++] = alc260_volume_init_verbs;

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;

	/* check whether NID 0x04 is valid */
	wcap = get_wcaps(codec, 0x04);
	wcap = (wcap & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT; /* get type */
	if (wcap != AC_WID_AUD_IN || spec->input_mux->num_items == 1) {
		spec->adc_nids = alc260_adc_nids_alt;
		spec->num_adc_nids = ARRAY_SIZE(alc260_adc_nids_alt);
		spec->mixers[spec->num_mixers] = alc260_capture_alt_mixer;
	} else {
		spec->adc_nids = alc260_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(alc260_adc_nids);
		spec->mixers[spec->num_mixers] = alc260_capture_mixer;
	}
	spec->num_mixers++;

	return 1;
}

/* additional initialization for auto-configuration model */
static void alc260_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc260_auto_init_multi_out(codec);
	alc260_auto_init_analog_input(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list alc260_loopbacks[] = {
	{ 0x07, HDA_INPUT, 0 },
	{ 0x07, HDA_INPUT, 1 },
	{ 0x07, HDA_INPUT, 2 },
	{ 0x07, HDA_INPUT, 3 },
	{ 0x07, HDA_INPUT, 4 },
	{ } /* end */
};
#endif

/*
 * ALC260 configurations
 */
static const char *alc260_models[ALC260_MODEL_LAST] = {
	[ALC260_BASIC]		= "basic",
	[ALC260_HP]		= "hp",
	[ALC260_HP_3013]	= "hp-3013",
	[ALC260_HP_DC7600]	= "hp-dc7600",
	[ALC260_FUJITSU_S702X]	= "fujitsu",
	[ALC260_ACER]		= "acer",
	[ALC260_WILL]		= "will",
	[ALC260_REPLACER_672V]	= "replacer",
#ifdef CONFIG_SND_DEBUG
	[ALC260_TEST]		= "test",
#endif
	[ALC260_AUTO]		= "auto",
};

static struct snd_pci_quirk alc260_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x007b, "Acer C20x", ALC260_ACER),
	SND_PCI_QUIRK(0x1025, 0x008f, "Acer", ALC260_ACER),
	SND_PCI_QUIRK(0x103c, 0x2808, "HP d5700", ALC260_HP_3013),
	SND_PCI_QUIRK(0x103c, 0x280a, "HP d5750", ALC260_HP_3013),
	SND_PCI_QUIRK(0x103c, 0x3010, "HP", ALC260_HP_3013),
	SND_PCI_QUIRK(0x103c, 0x3011, "HP", ALC260_HP_3013),
	SND_PCI_QUIRK(0x103c, 0x3012, "HP", ALC260_HP_DC7600),
	SND_PCI_QUIRK(0x103c, 0x3013, "HP", ALC260_HP_3013),
	SND_PCI_QUIRK(0x103c, 0x3014, "HP", ALC260_HP),
	SND_PCI_QUIRK(0x103c, 0x3015, "HP", ALC260_HP),
	SND_PCI_QUIRK(0x103c, 0x3016, "HP", ALC260_HP),
	SND_PCI_QUIRK(0x104d, 0x81bb, "Sony VAIO", ALC260_BASIC),
	SND_PCI_QUIRK(0x104d, 0x81cc, "Sony VAIO", ALC260_BASIC),
	SND_PCI_QUIRK(0x104d, 0x81cd, "Sony VAIO", ALC260_BASIC),
	SND_PCI_QUIRK(0x10cf, 0x1326, "Fujitsu S702X", ALC260_FUJITSU_S702X),
	SND_PCI_QUIRK(0x152d, 0x0729, "CTL U553W", ALC260_BASIC),
	SND_PCI_QUIRK(0x161f, 0x2057, "Replacer 672V", ALC260_REPLACER_672V),
	SND_PCI_QUIRK(0x1631, 0xc017, "PB V7900", ALC260_WILL),
	{}
};

static struct alc_config_preset alc260_presets[] = {
	[ALC260_BASIC] = {
		.mixers = { alc260_base_output_mixer,
			    alc260_input_mixer,
			    alc260_pc_beep_mixer,
			    alc260_capture_mixer },
		.init_verbs = { alc260_init_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_adc_nids),
		.adc_nids = alc260_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.input_mux = &alc260_capture_source,
	},
	[ALC260_HP] = {
		.mixers = { alc260_hp_output_mixer,
			    alc260_input_mixer,
			    alc260_capture_alt_mixer },
		.init_verbs = { alc260_init_verbs,
				alc260_hp_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_hp_adc_nids),
		.adc_nids = alc260_hp_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.input_mux = &alc260_capture_source,
		.unsol_event = alc260_hp_unsol_event,
		.init_hook = alc260_hp_automute,
	},
	[ALC260_HP_DC7600] = {
		.mixers = { alc260_hp_dc7600_mixer,
			    alc260_input_mixer,
			    alc260_capture_alt_mixer },
		.init_verbs = { alc260_init_verbs,
				alc260_hp_dc7600_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_hp_adc_nids),
		.adc_nids = alc260_hp_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.input_mux = &alc260_capture_source,
		.unsol_event = alc260_hp_3012_unsol_event,
		.init_hook = alc260_hp_3012_automute,
	},
	[ALC260_HP_3013] = {
		.mixers = { alc260_hp_3013_mixer,
			    alc260_input_mixer,
			    alc260_capture_alt_mixer },
		.init_verbs = { alc260_hp_3013_init_verbs,
				alc260_hp_3013_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_hp_adc_nids),
		.adc_nids = alc260_hp_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.input_mux = &alc260_capture_source,
		.unsol_event = alc260_hp_3013_unsol_event,
		.init_hook = alc260_hp_3013_automute,
	},
	[ALC260_FUJITSU_S702X] = {
		.mixers = { alc260_fujitsu_mixer,
			    alc260_capture_mixer },
		.init_verbs = { alc260_fujitsu_init_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_dual_adc_nids),
		.adc_nids = alc260_dual_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.num_mux_defs = ARRAY_SIZE(alc260_fujitsu_capture_sources),
		.input_mux = alc260_fujitsu_capture_sources,
	},
	[ALC260_ACER] = {
		.mixers = { alc260_acer_mixer,
			    alc260_capture_mixer },
		.init_verbs = { alc260_acer_init_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_dual_adc_nids),
		.adc_nids = alc260_dual_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.num_mux_defs = ARRAY_SIZE(alc260_acer_capture_sources),
		.input_mux = alc260_acer_capture_sources,
	},
	[ALC260_WILL] = {
		.mixers = { alc260_will_mixer,
			    alc260_capture_mixer },
		.init_verbs = { alc260_init_verbs, alc260_will_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_adc_nids),
		.adc_nids = alc260_adc_nids,
		.dig_out_nid = ALC260_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.input_mux = &alc260_capture_source,
	},
	[ALC260_REPLACER_672V] = {
		.mixers = { alc260_replacer_672v_mixer,
			    alc260_capture_mixer },
		.init_verbs = { alc260_init_verbs, alc260_replacer_672v_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_adc_nids),
		.adc_nids = alc260_adc_nids,
		.dig_out_nid = ALC260_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.input_mux = &alc260_capture_source,
		.unsol_event = alc260_replacer_672v_unsol_event,
		.init_hook = alc260_replacer_672v_automute,
	},
#ifdef CONFIG_SND_DEBUG
	[ALC260_TEST] = {
		.mixers = { alc260_test_mixer,
			    alc260_capture_mixer },
		.init_verbs = { alc260_test_init_verbs },
		.num_dacs = ARRAY_SIZE(alc260_test_dac_nids),
		.dac_nids = alc260_test_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_test_adc_nids),
		.adc_nids = alc260_test_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.num_mux_defs = ARRAY_SIZE(alc260_test_capture_sources),
		.input_mux = alc260_test_capture_sources,
	},
#endif
};

static int patch_alc260(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	board_config = snd_hda_check_board_config(codec, ALC260_MODEL_LAST,
						  alc260_models,
						  alc260_cfg_tbl);
	if (board_config < 0) {
		snd_printd(KERN_INFO "hda_codec: Unknown model for ALC260, "
			   "trying auto-probe from BIOS...\n");
		board_config = ALC260_AUTO;
	}

	if (board_config == ALC260_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc260_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC260_BASIC;
		}
	}

	if (board_config != ALC260_AUTO)
		setup_preset(spec, &alc260_presets[board_config]);

	spec->stream_name_analog = "ALC260 Analog";
	spec->stream_analog_playback = &alc260_pcm_analog_playback;
	spec->stream_analog_capture = &alc260_pcm_analog_capture;

	spec->stream_name_digital = "ALC260 Digital";
	spec->stream_digital_playback = &alc260_pcm_digital_playback;
	spec->stream_digital_capture = &alc260_pcm_digital_capture;

	spec->vmaster_nid = 0x08;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC260_AUTO)
		spec->init_hook = alc260_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc260_loopbacks;
#endif

	return 0;
}


/*
 * ALC882 support
 *
 * ALC882 is almost identical with ALC880 but has cleaner and more flexible
 * configuration.  Each pin widget can choose any input DACs and a mixer.
 * Each ADC is connected from a mixer of all inputs.  This makes possible
 * 6-channel independent captures.
 *
 * In addition, an independent DAC for the multi-playback (not used in this
 * driver yet).
 */
#define ALC882_DIGOUT_NID	0x06
#define ALC882_DIGIN_NID	0x0a

static struct hda_channel_mode alc882_ch_modes[1] = {
	{ 8, NULL }
};

static hda_nid_t alc882_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x03, 0x04, 0x05
};

/* identical with ALC880 */
#define alc882_adc_nids		alc880_adc_nids
#define alc882_adc_nids_alt	alc880_adc_nids_alt

static hda_nid_t alc882_capsrc_nids[3] = { 0x24, 0x23, 0x22 };
static hda_nid_t alc882_capsrc_nids_alt[2] = { 0x23, 0x22 };

/* input MUX */
/* FIXME: should be a matrix-type input source selection */

static struct hda_input_mux alc882_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};
#define alc882_mux_enum_info alc_mux_enum_info
#define alc882_mux_enum_get alc_mux_enum_get

static int alc882_mux_enum_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	const struct hda_input_mux *imux = spec->input_mux;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	hda_nid_t nid = spec->capsrc_nids ?
		spec->capsrc_nids[adc_idx] : spec->adc_nids[adc_idx];
	unsigned int *cur_val = &spec->cur_mux[adc_idx];
	unsigned int i, idx;

	idx = ucontrol->value.enumerated.item[0];
	if (idx >= imux->num_items)
		idx = imux->num_items - 1;
	if (*cur_val == idx)
		return 0;
	for (i = 0; i < imux->num_items; i++) {
		unsigned int v = (i == idx) ? 0 : HDA_AMP_MUTE;
		snd_hda_codec_amp_stereo(codec, nid, HDA_INPUT,
					 imux->items[i].index,
					 HDA_AMP_MUTE, v);
	}
	*cur_val = idx;
	return 1;
}

/*
 * 2ch mode
 */
static struct hda_verb alc882_3ST_ch2_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 6ch mode
 */
static struct hda_verb alc882_3ST_ch6_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static struct hda_channel_mode alc882_3ST_6ch_modes[2] = {
	{ 2, alc882_3ST_ch2_init },
	{ 6, alc882_3ST_ch6_init },
};

/*
 * 6ch mode
 */
static struct hda_verb alc882_sixstack_ch6_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

/*
 * 8ch mode
 */
static struct hda_verb alc882_sixstack_ch8_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

static struct hda_channel_mode alc882_sixstack_modes[2] = {
	{ 6, alc882_sixstack_ch6_init },
	{ 8, alc882_sixstack_ch8_init },
};

/*
 * macbook pro ALC885 can switch LineIn to LineOut without loosing Mic
 */

/*
 * 2ch mode
 */
static struct hda_verb alc885_mbp_ch2_init[] = {
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{ } /* end */
};

/*
 * 6ch mode
 */
static struct hda_verb alc885_mbp_ch6_init[] = {
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{ } /* end */
};

static struct hda_channel_mode alc885_mbp_6ch_modes[2] = {
	{ 2, alc885_mbp_ch2_init },
	{ 6, alc885_mbp_ch6_init },
};


/* Pin assignment: Front=0x14, Rear=0x15, CLFE=0x16, Side=0x17
 *                 Mic=0x18, Front Mic=0x19, Line-In=0x1a, HP=0x1b
 */
static struct snd_kcontrol_new alc882_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc885_mbp3_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Front Playback Switch", 0x0c, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE  ("Speaker Playback Switch", 0x14, 0x00, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line-Out Playback Volume", 0x0d, 0x00, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE  ("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE  ("Mic Playback Switch", 0x0b, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Boost", 0x1a, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0x00, HDA_INPUT),
	{ } /* end */
};
static struct snd_kcontrol_new alc882_w2jc_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc882_targa_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	{ } /* end */
};

/* Pin assignment: Front=0x14, HP = 0x15, Front = 0x16, ???
 *                 Front Mic=0x18, Line In = 0x1a, Line In = 0x1b, CD = 0x1c
 */
static struct snd_kcontrol_new alc882_asus_a7j_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Mobile Front Playback Switch", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mobile Line Playback Volume", 0x0b, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Mobile Line Playback Switch", 0x0b, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc882_asus_a7m_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc882_chmode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct hda_verb alc882_init_verbs[] = {
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Rear mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* CLFE mixer */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Side mixer */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	/* Front Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Rear Pin: output 1 (0x0d) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* CLFE Pin: output 2 (0x0e) */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* Side Pin: output 3 (0x0f) */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
	/* Mic (rear) pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line-2 In: Headphone output (output 0 - 0x0c) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* ADC1: mute amp left and right */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC2: mute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC3: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},

	{ }
};

static struct hda_verb alc882_eapd_verbs[] = {
	/* change to EAPD mode */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF, 0x3060},
	{ }
};

/* Mac Pro test */
static struct snd_kcontrol_new alc882_macpro_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x18, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x02, HDA_INPUT),
	{ } /* end */
};

static struct hda_verb alc882_macpro_init_verbs[] = {
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Front Pin: output 0 (0x0c) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Speaker:  output */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x04},
	/* Headphone output (output 0 - 0x0c) */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* ADC1: mute amp left and right */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC2: mute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC3: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},

	{ }
};

/* Macbook Pro rev3 */
static struct hda_verb alc885_mbp3_init_verbs[] = {
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Rear mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Front Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* HP Pin: output 0 (0x0d) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc4},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	/* Mic (rear) pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: use output 1 when in LineOut mode */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* ADC1: mute amp left and right */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC2: mute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC3: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},

	{ }
};

/* iMac 24 mixer. */
static struct snd_kcontrol_new alc885_imac24_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("Master Playback Switch", 0x0c, 0x00, HDA_INPUT),
	{ } /* end */
};

/* iMac 24 init verbs. */
static struct hda_verb alc885_imac24_init_verbs[] = {
	/* Internal speakers: output 0 (0x0c) */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Internal speakers: output 0 (0x0c) */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Headphone: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	/* Front Mic: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{ }
};

/* Toggle speaker-output according to the hp-jack state */
static void alc885_imac24_automute(struct hda_codec *codec)
{
 	unsigned int present;

 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x18, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x1a, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

/* Processes unsolicited events. */
static void alc885_imac24_unsol_event(struct hda_codec *codec,
				      unsigned int res)
{
	/* Headphone insertion or removal. */
	if ((res >> 26) == ALC880_HP_EVENT)
		alc885_imac24_automute(codec);
}

static void alc885_mbp3_automute(struct hda_codec *codec)
{
 	unsigned int present;

 	present = snd_hda_codec_read(codec, 0x15, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x14,  HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? 0 : HDA_AMP_MUTE);

}
static void alc885_mbp3_unsol_event(struct hda_codec *codec,
				    unsigned int res)
{
	/* Headphone insertion or removal. */
	if ((res >> 26) == ALC880_HP_EVENT)
		alc885_mbp3_automute(codec);
}


static struct hda_verb alc882_targa_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01}, /* line/surround */
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x01, AC_VERB_SET_GPIO_MASK, 0x03},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x03},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x03},
	{ } /* end */
};

/* toggle speaker-output according to the hp-jack state */
static void alc882_targa_automute(struct hda_codec *codec)
{
 	unsigned int present;

 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_write_cache(codec, 1, 0, AC_VERB_SET_GPIO_DATA,
				  present ? 1 : 3);
}

static void alc882_targa_unsol_event(struct hda_codec *codec, unsigned int res)
{
	/* Looks like the unsol event is incompatible with the standard
	 * definition.  4bit tag is placed at 26 bit!
	 */
	if (((res >> 26) == ALC880_HP_EVENT)) {
		alc882_targa_automute(codec);
	}
}

static struct hda_verb alc882_asus_a7j_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00}, /* Front */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x00}, /* Front */

	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01}, /* line/surround */
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{ } /* end */
};

static struct hda_verb alc882_asus_a7m_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00}, /* Front */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x00}, /* Front */

	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01}, /* line/surround */
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */
 	{ } /* end */
};

static void alc882_gpio_mute(struct hda_codec *codec, int pin, int muted)
{
	unsigned int gpiostate, gpiomask, gpiodir;

	gpiostate = snd_hda_codec_read(codec, codec->afg, 0,
				       AC_VERB_GET_GPIO_DATA, 0);

	if (!muted)
		gpiostate |= (1 << pin);
	else
		gpiostate &= ~(1 << pin);

	gpiomask = snd_hda_codec_read(codec, codec->afg, 0,
				      AC_VERB_GET_GPIO_MASK, 0);
	gpiomask |= (1 << pin);

	gpiodir = snd_hda_codec_read(codec, codec->afg, 0,
				     AC_VERB_GET_GPIO_DIRECTION, 0);
	gpiodir |= (1 << pin);


	snd_hda_codec_write(codec, codec->afg, 0,
			    AC_VERB_SET_GPIO_MASK, gpiomask);
	snd_hda_codec_write(codec, codec->afg, 0,
			    AC_VERB_SET_GPIO_DIRECTION, gpiodir);

	msleep(1);

	snd_hda_codec_write(codec, codec->afg, 0,
			    AC_VERB_SET_GPIO_DATA, gpiostate);
}

/* set up GPIO at initialization */
static void alc885_macpro_init_hook(struct hda_codec *codec)
{
	alc882_gpio_mute(codec, 0, 0);
	alc882_gpio_mute(codec, 1, 0);
}

/* set up GPIO and update auto-muting at initialization */
static void alc885_imac24_init_hook(struct hda_codec *codec)
{
	alc885_macpro_init_hook(codec);
	alc885_imac24_automute(codec);
}

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc882_auto_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for
	 * front panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/*
	 * Set up output mixers (0x0c - 0x0f)
	 */
	/* set vol=0 to output mixers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},

	{ }
};

/* capture mixer elements */
static struct snd_kcontrol_new alc882_capture_alt_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc882_mux_enum_info,
		.get = alc882_mux_enum_get,
		.put = alc882_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc882_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 2, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 2, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 3,
		.info = alc882_mux_enum_info,
		.get = alc882_mux_enum_get,
		.put = alc882_mux_enum_put,
	},
	{ } /* end */
};

#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc882_loopbacks	alc880_loopbacks
#endif

/* pcm configuration: identiacal with ALC880 */
#define alc882_pcm_analog_playback	alc880_pcm_analog_playback
#define alc882_pcm_analog_capture	alc880_pcm_analog_capture
#define alc882_pcm_digital_playback	alc880_pcm_digital_playback
#define alc882_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * configuration and preset
 */
static const char *alc882_models[ALC882_MODEL_LAST] = {
	[ALC882_3ST_DIG]	= "3stack-dig",
	[ALC882_6ST_DIG]	= "6stack-dig",
	[ALC882_ARIMA]		= "arima",
	[ALC882_W2JC]		= "w2jc",
	[ALC882_TARGA]		= "targa",
	[ALC882_ASUS_A7J]	= "asus-a7j",
	[ALC882_ASUS_A7M]	= "asus-a7m",
	[ALC885_MACPRO]		= "macpro",
	[ALC885_MBP3]		= "mbp3",
	[ALC885_IMAC24]		= "imac24",
	[ALC882_AUTO]		= "auto",
};

static struct snd_pci_quirk alc882_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x6668, "ECS", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x060d, "Asus A7J", ALC882_ASUS_A7J),
	SND_PCI_QUIRK(0x1043, 0x1243, "Asus A7J", ALC882_ASUS_A7J),
	SND_PCI_QUIRK(0x1043, 0x13c2, "Asus A7M", ALC882_ASUS_A7M),
	SND_PCI_QUIRK(0x1043, 0x1971, "Asus W2JC", ALC882_W2JC),
	SND_PCI_QUIRK(0x1043, 0x817f, "Asus P5LD2", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x81d8, "Asus P5WD", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x105b, 0x6668, "Foxconn", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1458, 0xa002, "Gigabyte P35 DS3R", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x28fb, "Targa T8", ALC882_TARGA), /* MSI-1049 T8  */
	SND_PCI_QUIRK(0x1462, 0x6668, "MSI", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x161f, 0x2054, "Arima W820", ALC882_ARIMA),
	{}
};

static struct alc_config_preset alc882_presets[] = {
	[ALC882_3ST_DIG] = {
		.mixers = { alc882_base_mixer },
		.init_verbs = { alc882_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc882_ch_modes),
		.channel_mode = alc882_ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
	},
	[ALC882_6ST_DIG] = {
		.mixers = { alc882_base_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc882_sixstack_modes),
		.channel_mode = alc882_sixstack_modes,
		.input_mux = &alc882_capture_source,
	},
	[ALC882_ARIMA] = {
		.mixers = { alc882_base_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_init_verbs, alc882_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc882_sixstack_modes),
		.channel_mode = alc882_sixstack_modes,
		.input_mux = &alc882_capture_source,
	},
	[ALC882_W2JC] = {
		.mixers = { alc882_w2jc_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_init_verbs, alc882_eapd_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
		.dig_out_nid = ALC882_DIGOUT_NID,
	},
	[ALC885_MBP3] = {
		.mixers = { alc885_mbp3_mixer, alc882_chmode_mixer },
		.init_verbs = { alc885_mbp3_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.channel_mode = alc885_mbp_6ch_modes,
		.num_channel_mode = ARRAY_SIZE(alc885_mbp_6ch_modes),
		.input_mux = &alc882_capture_source,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.unsol_event = alc885_mbp3_unsol_event,
		.init_hook = alc885_mbp3_automute,
	},
	[ALC885_MACPRO] = {
		.mixers = { alc882_macpro_mixer },
		.init_verbs = { alc882_macpro_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc882_ch_modes),
		.channel_mode = alc882_ch_modes,
		.input_mux = &alc882_capture_source,
		.init_hook = alc885_macpro_init_hook,
	},
	[ALC885_IMAC24] = {
		.mixers = { alc885_imac24_mixer },
		.init_verbs = { alc885_imac24_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc882_ch_modes),
		.channel_mode = alc882_ch_modes,
		.input_mux = &alc882_capture_source,
		.unsol_event = alc885_imac24_unsol_event,
		.init_hook = alc885_imac24_init_hook,
	},
	[ALC882_TARGA] = {
		.mixers = { alc882_targa_mixer, alc882_chmode_mixer,
			    alc882_capture_mixer },
		.init_verbs = { alc882_init_verbs, alc882_targa_verbs},
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.num_adc_nids = ARRAY_SIZE(alc882_adc_nids),
		.adc_nids = alc882_adc_nids,
		.capsrc_nids = alc882_capsrc_nids,
		.num_channel_mode = ARRAY_SIZE(alc882_3ST_6ch_modes),
		.channel_mode = alc882_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
		.unsol_event = alc882_targa_unsol_event,
		.init_hook = alc882_targa_automute,
	},
	[ALC882_ASUS_A7J] = {
		.mixers = { alc882_asus_a7j_mixer, alc882_chmode_mixer,
			    alc882_capture_mixer },
		.init_verbs = { alc882_init_verbs, alc882_asus_a7j_verbs},
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.num_adc_nids = ARRAY_SIZE(alc882_adc_nids),
		.adc_nids = alc882_adc_nids,
		.capsrc_nids = alc882_capsrc_nids,
		.num_channel_mode = ARRAY_SIZE(alc882_3ST_6ch_modes),
		.channel_mode = alc882_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
	},
	[ALC882_ASUS_A7M] = {
		.mixers = { alc882_asus_a7m_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_init_verbs, alc882_eapd_verbs,
				alc880_gpio1_init_verbs,
				alc882_asus_a7m_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
	},
};


/*
 * Pin config fixes
 */
enum {
	PINFIX_ABIT_AW9D_MAX
};

static struct alc_pincfg alc882_abit_aw9d_pinfix[] = {
	{ 0x15, 0x01080104 }, /* side */
	{ 0x16, 0x01011012 }, /* rear */
	{ 0x17, 0x01016011 }, /* clfe */
	{ }
};

static const struct alc_pincfg *alc882_pin_fixes[] = {
	[PINFIX_ABIT_AW9D_MAX] = alc882_abit_aw9d_pinfix,
};

static struct snd_pci_quirk alc882_pinfix_tbl[] = {
	SND_PCI_QUIRK(0x147b, 0x107a, "Abit AW9D-MAX", PINFIX_ABIT_AW9D_MAX),
	{}
};

/*
 * BIOS auto configuration
 */
static void alc882_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid, int pin_type,
					      int dac_idx)
{
	/* set as output */
	struct alc_spec *spec = codec->spec;
	int idx;

	alc_set_pin_output(codec, nid, pin_type);
	if (spec->multiout.dac_nids[dac_idx] == 0x25)
		idx = 4;
	else
		idx = spec->multiout.dac_nids[dac_idx] - 2;
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_CONNECT_SEL, idx);

}

static void alc882_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	alc_subsystem_id(codec, 0x15, 0x1b, 0x14);
	for (i = 0; i <= HDA_SIDE; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		if (nid)
			alc882_auto_set_output_and_unmute(codec, nid, pin_type,
							  i);
	}
}

static void alc882_auto_init_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pins[0];
	if (pin) /* connect to front */
		/* use dac 0 */
		alc882_auto_set_output_and_unmute(codec, pin, PIN_HP, 0);
	pin = spec->autocfg.speaker_pins[0];
	if (pin)
		alc882_auto_set_output_and_unmute(codec, pin, PIN_OUT, 0);
}

#define alc882_is_input_pin(nid)	alc880_is_input_pin(nid)
#define ALC882_PIN_CD_NID		ALC880_PIN_CD_NID

static void alc882_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];
		unsigned int vref;
		if (!nid)
			continue;
		vref = PIN_IN;
		if (1 /*i <= AUTO_PIN_FRONT_MIC*/) {
			unsigned int pincap;
			pincap = snd_hda_param_read(codec, nid, AC_PAR_PIN_CAP);
			if ((pincap >> AC_PINCAP_VREF_SHIFT) &
			    AC_PINCAP_VREF_80)
				vref = PIN_VREF80;
		}
		snd_hda_codec_write(codec, nid, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, vref);
		if (get_wcaps(codec, nid) & AC_WCAP_OUT_AMP)
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_AMP_GAIN_MUTE,
					    AMP_OUT_MUTE);
	}
}

static void alc882_auto_init_input_src(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	const struct hda_input_mux *imux = spec->input_mux;
	int c;

	for (c = 0; c < spec->num_adc_nids; c++) {
		hda_nid_t conn_list[HDA_MAX_NUM_INPUTS];
		hda_nid_t nid = spec->capsrc_nids[c];
		int conns, mute, idx, item;

		conns = snd_hda_get_connections(codec, nid, conn_list,
						ARRAY_SIZE(conn_list));
		if (conns < 0)
			continue;
		for (idx = 0; idx < conns; idx++) {
			/* if the current connection is the selected one,
			 * unmute it as default - otherwise mute it
			 */
			mute = AMP_IN_MUTE(idx);
			for (item = 0; item < imux->num_items; item++) {
				if (imux->items[item].index == idx) {
					if (spec->cur_mux[c] == item)
						mute = AMP_IN_UNMUTE(idx);
					break;
				}
			}
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_AMP_GAIN_MUTE, mute);
		}
	}
}

/* add mic boosts if needed */
static int alc_auto_add_mic_boost(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	hda_nid_t nid;

	nid = spec->autocfg.input_pins[AUTO_PIN_MIC];
	if (nid && (get_wcaps(codec, nid) & AC_WCAP_IN_AMP)) {
		err = add_control(spec, ALC_CTL_WIDGET_VOL,
				  "Mic Boost",
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_INPUT));
		if (err < 0)
			return err;
	}
	nid = spec->autocfg.input_pins[AUTO_PIN_FRONT_MIC];
	if (nid && (get_wcaps(codec, nid) & AC_WCAP_IN_AMP)) {
		err = add_control(spec, ALC_CTL_WIDGET_VOL,
				  "Front Mic Boost",
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_INPUT));
		if (err < 0)
			return err;
	}
	return 0;
}

/* almost identical with ALC880 parser... */
static int alc882_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err = alc880_parse_auto_config(codec);

	if (err < 0)
		return err;
	else if (!err)
		return 0; /* no config found */

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	/* hack - override the init verbs */
	spec->init_verbs[0] = alc882_auto_init_verbs;

	return 1; /* config found */
}

/* additional initialization for auto-configuration model */
static void alc882_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc882_auto_init_multi_out(codec);
	alc882_auto_init_hp_out(codec);
	alc882_auto_init_analog_input(codec);
	alc882_auto_init_input_src(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

static int patch_alc883(struct hda_codec *codec); /* called in patch_alc882() */

static int patch_alc882(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	board_config = snd_hda_check_board_config(codec, ALC882_MODEL_LAST,
						  alc882_models,
						  alc882_cfg_tbl);

	if (board_config < 0 || board_config >= ALC882_MODEL_LAST) {
		/* Pick up systems that don't supply PCI SSID */
		switch (codec->subsystem_id) {
		case 0x106b0c00: /* Mac Pro */
			board_config = ALC885_MACPRO;
			break;
		case 0x106b1000: /* iMac 24 */
			board_config = ALC885_IMAC24;
			break;
		case 0x106b00a1: /* Macbook (might be wrong - PCI SSID?) */
		case 0x106b2c00: /* Macbook Pro rev3 */
		case 0x106b3600: /* Macbook 3.1 */
			board_config = ALC885_MBP3;
			break;
		default:
			/* ALC889A is handled better as ALC888-compatible */
			if (codec->revision_id == 0x100103) {
				alc_free(codec);
				return patch_alc883(codec);
			}
			printk(KERN_INFO "hda_codec: Unknown model for ALC882, "
		       			 "trying auto-probe from BIOS...\n");
			board_config = ALC882_AUTO;
		}
	}

	alc_fix_pincfg(codec, alc882_pinfix_tbl, alc882_pin_fixes);

	if (board_config == ALC882_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc882_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC882_3ST_DIG;
		}
	}

	if (board_config != ALC882_AUTO)
		setup_preset(spec, &alc882_presets[board_config]);

	if (codec->vendor_id == 0x10ec0885) {
		spec->stream_name_analog = "ALC885 Analog";
		spec->stream_name_digital = "ALC885 Digital";
	} else {
		spec->stream_name_analog = "ALC882 Analog";
		spec->stream_name_digital = "ALC882 Digital";
	}

	spec->stream_analog_playback = &alc882_pcm_analog_playback;
	spec->stream_analog_capture = &alc882_pcm_analog_capture;
	/* FIXME: setup DAC5 */
	/*spec->stream_analog_alt_playback = &alc880_pcm_analog_alt_playback;*/
	spec->stream_analog_alt_capture = &alc880_pcm_analog_alt_capture;

	spec->stream_digital_playback = &alc882_pcm_digital_playback;
	spec->stream_digital_capture = &alc882_pcm_digital_capture;

	if (!spec->adc_nids && spec->input_mux) {
		/* check whether NID 0x07 is valid */
		unsigned int wcap = get_wcaps(codec, 0x07);
		/* get type */
		wcap = (wcap & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT;
		if (wcap != AC_WID_AUD_IN) {
			spec->adc_nids = alc882_adc_nids_alt;
			spec->num_adc_nids = ARRAY_SIZE(alc882_adc_nids_alt);
			spec->capsrc_nids = alc882_capsrc_nids_alt;
			spec->mixers[spec->num_mixers] =
				alc882_capture_alt_mixer;
			spec->num_mixers++;
		} else {
			spec->adc_nids = alc882_adc_nids;
			spec->num_adc_nids = ARRAY_SIZE(alc882_adc_nids);
			spec->capsrc_nids = alc882_capsrc_nids;
			spec->mixers[spec->num_mixers] = alc882_capture_mixer;
			spec->num_mixers++;
		}
	}

	spec->vmaster_nid = 0x0c;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC882_AUTO)
		spec->init_hook = alc882_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc882_loopbacks;
#endif

	return 0;
}

/*
 * ALC883 support
 *
 * ALC883 is almost identical with ALC880 but has cleaner and more flexible
 * configuration.  Each pin widget can choose any input DACs and a mixer.
 * Each ADC is connected from a mixer of all inputs.  This makes possible
 * 6-channel independent captures.
 *
 * In addition, an independent DAC for the multi-playback (not used in this
 * driver yet).
 */
#define ALC883_DIGOUT_NID	0x06
#define ALC883_DIGIN_NID	0x0a

static hda_nid_t alc883_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x03, 0x04, 0x05
};

static hda_nid_t alc883_adc_nids[2] = {
	/* ADC1-2 */
	0x08, 0x09,
};

static hda_nid_t alc883_capsrc_nids[2] = { 0x23, 0x22 };

/* input MUX */
/* FIXME: should be a matrix-type input source selection */

static struct hda_input_mux alc883_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static struct hda_input_mux alc883_3stack_6ch_intel = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x1 },
		{ "Front Mic", 0x0 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static struct hda_input_mux alc883_lenovo_101e_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x1 },
		{ "Line", 0x2 },
	},
};

static struct hda_input_mux alc883_lenovo_nb0763_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "iMic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static struct hda_input_mux alc883_fujitsu_pi2515_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x0 },
		{ "Int Mic", 0x1 },
	},
};

static struct hda_input_mux alc883_lenovo_sky_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x4 },
	},
};

static struct hda_input_mux alc883_asus_eee1601_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x0 },
		{ "Line", 0x2 },
	},
};

#define alc883_mux_enum_info alc_mux_enum_info
#define alc883_mux_enum_get alc_mux_enum_get
/* ALC883 has the ALC882-type input selection */
#define alc883_mux_enum_put alc882_mux_enum_put

/*
 * 2ch mode
 */
static struct hda_channel_mode alc883_3ST_2ch_modes[1] = {
	{ 2, NULL }
};

/*
 * 2ch mode
 */
static struct hda_verb alc883_3ST_ch2_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 4ch mode
 */
static struct hda_verb alc883_3ST_ch4_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

/*
 * 6ch mode
 */
static struct hda_verb alc883_3ST_ch6_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static struct hda_channel_mode alc883_3ST_6ch_modes[3] = {
	{ 2, alc883_3ST_ch2_init },
	{ 4, alc883_3ST_ch4_init },
	{ 6, alc883_3ST_ch6_init },
};

/*
 * 2ch mode
 */
static struct hda_verb alc883_3ST_ch2_intel_init[] = {
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 4ch mode
 */
static struct hda_verb alc883_3ST_ch4_intel_init[] = {
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

/*
 * 6ch mode
 */
static struct hda_verb alc883_3ST_ch6_intel_init[] = {
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x19, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static struct hda_channel_mode alc883_3ST_6ch_intel_modes[3] = {
	{ 2, alc883_3ST_ch2_intel_init },
	{ 4, alc883_3ST_ch4_intel_init },
	{ 6, alc883_3ST_ch6_intel_init },
};

/*
 * 6ch mode
 */
static struct hda_verb alc883_sixstack_ch6_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

/*
 * 8ch mode
 */
static struct hda_verb alc883_sixstack_ch8_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

static struct hda_channel_mode alc883_sixstack_modes[2] = {
	{ 6, alc883_sixstack_ch6_init },
	{ 8, alc883_sixstack_ch8_init },
};

static struct hda_verb alc883_medion_eapd_verbs[] = {
        /* eanable EAPD on medion laptop */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF, 0x3070},
	{ }
};

/* Pin assignment: Front=0x14, Rear=0x15, CLFE=0x16, Side=0x17
 *                 Mic=0x18, Front Mic=0x19, Line-In=0x1a, HP=0x1b
 */

static struct snd_kcontrol_new alc883_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_mitac_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_clevo_m720_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_2ch_fujitsu_pi2515_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_3ST_2ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_3ST_6ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_3ST_6ch_intel_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0,
			      HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_fivestack_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),

	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_tagra_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_tagra_2ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_lenovo_101e_2ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_lenovo_nb0763_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("iMic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("iMic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_medion_md2_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_acer_aspire_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc888_lenovo_sky_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0e, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0e, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume",
						0x0d, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0d, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0d, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0d, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("iSpeaker Playback Switch", 0x1a, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct hda_bind_ctls alc883_bind_cap_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x08, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x09, 3, 0, HDA_INPUT),
		0
	},
};

static struct hda_bind_ctls alc883_bind_cap_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x08, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x09, 3, 0, HDA_INPUT),
		0
	},
};

static struct snd_kcontrol_new alc883_asus_eee1601_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_BIND_VOL("Capture Volume", &alc883_bind_cap_vol),
	HDA_BIND_SW("Capture Switch", &alc883_bind_cap_switch),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc883_mux_enum_info,
		.get = alc883_mux_enum_get,
		.put = alc883_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc883_chmode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct hda_verb alc883_init_verbs[] = {
	/* ADC1: mute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC2: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Rear mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* CLFE mixer */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Side mixer */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	/* mute analog input loopbacks */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/* Front Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Rear Pin: output 1 (0x0d) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* CLFE Pin: output 2 (0x0e) */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* Side Pin: output 3 (0x0f) */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
	/* Mic (rear) pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line-2 In: Headphone output (output 0 - 0x0c) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc883_mitac_hp_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x15, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x17, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

/* auto-toggle front mic */
/*
static void alc883_mitac_mic_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x0b, HDA_INPUT, 1, HDA_AMP_MUTE, bits);
}
*/

static void alc883_mitac_automute(struct hda_codec *codec)
{
	alc883_mitac_hp_automute(codec);
	/* alc883_mitac_mic_automute(codec); */
}

static void alc883_mitac_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc883_mitac_hp_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		/* alc883_mitac_mic_automute(codec); */
		break;
	}
}

static struct hda_verb alc883_mitac_verbs[] = {
	/* HP */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Subwoofer */
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x02},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* enable unsolicited event */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	/* {0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_MIC_EVENT | AC_USRSP_EN}, */

	{ } /* end */
};

static struct hda_verb alc883_clevo_m720_verbs[] = {
	/* HP */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Int speaker */
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* enable unsolicited event */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_MIC_EVENT | AC_USRSP_EN},

	{ } /* end */
};

static struct hda_verb alc883_2ch_fujitsu_pi2515_verbs[] = {
	/* HP */
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Subwoofer */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* enable unsolicited event */
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},

	{ } /* end */
};

static struct hda_verb alc883_tagra_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01}, /* line/surround */
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x01, AC_VERB_SET_GPIO_MASK, 0x03},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x03},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x03},

	{ } /* end */
};

static struct hda_verb alc883_lenovo_101e_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_FRONT_EVENT|AC_USRSP_EN},
        {0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT|AC_USRSP_EN},
	{ } /* end */
};

static struct hda_verb alc883_lenovo_nb0763_verbs[] = {
        {0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
        {0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
        {0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{ } /* end */
};

static struct hda_verb alc888_lenovo_ms7195_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_FRONT_EVENT | AC_USRSP_EN},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT    | AC_USRSP_EN},
	{ } /* end */
};

static struct hda_verb alc883_haier_w66_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{ } /* end */
};

static struct hda_verb alc888_lenovo_sky_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ } /* end */
};

static struct hda_verb alc888_3st_hp_verbs[] = {
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},	/* Front: output 0 (0x0c) */
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x01},	/* Rear : output 1 (0x0d) */
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02},	/* CLFE : output 2 (0x0e) */
	{ }
};

static struct hda_verb alc888_6st_dell_verbs[] = {
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ }
};

static struct hda_verb alc888_3st_hp_2ch_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ }
};

static struct hda_verb alc888_3st_hp_6ch_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ }
};

static struct hda_channel_mode alc888_3st_hp_modes[2] = {
	{ 2, alc888_3st_hp_2ch_init },
	{ 6, alc888_3st_hp_6ch_init },
};

/* toggle front-jack and RCA according to the hp-jack state */
static void alc888_lenovo_ms7195_front_automute(struct hda_codec *codec)
{
 	unsigned int present;

 	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

/* toggle RCA according to the front-jack state */
static void alc888_lenovo_ms7195_rca_automute(struct hda_codec *codec)
{
 	unsigned int present;

 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

static void alc883_lenovo_ms7195_unsol_event(struct hda_codec *codec,
					     unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc888_lenovo_ms7195_front_automute(codec);
	if ((res >> 26) == ALC880_FRONT_EVENT)
		alc888_lenovo_ms7195_rca_automute(codec);
}

static struct hda_verb alc883_medion_md2_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ } /* end */
};

/* toggle speaker-output according to the hp-jack state */
static void alc883_medion_md2_automute(struct hda_codec *codec)
{
 	unsigned int present;

 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

static void alc883_medion_md2_unsol_event(struct hda_codec *codec,
					  unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc883_medion_md2_automute(codec);
}

/* toggle speaker-output according to the hp-jack state */
static void alc883_tagra_automute(struct hda_codec *codec)
{
 	unsigned int present;
	unsigned char bits;

 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	snd_hda_codec_write_cache(codec, 1, 0, AC_VERB_SET_GPIO_DATA,
				  present ? 1 : 3);
}

static void alc883_tagra_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc883_tagra_automute(codec);
}

/* toggle speaker-output according to the hp-jack state */
static void alc883_clevo_m720_hp_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x15, 0, AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc883_clevo_m720_mic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x0b, HDA_INPUT, 1,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

static void alc883_clevo_m720_automute(struct hda_codec *codec)
{
	alc883_clevo_m720_hp_automute(codec);
	alc883_clevo_m720_mic_automute(codec);
}

static void alc883_clevo_m720_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc883_clevo_m720_hp_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc883_clevo_m720_mic_automute(codec);
		break;
	}
}

/* toggle speaker-output according to the hp-jack state */
static void alc883_2ch_fujitsu_pi2515_automute(struct hda_codec *codec)
{
 	unsigned int present;
	unsigned char bits;

 	present = snd_hda_codec_read(codec, 0x14, 0, AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc883_2ch_fujitsu_pi2515_unsol_event(struct hda_codec *codec,
						  unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc883_2ch_fujitsu_pi2515_automute(codec);
}

static void alc883_haier_w66_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? 0x80 : 0;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 0x80, bits);
}

static void alc883_haier_w66_unsol_event(struct hda_codec *codec,
					 unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc883_haier_w66_automute(codec);
}

static void alc883_lenovo_101e_ispeaker_automute(struct hda_codec *codec)
{
 	unsigned int present;
	unsigned char bits;

 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc883_lenovo_101e_all_automute(struct hda_codec *codec)
{
 	unsigned int present;
	unsigned char bits;

 	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc883_lenovo_101e_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc883_lenovo_101e_all_automute(codec);
	if ((res >> 26) == ALC880_FRONT_EVENT)
		alc883_lenovo_101e_ispeaker_automute(codec);
}

/* toggle speaker-output according to the hp-jack state */
static void alc883_acer_aspire_automute(struct hda_codec *codec)
{
 	unsigned int present;

 	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x16, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

static void alc883_acer_aspire_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc883_acer_aspire_automute(codec);
}

static struct hda_verb alc883_acer_eapd_verbs[] = {
	/* HP Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Front Pin: output 0 (0x0c) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x00},
        /* eanable EAPD on medion laptop */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF, 0x3050},
	/* enable unsolicited event */
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ }
};

static void alc888_6st_dell_front_automute(struct hda_codec *codec)
{
 	unsigned int present;

 	present = snd_hda_codec_read(codec, 0x1b, 0,
				AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x16, HDA_OUTPUT, 0,
				HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x17, HDA_OUTPUT, 0,
				HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

static void alc888_6st_dell_unsol_event(struct hda_codec *codec,
					     unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		printk("hp_event\n");
		alc888_6st_dell_front_automute(codec);
		break;
	}
}

static void alc888_lenovo_sky_front_automute(struct hda_codec *codec)
{
	unsigned int mute;
	unsigned int present;

	snd_hda_codec_read(codec, 0x1b, 0, AC_VERB_SET_PIN_SENSE, 0);
	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0);
	present = (present & 0x80000000) != 0;
	if (present) {
		/* mute internal speaker */
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, HDA_AMP_MUTE);
		snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, HDA_AMP_MUTE);
		snd_hda_codec_amp_stereo(codec, 0x16, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, HDA_AMP_MUTE);
		snd_hda_codec_amp_stereo(codec, 0x17, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, HDA_AMP_MUTE);
		snd_hda_codec_amp_stereo(codec, 0x1a, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, HDA_AMP_MUTE);
	} else {
		/* unmute internal speaker if necessary */
		mute = snd_hda_codec_amp_read(codec, 0x1b, 0, HDA_OUTPUT, 0);
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
		snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
		snd_hda_codec_amp_stereo(codec, 0x16, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
		snd_hda_codec_amp_stereo(codec, 0x17, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
		snd_hda_codec_amp_stereo(codec, 0x1a, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
	}
}

static void alc883_lenovo_sky_unsol_event(struct hda_codec *codec,
					     unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc888_lenovo_sky_front_automute(codec);
}

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc883_auto_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for
	 * front panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/*
	 * Set up output mixers (0x0c - 0x0f)
	 */
	/* set vol=0 to output mixers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x26, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	/* {0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)}, */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},
	/* Input mixer2 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	/* {0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)}, */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	{ }
};

/* capture mixer elements */
static struct snd_kcontrol_new alc883_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc882_mux_enum_info,
		.get = alc882_mux_enum_get,
		.put = alc882_mux_enum_put,
	},
	{ } /* end */
};

static struct hda_verb alc888_asus_m90v_verbs[] = {
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* enable unsolicited event */
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_MIC_EVENT | AC_USRSP_EN},
	{ } /* end */
};

static void alc883_nb_mic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    0x7000 | (0x00 << 8) | (present ? 0 : 0x80));
	snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    0x7000 | (0x01 << 8) | (present ? 0x80 : 0));
}

static void alc883_M90V_speaker_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	bits = present ? 0 : PIN_OUT;
	snd_hda_codec_write(codec, 0x14, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    bits);
	snd_hda_codec_write(codec, 0x15, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    bits);
	snd_hda_codec_write(codec, 0x16, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    bits);
}

static void alc883_mode2_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc883_M90V_speaker_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc883_nb_mic_automute(codec);
		break;
	}
}

static void alc883_mode2_inithook(struct hda_codec *codec)
{
	alc883_M90V_speaker_automute(codec);
	alc883_nb_mic_automute(codec);
}

static struct hda_verb alc888_asus_eee1601_verbs[] = {
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x20, AC_VERB_SET_COEF_INDEX, 0x0b},
	{0x20, AC_VERB_SET_PROC_COEF,  0x0838},
	/* enable unsolicited event */
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ } /* end */
};

static void alc883_eee1601_speaker_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	bits = present ? 0 : PIN_OUT;
	snd_hda_codec_write(codec, 0x1b, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    bits);
}

static void alc883_eee1601_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc883_eee1601_speaker_automute(codec);
		break;
	}
}

static void alc883_eee1601_inithook(struct hda_codec *codec)
{
	alc883_eee1601_speaker_automute(codec);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc883_loopbacks	alc880_loopbacks
#endif

/* pcm configuration: identiacal with ALC880 */
#define alc883_pcm_analog_playback	alc880_pcm_analog_playback
#define alc883_pcm_analog_capture	alc880_pcm_analog_capture
#define alc883_pcm_analog_alt_capture	alc880_pcm_analog_alt_capture
#define alc883_pcm_digital_playback	alc880_pcm_digital_playback
#define alc883_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * configuration and preset
 */
static const char *alc883_models[ALC883_MODEL_LAST] = {
	[ALC883_3ST_2ch_DIG]	= "3stack-dig",
	[ALC883_3ST_6ch_DIG]	= "3stack-6ch-dig",
	[ALC883_3ST_6ch]	= "3stack-6ch",
	[ALC883_6ST_DIG]	= "6stack-dig",
	[ALC883_TARGA_DIG]	= "targa-dig",
	[ALC883_TARGA_2ch_DIG]	= "targa-2ch-dig",
	[ALC883_ACER]		= "acer",
	[ALC883_ACER_ASPIRE]	= "acer-aspire",
	[ALC883_MEDION]		= "medion",
	[ALC883_MEDION_MD2]	= "medion-md2",
	[ALC883_LAPTOP_EAPD]	= "laptop-eapd",
	[ALC883_LENOVO_101E_2ch] = "lenovo-101e",
	[ALC883_LENOVO_NB0763]	= "lenovo-nb0763",
	[ALC888_LENOVO_MS7195_DIG] = "lenovo-ms7195-dig",
	[ALC888_LENOVO_SKY] = "lenovo-sky",
	[ALC883_HAIER_W66] 	= "haier-w66",
	[ALC888_3ST_HP]		= "3stack-hp",
	[ALC888_6ST_DELL]	= "6stack-dell",
	[ALC883_MITAC]		= "mitac",
	[ALC883_CLEVO_M720]	= "clevo-m720",
	[ALC883_FUJITSU_PI2515] = "fujitsu-pi2515",
	[ALC883_3ST_6ch_INTEL]	= "3stack-6ch-intel",
	[ALC883_AUTO]		= "auto",
};

static struct snd_pci_quirk alc883_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x6668, "ECS", ALC883_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1025, 0x006c, "Acer Aspire 9810", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x0110, "Acer Aspire", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x0112, "Acer Aspire 9303", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x0121, "Acer Aspire 5920G", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0, "Acer laptop", ALC883_ACER), /* default Acer */
	SND_PCI_QUIRK(0x1028, 0x020d, "Dell Inspiron 530", ALC888_6ST_DELL),
	SND_PCI_QUIRK(0x103c, 0x2a3d, "HP Pavillion", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x103c, 0x2a4f, "HP Samba", ALC888_3ST_HP),
	SND_PCI_QUIRK(0x103c, 0x2a60, "HP Lucknow", ALC888_3ST_HP),
	SND_PCI_QUIRK(0x103c, 0x2a61, "HP Nettle", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x8249, "Asus M2A-VM HDMI", ALC883_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1043, 0x8317, "Asus M90V", ALC888_ASUS_M90V),
	SND_PCI_QUIRK(0x1043, 0x835f, "Asus Eee 1601", ALC888_ASUS_EEE1601),
	SND_PCI_QUIRK(0x105b, 0x0ce8, "Foxconn P35AX-S", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x105b, 0x6668, "Foxconn", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1071, 0x8253, "Mitac 8252d", ALC883_MITAC),
	SND_PCI_QUIRK(0x1071, 0x8258, "Evesham Voyaeger", ALC883_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x10f1, 0x2350, "TYAN-S2350", ALC888_6ST_DELL),
	SND_PCI_QUIRK(0x108e, 0x534d, NULL, ALC883_3ST_6ch),
	SND_PCI_QUIRK(0x1458, 0xa002, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x0349, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x040d, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x0579, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x2fb3, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x3729, "MSI S420", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3783, "NEC S970", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3b7f, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x3ef9, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fc1, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fc3, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fcc, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fdf, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x4314, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x4319, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x4324, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x6668, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7187, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7250, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7267, "MSI", ALC883_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x7280, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7327, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0xa422, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x147b, 0x1083, "Abit IP35-PRO", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1558, 0x0721, "Clevo laptop M720R", ALC883_CLEVO_M720),
	SND_PCI_QUIRK(0x1558, 0x0722, "Clevo laptop M720SR", ALC883_CLEVO_M720),
	SND_PCI_QUIRK(0x1558, 0, "Clevo laptop", ALC883_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x15d9, 0x8780, "Supermicro PDSBA", ALC883_3ST_6ch),
	SND_PCI_QUIRK(0x161f, 0x2054, "Medion laptop", ALC883_MEDION),
	SND_PCI_QUIRK(0x1734, 0x1108, "Fujitsu AMILO Pi2515", ALC883_FUJITSU_PI2515),
	SND_PCI_QUIRK(0x17aa, 0x101e, "Lenovo 101e", ALC883_LENOVO_101E_2ch),
	SND_PCI_QUIRK(0x17aa, 0x2085, "Lenovo NB0763", ALC883_LENOVO_NB0763),
	SND_PCI_QUIRK(0x17aa, 0x3bfc, "Lenovo NB0763", ALC883_LENOVO_NB0763),
	SND_PCI_QUIRK(0x17aa, 0x3bfd, "Lenovo NB0763", ALC883_LENOVO_NB0763),
	SND_PCI_QUIRK(0x17aa, 0x101d, "Lenovo Sky", ALC888_LENOVO_SKY),
	SND_PCI_QUIRK(0x17c0, 0x4071, "MEDION MD2", ALC883_MEDION_MD2),
	SND_PCI_QUIRK(0x17f2, 0x5000, "Albatron KI690-AM2", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1991, 0x5625, "Haier W66", ALC883_HAIER_W66),
	SND_PCI_QUIRK(0x8086, 0x0001, "DG33BUC", ALC883_3ST_6ch_INTEL),
	SND_PCI_QUIRK(0x8086, 0x0002, "DG33FBC", ALC883_3ST_6ch_INTEL),
	SND_PCI_QUIRK(0x8086, 0xd601, "D102GGC", ALC883_3ST_6ch),
	{}
};

static struct alc_config_preset alc883_presets[] = {
	[ALC883_3ST_2ch_DIG] = {
		.mixers = { alc883_3ST_2ch_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_3ST_6ch_DIG] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_3ST_6ch] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_3ST_6ch_INTEL] = {
		.mixers = { alc883_3ST_6ch_intel_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_intel_modes),
		.channel_mode = alc883_3ST_6ch_intel_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_3stack_6ch_intel,
	},
	[ALC883_6ST_DIG] = {
		.mixers = { alc883_base_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_sixstack_modes),
		.channel_mode = alc883_sixstack_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_TARGA_DIG] = {
		.mixers = { alc883_tagra_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc883_tagra_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_tagra_unsol_event,
		.init_hook = alc883_tagra_automute,
	},
	[ALC883_TARGA_2ch_DIG] = {
		.mixers = { alc883_tagra_2ch_mixer},
		.init_verbs = { alc883_init_verbs, alc883_tagra_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_tagra_unsol_event,
		.init_hook = alc883_tagra_automute,
	},
	[ALC883_ACER] = {
		.mixers = { alc883_base_mixer },
		/* On TravelMate laptops, GPIO 0 enables the internal speaker
		 * and the headphone jack.  Turn this on and rely on the
		 * standard mute methods whenever the user wants to turn
		 * these outputs off.
		 */
		.init_verbs = { alc883_init_verbs, alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_ACER_ASPIRE] = {
		.mixers = { alc883_acer_aspire_mixer },
		.init_verbs = { alc883_init_verbs, alc883_acer_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_acer_aspire_unsol_event,
		.init_hook = alc883_acer_aspire_automute,
	},
	[ALC883_MEDION] = {
		.mixers = { alc883_fivestack_mixer,
			    alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs,
				alc883_medion_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_sixstack_modes),
		.channel_mode = alc883_sixstack_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_MEDION_MD2] = {
		.mixers = { alc883_medion_md2_mixer},
		.init_verbs = { alc883_init_verbs, alc883_medion_md2_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_medion_md2_unsol_event,
		.init_hook = alc883_medion_md2_automute,
	},
	[ALC883_LAPTOP_EAPD] = {
		.mixers = { alc883_base_mixer },
		.init_verbs = { alc883_init_verbs, alc882_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_CLEVO_M720] = {
		.mixers = { alc883_clevo_m720_mixer },
		.init_verbs = { alc883_init_verbs, alc883_clevo_m720_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_clevo_m720_unsol_event,
		.init_hook = alc883_clevo_m720_automute,
	},
	[ALC883_LENOVO_101E_2ch] = {
		.mixers = { alc883_lenovo_101e_2ch_mixer},
		.init_verbs = { alc883_init_verbs, alc883_lenovo_101e_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_lenovo_101e_capture_source,
		.unsol_event = alc883_lenovo_101e_unsol_event,
		.init_hook = alc883_lenovo_101e_all_automute,
	},
	[ALC883_LENOVO_NB0763] = {
		.mixers = { alc883_lenovo_nb0763_mixer },
		.init_verbs = { alc883_init_verbs, alc883_lenovo_nb0763_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_lenovo_nb0763_capture_source,
		.unsol_event = alc883_medion_md2_unsol_event,
		.init_hook = alc883_medion_md2_automute,
	},
	[ALC888_LENOVO_MS7195_DIG] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc888_lenovo_ms7195_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_lenovo_ms7195_unsol_event,
		.init_hook = alc888_lenovo_ms7195_front_automute,
	},
	[ALC883_HAIER_W66] = {
		.mixers = { alc883_tagra_2ch_mixer},
		.init_verbs = { alc883_init_verbs, alc883_haier_w66_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_haier_w66_unsol_event,
		.init_hook = alc883_haier_w66_automute,
	},
	[ALC888_3ST_HP] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc888_3st_hp_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc888_3st_hp_modes),
		.channel_mode = alc888_3st_hp_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
	},
	[ALC888_6ST_DELL] = {
		.mixers = { alc883_base_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc888_6st_dell_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_sixstack_modes),
		.channel_mode = alc883_sixstack_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc888_6st_dell_unsol_event,
		.init_hook = alc888_6st_dell_front_automute,
	},
	[ALC883_MITAC] = {
		.mixers = { alc883_mitac_mixer },
		.init_verbs = { alc883_init_verbs, alc883_mitac_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_mitac_unsol_event,
		.init_hook = alc883_mitac_automute,
	},
	[ALC883_FUJITSU_PI2515] = {
		.mixers = { alc883_2ch_fujitsu_pi2515_mixer },
		.init_verbs = { alc883_init_verbs,
				alc883_2ch_fujitsu_pi2515_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_fujitsu_pi2515_capture_source,
		.unsol_event = alc883_2ch_fujitsu_pi2515_unsol_event,
		.init_hook = alc883_2ch_fujitsu_pi2515_automute,
	},
	[ALC888_LENOVO_SKY] = {
		.mixers = { alc888_lenovo_sky_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc888_lenovo_sky_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids),
		.adc_nids = alc883_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_sixstack_modes),
		.channel_mode = alc883_sixstack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_lenovo_sky_capture_source,
		.unsol_event = alc883_lenovo_sky_unsol_event,
		.init_hook = alc888_lenovo_sky_front_automute,
	},
	[ALC888_ASUS_M90V] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc888_asus_m90v_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_fujitsu_pi2515_capture_source,
		.unsol_event = alc883_mode2_unsol_event,
		.init_hook = alc883_mode2_inithook,
	},
	[ALC888_ASUS_EEE1601] = {
		.mixers = { alc883_asus_eee1601_mixer },
		.init_verbs = { alc883_init_verbs, alc888_asus_eee1601_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_asus_eee1601_capture_source,
		.unsol_event = alc883_eee1601_unsol_event,
		.init_hook = alc883_eee1601_inithook,
	},
};


/*
 * BIOS auto configuration
 */
static void alc883_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid, int pin_type,
					      int dac_idx)
{
	/* set as output */
	struct alc_spec *spec = codec->spec;
	int idx;

	alc_set_pin_output(codec, nid, pin_type);
	if (spec->multiout.dac_nids[dac_idx] == 0x25)
		idx = 4;
	else
		idx = spec->multiout.dac_nids[dac_idx] - 2;
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_CONNECT_SEL, idx);

}

static void alc883_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	alc_subsystem_id(codec, 0x15, 0x1b, 0x14);
	for (i = 0; i <= HDA_SIDE; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		if (nid)
			alc883_auto_set_output_and_unmute(codec, nid, pin_type,
							  i);
	}
}

static void alc883_auto_init_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pins[0];
	if (pin) /* connect to front */
		/* use dac 0 */
		alc883_auto_set_output_and_unmute(codec, pin, PIN_HP, 0);
	pin = spec->autocfg.speaker_pins[0];
	if (pin)
		alc883_auto_set_output_and_unmute(codec, pin, PIN_OUT, 0);
}

#define alc883_is_input_pin(nid)	alc880_is_input_pin(nid)
#define ALC883_PIN_CD_NID		ALC880_PIN_CD_NID

static void alc883_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];
		if (alc883_is_input_pin(nid)) {
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    (i <= AUTO_PIN_FRONT_MIC ?
					     PIN_VREF80 : PIN_IN));
			if (nid != ALC883_PIN_CD_NID)
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_AMP_GAIN_MUTE,
						    AMP_OUT_MUTE);
		}
	}
}

#define alc883_auto_init_input_src	alc882_auto_init_input_src

/* almost identical with ALC880 parser... */
static int alc883_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err = alc880_parse_auto_config(codec);

	if (err < 0)
		return err;
	else if (!err)
		return 0; /* no config found */

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	/* hack - override the init verbs */
	spec->init_verbs[0] = alc883_auto_init_verbs;
	spec->mixers[spec->num_mixers] = alc883_capture_mixer;
	spec->num_mixers++;

	return 1; /* config found */
}

/* additional initialization for auto-configuration model */
static void alc883_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc883_auto_init_multi_out(codec);
	alc883_auto_init_hp_out(codec);
	alc883_auto_init_analog_input(codec);
	alc883_auto_init_input_src(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

static int patch_alc883(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	alc_fix_pll_init(codec, 0x20, 0x0a, 10);

	board_config = snd_hda_check_board_config(codec, ALC883_MODEL_LAST,
						  alc883_models,
						  alc883_cfg_tbl);
	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC883, "
		       "trying auto-probe from BIOS...\n");
		board_config = ALC883_AUTO;
	}

	if (board_config == ALC883_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc883_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC883_3ST_2ch_DIG;
		}
	}

	if (board_config != ALC883_AUTO)
		setup_preset(spec, &alc883_presets[board_config]);

	switch (codec->vendor_id) {
	case 0x10ec0888:
		spec->stream_name_analog = "ALC888 Analog";
		spec->stream_name_digital = "ALC888 Digital";
		break;
	case 0x10ec0889:
		spec->stream_name_analog = "ALC889 Analog";
		spec->stream_name_digital = "ALC889 Digital";
		break;
	default:
		spec->stream_name_analog = "ALC883 Analog";
		spec->stream_name_digital = "ALC883 Digital";
		break;
	}

	spec->stream_analog_playback = &alc883_pcm_analog_playback;
	spec->stream_analog_capture = &alc883_pcm_analog_capture;
	spec->stream_analog_alt_capture = &alc883_pcm_analog_alt_capture;

	spec->stream_digital_playback = &alc883_pcm_digital_playback;
	spec->stream_digital_capture = &alc883_pcm_digital_capture;

	spec->num_adc_nids = ARRAY_SIZE(alc883_adc_nids);
	spec->adc_nids = alc883_adc_nids;
	spec->capsrc_nids = alc883_capsrc_nids;

	spec->vmaster_nid = 0x0c;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC883_AUTO)
		spec->init_hook = alc883_auto_init;

#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc883_loopbacks;
#endif

	return 0;
}

/*
 * ALC262 support
 */

#define ALC262_DIGOUT_NID	ALC880_DIGOUT_NID
#define ALC262_DIGIN_NID	ALC880_DIGIN_NID

#define alc262_dac_nids		alc260_dac_nids
#define alc262_adc_nids		alc882_adc_nids
#define alc262_adc_nids_alt	alc882_adc_nids_alt
#define alc262_capsrc_nids	alc882_capsrc_nids
#define alc262_capsrc_nids_alt	alc882_capsrc_nids_alt

#define alc262_modes		alc260_modes
#define alc262_capture_source	alc882_capture_source

static hda_nid_t alc262_dmic_adc_nids[1] = {
	/* ADC0 */
	0x09
};

static hda_nid_t alc262_dmic_capsrc_nids[1] = { 0x22 };

static struct snd_kcontrol_new alc262_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	/* HDA_CODEC_VOLUME("PC Beep Playback Volume", 0x0b, 0x05, HDA_INPUT),
	   HDA_CODEC_MUTE("PC Beep Playback Switch", 0x0b, 0x05, HDA_INPUT), */
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0D, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Mono Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Mono Playback Switch", 0x16, 2, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc262_hippo1_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	/* HDA_CODEC_VOLUME("PC Beep Playback Volume", 0x0b, 0x05, HDA_INPUT),
	   HDA_CODEC_MUTE("PC Beep Playback Switch", 0x0b, 0x05, HDA_INPUT), */
	/*HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0D, 0x0, HDA_OUTPUT),*/
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	{ } /* end */
};

/* update HP, line and mono-out pins according to the master switch */
static void alc262_hp_master_update(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int val = spec->master_sw;

	/* HP & line-out */
	snd_hda_codec_write_cache(codec, 0x1b, 0,
				  AC_VERB_SET_PIN_WIDGET_CONTROL,
				  val ? PIN_HP : 0);
	snd_hda_codec_write_cache(codec, 0x15, 0,
				  AC_VERB_SET_PIN_WIDGET_CONTROL,
				  val ? PIN_HP : 0);
	/* mono (speaker) depending on the HP jack sense */
	val = val && !spec->jack_present;
	snd_hda_codec_write_cache(codec, 0x16, 0,
				  AC_VERB_SET_PIN_WIDGET_CONTROL,
				  val ? PIN_OUT : 0);
}

static void alc262_hp_bpc_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int presence;
	presence = snd_hda_codec_read(codec, 0x1b, 0,
				      AC_VERB_GET_PIN_SENSE, 0);
	spec->jack_present = !!(presence & AC_PINSENSE_PRESENCE);
	alc262_hp_master_update(codec);
}

static void alc262_hp_bpc_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc262_hp_bpc_automute(codec);
}

static void alc262_hp_wildwest_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int presence;
	presence = snd_hda_codec_read(codec, 0x15, 0,
				      AC_VERB_GET_PIN_SENSE, 0);
	spec->jack_present = !!(presence & AC_PINSENSE_PRESENCE);
	alc262_hp_master_update(codec);
}

static void alc262_hp_wildwest_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc262_hp_wildwest_automute(codec);
}

static int alc262_hp_master_sw_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	*ucontrol->value.integer.value = spec->master_sw;
	return 0;
}

static int alc262_hp_master_sw_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int val = !!*ucontrol->value.integer.value;

	if (val == spec->master_sw)
		return 0;
	spec->master_sw = val;
	alc262_hp_master_update(codec);
	return 1;
}

static struct snd_kcontrol_new alc262_HP_BPC_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_ctl_boolean_mono_info,
		.get = alc262_hp_master_sw_get,
		.put = alc262_hp_master_sw_put,
	},
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Speaker Playback Volume", 0x0e, 2, 0x0,
			      HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Speaker Playback Switch", 0x16, 2, 0x0,
			    HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Beep Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Beep Playback Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("AUX IN Playback Volume", 0x0b, 0x06, HDA_INPUT),
	HDA_CODEC_MUTE("AUX IN Playback Switch", 0x0b, 0x06, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc262_HP_BPC_WildWest_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_ctl_boolean_mono_info,
		.get = alc262_hp_master_sw_get,
		.put = alc262_hp_master_sw_put,
	},
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Speaker Playback Volume", 0x0e, 2, 0x0,
			      HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Speaker Playback Switch", 0x16, 2, 0x0,
			    HDA_OUTPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x1a, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Beep Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Beep Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc262_HP_BPC_WildWest_option_mixer[] = {
	HDA_CODEC_VOLUME("Rear Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Rear Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Rear Mic Boost", 0x18, 0, HDA_INPUT),
	{ } /* end */
};

/* mute/unmute internal speaker according to the hp jack and mute state */
static void alc262_hp_t5735_automute(struct hda_codec *codec, int force)
{
	struct alc_spec *spec = codec->spec;

	if (force || !spec->sense_updated) {
		unsigned int present;
		present = snd_hda_codec_read(codec, 0x15, 0,
					     AC_VERB_GET_PIN_SENSE, 0);
		spec->jack_present = (present & AC_PINSENSE_PRESENCE) != 0;
		spec->sense_updated = 1;
	}
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_OUTPUT, 0, HDA_AMP_MUTE,
				 spec->jack_present ? HDA_AMP_MUTE : 0);
}

static void alc262_hp_t5735_unsol_event(struct hda_codec *codec,
					unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc262_hp_t5735_automute(codec, 1);
}

static void alc262_hp_t5735_init_hook(struct hda_codec *codec)
{
	alc262_hp_t5735_automute(codec, 1);
}

static struct snd_kcontrol_new alc262_hp_t5735_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	{ } /* end */
};

static struct hda_verb alc262_hp_t5735_verbs[] = {
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},

	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ }
};

static struct snd_kcontrol_new alc262_hp_rp5700_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0e, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x01, HDA_INPUT),
	{ } /* end */
};

static struct hda_verb alc262_hp_rp5700_verbs[] = {
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x00 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x00 << 8))},
	{}
};

static struct hda_input_mux alc262_hp_rp5700_capture_source = {
	.num_items = 1,
	.items = {
		{ "Line", 0x1 },
	},
};

/* bind hp and internal speaker mute (with plug check) */
static int alc262_sony_master_sw_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change;

	/* change hp mute */
	change = snd_hda_codec_amp_update(codec, 0x15, 0, HDA_OUTPUT, 0,
					  HDA_AMP_MUTE,
					  valp[0] ? 0 : HDA_AMP_MUTE);
	change |= snd_hda_codec_amp_update(codec, 0x15, 1, HDA_OUTPUT, 0,
					   HDA_AMP_MUTE,
					   valp[1] ? 0 : HDA_AMP_MUTE);
	if (change) {
		/* change speaker according to HP jack state */
		struct alc_spec *spec = codec->spec;
		unsigned int mute;
		if (spec->jack_present)
			mute = HDA_AMP_MUTE;
		else
			mute = snd_hda_codec_amp_read(codec, 0x15, 0,
						      HDA_OUTPUT, 0);
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
	}
	return change;
}

static struct snd_kcontrol_new alc262_sony_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = alc262_sony_master_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x15, 3, 0, HDA_OUTPUT),
	},
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("ATAPI Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("ATAPI Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc262_benq_t31_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("ATAPI Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("ATAPI Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	{ } /* end */
};

#define alc262_capture_mixer		alc882_capture_mixer
#define alc262_capture_alt_mixer	alc882_capture_alt_mixer

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc262_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for
	 * front panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/*
	 * Set up output mixers (0x0c - 0x0e)
	 */
	/* set vol=0 to output mixers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},

	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},

	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},

	{ }
};

static struct hda_verb alc262_eapd_verbs[] = {
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

static struct hda_verb alc262_hippo_unsol_verbs[] = {
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

static struct hda_verb alc262_hippo1_unsol_verbs[] = {
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},

	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

static struct hda_verb alc262_sony_unsol_verbs[] = {
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},	// Front Mic

	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

static struct hda_input_mux alc262_dmic_capture_source = {
	.num_items = 2,
	.items = {
		{ "Int DMic", 0x9 },
		{ "Mic", 0x0 },
	},
};

static struct snd_kcontrol_new alc262_toshiba_s06_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

static struct hda_verb alc262_toshiba_s06_verbs[] = {
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x22, AC_VERB_SET_CONNECT_SEL, 0x09},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static void alc262_dmic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0,
					AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_write(codec, 0x22, 0,
				AC_VERB_SET_CONNECT_SEL, present ? 0x0 : 0x09);
}

/* toggle speaker-output according to the hp-jack state */
static void alc262_toshiba_s06_speaker_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x15, 0,
					AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? 0 : PIN_OUT;
	snd_hda_codec_write(codec, 0x14, 0,
					AC_VERB_SET_PIN_WIDGET_CONTROL, bits);
}



/* unsolicited event for HP jack sensing */
static void alc262_toshiba_s06_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc262_toshiba_s06_speaker_automute(codec);
	if ((res >> 26) == ALC880_MIC_EVENT)
		alc262_dmic_automute(codec);

}

static void alc262_toshiba_s06_init_hook(struct hda_codec *codec)
{
	alc262_toshiba_s06_speaker_automute(codec);
	alc262_dmic_automute(codec);
}

/* mute/unmute internal speaker according to the hp jack and mute state */
static void alc262_hippo_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int mute;
	unsigned int present;

	/* need to execute and sync at first */
	snd_hda_codec_read(codec, 0x15, 0, AC_VERB_SET_PIN_SENSE, 0);
	present = snd_hda_codec_read(codec, 0x15, 0,
				     AC_VERB_GET_PIN_SENSE, 0);
	spec->jack_present = (present & 0x80000000) != 0;
	if (spec->jack_present) {
		/* mute internal speaker */
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, HDA_AMP_MUTE);
	} else {
		/* unmute internal speaker if necessary */
		mute = snd_hda_codec_amp_read(codec, 0x15, 0, HDA_OUTPUT, 0);
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
	}
}

/* unsolicited event for HP jack sensing */
static void alc262_hippo_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc262_hippo_automute(codec);
}

static void alc262_hippo1_automute(struct hda_codec *codec)
{
	unsigned int mute;
	unsigned int present;

	snd_hda_codec_read(codec, 0x1b, 0, AC_VERB_SET_PIN_SENSE, 0);
	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0);
	present = (present & 0x80000000) != 0;
	if (present) {
		/* mute internal speaker */
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, HDA_AMP_MUTE);
	} else {
		/* unmute internal speaker if necessary */
		mute = snd_hda_codec_amp_read(codec, 0x1b, 0, HDA_OUTPUT, 0);
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
	}
}

/* unsolicited event for HP jack sensing */
static void alc262_hippo1_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc262_hippo1_automute(codec);
}

/*
 * nec model
 *  0x15 = headphone
 *  0x16 = internal speaker
 *  0x18 = external mic
 */

static struct snd_kcontrol_new alc262_nec_mixer[] = {
	HDA_CODEC_VOLUME_MONO("Speaker Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Speaker Playback Switch", 0x16, 0, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),

	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static struct hda_verb alc262_nec_verbs[] = {
	/* Unmute Speaker */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Headphone */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	/* External mic to headphone */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* External mic to speaker */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{}
};

/*
 * fujitsu model
 *  0x14 = headphone/spdif-out, 0x15 = internal speaker,
 *  0x1b = port replicator headphone out
 */

#define ALC_HP_EVENT	0x37

static struct hda_verb alc262_fujitsu_unsol_verbs[] = {
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

static struct hda_verb alc262_lenovo_3000_unsol_verbs[] = {
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

static struct hda_input_mux alc262_fujitsu_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Int Mic", 0x1 },
		{ "CD", 0x4 },
	},
};

static struct hda_input_mux alc262_HP_capture_source = {
	.num_items = 5,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
		{ "AUX IN", 0x6 },
	},
};

static struct hda_input_mux alc262_HP_D7000_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x2 },
		{ "Line", 0x1 },
		{ "CD", 0x4 },
	},
};

/* mute/unmute internal speaker according to the hp jacks and mute state */
static void alc262_fujitsu_automute(struct hda_codec *codec, int force)
{
	struct alc_spec *spec = codec->spec;
	unsigned int mute;

	if (force || !spec->sense_updated) {
		unsigned int present;
		/* need to execute and sync at first */
		snd_hda_codec_read(codec, 0x14, 0, AC_VERB_SET_PIN_SENSE, 0);
		/* check laptop HP jack */
		present = snd_hda_codec_read(codec, 0x14, 0,
					     AC_VERB_GET_PIN_SENSE, 0);
		/* need to execute and sync at first */
		snd_hda_codec_read(codec, 0x1b, 0, AC_VERB_SET_PIN_SENSE, 0);
		/* check docking HP jack */
		present |= snd_hda_codec_read(codec, 0x1b, 0,
					      AC_VERB_GET_PIN_SENSE, 0);
		if (present & AC_PINSENSE_PRESENCE)
			spec->jack_present = 1;
		else
			spec->jack_present = 0;
		spec->sense_updated = 1;
	}
	/* unmute internal speaker only if both HPs are unplugged and
	 * master switch is on
	 */
	if (spec->jack_present)
		mute = HDA_AMP_MUTE;
	else
		mute = snd_hda_codec_amp_read(codec, 0x14, 0, HDA_OUTPUT, 0);
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, mute);
}

/* unsolicited event for HP jack sensing */
static void alc262_fujitsu_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) != ALC_HP_EVENT)
		return;
	alc262_fujitsu_automute(codec, 1);
}

static void alc262_fujitsu_init_hook(struct hda_codec *codec)
{
	alc262_fujitsu_automute(codec, 1);
}

/* bind volumes of both NID 0x0c and 0x0d */
static struct hda_bind_ctls alc262_fujitsu_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x0c, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x0d, 3, 0, HDA_OUTPUT),
		0
	},
};

/* mute/unmute internal speaker according to the hp jack and mute state */
static void alc262_lenovo_3000_automute(struct hda_codec *codec, int force)
{
	struct alc_spec *spec = codec->spec;
	unsigned int mute;

	if (force || !spec->sense_updated) {
		unsigned int present_int_hp;
		/* need to execute and sync at first */
		snd_hda_codec_read(codec, 0x1b, 0, AC_VERB_SET_PIN_SENSE, 0);
		present_int_hp = snd_hda_codec_read(codec, 0x1b, 0,
					AC_VERB_GET_PIN_SENSE, 0);
		spec->jack_present = (present_int_hp & 0x80000000) != 0;
		spec->sense_updated = 1;
	}
	if (spec->jack_present) {
		/* mute internal speaker */
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, HDA_AMP_MUTE);
		snd_hda_codec_amp_stereo(codec, 0x16, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, HDA_AMP_MUTE);
	} else {
		/* unmute internal speaker if necessary */
		mute = snd_hda_codec_amp_read(codec, 0x1b, 0, HDA_OUTPUT, 0);
		snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
		snd_hda_codec_amp_stereo(codec, 0x16, HDA_OUTPUT, 0,
					 HDA_AMP_MUTE, mute);
	}
}

/* unsolicited event for HP jack sensing */
static void alc262_lenovo_3000_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) != ALC_HP_EVENT)
		return;
	alc262_lenovo_3000_automute(codec, 1);
}

/* bind hp and internal speaker mute (with plug check) */
static int alc262_fujitsu_master_sw_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change;

	change = snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE,
						 valp ? 0 : HDA_AMP_MUTE);
	change |= snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE,
						 valp ? 0 : HDA_AMP_MUTE);

	if (change)
		alc262_fujitsu_automute(codec, 0);
	return change;
}

static struct snd_kcontrol_new alc262_fujitsu_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc262_fujitsu_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = alc262_fujitsu_master_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
	},
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Switch", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

/* bind hp and internal speaker mute (with plug check) */
static int alc262_lenovo_3000_master_sw_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change;

	change = snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE,
						 valp ? 0 : HDA_AMP_MUTE);

	if (change)
		alc262_lenovo_3000_automute(codec, 0);
	return change;
}

static struct snd_kcontrol_new alc262_lenovo_3000_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc262_fujitsu_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = alc262_lenovo_3000_master_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x1b, 3, 0, HDA_OUTPUT),
	},
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

/* additional init verbs for Benq laptops */
static struct hda_verb alc262_EAPD_verbs[] = {
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3070},
	{}
};

static struct hda_verb alc262_benq_t31_EAPD_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},

	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3050},
	{}
};

/* Samsung Q1 Ultra Vista model setup */
static struct snd_kcontrol_new alc262_ultra_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Master Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Mic Boost", 0x15, 0, HDA_INPUT),
	{ } /* end */
};

static struct hda_verb alc262_ultra_verbs[] = {
	/* output mixer */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	/* speaker */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* HP */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	/* internal mic */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* ADC, choose mic */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(8)},
	{}
};

/* mute/unmute internal speaker according to the hp jack and mute state */
static void alc262_ultra_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int mute;

	mute = 0;
	/* auto-mute only when HP is used as HP */
	if (!spec->cur_mux[0]) {
		unsigned int present;
		/* need to execute and sync at first */
		snd_hda_codec_read(codec, 0x15, 0, AC_VERB_SET_PIN_SENSE, 0);
		present = snd_hda_codec_read(codec, 0x15, 0,
					     AC_VERB_GET_PIN_SENSE, 0);
		spec->jack_present = (present & AC_PINSENSE_PRESENCE) != 0;
		if (spec->jack_present)
			mute = HDA_AMP_MUTE;
	}
	/* mute/unmute internal speaker */
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, mute);
	/* mute/unmute HP */
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, mute ? 0 : HDA_AMP_MUTE);
}

/* unsolicited event for HP jack sensing */
static void alc262_ultra_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc262_ultra_automute(codec);
}

static struct hda_input_mux alc262_ultra_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x1 },
		{ "Headphone", 0x7 },
	},
};

static int alc262_ultra_mux_enum_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int ret;

	ret = alc882_mux_enum_put(kcontrol, ucontrol);
	if (!ret)
		return 0;
	/* reprogram the HP pin as mic or HP according to the input source */
	snd_hda_codec_write_cache(codec, 0x15, 0,
				  AC_VERB_SET_PIN_WIDGET_CONTROL,
				  spec->cur_mux[0] ? PIN_VREF80 : PIN_HP);
	alc262_ultra_automute(codec); /* mute/unmute HP */
	return ret;
}

static struct snd_kcontrol_new alc262_ultra_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x07, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = alc882_mux_enum_info,
		.get = alc882_mux_enum_get,
		.put = alc262_ultra_mux_enum_put,
	},
	{ } /* end */
};

/* add playback controls from the parsed DAC table */
static int alc262_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	hda_nid_t nid;
	int err;

	spec->multiout.num_dacs = 1;	/* only use one dac */
	spec->multiout.dac_nids = spec->private_dac_nids;
	spec->multiout.dac_nids[0] = 2;

	nid = cfg->line_out_pins[0];
	if (nid) {
		err = add_control(spec, ALC_CTL_WIDGET_VOL,
				  "Front Playback Volume",
				  HDA_COMPOSE_AMP_VAL(0x0c, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		err = add_control(spec, ALC_CTL_WIDGET_MUTE,
				  "Front Playback Switch",
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
	}

	nid = cfg->speaker_pins[0];
	if (nid) {
		if (nid == 0x16) {
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "Speaker Playback Volume",
					  HDA_COMPOSE_AMP_VAL(0x0e, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Speaker Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Speaker Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}
	nid = cfg->hp_pins[0];
	if (nid) {
		/* spec->multiout.hp_nid = 2; */
		if (nid == 0x16) {
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "Headphone Playback Volume",
					  HDA_COMPOSE_AMP_VAL(0x0e, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Headphone Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Headphone Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

/* identical with ALC880 */
#define alc262_auto_create_analog_input_ctls \
	alc880_auto_create_analog_input_ctls

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc262_volume_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for
	 * front panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/*
	 * Set up output mixers (0x0c - 0x0f)
	 */
	/* set vol=0 to output mixers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},

	{ }
};

static struct hda_verb alc262_HP_BPC_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for
	 * front panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)},
        {0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},

	/*
	 * Set up output mixers (0x0c - 0x0e)
	 */
	/* set vol=0 to output mixers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },

	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },

	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
        {0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},

	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0x7023 },
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
        {0x19, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, 0x7023 },
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },


	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x03 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x04 << 8))},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x03 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x04 << 8))},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x03 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x04 << 8))},

	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},

	{ }
};

static struct hda_verb alc262_HP_BPC_WildWest_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for front
	 * panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(6)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(7)},
	/*
	 * Set up output mixers (0x0c - 0x0e)
	 */
	/* set vol=0 to output mixers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},


	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },	/* HP */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },	/* Mono */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },	/* rear MIC */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },	/* Line in */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },	/* Front MIC */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },	/* Line out */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },	/* CD in */

	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },

	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},

	/* {0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0x7023 }, */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, 0x7023 },
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))}, /*rear MIC*/
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))}, /*Line in*/
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8))}, /*F MIC*/
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x03 << 8))}, /*Front*/
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x04 << 8))}, /*CD*/
        /* {0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x06 << 8))},  */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x07 << 8))}, /*HP*/
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x03 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x04 << 8))},
        /* {0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x06 << 8))}, */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x07 << 8))},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x03 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x04 << 8))},
        /* {0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x06 << 8))}, */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x07 << 8))},

	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},

	{ }
};

#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc262_loopbacks	alc880_loopbacks
#endif

/* pcm configuration: identiacal with ALC880 */
#define alc262_pcm_analog_playback	alc880_pcm_analog_playback
#define alc262_pcm_analog_capture	alc880_pcm_analog_capture
#define alc262_pcm_digital_playback	alc880_pcm_digital_playback
#define alc262_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * BIOS auto configuration
 */
static int alc262_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static hda_nid_t alc262_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc262_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */
	err = alc262_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc262_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC262_DIGOUT_NID;
	if (spec->autocfg.dig_in_pin)
		spec->dig_in_nid = ALC262_DIGIN_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->init_verbs[spec->num_init_verbs++] = alc262_volume_init_verbs;
	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	return 1;
}

#define alc262_auto_init_multi_out	alc882_auto_init_multi_out
#define alc262_auto_init_hp_out		alc882_auto_init_hp_out
#define alc262_auto_init_analog_input	alc882_auto_init_analog_input
#define alc262_auto_init_input_src	alc882_auto_init_input_src


/* init callback for auto-configuration model -- overriding the default init */
static void alc262_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc262_auto_init_multi_out(codec);
	alc262_auto_init_hp_out(codec);
	alc262_auto_init_analog_input(codec);
	alc262_auto_init_input_src(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

/*
 * configuration and preset
 */
static const char *alc262_models[ALC262_MODEL_LAST] = {
	[ALC262_BASIC]		= "basic",
	[ALC262_HIPPO]		= "hippo",
	[ALC262_HIPPO_1]	= "hippo_1",
	[ALC262_FUJITSU]	= "fujitsu",
	[ALC262_HP_BPC]		= "hp-bpc",
	[ALC262_HP_BPC_D7000_WL]= "hp-bpc-d7000",
	[ALC262_HP_TC_T5735]	= "hp-tc-t5735",
	[ALC262_HP_RP5700]	= "hp-rp5700",
	[ALC262_BENQ_ED8]	= "benq",
	[ALC262_BENQ_T31]	= "benq-t31",
	[ALC262_SONY_ASSAMD]	= "sony-assamd",
	[ALC262_TOSHIBA_S06]	= "toshiba-s06",
	[ALC262_ULTRA]		= "ultra",
	[ALC262_LENOVO_3000]	= "lenovo-3000",
	[ALC262_NEC]		= "nec",
	[ALC262_AUTO]		= "auto",
};

static struct snd_pci_quirk alc262_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1002, 0x437b, "Hippo", ALC262_HIPPO),
	SND_PCI_QUIRK(0x1033, 0x8895, "NEC Versa S9100", ALC262_NEC),
	SND_PCI_QUIRK(0x103c, 0x12fe, "HP xw9400", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x12ff, "HP xw4550", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x1306, "HP xw8600", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x1307, "HP xw6600", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x1308, "HP xw4600", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x1309, "HP xw4*00", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x130a, "HP xw6*00", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x130b, "HP xw8*00", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x2800, "HP D7000", ALC262_HP_BPC_D7000_WL),
	SND_PCI_QUIRK(0x103c, 0x2801, "HP D7000", ALC262_HP_BPC_D7000_WF),
	SND_PCI_QUIRK(0x103c, 0x2802, "HP D7000", ALC262_HP_BPC_D7000_WL),
	SND_PCI_QUIRK(0x103c, 0x2803, "HP D7000", ALC262_HP_BPC_D7000_WF),
	SND_PCI_QUIRK(0x103c, 0x2804, "HP D7000", ALC262_HP_BPC_D7000_WL),
	SND_PCI_QUIRK(0x103c, 0x2805, "HP D7000", ALC262_HP_BPC_D7000_WF),
	SND_PCI_QUIRK(0x103c, 0x2806, "HP D7000", ALC262_HP_BPC_D7000_WL),
	SND_PCI_QUIRK(0x103c, 0x2807, "HP D7000", ALC262_HP_BPC_D7000_WF),
	SND_PCI_QUIRK(0x103c, 0x280c, "HP xw4400", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x3014, "HP xw6400", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x3015, "HP xw8400", ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x302f, "HP Thin Client T5735",
		      ALC262_HP_TC_T5735),
	SND_PCI_QUIRK(0x103c, 0x2817, "HP RP5700", ALC262_HP_RP5700),
	SND_PCI_QUIRK(0x104d, 0x1f00, "Sony ASSAMD", ALC262_SONY_ASSAMD),
	SND_PCI_QUIRK(0x104d, 0x8203, "Sony UX-90", ALC262_HIPPO),
	SND_PCI_QUIRK(0x104d, 0x820f, "Sony ASSAMD", ALC262_SONY_ASSAMD),
	SND_PCI_QUIRK(0x104d, 0x900e, "Sony ASSAMD", ALC262_SONY_ASSAMD),
	SND_PCI_QUIRK(0x104d, 0x9015, "Sony 0x9015", ALC262_SONY_ASSAMD),
	SND_PCI_QUIRK(0x1179, 0x0001, "Toshiba dynabook SS RX1",
		      ALC262_SONY_ASSAMD),
	SND_PCI_QUIRK(0x1179, 0x0268, "Toshiba S06", ALC262_TOSHIBA_S06),
	SND_PCI_QUIRK(0x10cf, 0x1397, "Fujitsu", ALC262_FUJITSU),
	SND_PCI_QUIRK(0x10cf, 0x142d, "Fujitsu Lifebook E8410", ALC262_FUJITSU),
	SND_PCI_QUIRK(0x144d, 0xc032, "Samsung Q1 Ultra", ALC262_ULTRA),
	SND_PCI_QUIRK(0x144d, 0xc039, "Samsung Q1U EL", ALC262_ULTRA),
	SND_PCI_QUIRK(0x17aa, 0x384e, "Lenovo 3000 y410", ALC262_LENOVO_3000),
	SND_PCI_QUIRK(0x17ff, 0x0560, "Benq ED8", ALC262_BENQ_ED8),
	SND_PCI_QUIRK(0x17ff, 0x058d, "Benq T31-16", ALC262_BENQ_T31),
	SND_PCI_QUIRK(0x17ff, 0x058f, "Benq Hippo", ALC262_HIPPO_1),
	{}
};

static struct alc_config_preset alc262_presets[] = {
	[ALC262_BASIC] = {
		.mixers = { alc262_base_mixer },
		.init_verbs = { alc262_init_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
	},
	[ALC262_HIPPO] = {
		.mixers = { alc262_base_mixer },
		.init_verbs = { alc262_init_verbs, alc262_hippo_unsol_verbs},
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.dig_out_nid = ALC262_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
		.unsol_event = alc262_hippo_unsol_event,
		.init_hook = alc262_hippo_automute,
	},
	[ALC262_HIPPO_1] = {
		.mixers = { alc262_hippo1_mixer },
		.init_verbs = { alc262_init_verbs, alc262_hippo1_unsol_verbs},
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x02,
		.dig_out_nid = ALC262_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
		.unsol_event = alc262_hippo1_unsol_event,
		.init_hook = alc262_hippo1_automute,
	},
	[ALC262_FUJITSU] = {
		.mixers = { alc262_fujitsu_mixer },
		.init_verbs = { alc262_init_verbs, alc262_EAPD_verbs,
				alc262_fujitsu_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.dig_out_nid = ALC262_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_fujitsu_capture_source,
		.unsol_event = alc262_fujitsu_unsol_event,
		.init_hook = alc262_fujitsu_init_hook,
	},
	[ALC262_HP_BPC] = {
		.mixers = { alc262_HP_BPC_mixer },
		.init_verbs = { alc262_HP_BPC_init_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_HP_capture_source,
		.unsol_event = alc262_hp_bpc_unsol_event,
		.init_hook = alc262_hp_bpc_automute,
	},
	[ALC262_HP_BPC_D7000_WF] = {
		.mixers = { alc262_HP_BPC_WildWest_mixer },
		.init_verbs = { alc262_HP_BPC_WildWest_init_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_HP_D7000_capture_source,
		.unsol_event = alc262_hp_wildwest_unsol_event,
		.init_hook = alc262_hp_wildwest_automute,
	},
	[ALC262_HP_BPC_D7000_WL] = {
		.mixers = { alc262_HP_BPC_WildWest_mixer,
			    alc262_HP_BPC_WildWest_option_mixer },
		.init_verbs = { alc262_HP_BPC_WildWest_init_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_HP_D7000_capture_source,
		.unsol_event = alc262_hp_wildwest_unsol_event,
		.init_hook = alc262_hp_wildwest_automute,
	},
	[ALC262_HP_TC_T5735] = {
		.mixers = { alc262_hp_t5735_mixer },
		.init_verbs = { alc262_init_verbs, alc262_hp_t5735_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
		.unsol_event = alc262_hp_t5735_unsol_event,
		.init_hook = alc262_hp_t5735_init_hook,
	},
	[ALC262_HP_RP5700] = {
		.mixers = { alc262_hp_rp5700_mixer },
		.init_verbs = { alc262_init_verbs, alc262_hp_rp5700_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_hp_rp5700_capture_source,
        },
	[ALC262_BENQ_ED8] = {
		.mixers = { alc262_base_mixer },
		.init_verbs = { alc262_init_verbs, alc262_EAPD_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
	},
	[ALC262_SONY_ASSAMD] = {
		.mixers = { alc262_sony_mixer },
		.init_verbs = { alc262_init_verbs, alc262_sony_unsol_verbs},
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x02,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
		.unsol_event = alc262_hippo_unsol_event,
		.init_hook = alc262_hippo_automute,
	},
	[ALC262_BENQ_T31] = {
		.mixers = { alc262_benq_t31_mixer },
		.init_verbs = { alc262_init_verbs, alc262_benq_t31_EAPD_verbs, alc262_hippo_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
		.unsol_event = alc262_hippo_unsol_event,
		.init_hook = alc262_hippo_automute,
	},
	[ALC262_ULTRA] = {
		.mixers = { alc262_ultra_mixer, alc262_ultra_capture_mixer },
		.init_verbs = { alc262_ultra_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_ultra_capture_source,
		.adc_nids = alc262_adc_nids, /* ADC0 */
		.capsrc_nids = alc262_capsrc_nids,
		.num_adc_nids = 1, /* single ADC */
		.unsol_event = alc262_ultra_unsol_event,
		.init_hook = alc262_ultra_automute,
	},
	[ALC262_LENOVO_3000] = {
		.mixers = { alc262_lenovo_3000_mixer },
		.init_verbs = { alc262_init_verbs, alc262_EAPD_verbs,
				alc262_lenovo_3000_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.dig_out_nid = ALC262_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_fujitsu_capture_source,
		.unsol_event = alc262_lenovo_3000_unsol_event,
	},
	[ALC262_NEC] = {
		.mixers = { alc262_nec_mixer },
		.init_verbs = { alc262_nec_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
	},
	[ALC262_TOSHIBA_S06] = {
		.mixers = { alc262_toshiba_s06_mixer },
		.init_verbs = { alc262_init_verbs, alc262_toshiba_s06_verbs,
							alc262_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.capsrc_nids = alc262_dmic_capsrc_nids,
		.dac_nids = alc262_dac_nids,
		.adc_nids = alc262_dmic_adc_nids, /* ADC0 */
		.dig_out_nid = ALC262_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_dmic_capture_source,
		.unsol_event = alc262_toshiba_s06_unsol_event,
		.init_hook = alc262_toshiba_s06_init_hook,
	},
};

static int patch_alc262(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;
#if 0
	/* pshou 07/11/05  set a zero PCM sample to DAC when FIFO is
	 * under-run
	 */
	{
	int tmp;
	snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_COEF_INDEX, 7);
	tmp = snd_hda_codec_read(codec, 0x20, 0, AC_VERB_GET_PROC_COEF, 0);
	snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_COEF_INDEX, 7);
	snd_hda_codec_write(codec, 0x1a, 0, AC_VERB_SET_PROC_COEF, tmp | 0x80);
	}
#endif

	alc_fix_pll_init(codec, 0x20, 0x0a, 10);

	board_config = snd_hda_check_board_config(codec, ALC262_MODEL_LAST,
						  alc262_models,
						  alc262_cfg_tbl);

	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC262, "
		       "trying auto-probe from BIOS...\n");
		board_config = ALC262_AUTO;
	}

	if (board_config == ALC262_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc262_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC262_BASIC;
		}
	}

	if (board_config != ALC262_AUTO)
		setup_preset(spec, &alc262_presets[board_config]);

	spec->stream_name_analog = "ALC262 Analog";
	spec->stream_analog_playback = &alc262_pcm_analog_playback;
	spec->stream_analog_capture = &alc262_pcm_analog_capture;

	spec->stream_name_digital = "ALC262 Digital";
	spec->stream_digital_playback = &alc262_pcm_digital_playback;
	spec->stream_digital_capture = &alc262_pcm_digital_capture;

	if (!spec->adc_nids && spec->input_mux) {
		/* check whether NID 0x07 is valid */
		unsigned int wcap = get_wcaps(codec, 0x07);

		/* get type */
		wcap = (wcap & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT;
		if (wcap != AC_WID_AUD_IN) {
			spec->adc_nids = alc262_adc_nids_alt;
			spec->num_adc_nids = ARRAY_SIZE(alc262_adc_nids_alt);
			spec->capsrc_nids = alc262_capsrc_nids_alt;
			spec->mixers[spec->num_mixers] =
				alc262_capture_alt_mixer;
			spec->num_mixers++;
		} else {
			spec->adc_nids = alc262_adc_nids;
			spec->num_adc_nids = ARRAY_SIZE(alc262_adc_nids);
			spec->capsrc_nids = alc262_capsrc_nids;
			spec->mixers[spec->num_mixers] = alc262_capture_mixer;
			spec->num_mixers++;
		}
	}

	spec->vmaster_nid = 0x0c;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC262_AUTO)
		spec->init_hook = alc262_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc262_loopbacks;
#endif

	return 0;
}

/*
 *  ALC268 channel source setting (2 channel)
 */
#define ALC268_DIGOUT_NID	ALC880_DIGOUT_NID
#define alc268_modes		alc260_modes

static hda_nid_t alc268_dac_nids[2] = {
	/* front, hp */
	0x02, 0x03
};

static hda_nid_t alc268_adc_nids[2] = {
	/* ADC0-1 */
	0x08, 0x07
};

static hda_nid_t alc268_adc_nids_alt[1] = {
	/* ADC0 */
	0x08
};

static hda_nid_t alc268_capsrc_nids[2] = { 0x23, 0x24 };

static struct snd_kcontrol_new alc268_base_mixer[] = {
	/* output mixer control */
	HDA_CODEC_VOLUME("Front Playback Volume", 0x2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x3, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line In Boost", 0x1a, 0, HDA_INPUT),
	{ }
};

/* bind Beep switches of both NID 0x0f and 0x10 */
static struct hda_bind_ctls alc268_bind_beep_sw = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x0f, 3, 1, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x10, 3, 1, HDA_INPUT),
		0
	},
};

static struct snd_kcontrol_new alc268_beep_mixer[] = {
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x1d, 0x0, HDA_INPUT),
	HDA_BIND_SW("Beep Playback Switch", &alc268_bind_beep_sw),
	{ }
};

static struct hda_verb alc268_eapd_verbs[] = {
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

/* Toshiba specific */
#define alc268_toshiba_automute	alc262_hippo_automute

static struct hda_verb alc268_toshiba_verbs[] = {
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ } /* end */
};

static struct hda_input_mux alc268_acer_lc_capture_source = {
	.num_items = 2,
	.items = {
		{ "i-Mic", 0x6 },
		{ "E-Mic", 0x0 },
	},
};

/* Acer specific */
/* bind volumes of both NID 0x02 and 0x03 */
static struct hda_bind_ctls alc268_acer_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x02, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x03, 3, 0, HDA_OUTPUT),
		0
	},
};

/* mute/unmute internal speaker according to the hp jack and mute state */
static void alc268_acer_automute(struct hda_codec *codec, int force)
{
	struct alc_spec *spec = codec->spec;
	unsigned int mute;

	if (force || !spec->sense_updated) {
		unsigned int present;
		present = snd_hda_codec_read(codec, 0x14, 0,
				    	 AC_VERB_GET_PIN_SENSE, 0);
		spec->jack_present = (present & 0x80000000) != 0;
		spec->sense_updated = 1;
	}
	if (spec->jack_present)
		mute = HDA_AMP_MUTE; /* mute internal speaker */
	else /* unmute internal speaker if necessary */
		mute = snd_hda_codec_amp_read(codec, 0x14, 0, HDA_OUTPUT, 0);
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, mute);
}


/* bind hp and internal speaker mute (with plug check) */
static int alc268_acer_master_sw_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change;

	change = snd_hda_codec_amp_update(codec, 0x14, 0, HDA_OUTPUT, 0,
					  HDA_AMP_MUTE,
					  valp[0] ? 0 : HDA_AMP_MUTE);
	change |= snd_hda_codec_amp_update(codec, 0x14, 1, HDA_OUTPUT, 0,
					   HDA_AMP_MUTE,
					   valp[1] ? 0 : HDA_AMP_MUTE);
	if (change)
		alc268_acer_automute(codec, 0);
	return change;
}

static struct snd_kcontrol_new alc268_acer_aspire_one_mixer[] = {
	/* output mixer control */
	HDA_BIND_VOL("Master Playback Volume", &alc268_acer_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = alc268_acer_master_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
	},
	HDA_CODEC_VOLUME("Mic Boost Capture Volume", 0x18, 0, HDA_INPUT),
	{ }
};

static struct snd_kcontrol_new alc268_acer_mixer[] = {
	/* output mixer control */
	HDA_BIND_VOL("Master Playback Volume", &alc268_acer_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = alc268_acer_master_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
	},
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line In Boost", 0x1a, 0, HDA_INPUT),
	{ }
};

static struct hda_verb alc268_acer_aspire_one_verbs[] = {
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x23, AC_VERB_SET_CONNECT_SEL, 0x06},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, 0xa017},
	{ }
};

static struct hda_verb alc268_acer_verbs[] = {
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN}, /* internal dmic? */
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ }
};

/* unsolicited event for HP jack sensing */
static void alc268_toshiba_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc268_toshiba_automute(codec);
}

static void alc268_acer_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc268_acer_automute(codec, 1);
}

static void alc268_acer_init_hook(struct hda_codec *codec)
{
	alc268_acer_automute(codec, 1);
}

/* toggle speaker-output according to the hp-jack state */
static void alc268_aspire_one_speaker_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x15, 0,
				AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? AMP_IN_MUTE(0) : 0;
	snd_hda_codec_amp_stereo(codec, 0x0f, HDA_INPUT, 0,
				AMP_IN_MUTE(0), bits);
	snd_hda_codec_amp_stereo(codec, 0x0f, HDA_INPUT, 1,
				AMP_IN_MUTE(0), bits);
}


static void alc268_acer_mic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0,
				AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_CONNECT_SEL,
			    present ? 0x0 : 0x6);
}

static void alc268_acer_lc_unsol_event(struct hda_codec *codec,
				    unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc268_aspire_one_speaker_automute(codec);
	if ((res >> 26) == ALC880_MIC_EVENT)
		alc268_acer_mic_automute(codec);
}

static void alc268_acer_lc_init_hook(struct hda_codec *codec)
{
	alc268_aspire_one_speaker_automute(codec);
	alc268_acer_mic_automute(codec);
}

static struct snd_kcontrol_new alc268_dell_mixer[] = {
	/* output mixer control */
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost", 0x19, 0, HDA_INPUT),
	{ }
};

static struct hda_verb alc268_dell_verbs[] = {
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ }
};

/* mute/unmute internal speaker according to the hp jack and mute state */
static void alc268_dell_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned int mute;

	present = snd_hda_codec_read(codec, 0x15, 0, AC_VERB_GET_PIN_SENSE, 0);
	if (present & 0x80000000)
		mute = HDA_AMP_MUTE;
	else
		mute = snd_hda_codec_amp_read(codec, 0x15, 0, HDA_OUTPUT, 0);
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, mute);
}

static void alc268_dell_unsol_event(struct hda_codec *codec,
				    unsigned int res)
{
	if ((res >> 26) != ALC880_HP_EVENT)
		return;
	alc268_dell_automute(codec);
}

#define alc268_dell_init_hook	alc268_dell_automute

static struct snd_kcontrol_new alc267_quanta_il1_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x3, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Capture Volume", 0x23, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Mic Capture Switch", 0x23, 2, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Ext Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Boost", 0x19, 0, HDA_INPUT),
	{ }
};

static struct hda_verb alc267_quanta_il1_verbs[] = {
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_MIC_EVENT | AC_USRSP_EN},
	{ }
};

static void alc267_quanta_il1_hp_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x15, 0, AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	snd_hda_codec_write(codec, 0x14, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    present ? 0 : PIN_OUT);
}

static void alc267_quanta_il1_mic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_write(codec, 0x23, 0,
			    AC_VERB_SET_CONNECT_SEL,
			    present ? 0x00 : 0x01);
}

static void alc267_quanta_il1_automute(struct hda_codec *codec)
{
	alc267_quanta_il1_hp_automute(codec);
	alc267_quanta_il1_mic_automute(codec);
}

static void alc267_quanta_il1_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc267_quanta_il1_hp_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc267_quanta_il1_mic_automute(codec);
		break;
	}
}

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc268_base_init_verbs[] = {
	/* Unmute DAC0-1 and set vol = 0 */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/*
	 * Set up output mixers (0x0c - 0x0e)
	 */
	/* set vol=0 to output mixers */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
        {0x0e, AC_VERB_SET_CONNECT_SEL, 0x00},

	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},

	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	/* set PCBEEP vol = 0, mute connections */
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	/* Unmute Selector 23h,24h and set the default input to mic-in */

	{0x23, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x24, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{ }
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc268_volume_init_verbs[] = {
	/* set output DAC */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},

	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	/* set PCBEEP vol = 0, mute connections */
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	{ }
};

#define alc268_mux_enum_info alc_mux_enum_info
#define alc268_mux_enum_get alc_mux_enum_get
#define alc268_mux_enum_put alc_mux_enum_put

static struct snd_kcontrol_new alc268_capture_alt_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x23, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x23, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc268_mux_enum_info,
		.get = alc268_mux_enum_get,
		.put = alc268_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc268_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x23, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x23, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x24, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x24, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 2,
		.info = alc268_mux_enum_info,
		.get = alc268_mux_enum_get,
		.put = alc268_mux_enum_put,
	},
	{ } /* end */
};

static struct hda_input_mux alc268_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x3 },
	},
};

static struct hda_input_mux alc268_acer_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x6 },
		{ "Line", 0x2 },
	},
};

#ifdef CONFIG_SND_DEBUG
static struct snd_kcontrol_new alc268_test_mixer[] = {
	/* Volume widgets */
	HDA_CODEC_VOLUME("LOUT1 Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("LOUT2 Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Mono sum Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE("LINE-OUT sum Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_BIND_MUTE("HP-OUT sum Playback Switch", 0x10, 2, HDA_INPUT),
	HDA_BIND_MUTE("LINE-OUT Playback Switch", 0x14, 2, HDA_OUTPUT),
	HDA_BIND_MUTE("HP-OUT Playback Switch", 0x15, 2, HDA_OUTPUT),
	HDA_BIND_MUTE("Mono Playback Switch", 0x16, 2, HDA_OUTPUT),
	HDA_CODEC_VOLUME("MIC1 Capture Volume", 0x18, 0x0, HDA_INPUT),
	HDA_BIND_MUTE("MIC1 Capture Switch", 0x18, 2, HDA_OUTPUT),
	HDA_CODEC_VOLUME("MIC2 Capture Volume", 0x19, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("LINE1 Capture Volume", 0x1a, 0x0, HDA_INPUT),
	HDA_BIND_MUTE("LINE1 Capture Switch", 0x1a, 2, HDA_OUTPUT),
	/* The below appears problematic on some hardwares */
	/*HDA_CODEC_VOLUME("PCBEEP Playback Volume", 0x1d, 0x0, HDA_INPUT),*/
	HDA_CODEC_VOLUME("PCM-IN1 Capture Volume", 0x23, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("PCM-IN1 Capture Switch", 0x23, 2, HDA_OUTPUT),
	HDA_CODEC_VOLUME("PCM-IN2 Capture Volume", 0x24, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("PCM-IN2 Capture Switch", 0x24, 2, HDA_OUTPUT),

	/* Modes for retasking pin widgets */
	ALC_PIN_MODE("LINE-OUT pin mode", 0x14, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("HP-OUT pin mode", 0x15, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("MIC1 pin mode", 0x18, ALC_PIN_DIR_INOUT),
	ALC_PIN_MODE("LINE1 pin mode", 0x1a, ALC_PIN_DIR_INOUT),

	/* Controls for GPIO pins, assuming they are configured as outputs */
	ALC_GPIO_DATA_SWITCH("GPIO pin 0", 0x01, 0x01),
	ALC_GPIO_DATA_SWITCH("GPIO pin 1", 0x01, 0x02),
	ALC_GPIO_DATA_SWITCH("GPIO pin 2", 0x01, 0x04),
	ALC_GPIO_DATA_SWITCH("GPIO pin 3", 0x01, 0x08),

	/* Switches to allow the digital SPDIF output pin to be enabled.
	 * The ALC268 does not have an SPDIF input.
	 */
	ALC_SPDIF_CTRL_SWITCH("SPDIF Playback Switch", 0x06, 0x01),

	/* A switch allowing EAPD to be enabled.  Some laptops seem to use
	 * this output to turn on an external amplifier.
	 */
	ALC_EAPD_CTRL_SWITCH("LINE-OUT EAPD Enable Switch", 0x0f, 0x02),
	ALC_EAPD_CTRL_SWITCH("HP-OUT EAPD Enable Switch", 0x10, 0x02),

	{ } /* end */
};
#endif

/* create input playback/capture controls for the given pin */
static int alc268_new_analog_output(struct alc_spec *spec, hda_nid_t nid,
				    const char *ctlname, int idx)
{
	char name[32];
	int err;

	sprintf(name, "%s Playback Volume", ctlname);
	if (nid == 0x14) {
		err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
				  HDA_COMPOSE_AMP_VAL(0x02, 3, idx,
						      HDA_OUTPUT));
		if (err < 0)
			return err;
	} else if (nid == 0x15) {
		err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
				  HDA_COMPOSE_AMP_VAL(0x03, 3, idx,
						      HDA_OUTPUT));
		if (err < 0)
			return err;
	} else
		return -1;
	sprintf(name, "%s Playback Switch", ctlname);
	err = add_control(spec, ALC_CTL_WIDGET_MUTE, name,
			  HDA_COMPOSE_AMP_VAL(nid, 3, idx, HDA_OUTPUT));
	if (err < 0)
		return err;
	return 0;
}

/* add playback controls from the parsed DAC table */
static int alc268_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	hda_nid_t nid;
	int err;

	spec->multiout.num_dacs = 2;	/* only use one dac */
	spec->multiout.dac_nids = spec->private_dac_nids;
	spec->multiout.dac_nids[0] = 2;
	spec->multiout.dac_nids[1] = 3;

	nid = cfg->line_out_pins[0];
	if (nid)
		alc268_new_analog_output(spec, nid, "Front", 0);

	nid = cfg->speaker_pins[0];
	if (nid == 0x1d) {
		err = add_control(spec, ALC_CTL_WIDGET_VOL,
				  "Speaker Playback Volume",
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_INPUT));
		if (err < 0)
			return err;
	}
	nid = cfg->hp_pins[0];
	if (nid)
		alc268_new_analog_output(spec, nid, "Headphone", 0);

	nid = cfg->line_out_pins[1] | cfg->line_out_pins[2];
	if (nid == 0x16) {
		err = add_control(spec, ALC_CTL_WIDGET_MUTE,
				  "Mono Playback Switch",
				  HDA_COMPOSE_AMP_VAL(nid, 2, 0, HDA_INPUT));
		if (err < 0)
			return err;
	}
	return 0;
}

/* create playback/capture controls for input pins */
static int alc268_auto_create_analog_input_ctls(struct alc_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	struct hda_input_mux *imux = &spec->private_imux;
	int i, idx1;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		switch(cfg->input_pins[i]) {
		case 0x18:
			idx1 = 0;	/* Mic 1 */
			break;
		case 0x19:
			idx1 = 1;	/* Mic 2 */
			break;
		case 0x1a:
			idx1 = 2;	/* Line In */
			break;
		case 0x1c:
			idx1 = 3;	/* CD */
			break;
		case 0x12:
		case 0x13:
			idx1 = 6;	/* digital mics */
			break;
		default:
			continue;
		}
		imux->items[imux->num_items].label = auto_pin_cfg_labels[i];
		imux->items[imux->num_items].index = idx1;
		imux->num_items++;
	}
	return 0;
}

static void alc268_auto_init_mono_speaker_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t speaker_nid = spec->autocfg.speaker_pins[0];
	hda_nid_t hp_nid = spec->autocfg.hp_pins[0];
	hda_nid_t line_nid = spec->autocfg.line_out_pins[0];
	unsigned int	dac_vol1, dac_vol2;

	if (speaker_nid) {
		snd_hda_codec_write(codec, speaker_nid, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
		snd_hda_codec_write(codec, 0x0f, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_IN_UNMUTE(1));
		snd_hda_codec_write(codec, 0x10, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_IN_UNMUTE(1));
	} else {
		snd_hda_codec_write(codec, 0x0f, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1));
		snd_hda_codec_write(codec, 0x10, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1));
	}

	dac_vol1 = dac_vol2 = 0xb000 | 0x40;	/* set max volume  */
	if (line_nid == 0x14)
		dac_vol2 = AMP_OUT_ZERO;
	else if (line_nid == 0x15)
		dac_vol1 = AMP_OUT_ZERO;
	if (hp_nid == 0x14)
		dac_vol2 = AMP_OUT_ZERO;
	else if (hp_nid == 0x15)
		dac_vol1 = AMP_OUT_ZERO;
	if (line_nid != 0x16 || hp_nid != 0x16 ||
	    spec->autocfg.line_out_pins[1] != 0x16 ||
	    spec->autocfg.line_out_pins[2] != 0x16)
		dac_vol1 = dac_vol2 = AMP_OUT_ZERO;

	snd_hda_codec_write(codec, 0x02, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, dac_vol1);
	snd_hda_codec_write(codec, 0x03, 0,
			    AC_VERB_SET_AMP_GAIN_MUTE, dac_vol2);
}

/* pcm configuration: identiacal with ALC880 */
#define alc268_pcm_analog_playback	alc880_pcm_analog_playback
#define alc268_pcm_analog_capture	alc880_pcm_analog_capture
#define alc268_pcm_analog_alt_capture	alc880_pcm_analog_alt_capture
#define alc268_pcm_digital_playback	alc880_pcm_digital_playback

/*
 * BIOS auto configuration
 */
static int alc268_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static hda_nid_t alc268_ignore[] = { 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc268_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */

	err = alc268_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc268_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = 2;

	/* digital only support output */
	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC268_DIGOUT_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	if (spec->autocfg.speaker_pins[0] != 0x1d)
		spec->mixers[spec->num_mixers++] = alc268_beep_mixer;

	spec->init_verbs[spec->num_init_verbs++] = alc268_volume_init_verbs;
	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	return 1;
}

#define alc268_auto_init_multi_out	alc882_auto_init_multi_out
#define alc268_auto_init_hp_out		alc882_auto_init_hp_out
#define alc268_auto_init_analog_input	alc882_auto_init_analog_input

/* init callback for auto-configuration model -- overriding the default init */
static void alc268_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc268_auto_init_multi_out(codec);
	alc268_auto_init_hp_out(codec);
	alc268_auto_init_mono_speaker_out(codec);
	alc268_auto_init_analog_input(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

/*
 * configuration and preset
 */
static const char *alc268_models[ALC268_MODEL_LAST] = {
	[ALC267_QUANTA_IL1]	= "quanta-il1",
	[ALC268_3ST]		= "3stack",
	[ALC268_TOSHIBA]	= "toshiba",
	[ALC268_ACER]		= "acer",
	[ALC268_ACER_ASPIRE_ONE]	= "acer-aspire",
	[ALC268_DELL]		= "dell",
	[ALC268_ZEPTO]		= "zepto",
#ifdef CONFIG_SND_DEBUG
	[ALC268_TEST]		= "test",
#endif
	[ALC268_AUTO]		= "auto",
};

static struct snd_pci_quirk alc268_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x011e, "Acer Aspire 5720z", ALC268_ACER),
	SND_PCI_QUIRK(0x1025, 0x0126, "Acer", ALC268_ACER),
	SND_PCI_QUIRK(0x1025, 0x012e, "Acer Aspire 5310", ALC268_ACER),
	SND_PCI_QUIRK(0x1025, 0x0130, "Acer Extensa 5210", ALC268_ACER),
	SND_PCI_QUIRK(0x1025, 0x0136, "Acer Aspire 5315", ALC268_ACER),
	SND_PCI_QUIRK(0x1025, 0x015b, "Acer Aspire One",
						ALC268_ACER_ASPIRE_ONE),
	SND_PCI_QUIRK(0x1028, 0x0253, "Dell OEM", ALC268_DELL),
	SND_PCI_QUIRK(0x103c, 0x30cc, "TOSHIBA", ALC268_TOSHIBA),
	SND_PCI_QUIRK(0x1043, 0x1205, "ASUS W7J", ALC268_3ST),
	SND_PCI_QUIRK(0x1179, 0xff10, "TOSHIBA A205", ALC268_TOSHIBA),
	SND_PCI_QUIRK(0x1179, 0xff50, "TOSHIBA A305", ALC268_TOSHIBA),
	SND_PCI_QUIRK(0x14c0, 0x0025, "COMPAL IFL90/JFL-92", ALC268_TOSHIBA),
	SND_PCI_QUIRK(0x152d, 0x0763, "Diverse (CPR2000)", ALC268_ACER),
	SND_PCI_QUIRK(0x152d, 0x0771, "Quanta IL1", ALC267_QUANTA_IL1),
	SND_PCI_QUIRK(0x1170, 0x0040, "ZEPTO", ALC268_ZEPTO),
	{}
};

static struct alc_config_preset alc268_presets[] = {
	[ALC267_QUANTA_IL1] = {
		.mixers = { alc267_quanta_il1_mixer },
		.init_verbs = { alc268_base_init_verbs, alc268_eapd_verbs,
				alc267_quanta_il1_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt),
		.adc_nids = alc268_adc_nids_alt,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.input_mux = &alc268_capture_source,
		.unsol_event = alc267_quanta_il1_unsol_event,
		.init_hook = alc267_quanta_il1_automute,
	},
	[ALC268_3ST] = {
		.mixers = { alc268_base_mixer, alc268_capture_alt_mixer,
			    alc268_beep_mixer },
		.init_verbs = { alc268_base_init_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
                .num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt),
                .adc_nids = alc268_adc_nids_alt,
		.capsrc_nids = alc268_capsrc_nids,
		.hp_nid = 0x03,
		.dig_out_nid = ALC268_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.input_mux = &alc268_capture_source,
	},
	[ALC268_TOSHIBA] = {
		.mixers = { alc268_base_mixer, alc268_capture_alt_mixer,
			    alc268_beep_mixer },
		.init_verbs = { alc268_base_init_verbs, alc268_eapd_verbs,
				alc268_toshiba_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt),
		.adc_nids = alc268_adc_nids_alt,
		.capsrc_nids = alc268_capsrc_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.input_mux = &alc268_capture_source,
		.unsol_event = alc268_toshiba_unsol_event,
		.init_hook = alc268_toshiba_automute,
	},
	[ALC268_ACER] = {
		.mixers = { alc268_acer_mixer, alc268_capture_alt_mixer,
			    alc268_beep_mixer },
		.init_verbs = { alc268_base_init_verbs, alc268_eapd_verbs,
				alc268_acer_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt),
		.adc_nids = alc268_adc_nids_alt,
		.capsrc_nids = alc268_capsrc_nids,
		.hp_nid = 0x02,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.input_mux = &alc268_acer_capture_source,
		.unsol_event = alc268_acer_unsol_event,
		.init_hook = alc268_acer_init_hook,
	},
	[ALC268_ACER_ASPIRE_ONE] = {
		.mixers = { alc268_acer_aspire_one_mixer,
				alc268_capture_alt_mixer },
		.init_verbs = { alc268_base_init_verbs, alc268_eapd_verbs,
				alc268_acer_aspire_one_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt),
		.adc_nids = alc268_adc_nids_alt,
		.capsrc_nids = alc268_capsrc_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.input_mux = &alc268_acer_lc_capture_source,
		.unsol_event = alc268_acer_lc_unsol_event,
		.init_hook = alc268_acer_lc_init_hook,
	},
	[ALC268_DELL] = {
		.mixers = { alc268_dell_mixer, alc268_beep_mixer },
		.init_verbs = { alc268_base_init_verbs, alc268_eapd_verbs,
				alc268_dell_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
		.hp_nid = 0x02,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.unsol_event = alc268_dell_unsol_event,
		.init_hook = alc268_dell_init_hook,
		.input_mux = &alc268_capture_source,
	},
	[ALC268_ZEPTO] = {
		.mixers = { alc268_base_mixer, alc268_capture_alt_mixer,
			    alc268_beep_mixer },
		.init_verbs = { alc268_base_init_verbs, alc268_eapd_verbs,
				alc268_toshiba_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt),
		.adc_nids = alc268_adc_nids_alt,
		.capsrc_nids = alc268_capsrc_nids,
		.hp_nid = 0x03,
		.dig_out_nid = ALC268_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.input_mux = &alc268_capture_source,
		.unsol_event = alc268_toshiba_unsol_event,
		.init_hook = alc268_toshiba_automute
	},
#ifdef CONFIG_SND_DEBUG
	[ALC268_TEST] = {
		.mixers = { alc268_test_mixer, alc268_capture_mixer },
		.init_verbs = { alc268_base_init_verbs, alc268_eapd_verbs,
				alc268_volume_init_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt),
		.adc_nids = alc268_adc_nids_alt,
		.capsrc_nids = alc268_capsrc_nids,
		.hp_nid = 0x03,
		.dig_out_nid = ALC268_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.input_mux = &alc268_capture_source,
	},
#endif
};

static int patch_alc268(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int err;

	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	board_config = snd_hda_check_board_config(codec, ALC268_MODEL_LAST,
						  alc268_models,
						  alc268_cfg_tbl);

	if (board_config < 0 || board_config >= ALC268_MODEL_LAST) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC268, "
		       "trying auto-probe from BIOS...\n");
		board_config = ALC268_AUTO;
	}

	if (board_config == ALC268_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc268_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC268_3ST;
		}
	}

	if (board_config != ALC268_AUTO)
		setup_preset(spec, &alc268_presets[board_config]);

	if (codec->vendor_id == 0x10ec0267) {
		spec->stream_name_analog = "ALC267 Analog";
		spec->stream_name_digital = "ALC267 Digital";
	} else {
		spec->stream_name_analog = "ALC268 Analog";
		spec->stream_name_digital = "ALC268 Digital";
	}

	spec->stream_analog_playback = &alc268_pcm_analog_playback;
	spec->stream_analog_capture = &alc268_pcm_analog_capture;
	spec->stream_analog_alt_capture = &alc268_pcm_analog_alt_capture;

	spec->stream_digital_playback = &alc268_pcm_digital_playback;

	if (!query_amp_caps(codec, 0x1d, HDA_INPUT))
		/* override the amp caps for beep generator */
		snd_hda_override_amp_caps(codec, 0x1d, HDA_INPUT,
					  (0x0c << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x0c << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x07 << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (0 << AC_AMPCAP_MUTE_SHIFT));

	if (!spec->adc_nids && spec->input_mux) {
		/* check whether NID 0x07 is valid */
		unsigned int wcap = get_wcaps(codec, 0x07);
		int i;

		/* get type */
		wcap = (wcap & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT;
		if (wcap != AC_WID_AUD_IN || spec->input_mux->num_items == 1) {
			spec->adc_nids = alc268_adc_nids_alt;
			spec->num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt);
			spec->mixers[spec->num_mixers] =
					alc268_capture_alt_mixer;
			spec->num_mixers++;
		} else {
			spec->adc_nids = alc268_adc_nids;
			spec->num_adc_nids = ARRAY_SIZE(alc268_adc_nids);
			spec->mixers[spec->num_mixers] =
				alc268_capture_mixer;
			spec->num_mixers++;
		}
		spec->capsrc_nids = alc268_capsrc_nids;
		/* set default input source */
		for (i = 0; i < spec->num_adc_nids; i++)
			snd_hda_codec_write_cache(codec, alc268_capsrc_nids[i],
				0, AC_VERB_SET_CONNECT_SEL,
				spec->input_mux->items[0].index);
	}

	spec->vmaster_nid = 0x02;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC268_AUTO)
		spec->init_hook = alc268_auto_init;

	return 0;
}

/*
 *  ALC269 channel source setting (2 channel)
 */
#define ALC269_DIGOUT_NID	ALC880_DIGOUT_NID

#define alc269_dac_nids		alc260_dac_nids

static hda_nid_t alc269_adc_nids[1] = {
	/* ADC1 */
	0x08,
};

static hda_nid_t alc269_capsrc_nids[1] = {
	0x23,
};

/* NOTE: ADC2 (0x07) is connected from a recording *MIXER* (0x24),
 *       not a mux!
 */

static struct hda_input_mux alc269_eeepc_dmic_capture_source = {
	.num_items = 2,
	.items = {
		{ "i-Mic", 0x5 },
		{ "e-Mic", 0x0 },
	},
};

static struct hda_input_mux alc269_eeepc_amic_capture_source = {
	.num_items = 2,
	.items = {
		{ "i-Mic", 0x1 },
		{ "e-Mic", 0x0 },
	},
};

#define alc269_modes		alc260_modes
#define alc269_capture_source	alc880_lg_lw_capture_source

static struct snd_kcontrol_new alc269_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x0b, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x0b, 0x4, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Mono Playback Switch", 0x16, 2, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc269_quanta_fl1_mixer[] = {
	/* output mixer control */
	HDA_BIND_VOL("Master Playback Volume", &alc268_acer_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = alc268_acer_master_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
	},
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x04, HDA_INPUT),
	{ }
};

/* bind volumes of both NID 0x0c and 0x0d */
static struct hda_bind_ctls alc269_epc_bind_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x02, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x03, 3, 0, HDA_OUTPUT),
		0
	},
};

static struct snd_kcontrol_new alc269_eeepc_mixer[] = {
	HDA_CODEC_MUTE("iSpeaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_BIND_VOL("LineOut Playback Volume", &alc269_epc_bind_vol),
	HDA_CODEC_MUTE("LineOut Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	{ } /* end */
};

/* capture mixer elements */
static struct snd_kcontrol_new alc269_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

/* capture mixer elements */
static struct snd_kcontrol_new alc269_epc_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{ } /* end */
};

/* beep control */
static struct snd_kcontrol_new alc269_beep_mixer[] = {
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x0b, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x0b, 0x4, HDA_INPUT),
	{ } /* end */
};

static struct hda_verb alc269_quanta_fl1_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc269_quanta_fl1_speaker_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x15, 0,
			AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? AMP_IN_MUTE(0) : 0;
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 0,
			AMP_IN_MUTE(0), bits);
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 1,
			AMP_IN_MUTE(0), bits);

	snd_hda_codec_write(codec, 0x20, 0,
			AC_VERB_SET_COEF_INDEX, 0x0c);
	snd_hda_codec_write(codec, 0x20, 0,
			AC_VERB_SET_PROC_COEF, 0x680);

	snd_hda_codec_write(codec, 0x20, 0,
			AC_VERB_SET_COEF_INDEX, 0x0c);
	snd_hda_codec_write(codec, 0x20, 0,
			AC_VERB_SET_PROC_COEF, 0x480);
}

static void alc269_quanta_fl1_mic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0,
				AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_write(codec, 0x23, 0,
			    AC_VERB_SET_CONNECT_SEL, present ? 0x0 : 0x1);
}

static void alc269_quanta_fl1_unsol_event(struct hda_codec *codec,
				    unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc269_quanta_fl1_speaker_automute(codec);
	if ((res >> 26) == ALC880_MIC_EVENT)
		alc269_quanta_fl1_mic_automute(codec);
}

static void alc269_quanta_fl1_init_hook(struct hda_codec *codec)
{
	alc269_quanta_fl1_speaker_automute(codec);
	alc269_quanta_fl1_mic_automute(codec);
}

static struct hda_verb alc269_eeepc_dmic_init_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x23, AC_VERB_SET_CONNECT_SEL, 0x05},
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, 0xb026 },
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x7019 | (0x00 << 8))},
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static struct hda_verb alc269_eeepc_amic_init_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x23, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, 0xb026 },
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x701b | (0x00 << 8))},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

/* toggle speaker-output according to the hp-jack state */
static void alc269_speaker_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x15, 0,
				AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? AMP_IN_MUTE(0) : 0;
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 0,
				AMP_IN_MUTE(0), bits);
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 1,
				AMP_IN_MUTE(0), bits);
}

static void alc269_eeepc_dmic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0,
				AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_write(codec, 0x23, 0,
				AC_VERB_SET_CONNECT_SEL,  (present ? 0 : 5));
}

static void alc269_eeepc_amic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0,
				AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_write(codec, 0x24, 0, AC_VERB_SET_AMP_GAIN_MUTE,
				0x7000 | (0x00 << 8) | (present ? 0 : 0x80));
	snd_hda_codec_write(codec, 0x24, 0, AC_VERB_SET_AMP_GAIN_MUTE,
				0x7000 | (0x01 << 8) | (present ? 0x80 : 0));
}

/* unsolicited event for HP jack sensing */
static void alc269_eeepc_dmic_unsol_event(struct hda_codec *codec,
				     unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc269_speaker_automute(codec);

	if ((res >> 26) == ALC880_MIC_EVENT)
		alc269_eeepc_dmic_automute(codec);
}

static void alc269_eeepc_dmic_inithook(struct hda_codec *codec)
{
	alc269_speaker_automute(codec);
	alc269_eeepc_dmic_automute(codec);
}

/* unsolicited event for HP jack sensing */
static void alc269_eeepc_amic_unsol_event(struct hda_codec *codec,
				     unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc269_speaker_automute(codec);

	if ((res >> 26) == ALC880_MIC_EVENT)
		alc269_eeepc_amic_automute(codec);
}

static void alc269_eeepc_amic_inithook(struct hda_codec *codec)
{
	alc269_speaker_automute(codec);
	alc269_eeepc_amic_automute(codec);
}

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc269_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Mute input amps (PCBeep, Line In, Mic 1 & Mic 2) of the
	 * analog-loopback mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for
	 * front panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/*
	 * Set up output mixers (0x0c - 0x0e)
	 */
	/* set vol=0 to output mixers */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1d, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},

	/* set EAPD */
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

/* add playback controls from the parsed DAC table */
static int alc269_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	hda_nid_t nid;
	int err;

	spec->multiout.num_dacs = 1;	/* only use one dac */
	spec->multiout.dac_nids = spec->private_dac_nids;
	spec->multiout.dac_nids[0] = 2;

	nid = cfg->line_out_pins[0];
	if (nid) {
		err = add_control(spec, ALC_CTL_WIDGET_VOL,
				  "Front Playback Volume",
				  HDA_COMPOSE_AMP_VAL(0x02, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		err = add_control(spec, ALC_CTL_WIDGET_MUTE,
				  "Front Playback Switch",
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
	}

	nid = cfg->speaker_pins[0];
	if (nid) {
		if (!cfg->line_out_pins[0]) {
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "Speaker Playback Volume",
					  HDA_COMPOSE_AMP_VAL(0x02, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		}
		if (nid == 0x16) {
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Speaker Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Speaker Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}
	nid = cfg->hp_pins[0];
	if (nid) {
		/* spec->multiout.hp_nid = 2; */
		if (!cfg->line_out_pins[0] && !cfg->speaker_pins[0]) {
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "Headphone Playback Volume",
					  HDA_COMPOSE_AMP_VAL(0x02, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		}
		if (nid == 0x16) {
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Headphone Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			err = add_control(spec, ALC_CTL_WIDGET_MUTE,
					  "Headphone Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

#define alc269_auto_create_analog_input_ctls \
	alc880_auto_create_analog_input_ctls

#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc269_loopbacks	alc880_loopbacks
#endif

/* pcm configuration: identiacal with ALC880 */
#define alc269_pcm_analog_playback	alc880_pcm_analog_playback
#define alc269_pcm_analog_capture	alc880_pcm_analog_capture
#define alc269_pcm_digital_playback	alc880_pcm_digital_playback
#define alc269_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * BIOS auto configuration
 */
static int alc269_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i, err;
	static hda_nid_t alc269_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc269_ignore);
	if (err < 0)
		return err;

	err = alc269_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc269_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC269_DIGOUT_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	/* create a beep mixer control if the pin 0x1d isn't assigned */
	for (i = 0; i < ARRAY_SIZE(spec->autocfg.input_pins); i++)
		if (spec->autocfg.input_pins[i] == 0x1d)
			break;
	if (i >= ARRAY_SIZE(spec->autocfg.input_pins))
		spec->mixers[spec->num_mixers++] = alc269_beep_mixer;

	spec->init_verbs[spec->num_init_verbs++] = alc269_init_verbs;
	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;
	/* set default input source */
	snd_hda_codec_write_cache(codec, alc269_capsrc_nids[0],
				  0, AC_VERB_SET_CONNECT_SEL,
				  spec->input_mux->items[0].index);

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	spec->mixers[spec->num_mixers] = alc269_capture_mixer;
	spec->num_mixers++;

	return 1;
}

#define alc269_auto_init_multi_out	alc882_auto_init_multi_out
#define alc269_auto_init_hp_out		alc882_auto_init_hp_out
#define alc269_auto_init_analog_input	alc882_auto_init_analog_input


/* init callback for auto-configuration model -- overriding the default init */
static void alc269_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc269_auto_init_multi_out(codec);
	alc269_auto_init_hp_out(codec);
	alc269_auto_init_analog_input(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

/*
 * configuration and preset
 */
static const char *alc269_models[ALC269_MODEL_LAST] = {
	[ALC269_BASIC]			= "basic",
	[ALC269_QUANTA_FL1]		= "quanta",
	[ALC269_ASUS_EEEPC_P703]	= "eeepc-p703",
	[ALC269_ASUS_EEEPC_P901]	= "eeepc-p901"
};

static struct snd_pci_quirk alc269_cfg_tbl[] = {
	SND_PCI_QUIRK(0x17aa, 0x3bf8, "Quanta FL1", ALC269_QUANTA_FL1),
	SND_PCI_QUIRK(0x1043, 0x8330, "ASUS Eeepc P703 P900A",
		      ALC269_ASUS_EEEPC_P703),
	SND_PCI_QUIRK(0x1043, 0x831a, "ASUS Eeepc P901",
		      ALC269_ASUS_EEEPC_P901),
	SND_PCI_QUIRK(0x1043, 0x834a, "ASUS Eeepc S101",
		      ALC269_ASUS_EEEPC_P901),
	{}
};

static struct alc_config_preset alc269_presets[] = {
	[ALC269_BASIC] = {
		.mixers = { alc269_base_mixer, alc269_capture_mixer },
		.init_verbs = { alc269_init_verbs },
		.num_dacs = ARRAY_SIZE(alc269_dac_nids),
		.dac_nids = alc269_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc269_modes),
		.channel_mode = alc269_modes,
		.input_mux = &alc269_capture_source,
	},
	[ALC269_QUANTA_FL1] = {
		.mixers = { alc269_quanta_fl1_mixer },
		.init_verbs = { alc269_init_verbs, alc269_quanta_fl1_verbs },
		.num_dacs = ARRAY_SIZE(alc269_dac_nids),
		.dac_nids = alc269_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc269_modes),
		.channel_mode = alc269_modes,
		.input_mux = &alc269_capture_source,
		.unsol_event = alc269_quanta_fl1_unsol_event,
		.init_hook = alc269_quanta_fl1_init_hook,
	},
	[ALC269_ASUS_EEEPC_P703] = {
		.mixers = { alc269_eeepc_mixer, alc269_epc_capture_mixer },
		.init_verbs = { alc269_init_verbs,
				alc269_eeepc_amic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc269_dac_nids),
		.dac_nids = alc269_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc269_modes),
		.channel_mode = alc269_modes,
		.input_mux = &alc269_eeepc_amic_capture_source,
		.unsol_event = alc269_eeepc_amic_unsol_event,
		.init_hook = alc269_eeepc_amic_inithook,
	},
	[ALC269_ASUS_EEEPC_P901] = {
		.mixers = { alc269_eeepc_mixer, alc269_epc_capture_mixer},
		.init_verbs = { alc269_init_verbs,
				alc269_eeepc_dmic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc269_dac_nids),
		.dac_nids = alc269_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc269_modes),
		.channel_mode = alc269_modes,
		.input_mux = &alc269_eeepc_dmic_capture_source,
		.unsol_event = alc269_eeepc_dmic_unsol_event,
		.init_hook = alc269_eeepc_dmic_inithook,
	},
};

static int patch_alc269(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	alc_fix_pll_init(codec, 0x20, 0x04, 15);

	board_config = snd_hda_check_board_config(codec, ALC269_MODEL_LAST,
						  alc269_models,
						  alc269_cfg_tbl);

	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC269, "
		       "trying auto-probe from BIOS...\n");
		board_config = ALC269_AUTO;
	}

	if (board_config == ALC269_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc269_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC269_BASIC;
		}
	}

	if (board_config != ALC269_AUTO)
		setup_preset(spec, &alc269_presets[board_config]);

	spec->stream_name_analog = "ALC269 Analog";
	spec->stream_analog_playback = &alc269_pcm_analog_playback;
	spec->stream_analog_capture = &alc269_pcm_analog_capture;

	spec->stream_name_digital = "ALC269 Digital";
	spec->stream_digital_playback = &alc269_pcm_digital_playback;
	spec->stream_digital_capture = &alc269_pcm_digital_capture;

	spec->adc_nids = alc269_adc_nids;
	spec->num_adc_nids = ARRAY_SIZE(alc269_adc_nids);
	spec->capsrc_nids = alc269_capsrc_nids;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC269_AUTO)
		spec->init_hook = alc269_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc269_loopbacks;
#endif

	return 0;
}

/*
 *  ALC861 channel source setting (2/6 channel selection for 3-stack)
 */

/*
 * set the path ways for 2 channel output
 * need to set the codec line out and mic 1 pin widgets to inputs
 */
static struct hda_verb alc861_threestack_ch2_init[] = {
	/* set pin widget 1Ah (line in) for input */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* set pin widget 18h (mic1/2) for input, for mic also enable
	 * the vref
	 */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },

	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c },
#if 0
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8)) }, /*mic*/
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8)) }, /*line-in*/
#endif
	{ } /* end */
};
/*
 * 6ch mode
 * need to set the codec line out and mic 1 pin widgets to outputs
 */
static struct hda_verb alc861_threestack_ch6_init[] = {
	/* set pin widget 1Ah (line in) for output (Back Surround)*/
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* set pin widget 18h (mic1) for output (CLFE)*/
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },

	{ 0x0c, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x0d, AC_VERB_SET_CONNECT_SEL, 0x00 },

	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080 },
#if 0
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8)) }, /*mic*/
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8)) }, /*line in*/
#endif
	{ } /* end */
};

static struct hda_channel_mode alc861_threestack_modes[2] = {
	{ 2, alc861_threestack_ch2_init },
	{ 6, alc861_threestack_ch6_init },
};
/* Set mic1 as input and unmute the mixer */
static struct hda_verb alc861_uniwill_m31_ch2_init[] = {
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8)) }, /*mic*/
	{ } /* end */
};
/* Set mic1 as output and mute mixer */
static struct hda_verb alc861_uniwill_m31_ch4_init[] = {
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8)) }, /*mic*/
	{ } /* end */
};

static struct hda_channel_mode alc861_uniwill_m31_modes[2] = {
	{ 2, alc861_uniwill_m31_ch2_init },
	{ 4, alc861_uniwill_m31_ch4_init },
};

/* Set mic1 and line-in as input and unmute the mixer */
static struct hda_verb alc861_asus_ch2_init[] = {
	/* set pin widget 1Ah (line in) for input */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* set pin widget 18h (mic1/2) for input, for mic also enable
	 * the vref
	 */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },

	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c },
#if 0
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8)) }, /*mic*/
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x02 << 8)) }, /*line-in*/
#endif
	{ } /* end */
};
/* Set mic1 nad line-in as output and mute mixer */
static struct hda_verb alc861_asus_ch6_init[] = {
	/* set pin widget 1Ah (line in) for output (Back Surround)*/
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* { 0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE }, */
	/* set pin widget 18h (mic1) for output (CLFE)*/
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* { 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE }, */
	{ 0x0c, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x0d, AC_VERB_SET_CONNECT_SEL, 0x00 },

	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb080 },
#if 0
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8)) }, /*mic*/
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8)) }, /*line in*/
#endif
	{ } /* end */
};

static struct hda_channel_mode alc861_asus_modes[2] = {
	{ 2, alc861_asus_ch2_init },
	{ 6, alc861_asus_ch6_init },
};

/* patch-ALC861 */

static struct snd_kcontrol_new alc861_base_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Front Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x05, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Side Playback Switch", 0x04, 0x0, HDA_OUTPUT),

        /*Input mixer control */
	/* HDA_CODEC_VOLUME("Input Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	   HDA_CODEC_MUTE("Input Playback Switch", 0x15, 0x0, HDA_OUTPUT), */
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x10, 0x01, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x03, HDA_INPUT),

        /* Capture mixer control */
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc861_3ST_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Front Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x05, 2, 0x0, HDA_OUTPUT),
	/*HDA_CODEC_MUTE("Side Playback Switch", 0x04, 0x0, HDA_OUTPUT), */

	/* Input mixer control */
	/* HDA_CODEC_VOLUME("Input Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	   HDA_CODEC_MUTE("Input Playback Switch", 0x15, 0x0, HDA_OUTPUT), */
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x10, 0x01, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x03, HDA_INPUT),

	/* Capture mixer control */
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
                .private_value = ARRAY_SIZE(alc861_threestack_modes),
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc861_toshiba_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Master Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),

        /*Capture mixer control */
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},

	{ } /* end */
};

static struct snd_kcontrol_new alc861_uniwill_m31_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Front Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x05, 2, 0x0, HDA_OUTPUT),
	/*HDA_CODEC_MUTE("Side Playback Switch", 0x04, 0x0, HDA_OUTPUT), */

	/* Input mixer control */
	/* HDA_CODEC_VOLUME("Input Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	   HDA_CODEC_MUTE("Input Playback Switch", 0x15, 0x0, HDA_OUTPUT), */
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x10, 0x01, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x03, HDA_INPUT),

	/* Capture mixer control */
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
                .private_value = ARRAY_SIZE(alc861_uniwill_m31_modes),
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc861_asus_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Front Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x06, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x05, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x05, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Side Playback Switch", 0x04, 0x0, HDA_OUTPUT),

	/* Input mixer control */
	HDA_CODEC_VOLUME("Input Playback Volume", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Input Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x15, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x10, 0x01, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x03, HDA_OUTPUT),

	/* Capture mixer control */
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
                .private_value = ARRAY_SIZE(alc861_asus_modes),
	},
	{ }
};

/* additional mixer */
static struct snd_kcontrol_new alc861_asus_laptop_mixer[] = {
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Beep Playback Volume", 0x23, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("PC Beep Playback Switch", 0x23, 0x0, HDA_OUTPUT),
	{ }
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc861_base_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	/* port-A for surround (rear panel) */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x0e, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-C for line-in (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* port-D for Front */
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x0b, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-E for HP out (front panel) */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x1f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-H for side (rear panel) */
	{ 0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x20, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* route front mic to ADC1*/
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Unmute DAC0~3 & spdif out*/
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Unmute Mixer 14 (mic) 1c (Line in)*/
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Unmute Stereo Mixer 15 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c}, /* Output 0~12 step */

	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* hp used DAC 3 (Front) */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
        {0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},

	{ }
};

static struct hda_verb alc861_threestack_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	/* port-A for surround (rear panel) */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-C for line-in (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* port-D for Front */
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x0b, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-E for HP out (front panel) */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* port-H for side (rear panel) */
	{ 0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* route front mic to ADC1*/
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Unmute DAC0~3 & spdif out*/
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Unmute Mixer 14 (mic) 1c (Line in)*/
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Unmute Stereo Mixer 15 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c}, /* Output 0~12 step */

	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* hp used DAC 3 (Front) */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
        {0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{ }
};

static struct hda_verb alc861_uniwill_m31_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	/* port-A for surround (rear panel) */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-C for line-in (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* port-D for Front */
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x0b, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-E for HP out (front panel) */
	/* this has to be set to VREF80 */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* port-H for side (rear panel) */
	{ 0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* route front mic to ADC1*/
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Unmute DAC0~3 & spdif out*/
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Unmute Mixer 14 (mic) 1c (Line in)*/
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Unmute Stereo Mixer 15 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c}, /* Output 0~12 step */

	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* hp used DAC 3 (Front) */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
        {0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{ }
};

static struct hda_verb alc861_asus_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	/* port-A for surround (rear panel)
	 * according to codec#0 this is the HP jack
	 */
	{ 0x0e, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0 }, /* was 0x00 */
	/* route front PCM to HP */
	{ 0x0e, AC_VERB_SET_CONNECT_SEL, 0x01 },
	/* port-B for mic-in (rear panel) with vref */
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-C for line-in (rear panel) */
	{ 0x0c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* port-D for Front */
	{ 0x0b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x0b, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-E for HP out (front panel) */
	/* this has to be set to VREF80 */
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* route front PCM to HP */
	{ 0x0f, AC_VERB_SET_CONNECT_SEL, 0x00 },
	/* port-F for mic-in (front panel) with vref */
	{ 0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	/* port-G for CLFE (rear panel) */
	{ 0x1f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* port-H for side (rear panel) */
	{ 0x20, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	/* CD-in */
	{ 0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20 },
	/* route front mic to ADC1*/
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Unmute DAC0~3 & spdif out*/
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Unmute Mixer 14 (mic) 1c (Line in)*/
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
        {0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Unmute Stereo Mixer 15 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c}, /* Output 0~12 step */

	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* hp used DAC 3 (Front) */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{ }
};

/* additional init verbs for ASUS laptops */
static struct hda_verb alc861_asus_laptop_init_verbs[] = {
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x45 }, /* HP-out */
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2) }, /* mute line-in */
	{ }
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc861_auto_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	/* {0x08, AC_VERB_SET_CONNECT_SEL, 0x00}, */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Unmute DAC0~3 & spdif out*/
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x06, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Unmute Mixer 14 (mic) 1c (Line in)*/
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x014, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x01c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Unmute Stereo Mixer 15 */
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, 0xb00c},

	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},

	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},	/* set Mic 1 */

	{ }
};

static struct hda_verb alc861_toshiba_init_verbs[] = {
	{0x0f, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},

	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc861_toshiba_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x0f, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x16, HDA_INPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	snd_hda_codec_amp_stereo(codec, 0x1a, HDA_INPUT, 3,
				 HDA_AMP_MUTE, present ? 0 : HDA_AMP_MUTE);
}

static void alc861_toshiba_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc861_toshiba_automute(codec);
}

/* pcm configuration: identiacal with ALC880 */
#define alc861_pcm_analog_playback	alc880_pcm_analog_playback
#define alc861_pcm_analog_capture	alc880_pcm_analog_capture
#define alc861_pcm_digital_playback	alc880_pcm_digital_playback
#define alc861_pcm_digital_capture	alc880_pcm_digital_capture


#define ALC861_DIGOUT_NID	0x07

static struct hda_channel_mode alc861_8ch_modes[1] = {
	{ 8, NULL }
};

static hda_nid_t alc861_dac_nids[4] = {
	/* front, surround, clfe, side */
	0x03, 0x06, 0x05, 0x04
};

static hda_nid_t alc660_dac_nids[3] = {
	/* front, clfe, surround */
	0x03, 0x05, 0x06
};

static hda_nid_t alc861_adc_nids[1] = {
	/* ADC0-2 */
	0x08,
};

static struct hda_input_mux alc861_capture_source = {
	.num_items = 5,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x3 },
		{ "Line", 0x1 },
		{ "CD", 0x4 },
		{ "Mixer", 0x5 },
	},
};

/* fill in the dac_nids table from the parsed pin configuration */
static int alc861_auto_fill_dac_nids(struct alc_spec *spec,
				     const struct auto_pin_cfg *cfg)
{
	int i;
	hda_nid_t nid;

	spec->multiout.dac_nids = spec->private_dac_nids;
	for (i = 0; i < cfg->line_outs; i++) {
		nid = cfg->line_out_pins[i];
		if (nid) {
			if (i >= ARRAY_SIZE(alc861_dac_nids))
				continue;
			spec->multiout.dac_nids[i] = alc861_dac_nids[i];
		}
	}
	spec->multiout.num_dacs = cfg->line_outs;
	return 0;
}

/* add playback controls from the parsed DAC table */
static int alc861_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = {
		"Front", "Surround", NULL /*CLFE*/, "Side"
	};
	hda_nid_t nid;
	int i, idx, err;

	for (i = 0; i < cfg->line_outs; i++) {
		nid = spec->multiout.dac_nids[i];
		if (!nid)
			continue;
		if (nid == 0x05) {
			/* Center/LFE */
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "Center Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 1, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "LFE Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		} else {
			for (idx = 0; idx < ARRAY_SIZE(alc861_dac_nids) - 1;
			     idx++)
				if (nid == alc861_dac_nids[idx])
					break;
			sprintf(name, "%s Playback Switch", chname[idx]);
			err = add_control(spec, ALC_CTL_BIND_MUTE, name,
					  HDA_COMPOSE_AMP_VAL(nid, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

static int alc861_auto_create_hp_ctls(struct alc_spec *spec, hda_nid_t pin)
{
	int err;
	hda_nid_t nid;

	if (!pin)
		return 0;

	if ((pin >= 0x0b && pin <= 0x10) || pin == 0x1f || pin == 0x20) {
		nid = 0x03;
		err = add_control(spec, ALC_CTL_WIDGET_MUTE,
				  "Headphone Playback Switch",
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		spec->multiout.hp_nid = nid;
	}
	return 0;
}

/* create playback/capture controls for input pins */
static int alc861_auto_create_analog_input_ctls(struct alc_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	struct hda_input_mux *imux = &spec->private_imux;
	int i, err, idx, idx1;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		switch (cfg->input_pins[i]) {
		case 0x0c:
			idx1 = 1;
			idx = 2;	/* Line In */
			break;
		case 0x0f:
			idx1 = 2;
			idx = 2;	/* Line In */
			break;
		case 0x0d:
			idx1 = 0;
			idx = 1;	/* Mic In */
			break;
		case 0x10:
			idx1 = 3;
			idx = 1;	/* Mic In */
			break;
		case 0x11:
			idx1 = 4;
			idx = 0;	/* CD */
			break;
		default:
			continue;
		}

		err = new_analog_input(spec, cfg->input_pins[i],
				       auto_pin_cfg_labels[i], idx, 0x15);
		if (err < 0)
			return err;

		imux->items[imux->num_items].label = auto_pin_cfg_labels[i];
		imux->items[imux->num_items].index = idx1;
		imux->num_items++;
	}
	return 0;
}

static struct snd_kcontrol_new alc861_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),

	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

static void alc861_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid,
					      int pin_type, int dac_idx)
{
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    pin_type);
	snd_hda_codec_write(codec, dac_idx, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    AMP_OUT_UNMUTE);
}

static void alc861_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	alc_subsystem_id(codec, 0x0e, 0x0f, 0x0b);
	for (i = 0; i < spec->autocfg.line_outs; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		if (nid)
			alc861_auto_set_output_and_unmute(codec, nid, pin_type,
							  spec->multiout.dac_nids[i]);
	}
}

static void alc861_auto_init_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pins[0];
	if (pin) /* connect to front */
		alc861_auto_set_output_and_unmute(codec, pin, PIN_HP,
						  spec->multiout.dac_nids[0]);
	pin = spec->autocfg.speaker_pins[0];
	if (pin)
		alc861_auto_set_output_and_unmute(codec, pin, PIN_OUT, 0);
}

static void alc861_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];
		if (nid >= 0x0c && nid <= 0x11) {
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    i <= AUTO_PIN_FRONT_MIC ?
					    PIN_VREF80 : PIN_IN);
		}
	}
}

/* parse the BIOS configuration and set up the alc_spec */
/* return 1 if successful, 0 if the proper config is not found,
 * or a negative error code
 */
static int alc861_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static hda_nid_t alc861_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc861_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */

	err = alc861_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc861_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc861_auto_create_hp_ctls(spec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = alc861_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC861_DIGOUT_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->init_verbs[spec->num_init_verbs++] = alc861_auto_init_verbs;

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;

	spec->adc_nids = alc861_adc_nids;
	spec->num_adc_nids = ARRAY_SIZE(alc861_adc_nids);
	spec->mixers[spec->num_mixers] = alc861_capture_mixer;
	spec->num_mixers++;

	return 1;
}

/* additional initialization for auto-configuration model */
static void alc861_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc861_auto_init_multi_out(codec);
	alc861_auto_init_hp_out(codec);
	alc861_auto_init_analog_input(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static struct hda_amp_list alc861_loopbacks[] = {
	{ 0x15, HDA_INPUT, 0 },
	{ 0x15, HDA_INPUT, 1 },
	{ 0x15, HDA_INPUT, 2 },
	{ 0x15, HDA_INPUT, 3 },
	{ } /* end */
};
#endif


/*
 * configuration and preset
 */
static const char *alc861_models[ALC861_MODEL_LAST] = {
	[ALC861_3ST]		= "3stack",
	[ALC660_3ST]		= "3stack-660",
	[ALC861_3ST_DIG]	= "3stack-dig",
	[ALC861_6ST_DIG]	= "6stack-dig",
	[ALC861_UNIWILL_M31]	= "uniwill-m31",
	[ALC861_TOSHIBA]	= "toshiba",
	[ALC861_ASUS]		= "asus",
	[ALC861_ASUS_LAPTOP]	= "asus-laptop",
	[ALC861_AUTO]		= "auto",
};

static struct snd_pci_quirk alc861_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x1205, "ASUS W7J", ALC861_3ST),
	SND_PCI_QUIRK(0x1043, 0x1335, "ASUS F2/3", ALC861_ASUS_LAPTOP),
	SND_PCI_QUIRK(0x1043, 0x1338, "ASUS F2/3", ALC861_ASUS_LAPTOP),
	SND_PCI_QUIRK(0x1043, 0x1393, "ASUS", ALC861_ASUS),
	SND_PCI_QUIRK(0x1043, 0x13d7, "ASUS A9rp", ALC861_ASUS_LAPTOP),
	SND_PCI_QUIRK(0x1043, 0x81cb, "ASUS P1-AH2", ALC861_3ST_DIG),
	SND_PCI_QUIRK(0x1179, 0xff00, "Toshiba", ALC861_TOSHIBA),
	/* FIXME: the entry below breaks Toshiba A100 (model=auto works!)
	 *        Any other models that need this preset?
	 */
	/* SND_PCI_QUIRK(0x1179, 0xff10, "Toshiba", ALC861_TOSHIBA), */
	SND_PCI_QUIRK(0x1462, 0x7254, "HP dx2200 (MSI MS-7254)", ALC861_3ST),
	SND_PCI_QUIRK(0x1462, 0x7297, "HP dx2250 (MSI MS-7297)", ALC861_3ST),
	SND_PCI_QUIRK(0x1584, 0x2b01, "Uniwill X40AIx", ALC861_UNIWILL_M31),
	SND_PCI_QUIRK(0x1584, 0x9072, "Uniwill m31", ALC861_UNIWILL_M31),
	SND_PCI_QUIRK(0x1584, 0x9075, "Airis Praxis N1212", ALC861_ASUS_LAPTOP),
	/* FIXME: the below seems conflict */
	/* SND_PCI_QUIRK(0x1584, 0x9075, "Uniwill", ALC861_UNIWILL_M31), */
	SND_PCI_QUIRK(0x1849, 0x0660, "Asrock 939SLI32", ALC660_3ST),
	SND_PCI_QUIRK(0x8086, 0xd600, "Intel", ALC861_3ST),
	{}
};

static struct alc_config_preset alc861_presets[] = {
	[ALC861_3ST] = {
		.mixers = { alc861_3ST_mixer },
		.init_verbs = { alc861_threestack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc861_threestack_modes),
		.channel_mode = alc861_threestack_modes,
		.need_dac_fix = 1,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC861_3ST_DIG] = {
		.mixers = { alc861_base_mixer },
		.init_verbs = { alc861_threestack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.dig_out_nid = ALC861_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861_threestack_modes),
		.channel_mode = alc861_threestack_modes,
		.need_dac_fix = 1,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC861_6ST_DIG] = {
		.mixers = { alc861_base_mixer },
		.init_verbs = { alc861_base_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.dig_out_nid = ALC861_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861_8ch_modes),
		.channel_mode = alc861_8ch_modes,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC660_3ST] = {
		.mixers = { alc861_3ST_mixer },
		.init_verbs = { alc861_threestack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc660_dac_nids),
		.dac_nids = alc660_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc861_threestack_modes),
		.channel_mode = alc861_threestack_modes,
		.need_dac_fix = 1,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC861_UNIWILL_M31] = {
		.mixers = { alc861_uniwill_m31_mixer },
		.init_verbs = { alc861_uniwill_m31_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.dig_out_nid = ALC861_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861_uniwill_m31_modes),
		.channel_mode = alc861_uniwill_m31_modes,
		.need_dac_fix = 1,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC861_TOSHIBA] = {
		.mixers = { alc861_toshiba_mixer },
		.init_verbs = { alc861_base_init_verbs,
				alc861_toshiba_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
		.unsol_event = alc861_toshiba_unsol_event,
		.init_hook = alc861_toshiba_automute,
	},
	[ALC861_ASUS] = {
		.mixers = { alc861_asus_mixer },
		.init_verbs = { alc861_asus_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.dig_out_nid = ALC861_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861_asus_modes),
		.channel_mode = alc861_asus_modes,
		.need_dac_fix = 1,
		.hp_nid = 0x06,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
	[ALC861_ASUS_LAPTOP] = {
		.mixers = { alc861_toshiba_mixer, alc861_asus_laptop_mixer },
		.init_verbs = { alc861_asus_init_verbs,
				alc861_asus_laptop_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861_dac_nids),
		.dac_nids = alc861_dac_nids,
		.dig_out_nid = ALC861_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.need_dac_fix = 1,
		.num_adc_nids = ARRAY_SIZE(alc861_adc_nids),
		.adc_nids = alc861_adc_nids,
		.input_mux = &alc861_capture_source,
	},
};


static int patch_alc861(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

        board_config = snd_hda_check_board_config(codec, ALC861_MODEL_LAST,
						  alc861_models,
						  alc861_cfg_tbl);

	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC861, "
		       "trying auto-probe from BIOS...\n");
		board_config = ALC861_AUTO;
	}

	if (board_config == ALC861_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc861_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
		   board_config = ALC861_3ST_DIG;
		}
	}

	if (board_config != ALC861_AUTO)
		setup_preset(spec, &alc861_presets[board_config]);

	spec->stream_name_analog = "ALC861 Analog";
	spec->stream_analog_playback = &alc861_pcm_analog_playback;
	spec->stream_analog_capture = &alc861_pcm_analog_capture;

	spec->stream_name_digital = "ALC861 Digital";
	spec->stream_digital_playback = &alc861_pcm_digital_playback;
	spec->stream_digital_capture = &alc861_pcm_digital_capture;

	spec->vmaster_nid = 0x03;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC861_AUTO)
		spec->init_hook = alc861_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc861_loopbacks;
#endif

	return 0;
}

/*
 * ALC861-VD support
 *
 * Based on ALC882
 *
 * In addition, an independent DAC
 */
#define ALC861VD_DIGOUT_NID	0x06

static hda_nid_t alc861vd_dac_nids[4] = {
	/* front, surr, clfe, side surr */
	0x02, 0x03, 0x04, 0x05
};

/* dac_nids for ALC660vd are in a different order - according to
 * Realtek's driver.
 * This should probably tesult in a different mixer for 6stack models
 * of ALC660vd codecs, but for now there is only 3stack mixer
 * - and it is the same as in 861vd.
 * adc_nids in ALC660vd are (is) the same as in 861vd
 */
static hda_nid_t alc660vd_dac_nids[3] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x04, 0x03
};

static hda_nid_t alc861vd_adc_nids[1] = {
	/* ADC0 */
	0x09,
};

static hda_nid_t alc861vd_capsrc_nids[1] = { 0x22 };

/* input MUX */
/* FIXME: should be a matrix-type input source selection */
static struct hda_input_mux alc861vd_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static struct hda_input_mux alc861vd_dallas_capture_source = {
	.num_items = 2,
	.items = {
		{ "Ext Mic", 0x0 },
		{ "Int Mic", 0x1 },
	},
};

static struct hda_input_mux alc861vd_hp_capture_source = {
	.num_items = 2,
	.items = {
		{ "Front Mic", 0x0 },
		{ "ATAPI Mic", 0x1 },
	},
};

#define alc861vd_mux_enum_info alc_mux_enum_info
#define alc861vd_mux_enum_get alc_mux_enum_get
/* ALC861VD has the ALC882-type input selection (but has only one ADC) */
#define alc861vd_mux_enum_put alc882_mux_enum_put

/*
 * 2ch mode
 */
static struct hda_channel_mode alc861vd_3stack_2ch_modes[1] = {
	{ 2, NULL }
};

/*
 * 6ch mode
 */
static struct hda_verb alc861vd_6stack_ch6_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

/*
 * 8ch mode
 */
static struct hda_verb alc861vd_6stack_ch8_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

static struct hda_channel_mode alc861vd_6stack_modes[2] = {
	{ 6, alc861vd_6stack_ch6_init },
	{ 8, alc861vd_6stack_ch8_init },
};

static struct snd_kcontrol_new alc861vd_chmode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc861vd_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x09, 0x0, HDA_INPUT),

	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc861vd_mux_enum_info,
		.get = alc861vd_mux_enum_get,
		.put = alc861vd_mux_enum_put,
	},
	{ } /* end */
};

/* Pin assignment: Front=0x14, Rear=0x15, CLFE=0x16, Side=0x17
 *                 Mic=0x18, Front Mic=0x19, Line-In=0x1a, HP=0x1b
 */
static struct snd_kcontrol_new alc861vd_6st_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),

	HDA_CODEC_VOLUME("Surround Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),

	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x04, 1, 0x0,
				HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x04, 2, 0x0,
				HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),

	HDA_CODEC_VOLUME("Side Playback Volume", 0x05, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),

	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),

	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),

	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),

	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),

	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),

	{ } /* end */
};

static struct snd_kcontrol_new alc861vd_3st_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),

	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),

	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),

	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),

	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),

	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),

	{ } /* end */
};

static struct snd_kcontrol_new alc861vd_lenovo_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	/*HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),*/
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),

	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),

	HDA_CODEC_VOLUME("Front Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),

	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),

	{ } /* end */
};

/* Pin assignment: Speaker=0x14, HP = 0x15,
 *                 Ext Mic=0x18, Int Mic = 0x19, CD = 0x1c, PC Beep = 0x1d
 */
static struct snd_kcontrol_new alc861vd_dallas_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Ext Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Ext Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Ext Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Int Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Int Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Beep Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Beep Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

/* Pin assignment: Speaker=0x14, Line-out = 0x15,
 *                 Front Mic=0x18, ATAPI Mic = 0x19,
 */
static struct snd_kcontrol_new alc861vd_hp_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("ATAPI Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("ATAPI Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),

	{ } /* end */
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc861vd_volume_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of
	 * the analog-loopback mixer widget
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/* Capture mixer: unmute Mic, F-Mic, Line, CD inputs */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	/*
	 * Set up output mixers (0x02 - 0x05)
	 */
	/* set vol=0 to output mixers */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	{ }
};

/*
 * 3-stack pin configuration:
 * front = 0x14, mic/clfe = 0x18, HP = 0x19, line/surr = 0x1a, f-mic = 0x1b
 */
static struct hda_verb alc861vd_3stack_init_verbs[] = {
	/*
	 * Set pin mode and muting
	 */
	/* set front pin widgets 0x14 for output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Mic (rear) pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line-2 In: Headphone output (output 0 - 0x0c) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

/*
 * 6-stack pin configuration:
 */
static struct hda_verb alc861vd_6stack_init_verbs[] = {
	/*
	 * Set pin mode and muting
	 */
	/* set front pin widgets 0x14 for output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Rear Pin: output 1 (0x0d) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* CLFE Pin: output 2 (0x0e) */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* Side Pin: output 3 (0x0f) */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x03},

	/* Mic (rear) pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line-2 In: Headphone output (output 0 - 0x0c) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{ }
};

static struct hda_verb alc861vd_eapd_verbs[] = {
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

static struct hda_verb alc660vd_eapd_verbs[] = {
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

static struct hda_verb alc861vd_lenovo_unsol_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{}
};

/* toggle speaker-output according to the hp-jack state */
static void alc861vd_lenovo_hp_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc861vd_lenovo_mic_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x0b, HDA_INPUT, 1,
				 HDA_AMP_MUTE, bits);
}

static void alc861vd_lenovo_automute(struct hda_codec *codec)
{
	alc861vd_lenovo_hp_automute(codec);
	alc861vd_lenovo_mic_automute(codec);
}

static void alc861vd_lenovo_unsol_event(struct hda_codec *codec,
					unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc861vd_lenovo_hp_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc861vd_lenovo_mic_automute(codec);
		break;
	}
}

static struct hda_verb alc861vd_dallas_verbs[] = {
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},

	{ } /* end */
};

/* toggle speaker-output according to the hp-jack state */
static void alc861vd_dallas_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x15, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
}

static void alc861vd_dallas_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc861vd_dallas_automute(codec);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc861vd_loopbacks	alc880_loopbacks
#endif

/* pcm configuration: identiacal with ALC880 */
#define alc861vd_pcm_analog_playback	alc880_pcm_analog_playback
#define alc861vd_pcm_analog_capture	alc880_pcm_analog_capture
#define alc861vd_pcm_digital_playback	alc880_pcm_digital_playback
#define alc861vd_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * configuration and preset
 */
static const char *alc861vd_models[ALC861VD_MODEL_LAST] = {
	[ALC660VD_3ST]		= "3stack-660",
	[ALC660VD_3ST_DIG]	= "3stack-660-digout",
	[ALC861VD_3ST]		= "3stack",
	[ALC861VD_3ST_DIG]	= "3stack-digout",
	[ALC861VD_6ST_DIG]	= "6stack-digout",
	[ALC861VD_LENOVO]	= "lenovo",
	[ALC861VD_DALLAS]	= "dallas",
	[ALC861VD_HP]		= "hp",
	[ALC861VD_AUTO]		= "auto",
};

static struct snd_pci_quirk alc861vd_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0xa88d, "Realtek ALC660 demo", ALC660VD_3ST),
	SND_PCI_QUIRK(0x103c, 0x30bf, "HP TX1000", ALC861VD_HP),
	SND_PCI_QUIRK(0x1043, 0x12e2, "Asus z35m", ALC660VD_3ST),
	SND_PCI_QUIRK(0x1043, 0x1339, "Asus G1", ALC660VD_3ST),
	SND_PCI_QUIRK(0x1043, 0x1633, "Asus V1Sn", ALC861VD_LENOVO),
	SND_PCI_QUIRK(0x1043, 0x81e7, "ASUS", ALC660VD_3ST_DIG),
	SND_PCI_QUIRK(0x10de, 0x03f0, "Realtek ALC660 demo", ALC660VD_3ST),
	SND_PCI_QUIRK(0x1179, 0xff00, "Toshiba A135", ALC861VD_LENOVO),
	/*SND_PCI_QUIRK(0x1179, 0xff00, "DALLAS", ALC861VD_DALLAS),*/ /*lenovo*/
	SND_PCI_QUIRK(0x1179, 0xff01, "DALLAS", ALC861VD_DALLAS),
	SND_PCI_QUIRK(0x1179, 0xff03, "Toshiba P205", ALC861VD_LENOVO),
	SND_PCI_QUIRK(0x1179, 0xff31, "Toshiba L30-149", ALC861VD_DALLAS),
	SND_PCI_QUIRK(0x1565, 0x820d, "Biostar NF61S SE", ALC861VD_6ST_DIG),
	SND_PCI_QUIRK(0x17aa, 0x2066, "Lenovo", ALC861VD_LENOVO),
	SND_PCI_QUIRK(0x17aa, 0x3802, "Lenovo 3000 C200", ALC861VD_LENOVO),
	SND_PCI_QUIRK(0x17aa, 0x384e, "Lenovo 3000 N200", ALC861VD_LENOVO),
	SND_PCI_QUIRK(0x1849, 0x0862, "ASRock K8NF6G-VSTA", ALC861VD_6ST_DIG),
	{}
};

static struct alc_config_preset alc861vd_presets[] = {
	[ALC660VD_3ST] = {
		.mixers = { alc861vd_3st_mixer },
		.init_verbs = { alc861vd_volume_init_verbs,
				 alc861vd_3stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc660vd_dac_nids),
		.dac_nids = alc660vd_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_capture_source,
	},
	[ALC660VD_3ST_DIG] = {
		.mixers = { alc861vd_3st_mixer },
		.init_verbs = { alc861vd_volume_init_verbs,
				 alc861vd_3stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc660vd_dac_nids),
		.dac_nids = alc660vd_dac_nids,
		.dig_out_nid = ALC861VD_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_capture_source,
	},
	[ALC861VD_3ST] = {
		.mixers = { alc861vd_3st_mixer },
		.init_verbs = { alc861vd_volume_init_verbs,
				 alc861vd_3stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861vd_dac_nids),
		.dac_nids = alc861vd_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_capture_source,
	},
	[ALC861VD_3ST_DIG] = {
		.mixers = { alc861vd_3st_mixer },
		.init_verbs = { alc861vd_volume_init_verbs,
		 		 alc861vd_3stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861vd_dac_nids),
		.dac_nids = alc861vd_dac_nids,
		.dig_out_nid = ALC861VD_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_capture_source,
	},
	[ALC861VD_6ST_DIG] = {
		.mixers = { alc861vd_6st_mixer, alc861vd_chmode_mixer },
		.init_verbs = { alc861vd_volume_init_verbs,
				alc861vd_6stack_init_verbs },
		.num_dacs = ARRAY_SIZE(alc861vd_dac_nids),
		.dac_nids = alc861vd_dac_nids,
		.dig_out_nid = ALC861VD_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861vd_6stack_modes),
		.channel_mode = alc861vd_6stack_modes,
		.input_mux = &alc861vd_capture_source,
	},
	[ALC861VD_LENOVO] = {
		.mixers = { alc861vd_lenovo_mixer },
		.init_verbs = { alc861vd_volume_init_verbs,
				alc861vd_3stack_init_verbs,
				alc861vd_eapd_verbs,
				alc861vd_lenovo_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc660vd_dac_nids),
		.dac_nids = alc660vd_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_capture_source,
		.unsol_event = alc861vd_lenovo_unsol_event,
		.init_hook = alc861vd_lenovo_automute,
	},
	[ALC861VD_DALLAS] = {
		.mixers = { alc861vd_dallas_mixer },
		.init_verbs = { alc861vd_dallas_verbs },
		.num_dacs = ARRAY_SIZE(alc861vd_dac_nids),
		.dac_nids = alc861vd_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_dallas_capture_source,
		.unsol_event = alc861vd_dallas_unsol_event,
		.init_hook = alc861vd_dallas_automute,
	},
	[ALC861VD_HP] = {
		.mixers = { alc861vd_hp_mixer },
		.init_verbs = { alc861vd_dallas_verbs, alc861vd_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc861vd_dac_nids),
		.dac_nids = alc861vd_dac_nids,
		.dig_out_nid = ALC861VD_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_hp_capture_source,
		.unsol_event = alc861vd_dallas_unsol_event,
		.init_hook = alc861vd_dallas_automute,
	},
};

/*
 * BIOS auto configuration
 */
static void alc861vd_auto_set_output_and_unmute(struct hda_codec *codec,
				hda_nid_t nid, int pin_type, int dac_idx)
{
	alc_set_pin_output(codec, nid, pin_type);
}

static void alc861vd_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	alc_subsystem_id(codec, 0x15, 0x1b, 0x14);
	for (i = 0; i <= HDA_SIDE; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		if (nid)
			alc861vd_auto_set_output_and_unmute(codec, nid,
							    pin_type, i);
	}
}


static void alc861vd_auto_init_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pins[0];
	if (pin) /* connect to front and  use dac 0 */
		alc861vd_auto_set_output_and_unmute(codec, pin, PIN_HP, 0);
	pin = spec->autocfg.speaker_pins[0];
	if (pin)
		alc861vd_auto_set_output_and_unmute(codec, pin, PIN_OUT, 0);
}

#define alc861vd_is_input_pin(nid)	alc880_is_input_pin(nid)
#define ALC861VD_PIN_CD_NID		ALC880_PIN_CD_NID

static void alc861vd_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];
		if (alc861vd_is_input_pin(nid)) {
			snd_hda_codec_write(codec, nid, 0,
					AC_VERB_SET_PIN_WIDGET_CONTROL,
					i <= AUTO_PIN_FRONT_MIC ?
							PIN_VREF80 : PIN_IN);
			if (nid != ALC861VD_PIN_CD_NID)
				snd_hda_codec_write(codec, nid, 0,
						AC_VERB_SET_AMP_GAIN_MUTE,
						AMP_OUT_MUTE);
		}
	}
}

#define alc861vd_auto_init_input_src	alc882_auto_init_input_src

#define alc861vd_idx_to_mixer_vol(nid)		((nid) + 0x02)
#define alc861vd_idx_to_mixer_switch(nid)	((nid) + 0x0c)

/* add playback controls from the parsed DAC table */
/* Based on ALC880 version. But ALC861VD has separate,
 * different NIDs for mute/unmute switch and volume control */
static int alc861vd_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = {"Front", "Surround", "CLFE", "Side"};
	hda_nid_t nid_v, nid_s;
	int i, err;

	for (i = 0; i < cfg->line_outs; i++) {
		if (!spec->multiout.dac_nids[i])
			continue;
		nid_v = alc861vd_idx_to_mixer_vol(
				alc880_dac_to_idx(
					spec->multiout.dac_nids[i]));
		nid_s = alc861vd_idx_to_mixer_switch(
				alc880_dac_to_idx(
					spec->multiout.dac_nids[i]));

		if (i == 2) {
			/* Center/LFE */
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "Center Playback Volume",
					  HDA_COMPOSE_AMP_VAL(nid_v, 1, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "LFE Playback Volume",
					  HDA_COMPOSE_AMP_VAL(nid_v, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "Center Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid_s, 1, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "LFE Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid_s, 2, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
					  HDA_COMPOSE_AMP_VAL(nid_v, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = add_control(spec, ALC_CTL_BIND_MUTE, name,
					  HDA_COMPOSE_AMP_VAL(nid_s, 3, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

/* add playback controls for speaker and HP outputs */
/* Based on ALC880 version. But ALC861VD has separate,
 * different NIDs for mute/unmute switch and volume control */
static int alc861vd_auto_create_extra_out(struct alc_spec *spec,
					hda_nid_t pin, const char *pfx)
{
	hda_nid_t nid_v, nid_s;
	int err;
	char name[32];

	if (!pin)
		return 0;

	if (alc880_is_fixed_pin(pin)) {
		nid_v = alc880_idx_to_dac(alc880_fixed_pin_idx(pin));
		/* specify the DAC as the extra output */
		if (!spec->multiout.hp_nid)
			spec->multiout.hp_nid = nid_v;
		else
			spec->multiout.extra_out_nid[0] = nid_v;
		/* control HP volume/switch on the output mixer amp */
		nid_v = alc861vd_idx_to_mixer_vol(
				alc880_fixed_pin_idx(pin));
		nid_s = alc861vd_idx_to_mixer_switch(
				alc880_fixed_pin_idx(pin));

		sprintf(name, "%s Playback Volume", pfx);
		err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
				  HDA_COMPOSE_AMP_VAL(nid_v, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		sprintf(name, "%s Playback Switch", pfx);
		err = add_control(spec, ALC_CTL_BIND_MUTE, name,
				  HDA_COMPOSE_AMP_VAL(nid_s, 3, 2, HDA_INPUT));
		if (err < 0)
			return err;
	} else if (alc880_is_multi_pin(pin)) {
		/* set manual connection */
		/* we have only a switch on HP-out PIN */
		sprintf(name, "%s Playback Switch", pfx);
		err = add_control(spec, ALC_CTL_WIDGET_MUTE, name,
				  HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
	}
	return 0;
}

/* parse the BIOS configuration and set up the alc_spec
 * return 1 if successful, 0 if the proper config is not found,
 * or a negative error code
 * Based on ALC880 version - had to change it to override
 * alc880_auto_create_extra_out and alc880_auto_create_multi_out_ctls */
static int alc861vd_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static hda_nid_t alc861vd_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc861vd_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */

	err = alc880_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc861vd_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc861vd_auto_create_extra_out(spec,
					     spec->autocfg.speaker_pins[0],
					     "Speaker");
	if (err < 0)
		return err;
	err = alc861vd_auto_create_extra_out(spec,
					     spec->autocfg.hp_pins[0],
					     "Headphone");
	if (err < 0)
		return err;
	err = alc880_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC861VD_DIGOUT_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->init_verbs[spec->num_init_verbs++]
		= alc861vd_volume_init_verbs;

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	return 1;
}

/* additional initialization for auto-configuration model */
static void alc861vd_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc861vd_auto_init_multi_out(codec);
	alc861vd_auto_init_hp_out(codec);
	alc861vd_auto_init_analog_input(codec);
	alc861vd_auto_init_input_src(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

static int patch_alc861vd(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	board_config = snd_hda_check_board_config(codec, ALC861VD_MODEL_LAST,
						  alc861vd_models,
						  alc861vd_cfg_tbl);

	if (board_config < 0 || board_config >= ALC861VD_MODEL_LAST) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC660VD/"
			"ALC861VD, trying auto-probe from BIOS...\n");
		board_config = ALC861VD_AUTO;
	}

	if (board_config == ALC861VD_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc861vd_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC861VD_3ST;
		}
	}

	if (board_config != ALC861VD_AUTO)
		setup_preset(spec, &alc861vd_presets[board_config]);

	if (codec->vendor_id == 0x10ec0660) {
		spec->stream_name_analog = "ALC660-VD Analog";
		spec->stream_name_digital = "ALC660-VD Digital";
		/* always turn on EAPD */
		spec->init_verbs[spec->num_init_verbs++] = alc660vd_eapd_verbs;
	} else {
		spec->stream_name_analog = "ALC861VD Analog";
		spec->stream_name_digital = "ALC861VD Digital";
	}

	spec->stream_analog_playback = &alc861vd_pcm_analog_playback;
	spec->stream_analog_capture = &alc861vd_pcm_analog_capture;

	spec->stream_digital_playback = &alc861vd_pcm_digital_playback;
	spec->stream_digital_capture = &alc861vd_pcm_digital_capture;

	spec->adc_nids = alc861vd_adc_nids;
	spec->num_adc_nids = ARRAY_SIZE(alc861vd_adc_nids);
	spec->capsrc_nids = alc861vd_capsrc_nids;

	spec->mixers[spec->num_mixers] = alc861vd_capture_mixer;
	spec->num_mixers++;

	spec->vmaster_nid = 0x02;

	codec->patch_ops = alc_patch_ops;

	if (board_config == ALC861VD_AUTO)
		spec->init_hook = alc861vd_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc861vd_loopbacks;
#endif

	return 0;
}

/*
 * ALC662 support
 *
 * ALC662 is almost identical with ALC880 but has cleaner and more flexible
 * configuration.  Each pin widget can choose any input DACs and a mixer.
 * Each ADC is connected from a mixer of all inputs.  This makes possible
 * 6-channel independent captures.
 *
 * In addition, an independent DAC for the multi-playback (not used in this
 * driver yet).
 */
#define ALC662_DIGOUT_NID	0x06
#define ALC662_DIGIN_NID	0x0a

static hda_nid_t alc662_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x03, 0x04
};

static hda_nid_t alc662_adc_nids[1] = {
	/* ADC1-2 */
	0x09,
};

static hda_nid_t alc662_capsrc_nids[1] = { 0x22 };

/* input MUX */
/* FIXME: should be a matrix-type input source selection */
static struct hda_input_mux alc662_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static struct hda_input_mux alc662_lenovo_101e_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x1 },
		{ "Line", 0x2 },
	},
};

static struct hda_input_mux alc662_eeepc_capture_source = {
	.num_items = 2,
	.items = {
		{ "i-Mic", 0x1 },
		{ "e-Mic", 0x0 },
	},
};

static struct hda_input_mux alc663_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
	},
};

static struct hda_input_mux alc663_m51va_capture_source = {
	.num_items = 2,
	.items = {
		{ "Ext-Mic", 0x0 },
		{ "D-Mic", 0x9 },
	},
};

#define alc662_mux_enum_info alc_mux_enum_info
#define alc662_mux_enum_get alc_mux_enum_get
#define alc662_mux_enum_put alc882_mux_enum_put

/*
 * 2ch mode
 */
static struct hda_channel_mode alc662_3ST_2ch_modes[1] = {
	{ 2, NULL }
};

/*
 * 2ch mode
 */
static struct hda_verb alc662_3ST_ch2_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 6ch mode
 */
static struct hda_verb alc662_3ST_ch6_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static struct hda_channel_mode alc662_3ST_6ch_modes[2] = {
	{ 2, alc662_3ST_ch2_init },
	{ 6, alc662_3ST_ch6_init },
};

/*
 * 2ch mode
 */
static struct hda_verb alc662_sixstack_ch6_init[] = {
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

/*
 * 6ch mode
 */
static struct hda_verb alc662_sixstack_ch8_init[] = {
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

static struct hda_channel_mode alc662_5stack_modes[2] = {
	{ 2, alc662_sixstack_ch6_init },
	{ 6, alc662_sixstack_ch8_init },
};

/* Pin assignment: Front=0x14, Rear=0x15, CLFE=0x16, Side=0x17
 *                 Mic=0x18, Front Mic=0x19, Line-In=0x1a, HP=0x1b
 */

static struct snd_kcontrol_new alc662_base_mixer[] = {
	/* output mixer control */
	HDA_CODEC_VOLUME("Front Playback Volume", 0x2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x0c, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x3, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x0d, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x04, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x04, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x0e, 1, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),

	/*Input mixer control */
	HDA_CODEC_VOLUME("CD Playback Volume", 0xb, 0x4, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0xb, 0x4, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0xb, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0xb, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0xb, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0xb, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0xb, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0xb, 0x01, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc662_3ST_2ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x0c, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc662_3ST_6ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x0c, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x0d, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x04, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x04, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x0e, 1, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("PC Speaker Playback Volume", 0x0b, 0x05, HDA_INPUT),
	HDA_CODEC_MUTE("PC Speaker Playback Switch", 0x0b, 0x05, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc662_lenovo_101e_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x02, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x03, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc662_eeepc_p701_mixer[] = {
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Line-Out Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Line-Out Playback Switch", 0x1b, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("e-Mic Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("e-Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("e-Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),

	HDA_CODEC_VOLUME("i-Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("i-Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("i-Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc662_eeepc_ep20_mixer[] = {
	HDA_CODEC_VOLUME("Line-Out Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Line-Out Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x03, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x04, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x04, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x04, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x04, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("MuteCtrl Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static struct hda_bind_ctls alc663_asus_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x02, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x03, 3, 0, HDA_OUTPUT),
		0
	},
};

static struct hda_bind_ctls alc663_asus_one_bind_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x21, 3, 0, HDA_OUTPUT),
		0
	},
};

static struct snd_kcontrol_new alc663_m51va_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc663_asus_bind_master_vol),
	HDA_BIND_SW("Master Playback Switch", &alc663_asus_one_bind_switch),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static struct hda_bind_ctls alc663_asus_tree_bind_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x15, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x21, 3, 0, HDA_OUTPUT),
		0
	},
};

static struct snd_kcontrol_new alc663_two_hp_m1_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc663_asus_bind_master_vol),
	HDA_BIND_SW("Master Playback Switch", &alc663_asus_tree_bind_switch),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("F-Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("F-Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),

	{ } /* end */
};

static struct hda_bind_ctls alc663_asus_four_bind_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x15, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x1b, 3, 0, HDA_OUTPUT),
		0
	},
};

static struct snd_kcontrol_new alc663_two_hp_m2_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc663_asus_bind_master_vol),
	HDA_BIND_SW("Master Playback Switch", &alc663_asus_four_bind_switch),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("F-Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("F-Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc662_1bjd_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("F-Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("F-Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static struct hda_bind_ctls alc663_asus_two_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x02, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x04, 3, 0, HDA_OUTPUT),
		0
	},
};

static struct hda_bind_ctls alc663_asus_two_bind_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x16, 3, 0, HDA_OUTPUT),
		0
	},
};

static struct snd_kcontrol_new alc663_asus_21jd_clfe_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume",
				&alc663_asus_two_bind_master_vol),
	HDA_BIND_SW("Master Playback Switch", &alc663_asus_two_bind_switch),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x21, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc663_asus_15jd_clfe_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc663_asus_bind_master_vol),
	HDA_BIND_SW("Master Playback Switch", &alc663_asus_two_bind_switch),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc663_g71v_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Front Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x21, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("i-Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("i-Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc663_g50v_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x21, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("i-Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("i-Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	{ } /* end */
};

static struct snd_kcontrol_new alc662_chmode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static struct hda_verb alc662_init_verbs[] = {
	/* ADC: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Front mixer: unmute input/output amp left and right (volume = 0) */

	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Front Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Rear Pin: output 1 (0x0d) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* CLFE Pin: output 2 (0x0e) */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* Mic (rear) pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line-2 In: Headphone output (output 0 - 0x0c) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* CD pin widget for input */
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(4)},

	/* always trun on EAPD */
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},

	{ }
};

static struct hda_verb alc662_sue_init_verbs[] = {
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_FRONT_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_HP_EVENT},
	{}
};

static struct hda_verb alc662_eeepc_sue_init_verbs[] = {
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

/* Set Unsolicited Event*/
static struct hda_verb alc662_eeepc_ep20_sue_init_verbs[] = {
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static struct hda_verb alc662_auto_init_verbs[] = {
	/*
	 * Unmute ADC and set the default input to mic-in
	 */
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/* Unmute input amps (CD, Line In, Mic 1 & Mic 2) of the analog-loopback
	 * mixer widget
	 * Note: PASD motherboards uses the Line In 2 as the input for front
	 * panel mic (mic 2)
	 */
	/* Amp Indices: Mic1 = 0, Mic2 = 1, Line1 = 2, Line2 = 3, CD = 4 */
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},

	/*
	 * Set up output mixers (0x0c - 0x0f)
	 */
	/* set vol=0 to output mixers */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/* set up input amps for analog loopback */
	/* Amp Indices: DAC = 0, mixer = 1 */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},


	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 16, 17, 0b */
	/* Input mixer */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{ }
};

/* additional verbs for ALC663 */
static struct hda_verb alc663_auto_init_verbs[] = {
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{ }
};

static struct hda_verb alc663_m51va_init_verbs[] = {
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x01},	/* Headphone */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(9)},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static struct hda_verb alc663_21jd_amic_init_verbs[] = {
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x01},	/* Headphone */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static struct hda_verb alc662_1bjd_amic_init_verbs[] = {
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},	/* Headphone */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static struct hda_verb alc663_15jd_amic_init_verbs[] = {
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},	/* Headphone */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static struct hda_verb alc663_two_hp_amic_m1_init_verbs[] = {
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x0},	/* Headphone */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x0},	/* Headphone */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static struct hda_verb alc663_two_hp_amic_m2_init_verbs[] = {
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x01},	/* Headphone */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},	/* Headphone */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static struct hda_verb alc663_g71v_init_verbs[] = {
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* {0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, */
	/* {0x15, AC_VERB_SET_CONNECT_SEL, 0x01}, */ /* Headphone */

	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x00},	/* Headphone */

	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_FRONT_EVENT},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_HP_EVENT},
	{}
};

static struct hda_verb alc663_g50v_init_verbs[] = {
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x00},	/* Headphone */

	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static struct hda_verb alc662_ecs_init_verbs[] = {
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, 0x701f},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

/* capture mixer elements */
static struct snd_kcontrol_new alc662_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x09, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* The multiple "Capture Source" controls confuse alsamixer
		 * So call somewhat different..
		 */
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc662_mux_enum_info,
		.get = alc662_mux_enum_get,
		.put = alc662_mux_enum_put,
	},
	{ } /* end */
};

static struct snd_kcontrol_new alc662_auto_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x09, 0x0, HDA_INPUT),
	{ } /* end */
};

static void alc662_lenovo_101e_ispeaker_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc662_lenovo_101e_all_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

 	present = snd_hda_codec_read(codec, 0x1b, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc662_lenovo_101e_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc662_lenovo_101e_all_automute(codec);
	if ((res >> 26) == ALC880_FRONT_EVENT)
		alc662_lenovo_101e_ispeaker_automute(codec);
}

static void alc662_eeepc_mic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0,
				     AC_VERB_GET_PIN_SENSE, 0) & 0x80000000;
	snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    0x7000 | (0x00 << 8) | (present ? 0 : 0x80));
	snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    0x7000 | (0x00 << 8) | (present ? 0 : 0x80));
	snd_hda_codec_write(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    0x7000 | (0x01 << 8) | (present ? 0x80 : 0));
	snd_hda_codec_write(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    0x7000 | (0x01 << 8) | (present ? 0x80 : 0));
}

/* unsolicited event for HP jack sensing */
static void alc662_eeepc_unsol_event(struct hda_codec *codec,
				     unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc262_hippo1_automute( codec );

	if ((res >> 26) == ALC880_MIC_EVENT)
		alc662_eeepc_mic_automute(codec);
}

static void alc662_eeepc_inithook(struct hda_codec *codec)
{
	alc262_hippo1_automute( codec );
	alc662_eeepc_mic_automute(codec);
}

static void alc662_eeepc_ep20_automute(struct hda_codec *codec)
{
	unsigned int mute;
	unsigned int present;

	snd_hda_codec_read(codec, 0x14, 0, AC_VERB_SET_PIN_SENSE, 0);
	present = snd_hda_codec_read(codec, 0x14, 0,
				     AC_VERB_GET_PIN_SENSE, 0);
	present = (present & 0x80000000) != 0;
	if (present) {
		/* mute internal speaker */
		snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
					HDA_AMP_MUTE, HDA_AMP_MUTE);
	} else {
		/* unmute internal speaker if necessary */
		mute = snd_hda_codec_amp_read(codec, 0x14, 0, HDA_OUTPUT, 0);
		snd_hda_codec_amp_stereo(codec, 0x1b, HDA_OUTPUT, 0,
					HDA_AMP_MUTE, mute);
	}
}

/* unsolicited event for HP jack sensing */
static void alc662_eeepc_ep20_unsol_event(struct hda_codec *codec,
					  unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc662_eeepc_ep20_automute(codec);
}

static void alc662_eeepc_ep20_inithook(struct hda_codec *codec)
{
	alc662_eeepc_ep20_automute(codec);
}

static void alc663_m51va_speaker_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x21, 0,
			AC_VERB_GET_PIN_SENSE, 0)
			& AC_PINSENSE_PRESENCE;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 0,
				AMP_IN_MUTE(0), bits);
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 1,
				AMP_IN_MUTE(0), bits);
}

static void alc663_21jd_two_speaker_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x21, 0,
			AC_VERB_GET_PIN_SENSE, 0)
			& AC_PINSENSE_PRESENCE;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 0,
				AMP_IN_MUTE(0), bits);
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 1,
				AMP_IN_MUTE(0), bits);
	snd_hda_codec_amp_stereo(codec, 0x0e, HDA_INPUT, 0,
				AMP_IN_MUTE(0), bits);
	snd_hda_codec_amp_stereo(codec, 0x0e, HDA_INPUT, 1,
				AMP_IN_MUTE(0), bits);
}

static void alc663_15jd_two_speaker_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x15, 0,
			AC_VERB_GET_PIN_SENSE, 0)
			& AC_PINSENSE_PRESENCE;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 0,
				AMP_IN_MUTE(0), bits);
	snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 1,
				AMP_IN_MUTE(0), bits);
	snd_hda_codec_amp_stereo(codec, 0x0e, HDA_INPUT, 0,
				AMP_IN_MUTE(0), bits);
	snd_hda_codec_amp_stereo(codec, 0x0e, HDA_INPUT, 1,
				AMP_IN_MUTE(0), bits);
}

static void alc662_f5z_speaker_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x1b, 0,
			AC_VERB_GET_PIN_SENSE, 0)
			& AC_PINSENSE_PRESENCE;
	bits = present ? 0 : PIN_OUT;
	snd_hda_codec_write(codec, 0x14, 0,
			 AC_VERB_SET_PIN_WIDGET_CONTROL, bits);
}

static void alc663_two_hp_m1_speaker_automute(struct hda_codec *codec)
{
	unsigned int present1, present2;

	present1 = snd_hda_codec_read(codec, 0x21, 0,
			AC_VERB_GET_PIN_SENSE, 0)
			& AC_PINSENSE_PRESENCE;
	present2 = snd_hda_codec_read(codec, 0x15, 0,
			AC_VERB_GET_PIN_SENSE, 0)
			& AC_PINSENSE_PRESENCE;

	if (present1 || present2) {
		snd_hda_codec_write_cache(codec, 0x14, 0,
			AC_VERB_SET_PIN_WIDGET_CONTROL, 0);
	} else {
		snd_hda_codec_write_cache(codec, 0x14, 0,
			AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
	}
}

static void alc663_two_hp_m2_speaker_automute(struct hda_codec *codec)
{
	unsigned int present1, present2;

	present1 = snd_hda_codec_read(codec, 0x1b, 0,
				AC_VERB_GET_PIN_SENSE, 0)
				& AC_PINSENSE_PRESENCE;
	present2 = snd_hda_codec_read(codec, 0x15, 0,
				AC_VERB_GET_PIN_SENSE, 0)
				& AC_PINSENSE_PRESENCE;

	if (present1 || present2) {
		snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 0,
				AMP_IN_MUTE(0), AMP_IN_MUTE(0));
		snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 1,
				AMP_IN_MUTE(0), AMP_IN_MUTE(0));
	} else {
		snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 0,
				AMP_IN_MUTE(0), 0);
		snd_hda_codec_amp_stereo(codec, 0x0c, HDA_INPUT, 1,
				AMP_IN_MUTE(0), 0);
	}
}

static void alc663_m51va_mic_automute(struct hda_codec *codec)
{
	unsigned int present;

	present = snd_hda_codec_read(codec, 0x18, 0,
			AC_VERB_GET_PIN_SENSE, 0)
			& AC_PINSENSE_PRESENCE;
	snd_hda_codec_write_cache(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			0x7000 | (0x00 << 8) | (present ? 0 : 0x80));
	snd_hda_codec_write_cache(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			0x7000 | (0x00 << 8) | (present ? 0 : 0x80));
	snd_hda_codec_write_cache(codec, 0x22, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			0x7000 | (0x09 << 8) | (present ? 0x80 : 0));
	snd_hda_codec_write_cache(codec, 0x23, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			0x7000 | (0x09 << 8) | (present ? 0x80 : 0));
}

static void alc663_m51va_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc663_m51va_speaker_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc663_m51va_mic_automute(codec);
		break;
	}
}

static void alc663_m51va_inithook(struct hda_codec *codec)
{
	alc663_m51va_speaker_automute(codec);
	alc663_m51va_mic_automute(codec);
}

/* ***************** Mode1 ******************************/
static void alc663_mode1_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc663_m51va_speaker_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc662_eeepc_mic_automute(codec);
		break;
	}
}

static void alc663_mode1_inithook(struct hda_codec *codec)
{
	alc663_m51va_speaker_automute(codec);
	alc662_eeepc_mic_automute(codec);
}
/* ***************** Mode2 ******************************/
static void alc662_mode2_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc662_f5z_speaker_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc662_eeepc_mic_automute(codec);
		break;
	}
}

static void alc662_mode2_inithook(struct hda_codec *codec)
{
	alc662_f5z_speaker_automute(codec);
	alc662_eeepc_mic_automute(codec);
}
/* ***************** Mode3 ******************************/
static void alc663_mode3_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc663_two_hp_m1_speaker_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc662_eeepc_mic_automute(codec);
		break;
	}
}

static void alc663_mode3_inithook(struct hda_codec *codec)
{
	alc663_two_hp_m1_speaker_automute(codec);
	alc662_eeepc_mic_automute(codec);
}
/* ***************** Mode4 ******************************/
static void alc663_mode4_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc663_21jd_two_speaker_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc662_eeepc_mic_automute(codec);
		break;
	}
}

static void alc663_mode4_inithook(struct hda_codec *codec)
{
	alc663_21jd_two_speaker_automute(codec);
	alc662_eeepc_mic_automute(codec);
}
/* ***************** Mode5 ******************************/
static void alc663_mode5_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc663_15jd_two_speaker_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc662_eeepc_mic_automute(codec);
		break;
	}
}

static void alc663_mode5_inithook(struct hda_codec *codec)
{
	alc663_15jd_two_speaker_automute(codec);
	alc662_eeepc_mic_automute(codec);
}
/* ***************** Mode6 ******************************/
static void alc663_mode6_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc663_two_hp_m2_speaker_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc662_eeepc_mic_automute(codec);
		break;
	}
}

static void alc663_mode6_inithook(struct hda_codec *codec)
{
	alc663_two_hp_m2_speaker_automute(codec);
	alc662_eeepc_mic_automute(codec);
}

static void alc663_g71v_hp_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x21, 0,
				     AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x15, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc663_g71v_front_automute(struct hda_codec *codec)
{
	unsigned int present;
	unsigned char bits;

	present = snd_hda_codec_read(codec, 0x15, 0,
				     AC_VERB_GET_PIN_SENSE, 0)
		& AC_PINSENSE_PRESENCE;
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x14, HDA_OUTPUT, 0,
				 HDA_AMP_MUTE, bits);
}

static void alc663_g71v_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc663_g71v_hp_automute(codec);
		break;
	case ALC880_FRONT_EVENT:
		alc663_g71v_front_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc662_eeepc_mic_automute(codec);
		break;
	}
}

static void alc663_g71v_inithook(struct hda_codec *codec)
{
	alc663_g71v_front_automute(codec);
	alc663_g71v_hp_automute(codec);
	alc662_eeepc_mic_automute(codec);
}

static void alc663_g50v_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc663_m51va_speaker_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc662_eeepc_mic_automute(codec);
		break;
	}
}

static void alc663_g50v_inithook(struct hda_codec *codec)
{
	alc663_m51va_speaker_automute(codec);
	alc662_eeepc_mic_automute(codec);
}

/* bind hp and internal speaker mute (with plug check) */
static int alc662_ecs_master_sw_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int change;

	change = snd_hda_codec_amp_update(codec, 0x1b, 0, HDA_OUTPUT, 0,
					  HDA_AMP_MUTE,
					  valp[0] ? 0 : HDA_AMP_MUTE);
	change |= snd_hda_codec_amp_update(codec, 0x1b, 1, HDA_OUTPUT, 0,
					   HDA_AMP_MUTE,
					   valp[1] ? 0 : HDA_AMP_MUTE);
	if (change)
		alc262_hippo1_automute(codec);
	return change;
}

static struct snd_kcontrol_new alc662_ecs_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = alc662_ecs_master_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x1b, 3, 0, HDA_OUTPUT),
	},

	HDA_CODEC_VOLUME("e-Mic/LineIn Boost", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("e-Mic/LineIn Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("e-Mic/LineIn Playback Switch", 0x0b, 0x0, HDA_INPUT),

	HDA_CODEC_VOLUME("i-Mic Boost", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("i-Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("i-Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc662_loopbacks	alc880_loopbacks
#endif


/* pcm configuration: identiacal with ALC880 */
#define alc662_pcm_analog_playback	alc880_pcm_analog_playback
#define alc662_pcm_analog_capture	alc880_pcm_analog_capture
#define alc662_pcm_digital_playback	alc880_pcm_digital_playback
#define alc662_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * configuration and preset
 */
static const char *alc662_models[ALC662_MODEL_LAST] = {
	[ALC662_3ST_2ch_DIG]	= "3stack-dig",
	[ALC662_3ST_6ch_DIG]	= "3stack-6ch-dig",
	[ALC662_3ST_6ch]	= "3stack-6ch",
	[ALC662_5ST_DIG]	= "6stack-dig",
	[ALC662_LENOVO_101E]	= "lenovo-101e",
	[ALC662_ASUS_EEEPC_P701] = "eeepc-p701",
	[ALC662_ASUS_EEEPC_EP20] = "eeepc-ep20",
	[ALC662_ECS] = "ecs",
	[ALC663_ASUS_M51VA] = "m51va",
	[ALC663_ASUS_G71V] = "g71v",
	[ALC663_ASUS_H13] = "h13",
	[ALC663_ASUS_G50V] = "g50v",
	[ALC663_ASUS_MODE1] = "asus-mode1",
	[ALC662_ASUS_MODE2] = "asus-mode2",
	[ALC663_ASUS_MODE3] = "asus-mode3",
	[ALC663_ASUS_MODE4] = "asus-mode4",
	[ALC663_ASUS_MODE5] = "asus-mode5",
	[ALC663_ASUS_MODE6] = "asus-mode6",
	[ALC662_AUTO]		= "auto",
};

static struct snd_pci_quirk alc662_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x1878, "ASUS M51VA", ALC663_ASUS_M51VA),
	SND_PCI_QUIRK(0x1043, 0x19a3, "ASUS M51VA", ALC663_ASUS_G50V),
	SND_PCI_QUIRK(0x1043, 0x8290, "ASUS P5GC-MX", ALC662_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1043, 0x82a1, "ASUS Eeepc", ALC662_ASUS_EEEPC_P701),
	SND_PCI_QUIRK(0x1043, 0x82d1, "ASUS Eeepc EP20", ALC662_ASUS_EEEPC_EP20),
	SND_PCI_QUIRK(0x1043, 0x1903, "ASUS F5GL", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1878, "ASUS M50Vr", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1000, "ASUS N50Vm", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19b3, "ASUS F7Z", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1953, "ASUS NB", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19a3, "ASUS NB", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x11d3, "ASUS NB", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1203, "ASUS NB", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19e3, "ASUS NB", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19c3, "ASUS F5Z/F6x", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1339, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1913, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1843, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1813, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x11f3, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1876, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1864, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1783, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1753, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x16c3, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1933, "ASUS F80Q", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1893, "ASUS M50Vm", ALC663_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x11c3, "ASUS M70V", ALC663_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x1963, "ASUS X71C", ALC663_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x1894, "ASUS X55", ALC663_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x1092, "ASUS NB", ALC663_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x19f3, "ASUS NB", ALC663_ASUS_MODE4),
	SND_PCI_QUIRK(0x1043, 0x1823, "ASUS NB", ALC663_ASUS_MODE5),
	SND_PCI_QUIRK(0x1043, 0x1833, "ASUS NB", ALC663_ASUS_MODE6),
	SND_PCI_QUIRK(0x1043, 0x1763, "ASUS NB", ALC663_ASUS_MODE6),
	SND_PCI_QUIRK(0x1043, 0x1765, "ASUS NB", ALC663_ASUS_MODE6),
	SND_PCI_QUIRK(0x17aa, 0x101e, "Lenovo", ALC662_LENOVO_101E),
	SND_PCI_QUIRK(0x1019, 0x9087, "ECS", ALC662_ECS),
	SND_PCI_QUIRK(0x105b, 0x0cd6, "Foxconn", ALC662_ECS),
	SND_PCI_QUIRK(0x1854, 0x2000, "ASUS H13-2000", ALC663_ASUS_H13),
	SND_PCI_QUIRK(0x1854, 0x2001, "ASUS H13-2001", ALC663_ASUS_H13),
	SND_PCI_QUIRK(0x1854, 0x2002, "ASUS H13-2002", ALC663_ASUS_H13),
	{}
};

static struct alc_config_preset alc662_presets[] = {
	[ALC662_3ST_2ch_DIG] = {
		.mixers = { alc662_3ST_2ch_mixer, alc662_capture_mixer },
		.init_verbs = { alc662_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.dig_in_nid = ALC662_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_capture_source,
	},
	[ALC662_3ST_6ch_DIG] = {
		.mixers = { alc662_3ST_6ch_mixer, alc662_chmode_mixer,
			    alc662_capture_mixer },
		.init_verbs = { alc662_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.dig_in_nid = ALC662_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_6ch_modes),
		.channel_mode = alc662_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc662_capture_source,
	},
	[ALC662_3ST_6ch] = {
		.mixers = { alc662_3ST_6ch_mixer, alc662_chmode_mixer,
			    alc662_capture_mixer },
		.init_verbs = { alc662_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_6ch_modes),
		.channel_mode = alc662_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc662_capture_source,
	},
	[ALC662_5ST_DIG] = {
		.mixers = { alc662_base_mixer, alc662_chmode_mixer,
			    alc662_capture_mixer },
		.init_verbs = { alc662_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.dig_in_nid = ALC662_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_5stack_modes),
		.channel_mode = alc662_5stack_modes,
		.input_mux = &alc662_capture_source,
	},
	[ALC662_LENOVO_101E] = {
		.mixers = { alc662_lenovo_101e_mixer, alc662_capture_mixer },
		.init_verbs = { alc662_init_verbs, alc662_sue_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_lenovo_101e_capture_source,
		.unsol_event = alc662_lenovo_101e_unsol_event,
		.init_hook = alc662_lenovo_101e_all_automute,
	},
	[ALC662_ASUS_EEEPC_P701] = {
		.mixers = { alc662_eeepc_p701_mixer, alc662_capture_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eeepc_sue_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_eeepc_capture_source,
		.unsol_event = alc662_eeepc_unsol_event,
		.init_hook = alc662_eeepc_inithook,
	},
	[ALC662_ASUS_EEEPC_EP20] = {
		.mixers = { alc662_eeepc_ep20_mixer, alc662_capture_mixer,
			    alc662_chmode_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eeepc_ep20_sue_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_6ch_modes),
		.channel_mode = alc662_3ST_6ch_modes,
		.input_mux = &alc662_lenovo_101e_capture_source,
		.unsol_event = alc662_eeepc_ep20_unsol_event,
		.init_hook = alc662_eeepc_ep20_inithook,
	},
	[ALC662_ECS] = {
		.mixers = { alc662_ecs_mixer, alc662_capture_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_ecs_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_eeepc_capture_source,
		.unsol_event = alc662_eeepc_unsol_event,
		.init_hook = alc662_eeepc_inithook,
	},
	[ALC663_ASUS_M51VA] = {
		.mixers = { alc663_m51va_mixer, alc662_capture_mixer},
		.init_verbs = { alc662_init_verbs, alc663_m51va_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc663_m51va_capture_source,
		.unsol_event = alc663_m51va_unsol_event,
		.init_hook = alc663_m51va_inithook,
	},
	[ALC663_ASUS_G71V] = {
		.mixers = { alc663_g71v_mixer, alc662_capture_mixer},
		.init_verbs = { alc662_init_verbs, alc663_g71v_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_eeepc_capture_source,
		.unsol_event = alc663_g71v_unsol_event,
		.init_hook = alc663_g71v_inithook,
	},
	[ALC663_ASUS_H13] = {
		.mixers = { alc663_m51va_mixer, alc662_capture_mixer},
		.init_verbs = { alc662_init_verbs, alc663_m51va_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc663_m51va_capture_source,
		.unsol_event = alc663_m51va_unsol_event,
		.init_hook = alc663_m51va_inithook,
	},
	[ALC663_ASUS_G50V] = {
		.mixers = { alc663_g50v_mixer, alc662_capture_mixer},
		.init_verbs = { alc662_init_verbs, alc663_g50v_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_6ch_modes),
		.channel_mode = alc662_3ST_6ch_modes,
		.input_mux = &alc663_capture_source,
		.unsol_event = alc663_g50v_unsol_event,
		.init_hook = alc663_g50v_inithook,
	},
	[ALC663_ASUS_MODE1] = {
		.mixers = { alc663_m51va_mixer, alc662_auto_capture_mixer },
		.init_verbs = { alc662_init_verbs,
				alc663_21jd_amic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.hp_nid = 0x03,
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_eeepc_capture_source,
		.unsol_event = alc663_mode1_unsol_event,
		.init_hook = alc663_mode1_inithook,
	},
	[ALC662_ASUS_MODE2] = {
		.mixers = { alc662_1bjd_mixer, alc662_auto_capture_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_1bjd_amic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_eeepc_capture_source,
		.unsol_event = alc662_mode2_unsol_event,
		.init_hook = alc662_mode2_inithook,
	},
	[ALC663_ASUS_MODE3] = {
		.mixers = { alc663_two_hp_m1_mixer, alc662_auto_capture_mixer },
		.init_verbs = { alc662_init_verbs,
				alc663_two_hp_amic_m1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.hp_nid = 0x03,
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_eeepc_capture_source,
		.unsol_event = alc663_mode3_unsol_event,
		.init_hook = alc663_mode3_inithook,
	},
	[ALC663_ASUS_MODE4] = {
		.mixers = { alc663_asus_21jd_clfe_mixer,
				alc662_auto_capture_mixer},
		.init_verbs = { alc662_init_verbs,
				alc663_21jd_amic_init_verbs},
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.hp_nid = 0x03,
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_eeepc_capture_source,
		.unsol_event = alc663_mode4_unsol_event,
		.init_hook = alc663_mode4_inithook,
	},
	[ALC663_ASUS_MODE5] = {
		.mixers = { alc663_asus_15jd_clfe_mixer,
				alc662_auto_capture_mixer },
		.init_verbs = { alc662_init_verbs,
				alc663_15jd_amic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.hp_nid = 0x03,
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_eeepc_capture_source,
		.unsol_event = alc663_mode5_unsol_event,
		.init_hook = alc663_mode5_inithook,
	},
	[ALC663_ASUS_MODE6] = {
		.mixers = { alc663_two_hp_m2_mixer, alc662_auto_capture_mixer },
		.init_verbs = { alc662_init_verbs,
				alc663_two_hp_amic_m2_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.hp_nid = 0x03,
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_eeepc_capture_source,
		.unsol_event = alc663_mode6_unsol_event,
		.init_hook = alc663_mode6_inithook,
	},
};


/*
 * BIOS auto configuration
 */

/* add playback controls from the parsed DAC table */
static int alc662_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	char name[32];
	static const char *chname[4] = {
		"Front", "Surround", NULL /*CLFE*/, "Side"
	};
	hda_nid_t nid;
	int i, err;

	for (i = 0; i < cfg->line_outs; i++) {
		if (!spec->multiout.dac_nids[i])
			continue;
		nid = alc880_idx_to_dac(i);
		if (i == 2) {
			/* Center/LFE */
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "Center Playback Volume",
					  HDA_COMPOSE_AMP_VAL(nid, 1, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  "LFE Playback Volume",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "Center Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 1, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
			err = add_control(spec, ALC_CTL_BIND_MUTE,
					  "LFE Playback Switch",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
		} else {
			sprintf(name, "%s Playback Volume", chname[i]);
			err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
					  HDA_COMPOSE_AMP_VAL(nid, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			sprintf(name, "%s Playback Switch", chname[i]);
			err = add_control(spec, ALC_CTL_BIND_MUTE, name,
					  HDA_COMPOSE_AMP_VAL(nid, 3, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

/* add playback controls for speaker and HP outputs */
static int alc662_auto_create_extra_out(struct alc_spec *spec, hda_nid_t pin,
					const char *pfx)
{
	hda_nid_t nid;
	int err;
	char name[32];

	if (!pin)
		return 0;

	if (pin == 0x17) {
		/* ALC663 has a mono output pin on 0x17 */
		sprintf(name, "%s Playback Switch", pfx);
		err = add_control(spec, ALC_CTL_WIDGET_MUTE, name,
				  HDA_COMPOSE_AMP_VAL(pin, 2, 0, HDA_OUTPUT));
		return err;
	}

	if (alc880_is_fixed_pin(pin)) {
		nid = alc880_idx_to_dac(alc880_fixed_pin_idx(pin));
                /* printk("DAC nid=%x\n",nid); */
		/* specify the DAC as the extra output */
		if (!spec->multiout.hp_nid)
			spec->multiout.hp_nid = nid;
		else
			spec->multiout.extra_out_nid[0] = nid;
		/* control HP volume/switch on the output mixer amp */
		nid = alc880_idx_to_dac(alc880_fixed_pin_idx(pin));
		sprintf(name, "%s Playback Volume", pfx);
		err = add_control(spec, ALC_CTL_WIDGET_VOL, name,
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		sprintf(name, "%s Playback Switch", pfx);
		err = add_control(spec, ALC_CTL_BIND_MUTE, name,
				  HDA_COMPOSE_AMP_VAL(nid, 3, 2, HDA_INPUT));
		if (err < 0)
			return err;
	} else if (alc880_is_multi_pin(pin)) {
		/* set manual connection */
		/* we have only a switch on HP-out PIN */
		sprintf(name, "%s Playback Switch", pfx);
		err = add_control(spec, ALC_CTL_WIDGET_MUTE, name,
				  HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
	}
	return 0;
}

/* create playback/capture controls for input pins */
static int alc662_auto_create_analog_input_ctls(struct alc_spec *spec,
						const struct auto_pin_cfg *cfg)
{
	struct hda_input_mux *imux = &spec->private_imux;
	int i, err, idx;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		if (alc880_is_input_pin(cfg->input_pins[i])) {
			idx = alc880_input_pin_idx(cfg->input_pins[i]);
			err = new_analog_input(spec, cfg->input_pins[i],
					       auto_pin_cfg_labels[i],
					       idx, 0x0b);
			if (err < 0)
				return err;
			imux->items[imux->num_items].label =
				auto_pin_cfg_labels[i];
			imux->items[imux->num_items].index =
				alc880_input_pin_idx(cfg->input_pins[i]);
			imux->num_items++;
		}
	}
	return 0;
}

static void alc662_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid, int pin_type,
					      int dac_idx)
{
	alc_set_pin_output(codec, nid, pin_type);
	/* need the manual connection? */
	if (alc880_is_multi_pin(nid)) {
		struct alc_spec *spec = codec->spec;
		int idx = alc880_multi_pin_idx(nid);
		snd_hda_codec_write(codec, alc880_idx_to_selector(idx), 0,
				    AC_VERB_SET_CONNECT_SEL,
				    alc880_dac_to_idx(spec->multiout.dac_nids[dac_idx]));
	}
}

static void alc662_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	alc_subsystem_id(codec, 0x15, 0x1b, 0x14);
	for (i = 0; i <= HDA_SIDE; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		if (nid)
			alc662_auto_set_output_and_unmute(codec, nid, pin_type,
							  i);
	}
}

static void alc662_auto_init_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pins[0];
	if (pin) /* connect to front */
		/* use dac 0 */
		alc662_auto_set_output_and_unmute(codec, pin, PIN_HP, 0);
	pin = spec->autocfg.speaker_pins[0];
	if (pin)
		alc662_auto_set_output_and_unmute(codec, pin, PIN_OUT, 0);
}

#define alc662_is_input_pin(nid)	alc880_is_input_pin(nid)
#define ALC662_PIN_CD_NID		ALC880_PIN_CD_NID

static void alc662_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < AUTO_PIN_LAST; i++) {
		hda_nid_t nid = spec->autocfg.input_pins[i];
		if (alc662_is_input_pin(nid)) {
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    (i <= AUTO_PIN_FRONT_MIC ?
					     PIN_VREF80 : PIN_IN));
			if (nid != ALC662_PIN_CD_NID)
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_AMP_GAIN_MUTE,
						    AMP_OUT_MUTE);
		}
	}
}

#define alc662_auto_init_input_src	alc882_auto_init_input_src

static int alc662_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static hda_nid_t alc662_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc662_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */

	err = alc880_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc662_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc662_auto_create_extra_out(spec,
					   spec->autocfg.speaker_pins[0],
					   "Speaker");
	if (err < 0)
		return err;
	err = alc662_auto_create_extra_out(spec, spec->autocfg.hp_pins[0],
					   "Headphone");
	if (err < 0)
		return err;
	err = alc662_auto_create_analog_input_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	if (spec->autocfg.dig_out_pin)
		spec->multiout.dig_out_nid = ALC880_DIGOUT_NID;

	if (spec->kctl_alloc)
		spec->mixers[spec->num_mixers++] = spec->kctl_alloc;

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux;

	spec->init_verbs[spec->num_init_verbs++] = alc662_auto_init_verbs;
	if (codec->vendor_id == 0x10ec0663)
		spec->init_verbs[spec->num_init_verbs++] =
			alc663_auto_init_verbs;

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	spec->mixers[spec->num_mixers] = alc662_capture_mixer;
	spec->num_mixers++;
	return 1;
}

/* additional initialization for auto-configuration model */
static void alc662_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc662_auto_init_multi_out(codec);
	alc662_auto_init_hp_out(codec);
	alc662_auto_init_analog_input(codec);
	alc662_auto_init_input_src(codec);
	if (spec->unsol_event)
		alc_sku_automute(codec);
}

static int patch_alc662(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	codec->spec = spec;

	alc_fix_pll_init(codec, 0x20, 0x04, 15);

	board_config = snd_hda_check_board_config(codec, ALC662_MODEL_LAST,
						  alc662_models,
			  	                  alc662_cfg_tbl);
	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: Unknown model for ALC662, "
		       "trying auto-probe from BIOS...\n");
		board_config = ALC662_AUTO;
	}

	if (board_config == ALC662_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc662_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC662_3ST_2ch_DIG;
		}
	}

	if (board_config != ALC662_AUTO)
		setup_preset(spec, &alc662_presets[board_config]);

	if (codec->vendor_id == 0x10ec0663) {
		spec->stream_name_analog = "ALC663 Analog";
		spec->stream_name_digital = "ALC663 Digital";
	} else {
		spec->stream_name_analog = "ALC662 Analog";
		spec->stream_name_digital = "ALC662 Digital";
	}

	spec->stream_analog_playback = &alc662_pcm_analog_playback;
	spec->stream_analog_capture = &alc662_pcm_analog_capture;

	spec->stream_digital_playback = &alc662_pcm_digital_playback;
	spec->stream_digital_capture = &alc662_pcm_digital_capture;

	spec->adc_nids = alc662_adc_nids;
	spec->num_adc_nids = ARRAY_SIZE(alc662_adc_nids);
	spec->capsrc_nids = alc662_capsrc_nids;

	spec->vmaster_nid = 0x02;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC662_AUTO)
		spec->init_hook = alc662_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc662_loopbacks;
#endif

	return 0;
}

/*
 * patch entries
 */
struct hda_codec_preset snd_hda_preset_realtek[] = {
	{ .id = 0x10ec0260, .name = "ALC260", .patch = patch_alc260 },
	{ .id = 0x10ec0262, .name = "ALC262", .patch = patch_alc262 },
	{ .id = 0x10ec0267, .name = "ALC267", .patch = patch_alc268 },
	{ .id = 0x10ec0268, .name = "ALC268", .patch = patch_alc268 },
	{ .id = 0x10ec0269, .name = "ALC269", .patch = patch_alc269 },
	{ .id = 0x10ec0861, .rev = 0x100340, .name = "ALC660",
	  .patch = patch_alc861 },
	{ .id = 0x10ec0660, .name = "ALC660-VD", .patch = patch_alc861vd },
	{ .id = 0x10ec0861, .name = "ALC861", .patch = patch_alc861 },
	{ .id = 0x10ec0862, .name = "ALC861-VD", .patch = patch_alc861vd },
	{ .id = 0x10ec0662, .rev = 0x100002, .name = "ALC662 rev2",
	  .patch = patch_alc883 },
	{ .id = 0x10ec0662, .rev = 0x100101, .name = "ALC662 rev1",
	  .patch = patch_alc662 },
	{ .id = 0x10ec0663, .name = "ALC663", .patch = patch_alc662 },
	{ .id = 0x10ec0880, .name = "ALC880", .patch = patch_alc880 },
	{ .id = 0x10ec0882, .name = "ALC882", .patch = patch_alc882 },
	{ .id = 0x10ec0883, .name = "ALC883", .patch = patch_alc883 },
	{ .id = 0x10ec0885, .rev = 0x100103, .name = "ALC889A",
	  .patch = patch_alc882 }, /* should be patch_alc883() in future */
	{ .id = 0x10ec0885, .name = "ALC885", .patch = patch_alc882 },
	{ .id = 0x10ec0888, .name = "ALC888", .patch = patch_alc883 },
	{ .id = 0x10ec0889, .name = "ALC889", .patch = patch_alc883 },
	{} /* terminator */
};
