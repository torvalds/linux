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
#include <sound/jack.h>
#include <sound/tlv.h>
#include "hda_codec.h"
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_beep.h"
#include "hda_jack.h"
#include "hda_generic.h"

enum {
	STAC_VREF_EVENT	= 8,
	STAC_PWR_EVENT,
};

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
	STAC_9200_M4,
	STAC_9200_M4_2,
	STAC_9200_PANASONIC,
	STAC_9200_EAPD_INIT,
	STAC_9200_MODELS
};

enum {
	STAC_9205_REF,
	STAC_9205_DELL_M42,
	STAC_9205_DELL_M43,
	STAC_9205_DELL_M44,
	STAC_9205_EAPD,
	STAC_9205_MODELS
};

enum {
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
	STAC_92HD83XXX_REF,
	STAC_92HD83XXX_PWR_REF,
	STAC_DELL_S14,
	STAC_DELL_VOSTRO_3500,
	STAC_92HD83XXX_HP_cNB11_INTQUAD,
	STAC_HP_DV7_4000,
	STAC_HP_ZEPHYR,
	STAC_92HD83XXX_HP_LED,
	STAC_92HD83XXX_HP_INV_LED,
	STAC_92HD83XXX_HP_MIC_LED,
	STAC_92HD83XXX_HEADSET_JACK,
	STAC_92HD83XXX_HP,
	STAC_HP_ENVY_BASS,
	STAC_92HD83XXX_MODELS
};

enum {
	STAC_92HD71BXX_REF,
	STAC_DELL_M4_1,
	STAC_DELL_M4_2,
	STAC_DELL_M4_3,
	STAC_HP_M4,
	STAC_HP_DV4,
	STAC_HP_DV5,
	STAC_HP_HDX,
	STAC_92HD71BXX_HP,
	STAC_92HD71BXX_NO_DMIC,
	STAC_92HD71BXX_NO_SMUX,
	STAC_92HD71BXX_MODELS
};

enum {
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
	STAC_D945_REF,
	STAC_D945GTP3,
	STAC_D945GTP5,
	STAC_INTEL_MAC_V1,
	STAC_INTEL_MAC_V2,
	STAC_INTEL_MAC_V3,
	STAC_INTEL_MAC_V4,
	STAC_INTEL_MAC_V5,
	STAC_INTEL_MAC_AUTO,
	STAC_ECS_202,
	STAC_922X_DELL_D81,
	STAC_922X_DELL_D82,
	STAC_922X_DELL_M81,
	STAC_922X_DELL_M82,
	STAC_922X_INTEL_MAC_GPIO,
	STAC_922X_MODELS
};

enum {
	STAC_D965_REF_NO_JD, /* no jack-detection */
	STAC_D965_REF,
	STAC_D965_3ST,
	STAC_D965_5ST,
	STAC_D965_5ST_NO_FP,
	STAC_D965_VERBS,
	STAC_DELL_3ST,
	STAC_DELL_BIOS,
	STAC_DELL_BIOS_AMIC,
	STAC_DELL_BIOS_SPDIF,
	STAC_927X_DELL_DMIC,
	STAC_927X_VOLKNOB,
	STAC_927X_MODELS
};

enum {
	STAC_9872_VAIO,
	STAC_9872_MODELS
};

struct sigmatel_spec {
	struct hda_gen_spec gen;

	unsigned int eapd_switch: 1;
	unsigned int linear_tone_beep:1;
	unsigned int headset_jack:1; /* 4-pin headset jack (hp + mono mic) */
	unsigned int volknob_init:1; /* special volume-knob initialization */
	unsigned int powerdown_adcs:1;
	unsigned int have_spdif_mux:1;

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
	int default_polarity;

	unsigned int mic_mute_led_gpio; /* capture mute LED GPIO */
	bool mic_mute_led_on; /* current mic mute state */

	/* stream */
	unsigned int stream_delay;

	/* analog loopback */
	const struct snd_kcontrol_new *aloopback_ctl;
	unsigned int aloopback;
	unsigned char aloopback_mask;
	unsigned char aloopback_shift;

	/* power management */
	unsigned int power_map_bits;
	unsigned int num_pwrs;
	const hda_nid_t *pwr_nids;
	unsigned int active_adcs;

	/* beep widgets */
	hda_nid_t anabeep_nid;

	/* SPDIF-out mux */
	const char * const *spdif_labels;
	struct hda_input_mux spdif_mux;
	unsigned int cur_smux[2];
};

#define AC_VERB_IDT_SET_POWER_MAP	0x7ec
#define AC_VERB_IDT_GET_POWER_MAP	0xfec

static const hda_nid_t stac92hd73xx_pwr_nids[8] = {
	0x0a, 0x0b, 0x0c, 0xd, 0x0e,
	0x0f, 0x10, 0x11
};

static const hda_nid_t stac92hd83xxx_pwr_nids[7] = {
	0x0a, 0x0b, 0x0c, 0xd, 0x0e,
	0x0f, 0x10
};

static const hda_nid_t stac92hd71bxx_pwr_nids[3] = {
	0x0a, 0x0d, 0x0f
};


/*
 * PCM hooks
 */
static void stac_playback_pcm_hook(struct hda_pcm_stream *hinfo,
				   struct hda_codec *codec,
				   struct snd_pcm_substream *substream,
				   int action)
{
	struct sigmatel_spec *spec = codec->spec;
	if (action == HDA_GEN_PCM_ACT_OPEN && spec->stream_delay)
		msleep(spec->stream_delay);
}

static void stac_capture_pcm_hook(struct hda_pcm_stream *hinfo,
				  struct hda_codec *codec,
				  struct snd_pcm_substream *substream,
				  int action)
{
	struct sigmatel_spec *spec = codec->spec;
	int i, idx = 0;

	if (!spec->powerdown_adcs)
		return;

	for (i = 0; i < spec->gen.num_all_adcs; i++) {
		if (spec->gen.all_adcs[i] == hinfo->nid) {
			idx = i;
			break;
		}
	}

	switch (action) {
	case HDA_GEN_PCM_ACT_OPEN:
		msleep(40);
		snd_hda_codec_write(codec, hinfo->nid, 0,
				    AC_VERB_SET_POWER_STATE, AC_PWRST_D0);
		spec->active_adcs |= (1 << idx);
		break;
	case HDA_GEN_PCM_ACT_CLOSE:
		snd_hda_codec_write(codec, hinfo->nid, 0,
				    AC_VERB_SET_POWER_STATE, AC_PWRST_D3);
		spec->active_adcs &= ~(1 << idx);
		break;
	}
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

/* hook for controlling mic-mute LED GPIO */
static void stac_capture_led_hook(struct hda_codec *codec,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct sigmatel_spec *spec = codec->spec;
	bool mute;

	if (!ucontrol)
		return;

	mute = !(ucontrol->value.integer.value[0] ||
		 ucontrol->value.integer.value[1]);
	if (spec->mic_mute_led_on != mute) {
		spec->mic_mute_led_on = mute;
		if (mute)
			spec->gpio_data |= spec->mic_mute_led_gpio;
		else
			spec->gpio_data &= ~spec->mic_mute_led_gpio;
		stac_gpio_set(codec, spec->gpio_mask,
			      spec->gpio_dir, spec->gpio_data);
	}
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

	error = snd_hda_set_pin_ctl_cache(codec, nid, pinctl);
	if (error < 0)
		return error;

	return 1;
}

/* update mute-LED accoring to the master switch */
static void stac_update_led_status(struct hda_codec *codec, int enabled)
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

/* vmaster hook to update mute LED */
static void stac_vmaster_hook(void *private_data, int val)
{
	stac_update_led_status(private_data, val);
}

/* automute hook to handle GPIO mute and EAPD updates */
static void stac_update_outputs(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	if (spec->gpio_mute)
		spec->gen.master_mute =
			!(snd_hda_codec_read(codec, codec->afg, 0,
				AC_VERB_GET_GPIO_DATA, 0) & spec->gpio_mute);

	snd_hda_gen_update_outputs(codec);

	if (spec->eapd_mask && spec->eapd_switch) {
		unsigned int val = spec->gpio_data;
		if (spec->gen.speaker_muted)
			val &= ~spec->eapd_mask;
		else
			val |= spec->eapd_mask;
		if (spec->gpio_data != val) {
			spec->gpio_data = val;
			stac_gpio_set(codec, spec->gpio_mask, spec->gpio_dir,
				      val);
		}
	}
}

static void stac_toggle_power_map(struct hda_codec *codec, hda_nid_t nid,
				  bool enable, bool do_write)
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

	val = spec->power_map_bits;
	if (enable)
		val &= ~idx;
	else
		val |= idx;

	/* power down unused output ports */
	if (val != spec->power_map_bits) {
		spec->power_map_bits = val;
		if (do_write)
			snd_hda_codec_write(codec, codec->afg, 0,
					    AC_VERB_IDT_SET_POWER_MAP, val);
	}
}

/* update power bit per jack plug/unplug */
static void jack_update_power(struct hda_codec *codec,
			      struct hda_jack_tbl *jack)
{
	struct sigmatel_spec *spec = codec->spec;
	int i;

	if (!spec->num_pwrs)
		return;

	if (jack && jack->nid) {
		stac_toggle_power_map(codec, jack->nid,
				      snd_hda_jack_detect(codec, jack->nid),
				      true);
		return;
	}

	/* update all jacks */
	for (i = 0; i < spec->num_pwrs; i++) {
		hda_nid_t nid = spec->pwr_nids[i];
		jack = snd_hda_jack_tbl_get(codec, nid);
		if (!jack || !jack->action)
			continue;
		if (jack->action == STAC_PWR_EVENT ||
		    jack->action <= HDA_GEN_LAST_EVENT)
			stac_toggle_power_map(codec, nid,
					      snd_hda_jack_detect(codec, nid),
					      false);
	}

	snd_hda_codec_write(codec, codec->afg, 0, AC_VERB_IDT_SET_POWER_MAP,
			    spec->power_map_bits);
}

static void stac_hp_automute(struct hda_codec *codec,
				 struct hda_jack_tbl *jack)
{
	snd_hda_gen_hp_automute(codec, jack);
	jack_update_power(codec, jack);
}

static void stac_line_automute(struct hda_codec *codec,
				   struct hda_jack_tbl *jack)
{
	snd_hda_gen_line_automute(codec, jack);
	jack_update_power(codec, jack);
}

static void stac_mic_autoswitch(struct hda_codec *codec,
				struct hda_jack_tbl *jack)
{
	snd_hda_gen_mic_autoswitch(codec, jack);
	jack_update_power(codec, jack);
}

static void stac_vref_event(struct hda_codec *codec, struct hda_jack_tbl *event)
{
	unsigned int data;

	data = snd_hda_codec_read(codec, codec->afg, 0,
				  AC_VERB_GET_GPIO_DATA, 0);
	/* toggle VREF state based on GPIOx status */
	snd_hda_codec_write(codec, codec->afg, 0, 0x7e0,
			    !!(data & (1 << event->private_data)));
}

/* initialize the power map and enable the power event to jacks that
 * haven't been assigned to automute
 */
static void stac_init_power_map(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int i;

	for (i = 0; i < spec->num_pwrs; i++)  {
		hda_nid_t nid = spec->pwr_nids[i];
		unsigned int def_conf = snd_hda_codec_get_pincfg(codec, nid);
		def_conf = get_defcfg_connect(def_conf);
		if (snd_hda_jack_tbl_get(codec, nid))
			continue;
		if (def_conf == AC_JACK_PORT_COMPLEX &&
		    !(spec->vref_mute_led_nid == nid ||
		      is_jack_detectable(codec, nid))) {
			snd_hda_jack_detect_enable_callback(codec, nid,
							    STAC_PWR_EVENT,
							    jack_update_power);
		} else {
			if (def_conf == AC_JACK_PORT_NONE)
				stac_toggle_power_map(codec, nid, false, false);
			else
				stac_toggle_power_map(codec, nid, true, false);
		}
	}
}

/*
 */

static inline bool get_int_hint(struct hda_codec *codec, const char *key,
				int *valp)
{
	return !snd_hda_get_int_hint(codec, key, valp);
}

/* override some hints from the hwdep entry */
static void stac_store_hints(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int val;

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
}

/*
 * loopback controls
 */

#define stac_aloopback_info snd_ctl_boolean_mono_info

static int stac_aloopback_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	struct sigmatel_spec *spec = codec->spec;

	ucontrol->value.integer.value[0] = !!(spec->aloopback &
					      (spec->aloopback_mask << idx));
	return 0;
}

static int stac_aloopback_put(struct snd_kcontrol *kcontrol,
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

#define STAC_ANALOG_LOOPBACK(verb_read, verb_write, cnt) \
	{ \
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
		.name  = "Analog Loopback", \
		.count = cnt, \
		.info  = stac_aloopback_info, \
		.get   = stac_aloopback_get, \
		.put   = stac_aloopback_put, \
		.private_value = verb_read | (verb_write << 16), \
	}

/*
 * Mute LED handling on HP laptops
 */

/* check whether it's a HP laptop with a docking port */
static bool hp_bnb2011_with_dock(struct hda_codec *codec)
{
	if (codec->vendor_id != 0x111d7605 &&
	    codec->vendor_id != 0x111d76d1)
		return false;

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

		return true;
	}
	return false;
}

