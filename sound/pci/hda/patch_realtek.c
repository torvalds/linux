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
#include <sound/jack.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_beep.h"

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
	ALC260_FAVORIT100,
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
	ALC262_TOSHIBA_RX1,
	ALC262_TYAN,
	ALC262_AUTO,
	ALC262_MODEL_LAST /* last tag */
};

/* ALC268 models */
enum {
	ALC267_QUANTA_IL1,
	ALC268_3ST,
	ALC268_TOSHIBA,
	ALC268_ACER,
	ALC268_ACER_DMIC,
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
	ALC269_AMIC,
	ALC269_DMIC,
	ALC269VB_AMIC,
	ALC269VB_DMIC,
	ALC269_FUJITSU,
	ALC269_LIFEBOOK,
	ALC271_ACER,
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
	ALC660VD_ASUS_V1S,
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
	ALC663_ASUS_MODE7,
	ALC663_ASUS_MODE8,
	ALC272_DELL,
	ALC272_DELL_ZM1,
	ALC272_SAMSUNG_NC10,
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
	ALC885_MBA21,
	ALC885_MBP3,
	ALC885_MB5,
	ALC885_MACMINI3,
	ALC885_IMAC24,
	ALC885_IMAC91,
	ALC883_3ST_2ch_DIG,
	ALC883_3ST_6ch_DIG,
	ALC883_3ST_6ch,
	ALC883_6ST_DIG,
	ALC883_TARGA_DIG,
	ALC883_TARGA_2ch_DIG,
	ALC883_TARGA_8ch_DIG,
	ALC883_ACER,
	ALC883_ACER_ASPIRE,
	ALC888_ACER_ASPIRE_4930G,
	ALC888_ACER_ASPIRE_6530G,
	ALC888_ACER_ASPIRE_8930G,
	ALC888_ACER_ASPIRE_7730G,
	ALC883_MEDION,
	ALC883_MEDION_WIM2160,
	ALC883_LAPTOP_EAPD,
	ALC883_LENOVO_101E_2ch,
	ALC883_LENOVO_NB0763,
	ALC888_LENOVO_MS7195_DIG,
	ALC888_LENOVO_SKY,
	ALC883_HAIER_W66,
	ALC888_3ST_HP,
	ALC888_6ST_DELL,
	ALC883_MITAC,
	ALC883_CLEVO_M540R,
	ALC883_CLEVO_M720,
	ALC883_FUJITSU_PI2515,
	ALC888_FUJITSU_XA3530,
	ALC883_3ST_6ch_INTEL,
	ALC889A_INTEL,
	ALC889_INTEL,
	ALC888_ASUS_M90V,
	ALC888_ASUS_EEE1601,
	ALC889A_MB31,
	ALC1200_ASUS_P5Q,
	ALC883_SONY_VAIO_TT,
	ALC882_AUTO,
	ALC882_MODEL_LAST,
};

/* ALC680 models */
enum {
	ALC680_BASE,
	ALC680_AUTO,
	ALC680_MODEL_LAST,
};

/* for GPIO Poll */
#define GPIO_MASK	0x03

/* extra amp-initialization sequence types */
enum {
	ALC_INIT_NONE,
	ALC_INIT_DEFAULT,
	ALC_INIT_GPIO1,
	ALC_INIT_GPIO2,
	ALC_INIT_GPIO3,
};

struct alc_mic_route {
	hda_nid_t pin;
	unsigned char mux_idx;
	unsigned char amix_idx;
};

#define MUX_IDX_UNDEF	((unsigned char)-1)

struct alc_customize_define {
	unsigned int  sku_cfg;
	unsigned char port_connectivity;
	unsigned char check_sum;
	unsigned char customization;
	unsigned char external_amp;
	unsigned int  enable_pcbeep:1;
	unsigned int  platform_type:1;
	unsigned int  swap:1;
	unsigned int  override:1;
	unsigned int  fixup:1; /* Means that this sku is set by driver, not read from hw */
};

struct alc_fixup;

struct alc_multi_io {
	hda_nid_t pin;		/* multi-io widget pin NID */
	hda_nid_t dac;		/* DAC to be connected */
	unsigned int ctl_in;	/* cached input-pin control value */
};

enum {
	ALC_AUTOMUTE_PIN,	/* change the pin control */
	ALC_AUTOMUTE_AMP,	/* mute/unmute the pin AMP */
	ALC_AUTOMUTE_MIXER,	/* mute/unmute mixer widget AMP */
};

struct alc_spec {
	/* codec parameterization */
	const struct snd_kcontrol_new *mixers[5];	/* mixer arrays */
	unsigned int num_mixers;
	const struct snd_kcontrol_new *cap_mixer;	/* capture mixer */
	unsigned int beep_amp;	/* beep amp value, set via set_beep_amp() */

	const struct hda_verb *init_verbs[10];	/* initialization verbs
						 * don't forget NULL
						 * termination!
						 */
	unsigned int num_init_verbs;

	char stream_name_analog[32];	/* analog PCM stream */
	const struct hda_pcm_stream *stream_analog_playback;
	const struct hda_pcm_stream *stream_analog_capture;
	const struct hda_pcm_stream *stream_analog_alt_playback;
	const struct hda_pcm_stream *stream_analog_alt_capture;

	char stream_name_digital[32];	/* digital PCM stream */
	const struct hda_pcm_stream *stream_digital_playback;
	const struct hda_pcm_stream *stream_digital_capture;

	/* playback */
	struct hda_multi_out multiout;	/* playback set-up
					 * max_channels, dacs must be set
					 * dig_out_nid and hp_nid are optional
					 */
	hda_nid_t alt_dac_nid;
	hda_nid_t slave_dig_outs[3];	/* optional - for auto-parsing */
	int dig_out_type;

	/* capture */
	unsigned int num_adc_nids;
	const hda_nid_t *adc_nids;
	const hda_nid_t *capsrc_nids;
	hda_nid_t dig_in_nid;		/* digital-in NID; optional */

	/* capture setup for dynamic dual-adc switch */
	unsigned int cur_adc_idx;
	hda_nid_t cur_adc;
	unsigned int cur_adc_stream_tag;
	unsigned int cur_adc_format;

	/* capture source */
	unsigned int num_mux_defs;
	const struct hda_input_mux *input_mux;
	unsigned int cur_mux[3];
	struct alc_mic_route ext_mic;
	struct alc_mic_route dock_mic;
	struct alc_mic_route int_mic;

	/* channel model */
	const struct hda_channel_mode *channel_mode;
	int num_channel_mode;
	int need_dac_fix;
	int const_channel_count;
	int ext_channel_count;

	/* PCM information */
	struct hda_pcm pcm_rec[3];	/* used in alc_build_pcms() */

	/* dynamic controls, init_verbs and input_mux */
	struct auto_pin_cfg autocfg;
	struct alc_customize_define cdefine;
	struct snd_array kctls;
	struct hda_input_mux private_imux[3];
	hda_nid_t private_dac_nids[AUTO_CFG_MAX_OUTS];
	hda_nid_t private_adc_nids[AUTO_CFG_MAX_OUTS];
	hda_nid_t private_capsrc_nids[AUTO_CFG_MAX_OUTS];

	/* hooks */
	void (*init_hook)(struct hda_codec *codec);
	void (*unsol_event)(struct hda_codec *codec, unsigned int res);
#ifdef CONFIG_SND_HDA_POWER_SAVE
	void (*power_hook)(struct hda_codec *codec);
#endif
	void (*shutup)(struct hda_codec *codec);

	/* for pin sensing */
	unsigned int jack_present: 1;
	unsigned int line_jack_present:1;
	unsigned int master_mute:1;
	unsigned int auto_mic:1;
	unsigned int automute:1;	/* HP automute enabled */
	unsigned int detect_line:1;	/* Line-out detection enabled */
	unsigned int automute_lines:1;	/* automute line-out as well; NOP when automute_hp_lo isn't set */
	unsigned int automute_hp_lo:1;	/* both HP and LO available */

	/* other flags */
	unsigned int no_analog :1; /* digital I/O only */
	unsigned int dual_adc_switch:1; /* switch ADCs (for ALC275) */
	unsigned int single_input_src:1;

	/* auto-mute control */
	int automute_mode;
	hda_nid_t automute_mixer_nid[AUTO_CFG_MAX_OUTS];

	int init_amp;
	int codec_variant;	/* flag for other variants */

	/* for virtual master */
	hda_nid_t vmaster_nid;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	struct hda_loopback_check loopback;
#endif

	/* for PLL fix */
	hda_nid_t pll_nid;
	unsigned int pll_coef_idx, pll_coef_bit;

	/* fix-up list */
	int fixup_id;
	const struct alc_fixup *fixup_list;
	const char *fixup_name;

	/* multi-io */
	int multi_ios;
	struct alc_multi_io multi_io[4];
};

/*
 * configuration template - to be copied to the spec instance
 */
struct alc_config_preset {
	const struct snd_kcontrol_new *mixers[5]; /* should be identical size
					     * with spec
					     */
	const struct snd_kcontrol_new *cap_mixer; /* capture mixer */
	const struct hda_verb *init_verbs[5];
	unsigned int num_dacs;
	const hda_nid_t *dac_nids;
	hda_nid_t dig_out_nid;		/* optional */
	hda_nid_t hp_nid;		/* optional */
	const hda_nid_t *slave_dig_outs;
	unsigned int num_adc_nids;
	const hda_nid_t *adc_nids;
	const hda_nid_t *capsrc_nids;
	hda_nid_t dig_in_nid;
	unsigned int num_channel_mode;
	const struct hda_channel_mode *channel_mode;
	int need_dac_fix;
	int const_channel_count;
	unsigned int num_mux_defs;
	const struct hda_input_mux *input_mux;
	void (*unsol_event)(struct hda_codec *, unsigned int);
	void (*setup)(struct hda_codec *);
	void (*init_hook)(struct hda_codec *);
#ifdef CONFIG_SND_HDA_POWER_SAVE
	const struct hda_amp_list *loopbacks;
	void (*power_hook)(struct hda_codec *codec);
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
	if (!spec->input_mux[mux_idx].num_items && mux_idx > 0)
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
	const struct hda_input_mux *imux;
	unsigned int adc_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	unsigned int mux_idx;
	hda_nid_t nid = spec->capsrc_nids ?
		spec->capsrc_nids[adc_idx] : spec->adc_nids[adc_idx];
	unsigned int type;

	mux_idx = adc_idx >= spec->num_mux_defs ? 0 : adc_idx;
	imux = &spec->input_mux[mux_idx];
	if (!imux->num_items && mux_idx > 0)
		imux = &spec->input_mux[0];
	if (!imux->num_items)
		return 0;

	type = get_wcaps_type(get_wcaps(codec, nid));
	if (type == AC_WID_AUD_MIX) {
		/* Matrix-mixer style (e.g. ALC882) */
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
	} else {
		/* MUX style (e.g. ALC880) */
		return snd_hda_input_mux_put(codec, imux, ucontrol, nid,
					     &spec->cur_mux[adc_idx]);
	}
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
				   spec->ext_channel_count);
}

static int alc_ch_mode_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int err = snd_hda_ch_mode_put(codec, ucontrol, spec->channel_mode,
				      spec->num_channel_mode,
				      &spec->ext_channel_count);
	if (err >= 0 && !spec->const_channel_count) {
		spec->multiout.max_channels = spec->ext_channel_count;
		if (spec->need_dac_fix)
			spec->multiout.num_dacs = spec->multiout.max_channels / 2;
	}
	return err;
}

/*
 * Control the mode of pin widget settings via the mixer.  "pc" is used
 * instead of "%" to avoid consequences of accidentally treating the % as
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
static const char * const alc_pin_mode_names[] = {
	"Mic 50pc bias", "Mic 80pc bias",
	"Line in", "Line out", "Headphone out",
};
static const unsigned char alc_pin_mode_values[] = {
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
static const signed char alc_pin_mode_dir_info[5][2] = {
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
	while (i <= alc_pin_mode_max(dir) && alc_pin_mode_values[i] != pinctl)
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
	  .subdevice = HDA_SUBDEV_NID_FLAG | nid, \
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
	  .subdevice = HDA_SUBDEV_NID_FLAG | nid, \
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
	  .subdevice = HDA_SUBDEV_NID_FLAG | nid, \
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
	  .subdevice = HDA_SUBDEV_NID_FLAG | nid, \
	  .info = alc_eapd_ctrl_info, \
	  .get = alc_eapd_ctrl_get, \
	  .put = alc_eapd_ctrl_put, \
	  .private_value = nid | (mask<<16) }
#endif   /* CONFIG_SND_DEBUG */

/*
 * set up the input pin config (depending on the given auto-pin type)
 */
static void alc_set_input_pin(struct hda_codec *codec, hda_nid_t nid,
			      int auto_pin_type)
{
	unsigned int val = PIN_IN;

	if (auto_pin_type == AUTO_PIN_MIC) {
		unsigned int pincap;
		unsigned int oldval;
		oldval = snd_hda_codec_read(codec, nid, 0,
					    AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		pincap = snd_hda_query_pin_caps(codec, nid);
		pincap = (pincap & AC_PINCAP_VREF) >> AC_PINCAP_VREF_SHIFT;
		/* if the default pin setup is vref50, we give it priority */
		if ((pincap & AC_PINCAP_VREF_80) && oldval != PIN_VREF50)
			val = PIN_VREF80;
		else if (pincap & AC_PINCAP_VREF_50)
			val = PIN_VREF50;
		else if (pincap & AC_PINCAP_VREF_100)
			val = PIN_VREF100;
		else if (pincap & AC_PINCAP_VREF_GRD)
			val = PIN_VREFGRD;
	}
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, val);
}

static void alc_fixup_autocfg_pin_nums(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;

	if (!cfg->line_outs) {
		while (cfg->line_outs < AUTO_CFG_MAX_OUTS &&
		       cfg->line_out_pins[cfg->line_outs])
			cfg->line_outs++;
	}
	if (!cfg->speaker_outs) {
		while (cfg->speaker_outs < AUTO_CFG_MAX_OUTS &&
		       cfg->speaker_pins[cfg->speaker_outs])
			cfg->speaker_outs++;
	}
	if (!cfg->hp_outs) {
		while (cfg->hp_outs < AUTO_CFG_MAX_OUTS &&
		       cfg->hp_pins[cfg->hp_outs])
			cfg->hp_outs++;
	}
}

/*
 */
static void add_mixer(struct alc_spec *spec, const struct snd_kcontrol_new *mix)
{
	if (snd_BUG_ON(spec->num_mixers >= ARRAY_SIZE(spec->mixers)))
		return;
	spec->mixers[spec->num_mixers++] = mix;
}

static void add_verb(struct alc_spec *spec, const struct hda_verb *verb)
{
	if (snd_BUG_ON(spec->num_init_verbs >= ARRAY_SIZE(spec->init_verbs)))
		return;
	spec->init_verbs[spec->num_init_verbs++] = verb;
}

/*
 * set up from the preset table
 */
static void setup_preset(struct hda_codec *codec,
			 const struct alc_config_preset *preset)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < ARRAY_SIZE(preset->mixers) && preset->mixers[i]; i++)
		add_mixer(spec, preset->mixers[i]);
	spec->cap_mixer = preset->cap_mixer;
	for (i = 0; i < ARRAY_SIZE(preset->init_verbs) && preset->init_verbs[i];
	     i++)
		add_verb(spec, preset->init_verbs[i]);

	spec->channel_mode = preset->channel_mode;
	spec->num_channel_mode = preset->num_channel_mode;
	spec->need_dac_fix = preset->need_dac_fix;
	spec->const_channel_count = preset->const_channel_count;

	if (preset->const_channel_count)
		spec->multiout.max_channels = preset->const_channel_count;
	else
		spec->multiout.max_channels = spec->channel_mode[0].channels;
	spec->ext_channel_count = spec->channel_mode[0].channels;

	spec->multiout.num_dacs = preset->num_dacs;
	spec->multiout.dac_nids = preset->dac_nids;
	spec->multiout.dig_out_nid = preset->dig_out_nid;
	spec->multiout.slave_dig_outs = preset->slave_dig_outs;
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
	spec->power_hook = preset->power_hook;
	spec->loopback.amplist = preset->loopbacks;
#endif

	if (preset->setup)
		preset->setup(codec);

	alc_fixup_autocfg_pin_nums(codec);
}

/* Enable GPIO mask and set output */
static const struct hda_verb alc_gpio1_init_verbs[] = {
	{0x01, AC_VERB_SET_GPIO_MASK, 0x01},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x01},
	{ }
};

static const struct hda_verb alc_gpio2_init_verbs[] = {
	{0x01, AC_VERB_SET_GPIO_MASK, 0x02},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x02},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x02},
	{ }
};

static const struct hda_verb alc_gpio3_init_verbs[] = {
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

static int alc_init_jacks(struct hda_codec *codec)
{
#ifdef CONFIG_SND_HDA_INPUT_JACK
	struct alc_spec *spec = codec->spec;
	int err;
	unsigned int hp_nid = spec->autocfg.hp_pins[0];
	unsigned int mic_nid = spec->ext_mic.pin;
	unsigned int dock_nid = spec->dock_mic.pin;

	if (hp_nid) {
		err = snd_hda_input_jack_add(codec, hp_nid,
					     SND_JACK_HEADPHONE, NULL);
		if (err < 0)
			return err;
		snd_hda_input_jack_report(codec, hp_nid);
	}

	if (mic_nid) {
		err = snd_hda_input_jack_add(codec, mic_nid,
					     SND_JACK_MICROPHONE, NULL);
		if (err < 0)
			return err;
		snd_hda_input_jack_report(codec, mic_nid);
	}
	if (dock_nid) {
		err = snd_hda_input_jack_add(codec, dock_nid,
					     SND_JACK_MICROPHONE, NULL);
		if (err < 0)
			return err;
		snd_hda_input_jack_report(codec, dock_nid);
	}
#endif /* CONFIG_SND_HDA_INPUT_JACK */
	return 0;
}

static int detect_jacks(struct hda_codec *codec, int num_pins, hda_nid_t *pins)
{
	int i, present = 0;

	for (i = 0; i < num_pins; i++) {
		hda_nid_t nid = pins[i];
		if (!nid)
			break;
		snd_hda_input_jack_report(codec, nid);
		present |= snd_hda_jack_detect(codec, nid);
	}
	return present;
}

static void do_automute(struct hda_codec *codec, int num_pins, hda_nid_t *pins,
			bool mute, bool hp_out)
{
	struct alc_spec *spec = codec->spec;
	unsigned int mute_bits = mute ? HDA_AMP_MUTE : 0;
	unsigned int pin_bits = mute ? 0 : (hp_out ? PIN_HP : PIN_OUT);
	int i;

	for (i = 0; i < num_pins; i++) {
		hda_nid_t nid = pins[i];
		if (!nid)
			break;
		switch (spec->automute_mode) {
		case ALC_AUTOMUTE_PIN:
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    pin_bits);
			break;
		case ALC_AUTOMUTE_AMP:
			snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE, mute_bits);
			break;
		case ALC_AUTOMUTE_MIXER:
			nid = spec->automute_mixer_nid[i];
			if (!nid)
				break;
			snd_hda_codec_amp_stereo(codec, nid, HDA_INPUT, 0,
						 HDA_AMP_MUTE, mute_bits);
			snd_hda_codec_amp_stereo(codec, nid, HDA_INPUT, 1,
						 HDA_AMP_MUTE, mute_bits);
			break;
		}
	}
}

/* Toggle internal speakers muting */
static void update_speakers(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int on;

	/* Control HP pins/amps depending on master_mute state;
	 * in general, HP pins/amps control should be enabled in all cases,
	 * but currently set only for master_mute, just to be safe
	 */
	do_automute(codec, ARRAY_SIZE(spec->autocfg.hp_pins),
		    spec->autocfg.hp_pins, spec->master_mute, true);

	if (!spec->automute)
		on = 0;
	else
		on = spec->jack_present | spec->line_jack_present;
	on |= spec->master_mute;
	do_automute(codec, ARRAY_SIZE(spec->autocfg.speaker_pins),
		    spec->autocfg.speaker_pins, on, false);

	/* toggle line-out mutes if needed, too */
	/* if LO is a copy of either HP or Speaker, don't need to handle it */
	if (spec->autocfg.line_out_pins[0] == spec->autocfg.hp_pins[0] ||
	    spec->autocfg.line_out_pins[0] == spec->autocfg.speaker_pins[0])
		return;
	if (!spec->automute || (spec->automute_hp_lo && !spec->automute_lines))
		on = 0;
	else
		on = spec->jack_present;
	on |= spec->master_mute;
	do_automute(codec, ARRAY_SIZE(spec->autocfg.line_out_pins),
		    spec->autocfg.line_out_pins, on, false);
}

static void alc_hp_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec->automute)
		return;
	spec->jack_present =
		detect_jacks(codec, ARRAY_SIZE(spec->autocfg.hp_pins),
			     spec->autocfg.hp_pins);
	update_speakers(codec);
}

static void alc_line_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec->automute || !spec->detect_line)
		return;
	spec->line_jack_present =
		detect_jacks(codec, ARRAY_SIZE(spec->autocfg.line_out_pins),
			     spec->autocfg.line_out_pins);
	update_speakers(codec);
}

static int get_connection_index(struct hda_codec *codec, hda_nid_t mux,
				hda_nid_t nid)
{
	hda_nid_t conn[HDA_MAX_NUM_INPUTS];
	int i, nums;

	nums = snd_hda_get_connections(codec, mux, conn, ARRAY_SIZE(conn));
	for (i = 0; i < nums; i++)
		if (conn[i] == nid)
			return i;
	return -1;
}

/* switch the current ADC according to the jack state */
static void alc_dual_mic_adc_auto_switch(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int present;
	hda_nid_t new_adc;

	present = snd_hda_jack_detect(codec, spec->ext_mic.pin);
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

static void alc_mic_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct alc_mic_route *dead1, *dead2, *alive;
	unsigned int present, type;
	hda_nid_t cap_nid;

	if (!spec->auto_mic)
		return;
	if (!spec->int_mic.pin || !spec->ext_mic.pin)
		return;
	if (snd_BUG_ON(!spec->adc_nids))
		return;

	if (spec->dual_adc_switch) {
		alc_dual_mic_adc_auto_switch(codec);
		return;
	}

	cap_nid = spec->capsrc_nids ? spec->capsrc_nids[0] : spec->adc_nids[0];

	alive = &spec->int_mic;
	dead1 = &spec->ext_mic;
	dead2 = &spec->dock_mic;

	present = snd_hda_jack_detect(codec, spec->ext_mic.pin);
	if (present) {
		alive = &spec->ext_mic;
		dead1 = &spec->int_mic;
		dead2 = &spec->dock_mic;
	}
	if (!present && spec->dock_mic.pin > 0) {
		present = snd_hda_jack_detect(codec, spec->dock_mic.pin);
		if (present) {
			alive = &spec->dock_mic;
			dead1 = &spec->int_mic;
			dead2 = &spec->ext_mic;
		}
		snd_hda_input_jack_report(codec, spec->dock_mic.pin);
	}

	type = get_wcaps_type(get_wcaps(codec, cap_nid));
	if (type == AC_WID_AUD_MIX) {
		/* Matrix-mixer style (e.g. ALC882) */
		snd_hda_codec_amp_stereo(codec, cap_nid, HDA_INPUT,
					 alive->mux_idx,
					 HDA_AMP_MUTE, 0);
		if (dead1->pin > 0)
			snd_hda_codec_amp_stereo(codec, cap_nid, HDA_INPUT,
						 dead1->mux_idx,
						 HDA_AMP_MUTE, HDA_AMP_MUTE);
		if (dead2->pin > 0)
			snd_hda_codec_amp_stereo(codec, cap_nid, HDA_INPUT,
						 dead2->mux_idx,
						 HDA_AMP_MUTE, HDA_AMP_MUTE);
	} else {
		/* MUX style (e.g. ALC880) */
		snd_hda_codec_write_cache(codec, cap_nid, 0,
					  AC_VERB_SET_CONNECT_SEL,
					  alive->mux_idx);
	}
	snd_hda_input_jack_report(codec, spec->ext_mic.pin);

	/* FIXME: analog mixer */
}

/* unsolicited event for HP jack sensing */
static void alc_sku_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if (codec->vendor_id == 0x10ec0880)
		res >>= 28;
	else
		res >>= 26;
	switch (res) {
	case ALC880_HP_EVENT:
		alc_hp_automute(codec);
		break;
	case ALC880_FRONT_EVENT:
		alc_line_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc_mic_automute(codec);
		break;
	}
}

static void alc_inithook(struct hda_codec *codec)
{
	alc_hp_automute(codec);
	alc_line_automute(codec);
	alc_mic_automute(codec);
}

/* additional initialization for ALC888 variants */
static void alc888_coef_init(struct hda_codec *codec)
{
	unsigned int tmp;

	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX, 0);
	tmp = snd_hda_codec_read(codec, 0x20, 0, AC_VERB_GET_PROC_COEF, 0);
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX, 7);
	if ((tmp & 0xf0) == 0x20)
		/* alc888S-VC */
		snd_hda_codec_read(codec, 0x20, 0,
				   AC_VERB_SET_PROC_COEF, 0x830);
	 else
		 /* alc888-VB */
		 snd_hda_codec_read(codec, 0x20, 0,
				    AC_VERB_SET_PROC_COEF, 0x3030);
}

static void alc889_coef_init(struct hda_codec *codec)
{
	unsigned int tmp;

	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX, 7);
	tmp = snd_hda_codec_read(codec, 0x20, 0, AC_VERB_GET_PROC_COEF, 0);
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX, 7);
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_PROC_COEF, tmp|0x2010);
}

/* turn on/off EAPD control (only if available) */
static void set_eapd(struct hda_codec *codec, hda_nid_t nid, int on)
{
	if (get_wcaps_type(get_wcaps(codec, nid)) != AC_WID_PIN)
		return;
	if (snd_hda_query_pin_caps(codec, nid) & AC_PINCAP_EAPD)
		snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_EAPD_BTLENABLE,
				    on ? 2 : 0);
}

/* turn on/off EAPD controls of the codec */
static void alc_auto_setup_eapd(struct hda_codec *codec, bool on)
{
	/* We currently only handle front, HP */
	switch (codec->vendor_id) {
	case 0x10ec0260:
		set_eapd(codec, 0x0f, on);
		set_eapd(codec, 0x10, on);
		break;
	case 0x10ec0262:
	case 0x10ec0267:
	case 0x10ec0268:
	case 0x10ec0269:
	case 0x10ec0270:
	case 0x10ec0272:
	case 0x10ec0660:
	case 0x10ec0662:
	case 0x10ec0663:
	case 0x10ec0665:
	case 0x10ec0862:
	case 0x10ec0889:
	case 0x10ec0892:
		set_eapd(codec, 0x14, on);
		set_eapd(codec, 0x15, on);
		break;
	}
}

/* generic shutup callback;
 * just turning off EPAD and a little pause for avoiding pop-noise
 */
static void alc_eapd_shutup(struct hda_codec *codec)
{
	alc_auto_setup_eapd(codec, false);
	msleep(200);
}

static void alc_auto_init_amp(struct hda_codec *codec, int type)
{
	unsigned int tmp;

	switch (type) {
	case ALC_INIT_GPIO1:
		snd_hda_sequence_write(codec, alc_gpio1_init_verbs);
		break;
	case ALC_INIT_GPIO2:
		snd_hda_sequence_write(codec, alc_gpio2_init_verbs);
		break;
	case ALC_INIT_GPIO3:
		snd_hda_sequence_write(codec, alc_gpio3_init_verbs);
		break;
	case ALC_INIT_DEFAULT:
		alc_auto_setup_eapd(codec, true);
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
		case 0x10ec0887:
		/*case 0x10ec0889:*/ /* this causes an SPDIF problem */
			alc889_coef_init(codec);
			break;
		case 0x10ec0888:
			alc888_coef_init(codec);
			break;
#if 0 /* XXX: This may cause the silent output on speaker on some machines */
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
#endif /* XXX */
		}
		break;
	}
}

static int alc_automute_mode_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	static const char * const texts2[] = {
		"Disabled", "Enabled"
	};
	static const char * const texts3[] = {
		"Disabled", "Speaker Only", "Line-Out+Speaker"
	};
	const char * const *texts;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	if (spec->automute_hp_lo) {
		uinfo->value.enumerated.items = 3;
		texts = texts3;
	} else {
		uinfo->value.enumerated.items = 2;
		texts = texts2;
	}
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);
	return 0;
}

static int alc_automute_mode_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	unsigned int val;
	if (!spec->automute)
		val = 0;
	else if (!spec->automute_hp_lo || !spec->automute_lines)
		val = 1;
	else
		val = 2;
	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

static int alc_automute_mode_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;

	switch (ucontrol->value.enumerated.item[0]) {
	case 0:
		if (!spec->automute)
			return 0;
		spec->automute = 0;
		break;
	case 1:
		if (spec->automute &&
		    (!spec->automute_hp_lo || !spec->automute_lines))
			return 0;
		spec->automute = 1;
		spec->automute_lines = 0;
		break;
	case 2:
		if (!spec->automute_hp_lo)
			return -EINVAL;
		if (spec->automute && spec->automute_lines)
			return 0;
		spec->automute = 1;
		spec->automute_lines = 1;
		break;
	default:
		return -EINVAL;
	}
	update_speakers(codec);
	return 1;
}

static const struct snd_kcontrol_new alc_automute_mode_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Auto-Mute Mode",
	.info = alc_automute_mode_info,
	.get = alc_automute_mode_get,
	.put = alc_automute_mode_put,
};

static struct snd_kcontrol_new *alc_kcontrol_new(struct alc_spec *spec);

static int alc_add_automute_mode_enum(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct snd_kcontrol_new *knew;

	knew = alc_kcontrol_new(spec);
	if (!knew)
		return -ENOMEM;
	*knew = alc_automute_mode_enum;
	knew->name = kstrdup("Auto-Mute Mode", GFP_KERNEL);
	if (!knew->name)
		return -ENOMEM;
	return 0;
}

static void alc_init_auto_hp(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int present = 0;
	int i;

	if (cfg->hp_pins[0])
		present++;
	if (cfg->line_out_pins[0])
		present++;
	if (cfg->speaker_pins[0])
		present++;
	if (present < 2) /* need two different output types */
		return;
	if (present == 3)
		spec->automute_hp_lo = 1; /* both HP and LO automute */

	if (!cfg->speaker_pins[0] &&
	    cfg->line_out_type == AUTO_PIN_SPEAKER_OUT) {
		memcpy(cfg->speaker_pins, cfg->line_out_pins,
		       sizeof(cfg->speaker_pins));
		cfg->speaker_outs = cfg->line_outs;
	}

	if (!cfg->hp_pins[0] &&
	    cfg->line_out_type == AUTO_PIN_HP_OUT) {
		memcpy(cfg->hp_pins, cfg->line_out_pins,
		       sizeof(cfg->hp_pins));
		cfg->hp_outs = cfg->line_outs;
	}

	for (i = 0; i < cfg->hp_outs; i++) {
		hda_nid_t nid = cfg->hp_pins[i];
		if (!is_jack_detectable(codec, nid))
			continue;
		snd_printdd("realtek: Enable HP auto-muting on NID 0x%x\n",
			    nid);
		snd_hda_codec_write_cache(codec, nid, 0,
				  AC_VERB_SET_UNSOLICITED_ENABLE,
				  AC_USRSP_EN | ALC880_HP_EVENT);
		spec->automute = 1;
		spec->automute_mode = ALC_AUTOMUTE_PIN;
	}
	if (spec->automute && cfg->line_out_pins[0] &&
	    cfg->speaker_pins[0] &&
	    cfg->line_out_pins[0] != cfg->hp_pins[0] &&
	    cfg->line_out_pins[0] != cfg->speaker_pins[0]) {
		for (i = 0; i < cfg->line_outs; i++) {
			hda_nid_t nid = cfg->line_out_pins[i];
			if (!is_jack_detectable(codec, nid))
				continue;
			snd_printdd("realtek: Enable Line-Out auto-muting "
				    "on NID 0x%x\n", nid);
			snd_hda_codec_write_cache(codec, nid, 0,
					AC_VERB_SET_UNSOLICITED_ENABLE,
					AC_USRSP_EN | ALC880_FRONT_EVENT);
			spec->detect_line = 1;
		}
		spec->automute_lines = spec->detect_line;
	}

	if (spec->automute) {
		/* create a control for automute mode */
		alc_add_automute_mode_enum(codec);
		spec->unsol_event = alc_sku_unsol_event;
	}
}

static void alc_init_auto_mic(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	hda_nid_t fixed, ext, dock;
	int i;

	fixed = ext = dock = 0;
	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		unsigned int defcfg;
		defcfg = snd_hda_codec_get_pincfg(codec, nid);
		switch (snd_hda_get_input_pin_attr(defcfg)) {
		case INPUT_PIN_ATTR_INT:
			if (fixed)
				return; /* already occupied */
			if (cfg->inputs[i].type != AUTO_PIN_MIC)
				return; /* invalid type */
			fixed = nid;
			break;
		case INPUT_PIN_ATTR_UNUSED:
			return; /* invalid entry */
		case INPUT_PIN_ATTR_DOCK:
			if (dock)
				return; /* already occupied */
			if (cfg->inputs[i].type > AUTO_PIN_LINE_IN)
				return; /* invalid type */
			dock = nid;
			break;
		default:
			if (ext)
				return; /* already occupied */
			if (cfg->inputs[i].type != AUTO_PIN_MIC)
				return; /* invalid type */
			ext = nid;
			break;
		}
	}
	if (!ext && dock) {
		ext = dock;
		dock = 0;
	}
	if (!ext || !fixed)
		return;
	if (!is_jack_detectable(codec, ext))
		return; /* no unsol support */
	if (dock && !is_jack_detectable(codec, dock))
		return; /* no unsol support */
	snd_printdd("realtek: Enable auto-mic switch on NID 0x%x/0x%x/0x%x\n",
		    ext, fixed, dock);
	spec->ext_mic.pin = ext;
	spec->dock_mic.pin = dock;
	spec->int_mic.pin = fixed;
	spec->ext_mic.mux_idx = MUX_IDX_UNDEF; /* set later */
	spec->dock_mic.mux_idx = MUX_IDX_UNDEF; /* set later */
	spec->int_mic.mux_idx = MUX_IDX_UNDEF; /* set later */
	spec->auto_mic = 1;
	snd_hda_codec_write_cache(codec, spec->ext_mic.pin, 0,
				  AC_VERB_SET_UNSOLICITED_ENABLE,
				  AC_USRSP_EN | ALC880_MIC_EVENT);
	spec->unsol_event = alc_sku_unsol_event;
}

/* Could be any non-zero and even value. When used as fixup, tells
 * the driver to ignore any present sku defines.
 */
#define ALC_FIXUP_SKU_IGNORE (2)

static int alc_auto_parse_customize_define(struct hda_codec *codec)
{
	unsigned int ass, tmp, i;
	unsigned nid = 0;
	struct alc_spec *spec = codec->spec;

	spec->cdefine.enable_pcbeep = 1; /* assume always enabled */

	if (spec->cdefine.fixup) {
		ass = spec->cdefine.sku_cfg;
		if (ass == ALC_FIXUP_SKU_IGNORE)
			return -1;
		goto do_sku;
	}

	ass = codec->subsystem_id & 0xffff;
	if (ass != codec->bus->pci->subsystem_device && (ass & 1))
		goto do_sku;

	nid = 0x1d;
	if (codec->vendor_id == 0x10ec0260)
		nid = 0x17;
	ass = snd_hda_codec_get_pincfg(codec, nid);

	if (!(ass & 1)) {
		printk(KERN_INFO "hda_codec: %s: SKU not ready 0x%08x\n",
		       codec->chip_name, ass);
		return -1;
	}

	/* check sum */
	tmp = 0;
	for (i = 1; i < 16; i++) {
		if ((ass >> i) & 1)
			tmp++;
	}
	if (((ass >> 16) & 0xf) != tmp)
		return -1;

	spec->cdefine.port_connectivity = ass >> 30;
	spec->cdefine.enable_pcbeep = (ass & 0x100000) >> 20;
	spec->cdefine.check_sum = (ass >> 16) & 0xf;
	spec->cdefine.customization = ass >> 8;
do_sku:
	spec->cdefine.sku_cfg = ass;
	spec->cdefine.external_amp = (ass & 0x38) >> 3;
	spec->cdefine.platform_type = (ass & 0x4) >> 2;
	spec->cdefine.swap = (ass & 0x2) >> 1;
	spec->cdefine.override = ass & 0x1;

	snd_printd("SKU: Nid=0x%x sku_cfg=0x%08x\n",
		   nid, spec->cdefine.sku_cfg);
	snd_printd("SKU: port_connectivity=0x%x\n",
		   spec->cdefine.port_connectivity);
	snd_printd("SKU: enable_pcbeep=0x%x\n", spec->cdefine.enable_pcbeep);
	snd_printd("SKU: check_sum=0x%08x\n", spec->cdefine.check_sum);
	snd_printd("SKU: customization=0x%08x\n", spec->cdefine.customization);
	snd_printd("SKU: external_amp=0x%x\n", spec->cdefine.external_amp);
	snd_printd("SKU: platform_type=0x%x\n", spec->cdefine.platform_type);
	snd_printd("SKU: swap=0x%x\n", spec->cdefine.swap);
	snd_printd("SKU: override=0x%x\n", spec->cdefine.override);

	return 0;
}

/* check subsystem ID and set up device-specific initialization;
 * return 1 if initialized, 0 if invalid SSID
 */
/* 32-bit subsystem ID for BIOS loading in HD Audio codec.
 *	31 ~ 16 :	Manufacture ID
 *	15 ~ 8	:	SKU ID
 *	7  ~ 0	:	Assembly ID
 *	port-A --> pin 39/41, port-E --> pin 14/15, port-D --> pin 35/36
 */
static int alc_subsystem_id(struct hda_codec *codec,
			    hda_nid_t porta, hda_nid_t porte,
			    hda_nid_t portd, hda_nid_t porti)
{
	unsigned int ass, tmp, i;
	unsigned nid;
	struct alc_spec *spec = codec->spec;

	if (spec->cdefine.fixup) {
		ass = spec->cdefine.sku_cfg;
		if (ass == ALC_FIXUP_SKU_IGNORE)
			return 0;
		goto do_sku;
	}

	ass = codec->subsystem_id & 0xffff;
	if ((ass != codec->bus->pci->subsystem_device) && (ass & 1))
		goto do_sku;

	/* invalid SSID, check the special NID pin defcfg instead */
	/*
	 * 31~30	: port connectivity
	 * 29~21	: reserve
	 * 20		: PCBEEP input
	 * 19~16	: Check sum (15:1)
	 * 15~1		: Custom
	 * 0		: override
	*/
	nid = 0x1d;
	if (codec->vendor_id == 0x10ec0260)
		nid = 0x17;
	ass = snd_hda_codec_get_pincfg(codec, nid);
	snd_printd("realtek: No valid SSID, "
		   "checking pincfg 0x%08x for NID 0x%x\n",
		   ass, nid);
	if (!(ass & 1))
		return 0;
	if ((ass >> 30) != 1)	/* no physical connection */
		return 0;

	/* check sum */
	tmp = 0;
	for (i = 1; i < 16; i++) {
		if ((ass >> i) & 1)
			tmp++;
	}
	if (((ass >> 16) & 0xf) != tmp)
		return 0;
do_sku:
	snd_printd("realtek: Enabling init ASM_ID=0x%04x CODEC_ID=%08x\n",
		   ass & 0xffff, codec->vendor_id);
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
		spec->init_amp = ALC_INIT_GPIO1;
		break;
	case 3:
		spec->init_amp = ALC_INIT_GPIO2;
		break;
	case 7:
		spec->init_amp = ALC_INIT_GPIO3;
		break;
	case 5:
	default:
		spec->init_amp = ALC_INIT_DEFAULT;
		break;
	}

	/* is laptop or Desktop and enable the function "Mute internal speaker
	 * when the external headphone out jack is plugged"
	 */
	if (!(ass & 0x8000))
		return 1;
	/*
	 * 10~8 : Jack location
	 * 12~11: Headphone out -> 00: PortA, 01: PortE, 02: PortD, 03: Resvered
	 * 14~13: Resvered
	 * 15   : 1 --> enable the function "Mute internal speaker
	 *	        when the external headphone out jack is plugged"
	 */
	if (!spec->autocfg.hp_pins[0] &&
	    !(spec->autocfg.line_out_pins[0] &&
	      spec->autocfg.line_out_type == AUTO_PIN_HP_OUT)) {
		hda_nid_t nid;
		tmp = (ass >> 11) & 0x3;	/* HP to chassis */
		if (tmp == 0)
			nid = porta;
		else if (tmp == 1)
			nid = porte;
		else if (tmp == 2)
			nid = portd;
		else if (tmp == 3)
			nid = porti;
		else
			return 1;
		for (i = 0; i < spec->autocfg.line_outs; i++)
			if (spec->autocfg.line_out_pins[i] == nid)
				return 1;
		spec->autocfg.hp_pins[0] = nid;
	}
	return 1;
}

static void alc_ssid_check(struct hda_codec *codec,
			   hda_nid_t porta, hda_nid_t porte,
			   hda_nid_t portd, hda_nid_t porti)
{
	if (!alc_subsystem_id(codec, porta, porte, portd, porti)) {
		struct alc_spec *spec = codec->spec;
		snd_printd("realtek: "
			   "Enable default setup for auto mode as fallback\n");
		spec->init_amp = ALC_INIT_DEFAULT;
	}

	alc_init_auto_hp(codec);
	alc_init_auto_mic(codec);
}

/*
 * Fix-up pin default configurations and add default verbs
 */

struct alc_pincfg {
	hda_nid_t nid;
	u32 val;
};

struct alc_model_fixup {
	const int id;
	const char *name;
};

struct alc_fixup {
	int type;
	bool chained;
	int chain_id;
	union {
		unsigned int sku;
		const struct alc_pincfg *pins;
		const struct hda_verb *verbs;
		void (*func)(struct hda_codec *codec,
			     const struct alc_fixup *fix,
			     int action);
	} v;
};

enum {
	ALC_FIXUP_INVALID,
	ALC_FIXUP_SKU,
	ALC_FIXUP_PINS,
	ALC_FIXUP_VERBS,
	ALC_FIXUP_FUNC,
};

enum {
	ALC_FIXUP_ACT_PRE_PROBE,
	ALC_FIXUP_ACT_PROBE,
	ALC_FIXUP_ACT_INIT,
};

static void alc_apply_fixup(struct hda_codec *codec, int action)
{
	struct alc_spec *spec = codec->spec;
	int id = spec->fixup_id;
#ifdef CONFIG_SND_DEBUG_VERBOSE
	const char *modelname = spec->fixup_name;
#endif
	int depth = 0;

	if (!spec->fixup_list)
		return;

	while (id >= 0) {
		const struct alc_fixup *fix = spec->fixup_list + id;
		const struct alc_pincfg *cfg;

		switch (fix->type) {
		case ALC_FIXUP_SKU:
			if (action != ALC_FIXUP_ACT_PRE_PROBE || !fix->v.sku)
				break;;
			snd_printdd(KERN_INFO "hda_codec: %s: "
				    "Apply sku override for %s\n",
				    codec->chip_name, modelname);
			spec->cdefine.sku_cfg = fix->v.sku;
			spec->cdefine.fixup = 1;
			break;
		case ALC_FIXUP_PINS:
			cfg = fix->v.pins;
			if (action != ALC_FIXUP_ACT_PRE_PROBE || !cfg)
				break;
			snd_printdd(KERN_INFO "hda_codec: %s: "
				    "Apply pincfg for %s\n",
				    codec->chip_name, modelname);
			for (; cfg->nid; cfg++)
				snd_hda_codec_set_pincfg(codec, cfg->nid,
							 cfg->val);
			break;
		case ALC_FIXUP_VERBS:
			if (action != ALC_FIXUP_ACT_PROBE || !fix->v.verbs)
				break;
			snd_printdd(KERN_INFO "hda_codec: %s: "
				    "Apply fix-verbs for %s\n",
				    codec->chip_name, modelname);
			add_verb(codec->spec, fix->v.verbs);
			break;
		case ALC_FIXUP_FUNC:
			if (!fix->v.func)
				break;
			snd_printdd(KERN_INFO "hda_codec: %s: "
				    "Apply fix-func for %s\n",
				    codec->chip_name, modelname);
			fix->v.func(codec, fix, action);
			break;
		default:
			snd_printk(KERN_ERR "hda_codec: %s: "
				   "Invalid fixup type %d\n",
				   codec->chip_name, fix->type);
			break;
		}
		if (!fix->chained)
			break;
		if (++depth > 10)
			break;
		id = fix->chain_id;
	}
}

static void alc_pick_fixup(struct hda_codec *codec,
			   const struct alc_model_fixup *models,
			   const struct snd_pci_quirk *quirk,
			   const struct alc_fixup *fixlist)
{
	struct alc_spec *spec = codec->spec;
	int id = -1;
	const char *name = NULL;

	if (codec->modelname && models) {
		while (models->name) {
			if (!strcmp(codec->modelname, models->name)) {
				id = models->id;
				name = models->name;
				break;
			}
			models++;
		}
	}
	if (id < 0) {
		quirk = snd_pci_quirk_lookup(codec->bus->pci, quirk);
		if (quirk) {
			id = quirk->value;
#ifdef CONFIG_SND_DEBUG_VERBOSE
			name = quirk->name;
#endif
		}
	}

	spec->fixup_id = id;
	if (id >= 0) {
		spec->fixup_list = fixlist;
		spec->fixup_name = name;
	}
}

static int alc_read_coef_idx(struct hda_codec *codec,
			unsigned int coef_idx)
{
	unsigned int val;
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX,
		    		coef_idx);
	val = snd_hda_codec_read(codec, 0x20, 0,
			 	AC_VERB_GET_PROC_COEF, 0);
	return val;
}

static void alc_write_coef_idx(struct hda_codec *codec, unsigned int coef_idx,
							unsigned int coef_val)
{
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_COEF_INDEX,
			    coef_idx);
	snd_hda_codec_write(codec, 0x20, 0, AC_VERB_SET_PROC_COEF,
			    coef_val);
}

/* set right pin controls for digital I/O */
static void alc_auto_init_digital(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;
	hda_nid_t pin;

	for (i = 0; i < spec->autocfg.dig_outs; i++) {
		pin = spec->autocfg.dig_out_pins[i];
		if (pin) {
			snd_hda_codec_write(codec, pin, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    PIN_OUT);
		}
	}
	pin = spec->autocfg.dig_in_pin;
	if (pin)
		snd_hda_codec_write(codec, pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL,
				    PIN_IN);
}

/* parse digital I/Os and set up NIDs in BIOS auto-parse mode */
static void alc_auto_parse_digital(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i, err, nums;
	hda_nid_t dig_nid;

	/* support multiple SPDIFs; the secondary is set up as a slave */
	nums = 0;
	for (i = 0; i < spec->autocfg.dig_outs; i++) {
		err = snd_hda_get_connections(codec,
					      spec->autocfg.dig_out_pins[i],
					      &dig_nid, 1);
		if (err <= 0)
			continue;
		if (!nums) {
			spec->multiout.dig_out_nid = dig_nid;
			spec->dig_out_type = spec->autocfg.dig_out_type[0];
		} else {
			spec->multiout.slave_dig_outs = spec->slave_dig_outs;
			if (nums >= ARRAY_SIZE(spec->slave_dig_outs) - 1)
				break;
			spec->slave_dig_outs[nums - 1] = dig_nid;
		}
		nums++;
	}

	if (spec->autocfg.dig_in_pin) {
		dig_nid = codec->start_nid;
		for (i = 0; i < codec->num_nodes; i++, dig_nid++) {
			unsigned int wcaps = get_wcaps(codec, dig_nid);
			if (get_wcaps_type(wcaps) != AC_WID_AUD_IN)
				continue;
			if (!(wcaps & AC_WCAP_DIGITAL))
				continue;
			if (!(wcaps & AC_WCAP_CONN_LIST))
				continue;
			err = get_connection_index(codec, dig_nid,
						   spec->autocfg.dig_in_pin);
			if (err >= 0) {
				spec->dig_in_nid = dig_nid;
				break;
			}
		}
	}
}

/*
 * ALC888
 */

/*
 * 2ch mode
 */
static const struct hda_verb alc888_4ST_ch2_intel_init[] = {
/* Mic-in jack as mic in */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
/* Line-in jack as Line in */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
/* Line-Out as Front */
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x00},
	{ } /* end */
};

/*
 * 4ch mode
 */
static const struct hda_verb alc888_4ST_ch4_intel_init[] = {
/* Mic-in jack as mic in */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
/* Line-in jack as Surround */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
/* Line-Out as Front */
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x00},
	{ } /* end */
};

/*
 * 6ch mode
 */
static const struct hda_verb alc888_4ST_ch6_intel_init[] = {
/* Mic-in jack as CLFE */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
/* Line-in jack as Surround */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
/* Line-Out as CLFE (workaround because Mic-in is not loud enough) */
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
	{ } /* end */
};

/*
 * 8ch mode
 */
static const struct hda_verb alc888_4ST_ch8_intel_init[] = {
/* Mic-in jack as CLFE */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
/* Line-in jack as Surround */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
/* Line-Out as Side */
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
	{ } /* end */
};

static const struct hda_channel_mode alc888_4ST_8ch_intel_modes[4] = {
	{ 2, alc888_4ST_ch2_intel_init },
	{ 4, alc888_4ST_ch4_intel_init },
	{ 6, alc888_4ST_ch6_intel_init },
	{ 8, alc888_4ST_ch8_intel_init },
};

/*
 * ALC888 Fujitsu Siemens Amillo xa3530
 */

static const struct hda_verb alc888_fujitsu_xa3530_verbs[] = {
/* Front Mic: set to PIN_IN (empty by default) */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
/* Connect Internal HP to Front */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
/* Connect Bass HP to Front */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
/* Connect Line-Out side jack (SPDIF) to Side */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
/* Connect Mic jack to CLFE */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02},
/* Connect Line-in jack to Surround */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01},
/* Connect HP out jack to Front */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
/* Enable unsolicited event for HP jack and Line-out jack */
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x17, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{}
};

static void alc889_automute_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->autocfg.speaker_pins[2] = 0x17;
	spec->autocfg.speaker_pins[3] = 0x19;
	spec->autocfg.speaker_pins[4] = 0x1a;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc889_intel_init_hook(struct hda_codec *codec)
{
	alc889_coef_init(codec);
	alc_hp_automute(codec);
}

static void alc888_fujitsu_xa3530_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x17; /* line-out */
	spec->autocfg.hp_pins[1] = 0x1b; /* hp */
	spec->autocfg.speaker_pins[0] = 0x14; /* speaker */
	spec->autocfg.speaker_pins[1] = 0x15; /* bass */
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

/*
 * ALC888 Acer Aspire 4930G model
 */

static const struct hda_verb alc888_acer_aspire_4930g_verbs[] = {
/* Front Mic: set to PIN_IN (empty by default) */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
/* Unselect Front Mic by default in input mixer 3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0xb)},
/* Enable unsolicited event for HP jack */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
/* Connect Internal HP to front */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
/* Connect HP out to front */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

/*
 * ALC888 Acer Aspire 6530G model
 */

static const struct hda_verb alc888_acer_aspire_6530g_verbs[] = {
/* Route to built-in subwoofer as well as speakers */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
/* Bias voltage on for external mic port */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN | PIN_VREF80},
/* Front Mic: set to PIN_IN (empty by default) */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
/* Unselect Front Mic by default in input mixer 3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0xb)},
/* Enable unsolicited event for HP jack */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
/* Enable speaker output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
/* Enable headphone output */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

/*
 *ALC888 Acer Aspire 7730G model
 */

static const struct hda_verb alc888_acer_aspire_7730G_verbs[] = {
/* Bias voltage on for external mic port */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN | PIN_VREF80},
/* Front Mic: set to PIN_IN (empty by default) */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
/* Unselect Front Mic by default in input mixer 3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0xb)},
/* Enable unsolicited event for HP jack */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
/* Enable speaker output */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
/* Enable headphone output */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
/*Enable internal subwoofer */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x02},
	{0x17, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

/*
 * ALC889 Acer Aspire 8930G model
 */

static const struct hda_verb alc889_acer_aspire_8930g_verbs[] = {
/* Front Mic: set to PIN_IN (empty by default) */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
/* Unselect Front Mic by default in input mixer 3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0xb)},
/* Enable unsolicited event for HP jack */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
/* Connect Internal Front to Front */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
/* Connect Internal Rear to Rear */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x01},
/* Connect Internal CLFE to CLFE */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x02},
/* Connect HP out to Front */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
/* Enable all DACs */
/*  DAC DISABLE/MUTE 1? */
/*  setting bits 1-5 disables DAC nids 0x02-0x06 apparently. Init=0x38 */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x03},
	{0x20, AC_VERB_SET_PROC_COEF, 0x0000},
/*  DAC DISABLE/MUTE 2? */
/*  some bit here disables the other DACs. Init=0x4900 */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x08},
	{0x20, AC_VERB_SET_PROC_COEF, 0x0000},
/* DMIC fix
 * This laptop has a stereo digital microphone. The mics are only 1cm apart
 * which makes the stereo useless. However, either the mic or the ALC889
 * makes the signal become a difference/sum signal instead of standard
 * stereo, which is annoying. So instead we flip this bit which makes the
 * codec replicate the sum signal to both channels, turning it into a
 * normal mono mic.
 */
/*  DMIC_CONTROL? Init value = 0x0001 */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x0b},
	{0x20, AC_VERB_SET_PROC_COEF, 0x0003},
	{ }
};

static const struct hda_input_mux alc888_2_capture_sources[2] = {
	/* Front mic only available on one ADC */
	{
		.num_items = 4,
		.items = {
			{ "Mic", 0x0 },
			{ "Line", 0x2 },
			{ "CD", 0x4 },
			{ "Front Mic", 0xb },
		},
	},
	{
		.num_items = 3,
		.items = {
			{ "Mic", 0x0 },
			{ "Line", 0x2 },
			{ "CD", 0x4 },
		},
	}
};

static const struct hda_input_mux alc888_acer_aspire_6530_sources[2] = {
	/* Interal mic only available on one ADC */
	{
		.num_items = 5,
		.items = {
			{ "Mic", 0x0 },
			{ "Line In", 0x2 },
			{ "CD", 0x4 },
			{ "Input Mix", 0xa },
			{ "Internal Mic", 0xb },
		},
	},
	{
		.num_items = 4,
		.items = {
			{ "Mic", 0x0 },
			{ "Line In", 0x2 },
			{ "CD", 0x4 },
			{ "Input Mix", 0xa },
		},
	}
};

static const struct hda_input_mux alc889_capture_sources[3] = {
	/* Digital mic only available on first "ADC" */
	{
		.num_items = 5,
		.items = {
			{ "Mic", 0x0 },
			{ "Line", 0x2 },
			{ "CD", 0x4 },
			{ "Front Mic", 0xb },
			{ "Input Mix", 0xa },
		},
	},
	{
		.num_items = 4,
		.items = {
			{ "Mic", 0x0 },
			{ "Line", 0x2 },
			{ "CD", 0x4 },
			{ "Input Mix", 0xa },
		},
	},
	{
		.num_items = 4,
		.items = {
			{ "Mic", 0x0 },
			{ "Line", 0x2 },
			{ "CD", 0x4 },
			{ "Input Mix", 0xa },
		},
	}
};

static const struct snd_kcontrol_new alc888_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0,
		HDA_OUTPUT),
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
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc888_acer_aspire_4930g_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0,
		HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Internal LFE Playback Volume", 0x0f, 1, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Internal LFE Playback Switch", 0x0f, 1, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc889_acer_aspire_8930g_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0,
		HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};


static void alc888_acer_aspire_4930g_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->autocfg.speaker_pins[2] = 0x17;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc888_acer_aspire_6530g_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->autocfg.speaker_pins[2] = 0x17;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc888_acer_aspire_7730g_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->autocfg.speaker_pins[2] = 0x17;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc889_acer_aspire_8930g_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->autocfg.speaker_pins[2] = 0x1b;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

/*
 * ALC880 3-stack model
 *
 * DAC: Front = 0x02 (0x0c), Surr = 0x05 (0x0f), CLFE = 0x04 (0x0e)
 * Pin assignment: Front = 0x14, Line-In/Surr = 0x1a, Mic/CLFE = 0x18,
 *                 F-Mic = 0x1b, HP = 0x19
 */

static const hda_nid_t alc880_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x05, 0x04, 0x03
};

static const hda_nid_t alc880_adc_nids[3] = {
	/* ADC0-2 */
	0x07, 0x08, 0x09,
};

/* The datasheet says the node 0x07 is connected from inputs,
 * but it shows zero connection in the real implementation on some devices.
 * Note: this is a 915GAV bug, fixed on 915GLV
 */
static const hda_nid_t alc880_adc_nids_alt[2] = {
	/* ADC1-2 */
	0x08, 0x09,
};

#define ALC880_DIGOUT_NID	0x06
#define ALC880_DIGIN_NID	0x0a

static const struct hda_input_mux alc880_capture_source = {
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
static const struct hda_verb alc880_threestack_ch2_init[] = {
	/* set line-in to input, mute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	/* set mic-in to input vref 80%, mute it */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/* 6ch mode */
static const struct hda_verb alc880_threestack_ch6_init[] = {
	/* set line-in to output, unmute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	/* set mic-in to output, unmute it */
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ } /* end */
};

static const struct hda_channel_mode alc880_threestack_modes[2] = {
	{ 2, alc880_threestack_ch2_init },
	{ 6, alc880_threestack_ch6_init },
};

static const struct snd_kcontrol_new alc880_three_stack_mixer[] = {
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
static int alc_cap_vol_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int err;

	mutex_lock(&codec->control_mutex);
	kcontrol->private_value = HDA_COMPOSE_AMP_VAL(spec->adc_nids[0], 3, 0,
						      HDA_INPUT);
	err = snd_hda_mixer_amp_volume_info(kcontrol, uinfo);
	mutex_unlock(&codec->control_mutex);
	return err;
}

static int alc_cap_vol_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			   unsigned int size, unsigned int __user *tlv)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int err;

	mutex_lock(&codec->control_mutex);
	kcontrol->private_value = HDA_COMPOSE_AMP_VAL(spec->adc_nids[0], 3, 0,
						      HDA_INPUT);
	err = snd_hda_mixer_amp_tlv(kcontrol, op_flag, size, tlv);
	mutex_unlock(&codec->control_mutex);
	return err;
}

typedef int (*getput_call_t)(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol);

static int alc_cap_getput_caller(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol,
				 getput_call_t func, bool check_adc_switch)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int i, err = 0;

	mutex_lock(&codec->control_mutex);
	if (check_adc_switch && spec->dual_adc_switch) {
		for (i = 0; i < spec->num_adc_nids; i++) {
			kcontrol->private_value =
				HDA_COMPOSE_AMP_VAL(spec->adc_nids[i],
						    3, 0, HDA_INPUT);
			err = func(kcontrol, ucontrol);
			if (err < 0)
				goto error;
		}
	} else {
		i = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
		kcontrol->private_value =
			HDA_COMPOSE_AMP_VAL(spec->adc_nids[i],
					    3, 0, HDA_INPUT);
		err = func(kcontrol, ucontrol);
	}
 error:
	mutex_unlock(&codec->control_mutex);
	return err;
}

static int alc_cap_vol_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	return alc_cap_getput_caller(kcontrol, ucontrol,
				     snd_hda_mixer_amp_volume_get, false);
}

static int alc_cap_vol_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	return alc_cap_getput_caller(kcontrol, ucontrol,
				     snd_hda_mixer_amp_volume_put, true);
}

/* capture mixer elements */
#define alc_cap_sw_info		snd_ctl_boolean_stereo_info

static int alc_cap_sw_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	return alc_cap_getput_caller(kcontrol, ucontrol,
				     snd_hda_mixer_amp_switch_get, false);
}

static int alc_cap_sw_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	return alc_cap_getput_caller(kcontrol, ucontrol,
				     snd_hda_mixer_amp_switch_put, true);
}

#define _DEFINE_CAPMIX(num) \
	{ \
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name = "Capture Switch", \
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE, \
		.count = num, \
		.info = alc_cap_sw_info, \
		.get = alc_cap_sw_get, \
		.put = alc_cap_sw_put, \
	}, \
	{ \
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name = "Capture Volume", \
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE | \
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
			   SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK), \
		.count = num, \
		.info = alc_cap_vol_info, \
		.get = alc_cap_vol_get, \
		.put = alc_cap_vol_put, \
		.tlv = { .c = alc_cap_vol_tlv }, \
	}

#define _DEFINE_CAPSRC(num) \
	{ \
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
		/* .name = "Capture Source", */ \
		.name = "Input Source", \
		.count = num, \
		.info = alc_mux_enum_info, \
		.get = alc_mux_enum_get, \
		.put = alc_mux_enum_put, \
	}

#define DEFINE_CAPMIX(num) \
static const struct snd_kcontrol_new alc_capture_mixer ## num[] = { \
	_DEFINE_CAPMIX(num),				      \
	_DEFINE_CAPSRC(num),				      \
	{ } /* end */					      \
}

#define DEFINE_CAPMIX_NOSRC(num) \
static const struct snd_kcontrol_new alc_capture_mixer_nosrc ## num[] = { \
	_DEFINE_CAPMIX(num),					    \
	{ } /* end */						    \
}

/* up to three ADCs */
DEFINE_CAPMIX(1);
DEFINE_CAPMIX(2);
DEFINE_CAPMIX(3);
DEFINE_CAPMIX_NOSRC(1);
DEFINE_CAPMIX_NOSRC(2);
DEFINE_CAPMIX_NOSRC(3);

/*
 * ALC880 5-stack model
 *
 * DAC: Front = 0x02 (0x0c), Surr = 0x05 (0x0f), CLFE = 0x04 (0x0d),
 *      Side = 0x02 (0xd)
 * Pin assignment: Front = 0x14, Surr = 0x17, CLFE = 0x16
 *                 Line-In/Side = 0x1a, Mic = 0x18, F-Mic = 0x1b, HP = 0x19
 */

/* additional mixers to alc880_three_stack_mixer */
static const struct snd_kcontrol_new alc880_five_stack_mixer[] = {
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0d, 2, HDA_INPUT),
	{ } /* end */
};

/* channel source setting (6/8 channel selection for 5-stack) */
/* 6ch mode */
static const struct hda_verb alc880_fivestack_ch6_init[] = {
	/* set line-in to input, mute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/* 8ch mode */
static const struct hda_verb alc880_fivestack_ch8_init[] = {
	/* set line-in to output, unmute it */
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ } /* end */
};

static const struct hda_channel_mode alc880_fivestack_modes[2] = {
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

static const hda_nid_t alc880_6st_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x03, 0x04, 0x05
};

static const struct hda_input_mux alc880_6stack_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

/* fixed 8-channels */
static const struct hda_channel_mode alc880_sixstack_modes[1] = {
	{ 8, NULL },
};

static const struct snd_kcontrol_new alc880_six_stack_mixer[] = {
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

static const hda_nid_t alc880_w810_dac_nids[3] = {
	/* front, rear/surround, clfe */
	0x02, 0x03, 0x04
};

/* fixed 6 channels */
static const struct hda_channel_mode alc880_w810_modes[1] = {
	{ 6, NULL }
};

/* Pin assignment: Front = 0x14, Surr = 0x15, CLFE = 0x16, HP = 0x1b */
static const struct snd_kcontrol_new alc880_w810_base_mixer[] = {
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

static const hda_nid_t alc880_z71v_dac_nids[1] = {
	0x02
};
#define ALC880_Z71V_HP_DAC	0x03

/* fixed 2 channels */
static const struct hda_channel_mode alc880_2_jack_modes[1] = {
	{ 2, NULL }
};

static const struct snd_kcontrol_new alc880_z71v_mixer[] = {
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

static const hda_nid_t alc880_f1734_dac_nids[1] = {
	0x03
};
#define ALC880_F1734_HP_DAC	0x02

static const struct snd_kcontrol_new alc880_f1734_mixer[] = {
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

static const struct hda_input_mux alc880_f1734_capture_source = {
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

static const struct snd_kcontrol_new alc880_asus_mixer[] = {
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
static const struct snd_kcontrol_new alc880_asus_w1v_mixer[] = {
	HDA_CODEC_VOLUME("Line2 Playback Volume", 0x0b, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Line2 Playback Switch", 0x0b, 0x03, HDA_INPUT),
	{ } /* end */
};

/* TCL S700 */
static const struct snd_kcontrol_new alc880_tcl_s700_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0B, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0B, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0B, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0B, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{ } /* end */
};

/* Uniwill */
static const struct snd_kcontrol_new alc880_uniwill_mixer[] = {
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
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static const struct snd_kcontrol_new alc880_fujitsu_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc880_uniwill_p53_mixer[] = {
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
static const char * const alc_slave_vols[] = {
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

static const char * const alc_slave_sws[] = {
	"Front Playback Switch",
	"Surround Playback Switch",
	"Center Playback Switch",
	"LFE Playback Switch",
	"Side Playback Switch",
	"Headphone Playback Switch",
	"Speaker Playback Switch",
	"Mono Playback Switch",
	"IEC958 Playback Switch",
	"Line-Out Playback Switch",
	NULL,
};

/*
 * build control elements
 */

#define NID_MAPPING		(-1)

#define SUBDEV_SPEAKER_		(0 << 6)
#define SUBDEV_HP_		(1 << 6)
#define SUBDEV_LINE_		(2 << 6)
#define SUBDEV_SPEAKER(x)	(SUBDEV_SPEAKER_ | ((x) & 0x3f))
#define SUBDEV_HP(x)		(SUBDEV_HP_ | ((x) & 0x3f))
#define SUBDEV_LINE(x)		(SUBDEV_LINE_ | ((x) & 0x3f))

static void alc_free_kctls(struct hda_codec *codec);

#ifdef CONFIG_SND_HDA_INPUT_BEEP
/* additional beep mixers; the actual parameters are overwritten at build */
static const struct snd_kcontrol_new alc_beep_mixer[] = {
	HDA_CODEC_VOLUME("Beep Playback Volume", 0, 0, HDA_INPUT),
	HDA_CODEC_MUTE_BEEP("Beep Playback Switch", 0, 0, HDA_INPUT),
	{ } /* end */
};
#endif

static int alc_build_controls(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct snd_kcontrol *kctl = NULL;
	const struct snd_kcontrol_new *knew;
	int i, j, err;
	unsigned int u;
	hda_nid_t nid;

	for (i = 0; i < spec->num_mixers; i++) {
		err = snd_hda_add_new_ctls(codec, spec->mixers[i]);
		if (err < 0)
			return err;
	}
	if (spec->cap_mixer) {
		err = snd_hda_add_new_ctls(codec, spec->cap_mixer);
		if (err < 0)
			return err;
	}
	if (spec->multiout.dig_out_nid) {
		err = snd_hda_create_spdif_out_ctls(codec,
						    spec->multiout.dig_out_nid);
		if (err < 0)
			return err;
		if (!spec->no_analog) {
			err = snd_hda_create_spdif_share_sw(codec,
							    &spec->multiout);
			if (err < 0)
				return err;
			spec->multiout.share_spdif = 1;
		}
	}
	if (spec->dig_in_nid) {
		err = snd_hda_create_spdif_in_ctls(codec, spec->dig_in_nid);
		if (err < 0)
			return err;
	}

#ifdef CONFIG_SND_HDA_INPUT_BEEP
	/* create beep controls if needed */
	if (spec->beep_amp) {
		const struct snd_kcontrol_new *knew;
		for (knew = alc_beep_mixer; knew->name; knew++) {
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
#endif

	/* if we have no master control, let's create it */
	if (!spec->no_analog &&
	    !snd_hda_find_mixer_ctl(codec, "Master Playback Volume")) {
		unsigned int vmaster_tlv[4];
		snd_hda_set_vmaster_tlv(codec, spec->vmaster_nid,
					HDA_OUTPUT, vmaster_tlv);
		err = snd_hda_add_vmaster(codec, "Master Playback Volume",
					  vmaster_tlv, alc_slave_vols);
		if (err < 0)
			return err;
	}
	if (!spec->no_analog &&
	    !snd_hda_find_mixer_ctl(codec, "Master Playback Switch")) {
		err = snd_hda_add_vmaster(codec, "Master Playback Switch",
					  NULL, alc_slave_sws);
		if (err < 0)
			return err;
	}

	/* assign Capture Source enums to NID */
	if (spec->capsrc_nids || spec->adc_nids) {
		kctl = snd_hda_find_mixer_ctl(codec, "Capture Source");
		if (!kctl)
			kctl = snd_hda_find_mixer_ctl(codec, "Input Source");
		for (i = 0; kctl && i < kctl->count; i++) {
			const hda_nid_t *nids = spec->capsrc_nids;
			if (!nids)
				nids = spec->adc_nids;
			err = snd_hda_add_nid(codec, kctl, i, nids[i]);
			if (err < 0)
				return err;
		}
	}
	if (spec->cap_mixer) {
		const char *kname = kctl ? kctl->id.name : NULL;
		for (knew = spec->cap_mixer; knew->name; knew++) {
			if (kname && strcmp(knew->name, kname) == 0)
				continue;
			kctl = snd_hda_find_mixer_ctl(codec, knew->name);
			for (i = 0; kctl && i < kctl->count; i++) {
				err = snd_hda_add_nid(codec, kctl, i,
						      spec->adc_nids[i]);
				if (err < 0)
					return err;
			}
		}
	}

	/* other nid->control mapping */
	for (i = 0; i < spec->num_mixers; i++) {
		for (knew = spec->mixers[i]; knew->name; knew++) {
			if (knew->iface != NID_MAPPING)
				continue;
			kctl = snd_hda_find_mixer_ctl(codec, knew->name);
			if (kctl == NULL)
				continue;
			u = knew->subdevice;
			for (j = 0; j < 4; j++, u >>= 8) {
				nid = u & 0x3f;
				if (nid == 0)
					continue;
				switch (u & 0xc0) {
				case SUBDEV_SPEAKER_:
					nid = spec->autocfg.speaker_pins[nid];
					break;
				case SUBDEV_LINE_:
					nid = spec->autocfg.line_out_pins[nid];
					break;
				case SUBDEV_HP_:
					nid = spec->autocfg.hp_pins[nid];
					break;
				default:
					continue;
				}
				err = snd_hda_add_nid(codec, kctl, 0, nid);
				if (err < 0)
					return err;
			}
			u = knew->private_value;
			for (j = 0; j < 4; j++, u >>= 8) {
				nid = u & 0xff;
				if (nid == 0)
					continue;
				err = snd_hda_add_nid(codec, kctl, 0, nid);
				if (err < 0)
					return err;
			}
		}
	}

	alc_free_kctls(codec); /* no longer needed */

	return 0;
}


/*
 * initialize the codec volumes, etc
 */

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static const struct hda_verb alc880_volume_init_verbs[] = {
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
static const struct hda_verb alc880_pin_3stack_init_verbs[] = {
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
static const struct hda_verb alc880_pin_5stack_init_verbs[] = {
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
static const struct hda_verb alc880_pin_w810_init_verbs[] = {
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
static const struct hda_verb alc880_pin_z71v_init_verbs[] = {
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
static const struct hda_verb alc880_pin_6stack_init_verbs[] = {
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
static const struct hda_verb alc880_uniwill_init_verbs[] = {
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
static const struct hda_verb alc880_uniwill_p53_init_verbs[] = {
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

static const struct hda_verb alc880_beep_init_verbs[] = {
	{ 0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(5) },
	{ }
};

/* auto-toggle front mic */
static void alc88x_simple_mic_automute(struct hda_codec *codec)
{
 	unsigned int present;
	unsigned char bits;

	present = snd_hda_jack_detect(codec, 0x18);
	bits = present ? HDA_AMP_MUTE : 0;
	snd_hda_codec_amp_stereo(codec, 0x0b, HDA_INPUT, 1, HDA_AMP_MUTE, bits);
}

static void alc880_uniwill_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x16;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc880_uniwill_init_hook(struct hda_codec *codec)
{
	alc_hp_automute(codec);
	alc88x_simple_mic_automute(codec);
}

static void alc880_uniwill_unsol_event(struct hda_codec *codec,
				       unsigned int res)
{
	/* Looks like the unsol event is incompatible with the standard
	 * definition.  4bit tag is placed at 28 bit!
	 */
	switch (res >> 28) {
	case ALC880_MIC_EVENT:
		alc88x_simple_mic_automute(codec);
		break;
	default:
		alc_sku_unsol_event(codec, res);
		break;
	}
}

static void alc880_uniwill_p53_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
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
	if ((res >> 28) == ALC880_DCVOL_EVENT)
		alc880_uniwill_p53_dcvol_automute(codec);
	else
		alc_sku_unsol_event(codec, res);
}

/*
 * F1734 pin configuration:
 * HP = 0x14, speaker-out = 0x15, mic = 0x18
 */
static const struct hda_verb alc880_pin_f1734_init_verbs[] = {
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
static const struct hda_verb alc880_pin_asus_init_verbs[] = {
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
#define alc880_gpio3_init_verbs	alc_gpio3_init_verbs

/* Clevo m520g init */
static const struct hda_verb alc880_pin_clevo_init_verbs[] = {
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

static const struct hda_verb alc880_pin_tcl_S700_init_verbs[] = {
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
static const hda_nid_t alc880_lg_dac_nids[3] = {
	0x05, 0x02, 0x03
};

/* seems analog CD is not working */
static const struct hda_input_mux alc880_lg_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x1 },
		{ "Line", 0x5 },
		{ "Internal Mic", 0x6 },
	},
};

/* 2,4,6 channel modes */
static const struct hda_verb alc880_lg_ch2_init[] = {
	/* set line-in and mic-in to input */
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ }
};

static const struct hda_verb alc880_lg_ch4_init[] = {
	/* set line-in to out and mic-in to input */
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ }
};

static const struct hda_verb alc880_lg_ch6_init[] = {
	/* set line-in and mic-in to output */
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ }
};

static const struct hda_channel_mode alc880_lg_ch_modes[3] = {
	{ 2, alc880_lg_ch2_init },
	{ 4, alc880_lg_ch4_init },
	{ 6, alc880_lg_ch6_init },
};

static const struct snd_kcontrol_new alc880_lg_mixer[] = {
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

static const struct hda_verb alc880_lg_init_verbs[] = {
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
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc880_lg_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x17;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
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

static const struct hda_input_mux alc880_lg_lw_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x1 },
		{ "Line In", 0x2 },
	},
};

#define alc880_lg_lw_modes alc880_threestack_modes

static const struct snd_kcontrol_new alc880_lg_lw_mixer[] = {
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

static const struct hda_verb alc880_lg_lw_init_verbs[] = {
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
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc880_lg_lw_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct snd_kcontrol_new alc880_medion_rim_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Master Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct hda_input_mux alc880_medion_rim_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x1 },
	},
};

static const struct hda_verb alc880_medion_rim_init_verbs[] = {
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
	struct alc_spec *spec = codec->spec;
	alc_hp_automute(codec);
	/* toggle EAPD */
	if (spec->jack_present)
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

static void alc880_medion_rim_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x1b;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static const struct hda_amp_list alc880_loopbacks[] = {
	{ 0x0b, HDA_INPUT, 0 },
	{ 0x0b, HDA_INPUT, 1 },
	{ 0x0b, HDA_INPUT, 2 },
	{ 0x0b, HDA_INPUT, 3 },
	{ 0x0b, HDA_INPUT, 4 },
	{ } /* end */
};

static const struct hda_amp_list alc880_lg_loopbacks[] = {
	{ 0x0b, HDA_INPUT, 1 },
	{ 0x0b, HDA_INPUT, 6 },
	{ 0x0b, HDA_INPUT, 7 },
	{ } /* end */
};
#endif

/*
 * Common callbacks
 */

static void alc_init_special_input_src(struct hda_codec *codec);

static int alc_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	unsigned int i;

	alc_fix_pll(codec);
	alc_auto_init_amp(codec, spec->init_amp);

	for (i = 0; i < spec->num_init_verbs; i++)
		snd_hda_sequence_write(codec, spec->init_verbs[i]);
	alc_init_special_input_src(codec);

	if (spec->init_hook)
		spec->init_hook(codec);

	alc_apply_fixup(codec, ALC_FIXUP_ACT_INIT);

	hda_call_check_power_status(codec, 0x01);
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

static int alc880_dig_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
					   struct hda_codec *codec,
					   struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_cleanup(codec, &spec->multiout);
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

/* analog capture with dynamic dual-adc changes */
static int dualmic_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       unsigned int stream_tag,
				       unsigned int format,
				       struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	spec->cur_adc = spec->adc_nids[spec->cur_adc_idx];
	spec->cur_adc_stream_tag = stream_tag;
	spec->cur_adc_format = format;
	snd_hda_codec_setup_stream(codec, spec->cur_adc, stream_tag, 0, format);
	return 0;
}

static int dualmic_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				       struct hda_codec *codec,
				       struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;
	snd_hda_codec_cleanup_stream(codec, spec->cur_adc);
	spec->cur_adc = 0;
	return 0;
}

static const struct hda_pcm_stream dualmic_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.nid = 0, /* fill later */
	.ops = {
		.prepare = dualmic_capture_pcm_prepare,
		.cleanup = dualmic_capture_pcm_cleanup
	},
};

/*
 */
static const struct hda_pcm_stream alc880_pcm_analog_playback = {
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

static const struct hda_pcm_stream alc880_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

static const struct hda_pcm_stream alc880_pcm_analog_alt_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

static const struct hda_pcm_stream alc880_pcm_analog_alt_capture = {
	.substreams = 2, /* can be overridden */
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
	.ops = {
		.prepare = alc880_alt_capture_pcm_prepare,
		.cleanup = alc880_alt_capture_pcm_cleanup
	},
};

static const struct hda_pcm_stream alc880_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
	.ops = {
		.open = alc880_dig_playback_pcm_open,
		.close = alc880_dig_playback_pcm_close,
		.prepare = alc880_dig_playback_pcm_prepare,
		.cleanup = alc880_dig_playback_pcm_cleanup
	},
};

static const struct hda_pcm_stream alc880_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
};

/* Used by alc_build_pcms to flag that a PCM has no playback stream */
static const struct hda_pcm_stream alc_pcm_null_stream = {
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

	if (spec->no_analog)
		goto skip_analog;

	snprintf(spec->stream_name_analog, sizeof(spec->stream_name_analog),
		 "%s Analog", codec->chip_name);
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

 skip_analog:
	/* SPDIF for stream index #1 */
	if (spec->multiout.dig_out_nid || spec->dig_in_nid) {
		snprintf(spec->stream_name_digital,
			 sizeof(spec->stream_name_digital),
			 "%s Digital", codec->chip_name);
		codec->num_pcms = 2;
	        codec->slave_dig_outs = spec->multiout.slave_dig_outs;
		info = spec->pcm_rec + 1;
		info->name = spec->stream_name_digital;
		if (spec->dig_out_type)
			info->pcm_type = spec->dig_out_type;
		else
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

	if (spec->no_analog)
		return 0;

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
		if (spec->num_adc_nids > 1 && spec->stream_analog_alt_capture) {
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

static inline void alc_shutup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec && spec->shutup)
		spec->shutup(codec);
	snd_hda_shutup_pins(codec);
}

static void alc_free_kctls(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (spec->kctls.list) {
		struct snd_kcontrol_new *kctl = spec->kctls.list;
		int i;
		for (i = 0; i < spec->kctls.used; i++)
			kfree(kctl[i].name);
	}
	snd_array_free(&spec->kctls);
}

static void alc_free(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (!spec)
		return;

	alc_shutup(codec);
	snd_hda_input_jack_free(codec);
	alc_free_kctls(codec);
	kfree(spec);
	snd_hda_detach_beep_device(codec);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static void alc_power_eapd(struct hda_codec *codec)
{
	alc_auto_setup_eapd(codec, false);
}

static int alc_suspend(struct hda_codec *codec, pm_message_t state)
{
	struct alc_spec *spec = codec->spec;
	alc_shutup(codec);
	if (spec && spec->power_hook)
		spec->power_hook(codec);
	return 0;
}
#endif

#ifdef SND_HDA_NEEDS_RESUME
static int alc_resume(struct hda_codec *codec)
{
	msleep(150); /* to avoid pop noise */
	codec->patch_ops.init(codec);
	snd_hda_codec_resume_amp(codec);
	snd_hda_codec_resume_cache(codec);
	hda_call_check_power_status(codec, 0x01);
	return 0;
}
#endif

/*
 */
static const struct hda_codec_ops alc_patch_ops = {
	.build_controls = alc_build_controls,
	.build_pcms = alc_build_pcms,
	.init = alc_init,
	.free = alc_free,
	.unsol_event = alc_unsol_event,
#ifdef SND_HDA_NEEDS_RESUME
	.resume = alc_resume,
#endif
#ifdef CONFIG_SND_HDA_POWER_SAVE
	.suspend = alc_suspend,
	.check_power_status = alc_check_power_status,
#endif
	.reboot_notify = alc_shutup,
};

/* replace the codec chip_name with the given string */
static int alc_codec_rename(struct hda_codec *codec, const char *name)
{
	kfree(codec->chip_name);
	codec->chip_name = kstrdup(name, GFP_KERNEL);
	if (!codec->chip_name) {
		alc_free(codec);
		return -ENOMEM;
	}
	return 0;
}

/*
 * Test configuration for debugging
 *
 * Almost all inputs/outputs are enabled.  I/O pins can be configured via
 * enum controls.
 */
#ifdef CONFIG_SND_DEBUG
static const hda_nid_t alc880_test_dac_nids[4] = {
	0x02, 0x03, 0x04, 0x05
};

static const struct hda_input_mux alc880_test_capture_source = {
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

static const struct hda_channel_mode alc880_test_modes[4] = {
	{ 2, NULL },
	{ 4, NULL },
	{ 6, NULL },
	{ 8, NULL },
};

static int alc_test_pin_ctl_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = {
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
	static const unsigned int ctls[] = {
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
	static const char * const texts[] = {
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
			.subdevice = HDA_SUBDEV_NID_FLAG | nid, \
			.info = alc_test_pin_ctl_info, \
			.get = alc_test_pin_ctl_get,   \
			.put = alc_test_pin_ctl_put,   \
			.private_value = nid	       \
			}

#define PIN_SRC_TEST(xname,nid) {			\
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,	\
			.name = xname,		       \
			.subdevice = HDA_SUBDEV_NID_FLAG | nid, \
			.info = alc_test_pin_src_info, \
			.get = alc_test_pin_src_get,   \
			.put = alc_test_pin_src_put,   \
			.private_value = nid	       \
			}

static const struct snd_kcontrol_new alc880_test_mixer[] = {
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

static const struct hda_verb alc880_test_init_verbs[] = {
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

static const char * const alc880_models[ALC880_MODEL_LAST] = {
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

static const struct snd_pci_quirk alc880_cfg_tbl[] = {
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
	SND_PCI_QUIRK_VENDOR(0x1043, "ASUS", ALC880_ASUS), /* default ASUS */
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
	SND_PCI_QUIRK(0x1584, 0x9054, "Uniwill", ALC880_F1734),
	SND_PCI_QUIRK(0x1584, 0x9070, "Uniwill", ALC880_UNIWILL),
	SND_PCI_QUIRK(0x1584, 0x9077, "Uniwill P53", ALC880_UNIWILL_P53),
	SND_PCI_QUIRK(0x161f, 0x203d, "W810", ALC880_W810),
	SND_PCI_QUIRK(0x161f, 0x205d, "Medion Rim 2150", ALC880_MEDION_RIM),
	SND_PCI_QUIRK(0x1695, 0x400d, "EPoX", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x1695, 0x4012, "EPox EP-5LDA", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0x1734, 0x107c, "FSC F1734", ALC880_F1734),
	SND_PCI_QUIRK(0x1734, 0x1094, "FSC Amilo M1451G", ALC880_FUJITSU),
	SND_PCI_QUIRK(0x1734, 0x10ac, "FSC AMILO Xi 1526", ALC880_F1734),
	SND_PCI_QUIRK(0x1734, 0x10b0, "Fujitsu", ALC880_FUJITSU),
	SND_PCI_QUIRK(0x1854, 0x0018, "LG LW20", ALC880_LG_LW),
	SND_PCI_QUIRK(0x1854, 0x003b, "LG", ALC880_LG),
	SND_PCI_QUIRK(0x1854, 0x005f, "LG P1 Express", ALC880_LG),
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
	/* default Intel */
	SND_PCI_QUIRK_VENDOR(0x8086, "Intel mobo", ALC880_3ST),
	SND_PCI_QUIRK(0xa0a0, 0x0560, "AOpen i915GMm-HFS", ALC880_5ST_DIG),
	SND_PCI_QUIRK(0xe803, 0x1019, NULL, ALC880_6ST_DIG),
	{}
};

/*
 * ALC880 codec presets
 */
static const struct alc_config_preset alc880_presets[] = {
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
		.adc_nids = alc880_adc_nids_alt, /* FIXME: correct? */
		.num_adc_nids = 1, /* single ADC */
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
		.setup = alc880_uniwill_p53_setup,
		.init_hook = alc_hp_automute,
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
		.mixers = { alc880_asus_mixer },
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
		.setup = alc880_uniwill_setup,
		.init_hook = alc880_uniwill_init_hook,
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
		.setup = alc880_uniwill_p53_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC880_FUJITSU] = {
		.mixers = { alc880_fujitsu_mixer },
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
		.setup = alc880_uniwill_p53_setup,
		.init_hook = alc_hp_automute,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc880_lg_setup,
		.init_hook = alc_hp_automute,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc880_lg_lw_setup,
		.init_hook = alc_hp_automute,
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
		.setup = alc880_medion_rim_setup,
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

enum {
	ALC_CTL_WIDGET_VOL,
	ALC_CTL_WIDGET_MUTE,
	ALC_CTL_BIND_MUTE,
};
static const struct snd_kcontrol_new alc880_control_templates[] = {
	HDA_CODEC_VOLUME(NULL, 0, 0, 0),
	HDA_CODEC_MUTE(NULL, 0, 0, 0),
	HDA_BIND_MUTE(NULL, 0, 0, 0),
};

static struct snd_kcontrol_new *alc_kcontrol_new(struct alc_spec *spec)
{
	snd_array_init(&spec->kctls, sizeof(struct snd_kcontrol_new), 32);
	return snd_array_new(&spec->kctls);
}

/* add dynamic controls */
static int add_control(struct alc_spec *spec, int type, const char *name,
		       int cidx, unsigned long val)
{
	struct snd_kcontrol_new *knew;

	knew = alc_kcontrol_new(spec);
	if (!knew)
		return -ENOMEM;
	*knew = alc880_control_templates[type];
	knew->name = kstrdup(name, GFP_KERNEL);
	if (!knew->name)
		return -ENOMEM;
	knew->index = cidx;
	if (get_amp_nid_(val))
		knew->subdevice = HDA_SUBDEV_AMP_FLAG;
	knew->private_value = val;
	return 0;
}

static int add_control_with_pfx(struct alc_spec *spec, int type,
				const char *pfx, const char *dir,
				const char *sfx, int cidx, unsigned long val)
{
	char name[32];
	snprintf(name, sizeof(name), "%s %s %s", pfx, dir, sfx);
	return add_control(spec, type, name, cidx, val);
}

#define add_pb_vol_ctrl(spec, type, pfx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Volume", 0, val)
#define add_pb_sw_ctrl(spec, type, pfx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Switch", 0, val)
#define __add_pb_vol_ctrl(spec, type, pfx, cidx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Volume", cidx, val)
#define __add_pb_sw_ctrl(spec, type, pfx, cidx, val)			\
	add_control_with_pfx(spec, type, pfx, "Playback", "Switch", cidx, val)

#define alc880_is_fixed_pin(nid)	((nid) >= 0x14 && (nid) <= 0x17)
#define alc880_fixed_pin_idx(nid)	((nid) - 0x14)
#define alc880_is_multi_pin(nid)	((nid) >= 0x18)
#define alc880_multi_pin_idx(nid)	((nid) - 0x18)
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
			spec->private_dac_nids[i] = alc880_idx_to_dac(idx);
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
				spec->private_dac_nids[i] =
					alc880_idx_to_dac(j);
				assigned[j] = 1;
				break;
			}
		}
	}
	spec->multiout.num_dacs = cfg->line_outs;
	return 0;
}

static const char *alc_get_line_out_pfx(struct alc_spec *spec,
					bool can_be_master)
{
	struct auto_pin_cfg *cfg = &spec->autocfg;

	if (cfg->line_outs == 1 && !spec->multi_ios &&
	    !cfg->hp_outs && !cfg->speaker_outs && can_be_master)
		return "Master";

	switch (cfg->line_out_type) {
	case AUTO_PIN_SPEAKER_OUT:
		if (cfg->line_outs == 1)
			return "Speaker";
		break;
	case AUTO_PIN_HP_OUT:
		return "Headphone";
	default:
		if (cfg->line_outs == 1 && !spec->multi_ios)
			return "PCM";
		break;
	}
	return NULL;
}

/* add playback controls from the parsed DAC table */
static int alc880_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	static const char * const chname[4] = {
		"Front", "Surround", NULL /*CLFE*/, "Side"
	};
	const char *pfx = alc_get_line_out_pfx(spec, false);
	hda_nid_t nid;
	int i, err, noutputs;

	noutputs = cfg->line_outs;
	if (spec->multi_ios > 0)
		noutputs += spec->multi_ios;

	for (i = 0; i < noutputs; i++) {
		if (!spec->multiout.dac_nids[i])
			continue;
		nid = alc880_idx_to_mixer(alc880_dac_to_idx(spec->multiout.dac_nids[i]));
		if (!pfx && i == 2) {
			/* Center/LFE */
			err = add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL,
					      "Center",
					  HDA_COMPOSE_AMP_VAL(nid, 1, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL,
					      "LFE",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_pb_sw_ctrl(spec, ALC_CTL_BIND_MUTE,
					     "Center",
					  HDA_COMPOSE_AMP_VAL(nid, 1, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
			err = add_pb_sw_ctrl(spec, ALC_CTL_BIND_MUTE,
					     "LFE",
					  HDA_COMPOSE_AMP_VAL(nid, 2, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
		} else {
			const char *name = pfx;
			int index = i;
			if (!name) {
				name = chname[i];
				index = 0;
			}
			err = __add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL,
						name, index,
					  HDA_COMPOSE_AMP_VAL(nid, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = __add_pb_sw_ctrl(spec, ALC_CTL_BIND_MUTE,
					       name, index,
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
		err = add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL, pfx,
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		err = add_pb_sw_ctrl(spec, ALC_CTL_BIND_MUTE, pfx,
				  HDA_COMPOSE_AMP_VAL(nid, 3, 2, HDA_INPUT));
		if (err < 0)
			return err;
	} else if (alc880_is_multi_pin(pin)) {
		/* set manual connection */
		/* we have only a switch on HP-out PIN */
		err = add_pb_sw_ctrl(spec, ALC_CTL_WIDGET_MUTE, pfx,
				  HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
	}
	return 0;
}

/* create input playback/capture controls for the given pin */
static int new_analog_input(struct alc_spec *spec, hda_nid_t pin,
			    const char *ctlname, int ctlidx,
			    int idx, hda_nid_t mix_nid)
{
	int err;

	err = __add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL, ctlname, ctlidx,
			  HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT));
	if (err < 0)
		return err;
	err = __add_pb_sw_ctrl(spec, ALC_CTL_WIDGET_MUTE, ctlname, ctlidx,
			  HDA_COMPOSE_AMP_VAL(mix_nid, 3, idx, HDA_INPUT));
	if (err < 0)
		return err;
	return 0;
}

static int alc_is_input_pin(struct hda_codec *codec, hda_nid_t nid)
{
	unsigned int pincap = snd_hda_query_pin_caps(codec, nid);
	return (pincap & AC_PINCAP_IN) != 0;
}

/* create playback/capture controls for input pins */
static int alc_auto_create_input_ctls(struct hda_codec *codec,
				      const struct auto_pin_cfg *cfg,
				      hda_nid_t mixer,
				      hda_nid_t cap1, hda_nid_t cap2)
{
	struct alc_spec *spec = codec->spec;
	struct hda_input_mux *imux = &spec->private_imux[0];
	int i, err, idx, type_idx = 0;
	const char *prev_label = NULL;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t pin;
		const char *label;

		pin = cfg->inputs[i].pin;
		if (!alc_is_input_pin(codec, pin))
			continue;

		label = hda_get_autocfg_input_label(codec, cfg, i);
		if (prev_label && !strcmp(label, prev_label))
			type_idx++;
		else
			type_idx = 0;
		prev_label = label;

		if (mixer) {
			idx = get_connection_index(codec, mixer, pin);
			if (idx >= 0) {
				err = new_analog_input(spec, pin,
						       label, type_idx,
						       idx, mixer);
				if (err < 0)
					return err;
			}
		}

		if (!cap1)
			continue;
		idx = get_connection_index(codec, cap1, pin);
		if (idx < 0 && cap2)
			idx = get_connection_index(codec, cap2, pin);
		if (idx >= 0)
			snd_hda_add_imux_item(imux, label, idx, NULL);
	}
	return 0;
}

static int alc880_auto_create_input_ctls(struct hda_codec *codec,
						const struct auto_pin_cfg *cfg)
{
	return alc_auto_create_input_ctls(codec, cfg, 0x0b, 0x08, 0x09);
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
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		if (alc_is_input_pin(codec, nid)) {
			alc_set_input_pin(codec, nid, cfg->inputs[i].type);
			if (nid != ALC880_PIN_CD_NID &&
			    (get_wcaps(codec, nid) & AC_WCAP_OUT_AMP))
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_AMP_GAIN_MUTE,
						    AMP_OUT_MUTE);
		}
	}
}

static void alc880_auto_init_input_src(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int c;

	for (c = 0; c < spec->num_adc_nids; c++) {
		unsigned int mux_idx;
		const struct hda_input_mux *imux;
		mux_idx = c >= spec->num_mux_defs ? 0 : c;
		imux = &spec->input_mux[mux_idx];
		if (!imux->num_items && mux_idx > 0)
			imux = &spec->input_mux[0];
		if (imux)
			snd_hda_codec_write(codec, spec->adc_nids[c], 0,
					    AC_VERB_SET_CONNECT_SEL,
					    imux->items[0].index);
	}
}

static int alc_auto_add_multi_channel_mode(struct hda_codec *codec);

/* parse the BIOS configuration and set up the alc_spec */
/* return 1 if successful, 0 if the proper config is not found,
 * or a negative error code
 */
static int alc880_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static const hda_nid_t alc880_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc880_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */

	err = alc880_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc_auto_add_multi_channel_mode(codec);
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
	err = alc880_auto_create_input_ctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	alc_auto_parse_digital(codec);

	if (spec->kctls.list)
		add_mixer(spec, spec->kctls.list);

	add_verb(spec, alc880_volume_init_verbs);

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux[0];

	alc_ssid_check(codec, 0x15, 0x1b, 0x14, 0);

	return 1;
}

/* additional initialization for auto-configuration model */
static void alc880_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc880_auto_init_multi_out(codec);
	alc880_auto_init_extra_out(codec);
	alc880_auto_init_analog_input(codec);
	alc880_auto_init_input_src(codec);
	alc_auto_init_digital(codec);
	if (spec->unsol_event)
		alc_inithook(codec);
}

/* check the ADC/MUX contains all input pins; some ADC/MUX contains only
 * one of two digital mic pins, e.g. on ALC272
 */
static void fixup_automic_adc(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->num_adc_nids; i++) {
		hda_nid_t cap = spec->capsrc_nids ?
			spec->capsrc_nids[i] : spec->adc_nids[i];
		int iidx, eidx;

		iidx = get_connection_index(codec, cap, spec->int_mic.pin);
		if (iidx < 0)
			continue;
		eidx = get_connection_index(codec, cap, spec->ext_mic.pin);
		if (eidx < 0)
			continue;
		spec->int_mic.mux_idx = iidx;
		spec->ext_mic.mux_idx = eidx;
		if (spec->capsrc_nids)
			spec->capsrc_nids += i;
		spec->adc_nids += i;
		spec->num_adc_nids = 1;
		/* optional dock-mic */
		eidx = get_connection_index(codec, cap, spec->dock_mic.pin);
		if (eidx < 0)
			spec->dock_mic.pin = 0;
		else
			spec->dock_mic.mux_idx = eidx;
		return;
	}
	snd_printd(KERN_INFO "hda_codec: %s: "
		   "No ADC/MUX containing both 0x%x and 0x%x pins\n",
		   codec->chip_name, spec->int_mic.pin, spec->ext_mic.pin);
	spec->auto_mic = 0; /* disable auto-mic to be sure */
}

/* select or unmute the given capsrc route */
static void select_or_unmute_capsrc(struct hda_codec *codec, hda_nid_t cap,
				    int idx)
{
	if (get_wcaps_type(get_wcaps(codec, cap)) == AC_WID_AUD_MIX) {
		snd_hda_codec_amp_stereo(codec, cap, HDA_INPUT, idx,
					 HDA_AMP_MUTE, 0);
	} else {
		snd_hda_codec_write_cache(codec, cap, 0,
					  AC_VERB_SET_CONNECT_SEL, idx);
	}
}

/* set the default connection to that pin */
static int init_capsrc_for_pin(struct hda_codec *codec, hda_nid_t pin)
{
	struct alc_spec *spec = codec->spec;
	int i;

	if (!pin)
		return 0;
	for (i = 0; i < spec->num_adc_nids; i++) {
		hda_nid_t cap = spec->capsrc_nids ?
			spec->capsrc_nids[i] : spec->adc_nids[i];
		int idx;

		idx = get_connection_index(codec, cap, pin);
		if (idx < 0)
			continue;
		select_or_unmute_capsrc(codec, cap, idx);
		return i; /* return the found index */
	}
	return -1; /* not found */
}

/* choose the ADC/MUX containing the input pin and initialize the setup */
static void fixup_single_adc(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	/* search for the input pin; there must be only one */
	if (cfg->num_inputs != 1)
		return;
	i = init_capsrc_for_pin(codec, cfg->inputs[0].pin);
	if (i >= 0) {
		/* use only this ADC */
		if (spec->capsrc_nids)
			spec->capsrc_nids += i;
		spec->adc_nids += i;
		spec->num_adc_nids = 1;
		spec->single_input_src = 1;
	}
}

/* initialize dual adcs */
static void fixup_dual_adc_switch(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	init_capsrc_for_pin(codec, spec->ext_mic.pin);
	init_capsrc_for_pin(codec, spec->dock_mic.pin);
	init_capsrc_for_pin(codec, spec->int_mic.pin);
}

/* initialize some special cases for input sources */
static void alc_init_special_input_src(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	if (spec->dual_adc_switch)
		fixup_dual_adc_switch(codec);
	else if (spec->single_input_src)
		init_capsrc_for_pin(codec, spec->autocfg.inputs[0].pin);
}

static void set_capture_mixer(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	static const struct snd_kcontrol_new *caps[2][3] = {
		{ alc_capture_mixer_nosrc1,
		  alc_capture_mixer_nosrc2,
		  alc_capture_mixer_nosrc3 },
		{ alc_capture_mixer1,
		  alc_capture_mixer2,
		  alc_capture_mixer3 },
	};
	if (spec->num_adc_nids > 0 && spec->num_adc_nids <= 3) {
		int mux = 0;
		int num_adcs = spec->num_adc_nids;
		if (spec->dual_adc_switch)
			num_adcs = 1;
		else if (spec->auto_mic)
			fixup_automic_adc(codec);
		else if (spec->input_mux) {
			if (spec->input_mux->num_items > 1)
				mux = 1;
			else if (spec->input_mux->num_items == 1)
				fixup_single_adc(codec);
		}
		spec->cap_mixer = caps[mux][num_adcs - 1];
	}
}

/* fill adc_nids (and capsrc_nids) containing all active input pins */
static void fillup_priv_adc_nids(struct hda_codec *codec, const hda_nid_t *nids,
				 int num_nids)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int n;
	hda_nid_t fallback_adc = 0, fallback_cap = 0;

	for (n = 0; n < num_nids; n++) {
		hda_nid_t adc, cap;
		hda_nid_t conn[HDA_MAX_NUM_INPUTS];
		int nconns, i, j;

		adc = nids[n];
		if (get_wcaps_type(get_wcaps(codec, adc)) != AC_WID_AUD_IN)
			continue;
		cap = adc;
		nconns = snd_hda_get_connections(codec, cap, conn,
						 ARRAY_SIZE(conn));
		if (nconns == 1) {
			cap = conn[0];
			nconns = snd_hda_get_connections(codec, cap, conn,
							 ARRAY_SIZE(conn));
		}
		if (nconns <= 0)
			continue;
		if (!fallback_adc) {
			fallback_adc = adc;
			fallback_cap = cap;
		}
		for (i = 0; i < cfg->num_inputs; i++) {
			hda_nid_t nid = cfg->inputs[i].pin;
			for (j = 0; j < nconns; j++) {
				if (conn[j] == nid)
					break;
			}
			if (j >= nconns)
				break;
		}
		if (i >= cfg->num_inputs) {
			int num_adcs = spec->num_adc_nids;
			spec->private_adc_nids[num_adcs] = adc;
			spec->private_capsrc_nids[num_adcs] = cap;
			spec->num_adc_nids++;
			spec->adc_nids = spec->private_adc_nids;
			if (adc != cap)
				spec->capsrc_nids = spec->private_capsrc_nids;
		}
	}
	if (!spec->num_adc_nids) {
		printk(KERN_WARNING "hda_codec: %s: no valid ADC found;"
		       " using fallback 0x%x\n",
		       codec->chip_name, fallback_adc);
		spec->private_adc_nids[0] = fallback_adc;
		spec->adc_nids = spec->private_adc_nids;
		if (fallback_adc != fallback_cap) {
			spec->private_capsrc_nids[0] = fallback_cap;
			spec->capsrc_nids = spec->private_adc_nids;
		}
	}
}

#ifdef CONFIG_SND_HDA_INPUT_BEEP
#define set_beep_amp(spec, nid, idx, dir) \
	((spec)->beep_amp = HDA_COMPOSE_AMP_VAL(nid, 3, idx, dir))

static const struct snd_pci_quirk beep_white_list[] = {
	SND_PCI_QUIRK(0x1043, 0x829f, "ASUS", 1),
	SND_PCI_QUIRK(0x1043, 0x83ce, "EeePC", 1),
	SND_PCI_QUIRK(0x1043, 0x831a, "EeePC", 1),
	SND_PCI_QUIRK(0x1043, 0x834a, "EeePC", 1),
	SND_PCI_QUIRK(0x8086, 0xd613, "Intel", 1),
	{}
};

static inline int has_cdefine_beep(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	const struct snd_pci_quirk *q;
	q = snd_pci_quirk_lookup(codec->bus->pci, beep_white_list);
	if (q)
		return q->value;
	return spec->cdefine.enable_pcbeep;
}
#else
#define set_beep_amp(spec, nid, idx, dir) /* NOP */
#define has_cdefine_beep(codec)		0
#endif

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
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
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

	err = snd_hda_attach_beep_device(codec, 0x1);
	if (err < 0) {
		alc_free(codec);
		return err;
	}

	if (board_config != ALC880_AUTO)
		setup_preset(codec, &alc880_presets[board_config]);

	spec->stream_analog_playback = &alc880_pcm_analog_playback;
	spec->stream_analog_capture = &alc880_pcm_analog_capture;
	spec->stream_analog_alt_capture = &alc880_pcm_analog_alt_capture;

	spec->stream_digital_playback = &alc880_pcm_digital_playback;
	spec->stream_digital_capture = &alc880_pcm_digital_capture;

	if (!spec->adc_nids && spec->input_mux) {
		/* check whether NID 0x07 is valid */
		unsigned int wcap = get_wcaps(codec, alc880_adc_nids[0]);
		/* get type */
		wcap = get_wcaps_type(wcap);
		if (wcap != AC_WID_AUD_IN) {
			spec->adc_nids = alc880_adc_nids_alt;
			spec->num_adc_nids = ARRAY_SIZE(alc880_adc_nids_alt);
		} else {
			spec->adc_nids = alc880_adc_nids;
			spec->num_adc_nids = ARRAY_SIZE(alc880_adc_nids);
		}
	}
	set_capture_mixer(codec);
	set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);

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

static const hda_nid_t alc260_dac_nids[1] = {
	/* front */
	0x02,
};

static const hda_nid_t alc260_adc_nids[1] = {
	/* ADC0 */
	0x04,
};

static const hda_nid_t alc260_adc_nids_alt[1] = {
	/* ADC1 */
	0x05,
};

/* NIDs used when simultaneous access to both ADCs makes sense.  Note that
 * alc260_capture_mixer assumes ADC0 (nid 0x04) is the first ADC.
 */
static const hda_nid_t alc260_dual_adc_nids[2] = {
	/* ADC0, ADC1 */
	0x04, 0x05
};

#define ALC260_DIGOUT_NID	0x03
#define ALC260_DIGIN_NID	0x06

static const struct hda_input_mux alc260_capture_source = {
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
static const struct hda_input_mux alc260_fujitsu_capture_sources[2] = {
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
static const struct hda_input_mux alc260_acer_capture_sources[2] = {
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

/* Maxdata Favorit 100XS */
static const struct hda_input_mux alc260_favorit100_capture_sources[2] = {
	{
		.num_items = 2,
		.items = {
			{ "Line/Mic", 0x0 },
			{ "CD", 0x4 },
		},
	},
	{
		.num_items = 3,
		.items = {
			{ "Line/Mic", 0x0 },
			{ "CD", 0x4 },
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
static const struct hda_channel_mode alc260_modes[1] = {
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

static const struct snd_kcontrol_new alc260_base_output_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x08, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x09, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x09, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Mono Playback Volume", 0x0a, 1, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Mono Playback Switch", 0x0a, 1, 2, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc260_input_mixer[] = {
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

/* update HP, line and mono out pins according to the master switch */
static void alc260_hp_master_update(struct hda_codec *codec)
{
	update_speakers(codec);
}

static int alc260_hp_master_sw_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	*ucontrol->value.integer.value = !spec->master_mute;
	return 0;
}

static int alc260_hp_master_sw_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int val = !*ucontrol->value.integer.value;

	if (val == spec->master_mute)
		return 0;
	spec->master_mute = val;
	alc260_hp_master_update(codec);
	return 1;
}

static const struct snd_kcontrol_new alc260_hp_output_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.subdevice = HDA_SUBDEV_NID_FLAG | 0x11,
		.info = snd_ctl_boolean_mono_info,
		.get = alc260_hp_master_sw_get,
		.put = alc260_hp_master_sw_put,
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

static const struct hda_verb alc260_hp_unsol_verbs[] = {
	{0x10, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{},
};

static void alc260_hp_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x0f;
	spec->autocfg.speaker_pins[0] = 0x10;
	spec->autocfg.speaker_pins[1] = 0x11;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_PIN;
}

static const struct snd_kcontrol_new alc260_hp_3013_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.subdevice = HDA_SUBDEV_NID_FLAG | 0x11,
		.info = snd_ctl_boolean_mono_info,
		.get = alc260_hp_master_sw_get,
		.put = alc260_hp_master_sw_put,
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

static void alc260_hp_3013_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x10;
	spec->autocfg.speaker_pins[1] = 0x11;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_PIN;
}

static const struct hda_bind_ctls alc260_dc7600_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x08, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x09, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x0a, 3, 0, HDA_OUTPUT),
		0
	},
};

static const struct hda_bind_ctls alc260_dc7600_bind_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x11, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x15, 3, 0, HDA_OUTPUT),
		0
	},
};

static const struct snd_kcontrol_new alc260_hp_dc7600_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc260_dc7600_bind_master_vol),
	HDA_BIND_SW("LineOut Playback Switch", &alc260_dc7600_bind_switch),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x0f, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x10, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static const struct hda_verb alc260_hp_3013_unsol_verbs[] = {
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{},
};

static void alc260_hp_3012_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x10;
	spec->autocfg.speaker_pins[0] = 0x0f;
	spec->autocfg.speaker_pins[1] = 0x11;
	spec->autocfg.speaker_pins[2] = 0x15;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_PIN;
}

/* Fujitsu S702x series laptops.  ALC260 pin usage: Mic/Line jack = 0x12,
 * HP jack = 0x14, CD audio =  0x16, internal speaker = 0x10.
 */
static const struct snd_kcontrol_new alc260_fujitsu_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x08, 2, HDA_INPUT),
	ALC_PIN_MODE("Headphone Jack Mode", 0x14, ALC_PIN_DIR_INOUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x07, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic/Line Playback Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic/Line Playback Switch", 0x07, 0x0, HDA_INPUT),
	ALC_PIN_MODE("Mic/Line Jack Mode", 0x12, ALC_PIN_DIR_IN),
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
static const struct snd_kcontrol_new alc260_acer_mixer[] = {
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
	{ } /* end */
};

/* Maxdata Favorit 100XS: one output and one input (0x12) jack
 */
static const struct snd_kcontrol_new alc260_favorit100_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x08, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Master Playback Switch", 0x08, 2, HDA_INPUT),
	ALC_PIN_MODE("Output Jack Mode", 0x0f, ALC_PIN_DIR_INOUT),
	HDA_CODEC_VOLUME("Line/Mic Playback Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Line/Mic Playback Switch", 0x07, 0x0, HDA_INPUT),
	ALC_PIN_MODE("Line/Mic Jack Mode", 0x12, ALC_PIN_DIR_IN),
	{ } /* end */
};

/* Packard bell V7900  ALC260 pin usage: HP = 0x0f, Mic jack = 0x12,
 * Line In jack = 0x14, CD audio =  0x16, pc beep = 0x17.
 */
static const struct snd_kcontrol_new alc260_will_mixer[] = {
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
	{ } /* end */
};

/* Replacer 672V ALC260 pin usage: Mic jack = 0x12,
 * Line In jack = 0x14, ATAPI Mic = 0x13, speaker = 0x0f.
 */
static const struct snd_kcontrol_new alc260_replacer_672v_mixer[] = {
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

/*
 * initialization verbs
 */
static const struct hda_verb alc260_init_verbs[] = {
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
static const struct hda_verb alc260_hp_init_verbs[] = {
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

static const struct hda_verb alc260_hp_3013_init_verbs[] = {
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
static const struct hda_verb alc260_fujitsu_init_verbs[] = {
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
static const struct hda_verb alc260_acer_init_verbs[] = {
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

/* Initialisation sequence for Maxdata Favorit 100XS
 * (adapted from Acer init verbs).
 */
static const struct hda_verb alc260_favorit100_init_verbs[] = {
	/* GPIO 0 enables the output jack.
	 * Turn this on and rely on the standard mute
	 * methods whenever the user wants to turn these outputs off.
	 */
	{0x01, AC_VERB_SET_GPIO_MASK, 0x01},
	{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01},
	{0x01, AC_VERB_SET_GPIO_DATA, 0x01},
	/* Line/Mic input jack is connected to Mic1 pin */
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50},
	/* Ensure all other unused pins are disabled and muted. */
	{0x10, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x11, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x11, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x13, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
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
	/* Unmute Mic1 and Line1 pin widget input buffers since they start as
	 * inputs. If the pin mode is changed by the user the pin mode control
	 * will take care of enabling the pin's input/output buffers as needed.
	 * Therefore there's no need to enable the input buffer at this
	 * stage.
	 */
	{0x12, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

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

static const struct hda_verb alc260_will_verbs[] = {
	{0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x0b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x0d, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x0f, AC_VERB_SET_EAPD_BTLENABLE, 0x02},
	{0x1a, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x1a, AC_VERB_SET_PROC_COEF, 0x3040},
	{}
};

static const struct hda_verb alc260_replacer_672v_verbs[] = {
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
	present = snd_hda_jack_detect(codec, 0x0f);
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

static const struct hda_verb alc260_hp_dc7600_verbs[] = {
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
static const hda_nid_t alc260_test_dac_nids[1] = {
	0x02,
};
static const hda_nid_t alc260_test_adc_nids[2] = {
	0x04, 0x05,
};
/* For testing the ALC260, each input MUX needs its own definition since
 * the signal assignments are different.  This assumes that the first ADC
 * is NID 0x04.
 */
static const struct hda_input_mux alc260_test_capture_sources[2] = {
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
static const struct snd_kcontrol_new alc260_test_mixer[] = {
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
static const struct hda_verb alc260_test_init_verbs[] = {
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
					const char *pfx, int *vol_bits)
{
	hda_nid_t nid_vol;
	unsigned long vol_val, sw_val;
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

	if (!(*vol_bits & (1 << nid_vol))) {
		/* first control for the volume widget */
		err = add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL, pfx, vol_val);
		if (err < 0)
			return err;
		*vol_bits |= (1 << nid_vol);
	}
	err = add_pb_sw_ctrl(spec, ALC_CTL_WIDGET_MUTE, pfx, sw_val);
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
	int vols = 0;

	spec->multiout.num_dacs = 1;
	spec->multiout.dac_nids = spec->private_dac_nids;
	spec->private_dac_nids[0] = 0x02;

	nid = cfg->line_out_pins[0];
	if (nid) {
		const char *pfx;
		if (!cfg->speaker_pins[0] && !cfg->hp_pins[0])
			pfx = "Master";
		else if (cfg->line_out_type == AUTO_PIN_SPEAKER_OUT)
			pfx = "Speaker";
		else
			pfx = "Front";
		err = alc260_add_playback_controls(spec, nid, pfx, &vols);
		if (err < 0)
			return err;
	}

	nid = cfg->speaker_pins[0];
	if (nid) {
		err = alc260_add_playback_controls(spec, nid, "Speaker", &vols);
		if (err < 0)
			return err;
	}

	nid = cfg->hp_pins[0];
	if (nid) {
		err = alc260_add_playback_controls(spec, nid, "Headphone",
						   &vols);
		if (err < 0)
			return err;
	}
	return 0;
}

/* create playback/capture controls for input pins */
static int alc260_auto_create_input_ctls(struct hda_codec *codec,
						const struct auto_pin_cfg *cfg)
{
	return alc_auto_create_input_ctls(codec, cfg, 0x07, 0x04, 0x05);
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
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		if (nid >= 0x12) {
			alc_set_input_pin(codec, nid, cfg->inputs[i].type);
			if (nid != ALC260_PIN_CD_NID &&
			    (get_wcaps(codec, nid) & AC_WCAP_OUT_AMP))
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_AMP_GAIN_MUTE,
						    AMP_OUT_MUTE);
		}
	}
}

#define alc260_auto_init_input_src	alc880_auto_init_input_src

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static const struct hda_verb alc260_volume_init_verbs[] = {
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
	int err;
	static const hda_nid_t alc260_ignore[] = { 0x17, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc260_ignore);
	if (err < 0)
		return err;
	err = alc260_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	if (!spec->kctls.list)
		return 0; /* can't find valid BIOS pin config */
	err = alc260_auto_create_input_ctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = 2;

	if (spec->autocfg.dig_outs)
		spec->multiout.dig_out_nid = ALC260_DIGOUT_NID;
	if (spec->kctls.list)
		add_mixer(spec, spec->kctls.list);

	add_verb(spec, alc260_volume_init_verbs);

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux[0];

	alc_ssid_check(codec, 0x10, 0x15, 0x0f, 0);

	return 1;
}

/* additional initialization for auto-configuration model */
static void alc260_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc260_auto_init_multi_out(codec);
	alc260_auto_init_analog_input(codec);
	alc260_auto_init_input_src(codec);
	alc_auto_init_digital(codec);
	if (spec->unsol_event)
		alc_inithook(codec);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static const struct hda_amp_list alc260_loopbacks[] = {
	{ 0x07, HDA_INPUT, 0 },
	{ 0x07, HDA_INPUT, 1 },
	{ 0x07, HDA_INPUT, 2 },
	{ 0x07, HDA_INPUT, 3 },
	{ 0x07, HDA_INPUT, 4 },
	{ } /* end */
};
#endif

/*
 * Pin config fixes
 */
enum {
	PINFIX_HP_DC5750,
};

static const struct alc_fixup alc260_fixups[] = {
	[PINFIX_HP_DC5750] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x11, 0x90130110 }, /* speaker */
			{ }
		}
	},
};

static const struct snd_pci_quirk alc260_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x280a, "HP dc5750", PINFIX_HP_DC5750),
	{}
};

/*
 * ALC260 configurations
 */
static const char * const alc260_models[ALC260_MODEL_LAST] = {
	[ALC260_BASIC]		= "basic",
	[ALC260_HP]		= "hp",
	[ALC260_HP_3013]	= "hp-3013",
	[ALC260_HP_DC7600]	= "hp-dc7600",
	[ALC260_FUJITSU_S702X]	= "fujitsu",
	[ALC260_ACER]		= "acer",
	[ALC260_WILL]		= "will",
	[ALC260_REPLACER_672V]	= "replacer",
	[ALC260_FAVORIT100]	= "favorit100",
#ifdef CONFIG_SND_DEBUG
	[ALC260_TEST]		= "test",
#endif
	[ALC260_AUTO]		= "auto",
};

static const struct snd_pci_quirk alc260_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x007b, "Acer C20x", ALC260_ACER),
	SND_PCI_QUIRK(0x1025, 0x007f, "Acer", ALC260_WILL),
	SND_PCI_QUIRK(0x1025, 0x008f, "Acer", ALC260_ACER),
	SND_PCI_QUIRK(0x1509, 0x4540, "Favorit 100XS", ALC260_FAVORIT100),
	SND_PCI_QUIRK(0x103c, 0x2808, "HP d5700", ALC260_HP_3013),
	SND_PCI_QUIRK(0x103c, 0x280a, "HP d5750", ALC260_AUTO), /* no quirk */
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

static const struct alc_config_preset alc260_presets[] = {
	[ALC260_BASIC] = {
		.mixers = { alc260_base_output_mixer,
			    alc260_input_mixer },
		.init_verbs = { alc260_init_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_dual_adc_nids),
		.adc_nids = alc260_dual_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.input_mux = &alc260_capture_source,
	},
	[ALC260_HP] = {
		.mixers = { alc260_hp_output_mixer,
			    alc260_input_mixer },
		.init_verbs = { alc260_init_verbs,
				alc260_hp_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_adc_nids_alt),
		.adc_nids = alc260_adc_nids_alt,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.input_mux = &alc260_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc260_hp_setup,
		.init_hook = alc_inithook,
	},
	[ALC260_HP_DC7600] = {
		.mixers = { alc260_hp_dc7600_mixer,
			    alc260_input_mixer },
		.init_verbs = { alc260_init_verbs,
				alc260_hp_dc7600_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_adc_nids_alt),
		.adc_nids = alc260_adc_nids_alt,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.input_mux = &alc260_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc260_hp_3012_setup,
		.init_hook = alc_inithook,
	},
	[ALC260_HP_3013] = {
		.mixers = { alc260_hp_3013_mixer,
			    alc260_input_mixer },
		.init_verbs = { alc260_hp_3013_init_verbs,
				alc260_hp_3013_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_adc_nids_alt),
		.adc_nids = alc260_adc_nids_alt,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.input_mux = &alc260_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc260_hp_3013_setup,
		.init_hook = alc_inithook,
	},
	[ALC260_FUJITSU_S702X] = {
		.mixers = { alc260_fujitsu_mixer },
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
		.mixers = { alc260_acer_mixer },
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
	[ALC260_FAVORIT100] = {
		.mixers = { alc260_favorit100_mixer },
		.init_verbs = { alc260_favorit100_init_verbs },
		.num_dacs = ARRAY_SIZE(alc260_dac_nids),
		.dac_nids = alc260_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc260_dual_adc_nids),
		.adc_nids = alc260_dual_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc260_modes),
		.channel_mode = alc260_modes,
		.num_mux_defs = ARRAY_SIZE(alc260_favorit100_capture_sources),
		.input_mux = alc260_favorit100_capture_sources,
	},
	[ALC260_WILL] = {
		.mixers = { alc260_will_mixer },
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
		.mixers = { alc260_replacer_672v_mixer },
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
		.mixers = { alc260_test_mixer },
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
		snd_printd(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
			   codec->chip_name);
		board_config = ALC260_AUTO;
	}

	if (board_config == ALC260_AUTO) {
		alc_pick_fixup(codec, NULL, alc260_fixup_tbl, alc260_fixups);
		alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);
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

	err = snd_hda_attach_beep_device(codec, 0x1);
	if (err < 0) {
		alc_free(codec);
		return err;
	}

	if (board_config != ALC260_AUTO)
		setup_preset(codec, &alc260_presets[board_config]);

	spec->stream_analog_playback = &alc260_pcm_analog_playback;
	spec->stream_analog_capture = &alc260_pcm_analog_capture;
	spec->stream_analog_alt_capture = &alc260_pcm_analog_capture;

	spec->stream_digital_playback = &alc260_pcm_digital_playback;
	spec->stream_digital_capture = &alc260_pcm_digital_capture;

	if (!spec->adc_nids && spec->input_mux) {
		/* check whether NID 0x04 is valid */
		unsigned int wcap = get_wcaps(codec, 0x04);
		wcap = get_wcaps_type(wcap);
		/* get type */
		if (wcap != AC_WID_AUD_IN || spec->input_mux->num_items == 1) {
			spec->adc_nids = alc260_adc_nids_alt;
			spec->num_adc_nids = ARRAY_SIZE(alc260_adc_nids_alt);
		} else {
			spec->adc_nids = alc260_adc_nids;
			spec->num_adc_nids = ARRAY_SIZE(alc260_adc_nids);
		}
	}
	set_capture_mixer(codec);
	set_beep_amp(spec, 0x07, 0x05, HDA_INPUT);

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	spec->vmaster_nid = 0x08;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC260_AUTO)
		spec->init_hook = alc260_auto_init;
	spec->shutup = alc_eapd_shutup;
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc260_loopbacks;
#endif

	return 0;
}


/*
 * ALC882/883/885/888/889 support
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
#define ALC883_DIGOUT_NID	ALC882_DIGOUT_NID
#define ALC883_DIGIN_NID	ALC882_DIGIN_NID
#define ALC1200_DIGOUT_NID	0x10


static const struct hda_channel_mode alc882_ch_modes[1] = {
	{ 8, NULL }
};

/* DACs */
static const hda_nid_t alc882_dac_nids[4] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x03, 0x04, 0x05
};
#define alc883_dac_nids		alc882_dac_nids

/* ADCs */
#define alc882_adc_nids		alc880_adc_nids
#define alc882_adc_nids_alt	alc880_adc_nids_alt
#define alc883_adc_nids		alc882_adc_nids_alt
static const hda_nid_t alc883_adc_nids_alt[1] = { 0x08 };
static const hda_nid_t alc883_adc_nids_rev[2] = { 0x09, 0x08 };
#define alc889_adc_nids		alc880_adc_nids

static const hda_nid_t alc882_capsrc_nids[3] = { 0x24, 0x23, 0x22 };
static const hda_nid_t alc882_capsrc_nids_alt[2] = { 0x23, 0x22 };
#define alc883_capsrc_nids	alc882_capsrc_nids_alt
static const hda_nid_t alc883_capsrc_nids_rev[2] = { 0x22, 0x23 };
#define alc889_capsrc_nids	alc882_capsrc_nids

/* input MUX */
/* FIXME: should be a matrix-type input source selection */

static const struct hda_input_mux alc882_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

#define alc883_capture_source	alc882_capture_source

static const struct hda_input_mux alc889_capture_source = {
	.num_items = 3,
	.items = {
		{ "Front Mic", 0x0 },
		{ "Mic", 0x3 },
		{ "Line", 0x2 },
	},
};

static const struct hda_input_mux mb5_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x1 },
		{ "Line", 0x7 },
		{ "CD", 0x4 },
	},
};

static const struct hda_input_mux macmini3_capture_source = {
	.num_items = 2,
	.items = {
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static const struct hda_input_mux alc883_3stack_6ch_intel = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x1 },
		{ "Front Mic", 0x0 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static const struct hda_input_mux alc883_lenovo_101e_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x1 },
		{ "Line", 0x2 },
	},
};

static const struct hda_input_mux alc883_lenovo_nb0763_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static const struct hda_input_mux alc883_fujitsu_pi2515_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x1 },
	},
};

static const struct hda_input_mux alc883_lenovo_sky_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x4 },
	},
};

static const struct hda_input_mux alc883_asus_eee1601_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x0 },
		{ "Line", 0x2 },
	},
};

static const struct hda_input_mux alc889A_mb31_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x0 },
		/* Front Mic (0x01) unused */
		{ "Line", 0x2 },
		/* Line 2 (0x03) unused */
		/* CD (0x04) unused? */
	},
};

static const struct hda_input_mux alc889A_imac91_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x01 },
		{ "Line", 0x2 }, /* Not sure! */
	},
};

/*
 * 2ch mode
 */
static const struct hda_channel_mode alc883_3ST_2ch_modes[1] = {
	{ 2, NULL }
};

/*
 * 2ch mode
 */
static const struct hda_verb alc882_3ST_ch2_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 4ch mode
 */
static const struct hda_verb alc882_3ST_ch4_init[] = {
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
static const struct hda_verb alc882_3ST_ch6_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static const struct hda_channel_mode alc882_3ST_6ch_modes[3] = {
	{ 2, alc882_3ST_ch2_init },
	{ 4, alc882_3ST_ch4_init },
	{ 6, alc882_3ST_ch6_init },
};

#define alc883_3ST_6ch_modes	alc882_3ST_6ch_modes

/*
 * 2ch mode
 */
static const struct hda_verb alc883_3ST_ch2_clevo_init[] = {
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 4ch mode
 */
static const struct hda_verb alc883_3ST_ch4_clevo_init[] = {
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
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
static const struct hda_verb alc883_3ST_ch6_clevo_init[] = {
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static const struct hda_channel_mode alc883_3ST_6ch_clevo_modes[3] = {
	{ 2, alc883_3ST_ch2_clevo_init },
	{ 4, alc883_3ST_ch4_clevo_init },
	{ 6, alc883_3ST_ch6_clevo_init },
};


/*
 * 6ch mode
 */
static const struct hda_verb alc882_sixstack_ch6_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

/*
 * 8ch mode
 */
static const struct hda_verb alc882_sixstack_ch8_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

static const struct hda_channel_mode alc882_sixstack_modes[2] = {
	{ 6, alc882_sixstack_ch6_init },
	{ 8, alc882_sixstack_ch8_init },
};


/* Macbook Air 2,1 */

static const struct hda_channel_mode alc885_mba21_ch_modes[1] = {
      { 2, NULL },
};

/*
 * macbook pro ALC885 can switch LineIn to LineOut without losing Mic
 */

/*
 * 2ch mode
 */
static const struct hda_verb alc885_mbp_ch2_init[] = {
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{ } /* end */
};

/*
 * 4ch mode
 */
static const struct hda_verb alc885_mbp_ch4_init[] = {
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{ 0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{ } /* end */
};

static const struct hda_channel_mode alc885_mbp_4ch_modes[2] = {
	{ 2, alc885_mbp_ch2_init },
	{ 4, alc885_mbp_ch4_init },
};

/*
 * 2ch
 * Speakers/Woofer/HP = Front
 * LineIn = Input
 */
static const struct hda_verb alc885_mb5_ch2_init[] = {
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{ } /* end */
};

/*
 * 6ch mode
 * Speakers/HP = Front
 * Woofer = LFE
 * LineIn = Surround
 */
static const struct hda_verb alc885_mb5_ch6_init[] = {
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{ } /* end */
};

static const struct hda_channel_mode alc885_mb5_6ch_modes[2] = {
	{ 2, alc885_mb5_ch2_init },
	{ 6, alc885_mb5_ch6_init },
};

#define alc885_macmini3_6ch_modes	alc885_mb5_6ch_modes

/*
 * 2ch mode
 */
static const struct hda_verb alc883_4ST_ch2_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 4ch mode
 */
static const struct hda_verb alc883_4ST_ch4_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
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
static const struct hda_verb alc883_4ST_ch6_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

/*
 * 8ch mode
 */
static const struct hda_verb alc883_4ST_ch8_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x03 },
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static const struct hda_channel_mode alc883_4ST_8ch_modes[4] = {
	{ 2, alc883_4ST_ch2_init },
	{ 4, alc883_4ST_ch4_init },
	{ 6, alc883_4ST_ch6_init },
	{ 8, alc883_4ST_ch8_init },
};


/*
 * 2ch mode
 */
static const struct hda_verb alc883_3ST_ch2_intel_init[] = {
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 4ch mode
 */
static const struct hda_verb alc883_3ST_ch4_intel_init[] = {
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
static const struct hda_verb alc883_3ST_ch6_intel_init[] = {
	{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x19, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static const struct hda_channel_mode alc883_3ST_6ch_intel_modes[3] = {
	{ 2, alc883_3ST_ch2_intel_init },
	{ 4, alc883_3ST_ch4_intel_init },
	{ 6, alc883_3ST_ch6_intel_init },
};

/*
 * 2ch mode
 */
static const struct hda_verb alc889_ch2_intel_init[] = {
	{ 0x14, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x19, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x16, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 6ch mode
 */
static const struct hda_verb alc889_ch6_intel_init[] = {
	{ 0x14, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x19, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ 0x16, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x03 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 8ch mode
 */
static const struct hda_verb alc889_ch8_intel_init[] = {
	{ 0x14, AC_VERB_SET_CONNECT_SEL, 0x00 },
	{ 0x19, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ 0x16, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x17, AC_VERB_SET_CONNECT_SEL, 0x03 },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x03 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ } /* end */
};

static const struct hda_channel_mode alc889_8ch_intel_modes[3] = {
	{ 2, alc889_ch2_intel_init },
	{ 6, alc889_ch6_intel_init },
	{ 8, alc889_ch8_intel_init },
};

/*
 * 6ch mode
 */
static const struct hda_verb alc883_sixstack_ch6_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

/*
 * 8ch mode
 */
static const struct hda_verb alc883_sixstack_ch8_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

static const struct hda_channel_mode alc883_sixstack_modes[2] = {
	{ 6, alc883_sixstack_ch6_init },
	{ 8, alc883_sixstack_ch8_init },
};


/* Pin assignment: Front=0x14, Rear=0x15, CLFE=0x16, Side=0x17
 *                 Mic=0x18, Front Mic=0x19, Line-In=0x1a, HP=0x1b
 */
static const struct snd_kcontrol_new alc882_base_mixer[] = {
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
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

/* Macbook Air 2,1 same control for HP and internal Speaker */

static const struct snd_kcontrol_new alc885_mba21_mixer[] = {
      HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
      HDA_BIND_MUTE("Speaker Playback Switch", 0x0c, 0x02, HDA_OUTPUT),
     { }
};


static const struct snd_kcontrol_new alc885_mbp3_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Speaker Playback Switch", 0x0c, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0e, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Headphone Playback Switch", 0x0e, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x00, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE  ("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE  ("Mic Playback Switch", 0x0b, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Boost Volume", 0x1a, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0x00, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc885_mb5_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Front Playback Switch", 0x0c, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Surround Playback Switch", 0x0d, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("LFE Playback Volume", 0x0e, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("LFE Playback Switch", 0x0e, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0f, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Headphone Playback Switch", 0x0f, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x07, HDA_INPUT),
	HDA_CODEC_MUTE  ("Line Playback Switch", 0x0b, 0x07, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE  ("Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Boost Volume", 0x15, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x19, 0x00, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc885_macmini3_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Front Playback Switch", 0x0c, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Surround Playback Switch", 0x0d, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("LFE Playback Volume", 0x0e, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("LFE Playback Switch", 0x0e, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0f, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE   ("Headphone Playback Switch", 0x0f, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x07, HDA_INPUT),
	HDA_CODEC_MUTE  ("Line Playback Switch", 0x0b, 0x07, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Boost Volume", 0x15, 0x00, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc885_imac91_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0c, 0x02, HDA_INPUT),
	{ } /* end */
};


static const struct snd_kcontrol_new alc882_w2jc_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc882_targa_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	{ } /* end */
};

/* Pin assignment: Front=0x14, HP = 0x15, Front = 0x16, ???
 *                 Front Mic=0x18, Line In = 0x1a, Line In = 0x1b, CD = 0x1c
 */
static const struct snd_kcontrol_new alc882_asus_a7j_mixer[] = {
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
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc882_asus_a7m_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc882_chmode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static const struct hda_verb alc882_base_init_verbs[] = {
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Rear mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* CLFE mixer */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Side mixer */
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
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* ADC2: mute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* ADC3: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},

	{ }
};

static const struct hda_verb alc882_adc1_init_verbs[] = {
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* ADC1: mute amp left and right */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	{ }
};

static const struct hda_verb alc882_eapd_verbs[] = {
	/* change to EAPD mode */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF, 0x3060},
	{ }
};

static const struct hda_verb alc889_eapd_verbs[] = {
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

static const struct hda_verb alc_hp15_unsol_verbs[] = {
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

static const struct hda_verb alc885_init_verbs[] = {
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* Rear mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* CLFE mixer */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* Side mixer */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Front HP Pin: output 0 (0x0c) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Front Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* Rear Pin: output 1 (0x0d) */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x19, AC_VERB_SET_CONNECT_SEL, 0x01},
	/* CLFE Pin: output 2 (0x0e) */
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* Side Pin: output 3 (0x0f) */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
	/* Mic (rear) pin: input vref at 80% */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin: input */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	/* Mixer elements: 0x18, , 0x1a, 0x1b */
	/* Input mixer1 */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* ADC2: mute amp left and right */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	/* ADC3: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},

	{ }
};

static const struct hda_verb alc885_init_input_verbs[] = {
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(3)},
	{ }
};


/* Unmute Selector 24h and set the default input to front mic */
static const struct hda_verb alc889_init_input_verbs[] = {
	{0x24, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{ }
};


#define alc883_init_verbs	alc882_base_init_verbs

/* Mac Pro test */
static const struct snd_kcontrol_new alc882_macpro_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x18, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x01, HDA_INPUT),
	/* FIXME: this looks suspicious...
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Beep Playback Switch", 0x0b, 0x02, HDA_INPUT),
	*/
	{ } /* end */
};

static const struct hda_verb alc882_macpro_init_verbs[] = {
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

/* Macbook 5,1 */
static const struct hda_verb alc885_mb5_init_verbs[] = {
	/* DACs */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Front mixer */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Surround mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* LFE mixer */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* HP mixer */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Front Pin (0x0c) */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x01},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* LFE Pin (0x0e) */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x01},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* HP Pin (0x0f) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x03},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Line In pin */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0x1)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0x7)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0x4)},
	{ }
};

/* Macmini 3,1 */
static const struct hda_verb alc885_macmini3_init_verbs[] = {
	/* DACs */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x05, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	/* Front mixer */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Surround mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* LFE mixer */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* HP mixer */
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Front Pin (0x0c) */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x01},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* LFE Pin (0x0e) */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT | 0x01},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x02},
	/* HP Pin (0x0f) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x03},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	/* Line In pin */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	{ }
};


static const struct hda_verb alc885_mba21_init_verbs[] = {
	/*Internal and HP Speaker Mixer*/
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/*Internal Speaker Pin (0x0c)*/
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, (PIN_OUT | AC_PINCTL_VREF_50) },
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* HP Pin: output 0 (0x0e) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc4},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, (ALC880_HP_EVENT | AC_USRSP_EN)},
	/* Line in (is hp when jack connected)*/
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, AC_PINCTL_VREF_50},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{ }
 };


/* Macbook Pro rev3 */
static const struct hda_verb alc885_mbp3_init_verbs[] = {
	/* Front mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Rear mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* HP mixer */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Front Pin: output 0 (0x0c) */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* HP Pin: output 0 (0x0e) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc4},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x02},
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

/* iMac 9,1 */
static const struct hda_verb alc885_imac91_init_verbs[] = {
	/* Internal Speaker Pin (0x0c) */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, (PIN_OUT | AC_PINCTL_VREF_50) },
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, (PIN_OUT | AC_PINCTL_VREF_50) },
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* HP Pin: Rear */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, (ALC880_HP_EVENT | AC_USRSP_EN)},
	/* Line in Rear */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, AC_PINCTL_VREF_50},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Front Mic pin: input vref at 80% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	/* Rear mixer */
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* Line-Out mixer: unmute input/output amp left and right (volume = 0) */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	/* 0x24 [Audio Mixer] wcaps 0x20010b: Stereo Amp-In */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* 0x23 [Audio Mixer] wcaps 0x20010b: Stereo Amp-In */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* 0x22 [Audio Mixer] wcaps 0x20010b: Stereo Amp-In */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(4)},
	/* 0x07 [Audio Input] wcaps 0x10011b: Stereo Amp-In */
	{0x07, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x07, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* 0x08 [Audio Input] wcaps 0x10011b: Stereo Amp-In */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	/* 0x09 [Audio Input] wcaps 0x10011b: Stereo Amp-In */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{ }
};

/* iMac 24 mixer. */
static const struct snd_kcontrol_new alc885_imac24_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("Master Playback Switch", 0x0c, 0x00, HDA_INPUT),
	{ } /* end */
};

/* iMac 24 init verbs. */
static const struct hda_verb alc885_imac24_init_verbs[] = {
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
static void alc885_imac24_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x18;
	spec->autocfg.speaker_pins[1] = 0x1a;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

#define alc885_mb5_setup	alc885_imac24_setup
#define alc885_macmini3_setup	alc885_imac24_setup

/* Macbook Air 2,1 */
static void alc885_mba21_setup(struct hda_codec *codec)
{
       struct alc_spec *spec = codec->spec;

       spec->autocfg.hp_pins[0] = 0x14;
       spec->autocfg.speaker_pins[0] = 0x18;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}



static void alc885_mbp3_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc885_imac91_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x18;
	spec->autocfg.speaker_pins[1] = 0x1a;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct hda_verb alc882_targa_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02}, /* mic/clfe */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01}, /* line/surround */
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00}, /* HP */

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ } /* end */
};

/* toggle speaker-output according to the hp-jack state */
static void alc882_targa_automute(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc_hp_automute(codec);
	snd_hda_codec_write_cache(codec, 1, 0, AC_VERB_SET_GPIO_DATA,
				  spec->jack_present ? 1 : 3);
}

static void alc882_targa_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x1b;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc882_targa_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc882_targa_automute(codec);
}

static const struct hda_verb alc882_asus_a7j_verbs[] = {
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

static const struct hda_verb alc882_asus_a7m_verbs[] = {
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
	alc_hp_automute(codec);
}

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static const struct hda_verb alc883_auto_init_verbs[] = {
	/*
	 * Unmute ADC0-2 and set the default input to mic-in
	 */
	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

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
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{ }
};

/* 2ch mode (Speaker:front, Subwoofer:CLFE, Line:input, Headphones:front) */
static const struct hda_verb alc889A_mb31_ch2_init[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},             /* HP as front */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, /* Subwoofer on */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},    /* Line as input */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},   /* Line off */
	{ } /* end */
};

/* 4ch mode (Speaker:front, Subwoofer:CLFE, Line:CLFE, Headphones:front) */
static const struct hda_verb alc889A_mb31_ch4_init[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},             /* HP as front */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, /* Subwoofer on */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},   /* Line as output */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, /* Line on */
	{ } /* end */
};

/* 5ch mode (Speaker:front, Subwoofer:CLFE, Line:input, Headphones:rear) */
static const struct hda_verb alc889A_mb31_ch5_init[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},             /* HP as rear */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, /* Subwoofer on */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},    /* Line as input */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},   /* Line off */
	{ } /* end */
};

/* 6ch mode (Speaker:front, Subwoofer:off, Line:CLFE, Headphones:Rear) */
static const struct hda_verb alc889A_mb31_ch6_init[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},             /* HP as front */
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},   /* Subwoofer off */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},   /* Line as output */
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE}, /* Line on */
	{ } /* end */
};

static const struct hda_channel_mode alc889A_mb31_6ch_modes[4] = {
	{ 2, alc889A_mb31_ch2_init },
	{ 4, alc889A_mb31_ch4_init },
	{ 5, alc889A_mb31_ch5_init },
	{ 6, alc889A_mb31_ch6_init },
};

static const struct hda_verb alc883_medion_eapd_verbs[] = {
        /* eanable EAPD on medion laptop */
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF, 0x3070},
	{ }
};

#define alc883_base_mixer	alc882_base_mixer

static const struct snd_kcontrol_new alc883_mitac_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_clevo_m720_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_2ch_fujitsu_pi2515_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_3ST_2ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_3ST_6ch_mixer[] = {
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
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_3ST_6ch_intel_mixer[] = {
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
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc885_8ch_intel_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x0,
			      HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 2, HDA_INPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x1b, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x3, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_fivestack_mixer[] = {
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
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_targa_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
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
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_targa_2ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_targa_8ch_mixer[] = {
	HDA_CODEC_VOLUME("Side Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Side Playback Switch", 0x0f, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_lenovo_101e_2ch_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_lenovo_nb0763_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_medion_wim2160_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1a, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x08, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct hda_verb alc883_medion_wim2160_verbs[] = {
	/* Unmute front mixer */
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	/* Set speaker pin to front mixer */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* Init headphone pin */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x1a, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},

	{ } /* end */
};

/* toggle speaker-output according to the hp-jack state */
static void alc883_medion_wim2160_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1a;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct snd_kcontrol_new alc883_acer_aspire_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc888_acer_aspire_6530_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("LFE Playback Volume", 0x0f, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc888_lenovo_sky_mixer[] = {
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
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc889A_mb31_mixer[] = {
	/* Output mixers */
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x0d, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE("Surround Playback Switch", 0x0d, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x0e, 1, 0x00,
		HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("Center Playback Switch", 0x0e, 1, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x0e, 2, 0x00, HDA_OUTPUT),
	HDA_BIND_MUTE_MONO("LFE Playback Switch", 0x0e, 2, 0x02, HDA_INPUT),
	/* Output switches */
	HDA_CODEC_MUTE("Enable Speaker", 0x14, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE("Enable Headphones", 0x15, 0x00, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Enable LFE", 0x16, 2, 0x00, HDA_OUTPUT),
	/* Boost mixers */
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Boost Volume", 0x1a, 0x00, HDA_INPUT),
	/* Input mixers */
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x00, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x00, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_vaiott_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct hda_bind_ctls alc883_bind_cap_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x08, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x09, 3, 0, HDA_INPUT),
		0
	},
};

static const struct hda_bind_ctls alc883_bind_cap_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x08, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x09, 3, 0, HDA_INPUT),
		0
	},
};

static const struct snd_kcontrol_new alc883_asus_eee1601_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_asus_eee1601_cap_mixer[] = {
	HDA_BIND_VOL("Capture Volume", &alc883_bind_cap_vol),
	HDA_BIND_SW("Capture Switch", &alc883_bind_cap_switch),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		/* .name = "Capture Source", */
		.name = "Input Source",
		.count = 1,
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc_mux_enum_put,
	},
	{ } /* end */
};

static const struct snd_kcontrol_new alc883_chmode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

/* toggle speaker-output according to the hp-jack state */
static void alc883_mitac_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x17;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct hda_verb alc883_mitac_verbs[] = {
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

static const struct hda_verb alc883_clevo_m540r_verbs[] = {
	/* HP */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	/* Int speaker */
	/*{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},*/

	/* enable unsolicited event */
	/*
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_MIC_EVENT | AC_USRSP_EN},
	*/

	{ } /* end */
};

static const struct hda_verb alc883_clevo_m720_verbs[] = {
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

static const struct hda_verb alc883_2ch_fujitsu_pi2515_verbs[] = {
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

static const struct hda_verb alc883_targa_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

/* Connect Line-Out side jack (SPDIF) to Side */
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x17, AC_VERB_SET_CONNECT_SEL, 0x03},
/* Connect Mic jack to CLFE */
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02},
/* Connect Line-in jack to Surround */
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01},
/* Connect HP out jack to Front */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},

	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},

	{ } /* end */
};

static const struct hda_verb alc883_lenovo_101e_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_FRONT_EVENT|AC_USRSP_EN},
        {0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT|AC_USRSP_EN},
	{ } /* end */
};

static const struct hda_verb alc883_lenovo_nb0763_verbs[] = {
        {0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
        {0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
        {0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{ } /* end */
};

static const struct hda_verb alc888_lenovo_ms7195_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_FRONT_EVENT | AC_USRSP_EN},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT    | AC_USRSP_EN},
	{ } /* end */
};

static const struct hda_verb alc883_haier_w66_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},

	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{ } /* end */
};

static const struct hda_verb alc888_lenovo_sky_verbs[] = {
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

static const struct hda_verb alc888_6st_dell_verbs[] = {
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ }
};

static const struct hda_verb alc883_vaiott_verbs[] = {
	/* HP */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},

	/* enable unsolicited event */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},

	{ } /* end */
};

static void alc888_3st_hp_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->autocfg.speaker_pins[2] = 0x18;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct hda_verb alc888_3st_hp_verbs[] = {
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x00},	/* Front: output 0 (0x0c) */
	{0x16, AC_VERB_SET_CONNECT_SEL, 0x01},	/* Rear : output 1 (0x0d) */
	{0x18, AC_VERB_SET_CONNECT_SEL, 0x02},	/* CLFE : output 2 (0x0e) */
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ } /* end */
};

/*
 * 2ch mode
 */
static const struct hda_verb alc888_3st_hp_2ch_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 4ch mode
 */
static const struct hda_verb alc888_3st_hp_4ch_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x16, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

/*
 * 6ch mode
 */
static const struct hda_verb alc888_3st_hp_6ch_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x16, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static const struct hda_channel_mode alc888_3st_hp_modes[3] = {
	{ 2, alc888_3st_hp_2ch_init },
	{ 4, alc888_3st_hp_4ch_init },
	{ 6, alc888_3st_hp_6ch_init },
};

static void alc888_lenovo_ms7195_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.line_out_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

/* toggle speaker-output according to the hp-jack state */
static void alc883_lenovo_nb0763_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

/* toggle speaker-output according to the hp-jack state */
#define alc883_targa_init_hook		alc882_targa_init_hook
#define alc883_targa_unsol_event	alc882_targa_unsol_event

static void alc883_clevo_m720_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc883_clevo_m720_init_hook(struct hda_codec *codec)
{
	alc_hp_automute(codec);
	alc88x_simple_mic_automute(codec);
}

static void alc883_clevo_m720_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	switch (res >> 26) {
	case ALC880_MIC_EVENT:
		alc88x_simple_mic_automute(codec);
		break;
	default:
		alc_sku_unsol_event(codec, res);
		break;
	}
}

/* toggle speaker-output according to the hp-jack state */
static void alc883_2ch_fujitsu_pi2515_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc883_haier_w66_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc883_lenovo_101e_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.line_out_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->automute = 1;
	spec->detect_line = 1;
	spec->automute_lines = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

/* toggle speaker-output according to the hp-jack state */
static void alc883_acer_aspire_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct hda_verb alc883_acer_eapd_verbs[] = {
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

static void alc888_6st_dell_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x15;
	spec->autocfg.speaker_pins[2] = 0x16;
	spec->autocfg.speaker_pins[3] = 0x17;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc888_lenovo_sky_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x15;
	spec->autocfg.speaker_pins[2] = 0x16;
	spec->autocfg.speaker_pins[3] = 0x17;
	spec->autocfg.speaker_pins[4] = 0x1a;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc883_vaiott_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x17;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct hda_verb alc888_asus_m90v_verbs[] = {
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	/* enable unsolicited event */
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_MIC_EVENT | AC_USRSP_EN},
	{ } /* end */
};

static void alc883_mode2_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x15;
	spec->autocfg.speaker_pins[2] = 0x16;
	spec->ext_mic.pin = 0x18;
	spec->int_mic.pin = 0x19;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.mux_idx = 1;
	spec->auto_mic = 1;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct hda_verb alc888_asus_eee1601_verbs[] = {
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

static void alc883_eee1601_inithook(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x1b;
	alc_hp_automute(codec);
}

static const struct hda_verb alc889A_mb31_verbs[] = {
	/* Init rear pin (used as headphone output) */
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc4},    /* Apple Headphones */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},           /* Connect to front */
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	/* Init line pin (used as output in 4ch and 6ch mode) */
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x02},           /* Connect to CLFE */
	/* Init line 2 pin (used as headphone out by default) */
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},  /* Use as input */
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE}, /* Mute output */
	{ } /* end */
};

/* Mute speakers according to the headphone jack state */
static void alc889A_mb31_automute(struct hda_codec *codec)
{
	unsigned int present;

	/* Mute only in 2ch or 4ch mode */
	if (snd_hda_codec_read(codec, 0x15, 0, AC_VERB_GET_CONNECT_SEL, 0)
	    == 0x00) {
		present = snd_hda_jack_detect(codec, 0x15);
		snd_hda_codec_amp_stereo(codec, 0x14,  HDA_OUTPUT, 0,
			HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
		snd_hda_codec_amp_stereo(codec, 0x16, HDA_OUTPUT, 0,
			HDA_AMP_MUTE, present ? HDA_AMP_MUTE : 0);
	}
}

static void alc889A_mb31_unsol_event(struct hda_codec *codec, unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc889A_mb31_automute(codec);
}


#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc882_loopbacks	alc880_loopbacks
#endif

/* pcm configuration: identical with ALC880 */
#define alc882_pcm_analog_playback	alc880_pcm_analog_playback
#define alc882_pcm_analog_capture	alc880_pcm_analog_capture
#define alc882_pcm_digital_playback	alc880_pcm_digital_playback
#define alc882_pcm_digital_capture	alc880_pcm_digital_capture

static const hda_nid_t alc883_slave_dig_outs[] = {
	ALC1200_DIGOUT_NID, 0,
};

static const hda_nid_t alc1200_slave_dig_outs[] = {
	ALC883_DIGOUT_NID, 0,
};

/*
 * configuration and preset
 */
static const char * const alc882_models[ALC882_MODEL_LAST] = {
	[ALC882_3ST_DIG]	= "3stack-dig",
	[ALC882_6ST_DIG]	= "6stack-dig",
	[ALC882_ARIMA]		= "arima",
	[ALC882_W2JC]		= "w2jc",
	[ALC882_TARGA]		= "targa",
	[ALC882_ASUS_A7J]	= "asus-a7j",
	[ALC882_ASUS_A7M]	= "asus-a7m",
	[ALC885_MACPRO]		= "macpro",
	[ALC885_MB5]		= "mb5",
	[ALC885_MACMINI3]	= "macmini3",
	[ALC885_MBA21]		= "mba21",
	[ALC885_MBP3]		= "mbp3",
	[ALC885_IMAC24]		= "imac24",
	[ALC885_IMAC91]		= "imac91",
	[ALC883_3ST_2ch_DIG]	= "3stack-2ch-dig",
	[ALC883_3ST_6ch_DIG]	= "3stack-6ch-dig",
	[ALC883_3ST_6ch]	= "3stack-6ch",
	[ALC883_6ST_DIG]	= "alc883-6stack-dig",
	[ALC883_TARGA_DIG]	= "targa-dig",
	[ALC883_TARGA_2ch_DIG]	= "targa-2ch-dig",
	[ALC883_TARGA_8ch_DIG]	= "targa-8ch-dig",
	[ALC883_ACER]		= "acer",
	[ALC883_ACER_ASPIRE]	= "acer-aspire",
	[ALC888_ACER_ASPIRE_4930G]	= "acer-aspire-4930g",
	[ALC888_ACER_ASPIRE_6530G]	= "acer-aspire-6530g",
	[ALC888_ACER_ASPIRE_8930G]	= "acer-aspire-8930g",
	[ALC888_ACER_ASPIRE_7730G]	= "acer-aspire-7730g",
	[ALC883_MEDION]		= "medion",
	[ALC883_MEDION_WIM2160]	= "medion-wim2160",
	[ALC883_LAPTOP_EAPD]	= "laptop-eapd",
	[ALC883_LENOVO_101E_2ch] = "lenovo-101e",
	[ALC883_LENOVO_NB0763]	= "lenovo-nb0763",
	[ALC888_LENOVO_MS7195_DIG] = "lenovo-ms7195-dig",
	[ALC888_LENOVO_SKY] = "lenovo-sky",
	[ALC883_HAIER_W66] 	= "haier-w66",
	[ALC888_3ST_HP]		= "3stack-hp",
	[ALC888_6ST_DELL]	= "6stack-dell",
	[ALC883_MITAC]		= "mitac",
	[ALC883_CLEVO_M540R]	= "clevo-m540r",
	[ALC883_CLEVO_M720]	= "clevo-m720",
	[ALC883_FUJITSU_PI2515] = "fujitsu-pi2515",
	[ALC888_FUJITSU_XA3530] = "fujitsu-xa3530",
	[ALC883_3ST_6ch_INTEL]	= "3stack-6ch-intel",
	[ALC889A_INTEL]		= "intel-alc889a",
	[ALC889_INTEL]		= "intel-x58",
	[ALC1200_ASUS_P5Q]	= "asus-p5q",
	[ALC889A_MB31]		= "mb31",
	[ALC883_SONY_VAIO_TT]	= "sony-vaio-tt",
	[ALC882_AUTO]		= "auto",
};

static const struct snd_pci_quirk alc882_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x6668, "ECS", ALC882_6ST_DIG),

	SND_PCI_QUIRK(0x1025, 0x006c, "Acer Aspire 9810", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x0090, "Acer Aspire", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x010a, "Acer Ferrari 5000", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x0110, "Acer Aspire", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x0112, "Acer Aspire 9303", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x0121, "Acer Aspire 5920G", ALC883_ACER_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x013e, "Acer Aspire 4930G",
		ALC888_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x013f, "Acer Aspire 5930G",
		ALC888_ACER_ASPIRE_4930G),
	SND_PCI_QUIRK(0x1025, 0x0145, "Acer Aspire 8930G",
		ALC888_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x0146, "Acer Aspire 6935G",
		ALC888_ACER_ASPIRE_8930G),
	SND_PCI_QUIRK(0x1025, 0x0157, "Acer X3200", ALC882_AUTO),
	SND_PCI_QUIRK(0x1025, 0x0158, "Acer AX1700-U3700A", ALC882_AUTO),
	SND_PCI_QUIRK(0x1025, 0x015e, "Acer Aspire 6930G",
		ALC888_ACER_ASPIRE_6530G),
	SND_PCI_QUIRK(0x1025, 0x0166, "Acer Aspire 6530G",
		ALC888_ACER_ASPIRE_6530G),
	SND_PCI_QUIRK(0x1025, 0x0142, "Acer Aspire 7730G",
		ALC888_ACER_ASPIRE_7730G),
	/* default Acer -- disabled as it causes more problems.
	 *    model=auto should work fine now
	 */
	/* SND_PCI_QUIRK_VENDOR(0x1025, "Acer laptop", ALC883_ACER), */

	SND_PCI_QUIRK(0x1028, 0x020d, "Dell Inspiron 530", ALC888_6ST_DELL),

	SND_PCI_QUIRK(0x103c, 0x2a3d, "HP Pavilion", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x103c, 0x2a4f, "HP Samba", ALC888_3ST_HP),
	SND_PCI_QUIRK(0x103c, 0x2a60, "HP Lucknow", ALC888_3ST_HP),
	SND_PCI_QUIRK(0x103c, 0x2a61, "HP Nettle", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x103c, 0x2a66, "HP Acacia", ALC888_3ST_HP),
	SND_PCI_QUIRK(0x103c, 0x2a72, "HP Educ.ar", ALC888_3ST_HP),

	SND_PCI_QUIRK(0x1043, 0x060d, "Asus A7J", ALC882_ASUS_A7J),
	SND_PCI_QUIRK(0x1043, 0x1243, "Asus A7J", ALC882_ASUS_A7J),
	SND_PCI_QUIRK(0x1043, 0x13c2, "Asus A7M", ALC882_ASUS_A7M),
	SND_PCI_QUIRK(0x1043, 0x1873, "Asus M90V", ALC888_ASUS_M90V),
	SND_PCI_QUIRK(0x1043, 0x1971, "Asus W2JC", ALC882_W2JC),
	SND_PCI_QUIRK(0x1043, 0x817f, "Asus P5LD2", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x81d8, "Asus P5WD", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x8249, "Asus M2A-VM HDMI", ALC883_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1043, 0x8284, "Asus Z37E", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1043, 0x82fe, "Asus P5Q-EM HDMI", ALC1200_ASUS_P5Q),
	SND_PCI_QUIRK(0x1043, 0x835f, "Asus Eee 1601", ALC888_ASUS_EEE1601),

	SND_PCI_QUIRK(0x104d, 0x9047, "Sony Vaio TT", ALC883_SONY_VAIO_TT),
	SND_PCI_QUIRK(0x105b, 0x0ce8, "Foxconn P35AX-S", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x105b, 0x6668, "Foxconn", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1071, 0x8227, "Mitac 82801H", ALC883_MITAC),
	SND_PCI_QUIRK(0x1071, 0x8253, "Mitac 8252d", ALC883_MITAC),
	SND_PCI_QUIRK(0x1071, 0x8258, "Evesham Voyaeger", ALC883_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x10f1, 0x2350, "TYAN-S2350", ALC888_6ST_DELL),
	SND_PCI_QUIRK(0x108e, 0x534d, NULL, ALC883_3ST_6ch),
	SND_PCI_QUIRK(0x1458, 0xa002, "Gigabyte P35 DS3R", ALC882_6ST_DIG),

	SND_PCI_QUIRK(0x1462, 0x0349, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x040d, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x0579, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x28fb, "Targa T8", ALC882_TARGA), /* MSI-1049 T8  */
	SND_PCI_QUIRK(0x1462, 0x2fb3, "MSI", ALC882_AUTO),
	SND_PCI_QUIRK(0x1462, 0x6668, "MSI", ALC882_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x3729, "MSI S420", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3783, "NEC S970", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3b7f, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x3ef9, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fc1, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fc3, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fcc, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x3fdf, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x42cd, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x4314, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x4319, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x4324, "MSI", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x4570, "MSI Wind Top AE2220", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x6510, "MSI GX620", ALC883_TARGA_8ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x6668, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7187, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7250, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7260, "MSI 7260", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0x7267, "MSI", ALC883_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1462, 0x7280, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7327, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7350, "MSI", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1462, 0x7437, "MSI NetOn AP1900", ALC883_TARGA_DIG),
	SND_PCI_QUIRK(0x1462, 0xa422, "MSI", ALC883_TARGA_2ch_DIG),
	SND_PCI_QUIRK(0x1462, 0xaa08, "MSI", ALC883_TARGA_2ch_DIG),

	SND_PCI_QUIRK(0x147b, 0x1083, "Abit IP35-PRO", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1558, 0x0571, "Clevo laptop M570U", ALC883_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1558, 0x0721, "Clevo laptop M720R", ALC883_CLEVO_M720),
	SND_PCI_QUIRK(0x1558, 0x0722, "Clevo laptop M720SR", ALC883_CLEVO_M720),
	SND_PCI_QUIRK(0x1558, 0x5409, "Clevo laptop M540R", ALC883_CLEVO_M540R),
	SND_PCI_QUIRK_VENDOR(0x1558, "Clevo laptop", ALC883_LAPTOP_EAPD),
	SND_PCI_QUIRK(0x15d9, 0x8780, "Supermicro PDSBA", ALC883_3ST_6ch),
	/* SND_PCI_QUIRK(0x161f, 0x2054, "Arima W820", ALC882_ARIMA), */
	SND_PCI_QUIRK(0x161f, 0x2054, "Medion laptop", ALC883_MEDION),
	SND_PCI_QUIRK_MASK(0x1734, 0xfff0, 0x1100, "FSC AMILO Xi/Pi25xx",
		      ALC883_FUJITSU_PI2515),
	SND_PCI_QUIRK_MASK(0x1734, 0xfff0, 0x1130, "Fujitsu AMILO Xa35xx",
		ALC888_FUJITSU_XA3530),
	SND_PCI_QUIRK(0x17aa, 0x101e, "Lenovo 101e", ALC883_LENOVO_101E_2ch),
	SND_PCI_QUIRK(0x17aa, 0x2085, "Lenovo NB0763", ALC883_LENOVO_NB0763),
	SND_PCI_QUIRK(0x17aa, 0x3bfc, "Lenovo NB0763", ALC883_LENOVO_NB0763),
	SND_PCI_QUIRK(0x17aa, 0x3bfd, "Lenovo NB0763", ALC883_LENOVO_NB0763),
	SND_PCI_QUIRK(0x17aa, 0x101d, "Lenovo Sky", ALC888_LENOVO_SKY),
	SND_PCI_QUIRK(0x17c0, 0x4085, "MEDION MD96630", ALC888_LENOVO_MS7195_DIG),
	SND_PCI_QUIRK(0x17f2, 0x5000, "Albatron KI690-AM2", ALC883_6ST_DIG),
	SND_PCI_QUIRK(0x1991, 0x5625, "Haier W66", ALC883_HAIER_W66),

	SND_PCI_QUIRK(0x8086, 0x0001, "DG33BUC", ALC883_3ST_6ch_INTEL),
	SND_PCI_QUIRK(0x8086, 0x0002, "DG33FBC", ALC883_3ST_6ch_INTEL),
	SND_PCI_QUIRK(0x8086, 0x2503, "82801H", ALC883_MITAC),
	SND_PCI_QUIRK(0x8086, 0x0022, "DX58SO", ALC889_INTEL),
	SND_PCI_QUIRK(0x8086, 0x0021, "Intel IbexPeak", ALC889A_INTEL),
	SND_PCI_QUIRK(0x8086, 0x3b56, "Intel IbexPeak", ALC889A_INTEL),
	SND_PCI_QUIRK(0x8086, 0xd601, "D102GGC", ALC882_6ST_DIG),

	{}
};

/* codec SSID table for Intel Mac */
static const struct snd_pci_quirk alc882_ssid_cfg_tbl[] = {
	SND_PCI_QUIRK(0x106b, 0x00a0, "MacBookPro 3,1", ALC885_MBP3),
	SND_PCI_QUIRK(0x106b, 0x00a1, "Macbook", ALC885_MBP3),
	SND_PCI_QUIRK(0x106b, 0x00a4, "MacbookPro 4,1", ALC885_MBP3),
	SND_PCI_QUIRK(0x106b, 0x0c00, "Mac Pro", ALC885_MACPRO),
	SND_PCI_QUIRK(0x106b, 0x1000, "iMac 24", ALC885_IMAC24),
	SND_PCI_QUIRK(0x106b, 0x2800, "AppleTV", ALC885_IMAC24),
	SND_PCI_QUIRK(0x106b, 0x2c00, "MacbookPro rev3", ALC885_MBP3),
	SND_PCI_QUIRK(0x106b, 0x3000, "iMac", ALC889A_MB31),
	SND_PCI_QUIRK(0x106b, 0x3200, "iMac 7,1 Aluminum", ALC882_ASUS_A7M),
	SND_PCI_QUIRK(0x106b, 0x3400, "MacBookAir 1,1", ALC885_MBP3),
	SND_PCI_QUIRK(0x106b, 0x3500, "MacBookAir 2,1", ALC885_MBA21),
	SND_PCI_QUIRK(0x106b, 0x3600, "Macbook 3,1", ALC889A_MB31),
	SND_PCI_QUIRK(0x106b, 0x3800, "MacbookPro 4,1", ALC885_MBP3),
	SND_PCI_QUIRK(0x106b, 0x3e00, "iMac 24 Aluminum", ALC885_IMAC24),
	SND_PCI_QUIRK(0x106b, 0x4900, "iMac 9,1 Aluminum", ALC885_IMAC91),
	SND_PCI_QUIRK(0x106b, 0x3f00, "Macbook 5,1", ALC885_MB5),
	SND_PCI_QUIRK(0x106b, 0x4a00, "Macbook 5,2", ALC885_MB5),
	/* FIXME: HP jack sense seems not working for MBP 5,1 or 5,2,
	 * so apparently no perfect solution yet
	 */
	SND_PCI_QUIRK(0x106b, 0x4000, "MacbookPro 5,1", ALC885_MB5),
	SND_PCI_QUIRK(0x106b, 0x4600, "MacbookPro 5,2", ALC885_MB5),
	SND_PCI_QUIRK(0x106b, 0x4100, "Macmini 3,1", ALC885_MACMINI3),
	{} /* terminator */
};

static const struct alc_config_preset alc882_presets[] = {
	[ALC882_3ST_DIG] = {
		.mixers = { alc882_base_mixer },
		.init_verbs = { alc882_base_init_verbs,
				alc882_adc1_init_verbs },
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
		.init_verbs = { alc882_base_init_verbs,
				alc882_adc1_init_verbs },
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
		.init_verbs = { alc882_base_init_verbs, alc882_adc1_init_verbs,
				alc882_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc882_sixstack_modes),
		.channel_mode = alc882_sixstack_modes,
		.input_mux = &alc882_capture_source,
	},
	[ALC882_W2JC] = {
		.mixers = { alc882_w2jc_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_base_init_verbs, alc882_adc1_init_verbs,
				alc882_eapd_verbs, alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
		.dig_out_nid = ALC882_DIGOUT_NID,
	},
	   [ALC885_MBA21] = {
			.mixers = { alc885_mba21_mixer },
			.init_verbs = { alc885_mba21_init_verbs, alc880_gpio1_init_verbs },
			.num_dacs = 2,
			.dac_nids = alc882_dac_nids,
			.channel_mode = alc885_mba21_ch_modes,
			.num_channel_mode = ARRAY_SIZE(alc885_mba21_ch_modes),
			.input_mux = &alc882_capture_source,
			.unsol_event = alc_sku_unsol_event,
			.setup = alc885_mba21_setup,
			.init_hook = alc_hp_automute,
       },
	[ALC885_MBP3] = {
		.mixers = { alc885_mbp3_mixer, alc882_chmode_mixer },
		.init_verbs = { alc885_mbp3_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = 2,
		.dac_nids = alc882_dac_nids,
		.hp_nid = 0x04,
		.channel_mode = alc885_mbp_4ch_modes,
		.num_channel_mode = ARRAY_SIZE(alc885_mbp_4ch_modes),
		.input_mux = &alc882_capture_source,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc885_mbp3_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC885_MB5] = {
		.mixers = { alc885_mb5_mixer, alc882_chmode_mixer },
		.init_verbs = { alc885_mb5_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.channel_mode = alc885_mb5_6ch_modes,
		.num_channel_mode = ARRAY_SIZE(alc885_mb5_6ch_modes),
		.input_mux = &mb5_capture_source,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc885_mb5_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC885_MACMINI3] = {
		.mixers = { alc885_macmini3_mixer, alc882_chmode_mixer },
		.init_verbs = { alc885_macmini3_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.channel_mode = alc885_macmini3_6ch_modes,
		.num_channel_mode = ARRAY_SIZE(alc885_macmini3_6ch_modes),
		.input_mux = &macmini3_capture_source,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc885_macmini3_setup,
		.init_hook = alc_hp_automute,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc885_imac24_setup,
		.init_hook = alc885_imac24_init_hook,
	},
	[ALC885_IMAC91] = {
		.mixers = {alc885_imac91_mixer},
		.init_verbs = { alc885_imac91_init_verbs,
				alc880_gpio1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.channel_mode = alc885_mba21_ch_modes,
		.num_channel_mode = ARRAY_SIZE(alc885_mba21_ch_modes),
		.input_mux = &alc889A_imac91_capture_source,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.dig_in_nid = ALC882_DIGIN_NID,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc885_imac91_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC882_TARGA] = {
		.mixers = { alc882_targa_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_base_init_verbs, alc882_adc1_init_verbs,
				alc880_gpio3_init_verbs, alc882_targa_verbs},
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc882_targa_setup,
		.init_hook = alc882_targa_automute,
	},
	[ALC882_ASUS_A7J] = {
		.mixers = { alc882_asus_a7j_mixer, alc882_chmode_mixer },
		.init_verbs = { alc882_base_init_verbs, alc882_adc1_init_verbs,
				alc882_asus_a7j_verbs},
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
		.init_verbs = { alc882_base_init_verbs, alc882_adc1_init_verbs,
				alc882_eapd_verbs, alc880_gpio1_init_verbs,
				alc882_asus_a7m_verbs },
		.num_dacs = ARRAY_SIZE(alc882_dac_nids),
		.dac_nids = alc882_dac_nids,
		.dig_out_nid = ALC882_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc880_threestack_modes),
		.channel_mode = alc880_threestack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc882_capture_source,
	},
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
		.slave_dig_outs = alc883_slave_dig_outs,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_intel_modes),
		.channel_mode = alc883_3ST_6ch_intel_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_3stack_6ch_intel,
	},
	[ALC889A_INTEL] = {
		.mixers = { alc885_8ch_intel_mixer, alc883_chmode_mixer },
		.init_verbs = { alc885_init_verbs, alc885_init_input_verbs,
				alc_hp15_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc889_adc_nids),
		.adc_nids = alc889_adc_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.slave_dig_outs = alc883_slave_dig_outs,
		.num_channel_mode = ARRAY_SIZE(alc889_8ch_intel_modes),
		.channel_mode = alc889_8ch_intel_modes,
		.capsrc_nids = alc889_capsrc_nids,
		.input_mux = &alc889_capture_source,
		.setup = alc889_automute_setup,
		.init_hook = alc_hp_automute,
		.unsol_event = alc_sku_unsol_event,
		.need_dac_fix = 1,
	},
	[ALC889_INTEL] = {
		.mixers = { alc885_8ch_intel_mixer, alc883_chmode_mixer },
		.init_verbs = { alc885_init_verbs, alc889_init_input_verbs,
				alc889_eapd_verbs, alc_hp15_unsol_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc889_adc_nids),
		.adc_nids = alc889_adc_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.slave_dig_outs = alc883_slave_dig_outs,
		.num_channel_mode = ARRAY_SIZE(alc889_8ch_intel_modes),
		.channel_mode = alc889_8ch_intel_modes,
		.capsrc_nids = alc889_capsrc_nids,
		.input_mux = &alc889_capture_source,
		.setup = alc889_automute_setup,
		.init_hook = alc889_intel_init_hook,
		.unsol_event = alc_sku_unsol_event,
		.need_dac_fix = 1,
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
		.mixers = { alc883_targa_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc880_gpio3_init_verbs,
				alc883_targa_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_targa_unsol_event,
		.setup = alc882_targa_setup,
		.init_hook = alc882_targa_automute,
	},
	[ALC883_TARGA_2ch_DIG] = {
		.mixers = { alc883_targa_2ch_mixer},
		.init_verbs = { alc883_init_verbs, alc880_gpio3_init_verbs,
				alc883_targa_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.adc_nids = alc883_adc_nids_alt,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_alt),
		.capsrc_nids = alc883_capsrc_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_targa_unsol_event,
		.setup = alc882_targa_setup,
		.init_hook = alc882_targa_automute,
	},
	[ALC883_TARGA_8ch_DIG] = {
		.mixers = { alc883_targa_mixer, alc883_targa_8ch_mixer,
			    alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc880_gpio3_init_verbs,
				alc883_targa_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_rev),
		.adc_nids = alc883_adc_nids_rev,
		.capsrc_nids = alc883_capsrc_nids_rev,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_4ST_8ch_modes),
		.channel_mode = alc883_4ST_8ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc883_targa_unsol_event,
		.setup = alc882_targa_setup,
		.init_hook = alc882_targa_automute,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_acer_aspire_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC888_ACER_ASPIRE_4930G] = {
		.mixers = { alc888_acer_aspire_4930g_mixer,
				alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc880_gpio1_init_verbs,
				alc888_acer_aspire_4930g_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_rev),
		.adc_nids = alc883_adc_nids_rev,
		.capsrc_nids = alc883_capsrc_nids_rev,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.const_channel_count = 6,
		.num_mux_defs =
			ARRAY_SIZE(alc888_2_capture_sources),
		.input_mux = alc888_2_capture_sources,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_acer_aspire_4930g_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC888_ACER_ASPIRE_6530G] = {
		.mixers = { alc888_acer_aspire_6530_mixer },
		.init_verbs = { alc883_init_verbs, alc880_gpio1_init_verbs,
				alc888_acer_aspire_6530g_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_rev),
		.adc_nids = alc883_adc_nids_rev,
		.capsrc_nids = alc883_capsrc_nids_rev,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.num_mux_defs =
			ARRAY_SIZE(alc888_2_capture_sources),
		.input_mux = alc888_acer_aspire_6530_sources,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_acer_aspire_6530g_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC888_ACER_ASPIRE_8930G] = {
		.mixers = { alc889_acer_aspire_8930g_mixer,
				alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc880_gpio1_init_verbs,
				alc889_acer_aspire_8930g_verbs,
				alc889_eapd_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc889_adc_nids),
		.adc_nids = alc889_adc_nids,
		.capsrc_nids = alc889_capsrc_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.const_channel_count = 6,
		.num_mux_defs =
			ARRAY_SIZE(alc889_capture_sources),
		.input_mux = alc889_capture_sources,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc889_acer_aspire_8930g_setup,
		.init_hook = alc_hp_automute,
#ifdef CONFIG_SND_HDA_POWER_SAVE
		.power_hook = alc_power_eapd,
#endif
	},
	[ALC888_ACER_ASPIRE_7730G] = {
		.mixers = { alc883_3ST_6ch_mixer,
				alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc880_gpio1_init_verbs,
				alc888_acer_aspire_7730G_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_rev),
		.adc_nids = alc883_adc_nids_rev,
		.capsrc_nids = alc883_capsrc_nids_rev,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_modes),
		.channel_mode = alc883_3ST_6ch_modes,
		.need_dac_fix = 1,
		.const_channel_count = 6,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_acer_aspire_7730g_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC883_MEDION] = {
		.mixers = { alc883_fivestack_mixer,
			    alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs,
				alc883_medion_eapd_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.adc_nids = alc883_adc_nids_alt,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_alt),
		.capsrc_nids = alc883_capsrc_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_sixstack_modes),
		.channel_mode = alc883_sixstack_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC883_MEDION_WIM2160] = {
		.mixers = { alc883_medion_wim2160_mixer },
		.init_verbs = { alc883_init_verbs, alc883_medion_wim2160_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids),
		.adc_nids = alc883_adc_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_medion_wim2160_setup,
		.init_hook = alc_hp_automute,
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
	[ALC883_CLEVO_M540R] = {
		.mixers = { alc883_3ST_6ch_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc883_clevo_m540r_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_6ch_clevo_modes),
		.channel_mode = alc883_3ST_6ch_clevo_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_capture_source,
		/* This machine has the hardware HP auto-muting, thus
		 * we need no software mute via unsol event
		 */
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
		.setup = alc883_clevo_m720_setup,
		.init_hook = alc883_clevo_m720_init_hook,
	},
	[ALC883_LENOVO_101E_2ch] = {
		.mixers = { alc883_lenovo_101e_2ch_mixer},
		.init_verbs = { alc883_init_verbs, alc883_lenovo_101e_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.adc_nids = alc883_adc_nids_alt,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_alt),
		.capsrc_nids = alc883_capsrc_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_lenovo_101e_capture_source,
		.setup = alc883_lenovo_101e_setup,
		.unsol_event = alc_sku_unsol_event,
		.init_hook = alc_inithook,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_lenovo_nb0763_setup,
		.init_hook = alc_hp_automute,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_lenovo_ms7195_setup,
		.init_hook = alc_inithook,
	},
	[ALC883_HAIER_W66] = {
		.mixers = { alc883_targa_2ch_mixer},
		.init_verbs = { alc883_init_verbs, alc883_haier_w66_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_haier_w66_setup,
		.init_hook = alc_hp_automute,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_3st_hp_setup,
		.init_hook = alc_hp_automute,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_6st_dell_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC883_MITAC] = {
		.mixers = { alc883_mitac_mixer },
		.init_verbs = { alc883_init_verbs, alc883_mitac_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_mitac_setup,
		.init_hook = alc_hp_automute,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_2ch_fujitsu_pi2515_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC888_FUJITSU_XA3530] = {
		.mixers = { alc888_base_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs,
			alc888_fujitsu_xa3530_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids_rev),
		.adc_nids = alc883_adc_nids_rev,
		.capsrc_nids = alc883_capsrc_nids_rev,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc888_4ST_8ch_intel_modes),
		.channel_mode = alc888_4ST_8ch_intel_modes,
		.num_mux_defs =
			ARRAY_SIZE(alc888_2_capture_sources),
		.input_mux = alc888_2_capture_sources,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_fujitsu_xa3530_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC888_LENOVO_SKY] = {
		.mixers = { alc888_lenovo_sky_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs, alc888_lenovo_sky_verbs},
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_sixstack_modes),
		.channel_mode = alc883_sixstack_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_lenovo_sky_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc888_lenovo_sky_setup,
		.init_hook = alc_hp_automute,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_mode2_setup,
		.init_hook = alc_inithook,
	},
	[ALC888_ASUS_EEE1601] = {
		.mixers = { alc883_asus_eee1601_mixer },
		.cap_mixer = alc883_asus_eee1601_cap_mixer,
		.init_verbs = { alc883_init_verbs, alc888_asus_eee1601_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc883_asus_eee1601_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.init_hook = alc883_eee1601_inithook,
	},
	[ALC1200_ASUS_P5Q] = {
		.mixers = { alc883_base_mixer, alc883_chmode_mixer },
		.init_verbs = { alc883_init_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.dig_out_nid = ALC1200_DIGOUT_NID,
		.dig_in_nid = ALC883_DIGIN_NID,
		.slave_dig_outs = alc1200_slave_dig_outs,
		.num_channel_mode = ARRAY_SIZE(alc883_sixstack_modes),
		.channel_mode = alc883_sixstack_modes,
		.input_mux = &alc883_capture_source,
	},
	[ALC889A_MB31] = {
		.mixers = { alc889A_mb31_mixer, alc883_chmode_mixer},
		.init_verbs = { alc883_init_verbs, alc889A_mb31_verbs,
			alc880_gpio1_init_verbs },
		.adc_nids = alc883_adc_nids,
		.num_adc_nids = ARRAY_SIZE(alc883_adc_nids),
		.capsrc_nids = alc883_capsrc_nids,
		.dac_nids = alc883_dac_nids,
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.channel_mode = alc889A_mb31_6ch_modes,
		.num_channel_mode = ARRAY_SIZE(alc889A_mb31_6ch_modes),
		.input_mux = &alc889A_mb31_capture_source,
		.dig_out_nid = ALC883_DIGOUT_NID,
		.unsol_event = alc889A_mb31_unsol_event,
		.init_hook = alc889A_mb31_automute,
	},
	[ALC883_SONY_VAIO_TT] = {
		.mixers = { alc883_vaiott_mixer },
		.init_verbs = { alc883_init_verbs, alc883_vaiott_verbs },
		.num_dacs = ARRAY_SIZE(alc883_dac_nids),
		.dac_nids = alc883_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc883_3ST_2ch_modes),
		.channel_mode = alc883_3ST_2ch_modes,
		.input_mux = &alc883_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc883_vaiott_setup,
		.init_hook = alc_hp_automute,
	},
};


/*
 * Pin config fixes
 */
enum {
	PINFIX_ABIT_AW9D_MAX,
	PINFIX_LENOVO_Y530,
	PINFIX_PB_M5210,
	PINFIX_ACER_ASPIRE_7736,
};

static const struct alc_fixup alc882_fixups[] = {
	[PINFIX_ABIT_AW9D_MAX] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x15, 0x01080104 }, /* side */
			{ 0x16, 0x01011012 }, /* rear */
			{ 0x17, 0x01016011 }, /* clfe */
			{ }
		}
	},
	[PINFIX_LENOVO_Y530] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x15, 0x99130112 }, /* rear int speakers */
			{ 0x16, 0x99130111 }, /* subwoofer */
			{ }
		}
	},
	[PINFIX_PB_M5210] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{ 0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50 },
			{}
		}
	},
	[PINFIX_ACER_ASPIRE_7736] = {
		.type = ALC_FIXUP_SKU,
		.v.sku = ALC_FIXUP_SKU_IGNORE,
	},
};

static const struct snd_pci_quirk alc882_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x0155, "Packard-Bell M5120", PINFIX_PB_M5210),
	SND_PCI_QUIRK(0x17aa, 0x3a0d, "Lenovo Y530", PINFIX_LENOVO_Y530),
	SND_PCI_QUIRK(0x147b, 0x107a, "Abit AW9D-MAX", PINFIX_ABIT_AW9D_MAX),
	SND_PCI_QUIRK(0x1025, 0x0296, "Acer Aspire 7736z", PINFIX_ACER_ASPIRE_7736),
	{}
};

/*
 * BIOS auto configuration
 */
static int alc882_auto_create_input_ctls(struct hda_codec *codec,
						const struct auto_pin_cfg *cfg)
{
	return alc_auto_create_input_ctls(codec, cfg, 0x0b, 0x23, 0x22);
}

static void alc882_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid, int pin_type,
					      hda_nid_t dac)
{
	int idx;

	/* set as output */
	alc_set_pin_output(codec, nid, pin_type);

	if (dac == 0x25)
		idx = 4;
	else if (dac >= 0x02 && dac <= 0x05)
		idx = dac - 2;
	else
		return;
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_CONNECT_SEL, idx);
}

static void alc882_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i <= HDA_SIDE; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		if (nid)
			alc882_auto_set_output_and_unmute(codec, nid, pin_type,
					spec->multiout.dac_nids[i]);
	}
}

static void alc882_auto_init_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin, dac;
	int i;

	if (spec->autocfg.line_out_type != AUTO_PIN_HP_OUT) {
		for (i = 0; i < ARRAY_SIZE(spec->autocfg.hp_pins); i++) {
			pin = spec->autocfg.hp_pins[i];
			if (!pin)
				break;
			dac = spec->multiout.hp_nid;
			if (!dac)
				dac = spec->multiout.dac_nids[0]; /* to front */
			alc882_auto_set_output_and_unmute(codec, pin, PIN_HP, dac);
		}
	}

	if (spec->autocfg.line_out_type != AUTO_PIN_SPEAKER_OUT) {
		for (i = 0; i < ARRAY_SIZE(spec->autocfg.speaker_pins); i++) {
			pin = spec->autocfg.speaker_pins[i];
			if (!pin)
				break;
			dac = spec->multiout.extra_out_nid[0];
			if (!dac)
				dac = spec->multiout.dac_nids[0]; /* to front */
			alc882_auto_set_output_and_unmute(codec, pin, PIN_OUT, dac);
		}
	}
}

static void alc882_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		alc_set_input_pin(codec, nid, cfg->inputs[i].type);
		if (get_wcaps(codec, nid) & AC_WCAP_OUT_AMP)
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_AMP_GAIN_MUTE,
					    AMP_OUT_MUTE);
	}
}

static void alc882_auto_init_input_src(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int c;

	for (c = 0; c < spec->num_adc_nids; c++) {
		hda_nid_t conn_list[HDA_MAX_NUM_INPUTS];
		hda_nid_t nid = spec->capsrc_nids[c];
		unsigned int mux_idx;
		const struct hda_input_mux *imux;
		int conns, mute, idx, item;

		/* mute ADC */
		snd_hda_codec_write(codec, spec->adc_nids[c], 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_IN_MUTE(0));

		conns = snd_hda_get_connections(codec, nid, conn_list,
						ARRAY_SIZE(conn_list));
		if (conns < 0)
			continue;
		mux_idx = c >= spec->num_mux_defs ? 0 : c;
		imux = &spec->input_mux[mux_idx];
		if (!imux->num_items && mux_idx > 0)
			imux = &spec->input_mux[0];
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
			/* check if we have a selector or mixer
			 * we could check for the widget type instead, but
			 * just check for Amp-In presence (in case of mixer
			 * without amp-in there is something wrong, this
			 * function shouldn't be used or capsrc nid is wrong)
			 */
			if (get_wcaps(codec, nid) & AC_WCAP_IN_AMP)
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_AMP_GAIN_MUTE,
						    mute);
			else if (mute != AMP_IN_MUTE(idx))
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_CONNECT_SEL,
						    idx);
		}
	}
}

/* add mic boosts if needed */
static int alc_auto_add_mic_boost(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i, err;
	int type_idx = 0;
	hda_nid_t nid;
	const char *prev_label = NULL;

	for (i = 0; i < cfg->num_inputs; i++) {
		if (cfg->inputs[i].type > AUTO_PIN_MIC)
			break;
		nid = cfg->inputs[i].pin;
		if (get_wcaps(codec, nid) & AC_WCAP_IN_AMP) {
			const char *label;
			char boost_label[32];

			label = hda_get_autocfg_input_label(codec, cfg, i);
			if (prev_label && !strcmp(label, prev_label))
				type_idx++;
			else
				type_idx = 0;
			prev_label = label;

			snprintf(boost_label, sizeof(boost_label),
				 "%s Boost Volume", label);
			err = add_control(spec, ALC_CTL_WIDGET_VOL,
					  boost_label, type_idx,
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_INPUT));
			if (err < 0)
				return err;
		}
	}
	return 0;
}

/* almost identical with ALC880 parser... */
static int alc882_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	static const hda_nid_t alc882_ignore[] = { 0x1d, 0 };
	int err;

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc882_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */

	err = alc880_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc_auto_add_multi_channel_mode(codec);
	if (err < 0)
		return err;
	err = alc880_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc880_auto_create_extra_out(spec, spec->autocfg.hp_pins[0],
					   "Headphone");
	if (err < 0)
		return err;
	err = alc880_auto_create_extra_out(spec,
					   spec->autocfg.speaker_pins[0],
					   "Speaker");
	if (err < 0)
		return err;
	err = alc882_auto_create_input_ctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	alc_auto_parse_digital(codec);

	if (spec->kctls.list)
		add_mixer(spec, spec->kctls.list);

	add_verb(spec, alc883_auto_init_verbs);
	/* if ADC 0x07 is available, initialize it, too */
	if (get_wcaps_type(get_wcaps(codec, 0x07)) == AC_WID_AUD_IN)
		add_verb(spec, alc882_adc1_init_verbs);

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux[0];

	alc_ssid_check(codec, 0x15, 0x1b, 0x14, 0);

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

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
	alc_auto_init_digital(codec);
	if (spec->unsol_event)
		alc_inithook(codec);
}

static int patch_alc882(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	switch (codec->vendor_id) {
	case 0x10ec0882:
	case 0x10ec0885:
		break;
	default:
		/* ALC883 and variants */
		alc_fix_pll_init(codec, 0x20, 0x0a, 10);
		break;
	}

	board_config = snd_hda_check_board_config(codec, ALC882_MODEL_LAST,
						  alc882_models,
						  alc882_cfg_tbl);

	if (board_config < 0 || board_config >= ALC882_MODEL_LAST)
		board_config = snd_hda_check_board_codec_sid_config(codec,
			ALC882_MODEL_LAST, alc882_models, alc882_ssid_cfg_tbl);

	if (board_config < 0 || board_config >= ALC882_MODEL_LAST) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC882_AUTO;
	}

	if (board_config == ALC882_AUTO) {
		alc_pick_fixup(codec, NULL, alc882_fixup_tbl, alc882_fixups);
		alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);
	}

	alc_auto_parse_customize_define(codec);

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

	if (has_cdefine_beep(codec)) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
	}

	if (board_config != ALC882_AUTO)
		setup_preset(codec, &alc882_presets[board_config]);

	spec->stream_analog_playback = &alc882_pcm_analog_playback;
	spec->stream_analog_capture = &alc882_pcm_analog_capture;
	/* FIXME: setup DAC5 */
	/*spec->stream_analog_alt_playback = &alc880_pcm_analog_alt_playback;*/
	spec->stream_analog_alt_capture = &alc880_pcm_analog_alt_capture;

	spec->stream_digital_playback = &alc882_pcm_digital_playback;
	spec->stream_digital_capture = &alc882_pcm_digital_capture;

	if (!spec->adc_nids && spec->input_mux) {
		int i, j;
		spec->num_adc_nids = 0;
		for (i = 0; i < ARRAY_SIZE(alc882_adc_nids); i++) {
			const struct hda_input_mux *imux = spec->input_mux;
			hda_nid_t cap;
			hda_nid_t items[16];
			hda_nid_t nid = alc882_adc_nids[i];
			unsigned int wcap = get_wcaps(codec, nid);
			/* get type */
			wcap = get_wcaps_type(wcap);
			if (wcap != AC_WID_AUD_IN)
				continue;
			spec->private_adc_nids[spec->num_adc_nids] = nid;
			err = snd_hda_get_connections(codec, nid, &cap, 1);
			if (err < 0)
				continue;
			err = snd_hda_get_connections(codec, cap, items,
						      ARRAY_SIZE(items));
			if (err < 0)
				continue;
			for (j = 0; j < imux->num_items; j++)
				if (imux->items[j].index >= err)
					break;
			if (j < imux->num_items)
				continue;
			spec->private_capsrc_nids[spec->num_adc_nids] = cap;
			spec->num_adc_nids++;
		}
		spec->adc_nids = spec->private_adc_nids;
		spec->capsrc_nids = spec->private_capsrc_nids;
	}

	set_capture_mixer(codec);

	if (has_cdefine_beep(codec))
		set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	spec->vmaster_nid = 0x0c;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC882_AUTO)
		spec->init_hook = alc882_auto_init;

	alc_init_jacks(codec);
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc882_loopbacks;
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

static const hda_nid_t alc262_dmic_adc_nids[1] = {
	/* ADC0 */
	0x09
};

static const hda_nid_t alc262_dmic_capsrc_nids[1] = { 0x22 };

static const struct snd_kcontrol_new alc262_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0D, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Mono Playback Volume", 0x0e, 2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Mono Playback Switch", 0x16, 2, 0x0, HDA_OUTPUT),
	{ } /* end */
};

/* update HP, line and mono-out pins according to the master switch */
#define alc262_hp_master_update		alc260_hp_master_update

static void alc262_hp_bpc_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x16;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_PIN;
}

static void alc262_hp_wildwest_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x16;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_PIN;
}

#define alc262_hp_master_sw_get		alc260_hp_master_sw_get
#define alc262_hp_master_sw_put		alc260_hp_master_sw_put

#define ALC262_HP_MASTER_SWITCH					\
	{							\
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,		\
		.name = "Master Playback Switch",		\
		.info = snd_ctl_boolean_mono_info,		\
		.get = alc262_hp_master_sw_get,			\
		.put = alc262_hp_master_sw_put,			\
	}, \
	{							\
		.iface = NID_MAPPING,				\
		.name = "Master Playback Switch",		\
		.private_value = 0x15 | (0x16 << 8) | (0x1b << 16),	\
	}


static const struct snd_kcontrol_new alc262_HP_BPC_mixer[] = {
	ALC262_HP_MASTER_SWITCH,
	HDA_CODEC_VOLUME("Front Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Speaker Playback Volume", 0x0e, 2, 0x0,
			      HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Speaker Playback Switch", 0x16, 2, 0x0,
			    HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("AUX IN Playback Volume", 0x0b, 0x06, HDA_INPUT),
	HDA_CODEC_MUTE("AUX IN Playback Switch", 0x0b, 0x06, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc262_HP_BPC_WildWest_mixer[] = {
	ALC262_HP_MASTER_SWITCH,
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
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x1a, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc262_HP_BPC_WildWest_option_mixer[] = {
	HDA_CODEC_VOLUME("Rear Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Rear Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Rear Mic Boost Volume", 0x18, 0, HDA_INPUT),
	{ } /* end */
};

/* mute/unmute internal speaker according to the hp jack and mute state */
static void alc262_hp_t5735_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_PIN;
}

static const struct snd_kcontrol_new alc262_hp_t5735_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	{ } /* end */
};

static const struct hda_verb alc262_hp_t5735_verbs[] = {
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},

	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ }
};

static const struct snd_kcontrol_new alc262_hp_rp5700_mixer[] = {
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0e, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x01, HDA_INPUT),
	{ } /* end */
};

static const struct hda_verb alc262_hp_rp5700_verbs[] = {
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

static const struct hda_input_mux alc262_hp_rp5700_capture_source = {
	.num_items = 1,
	.items = {
		{ "Line", 0x1 },
	},
};

/* bind hp and internal speaker mute (with plug check) as master switch */
#define alc262_hippo_master_update	alc262_hp_master_update
#define alc262_hippo_master_sw_get	alc262_hp_master_sw_get
#define alc262_hippo_master_sw_put	alc262_hp_master_sw_put

#define ALC262_HIPPO_MASTER_SWITCH				\
	{							\
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,		\
		.name = "Master Playback Switch",		\
		.info = snd_ctl_boolean_mono_info,		\
		.get = alc262_hippo_master_sw_get,		\
		.put = alc262_hippo_master_sw_put,		\
	},							\
	{							\
		.iface = NID_MAPPING,				\
		.name = "Master Playback Switch",		\
		.subdevice = SUBDEV_HP(0) | (SUBDEV_LINE(0) << 8) | \
			     (SUBDEV_SPEAKER(0) << 16), \
	}

static const struct snd_kcontrol_new alc262_hippo_mixer[] = {
	ALC262_HIPPO_MASTER_SWITCH,
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc262_hippo1_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	ALC262_HIPPO_MASTER_SWITCH,
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	{ } /* end */
};

/* mute/unmute internal speaker according to the hp jack and mute state */
static void alc262_hippo_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc262_hippo1_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}


static const struct snd_kcontrol_new alc262_sony_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	ALC262_HIPPO_MASTER_SWITCH,
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("ATAPI Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("ATAPI Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc262_benq_t31_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	ALC262_HIPPO_MASTER_SWITCH,
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("ATAPI Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("ATAPI Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc262_tyan_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Master Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Aux Playback Volume", 0x0b, 0x06, HDA_INPUT),
	HDA_CODEC_MUTE("Aux Playback Switch", 0x0b, 0x06, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	{ } /* end */
};

static const struct hda_verb alc262_tyan_verbs[] = {
	/* Headphone automute */
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* P11 AUX_IN, white 4-pin connector */
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x14, AC_VERB_SET_CONFIG_DEFAULT_BYTES_1, 0xe1},
	{0x14, AC_VERB_SET_CONFIG_DEFAULT_BYTES_2, 0x93},
	{0x14, AC_VERB_SET_CONFIG_DEFAULT_BYTES_3, 0x19},

	{}
};

/* unsolicited event for HP jack sensing */
static void alc262_tyan_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}


#define alc262_capture_mixer		alc882_capture_mixer
#define alc262_capture_alt_mixer	alc882_capture_alt_mixer

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static const struct hda_verb alc262_init_verbs[] = {
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

static const struct hda_verb alc262_eapd_verbs[] = {
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

static const struct hda_verb alc262_hippo1_unsol_verbs[] = {
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, 0x0000},

	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

static const struct hda_verb alc262_sony_unsol_verbs[] = {
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, 0xc0},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},	// Front Mic

	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

static const struct snd_kcontrol_new alc262_toshiba_s06_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct hda_verb alc262_toshiba_s06_verbs[] = {
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

static void alc262_toshiba_s06_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x12;
	spec->int_mic.mux_idx = 9;
	spec->auto_mic = 1;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_PIN;
}

/*
 * nec model
 *  0x15 = headphone
 *  0x16 = internal speaker
 *  0x18 = external mic
 */

static const struct snd_kcontrol_new alc262_nec_mixer[] = {
	HDA_CODEC_VOLUME_MONO("Speaker Playback Volume", 0x0e, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Speaker Playback Switch", 0x16, 0, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),

	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x0d, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static const struct hda_verb alc262_nec_verbs[] = {
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

#define ALC_HP_EVENT	ALC880_HP_EVENT

static const struct hda_verb alc262_fujitsu_unsol_verbs[] = {
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

static const struct hda_verb alc262_lenovo_3000_unsol_verbs[] = {
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC_HP_EVENT},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{}
};

static const struct hda_verb alc262_lenovo_3000_init_verbs[] = {
	/* Front Mic pin: input vref at 50% */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{}
};

static const struct hda_input_mux alc262_fujitsu_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x1 },
		{ "CD", 0x4 },
	},
};

static const struct hda_input_mux alc262_HP_capture_source = {
	.num_items = 5,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
		{ "AUX IN", 0x6 },
	},
};

static const struct hda_input_mux alc262_HP_D7000_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x2 },
		{ "Line", 0x1 },
		{ "CD", 0x4 },
	},
};

static void alc262_fujitsu_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.hp_pins[1] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

/* bind volumes of both NID 0x0c and 0x0d */
static const struct hda_bind_ctls alc262_fujitsu_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x0c, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x0d, 3, 0, HDA_OUTPUT),
		0
	},
};

static const struct snd_kcontrol_new alc262_fujitsu_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc262_fujitsu_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.subdevice = HDA_SUBDEV_NID_FLAG | 0x14,
		.info = snd_ctl_boolean_mono_info,
		.get = alc262_hp_master_sw_get,
		.put = alc262_hp_master_sw_put,
	},
	{
		.iface = NID_MAPPING,
		.name = "Master Playback Switch",
		.private_value = 0x1b,
	},
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static void alc262_lenovo_3000_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static const struct snd_kcontrol_new alc262_lenovo_3000_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc262_fujitsu_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.subdevice = HDA_SUBDEV_NID_FLAG | 0x1b,
		.info = snd_ctl_boolean_mono_info,
		.get = alc262_hp_master_sw_get,
		.put = alc262_hp_master_sw_put,
	},
	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc262_toshiba_rx1_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc262_fujitsu_bind_master_vol),
	ALC262_HIPPO_MASTER_SWITCH,
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	{ } /* end */
};

/* additional init verbs for Benq laptops */
static const struct hda_verb alc262_EAPD_verbs[] = {
	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3070},
	{}
};

static const struct hda_verb alc262_benq_t31_EAPD_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},

	{0x20, AC_VERB_SET_COEF_INDEX, 0x07},
	{0x20, AC_VERB_SET_PROC_COEF,  0x3050},
	{}
};

/* Samsung Q1 Ultra Vista model setup */
static const struct snd_kcontrol_new alc262_ultra_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x0c, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Master Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Mic Boost Volume", 0x15, 0, HDA_INPUT),
	{ } /* end */
};

static const struct hda_verb alc262_ultra_verbs[] = {
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
		spec->jack_present = snd_hda_jack_detect(codec, 0x15);
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

static const struct hda_input_mux alc262_ultra_capture_source = {
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

	ret = alc_mux_enum_put(kcontrol, ucontrol);
	if (!ret)
		return 0;
	/* reprogram the HP pin as mic or HP according to the input source */
	snd_hda_codec_write_cache(codec, 0x15, 0,
				  AC_VERB_SET_PIN_WIDGET_CONTROL,
				  spec->cur_mux[0] ? PIN_VREF80 : PIN_HP);
	alc262_ultra_automute(codec); /* mute/unmute HP */
	return ret;
}

static const struct snd_kcontrol_new alc262_ultra_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x07, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x07, 0x0, HDA_INPUT),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = alc_mux_enum_info,
		.get = alc_mux_enum_get,
		.put = alc262_ultra_mux_enum_put,
	},
	{
		.iface = NID_MAPPING,
		.name = "Capture Source",
		.private_value = 0x15,
	},
	{ } /* end */
};

/* We use two mixers depending on the output pin; 0x16 is a mono output
 * and thus it's bound with a different mixer.
 * This function returns which mixer amp should be used.
 */
static int alc262_check_volbit(hda_nid_t nid)
{
	if (!nid)
		return 0;
	else if (nid == 0x16)
		return 2;
	else
		return 1;
}

static int alc262_add_out_vol_ctl(struct alc_spec *spec, hda_nid_t nid,
				  const char *pfx, int *vbits, int idx)
{
	unsigned long val;
	int vbit;

	vbit = alc262_check_volbit(nid);
	if (!vbit)
		return 0;
	if (*vbits & vbit) /* a volume control for this mixer already there */
		return 0;
	*vbits |= vbit;
	if (vbit == 2)
		val = HDA_COMPOSE_AMP_VAL(0x0e, 2, 0, HDA_OUTPUT);
	else
		val = HDA_COMPOSE_AMP_VAL(0x0c, 3, 0, HDA_OUTPUT);
	return __add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL, pfx, idx, val);
}

static int alc262_add_out_sw_ctl(struct alc_spec *spec, hda_nid_t nid,
				 const char *pfx, int idx)
{
	unsigned long val;

	if (!nid)
		return 0;
	if (nid == 0x16)
		val = HDA_COMPOSE_AMP_VAL(nid, 2, 0, HDA_OUTPUT);
	else
		val = HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_OUTPUT);
	return __add_pb_sw_ctrl(spec, ALC_CTL_WIDGET_MUTE, pfx, idx, val);
}

/* add playback controls from the parsed DAC table */
static int alc262_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	const char *pfx;
	int vbits;
	int i, err;

	spec->multiout.num_dacs = 1;	/* only use one dac */
	spec->multiout.dac_nids = spec->private_dac_nids;
	spec->private_dac_nids[0] = 2;

	pfx = alc_get_line_out_pfx(spec, true);
	if (!pfx)
		pfx = "Front";
	for (i = 0; i < 2; i++) {
		err = alc262_add_out_sw_ctl(spec, cfg->line_out_pins[i], pfx, i);
		if (err < 0)
			return err;
		if (cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
			err = alc262_add_out_sw_ctl(spec, cfg->speaker_pins[i],
						    "Speaker", i);
			if (err < 0)
				return err;
		}
		if (cfg->line_out_type != AUTO_PIN_HP_OUT) {
			err = alc262_add_out_sw_ctl(spec, cfg->hp_pins[i],
						    "Headphone", i);
			if (err < 0)
				return err;
		}
	}

	vbits = alc262_check_volbit(cfg->line_out_pins[0]) |
		alc262_check_volbit(cfg->speaker_pins[0]) |
		alc262_check_volbit(cfg->hp_pins[0]);
	if (vbits == 1 || vbits == 2)
		pfx = "Master"; /* only one mixer is used */
	vbits = 0;
	for (i = 0; i < 2; i++) {
		err = alc262_add_out_vol_ctl(spec, cfg->line_out_pins[i], pfx,
					     &vbits, i);
		if (err < 0)
			return err;
		if (cfg->line_out_type != AUTO_PIN_SPEAKER_OUT) {
			err = alc262_add_out_vol_ctl(spec, cfg->speaker_pins[i],
						     "Speaker", &vbits, i);
			if (err < 0)
				return err;
		}
		if (cfg->line_out_type != AUTO_PIN_HP_OUT) {
			err = alc262_add_out_vol_ctl(spec, cfg->hp_pins[i],
						     "Headphone", &vbits, i);
			if (err < 0)
				return err;
		}
	}
	return 0;
}

#define alc262_auto_create_input_ctls \
	alc882_auto_create_input_ctls

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static const struct hda_verb alc262_volume_init_verbs[] = {
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

static const struct hda_verb alc262_HP_BPC_init_verbs[] = {
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

	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
        {0x19, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
	{0x1c, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, 0x7000 },


	/* FIXME: use matrix-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1c, 1d, 14, 15, 0b, 12 */
	/* Input mixer1: only unmute Mic */
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x05 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x06 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x07 << 8))},
	{0x24, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x08 << 8))},
	/* Input mixer2 */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x05 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x06 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x07 << 8))},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x08 << 8))},
	/* Input mixer3 */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x00 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x02 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x03 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x04 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x05 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x06 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x07 << 8))},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x08 << 8))},

	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},

	{ }
};

static const struct hda_verb alc262_HP_BPC_WildWest_init_verbs[] = {
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

static const struct hda_verb alc262_toshiba_rx1_unsol_verbs[] = {

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },	/* Front Speaker */
	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{0x14, AC_VERB_SET_CONNECT_SEL, 0x01},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },	/* MIC jack */
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },	/* Front MIC */
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) },
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0) },

	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP },	/* HP  jack */
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

/*
 * Pin config fixes
 */
enum {
	PINFIX_FSC_H270,
	PINFIX_HP_Z200,
};

static const struct alc_fixup alc262_fixups[] = {
	[PINFIX_FSC_H270] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x14, 0x99130110 }, /* speaker */
			{ 0x15, 0x0221142f }, /* front HP */
			{ 0x1b, 0x0121141f }, /* rear HP */
			{ }
		}
	},
	[PINFIX_HP_Z200] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x16, 0x99130120 }, /* internal speaker */
			{ }
		}
	},
};

static const struct snd_pci_quirk alc262_fixup_tbl[] = {
	SND_PCI_QUIRK(0x103c, 0x170b, "HP Z200", PINFIX_HP_Z200),
	SND_PCI_QUIRK(0x1734, 0x1147, "FSC Celsius H270", PINFIX_FSC_H270),
	{}
};


#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc262_loopbacks	alc880_loopbacks
#endif

/* pcm configuration: identical with ALC880 */
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
	static const hda_nid_t alc262_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc262_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs) {
		if (spec->autocfg.dig_outs || spec->autocfg.dig_in_pin) {
			spec->multiout.max_channels = 2;
			spec->no_analog = 1;
			goto dig_only;
		}
		return 0; /* can't find valid BIOS pin config */
	}
	err = alc262_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc262_auto_create_input_ctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

 dig_only:
	alc_auto_parse_digital(codec);

	if (spec->kctls.list)
		add_mixer(spec, spec->kctls.list);

	add_verb(spec, alc262_volume_init_verbs);
	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux[0];

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	alc_ssid_check(codec, 0x15, 0x1b, 0x14, 0);

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
	alc_auto_init_digital(codec);
	if (spec->unsol_event)
		alc_inithook(codec);
}

/*
 * configuration and preset
 */
static const char * const alc262_models[ALC262_MODEL_LAST] = {
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
	[ALC262_TOSHIBA_RX1]	= "toshiba-rx1",
	[ALC262_ULTRA]		= "ultra",
	[ALC262_LENOVO_3000]	= "lenovo-3000",
	[ALC262_NEC]		= "nec",
	[ALC262_TYAN]		= "tyan",
	[ALC262_AUTO]		= "auto",
};

static const struct snd_pci_quirk alc262_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1002, 0x437b, "Hippo", ALC262_HIPPO),
	SND_PCI_QUIRK(0x1033, 0x8895, "NEC Versa S9100", ALC262_NEC),
	SND_PCI_QUIRK_MASK(0x103c, 0xff00, 0x1200, "HP xw series",
			   ALC262_HP_BPC),
	SND_PCI_QUIRK_MASK(0x103c, 0xff00, 0x1300, "HP xw series",
			   ALC262_HP_BPC),
	SND_PCI_QUIRK_MASK(0x103c, 0xff00, 0x1500, "HP z series",
			   ALC262_HP_BPC),
	SND_PCI_QUIRK(0x103c, 0x170b, "HP Z200",
			   ALC262_AUTO),
	SND_PCI_QUIRK_MASK(0x103c, 0xff00, 0x1700, "HP xw series",
			   ALC262_HP_BPC),
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
	SND_PCI_QUIRK(0x104d, 0x9016, "Sony VAIO", ALC262_AUTO), /* dig-only */
	SND_PCI_QUIRK(0x104d, 0x9025, "Sony VAIO Z21MN", ALC262_TOSHIBA_S06),
	SND_PCI_QUIRK(0x104d, 0x9035, "Sony VAIO VGN-FW170J", ALC262_AUTO),
	SND_PCI_QUIRK(0x104d, 0x9047, "Sony VAIO Type G", ALC262_AUTO),
#if 0 /* disable the quirk since model=auto works better in recent versions */
	SND_PCI_QUIRK_MASK(0x104d, 0xff00, 0x9000, "Sony VAIO",
			   ALC262_SONY_ASSAMD),
#endif
	SND_PCI_QUIRK(0x1179, 0x0001, "Toshiba dynabook SS RX1",
		      ALC262_TOSHIBA_RX1),
	SND_PCI_QUIRK(0x1179, 0xff7b, "Toshiba S06", ALC262_TOSHIBA_S06),
	SND_PCI_QUIRK(0x10cf, 0x1397, "Fujitsu", ALC262_FUJITSU),
	SND_PCI_QUIRK(0x10cf, 0x142d, "Fujitsu Lifebook E8410", ALC262_FUJITSU),
	SND_PCI_QUIRK(0x10f1, 0x2915, "Tyan Thunder n6650W", ALC262_TYAN),
	SND_PCI_QUIRK_MASK(0x144d, 0xff00, 0xc032, "Samsung Q1",
			   ALC262_ULTRA),
	SND_PCI_QUIRK(0x144d, 0xc510, "Samsung Q45", ALC262_HIPPO),
	SND_PCI_QUIRK(0x17aa, 0x384e, "Lenovo 3000 y410", ALC262_LENOVO_3000),
	SND_PCI_QUIRK(0x17ff, 0x0560, "Benq ED8", ALC262_BENQ_ED8),
	SND_PCI_QUIRK(0x17ff, 0x058d, "Benq T31-16", ALC262_BENQ_T31),
	SND_PCI_QUIRK(0x17ff, 0x058f, "Benq Hippo", ALC262_HIPPO_1),
	{}
};

static const struct alc_config_preset alc262_presets[] = {
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
		.mixers = { alc262_hippo_mixer },
		.init_verbs = { alc262_init_verbs, alc_hp15_unsol_verbs},
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.dig_out_nid = ALC262_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc262_hippo_setup,
		.init_hook = alc_inithook,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc262_hippo1_setup,
		.init_hook = alc_inithook,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc262_fujitsu_setup,
		.init_hook = alc_inithook,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc262_hp_bpc_setup,
		.init_hook = alc_inithook,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc262_hp_wildwest_setup,
		.init_hook = alc_inithook,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc262_hp_wildwest_setup,
		.init_hook = alc_inithook,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc262_hp_t5735_setup,
		.init_hook = alc_inithook,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc262_hippo_setup,
		.init_hook = alc_inithook,
	},
	[ALC262_BENQ_T31] = {
		.mixers = { alc262_benq_t31_mixer },
		.init_verbs = { alc262_init_verbs, alc262_benq_t31_EAPD_verbs,
				alc_hp15_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc262_hippo_setup,
		.init_hook = alc_inithook,
	},
	[ALC262_ULTRA] = {
		.mixers = { alc262_ultra_mixer },
		.cap_mixer = alc262_ultra_capture_mixer,
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
				alc262_lenovo_3000_unsol_verbs,
				alc262_lenovo_3000_init_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.dig_out_nid = ALC262_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_fujitsu_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc262_lenovo_3000_setup,
		.init_hook = alc_inithook,
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
		.num_adc_nids = 1, /* single ADC */
		.dig_out_nid = ALC262_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc262_toshiba_s06_setup,
		.init_hook = alc_inithook,
	},
	[ALC262_TOSHIBA_RX1] = {
		.mixers = { alc262_toshiba_rx1_mixer },
		.init_verbs = { alc262_init_verbs, alc262_toshiba_rx1_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc262_hippo_setup,
		.init_hook = alc_inithook,
	},
	[ALC262_TYAN] = {
		.mixers = { alc262_tyan_mixer },
		.init_verbs = { alc262_init_verbs, alc262_tyan_verbs},
		.num_dacs = ARRAY_SIZE(alc262_dac_nids),
		.dac_nids = alc262_dac_nids,
		.hp_nid = 0x02,
		.dig_out_nid = ALC262_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc262_modes),
		.channel_mode = alc262_modes,
		.input_mux = &alc262_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc262_tyan_setup,
		.init_hook = alc_hp_automute,
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
	alc_auto_parse_customize_define(codec);

	alc_fix_pll_init(codec, 0x20, 0x0a, 10);

	board_config = snd_hda_check_board_config(codec, ALC262_MODEL_LAST,
						  alc262_models,
						  alc262_cfg_tbl);

	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC262_AUTO;
	}

	if (board_config == ALC262_AUTO) {
		alc_pick_fixup(codec, NULL, alc262_fixup_tbl, alc262_fixups);
		alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);
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

	if (!spec->no_analog && has_cdefine_beep(codec)) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
	}

	if (board_config != ALC262_AUTO)
		setup_preset(codec, &alc262_presets[board_config]);

	spec->stream_analog_playback = &alc262_pcm_analog_playback;
	spec->stream_analog_capture = &alc262_pcm_analog_capture;

	spec->stream_digital_playback = &alc262_pcm_digital_playback;
	spec->stream_digital_capture = &alc262_pcm_digital_capture;

	if (!spec->adc_nids && spec->input_mux) {
		int i;
		/* check whether the digital-mic has to be supported */
		for (i = 0; i < spec->input_mux->num_items; i++) {
			if (spec->input_mux->items[i].index >= 9)
				break;
		}
		if (i < spec->input_mux->num_items) {
			/* use only ADC0 */
			spec->adc_nids = alc262_dmic_adc_nids;
			spec->num_adc_nids = 1;
			spec->capsrc_nids = alc262_dmic_capsrc_nids;
		} else {
			/* all analog inputs */
			/* check whether NID 0x07 is valid */
			unsigned int wcap = get_wcaps(codec, 0x07);

			/* get type */
			wcap = get_wcaps_type(wcap);
			if (wcap != AC_WID_AUD_IN) {
				spec->adc_nids = alc262_adc_nids_alt;
				spec->num_adc_nids =
					ARRAY_SIZE(alc262_adc_nids_alt);
				spec->capsrc_nids = alc262_capsrc_nids_alt;
			} else {
				spec->adc_nids = alc262_adc_nids;
				spec->num_adc_nids =
					ARRAY_SIZE(alc262_adc_nids);
				spec->capsrc_nids = alc262_capsrc_nids;
			}
		}
	}
	if (!spec->cap_mixer && !spec->no_analog)
		set_capture_mixer(codec);
	if (!spec->no_analog && has_cdefine_beep(codec))
		set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	spec->vmaster_nid = 0x0c;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC262_AUTO)
		spec->init_hook = alc262_auto_init;
	spec->shutup = alc_eapd_shutup;

	alc_init_jacks(codec);
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

static const hda_nid_t alc268_dac_nids[2] = {
	/* front, hp */
	0x02, 0x03
};

static const hda_nid_t alc268_adc_nids[2] = {
	/* ADC0-1 */
	0x08, 0x07
};

static const hda_nid_t alc268_adc_nids_alt[1] = {
	/* ADC0 */
	0x08
};

static const hda_nid_t alc268_capsrc_nids[2] = { 0x23, 0x24 };

static const struct snd_kcontrol_new alc268_base_mixer[] = {
	/* output mixer control */
	HDA_CODEC_VOLUME("Front Playback Volume", 0x2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x3, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line In Boost Volume", 0x1a, 0, HDA_INPUT),
	{ }
};

static const struct snd_kcontrol_new alc268_toshiba_mixer[] = {
	/* output mixer control */
	HDA_CODEC_VOLUME("Front Playback Volume", 0x2, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x3, 0x0, HDA_OUTPUT),
	ALC262_HIPPO_MASTER_SWITCH,
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line In Boost Volume", 0x1a, 0, HDA_INPUT),
	{ }
};

/* bind Beep switches of both NID 0x0f and 0x10 */
static const struct hda_bind_ctls alc268_bind_beep_sw = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x0f, 3, 1, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x10, 3, 1, HDA_INPUT),
		0
	},
};

static const struct snd_kcontrol_new alc268_beep_mixer[] = {
	HDA_CODEC_VOLUME("Beep Playback Volume", 0x1d, 0x0, HDA_INPUT),
	HDA_BIND_SW("Beep Playback Switch", &alc268_bind_beep_sw),
	{ }
};

static const struct hda_verb alc268_eapd_verbs[] = {
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

/* Toshiba specific */
static const struct hda_verb alc268_toshiba_verbs[] = {
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{ } /* end */
};

/* Acer specific */
/* bind volumes of both NID 0x02 and 0x03 */
static const struct hda_bind_ctls alc268_acer_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x02, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x03, 3, 0, HDA_OUTPUT),
		0
	},
};

static void alc268_acer_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

#define alc268_acer_master_sw_get	alc262_hp_master_sw_get
#define alc268_acer_master_sw_put	alc262_hp_master_sw_put

static const struct snd_kcontrol_new alc268_acer_aspire_one_mixer[] = {
	/* output mixer control */
	HDA_BIND_VOL("Master Playback Volume", &alc268_acer_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.subdevice = HDA_SUBDEV_NID_FLAG | 0x15,
		.info = snd_ctl_boolean_mono_info,
		.get = alc268_acer_master_sw_get,
		.put = alc268_acer_master_sw_put,
	},
	HDA_CODEC_VOLUME("Mic Boost Capture Volume", 0x18, 0, HDA_INPUT),
	{ }
};

static const struct snd_kcontrol_new alc268_acer_mixer[] = {
	/* output mixer control */
	HDA_BIND_VOL("Master Playback Volume", &alc268_acer_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.subdevice = HDA_SUBDEV_NID_FLAG | 0x14,
		.info = snd_ctl_boolean_mono_info,
		.get = alc268_acer_master_sw_get,
		.put = alc268_acer_master_sw_put,
	},
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line In Boost Volume", 0x1a, 0, HDA_INPUT),
	{ }
};

static const struct snd_kcontrol_new alc268_acer_dmic_mixer[] = {
	/* output mixer control */
	HDA_BIND_VOL("Master Playback Volume", &alc268_acer_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.subdevice = HDA_SUBDEV_NID_FLAG | 0x14,
		.info = snd_ctl_boolean_mono_info,
		.get = alc268_acer_master_sw_get,
		.put = alc268_acer_master_sw_put,
	},
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line In Boost Volume", 0x1a, 0, HDA_INPUT),
	{ }
};

static const struct hda_verb alc268_acer_aspire_one_verbs[] = {
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x23, AC_VERB_SET_CONNECT_SEL, 0x06},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, 0xa017},
	{ }
};

static const struct hda_verb alc268_acer_verbs[] = {
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
#define alc268_toshiba_setup		alc262_hippo_setup

static void alc268_acer_lc_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x12;
	spec->int_mic.mux_idx = 6;
	spec->auto_mic = 1;
}

static const struct snd_kcontrol_new alc268_dell_mixer[] = {
	/* output mixer control */
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	{ }
};

static const struct hda_verb alc268_dell_verbs[] = {
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_MIC_EVENT | AC_USRSP_EN},
	{ }
};

/* mute/unmute internal speaker according to the hp jack and mute state */
static void alc268_dell_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x19;
	spec->int_mic.mux_idx = 1;
	spec->auto_mic = 1;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_PIN;
}

static const struct snd_kcontrol_new alc267_quanta_il1_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x3, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Capture Volume", 0x23, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Mic Capture Switch", 0x23, 2, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	{ }
};

static const struct hda_verb alc267_quanta_il1_verbs[] = {
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_MIC_EVENT | AC_USRSP_EN},
	{ }
};

static void alc267_quanta_il1_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x19;
	spec->int_mic.mux_idx = 1;
	spec->auto_mic = 1;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_PIN;
}

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static const struct hda_verb alc268_base_init_verbs[] = {
	/* Unmute DAC0-1 and set vol = 0 */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	/*
	 * Set up output mixers (0x0c - 0x0e)
	 */
	/* set vol=0 to output mixers */
	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
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
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

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
static const struct hda_verb alc268_volume_init_verbs[] = {
	/* set output DAC */
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO},

	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x1c, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x20},

	{0x0e, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	/* set PCBEEP vol = 0, mute connections */
	{0x1d, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x10, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	{ }
};

static const struct snd_kcontrol_new alc268_capture_nosrc_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x23, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x23, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc268_capture_alt_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x23, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x23, 0x0, HDA_OUTPUT),
	_DEFINE_CAPSRC(1),
	{ } /* end */
};

static const struct snd_kcontrol_new alc268_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x23, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x23, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_IDX("Capture Volume", 1, 0x24, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_IDX("Capture Switch", 1, 0x24, 0x0, HDA_OUTPUT),
	_DEFINE_CAPSRC(2),
	{ } /* end */
};

static const struct hda_input_mux alc268_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x3 },
	},
};

static const struct hda_input_mux alc268_acer_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x1 },
		{ "Line", 0x2 },
	},
};

static const struct hda_input_mux alc268_acer_dmic_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x6 },
		{ "Line", 0x2 },
	},
};

#ifdef CONFIG_SND_DEBUG
static const struct snd_kcontrol_new alc268_test_mixer[] = {
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
	hda_nid_t dac;
	int err;

	switch (nid) {
	case 0x14:
	case 0x16:
		dac = 0x02;
		break;
	case 0x15:
	case 0x1a: /* ALC259/269 only */
	case 0x1b: /* ALC259/269 only */
	case 0x21: /* ALC269vb has this pin, too */
		dac = 0x03;
		break;
	default:
		snd_printd(KERN_WARNING "hda_codec: "
			   "ignoring pin 0x%x as unknown\n", nid);
		return 0;
	}
	if (spec->multiout.dac_nids[0] != dac &&
	    spec->multiout.dac_nids[1] != dac) {
		err = add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL, ctlname,
				  HDA_COMPOSE_AMP_VAL(dac, 3, idx,
						      HDA_OUTPUT));
		if (err < 0)
			return err;
		spec->private_dac_nids[spec->multiout.num_dacs++] = dac;
	}

	if (nid != 0x16)
		err = add_pb_sw_ctrl(spec, ALC_CTL_WIDGET_MUTE, ctlname,
			  HDA_COMPOSE_AMP_VAL(nid, 3, idx, HDA_OUTPUT));
	else /* mono */
		err = add_pb_sw_ctrl(spec, ALC_CTL_WIDGET_MUTE, ctlname,
			  HDA_COMPOSE_AMP_VAL(nid, 2, idx, HDA_OUTPUT));
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

	spec->multiout.dac_nids = spec->private_dac_nids;

	nid = cfg->line_out_pins[0];
	if (nid) {
		const char *name;
		if (cfg->line_out_type == AUTO_PIN_SPEAKER_OUT)
			name = "Speaker";
		else
			name = "Front";
		err = alc268_new_analog_output(spec, nid, name, 0);
		if (err < 0)
			return err;
	}

	nid = cfg->speaker_pins[0];
	if (nid == 0x1d) {
		err = add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL, "Speaker",
				  HDA_COMPOSE_AMP_VAL(nid, 3, 0, HDA_INPUT));
		if (err < 0)
			return err;
	} else if (nid) {
		err = alc268_new_analog_output(spec, nid, "Speaker", 0);
		if (err < 0)
			return err;
	}
	nid = cfg->hp_pins[0];
	if (nid) {
		err = alc268_new_analog_output(spec, nid, "Headphone", 0);
		if (err < 0)
			return err;
	}

	nid = cfg->line_out_pins[1] | cfg->line_out_pins[2];
	if (nid == 0x16) {
		err = add_pb_sw_ctrl(spec, ALC_CTL_WIDGET_MUTE, "Mono",
				  HDA_COMPOSE_AMP_VAL(nid, 2, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
	}
	return 0;
}

/* create playback/capture controls for input pins */
static int alc268_auto_create_input_ctls(struct hda_codec *codec,
						const struct auto_pin_cfg *cfg)
{
	return alc_auto_create_input_ctls(codec, cfg, 0, 0x23, 0x24);
}

static void alc268_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid, int pin_type)
{
	int idx;

	alc_set_pin_output(codec, nid, pin_type);
	if (nid == 0x14 || nid == 0x16)
		idx = 0;
	else
		idx = 1;
	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_CONNECT_SEL, idx);
}

static void alc268_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->autocfg.line_outs; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		alc268_auto_set_output_and_unmute(codec, nid, pin_type);
	}
}

static void alc268_auto_init_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;
	int i;

	for (i = 0; i < spec->autocfg.hp_outs; i++) {
		pin = spec->autocfg.hp_pins[i];
		alc268_auto_set_output_and_unmute(codec, pin, PIN_HP);
	}
	for (i = 0; i < spec->autocfg.speaker_outs; i++) {
		pin = spec->autocfg.speaker_pins[i];
		alc268_auto_set_output_and_unmute(codec, pin, PIN_OUT);
	}
	if (spec->autocfg.mono_out_pin)
		snd_hda_codec_write(codec, spec->autocfg.mono_out_pin, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
}

static void alc268_auto_init_mono_speaker_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t speaker_nid = spec->autocfg.speaker_pins[0];
	hda_nid_t hp_nid = spec->autocfg.hp_pins[0];
	hda_nid_t line_nid = spec->autocfg.line_out_pins[0];
	unsigned int	dac_vol1, dac_vol2;

	if (line_nid == 0x1d || speaker_nid == 0x1d) {
		snd_hda_codec_write(codec, speaker_nid, 0,
				    AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT);
		/* mute mixer inputs from 0x1d */
		snd_hda_codec_write(codec, 0x0f, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_IN_UNMUTE(1));
		snd_hda_codec_write(codec, 0x10, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_IN_UNMUTE(1));
	} else {
		/* unmute mixer inputs from 0x1d */
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

/* pcm configuration: identical with ALC880 */
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
	static const hda_nid_t alc268_ignore[] = { 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc268_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs) {
		if (spec->autocfg.dig_outs || spec->autocfg.dig_in_pin) {
			spec->multiout.max_channels = 2;
			spec->no_analog = 1;
			goto dig_only;
		}
		return 0; /* can't find valid BIOS pin config */
	}
	err = alc268_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc268_auto_create_input_ctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = 2;

 dig_only:
	/* digital only support output */
	alc_auto_parse_digital(codec);
	if (spec->kctls.list)
		add_mixer(spec, spec->kctls.list);

	if (!spec->no_analog && spec->autocfg.speaker_pins[0] != 0x1d)
		add_mixer(spec, alc268_beep_mixer);

	add_verb(spec, alc268_volume_init_verbs);
	spec->num_mux_defs = 2;
	spec->input_mux = &spec->private_imux[0];

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	alc_ssid_check(codec, 0x15, 0x1b, 0x14, 0);

	return 1;
}

#define alc268_auto_init_analog_input	alc882_auto_init_analog_input
#define alc268_auto_init_input_src	alc882_auto_init_input_src

/* init callback for auto-configuration model -- overriding the default init */
static void alc268_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc268_auto_init_multi_out(codec);
	alc268_auto_init_hp_out(codec);
	alc268_auto_init_mono_speaker_out(codec);
	alc268_auto_init_analog_input(codec);
	alc268_auto_init_input_src(codec);
	alc_auto_init_digital(codec);
	if (spec->unsol_event)
		alc_inithook(codec);
}

/*
 * configuration and preset
 */
static const char * const alc268_models[ALC268_MODEL_LAST] = {
	[ALC267_QUANTA_IL1]	= "quanta-il1",
	[ALC268_3ST]		= "3stack",
	[ALC268_TOSHIBA]	= "toshiba",
	[ALC268_ACER]		= "acer",
	[ALC268_ACER_DMIC]	= "acer-dmic",
	[ALC268_ACER_ASPIRE_ONE]	= "acer-aspire",
	[ALC268_DELL]		= "dell",
	[ALC268_ZEPTO]		= "zepto",
#ifdef CONFIG_SND_DEBUG
	[ALC268_TEST]		= "test",
#endif
	[ALC268_AUTO]		= "auto",
};

static const struct snd_pci_quirk alc268_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x011e, "Acer Aspire 5720z", ALC268_ACER),
	SND_PCI_QUIRK(0x1025, 0x0126, "Acer", ALC268_ACER),
	SND_PCI_QUIRK(0x1025, 0x012e, "Acer Aspire 5310", ALC268_ACER),
	SND_PCI_QUIRK(0x1025, 0x0130, "Acer Extensa 5210", ALC268_ACER),
	SND_PCI_QUIRK(0x1025, 0x0136, "Acer Aspire 5315", ALC268_ACER),
	SND_PCI_QUIRK(0x1025, 0x015b, "Acer Aspire One",
						ALC268_ACER_ASPIRE_ONE),
	SND_PCI_QUIRK(0x1028, 0x0253, "Dell OEM", ALC268_DELL),
	SND_PCI_QUIRK(0x1028, 0x02b0, "Dell Inspiron 910", ALC268_AUTO),
	SND_PCI_QUIRK_MASK(0x1028, 0xfff0, 0x02b0,
			"Dell Inspiron Mini9/Vostro A90", ALC268_DELL),
	/* almost compatible with toshiba but with optional digital outs;
	 * auto-probing seems working fine
	 */
	SND_PCI_QUIRK_MASK(0x103c, 0xff00, 0x3000, "HP TX25xx series",
			   ALC268_AUTO),
	SND_PCI_QUIRK(0x1043, 0x1205, "ASUS W7J", ALC268_3ST),
	SND_PCI_QUIRK(0x1170, 0x0040, "ZEPTO", ALC268_ZEPTO),
	SND_PCI_QUIRK(0x14c0, 0x0025, "COMPAL IFL90/JFL-92", ALC268_TOSHIBA),
	SND_PCI_QUIRK(0x152d, 0x0771, "Quanta IL1", ALC267_QUANTA_IL1),
	{}
};

/* Toshiba laptops have no unique PCI SSID but only codec SSID */
static const struct snd_pci_quirk alc268_ssid_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1179, 0xff0a, "TOSHIBA X-200", ALC268_AUTO),
	SND_PCI_QUIRK(0x1179, 0xff0e, "TOSHIBA X-200 HDMI", ALC268_AUTO),
	SND_PCI_QUIRK_MASK(0x1179, 0xff00, 0xff00, "TOSHIBA A/Lx05",
			   ALC268_TOSHIBA),
	{}
};

static const struct alc_config_preset alc268_presets[] = {
	[ALC267_QUANTA_IL1] = {
		.mixers = { alc267_quanta_il1_mixer, alc268_beep_mixer,
			    alc268_capture_nosrc_mixer },
		.init_verbs = { alc268_base_init_verbs, alc268_eapd_verbs,
				alc267_quanta_il1_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt),
		.adc_nids = alc268_adc_nids_alt,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc267_quanta_il1_setup,
		.init_hook = alc_inithook,
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
		.mixers = { alc268_toshiba_mixer, alc268_capture_alt_mixer,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc268_toshiba_setup,
		.init_hook = alc_inithook,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc268_acer_setup,
		.init_hook = alc_inithook,
	},
	[ALC268_ACER_DMIC] = {
		.mixers = { alc268_acer_dmic_mixer, alc268_capture_alt_mixer,
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
		.input_mux = &alc268_acer_dmic_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc268_acer_setup,
		.init_hook = alc_inithook,
	},
	[ALC268_ACER_ASPIRE_ONE] = {
		.mixers = { alc268_acer_aspire_one_mixer,
			    alc268_beep_mixer,
			    alc268_capture_nosrc_mixer },
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc268_acer_lc_setup,
		.init_hook = alc_inithook,
	},
	[ALC268_DELL] = {
		.mixers = { alc268_dell_mixer, alc268_beep_mixer,
			    alc268_capture_nosrc_mixer },
		.init_verbs = { alc268_base_init_verbs, alc268_eapd_verbs,
				alc268_dell_verbs },
		.num_dacs = ARRAY_SIZE(alc268_dac_nids),
		.dac_nids = alc268_dac_nids,
		.num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt),
		.adc_nids = alc268_adc_nids_alt,
		.capsrc_nids = alc268_capsrc_nids,
		.hp_nid = 0x02,
		.num_channel_mode = ARRAY_SIZE(alc268_modes),
		.channel_mode = alc268_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc268_dell_setup,
		.init_hook = alc_inithook,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc268_toshiba_setup,
		.init_hook = alc_inithook,
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
	int i, has_beep, err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	board_config = snd_hda_check_board_config(codec, ALC268_MODEL_LAST,
						  alc268_models,
						  alc268_cfg_tbl);

	if (board_config < 0 || board_config >= ALC268_MODEL_LAST)
		board_config = snd_hda_check_board_codec_sid_config(codec,
			ALC268_MODEL_LAST, alc268_models, alc268_ssid_cfg_tbl);

	if (board_config < 0 || board_config >= ALC268_MODEL_LAST) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
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
		setup_preset(codec, &alc268_presets[board_config]);

	spec->stream_analog_playback = &alc268_pcm_analog_playback;
	spec->stream_analog_capture = &alc268_pcm_analog_capture;
	spec->stream_analog_alt_capture = &alc268_pcm_analog_alt_capture;

	spec->stream_digital_playback = &alc268_pcm_digital_playback;

	has_beep = 0;
	for (i = 0; i < spec->num_mixers; i++) {
		if (spec->mixers[i] == alc268_beep_mixer) {
			has_beep = 1;
			break;
		}
	}

	if (has_beep) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
		if (!query_amp_caps(codec, 0x1d, HDA_INPUT))
			/* override the amp caps for beep generator */
			snd_hda_override_amp_caps(codec, 0x1d, HDA_INPUT,
					  (0x0c << AC_AMPCAP_OFFSET_SHIFT) |
					  (0x0c << AC_AMPCAP_NUM_STEPS_SHIFT) |
					  (0x07 << AC_AMPCAP_STEP_SIZE_SHIFT) |
					  (0 << AC_AMPCAP_MUTE_SHIFT));
	}

	if (!spec->no_analog && !spec->adc_nids && spec->input_mux) {
		/* check whether NID 0x07 is valid */
		unsigned int wcap = get_wcaps(codec, 0x07);

		spec->capsrc_nids = alc268_capsrc_nids;
		/* get type */
		wcap = get_wcaps_type(wcap);
		if (spec->auto_mic ||
		    wcap != AC_WID_AUD_IN || spec->input_mux->num_items == 1) {
			spec->adc_nids = alc268_adc_nids_alt;
			spec->num_adc_nids = ARRAY_SIZE(alc268_adc_nids_alt);
			if (spec->auto_mic)
				fixup_automic_adc(codec);
			if (spec->auto_mic || spec->input_mux->num_items == 1)
				add_mixer(spec, alc268_capture_nosrc_mixer);
			else
				add_mixer(spec, alc268_capture_alt_mixer);
		} else {
			spec->adc_nids = alc268_adc_nids;
			spec->num_adc_nids = ARRAY_SIZE(alc268_adc_nids);
			add_mixer(spec, alc268_capture_mixer);
		}
	}

	spec->vmaster_nid = 0x02;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC268_AUTO)
		spec->init_hook = alc268_auto_init;
	spec->shutup = alc_eapd_shutup;

	alc_init_jacks(codec);

	return 0;
}

/*
 *  ALC269 channel source setting (2 channel)
 */
#define ALC269_DIGOUT_NID	ALC880_DIGOUT_NID

#define alc269_dac_nids		alc260_dac_nids

static const hda_nid_t alc269_adc_nids[1] = {
	/* ADC1 */
	0x08,
};

static const hda_nid_t alc269_capsrc_nids[1] = {
	0x23,
};

static const hda_nid_t alc269vb_adc_nids[1] = {
	/* ADC1 */
	0x09,
};

static const hda_nid_t alc269vb_capsrc_nids[1] = {
	0x22,
};

static const hda_nid_t alc269_adc_candidates[] = {
	0x08, 0x09, 0x07, 0x11,
};

#define alc269_modes		alc260_modes
#define alc269_capture_source	alc880_lg_lw_capture_source

static const struct snd_kcontrol_new alc269_base_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Mono Playback Switch", 0x16, 2, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc269_quanta_fl1_mixer[] = {
	/* output mixer control */
	HDA_BIND_VOL("Master Playback Volume", &alc268_acer_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.subdevice = HDA_SUBDEV_AMP_FLAG,
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = alc268_acer_master_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
	},
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	{ }
};

static const struct snd_kcontrol_new alc269_lifebook_mixer[] = {
	/* output mixer control */
	HDA_BIND_VOL("Master Playback Volume", &alc268_acer_bind_master_vol),
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.subdevice = HDA_SUBDEV_AMP_FLAG,
		.info = snd_hda_mixer_amp_switch_info,
		.get = snd_hda_mixer_amp_switch_get,
		.put = alc268_acer_master_sw_put,
		.private_value = HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
	},
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x01, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Dock Mic Playback Volume", 0x0b, 0x03, HDA_INPUT),
	HDA_CODEC_MUTE("Dock Mic Playback Switch", 0x0b, 0x03, HDA_INPUT),
	HDA_CODEC_VOLUME("Dock Mic Boost Volume", 0x1b, 0, HDA_INPUT),
	{ }
};

static const struct snd_kcontrol_new alc269_laptop_mixer[] = {
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc269vb_laptop_mixer[] = {
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x21, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc269_asus_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Master Playback Switch", 0x0c, 0x0, HDA_INPUT),
	{ } /* end */
};

/* capture mixer elements */
static const struct snd_kcontrol_new alc269_laptop_analog_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc269_laptop_digital_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc269vb_laptop_analog_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc269vb_laptop_digital_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	{ } /* end */
};

/* FSC amilo */
#define alc269_fujitsu_mixer	alc269_laptop_mixer

static const struct hda_verb alc269_quanta_fl1_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{ }
};

static const struct hda_verb alc269_lifebook_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x1a, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1a, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x1d, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc269_quanta_fl1_speaker_automute(struct hda_codec *codec)
{
	alc_hp_automute(codec);

	snd_hda_codec_write(codec, 0x20, 0,
			AC_VERB_SET_COEF_INDEX, 0x0c);
	snd_hda_codec_write(codec, 0x20, 0,
			AC_VERB_SET_PROC_COEF, 0x680);

	snd_hda_codec_write(codec, 0x20, 0,
			AC_VERB_SET_COEF_INDEX, 0x0c);
	snd_hda_codec_write(codec, 0x20, 0,
			AC_VERB_SET_PROC_COEF, 0x480);
}

#define alc269_lifebook_speaker_automute \
	alc269_quanta_fl1_speaker_automute

static void alc269_lifebook_mic_autoswitch(struct hda_codec *codec)
{
	unsigned int present_laptop;
	unsigned int present_dock;

	present_laptop	= snd_hda_jack_detect(codec, 0x18);
	present_dock	= snd_hda_jack_detect(codec, 0x1b);

	/* Laptop mic port overrides dock mic port, design decision */
	if (present_dock)
		snd_hda_codec_write(codec, 0x23, 0,
				AC_VERB_SET_CONNECT_SEL, 0x3);
	if (present_laptop)
		snd_hda_codec_write(codec, 0x23, 0,
				AC_VERB_SET_CONNECT_SEL, 0x0);
	if (!present_dock && !present_laptop)
		snd_hda_codec_write(codec, 0x23, 0,
				AC_VERB_SET_CONNECT_SEL, 0x1);
}

static void alc269_quanta_fl1_unsol_event(struct hda_codec *codec,
				    unsigned int res)
{
	switch (res >> 26) {
	case ALC880_HP_EVENT:
		alc269_quanta_fl1_speaker_automute(codec);
		break;
	case ALC880_MIC_EVENT:
		alc_mic_automute(codec);
		break;
	}
}

static void alc269_lifebook_unsol_event(struct hda_codec *codec,
					unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc269_lifebook_speaker_automute(codec);
	if ((res >> 26) == ALC880_MIC_EVENT)
		alc269_lifebook_mic_autoswitch(codec);
}

static void alc269_quanta_fl1_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute_mixer_nid[0] = 0x0c;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_MIXER;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x19;
	spec->int_mic.mux_idx = 1;
	spec->auto_mic = 1;
}

static void alc269_quanta_fl1_init_hook(struct hda_codec *codec)
{
	alc269_quanta_fl1_speaker_automute(codec);
	alc_mic_automute(codec);
}

static void alc269_lifebook_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.hp_pins[1] = 0x1a;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute_mixer_nid[0] = 0x0c;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_MIXER;
}

static void alc269_lifebook_init_hook(struct hda_codec *codec)
{
	alc269_lifebook_speaker_automute(codec);
	alc269_lifebook_mic_autoswitch(codec);
}

static const struct hda_verb alc269_laptop_dmic_init_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x23, AC_VERB_SET_CONNECT_SEL, 0x05},
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, 0xb026 },
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x7019 | (0x00 << 8))},
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static const struct hda_verb alc269_laptop_amic_init_verbs[] = {
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x23, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, 0xb026 },
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, (0x701b | (0x00 << 8))},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static const struct hda_verb alc269vb_laptop_dmic_init_verbs[] = {
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x22, AC_VERB_SET_CONNECT_SEL, 0x06},
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, 0xb026 },
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, (0x7019 | (0x00 << 8))},
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static const struct hda_verb alc269vb_laptop_amic_init_verbs[] = {
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x22, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, 0xb026 },
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, (0x7019 | (0x00 << 8))},
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static const struct hda_verb alc271_acer_dmic_verbs[] = {
	{0x20, AC_VERB_SET_COEF_INDEX, 0x0d},
	{0x20, AC_VERB_SET_PROC_COEF, 0x4000},
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x00},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x22, AC_VERB_SET_CONNECT_SEL, 6},
	{ }
};

static void alc269_laptop_amic_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute_mixer_nid[0] = 0x0c;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_MIXER;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x19;
	spec->int_mic.mux_idx = 1;
	spec->auto_mic = 1;
}

static void alc269_laptop_dmic_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute_mixer_nid[0] = 0x0c;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_MIXER;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x12;
	spec->int_mic.mux_idx = 5;
	spec->auto_mic = 1;
}

static void alc269vb_laptop_amic_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x21;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute_mixer_nid[0] = 0x0c;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_MIXER;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x19;
	spec->int_mic.mux_idx = 1;
	spec->auto_mic = 1;
}

static void alc269vb_laptop_dmic_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x21;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute_mixer_nid[0] = 0x0c;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_MIXER;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x12;
	spec->int_mic.mux_idx = 6;
	spec->auto_mic = 1;
}

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static const struct hda_verb alc269_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	{0x08, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/*
	 * Set up output mixers (0x02 - 0x03)
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

	/* FIXME: use Mux-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1d, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x23, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* set EAPD */
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

static const struct hda_verb alc269vb_init_verbs[] = {
	/*
	 * Unmute ADC0 and set the default input to mic-in
	 */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	/*
	 * Set up output mixers (0x02 - 0x03)
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
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0f, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},

	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	/* FIXME: use Mux-type input source selection */
	/* Mixer elements: 0x18, 19, 1a, 1b, 1d, 0b */
	/* Input mixer1: unmute Mic, F-Mic, Line, CD inputs */
	{0x22, AC_VERB_SET_CONNECT_SEL, 0x00},

	/* set EAPD */
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

#define alc269_auto_create_multi_out_ctls \
	alc268_auto_create_multi_out_ctls
#define alc269_auto_create_input_ctls \
	alc268_auto_create_input_ctls

#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc269_loopbacks	alc880_loopbacks
#endif

/* pcm configuration: identical with ALC880 */
#define alc269_pcm_analog_playback	alc880_pcm_analog_playback
#define alc269_pcm_analog_capture	alc880_pcm_analog_capture
#define alc269_pcm_digital_playback	alc880_pcm_digital_playback
#define alc269_pcm_digital_capture	alc880_pcm_digital_capture

static const struct hda_pcm_stream alc269_44k_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 8,
	.rates = SNDRV_PCM_RATE_44100, /* fixed rate */
	/* NID is set in alc_build_pcms */
	.ops = {
		.open = alc880_playback_pcm_open,
		.prepare = alc880_playback_pcm_prepare,
		.cleanup = alc880_playback_pcm_cleanup
	},
};

static const struct hda_pcm_stream alc269_44k_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_44100, /* fixed rate */
	/* NID is set in alc_build_pcms */
};

#ifdef CONFIG_SND_HDA_POWER_SAVE
static int alc269_mic2_for_mute_led(struct hda_codec *codec)
{
	switch (codec->subsystem_id) {
	case 0x103c1586:
		return 1;
	}
	return 0;
}

static int alc269_mic2_mute_check_ps(struct hda_codec *codec, hda_nid_t nid)
{
	/* update mute-LED according to the speaker mute state */
	if (nid == 0x01 || nid == 0x14) {
		int pinval;
		if (snd_hda_codec_amp_read(codec, 0x14, 0, HDA_OUTPUT, 0) &
		    HDA_AMP_MUTE)
			pinval = 0x24;
		else
			pinval = 0x20;
		/* mic2 vref pin is used for mute LED control */
		snd_hda_codec_update_cache(codec, 0x19, 0,
					   AC_VERB_SET_PIN_WIDGET_CONTROL,
					   pinval);
	}
	return alc_check_power_status(codec, nid);
}
#endif /* CONFIG_SND_HDA_POWER_SAVE */

static int alc275_setup_dual_adc(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	if (codec->vendor_id != 0x10ec0275 || !spec->auto_mic)
		return 0;
	if ((spec->ext_mic.pin >= 0x18 && spec->int_mic.pin <= 0x13) ||
	    (spec->ext_mic.pin <= 0x12 && spec->int_mic.pin >= 0x18)) {
		if (spec->ext_mic.pin <= 0x12) {
			spec->private_adc_nids[0] = 0x08;
			spec->private_adc_nids[1] = 0x11;
			spec->private_capsrc_nids[0] = 0x23;
			spec->private_capsrc_nids[1] = 0x22;
		} else {
			spec->private_adc_nids[0] = 0x11;
			spec->private_adc_nids[1] = 0x08;
			spec->private_capsrc_nids[0] = 0x22;
			spec->private_capsrc_nids[1] = 0x23;
		}
		spec->adc_nids = spec->private_adc_nids;
		spec->capsrc_nids = spec->private_capsrc_nids;
		spec->num_adc_nids = 2;
		spec->dual_adc_switch = 1;
		snd_printdd("realtek: enabling dual ADC switchg (%02x:%02x)\n",
			    spec->adc_nids[0], spec->adc_nids[1]);
		return 1;
	}
	return 0;
}

/* different alc269-variants */
enum {
	ALC269_TYPE_NORMAL,
	ALC269_TYPE_ALC258,
	ALC269_TYPE_ALC259,
	ALC269_TYPE_ALC269VB,
	ALC269_TYPE_ALC270,
	ALC269_TYPE_ALC271X,
};

/*
 * BIOS auto configuration
 */
static int alc269_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static const hda_nid_t alc269_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc269_ignore);
	if (err < 0)
		return err;

	err = alc269_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;
	if (spec->codec_variant == ALC269_TYPE_NORMAL)
		err = alc269_auto_create_input_ctls(codec, &spec->autocfg);
	else
		err = alc_auto_create_input_ctls(codec, &spec->autocfg, 0,
						 0x22, 0);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	alc_auto_parse_digital(codec);

	if (spec->kctls.list)
		add_mixer(spec, spec->kctls.list);

	if (spec->codec_variant != ALC269_TYPE_NORMAL) {
		add_verb(spec, alc269vb_init_verbs);
		alc_ssid_check(codec, 0, 0x1b, 0x14, 0x21);
	} else {
		add_verb(spec, alc269_init_verbs);
		alc_ssid_check(codec, 0x15, 0x1b, 0x14, 0);
	}

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux[0];

	if (!alc275_setup_dual_adc(codec))
		fillup_priv_adc_nids(codec, alc269_adc_candidates,
				     sizeof(alc269_adc_candidates));

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	if (!spec->cap_mixer && !spec->no_analog)
		set_capture_mixer(codec);

	return 1;
}

#define alc269_auto_init_multi_out	alc268_auto_init_multi_out
#define alc269_auto_init_hp_out		alc268_auto_init_hp_out
#define alc269_auto_init_analog_input	alc882_auto_init_analog_input
#define alc269_auto_init_input_src	alc882_auto_init_input_src


/* init callback for auto-configuration model -- overriding the default init */
static void alc269_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc269_auto_init_multi_out(codec);
	alc269_auto_init_hp_out(codec);
	alc269_auto_init_analog_input(codec);
	if (!spec->dual_adc_switch)
		alc269_auto_init_input_src(codec);
	alc_auto_init_digital(codec);
	if (spec->unsol_event)
		alc_inithook(codec);
}

static void alc269_toggle_power_output(struct hda_codec *codec, int power_up)
{
	int val = alc_read_coef_idx(codec, 0x04);
	if (power_up)
		val |= 1 << 11;
	else
		val &= ~(1 << 11);
	alc_write_coef_idx(codec, 0x04, val);
}

static void alc269_shutup(struct hda_codec *codec)
{
	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x017)
		alc269_toggle_power_output(codec, 0);
	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x018) {
		alc269_toggle_power_output(codec, 0);
		msleep(150);
	}
}

#ifdef SND_HDA_NEEDS_RESUME
static int alc269_resume(struct hda_codec *codec)
{
	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x018) {
		alc269_toggle_power_output(codec, 0);
		msleep(150);
	}

	codec->patch_ops.init(codec);

	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x017) {
		alc269_toggle_power_output(codec, 1);
		msleep(200);
	}

	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x018)
		alc269_toggle_power_output(codec, 1);

	snd_hda_codec_resume_amp(codec);
	snd_hda_codec_resume_cache(codec);
	hda_call_check_power_status(codec, 0x01);
	return 0;
}
#endif /* SND_HDA_NEEDS_RESUME */

static void alc269_fixup_hweq(struct hda_codec *codec,
			       const struct alc_fixup *fix, int action)
{
	int coef;

	if (action != ALC_FIXUP_ACT_INIT)
		return;
	coef = alc_read_coef_idx(codec, 0x1e);
	alc_write_coef_idx(codec, 0x1e, coef | 0x80);
}

static void alc271_fixup_dmic(struct hda_codec *codec,
			      const struct alc_fixup *fix, int action)
{
	static const struct hda_verb verbs[] = {
		{0x20, AC_VERB_SET_COEF_INDEX, 0x0d},
		{0x20, AC_VERB_SET_PROC_COEF, 0x4000},
		{}
	};
	unsigned int cfg;

	if (strcmp(codec->chip_name, "ALC271X"))
		return;
	cfg = snd_hda_codec_get_pincfg(codec, 0x12);
	if (get_defcfg_connect(cfg) == AC_JACK_PORT_FIXED)
		snd_hda_sequence_write(codec, verbs);
}

enum {
	ALC269_FIXUP_SONY_VAIO,
	ALC275_FIXUP_SONY_VAIO_GPIO2,
	ALC269_FIXUP_DELL_M101Z,
	ALC269_FIXUP_SKU_IGNORE,
	ALC269_FIXUP_ASUS_G73JW,
	ALC269_FIXUP_LENOVO_EAPD,
	ALC275_FIXUP_SONY_HWEQ,
	ALC271_FIXUP_DMIC,
};

static const struct alc_fixup alc269_fixups[] = {
	[ALC269_FIXUP_SONY_VAIO] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREFGRD},
			{}
		}
	},
	[ALC275_FIXUP_SONY_VAIO_GPIO2] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x01, AC_VERB_SET_GPIO_MASK, 0x04},
			{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x04},
			{0x01, AC_VERB_SET_GPIO_DATA, 0x00},
			{ }
		},
		.chained = true,
		.chain_id = ALC269_FIXUP_SONY_VAIO
	},
	[ALC269_FIXUP_DELL_M101Z] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* Enables internal speaker */
			{0x20, AC_VERB_SET_COEF_INDEX, 13},
			{0x20, AC_VERB_SET_PROC_COEF, 0x4040},
			{}
		}
	},
	[ALC269_FIXUP_SKU_IGNORE] = {
		.type = ALC_FIXUP_SKU,
		.v.sku = ALC_FIXUP_SKU_IGNORE,
	},
	[ALC269_FIXUP_ASUS_G73JW] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x17, 0x99130111 }, /* subwoofer */
			{ }
		}
	},
	[ALC269_FIXUP_LENOVO_EAPD] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x14, AC_VERB_SET_EAPD_BTLENABLE, 0},
			{}
		}
	},
	[ALC275_FIXUP_SONY_HWEQ] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc269_fixup_hweq,
		.chained = true,
		.chain_id = ALC275_FIXUP_SONY_VAIO_GPIO2
	},
	[ALC271_FIXUP_DMIC] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc271_fixup_dmic,
	},
};

static const struct snd_pci_quirk alc269_fixup_tbl[] = {
	SND_PCI_QUIRK(0x104d, 0x9073, "Sony VAIO", ALC275_FIXUP_SONY_VAIO_GPIO2),
	SND_PCI_QUIRK(0x104d, 0x907b, "Sony VAIO", ALC275_FIXUP_SONY_HWEQ),
	SND_PCI_QUIRK(0x104d, 0x9084, "Sony VAIO", ALC275_FIXUP_SONY_HWEQ),
	SND_PCI_QUIRK_VENDOR(0x104d, "Sony VAIO", ALC269_FIXUP_SONY_VAIO),
	SND_PCI_QUIRK(0x1028, 0x0470, "Dell M101z", ALC269_FIXUP_DELL_M101Z),
	SND_PCI_QUIRK_VENDOR(0x1025, "Acer Aspire", ALC271_FIXUP_DMIC),
	SND_PCI_QUIRK(0x17aa, 0x20f2, "Thinkpad SL410/510", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x215e, "Thinkpad L512", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21b8, "Thinkpad Edge 14", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21ca, "Thinkpad L412", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x17aa, 0x21e9, "Thinkpad Edge 15", ALC269_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x1043, 0x1a13, "Asus G73Jw", ALC269_FIXUP_ASUS_G73JW),
	SND_PCI_QUIRK(0x17aa, 0x9e54, "LENOVO NB", ALC269_FIXUP_LENOVO_EAPD),
	{}
};


/*
 * configuration and preset
 */
static const char * const alc269_models[ALC269_MODEL_LAST] = {
	[ALC269_BASIC]			= "basic",
	[ALC269_QUANTA_FL1]		= "quanta",
	[ALC269_AMIC]			= "laptop-amic",
	[ALC269_DMIC]			= "laptop-dmic",
	[ALC269_FUJITSU]		= "fujitsu",
	[ALC269_LIFEBOOK]		= "lifebook",
	[ALC269_AUTO]			= "auto",
};

static const struct snd_pci_quirk alc269_cfg_tbl[] = {
	SND_PCI_QUIRK(0x17aa, 0x3bf8, "Quanta FL1", ALC269_QUANTA_FL1),
	SND_PCI_QUIRK(0x1025, 0x047c, "ACER ZGA", ALC271_ACER),
	SND_PCI_QUIRK(0x1043, 0x8330, "ASUS Eeepc P703 P900A",
		      ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1013, "ASUS N61Da", ALC269VB_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1113, "ASUS N63Jn", ALC269VB_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1143, "ASUS B53f", ALC269VB_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1133, "ASUS UJ20ft", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1183, "ASUS K72DR", ALC269VB_AMIC),
	SND_PCI_QUIRK(0x1043, 0x11b3, "ASUS K52DR", ALC269VB_AMIC),
	SND_PCI_QUIRK(0x1043, 0x11e3, "ASUS U33Jc", ALC269VB_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1273, "ASUS UL80Jt", ALC269VB_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1283, "ASUS U53Jc", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x12b3, "ASUS N82JV", ALC269VB_AMIC),
	SND_PCI_QUIRK(0x1043, 0x12d3, "ASUS N61Jv", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x13a3, "ASUS UL30Vt", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1373, "ASUS G73JX", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1383, "ASUS UJ30Jc", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x13d3, "ASUS N61JA", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1413, "ASUS UL50", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1443, "ASUS UL30", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1453, "ASUS M60Jv", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1483, "ASUS UL80", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x14f3, "ASUS F83Vf", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x14e3, "ASUS UL20", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1513, "ASUS UX30", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1593, "ASUS N51Vn", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15a3, "ASUS N60Jv", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15b3, "ASUS N60Dp", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15c3, "ASUS N70De", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x15e3, "ASUS F83T", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1643, "ASUS M60J", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1653, "ASUS U50", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1693, "ASUS F50N", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x16a3, "ASUS F5Q", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x16e3, "ASUS UX50", ALC269_DMIC),
	SND_PCI_QUIRK(0x1043, 0x1723, "ASUS P80", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1743, "ASUS U80", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1773, "ASUS U20A", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x1883, "ASUS F81Se", ALC269_AMIC),
	SND_PCI_QUIRK(0x1043, 0x831a, "ASUS Eeepc P901",
		      ALC269_DMIC),
	SND_PCI_QUIRK(0x1043, 0x834a, "ASUS Eeepc S101",
		      ALC269_DMIC),
	SND_PCI_QUIRK(0x1043, 0x8398, "ASUS P1005HA", ALC269_DMIC),
	SND_PCI_QUIRK(0x1043, 0x83ce, "ASUS P1005HA", ALC269_DMIC),
	SND_PCI_QUIRK(0x104d, 0x9071, "Sony VAIO", ALC269_AUTO),
	SND_PCI_QUIRK(0x10cf, 0x1475, "Lifebook ICH9M-based", ALC269_LIFEBOOK),
	SND_PCI_QUIRK(0x152d, 0x1778, "Quanta ON1", ALC269_DMIC),
	SND_PCI_QUIRK(0x1734, 0x115d, "FSC Amilo", ALC269_FUJITSU),
	SND_PCI_QUIRK(0x17aa, 0x3be9, "Quanta Wistron", ALC269_AMIC),
	SND_PCI_QUIRK(0x17aa, 0x3bf8, "Quanta FL1", ALC269_AMIC),
	SND_PCI_QUIRK(0x17ff, 0x059a, "Quanta EL3", ALC269_DMIC),
	SND_PCI_QUIRK(0x17ff, 0x059b, "Quanta JR1", ALC269_DMIC),
	{}
};

static const struct alc_config_preset alc269_presets[] = {
	[ALC269_BASIC] = {
		.mixers = { alc269_base_mixer },
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
		.setup = alc269_quanta_fl1_setup,
		.init_hook = alc269_quanta_fl1_init_hook,
	},
	[ALC269_AMIC] = {
		.mixers = { alc269_laptop_mixer },
		.cap_mixer = alc269_laptop_analog_capture_mixer,
		.init_verbs = { alc269_init_verbs,
				alc269_laptop_amic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc269_dac_nids),
		.dac_nids = alc269_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc269_modes),
		.channel_mode = alc269_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc269_laptop_amic_setup,
		.init_hook = alc_inithook,
	},
	[ALC269_DMIC] = {
		.mixers = { alc269_laptop_mixer },
		.cap_mixer = alc269_laptop_digital_capture_mixer,
		.init_verbs = { alc269_init_verbs,
				alc269_laptop_dmic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc269_dac_nids),
		.dac_nids = alc269_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc269_modes),
		.channel_mode = alc269_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc269_laptop_dmic_setup,
		.init_hook = alc_inithook,
	},
	[ALC269VB_AMIC] = {
		.mixers = { alc269vb_laptop_mixer },
		.cap_mixer = alc269vb_laptop_analog_capture_mixer,
		.init_verbs = { alc269vb_init_verbs,
				alc269vb_laptop_amic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc269_dac_nids),
		.dac_nids = alc269_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc269_modes),
		.channel_mode = alc269_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc269vb_laptop_amic_setup,
		.init_hook = alc_inithook,
	},
	[ALC269VB_DMIC] = {
		.mixers = { alc269vb_laptop_mixer },
		.cap_mixer = alc269vb_laptop_digital_capture_mixer,
		.init_verbs = { alc269vb_init_verbs,
				alc269vb_laptop_dmic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc269_dac_nids),
		.dac_nids = alc269_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc269_modes),
		.channel_mode = alc269_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc269vb_laptop_dmic_setup,
		.init_hook = alc_inithook,
	},
	[ALC269_FUJITSU] = {
		.mixers = { alc269_fujitsu_mixer },
		.cap_mixer = alc269_laptop_digital_capture_mixer,
		.init_verbs = { alc269_init_verbs,
				alc269_laptop_dmic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc269_dac_nids),
		.dac_nids = alc269_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc269_modes),
		.channel_mode = alc269_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc269_laptop_dmic_setup,
		.init_hook = alc_inithook,
	},
	[ALC269_LIFEBOOK] = {
		.mixers = { alc269_lifebook_mixer },
		.init_verbs = { alc269_init_verbs, alc269_lifebook_verbs },
		.num_dacs = ARRAY_SIZE(alc269_dac_nids),
		.dac_nids = alc269_dac_nids,
		.hp_nid = 0x03,
		.num_channel_mode = ARRAY_SIZE(alc269_modes),
		.channel_mode = alc269_modes,
		.input_mux = &alc269_capture_source,
		.unsol_event = alc269_lifebook_unsol_event,
		.setup = alc269_lifebook_setup,
		.init_hook = alc269_lifebook_init_hook,
	},
	[ALC271_ACER] = {
		.mixers = { alc269_asus_mixer },
		.cap_mixer = alc269vb_laptop_digital_capture_mixer,
		.init_verbs = { alc269_init_verbs, alc271_acer_dmic_verbs },
		.num_dacs = ARRAY_SIZE(alc269_dac_nids),
		.dac_nids = alc269_dac_nids,
		.adc_nids = alc262_dmic_adc_nids,
		.num_adc_nids = ARRAY_SIZE(alc262_dmic_adc_nids),
		.capsrc_nids = alc262_dmic_capsrc_nids,
		.num_channel_mode = ARRAY_SIZE(alc269_modes),
		.channel_mode = alc269_modes,
		.input_mux = &alc269_capture_source,
		.dig_out_nid = ALC880_DIGOUT_NID,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc269vb_laptop_dmic_setup,
		.init_hook = alc_inithook,
	},
};

static int alc269_fill_coef(struct hda_codec *codec)
{
	int val;

	if ((alc_read_coef_idx(codec, 0) & 0x00ff) < 0x015) {
		alc_write_coef_idx(codec, 0xf, 0x960b);
		alc_write_coef_idx(codec, 0xe, 0x8817);
	}

	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x016) {
		alc_write_coef_idx(codec, 0xf, 0x960b);
		alc_write_coef_idx(codec, 0xe, 0x8814);
	}

	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x017) {
		val = alc_read_coef_idx(codec, 0x04);
		/* Power up output pin */
		alc_write_coef_idx(codec, 0x04, val | (1<<11));
	}

	if ((alc_read_coef_idx(codec, 0) & 0x00ff) == 0x018) {
		val = alc_read_coef_idx(codec, 0xd);
		if ((val & 0x0c00) >> 10 != 0x1) {
			/* Capless ramp up clock control */
			alc_write_coef_idx(codec, 0xd, val | (1<<10));
		}
		val = alc_read_coef_idx(codec, 0x17);
		if ((val & 0x01c0) >> 6 != 0x4) {
			/* Class D power on reset */
			alc_write_coef_idx(codec, 0x17, val | (1<<7));
		}
	}

	val = alc_read_coef_idx(codec, 0xd); /* Class D */
	alc_write_coef_idx(codec, 0xd, val | (1<<14));

	val = alc_read_coef_idx(codec, 0x4); /* HP */
	alc_write_coef_idx(codec, 0x4, val | (1<<11));

	return 0;
}

static int patch_alc269(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config, coef;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	alc_auto_parse_customize_define(codec);

	if (codec->vendor_id == 0x10ec0269) {
		coef = alc_read_coef_idx(codec, 0);
		if ((coef & 0x00f0) == 0x0010) {
			if (codec->bus->pci->subsystem_vendor == 0x1025 &&
			    spec->cdefine.platform_type == 1) {
				alc_codec_rename(codec, "ALC271X");
				spec->codec_variant = ALC269_TYPE_ALC271X;
			} else if ((coef & 0xf000) == 0x1000) {
				spec->codec_variant = ALC269_TYPE_ALC270;
			} else if ((coef & 0xf000) == 0x2000) {
				alc_codec_rename(codec, "ALC259");
				spec->codec_variant = ALC269_TYPE_ALC259;
			} else if ((coef & 0xf000) == 0x3000) {
				alc_codec_rename(codec, "ALC258");
				spec->codec_variant = ALC269_TYPE_ALC258;
			} else {
				alc_codec_rename(codec, "ALC269VB");
				spec->codec_variant = ALC269_TYPE_ALC269VB;
			}
		} else
			alc_fix_pll_init(codec, 0x20, 0x04, 15);
		alc269_fill_coef(codec);
	}

	board_config = snd_hda_check_board_config(codec, ALC269_MODEL_LAST,
						  alc269_models,
						  alc269_cfg_tbl);

	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC269_AUTO;
	}

	if (board_config == ALC269_AUTO) {
		alc_pick_fixup(codec, NULL, alc269_fixup_tbl, alc269_fixups);
		alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);
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

	if (has_cdefine_beep(codec)) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
	}

	if (board_config != ALC269_AUTO)
		setup_preset(codec, &alc269_presets[board_config]);

	if (board_config == ALC269_QUANTA_FL1) {
		/* Due to a hardware problem on Lenovo Ideadpad, we need to
		 * fix the sample rate of analog I/O to 44.1kHz
		 */
		spec->stream_analog_playback = &alc269_44k_pcm_analog_playback;
		spec->stream_analog_capture = &alc269_44k_pcm_analog_capture;
	} else if (spec->dual_adc_switch) {
		spec->stream_analog_playback = &alc269_pcm_analog_playback;
		/* switch ADC dynamically */
		spec->stream_analog_capture = &dualmic_pcm_analog_capture;
	} else {
		spec->stream_analog_playback = &alc269_pcm_analog_playback;
		spec->stream_analog_capture = &alc269_pcm_analog_capture;
	}
	spec->stream_digital_playback = &alc269_pcm_digital_playback;
	spec->stream_digital_capture = &alc269_pcm_digital_capture;

	if (!spec->adc_nids) { /* wasn't filled automatically? use default */
		if (spec->codec_variant == ALC269_TYPE_NORMAL) {
			spec->adc_nids = alc269_adc_nids;
			spec->num_adc_nids = ARRAY_SIZE(alc269_adc_nids);
			spec->capsrc_nids = alc269_capsrc_nids;
		} else {
			spec->adc_nids = alc269vb_adc_nids;
			spec->num_adc_nids = ARRAY_SIZE(alc269vb_adc_nids);
			spec->capsrc_nids = alc269vb_capsrc_nids;
		}
	}

	if (!spec->cap_mixer)
		set_capture_mixer(codec);
	if (has_cdefine_beep(codec))
		set_beep_amp(spec, 0x0b, 0x04, HDA_INPUT);

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	spec->vmaster_nid = 0x02;

	codec->patch_ops = alc_patch_ops;
#ifdef SND_HDA_NEEDS_RESUME
	codec->patch_ops.resume = alc269_resume;
#endif
	if (board_config == ALC269_AUTO)
		spec->init_hook = alc269_auto_init;
	spec->shutup = alc269_shutup;

	alc_init_jacks(codec);
#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc269_loopbacks;
	if (alc269_mic2_for_mute_led(codec))
		codec->patch_ops.check_power_status = alc269_mic2_mute_check_ps;
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
static const struct hda_verb alc861_threestack_ch2_init[] = {
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
static const struct hda_verb alc861_threestack_ch6_init[] = {
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

static const struct hda_channel_mode alc861_threestack_modes[2] = {
	{ 2, alc861_threestack_ch2_init },
	{ 6, alc861_threestack_ch6_init },
};
/* Set mic1 as input and unmute the mixer */
static const struct hda_verb alc861_uniwill_m31_ch2_init[] = {
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x24 },
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7080 | (0x01 << 8)) }, /*mic*/
	{ } /* end */
};
/* Set mic1 as output and mute mixer */
static const struct hda_verb alc861_uniwill_m31_ch4_init[] = {
	{ 0x0d, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40 },
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, (0x7000 | (0x01 << 8)) }, /*mic*/
	{ } /* end */
};

static const struct hda_channel_mode alc861_uniwill_m31_modes[2] = {
	{ 2, alc861_uniwill_m31_ch2_init },
	{ 4, alc861_uniwill_m31_ch4_init },
};

/* Set mic1 and line-in as input and unmute the mixer */
static const struct hda_verb alc861_asus_ch2_init[] = {
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
static const struct hda_verb alc861_asus_ch6_init[] = {
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

static const struct hda_channel_mode alc861_asus_modes[2] = {
	{ 2, alc861_asus_ch2_init },
	{ 6, alc861_asus_ch6_init },
};

/* patch-ALC861 */

static const struct snd_kcontrol_new alc861_base_mixer[] = {
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

	{ } /* end */
};

static const struct snd_kcontrol_new alc861_3ST_mixer[] = {
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

static const struct snd_kcontrol_new alc861_toshiba_mixer[] = {
        /* output mixer control */
	HDA_CODEC_MUTE("Master Playback Switch", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x15, 0x01, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x15, 0x01, HDA_INPUT),

	{ } /* end */
};

static const struct snd_kcontrol_new alc861_uniwill_m31_mixer[] = {
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

static const struct snd_kcontrol_new alc861_asus_mixer[] = {
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
static const struct snd_kcontrol_new alc861_asus_laptop_mixer[] = {
	HDA_CODEC_VOLUME("CD Playback Volume", 0x15, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x15, 0x0, HDA_INPUT),
	{ }
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static const struct hda_verb alc861_base_init_verbs[] = {
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

static const struct hda_verb alc861_threestack_init_verbs[] = {
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

static const struct hda_verb alc861_uniwill_m31_init_verbs[] = {
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

static const struct hda_verb alc861_asus_init_verbs[] = {
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
static const struct hda_verb alc861_asus_laptop_init_verbs[] = {
	{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x45 }, /* HP-out */
	{ 0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2) }, /* mute line-in */
	{ }
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static const struct hda_verb alc861_auto_init_verbs[] = {
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

	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},

	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(1)},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(2)},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(3)},

	{0x08, AC_VERB_SET_CONNECT_SEL, 0x00},	/* set Mic 1 */

	{ }
};

static const struct hda_verb alc861_toshiba_init_verbs[] = {
	{0x0f, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},

	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc861_toshiba_automute(struct hda_codec *codec)
{
	unsigned int present = snd_hda_jack_detect(codec, 0x0f);

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

/* pcm configuration: identical with ALC880 */
#define alc861_pcm_analog_playback	alc880_pcm_analog_playback
#define alc861_pcm_analog_capture	alc880_pcm_analog_capture
#define alc861_pcm_digital_playback	alc880_pcm_digital_playback
#define alc861_pcm_digital_capture	alc880_pcm_digital_capture


#define ALC861_DIGOUT_NID	0x07

static const struct hda_channel_mode alc861_8ch_modes[1] = {
	{ 8, NULL }
};

static const hda_nid_t alc861_dac_nids[4] = {
	/* front, surround, clfe, side */
	0x03, 0x06, 0x05, 0x04
};

static const hda_nid_t alc660_dac_nids[3] = {
	/* front, clfe, surround */
	0x03, 0x05, 0x06
};

static const hda_nid_t alc861_adc_nids[1] = {
	/* ADC0-2 */
	0x08,
};

static const struct hda_input_mux alc861_capture_source = {
	.num_items = 5,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x3 },
		{ "Line", 0x1 },
		{ "CD", 0x4 },
		{ "Mixer", 0x5 },
	},
};

static hda_nid_t alc861_look_for_dac(struct hda_codec *codec, hda_nid_t pin)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t mix, srcs[5];
	int i, j, num;

	if (snd_hda_get_connections(codec, pin, &mix, 1) != 1)
		return 0;
	num = snd_hda_get_connections(codec, mix, srcs, ARRAY_SIZE(srcs));
	if (num < 0)
		return 0;
	for (i = 0; i < num; i++) {
		unsigned int type;
		type = get_wcaps_type(get_wcaps(codec, srcs[i]));
		if (type != AC_WID_AUD_OUT)
			continue;
		for (j = 0; j < spec->multiout.num_dacs; j++)
			if (spec->multiout.dac_nids[j] == srcs[i])
				break;
		if (j >= spec->multiout.num_dacs)
			return srcs[i];
	}
	return 0;
}

/* fill in the dac_nids table from the parsed pin configuration */
static int alc861_auto_fill_dac_nids(struct hda_codec *codec,
				     const struct auto_pin_cfg *cfg)
{
	struct alc_spec *spec = codec->spec;
	int i;
	hda_nid_t nid, dac;

	spec->multiout.dac_nids = spec->private_dac_nids;
	for (i = 0; i < cfg->line_outs; i++) {
		nid = cfg->line_out_pins[i];
		dac = alc861_look_for_dac(codec, nid);
		if (!dac)
			continue;
		spec->private_dac_nids[spec->multiout.num_dacs++] = dac;
	}
	return 0;
}

static int __alc861_create_out_sw(struct hda_codec *codec, const char *pfx,
				  hda_nid_t nid, int idx, unsigned int chs)
{
	return __add_pb_sw_ctrl(codec->spec, ALC_CTL_WIDGET_MUTE, pfx, idx,
			   HDA_COMPOSE_AMP_VAL(nid, chs, 0, HDA_OUTPUT));
}

#define alc861_create_out_sw(codec, pfx, nid, chs) \
	__alc861_create_out_sw(codec, pfx, nid, 0, chs)

/* add playback controls from the parsed DAC table */
static int alc861_auto_create_multi_out_ctls(struct hda_codec *codec,
					     const struct auto_pin_cfg *cfg)
{
	struct alc_spec *spec = codec->spec;
	static const char * const chname[4] = {
		"Front", "Surround", NULL /*CLFE*/, "Side"
	};
	const char *pfx = alc_get_line_out_pfx(spec, true);
	hda_nid_t nid;
	int i, err, noutputs;

	noutputs = cfg->line_outs;
	if (spec->multi_ios > 0)
		noutputs += spec->multi_ios;

	for (i = 0; i < noutputs; i++) {
		nid = spec->multiout.dac_nids[i];
		if (!nid)
			continue;
		if (!pfx && i == 2) {
			/* Center/LFE */
			err = alc861_create_out_sw(codec, "Center", nid, 1);
			if (err < 0)
				return err;
			err = alc861_create_out_sw(codec, "LFE", nid, 2);
			if (err < 0)
				return err;
		} else {
			const char *name = pfx;
			int index = i;
			if (!name) {
				name = chname[i];
				index = 0;
			}
			err = __alc861_create_out_sw(codec, name, nid, index, 3);
			if (err < 0)
				return err;
		}
	}
	return 0;
}

static int alc861_auto_create_hp_ctls(struct hda_codec *codec, hda_nid_t pin)
{
	struct alc_spec *spec = codec->spec;
	int err;
	hda_nid_t nid;

	if (!pin)
		return 0;

	if ((pin >= 0x0b && pin <= 0x10) || pin == 0x1f || pin == 0x20) {
		nid = alc861_look_for_dac(codec, pin);
		if (nid) {
			err = alc861_create_out_sw(codec, "Headphone", nid, 3);
			if (err < 0)
				return err;
			spec->multiout.hp_nid = nid;
		}
	}
	return 0;
}

/* create playback/capture controls for input pins */
static int alc861_auto_create_input_ctls(struct hda_codec *codec,
						const struct auto_pin_cfg *cfg)
{
	return alc_auto_create_input_ctls(codec, cfg, 0x15, 0x08, 0);
}

static void alc861_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid,
					      int pin_type, hda_nid_t dac)
{
	hda_nid_t mix, srcs[5];
	int i, num;

	snd_hda_codec_write(codec, nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL,
			    pin_type);
	snd_hda_codec_write(codec, dac, 0, AC_VERB_SET_AMP_GAIN_MUTE,
			    AMP_OUT_UNMUTE);
	if (snd_hda_get_connections(codec, nid, &mix, 1) != 1)
		return;
	num = snd_hda_get_connections(codec, mix, srcs, ARRAY_SIZE(srcs));
	if (num < 0)
		return;
	for (i = 0; i < num; i++) {
		unsigned int mute;
		if (srcs[i] == dac || srcs[i] == 0x15)
			mute = AMP_IN_UNMUTE(i);
		else
			mute = AMP_IN_MUTE(i);
		snd_hda_codec_write(codec, mix, 0, AC_VERB_SET_AMP_GAIN_MUTE,
				    mute);
	}
}

static void alc861_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

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

	if (spec->autocfg.hp_outs)
		alc861_auto_set_output_and_unmute(codec,
						  spec->autocfg.hp_pins[0],
						  PIN_HP,
						  spec->multiout.hp_nid);
	if (spec->autocfg.speaker_outs)
		alc861_auto_set_output_and_unmute(codec,
						  spec->autocfg.speaker_pins[0],
						  PIN_OUT,
						  spec->multiout.dac_nids[0]);
}

static void alc861_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		if (nid >= 0x0c && nid <= 0x11)
			alc_set_input_pin(codec, nid, cfg->inputs[i].type);
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
	static const hda_nid_t alc861_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc861_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */

	err = alc861_auto_fill_dac_nids(codec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc_auto_add_multi_channel_mode(codec);
	if (err < 0)
		return err;
	err = alc861_auto_create_multi_out_ctls(codec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc861_auto_create_hp_ctls(codec, spec->autocfg.hp_pins[0]);
	if (err < 0)
		return err;
	err = alc861_auto_create_input_ctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	alc_auto_parse_digital(codec);

	if (spec->kctls.list)
		add_mixer(spec, spec->kctls.list);

	add_verb(spec, alc861_auto_init_verbs);

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux[0];

	spec->adc_nids = alc861_adc_nids;
	spec->num_adc_nids = ARRAY_SIZE(alc861_adc_nids);
	set_capture_mixer(codec);

	alc_ssid_check(codec, 0x0e, 0x0f, 0x0b, 0);

	return 1;
}

/* additional initialization for auto-configuration model */
static void alc861_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc861_auto_init_multi_out(codec);
	alc861_auto_init_hp_out(codec);
	alc861_auto_init_analog_input(codec);
	alc_auto_init_digital(codec);
	if (spec->unsol_event)
		alc_inithook(codec);
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
static const struct hda_amp_list alc861_loopbacks[] = {
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
static const char * const alc861_models[ALC861_MODEL_LAST] = {
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

static const struct snd_pci_quirk alc861_cfg_tbl[] = {
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

static const struct alc_config_preset alc861_presets[] = {
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

/* Pin config fixes */
enum {
	PINFIX_FSC_AMILO_PI1505,
	PINFIX_ASUS_A6RP,
};

static const struct alc_fixup alc861_fixups[] = {
	[PINFIX_FSC_AMILO_PI1505] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x0b, 0x0221101f }, /* HP */
			{ 0x0f, 0x90170310 }, /* speaker */
			{ }
		}
	},
	[PINFIX_ASUS_A6RP] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			/* node 0x0f VREF seems controlling the master output */
			{ 0x0f, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF50 },
			{ }
		},
	},
};

static const struct snd_pci_quirk alc861_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x1393, "ASUS A6Rp", PINFIX_ASUS_A6RP),
	SND_PCI_QUIRK(0x1584, 0x2b01, "Haier W18", PINFIX_ASUS_A6RP),
	SND_PCI_QUIRK(0x1734, 0x10c7, "FSC Amilo Pi1505", PINFIX_FSC_AMILO_PI1505),
	{}
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
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC861_AUTO;
	}

	if (board_config == ALC861_AUTO) {
		alc_pick_fixup(codec, NULL, alc861_fixup_tbl, alc861_fixups);
		alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);
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

	err = snd_hda_attach_beep_device(codec, 0x23);
	if (err < 0) {
		alc_free(codec);
		return err;
	}

	if (board_config != ALC861_AUTO)
		setup_preset(codec, &alc861_presets[board_config]);

	spec->stream_analog_playback = &alc861_pcm_analog_playback;
	spec->stream_analog_capture = &alc861_pcm_analog_capture;

	spec->stream_digital_playback = &alc861_pcm_digital_playback;
	spec->stream_digital_capture = &alc861_pcm_digital_capture;

	if (!spec->cap_mixer)
		set_capture_mixer(codec);
	set_beep_amp(spec, 0x23, 0, HDA_OUTPUT);

	spec->vmaster_nid = 0x03;

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC861_AUTO) {
		spec->init_hook = alc861_auto_init;
#ifdef CONFIG_SND_HDA_POWER_SAVE
		spec->power_hook = alc_power_eapd;
#endif
	}
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

static const hda_nid_t alc861vd_dac_nids[4] = {
	/* front, surr, clfe, side surr */
	0x02, 0x03, 0x04, 0x05
};

/* dac_nids for ALC660vd are in a different order - according to
 * Realtek's driver.
 * This should probably result in a different mixer for 6stack models
 * of ALC660vd codecs, but for now there is only 3stack mixer
 * - and it is the same as in 861vd.
 * adc_nids in ALC660vd are (is) the same as in 861vd
 */
static const hda_nid_t alc660vd_dac_nids[3] = {
	/* front, rear, clfe, rear_surr */
	0x02, 0x04, 0x03
};

static const hda_nid_t alc861vd_adc_nids[1] = {
	/* ADC0 */
	0x09,
};

static const hda_nid_t alc861vd_capsrc_nids[1] = { 0x22 };

/* input MUX */
/* FIXME: should be a matrix-type input source selection */
static const struct hda_input_mux alc861vd_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static const struct hda_input_mux alc861vd_dallas_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x0 },
		{ "Internal Mic", 0x1 },
	},
};

static const struct hda_input_mux alc861vd_hp_capture_source = {
	.num_items = 2,
	.items = {
		{ "Front Mic", 0x0 },
		{ "ATAPI Mic", 0x1 },
	},
};

/*
 * 2ch mode
 */
static const struct hda_channel_mode alc861vd_3stack_2ch_modes[1] = {
	{ 2, NULL }
};

/*
 * 6ch mode
 */
static const struct hda_verb alc861vd_6stack_ch6_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

/*
 * 8ch mode
 */
static const struct hda_verb alc861vd_6stack_ch8_init[] = {
	{ 0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

static const struct hda_channel_mode alc861vd_6stack_modes[2] = {
	{ 6, alc861vd_6stack_ch6_init },
	{ 8, alc861vd_6stack_ch8_init },
};

static const struct snd_kcontrol_new alc861vd_chmode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

/* Pin assignment: Front=0x14, Rear=0x15, CLFE=0x16, Side=0x17
 *                 Mic=0x18, Front Mic=0x19, Line-In=0x1a, HP=0x1b
 */
static const struct snd_kcontrol_new alc861vd_6st_mixer[] = {
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

	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),

	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),

	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),

	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),

	{ } /* end */
};

static const struct snd_kcontrol_new alc861vd_3st_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),

	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),

	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),

	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),

	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),

	{ } /* end */
};

static const struct snd_kcontrol_new alc861vd_lenovo_mixer[] = {
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	/*HDA_BIND_MUTE("Front Playback Switch", 0x0c, 2, HDA_INPUT),*/
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),

	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),

	HDA_CODEC_VOLUME("Front Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Front Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Front Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),

	HDA_CODEC_VOLUME("CD Playback Volume", 0x0b, 0x04, HDA_INPUT),
	HDA_CODEC_MUTE("CD Playback Switch", 0x0b, 0x04, HDA_INPUT),

	{ } /* end */
};

/* Pin assignment: Speaker=0x14, HP = 0x15,
 *                 Mic=0x18, Internal Mic = 0x19, CD = 0x1c, PC Beep = 0x1d
 */
static const struct snd_kcontrol_new alc861vd_dallas_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Speaker Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("Headphone Playback Switch", 0x0d, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

/* Pin assignment: Speaker=0x14, Line-out = 0x15,
 *                 Front Mic=0x18, ATAPI Mic = 0x19,
 */
static const struct snd_kcontrol_new alc861vd_hp_mixer[] = {
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
static const struct hda_verb alc861vd_volume_init_verbs[] = {
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
static const struct hda_verb alc861vd_3stack_init_verbs[] = {
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
static const struct hda_verb alc861vd_6stack_init_verbs[] = {
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

static const struct hda_verb alc861vd_eapd_verbs[] = {
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

static const struct hda_verb alc660vd_eapd_verbs[] = {
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

static const struct hda_verb alc861vd_lenovo_unsol_verbs[] = {
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},
	{0x0c, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x0b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(5)},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{}
};

static void alc861vd_lenovo_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc861vd_lenovo_init_hook(struct hda_codec *codec)
{
	alc_hp_automute(codec);
	alc88x_simple_mic_automute(codec);
}

static void alc861vd_lenovo_unsol_event(struct hda_codec *codec,
					unsigned int res)
{
	switch (res >> 26) {
	case ALC880_MIC_EVENT:
		alc88x_simple_mic_automute(codec);
		break;
	default:
		alc_sku_unsol_event(codec, res);
		break;
	}
}

static const struct hda_verb alc861vd_dallas_verbs[] = {
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
static void alc861vd_dallas_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc861vd_loopbacks	alc880_loopbacks
#endif

/* pcm configuration: identical with ALC880 */
#define alc861vd_pcm_analog_playback	alc880_pcm_analog_playback
#define alc861vd_pcm_analog_capture	alc880_pcm_analog_capture
#define alc861vd_pcm_digital_playback	alc880_pcm_digital_playback
#define alc861vd_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * configuration and preset
 */
static const char * const alc861vd_models[ALC861VD_MODEL_LAST] = {
	[ALC660VD_3ST]		= "3stack-660",
	[ALC660VD_3ST_DIG]	= "3stack-660-digout",
	[ALC660VD_ASUS_V1S]	= "asus-v1s",
	[ALC861VD_3ST]		= "3stack",
	[ALC861VD_3ST_DIG]	= "3stack-digout",
	[ALC861VD_6ST_DIG]	= "6stack-digout",
	[ALC861VD_LENOVO]	= "lenovo",
	[ALC861VD_DALLAS]	= "dallas",
	[ALC861VD_HP]		= "hp",
	[ALC861VD_AUTO]		= "auto",
};

static const struct snd_pci_quirk alc861vd_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0xa88d, "Realtek ALC660 demo", ALC660VD_3ST),
	SND_PCI_QUIRK(0x103c, 0x30bf, "HP TX1000", ALC861VD_HP),
	SND_PCI_QUIRK(0x1043, 0x12e2, "Asus z35m", ALC660VD_3ST),
	/*SND_PCI_QUIRK(0x1043, 0x1339, "Asus G1", ALC660VD_3ST),*/ /* auto */
	SND_PCI_QUIRK(0x1043, 0x1633, "Asus V1Sn", ALC660VD_ASUS_V1S),
	SND_PCI_QUIRK(0x1043, 0x81e7, "ASUS", ALC660VD_3ST_DIG),
	SND_PCI_QUIRK(0x10de, 0x03f0, "Realtek ALC660 demo", ALC660VD_3ST),
	SND_PCI_QUIRK(0x1179, 0xff00, "Toshiba A135", ALC861VD_LENOVO),
	/*SND_PCI_QUIRK(0x1179, 0xff00, "DALLAS", ALC861VD_DALLAS),*/ /*lenovo*/
	SND_PCI_QUIRK(0x1179, 0xff01, "Toshiba A135", ALC861VD_LENOVO),
	SND_PCI_QUIRK(0x1179, 0xff03, "Toshiba P205", ALC861VD_LENOVO),
	SND_PCI_QUIRK(0x1179, 0xff31, "Toshiba L30-149", ALC861VD_DALLAS),
	SND_PCI_QUIRK(0x1565, 0x820d, "Biostar NF61S SE", ALC861VD_6ST_DIG),
	SND_PCI_QUIRK_VENDOR(0x17aa, "Lenovo", ALC861VD_LENOVO),
	SND_PCI_QUIRK(0x1849, 0x0862, "ASRock K8NF6G-VSTA", ALC861VD_6ST_DIG),
	{}
};

static const struct alc_config_preset alc861vd_presets[] = {
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
		.setup = alc861vd_lenovo_setup,
		.init_hook = alc861vd_lenovo_init_hook,
	},
	[ALC861VD_DALLAS] = {
		.mixers = { alc861vd_dallas_mixer },
		.init_verbs = { alc861vd_dallas_verbs },
		.num_dacs = ARRAY_SIZE(alc861vd_dac_nids),
		.dac_nids = alc861vd_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_dallas_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc861vd_dallas_setup,
		.init_hook = alc_hp_automute,
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
		.unsol_event = alc_sku_unsol_event,
		.setup = alc861vd_dallas_setup,
		.init_hook = alc_hp_automute,
	},
	[ALC660VD_ASUS_V1S] = {
		.mixers = { alc861vd_lenovo_mixer },
		.init_verbs = { alc861vd_volume_init_verbs,
				alc861vd_3stack_init_verbs,
				alc861vd_eapd_verbs,
				alc861vd_lenovo_unsol_verbs },
		.num_dacs = ARRAY_SIZE(alc660vd_dac_nids),
		.dac_nids = alc660vd_dac_nids,
		.dig_out_nid = ALC861VD_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc861vd_3stack_2ch_modes),
		.channel_mode = alc861vd_3stack_2ch_modes,
		.input_mux = &alc861vd_capture_source,
		.unsol_event = alc861vd_lenovo_unsol_event,
		.setup = alc861vd_lenovo_setup,
		.init_hook = alc861vd_lenovo_init_hook,
	},
};

/*
 * BIOS auto configuration
 */
static int alc861vd_auto_create_input_ctls(struct hda_codec *codec,
						const struct auto_pin_cfg *cfg)
{
	return alc_auto_create_input_ctls(codec, cfg, 0x0b, 0x22, 0);
}


static void alc861vd_auto_set_output_and_unmute(struct hda_codec *codec,
				hda_nid_t nid, int pin_type, int dac_idx)
{
	alc_set_pin_output(codec, nid, pin_type);
}

static void alc861vd_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int i;

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
	if (pin) /* connect to front and use dac 0 */
		alc861vd_auto_set_output_and_unmute(codec, pin, PIN_HP, 0);
	pin = spec->autocfg.speaker_pins[0];
	if (pin)
		alc861vd_auto_set_output_and_unmute(codec, pin, PIN_OUT, 0);
}

#define ALC861VD_PIN_CD_NID		ALC880_PIN_CD_NID

static void alc861vd_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		if (alc_is_input_pin(codec, nid)) {
			alc_set_input_pin(codec, nid, cfg->inputs[i].type);
			if (nid != ALC861VD_PIN_CD_NID &&
			    (get_wcaps(codec, nid) & AC_WCAP_OUT_AMP))
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
	static const char * const chname[4] = {
		"Front", "Surround", "CLFE", "Side"
	};
	const char *pfx = alc_get_line_out_pfx(spec, true);
	hda_nid_t nid_v, nid_s;
	int i, err, noutputs;

	noutputs = cfg->line_outs;
	if (spec->multi_ios > 0)
		noutputs += spec->multi_ios;

	for (i = 0; i < noutputs; i++) {
		if (!spec->multiout.dac_nids[i])
			continue;
		nid_v = alc861vd_idx_to_mixer_vol(
				alc880_dac_to_idx(
					spec->multiout.dac_nids[i]));
		nid_s = alc861vd_idx_to_mixer_switch(
				alc880_dac_to_idx(
					spec->multiout.dac_nids[i]));

		if (!pfx && i == 2) {
			/* Center/LFE */
			err = add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL,
					      "Center",
					  HDA_COMPOSE_AMP_VAL(nid_v, 1, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL,
					      "LFE",
					  HDA_COMPOSE_AMP_VAL(nid_v, 2, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = add_pb_sw_ctrl(spec, ALC_CTL_BIND_MUTE,
					     "Center",
					  HDA_COMPOSE_AMP_VAL(nid_s, 1, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
			err = add_pb_sw_ctrl(spec, ALC_CTL_BIND_MUTE,
					     "LFE",
					  HDA_COMPOSE_AMP_VAL(nid_s, 2, 2,
							      HDA_INPUT));
			if (err < 0)
				return err;
		} else {
			const char *name = pfx;
			int index = i;
			if (!name) {
				name = chname[i];
				index = 0;
			}
			err = __add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL,
						name, index,
					  HDA_COMPOSE_AMP_VAL(nid_v, 3, 0,
							      HDA_OUTPUT));
			if (err < 0)
				return err;
			err = __add_pb_sw_ctrl(spec, ALC_CTL_BIND_MUTE,
					       name, index,
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

		err = add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL, pfx,
				  HDA_COMPOSE_AMP_VAL(nid_v, 3, 0, HDA_OUTPUT));
		if (err < 0)
			return err;
		err = add_pb_sw_ctrl(spec, ALC_CTL_BIND_MUTE, pfx,
				  HDA_COMPOSE_AMP_VAL(nid_s, 3, 2, HDA_INPUT));
		if (err < 0)
			return err;
	} else if (alc880_is_multi_pin(pin)) {
		/* set manual connection */
		/* we have only a switch on HP-out PIN */
		err = add_pb_sw_ctrl(spec, ALC_CTL_WIDGET_MUTE, pfx,
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
	static const hda_nid_t alc861vd_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc861vd_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */

	err = alc880_auto_fill_dac_nids(spec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc_auto_add_multi_channel_mode(codec);
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
	err = alc861vd_auto_create_input_ctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	alc_auto_parse_digital(codec);

	if (spec->kctls.list)
		add_mixer(spec, spec->kctls.list);

	add_verb(spec, alc861vd_volume_init_verbs);

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux[0];

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	alc_ssid_check(codec, 0x15, 0x1b, 0x14, 0);

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
	alc_auto_init_digital(codec);
	if (spec->unsol_event)
		alc_inithook(codec);
}

enum {
	ALC660VD_FIX_ASUS_GPIO1
};

/* reset GPIO1 */
static const struct alc_fixup alc861vd_fixups[] = {
	[ALC660VD_FIX_ASUS_GPIO1] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x01, AC_VERB_SET_GPIO_MASK, 0x03},
			{0x01, AC_VERB_SET_GPIO_DIRECTION, 0x01},
			{0x01, AC_VERB_SET_GPIO_DATA, 0x01},
			{ }
		}
	},
};

static const struct snd_pci_quirk alc861vd_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x1339, "ASUS A7-K", ALC660VD_FIX_ASUS_GPIO1),
	{}
};

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
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC861VD_AUTO;
	}

	if (board_config == ALC861VD_AUTO) {
		alc_pick_fixup(codec, NULL, alc861vd_fixup_tbl, alc861vd_fixups);
		alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);
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

	err = snd_hda_attach_beep_device(codec, 0x23);
	if (err < 0) {
		alc_free(codec);
		return err;
	}

	if (board_config != ALC861VD_AUTO)
		setup_preset(codec, &alc861vd_presets[board_config]);

	if (codec->vendor_id == 0x10ec0660) {
		/* always turn on EAPD */
		add_verb(spec, alc660vd_eapd_verbs);
	}

	spec->stream_analog_playback = &alc861vd_pcm_analog_playback;
	spec->stream_analog_capture = &alc861vd_pcm_analog_capture;

	spec->stream_digital_playback = &alc861vd_pcm_digital_playback;
	spec->stream_digital_capture = &alc861vd_pcm_digital_capture;

	if (!spec->adc_nids) {
		spec->adc_nids = alc861vd_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(alc861vd_adc_nids);
	}
	if (!spec->capsrc_nids)
		spec->capsrc_nids = alc861vd_capsrc_nids;

	set_capture_mixer(codec);
	set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);

	spec->vmaster_nid = 0x02;

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	codec->patch_ops = alc_patch_ops;

	if (board_config == ALC861VD_AUTO)
		spec->init_hook = alc861vd_auto_init;
	spec->shutup = alc_eapd_shutup;
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

static const hda_nid_t alc662_dac_nids[3] = {
	/* front, rear, clfe */
	0x02, 0x03, 0x04
};

static const hda_nid_t alc272_dac_nids[2] = {
	0x02, 0x03
};

static const hda_nid_t alc662_adc_nids[2] = {
	/* ADC1-2 */
	0x09, 0x08
};

static const hda_nid_t alc272_adc_nids[1] = {
	/* ADC1-2 */
	0x08,
};

static const hda_nid_t alc662_capsrc_nids[2] = { 0x22, 0x23 };
static const hda_nid_t alc272_capsrc_nids[1] = { 0x23 };


/* input MUX */
/* FIXME: should be a matrix-type input source selection */
static const struct hda_input_mux alc662_capture_source = {
	.num_items = 4,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
		{ "CD", 0x4 },
	},
};

static const struct hda_input_mux alc662_lenovo_101e_capture_source = {
	.num_items = 2,
	.items = {
		{ "Mic", 0x1 },
		{ "Line", 0x2 },
	},
};

static const struct hda_input_mux alc663_capture_source = {
	.num_items = 3,
	.items = {
		{ "Mic", 0x0 },
		{ "Front Mic", 0x1 },
		{ "Line", 0x2 },
	},
};

#if 0 /* set to 1 for testing other input sources below */
static const struct hda_input_mux alc272_nc10_capture_source = {
	.num_items = 16,
	.items = {
		{ "Autoselect Mic", 0x0 },
		{ "Internal Mic", 0x1 },
		{ "In-0x02", 0x2 },
		{ "In-0x03", 0x3 },
		{ "In-0x04", 0x4 },
		{ "In-0x05", 0x5 },
		{ "In-0x06", 0x6 },
		{ "In-0x07", 0x7 },
		{ "In-0x08", 0x8 },
		{ "In-0x09", 0x9 },
		{ "In-0x0a", 0x0a },
		{ "In-0x0b", 0x0b },
		{ "In-0x0c", 0x0c },
		{ "In-0x0d", 0x0d },
		{ "In-0x0e", 0x0e },
		{ "In-0x0f", 0x0f },
	},
};
#endif

/*
 * 2ch mode
 */
static const struct hda_channel_mode alc662_3ST_2ch_modes[1] = {
	{ 2, NULL }
};

/*
 * 2ch mode
 */
static const struct hda_verb alc662_3ST_ch2_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80 },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE },
	{ } /* end */
};

/*
 * 6ch mode
 */
static const struct hda_verb alc662_3ST_ch6_init[] = {
	{ 0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x18, AC_VERB_SET_CONNECT_SEL, 0x02 },
	{ 0x1a, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x1a, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE },
	{ 0x1a, AC_VERB_SET_CONNECT_SEL, 0x01 },
	{ } /* end */
};

static const struct hda_channel_mode alc662_3ST_6ch_modes[2] = {
	{ 2, alc662_3ST_ch2_init },
	{ 6, alc662_3ST_ch6_init },
};

/*
 * 2ch mode
 */
static const struct hda_verb alc662_sixstack_ch6_init[] = {
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00 },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

/*
 * 6ch mode
 */
static const struct hda_verb alc662_sixstack_ch8_init[] = {
	{ 0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ 0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT },
	{ } /* end */
};

static const struct hda_channel_mode alc662_5stack_modes[2] = {
	{ 2, alc662_sixstack_ch6_init },
	{ 6, alc662_sixstack_ch8_init },
};

/* Pin assignment: Front=0x14, Rear=0x15, CLFE=0x16, Side=0x17
 *                 Mic=0x18, Front Mic=0x19, Line-In=0x1a, HP=0x1b
 */

static const struct snd_kcontrol_new alc662_base_mixer[] = {
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

static const struct snd_kcontrol_new alc662_3ST_2ch_mixer[] = {
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
	{ } /* end */
};

static const struct snd_kcontrol_new alc662_3ST_6ch_mixer[] = {
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
	{ } /* end */
};

static const struct snd_kcontrol_new alc662_lenovo_101e_mixer[] = {
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

static const struct snd_kcontrol_new alc662_eeepc_p701_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	ALC262_HIPPO_MASTER_SWITCH,

	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),

	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc662_eeepc_ep20_mixer[] = {
	ALC262_HIPPO_MASTER_SWITCH,
	HDA_CODEC_VOLUME("Front Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x04, 1, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x04, 2, 0x0, HDA_OUTPUT),
	HDA_BIND_MUTE("MuteCtrl Playback Switch", 0x0c, 2, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct hda_bind_ctls alc663_asus_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x02, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x03, 3, 0, HDA_OUTPUT),
		0
	},
};

static const struct hda_bind_ctls alc663_asus_one_bind_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x21, 3, 0, HDA_OUTPUT),
		0
	},
};

static const struct snd_kcontrol_new alc663_m51va_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc663_asus_bind_master_vol),
	HDA_BIND_SW("Master Playback Switch", &alc663_asus_one_bind_switch),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct hda_bind_ctls alc663_asus_tree_bind_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x15, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x21, 3, 0, HDA_OUTPUT),
		0
	},
};

static const struct snd_kcontrol_new alc663_two_hp_m1_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc663_asus_bind_master_vol),
	HDA_BIND_SW("Master Playback Switch", &alc663_asus_tree_bind_switch),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("F-Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("F-Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),

	{ } /* end */
};

static const struct hda_bind_ctls alc663_asus_four_bind_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x15, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x1b, 3, 0, HDA_OUTPUT),
		0
	},
};

static const struct snd_kcontrol_new alc663_two_hp_m2_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc663_asus_bind_master_vol),
	HDA_BIND_SW("Master Playback Switch", &alc663_asus_four_bind_switch),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("F-Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("F-Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc662_1bjd_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("F-Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("F-Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct hda_bind_ctls alc663_asus_two_bind_master_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x02, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x04, 3, 0, HDA_OUTPUT),
		0
	},
};

static const struct hda_bind_ctls alc663_asus_two_bind_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x16, 3, 0, HDA_OUTPUT),
		0
	},
};

static const struct snd_kcontrol_new alc663_asus_21jd_clfe_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume",
				&alc663_asus_two_bind_master_vol),
	HDA_BIND_SW("Master Playback Switch", &alc663_asus_two_bind_switch),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x21, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc663_asus_15jd_clfe_mixer[] = {
	HDA_BIND_VOL("Master Playback Volume", &alc663_asus_bind_master_vol),
	HDA_BIND_SW("Master Playback Switch", &alc663_asus_two_bind_switch),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc663_g71v_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Front Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x21, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc663_g50v_mixer[] = {
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x21, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Line Playback Volume", 0x0b, 0x02, HDA_INPUT),
	HDA_CODEC_MUTE("Line Playback Switch", 0x0b, 0x02, HDA_INPUT),
	{ } /* end */
};

static const struct hda_bind_ctls alc663_asus_mode7_8_all_bind_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x15, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x17, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x1b, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x21, 3, 0, HDA_OUTPUT),
		0
	},
};

static const struct hda_bind_ctls alc663_asus_mode7_8_sp_bind_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x14, 3, 0, HDA_OUTPUT),
		HDA_COMPOSE_AMP_VAL(0x17, 3, 0, HDA_OUTPUT),
		0
	},
};

static const struct snd_kcontrol_new alc663_mode7_mixer[] = {
	HDA_BIND_SW("Master Playback Switch", &alc663_asus_mode7_8_all_bind_switch),
	HDA_BIND_VOL("Speaker Playback Volume", &alc663_asus_bind_master_vol),
	HDA_BIND_SW("Speaker Playback Switch", &alc663_asus_mode7_8_sp_bind_switch),
	HDA_CODEC_MUTE("Headphone1 Playback Switch", 0x1b, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone2 Playback Switch", 0x21, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("IntMic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("IntMic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc663_mode8_mixer[] = {
	HDA_BIND_SW("Master Playback Switch", &alc663_asus_mode7_8_all_bind_switch),
	HDA_BIND_VOL("Speaker Playback Volume", &alc663_asus_bind_master_vol),
	HDA_BIND_SW("Speaker Playback Switch", &alc663_asus_mode7_8_sp_bind_switch),
	HDA_CODEC_MUTE("Headphone1 Playback Switch", 0x15, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone2 Playback Switch", 0x21, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	{ } /* end */
};


static const struct snd_kcontrol_new alc662_chmode_mixer[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Channel Mode",
		.info = alc_ch_mode_info,
		.get = alc_ch_mode_get,
		.put = alc_ch_mode_put,
	},
	{ } /* end */
};

static const struct hda_verb alc662_init_verbs[] = {
	/* ADC: mute amp left and right */
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x09, AC_VERB_SET_CONNECT_SEL, 0x00},

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
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(0)},

	{ }
};

static const struct hda_verb alc662_eapd_init_verbs[] = {
	/* always trun on EAPD */
	{0x14, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{0x15, AC_VERB_SET_EAPD_BTLENABLE, 2},
	{ }
};

static const struct hda_verb alc662_sue_init_verbs[] = {
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_FRONT_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN|ALC880_HP_EVENT},
	{}
};

static const struct hda_verb alc662_eeepc_sue_init_verbs[] = {
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

/* Set Unsolicited Event*/
static const struct hda_verb alc662_eeepc_ep20_sue_init_verbs[] = {
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x14, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static const struct hda_verb alc663_m51va_init_verbs[] = {
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

static const struct hda_verb alc663_21jd_amic_init_verbs[] = {
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x01},	/* Headphone */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static const struct hda_verb alc662_1bjd_amic_init_verbs[] = {
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

static const struct hda_verb alc663_15jd_amic_init_verbs[] = {
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},	/* Headphone */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(1)},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static const struct hda_verb alc663_two_hp_amic_m1_init_verbs[] = {
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

static const struct hda_verb alc663_two_hp_amic_m2_init_verbs[] = {
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

static const struct hda_verb alc663_g71v_init_verbs[] = {
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

static const struct hda_verb alc663_g50v_init_verbs[] = {
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x00},	/* Headphone */

	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static const struct hda_verb alc662_ecs_init_verbs[] = {
	{0x09, AC_VERB_SET_AMP_GAIN_MUTE, 0x701f},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static const struct hda_verb alc272_dell_zm1_init_verbs[] = {
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
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

static const struct hda_verb alc272_dell_init_verbs[] = {
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x13, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x01},	/* Headphone */
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x23, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(9)},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static const struct hda_verb alc663_mode7_init_verbs[] = {
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x1b, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x01},	/* Headphone */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(9)},
	{0x19, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x1b, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static const struct hda_verb alc663_mode8_init_verbs[] = {
	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x15, AC_VERB_SET_CONNECT_SEL, 0x01},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x17, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x17, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x1b, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x21, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x21, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x21, AC_VERB_SET_CONNECT_SEL, 0x01},	/* Headphone */
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_MUTE(0)},
	{0x22, AC_VERB_SET_AMP_GAIN_MUTE, AMP_IN_UNMUTE(9)},
	{0x15, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_MIC_EVENT},
	{0x21, AC_VERB_SET_UNSOLICITED_ENABLE, AC_USRSP_EN | ALC880_HP_EVENT},
	{}
};

static const struct snd_kcontrol_new alc662_auto_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x09, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x09, 0x0, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc272_auto_capture_mixer[] = {
	HDA_CODEC_VOLUME("Capture Volume", 0x08, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Capture Switch", 0x08, 0x0, HDA_INPUT),
	{ } /* end */
};

static void alc662_lenovo_101e_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.line_out_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x15;
	spec->automute = 1;
	spec->detect_line = 1;
	spec->automute_lines = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc662_eeepc_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	alc262_hippo1_setup(codec);
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x19;
	spec->int_mic.mux_idx = 1;
	spec->auto_mic = 1;
}

static void alc662_eeepc_ep20_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x1b;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc663_m51va_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x21;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute_mixer_nid[0] = 0x0c;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_MIXER;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x12;
	spec->int_mic.mux_idx = 9;
	spec->auto_mic = 1;
}

/* ***************** Mode1 ******************************/
static void alc663_mode1_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x21;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute_mixer_nid[0] = 0x0c;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_MIXER;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x19;
	spec->int_mic.mux_idx = 1;
	spec->auto_mic = 1;
}

/* ***************** Mode2 ******************************/
static void alc662_mode2_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_PIN;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x19;
	spec->int_mic.mux_idx = 1;
	spec->auto_mic = 1;
}

/* ***************** Mode3 ******************************/
static void alc663_mode3_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x21;
	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_PIN;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x19;
	spec->int_mic.mux_idx = 1;
	spec->auto_mic = 1;
}

/* ***************** Mode4 ******************************/
static void alc663_mode4_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x21;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->automute_mixer_nid[0] = 0x0c;
	spec->automute_mixer_nid[1] = 0x0e;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_MIXER;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x19;
	spec->int_mic.mux_idx = 1;
	spec->auto_mic = 1;
}

/* ***************** Mode5 ******************************/
static void alc663_mode5_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x16;
	spec->automute_mixer_nid[0] = 0x0c;
	spec->automute_mixer_nid[1] = 0x0e;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_MIXER;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x19;
	spec->int_mic.mux_idx = 1;
	spec->auto_mic = 1;
}

/* ***************** Mode6 ******************************/
static void alc663_mode6_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.hp_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute_mixer_nid[0] = 0x0c;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_MIXER;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x19;
	spec->int_mic.mux_idx = 1;
	spec->auto_mic = 1;
}

/* ***************** Mode7 ******************************/
static void alc663_mode7_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x1b;
	spec->autocfg.hp_pins[0] = 0x21;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x17;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_PIN;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x19;
	spec->int_mic.mux_idx = 1;
	spec->auto_mic = 1;
}

/* ***************** Mode8 ******************************/
static void alc663_mode8_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x21;
	spec->autocfg.hp_pins[1] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[0] = 0x17;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_PIN;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x12;
	spec->int_mic.mux_idx = 9;
	spec->auto_mic = 1;
}

static void alc663_g71v_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	spec->autocfg.hp_pins[0] = 0x21;
	spec->autocfg.line_out_pins[0] = 0x15;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
	spec->detect_line = 1;
	spec->automute_lines = 1;
	spec->ext_mic.pin = 0x18;
	spec->ext_mic.mux_idx = 0;
	spec->int_mic.pin = 0x12;
	spec->int_mic.mux_idx = 9;
	spec->auto_mic = 1;
}

#define alc663_g50v_setup	alc663_m51va_setup

static const struct snd_kcontrol_new alc662_ecs_mixer[] = {
	HDA_CODEC_VOLUME("Master Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	ALC262_HIPPO_MASTER_SWITCH,

	HDA_CODEC_VOLUME("Mic/LineIn Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic/LineIn Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic/LineIn Playback Switch", 0x0b, 0x0, HDA_INPUT),

	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	{ } /* end */
};

static const struct snd_kcontrol_new alc272_nc10_mixer[] = {
	/* Master Playback automatically created from Speaker and Headphone */
	HDA_CODEC_VOLUME("Speaker Playback Volume", 0x02, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Speaker Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x03, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x21, 0x0, HDA_OUTPUT),

	HDA_CODEC_VOLUME("Mic Playback Volume", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_MUTE("Mic Playback Switch", 0x0b, 0x0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),

	HDA_CODEC_VOLUME("Internal Mic Playback Volume", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_MUTE("Internal Mic Playback Switch", 0x0b, 0x1, HDA_INPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x19, 0, HDA_INPUT),
	{ } /* end */
};

#ifdef CONFIG_SND_HDA_POWER_SAVE
#define alc662_loopbacks	alc880_loopbacks
#endif


/* pcm configuration: identical with ALC880 */
#define alc662_pcm_analog_playback	alc880_pcm_analog_playback
#define alc662_pcm_analog_capture	alc880_pcm_analog_capture
#define alc662_pcm_digital_playback	alc880_pcm_digital_playback
#define alc662_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * configuration and preset
 */
static const char * const alc662_models[ALC662_MODEL_LAST] = {
	[ALC662_3ST_2ch_DIG]	= "3stack-dig",
	[ALC662_3ST_6ch_DIG]	= "3stack-6ch-dig",
	[ALC662_3ST_6ch]	= "3stack-6ch",
	[ALC662_5ST_DIG]	= "5stack-dig",
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
	[ALC663_ASUS_MODE7] = "asus-mode7",
	[ALC663_ASUS_MODE8] = "asus-mode8",
	[ALC272_DELL]		= "dell",
	[ALC272_DELL_ZM1]	= "dell-zm1",
	[ALC272_SAMSUNG_NC10]	= "samsung-nc10",
	[ALC662_AUTO]		= "auto",
};

static const struct snd_pci_quirk alc662_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1019, 0x9087, "ECS", ALC662_ECS),
	SND_PCI_QUIRK(0x1028, 0x02d6, "DELL", ALC272_DELL),
	SND_PCI_QUIRK(0x1028, 0x02f4, "DELL ZM1", ALC272_DELL_ZM1),
	SND_PCI_QUIRK(0x1043, 0x1000, "ASUS N50Vm", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1092, "ASUS NB", ALC663_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x1173, "ASUS K73Jn", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x11c3, "ASUS M70V", ALC663_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x11d3, "ASUS NB", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x11f3, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1203, "ASUS NB", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1303, "ASUS G60J", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1333, "ASUS G60Jx", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1339, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x13e3, "ASUS N71JA", ALC663_ASUS_MODE7),
	SND_PCI_QUIRK(0x1043, 0x1463, "ASUS N71", ALC663_ASUS_MODE7),
	SND_PCI_QUIRK(0x1043, 0x14d3, "ASUS G72", ALC663_ASUS_MODE8),
	SND_PCI_QUIRK(0x1043, 0x1563, "ASUS N90", ALC663_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x15d3, "ASUS N50SF F50SF", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x16c3, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x16f3, "ASUS K40C K50C", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1733, "ASUS N81De", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1753, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1763, "ASUS NB", ALC663_ASUS_MODE6),
	SND_PCI_QUIRK(0x1043, 0x1765, "ASUS NB", ALC663_ASUS_MODE6),
	SND_PCI_QUIRK(0x1043, 0x1783, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1793, "ASUS F50GX", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x17b3, "ASUS F70SL", ALC663_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x17c3, "ASUS UX20", ALC663_ASUS_M51VA),
	SND_PCI_QUIRK(0x1043, 0x17f3, "ASUS X58LE", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1813, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1823, "ASUS NB", ALC663_ASUS_MODE5),
	SND_PCI_QUIRK(0x1043, 0x1833, "ASUS NB", ALC663_ASUS_MODE6),
	SND_PCI_QUIRK(0x1043, 0x1843, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1853, "ASUS F50Z", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1864, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1876, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1878, "ASUS M51VA", ALC663_ASUS_M51VA),
	/*SND_PCI_QUIRK(0x1043, 0x1878, "ASUS M50Vr", ALC663_ASUS_MODE1),*/
	SND_PCI_QUIRK(0x1043, 0x1893, "ASUS M50Vm", ALC663_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x1894, "ASUS X55", ALC663_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x18b3, "ASUS N80Vc", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x18c3, "ASUS VX5", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x18d3, "ASUS N81Te", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x18f3, "ASUS N505Tp", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1903, "ASUS F5GL", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1913, "ASUS NB", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1933, "ASUS F80Q", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x1943, "ASUS Vx3V", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1953, "ASUS NB", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1963, "ASUS X71C", ALC663_ASUS_MODE3),
	SND_PCI_QUIRK(0x1043, 0x1983, "ASUS N5051A", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x1993, "ASUS N20", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19a3, "ASUS G50V", ALC663_ASUS_G50V),
	/*SND_PCI_QUIRK(0x1043, 0x19a3, "ASUS NB", ALC663_ASUS_MODE1),*/
	SND_PCI_QUIRK(0x1043, 0x19b3, "ASUS F7Z", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19c3, "ASUS F5Z/F6x", ALC662_ASUS_MODE2),
	SND_PCI_QUIRK(0x1043, 0x19d3, "ASUS NB", ALC663_ASUS_M51VA),
	SND_PCI_QUIRK(0x1043, 0x19e3, "ASUS NB", ALC663_ASUS_MODE1),
	SND_PCI_QUIRK(0x1043, 0x19f3, "ASUS NB", ALC663_ASUS_MODE4),
	SND_PCI_QUIRK(0x1043, 0x8290, "ASUS P5GC-MX", ALC662_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1043, 0x82a1, "ASUS Eeepc", ALC662_ASUS_EEEPC_P701),
	SND_PCI_QUIRK(0x1043, 0x82d1, "ASUS Eeepc EP20", ALC662_ASUS_EEEPC_EP20),
	SND_PCI_QUIRK(0x105b, 0x0cd6, "Foxconn", ALC662_ECS),
	SND_PCI_QUIRK(0x105b, 0x0d47, "Foxconn 45CMX/45GMX/45CMX-K",
		      ALC662_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1179, 0xff6e, "Toshiba NB20x", ALC662_AUTO),
	SND_PCI_QUIRK(0x144d, 0xca00, "Samsung NC10", ALC272_SAMSUNG_NC10),
	SND_PCI_QUIRK(0x1458, 0xa002, "Gigabyte 945GCM-S2L",
		      ALC662_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x152d, 0x2304, "Quanta WH1", ALC663_ASUS_H13),
	SND_PCI_QUIRK(0x1565, 0x820f, "Biostar TA780G M2+", ALC662_3ST_6ch_DIG),
	SND_PCI_QUIRK(0x1631, 0xc10c, "PB RS65", ALC663_ASUS_M51VA),
	SND_PCI_QUIRK(0x17aa, 0x101e, "Lenovo", ALC662_LENOVO_101E),
	SND_PCI_QUIRK(0x1849, 0x3662, "ASROCK K10N78FullHD-hSLI R3.0",
					ALC662_3ST_6ch_DIG),
	SND_PCI_QUIRK_MASK(0x1854, 0xf000, 0x2000, "ASUS H13-200x",
			   ALC663_ASUS_H13),
	SND_PCI_QUIRK(0x1991, 0x5628, "Ordissimo EVE", ALC662_LENOVO_101E),
	{}
};

static const struct alc_config_preset alc662_presets[] = {
	[ALC662_3ST_2ch_DIG] = {
		.mixers = { alc662_3ST_2ch_mixer },
		.init_verbs = { alc662_init_verbs, alc662_eapd_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.dig_in_nid = ALC662_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_capture_source,
	},
	[ALC662_3ST_6ch_DIG] = {
		.mixers = { alc662_3ST_6ch_mixer, alc662_chmode_mixer },
		.init_verbs = { alc662_init_verbs, alc662_eapd_init_verbs },
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
		.mixers = { alc662_3ST_6ch_mixer, alc662_chmode_mixer },
		.init_verbs = { alc662_init_verbs, alc662_eapd_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_6ch_modes),
		.channel_mode = alc662_3ST_6ch_modes,
		.need_dac_fix = 1,
		.input_mux = &alc662_capture_source,
	},
	[ALC662_5ST_DIG] = {
		.mixers = { alc662_base_mixer, alc662_chmode_mixer },
		.init_verbs = { alc662_init_verbs, alc662_eapd_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.dig_in_nid = ALC662_DIGIN_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_5stack_modes),
		.channel_mode = alc662_5stack_modes,
		.input_mux = &alc662_capture_source,
	},
	[ALC662_LENOVO_101E] = {
		.mixers = { alc662_lenovo_101e_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc662_sue_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.input_mux = &alc662_lenovo_101e_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc662_lenovo_101e_setup,
		.init_hook = alc_inithook,
	},
	[ALC662_ASUS_EEEPC_P701] = {
		.mixers = { alc662_eeepc_p701_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc662_eeepc_sue_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc662_eeepc_setup,
		.init_hook = alc_inithook,
	},
	[ALC662_ASUS_EEEPC_EP20] = {
		.mixers = { alc662_eeepc_ep20_mixer,
			    alc662_chmode_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc662_eeepc_ep20_sue_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_6ch_modes),
		.channel_mode = alc662_3ST_6ch_modes,
		.input_mux = &alc662_lenovo_101e_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc662_eeepc_ep20_setup,
		.init_hook = alc_inithook,
	},
	[ALC662_ECS] = {
		.mixers = { alc662_ecs_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc662_ecs_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc662_eeepc_setup,
		.init_hook = alc_inithook,
	},
	[ALC663_ASUS_M51VA] = {
		.mixers = { alc663_m51va_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_m51va_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_m51va_setup,
		.init_hook = alc_inithook,
	},
	[ALC663_ASUS_G71V] = {
		.mixers = { alc663_g71v_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_g71v_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_g71v_setup,
		.init_hook = alc_inithook,
	},
	[ALC663_ASUS_H13] = {
		.mixers = { alc663_m51va_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_m51va_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.setup = alc663_m51va_setup,
		.unsol_event = alc_sku_unsol_event,
		.init_hook = alc_inithook,
	},
	[ALC663_ASUS_G50V] = {
		.mixers = { alc663_g50v_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_g50v_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_6ch_modes),
		.channel_mode = alc662_3ST_6ch_modes,
		.input_mux = &alc663_capture_source,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_g50v_setup,
		.init_hook = alc_inithook,
	},
	[ALC663_ASUS_MODE1] = {
		.mixers = { alc663_m51va_mixer },
		.cap_mixer = alc662_auto_capture_mixer,
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_21jd_amic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.hp_nid = 0x03,
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_mode1_setup,
		.init_hook = alc_inithook,
	},
	[ALC662_ASUS_MODE2] = {
		.mixers = { alc662_1bjd_mixer },
		.cap_mixer = alc662_auto_capture_mixer,
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc662_1bjd_amic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc662_mode2_setup,
		.init_hook = alc_inithook,
	},
	[ALC663_ASUS_MODE3] = {
		.mixers = { alc663_two_hp_m1_mixer },
		.cap_mixer = alc662_auto_capture_mixer,
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_two_hp_amic_m1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.hp_nid = 0x03,
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_mode3_setup,
		.init_hook = alc_inithook,
	},
	[ALC663_ASUS_MODE4] = {
		.mixers = { alc663_asus_21jd_clfe_mixer },
		.cap_mixer = alc662_auto_capture_mixer,
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_21jd_amic_init_verbs},
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.hp_nid = 0x03,
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_mode4_setup,
		.init_hook = alc_inithook,
	},
	[ALC663_ASUS_MODE5] = {
		.mixers = { alc663_asus_15jd_clfe_mixer },
		.cap_mixer = alc662_auto_capture_mixer,
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_15jd_amic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.hp_nid = 0x03,
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_mode5_setup,
		.init_hook = alc_inithook,
	},
	[ALC663_ASUS_MODE6] = {
		.mixers = { alc663_two_hp_m2_mixer },
		.cap_mixer = alc662_auto_capture_mixer,
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_two_hp_amic_m2_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.hp_nid = 0x03,
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_mode6_setup,
		.init_hook = alc_inithook,
	},
	[ALC663_ASUS_MODE7] = {
		.mixers = { alc663_mode7_mixer },
		.cap_mixer = alc662_auto_capture_mixer,
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_mode7_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.hp_nid = 0x03,
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_mode7_setup,
		.init_hook = alc_inithook,
	},
	[ALC663_ASUS_MODE8] = {
		.mixers = { alc663_mode8_mixer },
		.cap_mixer = alc662_auto_capture_mixer,
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_mode8_init_verbs },
		.num_dacs = ARRAY_SIZE(alc662_dac_nids),
		.hp_nid = 0x03,
		.dac_nids = alc662_dac_nids,
		.dig_out_nid = ALC662_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_mode8_setup,
		.init_hook = alc_inithook,
	},
	[ALC272_DELL] = {
		.mixers = { alc663_m51va_mixer },
		.cap_mixer = alc272_auto_capture_mixer,
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc272_dell_init_verbs },
		.num_dacs = ARRAY_SIZE(alc272_dac_nids),
		.dac_nids = alc272_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.adc_nids = alc272_adc_nids,
		.num_adc_nids = ARRAY_SIZE(alc272_adc_nids),
		.capsrc_nids = alc272_capsrc_nids,
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_m51va_setup,
		.init_hook = alc_inithook,
	},
	[ALC272_DELL_ZM1] = {
		.mixers = { alc663_m51va_mixer },
		.cap_mixer = alc662_auto_capture_mixer,
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc272_dell_zm1_init_verbs },
		.num_dacs = ARRAY_SIZE(alc272_dac_nids),
		.dac_nids = alc272_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.adc_nids = alc662_adc_nids,
		.num_adc_nids = 1,
		.capsrc_nids = alc662_capsrc_nids,
		.channel_mode = alc662_3ST_2ch_modes,
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_m51va_setup,
		.init_hook = alc_inithook,
	},
	[ALC272_SAMSUNG_NC10] = {
		.mixers = { alc272_nc10_mixer },
		.init_verbs = { alc662_init_verbs,
				alc662_eapd_init_verbs,
				alc663_21jd_amic_init_verbs },
		.num_dacs = ARRAY_SIZE(alc272_dac_nids),
		.dac_nids = alc272_dac_nids,
		.num_channel_mode = ARRAY_SIZE(alc662_3ST_2ch_modes),
		.channel_mode = alc662_3ST_2ch_modes,
		/*.input_mux = &alc272_nc10_capture_source,*/
		.unsol_event = alc_sku_unsol_event,
		.setup = alc663_mode4_setup,
		.init_hook = alc_inithook,
	},
};


/*
 * BIOS auto configuration
 */

/* convert from MIX nid to DAC */
static hda_nid_t alc_auto_mix_to_dac(struct hda_codec *codec, hda_nid_t nid)
{
	hda_nid_t list[5];
	int i, num;

	num = snd_hda_get_connections(codec, nid, list, ARRAY_SIZE(list));
	for (i = 0; i < num; i++) {
		if (get_wcaps_type(get_wcaps(codec, list[i])) == AC_WID_AUD_OUT)
			return list[i];
	}
	return 0;
}

/* go down to the selector widget before the mixer */
static hda_nid_t alc_go_down_to_selector(struct hda_codec *codec, hda_nid_t pin)
{
	hda_nid_t srcs[5];
	int num = snd_hda_get_connections(codec, pin, srcs,
					  ARRAY_SIZE(srcs));
	if (num != 1 ||
	    get_wcaps_type(get_wcaps(codec, srcs[0])) != AC_WID_AUD_SEL)
		return pin;
	return srcs[0];
}

/* get MIX nid connected to the given pin targeted to DAC */
static hda_nid_t alc_auto_dac_to_mix(struct hda_codec *codec, hda_nid_t pin,
				   hda_nid_t dac)
{
	hda_nid_t mix[5];
	int i, num;

	pin = alc_go_down_to_selector(codec, pin);
	num = snd_hda_get_connections(codec, pin, mix, ARRAY_SIZE(mix));
	for (i = 0; i < num; i++) {
		if (alc_auto_mix_to_dac(codec, mix[i]) == dac)
			return mix[i];
	}
	return 0;
}

/* select the connection from pin to DAC if needed */
static int alc_auto_select_dac(struct hda_codec *codec, hda_nid_t pin,
			       hda_nid_t dac)
{
	hda_nid_t mix[5];
	int i, num;

	pin = alc_go_down_to_selector(codec, pin);
	num = snd_hda_get_connections(codec, pin, mix, ARRAY_SIZE(mix));
	if (num < 2)
		return 0;
	for (i = 0; i < num; i++) {
		if (alc_auto_mix_to_dac(codec, mix[i]) == dac) {
			snd_hda_codec_update_cache(codec, pin, 0,
						   AC_VERB_SET_CONNECT_SEL, i);
			return 0;
		}
	}
	return 0;
}

/* look for an empty DAC slot */
static hda_nid_t alc_auto_look_for_dac(struct hda_codec *codec, hda_nid_t pin)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t srcs[5];
	int i, j, num;

	pin = alc_go_down_to_selector(codec, pin);
	num = snd_hda_get_connections(codec, pin, srcs, ARRAY_SIZE(srcs));
	for (i = 0; i < num; i++) {
		hda_nid_t nid = alc_auto_mix_to_dac(codec, srcs[i]);
		if (!nid)
			continue;
		for (j = 0; j < spec->multiout.num_dacs; j++)
			if (spec->multiout.dac_nids[j] == nid)
				break;
		if (j >= spec->multiout.num_dacs)
			return nid;
	}
	return 0;
}

/* fill in the dac_nids table from the parsed pin configuration */
static int alc662_auto_fill_dac_nids(struct hda_codec *codec,
				     const struct auto_pin_cfg *cfg)
{
	struct alc_spec *spec = codec->spec;
	int i;
	hda_nid_t dac;

	spec->multiout.dac_nids = spec->private_dac_nids;
	for (i = 0; i < cfg->line_outs; i++) {
		dac = alc_auto_look_for_dac(codec, cfg->line_out_pins[i]);
		if (!dac)
			continue;
		spec->private_dac_nids[spec->multiout.num_dacs++] = dac;
	}
	return 0;
}

static inline int __alc662_add_vol_ctl(struct alc_spec *spec, const char *pfx,
				       hda_nid_t nid, int idx, unsigned int chs)
{
	return __add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL, pfx, idx,
			   HDA_COMPOSE_AMP_VAL(nid, chs, 0, HDA_OUTPUT));
}

static inline int __alc662_add_sw_ctl(struct alc_spec *spec, const char *pfx,
				      hda_nid_t nid, int idx, unsigned int chs)
{
	return __add_pb_sw_ctrl(spec, ALC_CTL_WIDGET_MUTE, pfx, idx,
			   HDA_COMPOSE_AMP_VAL(nid, chs, 0, HDA_INPUT));
}

#define alc662_add_vol_ctl(spec, pfx, nid, chs) \
	__alc662_add_vol_ctl(spec, pfx, nid, 0, chs)
#define alc662_add_sw_ctl(spec, pfx, nid, chs) \
	__alc662_add_sw_ctl(spec, pfx, nid, 0, chs)
#define alc662_add_stereo_vol(spec, pfx, nid) \
	alc662_add_vol_ctl(spec, pfx, nid, 3)
#define alc662_add_stereo_sw(spec, pfx, nid) \
	alc662_add_sw_ctl(spec, pfx, nid, 3)

/* add playback controls from the parsed DAC table */
static int alc662_auto_create_multi_out_ctls(struct hda_codec *codec,
					     const struct auto_pin_cfg *cfg)
{
	struct alc_spec *spec = codec->spec;
	static const char * const chname[4] = {
		"Front", "Surround", NULL /*CLFE*/, "Side"
	};
	const char *pfx = alc_get_line_out_pfx(spec, true);
	hda_nid_t nid, mix, pin;
	int i, err, noutputs;

	noutputs = cfg->line_outs;
	if (spec->multi_ios > 0)
		noutputs += spec->multi_ios;

	for (i = 0; i < noutputs; i++) {
		nid = spec->multiout.dac_nids[i];
		if (!nid)
			continue;
		if (i >= cfg->line_outs)
			pin = spec->multi_io[i - 1].pin;
		else
			pin = cfg->line_out_pins[i];
		mix = alc_auto_dac_to_mix(codec, pin, nid);
		if (!mix)
			continue;
		if (!pfx && i == 2) {
			/* Center/LFE */
			err = alc662_add_vol_ctl(spec, "Center", nid, 1);
			if (err < 0)
				return err;
			err = alc662_add_vol_ctl(spec, "LFE", nid, 2);
			if (err < 0)
				return err;
			err = alc662_add_sw_ctl(spec, "Center", mix, 1);
			if (err < 0)
				return err;
			err = alc662_add_sw_ctl(spec, "LFE", mix, 2);
			if (err < 0)
				return err;
		} else {
			const char *name = pfx;
			int index = i;
			if (!name) {
				name = chname[i];
				index = 0;
			}
			err = __alc662_add_vol_ctl(spec, name, nid, index, 3);
			if (err < 0)
				return err;
			err = __alc662_add_sw_ctl(spec, name, mix, index, 3);
			if (err < 0)
				return err;
		}
	}
	return 0;
}

/* add playback controls for speaker and HP outputs */
/* return DAC nid if any new DAC is assigned */
static int alc662_auto_create_extra_out(struct hda_codec *codec, hda_nid_t pin,
					const char *pfx)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t nid, mix;
	int err;

	if (!pin)
		return 0;
	nid = alc_auto_look_for_dac(codec, pin);
	if (!nid) {
		/* the corresponding DAC is already occupied */
		if (!(get_wcaps(codec, pin) & AC_WCAP_OUT_AMP))
			return 0; /* no way */
		/* create a switch only */
		return add_pb_sw_ctrl(spec, ALC_CTL_WIDGET_MUTE, pfx,
				   HDA_COMPOSE_AMP_VAL(pin, 3, 0, HDA_OUTPUT));
	}

	mix = alc_auto_dac_to_mix(codec, pin, nid);
	if (!mix)
		return 0;
	err = alc662_add_vol_ctl(spec, pfx, nid, 3);
	if (err < 0)
		return err;
	err = alc662_add_sw_ctl(spec, pfx, mix, 3);
	if (err < 0)
		return err;
	return nid;
}

/* create playback/capture controls for input pins */
#define alc662_auto_create_input_ctls \
	alc882_auto_create_input_ctls

static void alc662_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid, int pin_type,
					      hda_nid_t dac)
{
	int i, num;
	hda_nid_t srcs[HDA_MAX_CONNECTIONS];

	alc_set_pin_output(codec, nid, pin_type);
	num = snd_hda_get_connections(codec, nid, srcs, ARRAY_SIZE(srcs));
	for (i = 0; i < num; i++) {
		if (alc_auto_mix_to_dac(codec, srcs[i]) != dac)
			continue;
		/* need the manual connection? */
		if (num > 1)
			snd_hda_codec_write(codec, nid, 0,
					    AC_VERB_SET_CONNECT_SEL, i);
		/* unmute mixer widget inputs */
		snd_hda_codec_write(codec, srcs[i], 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_IN_UNMUTE(0));
		snd_hda_codec_write(codec, srcs[i], 0,
				    AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_IN_UNMUTE(1));
		return;
	}
}

static void alc662_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int pin_type = get_pin_type(spec->autocfg.line_out_type);
	int i;

	for (i = 0; i <= HDA_SIDE; i++) {
		hda_nid_t nid = spec->autocfg.line_out_pins[i];
		if (nid)
			alc662_auto_set_output_and_unmute(codec, nid, pin_type,
					spec->multiout.dac_nids[i]);
	}
}

static void alc662_auto_init_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pins[0];
	if (pin)
		alc662_auto_set_output_and_unmute(codec, pin, PIN_HP,
						  spec->multiout.hp_nid);
	pin = spec->autocfg.speaker_pins[0];
	if (pin)
		alc662_auto_set_output_and_unmute(codec, pin, PIN_OUT,
					spec->multiout.extra_out_nid[0]);
}

#define ALC662_PIN_CD_NID		ALC880_PIN_CD_NID

static void alc662_auto_init_analog_input(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		hda_nid_t nid = cfg->inputs[i].pin;
		if (alc_is_input_pin(codec, nid)) {
			alc_set_input_pin(codec, nid, cfg->inputs[i].type);
			if (nid != ALC662_PIN_CD_NID &&
			    (get_wcaps(codec, nid) & AC_WCAP_OUT_AMP))
				snd_hda_codec_write(codec, nid, 0,
						    AC_VERB_SET_AMP_GAIN_MUTE,
						    AMP_OUT_MUTE);
		}
	}
}

#define alc662_auto_init_input_src	alc882_auto_init_input_src

/*
 * multi-io helper
 */
static int alc_auto_fill_multi_ios(struct hda_codec *codec,
				   unsigned int location)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int type, i, num_pins = 0;

	for (type = AUTO_PIN_LINE_IN; type >= AUTO_PIN_MIC; type--) {
		for (i = 0; i < cfg->num_inputs; i++) {
			hda_nid_t nid = cfg->inputs[i].pin;
			hda_nid_t dac;
			unsigned int defcfg, caps;
			if (cfg->inputs[i].type != type)
				continue;
			defcfg = snd_hda_codec_get_pincfg(codec, nid);
			if (get_defcfg_connect(defcfg) != AC_JACK_PORT_COMPLEX)
				continue;
			if (location && get_defcfg_location(defcfg) != location)
				continue;
			caps = snd_hda_query_pin_caps(codec, nid);
			if (!(caps & AC_PINCAP_OUT))
				continue;
			dac = alc_auto_look_for_dac(codec, nid);
			if (!dac)
				continue;
			spec->multi_io[num_pins].pin = nid;
			spec->multi_io[num_pins].dac = dac;
			num_pins++;
			spec->private_dac_nids[spec->multiout.num_dacs++] = dac;
		}
	}
	spec->multiout.num_dacs = 1;
	if (num_pins < 2)
		return 0;
	return num_pins;
}

static int alc_auto_ch_mode_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = spec->multi_ios + 1;
	if (uinfo->value.enumerated.item > spec->multi_ios)
		uinfo->value.enumerated.item = spec->multi_ios;
	sprintf(uinfo->value.enumerated.name, "%dch",
		(uinfo->value.enumerated.item + 1) * 2);
	return 0;
}

static int alc_auto_ch_mode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	ucontrol->value.enumerated.item[0] = (spec->ext_channel_count - 1) / 2;
	return 0;
}

static int alc_set_multi_io(struct hda_codec *codec, int idx, bool output)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t nid = spec->multi_io[idx].pin;

	if (!spec->multi_io[idx].ctl_in)
		spec->multi_io[idx].ctl_in =
			snd_hda_codec_read(codec, nid, 0,
					   AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
	if (output) {
		snd_hda_codec_update_cache(codec, nid, 0,
					   AC_VERB_SET_PIN_WIDGET_CONTROL,
					   PIN_OUT);
		if (get_wcaps(codec, nid) & AC_WCAP_OUT_AMP)
			snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE, 0);
		alc_auto_select_dac(codec, nid, spec->multi_io[idx].dac);
	} else {
		if (get_wcaps(codec, nid) & AC_WCAP_OUT_AMP)
			snd_hda_codec_amp_stereo(codec, nid, HDA_OUTPUT, 0,
						 HDA_AMP_MUTE, HDA_AMP_MUTE);
		snd_hda_codec_update_cache(codec, nid, 0,
					   AC_VERB_SET_PIN_WIDGET_CONTROL,
					   spec->multi_io[idx].ctl_in);
	}
	return 0;
}

static int alc_auto_ch_mode_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct alc_spec *spec = codec->spec;
	int i, ch;

	ch = ucontrol->value.enumerated.item[0];
	if (ch < 0 || ch > spec->multi_ios)
		return -EINVAL;
	if (ch == (spec->ext_channel_count - 1) / 2)
		return 0;
	spec->ext_channel_count = (ch + 1) * 2;
	for (i = 0; i < spec->multi_ios; i++)
		alc_set_multi_io(codec, i, i < ch);
	spec->multiout.max_channels = spec->ext_channel_count;
	return 1;
}

static const struct snd_kcontrol_new alc_auto_channel_mode_enum = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Channel Mode",
	.info = alc_auto_ch_mode_info,
	.get = alc_auto_ch_mode_get,
	.put = alc_auto_ch_mode_put,
};

static int alc_auto_add_multi_channel_mode(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int location, defcfg;
	int num_pins;

	if (cfg->line_outs != 1 ||
	    cfg->line_out_type != AUTO_PIN_LINE_OUT)
		return 0;

	defcfg = snd_hda_codec_get_pincfg(codec, cfg->line_out_pins[0]);
	location = get_defcfg_location(defcfg);

	num_pins = alc_auto_fill_multi_ios(codec, location);
	if (num_pins > 0) {
		struct snd_kcontrol_new *knew;

		knew = alc_kcontrol_new(spec);
		if (!knew)
			return -ENOMEM;
		*knew = alc_auto_channel_mode_enum;
		knew->name = kstrdup("Channel Mode", GFP_KERNEL);
		if (!knew->name)
			return -ENOMEM;

		spec->multi_ios = num_pins;
		spec->ext_channel_count = 2;
		spec->multiout.num_dacs = num_pins + 1;
	}
	return 0;
}

static int alc662_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static const hda_nid_t alc662_ignore[] = { 0x1d, 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc662_ignore);
	if (err < 0)
		return err;
	if (!spec->autocfg.line_outs)
		return 0; /* can't find valid BIOS pin config */

	err = alc662_auto_fill_dac_nids(codec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc_auto_add_multi_channel_mode(codec);
	if (err < 0)
		return err;
	err = alc662_auto_create_multi_out_ctls(codec, &spec->autocfg);
	if (err < 0)
		return err;
	err = alc662_auto_create_extra_out(codec,
					   spec->autocfg.speaker_pins[0],
					   "Speaker");
	if (err < 0)
		return err;
	if (err)
		spec->multiout.extra_out_nid[0] = err;
	err = alc662_auto_create_extra_out(codec, spec->autocfg.hp_pins[0],
					   "Headphone");
	if (err < 0)
		return err;
	if (err)
		spec->multiout.hp_nid = err;
	err = alc662_auto_create_input_ctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = spec->multiout.num_dacs * 2;

	alc_auto_parse_digital(codec);

	if (spec->kctls.list)
		add_mixer(spec, spec->kctls.list);

	spec->num_mux_defs = 1;
	spec->input_mux = &spec->private_imux[0];

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	if (codec->vendor_id == 0x10ec0272 || codec->vendor_id == 0x10ec0663 ||
	    codec->vendor_id == 0x10ec0665 || codec->vendor_id == 0x10ec0670)
	    alc_ssid_check(codec, 0x15, 0x1b, 0x14, 0x21);
	else
	    alc_ssid_check(codec, 0x15, 0x1b, 0x14, 0);

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
	alc_auto_init_digital(codec);
	if (spec->unsol_event)
		alc_inithook(codec);
}

static void alc272_fixup_mario(struct hda_codec *codec,
			       const struct alc_fixup *fix, int action)
{
	if (action != ALC_FIXUP_ACT_PROBE)
		return;
	if (snd_hda_override_amp_caps(codec, 0x2, HDA_OUTPUT,
				      (0x3b << AC_AMPCAP_OFFSET_SHIFT) |
				      (0x3b << AC_AMPCAP_NUM_STEPS_SHIFT) |
				      (0x03 << AC_AMPCAP_STEP_SIZE_SHIFT) |
				      (0 << AC_AMPCAP_MUTE_SHIFT)))
		printk(KERN_WARNING
		       "hda_codec: failed to override amp caps for NID 0x2\n");
}

enum {
	ALC662_FIXUP_ASPIRE,
	ALC662_FIXUP_IDEAPAD,
	ALC272_FIXUP_MARIO,
	ALC662_FIXUP_CZC_P10T,
	ALC662_FIXUP_SKU_IGNORE,
};

static const struct alc_fixup alc662_fixups[] = {
	[ALC662_FIXUP_ASPIRE] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x15, 0x99130112 }, /* subwoofer */
			{ }
		}
	},
	[ALC662_FIXUP_IDEAPAD] = {
		.type = ALC_FIXUP_PINS,
		.v.pins = (const struct alc_pincfg[]) {
			{ 0x17, 0x99130112 }, /* subwoofer */
			{ }
		}
	},
	[ALC272_FIXUP_MARIO] = {
		.type = ALC_FIXUP_FUNC,
		.v.func = alc272_fixup_mario,
	},
	[ALC662_FIXUP_CZC_P10T] = {
		.type = ALC_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x14, AC_VERB_SET_EAPD_BTLENABLE, 0},
			{}
		}
	},
	[ALC662_FIXUP_SKU_IGNORE] = {
		.type = ALC_FIXUP_SKU,
		.v.sku = ALC_FIXUP_SKU_IGNORE,
	},
};

static const struct snd_pci_quirk alc662_fixup_tbl[] = {
	SND_PCI_QUIRK(0x1025, 0x0308, "Acer Aspire 8942G", ALC662_FIXUP_ASPIRE),
	SND_PCI_QUIRK(0x1025, 0x031c, "Gateway NV79", ALC662_FIXUP_SKU_IGNORE),
	SND_PCI_QUIRK(0x1025, 0x038b, "Acer Aspire 8943G", ALC662_FIXUP_ASPIRE),
	SND_PCI_QUIRK(0x144d, 0xc051, "Samsung R720", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x17aa, 0x38af, "Lenovo Ideapad Y550P", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x17aa, 0x3a0d, "Lenovo Ideapad Y550", ALC662_FIXUP_IDEAPAD),
	SND_PCI_QUIRK(0x1b35, 0x2206, "CZC P10T", ALC662_FIXUP_CZC_P10T),
	{}
};

static const struct alc_model_fixup alc662_fixup_models[] = {
	{.id = ALC272_FIXUP_MARIO, .name = "mario"},
	{}
};


static int patch_alc662(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int err, board_config;
	int coef;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	codec->spec = spec;

	alc_auto_parse_customize_define(codec);

	alc_fix_pll_init(codec, 0x20, 0x04, 15);

	coef = alc_read_coef_idx(codec, 0);
	if (coef == 0x8020 || coef == 0x8011)
		alc_codec_rename(codec, "ALC661");
	else if (coef & (1 << 14) &&
		codec->bus->pci->subsystem_vendor == 0x1025 &&
		spec->cdefine.platform_type == 1)
		alc_codec_rename(codec, "ALC272X");
	else if (coef == 0x4011)
		alc_codec_rename(codec, "ALC656");

	board_config = snd_hda_check_board_config(codec, ALC662_MODEL_LAST,
						  alc662_models,
			  	                  alc662_cfg_tbl);
	if (board_config < 0) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC662_AUTO;
	}

	if (board_config == ALC662_AUTO) {
		alc_pick_fixup(codec, alc662_fixup_models,
			       alc662_fixup_tbl, alc662_fixups);
		alc_apply_fixup(codec, ALC_FIXUP_ACT_PRE_PROBE);
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

	if (has_cdefine_beep(codec)) {
		err = snd_hda_attach_beep_device(codec, 0x1);
		if (err < 0) {
			alc_free(codec);
			return err;
		}
	}

	if (board_config != ALC662_AUTO)
		setup_preset(codec, &alc662_presets[board_config]);

	spec->stream_analog_playback = &alc662_pcm_analog_playback;
	spec->stream_analog_capture = &alc662_pcm_analog_capture;

	spec->stream_digital_playback = &alc662_pcm_digital_playback;
	spec->stream_digital_capture = &alc662_pcm_digital_capture;

	if (!spec->adc_nids) {
		spec->adc_nids = alc662_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(alc662_adc_nids);
	}
	if (!spec->capsrc_nids)
		spec->capsrc_nids = alc662_capsrc_nids;

	if (!spec->cap_mixer)
		set_capture_mixer(codec);

	if (has_cdefine_beep(codec)) {
		switch (codec->vendor_id) {
		case 0x10ec0662:
			set_beep_amp(spec, 0x0b, 0x05, HDA_INPUT);
			break;
		case 0x10ec0272:
		case 0x10ec0663:
		case 0x10ec0665:
			set_beep_amp(spec, 0x0b, 0x04, HDA_INPUT);
			break;
		case 0x10ec0273:
			set_beep_amp(spec, 0x0b, 0x03, HDA_INPUT);
			break;
		}
	}
	spec->vmaster_nid = 0x02;

	alc_apply_fixup(codec, ALC_FIXUP_ACT_PROBE);

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC662_AUTO)
		spec->init_hook = alc662_auto_init;
	spec->shutup = alc_eapd_shutup;

	alc_init_jacks(codec);

#ifdef CONFIG_SND_HDA_POWER_SAVE
	if (!spec->loopback.amplist)
		spec->loopback.amplist = alc662_loopbacks;
#endif

	return 0;
}

static int patch_alc888(struct hda_codec *codec)
{
	if ((alc_read_coef_idx(codec, 0) & 0x00f0)==0x0030){
		kfree(codec->chip_name);
		if (codec->vendor_id == 0x10ec0887)
			codec->chip_name = kstrdup("ALC887-VD", GFP_KERNEL);
		else
			codec->chip_name = kstrdup("ALC888-VD", GFP_KERNEL);
		if (!codec->chip_name) {
			alc_free(codec);
			return -ENOMEM;
		}
		return patch_alc662(codec);
	}
	return patch_alc882(codec);
}

static int patch_alc899(struct hda_codec *codec)
{
	if ((alc_read_coef_idx(codec, 0) & 0x2000) != 0x2000) {
		kfree(codec->chip_name);
		codec->chip_name = kstrdup("ALC898", GFP_KERNEL);
	}
	return patch_alc882(codec);
}

/*
 * ALC680 support
 */
#define ALC680_DIGIN_NID	ALC880_DIGIN_NID
#define ALC680_DIGOUT_NID	ALC880_DIGOUT_NID
#define alc680_modes		alc260_modes

static const hda_nid_t alc680_dac_nids[3] = {
	/* Lout1, Lout2, hp */
	0x02, 0x03, 0x04
};

static const hda_nid_t alc680_adc_nids[3] = {
	/* ADC0-2 */
	/* DMIC, MIC, Line-in*/
	0x07, 0x08, 0x09
};

/*
 * Analog capture ADC cgange
 */
static void alc680_rec_autoswitch(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int pin_found = 0;
	int type_found = AUTO_PIN_LAST;
	hda_nid_t nid;
	int i;

	for (i = 0; i < cfg->num_inputs; i++) {
		nid = cfg->inputs[i].pin;
		if (!is_jack_detectable(codec, nid))
			continue;
		if (snd_hda_jack_detect(codec, nid)) {
			if (cfg->inputs[i].type < type_found) {
				type_found = cfg->inputs[i].type;
				pin_found = nid;
			}
		}
	}

	nid = 0x07;
	if (pin_found)
		snd_hda_get_connections(codec, pin_found, &nid, 1);

	if (nid != spec->cur_adc)
		__snd_hda_codec_cleanup_stream(codec, spec->cur_adc, 1);
	spec->cur_adc = nid;
	snd_hda_codec_setup_stream(codec, nid, spec->cur_adc_stream_tag, 0,
				   spec->cur_adc_format);
}

static int alc680_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      unsigned int stream_tag,
				      unsigned int format,
				      struct snd_pcm_substream *substream)
{
	struct alc_spec *spec = codec->spec;

	spec->cur_adc = 0x07;
	spec->cur_adc_stream_tag = stream_tag;
	spec->cur_adc_format = format;

	alc680_rec_autoswitch(codec);
	return 0;
}

static int alc680_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
				      struct hda_codec *codec,
				      struct snd_pcm_substream *substream)
{
	snd_hda_codec_cleanup_stream(codec, 0x07);
	snd_hda_codec_cleanup_stream(codec, 0x08);
	snd_hda_codec_cleanup_stream(codec, 0x09);
	return 0;
}

static const struct hda_pcm_stream alc680_pcm_analog_auto_capture = {
	.substreams = 1, /* can be overridden */
	.channels_min = 2,
	.channels_max = 2,
	/* NID is set in alc_build_pcms */
	.ops = {
		.prepare = alc680_capture_pcm_prepare,
		.cleanup = alc680_capture_pcm_cleanup
	},
};

static const struct snd_kcontrol_new alc680_base_mixer[] = {
	/* output mixer control */
	HDA_CODEC_VOLUME("Front Playback Volume", 0x2, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Front Playback Switch", 0x14, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Headphone Playback Volume", 0x4, 0x0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Headphone Playback Switch", 0x16, 0x0, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Internal Mic Boost Volume", 0x12, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Mic Boost Volume", 0x18, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("Line In Boost Volume", 0x19, 0, HDA_INPUT),
	{ }
};

static const struct hda_bind_ctls alc680_bind_cap_vol = {
	.ops = &snd_hda_bind_vol,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x07, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x08, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x09, 3, 0, HDA_INPUT),
		0
	},
};

static const struct hda_bind_ctls alc680_bind_cap_switch = {
	.ops = &snd_hda_bind_sw,
	.values = {
		HDA_COMPOSE_AMP_VAL(0x07, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x08, 3, 0, HDA_INPUT),
		HDA_COMPOSE_AMP_VAL(0x09, 3, 0, HDA_INPUT),
		0
	},
};

static const struct snd_kcontrol_new alc680_master_capture_mixer[] = {
	HDA_BIND_VOL("Capture Volume", &alc680_bind_cap_vol),
	HDA_BIND_SW("Capture Switch", &alc680_bind_cap_switch),
	{ } /* end */
};

/*
 * generic initialization of ADC, input mixers and output mixers
 */
static const struct hda_verb alc680_init_verbs[] = {
	{0x02, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x03, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},
	{0x04, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_UNMUTE},

	{0x12, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},
	{0x14, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x15, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_OUT},
	{0x16, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_HP},
	{0x18, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_VREF80},
	{0x19, AC_VERB_SET_PIN_WIDGET_CONTROL, PIN_IN},

	{0x14, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x15, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x16, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x18, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},
	{0x19, AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_MUTE},

	{0x16, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_HP_EVENT   | AC_USRSP_EN},
	{0x18, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_MIC_EVENT  | AC_USRSP_EN},
	{0x19, AC_VERB_SET_UNSOLICITED_ENABLE, ALC880_MIC_EVENT  | AC_USRSP_EN},

	{ }
};

/* toggle speaker-output according to the hp-jack state */
static void alc680_base_setup(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;

	spec->autocfg.hp_pins[0] = 0x16;
	spec->autocfg.speaker_pins[0] = 0x14;
	spec->autocfg.speaker_pins[1] = 0x15;
	spec->autocfg.num_inputs = 2;
	spec->autocfg.inputs[0].pin = 0x18;
	spec->autocfg.inputs[0].type = AUTO_PIN_MIC;
	spec->autocfg.inputs[1].pin = 0x19;
	spec->autocfg.inputs[1].type = AUTO_PIN_LINE_IN;
	spec->automute = 1;
	spec->automute_mode = ALC_AUTOMUTE_AMP;
}

static void alc680_unsol_event(struct hda_codec *codec,
					   unsigned int res)
{
	if ((res >> 26) == ALC880_HP_EVENT)
		alc_hp_automute(codec);
	if ((res >> 26) == ALC880_MIC_EVENT)
		alc680_rec_autoswitch(codec);
}

static void alc680_inithook(struct hda_codec *codec)
{
	alc_hp_automute(codec);
	alc680_rec_autoswitch(codec);
}

/* create input playback/capture controls for the given pin */
static int alc680_new_analog_output(struct alc_spec *spec, hda_nid_t nid,
				    const char *ctlname, int idx)
{
	hda_nid_t dac;
	int err;

	switch (nid) {
	case 0x14:
		dac = 0x02;
		break;
	case 0x15:
		dac = 0x03;
		break;
	case 0x16:
		dac = 0x04;
		break;
	default:
		return 0;
	}
	if (spec->multiout.dac_nids[0] != dac &&
	    spec->multiout.dac_nids[1] != dac) {
		err = add_pb_vol_ctrl(spec, ALC_CTL_WIDGET_VOL, ctlname,
				  HDA_COMPOSE_AMP_VAL(dac, 3, idx,
						      HDA_OUTPUT));
		if (err < 0)
			return err;

		err = add_pb_sw_ctrl(spec, ALC_CTL_WIDGET_MUTE, ctlname,
			  HDA_COMPOSE_AMP_VAL(nid, 3, idx, HDA_OUTPUT));

		if (err < 0)
			return err;
		spec->private_dac_nids[spec->multiout.num_dacs++] = dac;
	}

	return 0;
}

/* add playback controls from the parsed DAC table */
static int alc680_auto_create_multi_out_ctls(struct alc_spec *spec,
					     const struct auto_pin_cfg *cfg)
{
	hda_nid_t nid;
	int err;

	spec->multiout.dac_nids = spec->private_dac_nids;

	nid = cfg->line_out_pins[0];
	if (nid) {
		const char *name;
		if (cfg->line_out_type == AUTO_PIN_SPEAKER_OUT)
			name = "Speaker";
		else
			name = "Front";
		err = alc680_new_analog_output(spec, nid, name, 0);
		if (err < 0)
			return err;
	}

	nid = cfg->speaker_pins[0];
	if (nid) {
		err = alc680_new_analog_output(spec, nid, "Speaker", 0);
		if (err < 0)
			return err;
	}
	nid = cfg->hp_pins[0];
	if (nid) {
		err = alc680_new_analog_output(spec, nid, "Headphone", 0);
		if (err < 0)
			return err;
	}

	return 0;
}

static void alc680_auto_set_output_and_unmute(struct hda_codec *codec,
					      hda_nid_t nid, int pin_type)
{
	alc_set_pin_output(codec, nid, pin_type);
}

static void alc680_auto_init_multi_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t nid = spec->autocfg.line_out_pins[0];
	if (nid) {
		int pin_type = get_pin_type(spec->autocfg.line_out_type);
		alc680_auto_set_output_and_unmute(codec, nid, pin_type);
	}
}

static void alc680_auto_init_hp_out(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	hda_nid_t pin;

	pin = spec->autocfg.hp_pins[0];
	if (pin)
		alc680_auto_set_output_and_unmute(codec, pin, PIN_HP);
	pin = spec->autocfg.speaker_pins[0];
	if (pin)
		alc680_auto_set_output_and_unmute(codec, pin, PIN_OUT);
}

/* pcm configuration: identical with ALC880 */
#define alc680_pcm_analog_playback	alc880_pcm_analog_playback
#define alc680_pcm_analog_capture	alc880_pcm_analog_capture
#define alc680_pcm_analog_alt_capture	alc880_pcm_analog_alt_capture
#define alc680_pcm_digital_playback	alc880_pcm_digital_playback
#define alc680_pcm_digital_capture	alc880_pcm_digital_capture

/*
 * BIOS auto configuration
 */
static int alc680_parse_auto_config(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	int err;
	static const hda_nid_t alc680_ignore[] = { 0 };

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg,
					   alc680_ignore);
	if (err < 0)
		return err;

	if (!spec->autocfg.line_outs) {
		if (spec->autocfg.dig_outs || spec->autocfg.dig_in_pin) {
			spec->multiout.max_channels = 2;
			spec->no_analog = 1;
			goto dig_only;
		}
		return 0; /* can't find valid BIOS pin config */
	}
	err = alc680_auto_create_multi_out_ctls(spec, &spec->autocfg);
	if (err < 0)
		return err;

	spec->multiout.max_channels = 2;

 dig_only:
	/* digital only support output */
	alc_auto_parse_digital(codec);
	if (spec->kctls.list)
		add_mixer(spec, spec->kctls.list);

	add_verb(spec, alc680_init_verbs);

	err = alc_auto_add_mic_boost(codec);
	if (err < 0)
		return err;

	return 1;
}

#define alc680_auto_init_analog_input	alc882_auto_init_analog_input

/* init callback for auto-configuration model -- overriding the default init */
static void alc680_auto_init(struct hda_codec *codec)
{
	struct alc_spec *spec = codec->spec;
	alc680_auto_init_multi_out(codec);
	alc680_auto_init_hp_out(codec);
	alc680_auto_init_analog_input(codec);
	alc_auto_init_digital(codec);
	if (spec->unsol_event)
		alc_inithook(codec);
}

/*
 * configuration and preset
 */
static const char * const alc680_models[ALC680_MODEL_LAST] = {
	[ALC680_BASE]		= "base",
	[ALC680_AUTO]		= "auto",
};

static const struct snd_pci_quirk alc680_cfg_tbl[] = {
	SND_PCI_QUIRK(0x1043, 0x12f3, "ASUS NX90", ALC680_BASE),
	{}
};

static const struct alc_config_preset alc680_presets[] = {
	[ALC680_BASE] = {
		.mixers = { alc680_base_mixer },
		.cap_mixer =  alc680_master_capture_mixer,
		.init_verbs = { alc680_init_verbs },
		.num_dacs = ARRAY_SIZE(alc680_dac_nids),
		.dac_nids = alc680_dac_nids,
		.dig_out_nid = ALC680_DIGOUT_NID,
		.num_channel_mode = ARRAY_SIZE(alc680_modes),
		.channel_mode = alc680_modes,
		.unsol_event = alc680_unsol_event,
		.setup = alc680_base_setup,
		.init_hook = alc680_inithook,

	},
};

static int patch_alc680(struct hda_codec *codec)
{
	struct alc_spec *spec;
	int board_config;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (spec == NULL)
		return -ENOMEM;

	codec->spec = spec;

	board_config = snd_hda_check_board_config(codec, ALC680_MODEL_LAST,
						  alc680_models,
						  alc680_cfg_tbl);

	if (board_config < 0 || board_config >= ALC680_MODEL_LAST) {
		printk(KERN_INFO "hda_codec: %s: BIOS auto-probing.\n",
		       codec->chip_name);
		board_config = ALC680_AUTO;
	}

	if (board_config == ALC680_AUTO) {
		/* automatic parse from the BIOS config */
		err = alc680_parse_auto_config(codec);
		if (err < 0) {
			alc_free(codec);
			return err;
		} else if (!err) {
			printk(KERN_INFO
			       "hda_codec: Cannot set up configuration "
			       "from BIOS.  Using base mode...\n");
			board_config = ALC680_BASE;
		}
	}

	if (board_config != ALC680_AUTO)
		setup_preset(codec, &alc680_presets[board_config]);

	spec->stream_analog_playback = &alc680_pcm_analog_playback;
	spec->stream_analog_capture = &alc680_pcm_analog_auto_capture;
	spec->stream_digital_playback = &alc680_pcm_digital_playback;
	spec->stream_digital_capture = &alc680_pcm_digital_capture;

	if (!spec->adc_nids) {
		spec->adc_nids = alc680_adc_nids;
		spec->num_adc_nids = ARRAY_SIZE(alc680_adc_nids);
	}

	if (!spec->cap_mixer)
		set_capture_mixer(codec);

	spec->vmaster_nid = 0x02;

	codec->patch_ops = alc_patch_ops;
	if (board_config == ALC680_AUTO)
		spec->init_hook = alc680_auto_init;

	return 0;
}

/*
 * patch entries
 */
static const struct hda_codec_preset snd_hda_preset_realtek[] = {
	{ .id = 0x10ec0221, .name = "ALC221", .patch = patch_alc269 },
	{ .id = 0x10ec0260, .name = "ALC260", .patch = patch_alc260 },
	{ .id = 0x10ec0262, .name = "ALC262", .patch = patch_alc262 },
	{ .id = 0x10ec0267, .name = "ALC267", .patch = patch_alc268 },
	{ .id = 0x10ec0268, .name = "ALC268", .patch = patch_alc268 },
	{ .id = 0x10ec0269, .name = "ALC269", .patch = patch_alc269 },
	{ .id = 0x10ec0270, .name = "ALC270", .patch = patch_alc269 },
	{ .id = 0x10ec0272, .name = "ALC272", .patch = patch_alc662 },
	{ .id = 0x10ec0275, .name = "ALC275", .patch = patch_alc269 },
	{ .id = 0x10ec0276, .name = "ALC276", .patch = patch_alc269 },
	{ .id = 0x10ec0861, .rev = 0x100340, .name = "ALC660",
	  .patch = patch_alc861 },
	{ .id = 0x10ec0660, .name = "ALC660-VD", .patch = patch_alc861vd },
	{ .id = 0x10ec0861, .name = "ALC861", .patch = patch_alc861 },
	{ .id = 0x10ec0862, .name = "ALC861-VD", .patch = patch_alc861vd },
	{ .id = 0x10ec0662, .rev = 0x100002, .name = "ALC662 rev2",
	  .patch = patch_alc882 },
	{ .id = 0x10ec0662, .rev = 0x100101, .name = "ALC662 rev1",
	  .patch = patch_alc662 },
	{ .id = 0x10ec0662, .rev = 0x100300, .name = "ALC662 rev3",
	  .patch = patch_alc662 },
	{ .id = 0x10ec0663, .name = "ALC663", .patch = patch_alc662 },
	{ .id = 0x10ec0665, .name = "ALC665", .patch = patch_alc662 },
	{ .id = 0x10ec0670, .name = "ALC670", .patch = patch_alc662 },
	{ .id = 0x10ec0680, .name = "ALC680", .patch = patch_alc680 },
	{ .id = 0x10ec0880, .name = "ALC880", .patch = patch_alc880 },
	{ .id = 0x10ec0882, .name = "ALC882", .patch = patch_alc882 },
	{ .id = 0x10ec0883, .name = "ALC883", .patch = patch_alc882 },
	{ .id = 0x10ec0885, .rev = 0x100101, .name = "ALC889A",
	  .patch = patch_alc882 },
	{ .id = 0x10ec0885, .rev = 0x100103, .name = "ALC889A",
	  .patch = patch_alc882 },
	{ .id = 0x10ec0885, .name = "ALC885", .patch = patch_alc882 },
	{ .id = 0x10ec0887, .name = "ALC887", .patch = patch_alc888 },
	{ .id = 0x10ec0888, .rev = 0x100101, .name = "ALC1200",
	  .patch = patch_alc882 },
	{ .id = 0x10ec0888, .name = "ALC888", .patch = patch_alc888 },
	{ .id = 0x10ec0889, .name = "ALC889", .patch = patch_alc882 },
	{ .id = 0x10ec0892, .name = "ALC892", .patch = patch_alc662 },
	{ .id = 0x10ec0899, .name = "ALC899", .patch = patch_alc899 },
	{} /* terminator */
};

MODULE_ALIAS("snd-hda-codec-id:10ec*");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek HD-audio codec");

static struct hda_codec_preset_list realtek_list = {
	.preset = snd_hda_preset_realtek,
	.owner = THIS_MODULE,
};

static int __init patch_realtek_init(void)
{
	return snd_hda_add_codec_preset(&realtek_list);
}

static void __exit patch_realtek_exit(void)
{
	snd_hda_delete_codec_preset(&realtek_list);
}

module_init(patch_realtek_init)
module_exit(patch_realtek_exit)