static bool hp_blike_system(u32 subsystem_id)
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
		return true;
	}
	return false;
}

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

	if (get_int_hint(codec, "gpio_led", &spec->gpio_led)) {
		get_int_hint(codec, "gpio_led_polarity",
			     &spec->gpio_led_polarity);
		return 1;
	}

	while ((dev = dmi_find_device(DMI_DEV_TYPE_OEM_STRING, NULL, dev))) {
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
			if (default_polarity >= 0)
				spec->gpio_led_polarity = default_polarity;
			else
				spec->gpio_led_polarity = 1;
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
	return 0;
}

/* check whether a built-in speaker is included in parsed pins */
static bool has_builtin_speaker(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	hda_nid_t *nid_pin;
	int nids, i;

	if (spec->gen.autocfg.line_out_type == AUTO_PIN_SPEAKER_OUT) {
		nid_pin = spec->gen.autocfg.line_out_pins;
		nids = spec->gen.autocfg.line_outs;
	} else {
		nid_pin = spec->gen.autocfg.speaker_pins;
		nids = spec->gen.autocfg.speaker_outs;
	}

	for (i = 0; i < nids; i++) {
		unsigned int def_conf = snd_hda_codec_get_pincfg(codec, nid_pin[i]);
		if (snd_hda_get_input_pin_attr(def_conf) == INPUT_PIN_ATTR_INT)
			return true;
	}
	return false;
}

/*
 * PC beep controls
 */

/* create PC beep volume controls */
static int stac_auto_create_beep_ctls(struct hda_codec *codec,
						hda_nid_t nid)
{
	struct sigmatel_spec *spec = codec->spec;
	u32 caps = query_amp_caps(codec, nid, HDA_OUTPUT);
	struct snd_kcontrol_new *knew;
	static struct snd_kcontrol_new abeep_mute_ctl =
		HDA_CODEC_MUTE(NULL, 0, 0, 0);
	static struct snd_kcontrol_new dbeep_mute_ctl =
		HDA_CODEC_MUTE_BEEP(NULL, 0, 0, 0);
	static struct snd_kcontrol_new beep_vol_ctl =
		HDA_CODEC_VOLUME(NULL, 0, 0, 0);

	/* check for mute support for the the amp */
	if ((caps & AC_AMPCAP_MUTE) >> AC_AMPCAP_MUTE_SHIFT) {
		const struct snd_kcontrol_new *temp;
		if (spec->anabeep_nid == nid)
			temp = &abeep_mute_ctl;
		else
			temp = &dbeep_mute_ctl;
		knew = snd_hda_gen_add_kctl(&spec->gen,
					    "Beep Playback Switch", temp);
		if (!knew)
			return -ENOMEM;
		knew->private_value =
			HDA_COMPOSE_AMP_VAL(nid, 1, 0, HDA_OUTPUT);
	}

	/* check to see if there is volume support for the amp */
	if ((caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT) {
		knew = snd_hda_gen_add_kctl(&spec->gen,
					    "Beep Playback Volume",
					    &beep_vol_ctl);
		if (!knew)
			return -ENOMEM;
		knew->private_value =
			HDA_COMPOSE_AMP_VAL(nid, 1, 0, HDA_OUTPUT);
	}
	return 0;
}

#ifdef CONFIG_SND_HDA_INPUT_BEEP
#define stac_dig_beep_switch_info snd_ctl_boolean_mono_info

static int stac_dig_beep_switch_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = codec->beep->enabled;
	return 0;
}

static int stac_dig_beep_switch_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	return snd_hda_enable_beep_device(codec, ucontrol->value.integer.value[0]);
}

static const struct snd_kcontrol_new stac_dig_beep_ctrl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Beep Playback Switch",
	.info = stac_dig_beep_switch_info,
	.get = stac_dig_beep_switch_get,
	.put = stac_dig_beep_switch_put,
};

static int stac_beep_switch_ctl(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	if (!snd_hda_gen_add_kctl(&spec->gen, NULL, &stac_dig_beep_ctrl))
		return -ENOMEM;
	return 0;
}
#endif

/*
 * SPDIF-out mux controls
 */

static int stac_smux_enum_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	return snd_hda_input_mux_info(&spec->spdif_mux, uinfo);
}

static int stac_smux_enum_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	unsigned int smux_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	ucontrol->value.enumerated.item[0] = spec->cur_smux[smux_idx];
	return 0;
}

static int stac_smux_enum_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct sigmatel_spec *spec = codec->spec;
	unsigned int smux_idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	return snd_hda_input_mux_put(codec, &spec->spdif_mux, ucontrol,
				     spec->gen.autocfg.dig_out_pins[smux_idx],
				     &spec->cur_smux[smux_idx]);
}

static struct snd_kcontrol_new stac_smux_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "IEC958 Playback Source",
	/* count set later */
	.info = stac_smux_enum_info,
	.get = stac_smux_enum_get,
	.put = stac_smux_enum_put,
};

static const char * const stac_spdif_labels[] = {
	"Digital Playback", "Analog Mux 1", "Analog Mux 2", NULL
};

static int stac_create_spdif_mux_ctls(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->gen.autocfg;
	const char * const *labels = spec->spdif_labels;
	struct snd_kcontrol_new *kctl;
	int i, num_cons;

	if (cfg->dig_outs < 1)
		return 0;

	num_cons = snd_hda_get_num_conns(codec, cfg->dig_out_pins[0]);
	if (num_cons <= 1)
		return 0;

	if (!labels)
		labels = stac_spdif_labels;
	for (i = 0; i < num_cons; i++) {
		if (snd_BUG_ON(!labels[i]))
			return -EINVAL;
		snd_hda_add_imux_item(&spec->spdif_mux, labels[i], i, NULL);
	}

	kctl = snd_hda_gen_add_kctl(&spec->gen, NULL, &stac_smux_mixer);
	if (!kctl)
		return -ENOMEM;
	kctl->count = cfg->dig_outs;

	return 0;
}

/*
 */

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

static const struct snd_kcontrol_new stac92hd73xx_6ch_loopback =
	STAC_ANALOG_LOOPBACK(0xFA0, 0x7A1, 3);

static const struct snd_kcontrol_new stac92hd73xx_8ch_loopback =
	STAC_ANALOG_LOOPBACK(0xFA0, 0x7A1, 4);

static const struct snd_kcontrol_new stac92hd73xx_10ch_loopback =
	STAC_ANALOG_LOOPBACK(0xFA0, 0x7A1, 5);

static const struct snd_kcontrol_new stac92hd71bxx_loopback =
	STAC_ANALOG_LOOPBACK(0xFA0, 0x7A0, 2);

static const struct snd_kcontrol_new stac9205_loopback =
	STAC_ANALOG_LOOPBACK(0xFE0, 0x7E0, 1);

static const struct snd_kcontrol_new stac927x_loopback =
	STAC_ANALOG_LOOPBACK(0xFEB, 0x7EB, 1);

static const struct hda_pintbl ref9200_pin_configs[] = {
	{ 0x08, 0x01c47010 },
	{ 0x09, 0x01447010 },
	{ 0x0d, 0x0221401f },
	{ 0x0e, 0x01114010 },
	{ 0x0f, 0x02a19020 },
	{ 0x10, 0x01a19021 },
	{ 0x11, 0x90100140 },
	{ 0x12, 0x01813122 },
	{}
};

static const struct hda_pintbl gateway9200_m4_pin_configs[] = {
	{ 0x08, 0x400000fe },
	{ 0x09, 0x404500f4 },
	{ 0x0d, 0x400100f0 },
	{ 0x0e, 0x90110010 },
	{ 0x0f, 0x400100f1 },
	{ 0x10, 0x02a1902e },
	{ 0x11, 0x500000f2 },
	{ 0x12, 0x500000f3 },
	{}
};

static const struct hda_pintbl gateway9200_m4_2_pin_configs[] = {
	{ 0x08, 0x400000fe },
	{ 0x09, 0x404500f4 },
	{ 0x0d, 0x400100f0 },
	{ 0x0e, 0x90110010 },
	{ 0x0f, 0x400100f1 },
	{ 0x10, 0x02a1902e },
	{ 0x11, 0x500000f2 },
	{ 0x12, 0x500000f3 },
	{}
};

/*
    STAC 9200 pin configs for
    102801A8
    102801DE
    102801E8
*/
static const struct hda_pintbl dell9200_d21_pin_configs[] = {
	{ 0x08, 0x400001f0 },
	{ 0x09, 0x400001f1 },
	{ 0x0d, 0x02214030 },
	{ 0x0e, 0x01014010 },
	{ 0x0f, 0x02a19020 },
	{ 0x10, 0x01a19021 },
	{ 0x11, 0x90100140 },
	{ 0x12, 0x01813122 },
	{}
};

/*
    STAC 9200 pin configs for
    102801C0
    102801C1
*/
static const struct hda_pintbl dell9200_d22_pin_configs[] = {
	{ 0x08, 0x400001f0 },
	{ 0x09, 0x400001f1 },
	{ 0x0d, 0x0221401f },
	{ 0x0e, 0x01014010 },
	{ 0x0f, 0x01813020 },
	{ 0x10, 0x02a19021 },
	{ 0x11, 0x90100140 },
	{ 0x12, 0x400001f2 },
	{}
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
static const struct hda_pintbl dell9200_d23_pin_configs[] = {
	{ 0x08, 0x400001f0 },
	{ 0x09, 0x400001f1 },
	{ 0x0d, 0x0221401f },
	{ 0x0e, 0x01014010 },
	{ 0x0f, 0x01813020 },
	{ 0x10, 0x01a19021 },
	{ 0x11, 0x90100140 },
	{ 0x12, 0x400001f2 },
	{}
};


/* 
    STAC 9200-32 pin configs for
    102801B5 (Dell Inspiron 630m)
    102801D8 (Dell Inspiron 640m)
*/
static const struct hda_pintbl dell9200_m21_pin_configs[] = {
	{ 0x08, 0x40c003fa },
	{ 0x09, 0x03441340 },
	{ 0x0d, 0x0321121f },
	{ 0x0e, 0x90170310 },
	{ 0x0f, 0x408003fb },
	{ 0x10, 0x03a11020 },
	{ 0x11, 0x401003fc },
	{ 0x12, 0x403003fd },
	{}
};

/* 
    STAC 9200-32 pin configs for
    102801C2 (Dell Latitude D620)
    102801C8 
    102801CC (Dell Latitude D820)
    102801D4 
    102801D6 
*/
static const struct hda_pintbl dell9200_m22_pin_configs[] = {
	{ 0x08, 0x40c003fa },
	{ 0x09, 0x0144131f },
	{ 0x0d, 0x0321121f },
	{ 0x0e, 0x90170310 },
	{ 0x0f, 0x90a70321 },
	{ 0x10, 0x03a11020 },
	{ 0x11, 0x401003fb },
	{ 0x12, 0x40f000fc },
	{}
};

/* 
    STAC 9200-32 pin configs for
    102801CE (Dell XPS M1710)
    102801CF (Dell Precision M90)
*/
static const struct hda_pintbl dell9200_m23_pin_configs[] = {
	{ 0x08, 0x40c003fa },
	{ 0x09, 0x01441340 },
	{ 0x0d, 0x0421421f },
	{ 0x0e, 0x90170310 },
	{ 0x0f, 0x408003fb },
	{ 0x10, 0x04a1102e },
	{ 0x11, 0x90170311 },
	{ 0x12, 0x403003fc },
	{}
};

/*
    STAC 9200-32 pin configs for 
    102801C9
    102801CA
    102801CB (Dell Latitude 120L)
    102801D3
*/
static const struct hda_pintbl dell9200_m24_pin_configs[] = {
	{ 0x08, 0x40c003fa },
	{ 0x09, 0x404003fb },
	{ 0x0d, 0x0321121f },
	{ 0x0e, 0x90170310 },
	{ 0x0f, 0x408003fc },
	{ 0x10, 0x03a11020 },
	{ 0x11, 0x401003fd },
	{ 0x12, 0x403003fe },
	{}
};

/*
    STAC 9200-32 pin configs for
    102801BD (Dell Inspiron E1505n)
    102801EE
    102801EF
*/
static const struct hda_pintbl dell9200_m25_pin_configs[] = {
	{ 0x08, 0x40c003fa },
	{ 0x09, 0x01441340 },
	{ 0x0d, 0x0421121f },
	{ 0x0e, 0x90170310 },
	{ 0x0f, 0x408003fb },
	{ 0x10, 0x04a11020 },
	{ 0x11, 0x401003fc },
	{ 0x12, 0x403003fd },
	{}
};

/*
    STAC 9200-32 pin configs for
    102801F5 (Dell Inspiron 1501)
    102801F6
*/
static const struct hda_pintbl dell9200_m26_pin_configs[] = {
	{ 0x08, 0x40c003fa },
	{ 0x09, 0x404003fb },
	{ 0x0d, 0x0421121f },
	{ 0x0e, 0x90170310 },
	{ 0x0f, 0x408003fc },
	{ 0x10, 0x04a11020 },
	{ 0x11, 0x401003fd },
	{ 0x12, 0x403003fe },
	{}
};

/*
    STAC 9200-32
    102801CD (Dell Inspiron E1705/9400)
*/
static const struct hda_pintbl dell9200_m27_pin_configs[] = {
	{ 0x08, 0x40c003fa },
	{ 0x09, 0x01441340 },
	{ 0x0d, 0x0421121f },
	{ 0x0e, 0x90170310 },
	{ 0x0f, 0x90170310 },
	{ 0x10, 0x04a11020 },
	{ 0x11, 0x90170310 },
	{ 0x12, 0x40f003fc },
	{}
};

static const struct hda_pintbl oqo9200_pin_configs[] = {
	{ 0x08, 0x40c000f0 },
	{ 0x09, 0x404000f1 },
	{ 0x0d, 0x0221121f },
	{ 0x0e, 0x02211210 },
	{ 0x0f, 0x90170111 },
	{ 0x10, 0x90a70120 },
	{ 0x11, 0x400000f2 },
	{ 0x12, 0x400000f3 },
	{}
};


static void stac9200_fixup_panasonic(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gpio_mask = spec->gpio_dir = 0x09;
		spec->gpio_data = 0x00;
		/* CF-74 has no headphone detection, and the driver should *NOT*
		 * do detection and HP/speaker toggle because the hardware does it.
		 */
		spec->gen.suppress_auto_mute = 1;
	}
}


static const struct hda_fixup stac9200_fixups[] = {
	[STAC_REF] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = ref9200_pin_configs,
	},
	[STAC_9200_OQO] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = oqo9200_pin_configs,
		.chained = true,
		.chain_id = STAC_9200_EAPD_INIT,
	},
	[STAC_9200_DELL_D21] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell9200_d21_pin_configs,
	},
	[STAC_9200_DELL_D22] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell9200_d22_pin_configs,
	},
	[STAC_9200_DELL_D23] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell9200_d23_pin_configs,
	},
	[STAC_9200_DELL_M21] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell9200_m21_pin_configs,
	},
	[STAC_9200_DELL_M22] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell9200_m22_pin_configs,
	},
	[STAC_9200_DELL_M23] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell9200_m23_pin_configs,
	},
	[STAC_9200_DELL_M24] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell9200_m24_pin_configs,
	},
	[STAC_9200_DELL_M25] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell9200_m25_pin_configs,
	},
	[STAC_9200_DELL_M26] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell9200_m26_pin_configs,
	},
	[STAC_9200_DELL_M27] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell9200_m27_pin_configs,
	},
	[STAC_9200_M4] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = gateway9200_m4_pin_configs,
		.chained = true,
		.chain_id = STAC_9200_EAPD_INIT,
	},
	[STAC_9200_M4_2] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = gateway9200_m4_2_pin_configs,
		.chained = true,
		.chain_id = STAC_9200_EAPD_INIT,
	},
	[STAC_9200_PANASONIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac9200_fixup_panasonic,
	},
	[STAC_9200_EAPD_INIT] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = (const struct hda_verb[]) {
			{0x08, AC_VERB_SET_EAPD_BTLENABLE, 0x02},
			{}
		},
	},
};

static const struct hda_model_fixup stac9200_models[] = {
	{ .id = STAC_REF, .name = "ref" },
	{ .id = STAC_9200_OQO, .name = "oqo" },
	{ .id = STAC_9200_DELL_D21, .name = "dell-d21" },
	{ .id = STAC_9200_DELL_D22, .name = "dell-d22" },
	{ .id = STAC_9200_DELL_D23, .name = "dell-d23" },
	{ .id = STAC_9200_DELL_M21, .name = "dell-m21" },
	{ .id = STAC_9200_DELL_M22, .name = "dell-m22" },
	{ .id = STAC_9200_DELL_M23, .name = "dell-m23" },
	{ .id = STAC_9200_DELL_M24, .name = "dell-m24" },
	{ .id = STAC_9200_DELL_M25, .name = "dell-m25" },
	{ .id = STAC_9200_DELL_M26, .name = "dell-m26" },
	{ .id = STAC_9200_DELL_M27, .name = "dell-m27" },
	{ .id = STAC_9200_M4, .name = "gateway-m4" },
	{ .id = STAC_9200_M4_2, .name = "gateway-m4-2" },
	{ .id = STAC_9200_PANASONIC, .name = "panasonic" },
	{}
};

static const struct snd_pci_quirk stac9200_fixup_tbl[] = {
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

static const struct hda_pintbl ref925x_pin_configs[] = {
	{ 0x07, 0x40c003f0 },
	{ 0x08, 0x424503f2 },
	{ 0x0a, 0x01813022 },
	{ 0x0b, 0x02a19021 },
	{ 0x0c, 0x90a70320 },
	{ 0x0d, 0x02214210 },
	{ 0x10, 0x01019020 },
	{ 0x11, 0x9033032e },
	{}
};

static const struct hda_pintbl stac925xM1_pin_configs[] = {
	{ 0x07, 0x40c003f4 },
	{ 0x08, 0x424503f2 },
	{ 0x0a, 0x400000f3 },
	{ 0x0b, 0x02a19020 },
	{ 0x0c, 0x40a000f0 },
	{ 0x0d, 0x90100210 },
	{ 0x10, 0x400003f1 },
	{ 0x11, 0x9033032e },
	{}
};

static const struct hda_pintbl stac925xM1_2_pin_configs[] = {
	{ 0x07, 0x40c003f4 },
	{ 0x08, 0x424503f2 },
	{ 0x0a, 0x400000f3 },
	{ 0x0b, 0x02a19020 },
	{ 0x0c, 0x40a000f0 },
	{ 0x0d, 0x90100210 },
	{ 0x10, 0x400003f1 },
	{ 0x11, 0x9033032e },
	{}
};

static const struct hda_pintbl stac925xM2_pin_configs[] = {
	{ 0x07, 0x40c003f4 },
	{ 0x08, 0x424503f2 },
	{ 0x0a, 0x400000f3 },
	{ 0x0b, 0x02a19020 },
	{ 0x0c, 0x40a000f0 },
	{ 0x0d, 0x90100210 },
	{ 0x10, 0x400003f1 },
	{ 0x11, 0x9033032e },
	{}
};

static const struct hda_pintbl stac925xM2_2_pin_configs[] = {
	{ 0x07, 0x40c003f4 },
	{ 0x08, 0x424503f2 },
	{ 0x0a, 0x400000f3 },
	{ 0x0b, 0x02a19020 },
	{ 0x0c, 0x40a000f0 },
	{ 0x0d, 0x90100210 },
	{ 0x10, 0x400003f1 },
	{ 0x11, 0x9033032e },
	{}
};

static const struct hda_pintbl stac925xM3_pin_configs[] = {
	{ 0x07, 0x40c003f4 },
	{ 0x08, 0x424503f2 },
	{ 0x0a, 0x400000f3 },
	{ 0x0b, 0x02a19020 },
	{ 0x0c, 0x40a000f0 },
	{ 0x0d, 0x90100210 },
	{ 0x10, 0x400003f1 },
	{ 0x11, 0x503303f3 },
	{}
};

static const struct hda_pintbl stac925xM5_pin_configs[] = {
	{ 0x07, 0x40c003f4 },
	{ 0x08, 0x424503f2 },
	{ 0x0a, 0x400000f3 },
	{ 0x0b, 0x02a19020 },
	{ 0x0c, 0x40a000f0 },
	{ 0x0d, 0x90100210 },
	{ 0x10, 0x400003f1 },
	{ 0x11, 0x9033032e },
	{}
};

static const struct hda_pintbl stac925xM6_pin_configs[] = {
	{ 0x07, 0x40c003f4 },
	{ 0x08, 0x424503f2 },
	{ 0x0a, 0x400000f3 },
	{ 0x0b, 0x02a19020 },
	{ 0x0c, 0x40a000f0 },
	{ 0x0d, 0x90100210 },
	{ 0x10, 0x400003f1 },
	{ 0x11, 0x90330320 },
	{}
};

static const struct hda_fixup stac925x_fixups[] = {
	[STAC_REF] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = ref925x_pin_configs,
	},
	[STAC_M1] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = stac925xM1_pin_configs,
	},
	[STAC_M1_2] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = stac925xM1_2_pin_configs,
	},
	[STAC_M2] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = stac925xM2_pin_configs,
	},
	[STAC_M2_2] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = stac925xM2_2_pin_configs,
	},
	[STAC_M3] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = stac925xM3_pin_configs,
	},
	[STAC_M5] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = stac925xM5_pin_configs,
	},
	[STAC_M6] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = stac925xM6_pin_configs,
	},
};

static const struct hda_model_fixup stac925x_models[] = {
	{ .id = STAC_REF, .name = "ref" },
	{ .id = STAC_M1, .name = "m1" },
	{ .id = STAC_M1_2, .name = "m1-2" },
	{ .id = STAC_M2, .name = "m2" },
	{ .id = STAC_M2_2, .name = "m2-2" },
	{ .id = STAC_M3, .name = "m3" },
	{ .id = STAC_M5, .name = "m5" },
	{ .id = STAC_M6, .name = "m6" },
	{}
};

static const struct snd_pci_quirk stac925x_fixup_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668, "DFI LanParty", STAC_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DFI, 0x3101, "DFI LanParty", STAC_REF),
	SND_PCI_QUIRK(0x8384, 0x7632, "Stac9202 Reference Board", STAC_REF),

	/* Default table for unknown ID */
	SND_PCI_QUIRK(0x1002, 0x437b, "Gateway mobile", STAC_M2_2),

	/* gateway machines are checked via codec ssid */
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

static const struct hda_pintbl ref92hd73xx_pin_configs[] = {
	{ 0x0a, 0x02214030 },
	{ 0x0b, 0x02a19040 },
	{ 0x0c, 0x01a19020 },
	{ 0x0d, 0x02214030 },
	{ 0x0e, 0x0181302e },
	{ 0x0f, 0x01014010 },
	{ 0x10, 0x01014020 },
	{ 0x11, 0x01014030 },
	{ 0x12, 0x02319040 },
	{ 0x13, 0x90a000f0 },
	{ 0x14, 0x90a000f0 },
	{ 0x22, 0x01452050 },
	{ 0x23, 0x01452050 },
	{}
};

static const struct hda_pintbl dell_m6_pin_configs[] = {
	{ 0x0a, 0x0321101f },
	{ 0x0b, 0x4f00000f },
	{ 0x0c, 0x4f0000f0 },
	{ 0x0d, 0x90170110 },
	{ 0x0e, 0x03a11020 },
	{ 0x0f, 0x0321101f },
	{ 0x10, 0x4f0000f0 },
	{ 0x11, 0x4f0000f0 },
	{ 0x12, 0x4f0000f0 },
	{ 0x13, 0x90a60160 },
	{ 0x14, 0x4f0000f0 },
	{ 0x22, 0x4f0000f0 },
	{ 0x23, 0x4f0000f0 },
	{}
};

static const struct hda_pintbl alienware_m17x_pin_configs[] = {
	{ 0x0a, 0x0321101f },
	{ 0x0b, 0x0321101f },
	{ 0x0c, 0x03a11020 },
	{ 0x0d, 0x03014020 },
	{ 0x0e, 0x90170110 },
	{ 0x0f, 0x4f0000f0 },
	{ 0x10, 0x4f0000f0 },
	{ 0x11, 0x4f0000f0 },
	{ 0x12, 0x4f0000f0 },
	{ 0x13, 0x90a60160 },
	{ 0x14, 0x4f0000f0 },
	{ 0x22, 0x4f0000f0 },
	{ 0x23, 0x904601b0 },
	{}
};

static const struct hda_pintbl intel_dg45id_pin_configs[] = {
	{ 0x0a, 0x02214230 },
	{ 0x0b, 0x02A19240 },
	{ 0x0c, 0x01013214 },
	{ 0x0d, 0x01014210 },
	{ 0x0e, 0x01A19250 },
	{ 0x0f, 0x01011212 },
	{ 0x10, 0x01016211 },
	{}
};

static void stac92hd73xx_fixup_ref(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	snd_hda_apply_pincfgs(codec, ref92hd73xx_pin_configs);
	spec->gpio_mask = spec->gpio_dir = spec->gpio_data = 0;
}

static void stac92hd73xx_fixup_dell(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	snd_hda_apply_pincfgs(codec, dell_m6_pin_configs);
	spec->eapd_switch = 0;
}

static void stac92hd73xx_fixup_dell_eq(struct hda_codec *codec,
				       const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	stac92hd73xx_fixup_dell(codec);
	snd_hda_add_verbs(codec, dell_eq_core_init);
	spec->volknob_init = 1;
}

/* Analog Mics */
static void stac92hd73xx_fixup_dell_m6_amic(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	stac92hd73xx_fixup_dell(codec);
	snd_hda_codec_set_pincfg(codec, 0x0b, 0x90A70170);
}

/* Digital Mics */
static void stac92hd73xx_fixup_dell_m6_dmic(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	stac92hd73xx_fixup_dell(codec);
	snd_hda_codec_set_pincfg(codec, 0x13, 0x90A60160);
}

/* Both */
static void stac92hd73xx_fixup_dell_m6_both(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	stac92hd73xx_fixup_dell(codec);
	snd_hda_codec_set_pincfg(codec, 0x0b, 0x90A70170);
	snd_hda_codec_set_pincfg(codec, 0x13, 0x90A60160);
}

static void stac92hd73xx_fixup_alienware_m17x(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	snd_hda_apply_pincfgs(codec, alienware_m17x_pin_configs);
	spec->eapd_switch = 0;
}

static void stac92hd73xx_fixup_no_jd(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		codec->no_jack_detect = 1;
}

static const struct hda_fixup stac92hd73xx_fixups[] = {
	[STAC_92HD73XX_REF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd73xx_fixup_ref,
	},
	[STAC_DELL_M6_AMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd73xx_fixup_dell_m6_amic,
	},
	[STAC_DELL_M6_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd73xx_fixup_dell_m6_dmic,
	},
	[STAC_DELL_M6_BOTH] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd73xx_fixup_dell_m6_both,
	},
	[STAC_DELL_EQ]	= {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd73xx_fixup_dell_eq,
	},
	[STAC_ALIENWARE_M17X] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd73xx_fixup_alienware_m17x,
	},
	[STAC_92HD73XX_INTEL] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = intel_dg45id_pin_configs,
	},
	[STAC_92HD73XX_NO_JD] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd73xx_fixup_no_jd,
	}
};

static const struct hda_model_fixup stac92hd73xx_models[] = {
	{ .id = STAC_92HD73XX_NO_JD, .name = "no-jd" },
	{ .id = STAC_92HD73XX_REF, .name = "ref" },
	{ .id = STAC_92HD73XX_INTEL, .name = "intel" },
	{ .id = STAC_DELL_M6_AMIC, .name = "dell-m6-amic" },
	{ .id = STAC_DELL_M6_DMIC, .name = "dell-m6-dmic" },
	{ .id = STAC_DELL_M6_BOTH, .name = "dell-m6" },
	{ .id = STAC_DELL_EQ, .name = "dell-eq" },
	{ .id = STAC_ALIENWARE_M17X, .name = "alienware" },
	{}
};

static const struct snd_pci_quirk stac92hd73xx_fixup_tbl[] = {
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
	/* codec SSID matching */
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x02a1,
		      "Alienware M17x", STAC_ALIENWARE_M17X),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x043a,
		      "Alienware M17x", STAC_ALIENWARE_M17X),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0490,
		      "Alienware M17x R3", STAC_DELL_EQ),
	{} /* terminator */
};

static const struct hda_pintbl ref92hd83xxx_pin_configs[] = {
	{ 0x0a, 0x02214030 },
	{ 0x0b, 0x02211010 },
	{ 0x0c, 0x02a19020 },
	{ 0x0d, 0x02170130 },
	{ 0x0e, 0x01014050 },
	{ 0x0f, 0x01819040 },
	{ 0x10, 0x01014020 },
	{ 0x11, 0x90a3014e },
	{ 0x1f, 0x01451160 },
	{ 0x20, 0x98560170 },
	{}
};

static const struct hda_pintbl dell_s14_pin_configs[] = {
	{ 0x0a, 0x0221403f },
	{ 0x0b, 0x0221101f },
	{ 0x0c, 0x02a19020 },
	{ 0x0d, 0x90170110 },
	{ 0x0e, 0x40f000f0 },
	{ 0x0f, 0x40f000f0 },
	{ 0x10, 0x40f000f0 },
	{ 0x11, 0x90a60160 },
	{ 0x1f, 0x40f000f0 },
	{ 0x20, 0x40f000f0 },
	{}
};

static const struct hda_pintbl dell_vostro_3500_pin_configs[] = {
	{ 0x0a, 0x02a11020 },
	{ 0x0b, 0x0221101f },
	{ 0x0c, 0x400000f0 },
	{ 0x0d, 0x90170110 },
	{ 0x0e, 0x400000f1 },
	{ 0x0f, 0x400000f2 },
	{ 0x10, 0x400000f3 },
	{ 0x11, 0x90a60160 },
	{ 0x1f, 0x400000f4 },
	{ 0x20, 0x400000f5 },
	{}
};

static const struct hda_pintbl hp_dv7_4000_pin_configs[] = {
	{ 0x0a, 0x03a12050 },
	{ 0x0b, 0x0321201f },
	{ 0x0c, 0x40f000f0 },
	{ 0x0d, 0x90170110 },
	{ 0x0e, 0x40f000f0 },
	{ 0x0f, 0x40f000f0 },
	{ 0x10, 0x90170110 },
	{ 0x11, 0xd5a30140 },
	{ 0x1f, 0x40f000f0 },
	{ 0x20, 0x40f000f0 },
	{}
};

static const struct hda_pintbl hp_zephyr_pin_configs[] = {
	{ 0x0a, 0x01813050 },
	{ 0x0b, 0x0421201f },
	{ 0x0c, 0x04a1205e },
	{ 0x0d, 0x96130310 },
	{ 0x0e, 0x96130310 },
	{ 0x0f, 0x0101401f },
	{ 0x10, 0x1111611f },
	{ 0x11, 0xd5a30130 },
	{}
};

static const struct hda_pintbl hp_cNB11_intquad_pin_configs[] = {
	{ 0x0a, 0x40f000f0 },
	{ 0x0b, 0x0221101f },
	{ 0x0c, 0x02a11020 },
	{ 0x0d, 0x92170110 },
	{ 0x0e, 0x40f000f0 },
	{ 0x0f, 0x92170110 },
	{ 0x10, 0x40f000f0 },
	{ 0x11, 0xd5a30130 },
	{ 0x1f, 0x40f000f0 },
	{ 0x20, 0x40f000f0 },
	{}
};

static void stac92hd83xxx_fixup_hp(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	if (hp_bnb2011_with_dock(codec)) {
		snd_hda_codec_set_pincfg(codec, 0xa, 0x2101201f);
		snd_hda_codec_set_pincfg(codec, 0xf, 0x2181205e);
	}

	if (find_mute_led_cfg(codec, spec->default_polarity))
		snd_printd("mute LED gpio %d polarity %d\n",
				spec->gpio_led,
				spec->gpio_led_polarity);
}

static void stac92hd83xxx_fixup_hp_zephyr(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	snd_hda_apply_pincfgs(codec, hp_zephyr_pin_configs);
	snd_hda_add_verbs(codec, stac92hd83xxx_hp_zephyr_init);
}

static void stac92hd83xxx_fixup_hp_led(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->default_polarity = 0;
}

static void stac92hd83xxx_fixup_hp_inv_led(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->default_polarity = 1;
}

static void stac92hd83xxx_fixup_hp_mic_led(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->mic_mute_led_gpio = 0x08; /* GPIO3 */
}

static void stac92hd83xxx_fixup_headset_jack(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->headset_jack = 1;
}

static const struct hda_fixup stac92hd83xxx_fixups[] = {
	[STAC_92HD83XXX_REF] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = ref92hd83xxx_pin_configs,
	},
	[STAC_92HD83XXX_PWR_REF] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = ref92hd83xxx_pin_configs,
	},
	[STAC_DELL_S14] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell_s14_pin_configs,
	},
	[STAC_DELL_VOSTRO_3500] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell_vostro_3500_pin_configs,
	},
	[STAC_92HD83XXX_HP_cNB11_INTQUAD] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = hp_cNB11_intquad_pin_configs,
		.chained = true,
		.chain_id = STAC_92HD83XXX_HP,
	},
	[STAC_92HD83XXX_HP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd83xxx_fixup_hp,
	},
	[STAC_HP_DV7_4000] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = hp_dv7_4000_pin_configs,
		.chained = true,
		.chain_id = STAC_92HD83XXX_HP,
	},
	[STAC_HP_ZEPHYR] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd83xxx_fixup_hp_zephyr,
		.chained = true,
		.chain_id = STAC_92HD83XXX_HP,
	},
	[STAC_92HD83XXX_HP_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd83xxx_fixup_hp_led,
		.chained = true,
		.chain_id = STAC_92HD83XXX_HP,
	},
	[STAC_92HD83XXX_HP_INV_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd83xxx_fixup_hp_inv_led,
		.chained = true,
		.chain_id = STAC_92HD83XXX_HP,
	},
	[STAC_92HD83XXX_HP_MIC_LED] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd83xxx_fixup_hp_mic_led,
		.chained = true,
		.chain_id = STAC_92HD83XXX_HP,
	},
	[STAC_92HD83XXX_HEADSET_JACK] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd83xxx_fixup_headset_jack,
	},
	[STAC_HP_ENVY_BASS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			{ 0x0f, 0x90170111 },
			{}
		},
	},
};

static const struct hda_model_fixup stac92hd83xxx_models[] = {
	{ .id = STAC_92HD83XXX_REF, .name = "ref" },
	{ .id = STAC_92HD83XXX_PWR_REF, .name = "mic-ref" },
	{ .id = STAC_DELL_S14, .name = "dell-s14" },
	{ .id = STAC_DELL_VOSTRO_3500, .name = "dell-vostro-3500" },
	{ .id = STAC_92HD83XXX_HP_cNB11_INTQUAD, .name = "hp_cNB11_intquad" },
	{ .id = STAC_HP_DV7_4000, .name = "hp-dv7-4000" },
	{ .id = STAC_HP_ZEPHYR, .name = "hp-zephyr" },
	{ .id = STAC_92HD83XXX_HP_LED, .name = "hp-led" },
	{ .id = STAC_92HD83XXX_HP_INV_LED, .name = "hp-inv-led" },
	{ .id = STAC_92HD83XXX_HP_MIC_LED, .name = "hp-mic-led" },
	{ .id = STAC_92HD83XXX_HEADSET_JACK, .name = "headset-jack" },
	{ .id = STAC_HP_ENVY_BASS, .name = "hp-envy-bass" },
	{}
};

static const struct snd_pci_quirk stac92hd83xxx_fixup_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668,
		      "DFI LanParty", STAC_92HD83XXX_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DFI, 0x3101,
		      "DFI LanParty", STAC_92HD83XXX_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x02ba,
		      "unknown Dell", STAC_DELL_S14),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0532,
		      "Dell Latitude E6230", STAC_92HD83XXX_HEADSET_JACK),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0533,
		      "Dell Latitude E6330", STAC_92HD83XXX_HEADSET_JACK),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0534,
		      "Dell Latitude E6430", STAC_92HD83XXX_HEADSET_JACK),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0535,
		      "Dell Latitude E6530", STAC_92HD83XXX_HEADSET_JACK),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x053c,
		      "Dell Latitude E5430", STAC_92HD83XXX_HEADSET_JACK),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x053d,
		      "Dell Latitude E5530", STAC_92HD83XXX_HEADSET_JACK),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0549,
		      "Dell Latitude E5430", STAC_92HD83XXX_HEADSET_JACK),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x057d,
		      "Dell Latitude E6430s", STAC_92HD83XXX_HEADSET_JACK),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x0584,
		      "Dell Latitude E6430U", STAC_92HD83XXX_HEADSET_JACK),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL, 0x1028,
		      "Dell Vostro 3500", STAC_DELL_VOSTRO_3500),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x1656,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x1657,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x1658,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x1659,
			  "HP Pavilion dv7", STAC_HP_DV7_4000),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x165A,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x165B,
			  "HP", STAC_92HD83XXX_HP_cNB11_INTQUAD),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x1888,
			  "HP Envy Spectre", STAC_HP_ENVY_BASS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x18df,
			  "HP Folio", STAC_92HD83XXX_HP_MIC_LED),
	SND_PCI_QUIRK_MASK(PCI_VENDOR_ID_HP, 0xff00, 0x1900,
			  "HP", STAC_92HD83XXX_HP_MIC_LED),
	SND_PCI_QUIRK_MASK(PCI_VENDOR_ID_HP, 0xff00, 0x2000,
			  "HP", STAC_92HD83XXX_HP_MIC_LED),
	SND_PCI_QUIRK_MASK(PCI_VENDOR_ID_HP, 0xff00, 0x2100,
			  "HP", STAC_92HD83XXX_HP_MIC_LED),
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
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x144E,
			  "HP Pavilion dv5", STAC_92HD83XXX_HP_INV_LED),
	SND_PCI_QUIRK(PCI_VENDOR_ID_HP, 0x148a,
		      "HP Mini", STAC_92HD83XXX_HP_LED),
	SND_PCI_QUIRK_VENDOR(PCI_VENDOR_ID_HP, "HP", STAC_92HD83XXX_HP),
	{} /* terminator */
};

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

	if (!snd_hda_gen_add_kctl(&spec->gen, "Bass Speaker Playback Switch",
				  &stac_hp_bass_sw_ctrl))
		return -ENOMEM;

	spec->gpio_mask |= 0x20;
	spec->gpio_dir |= 0x20;
	spec->gpio_data |= 0x20;
	return 0;
}

static const struct hda_pintbl ref92hd71bxx_pin_configs[] = {
	{ 0x0a, 0x02214030 },
	{ 0x0b, 0x02a19040 },
	{ 0x0c, 0x01a19020 },
	{ 0x0d, 0x01014010 },
	{ 0x0e, 0x0181302e },
	{ 0x0f, 0x01014010 },
	{ 0x14, 0x01019020 },
	{ 0x18, 0x90a000f0 },
	{ 0x19, 0x90a000f0 },
	{ 0x1e, 0x01452050 },
	{ 0x1f, 0x01452050 },
	{}
};

static const struct hda_pintbl dell_m4_1_pin_configs[] = {
	{ 0x0a, 0x0421101f },
	{ 0x0b, 0x04a11221 },
	{ 0x0c, 0x40f000f0 },
	{ 0x0d, 0x90170110 },
	{ 0x0e, 0x23a1902e },
	{ 0x0f, 0x23014250 },
	{ 0x14, 0x40f000f0 },
	{ 0x18, 0x90a000f0 },
	{ 0x19, 0x40f000f0 },
	{ 0x1e, 0x4f0000f0 },
	{ 0x1f, 0x4f0000f0 },
	{}
};

static const struct hda_pintbl dell_m4_2_pin_configs[] = {
	{ 0x0a, 0x0421101f },
	{ 0x0b, 0x04a11221 },
	{ 0x0c, 0x90a70330 },
	{ 0x0d, 0x90170110 },
	{ 0x0e, 0x23a1902e },
	{ 0x0f, 0x23014250 },
	{ 0x14, 0x40f000f0 },
	{ 0x18, 0x40f000f0 },
	{ 0x19, 0x40f000f0 },
	{ 0x1e, 0x044413b0 },
	{ 0x1f, 0x044413b0 },
	{}
};

static const struct hda_pintbl dell_m4_3_pin_configs[] = {
	{ 0x0a, 0x0421101f },
	{ 0x0b, 0x04a11221 },
	{ 0x0c, 0x90a70330 },
	{ 0x0d, 0x90170110 },
	{ 0x0e, 0x40f000f0 },
	{ 0x0f, 0x40f000f0 },
	{ 0x14, 0x40f000f0 },
	{ 0x18, 0x90a000f0 },
	{ 0x19, 0x40f000f0 },
	{ 0x1e, 0x044413b0 },
	{ 0x1f, 0x044413b0 },
	{}
};

static void stac92hd71bxx_fixup_ref(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	snd_hda_apply_pincfgs(codec, ref92hd71bxx_pin_configs);
	spec->gpio_mask = spec->gpio_dir = spec->gpio_data = 0;
}

static void stac92hd71bxx_fixup_hp_m4(struct hda_codec *codec,
				      const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;
	struct hda_jack_tbl *jack;

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	/* Enable VREF power saving on GPIO1 detect */
	snd_hda_codec_write_cache(codec, codec->afg, 0,
				  AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK, 0x02);
	snd_hda_jack_detect_enable_callback(codec, codec->afg,
					    STAC_VREF_EVENT,
					    stac_vref_event);
	jack = snd_hda_jack_tbl_get(codec, codec->afg);
	if (jack)
		jack->private_data = 0x02;

	spec->gpio_mask |= 0x02;

	/* enable internal microphone */
	snd_hda_codec_set_pincfg(codec, 0x0e, 0x01813040);
}

static void stac92hd71bxx_fixup_hp_dv4(struct hda_codec *codec,
				       const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;
	spec->gpio_led = 0x01;
}

static void stac92hd71bxx_fixup_hp_dv5(struct hda_codec *codec,
				       const struct hda_fixup *fix, int action)
{
	unsigned int cap;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_codec_set_pincfg(codec, 0x0d, 0x90170010);
		break;

	case HDA_FIXUP_ACT_PROBE:
		/* enable bass on HP dv7 */
		cap = snd_hda_param_read(codec, 0x1, AC_PAR_GPIO_CAP);
		cap &= AC_GPIO_IO_COUNT;
		if (cap >= 6)
			stac_add_hp_bass_switch(codec);
		break;
	}
}

static void stac92hd71bxx_fixup_hp_hdx(struct hda_codec *codec,
				       const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;
	spec->gpio_led = 0x08;
}


static void stac92hd71bxx_fixup_hp(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	if (hp_blike_system(codec->subsystem_id)) {
		unsigned int pin_cfg = snd_hda_codec_get_pincfg(codec, 0x0f);
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

}

static const struct hda_fixup stac92hd71bxx_fixups[] = {
	[STAC_92HD71BXX_REF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd71bxx_fixup_ref,
	},
	[STAC_DELL_M4_1] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell_m4_1_pin_configs,
	},
	[STAC_DELL_M4_2] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell_m4_2_pin_configs,
	},
	[STAC_DELL_M4_3] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell_m4_3_pin_configs,
	},
	[STAC_HP_M4] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd71bxx_fixup_hp_m4,
		.chained = true,
		.chain_id = STAC_92HD71BXX_HP,
	},
	[STAC_HP_DV4] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd71bxx_fixup_hp_dv4,
		.chained = true,
		.chain_id = STAC_HP_DV5,
	},
	[STAC_HP_DV5] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd71bxx_fixup_hp_dv5,
		.chained = true,
		.chain_id = STAC_92HD71BXX_HP,
	},
	[STAC_HP_HDX] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd71bxx_fixup_hp_hdx,
		.chained = true,
		.chain_id = STAC_92HD71BXX_HP,
	},
	[STAC_92HD71BXX_HP] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac92hd71bxx_fixup_hp,
	},
};

static const struct hda_model_fixup stac92hd71bxx_models[] = {
	{ .id = STAC_92HD71BXX_REF, .name = "ref" },
	{ .id = STAC_DELL_M4_1, .name = "dell-m4-1" },
	{ .id = STAC_DELL_M4_2, .name = "dell-m4-2" },
	{ .id = STAC_DELL_M4_3, .name = "dell-m4-3" },
	{ .id = STAC_HP_M4, .name = "hp-m4" },
	{ .id = STAC_HP_DV4, .name = "hp-dv4" },
	{ .id = STAC_HP_DV5, .name = "hp-dv5" },
	{ .id = STAC_HP_HDX, .name = "hp-hdx" },
	{ .id = STAC_HP_DV4, .name = "hp-dv4-1222nr" },
	{}
};

static const struct snd_pci_quirk stac92hd71bxx_fixup_tbl[] = {
	/* SigmaTel reference board */
	SND_PCI_QUIRK(PCI_VENDOR_ID_INTEL, 0x2668,
		      "DFI LanParty", STAC_92HD71BXX_REF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DFI, 0x3101,
		      "DFI LanParty", STAC_92HD71BXX_REF),
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
	SND_PCI_QUIRK_VENDOR(PCI_VENDOR_ID_HP, "HP", STAC_92HD71BXX_HP),
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

static const struct hda_pintbl ref922x_pin_configs[] = {
	{ 0x0a, 0x01014010 },
	{ 0x0b, 0x01016011 },
	{ 0x0c, 0x01012012 },
	{ 0x0d, 0x0221401f },
	{ 0x0e, 0x01813122 },
	{ 0x0f, 0x01011014 },
	{ 0x10, 0x01441030 },
	{ 0x11, 0x01c41030 },
	{ 0x15, 0x40000100 },
	{ 0x1b, 0x40000100 },
	{}
};

/*
    STAC 922X pin configs for
    102801A7
    102801AB
    102801A9
    102801D1
    102801D2
*/
static const struct hda_pintbl dell_922x_d81_pin_configs[] = {
	{ 0x0a, 0x02214030 },
	{ 0x0b, 0x01a19021 },
	{ 0x0c, 0x01111012 },
	{ 0x0d, 0x01114010 },
	{ 0x0e, 0x02a19020 },
	{ 0x0f, 0x01117011 },
	{ 0x10, 0x400001f0 },
	{ 0x11, 0x400001f1 },
	{ 0x15, 0x01813122 },
	{ 0x1b, 0x400001f2 },
	{}
};

/*
    STAC 922X pin configs for
    102801AC
    102801D0
*/
static const struct hda_pintbl dell_922x_d82_pin_configs[] = {
	{ 0x0a, 0x02214030 },
	{ 0x0b, 0x01a19021 },
	{ 0x0c, 0x01111012 },
	{ 0x0d, 0x01114010 },
	{ 0x0e, 0x02a19020 },
	{ 0x0f, 0x01117011 },
	{ 0x10, 0x01451140 },
	{ 0x11, 0x400001f0 },
	{ 0x15, 0x01813122 },
	{ 0x1b, 0x400001f1 },
	{}
};

/*
    STAC 922X pin configs for
    102801BF
*/
static const struct hda_pintbl dell_922x_m81_pin_configs[] = {
	{ 0x0a, 0x0321101f },
	{ 0x0b, 0x01112024 },
	{ 0x0c, 0x01111222 },
	{ 0x0d, 0x91174220 },
	{ 0x0e, 0x03a11050 },
	{ 0x0f, 0x01116221 },
	{ 0x10, 0x90a70330 },
	{ 0x11, 0x01452340 },
	{ 0x15, 0x40C003f1 },
	{ 0x1b, 0x405003f0 },
	{}
};

/*
    STAC 9221 A1 pin configs for
    102801D7 (Dell XPS M1210)
*/
static const struct hda_pintbl dell_922x_m82_pin_configs[] = {
	{ 0x0a, 0x02211211 },
	{ 0x0b, 0x408103ff },
	{ 0x0c, 0x02a1123e },
	{ 0x0d, 0x90100310 },
	{ 0x0e, 0x408003f1 },
	{ 0x0f, 0x0221121f },
	{ 0x10, 0x03451340 },
	{ 0x11, 0x40c003f2 },
	{ 0x15, 0x508003f3 },
	{ 0x1b, 0x405003f4 },
	{}
};

static const struct hda_pintbl d945gtp3_pin_configs[] = {
	{ 0x0a, 0x0221401f },
	{ 0x0b, 0x01a19022 },
	{ 0x0c, 0x01813021 },
	{ 0x0d, 0x01014010 },
	{ 0x0e, 0x40000100 },
	{ 0x0f, 0x40000100 },
	{ 0x10, 0x40000100 },
	{ 0x11, 0x40000100 },
	{ 0x15, 0x02a19120 },
	{ 0x1b, 0x40000100 },
	{}
};

static const struct hda_pintbl d945gtp5_pin_configs[] = {
	{ 0x0a, 0x0221401f },
	{ 0x0b, 0x01011012 },
	{ 0x0c, 0x01813024 },
	{ 0x0d, 0x01014010 },
	{ 0x0e, 0x01a19021 },
	{ 0x0f, 0x01016011 },
	{ 0x10, 0x01452130 },
	{ 0x11, 0x40000100 },
	{ 0x15, 0x02a19320 },
	{ 0x1b, 0x40000100 },
	{}
};

static const struct hda_pintbl intel_mac_v1_pin_configs[] = {
	{ 0x0a, 0x0121e21f },
	{ 0x0b, 0x400000ff },
	{ 0x0c, 0x9017e110 },
	{ 0x0d, 0x400000fd },
	{ 0x0e, 0x400000fe },
	{ 0x0f, 0x0181e020 },
	{ 0x10, 0x1145e030 },
	{ 0x11, 0x11c5e240 },
	{ 0x15, 0x400000fc },
	{ 0x1b, 0x400000fb },
	{}
};

static const struct hda_pintbl intel_mac_v2_pin_configs[] = {
	{ 0x0a, 0x0121e21f },
	{ 0x0b, 0x90a7012e },
	{ 0x0c, 0x9017e110 },
	{ 0x0d, 0x400000fd },
	{ 0x0e, 0x400000fe },
	{ 0x0f, 0x0181e020 },
	{ 0x10, 0x1145e230 },
	{ 0x11, 0x500000fa },
	{ 0x15, 0x400000fc },
	{ 0x1b, 0x400000fb },
	{}
};

static const struct hda_pintbl intel_mac_v3_pin_configs[] = {
	{ 0x0a, 0x0121e21f },
	{ 0x0b, 0x90a7012e },
	{ 0x0c, 0x9017e110 },
	{ 0x0d, 0x400000fd },
	{ 0x0e, 0x400000fe },
	{ 0x0f, 0x0181e020 },
	{ 0x10, 0x1145e230 },
	{ 0x11, 0x11c5e240 },
	{ 0x15, 0x400000fc },
	{ 0x1b, 0x400000fb },
	{}
};

static const struct hda_pintbl intel_mac_v4_pin_configs[] = {
	{ 0x0a, 0x0321e21f },
	{ 0x0b, 0x03a1e02e },
	{ 0x0c, 0x9017e110 },
	{ 0x0d, 0x9017e11f },
	{ 0x0e, 0x400000fe },
	{ 0x0f, 0x0381e020 },
	{ 0x10, 0x1345e230 },
	{ 0x11, 0x13c5e240 },
	{ 0x15, 0x400000fc },
	{ 0x1b, 0x400000fb },
	{}
};

static const struct hda_pintbl intel_mac_v5_pin_configs[] = {
	{ 0x0a, 0x0321e21f },
	{ 0x0b, 0x03a1e02e },
	{ 0x0c, 0x9017e110 },
	{ 0x0d, 0x9017e11f },
	{ 0x0e, 0x400000fe },
	{ 0x0f, 0x0381e020 },
	{ 0x10, 0x1345e230 },
	{ 0x11, 0x13c5e240 },
	{ 0x15, 0x400000fc },
	{ 0x1b, 0x400000fb },
	{}
};

static const struct hda_pintbl ecs202_pin_configs[] = {
	{ 0x0a, 0x0221401f },
	{ 0x0b, 0x02a19020 },
	{ 0x0c, 0x01a19020 },
	{ 0x0d, 0x01114010 },
	{ 0x0e, 0x408000f0 },
	{ 0x0f, 0x01813022 },
	{ 0x10, 0x074510a0 },
	{ 0x11, 0x40c400f1 },
	{ 0x15, 0x9037012e },
	{ 0x1b, 0x40e000f2 },
	{}
};

/* codec SSIDs for Intel Mac sharing the same PCI SSID 8384:7680 */
static const struct snd_pci_quirk stac922x_intel_mac_fixup_tbl[] = {
	SND_PCI_QUIRK(0x0000, 0x0100, "Mac Mini", STAC_INTEL_MAC_V3),
	SND_PCI_QUIRK(0x106b, 0x0800, "Mac", STAC_INTEL_MAC_V1),
	SND_PCI_QUIRK(0x106b, 0x0600, "Mac", STAC_INTEL_MAC_V2),
	SND_PCI_QUIRK(0x106b, 0x0700, "Mac", STAC_INTEL_MAC_V2),
	SND_PCI_QUIRK(0x106b, 0x0e00, "Mac", STAC_INTEL_MAC_V3),
	SND_PCI_QUIRK(0x106b, 0x0f00, "Mac", STAC_INTEL_MAC_V3),
	SND_PCI_QUIRK(0x106b, 0x1600, "Mac", STAC_INTEL_MAC_V3),
	SND_PCI_QUIRK(0x106b, 0x1700, "Mac", STAC_INTEL_MAC_V3),
	SND_PCI_QUIRK(0x106b, 0x0200, "Mac", STAC_INTEL_MAC_V3),
	SND_PCI_QUIRK(0x106b, 0x1e00, "Mac", STAC_INTEL_MAC_V3),
	SND_PCI_QUIRK(0x106b, 0x1a00, "Mac", STAC_INTEL_MAC_V4),
	SND_PCI_QUIRK(0x106b, 0x0a00, "Mac", STAC_INTEL_MAC_V5),
	SND_PCI_QUIRK(0x106b, 0x2200, "Mac", STAC_INTEL_MAC_V5),
	{}
};

static const struct hda_fixup stac922x_fixups[];

/* remap the fixup from codec SSID and apply it */
static void stac922x_fixup_intel_mac_auto(struct hda_codec *codec,
					  const struct hda_fixup *fix,
					  int action)
{
	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;
	snd_hda_pick_fixup(codec, NULL, stac922x_intel_mac_fixup_tbl,
			   stac922x_fixups);
	if (codec->fixup_id != STAC_INTEL_MAC_AUTO)
		snd_hda_apply_fixup(codec, action);
}

static void stac922x_fixup_intel_mac_gpio(struct hda_codec *codec,
					  const struct hda_fixup *fix,
					  int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		spec->gpio_mask = spec->gpio_dir = 0x03;
		spec->gpio_data = 0x03;
	}
}

static const struct hda_fixup stac922x_fixups[] = {
	[STAC_D945_REF] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = ref922x_pin_configs,
	},
	[STAC_D945GTP3] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = d945gtp3_pin_configs,
	},
	[STAC_D945GTP5] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = d945gtp5_pin_configs,
	},
	[STAC_INTEL_MAC_AUTO] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac922x_fixup_intel_mac_auto,
	},
	[STAC_INTEL_MAC_V1] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = intel_mac_v1_pin_configs,
		.chained = true,
		.chain_id = STAC_922X_INTEL_MAC_GPIO,
	},
	[STAC_INTEL_MAC_V2] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = intel_mac_v2_pin_configs,
		.chained = true,
		.chain_id = STAC_922X_INTEL_MAC_GPIO,
	},
	[STAC_INTEL_MAC_V3] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = intel_mac_v3_pin_configs,
		.chained = true,
		.chain_id = STAC_922X_INTEL_MAC_GPIO,
	},
	[STAC_INTEL_MAC_V4] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = intel_mac_v4_pin_configs,
		.chained = true,
		.chain_id = STAC_922X_INTEL_MAC_GPIO,
	},
	[STAC_INTEL_MAC_V5] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = intel_mac_v5_pin_configs,
		.chained = true,
		.chain_id = STAC_922X_INTEL_MAC_GPIO,
	},
	[STAC_922X_INTEL_MAC_GPIO] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac922x_fixup_intel_mac_gpio,
	},
	[STAC_ECS_202] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = ecs202_pin_configs,
	},
	[STAC_922X_DELL_D81] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell_922x_d81_pin_configs,
	},
	[STAC_922X_DELL_D82] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell_922x_d82_pin_configs,
	},
	[STAC_922X_DELL_M81] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell_922x_m81_pin_configs,
	},
	[STAC_922X_DELL_M82] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell_922x_m82_pin_configs,
	},
};

static const struct hda_model_fixup stac922x_models[] = {
	{ .id = STAC_D945_REF, .name = "ref" },
	{ .id = STAC_D945GTP5, .name = "5stack" },
	{ .id = STAC_D945GTP3, .name = "3stack" },
	{ .id = STAC_INTEL_MAC_V1, .name = "intel-mac-v1" },
	{ .id = STAC_INTEL_MAC_V2, .name = "intel-mac-v2" },
	{ .id = STAC_INTEL_MAC_V3, .name = "intel-mac-v3" },
	{ .id = STAC_INTEL_MAC_V4, .name = "intel-mac-v4" },
	{ .id = STAC_INTEL_MAC_V5, .name = "intel-mac-v5" },
	{ .id = STAC_INTEL_MAC_AUTO, .name = "intel-mac-auto" },
	{ .id = STAC_ECS_202, .name = "ecs202" },
	{ .id = STAC_922X_DELL_D81, .name = "dell-d81" },
	{ .id = STAC_922X_DELL_D82, .name = "dell-d82" },
	{ .id = STAC_922X_DELL_M81, .name = "dell-m81" },
	{ .id = STAC_922X_DELL_M82, .name = "dell-m82" },
	/* for backward compatibility */
	{ .id = STAC_INTEL_MAC_V3, .name = "macmini" },
	{ .id = STAC_INTEL_MAC_V5, .name = "macbook" },
	{ .id = STAC_INTEL_MAC_V3, .name = "macbook-pro-v1" },
	{ .id = STAC_INTEL_MAC_V3, .name = "macbook-pro" },
	{ .id = STAC_INTEL_MAC_V2, .name = "imac-intel" },
	{ .id = STAC_INTEL_MAC_V3, .name = "imac-intel-20" },
	{}
};

static const struct snd_pci_quirk stac922x_fixup_tbl[] = {
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
	SND_PCI_QUIRK(0x8384, 0x7680, "Mac", STAC_INTEL_MAC_AUTO),

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

static const struct hda_pintbl ref927x_pin_configs[] = {
	{ 0x0a, 0x02214020 },
	{ 0x0b, 0x02a19080 },
	{ 0x0c, 0x0181304e },
	{ 0x0d, 0x01014010 },
	{ 0x0e, 0x01a19040 },
	{ 0x0f, 0x01011012 },
	{ 0x10, 0x01016011 },
	{ 0x11, 0x0101201f },
	{ 0x12, 0x183301f0 },
	{ 0x13, 0x18a001f0 },
	{ 0x14, 0x18a001f0 },
	{ 0x21, 0x01442070 },
	{ 0x22, 0x01c42190 },
	{ 0x23, 0x40000100 },
	{}
};

static const struct hda_pintbl d965_3st_pin_configs[] = {
	{ 0x0a, 0x0221401f },
	{ 0x0b, 0x02a19120 },
	{ 0x0c, 0x40000100 },
	{ 0x0d, 0x01014011 },
	{ 0x0e, 0x01a19021 },
	{ 0x0f, 0x01813024 },
	{ 0x10, 0x40000100 },
	{ 0x11, 0x40000100 },
	{ 0x12, 0x40000100 },
	{ 0x13, 0x40000100 },
	{ 0x14, 0x40000100 },
	{ 0x21, 0x40000100 },
	{ 0x22, 0x40000100 },
	{ 0x23, 0x40000100 },
	{}
};

static const struct hda_pintbl d965_5st_pin_configs[] = {
	{ 0x0a, 0x02214020 },
	{ 0x0b, 0x02a19080 },
	{ 0x0c, 0x0181304e },
	{ 0x0d, 0x01014010 },
	{ 0x0e, 0x01a19040 },
	{ 0x0f, 0x01011012 },
	{ 0x10, 0x01016011 },
	{ 0x11, 0x40000100 },
	{ 0x12, 0x40000100 },
	{ 0x13, 0x40000100 },
	{ 0x14, 0x40000100 },
	{ 0x21, 0x01442070 },
	{ 0x22, 0x40000100 },
	{ 0x23, 0x40000100 },
	{}
};

static const struct hda_pintbl d965_5st_no_fp_pin_configs[] = {
	{ 0x0a, 0x40000100 },
	{ 0x0b, 0x40000100 },
	{ 0x0c, 0x0181304e },
	{ 0x0d, 0x01014010 },
	{ 0x0e, 0x01a19040 },
	{ 0x0f, 0x01011012 },
	{ 0x10, 0x01016011 },
	{ 0x11, 0x40000100 },
	{ 0x12, 0x40000100 },
	{ 0x13, 0x40000100 },
	{ 0x14, 0x40000100 },
	{ 0x21, 0x01442070 },
	{ 0x22, 0x40000100 },
	{ 0x23, 0x40000100 },
	{}
};

static const struct hda_pintbl dell_3st_pin_configs[] = {
	{ 0x0a, 0x02211230 },
	{ 0x0b, 0x02a11220 },
	{ 0x0c, 0x01a19040 },
	{ 0x0d, 0x01114210 },
	{ 0x0e, 0x01111212 },
	{ 0x0f, 0x01116211 },
	{ 0x10, 0x01813050 },
	{ 0x11, 0x01112214 },
	{ 0x12, 0x403003fa },
	{ 0x13, 0x90a60040 },
	{ 0x14, 0x90a60040 },
	{ 0x21, 0x404003fb },
	{ 0x22, 0x40c003fc },
	{ 0x23, 0x40000100 },
	{}
};

static void stac927x_fixup_ref_no_jd(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	/* no jack detecion for ref-no-jd model */
	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		codec->no_jack_detect = 1;
}

static void stac927x_fixup_ref(struct hda_codec *codec,
			       const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		snd_hda_apply_pincfgs(codec, ref927x_pin_configs);
		spec->eapd_mask = spec->gpio_mask = 0;
		spec->gpio_dir = spec->gpio_data = 0;
	}
}

static void stac927x_fixup_dell_dmic(struct hda_codec *codec,
				     const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action != HDA_FIXUP_ACT_PRE_PROBE)
		return;

	if (codec->subsystem_id != 0x1028022f) {
		/* GPIO2 High = Enable EAPD */
		spec->eapd_mask = spec->gpio_mask = 0x04;
		spec->gpio_dir = spec->gpio_data = 0x04;
	}

	snd_hda_add_verbs(codec, dell_3st_core_init);
	spec->volknob_init = 1;
}

static void stac927x_fixup_volknob(struct hda_codec *codec,
				   const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		snd_hda_add_verbs(codec, stac927x_volknob_core_init);
		spec->volknob_init = 1;
	}
}

static const struct hda_fixup stac927x_fixups[] = {
	[STAC_D965_REF_NO_JD] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac927x_fixup_ref_no_jd,
		.chained = true,
		.chain_id = STAC_D965_REF,
	},
	[STAC_D965_REF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac927x_fixup_ref,
	},
	[STAC_D965_3ST] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = d965_3st_pin_configs,
		.chained = true,
		.chain_id = STAC_D965_VERBS,
	},
	[STAC_D965_5ST] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = d965_5st_pin_configs,
		.chained = true,
		.chain_id = STAC_D965_VERBS,
	},
	[STAC_D965_VERBS] = {
		.type = HDA_FIXUP_VERBS,
		.v.verbs = d965_core_init,
	},
	[STAC_D965_5ST_NO_FP] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = d965_5st_no_fp_pin_configs,
	},
	[STAC_DELL_3ST] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell_3st_pin_configs,
		.chained = true,
		.chain_id = STAC_927X_DELL_DMIC,
	},
	[STAC_DELL_BIOS] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* correct the front output jack as a hp out */
			{ 0x0f, 0x0221101f },
			/* correct the front input jack as a mic */
			{ 0x0e, 0x02a79130 },
			{}
		},
		.chained = true,
		.chain_id = STAC_927X_DELL_DMIC,
	},
	[STAC_DELL_BIOS_AMIC] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* configure the analog microphone on some laptops */
			{ 0x0c, 0x90a79130 },
			{}
		},
		.chained = true,
		.chain_id = STAC_DELL_BIOS,
	},
	[STAC_DELL_BIOS_SPDIF] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = (const struct hda_pintbl[]) {
			/* correct the device field to SPDIF out */
			{ 0x21, 0x01442070 },
			{}
		},
		.chained = true,
		.chain_id = STAC_DELL_BIOS,
	},
	[STAC_927X_DELL_DMIC] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac927x_fixup_dell_dmic,
	},
	[STAC_927X_VOLKNOB] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac927x_fixup_volknob,
	},
};

static const struct hda_model_fixup stac927x_models[] = {
	{ .id = STAC_D965_REF_NO_JD, .name = "ref-no-jd" },
	{ .id = STAC_D965_REF, .name = "ref" },
	{ .id = STAC_D965_3ST, .name = "3stack" },
	{ .id = STAC_D965_5ST, .name = "5stack" },
	{ .id = STAC_D965_5ST_NO_FP, .name = "5stack-no-fp" },
	{ .id = STAC_DELL_3ST, .name = "dell-3stack" },
	{ .id = STAC_DELL_BIOS, .name = "dell-bios" },
	{ .id = STAC_DELL_BIOS_AMIC, .name = "dell-bios-amic" },
	{ .id = STAC_927X_VOLKNOB, .name = "volknob" },
	{}
};

static const struct snd_pci_quirk stac927x_fixup_tbl[] = {
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
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x022e, "Dell     ", STAC_DELL_BIOS_SPDIF),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x022f, "Dell Inspiron 1525", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x0242, "Dell     ", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x0243, "Dell     ", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x02ff, "Dell     ", STAC_DELL_BIOS),
	SND_PCI_QUIRK(PCI_VENDOR_ID_DELL,  0x0209, "Dell XPS 1330", STAC_DELL_BIOS_SPDIF),
	/* 965 based 5 stack systems */
	SND_PCI_QUIRK_MASK(PCI_VENDOR_ID_INTEL, 0xff00, 0x2300,
			   "Intel D965", STAC_D965_5ST),
	SND_PCI_QUIRK_MASK(PCI_VENDOR_ID_INTEL, 0xff00, 0x2500,
			   "Intel D965", STAC_D965_5ST),
	/* volume-knob fixes */
	SND_PCI_QUIRK_VENDOR(0x10cf, "FSC", STAC_927X_VOLKNOB),
	{} /* terminator */
};

static const struct hda_pintbl ref9205_pin_configs[] = {
	{ 0x0a, 0x40000100 },
	{ 0x0b, 0x40000100 },
	{ 0x0c, 0x01016011 },
	{ 0x0d, 0x01014010 },
	{ 0x0e, 0x01813122 },
	{ 0x0f, 0x01a19021 },
	{ 0x14, 0x01019020 },
	{ 0x16, 0x40000100 },
	{ 0x17, 0x90a000f0 },
	{ 0x18, 0x90a000f0 },
	{ 0x21, 0x01441030 },
	{ 0x22, 0x01c41030 },
	{}
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
static const struct hda_pintbl dell_9205_m42_pin_configs[] = {
	{ 0x0a, 0x0321101F },
	{ 0x0b, 0x03A11020 },
	{ 0x0c, 0x400003FA },
	{ 0x0d, 0x90170310 },
	{ 0x0e, 0x400003FB },
	{ 0x0f, 0x400003FC },
	{ 0x14, 0x400003FD },
	{ 0x16, 0x40F000F9 },
	{ 0x17, 0x90A60330 },
	{ 0x18, 0x400003FF },
	{ 0x21, 0x0144131F },
	{ 0x22, 0x40C003FE },
	{}
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
static const struct hda_pintbl dell_9205_m43_pin_configs[] = {
	{ 0x0a, 0x0321101f },
	{ 0x0b, 0x03a11020 },
	{ 0x0c, 0x90a70330 },
	{ 0x0d, 0x90170310 },
	{ 0x0e, 0x400000fe },
	{ 0x0f, 0x400000ff },
	{ 0x14, 0x400000fd },
	{ 0x16, 0x40f000f9 },
	{ 0x17, 0x400000fa },
	{ 0x18, 0x400000fc },
	{ 0x21, 0x0144131f },
	{ 0x22, 0x40c003f8 },
	/* Enable SPDIF in/out */
	{ 0x1f, 0x01441030 },
	{ 0x20, 0x1c410030 },
	{}
};

static const struct hda_pintbl dell_9205_m44_pin_configs[] = {
	{ 0x0a, 0x0421101f },
	{ 0x0b, 0x04a11020 },
	{ 0x0c, 0x400003fa },
	{ 0x0d, 0x90170310 },
	{ 0x0e, 0x400003fb },
	{ 0x0f, 0x400003fc },
	{ 0x14, 0x400003fd },
	{ 0x16, 0x400003f9 },
	{ 0x17, 0x90a60330 },
	{ 0x18, 0x400003ff },
	{ 0x21, 0x01441340 },
	{ 0x22, 0x40c003fe },
	{}
};

static void stac9205_fixup_ref(struct hda_codec *codec,
			       const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		snd_hda_apply_pincfgs(codec, ref9205_pin_configs);
		/* SPDIF-In enabled */
		spec->eapd_mask = spec->gpio_mask = spec->gpio_dir = 0;
	}
}

static void stac9205_fixup_dell_m43(struct hda_codec *codec,
				    const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;
	struct hda_jack_tbl *jack;

	if (action == HDA_FIXUP_ACT_PRE_PROBE) {
		snd_hda_apply_pincfgs(codec, dell_9205_m43_pin_configs);

		/* Enable unsol response for GPIO4/Dock HP connection */
		snd_hda_codec_write_cache(codec, codec->afg, 0,
			AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK, 0x10);
		snd_hda_jack_detect_enable_callback(codec, codec->afg,
						    STAC_VREF_EVENT,
						    stac_vref_event);
		jack = snd_hda_jack_tbl_get(codec, codec->afg);
		if (jack)
			jack->private_data = 0x01;

		spec->gpio_dir = 0x0b;
		spec->eapd_mask = 0x01;
		spec->gpio_mask = 0x1b;
		spec->gpio_mute = 0x10;
		/* GPIO0 High = EAPD, GPIO1 Low = Headphone Mute,
		 * GPIO3 Low = DRM
		 */
		spec->gpio_data = 0x01;
	}
}

static void stac9205_fixup_eapd(struct hda_codec *codec,
				const struct hda_fixup *fix, int action)
{
	struct sigmatel_spec *spec = codec->spec;

	if (action == HDA_FIXUP_ACT_PRE_PROBE)
		spec->eapd_switch = 0;
}

static const struct hda_fixup stac9205_fixups[] = {
	[STAC_9205_REF] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac9205_fixup_ref,
	},
	[STAC_9205_DELL_M42] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell_9205_m42_pin_configs,
	},
	[STAC_9205_DELL_M43] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac9205_fixup_dell_m43,
	},
	[STAC_9205_DELL_M44] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = dell_9205_m44_pin_configs,
	},
	[STAC_9205_EAPD] = {
		.type = HDA_FIXUP_FUNC,
		.v.func = stac9205_fixup_eapd,
	},
	{}
};

static const struct hda_model_fixup stac9205_models[] = {
	{ .id = STAC_9205_REF, .name = "ref" },
	{ .id = STAC_9205_DELL_M42, .name = "dell-m42" },
	{ .id = STAC_9205_DELL_M43, .name = "dell-m43" },
	{ .id = STAC_9205_DELL_M44, .name = "dell-m44" },
	{ .id = STAC_9205_EAPD, .name = "eapd" },
	{}
};

static const struct snd_pci_quirk stac9205_fixup_tbl[] = {
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

static int stac_parse_auto_config(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int err;
	int flags = 0;

	if (spec->headset_jack)
		flags |= HDA_PINCFG_HEADSET_MIC;

	err = snd_hda_parse_pin_defcfg(codec, &spec->gen.autocfg, NULL, flags);
	if (err < 0)
		return err;

	/* add hooks */
	spec->gen.pcm_playback_hook = stac_playback_pcm_hook;
	spec->gen.pcm_capture_hook = stac_capture_pcm_hook;

	spec->gen.automute_hook = stac_update_outputs;
	spec->gen.hp_automute_hook = stac_hp_automute;
	spec->gen.line_automute_hook = stac_line_automute;
	spec->gen.mic_autoswitch_hook = stac_mic_autoswitch;

	err = snd_hda_gen_parse_auto_config(codec, &spec->gen.autocfg);
	if (err < 0)
		return err;

	/* minimum value is actually mute */
	spec->gen.vmaster_tlv[3] |= TLV_DB_SCALE_MUTE;

	/* setup analog beep controls */
	if (spec->anabeep_nid > 0) {
		err = stac_auto_create_beep_ctls(codec,
						 spec->anabeep_nid);
		if (err < 0)
			return err;
	}

	/* setup digital beep controls and input device */
#ifdef CONFIG_SND_HDA_INPUT_BEEP
	if (spec->gen.beep_nid) {
		hda_nid_t nid = spec->gen.beep_nid;
		unsigned int caps;

		err = stac_auto_create_beep_ctls(codec, nid);
		if (err < 0)
			return err;
		if (codec->beep) {
			/* IDT/STAC codecs have linear beep tone parameter */
			codec->beep->linear_tone = spec->linear_tone_beep;
			/* if no beep switch is available, make its own one */
			caps = query_amp_caps(codec, nid, HDA_OUTPUT);
			if (!(caps & AC_AMPCAP_MUTE)) {
				err = stac_beep_switch_ctl(codec);
				if (err < 0)
					return err;
			}
		}
	}
#endif

	if (spec->gpio_led)
		spec->gen.vmaster_mute.hook = stac_vmaster_hook;

	if (spec->aloopback_ctl &&
	    snd_hda_get_bool_hint(codec, "loopback") == 1) {
		if (!snd_hda_gen_add_kctl(&spec->gen, NULL, spec->aloopback_ctl))
			return -ENOMEM;
	}

	if (spec->have_spdif_mux) {
		err = stac_create_spdif_mux_ctls(codec);
		if (err < 0)
			return err;
	}

	stac_init_power_map(codec);

	return 0;
}


static int stac_init(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;
	int i;

	/* override some hints */
	stac_store_hints(codec);

	/* set up GPIO */
	/* turn on EAPD statically when spec->eapd_switch isn't set.
	 * otherwise, unsol event will turn it on/off dynamically
	 */
	if (!spec->eapd_switch)
		spec->gpio_data |= spec->eapd_mask;
	stac_gpio_set(codec, spec->gpio_mask, spec->gpio_dir, spec->gpio_data);

	snd_hda_gen_init(codec);

	/* sync the power-map */
	if (spec->num_pwrs)
		snd_hda_codec_write(codec, codec->afg, 0,
				    AC_VERB_IDT_SET_POWER_MAP,
				    spec->power_map_bits);

	/* power down inactive ADCs */
	if (spec->powerdown_adcs) {
		for (i = 0; i < spec->gen.num_all_adcs; i++) {
			if (spec->active_adcs & (1 << i))
				continue;
			snd_hda_codec_write(codec, spec->gen.all_adcs[i], 0,
					    AC_VERB_SET_POWER_STATE,
					    AC_PWRST_D3);
		}
	}

	return 0;
}

static void stac_shutup(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	snd_hda_shutup_pins(codec);

	if (spec->eapd_mask)
		stac_gpio_set(codec, spec->gpio_mask,
				spec->gpio_dir, spec->gpio_data &
				~spec->eapd_mask);
}

#define stac_free	snd_hda_gen_free

#ifdef CONFIG_PROC_FS
static void stac92hd_proc_hook(struct snd_info_buffer *buffer,
			       struct hda_codec *codec, hda_nid_t nid)
{
	if (nid == codec->afg)
		snd_iprintf(buffer, "Power-Map: 0x%02x\n", 
			    snd_hda_codec_read(codec, nid, 0,
					       AC_VERB_IDT_GET_POWER_MAP, 0));
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
static int stac_suspend(struct hda_codec *codec)
{
	stac_shutup(codec);
	return 0;
}

static void stac_set_power_state(struct hda_codec *codec, hda_nid_t fg,
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
	snd_hda_codec_set_power_to_all(codec, fg, power_state);
}
#else
#define stac_suspend		NULL
#define stac_set_power_state	NULL
#endif /* CONFIG_PM */

static const struct hda_codec_ops stac_patch_ops = {
	.build_controls = snd_hda_gen_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = stac_init,
	.free = stac_free,
	.unsol_event = snd_hda_jack_unsol_event,
#ifdef CONFIG_PM
	.suspend = stac_suspend,
#endif
	.reboot_notify = stac_shutup,
};

static int alloc_stac_spec(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	snd_hda_gen_spec_init(&spec->gen);
	codec->spec = spec;
	codec->no_trigger_sense = 1; /* seems common with STAC/IDT codecs */
	return 0;
}

static int patch_stac9200(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	err = alloc_stac_spec(codec);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->linear_tone_beep = 1;
	spec->gen.own_eapd_ctl = 1;

	codec->patch_ops = stac_patch_ops;
	codec->power_filter = snd_hda_codec_eapd_power_filter;

	snd_hda_add_verbs(codec, stac9200_eapd_init);

	snd_hda_pick_fixup(codec, stac9200_models, stac9200_fixup_tbl,
			   stac9200_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = stac_parse_auto_config(codec);
	if (err < 0) {
		stac_free(codec);
		return err;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;
}

static int patch_stac925x(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	err = alloc_stac_spec(codec);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->linear_tone_beep = 1;
	spec->gen.own_eapd_ctl = 1;

	codec->patch_ops = stac_patch_ops;

	snd_hda_add_verbs(codec, stac925x_core_init);

	snd_hda_pick_fixup(codec, stac925x_models, stac925x_fixup_tbl,
			   stac925x_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = stac_parse_auto_config(codec);
	if (err < 0) {
		stac_free(codec);
		return err;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;
}

static int patch_stac92hd73xx(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;
	int num_dacs;

	err = alloc_stac_spec(codec);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->linear_tone_beep = 0;
	spec->gen.mixer_nid = 0x1d;
	spec->have_spdif_mux = 1;

	num_dacs = snd_hda_get_num_conns(codec, 0x0a) - 1;
	if (num_dacs < 3 || num_dacs > 5) {
		printk(KERN_WARNING "hda_codec: Could not determine "
		       "number of channels defaulting to DAC count\n");
		num_dacs = 5;
	}

	switch (num_dacs) {
	case 0x3: /* 6 Channel */
		spec->aloopback_ctl = &stac92hd73xx_6ch_loopback;
		break;
	case 0x4: /* 8 Channel */
		spec->aloopback_ctl = &stac92hd73xx_8ch_loopback;
		break;
	case 0x5: /* 10 Channel */
		spec->aloopback_ctl = &stac92hd73xx_10ch_loopback;
		break;
	}

	spec->aloopback_mask = 0x01;
	spec->aloopback_shift = 8;

	spec->gen.beep_nid = 0x1c; /* digital beep */

	/* GPIO0 High = Enable EAPD */
	spec->eapd_mask = spec->gpio_mask = spec->gpio_dir = 0x1;
	spec->gpio_data = 0x01;

	spec->eapd_switch = 1;

	spec->num_pwrs = ARRAY_SIZE(stac92hd73xx_pwr_nids);
	spec->pwr_nids = stac92hd73xx_pwr_nids;

	spec->gen.own_eapd_ctl = 1;
	spec->gen.power_down_unused = 1;

	codec->patch_ops = stac_patch_ops;

	snd_hda_pick_fixup(codec, stac92hd73xx_models, stac92hd73xx_fixup_tbl,
			   stac92hd73xx_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	if (!spec->volknob_init)
		snd_hda_add_verbs(codec, stac92hd73xx_core_init);

	err = stac_parse_auto_config(codec);
	if (err < 0) {
		stac_free(codec);
		return err;
	}

	/* Don't GPIO-mute speakers if there are no internal speakers, because
	 * the GPIO might be necessary for Headphone
	 */
	if (spec->eapd_switch && !has_builtin_speaker(codec))
		spec->eapd_switch = 0;

	codec->proc_widget_hook = stac92hd7x_proc_hook;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;
}

static void stac_setup_gpio(struct hda_codec *codec)
{
	struct sigmatel_spec *spec = codec->spec;

	spec->gpio_mask |= spec->eapd_mask;
	if (spec->gpio_led) {
		if (!spec->vref_mute_led_nid) {
			spec->gpio_mask |= spec->gpio_led;
			spec->gpio_dir |= spec->gpio_led;
			spec->gpio_data |= spec->gpio_led;
		} else {
			codec->patch_ops.set_power_state =
					stac_set_power_state;
		}
	}

	if (spec->mic_mute_led_gpio) {
		spec->gpio_mask |= spec->mic_mute_led_gpio;
		spec->gpio_dir |= spec->mic_mute_led_gpio;
		spec->mic_mute_led_on = true;
		spec->gpio_data |= spec->mic_mute_led_gpio;

		spec->gen.cap_sync_hook = stac_capture_led_hook;
	}
}

static int patch_stac92hd83xxx(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	err = alloc_stac_spec(codec);
	if (err < 0)
		return err;

	codec->epss = 0; /* longer delay needed for D3 */

	spec = codec->spec;
	spec->linear_tone_beep = 0;
	spec->gen.own_eapd_ctl = 1;
	spec->gen.power_down_unused = 1;
	spec->gen.mixer_nid = 0x1b;

	spec->gen.beep_nid = 0x21; /* digital beep */
	spec->pwr_nids = stac92hd83xxx_pwr_nids;
	spec->num_pwrs = ARRAY_SIZE(stac92hd83xxx_pwr_nids);
	spec->default_polarity = -1; /* no default cfg */

	codec->patch_ops = stac_patch_ops;

	snd_hda_add_verbs(codec, stac92hd83xxx_core_init);

	snd_hda_pick_fixup(codec, stac92hd83xxx_models, stac92hd83xxx_fixup_tbl,
			   stac92hd83xxx_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	stac_setup_gpio(codec);

	err = stac_parse_auto_config(codec);
	if (err < 0) {
		stac_free(codec);
		return err;
	}

	codec->proc_widget_hook = stac92hd_proc_hook;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;
}

static const hda_nid_t stac92hd95_pwr_nids[] = {
	0x0a, 0x0b, 0x0c, 0x0d
};

static int patch_stac92hd95(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	err = alloc_stac_spec(codec);
	if (err < 0)
		return err;

	codec->epss = 0; /* longer delay needed for D3 */

	spec = codec->spec;
	spec->linear_tone_beep = 0;
	spec->gen.own_eapd_ctl = 1;
	spec->gen.power_down_unused = 1;

	spec->gen.beep_nid = 0x19; /* digital beep */
	spec->pwr_nids = stac92hd95_pwr_nids;
	spec->num_pwrs = ARRAY_SIZE(stac92hd95_pwr_nids);
	spec->default_polarity = -1; /* no default cfg */

	codec->patch_ops = stac_patch_ops;

	err = stac_parse_auto_config(codec);
	if (err < 0) {
		stac_free(codec);
		return err;
	}

	codec->proc_widget_hook = stac92hd_proc_hook;

	return 0;
}

static int patch_stac92hd71bxx(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	const struct hda_verb *unmute_init = stac92hd71bxx_unmute_core_init;
	int err;

	err = alloc_stac_spec(codec);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->linear_tone_beep = 0;
	spec->gen.own_eapd_ctl = 1;
	spec->gen.power_down_unused = 1;
	spec->gen.mixer_nid = 0x17;
	spec->have_spdif_mux = 1;

	codec->patch_ops = stac_patch_ops;

	/* GPIO0 = EAPD */
	spec->gpio_mask = 0x01;
	spec->gpio_dir = 0x01;
	spec->gpio_data = 0x01;

	switch (codec->vendor_id) {
	case 0x111d76b6: /* 4 Port without Analog Mixer */
	case 0x111d76b7:
		unmute_init++;
		break;
	case 0x111d7608: /* 5 Port with Analog Mixer */
		if ((codec->revision_id & 0xf) == 0 ||
		    (codec->revision_id & 0xf) == 1)
			spec->stream_delay = 40; /* 40 milliseconds */

		/* disable VSW */
		unmute_init++;
		snd_hda_codec_set_pincfg(codec, 0x0f, 0x40f000f0);
		snd_hda_codec_set_pincfg(codec, 0x19, 0x40f000f3);
		break;
	case 0x111d7603: /* 6 Port with Analog Mixer */
		if ((codec->revision_id & 0xf) == 1)
			spec->stream_delay = 40; /* 40 milliseconds */

		break;
	}

	if (get_wcaps_type(get_wcaps(codec, 0x28)) == AC_WID_VOL_KNB)
		snd_hda_add_verbs(codec, stac92hd71bxx_core_init);

	if (get_wcaps(codec, 0xa) & AC_WCAP_IN_AMP)
		snd_hda_sequence_write_cache(codec, unmute_init);

	spec->aloopback_ctl = &stac92hd71bxx_loopback;
	spec->aloopback_mask = 0x50;
	spec->aloopback_shift = 0;

	spec->powerdown_adcs = 1;
	spec->gen.beep_nid = 0x26; /* digital beep */
	spec->num_pwrs = ARRAY_SIZE(stac92hd71bxx_pwr_nids);
	spec->pwr_nids = stac92hd71bxx_pwr_nids;

	snd_hda_pick_fixup(codec, stac92hd71bxx_models, stac92hd71bxx_fixup_tbl,
			   stac92hd71bxx_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	stac_setup_gpio(codec);

	err = stac_parse_auto_config(codec);
	if (err < 0) {
		stac_free(codec);
		return err;
	}

	codec->proc_widget_hook = stac92hd7x_proc_hook;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;
}

static int patch_stac922x(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	err = alloc_stac_spec(codec);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->linear_tone_beep = 1;
	spec->gen.own_eapd_ctl = 1;

	codec->patch_ops = stac_patch_ops;

	snd_hda_add_verbs(codec, stac922x_core_init);

	/* Fix Mux capture level; max to 2 */
	snd_hda_override_amp_caps(codec, 0x12, HDA_OUTPUT,
				  (0 << AC_AMPCAP_OFFSET_SHIFT) |
				  (2 << AC_AMPCAP_NUM_STEPS_SHIFT) |
				  (0x27 << AC_AMPCAP_STEP_SIZE_SHIFT) |
				  (0 << AC_AMPCAP_MUTE_SHIFT));

	snd_hda_pick_fixup(codec, stac922x_models, stac922x_fixup_tbl,
			   stac922x_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = stac_parse_auto_config(codec);
	if (err < 0) {
		stac_free(codec);
		return err;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;
}

static const char * const stac927x_spdif_labels[] = {
	"Digital Playback", "ADAT", "Analog Mux 1",
	"Analog Mux 2", "Analog Mux 3", NULL
};

static int patch_stac927x(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	err = alloc_stac_spec(codec);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->linear_tone_beep = 1;
	spec->gen.own_eapd_ctl = 1;
	spec->have_spdif_mux = 1;
	spec->spdif_labels = stac927x_spdif_labels;

	spec->gen.beep_nid = 0x23; /* digital beep */

	/* GPIO0 High = Enable EAPD */
	spec->eapd_mask = spec->gpio_mask = 0x01;
	spec->gpio_dir = spec->gpio_data = 0x01;

	spec->aloopback_ctl = &stac927x_loopback;
	spec->aloopback_mask = 0x40;
	spec->aloopback_shift = 0;
	spec->eapd_switch = 1;

	codec->patch_ops = stac_patch_ops;

	snd_hda_pick_fixup(codec, stac927x_models, stac927x_fixup_tbl,
			   stac927x_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	if (!spec->volknob_init)
		snd_hda_add_verbs(codec, stac927x_core_init);

	err = stac_parse_auto_config(codec);
	if (err < 0) {
		stac_free(codec);
		return err;
	}

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

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

	return 0;
}

static int patch_stac9205(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	err = alloc_stac_spec(codec);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->linear_tone_beep = 1;
	spec->gen.own_eapd_ctl = 1;
	spec->have_spdif_mux = 1;

	spec->gen.beep_nid = 0x23; /* digital beep */

	snd_hda_add_verbs(codec, stac9205_core_init);
	spec->aloopback_ctl = &stac9205_loopback;

	spec->aloopback_mask = 0x40;
	spec->aloopback_shift = 0;
	
	/* GPIO0 High = EAPD */
	spec->eapd_mask = spec->gpio_mask = spec->gpio_dir = 0x1;
	spec->gpio_data = 0x01;

	/* Turn on/off EAPD per HP plugging */
	spec->eapd_switch = 1;

	codec->patch_ops = stac_patch_ops;

	snd_hda_pick_fixup(codec, stac9205_models, stac9205_fixup_tbl,
			   stac9205_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = stac_parse_auto_config(codec);
	if (err < 0) {
		stac_free(codec);
		return err;
	}

	codec->proc_widget_hook = stac9205_proc_hook;

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

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

static const struct hda_pintbl stac9872_vaio_pin_configs[] = {
	{ 0x0a, 0x03211020 },
	{ 0x0b, 0x411111f0 },
	{ 0x0c, 0x411111f0 },
	{ 0x0d, 0x03a15030 },
	{ 0x0e, 0x411111f0 },
	{ 0x0f, 0x90170110 },
	{ 0x11, 0x411111f0 },
	{ 0x13, 0x411111f0 },
	{ 0x14, 0x90a7013e },
	{}
};

static const struct hda_model_fixup stac9872_models[] = {
	{ .id = STAC_9872_VAIO, .name = "vaio" },
	{}
};

static const struct hda_fixup stac9872_fixups[] = {
	[STAC_9872_VAIO] = {
		.type = HDA_FIXUP_PINS,
		.v.pins = stac9872_vaio_pin_configs,
	},
};

static const struct snd_pci_quirk stac9872_fixup_tbl[] = {
	SND_PCI_QUIRK_MASK(0x104d, 0xfff0, 0x81e0,
			   "Sony VAIO F/S", STAC_9872_VAIO),
	{} /* terminator */
};

static int patch_stac9872(struct hda_codec *codec)
{
	struct sigmatel_spec *spec;
	int err;

	err = alloc_stac_spec(codec);
	if (err < 0)
		return err;

	spec = codec->spec;
	spec->linear_tone_beep = 1;
	spec->gen.own_eapd_ctl = 1;

	codec->patch_ops = stac_patch_ops;

	snd_hda_add_verbs(codec, stac9872_core_init);

	snd_hda_pick_fixup(codec, stac9872_models, stac9872_fixup_tbl,
			   stac9872_fixups);
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = stac_parse_auto_config(codec);
	if (err < 0) {
		stac_free(codec);
		return -EINVAL;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);

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
	{ .id = 0x111d7695, .name = "92HD95", .patch = patch_stac92hd95 },
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
